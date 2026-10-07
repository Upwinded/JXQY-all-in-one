#include "SavePackage.h"
#include "RuntimeSaveGenerationPolicy.h"

#include "../Data/SaveVersionCompatibility.h"
#include "../../File/StrictRelativeResourcePath.h"
#include "../../Resource/ResourceIniReader.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <memory>
#include <set>

extern "C"
{
#include "miniz.h"
}

namespace
{
constexpr std::size_t MaximumArchiveBytes = 256 * 1024 * 1024;
constexpr std::size_t MaximumExpandedBytes = 128 * 1024 * 1024;
constexpr std::size_t MaximumFileBytes = 32 * 1024 * 1024;
constexpr std::size_t MaximumFiles = 32768;
constexpr const char* ManifestName = "save_package.ini";

using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

struct Zip
{
	mz_zip_archive value{};
	~Zip()
	{
		if (value.m_zip_mode == MZ_ZIP_MODE_READING) mz_zip_reader_end(&value);
		else if (value.m_zip_mode != MZ_ZIP_MODE_INVALID) mz_zip_writer_end(&value);
	}
};

std::string fold(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
	}
	return value;
}

bool fail(std::string& error, const std::string& message)
{
	error = message;
	return false;
}

bool isLink(const std::filesystem::path& path)
{
	if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) return true;
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
	return false;
#endif
}

bool plainTree(const std::filesystem::path& root)
{
	if (isLink(root)) return false;
	if (!std::filesystem::exists(root)) return true;
	if (!std::filesystem::is_directory(root)) return false;
	for (const auto& item : std::filesystem::recursive_directory_iterator(root))
	{
		if (isLink(item.path()) ||
			(!item.is_directory() && !item.is_regular_file())) return false;
	}
	return true;
}

bool readBytes(const std::string& path, std::size_t limit,
	std::vector<char>& bytes, std::string& error)
{
	Stream input(SDL_IOFromFile(path.c_str(), "rb"), SDL_CloseIO);
	if (!input) return fail(error, u8"无法打开文件：" + std::string(SDL_GetError()));
	bytes.clear();
	std::array<char, 64 * 1024> buffer;
	for (;;)
	{
		const std::size_t count = SDL_ReadIO(input.get(), buffer.data(), buffer.size());
		if (count > limit - bytes.size()) return fail(error, u8"存档包或文件超过容量限制");
		bytes.insert(bytes.end(), buffer.data(), buffer.data() + count);
		if (count < buffer.size())
		{
			if (SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_EOF) return true;
			if (count == 0 || SDL_GetIOStatus(input.get()) != SDL_IO_STATUS_READY)
				return fail(error, u8"文件读取失败：" + std::string(SDL_GetError()));
		}
	}
}

bool validIdentity(const SavePackage::GameIdentity& game)
{
	for (const auto* field : { &game.id, &game.name, &game.saveNamespace })
	{
		if (field->empty() || field->size() > 1024 ||
			!ResourcePathSafety::isValidUtf8(*field) ||
			std::any_of(field->begin(), field->end(), [](unsigned char character)
			{
				return character < 32;
			}))
			return false;
	}
	const auto path = ResourcePathSafety::normalizeStrictRelativeResourcePath(game.saveNamespace);
	return path.succeeded() && path.normalizedPath.find('/') == std::string::npos;
}

// A package has a closed set of roots. In particular, game/, config.ini and
// arbitrary namespace directories cannot enter an import through ZIP names.
int entrySlot(const std::string& path)
{
	for (int slot = 1; slot <= SavePackage::AutomaticSlot; ++slot)
	{
		if (path.rfind(SavePackage::slotDirectory(slot) + "/", 0) == 0) return slot;
		if (slot != SavePackage::AutomaticSlot &&
			(path == "shot/rpg" + std::to_string(slot) + ".png" ||
			 path == "shot/rpg" + std::to_string(slot) + ".bmp")) return slot;
	}
	return 0;
}

bool slotMetadata(int index, const std::vector<char>& bytes,
	SavePackage::Slot& slot, std::string& error)
{
	const ResourceIniReader ini(bytes.data(), bytes.size());
	if (ini.parseError() != 0) return fail(error, u8"存档 game.ini 格式错误");
	slot = { index, ini.get("Save", "EngineVersion", "1.0.0"),
		ini.get("Save", "ResourceVersion", "1.0.0") };
	return true;
}
}

