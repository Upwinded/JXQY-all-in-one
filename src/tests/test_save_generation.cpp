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

SaveGenerationPreflightPolicy makeCharacterPolicy()
{
	SaveGenerationPreflightPolicy policy;
	policy.limits.maximumFileCount = 32;
	policy.limits.maximumTotalBytes =
		64U * 1024U;
	policy.limits.maximumSingleFileBytes =
		16 * 1024;

	SaveGenerationReferenceRule characterRule;
	characterRule.section = "State";
	characterRule.key = "Chr";
	characterRule.required = true;
	characterRule.useDefaultValueWhenMissing = true;
	characterRule.useDefaultValueWhenInvalid = true;
	characterRule.defaultValue = "-1";
	characterRule.valueFormat =
		SaveGenerationReferenceValueFormat::
			OptionalNonNegativeInteger;
	characterRule.contentKind =
		SaveGenerationContentKind::Ini;
	characterRule.candidates =
	{
		{
			SaveGenerationReferenceScope::
				SourceGeneration,
			PLAYER_INI_NAME,
			PLAYER_INI_EXT
		},
		{
			SaveGenerationReferenceScope::Resource,
			std::string(INI_SAVE_FOLDER) +
				PLAYER_INI_NAME,
			PLAYER_INI_EXT
		}
	};
	policy.referenceRules.push_back(
		std::move(characterRule));
	return policy;
}

bool hasError(
	const SaveGenerationResult& result,
	SaveGenerationError expectedError,
	const std::string& context)
{
	return check(
		result.error == expectedError,
		context + ": expected " +
			SaveGeneration::DescribeError(expectedError) +
			", actual " +
			SaveGeneration::DescribeError(result.error));
}

bool runValidDefaultCharacterTest(
	SaveGenerationFixture& fixture)
{
	const std::string gameIni =
		"[State]\n"
		"Map=sample\n";
	const std::string playerIni =
		"[Init]\n"
		"Level=1\n";
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/valid/game.ini", gameIni) &&
			fixture.write(
				"save/valid/player.ini", playerIni),
		"valid default-character fixture is created"))
	{
		return false;
	}

	const SaveGenerationResult result =
		SaveGeneration::Preflight(
			"save\\valid\\",
			makeCharacterPolicy());
	bool ok = check(
		result.succeeded(),
		"valid generation passes preflight");
	ok = check(
		result.fileCount == 2 &&
			result.totalBytes ==
				static_cast<std::uint64_t>(
					gameIni.size() + playerIni.size()),
		"valid generation reports exact file and byte totals") &&
		ok;
	ok = check(
		result.resolvedReferences.size() == 1 &&
			result.resolvedReferences.front().value == "-1" &&
			result.resolvedReferences.front().scope ==
				SaveGenerationReferenceScope::
					SourceGeneration &&
			normalizeVirtualPath(
				result.resolvedReferences.front().path) ==
				"save\\valid\\player.ini",
		"missing State.Chr uses the explicit legacy player.ini default") &&
		ok;
	return ok;
}

