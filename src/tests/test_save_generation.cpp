#include "../File/File.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

std::string normalizeVirtualPath(std::string path)
{
	for (char& character : path)
	{
		if (character == '/')
		{
			character = '\\';
		}
		else if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	while (!path.empty() && path.back() == '\\')
	{
		path.pop_back();
	}
	return path;
}

class SaveGenerationFixture final
{
public:
	SaveGenerationFixture() :
		root(makeUniqueTestDirectory(
			"jxqy_save_generation_test")),
		assetsCollectionRoot(root / "assets"),
		activeRoot(root / "Active"),
		userSaveRoot(root / "save" / SaveNamespace),
		previousAssetsCollectionRoot(
			File::getAssetsCollectionRoot()),
		previousActiveResourceRoot(
			File::getActiveResourceRoot()),
		previousSaveNamespace(
			File::getActiveSaveNamespace())
	{
		std::error_code errorCode;
		std::filesystem::remove_all(root, errorCode);
		errorCode.clear();
		std::filesystem::create_directories(
			activeRoot, errorCode);
		if (!errorCode)
		{
			std::filesystem::create_directories(
				assetsCollectionRoot, errorCode);
		}
		ready = !errorCode;
		if (!ready)
		{
			return;
		}

		// SaveGeneration tests run as an isolated command in the native test
		// process. Clear every fallback so a missing reference cannot resolve
		// against a developer's formal assets.
		File::setPlatformStateParentForTests(
			root.u8string());
		File::setAssetsCollectionRoot(
			assetsCollectionRoot.u8string());
		File::setActiveResourceRoot(
			activeRoot.u8string());
		File::setCommonResourceRoot("");
		File::setResourceFallbackRoots({});
		File::setUiResourceFallbackRoots({});
		File::setActiveSaveNamespace(SaveNamespace);
	}

	~SaveGenerationFixture()
	{
		File::setActiveResourceRoot(
			previousActiveResourceRoot);
		File::setAssetsCollectionRoot(
			previousAssetsCollectionRoot);
		File::setActiveSaveNamespace(
			previousSaveNamespace);
		File::setCommonResourceRoot("");
		File::setResourceFallbackRoots({});
		File::setUiResourceFallbackRoots({});
		File::setPlatformStateParentForTests("");

		std::error_code errorCode;
		std::filesystem::remove_all(root, errorCode);
	}

	bool valid() const
	{
		return ready;
	}

	bool reset()
	{
		std::error_code errorCode;
		std::filesystem::remove_all(
			activeRoot, errorCode);
		if (errorCode)
		{
			return false;
		}
		std::filesystem::create_directories(
			activeRoot, errorCode);
		if (errorCode)
		{
			return false;
		}
		std::filesystem::remove_all(
			userSaveRoot, errorCode);
		return !errorCode;
	}

	std::filesystem::path physicalPath(
		std::string virtualPath) const
	{
		for (char& character : virtualPath)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}
		const std::filesystem::path path =
			std::filesystem::u8path(virtualPath);
		if (virtualPath == "save")
		{
			return userSaveRoot;
		}
		if (virtualPath.rfind("save/", 0) == 0)
		{
			return userSaveRoot / path.lexically_relative("save");
		}
		return activeRoot /
			path;
	}

	bool write(
		const std::string& virtualPath,
		const std::string& content)
	{
		const std::filesystem::path path =
			physicalPath(virtualPath);
		std::error_code errorCode;
		std::filesystem::create_directories(
			path.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}
		std::ofstream output(
			path, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			return false;
		}
		output.write(
			content.data(),
			static_cast<std::streamsize>(content.size()));
		return output.good();
	}

	bool writeFormalResource(
		const std::string& relativePath,
		const std::string& content)
	{
		const std::filesystem::path path =
			activeRoot /
			std::filesystem::u8path(relativePath);
		std::error_code errorCode;
		std::filesystem::create_directories(
			path.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}
		std::ofstream output(
			path, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			return false;
		}
		output.write(
			content.data(),
			static_cast<std::streamsize>(content.size()));
		return output.good();
	}

	std::string read(
		const std::string& virtualPath) const
	{
		std::ifstream input(
			physicalPath(virtualPath),
			std::ios::binary);
		return std::string(
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>());
	}

	std::vector<std::pair<std::string, std::string>>
		snapshot(const std::string& virtualDirectory) const
	{
		std::vector<
			std::pair<std::string, std::string>> result;
		const std::filesystem::path directory =
			physicalPath(virtualDirectory);
		std::error_code errorCode;
		std::filesystem::directory_iterator iterator(
			directory, errorCode);
		const std::filesystem::directory_iterator end;
		for (; !errorCode && iterator != end;
			iterator.increment(errorCode))
		{
			if (!iterator->is_regular_file(errorCode) ||
				errorCode)
			{
				continue;
			}
			std::ifstream input(
				iterator->path(), std::ios::binary);
			result.emplace_back(
				iterator->path().filename().u8string(),
				std::string(
					std::istreambuf_iterator<char>(input),
					std::istreambuf_iterator<char>()));
		}
		std::sort(result.begin(), result.end());
		return result;
	}

private:
	static constexpr const char* SaveNamespace =
		"save-generation-tests";
	std::filesystem::path root;
	std::filesystem::path assetsCollectionRoot;
	std::filesystem::path activeRoot;
	std::filesystem::path userSaveRoot;
	std::string previousAssetsCollectionRoot;
	std::string previousActiveResourceRoot;
	std::string previousSaveNamespace;
	bool ready = false;
};

