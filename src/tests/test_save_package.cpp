#include "../Game/GameManager/SavePackage.h"
#include "../Game/GameManager/RuntimeSaveGenerationPolicy.h"
#include "../JxqyEngineVersion.h"
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
		put(desktop / "rpg3/game.ini", state());
		put(desktop / "rpg3/items.dat", binary);
		put(desktop / "rpg_auto/game.ini", state());
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
		require(package.slots().size() == 4, "all slots includes automatic");
		require(SavePackage::importTo(package, game, mobile, SavePackage::AllSlots, error), error);
		require(get(mobile / "rpg3/items.dat") == binary && get(mobile / "rpg_auto/game.ini") == state(), "all payloads");
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

		const std::string manifest = "[Package]\nFormat=JXQY-SAVE\nVersion=1\nGameId=TEST_MOD\nGameName=" +
			std::string(u8"测试模组") + "\nSaveNamespace=test_mod\n";
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
			{ u8"rpg1/地图/旧地图.npc", "nested" } }, false, u8"不支持的存档结构");
		checkImportBoundary({ { "save_package.ini", manifest }, { "rpg_auto/game.ini", state() },
			{ "rpg_auto/large.bin", std::string(limits.maximumSingleFileBytes + 1, 'x') } },
			false, u8"单文件大小");
		{
			std::vector<std::pair<std::string, std::string>> files{
				{ "save_package.ini", manifest }, { "rpg1/game.ini", state() }
			};
			for (std::size_t index = 1; index < limits.maximumFileCount; ++index)
				files.emplace_back("rpg1/map" + std::to_string(index) + ".npc", "");
			files.emplace_back("rpg3/game.ini", state());
			files.emplace_back("rpg1/list.ini", "obsolete");
			files.emplace_back("shot/rpg1.png", binary);
			checkImportBoundary(files, true, "file count is per slot, excluding skipped list and screenshot");
			files.emplace_back("rpg1/extra.npc", "");
			checkImportBoundary(files, false, u8"文件数量");
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
			checkImportBoundary(files, true, "byte limit is per slot and accepts exact file and slot limits");
			files.emplace_back("rpg1/extra.npc", "x");
			checkImportBoundary(files, false, u8"总大小");
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
		if (argc == 3)
		{
			// Optional real save roundtrip into a caller-provided, empty test path.
			const fs::path actual = fs::u8path(argv[1]);
			const fs::path target = fs::u8path(argv[2]);
			require(!fs::exists(target), "real-save test destination must not exist");
			require(SavePackage::write(actual, game, 1, archive.u8string(), error), error);
			require(SavePackage::read(archive.u8string(), package, error), error);
			auto actualGame = game;
			actualGame.resourceVersion = package.slots().front().resourceVersion;
			require(SavePackage::importTo(package, actualGame, target, 1, error), error);
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