bool runLegacyCharacterNumberSyntaxTests(
	SaveGenerationFixture& fixture)
{
	bool ok = true;
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/octal/game.ini",
				"[State]\nChr=010\n") &&
			fixture.write(
				"save/octal/player8.ini",
				"[Init]\nLevel=1\n"),
		"legacy octal character fixture is created"))
	{
		return false;
	}
	SaveGenerationResult result =
		SaveGeneration::Preflight(
			"save/octal",
			makeCharacterPolicy());
	ok = check(
		result.succeeded() &&
			result.resolvedReferences.size() == 1 &&
			normalizeVirtualPath(
				result.resolvedReferences.front().path) ==
				"save\\octal\\player8.ini",
		"legacy octal State.Chr resolves the same player file as INIReader") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/hexadecimal/game.ini",
				"[State]\nChr=0x1\n") &&
			fixture.write(
				"save/hexadecimal/player1.ini",
				"[Init]\nLevel=1\n"),
		"legacy hexadecimal character fixture is created"))
	{
		return false;
	}
	result = SaveGeneration::Preflight(
		"save/hexadecimal",
		makeCharacterPolicy());
	ok = check(
		result.succeeded() &&
			result.resolvedReferences.size() == 1 &&
			normalizeVirtualPath(
				result.resolvedReferences.front().path) ==
				"save\\hexadecimal\\player1.ini",
		"legacy hexadecimal State.Chr resolves the same player file as INIReader") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/invalid-character/game.ini",
				"[State]\nChr=not-a-number\n") &&
			fixture.write(
				"save/invalid-character/player.ini",
				"[Init]\nLevel=1\n"),
		"legacy invalid character fixture is created"))
	{
		return false;
	}
	result = SaveGeneration::Preflight(
		"save/invalid-character",
		makeCharacterPolicy());
	ok = check(
		result.succeeded() &&
			result.resolvedReferences.size() == 1 &&
			normalizeVirtualPath(
				result.resolvedReferences.front().path) ==
				"save\\invalid-character\\player.ini",
		"invalid legacy State.Chr uses the same -1 default as INIReader") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/negative-character/game.ini",
				"[State]\nChr=-2\n") &&
			fixture.write(
				"save/negative-character/player.ini",
				"[Init]\nLevel=1\n"),
		"legacy negative character fixture is created"))
	{
		return false;
	}
	result = SaveGeneration::Preflight(
		"save/negative-character",
		makeCharacterPolicy());
	ok = check(
		result.succeeded() &&
			result.resolvedReferences.size() == 1 &&
			normalizeVirtualPath(
				result.resolvedReferences.front().path) ==
				"save\\negative-character\\player.ini",
		"any legacy negative State.Chr resolves the unnumbered player file") &&
		ok;
	return ok;
}

bool runGameIniValidationTests(
	SaveGenerationFixture& fixture)
{
	bool ok = true;
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/missing-game/player.ini",
				"[Init]\nLevel=1\n"),
		"missing-game fixture is created"))
	{
		return false;
	}
	ok = hasError(
		SaveGeneration::Preflight(
			"save/missing-game",
			makeCharacterPolicy()),
		SaveGenerationError::GameIniMissing,
		"missing game.ini") && ok;

	std::string invalidGameIni =
		"[State]\nChr=0\n";
	invalidGameIni.push_back('\0');
	invalidGameIni += "[Trailing]\nValue=1\n";
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/invalid-game/game.ini",
				invalidGameIni) &&
			fixture.write(
				"save/invalid-game/player0.ini",
				"[Init]\nLevel=1\n"),
		"invalid-game fixture is created"))
	{
		return false;
	}
	ok = hasError(
		SaveGeneration::Preflight(
			"save/invalid-game",
			makeCharacterPolicy()),
		SaveGenerationError::GameIniInvalid,
		"game.ini containing NUL") && ok;
	return ok;
}

bool runLimitTests(
	SaveGenerationFixture& fixture)
{
	const std::string gameIni =
		"[State]\nMap=sample\n";
	const std::string playerIni =
		"[Init]\nLevel=1\n";
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/limits/game.ini", gameIni) &&
			fixture.write(
				"save/limits/player.ini", playerIni),
		"limit fixture is created"))
	{
		return false;
	}

	bool ok = true;
	SaveGenerationPreflightPolicy policy =
		makeCharacterPolicy();
	policy.limits.maximumFileCount = 1;
	ok = hasError(
		SaveGeneration::Preflight(
			"save/limits", policy),
		SaveGenerationError::FileCountLimitExceeded,
		"generation file-count limit") && ok;

	policy = makeCharacterPolicy();
	policy.limits.maximumSingleFileBytes = 1;
	ok = hasError(
		SaveGeneration::Preflight(
			"save/limits", policy),
		SaveGenerationError::
			SourceFileReadOrLimitFailed,
		"generation single-file byte limit") && ok;

	policy = makeCharacterPolicy();
	policy.limits.maximumTotalBytes =
		static_cast<std::uint64_t>(
			gameIni.size() + playerIni.size() - 1);
	ok = hasError(
		SaveGeneration::Preflight(
			"save/limits", policy),
		SaveGenerationError::TotalByteLimitExceeded,
		"generation total-byte limit") && ok;

	policy = makeCharacterPolicy();
	policy.cancellationRequested =
		[]()
		{
			return true;
		};
	ok = hasError(
		SaveGeneration::Preflight(
			"save/limits", policy),
		SaveGenerationError::Cancelled,
		"generation cooperative cancellation") && ok;
	return ok;
}