bool runDirectSaveTests(SaveGenerationFixture& fixture)
{
	if (!check(fixture.reset() &&
		fixture.write("save/game/game.ini", "new-current") &&
		fixture.write("save/game/earlier-map.npc", "persistent-npc") &&
		fixture.write("save/game/earlier-map.obj", "persistent-object") &&
		fixture.write("save/game/list.ini", "legacy-list") &&
		fixture.write("save/rpg1/stale.ini", "old-slot") &&
		fixture.write("save/rpg2/game.ini", "other-slot"), "create direct save fixture")) return false;
	const auto currentBefore = fixture.snapshot("save/game");
	bool ok = check(SaveFileManager::CopySaveFileTo(1) &&
		fixture.read("save/rpg1/game.ini") == "new-current" &&
		fixture.read("save/rpg1/earlier-map.npc") == "persistent-npc" &&
		fixture.read("save/rpg1/earlier-map.obj") == "persistent-object" &&
		!File::fileExist("save/rpg1/stale.ini") && !File::fileExist("save/rpg1/list.ini") &&
		fixture.snapshot("save/game") == currentBefore &&
		fixture.read("save/rpg2/game.ini") == "other-slot",
		"direct save clears old slot files, carries earlier maps and leaves the source and other slots intact");
	ok = check(!std::filesystem::exists(fixture.physicalPath("save/game_build")) &&
		!std::filesystem::exists(fixture.physicalPath("save/.jxqy-rpg1-staging")) &&
		!std::filesystem::exists(fixture.physicalPath("save/.jxqy-rpg1-backup")) &&
		!std::filesystem::exists(fixture.physicalPath("save/.jxqy-rpg1-staging-ready")),
		"direct save creates no draft, backup or publication marker") && ok;
	ok = check(SaveFileManager::CopySaveFileToAuto() &&
		fixture.snapshot("save/rpg_auto") == fixture.snapshot("save/rpg1"),
		"automatic save uses the same direct copy") && ok;
	const auto slotBefore = fixture.snapshot("save/rpg1");
	ok = check(!SaveFileManager::CopySaveFileTo(0) && !SaveFileManager::CopySaveFileTo(8) &&
		!File::overwriteDirectoryFiles("save/game", "save/game") &&
		!SaveFileManager::CopySaveFileTo(1, []() { return true; }) &&
		fixture.snapshot("save/rpg1") == slotBefore && fixture.snapshot("save/game") == currentBefore,
		"invalid targets and cancellation before copying leave existing files intact") && ok;
	bool cancelledAfterClear = false;
	ok = check(!SaveFileManager::CopySaveFileTo(1, [&]()
		{
			cancelledAfterClear = fixture.snapshot("save/rpg1").empty();
			return cancelledAfterClear;
		}) && cancelledAfterClear && fixture.snapshot("save/rpg1").empty() &&
		fixture.snapshot("save/game") == currentBefore && fixture.read("save/rpg2/game.ini") == "other-slot",
		"cancellation after clearing reports failure without restoring the old slot or changing other data") && ok;
	ok = check(SaveFileManager::CopySaveFileTo(1) && fixture.snapshot("save/rpg1") == slotBefore,
		"saving again replaces the incomplete slot") && ok;
	std::filesystem::remove(fixture.physicalPath("save/game/game.ini"));
	ok = check(!SaveFileManager::CopySaveFileTo(1) && fixture.snapshot("save/rpg1") == slotBefore,
		"missing current state is reported before clearing a slot") && ok;
	return ok;
}