namespace SavePackage
{
std::string slotDirectory(int slot)
{
	if (slot == AutomaticSlot) return "rpg_auto";
	return slot >= 1 && slot <= 7 ? "rpg" + std::to_string(slot) : "";
}

std::string slotLabel(int slot)
{
	if (slot == AllSlots) return u8"全部档位";
	if (slot == AutomaticSlot) return u8"自动存档";
	return u8"手动存档 " + std::to_string(slot);
}

std::vector<int> listSlots(const std::filesystem::path& root)
{
	std::vector<int> slots;
	for (int slot = 1; slot <= AutomaticSlot; ++slot)
	{
		std::error_code error;
		if (std::filesystem::is_regular_file(root / slotDirectory(slot) / "game.ini", error))
			slots.push_back(slot);
	}
	return slots;
}

bool write(const std::filesystem::path& root, const GameIdentity& game,
	int slot, const std::string& destination, std::string& error)
{
	error.clear();
	try
	{
		if (!validIdentity(game) || slot < AllSlots || slot > AutomaticSlot)
			return fail(error, u8"游戏或存档选择无效");
		const std::vector<int> slots = slot == AllSlots ? listSlots(root) : std::vector<int>{ slot };
		if (slots.empty()) return fail(error, u8"没有可导出的存档");
		// Reject desktop destinations inside the save directory before opening
		// for writing. Android content URIs are granted by the system picker.
		if (destination.rfind("content://", 0) != 0)
		{
			const auto relative = std::filesystem::weakly_canonical(std::filesystem::u8path(destination))
				.lexically_relative(std::filesystem::weakly_canonical(root.parent_path()));
			if (!relative.empty() && *relative.begin() != "..")
				return fail(error, u8"请将导出文件保存到游戏存档目录之外");
		}
		if (isLink(root)) return fail(error, u8"存档目录不能是链接");
		std::vector<std::pair<std::string, std::filesystem::path>> files;
		for (int selected : slots)
		{
			const std::string directory = slotDirectory(selected);
			const auto source = root / directory;
			if (!std::filesystem::is_regular_file(source / "game.ini") || !plainTree(source))
				return fail(error, u8"存档目录不可读取或缺少 game.ini");
			for (const auto& item : std::filesystem::recursive_directory_iterator(source))
			{
				if (item.is_regular_file())
				{
					const std::string relative = item.path().lexically_relative(source).generic_u8string();
					if (fold(relative) != "list.ini") files.emplace_back(directory + "/" + relative, item.path());
				}
			}
			if (selected != AutomaticSlot)
			{
				for (const char* extension : { ".png", ".bmp" })
				{
					const std::string name = "shot/rpg" + std::to_string(selected) + extension;
					if (isLink(root / "shot") || isLink(root / name))
						return fail(error, u8"存档截图不能是链接");
					if (std::filesystem::is_regular_file(root / name)) files.emplace_back(name, root / name);
				}
			}
		}
		if (files.size() > MaximumFiles) return fail(error, u8"存档文件数量超过限制");
		Zip zip;
		if (!mz_zip_writer_init_heap(&zip.value, 0, 0)) return fail(error, u8"无法创建存档包");
		const std::string manifest = "[Package]\nFormat=JXQY-SAVE\nVersion=1\nGameId=" + game.id +
			"\nGameName=" + game.name + "\nSaveNamespace=" + game.saveNamespace + "\n";
		if (!mz_zip_writer_add_mem(&zip.value, ManifestName, manifest.data(), manifest.size(), MZ_BEST_SPEED))
			return fail(error, u8"无法写入存档包信息");
		std::set<std::string> names;
		std::size_t total = manifest.size();
		std::vector<char> bytes;
		for (const auto& file : files)
		{
			const auto name = ResourcePathSafety::normalizeStrictRelativeResourcePath(file.first);
			if (!name.succeeded() || !names.insert(fold(name.normalizedPath)).second)
				return fail(error, u8"存档含不兼容或重复的文件名");
			if (!readBytes(file.second.u8string(), MaximumFileBytes, bytes, error)) return false;
			if (bytes.size() > MaximumExpandedBytes - total) return fail(error, u8"存档内容超过 128 MiB");
			total += bytes.size();
			if (!mz_zip_writer_add_mem(&zip.value, name.normalizedPath.c_str(),
				bytes.data(), bytes.size(), MZ_BEST_SPEED)) return fail(error, u8"存档压缩失败");
		}
		void* output = nullptr;
		std::size_t size = 0;
		if (!mz_zip_writer_finalize_heap_archive(&zip.value, &output, &size))
			return fail(error, u8"存档打包失败");
		std::unique_ptr<void, decltype(&mz_free)> buffer(output, mz_free);
		Stream stream(SDL_IOFromFile(destination.c_str(), "wb"), SDL_CloseIO);
		if (!stream || SDL_WriteIO(stream.get(), buffer.get(), size) != size)
			return fail(error, u8"存档包写入失败：" + std::string(SDL_GetError()));
		if (!SDL_CloseIO(stream.release())) return fail(error, u8"存档包未能完整保存");
		return true;
	}
	catch (const std::exception&)
	{
		return fail(error, u8"无法读取或导出存档，请检查文件位置和可用空间");
	}
}

bool read(const std::string& source, Package& package, std::string& error)
{
	error.clear();
	package = {};
	try
	{
		std::vector<char> bytes;
		if (!readBytes(source, MaximumArchiveBytes, bytes, error)) return false;
		Zip zip;
		if (!mz_zip_reader_init_mem(&zip.value, bytes.data(), bytes.size(), 0))
			return fail(error, u8"所选文件不是有效的 ZIP 存档包");
		const mz_uint count = mz_zip_reader_get_num_files(&zip.value);
		if (count > MaximumFiles + 32) return fail(error, u8"存档包文件数量超过限制");
		Package candidate;
		std::set<std::string> names;
		std::map<int, Slot> slots;
		std::set<int> usedSlots;
		const auto limits = createRuntimeSaveGenerationPolicy().limits;
		std::array<std::size_t, AutomaticSlot + 1> slotFileCounts{};
		std::array<std::uint64_t, AutomaticSlot + 1> slotBytes{};
		std::size_t total = 0;
		bool manifestFound = false;
		for (mz_uint index = 0; index < count; ++index)
		{
			mz_zip_archive_file_stat info;
			if (!mz_zip_reader_file_stat(&zip.value, index, &info) || info.m_is_encrypted ||
				!info.m_is_supported || mz_zip_reader_get_filename(&zip.value, index, nullptr, 0) > sizeof(info.m_filename))
				return fail(error, u8"存档包含不支持的文件条目");
			std::string name = info.m_filename;
			if (info.m_is_directory && !name.empty()) name.pop_back();
			const auto path = ResourcePathSafety::normalizeStrictRelativeResourcePath(name);
			const unsigned type = (info.m_external_attr >> 16) & 0170000;
			if (!path.succeeded() || (type != 0 && type != 0100000 && type != 0040000))
				return fail(error, u8"存档包含非法路径或链接");
			name = path.normalizedPath;
			if (info.m_is_directory) continue;
			if (!names.insert(fold(name)).second) return fail(error, u8"存档包含重复文件");
			const int slot = entrySlot(name);
			if (name != ManifestName && slot == 0) return fail(error, u8"文件不属于存档档位");
			if (slot != 0 && name.rfind("shot/", 0) != 0)
			{
				const std::string relative = name.substr(name.find('/') + 1);
				if (relative.find('/') != std::string::npos)
					return fail(error, u8"不支持的存档结构：档位内不能包含子目录");
				// Count only files that importTo writes into the flat runtime slot.
				if (fold(relative) != "list.ini")
				{
					if (++slotFileCounts[slot] > limits.maximumFileCount)
						return fail(error, slotLabel(slot) + u8"的文件数量超过读档限制");
					if (info.m_uncomp_size > static_cast<std::uint64_t>(limits.maximumSingleFileBytes))
						return fail(error, slotLabel(slot) + u8"的单文件大小超过读档限制");
					if (info.m_uncomp_size > limits.maximumTotalBytes - slotBytes[slot])
						return fail(error, slotLabel(slot) + u8"的总大小超过读档限制");
					slotBytes[slot] += info.m_uncomp_size;
				}
			}
			if (info.m_uncomp_size > MaximumFileBytes || info.m_uncomp_size > MaximumExpandedBytes - total ||
				(name == ManifestName && info.m_uncomp_size > 65536))
				return fail(error, u8"存档包解压内容超过容量限制");
			total += static_cast<std::size_t>(info.m_uncomp_size);
			Package::Entry entry{ name, std::vector<char>(static_cast<std::size_t>(info.m_uncomp_size)) };
			if (!mz_zip_reader_extract_to_mem(&zip.value, index, entry.bytes.data(), entry.bytes.size(), 0))
				return fail(error, u8"存档包内容损坏或未传输完整");
			if (name == ManifestName)
			{
				ResourceIniReader ini(entry.bytes.data(), entry.bytes.size());
				if (ini.parseError() != 0 || ini.get("Package", "Format", "") != "JXQY-SAVE" ||
					ini.get("Package", "Version", "") != "1") return fail(error, u8"不支持此存档包格式");
				candidate.identity.id = ini.get("Package", "GameId", "");
				candidate.identity.name = ini.get("Package", "GameName", "");
				candidate.identity.saveNamespace = ini.get("Package", "SaveNamespace", "");
				manifestFound = validIdentity(candidate.identity);
				continue;
			}
			usedSlots.insert(slot);
			if (name == slotDirectory(slot) + "/game.ini")
			{
				if (!slotMetadata(slot, entry.bytes, slots[slot], error)) return false;
			}
			candidate.entries.push_back(std::move(entry));
		}
		if (!manifestFound || slots.empty() || slots.size() != usedSlots.size())
			return fail(error, u8"存档包缺少游戏信息或档位 game.ini");
		for (const auto& name : names)
		{
			for (auto slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1))
			{
				if (names.count(name.substr(0, slash))) return fail(error, u8"存档包文件与目录重名");
			}
		}
		for (const auto& slot : slots) candidate.savedSlots.push_back(slot.second);
		package = std::move(candidate);
		return true;
	}
	catch (const std::exception&)
	{
		return fail(error, u8"存档包无法读取或内存不足");
	}
}