bool runUnsafeSourceTests()
{
	const SaveGenerationPreflightPolicy policy =
		makeCharacterPolicy();
	bool ok = hasError(
		SaveGeneration::Preflight(
			"../save/unsafe", policy),
		SaveGenerationError::UnsafeSourceDirectory,
		"source traversal") ;
	ok = hasError(
		SaveGeneration::Preflight(
			"ini/save/outside", policy),
		SaveGenerationError::SourceOutsideSaveRoot,
		"safe source outside save root") && ok;
	ok = hasError(
		SaveGeneration::Preflight(
			"save/.", policy),
		SaveGenerationError::SourceOutsideSaveRoot,
		"save root dot alias") && ok;
	ok = hasError(
		SaveGeneration::Preflight(
			"save//unsafe", policy),
		SaveGenerationError::SourceOutsideSaveRoot,
		"generation path with an empty component") && ok;
	return ok;
}

bool runSourcePriorityTest(
	SaveGenerationFixture& fixture)
{
	std::string invalidSourcePlayer =
		"[Init]\nLevel=1\n";
	invalidSourcePlayer.push_back('\0');
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/source-priority/game.ini",
				"[State]\nChr=0\n") &&
			fixture.write(
				"save/source-priority/player0.ini",
				invalidSourcePlayer) &&
			fixture.write(
				"ini/save/player0.ini",
				"[Init]\nLevel=20\n"),
		"source-priority fixture is created"))
	{
		return false;
	}

	const SaveGenerationResult result =
		SaveGeneration::Preflight(
			"save/source-priority",
			makeCharacterPolicy());
	bool ok = hasError(
		result,
		SaveGenerationError::ReferencedFileInvalid,
		"invalid source-generation reference");
	ok = check(
		normalizeVirtualPath(result.errorPath) ==
			"save\\source-priority\\player0.ini" &&
			result.resolvedReferences.empty(),
		"invalid authoritative source reference does not fall back to valid resource") &&
		ok;
	return ok;
}

bool runRequiredReferenceMissingTest(
	SaveGenerationFixture& fixture)
{
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/reference-missing/game.ini",
				"[State]\nChr=7\n"),
		"missing-reference fixture is created"))
	{
		return false;
	}

	const SaveGenerationResult result =
		SaveGeneration::Preflight(
			"save/reference-missing",
			makeCharacterPolicy());
	bool ok = hasError(
		result,
		SaveGenerationError::ReferencedFileMissing,
		"required player reference is missing");
	ok = check(
		result.errorSection == "State" &&
			result.errorKey == "Chr",
		"missing reference reports its game.ini field") && ok;
	return ok;
}

bool runAllowedMissingReferencedFileTest(
	SaveGenerationFixture& fixture)
{
	SaveGenerationPreflightPolicy policy =
		makeCharacterPolicy();
	policy.referenceRules.front().
		allowMissingReferencedFile = true;

	if (!check(
			fixture.reset() &&
				fixture.write(
					"save/allowed-reference-missing/game.ini",
					"[State]\nChr=7\n"),
			"allowed-missing-reference fixture is created"))
	{
		return false;
	}
	const SaveGenerationResult missingFileResult =
		SaveGeneration::Preflight(
			"save/allowed-reference-missing",
			policy);
	bool ok = check(
		missingFileResult.succeeded() &&
			missingFileResult.resolvedReferences.empty(),
		"an explicitly allowed missing reference preserves its required key and succeeds");

	if (!check(
			fixture.reset() &&
				fixture.write(
					"save/allowed-reference-invalid/game.ini",
					"[State]\nChr=7\n") &&
				fixture.write(
					"save/allowed-reference-invalid/player7.ini",
					"[Init\nLevel=7\n"),
			"invalid allowed-reference fixture is created"))
	{
		return false;
	}
	const SaveGenerationResult invalidFileResult =
		SaveGeneration::Preflight(
			"save/allowed-reference-invalid",
			policy);
	ok = hasError(
		invalidFileResult,
		SaveGenerationError::ReferencedFileInvalid,
		"an existing invalid reference remains authoritative") &&
		ok;

	policy.referenceRules.front().
		useDefaultValueWhenMissing = false;
	if (!check(
			fixture.reset() &&
				fixture.write(
					"save/allowed-reference-key-missing/game.ini",
					"[State]\n"),
			"missing-key allowed-reference fixture is created"))
	{
		return false;
	}
	const SaveGenerationResult missingKeyResult =
		SaveGeneration::Preflight(
			"save/allowed-reference-key-missing",
			policy);
	ok = hasError(
		missingKeyResult,
		SaveGenerationError::ReferenceKeyMissing,
		"allowed missing file does not allow a missing required key") &&
		ok;
	return ok;
}