bool runDirectLoadTests(SaveGenerationFixture& fixture)
{
	if (!check(fixture.reset() &&
		fixture.write("save/rpg1/game.ini", "selected-save") &&
		fixture.write("save/rpg1/earlier-map.npc", "persistent-npc") &&
		fixture.write("save/rpg1/list.ini", "legacy-list") &&
		fixture.write("save/game/stale.obj", "old-world") &&
		fixture.write("save/rpg2/game.ini", "other-slot"), "create direct load fixture")) return false;
	const auto selectedBefore = fixture.snapshot("save/rpg1");
	bool ok = check(SaveFileManager::CopySaveFileFrom(1) &&
		fixture.read("save/game/game.ini") == "selected-save" &&
		fixture.read("save/game/earlier-map.npc") == "persistent-npc" &&
		!File::fileExist("save/game/stale.obj") && !File::fileExist("save/game/list.ini") &&
		fixture.snapshot("save/rpg1") == selectedBefore &&
		fixture.read("save/rpg2/game.ini") == "other-slot",
		"direct load replaces runtime files and preserves selected and unrelated slots");
	ok = check(!std::filesystem::exists(fixture.physicalPath("save/load_candidate")) &&
		!std::filesystem::exists(fixture.physicalPath("save/.jxqy-game-staging")) &&
		!std::filesystem::exists(fixture.physicalPath("save/.jxqy-game-backup")) &&
		!std::filesystem::exists(fixture.physicalPath("save/.jxqy-game-staging-ready")),
		"loading creates no candidate directory or publication transaction") && ok;
	const auto runtimeBefore = fixture.snapshot("save/game");
	ok = check(!SaveFileManager::CopySaveFileFrom(8) && !SaveFileManager::CopySaveFileFrom(7) &&
		fixture.snapshot("save/game") == runtimeBefore,
		"invalid and missing slots are rejected before clearing runtime files") && ok;
	ok = check(fixture.write("save/rpg_auto/game.ini", "automatic-save") &&
		SaveFileManager::CopySaveFileFromAuto() &&
		fixture.read("save/game/game.ini") == "automatic-save" &&
		!File::fileExist("save/game/earlier-map.npc") &&
		fixture.read("save/rpg_auto/game.ini") == "automatic-save",
		"automatic load uses direct replacement and leaves its source intact") && ok;
	File::DirectoryCopyLimits limits;
	limits.maximumSingleFileBytes = 2;
	ok = check(!File::overwriteDirectoryFiles("save/rpg1", "save/game", {}, {}, limits) &&
		fixture.snapshot("save/rpg1") == selectedBefore &&
		SaveFileManager::CopySaveFileFrom(1),
		"an oversized source fails without changing the selected slot and can be retried") && ok;
	limits = {};
	limits.maximumFileCount = 1;
	ok = check(!File::overwriteDirectoryFiles("save/rpg1", "save/game", {}, {}, limits) &&
		fixture.snapshot("save/rpg1") == selectedBefore &&
		SaveFileManager::CopySaveFileFrom(1),
		"direct copying enforces the file count limit and preserves the source") && ok;
	limits = {};
	limits.maximumTotalBytes = 20;
	ok = check(!File::overwriteDirectoryFiles("save/rpg1", "save/game", {}, {}, limits) &&
		fixture.snapshot("save/rpg1") == selectedBefore &&
		SaveFileManager::CopySaveFileFrom(1),
		"direct copying enforces the total byte limit and preserves the source") && ok;
	bool cancelledAfterClear = false;
	ok = check(!File::overwriteDirectoryFiles("save/rpg1", "save/game", {}, [&]()
		{
			cancelledAfterClear = fixture.snapshot("save/game").empty();
			return cancelledAfterClear;
		}) && cancelledAfterClear && fixture.snapshot("save/game").empty() &&
		fixture.snapshot("save/rpg1") == selectedBefore &&
		SaveFileManager::CopySaveFileFrom(1),
		"cancelling a direct load leaves the selected slot available for a complete retry") && ok;
	ok = check(fixture.write("save/.jxqy-rpg1-backup/legacy.ini", "slot-backup") &&
		fixture.write("save/.jxqy-game-backup/legacy.ini", "runtime-backup") &&
		SaveFileManager::HasSaveFile(1) &&
		SaveFileManager::CopySaveFileFrom(1) &&
		SaveFileManager::CopySaveFileTo(1) &&
		fixture.read("save/.jxqy-rpg1-backup/legacy.ini") == "slot-backup" &&
		fixture.read("save/.jxqy-game-backup/legacy.ini") == "runtime-backup",
		"normal slot queries, loads and saves do not revisit legacy transaction artifacts") && ok;
	return ok;
}

