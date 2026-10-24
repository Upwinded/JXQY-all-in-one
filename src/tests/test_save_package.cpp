#include "../Game/GameManager/SavePackage.h"
#include "../Game/GameManager/RuntimeSaveGenerationPolicy.h"
#include "../JxqyEngineVersion.h"
#include "../Resource/ResourceIniReader.h"
#include "TestTemporaryDirectory.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

extern "C"
{
#include "miniz.h"
}

namespace fs = std::filesystem;

namespace
{
void require(bool value, const std::string& message)
{
	if (!value) throw std::runtime_error(message);
}

void put(const fs::path& path, const std::string& content)
{
	fs::create_directories(path.parent_path());
	std::ofstream stream(path, std::ios::binary);
	stream.write(content.data(), static_cast<std::streamsize>(content.size()));
	require(static_cast<bool>(stream), "fixture write failed");
}

std::string get(const fs::path& path)
{
	std::ifstream stream(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(stream), {} };
}

std::string state(const std::string& resourceVersion = "1.03",
	const std::string& engineVersion = JxqyBuildVersion::EngineVersion)
{
	return "[Save]\nEngineVersion=" + engineVersion + "\nResourceVersion=" + resourceVersion +
		"\n[State]\nMap=" + u8"狂沙镇.map\n";
}

void makeArchive(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& files,
	int compression = 0)
{
	mz_zip_archive zip{};
	require(mz_zip_writer_init_heap(&zip, 0, 0), "init zip");
	for (const auto& entry : files)
	{
		std::string name = entry.first;
		if (!name.empty() && name.front() == '/') name.front() = '_';
		require(mz_zip_writer_add_mem(&zip, name.c_str(), entry.second.data(), entry.second.size(), compression), "zip entry: " + name);
	}
	void* bytes = nullptr;
	std::size_t size = 0;
	require(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &size), "finalize zip");
	std::string archive(static_cast<const char*>(bytes), size);
	for (const auto& entry : files)
	{
		if (entry.first.empty() || entry.first.front() != '/') continue;
		const std::string safeName = "_" + entry.first.substr(1);
		for (auto offset = archive.find(safeName); offset != std::string::npos; offset = archive.find(safeName, offset + 1))
			archive[offset] = '/';
	}
	put(file, archive);
	mz_free(bytes);
	mz_zip_writer_end(&zip);
}
}