bool runExcludedRequiredFileTest(
	SaveGenerationFixture& fixture)
{
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/excluded-source/game.ini",
				"[State]\nMap=sample\n") &&
			fixture.write(
				"save/excluded-source/player.ini",
				"[Init]\nLevel=1\n") &&
			fixture.write(
				"save/excluded-target/game.ini",
				"[Old]\nValue=1\n") &&
			fixture.write(
				"save/excluded-target/sentinel.dat",
				"unchanged"),
		"excluded-required fixture is created"))
	{
		return false;
	}
	const auto before =
		fixture.snapshot("save/excluded-target");
	const SaveGenerationResult result =
		SaveGeneration::Publish(
			"save/excluded-source",
			"save/excluded-target",
			makeCharacterPolicy(),
			{ "player.ini" });
	bool ok = hasError(
		result,
		SaveGenerationError::RequiredFileExcluded,
		"resolved source reference is excluded");
	ok = check(
		fixture.snapshot("save/excluded-target") == before,
		"rejected required exclusion leaves destination unchanged") &&
		ok;
	return ok;
}

bool runPreflightFailureDoesNotPublishTest(
	SaveGenerationFixture& fixture)
{
	std::string invalidGameIni =
		"[State]\nChr=0\n";
	invalidGameIni.push_back('\0');
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/invalid-publish-source/game.ini",
				invalidGameIni) &&
			fixture.write(
				"save/invalid-publish-source/player0.ini",
				"[Init]\nLevel=1\n") &&
			fixture.write(
				"save/publish-target/game.ini",
				"[Old]\nValue=1\n") &&
			fixture.write(
				"save/publish-target/sentinel.dat",
				"old target bytes"),
		"preflight-failure publish fixture is created"))
	{
		return false;
	}

	const auto before =
		fixture.snapshot("save/publish-target");
	const SaveGenerationResult result =
		SaveGeneration::Publish(
			"save/invalid-publish-source",
			"save/publish-target",
			makeCharacterPolicy());
	bool ok = hasError(
		result,
		SaveGenerationError::GameIniInvalid,
		"publish source fails preflight");
	ok = check(
		fixture.snapshot("save/publish-target") == before &&
			!std::filesystem::exists(
				fixture.physicalPath(
					"save/.jxqy-publish-target-staging")) &&
			!std::filesystem::exists(
				fixture.physicalPath(
					"save/.jxqy-publish-target-backup")),
		"preflight failure never enters publication or changes destination") &&
		ok;
	return ok;
}

bool runSuccessfulPublicationTest(
	SaveGenerationFixture& fixture)
{
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/publish-source/game.ini",
				"[State]\nChr=0\n") &&
			fixture.write(
				"save/publish-source/player0.ini",
				"[Init]\nLevel=9\n") &&
			fixture.write(
				"save/publish-target/game.ini",
				"[Old]\nValue=1\n") &&
			fixture.write(
				"save/publish-target/stale.dat",
				"stale"),
		"successful publication fixture is created"))
	{
		return false;
	}

	const SaveGenerationResult result =
		SaveGeneration::Publish(
			"save/publish-source",
			"save/publish-target",
			makeCharacterPolicy());
	return check(
		result.succeeded() &&
			fixture.read("save/publish-target/game.ini") ==
				"[State]\nChr=0\n" &&
			fixture.read("save/publish-target/player0.ini") ==
				"[Init]\nLevel=9\n" &&
			!std::filesystem::exists(
				fixture.physicalPath(
					"save/publish-target/stale.dat")),
		"successful publication atomically replaces the complete destination");
}

