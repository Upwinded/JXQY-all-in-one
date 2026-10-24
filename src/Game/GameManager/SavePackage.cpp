#include "SavePackage.h"

#include "../Data/SaveVersionCompatibility.h"
#include "../../File/StrictRelativeResourcePath.h"
#include "../../Resource/ResourceIniReader.h"

#if defined(__ANDROID__)
#include "../../Platform/AndroidExternalStorage.h"
#endif

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>

extern "C"
{
#include "miniz.h"
}

namespace
{
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

std::size_t readStream(void* context, mz_uint64 offset, void* bytes, std::size_t size)
{
	auto* stream = static_cast<SDL_IOStream*>(context);
	if (offset > static_cast<mz_uint64>(std::numeric_limits<Sint64>::max()) ||
		SDL_SeekIO(stream, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0) return 0;
	return SDL_ReadIO(stream, bytes, size);
}

struct Output
{
	SDL_IOStream* stream;
	mz_uint64 position = 0;
};

std::size_t writeStream(void* context, mz_uint64 offset, const void* bytes, std::size_t size)
{
	auto& output = *static_cast<Output*>(context);
	if (offset != output.position &&
		(offset > static_cast<mz_uint64>(std::numeric_limits<Sint64>::max()) ||
			SDL_SeekIO(output.stream, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0)) return 0;
	const auto written = SDL_WriteIO(output.stream, bytes, size);
	output.position = offset + written;
	return written;
}

std::size_t discardBytes(void*, mz_uint64, const void*, std::size_t size)
{
	return size;
}

std::size_t writeFile(void* context, mz_uint64, const void* bytes, std::size_t size)
{
	auto& stream = *static_cast<std::ofstream*>(context);
	stream.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
	return stream ? size : 0;
}

unsigned int littleEndian16(const unsigned char* bytes)
{
	return bytes[0] | (static_cast<unsigned int>(bytes[1]) << 8);
}

bool entryName(mz_zip_archive& zip, const mz_zip_archive_file_stat& info, std::string& name)
{
	// ZIP APPNOTE 4.6.9: ordinary Windows archivers can keep a local-code-page
	// filename alongside its authoritative UTF-8 Unicode Path extra field.
	std::array<unsigned char, 46> header;
	const mz_uint64 offset = zip.m_central_directory_file_ofs + info.m_central_dir_ofs;
	if (mz_zip_read_archive_data(&zip, offset, header.data(), header.size()) != header.size()) return false;
	const auto length = littleEndian16(header.data() + 28);
	if (length == 0) return false;
	std::vector<unsigned char> fields(length + littleEndian16(header.data() + 30));
	if (mz_zip_read_archive_data(&zip, offset + header.size(), fields.data(), fields.size()) != fields.size()) return false;
	name.assign(reinterpret_cast<const char*>(fields.data()), length);
	if ((info.m_bit_flag & (1 << 11)) != 0) return true;
	for (std::size_t index = length; index + 4 <= fields.size();)
	{
		const auto tag = littleEndian16(fields.data() + index);
		const auto size = littleEndian16(fields.data() + index + 2);
		index += 4;
		if (size > fields.size() - index) return false;
		if (tag == 0x7075 && size >= 5 && fields[index] == 1)
		{
			const auto checksum = static_cast<mz_uint32>(fields[index + 1]) |
				(static_cast<mz_uint32>(fields[index + 2]) << 8) |
				(static_cast<mz_uint32>(fields[index + 3]) << 16) |
				(static_cast<mz_uint32>(fields[index + 4]) << 24);
			if (checksum == mz_crc32(0, fields.data(), length))
			{
				name.assign(reinterpret_cast<const char*>(fields.data() + index + 5), size - 5);
				return true;
			}
		}
		index += size;
	}
	return true;
}

bool validIdentity(const SavePackage::GameIdentity& game, bool requireGameId = true)
{
	if (game.name.empty() || game.saveNamespace.empty() || (requireGameId && game.id.empty())) return false;
	for (const auto* field : { &game.id, &game.name, &game.saveNamespace })
	{
		if (field->size() > 1024 ||
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
		if (path == SaveSlots::screenshot(slot) ||
			path == SaveSlots::screenshot(slot, ".bmp")) return slot;
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
	slot.minimumReaderVersion = ini.get("Save", "MinimumReaderVersion", slot.engineVersion);
	return true;
}
}

namespace SavePackage
{
// Keep a compressed snapshot until the import is confirmed. Large payloads and
// non-seekable Android providers never require a complete ZIP in RAM.
struct Package::Source
{
	std::filesystem::path directory;
	Stream stream{ nullptr, SDL_CloseIO };
	~Source()
	{
		stream.reset();
		std::error_code error;
		if (!directory.empty())
		{
			std::filesystem::remove(directory / "package.zip", error);
			std::filesystem::remove(directory, error);
		}
	}
};

std::string slotDirectory(int slot)
{
	return SaveSlots::directoryName(slot);
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

static bool writeArchive(const std::filesystem::path& root, const GameIdentity& game,
	int slot, const std::string& destination, bool& destinationOpened, std::string& error)
{
	error.clear();
	try
	{
		if (!validIdentity(game, false) || slot < AllSlots || slot > AutomaticSlot)
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
			{
				for (const char* extension : { ".png", ".bmp" })
				{
					const std::string name = SaveSlots::screenshot(selected, extension);
					if (isLink(root / "shot") || isLink(root / name))
						return fail(error, u8"存档截图不能是链接");
					if (std::filesystem::is_regular_file(root / name)) files.emplace_back(name, root / name);
				}
			}
		}
		Stream stream(SDL_IOFromFile(destination.c_str(), "wb"), SDL_CloseIO);
		if (!stream) return fail(error, u8"无法打开导出位置：" + std::string(SDL_GetError()));
		destinationOpened = true;
		Output output{ stream.get() };
		Zip zip;
		zip.value.m_pWrite = writeStream;
		zip.value.m_pIO_opaque = &output;
		if (!mz_zip_writer_init(&zip.value, 0)) return fail(error, u8"无法创建存档包");
		const std::string manifest = "[Package]\nFormat=JXQY-SAVE\nVersion=1\nGameId=" + game.id +
			"\nGameName=" + game.name + "\nSaveNamespace=" + game.saveNamespace + "\n";
		if (!mz_zip_writer_add_mem(&zip.value, ManifestName, manifest.data(), manifest.size(), MZ_BEST_SPEED))
			return fail(error, u8"无法写入存档包信息");
		std::set<std::string> names;
		for (const auto& file : files)
		{
			const auto name = ResourcePathSafety::normalizeStrictRelativeResourcePath(file.first);
			if (!name.succeeded() || !names.insert(fold(name.normalizedPath)).second)
				return fail(error, u8"存档含不兼容或重复的文件名");
			Stream input(SDL_IOFromFile(file.second.u8string().c_str(), "rb"), SDL_CloseIO);
			const Sint64 size = input ? SDL_GetIOSize(input.get()) : -1;
			if (size < 0) return fail(error, u8"无法读取存档文件：" + std::string(SDL_GetError()));
			if (!mz_zip_writer_add_read_buf_callback(&zip.value, name.normalizedPath.c_str(),
				readStream, input.get(), static_cast<mz_uint64>(size), nullptr, nullptr, 0,
				MZ_BEST_SPEED, nullptr, 0, nullptr, 0) || SDL_TellIO(input.get()) != size)
				return fail(error, u8"存档读取、压缩或导出写入失败，请检查可用空间");
		}
		if (!mz_zip_writer_finalize_archive(&zip.value))
			return fail(error, u8"存档打包或写入失败，请检查可用空间");
		if (!SDL_CloseIO(stream.release())) return fail(error, u8"存档包未能完整保存");
		return true;
	}
	catch (const std::exception&)
	{
		return fail(error, u8"无法读取或导出存档，请检查文件位置和可用空间");
	}
}

bool write(const std::filesystem::path& root, const GameIdentity& game,
	int slot, const std::string& destination, std::string& error)
{
	bool destinationOpened = false;
	const bool succeeded = writeArchive(root, game, slot, destination, destinationOpened, error);
	if (!succeeded && destinationOpened)
	{
		// writeArchive has closed the stream, including on exceptions or a failed close.
		// Android document URIs must be removed through the document provider.
#if defined(__ANDROID__)
		const bool removed = destination.rfind("content://", 0) == 0
			? AndroidExternalStorage::removeDocument(destination)
			: SDL_RemovePath(destination.c_str());
#else
		const bool removed = SDL_RemovePath(destination.c_str());
#endif
		if (!removed)
		{
			const std::string removalError = SDL_GetError();
			// SDL maps "wb" to Android's truncating "wt" document mode.
			Stream remainder(SDL_IOFromFile(destination.c_str(), "wb"), SDL_CloseIO);
			const bool empty = remainder && SDL_GetIOSize(remainder.get()) == 0;
			const bool closed = remainder && SDL_CloseIO(remainder.release());
			error += empty && closed
				? u8"\n未能删除未完成的存档包，已清空内容，请在导出位置手动删除空文件："
				: u8"\n未能删除或确认清空未完成的存档包，请在导出位置手动删除：";
			error += removalError;
		}
	}
	return succeeded;
}

bool read(const std::string& source, Package& package, std::string& error)
{
	error.clear();
	package = {};
	try
	{
		Stream input(SDL_IOFromFile(source.c_str(), "rb"), SDL_CloseIO);
		if (!input) return fail(error, u8"无法打开文件：" + std::string(SDL_GetError()));
		auto snapshot = std::make_shared<Package::Source>();
#if defined(__ANDROID__)
		const char* cache = SDL_GetAndroidCachePath();
		if (!cache) return fail(error, u8"无法访问导入缓存目录");
		const auto temporaryRoot = std::filesystem::u8path(cache);
#else
		const auto temporaryRoot = std::filesystem::temp_directory_path();
#endif
		static std::atomic<unsigned int> sequence{ 0 };
		for (;;)
		{
			const auto directory = temporaryRoot / ("jxqy-save-import-" +
				std::to_string(SDL_GetTicksNS()) + "-" + std::to_string(sequence++));
			if (std::filesystem::create_directory(directory))
			{
				snapshot->directory = directory;
				break;
			}
		}
		snapshot->stream.reset(SDL_IOFromFile((snapshot->directory / "package.zip").u8string().c_str(), "w+b"));
		if (!snapshot->stream) return fail(error, u8"无法创建导入缓存，请检查可用空间");
		std::array<char, 64 * 1024> buffer;
		for (;;)
		{
			const auto count = SDL_ReadIO(input.get(), buffer.data(), buffer.size());
			if (SDL_WriteIO(snapshot->stream.get(), buffer.data(), count) != count)
				return fail(error, u8"无法缓存存档包，请检查可用空间");
			if (SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_EOF) break;
			if (count == 0 || SDL_GetIOStatus(input.get()) != SDL_IO_STATUS_READY)
				return fail(error, u8"存档包读取未完成：" + std::string(SDL_GetError()));
		}
		if (!SDL_FlushIO(snapshot->stream.get())) return fail(error, u8"存档包缓存写入失败");
		const Sint64 archiveSize = SDL_GetIOSize(snapshot->stream.get());
		Zip zip;
		zip.value.m_pRead = readStream;
		zip.value.m_pIO_opaque = snapshot->stream.get();
		if (archiveSize < 0 || !mz_zip_reader_init(&zip.value, static_cast<mz_uint64>(archiveSize), 0))
			return fail(error, u8"所选文件不是有效的 ZIP 存档包");
		const mz_uint count = mz_zip_reader_get_num_files(&zip.value);
		std::string prefix;
		if (mz_zip_reader_locate_file(&zip.value, ManifestName, nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE) < 0)
		{
			for (mz_uint index = 0; index < count; ++index)
			{
				mz_zip_archive_file_stat info;
				if (!mz_zip_reader_file_stat(&zip.value, index, &info))
					return fail(error, u8"无法读取存档包目录");
				std::string name;
				if (!entryName(zip.value, info, name)) return fail(error, u8"无法读取存档包文件名");
				const auto slash = name.find('/');
				if (slash == std::string::npos || name.substr(slash + 1) != ManifestName) continue;
				if (!prefix.empty()) return fail(error, u8"存档包包含多个游戏目录，请分别导入");
				prefix = name.substr(0, slash + 1);
			}
		}
		Package candidate;
		candidate.source = std::move(snapshot);
		std::set<std::string> names;
		std::map<int, Slot> slots;
		std::set<int> usedSlots;
		bool manifestFound = false;
		for (mz_uint index = 0; index < count; ++index)
		{
			mz_zip_archive_file_stat info;
			if (!mz_zip_reader_file_stat(&zip.value, index, &info) || info.m_is_encrypted ||
				!info.m_is_supported)
				return fail(error, u8"存档包含不支持的文件条目");
			std::string name;
			if (!entryName(zip.value, info, name)) return fail(error, u8"无法读取存档包文件名");
			if (info.m_is_directory && !name.empty()) name.pop_back();
			const auto path = ResourcePathSafety::normalizeStrictRelativeResourcePath(name);
			const unsigned type = (info.m_external_attr >> 16) & 0170000;
			if (!path.succeeded() || (type != 0 && type != 0100000 && type != 0040000))
				return fail(error, u8"存档包含非法路径或链接");
			name = path.normalizedPath;
			if (info.m_is_directory) continue;
			if (name.rfind("__MACOSX/", 0) == 0 || name == ".DS_Store" ||
				(name.size() >= 10 && name.compare(name.size() - 10, 10, "/.DS_Store") == 0))
			{
				if (!mz_zip_reader_extract_to_callback(&zip.value, index, discardBytes, nullptr, 0))
					return fail(error, u8"存档包内容损坏或未传输完整");
				continue;
			}
			if (!prefix.empty())
			{
				if (name.rfind(prefix, 0) != 0) return fail(error, u8"文件不属于所选存档目录");
				name.erase(0, prefix.size());
			}
			if (!names.insert(fold(name)).second) return fail(error, u8"存档包含重复文件");
			const int slot = entrySlot(name);
			if (name != ManifestName && slot == 0) return fail(error, u8"文件不属于存档档位");
			const bool metadata = name == ManifestName || (slot != 0 && name == slotDirectory(slot) + "/game.ini");
			std::vector<char> bytes;
			if (metadata)
			{
				if (info.m_uncomp_size > bytes.max_size()) return fail(error, u8"存档信息超出系统可处理范围");
				bytes.resize(static_cast<std::size_t>(info.m_uncomp_size));
				if (!mz_zip_reader_extract_to_mem(&zip.value, index, bytes.data(), bytes.size(), 0))
					return fail(error, u8"存档包内容损坏或未传输完整");
			}
			else if (!mz_zip_reader_extract_to_callback(&zip.value, index, discardBytes, nullptr, 0))
				return fail(error, u8"存档包内容损坏或未传输完整");
			if (name == ManifestName)
			{
				ResourceIniReader ini(bytes.data(), bytes.size());
				if (ini.parseError() != 0 || ini.get("Package", "Format", "") != "JXQY-SAVE" ||
					ini.get("Package", "Version", "") != "1") return fail(error, u8"不支持此存档包格式");
				candidate.identity.id = ini.get("Package", "GameId", "");
				candidate.identity.name = ini.get("Package", "GameName", "");
				candidate.identity.saveNamespace = ini.get("Package", "SaveNamespace", "");
				manifestFound = validIdentity(candidate.identity, false);
				continue;
			}
			usedSlots.insert(slot);
			if (name == slotDirectory(slot) + "/game.ini")
			{
				if (!slotMetadata(slot, bytes, slots[slot], error)) return false;
			}
			candidate.entries.push_back({ name, index });
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
		(!package.game().id.empty() && fold(package.game().id) != fold(game.id)) ||
		fold(package.game().saveNamespace) != fold(game.saveNamespace))
		return fail(error, u8"存档包与所选游戏不匹配，请安装对应游戏或 MOD");
	for (const Slot& slot : package.slots())
	{
		if (!SaveVersionCompatibility::validateEngineVersion(slot.engineVersion, slot.minimumReaderVersion, false, error) ||
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
		if (!package.source) return fail(error, u8"存档包尚未读取");
		Zip zip;
		zip.value.m_pRead = readStream;
		zip.value.m_pIO_opaque = package.source->stream.get();
		const Sint64 archiveSize = SDL_GetIOSize(package.source->stream.get());
		if (archiveSize < 0 || !mz_zip_reader_init(&zip.value, static_cast<mz_uint64>(archiveSize), 0))
			return fail(error, u8"存档包缓存不可读取，请重新选择文件");
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
			{
				for (const char* extension : { ".png", ".bmp" })
					std::filesystem::remove(root / (SaveSlots::screenshot(target, extension)));
			}
		}
		for (const auto& entry : package.entries)
		{
			const int sourceSlot = entrySlot(entry.path);
			const int target = targetSlot == AllSlots ? sourceSlot : targetSlot;
			std::string path;
			if (entry.path.rfind("shot/", 0) == 0)
			{
				path = SaveSlots::screenshot(target, entry.path.substr(entry.path.size() - 4).c_str());
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
			const bool extracted = stream && mz_zip_reader_extract_to_callback(
				&zip.value, entry.archiveIndex, writeFile, &stream, 0);
			stream.close();
			if (!extracted || !stream) return fail(error, u8"存档导入未完成，请检查可用空间后重试");
		}
		return true;
	}
	catch (const std::exception&)
	{
		return fail(error, u8"存档导入未完成，请检查目录和可用空间后重试");
	}
}
}