bool runCurrentPathScopeTests()
{
	const std::string originalPath =
		SaveFileManager::CurrentPath();
	bool ok = true;
	{
		SaveFileManager::CurrentPathScope unsafeScope(
			"../save/unsafe");
		ok = check(
			!unsafeScope.valid() &&
				SaveFileManager::CurrentPath() ==
					originalPath,
			"unsafe CurrentPathScope is invalid and preserves current path") &&
			ok;
	}
	{
		SaveFileManager::CurrentPathScope outsideScope(
			"ini/save/outside");
		ok = check(
			!outsideScope.valid() &&
				SaveFileManager::CurrentPath() ==
					originalPath,
			"outside-save CurrentPathScope is invalid") &&
			ok;
	}
	{
		SaveFileManager::CurrentPathScope dotScope(
			"save/.");
		ok = check(
			!dotScope.valid() &&
				SaveFileManager::CurrentPath() ==
					originalPath,
			"save root dot alias is not a valid generation") &&
			ok;
	}
	{
		SaveFileManager::CurrentPathScope emptyComponentScope(
			"save//generated");
		ok = check(
			!emptyComponentScope.valid() &&
				SaveFileManager::CurrentPath() ==
					originalPath,
			"generation path rejects an empty component") &&
			ok;
	}
	{
		SaveFileManager::CurrentPathScope outerScope(
			"save\\generated");
		ok = check(
			outerScope.valid() &&
				SaveFileManager::CurrentPath() ==
					"save\\generated\\",
			"valid CurrentPathScope installs a trailing separator") &&
			ok;
		{
			SaveFileManager::CurrentPathScope invalidNestedScope(
				"../save/nested");
			ok = check(
				!invalidNestedScope.valid() &&
					SaveFileManager::CurrentPath() ==
						"save\\generated\\",
				"invalid nested scope preserves active outer path") &&
				ok;
		}
		{
			SaveFileManager::CurrentPathScope nestedScope(
				"save\\generated\\nested");
			ok = check(
				nestedScope.valid() &&
					SaveFileManager::CurrentPath() ==
						"save\\generated\\nested\\",
				"nested valid scope installs its path") &&
				ok;
		}
		ok = check(
			SaveFileManager::CurrentPath() ==
				"save\\generated\\",
			"nested valid scope restores outer path") &&
			ok;
	}
	ok = check(
		SaveFileManager::CurrentPath() == originalPath,
		"outer CurrentPathScope restores original path") &&
		ok;
	return ok;
}