int main(int argc, char** argv)
{
	const auto root = makeUniqueTestDirectory("jxqy-save-package");
	try
	{
		const SavePackage::GameIdentity game{ "TEST_MOD", u8"测试模组", "test_mod", "1.03", "1.0.0" };
		const fs::path desktop = root / "desktop/save/test_mod";
		const fs::path mobile = root / "mobile/save/test_mod";
		const fs::path archive = root / fs::u8path(u8"中文存档包.jxqy-save.zip");
		const std::string binary("\0\1\2\xff\0", 5);
		put(desktop / "rpg1/game.ini", state());
		put(desktop / fs::u8path(u8"rpg1/旧地图.npc"), u8"[NPC]\nName=张如梦\n");
		put(desktop / "rpg1/player0.ini", "[Player]\nLife=123\n");
		put(desktop / "rpg1/empty.txt", "");
		put(desktop / "rpg1/list.ini", "obsolete");
		put(desktop / "rpg2/player0.ini", "incomplete-slot");
		put(desktop / "shot/rpg2.png", "incomplete-slot-preview");
		put(desktop / "rpg3/game.ini", state());
		put(desktop / "rpg3/items.dat", binary);
		put(desktop / "rpg8/game.ini", state());
		put(desktop / "rpg8/marker", "manual-eight");
		put(desktop / "rpg9/game.ini", state());
		put(desktop / "rpg9/marker", "manual-nine");
		put(desktop / "rpg_auto/game.ini", state());
		put(desktop / "rpg_auto/marker", "automatic");
		put(desktop / "shot/rpg_auto.png", binary);
		put(desktop / "shot/rpg1.png", binary);
		put(desktop / "shot/rpg3.bmp", binary);
		put(desktop / "game/live.ini", "live");
		put(desktop / "rpg0/game.ini", "template");
		put(desktop / "config.ini", "desktop-settings");
		put(mobile / "rpg2/old.ini", "stale");
		put(mobile / "shot/rpg2.bmp", "old-preview");
		put(mobile / "rpg7/game.ini", "keep-other-slot");
		put(mobile / "game/live.ini", "mobile-live");
		put(mobile / "config.ini", "mobile-settings");
		std::string error;
		SavePackage::Package package;
		require(SavePackage::write(desktop, game, 1, archive.u8string(), error), error);
		require(SavePackage::read(archive.u8string(), package, error), error);
		require(package.slots().size() == 1 && package.game().name == game.name, "single package identity");
		require(SavePackage::importTo(package, game, mobile, 2, error), error);
		require(get(mobile / "rpg2/game.ini") == state(), "slot state remap");
		require(get(mobile / fs::u8path(u8"rpg2/旧地图.npc")) == u8"[NPC]\nName=张如梦\n", "unicode cross-map data");
		require(get(mobile / "shot/rpg2.png") == binary, "thumbnail remap");
		require(!fs::exists(mobile / "rpg2/old.ini") && !fs::exists(mobile / "shot/rpg2.bmp"), "replace selected files");
		require(fs::exists(mobile / "rpg2/empty.txt") && !fs::exists(mobile / "rpg2/list.ini"), "empty file and legacy list");
		require(get(mobile / "rpg7/game.ini") == "keep-other-slot" &&
			get(mobile / "game/live.ini") == "mobile-live" && get(mobile / "config.ini") == "mobile-settings", "unrelated state preserved");
		// Reverse transfer uses precisely the same format and file bytes.
		require(SavePackage::write(mobile, game, 2, archive.u8string(), error), error);
		require(SavePackage::read(archive.u8string(), package, error), error);
		require(SavePackage::importTo(package, game, desktop, 4, error), error);
		require(get(desktop / "rpg4/player0.ini") == get(mobile / "rpg2/player0.ini"), "reverse transfer");
		require(SavePackage::write(desktop, game, SavePackage::AllSlots, archive.u8string(), error), error);
		require(SavePackage::read(archive.u8string(), package, error), error);
		require(package.slots().size() == 6, "all slots includes automatic");
		require(SavePackage::importTo(package, game, mobile, SavePackage::AllSlots, error), error);
		require(get(mobile / "rpg3/items.dat") == binary && get(mobile / "rpg_auto/game.ini") == state(), "all payloads");
		require(SavePackage::AutomaticSlot == 10 && SavePackage::slotDirectory(8) == "rpg8" &&
			get(mobile / "rpg8/marker") == "manual-eight" && get(mobile / "rpg9/marker") == "manual-nine" &&
			get(mobile / "rpg_auto/marker") == "automatic" && get(mobile / "shot/rpg_auto.png") == binary &&
			!fs::exists(mobile / "rpg10"), "eight, nine and automatic remain distinct across transfers");
		require(!fs::exists(mobile / "rpg0") && get(mobile / "rpg7/game.ini") == "keep-other-slot", "only packaged slots imported");
		require(!SavePackage::importTo(package, game, mobile, 5, error), "multiple slots cannot remap to one");
		auto wrongGame = game;
		wrongGame.id = "OTHER_MOD";
		require(!SavePackage::importTo(package, wrongGame, mobile, 0, error), "game mismatch rejected");
		wrongGame = game;
		wrongGame.resourceVersion = "1.0.0";
		require(!SavePackage::importTo(package, wrongGame, mobile, 0, error), "newer resource rejected");
		wrongGame = game;
		wrongGame.minimumResourceVersion = "1.04";
		require(!SavePackage::importTo(package, wrongGame, mobile, 0, error), "invalid version range rejected");
		require(!SavePackage::write(desktop, game, 1, (desktop / "rpg1/game.ini").u8string(), error) &&
			get(desktop / "rpg1/game.ini") == state(), "export cannot overwrite live saves");
		{
			const auto editedArchive = root / "user-edited.zip";
			require(SavePackage::write(desktop, game, 1, editedArchive.u8string(), error), error);
			const auto exported = get(editedArchive);
			mz_zip_archive zip{};
			require(mz_zip_reader_init_mem(&zip, exported.data(), exported.size(), 0), "open exported archive for editing");
			std::vector<std::pair<std::string, std::string>> entries;
			const std::string editedPlayer = "[Player]\nLife=9999\nMoney=7654321\n";
			for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&zip); ++index)
			{
				mz_zip_archive_file_stat entry{};
				require(mz_zip_reader_file_stat(&zip, index, &entry), "read exported entry");
				std::string content(static_cast<std::size_t>(entry.m_uncomp_size), '\0');
				require(mz_zip_reader_extract_to_mem(&zip, index, content.data(), content.size(), 0), "extract exported entry");
				entries.emplace_back(entry.m_filename,
					std::string(entry.m_filename) == "rpg1/player0.ini" ? editedPlayer : content);
			}
			mz_zip_reader_end(&zip);
			const std::string editedVariables = "[Variable]\nevent=123\nCustomQuest=1\n";
			entries.emplace_back("rpg1/variable.ini", editedVariables);
			makeArchive(editedArchive, entries, MZ_BEST_SPEED);
			SavePackage::Package editedPackage;
			require(SavePackage::read(editedArchive.u8string(), editedPackage, error), error);
			const auto editedRoot = root / "edited-import/save/test_mod";
			require(SavePackage::importTo(editedPackage, game, editedRoot, 6, error), error);
			require(get(editedRoot / "rpg6/player0.ini") == editedPlayer &&
				get(editedRoot / "rpg6/variable.ini") == editedVariables,
				"exported saves can be edited, repacked and imported without restoring original gameplay values");
		}
		auto uninstalledGame = game;
		uninstalledGame.id.clear();
		uninstalledGame.name = uninstalledGame.saveNamespace;
		SavePackage::Package uninstalledPackage;
		require(SavePackage::write(desktop, uninstalledGame, 1, archive.u8string(), error) &&
			SavePackage::read(archive.u8string(), uninstalledPackage, error), "export without installed resources: " + error);
		require(uninstalledPackage.game().id.empty() && uninstalledPackage.game().saveNamespace == game.saveNamespace,
			"uninstalled export preserves its namespace without inventing a game ID");
		require(SavePackage::importTo(uninstalledPackage, game, mobile, 6, error) &&
			get(mobile / "rpg6/game.ini") == state(), "namespace-only export imports into its installed game");
		wrongGame = game;
		wrongGame.saveNamespace = "other_mod";
		require(!SavePackage::compatible(uninstalledPackage, wrongGame, error), "namespace-only package rejects another namespace");
		wrongGame = game;
		wrongGame.resourceVersion = "1.0.0";
		require(!SavePackage::compatible(uninstalledPackage, wrongGame, error), "namespace-only package keeps version checks");
		wrongGame = game;
		wrongGame.id.clear();
		require(!SavePackage::compatible(uninstalledPackage, wrongGame, error), "import still requires an installed game identity");
		for (int selected : { 1, 8, 9, SavePackage::AutomaticSlot })
		{
			require(SavePackage::write(desktop, game, selected, archive.u8string(), error) &&
				SavePackage::read(archive.u8string(), package, error), "export selected slot: " + error);
			require(package.slots().size() == 1 && package.slots().front().index == selected,
				"single manual and automatic exports preserve their slot numbers");
		}
		auto caseVariantGame = game;
		caseVariantGame.saveNamespace = "TEST_MOD";
		require(SavePackage::write(desktop, caseVariantGame, 1, archive.u8string(), error) &&
			SavePackage::read(archive.u8string(), package, error) &&
			package.game().saveNamespace == "TEST_MOD" && SavePackage::compatible(package, game, error),
			"source directory spelling and preserved package identity are independent: " + error);
		const auto unavailableDestination = root / "destination-directory";
		fs::create_directory(unavailableDestination);
		require(!SavePackage::write(desktop, game, 1, unavailableDestination.u8string(), error) &&
			fs::is_directory(unavailableDestination) && get(desktop / "rpg1/game.ini") == state(),
			"failed open leaves the destination directory and source saves intact");
		const auto incompleteArchive = root / fs::u8path(u8"导出失败.zip");
		const auto invalidName = desktop / fs::u8path(u8"rpg1/invalid\u0085.ini");
		put(invalidName, "invalid ZIP entry name");
		require(!SavePackage::write(desktop, game, 1, incompleteArchive.u8string(), error) &&
			error == u8"存档含不兼容或重复的文件名", "export fails after writing the manifest");
		require(!fs::exists(incompleteArchive), "failed export removes the partial ZIP");
		fs::remove(invalidName);