bool runRecoverableSourcePreflightTest(
	SaveGenerationFixture& fixture)
{
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/.jxqy-recoverable-backup/game.ini",
				"[State]\nChr=0\n") &&
			fixture.write(
				"save/.jxqy-recoverable-backup/player0.ini",
				"[Init]\nLevel=3\n") &&
			fixture.write(
				"save/.jxqy-recoverable-staging/partial.dat",
				"partial"),
		"recoverable source fixture is created"))
	{
		return false;
	}

	const SaveGenerationResult result =
		SaveGeneration::Preflight(
			"save/recoverable",
			makeCharacterPolicy());
	return check(
		result.succeeded() &&
			fixture.read("save/recoverable/game.ini") ==
				"[State]\nChr=0\n" &&
			!std::filesystem::exists(
				fixture.physicalPath(
					"save/.jxqy-recoverable-backup")) &&
			!std::filesystem::exists(
				fixture.physicalPath(
					"save/.jxqy-recoverable-staging")),
		"preflight recovers an interrupted source generation before validation");
}

bool runBoundedScratchCopyTests(
	SaveGenerationFixture& fixture)
{
	const std::string gameIni =
		"[State]\nChr=-1\n";
	const std::string playerIni =
		"[Init]\nLevel=1\n";
	const std::string unknownBytes(32, 'x');
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/source/game.ini", gameIni) &&
			fixture.write(
				"save/source/player.ini", playerIni) &&
			fixture.write(
				"save/source/unknown.dat", unknownBytes) &&
			fixture.write(
				"save/load_candidate/sentinel.dat",
				"old target"),
		"bounded scratch-copy fixture is created"))
	{
		return false;
	}

	const auto targetBefore =
		fixture.snapshot("save/load_candidate");
	SaveGenerationLimits limits =
		makeCharacterPolicy().limits;
	limits.maximumFileCount = 2;
	bool ok = check(
		!SaveFileManager::CopySaveGenerationWithinLimits(
			"save/source",
			"save/load_candidate",
			limits) &&
			fixture.snapshot("save/load_candidate") ==
				targetBefore,
		"bounded scratch copy rejects excess files before replacing destination");

	limits = makeCharacterPolicy().limits;
	limits.maximumSingleFileBytes = 8;
	ok = check(
		!SaveFileManager::CopySaveGenerationWithinLimits(
			"save/source",
			"save/load_candidate",
			limits) &&
			fixture.snapshot("save/load_candidate") ==
				targetBefore,
		"bounded scratch copy rejects an oversized source file without publishing") &&
		ok;

	limits = makeCharacterPolicy().limits;
	limits.maximumTotalBytes =
		static_cast<std::uint64_t>(
			gameIni.size() + playerIni.size());
	ok = check(
		!SaveFileManager::CopySaveGenerationWithinLimits(
			"save/source",
			"save/load_candidate",
			limits) &&
			fixture.snapshot("save/load_candidate") ==
				targetBefore,
		"bounded scratch copy rejects excess aggregate bytes without publishing") &&
		ok;

	limits = makeCharacterPolicy().limits;
	ok = check(
		SaveFileManager::CopySaveGenerationWithinLimits(
			"save/source",
			"save/load_candidate",
			limits) &&
			fixture.snapshot("save/load_candidate") ==
				fixture.snapshot("save/source"),
		"bounded scratch copy publishes a complete in-limit snapshot") &&
		ok;
	const auto publishedTarget =
		fixture.snapshot("save/load_candidate");
	ok = check(
		!SaveFileManager::CopySaveGenerationWithinLimits(
			"save/source",
			"save/load_candidate",
			limits,
			{},
			[]()
			{
				return true;
			}) &&
			fixture.snapshot("save/load_candidate") ==
				publishedTarget,
		"cancelled scratch copy leaves the published destination unchanged") &&
		ok;
	ok = check(
		!SaveFileManager::CopySaveGenerationWithinLimits(
			"save/source",
			"save/game",
			limits),
		"bounded clone API refuses a formal save destination") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/.jxqy-game-backup/game.ini",
				"recovered current"),
		"interrupted current-save source fixture is created"))
	{
		return false;
	}
	ok = check(
		SaveFileManager::CopySaveGenerationWithinLimits(
			"save/game",
			"save/load_candidate",
			limits) &&
			fixture.read("save/game/game.ini") ==
				"recovered current" &&
			fixture.read(
				"save/load_candidate/game.ini") ==
				"recovered current" &&
			!std::filesystem::exists(
				fixture.physicalPath(
					"save/.jxqy-game-backup")),
		"bounded scratch capture recovers an interrupted formal source before cloning") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.writeFormalResource(
				"ini/save/game.ini", gameIni) &&
			fixture.writeFormalResource(
				"ini/save/player.ini", playerIni),
		"resource new-game template fixture is created"))
	{
		return false;
	}
	ok = check(
		SaveFileManager::CopySaveGenerationWithinLimits(
				"ini/save",
				"save/load_candidate",
				limits) &&
			fixture.read("save/load_candidate/game.ini") ==
				gameIni &&
			fixture.read("save/load_candidate/player.ini") ==
				playerIni &&
			!std::filesystem::exists(
				fixture.physicalPath("save/rpg0")),
		"resource ini/save template clones into scratch without creating user rpg0") &&
		ok;
	return ok;
}