bool runEntityListNamePolicyTests()
{
	const std::vector<std::string> reservedNames = {
		"GAME.INI",
		"list.ini",
		"memo.txt",
		"variable.ini",
		"traps.ini",
		"trapindexignore.ini",
		"proj.ini",
		"partneridx.ini",
		"player.ini",
		"PLAYER7.INI",
		"partner0.ini",
		"partner42.ini",
		"magic.ini",
		"magic12.ini",
		"goods.ini",
		"goods999.ini"
	};
	bool ok = check(
		SaveFileManager::IsSafeEntityListFileName(
			"runtime-state.npc") &&
		SaveFileManager::IsSafeEntityListFileName(
			"runtime-state.obj") &&
		SaveFileManager::IsSafeEntityListFileName(
			"memo.ini"),
		"entity list names allow ordinary files and the legacy memo.ini object alias");
	ok = check(
		!SaveFileManager::IsSafeEntityListFileName("") &&
		!SaveFileManager::IsSafeEntityListFileName(
			"nested/runtime.npc") &&
		!SaveFileManager::IsSafeEntityListFileName(
			"nested\\runtime.obj") &&
		std::all_of(
			reservedNames.cbegin(),
			reservedNames.cend(),
			[](const std::string& fileName)
			{
				return !SaveFileManager::
					IsSafeEntityListFileName(fileName);
			}),
		"entity list names cannot escape the flat generation or overwrite core save files") &&
		ok;
	ok = check(
		SaveFileManager::AreEntityListFileNamesDistinct(
			"actors.npc", "objects.obj") &&
		SaveFileManager::AreEntityListFileNamesDistinct(
			"", "") &&
		SaveFileManager::AreEntityListFileNamesDistinct(
			"", "shared.ini") &&
		!SaveFileManager::AreEntityListFileNamesDistinct(
			"Shared.ini", "shared.INI"),
		"NPC and object list names cannot collide on case-insensitive save roots") &&
		ok;
	return ok;
}

bool runEmptyNamespaceStartupRecoveryTest(
	SaveGenerationFixture& fixture)
{
	if (!check(
			fixture.reset(),
			"empty save namespace fixture is reset"))
	{
		return false;
	}
	bool ok = check(
		SaveFileManager::RecoverInterruptedSaveOperations(),
		"startup recovery accepts a missing save namespace");
	ok = check(
		std::filesystem::is_directory(
			fixture.physicalPath("save")),
		"startup recovery creates the save namespace parent") &&
		ok;
	ok = check(
		!std::filesystem::exists(
			fixture.physicalPath("save/game_build")) &&
			!std::filesystem::exists(
				fixture.physicalPath("save/load_candidate")),
		"startup recovery does not create missing scratch directories") &&
		ok;
	ok = check(
		SaveFileManager::RecoverInterruptedSaveOperations(),
		"startup recovery accepts an existing empty save namespace") &&
		ok;
	return ok;
}
}

bool runSaveGenerationTests()
{
	SaveGenerationFixture fixture;
	if (!check(
		fixture.valid(),
		"save-generation temporary routing fixture is ready"))
	{
		return false;
	}

	bool ok = true;
	ok = runEmptyNamespaceStartupRecoveryTest(fixture) && ok;
	ok = runDirectSaveTests(
		fixture) && ok;
	ok = runDirectLoadTests(fixture) && ok;
	ok = runCurrentPathScopeTests() && ok;
	ok = runEntityListNamePolicyTests() && ok;
	return ok;
}

#if defined(JXQY_SAVE_GENERATION_STANDALONE)
int main()
{
	return runSaveGenerationTests() ? 0 : 1;
}
#endif