bool compatible(const Package& package, const GameIdentity& game, std::string& error)
{
	error.clear();
	if (!validIdentity(game) || package.slots().empty() ||
		fold(package.game().id) != fold(game.id) ||
		fold(package.game().saveNamespace) != fold(game.saveNamespace))
		return fail(error, u8"存档包与所选游戏不匹配，请安装对应游戏或 MOD");
	for (const Slot& slot : package.slots())
	{
		if (!SaveVersionCompatibility::validateEngineVersion(slot.engineVersion, false, error) ||
			!SaveVersionCompatibility::validateResourceVersion(slot.resourceVersion,
				game.resourceVersion, game.minimumResourceVersion, false, error)) return false;
	}
	return true;
}

bool importTo(const Package& package, const GameIdentity& game,
	const std::filesystem::path& root, int targetSlot, std::string& error)
{
	if (!compatible(package, game, error)) return false;
	if (targetSlot < AllSlots || targetSlot > AutomaticSlot ||
		(targetSlot != AllSlots && package.slots().size() != 1))
		return fail(error, u8"导入目标档位无效");
	try
	{
		if (isLink(root) || !plainTree(root / "shot")) return fail(error, u8"目标存档目录或截图目录不可用");
		for (const Slot& slot : package.slots())
		{
			const int target = targetSlot == AllSlots ? slot.index : targetSlot;
			if (!plainTree(root / slotDirectory(target))) return fail(error, u8"目标档位目录不可用");
		}
		// The whole selected package has been read and CRC-checked before any
		// overwrite. Applying an explicit import is one direct overwrite, with
		// the same failure semantics as saving; no backup or rollback generations.
		std::filesystem::create_directories(root);
		for (const Slot& slot : package.slots())
		{
			const int target = targetSlot == AllSlots ? slot.index : targetSlot;
			std::filesystem::remove_all(root / slotDirectory(target));
			std::filesystem::create_directories(root / slotDirectory(target));
			if (target != AutomaticSlot)
			{
				for (const char* extension : { ".png", ".bmp" })
					std::filesystem::remove(root / ("shot/rpg" + std::to_string(target) + extension));
			}
		}
		for (const auto& entry : package.entries)
		{
			const int sourceSlot = entrySlot(entry.path);
			const int target = targetSlot == AllSlots ? sourceSlot : targetSlot;
			std::string path;
			if (entry.path.rfind("shot/", 0) == 0)
			{
				if (target == AutomaticSlot) continue;
				path = "shot/rpg" + std::to_string(target) + entry.path.substr(entry.path.size() - 4);
			}
			else
			{
				const std::string relative = entry.path.substr(slotDirectory(sourceSlot).size() + 1);
				if (fold(relative) == "list.ini") continue;
				path = slotDirectory(target) + "/" + relative;
			}
			const auto output = root / std::filesystem::u8path(path);
			std::filesystem::create_directories(output.parent_path());
			std::ofstream stream(output, std::ios::binary | std::ios::trunc);
			stream.write(entry.bytes.data(), static_cast<std::streamsize>(entry.bytes.size()));
			stream.close();
			if (!stream) return fail(error, u8"存档导入未完成，请检查可用空间后重试");
		}
		return true;
	}
	catch (const std::exception&)
	{
		return fail(error, u8"存档导入未完成，请检查目录和可用空间后重试");
	}
}
}