bool runPreparedLoadCandidatePublicationTests(
	SaveGenerationFixture& fixture)
{
	const SaveGenerationLimits limits =
		makeCharacterPolicy().limits;
	const std::string oldGameIni =
		"[State]\nMap=old.map\n";
	const std::string unparsedGameIni =
		"this candidate is intentionally not parsed";
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/game/old.dat", "old") &&
			fixture.write(
				"save/load_candidate/game.ini",
				unparsedGameIni) &&
			fixture.write(
				"save/load_candidate/broken.map",
				"not a map"),
		"prepared-load publication fixture is created"))
	{
		return false;
	}

	bool ok = check(
		SaveFileManager::
			PublishPreparedLoadCandidateToCurrent(
				limits).succeeded() &&
			fixture.read("save/game/game.ini") ==
				unparsedGameIni &&
			fixture.read("save/game/broken.map") ==
				"not a map" &&
			!File::fileExist("save/game/old.dat") &&
			!File::fileExist(
				"save/load_candidate/game.ini"),
		"prepared-load publication promotes and consumes the already prepared candidate without repeating resource validation");

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/load_candidate/game.ini",
				"candidate") &&
			fixture.write(
				"save/load_candidate/list.ini",
				"legacy list"),
		"unprepared candidate fixture is created"))
	{
		return false;
	}
	const auto currentBeforeUnpreparedCandidate =
		fixture.snapshot("save/game");
	const auto unpreparedCandidateBeforePublication =
		fixture.snapshot("save/load_candidate");
	const SaveGenerationResult unpreparedResult =
		SaveFileManager::
			PublishPreparedLoadCandidateToCurrent(limits);
	ok = check(
		unpreparedResult.error ==
			SaveGenerationError::PublicationFailed &&
			unpreparedResult.errorPath ==
				"save\\load_candidate\\list.ini" &&
			fixture.snapshot("save/game") ==
				currentBeforeUnpreparedCandidate &&
			fixture.snapshot("save/load_candidate") ==
				unpreparedCandidateBeforePublication,
		"an unprepared candidate with list.ini is rejected before either current or candidate is changed") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/load_candidate/unknown.dat",
				"unknown"),
		"missing game.ini publication fixture is created"))
	{
		return false;
	}
	const auto currentBeforeMissing =
		fixture.snapshot("save/game");
	const SaveGenerationResult missingResult =
		SaveFileManager::
			PublishPreparedLoadCandidateToCurrent(limits);
	ok = check(
		missingResult.error ==
			SaveGenerationError::GameIniMissing &&
			fixture.snapshot("save/game") ==
				currentBeforeMissing,
		"a prepared candidate without game.ini does not replace the current generation") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/load_candidate/game.ini",
				"[State]\nMap=new.map\n"),
		"cancelled prepared-load publication fixture is created"))
	{
		return false;
	}
	const auto currentBeforeCancellation =
		fixture.snapshot("save/game");
	const SaveGenerationResult cancelledResult =
		SaveFileManager::
			PublishPreparedLoadCandidateToCurrent(
				limits,
				[]()
				{
					return true;
				});
	ok = check(
		cancelledResult.error ==
			SaveGenerationError::Cancelled &&
			fixture.snapshot("save/game") ==
				currentBeforeCancellation,
		"cancelled prepared-load publication leaves the current generation unchanged") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/load_candidate/game.ini",
				"candidate exceeding publication limits"),
		"bounded prepared-load promotion fixture is created"))
	{
		return false;
	}
	SaveGenerationLimits tinyLimits = limits;
	tinyLimits.maximumTotalBytes = 4;
	const auto currentBeforeLimitFailure =
		fixture.snapshot("save/game");
	const auto candidateBeforeLimitFailure =
		fixture.snapshot("save/load_candidate");
	const SaveGenerationResult limitedResult =
		SaveFileManager::
			PublishPreparedLoadCandidateToCurrent(
				tinyLimits);
	ok = check(
		limitedResult.error ==
			SaveGenerationError::PublicationFailed &&
			fixture.snapshot("save/game") ==
				currentBeforeLimitFailure &&
			fixture.snapshot("save/load_candidate") ==
				candidateBeforeLimitFailure,
		"prepared-load promotion enforces byte limits before consuming the candidate or replacing the current generation") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/game/unknown-old.dat", "old unknown") &&
			fixture.write(
				"save/load_candidate/game.ini",
				"new candidate") &&
			fixture.write(
				"save/load_candidate/unknown-new.dat",
				"new unknown"),
		"failed prepared-load promotion fixture is created"))
	{
		return false;
	}
	const auto currentBeforeInjectedFailure =
		fixture.snapshot("save/game");
	const auto candidateBeforeInjectedFailure =
		fixture.snapshot("save/load_candidate");
	File::DirectoryCopyLimits copyLimits;
	copyLimits.maximumFileCount = limits.maximumFileCount;
	copyLimits.maximumTotalBytes = limits.maximumTotalBytes;
	copyLimits.maximumSingleFileBytes =
		limits.maximumSingleFileBytes;
	ok = check(
		!File::promotePreparedScratchDirectory(
			"save/load_candidate",
			"save/game",
			[](File::DirectoryCopyPhase phase)
			{
				return phase ==
					File::DirectoryCopyPhase::BeforePublish;
			},
			copyLimits) &&
			fixture.snapshot("save/game") ==
				currentBeforeInjectedFailure &&
			fixture.snapshot("save/load_candidate") ==
				candidateBeforeInjectedFailure,
		"failed prepared-load promotion restores both the previous current generation and the complete candidate") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", oldGameIni) &&
			fixture.write(
				"save/load_candidate/game.ini",
				"cancelled candidate") &&
			fixture.write(
				"save/load_candidate/unknown.dat",
				"unknown candidate bytes"),
		"post-staging cancellation fixture is created"))
	{
		return false;
	}
	const auto currentBeforePostStagingCancellation =
		fixture.snapshot("save/game");
	const auto candidateBeforePostStagingCancellation =
		fixture.snapshot("save/load_candidate");
	copyLimits.cancellationRequested = []()
	{
		return !File::fileExist(
			"save/load_candidate/game.ini");
	};
	ok = check(
		!File::promotePreparedScratchDirectory(
			"save/load_candidate",
			"save/game",
			{},
			copyLimits) &&
			fixture.snapshot("save/game") ==
				currentBeforePostStagingCancellation &&
			fixture.snapshot("save/load_candidate") ==
				candidateBeforePostStagingCancellation,
		"cancellation after source staging restores the complete candidate and leaves the current generation unchanged") &&
		ok;
	return ok;
}