#if defined(_WIN32)
		const HANDLE lockedSource = CreateFileW((desktop / "rpg1/game.ini").c_str(),
			GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		require(lockedSource != INVALID_HANDLE_VALUE, "lock an export source against reading");
		const bool exportedLockedSource = SavePackage::write(desktop, game, 1, incompleteArchive.u8string(), error);
		CloseHandle(lockedSource);
		require(!exportedLockedSource && error.find(u8"无法读取存档文件：") == 0,
			"source read failure occurs after opening the destination");
		require(!fs::exists(incompleteArchive) && get(desktop / "rpg1/game.ini") == state(),
			"source read failure removes the partial ZIP and preserves the source");
#else
		if (geteuid() != 0)
		{
			const auto restrictedDirectory = root / "no-delete";
			const auto remainingArchive = restrictedDirectory / "remaining.zip";
			put(remainingArchive, "previous export");
			put(invalidName, "invalid ZIP entry name");
			// The existing file is writable, but its parent does not allow unlink.
			fs::permissions(restrictedDirectory, fs::perms::owner_read | fs::perms::owner_exec);
			const bool exported = SavePackage::write(desktop, game, 1, remainingArchive.u8string(), error);
			fs::permissions(restrictedDirectory, fs::perms::owner_all);
			fs::remove(invalidName);
			require(!exported && error.find(u8"存档含不兼容或重复的文件名") == 0 &&
				error.find(u8"已清空内容") != std::string::npos && fs::file_size(remainingArchive) == 0,
				"failed deletion clears the partial ZIP and requests manual deletion: " + error);
		}
#endif
		require(SavePackage::write(desktop, game, 1, incompleteArchive.u8string(), error) &&
			SavePackage::read(incompleteArchive.u8string(), package, error), "export can retry after failure: " + error);
		const auto completedArchive = get(incompleteArchive);
		require(SavePackage::listSlots(root / "missing-namespace").empty() &&
			!SavePackage::write(root / "missing-namespace", game, SavePackage::AllSlots, incompleteArchive.u8string(), error) &&
			get(incompleteArchive) == completedArchive,
			"failure before opening the destination preserves an existing package");

		const std::string manifest = "[Package]\nFormat=JXQY-SAVE\nVersion=1\nGameId=TEST_MOD\nGameName=" +
			std::string(u8"测试模组") + "\nSaveNamespace=test_mod\n";
		const std::string editedPlayer = u8"[Player]\nName=自行修改\nLife=98765\n";
		makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", state() },
			{ "rpg1/player0.ini", editedPlayer } });
		require(SavePackage::read(archive.u8string(), package, error) &&
			SavePackage::importTo(package, game, mobile, 8, error) && get(mobile / "rpg8/player0.ini") == editedPlayer,
			"user-edited save content imports unchanged: " + error);
		makeArchive(archive, { {"save_package.ini", manifest}, {"rpg_auto/game.ini", state()} });
		require(SavePackage::read(archive.u8string(), package, error) && package.slots().size() == 1 &&
			package.slots().front().index == 10, "old automatic archives map to automatic slot ten");
		require(SavePackage::importTo(package, game, mobile, 8, error) &&
			get(mobile / "rpg_auto/marker") == "automatic", "explicit import into manual eight preserves automatic");
		const auto limits = createRuntimeSaveGenerationPolicy().limits;
		const auto checkImportBoundary = [&](const std::vector<std::pair<std::string, std::string>>& files,
			bool readable, const std::string& expectedError)
		{
			makeArchive(archive, files, MZ_BEST_SPEED);
			const bool accepted = SavePackage::read(archive.u8string(), package, error);
			require(accepted == readable,
				"import readability boundary: " + expectedError + "; " + error);
			if (!readable)
			{
				require(error.find(expectedError) != std::string::npos, "specific import boundary error");
				require(package.slots().empty() && !SavePackage::importTo(package, game, mobile, 2, error),
					"rejected preview cannot be applied");
				require(get(mobile / "rpg2/game.ini") == state() && get(mobile / "shot/rpg2.png") == binary,
					"rejected import preserves selected slot and screenshot");
			}
		};
		checkImportBoundary({ { "save_package.ini", manifest }, { "rpg1/game.ini", state() },
			{ u8"rpg1/地图/旧地图.npc", "nested" } }, true, "nested user files are preserved");
		require(SavePackage::importTo(package, game, mobile, 8, error) &&
			get(mobile / fs::u8path(u8"rpg8/地图/旧地图.npc")) == "nested", "nested payload imports unchanged");
		checkImportBoundary({ { u8"玩家存档/save_package.ini", manifest }, { u8"玩家存档/rpg1/game.ini", state() },
			{ u8"玩家存档/rpg1/player0.ini", editedPlayer }, { "__MACOSX/._player0.ini", "metadata" },
			{ u8"玩家存档/.DS_Store", "metadata" } }, true, "ordinary tools may wrap the save in one folder");
		require(SavePackage::importTo(package, game, mobile, 8, error) &&
			get(mobile / "rpg8/player0.ini") == editedPlayer && !fs::exists(mobile / ".DS_Store"),
			"wrapped ZIP imports payload and ignores desktop metadata");
		{
			// 7-Zip's default Chinese Windows ZIP uses a GBK filename plus the
			// standard Unicode Path extra field, without setting the UTF-8 flag.
			const std::string encodedName = "rpg1/\xb5\xd8\xcd\xbc\xbf\xec\xd5\xd5.npc";
			const std::string unicodeName = u8"rpg1/地图快照.npc";
			const auto checksum = mz_crc32(0, reinterpret_cast<const unsigned char*>(encodedName.data()), encodedName.size());
			std::string extra{ '\x75', '\x70', static_cast<char>(5 + unicodeName.size()), '\0', '\x01' };
			for (int shift = 0; shift != 32; shift += 8) extra += static_cast<char>(checksum >> shift);
			extra += unicodeName;
			mz_zip_archive zip{};
			require(mz_zip_writer_init_heap(&zip, 0, 0), "init ordinary Unicode ZIP");
			require(mz_zip_writer_add_mem(&zip, "save_package.ini", manifest.data(), manifest.size(), 0) &&
				mz_zip_writer_add_mem(&zip, "rpg1/game.ini", state().data(), state().size(), 0) &&
				mz_zip_writer_add_mem_ex_v2(&zip, encodedName.c_str(), binary.data(), binary.size(), nullptr, 0,
					MZ_ZIP_FLAG_ASCII_FILENAME, 0, 0, nullptr, extra.data(), extra.size(), extra.data(), extra.size()),
				"write ordinary Unicode ZIP");
			void* bytes = nullptr;
			std::size_t size = 0;
			require(mz_zip_writer_finalize_heap_archive(&zip, &bytes, &size), "finalize ordinary Unicode ZIP");
			std::string encodedArchive(static_cast<const char*>(bytes), size);
			put(archive, encodedArchive);
			mz_free(bytes);
			mz_zip_writer_end(&zip);
			require(SavePackage::read(archive.u8string(), package, error) &&
				SavePackage::importTo(package, game, mobile, 8, error) &&
				get(mobile / fs::u8path(u8"rpg8/地图快照.npc")) == binary,
				"ordinary Unicode Path filenames import without changing compression settings: " + error);
			const std::string unsafeName = "../" + std::string(unicodeName.size() - 3, 'x');
			for (auto offset = encodedArchive.find(unicodeName); offset != std::string::npos;
				offset = encodedArchive.find(unicodeName, offset + 1))
				encodedArchive.replace(offset, unicodeName.size(), unsafeName);
			put(archive, encodedArchive);
			require(!SavePackage::read(archive.u8string(), package, error) &&
				get(mobile / fs::u8path(u8"rpg8/地图快照.npc")) == binary,
				"Unicode Path extra field cannot escape the selected save directory");
		}
		checkImportBoundary({ { "save_package.ini", manifest }, { "rpg_auto/game.ini", state() },
			{ "rpg_auto/large.bin", std::string(32 * 1024 * 1024 + 1, 'x') } },
			true, "transfer accepts files larger than the former package and runtime limits");
		require(SavePackage::importTo(package, game, mobile, 9, error) &&
			fs::file_size(mobile / "rpg9/large.bin") == 32 * 1024 * 1024 + 1,
			"large payload imports in chunks: " + error);
		require(SavePackage::write(mobile, game, 9, archive.u8string(), error) &&
			SavePackage::read(archive.u8string(), package, error), "large payload exports and reopens: " + error);
		{
			std::vector<std::pair<std::string, std::string>> files{
				{ "save_package.ini", manifest }, { "rpg1/game.ini", state() }
			};
			for (std::size_t index = 1; index < limits.maximumFileCount; ++index)
				files.emplace_back("rpg1/map" + std::to_string(index) + ".npc", "");
			files.emplace_back("rpg3/game.ini", state());
			files.emplace_back("rpg1/list.ini", "obsolete");
			files.emplace_back("shot/rpg1.png", binary);
			checkImportBoundary(files, true, "many slot files, skipped legacy list and screenshot transfer");
			files.emplace_back("rpg1/extra.npc", "");
			checkImportBoundary(files, true, "transfer file count is independent of runtime copy bounds");
		}
		{
			std::vector<std::pair<std::string, std::string>> files{
				{ "save_package.ini", manifest }, { "rpg1/game.ini", state() }
			};
			auto remaining = limits.maximumTotalBytes - state().size();
			for (int index = 0; remaining != 0; ++index)
			{
				const auto length = std::min<std::uint64_t>(remaining, limits.maximumSingleFileBytes);
				files.emplace_back("rpg1/map" + std::to_string(index) + ".npc", std::string(length, 'x'));
				remaining -= length;
			}
			files.emplace_back("rpg_auto/game.ini", state());
			files.emplace_back("rpg_auto/map.npc", std::string(limits.maximumSingleFileBytes, 'x'));
			files.emplace_back("shot/rpg1.png", binary);
			checkImportBoundary(files, true, "large slot payloads and screenshot transfer");
			files.emplace_back("rpg1/extra.npc", "x");
			checkImportBoundary(files, true, "transfer accepts slot contents beyond runtime copy bounds");
			files.emplace_back("rpg3/game.ini", state());
			files.emplace_back("rpg3/large.bin", std::string(64 * 1024 * 1024, 'x'));
			checkImportBoundary(files, true, "transfer accepts expanded packages above 128 MiB");
		}
		for (const std::string& bad : { "../escape", "/absolute", "rpg1/../../escape", "rpg1/C:stream", "rpg1/CON", "game/game.ini", "config.ini", "other_mod/rpg1/game.ini" })
		{
			makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", state() }, { bad, "bad" } });
			require(!SavePackage::read(archive.u8string(), package, error), "unsafe entry accepted: " + bad);
			require(package.slots().empty(), "failed preview clears package");
		}
		makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", state() }, { "rpg1/GAME.INI", state() } });
		require(!SavePackage::read(archive.u8string(), package, error), "case collision rejected");
		makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/player.ini", "only player" } });
		require(!SavePackage::read(archive.u8string(), package, error), "missing global state rejected");
		makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", state() }, { "rpg1/foo", "file" }, { "rpg1/foo/bar", "nested" } });
		require(!SavePackage::read(archive.u8string(), package, error), "file-directory collision rejected");
		for (const auto& version : { "1.0.0", "1.0.6", "99.0.0", "broken" })
		{
			makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", state("1.03", version) } });
			require(SavePackage::read(archive.u8string(), package, error), error);
			require(!SavePackage::importTo(package, game, mobile, 2, error), "engine version rejected");
		}
		struct ReaderVersionCase
		{
			const char* writer;
			const char* reader;
			bool accepted;
		};
		for (const auto& version : std::vector<ReaderVersionCase>
		{
			{ "1.1.9", nullptr, true },
			{ "1.2.1", "1.2.0", true },
			{ "999.0.0", "1.2.0", true },
			{ "999.0.0", "999.0.0", false },
			{ "999.0.0", nullptr, false },
			{ "1.2.0", "broken", false },
			{ "1.2.0", "", false },
			{ "1.2.0", "1.2.1", false },
		})
		{
			const std::string savedState = state("1.03", version.writer) +
				(version.reader == nullptr ? "" : std::string("[Save]\nMinimumReaderVersion=") + version.reader + "\n");
			makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", savedState } });
			require(SavePackage::read(archive.u8string(), package, error), error);
			require(package.slots().front().engineVersion == version.writer &&
				package.slots().front().minimumReaderVersion == (version.reader == nullptr ? version.writer : version.reader),
				"package keeps writer and minimum reader versions separate");
			const std::string previousState = get(mobile / "rpg6/game.ini");
			require(SavePackage::importTo(package, game, mobile, 6, error) == version.accepted,
				"package uses the declared minimum reader: " + error);
			require(get(mobile / "rpg6/game.ini") == (version.accepted ? savedState : previousState),
				"version-compatible imports preserve bytes and rejected imports preserve the target");
			if (version.accepted)
			{
				auto oldResources = game;
				oldResources.resourceVersion = "1.0.0";
				require(!SavePackage::compatible(package, oldResources, error),
					"a compatible reader does not bypass resource version checks");
				require(SavePackage::write(mobile, game, 6, archive.u8string(), error) &&
					SavePackage::read(archive.u8string(), package, error) &&
					SavePackage::importTo(package, game, desktop, 6, error) && get(desktop / "rpg6/game.ini") == savedState,
					"re-export preserves both version fields without rewriting the save");
			}
		}
		makeArchive(archive, { { "save_package.ini", manifest }, { "rpg1/game.ini", state() } });
		std::string corrupt = get(archive);
		const auto marker = corrupt.find("EngineVersion=");
		require(marker != std::string::npos, "stored ZIP marker");
		corrupt[marker] ^= 1;
		put(archive, corrupt);
		require(!SavePackage::read(archive.u8string(), package, error), "CRC corruption rejected");
		put(archive, corrupt.substr(0, corrupt.size() / 2));
		require(!SavePackage::read(archive.u8string(), package, error), "truncated ZIP rejected");
		require(get(mobile / "rpg2/game.ini") == state(), "rejected imports preserve target");
		if (argc == 3 || argc == 5)
		{
			// Optional real save roundtrip into a caller-provided, empty test path.
			const fs::path actual = fs::u8path(argv[1]);
			const fs::path target = fs::u8path(argv[2]);
			auto actualGame = game;
			if (argc == 5)
			{
				const std::string profile = get(fs::u8path(argv[3]));
				const ResourceIniReader settings(profile.data(), profile.size());
				require(settings.parseError() == 0, "read real-save game profile");
				actualGame = { settings.get("Game", "Id", ""), settings.get("Game", "Name", ""),
					settings.get("Save", "Namespace", ""), settings.get("Game", "Version", ""),
					settings.get("Save", "MinimumCompatibleResourceVersion", "1.0.0") };
			}
			const fs::path realArchive = argc == 5 ? fs::u8path(argv[4]) : archive;
			require(!fs::exists(target), "real-save test destination must not exist");
			require(argc != 5 || !fs::exists(realArchive), "real-save test archive must not exist");
			require(SavePackage::write(actual, actualGame, 1, realArchive.u8string(), error), error);
			require(SavePackage::read(realArchive.u8string(), package, error), error);
			if (argc == 3) actualGame.resourceVersion = package.slots().front().resourceVersion;
			require(SavePackage::importTo(package, actualGame, target, 1, error), error);
			if (argc == 5)
			{
				const std::string exported = get(realArchive);
				mz_zip_archive zip{};
				require(mz_zip_reader_init_mem(&zip, exported.data(), exported.size(), 0), "open real exported save");
				std::vector<std::pair<std::string, std::string>> entries;
				for (mz_uint index = 0; index < mz_zip_reader_get_num_files(&zip); ++index)
				{
					mz_zip_archive_file_stat entry{};
					require(mz_zip_reader_file_stat(&zip, index, &entry), "read real exported entry");
					std::string content(static_cast<std::size_t>(entry.m_uncomp_size), '\0');
					require(mz_zip_reader_extract_to_mem(&zip, index, content.data(), content.size(), 0), "extract real exported entry");
					if (std::string(entry.m_filename) == "rpg1/game.ini") content += "\n; user-edited export compatibility probe\n";
					entries.emplace_back(entry.m_filename, std::move(content));
				}
				mz_zip_reader_end(&zip);
				const fs::path editedArchive = realArchive.parent_path() / "real-save-edited.jxqy-save.zip";
				makeArchive(editedArchive, entries);
				require(SavePackage::read(editedArchive.u8string(), package, error), error);
				require(SavePackage::importTo(package, actualGame, target, 2, error), error);
				require(get(target / "rpg2/game.ini").find("user-edited export compatibility probe") != std::string::npos,
					"real exported save remains user-editable and preserves its edited bytes on import");
			}
		}
		fs::remove_all(root);
		std::cout << "Save package roundtrip, slot mapping, identity, versions and malformed ZIP checks passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << "\nFixtures: " << root.u8string() << '\n';
		return 1;
	}
}