bool runIndependentSavePublicationTests(
	SaveGenerationFixture& fixture)
{
	const SaveGenerationLimits limits =
		makeCharacterPolicy().limits;
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini",
				"[State]\nVersion=old-current\n") &&
			fixture.write(
				"save/rpg1/game.ini",
				"[State]\nVersion=old-slot\n") &&
			fixture.write(
				"save/game_build/game.ini",
				"[State]\nVersion=new\n") &&
			fixture.write(
				"save/game_build/player.ini",
				"[Init]\nLevel=2\n"),
		"independent save publication fixture is created"))
	{
		return false;
	}

	const SaveGenerationResult slotPublication =
		SaveFileManager::PublishPreparedSaveGeneration(
			"save/game_build",
			"save/rpg1",
			limits,
			{ SAVE_LIST_FILE });
	bool ok = check(
		slotPublication.succeeded() &&
			fixture.read("save/rpg1/game.ini") ==
				"[State]\nVersion=new\n" &&
			fixture.read("save/game/game.ini") ==
				"[State]\nVersion=old-current\n",
		"slot publication replaces only the selected slot") ;

	const SaveGenerationResult currentPublication =
		SaveFileManager::PublishPreparedSaveGeneration(
			"save/game_build",
			"save/game",
			limits,
			{ SAVE_LIST_FILE });
	ok = check(
		currentPublication.succeeded() &&
			fixture.read("save/game/game.ini") ==
				"[State]\nVersion=new\n" &&
			fixture.read("save/rpg1/game.ini") ==
				"[State]\nVersion=new\n",
		"current publication is a separate ordinary directory transaction") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/game.ini", "old") &&
			fixture.write(
				"save/game_build/other.ini", "draft"),
		"missing game.ini draft fixture is created"))
	{
		return false;
	}
	const auto currentBeforeMissing =
		fixture.snapshot("save/game");
	const SaveGenerationResult missingGameIni =
		SaveFileManager::PublishPreparedSaveGeneration(
			"save/game_build",
			"save/game",
			limits);
	ok = check(
		missingGameIni.error ==
			SaveGenerationError::GameIniMissing &&
			fixture.snapshot("save/game") ==
				currentBeforeMissing,
		"a draft without game.ini is rejected without changing the destination") &&
		ok;

	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game_build/game.ini", "draft"),
		"invalid save destination fixture is created"))
	{
		return false;
	}
	const SaveGenerationResult invalidDestination =
		SaveFileManager::PublishPreparedSaveGeneration(
			"save/game_build",
			"save/rpg0",
			limits);
	ok = check(
		invalidDestination.error ==
			SaveGenerationError::UnsafeDestinationDirectory &&
			!File::fileExist("save/rpg0/game.ini"),
		"prepared save publication accepts only current, auto and user slots") &&
		ok;

	const SaveGenerationResult cancelled =
		SaveFileManager::PublishPreparedSaveGeneration(
			"save/game_build",
			"save/rpg1",
			limits,
			{},
			[]()
			{
				return true;
			});
	ok = check(
		cancelled.error == SaveGenerationError::Cancelled &&
			!File::fileExist("save/rpg1/game.ini"),
		"cancelled publication leaves the selected slot untouched") &&
		ok;
	return ok;
}

bool runScratchGenerationCleanupTests(
	SaveGenerationFixture& fixture)
{
	if (!check(
		fixture.reset() &&
			fixture.write(
				"save/game/sentinel.dat",
				"formal save") &&
			fixture.write(
				"save/load_candidate/candidate.dat",
				"scratch"),
		"scratch cleanup fixture is created"))
	{
		return false;
	}

	bool ok = true;
	{
		SaveFileManager::ScratchGenerationScope unsafeCleanup(
			"save/game");
		ok = check(
			!unsafeCleanup.valid(),
			"scratch cleanup refuses the formal current-save directory") &&
			ok;
	}
	ok = check(
		fixture.read("save/game/sentinel.dat") ==
			"formal save",
		"refused scratch cleanup leaves the formal save unchanged") &&
		ok;
	{
		SaveFileManager::ScratchGenerationScope candidateCleanup(
			"save/load_candidate");
		ok = check(
			candidateCleanup.valid(),
			"known internal candidate directory is accepted for cleanup") &&
			ok;
	}
	ok = check(
		!std::filesystem::exists(
			fixture.physicalPath(
				"save/load_candidate/candidate.dat")),
		"internal candidate files are removed when cleanup scope ends") &&
		ok;

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
	ok = runValidDefaultCharacterTest(fixture) && ok;
	ok = runLegacyCharacterNumberSyntaxTests(fixture) && ok;
	ok = runGameIniValidationTests(fixture) && ok;
	ok = runLimitTests(fixture) && ok;
	ok = runUnsafeSourceTests() && ok;
	ok = runSourcePriorityTest(fixture) && ok;
	ok = runRequiredReferenceMissingTest(fixture) && ok;
	ok = runAllowedMissingReferencedFileTest(fixture) && ok;
	ok = runExcludedRequiredFileTest(fixture) && ok;
	ok = runPreflightFailureDoesNotPublishTest(fixture) && ok;
	ok = runSuccessfulPublicationTest(fixture) && ok;
	ok = runRecoverableSourcePreflightTest(fixture) && ok;
	ok = runBoundedScratchCopyTests(fixture) && ok;
	ok = runPreparedLoadCandidatePublicationTests(
		fixture) && ok;
	ok = runIndependentSavePublicationTests(
		fixture) && ok;
	ok = runScratchGenerationCleanupTests(fixture) && ok;
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
