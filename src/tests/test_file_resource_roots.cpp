#include "../File/File.h"
#include "../File/log.h"
#include "../File/ResourceReadPrefixPolicy.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "../Game/GameTypes.h"
#include "../Game/Data/TalkTextList.h"
#include "../Image/IMPFormatValidation.h"
#include "../Launch/EditorRunDiagnosticsFile.h"
#include "../Launch/EditorRunRuntimeTraceFile.h"
#include "../Resource/ResourceCatalog.h"
#include "../Resource/ResourceManifest.h"
#include "../Resource/ResourceManager.h"
#include "../libconvert/libconvert.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#endif

class ResourceManagerPolicyTestAccess
{
public:
	static void reset(ResourceManager& manager)
	{
		manager.discoveredPacks.clear();
		manager.resourceCatalogDiagnostics.clear();
		manager.currentCatalogRequest = {};
		manager.currentCatalogRequestValid = false;
		manager.assetsCollectionRoot.clear();
		manager.writableResourceCollectionRoot.clear();
		manager.commonResourceRoot.clear();
		manager.writableCommonResourceRoot.clear();
		manager.activeResourceRoot.clear();
		manager.activeResourceEntryKey.clear();
		manager.activeManifest = ResourceManifest();
		manager.activeResourceSelectionValid = false;
		manager.initialized = false;
		File::setAssetsCollectionRoot("");
		File::setActiveResourceRoot("");
		File::setCommonResourceRoot("");
		File::setCommonResourceFallbackRoots({});
		File::setResourceFallbackRoots({});
		File::setUiResourceFallbackRoots({});
		File::setActiveSaveNamespace("");
	}

	static void scanCollectionRoot(
		ResourceManager& manager,
		const std::string& collectionRoot)
	{
		manager.assetsCollectionRoot = collectionRoot;
		manager.writableResourceCollectionRoot = collectionRoot;
		manager.writableCommonResourceRoot.clear();
		File::setAssetsCollectionRoot(collectionRoot);
		manager.scanCollectionRoot(collectionRoot);
	}

	static void scanCollectionWithWritableRoot(
		ResourceManager& manager,
		const std::string& collectionRoot,
		const std::string& writableRoot)
	{
		manager.assetsCollectionRoot = collectionRoot;
		manager.writableResourceCollectionRoot = writableRoot;
		manager.writableCommonResourceRoot.clear();
		File::setAssetsCollectionRoot(collectionRoot);
		manager.scanCollectionRoot(collectionRoot);
	}

	static const std::string& activeResourceEntryKey(
		const ResourceManager& manager)
	{
		return manager.activeResourceEntryKey;
	}
};

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		return false;
	}
	return true;
}

void writeRawFile(const std::filesystem::path& path, const std::string& content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << content;
}

std::string readRawFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
}

std::vector<std::string> snapshotDirectoryTree(const std::filesystem::path& root)
{
	std::vector<std::string> snapshot;
	std::error_code errorCode;
	if (!std::filesystem::is_directory(root, errorCode) || errorCode)
	{
		return snapshot;
	}
	const std::filesystem::file_time_type rootWriteTime =
		std::filesystem::last_write_time(root, errorCode);
	if (errorCode)
	{
		return snapshot;
	}
	snapshot.push_back("D:.:" + std::to_string(
		static_cast<long long>(
			rootWriteTime.time_since_epoch().count())));

	std::filesystem::recursive_directory_iterator iterator(root, errorCode);
	const std::filesystem::recursive_directory_iterator end;
	for (; !errorCode && iterator != end; iterator.increment(errorCode))
	{
		const std::filesystem::path relativePath =
			std::filesystem::relative(iterator->path(), root, errorCode);
		if (errorCode)
		{
			break;
		}
		const std::string relativeName = relativePath.generic_u8string();
		const std::filesystem::file_status status =
			iterator->symlink_status(errorCode);
		if (errorCode)
		{
			break;
		}
		if (std::filesystem::is_directory(status))
		{
			snapshot.push_back("D:" + relativeName);
		}
		else if (std::filesystem::is_regular_file(status))
		{
			snapshot.push_back("F:" + relativeName + ":" +
				readRawFile(iterator->path()));
		}
		else if (std::filesystem::is_symlink(status))
		{
			snapshot.push_back("L:" + relativeName);
		}
		else
		{
			snapshot.push_back("O:" + relativeName);
		}
	}
	if (errorCode)
	{
		snapshot.push_back("E:" + errorCode.message());
	}
	std::sort(snapshot.begin(), snapshot.end());
	return snapshot;
}

std::vector<std::string> snapshotDirectoryTreeWithWriteTimes(
	const std::filesystem::path& root)
{
	std::vector<std::string> snapshot;
	std::error_code errorCode;
	if (!std::filesystem::is_directory(root, errorCode) || errorCode)
	{
		return snapshot;
	}
	const std::filesystem::file_time_type rootWriteTime =
		std::filesystem::last_write_time(root, errorCode);
	if (errorCode)
	{
		return snapshot;
	}
	snapshot.push_back("D:.:" + std::to_string(
		static_cast<long long>(
			rootWriteTime.time_since_epoch().count())));

	std::filesystem::recursive_directory_iterator iterator(root, errorCode);
	const std::filesystem::recursive_directory_iterator end;
	for (; !errorCode && iterator != end; iterator.increment(errorCode))
	{
		const std::filesystem::path relativePath =
			std::filesystem::relative(iterator->path(), root, errorCode);
		if (errorCode)
		{
			break;
		}
		const std::string relativeName = relativePath.generic_u8string();
		const std::filesystem::file_status status =
			iterator->symlink_status(errorCode);
		if (errorCode)
		{
			break;
		}
		const std::filesystem::file_time_type writeTime =
			std::filesystem::last_write_time(iterator->path(), errorCode);
		if (errorCode)
		{
			break;
		}
		const std::string writeTimeText = std::to_string(
			static_cast<long long>(writeTime.time_since_epoch().count()));
		if (std::filesystem::is_directory(status))
		{
			snapshot.push_back(
				"D:" + relativeName + ":" + writeTimeText);
		}
		else if (std::filesystem::is_regular_file(status))
		{
			snapshot.push_back("F:" + relativeName + ":" +
				writeTimeText + ":" + readRawFile(iterator->path()));
		}
		else if (std::filesystem::is_symlink(status))
		{
			snapshot.push_back(
				"L:" + relativeName + ":" + writeTimeText);
		}
		else
		{
			snapshot.push_back(
				"O:" + relativeName + ":" + writeTimeText);
		}
	}
	if (errorCode)
	{
		snapshot.push_back("E:" + errorCode.message());
	}
	std::sort(snapshot.begin(), snapshot.end());
	return snapshot;
}

struct RegularFileSnapshot
{
	std::string content;
	std::filesystem::file_time_type writeTime;
	bool valid = false;
};

RegularFileSnapshot snapshotRegularFile(
	const std::filesystem::path& path)
{
	RegularFileSnapshot snapshot;
	std::error_code errorCode;
	if (!std::filesystem::is_regular_file(path, errorCode) || errorCode)
	{
		return snapshot;
	}
	snapshot.writeTime =
		std::filesystem::last_write_time(path, errorCode);
	if (errorCode)
	{
		return snapshot;
	}
	snapshot.content = readRawFile(path);
	snapshot.valid = true;
	return snapshot;
}

bool regularFileMatchesSnapshot(const std::filesystem::path& path,
	const RegularFileSnapshot& snapshot)
{
	if (!snapshot.valid)
	{
		return false;
	}
	std::error_code errorCode;
	const std::filesystem::file_time_type writeTime =
		std::filesystem::last_write_time(path, errorCode);
	return !errorCode &&
		readRawFile(path) == snapshot.content &&
		writeTime == snapshot.writeTime;
}

bool createSymbolicLinkFixture(const std::filesystem::path& targetPath,
	const std::filesystem::path& linkPath,
	bool directory,
	std::string& errorMessage)
{
	namespace fs = std::filesystem;
	errorMessage.clear();
	std::error_code errorCode;
	if (directory)
	{
		fs::create_directory_symlink(targetPath, linkPath, errorCode);
	}
	else
	{
		fs::create_symlink(targetPath, linkPath, errorCode);
	}
	if (!errorCode)
	{
		return true;
	}

#if defined(_WIN32)
	try
	{
		const DWORD flags =
			(directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0) |
			SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
		if (CreateSymbolicLinkW(linkPath.wstring().c_str(),
				targetPath.wstring().c_str(), flags) != 0)
		{
			return true;
		}
		errorMessage = errorCode.message() +
			"; CreateSymbolicLinkW error " +
			std::to_string(GetLastError());
	}
	catch (const std::exception& exception)
	{
		errorMessage = errorCode.message() + "; " + exception.what();
	}
#else
	errorMessage = errorCode.message();
#endif
	return false;
}

bool swapDirectoryNames(
	const std::filesystem::path& firstPath,
	const std::filesystem::path& secondPath,
	const std::filesystem::path& temporaryPath,
	std::string& errorMessage)
{
	namespace fs = std::filesystem;
	errorMessage.clear();
	std::error_code errorCode;
	if (fs::exists(temporaryPath, errorCode) || errorCode)
	{
		errorMessage =
			"temporary swap path already exists";
		return false;
	}

	fs::rename(firstPath, temporaryPath, errorCode);
	if (errorCode)
	{
		errorMessage =
			"first-to-temporary rename failed: " +
			errorCode.message();
		return false;
	}

	errorCode.clear();
	fs::rename(secondPath, firstPath, errorCode);
	if (errorCode)
	{
		errorMessage =
			"second-to-first rename failed: " +
			errorCode.message();
		errorCode.clear();
		fs::rename(temporaryPath, firstPath, errorCode);
		return false;
	}

	errorCode.clear();
	fs::rename(temporaryPath, secondPath, errorCode);
	if (errorCode)
	{
		errorMessage =
			"temporary-to-second rename failed: " +
			errorCode.message();
		std::error_code rollbackError;
		fs::rename(firstPath, secondPath, rollbackError);
		rollbackError.clear();
		fs::rename(temporaryPath, firstPath, rollbackError);
		return false;
	}
	return true;
}

bool runDirectoryReplacementFixture(const std::filesystem::path& installedRoot,
	const std::filesystem::path& formalTarget,
	const std::string& fixtureName,
	const std::function<bool()>& verifyReplacement,
	bool replacementInvalidatesLayout = true)
{
	namespace fs = std::filesystem;
	bool ok = true;
	fs::path backupRoot = installedRoot;
	backupRoot += ".installed-identity";

	std::error_code errorCode;
	const fs::file_time_type installedWriteTime =
		fs::last_write_time(installedRoot, errorCode);
	if (errorCode)
	{
		return check(false, fixtureName +
			" fixture could not snapshot the installed root mtime");
	}
	const fs::path installedParent = installedRoot.parent_path();
	const fs::file_time_type parentWriteTime =
		fs::last_write_time(installedParent, errorCode);
	if (errorCode)
	{
		return check(false, fixtureName +
			" fixture could not snapshot the parent mtime");
	}
	fs::rename(installedRoot, backupRoot, errorCode);
	if (errorCode)
	{
		return check(false, fixtureName +
			" fixture could not rename the installed output root: " +
			errorCode.message());
	}

	std::string linkError;
	const bool linkCreated = createSymbolicLinkFixture(
		formalTarget, installedRoot, true, linkError);
	ok = check(linkCreated, fixtureName +
		" fixture must create a real directory symlink: " + linkError) && ok;
	if (linkCreated)
	{
		std::cout << "FIXTURE: executed " << fixtureName << std::endl;
		std::string editorRunLogPath;
		std::string editorRunDiagnosticsPath;
		const File::EditorRunFileLayoutState expectedState =
			replacementInvalidatesLayout
				? File::EditorRunFileLayoutState::Invalid
				: File::EditorRunFileLayoutState::Valid;
		ok = check(File::hasEditorRunFileLayout() &&
			File::getEditorRunLogPath(editorRunLogPath) ==
				expectedState &&
			File::getEditorRunDiagnosticsPath(
				editorRunDiagnosticsPath) ==
				expectedState &&
			(replacementInvalidatesLayout
				? editorRunLogPath.empty() &&
					editorRunDiagnosticsPath.empty()
				: !editorRunLogPath.empty() &&
					!editorRunDiagnosticsPath.empty()),
			fixtureName +
			(replacementInvalidatesLayout
				? " invalidates the private output layout"
				: " leaves the private output layout valid")) && ok;
		ok = verifyReplacement() && ok;

		errorCode.clear();
		const bool removed = fs::remove(installedRoot, errorCode);
		ok = check(removed && !errorCode,
			fixtureName + " removes only the replacement symlink") && ok;
	}

	errorCode.clear();
	fs::rename(backupRoot, installedRoot, errorCode);
	ok = check(!errorCode,
		fixtureName + " restores the original installed output root") && ok;
	if (!errorCode)
	{
		fs::last_write_time(
			installedRoot, installedWriteTime, errorCode);
		ok = check(!errorCode,
			fixtureName + " restores the installed root mtime") && ok;
		errorCode.clear();
		fs::last_write_time(
			installedParent, parentWriteTime, errorCode);
		ok = check(!errorCode,
			fixtureName + " restores the parent mtime") && ok;
		std::string editorRunLogPath;
		std::string editorRunDiagnosticsPath;
		ok = check(File::getEditorRunLogPath(editorRunLogPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!editorRunLogPath.empty() &&
			File::getEditorRunDiagnosticsPath(
				editorRunDiagnosticsPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!editorRunDiagnosticsPath.empty(),
			fixtureName +
			" restoration recovers the installed layout identity") && ok;
	}
	return ok;
}

bool runOrdinaryDirectoryReplacementFixture(
	const std::filesystem::path& installedRoot,
	const std::string& fixtureName)
{
	namespace fs = std::filesystem;
	bool ok = true;
	fs::path backupRoot = installedRoot;
	backupRoot += ".ordinary-installed-identity";

	std::error_code errorCode;
	const fs::file_time_type installedWriteTime =
		fs::last_write_time(installedRoot, errorCode);
	if (errorCode)
	{
		return check(false, fixtureName +
			" fixture could not snapshot the installed root mtime");
	}
	const fs::path installedParent = installedRoot.parent_path();
	const fs::file_time_type parentWriteTime =
		fs::last_write_time(installedParent, errorCode);
	if (errorCode)
	{
		return check(false, fixtureName +
			" fixture could not snapshot the parent mtime");
	}
	fs::rename(installedRoot, backupRoot, errorCode);
	if (errorCode)
	{
		return check(false, fixtureName +
			" fixture could not rename the installed output root: " +
			errorCode.message());
	}
	const bool created = fs::create_directory(installedRoot, errorCode);
	ok = check(created && !errorCode,
		fixtureName +
		" fixture creates a different ordinary directory at the same path") &&
		ok;
	if (created && !errorCode)
	{
		std::cout << "FIXTURE: executed " << fixtureName << std::endl;
		std::string editorRunLogPath;
		std::string editorRunDiagnosticsPath;
		ok = check(File::hasEditorRunFileLayout() &&
			File::getEditorRunLogPath(editorRunLogPath) ==
				File::EditorRunFileLayoutState::Invalid &&
			editorRunLogPath.empty() &&
			File::getEditorRunDiagnosticsPath(
				editorRunDiagnosticsPath) ==
				File::EditorRunFileLayoutState::Invalid &&
			editorRunDiagnosticsPath.empty(),
			fixtureName +
			" marks the installed layout invalid by physical directory identity") &&
			ok;
		errorCode.clear();
		const bool removed = fs::remove(installedRoot, errorCode);
		ok = check(removed && !errorCode,
			fixtureName + " removes only the replacement directory") && ok;
	}

	errorCode.clear();
	fs::rename(backupRoot, installedRoot, errorCode);
	ok = check(!errorCode,
		fixtureName + " restores the original installed output root") && ok;
	if (!errorCode)
	{
		fs::last_write_time(
			installedRoot, installedWriteTime, errorCode);
		ok = check(!errorCode,
			fixtureName + " restores the installed root mtime") && ok;
		errorCode.clear();
		fs::last_write_time(
			installedParent, parentWriteTime, errorCode);
		ok = check(!errorCode,
			fixtureName + " restores the parent mtime") && ok;
		std::string editorRunLogPath;
		std::string editorRunDiagnosticsPath;
		ok = check(File::getEditorRunLogPath(editorRunLogPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!editorRunLogPath.empty() &&
			File::getEditorRunDiagnosticsPath(
				editorRunDiagnosticsPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!editorRunDiagnosticsPath.empty(),
			fixtureName +
			" restoration recovers the installed physical identity") && ok;
	}
	return ok;
}

bool beginDirectoryReplacementFixture(
	const std::filesystem::path& installedRoot,
	const std::filesystem::path& backupRoot,
	const std::filesystem::path& replacementTarget,
	std::string& errorMessage)
{
	namespace fs = std::filesystem;
	errorMessage.clear();
	std::error_code errorCode;
	fs::rename(installedRoot, backupRoot, errorCode);
	if (errorCode)
	{
		errorMessage = errorCode.message();
		return false;
	}

	std::string linkError;
	if (createSymbolicLinkFixture(
			replacementTarget, installedRoot, true, linkError))
	{
		return true;
	}

	errorCode.clear();
	fs::rename(backupRoot, installedRoot, errorCode);
	errorMessage = linkError;
	if (errorCode)
	{
		errorMessage += "; restore error " + errorCode.message();
	}
	return false;
}

bool endDirectoryReplacementFixture(
	const std::filesystem::path& installedRoot,
	const std::filesystem::path& backupRoot,
	std::string& errorMessage)
{
	namespace fs = std::filesystem;
	errorMessage.clear();
	std::error_code errorCode;
	const bool removed = fs::remove(installedRoot, errorCode);
	if (!removed || errorCode)
	{
		errorMessage = errorCode
			? errorCode.message()
			: "replacement link was absent";
		return false;
	}
	fs::rename(backupRoot, installedRoot, errorCode);
	if (errorCode)
	{
		errorMessage = errorCode.message();
		return false;
	}
	return true;
}

std::string readViaFile(const std::string& relativePath)
{
	std::unique_ptr<char[]> data;
	int len = File::readFile(relativePath, data);
	if (data == nullptr || len <= 0)
	{
		return "";
	}
	return std::string(data.get(), len);
}

std::string readViaCommonResourceFile(const std::string& relativePath)
{
	std::unique_ptr<char[]> data;
	int len = 0;
	if (!File::readCommonResourceFile(relativePath, data, len) || data == nullptr)
	{
		return "";
	}
	return std::string(data.get(), len);
}

std::string readViaActiveResourceFile(const std::string& relativePath)
{
	std::unique_ptr<char[]> data;
	int len = 0;
	if (!File::readActiveResourceFile(relativePath, data, len) || data == nullptr)
	{
		return "";
	}
	return std::string(data.get(), len);
}

std::string readViaBundledApplicationFile(const std::string& relativePath)
{
	std::unique_ptr<char[]> data;
	int len = 0;
	if (!File::readBundledApplicationFile(
			relativePath, data, len, 1024 * 1024) || data == nullptr)
	{
		return "";
	}
	return std::string(data.get(), len);
}

std::string readViaSharedApplicationFile(const std::string& relativePath)
{
	std::unique_ptr<char[]> data;
	int len = 0;
	if (!File::readSharedApplicationFile(relativePath, data, len, 1024 * 1024) ||
		data == nullptr)
	{
		return "";
	}
	return std::string(data.get(), len);
}

std::string normalizePath(std::string path)
{
	for (char& ch : path)
	{
		if (ch == '\\')
		{
			ch = '/';
		}
	}
	return path;
}

bool testSafeResourceTextFormatting(const std::filesystem::path& root)
{
	bool ok = true;
	const std::string longValue(5000, 'x');
	ok = check(convert::formatString("%s", longValue.c_str()) == longValue,
		"formatString supports output larger than the historical fixed buffer") && ok;
	std::string appended = "prefix:";
	convert::formatAppendString(appended, "%s", longValue.c_str());
	ok = check(appended == "prefix:" + longValue,
		"formatAppendString supports output larger than the historical fixed buffer") && ok;

	std::string formatted;
	ok = check(convert::formatIntegerValues(u8"生命:%d/%d（%d%%）", { 30, 50, 60 }, formatted) &&
		formatted == u8"生命:30/50（60%）",
		"integer data binding formatter accepts %d and escaped percent") && ok;
	ok = check(!convert::formatIntegerValues("%s", { 1 }, formatted) && formatted.empty(),
		"integer data binding formatter rejects string conversions") && ok;
	ok = check(!convert::formatIntegerValues("%n", { 1 }, formatted) && formatted.empty(),
		"integer data binding formatter rejects write conversions") && ok;
	ok = check(!convert::formatIntegerValues("%d/%d", { 1 }, formatted) && formatted.empty(),
		"integer data binding formatter rejects missing values") && ok;
	ok = check(!convert::formatIntegerValues("%d", { 1, 2 }, formatted) && formatted.empty(),
		"integer data binding formatter rejects unused values") && ok;
	ok = check(!convert::formatIntegerValues("%", {}, formatted) && formatted.empty(),
		"integer data binding formatter rejects a trailing percent") && ok;

	int parsedValue = 0;
	ok = check(convert::parseInteger("2147483647", parsedValue) && parsedValue == 2147483647,
		"resource integer parser accepts the maximum int") && ok;
	ok = check(!convert::parseInteger("2147483648", parsedValue),
		"resource integer parser rejects overflow without throwing") && ok;
	ok = check(!convert::parseInteger("12x", parsedValue),
		"resource integer parser rejects partial numbers") && ok;

	const std::filesystem::path talkIndexPath = root / "talkindex-overflow.txt";
	writeRawFile(talkIndexPath,
		"[1,2]valid\n"
		"[999999999999999999999999,3]overflow index\n"
		"[4,999999999999999999999999]overflow portrait\n"
		"[5,6]still valid\n");
	File::setAssetsCollectionRoot("");
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	TalkTextList talkTextList;
	talkTextList.load("talkindex-overflow.txt");
	ok = check(talkTextList.list.size() == 2 &&
		talkTextList.list[0].index == 1 && talkTextList.list[0].portraitIndex == 2 &&
		talkTextList.list[1].index == 5 && talkTextList.list[1].portraitIndex == 6,
		"talk index loader skips overflowing numeric fields and continues") && ok;
	File::setActiveResourceRoot("");
	File::setAssetsCollectionRoot("");
	File::setResourceFallbackRoots({});
	return ok;
}

bool testResourceReadPrefixPolicy()
{
	using ResourceReadPrefixPolicy::BundledRootMode;
	bool ok = true;
	std::vector<std::string> prefixes;

		ok = check(
		!ResourceReadPrefixPolicy::appendPrimaryPrefix(
			prefixes, "", BundledRootMode::FilesystemPath) &&
		prefixes.empty(),
		"filesystem-backed platforms reject an empty bundled resource root") &&
		ok;
	ok = check(
		ResourceReadPrefixPolicy::appendPrimaryPrefix(
			prefixes, "", BundledRootMode::AndroidAssetNamespace) &&
		prefixes.size() == 1 && prefixes.front().empty(),
		"Android accepts the empty prefix used by the APK asset namespace") &&
		ok;
	ok = check(
		!ResourceReadPrefixPolicy::appendPrimaryPrefix(
			prefixes, "", BundledRootMode::AndroidAssetNamespace) &&
		prefixes.size() == 1,
		"Android bundled resource prefixes remain unique") &&
		ok;
	ok = check(
		ResourceReadPrefixPolicy::appendUniquePrefix(
			prefixes, "common/", false) &&
		prefixes.size() == 2 && prefixes.back() == "common/",
		"non-empty fallback roots remain available on every platform") &&
		ok;
	ok = check(
		!ResourceReadPrefixPolicy::appendUniquePrefix(
			prefixes, "", false) &&
		prefixes.size() == 2,
		"fallback roots never accept an accidental empty path") &&
		ok;
	return ok;
}

bool testSharedApplicationFiles(const std::filesystem::path& root)
{
	bool ok = true;
	const std::filesystem::path collectionRoot = root / "SharedCollection";
	const std::filesystem::path activeRoot = collectionRoot / "ModPack";
	const std::filesystem::path collectionConfig = collectionRoot / "common" / "config" / "config.ini";
	const std::filesystem::path activeConfig = activeRoot / "common" / "config" / "config.ini";
	const std::string originalConfig =
		"[game]\n"
		"fullscreenmode=0\n"
		"unknownsetting=keep\n"
		"\n"
		"[extension]\n"
		"customkey=customvalue\n";
	writeRawFile(collectionConfig, originalConfig);
	writeRawFile(activeConfig, "[game]\nfullscreenmode=99\n");

	File::setAssetsCollectionRoot(collectionRoot.string());
	File::setActiveResourceRoot(activeRoot.string());
	File::setResourceFallbackRoots({});

	ok = check(readViaSharedApplicationFile(CONFIG_INI) == originalConfig,
		"shared application reads ignore the active resource package") && ok;
	const std::filesystem::path obsoleteUserConfig =
		root / "UserState" / "common" / "config" / "config.ini";
	const std::string obsoleteUserConfigText =
		originalConfig +
		"\n[obsolete]\nsource=user-profile\n";
	writeRawFile(obsoleteUserConfig, obsoleteUserConfigText);
	ok = check(
		readViaSharedApplicationFile(CONFIG_INI) == originalConfig,
		"global config ignores obsolete platform-user locations") && ok;

	std::unique_ptr<char[]> configData;
	int configLength = 0;
	ok = check(File::readSharedApplicationFile(CONFIG_INI, configData, configLength,
		1024 * 1024), "shared application config is readable") && ok;
	INIReader config(configData);
	config.SetInteger("game", "fullscreenmode", 1);
	const std::string updatedConfig = config.saveToString();
	ok = check(File::writeSharedApplicationFile(CONFIG_INI, updatedConfig.data(),
		static_cast<int>(updatedConfig.size())),
		"shared application config is writable while a resource package is active") && ok;

	std::unique_ptr<char[]> updatedData;
	int updatedLength = 0;
	ok = check(File::readSharedApplicationFile(CONFIG_INI, updatedData, updatedLength,
		1024 * 1024), "updated shared application config is readable") && ok;
	INIReader updatedIni(updatedData);
	ok = check(updatedIni.GetInteger("game", "fullscreenmode", 0) == 1 &&
		updatedIni.Get("game", "unknownsetting", "") == "keep" &&
		updatedIni.Get("extension", "customkey", "") == "customvalue" &&
		updatedIni.Get("obsolete", "source", "").empty(),
		"shared config update preserves unknown keys and sections") && ok;
	ok = check(readViaFile(CONFIG_INI) == "[game]\nfullscreenmode=99\n",
		"ordinary resource reads still use the active package") && ok;
	const std::filesystem::path userConfig =
		root / "save" / "config.ini";
	ok = check(std::filesystem::exists(userConfig) &&
		readRawFile(collectionConfig) == originalConfig,
		"global config writes to the independent state root and preserves the bundled fallback") && ok;
	writeRawFile(obsoleteUserConfig, "obsolete-user-changed");
	ok = check(
		readViaSharedApplicationFile(CONFIG_INI) == updatedConfig,
		"current config remains independent from obsolete user locations") && ok;
	const std::string sharedLogLine = "shared-user-log\n";
	File::appendSharedApplicationFile(
		"logs/runtime.log",
		sharedLogLine.data(),
		static_cast<int>(sharedLogLine.size()));
	ok = check(readRawFile(
			root / "UserState" / "logs" / "runtime.log") ==
			sharedLogLine,
		"relative runtime logs append below the user profile") && ok;

	const char traversalData[] = "escape";
	ok = check(!File::writeSharedApplicationFile("../escaped.ini", traversalData,
		static_cast<int>(sizeof(traversalData) - 1)),
		"shared application writes reject parent traversal") && ok;
	std::unique_ptr<char[]> traversalReadData;
	int traversalReadLength = 123;
	ok = check(!File::readSharedApplicationFile("../escaped.ini", traversalReadData,
		traversalReadLength) && traversalReadData == nullptr && traversalReadLength == 0,
		"shared application reads reject parent traversal and clear outputs") && ok;
	ok = check(!std::filesystem::exists(root / "escaped.ini"),
		"rejected shared traversal does not create an outside file") && ok;
	ok = check(readViaFile(CONFIG_INI) == "[game]\nfullscreenmode=99\n",
		"shared application writes never overwrite the active package config") && ok;

	File::setSharedApplicationRootUnavailableForTests(true);
	const char unavailableWrite[] = "must-not-reach-assets";
	ok = check(
		!File::writeSharedApplicationFile(
			"blocked/config.ini",
			unavailableWrite,
			static_cast<int>(
				sizeof(unavailableWrite) - 1)),
		"shared application writes fail closed when the platform provides no writable root") &&
		ok;
	File::appendSharedApplicationFile(
		"logs/unavailable.log",
		unavailableWrite,
		static_cast<int>(
			sizeof(unavailableWrite) - 1));
	ok = check(
		!std::filesystem::exists(
			collectionRoot / "blocked" /
				"config.ini") &&
			!std::filesystem::exists(
				collectionRoot / "logs" /
					"unavailable.log") &&
			readRawFile(collectionConfig) ==
				originalConfig &&
			readViaSharedApplicationFile(
				CONFIG_INI) == updatedConfig,
		"missing shared-state root preserves bundled assets while the independent global config remains available") &&
		ok;
	File::setSharedApplicationRootUnavailableForTests(false);

	File::setActiveResourceRoot("");
	File::setAssetsCollectionRoot("");
	File::setResourceFallbackRoots({});
	return ok;
}

bool testEditorRunFileLayout(const std::filesystem::path& root)
{
	namespace fs = std::filesystem;
	bool ok = true;
	std::size_t requiredSecurityFixtureCount = 0;
	const auto announceRequiredSecurityFixture =
		[&requiredSecurityFixtureCount](const std::string& name)
		{
			++requiredSecurityFixtureCount;
			std::cout << "REQUIRED_SECURITY_FIXTURE[" <<
				requiredSecurityFixtureCount << "]: " << name << std::endl;
	};
	File::resetEditorRunFileLayout();
	std::string uninstalledDiagnosticsPath = "must-clear";
	uint64_t uninstalledDiagnosticsGeneration = 0;
	ok = check(
		File::getEditorRunDiagnosticsPath(
			uninstalledDiagnosticsPath,
			uninstalledDiagnosticsGeneration) ==
			File::EditorRunFileLayoutState::NotInstalled &&
		uninstalledDiagnosticsPath.empty(),
		"structured diagnostics reports NotInstalled before editor-run setup") &&
		ok;

	const fs::path snapshotProbeRoot =
		root / "editor-run-snapshot-root-mtime";
	fs::create_directories(snapshotProbeRoot);
	std::error_code snapshotProbeError;
	const fs::file_time_type snapshotProbeWriteTime =
		fs::last_write_time(snapshotProbeRoot, snapshotProbeError);
	const std::vector<std::string> snapshotProbeBefore =
		snapshotDirectoryTreeWithWriteTimes(snapshotProbeRoot);
	if (!snapshotProbeError)
	{
		fs::last_write_time(
			snapshotProbeRoot,
			snapshotProbeWriteTime - std::chrono::hours(1),
			snapshotProbeError);
	}
	const std::vector<std::string> snapshotProbeAfter =
		snapshotDirectoryTreeWithWriteTimes(snapshotProbeRoot);
	ok = check(!snapshotProbeError &&
			snapshotProbeBefore.size() == 1 &&
			snapshotProbeBefore.front().rfind("D:.:", 0) == 0 &&
			snapshotProbeAfter.size() == 1 &&
			snapshotProbeAfter.front().rfind("D:.:", 0) == 0 &&
			snapshotProbeBefore != snapshotProbeAfter,
		"formal-tree snapshot records and detects the supplied root directory mtime") &&
		ok;
	fs::remove_all(snapshotProbeRoot, snapshotProbeError);

	const fs::path formalRoot = root / "EditorRunFormal";
	const fs::path activeRoot = formalRoot / "active";
	const fs::path dependencyRoot = formalRoot / "dependency";
	const fs::path commonRoot = formalRoot / "common";
	const fs::path assetsRoot = formalRoot / "assets-collection";
	const fs::path normalSaveRoot =
		root / "save" / "editor-normal";
	const fs::path uiDependencyRoot =
		formalRoot / "ui-dependency";
	const fs::path uiCommonRoot = formalRoot / "ui-common";
	const fs::path sessionRoot =
		root / fs::u8path(u8"editor run 中文 空格");
	const fs::path overlayRoot = sessionRoot / "overlay";
	const fs::path saveRoot = sessionRoot / "save";
	const fs::path applicationStateRoot = sessionRoot / "application-state";
	const fs::path diagnosticsRoot = sessionRoot / "diagnostics";
	const fs::path diagnosticsPath =
		diagnosticsRoot / fs::u8path(u8"events 诊断.jsonl");
	const fs::path logPath = diagnosticsRoot / fs::u8path(u8"game 运行.log");
	const fs::path runtimeTracePath =
		diagnosticsRoot /
			fs::u8path(u8"runtime-trace 运行.jsonl");
	const fs::path legacyLogPath = root / "legacy-explicit.log";
	const fs::path formalOverlayAttackRoot =
		formalRoot / "replacement-overlay-target";
	const fs::path formalSaveAttackRoot =
		formalRoot / "replacement-save-target";
	const fs::path formalApplicationAttackRoot =
		formalRoot / "replacement-application-target";
	const fs::path formalDiagnosticsAttackRoot =
		formalRoot / "replacement-diagnostics-target";
	const fs::path formalTransactionAttackRoot =
		formalRoot / "replacement-transaction-target";
	const fs::path formalLogAttackTarget =
		formalRoot / "replacement-log-target.txt";
	const fs::path formalCheckedWriteHardLinkTarget =
		formalRoot / "hardlink-checked-write-target.txt";
	const fs::path formalAppendHardLinkTarget =
		formalRoot / "hardlink-append-target.txt";
	const fs::path formalSharedWriteHardLinkTarget =
		formalRoot / "hardlink-shared-write-target.txt";
	const fs::path formalRemoveHardLinkTarget =
		formalRoot / "hardlink-remove-target.txt";
	const fs::path formalClearHardLinkTarget =
		formalRoot / "hardlink-clear-target.txt";
	const fs::path formalClearLinkTargetRoot =
		formalRoot / "clear-directory-link-target";
	const fs::path formalTransactionFileHardLinkTarget =
		formalRoot / "hardlink-transaction-file-target.txt";
	const fs::path formalTransactionReadyHardLinkTarget =
		formalRoot / "hardlink-transaction-ready-target.txt";
	const fs::path activeReadReplacementTarget =
		root / "editor-run-active-read-replacement";
	const fs::path dependencyReadReplacementTarget =
		root / "editor-run-dependency-read-replacement";
	const fs::path commonReadReplacementTarget =
		root / "editor-run-common-read-replacement";
	const fs::path uiDependencyReadReplacementTarget =
		root / "editor-run-ui-dependency-read-replacement";
	const fs::path uiCommonReadReplacementTarget =
		root / "editor-run-ui-common-read-replacement";
	const fs::path setterReplacementTarget =
		root / "editor-run-setter-replacement";
	const fs::path formalDescendantLinkTargetA =
		root / "editor-run-formal-descendant-target-a";
	const fs::path formalDescendantLinkTargetB =
		root / "editor-run-formal-descendant-target-b";
	const fs::path formalFinalFileLinkTarget =
		root / "editor-run-formal-final-file-target.txt";
	fs::create_directories(overlayRoot);
	fs::create_directories(saveRoot);
	fs::create_directories(applicationStateRoot);
	fs::create_directories(diagnosticsRoot);

	writeRawFile(activeRoot / "config" / "order.txt", "active");
	writeRawFile(activeRoot / "config" / "active-first.txt", "active-first");
	writeRawFile(activeRoot / "config" / "remove.txt", "formal-remove");
	writeRawFile(activeRoot / "config" / "clear" / "formal.txt", "formal-clear");
	writeRawFile(activeRoot / "save" / "game" / "formal-only.ini", "formal-save");
	writeRawFile(activeRoot / "save" / "rpg1" / "game.ini", "formal-slot-one");
	writeRawFile(activeRoot / "save" / "rpg2" / "game.ini", "formal-slot-two");
	writeRawFile(dependencyRoot / "config" / "dependency-only.txt", "dependency");
	writeRawFile(commonRoot / "config" / "common-only.txt", "common");
	writeRawFile(commonRoot / "config" / "config.ini", "formal-shared");
	writeRawFile(
		assetsRoot / "config" / "assets-only.txt",
		"assets-only");
	writeRawFile(
		uiDependencyRoot /
			"ini" / "ui" / "ui-dependency-only.txt",
		"ui-dependency");
	writeRawFile(
		uiCommonRoot /
			"ini" / "ui" / "ui-common-only.txt",
		"ui-common");
	writeRawFile(formalOverlayAttackRoot / "attack-remove.txt", "keep-remove");
	writeRawFile(
		formalOverlayAttackRoot / "config" / "order.txt",
		"formal-read-attack");
	writeRawFile(formalOverlayAttackRoot / "attack-clear" / "keep.txt", "keep-clear");
	writeRawFile(formalOverlayAttackRoot / "attack-source" / "source.txt", "keep-source");
	writeRawFile(formalSaveAttackRoot / "game" / "formal-sentinel.ini", "keep-save");
	writeRawFile(formalApplicationAttackRoot / "config.ini", "keep-application");
	writeRawFile(formalDiagnosticsAttackRoot / "keep.txt", "keep-diagnostics");
	writeRawFile(formalTransactionAttackRoot /
		"race-before-backup" / "sentinel.txt", "keep-before-backup-destination");
	writeRawFile(formalTransactionAttackRoot /
		".jxqy-race-before-backup-staging" / "sentinel.txt",
		"keep-before-backup-staging");
	writeRawFile(formalTransactionAttackRoot /
		".jxqy-race-before-backup-backup" / "sentinel.txt",
		"keep-before-backup-backup");
	writeRawFile(formalTransactionAttackRoot /
		".jxqy-race-before-backup-staging-ready",
		"keep-before-backup-ready");
	writeRawFile(formalTransactionAttackRoot /
		"race-before-publish" / "sentinel.txt", "keep-before-publish-destination");
	writeRawFile(formalTransactionAttackRoot /
		".jxqy-race-before-publish-staging" / "sentinel.txt",
		"keep-before-publish-staging");
	writeRawFile(formalTransactionAttackRoot /
		".jxqy-race-before-publish-backup" / "sentinel.txt",
		"keep-before-publish-backup");
	writeRawFile(formalTransactionAttackRoot /
		".jxqy-race-before-publish-staging-ready",
		"keep-before-publish-ready");
	writeRawFile(formalLogAttackTarget, "keep-log-target");
	writeRawFile(formalCheckedWriteHardLinkTarget,
		"keep-checked-write-target");
	writeRawFile(formalAppendHardLinkTarget,
		"keep-append-target");
	writeRawFile(formalSharedWriteHardLinkTarget,
		"keep-shared-write-target");
	writeRawFile(formalRemoveHardLinkTarget,
		"keep-remove-target");
	writeRawFile(formalClearHardLinkTarget,
		"keep-clear-target");
	writeRawFile(formalClearLinkTargetRoot / "sentinel.txt",
		"keep-clear-directory-target");
	writeRawFile(formalTransactionFileHardLinkTarget,
		"keep-transaction-file-target");
	writeRawFile(formalTransactionReadyHardLinkTarget,
		"keep-transaction-ready-target");
	writeRawFile(
		activeReadReplacementTarget /
			"config/active-first.txt",
		"outside-active");
	writeRawFile(
		dependencyReadReplacementTarget /
			"config/dependency-only.txt",
		"outside-dependency");
	writeRawFile(
		commonReadReplacementTarget /
			"config/common-only.txt",
		"outside-common");
	writeRawFile(
		uiDependencyReadReplacementTarget /
			"ini/ui/ui-dependency-only.txt",
		"outside-ui-dependency");
	writeRawFile(
		uiCommonReadReplacementTarget /
			"ini/ui/ui-common-only.txt",
		"outside-ui-common");
	writeRawFile(
		setterReplacementTarget /
			"config/active-first.txt",
		"setter-outside-active");
	writeRawFile(
		formalDescendantLinkTargetA / "current.txt",
		"formal-link-a");
	writeRawFile(
		formalDescendantLinkTargetA / "CaseOnly.TXT",
		"formal-case-a");
	writeRawFile(
		formalDescendantLinkTargetA /
			fs::u8path(u8"tm910-正确.asf"),
		"formal-alias-a");
	writeRawFile(
		formalDescendantLinkTargetA / "only-a.txt",
		"only-a");
	writeRawFile(
		formalDescendantLinkTargetB / "current.txt",
		"formal-link-b");
	writeRawFile(
		formalDescendantLinkTargetB / "CaseOnly.TXT",
		"formal-case-b");
	writeRawFile(
		formalDescendantLinkTargetB /
			fs::u8path(u8"tm910-正确.asf"),
		"formal-alias-b");
	writeRawFile(
		formalDescendantLinkTargetB / "only-b.txt",
		"only-b");
	writeRawFile(
		formalFinalFileLinkTarget,
		"formal-final-link");

	writeRawFile(overlayRoot / "config" / "order.txt", "overlay");
	writeRawFile(overlayRoot / "config" / "overlay-second.txt", "overlay-second");
	writeRawFile(overlayRoot / "config" / "copy-source.txt", "copy-source");
	writeRawFile(overlayRoot / "config" / "remove.txt", "overlay-remove");
	writeRawFile(overlayRoot / "config" / "clear" / "session.txt", "session-clear");
	writeRawFile(saveRoot / "game" / "session-only.ini", "session-save");
	writeRawFile(saveRoot / "game" / "game.ini", "session-game");
	writeRawFile(saveRoot / "game" / "player.ini", "session-player");
	writeRawFile(saveRoot / "rpg1" / "stale.ini", "stale");

	File::setAssetsCollectionRoot(assetsRoot.u8string());
	File::setActiveResourceRoot(activeRoot.u8string());
	File::setActiveSaveNamespace("editor-normal");
	File::setCommonResourceRoot(commonRoot.u8string());
	File::setResourceFallbackRoots({
		dependencyRoot.u8string(),
		commonRoot.u8string()
	});
	File::setUiResourceFallbackRoots({
			uiDependencyRoot.u8string()
		}, true, uiCommonRoot.u8string());
	GameLog::setLogFilePath(legacyLogPath.u8string());
	GameLog::use_log_file = false;

	File::EditorRunFileLayout layout = {
		overlayRoot.u8string(),
		saveRoot.u8string(),
		applicationStateRoot.u8string(),
		diagnosticsRoot.u8string(),
		diagnosticsPath.u8string(),
		logPath.u8string(),
		runtimeTracePath.u8string()
	};

	File::EditorRunFileLayout invalidLayout = layout;
	invalidLayout.overlayRoot = "relative-overlay";
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout) &&
		!File::hasEditorRunFileLayout(),
		"editor-run layout rejects a relative output root without partial installation") && ok;

	const fs::path missingRoot = sessionRoot / "missing";
	invalidLayout = layout;
	invalidLayout.overlayRoot = missingRoot.u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout) &&
		!fs::exists(missingRoot),
		"editor-run layout installation never creates a missing root") && ok;

	invalidLayout = layout;
	invalidLayout.isolatedSaveRoot = overlayRoot.u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects equal output roots on every platform") && ok;
	const fs::path outputIdentitySwapTemporary =
		sessionRoot / "output-identity-swap-temporary";
	bool outputIdentitySwapExecuted = false;
	std::string outputIdentitySwapError;
	File::setEditorRunFileOperationTestHook(
		[&](File::EditorRunFileOperationPhase phase)
		{
			if (phase != File::EditorRunFileOperationPhase::
					AfterLayoutOverlayIdentityCapture ||
				outputIdentitySwapExecuted)
			{
				return;
			}
			outputIdentitySwapExecuted = swapDirectoryNames(
				overlayRoot,
				saveRoot,
				outputIdentitySwapTemporary,
				outputIdentitySwapError);
		});
	const bool physicalDuplicateLayoutInstalled =
		File::installEditorRunFileLayoutForTests(layout);
	File::setEditorRunFileOperationTestHook({});
	ok = check(
			outputIdentitySwapExecuted &&
			!physicalDuplicateLayoutInstalled &&
			!File::hasEditorRunFileLayout(),
		"editor-run layout rejects pairwise-equal output file identities after a deterministic directory-identity swap: " +
			outputIdentitySwapError) &&
		ok;
	if (outputIdentitySwapExecuted)
	{
		std::string restoreError;
		const bool restored = swapDirectoryNames(
			overlayRoot,
			saveRoot,
			outputIdentitySwapTemporary,
			restoreError);
		ok = check(restored &&
				readRawFile(overlayRoot / "config" / "order.txt") ==
					"overlay" &&
				readRawFile(saveRoot / "game" / "game.ini") ==
					"session-game",
			"output identity-swap fixture restores both output directory names: " +
				restoreError) &&
			ok;
	}
	const fs::path nestedRoot = overlayRoot / "nested";
	fs::create_directories(nestedRoot);
	invalidLayout = layout;
	invalidLayout.isolatedSaveRoot = nestedRoot.u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects nested output roots") && ok;
	invalidLayout = layout;
	invalidLayout.overlayRoot = activeRoot.u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects an output root overlapping formal content") && ok;
	invalidLayout = layout;
	invalidLayout.diagnosticsPath =
		(root / "outside-diagnostics.jsonl").u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects a diagnostics path outside diagnostics") && ok;
	invalidLayout = layout;
	invalidLayout.diagnosticsPath = layout.logPath;
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects identical diagnostics and log paths") && ok;
	invalidLayout = layout;
	invalidLayout.runtimeTracePath =
		layout.diagnosticsPath;
	ok = check(
		!File::installEditorRunFileLayoutForTests(
			invalidLayout),
		"editor-run layout rejects a runtime trace path colliding with diagnostics") &&
		ok;
#if defined(_WIN32)
	invalidLayout = layout;
	invalidLayout.diagnosticsPath =
		(diagnosticsRoot /
			fs::u8path(u8"Ä-events.jsonl")).u8string();
	invalidLayout.logPath =
		(diagnosticsRoot /
			fs::u8path(u8"ä-events.jsonl")).u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects Unicode case-equivalent diagnostics and log leaves on Windows") &&
		ok;
#endif
	invalidLayout = layout;
	invalidLayout.logPath = (root / "outside.log").u8string();
	ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
		"editor-run layout rejects a log path outside diagnostics") && ok;
	invalidLayout = layout;
	invalidLayout.runtimeTracePath =
		(root / "outside-runtime-trace.jsonl").
			u8string();
	ok = check(
		!File::installEditorRunFileLayoutForTests(
			invalidLayout),
		"editor-run layout rejects a runtime trace path outside diagnostics") &&
		ok;

	const fs::path aliasTarget = root / "editor-run-alias-target";
	const fs::path aliasPath = root / "editor-run-alias";
	fs::create_directories(aliasTarget);
	std::error_code aliasError;
	fs::create_directory_symlink(aliasTarget, aliasPath, aliasError);
	if (!aliasError)
	{
		invalidLayout = layout;
		invalidLayout.overlayRoot = aliasPath.u8string();
		ok = check(!File::installEditorRunFileLayoutForTests(invalidLayout),
			"editor-run layout rejects a symlink output root") && ok;
	}

	ok = check(File::installEditorRunFileLayoutForTests(layout) &&
		File::hasEditorRunFileLayout(),
		"editor-run layout installs once after validation") && ok;
	ok = check(!File::installEditorRunFileLayoutForTests(layout),
		"editor-run layout rejects repeated installation") && ok;

	const fs::path fileExistDirectory =
		overlayRoot / "file-exist" / "real-directory";
	const fs::path fileExistRegularFile =
		overlayRoot / "file-exist" / "regular.txt";
	const fs::path fileExistFileTarget =
		root / "editor-run-file-exist-file-target.txt";
	const fs::path fileExistDirectoryTarget =
		root / "editor-run-file-exist-directory-target";
	const fs::path fileExistFileLink =
		overlayRoot / "file-exist" / "file-link.txt";
	const fs::path fileExistDirectoryLink =
		overlayRoot / "file-exist" / "directory-link";
	fs::create_directories(fileExistDirectory);
	fs::create_directories(fileExistDirectoryTarget);
	writeRawFile(fileExistRegularFile, "regular");
	writeRawFile(fileExistFileTarget, "file-link-target");
	writeRawFile(
		fileExistDirectoryTarget / "sentinel.txt",
		"directory-link-target");
	std::string fileExistFileLinkError;
	std::string fileExistDirectoryLinkError;
	const bool fileExistFileLinkCreated =
		createSymbolicLinkFixture(
			fileExistFileTarget,
			fileExistFileLink,
			false,
			fileExistFileLinkError);
	const bool fileExistDirectoryLinkCreated =
		createSymbolicLinkFixture(
			fileExistDirectoryTarget,
			fileExistDirectoryLink,
			true,
			fileExistDirectoryLinkError);
	ok = check(fileExistFileLinkCreated,
		"editor-run fileExist file-link fixture is required: " +
			fileExistFileLinkError) && ok;
	ok = check(fileExistDirectoryLinkCreated,
		"editor-run fileExist directory-link fixture is required: " +
			fileExistDirectoryLinkError) && ok;
	ok = check(
			File::fileExist("file-exist/regular.txt") &&
			File::fileExist("file-exist/real-directory") &&
			!File::fileExist("file-exist/file-link.txt") &&
			!File::fileExist("file-exist/directory-link") &&
			File::listFiles("file-exist/directory-link").empty(),
		"editor-run private overlay reads accept regular entries while rejecting file and directory links") &&
		ok;
	writeRawFile(
		activeRoot / "file-exist" / "file-link.txt",
		"formal-file-link-fallback");
	writeRawFile(
		activeRoot / "file-exist" /
			"directory-link" / "sentinel.txt",
		"formal-directory-link-fallback");
	ok = check(
			File::getAssetsName(
				"file-exist/file-link.txt").empty() &&
			File::getAssetsName(
				"file-exist/directory-link/sentinel.txt").empty(),
		"editor-run private overlay media links fail closed instead of falling through to same-name formal resources") &&
		ok;
	std::error_code fileExistCleanupError;
	if (fileExistFileLinkCreated)
	{
		fs::remove(fileExistFileLink, fileExistCleanupError);
		ok = check(!fileExistCleanupError,
			"editor-run fileExist file-link fixture cleans up") && ok;
	}
	fileExistCleanupError.clear();
	if (fileExistDirectoryLinkCreated)
	{
		fs::remove(fileExistDirectoryLink, fileExistCleanupError);
		ok = check(!fileExistCleanupError,
			"editor-run fileExist directory-link fixture cleans up") && ok;
	}
	ok = check(
			readRawFile(fileExistFileTarget) == "file-link-target" &&
			readRawFile(fileExistDirectoryTarget / "sentinel.txt") ==
				"directory-link-target",
		"editor-run fileExist link probes never read or modify link targets") &&
		ok;

	const fs::path formalDescendantLink =
		activeRoot / "asf" / "formal-current";
	const fs::path formalFinalFileLink =
		activeRoot / "config" / "formal-final-link.txt";
	fs::create_directories(formalDescendantLink.parent_path());
	std::string formalDescendantLinkError;
	std::string formalFinalFileLinkError;
	bool formalDescendantLinkCreated =
		createSymbolicLinkFixture(
			formalDescendantLinkTargetA,
			formalDescendantLink,
			true,
			formalDescendantLinkError);
	const bool formalFinalFileLinkCreated =
		createSymbolicLinkFixture(
			formalFinalFileLinkTarget,
			formalFinalFileLink,
			false,
			formalFinalFileLinkError);
	ok = check(formalDescendantLinkCreated,
		"formal resource descendant-directory link fixture is required: " +
			formalDescendantLinkError) && ok;
	ok = check(formalFinalFileLinkCreated,
		"formal resource final-file link fixture is required: " +
			formalFinalFileLinkError) && ok;
	if (formalDescendantLinkCreated && formalFinalFileLinkCreated)
	{
		const std::vector<std::string> linkedFilesA =
			File::listFiles("asf/formal-current");
		const std::string linkedAssetPathA =
			File::getAssetsName(
				"asf/formal-current/current.txt");
		const std::string finalLinkedAssetPath =
			File::getAssetsName(
				"config/formal-final-link.txt");
		ok = check(
				readViaFile("asf/formal-current/current.txt") ==
						"formal-link-a" &&
					readViaActiveResourceFile(
						"asf/formal-current/current.txt") ==
						"formal-link-a" &&
					File::fileExist(
						"asf/formal-current/current.txt") &&
					File::activeResourceFileExist(
						"asf/formal-current/current.txt") &&
					readViaFile(
						"asf/formal-current/caseonly.txt") ==
						"formal-case-a" &&
					readViaFile(
						u8"asf/formal-current/tm910-丢失.asf") ==
						"formal-alias-a" &&
					std::find(
						linkedFilesA.begin(),
						linkedFilesA.end(),
						"only-a.txt") !=
						linkedFilesA.end() &&
					readViaFile(
						"config/formal-final-link.txt") ==
						"formal-final-link" &&
					File::fileExist(
						"config/formal-final-link.txt") &&
					File::activeResourceFileExist(
						"config/formal-final-link.txt") &&
					normalizePath(linkedAssetPathA) ==
						normalizePath(
							(formalDescendantLink /
								"current.txt").u8string()) &&
					readRawFile(
						fs::u8path(linkedAssetPathA)) ==
						"formal-link-a" &&
					normalizePath(finalLinkedAssetPath) ==
						normalizePath(
							formalFinalFileLink.u8string()) &&
					readRawFile(
						fs::u8path(finalLinkedAssetPath)) ==
						"formal-final-link",
			"installed editor-run reads and media paths follow current formal directory and file links when the private overlay is missing") &&
			ok;

		std::error_code retargetError;
		const bool oldLinkRemoved =
			fs::remove(formalDescendantLink, retargetError);
		ok = check(oldLinkRemoved && !retargetError,
			"formal resource descendant link removes before retarget") &&
			ok;
		formalDescendantLinkCreated = false;
		if (oldLinkRemoved && !retargetError)
		{
			formalDescendantLinkError.clear();
			formalDescendantLinkCreated =
				createSymbolicLinkFixture(
					formalDescendantLinkTargetB,
					formalDescendantLink,
					true,
					formalDescendantLinkError);
			ok = check(formalDescendantLinkCreated,
				"formal resource descendant link retargets to B: " +
					formalDescendantLinkError) && ok;
		}
		if (formalDescendantLinkCreated)
		{
			const std::vector<std::string> linkedFilesB =
				File::listFiles("asf/formal-current");
			const std::string linkedAssetPathB =
				File::getAssetsName(
					"asf/formal-current/current.txt");
			ok = check(
					readViaFile(
						"asf/formal-current/current.txt") ==
							"formal-link-b" &&
						readViaActiveResourceFile(
							"asf/formal-current/current.txt") ==
							"formal-link-b" &&
						File::fileExist(
							"asf/formal-current/current.txt") &&
						File::activeResourceFileExist(
							"asf/formal-current/current.txt") &&
						readViaFile(
							"asf/formal-current/caseonly.txt") ==
							"formal-case-b" &&
						readViaFile(
							u8"asf/formal-current/tm910-丢失.asf") ==
							"formal-alias-b" &&
						std::find(
							linkedFilesB.begin(),
							linkedFilesB.end(),
							"only-b.txt") !=
							linkedFilesB.end() &&
						std::find(
							linkedFilesB.begin(),
							linkedFilesB.end(),
							"only-a.txt") ==
							linkedFilesB.end() &&
						normalizePath(linkedAssetPathB) ==
							normalizePath(
								(formalDescendantLink /
									"current.txt").u8string()) &&
						readRawFile(
							fs::u8path(linkedAssetPathB)) ==
							"formal-link-b",
				"the same formal resource and media link immediately exposes target B without reinstalling the run layout") &&
				ok;
		}
	}
	std::error_code formalLinkCleanupError;
	if (formalDescendantLinkCreated)
	{
		fs::remove(
			formalDescendantLink,
			formalLinkCleanupError);
		ok = check(!formalLinkCleanupError,
			"formal descendant-directory link fixture cleans up") &&
			ok;
	}
	formalLinkCleanupError.clear();
	if (formalFinalFileLinkCreated)
	{
		fs::remove(
			formalFinalFileLink,
			formalLinkCleanupError);
		ok = check(!formalLinkCleanupError,
			"formal final-file link fixture cleans up") &&
			ok;
	}

	const std::vector<std::string> formalBefore =
		snapshotDirectoryTreeWithWriteTimes(formalRoot);
	const fs::path hookDiagnosticsBackup =
		sessionRoot / "hook-diagnostics-installed";
	bool logHookExecuted = false;
	std::string logHookError;
	File::setEditorRunFileOperationTestHook(
		[&](File::EditorRunFileOperationPhase phase)
		{
			if (phase != File::EditorRunFileOperationPhase::BeforeLogParentOpen ||
				logHookExecuted)
			{
				return;
			}
			logHookExecuted = beginDirectoryReplacementFixture(
				diagnosticsRoot, hookDiagnosticsBackup,
				formalDiagnosticsAttackRoot, logHookError);
		});
	GameLog::write("log parent final-check race must fail closed");
	File::setEditorRunFileOperationTestHook({});
	ok = check(logHookExecuted &&
		!fs::exists(logPath) &&
		!fs::exists(formalDiagnosticsAttackRoot / logPath.filename()),
		"deterministic log-parent hook blocks redirected output: " +
			logHookError) && ok;
	if (logHookExecuted)
	{
		announceRequiredSecurityFixture(
			"editor-run log-parent final-check race");
		std::string restoreError;
		ok = check(endDirectoryReplacementFixture(
				diagnosticsRoot, hookDiagnosticsBackup, restoreError),
			"log-parent hook restores diagnostics: " + restoreError) && ok;
	}

	const fs::path hookStructuredDiagnosticsBackup =
		sessionRoot / "hook-structured-diagnostics-installed";
	bool structuredDiagnosticsHookExecuted = false;
	std::string structuredDiagnosticsHookError;
	File::setEditorRunFileOperationTestHook(
		[&](File::EditorRunFileOperationPhase phase)
		{
			if (phase != File::EditorRunFileOperationPhase::
					BeforeDiagnosticsParentOpen ||
				structuredDiagnosticsHookExecuted)
			{
				return;
			}
			structuredDiagnosticsHookExecuted =
				beginDirectoryReplacementFixture(
					diagnosticsRoot,
					hookStructuredDiagnosticsBackup,
					formalDiagnosticsAttackRoot,
					structuredDiagnosticsHookError);
		});
	std::string hookedDiagnosticsPath;
	uint64_t hookedDiagnosticsGeneration = 0;
	std::FILE* hookedDiagnosticsFile = nullptr;
	std::intptr_t hookedDiagnosticsParent = -1;
	const bool hookedDiagnosticsOpened =
		File::getEditorRunDiagnosticsPath(
			hookedDiagnosticsPath,
			hookedDiagnosticsGeneration) ==
			File::EditorRunFileLayoutState::Valid &&
		File::openEditorRunDiagnostics(
			hookedDiagnosticsPath,
			hookedDiagnosticsGeneration,
			hookedDiagnosticsFile,
			hookedDiagnosticsParent);
	File::setEditorRunFileOperationTestHook({});
	if (hookedDiagnosticsFile != nullptr)
	{
		std::fclose(hookedDiagnosticsFile);
	}
	File::closeEditorRunDiagnosticsParent(
		hookedDiagnosticsParent);
	ok = check(structuredDiagnosticsHookExecuted &&
		!hookedDiagnosticsOpened &&
		!fs::exists(diagnosticsPath) &&
		!fs::exists(
			formalDiagnosticsAttackRoot /
				diagnosticsPath.filename()),
		"deterministic structured-diagnostics parent hook blocks redirected output: " +
			structuredDiagnosticsHookError) && ok;
	if (structuredDiagnosticsHookExecuted)
	{
		announceRequiredSecurityFixture(
			"editor-run structured-diagnostics parent final-check race");
		std::string restoreError;
		ok = check(endDirectoryReplacementFixture(
				diagnosticsRoot,
				hookStructuredDiagnosticsBackup,
				restoreError),
			"structured-diagnostics hook restores diagnostics: " +
				restoreError) && ok;
	}

	const fs::path hookOverlayWriteBackup =
		sessionRoot / "hook-overlay-write-installed";
	bool writeHookExecuted = false;
	std::string writeHookError;
	File::setEditorRunFileOperationTestHook(
		[&](File::EditorRunFileOperationPhase phase)
		{
			if (phase != File::EditorRunFileOperationPhase::BeforeWriteRootOpen ||
				writeHookExecuted)
			{
				return;
			}
			writeHookExecuted = beginDirectoryReplacementFixture(
				overlayRoot, hookOverlayWriteBackup,
				formalOverlayAttackRoot, writeHookError);
		});
	const char hookWriteData[] = "hook-write";
	const bool hookWriteResult = File::writeFileChecked(
		"hook-mkdir/deep/output.txt", hookWriteData,
		static_cast<int>(sizeof(hookWriteData) - 1));
	File::setEditorRunFileOperationTestHook({});
	ok = check(writeHookExecuted && !hookWriteResult &&
		!fs::exists(formalOverlayAttackRoot /
			"hook-mkdir/deep/output.txt"),
		"deterministic mkdir hook blocks formal parent creation: " +
			writeHookError) && ok;
	if (writeHookExecuted)
	{
		announceRequiredSecurityFixture(
			"editor-run mkdir final-check race");
		std::string restoreError;
		ok = check(endDirectoryReplacementFixture(
				overlayRoot, hookOverlayWriteBackup, restoreError),
			"mkdir hook restores overlay: " + restoreError) && ok;
	}
	ok = check(snapshotDirectoryTreeWithWriteTimes(formalRoot) ==
			formalBefore,
		"deterministic log/read/mkdir fixtures preserve formal bytes and mtimes") &&
		ok;
	const std::vector<std::pair<fs::path, std::string>>
		ordinaryReplacementFixtures = {
			{ overlayRoot,
				"editor-run overlay ordinary same-path root replacement" },
			{ saveRoot,
				"editor-run save ordinary same-path root replacement" },
			{ applicationStateRoot,
				"editor-run application-state ordinary same-path root replacement" },
			{ diagnosticsRoot,
				"editor-run diagnostics ordinary same-path root replacement" }
		};
	for (const auto& fixture : ordinaryReplacementFixtures)
	{
		const bool fixtureOk = runOrdinaryDirectoryReplacementFixture(
			fixture.first, fixture.second);
		if (fixtureOk)
		{
			announceRequiredSecurityFixture(fixture.second);
		}
		ok = fixtureOk && ok;
	}
	ok = check(snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
		"ordinary same-path root replacements leave formal content unchanged") &&
		ok;

	const auto verifyFormalRootReplacement =
		[&overlayRoot](
			const std::string& resourceName,
			const std::string& expectedContent,
			const fs::path& replacementTarget)
		{
			std::string editorRunLogPath;
			const char output[] = "private-output";
			return check(
				File::getEditorRunLogPath(
					editorRunLogPath) ==
						File::EditorRunFileLayoutState::Valid &&
					!editorRunLogPath.empty() &&
					readViaFile(resourceName) ==
						expectedContent &&
					File::writeFileChecked(
						"formal-replacement-output.txt",
						output,
						static_cast<int>(
							sizeof(output) - 1)) &&
					readRawFile(
						overlayRoot /
							"formal-replacement-output.txt") ==
						"private-output" &&
					!fs::exists(
						replacementTarget /
							"formal-replacement-output.txt"),
				"a replaced formal root exposes current content while writes stay private");
		};
	const std::vector<std::tuple<
		fs::path, fs::path, std::string, std::string,
		std::string>>
		formalReadReplacementFixtures = {
			{
				activeRoot,
				activeReadReplacementTarget,
				"editor-run active formal root replacement",
				"config/active-first.txt",
				"outside-active"
			},
			{
				dependencyRoot,
				dependencyReadReplacementTarget,
				"editor-run dependency formal root replacement",
				"config/dependency-only.txt",
				"outside-dependency"
			},
			{
				commonRoot,
				commonReadReplacementTarget,
				"editor-run Common formal root replacement",
				"config/common-only.txt",
				"outside-common"
			},
			{
				uiDependencyRoot,
				uiDependencyReadReplacementTarget,
				"editor-run UI dependency-only formal root replacement",
				"ini/ui/ui-dependency-only.txt",
				"outside-ui-dependency"
			},
			{
				uiCommonRoot,
				uiCommonReadReplacementTarget,
				"editor-run UI common-only formal root replacement",
				"ini/ui/ui-common-only.txt",
				"outside-ui-common"
			}
		};
	for (const auto& fixture : formalReadReplacementFixtures)
	{
		const bool fixtureOk = runDirectoryReplacementFixture(
			std::get<0>(fixture),
			std::get<1>(fixture),
			std::get<2>(fixture),
			[&verifyFormalRootReplacement, &fixture]()
			{
				return verifyFormalRootReplacement(
					std::get<3>(fixture),
					std::get<4>(fixture),
					std::get<1>(fixture));
			},
			false);
		if (fixtureOk)
		{
			announceRequiredSecurityFixture(
				std::get<2>(fixture));
		}
		ok = fixtureOk &&
			check(
				snapshotDirectoryTreeWithWriteTimes(formalRoot) ==
					formalBefore,
				"restoring a formal root restores the exact formal tree") &&
			ok;
	}

	File::setActiveResourceRoot(
		setterReplacementTarget.u8string());
	std::string setterLogPath;
	const bool setterFixtureOk = check(
		File::getEditorRunLogPath(setterLogPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!setterLogPath.empty() &&
			readViaFile("config/active-first.txt") ==
				"active-first",
		"post-install setter changes do not invalidate or retarget the installed selection");
	if (setterFixtureOk)
	{
		announceRequiredSecurityFixture(
			"editor-run post-install formal root setter change");
	}
	ok = setterFixtureOk && ok;
	File::setActiveResourceRoot(activeRoot.u8string());
	setterLogPath.clear();
	ok = check(
		File::getEditorRunLogPath(setterLogPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!setterLogPath.empty() &&
			readViaFile("config/active-first.txt") ==
				"active-first",
		"restoring the process-global setter leaves the installed selection unchanged") &&
		ok;

	const char tamperData[] = "tamper";
	ok = runDirectoryReplacementFixture(
		overlayRoot,
		formalOverlayAttackRoot,
		"editor-run overlay root replacement",
		[&]()
		{
			bool fixtureOk = true;
			auto activeData = std::make_unique<char[]>(1);
			int activeLength = 1;
			auto commonData = std::make_unique<char[]>(1);
			int commonLength = 1;
			fixtureOk = check(
				!File::activeResourceFileExist("config/active-first.txt") &&
				!File::readActiveResourceFile(
					"config/active-first.txt", activeData, activeLength) &&
				activeData == nullptr && activeLength == 0 &&
				!File::readCommonResourceFile(
					"config/common-only.txt", commonData, commonLength) &&
				commonData == nullptr && commonLength == 0,
				"invalid installed layout blocks every explicit formal-resource read") &&
				fixtureOk;
			fixtureOk = check(readViaFile("config/active-first.txt").empty(),
				"invalid overlay identity blocks reads instead of falling back to formal content") &&
				fixtureOk;
			fixtureOk = check(File::resolveFirstExistingResource({
					"config/active-first.txt"
				}).empty(),
				"invalid overlay identity blocks candidate resolution") && fixtureOk;
			bool visitorCalled = false;
			fixtureOk = check(!File::visitReadableResources({
					"config/active-first.txt"
				},
				[&visitorCalled](const std::string&,
					std::unique_ptr<char[]>&, int)
				{
					visitorCalled = true;
					return true;
				}) && !visitorCalled,
				"invalid overlay identity blocks readable-resource visitors") && fixtureOk;
			fixtureOk = check(File::listFiles("config").empty(),
				"invalid overlay identity blocks directory reads") && fixtureOk;
			fixtureOk = check(!File::writeFileChecked(
					"attack-checked.txt", tamperData, 6),
				"invalid overlay identity blocks checked ordinary writes") && fixtureOk;
			File::writeFile("attack-void.txt", tamperData, 6);
			File::appendFile("attack-append.txt", tamperData, 6);
			File::copy("attack-source/source.txt", "attack-copy.txt");
			fixtureOk = check(!File::removeFile("attack-remove.txt") &&
				!File::clearDirectoryFiles("attack-clear"),
				"invalid overlay identity blocks ordinary remove and clear operations") &&
				fixtureOk;
			fixtureOk = check(!File::copyDirectoryFiles(
					"attack-source/", "attack-destination/") &&
				!File::recoverDirectoryCopy("attack-destination/"),
				"invalid overlay identity blocks directory transactions") && fixtureOk;
			fixtureOk = check(!File::writeSharedApplicationFile(
					CONFIG_INI, tamperData, 6),
				"one invalid installed output root blocks shared-state writes") && fixtureOk;
			GameLog::write("invalid overlay identity must not use a legacy log");
			fixtureOk = check(!fs::exists(legacyLogPath) &&
				snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
				"invalid overlay identity swallows void operations and leaves formal content unchanged") &&
				fixtureOk;
			return fixtureOk;
		}) && ok;
	std::unique_ptr<char[]> restoredActiveData;
	int restoredActiveLength = 0;
	std::unique_ptr<char[]> restoredCommonData;
	int restoredCommonLength = 0;
	ok = check(File::activeResourceFileExist("config/active-first.txt") &&
		File::readActiveResourceFile(
			"config/active-first.txt", restoredActiveData, restoredActiveLength) &&
		std::string(restoredActiveData.get(), restoredActiveLength) == "active-first" &&
		File::readCommonResourceFile(
			"config/common-only.txt", restoredCommonData, restoredCommonLength) &&
		std::string(restoredCommonData.get(), restoredCommonLength) == "common",
		"restoring the output root restores explicit active and common reads") && ok;

	ok = runDirectoryReplacementFixture(
		saveRoot,
		formalSaveAttackRoot,
		"editor-run save root replacement",
		[&]()
		{
			bool fixtureOk = true;
			fixtureOk = check(
				readViaFile("save/game/formal-sentinel.ini").empty(),
				"invalid save identity blocks reads without a formal fallback") && fixtureOk;
			fixtureOk = check(!File::writeFileChecked(
					"save/game/attack.ini", tamperData, 6),
				"invalid save identity blocks checked save writes") && fixtureOk;
			File::writeFile("save/game/attack-void.ini", tamperData, 6);
			File::appendFile("save/game/attack-append.ini", tamperData, 6);
			fixtureOk = check(!File::copyDirectoryFiles(
					"save/game/", "save/rpg4/") &&
				!File::recoverDirectoryCopy("save/rpg4/"),
				"invalid save identity blocks save directory transactions") && fixtureOk;
			fixtureOk = check(snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
				"invalid save identity leaves formal content unchanged") && fixtureOk;
			return fixtureOk;
		}) && ok;

	ok = runDirectoryReplacementFixture(
		applicationStateRoot,
		formalApplicationAttackRoot,
		"editor-run application-state root replacement",
		[&]()
		{
			std::unique_ptr<char[]> data;
			int length = 0;
			const bool readsBlocked = !File::readSharedApplicationFile(
				CONFIG_INI, data, length) && data == nullptr && length == 0;
			const bool writesBlocked = !File::writeSharedApplicationFile(
				CONFIG_INI, tamperData, 6);
			return check(readsBlocked && writesBlocked &&
				snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
				"invalid application-state identity blocks shared state without touching formal content");
		}) && ok;

	ok = runDirectoryReplacementFixture(
		diagnosticsRoot,
		formalDiagnosticsAttackRoot,
		"editor-run diagnostics root replacement",
		[&]()
		{
			GameLog::write("invalid diagnostics identity must be swallowed");
			return check(!fs::exists(
					formalDiagnosticsAttackRoot / fs::u8path(u8"game 运行.log")) &&
				!fs::exists(legacyLogPath) &&
				snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
				"invalid diagnostics identity blocks logging without a legacy or formal fallback");
		}) && ok;

	std::string linkError;
	const bool logSymlinkCreated = createSymbolicLinkFixture(
		formalLogAttackTarget, logPath, false, linkError);
	ok = check(logSymlinkCreated,
		"editor-run log symlink fixture must execute: " + linkError) && ok;
	if (logSymlinkCreated)
	{
		std::cout << "FIXTURE: executed editor-run log leaf symlink" << std::endl;
		std::string editorRunLogPath;
		ok = check(File::getEditorRunLogPath(editorRunLogPath) ==
				File::EditorRunFileLayoutState::Invalid &&
			editorRunLogPath.empty(),
			"an editor-run log leaf symlink invalidates the installed layout") && ok;
		GameLog::write("editor-run log symlink attack");
		ok = check(readRawFile(formalLogAttackTarget) == "keep-log-target" &&
			!fs::exists(legacyLogPath),
			"editor-run log symlink attack is swallowed without following the link") && ok;
		std::error_code removeError;
		const bool removed = fs::remove(logPath, removeError);
		ok = check(removed && !removeError,
			"editor-run log symlink fixture removes only the symlink") && ok;
	}
	std::string editorRunLogPath;
	ok = check(File::getEditorRunLogPath(editorRunLogPath) ==
			File::EditorRunFileLayoutState::Valid &&
		!editorRunLogPath.empty(),
		"removing the editor-run log symlink restores the installed layout identity") && ok;

	std::error_code hardLinkError;
	fs::create_hard_link(formalLogAttackTarget, logPath, hardLinkError);
	ok = check(!hardLinkError,
		"editor-run log hard-link fixture must execute: " +
		hardLinkError.message()) && ok;
	if (!hardLinkError)
	{
		std::cout << "FIXTURE: executed editor-run log leaf hard link" << std::endl;
		editorRunLogPath.clear();
		ok = check(File::getEditorRunLogPath(editorRunLogPath) ==
				File::EditorRunFileLayoutState::Invalid &&
			editorRunLogPath.empty(),
			"an editor-run log leaf hard link invalidates the installed layout") && ok;
		GameLog::write("editor-run log hard-link attack");
		ok = check(readRawFile(formalLogAttackTarget) == "keep-log-target" &&
			!fs::exists(legacyLogPath),
			"editor-run log hard-link attack is swallowed without writing the target") && ok;
		std::error_code removeError;
		const bool removed = fs::remove(logPath, removeError);
		ok = check(removed && !removeError,
			"editor-run log hard-link fixture removes only the added link") && ok;
	}
	editorRunLogPath.clear();
	ok = check(File::getEditorRunLogPath(editorRunLogPath) ==
			File::EditorRunFileLayoutState::Valid &&
		!editorRunLogPath.empty() &&
		snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
		"restored directory and log leaves recover the valid layout without formal changes") && ok;

	ok = check(readViaFile("config/order.txt") == "overlay" &&
		readViaFile("config/dependency-only.txt") == "dependency" &&
		readViaFile("config/common-only.txt") == "common" &&
		readViaFile("ini/ui/ui-dependency-only.txt") ==
			"ui-dependency" &&
		readViaFile("ini/ui/ui-common-only.txt") ==
			"ui-common" &&
		File::getAssetsName("config/order.txt").empty(),
		"editor-run ordinary reads use overlay then formal fallback order") && ok;
	ok = check(File::resolveFirstExistingResource({
			"config/active-first.txt",
			"config/overlay-second.txt"
		}) == "config/overlay-second.txt",
		"editor-run candidate resolution keeps root-major overlay priority") && ok;
	std::string visitedResource;
	ok = check(File::visitReadableResources({
			"config/active-first.txt",
			"config/overlay-second.txt"
		},
		[&visitedResource](const std::string& resourceName,
			std::unique_ptr<char[]>&, int)
		{
			visitedResource = resourceName;
			return true;
		}) && visitedResource == "config/overlay-second.txt",
		"editor-run readable-resource visitor keeps overlay priority") && ok;

	ok = check(readViaFile("save/game/session-only.ini") == "session-save" &&
		readViaFile("save/game/formal-only.ini").empty(),
		"editor-run save reads use isolatedSaveRoot without formal fallback") && ok;
	const char checkedData[] = "checked";
	ok = check(File::writeFileChecked("config/checked.txt", checkedData, 7) &&
		readRawFile(overlayRoot / "config" / "checked.txt") == "checked",
		"checked ordinary writes land in overlay") && ok;
	auto ownedWrite = std::make_unique<char[]>(5);
	std::memcpy(ownedWrite.get(), "owned", 5);
	ok = check(File::writeFileChecked(
			"config/owned-checked.txt", ownedWrite, 5) &&
		readRawFile(overlayRoot / "config" / "owned-checked.txt") == "owned",
		"owned checked writes remain inside overlay") && ok;
	File::writeFile("config/owned.txt", ownedWrite, 5);
	File::appendFile("config/owned.txt", checkedData, 7);
	auto ownedAppend = std::make_unique<char[]>(1);
	ownedAppend[0] = '!';
	File::appendFile("config/owned.txt", ownedAppend, 1);
	ok = check(readRawFile(overlayRoot / "config" / "owned.txt") ==
		"ownedchecked!",
		"void and owned write/append overloads remain inside overlay") && ok;
	File::copy("config/copy-source.txt", "config/copied.txt");
	ok = check(readRawFile(overlayRoot / "config" / "copied.txt") == "copy-source",
		"copy reads and writes through the editor-run routes") && ok;
	ok = check(File::removeFile("config/remove.txt") &&
		!fs::exists(overlayRoot / "config" / "remove.txt") &&
		readRawFile(activeRoot / "config" / "remove.txt") == "formal-remove",
		"removeFile removes only the overlay target") && ok;
	ok = check(File::clearDirectoryFiles("config/clear") &&
		!fs::exists(overlayRoot / "config" / "clear" / "session.txt") &&
		readRawFile(activeRoot / "config" / "clear" / "formal.txt") == "formal-clear",
		"clearDirectoryFiles clears only the overlay directory") && ok;

	const fs::path removeHardLink =
		overlayRoot / "config" / "hardlink-remove.txt";
	std::error_code removeHardLinkError;
	fs::create_hard_link(
		formalRemoveHardLinkTarget, removeHardLink,
		removeHardLinkError);
	ok = check(!removeHardLinkError,
		"removeFile hard-link fixture must execute: " +
		removeHardLinkError.message()) && ok;
	if (!removeHardLinkError)
	{
		announceRequiredSecurityFixture(
			"editor-run removeFile hard-link leaf");
		ok = check(!File::removeFile(
				"config/hardlink-remove.txt") &&
			fs::exists(removeHardLink) &&
			readRawFile(formalRemoveHardLinkTarget) ==
				"keep-remove-target",
			"removeFile rejects a hard-linked leaf without changing its formal target") &&
			ok;
		std::error_code removeError;
		const bool removed = fs::remove(
			removeHardLink, removeError);
		ok = check(removed && !removeError,
			"removeFile hard-link fixture removes only the added link") &&
			ok;
	}

	const fs::path clearHardLinkDirectory =
		overlayRoot / "config" / "hardlink-clear";
	const fs::path clearHardLink =
		clearHardLinkDirectory / "linked.txt";
	fs::create_directories(clearHardLinkDirectory);
	std::error_code clearHardLinkError;
	fs::create_hard_link(
		formalClearHardLinkTarget, clearHardLink,
		clearHardLinkError);
	ok = check(!clearHardLinkError,
		"clearDirectoryFiles hard-link fixture must execute: " +
		clearHardLinkError.message()) && ok;
	if (!clearHardLinkError)
	{
		announceRequiredSecurityFixture(
			"editor-run clearDirectoryFiles hard-link leaf");
		ok = check(!File::clearDirectoryFiles(
				"config/hardlink-clear") &&
			fs::exists(clearHardLink) &&
			readRawFile(formalClearHardLinkTarget) ==
				"keep-clear-target",
			"clearDirectoryFiles rejects a hard-linked leaf without changing its formal target") &&
			ok;
		std::error_code removeError;
		const bool removed = fs::remove(
			clearHardLink, removeError);
		ok = check(removed && !removeError,
			"clearDirectoryFiles hard-link fixture removes only the added link") &&
			ok;
	}

	const fs::path clearDirectoryLink =
		overlayRoot / "config" / "clear-directory-link";
	std::string clearDirectoryLinkError;
	const bool clearDirectoryLinkCreated =
		createSymbolicLinkFixture(
			formalClearLinkTargetRoot,
			clearDirectoryLink, true,
			clearDirectoryLinkError);
	ok = check(clearDirectoryLinkCreated,
		"clearDirectoryFiles directory-link fixture must execute: " +
		clearDirectoryLinkError) && ok;
	if (clearDirectoryLinkCreated)
	{
		announceRequiredSecurityFixture(
			"editor-run clearDirectoryFiles directory link");
		ok = check(!File::clearDirectoryFiles(
				"config/clear-directory-link") &&
			readRawFile(
				formalClearLinkTargetRoot / "sentinel.txt") ==
				"keep-clear-directory-target",
			"clearDirectoryFiles rejects an outward directory link without enumerating its target") &&
			ok;
		std::error_code removeError;
		const bool removed = fs::remove(
			clearDirectoryLink, removeError);
		ok = check(removed && !removeError,
			"clearDirectoryFiles directory-link fixture removes only the link") &&
			ok;
	}

	const char saveData[] = "isolated";
	ok = check(File::writeFileChecked("save/game/output.ini", saveData, 8) &&
		readRawFile(saveRoot / "game" / "output.ini") == "isolated" &&
		!fs::exists(saveRoot / "save" / "game" / "output.ini"),
		"save/foo maps to isolatedSaveRoot/foo without a duplicate save component") && ok;
	File::writeFile("save/shot/rpg1.png", saveData, 8);
	ok = check(readRawFile(saveRoot / "shot" / "rpg1.png") == "isolated",
		"screenshot-style writes land in the isolated save leaf") && ok;

	std::unique_ptr<char[]> sharedData;
	int sharedLength = 0;
	ok = check(!File::readSharedApplicationFile(
			CONFIG_INI, sharedData, sharedLength),
		"editor-run shared state does not read the formal collection fallback") && ok;
	const char applicationData[] = "application-state";
	ok = check(File::writeSharedApplicationFile(
			CONFIG_INI, applicationData, 17) &&
		readViaSharedApplicationFile(CONFIG_INI) == "application-state",
		"editor-run shared state reads and writes only application-state") && ok;

	const char hardLinkAttackData[] = "attacker-data";
	const fs::path checkedWriteHardLink =
		overlayRoot / "config" / "hardlink-checked-write.txt";
	const RegularFileSnapshot checkedWriteTargetBefore =
		snapshotRegularFile(formalCheckedWriteHardLinkTarget);
	std::error_code checkedWriteHardLinkError;
	fs::create_hard_link(
		formalCheckedWriteHardLinkTarget,
		checkedWriteHardLink,
		checkedWriteHardLinkError);
	ok = check(!checkedWriteHardLinkError,
		"checked-write hard-link fixture must execute: " +
		checkedWriteHardLinkError.message()) && ok;
	if (!checkedWriteHardLinkError)
	{
		announceRequiredSecurityFixture(
			"editor-run overlay writeFileChecked hard link");
		ok = check(!File::writeFileChecked(
				"config/hardlink-checked-write.txt",
				hardLinkAttackData,
				static_cast<int>(sizeof(hardLinkAttackData) - 1)) &&
			regularFileMatchesSnapshot(
				formalCheckedWriteHardLinkTarget,
				checkedWriteTargetBefore),
			"writeFileChecked rejects a hard link before truncating its target") &&
			ok;
		std::error_code removeError;
		const bool removed = fs::remove(checkedWriteHardLink, removeError);
		ok = check(removed && !removeError &&
			regularFileMatchesSnapshot(
				formalCheckedWriteHardLinkTarget,
				checkedWriteTargetBefore),
			"checked-write fixture removes only the added hard link") && ok;
	}

	const fs::path appendHardLink =
		overlayRoot / "config" / "hardlink-append.txt";
	const RegularFileSnapshot appendTargetBefore =
		snapshotRegularFile(formalAppendHardLinkTarget);
	std::error_code appendHardLinkError;
	fs::create_hard_link(
		formalAppendHardLinkTarget,
		appendHardLink,
		appendHardLinkError);
	ok = check(!appendHardLinkError,
		"append hard-link fixture must execute: " +
		appendHardLinkError.message()) && ok;
	if (!appendHardLinkError)
	{
		announceRequiredSecurityFixture(
			"editor-run overlay appendFile hard link");
		File::appendFile(
			"config/hardlink-append.txt",
			hardLinkAttackData,
			static_cast<int>(sizeof(hardLinkAttackData) - 1));
		ok = check(regularFileMatchesSnapshot(
				formalAppendHardLinkTarget, appendTargetBefore),
			"appendFile rejects a hard link before appending to its target") &&
			ok;
		std::error_code removeError;
		const bool removed = fs::remove(appendHardLink, removeError);
		ok = check(removed && !removeError &&
			regularFileMatchesSnapshot(
				formalAppendHardLinkTarget, appendTargetBefore),
			"append fixture removes only the added hard link") && ok;
	}

	const fs::path sharedWriteHardLink =
		applicationStateRoot / "hardlink-shared-write.ini";
	const RegularFileSnapshot sharedWriteTargetBefore =
		snapshotRegularFile(formalSharedWriteHardLinkTarget);
	std::error_code sharedWriteHardLinkError;
	fs::create_hard_link(
		formalSharedWriteHardLinkTarget,
		sharedWriteHardLink,
		sharedWriteHardLinkError);
	ok = check(!sharedWriteHardLinkError,
		"shared-write hard-link fixture must execute: " +
		sharedWriteHardLinkError.message()) && ok;
	if (!sharedWriteHardLinkError)
	{
		announceRequiredSecurityFixture(
			"editor-run application-state shared write hard link");
		ok = check(!File::writeSharedApplicationFile(
				"hardlink-shared-write.ini",
				hardLinkAttackData,
				static_cast<int>(sizeof(hardLinkAttackData) - 1)) &&
			regularFileMatchesSnapshot(
				formalSharedWriteHardLinkTarget,
				sharedWriteTargetBefore),
			"writeSharedApplicationFile rejects a hard link before truncation") &&
			ok;
		std::error_code removeError;
		const bool removed = fs::remove(sharedWriteHardLink, removeError);
		ok = check(removed && !removeError &&
			regularFileMatchesSnapshot(
				formalSharedWriteHardLinkTarget,
				sharedWriteTargetBefore),
			"shared-write fixture removes only the added hard link") && ok;
	}

	ok = check(File::copyDirectoryFiles("save/game/", "save/rpg1/") &&
		readRawFile(saveRoot / "rpg1" / "game.ini") == "session-game" &&
		readRawFile(saveRoot / "rpg1" / "player.ini") == "session-player" &&
		!fs::exists(saveRoot / "rpg1" / "stale.ini"),
		"directory copy staging and publication stay in isolated save") && ok;
	fs::remove_all(saveRoot / "rpg2");
	writeRawFile(saveRoot / ".jxqy-rpg2-backup" / "game.ini", "recovered");
	ok = check(SaveFileManager::HasSaveFile(2) &&
		readRawFile(saveRoot / "rpg2" / "game.ini") == "recovered",
		"HasSaveFile recovery mutates only isolated save") && ok;
	fs::remove_all(saveRoot / "rpg3");
	writeRawFile(saveRoot / ".jxqy-rpg3-staging" / "game.ini", "partial");
	ok = check(File::recoverDirectoryCopy("save/rpg3/") &&
		!fs::exists(saveRoot / ".jxqy-rpg3-staging"),
		"explicit transaction recovery cleans only isolated save artifacts") && ok;

	const fs::path hookTransactionParent =
		saveRoot / "hook-transaction-parent";
	const fs::path hookTransactionBackup =
		sessionRoot / "hook-transaction-parent-installed";
	writeRawFile(
		hookTransactionParent / "slot" / "old.txt",
		"hook-transaction-old");
	bool transactionHookAttempted = false;
	bool transactionHookExecuted = false;
	std::string transactionHookError;
	File::setEditorRunFileOperationTestHook(
		[&](File::EditorRunFileOperationPhase phase)
		{
			if (phase !=
					File::EditorRunFileOperationPhase::
						BeforeTransactionMutation ||
				transactionHookAttempted)
			{
				return;
			}
			transactionHookAttempted = true;
			transactionHookExecuted =
				beginDirectoryReplacementFixture(
					hookTransactionParent,
					hookTransactionBackup,
					formalTransactionAttackRoot,
					transactionHookError);
		});
	const bool transactionHookCopyResult =
		File::copyDirectoryFiles(
			"save/game/",
			"save/hook-transaction-parent/slot/");
	File::setEditorRunFileOperationTestHook({});
#if defined(_WIN32)
	const bool transactionHookSecurityResult =
		transactionHookAttempted &&
		!transactionHookExecuted &&
		!transactionHookError.empty();
#else
	const bool transactionHookSecurityResult =
		transactionHookAttempted &&
		transactionHookExecuted;
#endif
	ok = check(transactionHookSecurityResult &&
		transactionHookCopyResult,
		"deterministic transaction hook keeps the mutation on the held parent (Windows blocks replacement; POSIX continues on the original fd): " +
			transactionHookError) && ok;
	if (transactionHookExecuted)
	{
		std::string restoreError;
		ok = check(endDirectoryReplacementFixture(
				hookTransactionParent,
				hookTransactionBackup,
				restoreError),
			"transaction hook restores the original parent: " +
				restoreError) && ok;
	}
	if (transactionHookSecurityResult)
	{
		announceRequiredSecurityFixture(
			"editor-run transaction final-check race");
	}
	ok = check(
		readRawFile(
			hookTransactionParent / "slot" /
				"game.ini") == "session-game" &&
		snapshotDirectoryTreeWithWriteTimes(formalRoot) ==
			formalBefore,
		"transaction final-check fixture publishes only below the held output parent and preserves formal bytes and mtimes") &&
		ok;

	writeRawFile(saveRoot /
		"race-before-backup" / "old.txt", "safe-before-backup-old");
	const fs::path beforeBackupSaveRoot =
		sessionRoot / "save-before-backup-installed";
	bool beforeBackupFixtureExecuted = false;
	std::string beforeBackupFixtureError;
	const bool beforeBackupCopyResult = File::copyDirectoryFiles(
		"save/game/",
		"save/race-before-backup/",
		{},
		[&](File::DirectoryCopyPhase phase)
		{
			if (phase != File::DirectoryCopyPhase::BeforeBackup)
			{
				return false;
			}
			beforeBackupFixtureExecuted = beginDirectoryReplacementFixture(
				saveRoot,
				beforeBackupSaveRoot,
				formalTransactionAttackRoot,
				beforeBackupFixtureError);
			if (beforeBackupFixtureExecuted)
			{
				std::cout <<
					"FIXTURE: executed transaction save-root replacement BeforeBackup" <<
					std::endl;
			}
			return true;
		});
#if defined(_WIN32)
	ok = check(!beforeBackupCopyResult &&
		!beforeBackupFixtureExecuted &&
		!beforeBackupFixtureError.empty(),
		"BeforeBackup fixture must be blocked by the held save parent on Windows: " +
			beforeBackupFixtureError) && ok;
#else
	ok = check(!beforeBackupCopyResult && beforeBackupFixtureExecuted,
		"BeforeBackup fixture must replace saveRoot while operations stay fd-relative: " +
			beforeBackupFixtureError) && ok;
#endif
	if (beforeBackupFixtureExecuted)
	{
		std::string restoreError;
		ok = check(endDirectoryReplacementFixture(
				saveRoot, beforeBackupSaveRoot, restoreError),
			"BeforeBackup fixture restores the installed save root: " +
			restoreError) && ok;
	}
	ok = check(snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
		"BeforeBackup routing drift leaves every formal transaction sentinel unchanged") && ok;
	ok = check(File::recoverDirectoryCopy("save/race-before-backup/") &&
		readRawFile(saveRoot /
			"race-before-backup" / "old.txt") == "safe-before-backup-old" &&
		!fs::exists(saveRoot / ".jxqy-race-before-backup-staging") &&
		!fs::exists(saveRoot / ".jxqy-race-before-backup-backup") &&
		!fs::exists(saveRoot / ".jxqy-race-before-backup-staging-ready"),
		"recovery cleans the preserved safe artifacts after BeforeBackup drift") && ok;

	writeRawFile(saveRoot /
		"race-before-publish" / "old.txt", "safe-before-publish-old");
	const fs::path beforePublishSaveRoot =
		sessionRoot / "save-before-publish-installed";
	bool beforePublishFixtureExecuted = false;
	std::string beforePublishFixtureError;
	const bool beforePublishCopyResult = File::copyDirectoryFiles(
		"save/game/",
		"save/race-before-publish/",
		{},
		[&](File::DirectoryCopyPhase phase)
		{
			if (phase != File::DirectoryCopyPhase::BeforePublish)
			{
				return false;
			}
			beforePublishFixtureExecuted = beginDirectoryReplacementFixture(
				saveRoot,
				beforePublishSaveRoot,
				formalTransactionAttackRoot,
				beforePublishFixtureError);
			if (beforePublishFixtureExecuted)
			{
				std::cout <<
					"FIXTURE: executed transaction save-root replacement BeforePublish" <<
					std::endl;
			}
			return true;
		});
#if defined(_WIN32)
	ok = check(!beforePublishCopyResult &&
		!beforePublishFixtureExecuted &&
		!beforePublishFixtureError.empty(),
		"BeforePublish fixture must be blocked by the held save parent on Windows: " +
			beforePublishFixtureError) && ok;
#else
	ok = check(!beforePublishCopyResult && beforePublishFixtureExecuted,
		"BeforePublish fixture must replace saveRoot while operations stay fd-relative: " +
			beforePublishFixtureError) && ok;
#endif
	if (beforePublishFixtureExecuted)
	{
		std::string restoreError;
		ok = check(endDirectoryReplacementFixture(
				saveRoot, beforePublishSaveRoot, restoreError),
			"BeforePublish fixture restores the installed save root: " +
			restoreError) && ok;
	}
	ok = check(snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
		"BeforePublish routing drift leaves every formal transaction sentinel unchanged") && ok;
	ok = check(File::recoverDirectoryCopy("save/race-before-publish/") &&
		readRawFile(saveRoot /
			"race-before-publish" / "old.txt") == "safe-before-publish-old" &&
		!fs::exists(saveRoot / ".jxqy-race-before-publish-staging") &&
		!fs::exists(saveRoot / ".jxqy-race-before-publish-backup") &&
		!fs::exists(saveRoot / ".jxqy-race-before-publish-staging-ready"),
		"recovery restores the old destination after BeforePublish drift") && ok;

	const fs::path outsideTransactionAttackRoot =
		root / "EditorRunOutsideTransactionAttacks";
	const auto runNestedParentReplacementFixture =
		[&](File::DirectoryCopyPhase attackPhase,
			const std::string& destinationLeaf,
			const std::string& fixtureName)
		{
			bool fixtureOk = true;
			const fs::path transactionParent = saveRoot / "nested";
			const fs::path outsideParent =
				outsideTransactionAttackRoot / fixtureName;
			const fs::path displacedParent =
				sessionRoot / (fixtureName + "-installed-parent");
			writeRawFile(
				outsideParent / destinationLeaf / "sentinel.txt",
				"keep-outside-destination");
			writeRawFile(
				outsideParent /
					fs::u8path(".jxqy-" + destinationLeaf + "-staging") /
					"sentinel.txt",
				"keep-outside-staging");
			writeRawFile(
				outsideParent /
					fs::u8path(".jxqy-" + destinationLeaf + "-backup") /
					"sentinel.txt",
				"keep-outside-backup");
			writeRawFile(
				outsideParent /
					fs::u8path(".jxqy-" + destinationLeaf +
						"-staging-ready"),
				"keep-outside-ready");
			const std::vector<std::string> outsideBefore =
				snapshotDirectoryTreeWithWriteTimes(outsideParent);
			writeRawFile(
				transactionParent / destinationLeaf / "old.txt",
				"safe-nested-old");

			bool fixtureAttempted = false;
			bool fixtureExecuted = false;
			std::string fixtureError;
			const bool copyResult = File::copyDirectoryFiles(
				"save/game/",
				"save/nested/" + destinationLeaf + "/",
				{},
				[&](File::DirectoryCopyPhase phase)
				{
					if (phase != attackPhase)
					{
						return false;
					}
					fixtureAttempted = true;
					fixtureExecuted = beginDirectoryReplacementFixture(
						transactionParent,
						displacedParent,
						outsideParent,
						fixtureError);
					if (fixtureExecuted
#if defined(_WIN32)
						|| !fixtureError.empty()
#endif
						)
					{
						announceRequiredSecurityFixture(fixtureName);
					}
					return true;
				});
#if defined(_WIN32)
			const bool expectedReplacementResult =
				!fixtureExecuted && !fixtureError.empty();
#else
			const bool expectedReplacementResult =
				fixtureExecuted;
#endif
			fixtureOk = check(!copyResult && fixtureAttempted &&
				expectedReplacementResult,
				fixtureName +
				" must execute the held-parent replacement attempt: " +
				fixtureError) && fixtureOk;
			if (fixtureExecuted)
			{
				fixtureOk = check(
					snapshotDirectoryTreeWithWriteTimes(outsideParent) ==
						outsideBefore,
					fixtureName +
					" leaves outside destination/staging/backup/ready bytes and mtimes unchanged") &&
					fixtureOk;
				std::string restoreError;
				fixtureOk = check(endDirectoryReplacementFixture(
						transactionParent, displacedParent, restoreError),
					fixtureName +
					" restores the original nested transaction parent: " +
					restoreError) && fixtureOk;
			}
			fixtureOk = check(File::recoverDirectoryCopy(
					"save/nested/" + destinationLeaf + "/") &&
				readRawFile(transactionParent /
					destinationLeaf / "old.txt") == "safe-nested-old" &&
				snapshotDirectoryTreeWithWriteTimes(outsideParent) ==
					outsideBefore,
				fixtureName +
				" recovers only the restored transaction parent") &&
				fixtureOk;
			return fixtureOk;
		};
	ok = runNestedParentReplacementFixture(
		File::DirectoryCopyPhase::BeforeBackup,
		"slot-before-backup",
		"nested-parent-link-before-backup") && ok;
	ok = runNestedParentReplacementFixture(
		File::DirectoryCopyPhase::BeforePublish,
		"slot-before-publish",
		"nested-parent-link-before-publish") && ok;

	enum class TransactionDirectoryArtifact
	{
		Staging,
		Backup
	};
	const auto runTransactionDirectoryLinkReplacementFixture =
		[&](File::DirectoryCopyPhase attackPhase,
			TransactionDirectoryArtifact artifact,
			const std::string& destinationLeaf,
			const std::string& fixtureName)
		{
			bool fixtureOk = true;
			const std::string artifactSuffix =
				artifact == TransactionDirectoryArtifact::Staging
				? "-staging"
				: "-backup";
			const fs::path artifactPath =
				saveRoot /
				fs::u8path(".jxqy-" + destinationLeaf + artifactSuffix);
			const fs::path displacedArtifact =
				saveRoot /
				fs::u8path(".fixture-displaced-" + destinationLeaf +
					artifactSuffix);
			const fs::path outsideArtifact =
				outsideTransactionAttackRoot / fixtureName;
			writeRawFile(
				outsideArtifact / "sentinel.txt",
				"keep-outside-transaction-artifact");
			const std::vector<std::string> outsideBefore =
				snapshotDirectoryTreeWithWriteTimes(outsideArtifact);
			writeRawFile(
				saveRoot / destinationLeaf / "old.txt",
				"safe-link-replacement-old");

			bool fixtureExecuted = false;
			std::string fixtureError;
			const bool copyResult = File::copyDirectoryFiles(
				"save/game/",
				"save/" + destinationLeaf + "/",
				{},
				[&](File::DirectoryCopyPhase phase)
				{
					if (phase != attackPhase)
					{
						return false;
					}
					fixtureExecuted = beginDirectoryReplacementFixture(
						artifactPath,
						displacedArtifact,
						outsideArtifact,
						fixtureError);
					if (fixtureExecuted)
					{
						announceRequiredSecurityFixture(fixtureName);
					}
					return !fixtureExecuted;
				});
			fixtureOk = check(!copyResult && fixtureExecuted,
				fixtureName +
				" must replace the transaction directory artifact: " +
				fixtureError) && fixtureOk;
			if (fixtureExecuted)
			{
				fixtureOk = check(
					snapshotDirectoryTreeWithWriteTimes(outsideArtifact) ==
						outsideBefore,
					fixtureName +
					" leaves outside bytes and mtimes unchanged") &&
					fixtureOk;
				std::string restoreError;
				fixtureOk = check(endDirectoryReplacementFixture(
						artifactPath, displacedArtifact, restoreError),
					fixtureName +
					" restores the original transaction artifact: " +
					restoreError) && fixtureOk;
			}
			fixtureOk = check(File::recoverDirectoryCopy(
					"save/" + destinationLeaf + "/") &&
				readRawFile(saveRoot /
					destinationLeaf / "old.txt") ==
					"safe-link-replacement-old" &&
				snapshotDirectoryTreeWithWriteTimes(outsideArtifact) ==
					outsideBefore,
				fixtureName +
				" recovery mutates only the restored transaction paths") &&
				fixtureOk;
			return fixtureOk;
		};
	ok = runTransactionDirectoryLinkReplacementFixture(
		File::DirectoryCopyPhase::BeforeBackup,
		TransactionDirectoryArtifact::Staging,
		"staging-link-before-backup",
		"staging-link-replacement-before-backup") && ok;
	ok = runTransactionDirectoryLinkReplacementFixture(
		File::DirectoryCopyPhase::BeforePublish,
		TransactionDirectoryArtifact::Backup,
		"backup-link-before-publish",
		"backup-link-replacement-before-publish") && ok;
	ok = runTransactionDirectoryLinkReplacementFixture(
		File::DirectoryCopyPhase::BeforePublish,
		TransactionDirectoryArtifact::Staging,
		"staging-link-before-publish",
		"staging-link-replacement-before-publish") && ok;

	enum class RecoveryLinkArtifact
	{
		Destination,
		Backup,
		Staging,
		Ready
	};
	const auto runRecoveryLinkFixture =
		[&](RecoveryLinkArtifact artifact,
			const std::string& destinationLeaf,
			const std::string& fixtureName)
		{
			bool fixtureOk = true;
			const bool directory =
				artifact != RecoveryLinkArtifact::Ready;
			fs::path artifactPath;
			switch (artifact)
			{
			case RecoveryLinkArtifact::Destination:
				artifactPath = saveRoot / destinationLeaf;
				break;
			case RecoveryLinkArtifact::Backup:
				artifactPath = saveRoot /
					fs::u8path(".jxqy-" + destinationLeaf + "-backup");
				break;
			case RecoveryLinkArtifact::Staging:
				artifactPath = saveRoot /
					fs::u8path(".jxqy-" + destinationLeaf + "-staging");
				break;
			case RecoveryLinkArtifact::Ready:
				artifactPath = saveRoot /
					fs::u8path(".jxqy-" + destinationLeaf +
						"-staging-ready");
				break;
			}
			const fs::path outsideArtifact =
				outsideTransactionAttackRoot / fixtureName /
				(directory ? "directory-target" : "file-target.txt");
			if (directory)
			{
				writeRawFile(
					outsideArtifact / "sentinel.txt",
					"keep-malicious-recovery-directory-target");
			}
			else
			{
				writeRawFile(
					outsideArtifact,
					"keep-malicious-recovery-file-target");
			}
			const std::vector<std::string> outsideDirectoryBefore =
				directory
				? snapshotDirectoryTreeWithWriteTimes(outsideArtifact)
				: std::vector<std::string>();
			const RegularFileSnapshot outsideFileBefore =
				directory
					? RegularFileSnapshot()
					: snapshotRegularFile(outsideArtifact);

			std::string linkError;
			const bool linkCreated = createSymbolicLinkFixture(
				outsideArtifact, artifactPath, directory, linkError);
			fixtureOk = check(linkCreated,
				fixtureName +
				" must create a real malicious recovery link: " +
				linkError) && fixtureOk;
			if (linkCreated)
			{
				announceRequiredSecurityFixture(fixtureName);
				const bool recoveryResult = File::recoverDirectoryCopy(
					"save/" + destinationLeaf + "/");
				std::error_code statusError;
				const fs::file_status status =
					fs::symlink_status(artifactPath, statusError);
				fixtureOk = check(!recoveryResult &&
					!statusError &&
					fs::is_symlink(status) &&
					(directory
						? snapshotDirectoryTreeWithWriteTimes(
							outsideArtifact) == outsideDirectoryBefore
						: regularFileMatchesSnapshot(
							outsideArtifact, outsideFileBefore)),
					fixtureName +
					" fails closed without deleting the link or changing target bytes/mtime") &&
					fixtureOk;
				std::error_code removeError;
				const bool removed = fs::remove(artifactPath, removeError);
				fixtureOk = check(removed && !removeError,
					fixtureName + " cleanup removes only the malicious link") &&
					fixtureOk;
			}
			return fixtureOk;
		};
	ok = runRecoveryLinkFixture(
		RecoveryLinkArtifact::Destination,
		"recover-destination-link",
		"recovery-malicious-destination-link") && ok;
	ok = runRecoveryLinkFixture(
		RecoveryLinkArtifact::Backup,
		"recover-backup-link",
		"recovery-malicious-backup-link") && ok;
	ok = runRecoveryLinkFixture(
		RecoveryLinkArtifact::Staging,
		"recover-staging-link",
		"recovery-malicious-staging-link") && ok;
	ok = runRecoveryLinkFixture(
		RecoveryLinkArtifact::Ready,
		"recover-ready-link",
		"recovery-malicious-ready-link") && ok;

	writeRawFile(
		saveRoot / "transaction-hardlink-file" / "old.txt",
		"safe-transaction-hardlink-file-old");
	const RegularFileSnapshot transactionFileTargetBefore =
		snapshotRegularFile(formalTransactionFileHardLinkTarget);
	bool transactionFileHardLinkExecuted = false;
	std::string transactionFileHardLinkError;
	const bool transactionFileHardLinkCopyResult =
		File::copyDirectoryFiles(
			"save/game/",
			"save/transaction-hardlink-file/",
			{},
			[&](File::DirectoryCopyPhase phase)
			{
				if (phase != File::DirectoryCopyPhase::BeforeBackup)
				{
					return false;
				}
				const fs::path stagedFile =
					saveRoot /
					".jxqy-transaction-hardlink-file-staging" /
					"game.ini";
				std::error_code removeError;
				const bool removed = fs::remove(stagedFile, removeError);
				if (!removed || removeError)
				{
					transactionFileHardLinkError =
						removeError
							? removeError.message()
							: "staged file was absent";
					return true;
				}
				std::error_code hardLinkError;
				fs::create_hard_link(
					formalTransactionFileHardLinkTarget,
					stagedFile,
					hardLinkError);
				if (hardLinkError)
				{
					transactionFileHardLinkError =
						hardLinkError.message();
					return true;
				}
				transactionFileHardLinkExecuted = true;
				announceRequiredSecurityFixture(
					"transaction staged file hard link before backup");
				return false;
			});
	ok = check(!transactionFileHardLinkCopyResult &&
		transactionFileHardLinkExecuted &&
		regularFileMatchesSnapshot(
			formalTransactionFileHardLinkTarget,
			transactionFileTargetBefore),
		"transaction staging rejects a hard-linked file before publication: " +
			transactionFileHardLinkError) && ok;
	if (transactionFileHardLinkExecuted)
	{
		const fs::path stagedFile =
			saveRoot /
				".jxqy-transaction-hardlink-file-staging" /
				"game.ini";
		std::error_code removeError;
		const bool removed = fs::remove(stagedFile, removeError);
		ok = check(removed && !removeError &&
			regularFileMatchesSnapshot(
				formalTransactionFileHardLinkTarget,
				transactionFileTargetBefore),
			"transaction staged-file fixture removes only the hard link") &&
			ok;
	}
	ok = check(File::recoverDirectoryCopy(
			"save/transaction-hardlink-file/") &&
		readRawFile(saveRoot /
			"transaction-hardlink-file" / "old.txt") ==
			"safe-transaction-hardlink-file-old" &&
		regularFileMatchesSnapshot(
			formalTransactionFileHardLinkTarget,
			transactionFileTargetBefore),
		"transaction staged-file recovery preserves the old destination and formal target") &&
		ok;

	writeRawFile(
		saveRoot / "transaction-hardlink-ready" / "old.txt",
		"safe-transaction-hardlink-ready-old");
	const RegularFileSnapshot transactionReadyTargetBefore =
		snapshotRegularFile(formalTransactionReadyHardLinkTarget);
	bool transactionReadyHardLinkExecuted = false;
	std::string transactionReadyHardLinkError;
	const bool transactionReadyHardLinkCopyResult =
		File::copyDirectoryFiles(
			"save/game/",
			"save/transaction-hardlink-ready/",
			{},
			[&](File::DirectoryCopyPhase phase)
			{
				if (phase != File::DirectoryCopyPhase::BeforeBackup)
				{
					return false;
				}
				const fs::path readyFile =
					saveRoot /
						".jxqy-transaction-hardlink-ready-staging-ready";
				std::error_code removeError;
				const bool removed = fs::remove(readyFile, removeError);
				if (!removed || removeError)
				{
					transactionReadyHardLinkError =
						removeError
							? removeError.message()
							: "ready file was absent";
					return true;
				}
				std::error_code hardLinkError;
				fs::create_hard_link(
					formalTransactionReadyHardLinkTarget,
					readyFile,
					hardLinkError);
				if (hardLinkError)
				{
					transactionReadyHardLinkError =
						hardLinkError.message();
					return true;
				}
				transactionReadyHardLinkExecuted = true;
				announceRequiredSecurityFixture(
					"transaction ready file hard link before backup");
				return false;
			});
	ok = check(!transactionReadyHardLinkCopyResult &&
		transactionReadyHardLinkExecuted &&
		regularFileMatchesSnapshot(
			formalTransactionReadyHardLinkTarget,
			transactionReadyTargetBefore),
		"transaction rejects a hard-linked ready file before mutation: " +
			transactionReadyHardLinkError) && ok;
	if (transactionReadyHardLinkExecuted)
	{
		const fs::path readyFile =
			saveRoot /
				".jxqy-transaction-hardlink-ready-staging-ready";
		std::error_code removeError;
		const bool removed = fs::remove(readyFile, removeError);
		ok = check(removed && !removeError &&
			regularFileMatchesSnapshot(
				formalTransactionReadyHardLinkTarget,
				transactionReadyTargetBefore),
			"transaction ready-file fixture removes only the hard link") &&
			ok;
	}
	ok = check(File::recoverDirectoryCopy(
			"save/transaction-hardlink-ready/") &&
		readRawFile(saveRoot /
			"transaction-hardlink-ready" / "old.txt") ==
			"safe-transaction-hardlink-ready-old" &&
		regularFileMatchesSnapshot(
			formalTransactionReadyHardLinkTarget,
			transactionReadyTargetBefore),
		"transaction ready-file recovery preserves the old destination and formal target") &&
		ok;

	std::string exactDiagnosticsPath;
	uint64_t exactDiagnosticsGeneration = 0;
	std::FILE* exactDiagnosticsFile = nullptr;
	std::intptr_t exactDiagnosticsParent = -1;
	ok = check(
		File::getEditorRunDiagnosticsPath(
			exactDiagnosticsPath,
			exactDiagnosticsGeneration) ==
			File::EditorRunFileLayoutState::Valid &&
		exactDiagnosticsPath == diagnosticsPath.u8string() &&
		File::openEditorRunDiagnostics(
			exactDiagnosticsPath,
			exactDiagnosticsGeneration,
			exactDiagnosticsFile,
			exactDiagnosticsParent) &&
		File::editorRunDiagnosticsHandleIsCurrent(
			exactDiagnosticsFile,
			exactDiagnosticsParent,
			exactDiagnosticsPath,
			exactDiagnosticsGeneration),
		"editor-run diagnostics opens only its exact held path") && ok;
	if (exactDiagnosticsFile != nullptr)
	{
		const std::string firstDiagnostic =
			"{\"sequence\":1,\"message\":\"中文\"}\n";
		const bool firstDiagnosticWritten =
			std::fwrite(
				firstDiagnostic.data(), 1,
				firstDiagnostic.size(),
				exactDiagnosticsFile) ==
				firstDiagnostic.size() &&
			std::fflush(exactDiagnosticsFile) == 0;
		ok = check(firstDiagnosticWritten &&
			readRawFile(diagnosticsPath) == firstDiagnostic,
			"editor-run diagnostics flushes UTF-8 bytes to the exact JSONL path") &&
			ok;

#if defined(_WIN32)
		const fs::path blockedDiagnosticsLeafRename =
			diagnosticsRoot / "blocked-events.jsonl";
		std::error_code blockedDiagnosticsLeafRenameError;
		fs::rename(
			diagnosticsPath,
			blockedDiagnosticsLeafRename,
			blockedDiagnosticsLeafRenameError);
		ok = check(
			static_cast<bool>(
				blockedDiagnosticsLeafRenameError) &&
			!fs::exists(blockedDiagnosticsLeafRename) &&
			File::editorRunDiagnosticsHandleIsCurrent(
				exactDiagnosticsFile,
				exactDiagnosticsParent,
				exactDiagnosticsPath,
				exactDiagnosticsGeneration),
			"held Windows diagnostics handle blocks exact-leaf replacement") &&
			ok;
#else
		const fs::path displacedDiagnosticsPath =
			diagnosticsRoot / "displaced-events.jsonl";
		std::error_code displacedDiagnosticsError;
		fs::rename(
			diagnosticsPath,
			displacedDiagnosticsPath,
			displacedDiagnosticsError);
		ok = check(!displacedDiagnosticsError,
			"structured diagnostics replacement fixture displaces the held leaf") &&
			ok;
		if (!displacedDiagnosticsError)
		{
			std::error_code diagnosticsHardLinkError;
			fs::create_hard_link(
				formalLogAttackTarget,
				diagnosticsPath,
				diagnosticsHardLinkError);
			ok = check(!diagnosticsHardLinkError &&
				!File::editorRunDiagnosticsHandleIsCurrent(
					exactDiagnosticsFile,
					exactDiagnosticsParent,
					exactDiagnosticsPath,
					exactDiagnosticsGeneration),
				"held diagnostics rejects a same-path formal hard-link replacement") &&
				ok;
			if (!diagnosticsHardLinkError)
			{
				std::error_code removeError;
				const bool removed =
					fs::remove(diagnosticsPath, removeError);
				ok = check(removed && !removeError &&
					readRawFile(formalLogAttackTarget) ==
						"keep-log-target",
					"structured diagnostics fixture removes only the hostile hard link") &&
					ok;
			}
			std::error_code restoreError;
			fs::rename(
				displacedDiagnosticsPath,
				diagnosticsPath,
				restoreError);
			ok = check(!restoreError &&
				File::editorRunDiagnosticsHandleIsCurrent(
					exactDiagnosticsFile,
					exactDiagnosticsParent,
					exactDiagnosticsPath,
					exactDiagnosticsGeneration),
				"held diagnostics resumes only after restoring its exact inode") &&
				ok;
		}
#endif
	}
	if (exactDiagnosticsFile != nullptr)
	{
		std::fclose(exactDiagnosticsFile);
	}
	File::closeEditorRunDiagnosticsParent(
		exactDiagnosticsParent);

	std::shared_ptr<EditorRun::RuntimeTraceFileSink>
		runtimeTraceSink =
			EditorRun::RuntimeTraceFileSink::open();
	ok = check(
		runtimeTraceSink != nullptr &&
		runtimeTraceSink->appendBatchAndFlush(
			"{\"sequence\":1,\"eventType\":\"session.start\"}\n"
			"{\"sequence\":2,\"eventType\":\"source.line\"}\n") &&
		readRawFile(runtimeTracePath).find(
			"\"sequence\":2") !=
			std::string::npos &&
		EditorRun::RuntimeTraceFileSink::open() ==
			nullptr,
		"runtime trace sink exclusively creates and durably batch-writes its independent exact leaf") &&
		ok;

	GameLog::write("editor-run exact log path");
	ok = check(readRawFile(logPath).find("editor-run exact log path") !=
			std::string::npos &&
		!fs::exists(legacyLogPath) &&
		!fs::exists(overlayRoot / "log.txt"),
		"editor-run logging forces the exact diagnostics logPath") && ok;

#if defined(_WIN32)
	const std::string heldLogBefore =
		readRawFile(logPath);
	const fs::path blockedHeldRename =
		diagnosticsRoot / "blocked-held-rename.log";
	std::error_code blockedHeldRenameError;
	fs::rename(
		logPath, blockedHeldRename,
		blockedHeldRenameError);
	ok = check(static_cast<bool>(blockedHeldRenameError) &&
		!fs::exists(blockedHeldRename),
		"held editor-run log denies delete sharing and blocks a leaf rename on Windows") &&
		ok;
	const fs::path blockedDiagnosticsRename =
		sessionRoot / "blocked-diagnostics-rename";
	std::error_code blockedDiagnosticsRenameError;
	fs::rename(
		diagnosticsRoot, blockedDiagnosticsRename,
		blockedDiagnosticsRenameError);
	ok = check(static_cast<bool>(blockedDiagnosticsRenameError) &&
		!fs::exists(blockedDiagnosticsRename),
		"held log parent denies delete sharing and blocks diagnostics replacement on Windows") &&
		ok;
	GameLog::write(
		"held log continues after blocked replacements");
	ok = check(
		readRawFile(logPath).size() > heldLogBefore.size() &&
		readRawFile(logPath).find(
			"held log continues after blocked replacements") !=
				std::string::npos &&
		readRawFile(formalLogAttackTarget) ==
			"keep-log-target",
		"held Windows logger continues only on its verified leaf and parent") &&
		ok;
#else
	const fs::path heldHardLinkOriginal =
		diagnosticsRoot / "held-before-hard-link.log";
	std::error_code heldRenameError;
	fs::rename(logPath, heldHardLinkOriginal, heldRenameError);
	ok = check(!heldRenameError,
		"held editor-run log permits a deterministic leaf rename fixture: " +
		heldRenameError.message()) && ok;
	if (!heldRenameError)
	{
		const std::string heldContentBeforeHardLink =
			readRawFile(heldHardLinkOriginal);
		std::error_code heldHardLinkError;
		fs::create_hard_link(
			formalLogAttackTarget, logPath, heldHardLinkError);
		ok = check(!heldHardLinkError,
			"held-log hard-link replacement fixture must execute: " +
			heldHardLinkError.message()) && ok;
		if (!heldHardLinkError)
		{
			std::cout <<
				"FIXTURE: executed held editor-run log hard-link replacement" <<
				std::endl;
			GameLog::write("held log hard-link replacement must be swallowed");
			ok = check(
				readRawFile(heldHardLinkOriginal) == heldContentBeforeHardLink &&
				readRawFile(formalLogAttackTarget) == "keep-log-target",
				"held logger writes neither its displaced file nor the formal hard-link target") &&
				ok;
			std::error_code removeError;
			const bool removed = fs::remove(logPath, removeError);
			ok = check(removed && !removeError,
				"held-log hard-link fixture removes only the added link") && ok;
		}
		std::error_code restoreError;
		fs::rename(heldHardLinkOriginal, logPath, restoreError);
		ok = check(!restoreError,
			"held-log hard-link fixture restores the original exact log leaf: " +
			restoreError.message()) && ok;
		if (!restoreError)
		{
			GameLog::write("held log continues after exact leaf restoration");
			ok = check(readRawFile(logPath).find(
					"held log continues after exact leaf restoration") !=
					std::string::npos,
				"held logger resumes only after its original exact leaf is restored") && ok;
		}
	}

	const fs::path heldOutsideDiagnosticsRoot =
		sessionRoot / "held-during-diagnostics-root-replacement.log";
	std::error_code heldOutsideRenameError;
	fs::rename(
		logPath, heldOutsideDiagnosticsRoot, heldOutsideRenameError);
	ok = check(!heldOutsideRenameError,
		"held log leaf moves aside before replacing its diagnostics root: " +
		heldOutsideRenameError.message()) && ok;
	if (!heldOutsideRenameError)
	{
		const std::string logBeforeDiagnosticsReplacement =
			readRawFile(heldOutsideDiagnosticsRoot);
		ok = runDirectoryReplacementFixture(
			diagnosticsRoot,
			formalDiagnosticsAttackRoot,
			"held editor-run diagnostics root replacement",
			[&]()
			{
				GameLog::write(
					"held logger must not follow a replaced diagnostics root");
				return check(
					readRawFile(heldOutsideDiagnosticsRoot) ==
						logBeforeDiagnosticsReplacement &&
					!fs::exists(
						formalDiagnosticsAttackRoot / logPath.filename()) &&
					snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
					"held logger remains silent while its diagnostics root identity is invalid");
			}) && ok;
		std::error_code heldOutsideRestoreError;
		fs::rename(
			heldOutsideDiagnosticsRoot, logPath, heldOutsideRestoreError);
		ok = check(!heldOutsideRestoreError,
			"held log leaf returns to the restored diagnostics root: " +
			heldOutsideRestoreError.message()) && ok;
		if (!heldOutsideRestoreError)
		{
			GameLog::write(
				"held logger continues after diagnostics root restoration");
			ok = check(readRawFile(logPath).find(
					"held logger continues after diagnostics root restoration") !=
					std::string::npos,
				"restoring the diagnostics root restores the held exact-path logger") && ok;
		}
	}

	const fs::path displacedHeldLog =
		diagnosticsRoot / "held-before-ordinary-replacement.log";
	std::error_code displacedRenameError;
	fs::rename(logPath, displacedHeldLog, displacedRenameError);
	ok = check(!displacedRenameError,
		"held editor-run log supports the ordinary replacement fixture: " +
		displacedRenameError.message()) && ok;
	if (!displacedRenameError)
	{
		const std::string displacedContentBefore =
			readRawFile(displacedHeldLog);
		writeRawFile(logPath, "ordinary-replacement");
		std::cout <<
			"FIXTURE: executed held log ordinary same-path replacement" <<
			std::endl;
		std::string currentLogPath;
		ok = check(File::getEditorRunLogPath(currentLogPath) ==
				File::EditorRunFileLayoutState::Valid,
			"an unrelated regular replacement remains a valid path shape") && ok;
		GameLog::write(
			"ordinary replacement must fail closed instead of reusing old fd");
		ok = check(
			readRawFile(displacedHeldLog) == displacedContentBefore &&
			readRawFile(logPath) == "ordinary-replacement",
			"held logger compares OS file identity and writes neither mismatched leaf") && ok;

		std::error_code removeError;
		const bool removed = fs::remove(logPath, removeError);
		ok = check(removed && !removeError,
			"ordinary replacement fixture removes the unrelated leaf") && ok;
		std::error_code restoreError;
		fs::rename(displacedHeldLog, logPath, restoreError);
		ok = check(!restoreError,
			"ordinary replacement fixture restores the verified held leaf: " +
				restoreError.message()) && ok;
		GameLog::write("logger resumes the restored held exact path");
		ok = check(
			readRawFile(logPath).find(
				"logger resumes the restored held exact path") !=
				std::string::npos,
			"after mismatch removal the logger writes only the restored inode") &&
			ok;
	}
#endif

	const fs::path priorGenerationLog =
		diagnosticsRoot / "prior-generation.log";
	const fs::path priorGenerationDiagnostics =
		diagnosticsRoot / "prior-generation-events.jsonl";
	const fs::path priorGenerationRuntimeTrace =
		diagnosticsRoot /
			"prior-generation-runtime-trace.jsonl";
	File::resetEditorRunFileLayout();
	std::error_code generationRenameError;
	fs::rename(logPath, priorGenerationLog, generationRenameError);
	std::error_code diagnosticsGenerationRenameError;
	fs::rename(
		diagnosticsPath,
		priorGenerationDiagnostics,
		diagnosticsGenerationRenameError);
	std::error_code runtimeTraceGenerationRenameError;
	fs::rename(
		runtimeTracePath,
		priorGenerationRuntimeTrace,
		runtimeTraceGenerationRenameError);
	ok = check(!generationRenameError,
		"reset closes the held editor-run descriptor without requiring a legacy write: " +
		generationRenameError.message()) && ok;
	ok = check(!diagnosticsGenerationRenameError,
		"closed structured diagnostics can be archived before the next generation: " +
			diagnosticsGenerationRenameError.message()) && ok;
	ok = check(!runtimeTraceGenerationRenameError,
		"closed runtime trace can be archived before the next generation: " +
			runtimeTraceGenerationRenameError.message()) && ok;
	ok = check(File::installEditorRunFileLayoutForTests(layout),
		"the next editor-run generation can reinstall the same exact output paths") &&
		ok;
	std::shared_ptr<EditorRun::DiagnosticsFileSink>
		newDiagnosticsSink;
	std::shared_ptr<EditorRun::RuntimeTraceFileSink>
		newRuntimeTraceSink;
	if (!generationRenameError &&
		!diagnosticsGenerationRenameError &&
		!runtimeTraceGenerationRenameError)
	{
		std::cout <<
			"FIXTURE: executed same-path editor-run generation reinstall" <<
			std::endl;
		const std::string priorGenerationContent =
			readRawFile(priorGenerationLog);
		const std::string priorDiagnosticsContent =
			readRawFile(priorGenerationDiagnostics);
		const std::string priorRuntimeTraceContent =
			readRawFile(priorGenerationRuntimeTrace);
		GameLog::write("same path belongs to the new editor-run generation");
		newDiagnosticsSink =
			EditorRun::DiagnosticsFileSink::open();
		EditorRun::DiagnosticsWriter newDiagnosticsWriter(
			"new-generation-session",
			newDiagnosticsSink
			? newDiagnosticsSink->lineSink()
			: EditorRun::DiagnosticLineSink{});
		EditorRun::DiagnosticEvent newDiagnostic;
		newDiagnostic.severity =
			EditorRun::DiagnosticSeverity::Info;
		newDiagnostic.code =
			"editor_run.test.new_generation";
		newDiagnostic.message = "new-generation 中文";
		const bool newDiagnosticsWritten =
			newDiagnosticsSink != nullptr &&
			newDiagnosticsWriter.write(newDiagnostic) &&
			newDiagnosticsWriter.emittedCount() == 1;
		newRuntimeTraceSink =
			EditorRun::RuntimeTraceFileSink::open();
		const bool newRuntimeTraceWritten =
			newRuntimeTraceSink != nullptr &&
			newRuntimeTraceSink->
				appendBatchAndFlush(
					"{\"sequence\":1,\"eventType\":\"session.start\"}\n");
		ok = check(
			readRawFile(priorGenerationLog) == priorGenerationContent &&
			readRawFile(logPath).find(
				"same path belongs to the new editor-run generation") !=
				std::string::npos &&
			newDiagnosticsWritten &&
			readRawFile(priorGenerationDiagnostics) ==
				priorDiagnosticsContent &&
			readRawFile(diagnosticsPath).find(
				"editor_run.test.new_generation") !=
				std::string::npos &&
			readRawFile(diagnosticsPath).find(
				"new-generation 中文") != std::string::npos,
			"same-path reinstall never reuses either prior-generation diagnostics descriptor") &&
			ok;
		ok = check(
			newRuntimeTraceWritten &&
			readRawFile(priorGenerationRuntimeTrace) ==
				priorRuntimeTraceContent &&
			readRawFile(runtimeTracePath).find(
				"\"eventType\":\"session.start\"") !=
				std::string::npos,
			"same-path reinstall never reuses the prior-generation runtime trace descriptor") &&
			ok;
	}
	ok = check(snapshotDirectoryTreeWithWriteTimes(formalRoot) == formalBefore,
		"editor-run central modification APIs leave the formal tree byte-identical") && ok;
	std::cout << "REQUIRED_SECURITY_FIXTURE_COUNT: " <<
		requiredSecurityFixtureCount << std::endl;
	ok = check(requiredSecurityFixtureCount == 31,
		"all 31 required editor-run private-output race, link, and hard-link fixtures execute without SKIP") &&
		ok;

	const fs::path resetClosedDiagnosticsPath =
		diagnosticsRoot / "reset-closed-events.jsonl";
	const fs::path resetClosedLogPath =
		diagnosticsRoot / "reset-closed-game.log";
	const fs::path resetClosedRuntimeTracePath =
		diagnosticsRoot /
			"reset-closed-runtime-trace.jsonl";
	bool writerPaused = false;
	bool logWriterPaused = false;
	bool traceWriterPaused = false;
	bool releaseWriter = false;
	bool resetFinished = false;
	bool concurrentWriteResult = false;
	bool concurrentTraceWriteResult = false;
	std::mutex lifecycleRaceMutex;
	std::condition_variable lifecycleRaceCondition;
	if (newDiagnosticsSink != nullptr)
	{
		announceRequiredSecurityFixture(
			"exact diagnostics and log appends serialized with layout reset");
		EditorRun::setDiagnosticsFileSinkWriteTestHookForTests(
			[&]()
			{
				std::unique_lock<std::mutex> lock(
					lifecycleRaceMutex);
				writerPaused = true;
				lifecycleRaceCondition.notify_all();
				lifecycleRaceCondition.wait(
					lock,
					[&]()
					{
						return releaseWriter;
					});
			});
		GameLog::setEditorRunLogWriteTestHookForTests(
			[&]()
			{
				std::unique_lock<std::mutex> lock(
					lifecycleRaceMutex);
				logWriterPaused = true;
				lifecycleRaceCondition.notify_all();
				lifecycleRaceCondition.wait(
					lock,
					[&]()
					{
						return releaseWriter;
					});
			});
		EditorRun::
			setRuntimeTraceFileSinkWriteTestHookForTests(
				[&]()
				{
					std::unique_lock<std::mutex> lock(
						lifecycleRaceMutex);
					traceWriterPaused = true;
					lifecycleRaceCondition.notify_all();
					lifecycleRaceCondition.wait(
						lock,
						[&]()
						{
							return releaseWriter;
						});
				});

		std::thread writerThread(
			[&]()
			{
				concurrentWriteResult =
					newDiagnosticsSink->appendAndFlush(
						"{\"sequence\":2,\"message\":\"reset race 中文\"}\n");
			});
		std::thread logWriterThread(
			[]()
			{
				GameLog::write(
					"editor-run log reset race 中文");
			});
		std::thread traceWriterThread(
			[&]()
			{
				concurrentTraceWriteResult =
					newRuntimeTraceSink != nullptr &&
					newRuntimeTraceSink->
						appendBatchAndFlush(
							"{\"sequence\":2,\"eventType\":\"source.line\"}\n");
			});
		bool writerReachedBarrier = false;
		{
			std::unique_lock<std::mutex> lock(
				lifecycleRaceMutex);
			writerReachedBarrier =
				lifecycleRaceCondition.wait_for(
					lock, std::chrono::seconds(5),
					[&]()
					{
						return writerPaused &&
							logWriterPaused &&
							traceWriterPaused;
					});
		}

		std::thread resetThread;
		const bool resetLockBlockedByWriters =
			writerReachedBarrier &&
			!File::
				editorRunFileLayoutResetLockIsAvailableForTests();
		if (writerReachedBarrier)
		{
			resetThread = std::thread(
				[&]()
				{
					File::resetEditorRunFileLayout();
					resetFinished = true;
				});
			{
				std::lock_guard<std::mutex> lock(
					lifecycleRaceMutex);
				releaseWriter = true;
			}
			lifecycleRaceCondition.notify_all();
		}
		else
		{
			{
				std::lock_guard<std::mutex> lock(
					lifecycleRaceMutex);
				releaseWriter = true;
			}
			lifecycleRaceCondition.notify_all();
		}
		writerThread.join();
		logWriterThread.join();
		traceWriterThread.join();
		if (resetThread.joinable())
		{
			resetThread.join();
		}
		else
		{
			File::resetEditorRunFileLayout();
			resetFinished = true;
		}
		EditorRun::setDiagnosticsFileSinkWriteTestHookForTests({});
		GameLog::setEditorRunLogWriteTestHookForTests({});
		EditorRun::
			setRuntimeTraceFileSinkWriteTestHookForTests({});

		const bool outputsClosedByReset =
			!newDiagnosticsSink->ownsOpenFileForTests() &&
			newRuntimeTraceSink != nullptr &&
			!newRuntimeTraceSink->
				ownsOpenFileForTests() &&
			!GameLog::editorRunLogOwnsOpenFileForTests();
		const std::string diagnosticsAfterReset =
			readRawFile(diagnosticsPath);
		const std::string logAfterReset =
			readRawFile(logPath);
		const std::string traceAfterReset =
			readRawFile(runtimeTracePath);
		std::error_code resetClosedRenameError;
		fs::rename(
			diagnosticsPath,
			resetClosedDiagnosticsPath,
			resetClosedRenameError);
		std::error_code resetClosedLogRenameError;
		fs::rename(
			logPath,
			resetClosedLogPath,
			resetClosedLogRenameError);
		std::error_code resetClosedTraceRenameError;
		fs::rename(
			runtimeTracePath,
			resetClosedRuntimeTracePath,
			resetClosedTraceRenameError);
		const bool staleWriteRejected =
			!newDiagnosticsSink->appendAndFlush(
				"{\"message\":\"stale generation\"}\n") &&
			readRawFile(resetClosedDiagnosticsPath) ==
				diagnosticsAfterReset;
		const bool staleTraceWriteRejected =
			!newRuntimeTraceSink->
				appendBatchAndFlush(
					"{\"eventType\":\"stale\"}\n") &&
			readRawFile(resetClosedRuntimeTracePath) ==
				traceAfterReset;
		ok = check(
			writerReachedBarrier &&
			resetLockBlockedByWriters &&
			concurrentWriteResult &&
			concurrentTraceWriteResult &&
			resetFinished &&
			File::
				editorRunFileLayoutResetLockIsAvailableForTests() &&
			outputsClosedByReset &&
			diagnosticsAfterReset.find(
				"reset race 中文") != std::string::npos &&
			logAfterReset.find(
				"editor-run log reset race 中文") !=
				std::string::npos &&
			traceAfterReset.find(
				"\"eventType\":\"source.line\"") !=
				std::string::npos &&
			staleWriteRejected &&
			staleTraceWriteRejected &&
			!resetClosedRenameError &&
			!resetClosedLogRenameError &&
			!resetClosedTraceRenameError,
			"layout reset waits for in-flight diagnostics, log, and trace appends, closes all held outputs, rejects stale sink writes, and releases every Windows leaf") &&
			ok;
	}
	else
	{
		ok = check(false,
			"structured diagnostics sink remains alive for the reset lifecycle fixture") &&
			ok;
		File::resetEditorRunFileLayout();
	}
	newDiagnosticsSink.reset();
	std::cout << "REQUIRED_SECURITY_FIXTURE_COUNT_AFTER_LIFECYCLE: " <<
		requiredSecurityFixtureCount << std::endl;
	ok = check(requiredSecurityFixtureCount == 32,
		"all 32 required editor-run private-output security and lifecycle fixtures execute without SKIP") &&
		ok;

	const fs::path destructorResetClosedDiagnosticsPath =
		diagnosticsRoot / "destructor-reset-closed-events.jsonl";
	const bool destructorLayoutInstalled =
		File::installEditorRunFileLayoutForTests(layout);
	ok = check(destructorLayoutInstalled,
		"destructor/reset fixture reinstalls a fresh exact-output generation") &&
		ok;
	std::shared_ptr<EditorRun::DiagnosticsFileSink>
		destructorRaceSink =
			destructorLayoutInstalled
			? EditorRun::DiagnosticsFileSink::open()
			: nullptr;
	ok = check(destructorRaceSink != nullptr,
		"destructor/reset fixture opens a registered diagnostics sink") &&
		ok;
	if (destructorRaceSink != nullptr)
	{
		announceRequiredSecurityFixture(
			"diagnostics last-owner destruction overlaps layout reset");
		bool destructorEntered = false;
		bool releaseDestructor = false;
		bool destructorResetFinished = false;
		std::mutex destructorRaceMutex;
		std::condition_variable destructorRaceCondition;
		EditorRun::
			setDiagnosticsFileSinkDestructorTestHookForTests(
				[&]()
				{
					std::unique_lock<std::mutex> lock(
						destructorRaceMutex);
					destructorEntered = true;
					destructorRaceCondition.notify_all();
					destructorRaceCondition.wait(
						lock,
						[&]()
						{
							return releaseDestructor;
						});
				});

		std::thread destructorThread(
			[sink = std::move(destructorRaceSink)]() mutable
			{
				sink.reset();
			});
		bool destructorReachedBarrier = false;
		{
			std::unique_lock<std::mutex> lock(
				destructorRaceMutex);
			destructorReachedBarrier =
				destructorRaceCondition.wait_for(
					lock, std::chrono::seconds(5),
					[&]()
					{
						return destructorEntered;
					});
		}

		std::thread destructorResetThread;
		bool resetReturnedWhileDestructorPaused = false;
		std::error_code destructorResetRenameError;
		if (destructorReachedBarrier)
		{
			destructorResetThread = std::thread(
				[&]()
				{
					File::resetEditorRunFileLayout();
					{
						std::lock_guard<std::mutex> lock(
							destructorRaceMutex);
						destructorResetFinished = true;
					}
					destructorRaceCondition.notify_all();
				});
			{
				std::unique_lock<std::mutex> lock(
					destructorRaceMutex);
				resetReturnedWhileDestructorPaused =
					destructorRaceCondition.wait_for(
						lock, std::chrono::seconds(5),
						[&]()
						{
							return destructorResetFinished;
						});
			}
			if (resetReturnedWhileDestructorPaused)
			{
				fs::rename(
					diagnosticsPath,
					destructorResetClosedDiagnosticsPath,
					destructorResetRenameError);
			}
		}

		{
			std::lock_guard<std::mutex> lock(
				destructorRaceMutex);
			releaseDestructor = true;
		}
		destructorRaceCondition.notify_all();
		destructorThread.join();
		if (destructorResetThread.joinable())
		{
			destructorResetThread.join();
		}
		else
		{
			File::resetEditorRunFileLayout();
		}
		EditorRun::
			setDiagnosticsFileSinkDestructorTestHookForTests({});

		ok = check(
			destructorReachedBarrier &&
			resetReturnedWhileDestructorPaused &&
			!destructorResetRenameError &&
			fs::exists(
				destructorResetClosedDiagnosticsPath) &&
			!File::hasEditorRunFileLayout(),
			"layout reset closes the independent diagnostics handle and permits immediate archive while the last-owner destructor remains paused") &&
			ok;
	}
	else if (destructorLayoutInstalled)
	{
		File::resetEditorRunFileLayout();
	}
	std::cout << "REQUIRED_SECURITY_FIXTURE_COUNT_AFTER_DESTRUCTOR_RESET: " <<
		requiredSecurityFixtureCount << std::endl;
	ok = check(requiredSecurityFixtureCount == 33,
		"all 33 required editor-run private-output security and lifecycle fixtures execute without SKIP") &&
		ok;
	ok = check(!File::hasEditorRunFileLayout(),
		"editor-run layout resets after runtime teardown") && ok;
	formalDescendantLinkError.clear();
	formalDescendantLinkCreated =
		createSymbolicLinkFixture(
			formalDescendantLinkTargetB,
			formalDescendantLink,
			true,
			formalDescendantLinkError);
	ok = check(formalDescendantLinkCreated,
		"ordinary formal getAssetsName link fixture is required: " +
			formalDescendantLinkError) && ok;
	if (formalDescendantLinkCreated)
	{
		const std::string resolvedLinkedAsset =
			File::getAssetsName(
				"asf/formal-current/current.txt");
		ok = check(
				normalizePath(resolvedLinkedAsset) ==
						normalizePath(
							(formalDescendantLink /
								"current.txt").u8string()) &&
					readRawFile(
						fs::u8path(resolvedLinkedAsset)) ==
						"formal-link-b",
			"getAssetsName returns the current lexical formal-resource link path without canonicalizing its target") &&
			ok;
		std::error_code getAssetsNameCleanupError;
		fs::remove(
			formalDescendantLink,
			getAssetsNameCleanupError);
		ok = check(!getAssetsNameCleanupError,
			"ordinary formal getAssetsName link fixture cleans up") &&
			ok;
		formalDescendantLinkCreated = false;
	}
	writeRawFile(normalSaveRoot /
		"not-installed-generation" / "old.txt",
		"normal-generation-old");
	bool notInstalledGenerationAdvanced = false;
	const bool notInstalledCopyResult = File::copyDirectoryFiles(
		"save/game/",
		"save/not-installed-generation/",
		{},
		[&](File::DirectoryCopyPhase phase)
		{
			if (phase == File::DirectoryCopyPhase::BeforeBackup)
			{
				File::resetEditorRunFileLayout();
				notInstalledGenerationAdvanced = true;
				std::cout <<
					"FIXTURE: executed NotInstalled transaction generation change" <<
					std::endl;
			}
			return false;
		});
	ok = check(!notInstalledCopyResult && notInstalledGenerationAdvanced,
		"a NotInstalled transaction stops when reset advances its routing generation") &&
		ok;
	ok = check(File::recoverDirectoryCopy(
			"save/not-installed-generation/") &&
		readRawFile(normalSaveRoot /
			"not-installed-generation" / "old.txt") ==
			"normal-generation-old",
		"a fresh NotInstalled transaction can recover artifacts from the old generation") &&
		ok;
	const char resetData[] = "normal-reset";
	ok = check(readViaFile("config/active-first.txt") == "active-first" &&
		File::writeFileChecked("config/after-reset.txt", resetData, 12) &&
		readRawFile(activeRoot / "config" / "after-reset.txt") == "normal-reset",
		"layout reset restores the ordinary desktop read and write routes") && ok;
	GameLog::use_log_file = true;
	GameLog::write("legacy log restored");
	ok = check(readRawFile(legacyLogPath).find("legacy log restored") !=
		std::string::npos,
		"layout reset restores the prior explicit log behavior") && ok;
	GameLog::use_log_file = false;
	GameLog::setLogFilePath("");
	File::setActiveResourceRoot("");
	File::setAssetsCollectionRoot("");
	File::setActiveSaveNamespace("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	return ok;
}

void writeInt32(std::vector<char>& data, size_t offset, int32_t value)
{
	std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::vector<char> createIMPFixture(int32_t frameCount, int32_t directions,
	const std::vector<int32_t>& frameDataLengths)
{
	size_t totalSize = IMPFormatValidation::ImageHeaderLength;
	for (int32_t dataLength : frameDataLengths)
	{
		totalSize += IMPFormatValidation::FrameHeaderLength;
		if (dataLength > 0)
		{
			totalSize += static_cast<size_t>(dataLength);
		}
	}
	std::vector<char> data(totalSize, 0);
	std::memcpy(data.data(), "IMG File Ver1.0", 16);
	writeInt32(data, 16, frameCount);
	writeInt32(data, 20, directions);
	writeInt32(data, 24, 100);
	size_t offset = IMPFormatValidation::ImageHeaderLength;
	for (int32_t dataLength : frameDataLengths)
	{
		writeInt32(data, offset, dataLength);
		offset += IMPFormatValidation::FrameHeaderLength;
		if (dataLength > 0)
		{
			offset += static_cast<size_t>(dataLength);
		}
	}
	return data;
}

bool testIMPFormatValidation()
{
	bool ok = true;
	auto valid = createIMPFixture(2, 1, { 4, 8 });
	ok = check(IMPFormatValidation::validate(valid.data(), static_cast<int>(valid.size())),
		"IMG validation accepts a complete multi-frame image") && ok;

	auto negativeFrameCount = createIMPFixture(-1, 1, {});
	ok = check(!IMPFormatValidation::validate(negativeFrameCount.data(),
		static_cast<int>(negativeFrameCount.size())),
		"IMG validation rejects a negative frame count before allocation") && ok;
	auto hugeFrameCount = createIMPFixture(2147483647, 1, {});
	ok = check(!IMPFormatValidation::validate(hugeFrameCount.data(),
		static_cast<int>(hugeFrameCount.size())),
		"IMG validation rejects a frame count larger than the remaining headers") && ok;
	auto invalidDirections = createIMPFixture(0, 0, {});
	ok = check(!IMPFormatValidation::validate(invalidDirections.data(),
		static_cast<int>(invalidDirections.size())),
		"IMG validation rejects non-positive direction counts") && ok;
	auto negativeDataLength = createIMPFixture(1, 1, { -1 });
	ok = check(!IMPFormatValidation::validate(negativeDataLength.data(),
		static_cast<int>(negativeDataLength.size())),
		"IMG validation rejects negative frame data lengths") && ok;
	auto truncatedData = createIMPFixture(1, 1, { 8 });
	truncatedData.resize(truncatedData.size() - 1);
	ok = check(!IMPFormatValidation::validate(truncatedData.data(),
		static_cast<int>(truncatedData.size())),
		"IMG validation rejects truncated frame data") && ok;
	auto truncatedSecondHeader = createIMPFixture(2, 1, { 4 });
	ok = check(!IMPFormatValidation::validate(truncatedSecondHeader.data(),
		static_cast<int>(truncatedSecondHeader.size())),
		"IMG validation rejects a missing later frame header") && ok;
	return ok;
}

bool testResourceManagerDependencySelection(const std::filesystem::path& root)
{
	namespace fs = std::filesystem;
	fs::path collectionRoot = root / "collection";
	fs::path baseRoot = collectionRoot / "base";
	fs::path secondBaseRoot = collectionRoot / "base2";
	fs::path uiBaseRoot = collectionRoot / "uibase";
	fs::path uiInferRoot = collectionRoot / "uiinfer";
	fs::path missingUiRoot = collectionRoot / "missingui";
	fs::path uiCycleARoot = collectionRoot / "uicyclea";
	fs::path uiCycleBRoot = collectionRoot / "uicycleb";
	fs::path invalidUiProfileRoot = collectionRoot / "invaliduiprofile";
	fs::path modRoot = collectionRoot / "mod";
	fs::path tooNewRoot = collectionRoot / "too-new";
	fs::path invalidMinimumRoot = collectionRoot / "invalid-minimum";
	fs::path chainModRoot = collectionRoot / "chainmod";
	fs::path middleModRoot = collectionRoot / "middlemod";
	fs::path multiModRoot = collectionRoot / "multimod";
	fs::path multiPathRoot = collectionRoot / "multipath";
	fs::path partialMultiRoot = collectionRoot / "partialmulti";
	fs::path pathModRoot = collectionRoot / "pathmod";
	fs::path pathMidRoot = collectionRoot / "pathmid";
	fs::path pathBaseRoot = collectionRoot / "pathbase";
	fs::path pathOnlyModRoot = collectionRoot / "pathonlymod";
	fs::path pathOnlyMidRoot = collectionRoot / "pathonlymid";
	fs::path pathOnlyBaseRoot = collectionRoot / "pathonlybase";
	fs::path pathCycleModRoot = collectionRoot / "pathcyclemod";
	fs::path pathCycleARoot = collectionRoot / "pathcyclea";
	fs::path pathCycleBRoot = collectionRoot / "pathcycleb";
	fs::path missingPathModRoot = collectionRoot / "missingpathmod";
	fs::path pathInvalidUiModRoot = collectionRoot / "pathinvaliduimod";
	fs::path pathInvalidUiBaseRoot = collectionRoot / "pathinvaliduibase";
	fs::path duplicateIdARoot = collectionRoot / "duplicateida";
	fs::path duplicateIdBRoot = collectionRoot / "duplicateidb";
	fs::path duplicateSaveARoot = collectionRoot / "duplicatesavea";
	fs::path duplicateSaveBRoot = collectionRoot / "duplicatesaveb";
	fs::path utf8PathModRoot = collectionRoot / std::filesystem::u8path(u8"中文模组");
	fs::path maximumManifestRoot = collectionRoot / "maximum-manifest";
	fs::path emptyManifestRoot = collectionRoot / "empty-manifest";
	fs::path oversizedManifestRoot = collectionRoot / "oversized-manifest";
	fs::path embeddedNullManifestRoot = collectionRoot / "embedded-null-manifest";
	fs::path escapedManifestRoot =
		collectionRoot / "escaped-manifest";
	fs::path standaloneType3Root = collectionRoot / "standalone-type3";
	fs::path orphanRoot = collectionRoot / "orphan";
	fs::path missingBaseRoot = collectionRoot / "missingbase";
	fs::path cycleARoot = collectionRoot / "cyclea";
	fs::path cycleBRoot = collectionRoot / "cycleb";
	fs::path commonRoot = collectionRoot / "common";

	writeRawFile(collectionRoot / "resources.ini",
		"[Collection]\n"
		"CommonPath=common\n"
		"\n"
		"[Pack.BASE]\n"
		"Id=BASE\n"
		"Path=base\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.BASE2]\n"
		"Id=BASE2\n"
		"Path=base2\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.UIBASE]\n"
		"Id=UIBASE\n"
		"Path=uibase\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.UIINFER]\n"
		"Id=UIINFER\n"
		"Path=uiinfer\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.MISSINGUI]\n"
		"Id=MISSINGUI\n"
		"Path=missingui\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.UICYCLEA]\n"
		"Id=UICYCLEA\n"
		"Path=uicyclea\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.UICYCLEB]\n"
		"Id=UICYCLEB\n"
		"Path=uicycleb\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.INVALIDUIPROFILE]\n"
		"Id=INVALIDUIPROFILE\n"
		"Path=invaliduiprofile\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.MOD]\n"
		"Id=MOD\n"
		"Path=mod\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"Author=Index Mod Author\n"
		"SaveNamespace=mod_save\n"
		"\n"
		"[Pack.TOONEW]\n"
		"Id=TOONEW\n"
		"Path=too-new\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.INVALIDMINIMUM]\n"
		"Id=INVALIDMINIMUM\n"
		"Path=invalid-minimum\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.CHAINMOD]\n"
		"Id=CHAINMOD\n"
		"Path=chainmod\n"
		"Manifest=game_profile.ini\n"
		"Base=MOD\n"
		"\n"
		"[Pack.MIDDLEMOD]\n"
		"Id=MIDDLEMOD\n"
		"Path=middlemod\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.MULTIMOD]\n"
		"Id=MULTIMOD\n"
		"Path=multimod\n"
		"Manifest=game_profile.ini\n"
		"Base=MIDDLEMOD, BASE2, BASE, middlemod\n"
		"\n"
		"[Pack.PARTIALMULTI]\n"
		"Id=PARTIALMULTI\n"
		"Path=partialmulti\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE, NOPE\n"
		"\n"
		"[Pack.PATHMOD]\n"
		"Id=PATHMOD\n"
		"Path=pathmod\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.PATHONLYMOD]\n"
		"Id=PATHONLYMOD\n"
		"Path=PathOnlyMod\n"
		"Manifest=Game_Profile.INI\n"
		"\n"
		"[Pack.PATHCYCLEMOD]\n"
		"Id=PATHCYCLEMOD\n"
		"Path=PathCycleMod\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.MISSINGPATHMOD]\n"
		"Id=MISSINGPATHMOD\n"
		"Path=MissingPathMod\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.PATHINVALIDUIMOD]\n"
		"Id=PATHINVALIDUIMOD\n"
		"Path=PathInvalidUiMod\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.DUPLICATEIDA]\n"
		"Id=DUPLICATE\n"
		"Path=DuplicateIdA\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"SaveNamespace=duplicate_a\n"
		"\n"
		"[Pack.DUPLICATEIDB]\n"
		"Id=DUPLICATE\n"
		"Path=DuplicateIdB\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"SaveNamespace=duplicate_b\n"
		"\n"
		"[Pack.DUPLICATESAVEA]\n"
		"Id=DUPSAVEA\n"
		"Path=DuplicateSaveA\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"SaveNamespace=shared.save\n"
		"\n"
		"[Pack.DUPLICATESAVEB]\n"
		"Id=DUPSAVEB\n"
		"Path=DuplicateSaveB\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"SaveNamespace=shared:save\n"
		"\n"
		"[Pack.UTF8PATHMOD]\n"
		"Id=UTF8PATHMOD\n"
		u8"Path=中文模组\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.MAXIMUMMANIFEST]\n"
		"Id=MAXIMUMMANIFEST\n"
		"Path=maximum-manifest\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.EMPTYMANIFEST]\n"
		"Id=EMPTYMANIFEST\n"
		"Path=empty-manifest\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.OVERSIZEDMANIFEST]\n"
		"Id=OVERSIZEDMANIFEST\n"
		"Path=oversized-manifest\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.EMBEDDEDNULLMANIFEST]\n"
		"Id=EMBEDDEDNULLMANIFEST\n"
		"Path=embedded-null-manifest\n"
		"Manifest=game_profile.ini\n"
		"Base=BASE\n"
		"\n"
		"[Pack.ESCAPEDMANIFEST]\n"
		"Id=ESCAPEDMANIFEST\n"
		"Path=escaped-manifest\n"
		"Manifest=../base/game_profile.ini\n"
		"\n"
		"[Pack.STANDALONETYPE3]\n"
		"Id=STANDALONETYPE3\n"
		"Path=standalone-type3\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.ORPHAN]\n"
		"Id=ORPHAN\n"
		"Path=orphan\n"
		"Manifest=game_profile.ini\n"
		"\n"
		"[Pack.MISSINGBASE]\n"
		"Id=MISSINGBASE\n"
		"Path=missingbase\n"
		"Manifest=game_profile.ini\n"
		"Base=NOPE\n"
		"\n"
		"[Pack.CYCLEA]\n"
		"Id=CYCLEA\n"
		"Path=cyclea\n"
		"Manifest=game_profile.ini\n"
		"Base=CYCLEB\n"
		"\n"
		"[Pack.CYCLEB]\n"
		"Id=CYCLEB\n"
		"Path=cycleb\n"
		"Manifest=game_profile.ini\n"
		"Base=CYCLEA\n");
	std::string resourceIndexWithEmbeddedNull =
		readRawFile(collectionRoot / "resources.ini");
	const std::string standaloneIndexSection =
		"[Pack.STANDALONETYPE3]\n";
	const std::size_t standaloneIndexPosition =
		resourceIndexWithEmbeddedNull.find(
			standaloneIndexSection);
	if (standaloneIndexPosition != std::string::npos)
	{
		resourceIndexWithEmbeddedNull.insert(
			standaloneIndexPosition +
				standaloneIndexSection.size(),
			std::string("Name=Ignored Index Name", 23) +
				std::string(1, '\0') +
				"suffix\n");
		writeRawFile(
			collectionRoot / "resources.ini",
			resourceIndexWithEmbeddedNull);
	}
	writeRawFile(baseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=BASE\n"
		"Name=Base\n"
		"Type=2\n"
		"UseWav=1\n");
	writeRawFile(secondBaseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=BASE2\n"
		"Name=Second Base\n"
		"Type=0\n");
	writeRawFile(uiBaseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=UIBASE\n"
		"Name=UI Base\n"
		"Type=2\n");
	writeRawFile(uiInferRoot / "game_profile.ini",
		"[Game]\n"
		"Id=UIINFER\n"
		"Name=UI Infer\n"
		"\n"
		"[UI]\n"
		"BaseId=UIBASE\n");
	writeRawFile(missingUiRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MISSINGUI\n"
		"Name=Missing UI\n"
		"\n"
		"[Resource]\n"
		"DependencyId=BASE\n"
		"\n"
		"[UI]\n"
		"BaseId=NO_UI\n");
	writeRawFile(uiCycleARoot / "game_profile.ini",
		"[Game]\n"
		"Id=UICYCLEA\n"
		"Name=UI Cycle A\n"
		"\n"
		"[UI]\n"
		"BaseId=UICYCLEB\n");
	writeRawFile(uiCycleBRoot / "game_profile.ini",
		"[Game]\n"
		"Id=UICYCLEB\n"
		"Name=UI Cycle B\n"
		"\n"
		"[UI]\n"
		"BaseId=UICYCLEA\n");
	writeRawFile(invalidUiProfileRoot / "game_profile.ini",
		"[Game]\n"
		"Id=INVALIDUIPROFILE\n"
		"Name=Invalid UI Profile\n"
		"\n"
		"[UI]\n"
		"Profile=UNKNOWN\n");
	writeRawFile(modRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MOD\n"
		"Name=Mod\n"
		"Author=Profile Mod Author\n"
		"\n"
		"[Resource]\n"
		"DependencyId=BASE\n"
		"\n"
		"[UI]\n"
		"BaseId=UIBASE\n"
		"Profile=YYCS\n"
		"PreferLocal=1\n"
		"\n"
		"[Features]\n"
		"FreezeVisualEffect=0\n"
		"MagicTriggerAtAnimationEnd=1\n"
		"\n"
		"[Save]\n"
		"Namespace=mod_save\n");
	writeRawFile(tooNewRoot / "game_profile.ini",
		"[Game]\n"
		"Id=TOONEW\n"
		"Name=Too New Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=BASE\n"
		"\n"
		"[Release]\n"
		"MinimumEngineVersion=9999.0.0\n");
	writeRawFile(invalidMinimumRoot / "game_profile.ini",
		"[Game]\n"
		"Id=INVALIDMINIMUM\n"
		"Name=Invalid Minimum Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=BASE\n"
		"\n"
		"[Release]\n"
		"MinimumEngineVersion=not-semver\n");
	writeRawFile(chainModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=CHAINMOD\n"
		"Name=Chain Mod\n"
		"[Resource]\n"
		"DependencyId=MOD\n");
	writeRawFile(middleModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MIDDLEMOD\n"
		"Name=Middle Mod\n"
		"[Resource]\n"
		"DependencyId=BASE\n");
	writeRawFile(multiModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MULTIMOD\n"
		"Name=Multi Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=MIDDLEMOD, BASE2, BASE, MULTIPATH, middlemod\n");
	writeRawFile(partialMultiRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PARTIALMULTI\n"
		"Name=Partial Multi Mod\n");
	writeRawFile(pathModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHMOD\n"
		"Name=Path Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=BASE, PATHMID\n");
	writeRawFile(pathMidRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHMID\n"
		"Name=Path Mid\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHBASE\n");
	writeRawFile(pathBaseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHBASE\n"
		"Name=Path Base\n"
		"Type=1\n"
		"UseWav=1\n");
	writeRawFile(pathBaseRoot / "config" / "path-grandparent-only.txt", "path-grandparent");
	writeRawFile(pathOnlyModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHONLYMOD\n"
		"Name=Path Only Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=pathonlymid\n");
	writeRawFile(pathOnlyMidRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHONLYMID\n"
		"Name=Path Only Mid\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHONLYBASE\n");
	writeRawFile(pathOnlyBaseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHONLYBASE\n"
		"Name=Path Only Base\n"
		"Type=1\n");
	writeRawFile(pathOnlyMidRoot / "config" / "path-mid-only.txt", "path-mid");
	writeRawFile(pathOnlyBaseRoot / "config" / "path-base-only.txt", "path-base");
	writeRawFile(pathOnlyBaseRoot / "ini" / "ui" / "path-base-ui.txt", "path-base-ui");
	writeRawFile(pathCycleModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHCYCLEMOD\n"
		"Name=Path Cycle Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHCYCLEA\n");
	writeRawFile(pathCycleARoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHCYCLEA\n"
		"Name=Path Cycle A\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHCYCLEB\n");
	writeRawFile(pathCycleBRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHCYCLEB\n"
		"Name=Path Cycle B\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHCYCLEA\n");
	writeRawFile(missingPathModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MISSINGPATHMOD\n"
		"Name=Missing Path Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=NOSUCHPATH\n");
	writeRawFile(
		missingPathModRoot / "config" / "local-only.txt",
		"missing-path-local");
	writeRawFile(pathInvalidUiModRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHINVALIDUIMOD\n"
		"Name=Path Invalid UI Mod\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHINVALIDUIBASE\n");
	writeRawFile(pathInvalidUiBaseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PATHINVALIDUIBASE\n"
		"Name=Path Invalid UI Base\n"
		"\n"
		"[Resource]\n"
		"DependencyId=PATHONLYBASE\n"
		"\n"
		"[UI]\n"
		"Profile=unsupported_path_profile\n");
	writeRawFile(duplicateIdARoot / "game_profile.ini", "[Game]\nId=DUPLICATE\nName=Duplicate Id A\n");
	writeRawFile(duplicateIdBRoot / "game_profile.ini", "[Game]\nId=DUPLICATE\nName=Duplicate Id B\n");
	writeRawFile(
		duplicateSaveARoot / "game_profile.ini",
		"[Game]\nId=DUPSAVEA\nName=Duplicate Save A\n"
		"[Save]\nNamespace=shared.save\n");
	writeRawFile(
		duplicateSaveBRoot / "game_profile.ini",
		"[Game]\nId=DUPSAVEB\nName=Duplicate Save B\n"
		"[Save]\nNamespace=shared:save\n");
	writeRawFile(utf8PathModRoot / "game_profile.ini", u8"[Game]\nId=UTF8PATHMOD\nName=中文路径模组\n");
	std::string maximumManifest =
		"[Game]\n"
		"Id=MAXIMUMMANIFEST\n"
		"Name=Maximum Manifest\n";
	maximumManifest.resize(1024 * 1024, '\n');
	writeRawFile(
		maximumManifestRoot / "game_profile.ini",
		maximumManifest);
	writeRawFile(emptyManifestRoot / "game_profile.ini", "");
	writeRawFile(
		oversizedManifestRoot / "game_profile.ini",
		std::string(1024 * 1024 + 1, 'x'));
	writeRawFile(
		embeddedNullManifestRoot / "game_profile.ini",
		std::string("[Game]\nId=EMBEDDEDNULL", 22) +
			std::string(1, '\0') +
			"\nName=Embedded Null\n");
	writeRawFile(
		escapedManifestRoot / "config" / "local-only.txt",
		"must-not-create-a-pack");
	writeRawFile(
		standaloneType3Root / "game_profile.ini",
		"[Game]\n"
		"Id=STANDALONETYPE3\n"
		"Name=Standalone Type 3\n"
		"Type=3\n");
	writeRawFile(
		standaloneType3Root / "config" / "local-only.txt",
		"standalone-type3-local");
	writeRawFile(orphanRoot / "game_profile.ini",
		"[Game]\n"
		"Id=ORPHAN\n"
		"Name=Orphan\n"
		"Type=99\n");
	writeRawFile(missingBaseRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MISSINGBASE\n"
		"Name=Missing Base\n"
		"Type=99\n");
	writeRawFile(
		missingBaseRoot / "config" / "local-only.txt",
		"missing-base-local");
	writeRawFile(
		missingUiRoot / "config" / "local-only.txt",
		"missing-ui-local");
	writeRawFile(cycleARoot / "game_profile.ini",
		"[Game]\n"
		"Id=CYCLEA\n"
		"Name=Cycle A\n"
		"Type=99\n");
	writeRawFile(cycleBRoot / "game_profile.ini",
		"[Game]\n"
		"Id=CYCLEB\n"
		"Name=Cycle B\n"
		"Type=99\n");
	writeRawFile(baseRoot / "config" / "base-only.txt", "base-from-manager");
	writeRawFile(baseRoot / "config" / "depth-priority.txt", "first-parent-depth");
	writeRawFile(baseRoot / "ini" / "ui" / "legacy-multi.txt", "first-parent-ui-depth");
	writeRawFile(secondBaseRoot / "config" / "base2-only.txt", "second-base-only");
	writeRawFile(secondBaseRoot / "config" / "depth-priority.txt", "second-direct-parent");
	writeRawFile(secondBaseRoot / "ini" / "ui" / "legacy-multi.txt", "second-parent-ui");
	writeRawFile(middleModRoot / "config" / "middle-only.txt", "middle-mod-only");
	writeRawFile(multiPathRoot / "config" / "path-only.txt", "multi-path-only");
	writeRawFile(multiPathRoot / "config" / "depth-priority.txt", "dependency-path-last");
	writeRawFile(multiPathRoot / "game_profile.ini",
		"[Game]\n"
		"Id=MULTIPATH\n"
		"Name=Multi Path\n"
		"Type=1\n");
	writeRawFile(baseRoot / "ini" / "ui" / "domain.txt", "content-base-ui");
	writeRawFile(baseRoot / "config" / "ui-base-only.txt", "content-base-config");
	writeRawFile(uiBaseRoot / "ini" / "ui" / "domain.txt", "ui-base-ui");
	writeRawFile(uiBaseRoot / "ini" / "ui" / "ui-only.txt", "ui-only");
	writeRawFile(uiBaseRoot / "config" / "ui-base-only.txt", "ui-base-config");
	writeRawFile(modRoot / "ini" / "ui" / "domain.txt", "local-ui");
	writeRawFile(baseRoot / "config" / "shared-with-common.txt", "base-over-common");
	writeRawFile(baseRoot / "save" / "game" / "base-only.sav", "base-save");
	writeRawFile(commonRoot / "config" / "common-only.txt", "common-from-manager");
	writeRawFile(commonRoot / "config" / "shared-with-common.txt", "common");
	writeRawFile(commonRoot / "font" / "font.ttf", "manager-common-font");
	writeRawFile(commonRoot / "ini" / "ui" / "selection-only.txt",
		"collection-common-selection-ui");
	writeRawFile(modRoot / "font" / "font.ttf", "manager-mod-font");
	writeRawFile(commonRoot / "save" / "game" / "common-only.sav", "common-save");
	const fs::path bundledRecentSelectionPath =
		collectionRoot / "save" / "system" /
		"resource_selection.ini";
	const std::string bundledRecentSelection =
		"[ResourceSelection]\n"
		"Id=mod\n"
		"RootPath=missing-old-path/\n";
	writeRawFile(
		bundledRecentSelectionPath,
		bundledRecentSelection);

	ResourceManager& manager = ResourceManager::instance();
	bool ok = check(manager.initialize(collectionRoot.string()), "ResourceManager initializes collection");
	RuntimeResource::ResourceCatalogFileAccess statlessPackagedAccess;
	statlessPackagedAccess.readFileFromRoot =
		[](const fs::path& fileRoot,
			std::string_view relativePath,
			std::size_t maximumBytes)
		{
			RuntimeResource::CatalogFileReadResult result;
			const std::string relativeText(relativePath);
			if (!File::isSafeResourcePath(relativeText))
			{
				result.status =
					RuntimeResource::CatalogFileReadStatus::UnsafePath;
				return result;
			}
			const fs::path path =
				(fileRoot / fs::u8path(relativeText)).
					lexically_normal();
			std::error_code error;
			if (!fs::is_regular_file(path, error) || error)
			{
				result.status =
					RuntimeResource::CatalogFileReadStatus::NotFound;
				return result;
			}
			const std::uintmax_t size =
				fs::file_size(path, error);
			if (error)
			{
				result.status =
					RuntimeResource::CatalogFileReadStatus::Unavailable;
				return result;
			}
			if (size > maximumBytes)
			{
				result.status =
					RuntimeResource::CatalogFileReadStatus::TooLarge;
				return result;
			}
			const std::string bytes = readRawFile(path);
			result.bytes.assign(bytes.begin(), bytes.end());
			result.status =
				RuntimeResource::CatalogFileReadStatus::Success;
			return result;
		};
	statlessPackagedAccess.getDirectoryStatus =
		[](const fs::path& directory)
		{
			std::error_code error;
			if (fs::is_regular_file(
					directory / "game_profile.ini",
					error) &&
				!error)
			{
				return RuntimeResource::
					CatalogDirectoryStatus::Exists;
			}
			return RuntimeResource::CatalogDirectoryStatus::Unknown;
		};
	statlessPackagedAccess.listChildDirectories =
		[](const fs::path& directory)
		{
			RuntimeResource::CatalogDirectoryListResult result;
			std::error_code error;
			fs::directory_iterator iterator(directory, error);
			const fs::directory_iterator end;
			while (!error && iterator != end)
			{
				if (iterator->is_directory(error) && !error)
				{
					result.childDirectoryNames.push_back(
						iterator->path().filename().u8string());
				}
				iterator.increment(error);
			}
			if (!error)
			{
				result.status = RuntimeResource::
					CatalogDirectoryListStatus::Success;
			}
			return result;
		};
	const RuntimeResource::ResourceCatalogSnapshotResult
		statlessSnapshot =
			RuntimeResource::loadResourceCatalogSnapshot(
				collectionRoot,
				statlessPackagedAccess);
	const auto hasStandaloneType3Diagnostic =
		[&statlessSnapshot](std::string_view code)
		{
			return std::any_of(
				statlessSnapshot.snapshot.diagnostics.begin(),
				statlessSnapshot.snapshot.diagnostics.end(),
				[code](const RuntimeResource::CatalogDiagnostic& diagnostic)
				{
					return diagnostic.code == code &&
						diagnostic.stableEntryKey ==
							"pack.standalone-type3" &&
						diagnostic.resourcePackId ==
							"STANDALONETYPE3" &&
						diagnostic.severity ==
							RuntimeResource::
								CatalogDiagnosticSeverity::Warning;
				});
		};
	ok = check(
		hasStandaloneType3Diagnostic(
			"resource.catalog.release_metadata_defaulted"),
		"standalone Type=3 reports the structured release metadata default warning") &&
		ok;
	ok = check(
		hasStandaloneType3Diagnostic(
			"resource.catalog.cover_defaulted"),
		"standalone Type=3 reports the structured cover default warning") &&
		ok;
	ok = check(
		hasStandaloneType3Diagnostic(
			"resource.catalog.description_defaulted"),
		"standalone Type=3 reports the structured description default warning") &&
		ok;
	ok = check(
		hasStandaloneType3Diagnostic(
			"resource.catalog.ui_defaulted"),
		"standalone Type=3 reports the structured UI default warning") &&
		ok;
	const RuntimeResource::ExactSelectionResult
		statlessPathSelection =
			RuntimeResource::resolveResourceCatalogEntrySelection(
				collectionRoot,
				"pack.pathonlymod",
				statlessPackagedAccess);
	ok = check(
		statlessSnapshot.succeeded() &&
			std::any_of(
				statlessSnapshot.snapshot.entries.begin(),
				statlessSnapshot.snapshot.entries.end(),
				[](const RuntimeResource::ResourceCatalogEntry& entry)
				{
					return entry.stableKey ==
						"pack.standalone-type3";
				}) &&
			statlessPathSelection.succeeded() &&
			statlessPathSelection.selection.activeManifest.type == 1 &&
			statlessPathSelection.selection.activeManifest.uiProfile ==
				"YYCS" &&
			statlessPathSelection.selection.orderedContentRoots.size() >= 3,
		"a statless APK/bundle directory adapter still uses the shared catalog for discovery, DependencyId, Game.Type, and UI materialization") &&
		ok;
	ok = check(manager.needsSelection(), "ResourceManager detects multi-pack selection") && ok;
	ok = check(readViaCommonResourceFile("font/font.ttf") == "manager-common-font",
		"ResourceManager configures the common root before resource selection") && ok;
	ok = check(readViaFile("ini/ui/selection-only.txt") ==
		"collection-common-selection-ui",
		"unselected multi-pack UI resolves through collection common without activating a candidate") && ok;

	int modIndex = -1;
	int tooNewIndex = -1;
	int invalidMinimumIndex = -1;
	int chainModIndex = -1;
	int multiModIndex = -1;
	int pathModIndex = -1;
	int pathOnlyModIndex = -1;
	int uiInferIndex = -1;
	int utf8PathModIndex = -1;
	int maximumManifestIndex = -1;
	int standaloneType3Index = -1;
	int missingBaseIndex = -1;
	int missingUiIndex = -1;
	int missingPathIndex = -1;
	int duplicateIdAIndex = -1;
	int duplicateIdBIndex = -1;
	int duplicateSaveAIndex = -1;
	int duplicateSaveBIndex = -1;
	bool hasCyclePack = false;
	bool hasInvalidBasePack = false;
	bool hasInvalidUiPack = false;
	bool hasInvalidPathUiPack = false;
	bool hasInvalidPathPack = false;
	bool hasAmbiguousIdentityPack = false;
	bool hasUnreadableManifestPack = false;
	bool hasSanitizedEmbeddedNullManifestPack = false;
	bool hasEscapedManifestPack = false;
	const auto& packs = manager.getDiscoveredPacks();
	ok = check(
		!packs.empty()
			&& packs.front().wasRecentlySelected
			&& std::count_if(
				packs.begin(), packs.end(),
				[](const ResourceManager::ResourcePack& pack)
				{
					return pack.wasRecentlySelected;
				}) == 1,
		"a valid bundled recent selection marks exactly the promoted resource pack") &&
		ok;
	ok = check(!packs.empty() && packs.front().manifest.id == "MOD",
		"ResourceManager promotes the most recently selected pack to the first position") && ok;
	for (int i = 0; i < (int)packs.size(); i++)
	{
		if (packs[i].manifest.id == "MOD")
		{
			modIndex = i;
		}
		if (packs[i].manifest.id == "TOONEW")
		{
			tooNewIndex = i;
		}
		if (packs[i].manifest.id == "INVALIDMINIMUM")
		{
			invalidMinimumIndex = i;
		}
		if (packs[i].manifest.id == "CHAINMOD")
		{
			chainModIndex = i;
		}
		if (packs[i].manifest.id == "MULTIMOD")
		{
			multiModIndex = i;
		}
		if (packs[i].manifest.id == "PATHMOD")
		{
			pathModIndex = i;
		}
		if (packs[i].manifest.id == "PATHONLYMOD")
		{
			pathOnlyModIndex = i;
		}
		if (packs[i].manifest.id == "UIINFER")
		{
			uiInferIndex = i;
		}
		if (packs[i].manifest.id == "UTF8PATHMOD")
		{
			utf8PathModIndex = i;
		}
		if (packs[i].manifest.id == "MAXIMUMMANIFEST")
		{
			maximumManifestIndex = i;
		}
		if (packs[i].manifest.id == "STANDALONETYPE3")
		{
			standaloneType3Index = i;
		}
		if (packs[i].manifest.id == "MISSINGBASE")
		{
			missingBaseIndex = i;
		}
		if (packs[i].manifest.id == "MISSINGUI")
		{
			missingUiIndex = i;
		}
		if (packs[i].manifest.id == "MISSINGPATHMOD")
		{
			missingPathIndex = i;
		}
		if (packs[i].manifest.id == "DUPLICATE")
		{
			if (duplicateIdAIndex < 0)
			{
				duplicateIdAIndex = i;
			}
			else
			{
				duplicateIdBIndex = i;
			}
		}
		if (packs[i].manifest.id == "DUPSAVEA")
		{
			duplicateSaveAIndex = i;
		}
		if (packs[i].manifest.id == "DUPSAVEB")
		{
			duplicateSaveBIndex = i;
		}
		if (packs[i].manifest.id == "CYCLEA" || packs[i].manifest.id == "CYCLEB")
		{
			hasCyclePack = true;
		}
		if (packs[i].manifest.id == "ORPHAN" || packs[i].manifest.id == "MISSINGBASE" ||
			packs[i].manifest.id == "PARTIALMULTI")
		{
			hasInvalidBasePack = true;
		}
		if (packs[i].manifest.id == "MISSINGUI" || packs[i].manifest.id == "UICYCLEA" ||
			packs[i].manifest.id == "UICYCLEB" || packs[i].manifest.id == "INVALIDUIPROFILE")
		{
			hasInvalidUiPack = true;
		}
		if (packs[i].manifest.id == "PATHCYCLEMOD" || packs[i].manifest.id == "MISSINGPATHMOD")
		{
			hasInvalidPathPack = true;
		}
		if (packs[i].manifest.id == "PATHINVALIDUIMOD")
		{
			hasInvalidPathUiPack = true;
		}
		if (packs[i].manifest.id == "DUPLICATE" || packs[i].manifest.id == "DUPSAVEA" ||
			packs[i].manifest.id == "DUPSAVEB")
		{
			hasAmbiguousIdentityPack = true;
		}
		if (packs[i].manifest.id == "EMPTYMANIFEST" ||
			packs[i].manifest.id == "OVERSIZEDMANIFEST")
		{
			hasUnreadableManifestPack = true;
		}
		if (packs[i].manifest.id == "EMBEDDEDNULLMANIFEST")
		{
			hasSanitizedEmbeddedNullManifestPack = true;
		}
		if (packs[i].manifest.id == "ESCAPEDMANIFEST")
		{
			hasEscapedManifestPack = true;
		}
	}
	ok = check(modIndex >= 0, "ResourceManager finds MOD pack") && ok;
	ok = check(tooNewIndex >= 0 && invalidMinimumIndex >= 0,
		"ResourceManager keeps incompatible MOD packs visible for selection") && ok;
	ok = check(chainModIndex >= 0, "ResourceManager keeps mod depending on another valid mod") && ok;
	ok = check(multiModIndex >= 0, "ResourceManager keeps a valid ordered multi-parent mod") && ok;
	ok = check(pathModIndex >= 0, "ResourceManager keeps path-dependent mod with valid indexed base") && ok;
	ok = check(pathOnlyModIndex >= 0,
		"ResourceManager keeps a path-only mod whose dependency chain reaches a trilogy base") && ok;
	ok = check(uiInferIndex >= 0, "ResourceManager keeps a mod with a valid independent UI base") && ok;
	ok = check(!manager.rememberResourcePackSelection(-1),
		"ResourceManager rejects an invalid recent-selection index") && ok;
#ifndef __MOBILE__
	const fs::path userRecentSelectionPath =
		root / "UserState" / "save" / "system" /
		"resource_selection.ini";
	ok = check(
		manager.rememberResourcePackSelection(
			chainModIndex) &&
			fs::is_regular_file(
				userRecentSelectionPath) &&
			readRawFile(
				bundledRecentSelectionPath) ==
				bundledRecentSelection,
		"ResourceManager saves a confirmed selection in shared user state without modifying the bundled collection fallback") &&
		ok;
	std::unique_ptr<char[]> recentSelectionData;
	int recentSelectionLength = 0;
	const bool recentSelectionReadable =
		File::readSharedApplicationFile(
			"save/system/resource_selection.ini",
			recentSelectionData,
			recentSelectionLength,
			16 * 1024);
	ok = check(
		recentSelectionReadable,
		"recent resource selection is readable through shared user-state routing") &&
		ok;
	INIReader recentSelection(recentSelectionData);
	const std::string recentSelectionBytes =
		recentSelectionData != nullptr &&
			recentSelectionLength >= 0
			? std::string(
				  recentSelectionData.get(),
				  static_cast<std::size_t>(
					  recentSelectionLength))
			: std::string();
	ok = check(
		recentSelection.Get(
			"ResourceSelection",
			"EntryKey",
			"") ==
				packs[chainModIndex].
					selectionEntryKey &&
			recentSelection.Get(
				"ResourceSelection",
				"Id",
				"") ==
				"CHAINMOD" &&
			normalizePath(
				recentSelection.Get(
					"ResourceSelection",
					"RootPath",
					"")) ==
				normalizePath(
					packs[chainModIndex].
						rootPath) &&
			readRawFile(
				userRecentSelectionPath) ==
				recentSelectionBytes,
		"recent resource selection persists stable entry key, id, and root fallback in shared user state") &&
		ok;
#endif
	ok = check(utf8PathModIndex >= 0 &&
		normalizePath(packs[utf8PathModIndex].rootPath).find(u8"/中文模组/") != std::string::npos,
		"ResourceManager loads indexed manifests from UTF-8 resource roots") && ok;
	ok = check(maximumManifestIndex >= 0,
		"ResourceManager accepts a manifest at the one-MiB byte limit") && ok;
	ok = check(
		standaloneType3Index >= 0,
		"ResourceManager retains an explicit standalone Type=3 custom pack") &&
		ok;
	ok = check(hasCyclePack,
		"ResourceManager retains cyclic packs and isolates the bad dependency branch") &&
		ok;
	ok = check(hasInvalidBasePack,
		"ResourceManager retains standalone and partially unresolved multi-parent packs") &&
		ok;
	ok = check(hasInvalidUiPack,
		"ResourceManager retains packs with missing, cyclic, or unsupported UI configuration") &&
		ok;
	ok = check(hasInvalidPathUiPack,
		"ResourceManager retains local content when a DependencyId ancestor has invalid UI metadata") &&
		ok;
	ok = check(hasInvalidPathPack,
		"ResourceManager retains packs with missing or cyclic DependencyId branches") &&
		ok;
	ok = check(hasAmbiguousIdentityPack,
		"ResourceManager retains duplicate Game.Id and portable Save.Namespace owners") &&
		ok;
	ok = check(!hasUnreadableManifestPack &&
		!hasSanitizedEmbeddedNullManifestPack,
		"ResourceManager skips manifests whose embedded-NUL line removes the required profile ID instead of borrowing identity from resources.ini") &&
		ok;
	ok = check(
		!hasEscapedManifestPack &&
			modIndex >= 0,
		"an indexed Manifest path cannot escape its pack root and only the affected entry is skipped on every platform") &&
		ok;
	if (tooNewIndex >= 0 && invalidMinimumIndex >= 0)
	{
		ok = check(
			packs[tooNewIndex].compatibility.status ==
				ModRelease::CompatibilityStatus::RequiresNewerEngine &&
			packs[invalidMinimumIndex].compatibility.status ==
				ModRelease::CompatibilityStatus::InvalidMinimumEngineVersion,
			"ResourceManager caches each pack's engine compatibility result") && ok;
		ModRelease::CompatibilityResult activationCompatibility;
		ok = check(!manager.setActiveResourcePack(
			tooNewIndex, &activationCompatibility) &&
			activationCompatibility.status ==
				ModRelease::CompatibilityStatus::RequiresNewerEngine &&
			!manager.hasActiveResourceRoot(),
			"a valid newer-engine requirement blocks MOD activation") && ok;
		// 格式无效只记录未知兼容性，不把不规范字段当作运行许可开关。
		ok = check(manager.setActiveResourcePackById(
			"INVALIDMINIMUM", &activationCompatibility) &&
			activationCompatibility.status ==
				ModRelease::CompatibilityStatus::InvalidMinimumEngineVersion &&
			manager.hasActiveResourceRoot(),
			"id activation ignores an invalid minimum engine version and treats the pack as runnable") && ok;
	}
	ok = check(
		standaloneType3Index >= 0 &&
			packs[standaloneType3Index].getDisplayName() ==
				"Standalone Type 3" &&
			manager.setActiveResourcePack(
				standaloneType3Index) &&
			manager.getActiveManifest().type == 3 &&
			manager.getActiveManifest().uiProfile ==
				"JXQY2" &&
			manager.getActiveManifest().
				releaseMetadata.displayVersion.empty() &&
			manager.getActiveManifest().
				releaseMetadata.coverPath.empty() &&
			manager.getActiveManifest().
				releaseMetadata.descriptionFilePath.empty() &&
			readViaFile("config/local-only.txt") ==
				"standalone-type3-local",
		"resources.ini embedded-NUL lines are isolated while standalone Type=3 and missing Release/cover/description/UI fields use stable defaults without blocking local content") &&
		ok;
	ok = check(
		missingBaseIndex >= 0 &&
			manager.setActiveResourcePack(missingBaseIndex) &&
			readViaFile("config/local-only.txt") ==
				"missing-base-local",
		"a missing content dependency affects only the fallback branch and keeps local content runnable") &&
		ok;
	ok = check(
		missingUiIndex >= 0 &&
			manager.setActiveResourcePack(missingUiIndex) &&
			manager.getActiveManifest().uiProfile ==
				"XJXQY" &&
			readViaFile("config/local-only.txt") ==
				"missing-ui-local",
		"a missing UI dependency falls back by effective Game.Type without disabling local content") &&
		ok;
	const fs::path lateMissingDependencyRoot =
		collectionRoot / "NoSuchPath";
	ok = check(
		missingPathIndex >= 0 &&
			!manager.setActiveResourcePack(missingPathIndex),
		"a missing DependencyId remains visible but cannot be activated") && ok;
	writeRawFile(
		lateMissingDependencyRoot / "config" /
			"late-created.txt",
		"must-not-be-routed");
	ok = check(
		readViaFile("config/late-created.txt").empty(),
		"a DependencyId that was unavailable during shared materialization is omitted from the installed fallback roots") &&
		ok;
	fs::remove_all(lateMissingDependencyRoot);
	ok = check(
		duplicateIdAIndex >= 0 &&
			duplicateIdBIndex >= 0 &&
			packs[duplicateIdAIndex].selectionEntryKey !=
				packs[duplicateIdBIndex].selectionEntryKey &&
			!manager.setActiveResourcePackById("DUPLICATE") &&
			!manager.setActiveResourcePack(duplicateIdAIndex) &&
			!manager.setActiveResourcePack(duplicateIdBIndex),
		"duplicate Game.Id owners remain visible but cannot be activated") &&
		ok;
	ok = check(
		duplicateSaveAIndex >= 0 &&
			duplicateSaveBIndex >= 0 &&
			!packs[duplicateSaveAIndex].
				saveNamespaceAdjusted &&
			packs[duplicateSaveBIndex].
				saveNamespaceAdjusted &&
			packs[duplicateSaveAIndex].
				effectiveSaveNamespace !=
				packs[duplicateSaveBIndex].
					effectiveSaveNamespace &&
			manager.setActiveResourcePack(
				duplicateSaveBIndex) &&
			File::getActiveSaveNamespace() ==
				packs[duplicateSaveBIndex].
					effectiveSaveNamespace,
		"portable save-namespace conflicts retain every pack and assign a stable effective suffix") &&
		ok;
	ok = check(!manager.setActiveResourcePackById("NOPE"),
		"ResourceManager rejects unknown resource pack id") && ok;
	ok = check(manager.setActiveResourcePackById("chainmod"),
		"ResourceManager selects pack by id case-insensitively") && ok;
	ModRelease::CompatibilityResult blockedCompatibility;
	ok = check(!manager.setActiveResourcePackById(
		"TOONEW", &blockedCompatibility) &&
		blockedCompatibility.status ==
			ModRelease::CompatibilityStatus::RequiresNewerEngine &&
		manager.getActiveManifest().id == "CHAINMOD",
		"a newer-engine declaration is reported and leaves existing routing unchanged") && ok;
	ok = check(manager.setActiveResourcePackById("CHAINMOD"),
		"ResourceManager can restore the dependency-chain fixture after advisory metadata checks") && ok;
	ok = check(manager.getActiveManifest().type == 2,
		"ResourceManager inherits Game.Type through dependency chain") && ok;
	ok = check(!manager.getActiveManifest().useWav,
		"ResourceManager does not inherit non-Type profile fields") && ok;
	ok = check(manager.getActiveManifest().name == "Chain Mod",
		"ResourceManager keeps selected profile fields while inheriting Game.Type") && ok;
	ok = check(File::getActiveSaveNamespace() == "CHAINMOD",
		"ResourceManager selected-by-id pack applies fallback Save.Namespace") && ok;
	manager.setActiveResourcePack(uiInferIndex);
	ok = check(manager.getActiveManifest().uiProfile == "XJXQY",
		"UI.BaseId inherits the referenced pack's effective UI.Profile when Profile is omitted") && ok;
	manager.setActiveResourcePack(multiModIndex);
	const auto multiDependencyIds = manager.getActiveManifest().getDependencyIds();
	ok = check(multiDependencyIds.size() == 4 &&
		multiDependencyIds[0] == "MIDDLEMOD" &&
		multiDependencyIds[1] == "BASE2" &&
		multiDependencyIds[2] == "BASE" &&
		multiDependencyIds[3] == "MULTIPATH",
		"DependencyId parses comma-separated parents in order and removes duplicates") && ok;
	ok = check(manager.getActiveManifest().type == 2,
		"Game.Type inheritance uses the first resolvable declared parent branch") && ok;
	ok = check(readViaFile("config/middle-only.txt") == "middle-mod-only" &&
		readViaFile("config/base2-only.txt") == "second-base-only" &&
		readViaFile("config/path-only.txt") == "multi-path-only",
		"multi-parent resource lookup includes every declared DependencyId subtree") && ok;
	ok = check(readViaFile("config/depth-priority.txt") == "first-parent-depth",
		"multi-parent resource lookup is left-to-right depth-first with first match winning") && ok;
	ok = check(readViaFile("ini/ui/legacy-multi.txt") == "first-parent-ui-depth",
		"legacy packs without UI.BaseId reuse the ordered multi-parent content graph for UI") && ok;
	manager.setActiveResourcePack(modIndex);

	ok = check(manager.getActiveManifest().type == 2,
		"ResourceManager inherits Game.Type from direct dependency") && ok;
	ok = check(manager.getActiveManifest().author == "Profile Mod Author" &&
		packs[modIndex].getDisplayName() == "Mod" &&
		packs[modIndex].getDisplayAuthorText() == u8"作者：Profile Mod Author",
		"resource selection display uses the authoritative game_profile.ini author and ignores resources.ini metadata") && ok;
	ok = check(readViaFile("font/font.ttf") == "manager-mod-font" &&
		readViaCommonResourceFile("font/font.ttf") == "manager-common-font",
		"active package font files cannot override the configured common font") && ok;
	ok = check(manager.getActiveManifest().uiBaseId == "UIBASE" &&
		manager.getActiveManifest().uiProfile == "YYCS",
		"ResourceManager keeps UI base and layout profile separate from content base") && ok;
	ok = check(!manager.isFeatureEnabled("FreezeVisualEffect", true) &&
		manager.isFeatureEnabled("magictriggeratanimationend"),
		"ResourceManager exposes case-insensitive explicit feature overrides") && ok;
	ok = check(File::getActiveSaveNamespace() == "mod_save", "ResourceManager applies Save.Namespace") && ok;
	ok = check(readViaFile("config/base-only.txt") == "base-from-manager",
		"ResourceManager applies dependency fallback to File") && ok;
	ok = check(readViaFile("config/shared-with-common.txt") == "base-over-common",
		"ResourceManager searches dependency before common root") && ok;
	ok = check(readViaFile("config/common-only.txt").empty() &&
		readViaCommonResourceFile("config/common-only.txt") ==
			"common-from-manager",
		"ResourceManager keeps Common behind its dedicated resource API") && ok;
	ok = check(readViaFile("ini/ui/domain.txt") == "local-ui",
		"local UI overrides the configured UI base when PreferLocal is enabled") && ok;
	ok = check(readViaFile("ini/ui/ui-only.txt") == "ui-only",
		"UI paths resolve through the independent UI base") && ok;
	std::filesystem::remove(modRoot / "ini" / "ui" / "domain.txt");
	ok = check(readViaFile("ini/ui/domain.txt") == "ui-base-ui",
		"UI paths do not fall through to the content dependency when UI.BaseId is explicit") && ok;
	ok = check(readViaFile("config/ui-base-only.txt") == "content-base-config",
		"ordinary content paths keep using Resource.DependencyId instead of UI.BaseId") && ok;
	ok = check(!File::fileExist("save/game/base-only.sav"),
		"ResourceManager dependency fallback excludes save paths") && ok;
	ok = check(!File::fileExist("save/game/common-only.sav"),
		"ResourceManager common fallback excludes save paths") && ok;
	manager.setActiveResourcePack(pathModIndex);

	ok = check(manager.getActiveManifest().type == 2,
		"ResourceManager inherits Game.Type from the first resolvable DependencyId branch") && ok;
	ok = check(!manager.getActiveManifest().useWav,
		"ResourceManager does not inherit non-Type fields through DependencyId") && ok;
	ok = check(readViaFile("config/path-grandparent-only.txt") == "path-grandparent",
		"DependencyId content lookup traverses the complete ancestor chain") && ok;

	manager.setActiveResourcePack(pathOnlyModIndex);
	ok = check(manager.getActiveManifest().type == 1,
		"resource packs inherit Game.Type through a case-insensitive DependencyId chain") && ok;
	ok = check(normalizePath(manager.getActiveResourceRoot()).find("/pathonlymod/") != std::string::npos,
		"mixed-case indexed resource paths resolve to the lowercase pack directory") && ok;
	ok = check(readViaFile("config/path-mid-only.txt") == "path-mid" &&
		readViaFile("config/path-base-only.txt") == "path-base",
		"resource lookup is depth-first across every DependencyId ancestor") && ok;
	ok = check(readViaFile("ini/ui/path-base-ui.txt") == "path-base-ui",
		"legacy UI lookup follows the complete DependencyId chain") && ok;
	ok = check(normalizePath(File::getAssetsName("config/path-base-only.txt")).find("/pathonlybase/") != std::string::npos,
		"mixed-case DependencyId values resolve to the indexed dependency directory") && ok;
#ifndef __MOBILE__
	fs::remove(userRecentSelectionPath);
	fs::remove(bundledRecentSelectionPath);
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = check(
		manager.initialize(collectionRoot.string())
			&& std::none_of(
				manager.getDiscoveredPacks().begin(),
				manager.getDiscoveredPacks().end(),
				[](const ResourceManager::ResourcePack& pack)
				{
					return pack.wasRecentlySelected;
				}),
		"resource packs have no recent-selection marker when no selection record exists") &&
		ok;
#endif

	const std::string duplicateRecentSelection =
		"[ResourceSelection]\n"
		"EntryKey=pack.duplicateidb\n"
		"Id=DUPLICATE\n"
		"RootPath=" +
		normalizePath(duplicateIdBRoot.string()) + "\n";
	ok = check(
		File::writeSharedApplicationFile(
			"save/system/resource_selection.ini",
			duplicateRecentSelection.data(),
			static_cast<int>(
				duplicateRecentSelection.size())),
		"duplicate-ID recent-selection fixture is published to user state") &&
		ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = check(
		manager.initialize(collectionRoot.string()) &&
			!manager.getDiscoveredPacks().empty() &&
			manager.getDiscoveredPacks().front().wasRecentlySelected &&
			std::count_if(
				manager.getDiscoveredPacks().begin(),
				manager.getDiscoveredPacks().end(),
				[](const ResourceManager::ResourcePack& pack)
				{
					return pack.wasRecentlySelected;
				}) == 1 &&
			fs::canonical(fs::u8path(
				manager.getDiscoveredPacks().front().rootPath)) ==
				fs::canonical(duplicateIdBRoot),
		"recent stable entry key still controls the resource-selection UI order") &&
		ok;
	ok = check(
		!manager.setActiveResourcePackById("duplicate"),
		"ID-only lookup rejects duplicate resource owners regardless of recent selection") &&
		ok;
	const RuntimeResource::ExactSelectionResult
		duplicateBExactSelection =
			RuntimeResource::resolveResourceCatalogEntrySelection(
				collectionRoot,
				"pack.duplicateidb");
	ok = check(
		!duplicateBExactSelection.succeeded() &&
			duplicateBExactSelection.error ==
				RuntimeResource::ExactSelectionError::
					ActiveResourcePackIdAmbiguous,
		"stable entry-key lookup cannot bypass duplicate Game.Id rejection") &&
		ok;

	return ok;
}

bool testWritableCommonIsNotResourceCandidate(
	const std::filesystem::path& root)
{
	namespace fs = std::filesystem;
	bool ok = true;
	ResourceManager& manager = ResourceManager::instance();
	const fs::path fixtureRoot = root / "writable-common-discovery";
	const fs::path collectionRoot = fixtureRoot / "packaged-assets";
	const fs::path writableRoot = fixtureRoot / "writable-assets";
	const fs::path writableCommonRoot = writableRoot / "common";
	fs::remove_all(fixtureRoot);
	writeRawFile(
		collectionRoot / "resources.ini",
		"[Collection]\nCommonPath=common\n");
	writeRawFile(
		collectionRoot / "base" / "game_profile.ini",
		"[Game]\nId=BASE\nName=Base Game\n");
	writeRawFile(
		writableRoot / "downloaded" / "game_profile.ini",
		"[Game]\nId=DOWNLOADED\nName=Downloaded Game\n"
		"[Resource]\nDependencyId=SHARED\n");
	writeRawFile(
		writableRoot / "shared" / "game_profile.ini",
		"[Game]\nId=SHARED\nName=Shared Resources\n"
		"[Resource]\nResourceOnly=1\n");
	writeRawFile(
		writableRoot / "shared" / "config" / "shared-only.txt",
		"shared-resource-content");
	writeRawFile(
		writableCommonRoot / "version.ini",
		"[Common]\nVersion=1.0.0\n");
	writeRawFile(
		writableCommonRoot / "config" / "common-only.txt",
		"writable-common-content");
	writeRawFile(
		writableCommonRoot / "ini" / "ui" / "mobile" /
			"skills" / "skills.menu.ini",
		"writable-common-mobile-ui");

	ResourceManagerPolicyTestAccess::reset(manager);
	ResourceManagerPolicyTestAccess::scanCollectionWithWritableRoot(
		manager,
		collectionRoot.u8string(),
		writableRoot.u8string());
	const auto& packs = manager.getDiscoveredPacks();
	ok = check(
		packs.size() == 3 &&
		std::any_of(packs.begin(), packs.end(),
			[](const ResourceManager::ResourcePack& pack)
			{
				return pack.manifest.id == "BASE";
			}) &&
		std::any_of(packs.begin(), packs.end(),
			[](const ResourceManager::ResourcePack& pack)
			{
				return pack.manifest.id == "DOWNLOADED";
			}) &&
		std::any_of(packs.begin(), packs.end(),
			[](const ResourceManager::ResourcePack& pack)
			{
				return pack.manifest.id == "SHARED" &&
					pack.manifest.resourceOnly;
			}),
		"writable discovery keeps games and resource-only dependencies while skipping common") && ok;
	const fs::path unexpectedManifest =
		(writableCommonRoot / "game_profile.ini").lexically_normal();
	ok = check(
		std::none_of(
			manager.getResourceCatalogDiagnostics().begin(),
			manager.getResourceCatalogDiagnostics().end(),
			[&unexpectedManifest](
				const RuntimeResource::CatalogDiagnostic& diagnostic)
			{
				return diagnostic.hostPath.lexically_normal() ==
					unexpectedManifest;
			}),
		"writable common does not create an invalid resource card") && ok;
	ok = check(
		!manager.setActiveResourcePackById("SHARED") &&
		manager.setActiveResourcePackById("DOWNLOADED") &&
		readViaFile("config/shared-only.txt") ==
			"shared-resource-content" &&
		readViaCommonResourceFile("config/common-only.txt") ==
			"writable-common-content" &&
		readViaFile("ini/ui/mobile/skills/skills.menu.ini") ==
			"writable-common-mobile-ui",
		"resource-only dependencies and writable Common remain routed after selecting a downloaded game") && ok;

	writeRawFile(
		collectionRoot / "common" / "config" / "common-only.txt",
		"bundled-common-content");
	writeRawFile(
		collectionRoot / "common" / "ini" / "ui" / "mobile" /
			"skills" / "skills.menu.ini",
		"bundled-common-mobile-ui");
	ResourceManagerPolicyTestAccess::reset(manager);
	ResourceManagerPolicyTestAccess::scanCollectionWithWritableRoot(
		manager,
		collectionRoot.u8string(),
		writableRoot.u8string());
	ok = check(
		manager.setActiveResourcePackById("DOWNLOADED") &&
		readViaCommonResourceFile("config/common-only.txt") ==
			"writable-common-content" &&
		readViaFile("ini/ui/mobile/skills/skills.menu.ini") ==
			"writable-common-mobile-ui",
		"downloaded Common overrides bundled Common after selecting a downloaded game") && ok;

	ResourceManagerPolicyTestAccess::reset(manager);
	fs::remove_all(fixtureRoot);
	return ok;
}

bool testResourceRemovalAndSaveCleanup(const std::filesystem::path& root)
{
	namespace fs = std::filesystem;
	bool ok = true;
	ResourceManager& manager = ResourceManager::instance();
	const fs::path fixtureRoot = root / "resource-removal";
	const fs::path assetsRoot = fixtureRoot / "assets";
	const fs::path saveRoot = fixtureRoot / "save";
	fs::remove_all(fixtureRoot);
	writeRawFile(
		assetsRoot / "base" / "game_profile.ini",
		"[Game]\nId=BASE\nName=Base Game\n"
		"[Save]\nNamespace=base.save\n");
	writeRawFile(
		assetsRoot / "mod" / "game_profile.ini",
		"[Game]\nId=MOD\nName=Dependent Mod\n"
		"[Resource]\nDependencyId=BASE\n"
		"[Save]\nNamespace=mod_save\n");
	writeRawFile(
		assetsRoot / "other" / "game_profile.ini",
		"[Game]\nId=OTHER\nName=Other Game\n"
		"[Save]\nNamespace=other_save\n");
	writeRawFile(
		assetsRoot / "duplicate-a" / "game_profile.ini",
		"[Game]\nId=DUPLICATE\nName=Duplicate A\n");
	writeRawFile(
		assetsRoot / "duplicate-b" / "game_profile.ini",
		"[Game]\nId=DUPLICATE\nName=Duplicate B\n");
	writeRawFile(
		assetsRoot / "duplicate-dependent" / "game_profile.ini",
		"[Game]\nId=DUPLICATE_DEPENDENT\nName=Duplicate Dependent\n"
		"[Resource]\nDependencyId=DUPLICATE\n");
	writeRawFile(assetsRoot / "base" / "payload.bin", "base");
	writeRawFile(assetsRoot / "mod" / "payload.bin", "mod");
	writeRawFile(assetsRoot / "other" / "payload.bin", "other");
	writeRawFile(saveRoot / "base_save" / "rpg1" / "game.ini", "base-save");
	writeRawFile(saveRoot / "mod_save" / "rpg2" / "game.ini", "mod-save");
	writeRawFile(saveRoot / "system" / "resource_selection.ini", "system");
	writeRawFile(saveRoot / "config.ini", "config");

	File::setPlatformStateParentForTests(fixtureRoot.string());
	ResourceManagerPolicyTestAccess::reset(manager);
	ResourceManagerPolicyTestAccess::scanCollectionRoot(
		manager, assetsRoot.u8string());
	const auto& packs = manager.getDiscoveredPacks();
	const auto base = std::find_if(
		packs.begin(), packs.end(),
		[](const ResourceManager::ResourcePack& pack)
		{
			return pack.manifest.id == "BASE";
		});
	if (!check(base != packs.end(),
			"resource removal fixture discovers its base"))
	{
		ResourceManagerPolicyTestAccess::reset(manager);
		File::setPlatformStateParentForTests(root.string());
		return false;
	}
	const int baseIndex = static_cast<int>(base - packs.begin());
	const auto duplicateA = std::find_if(
		packs.begin(), packs.end(),
		[](const ResourceManager::ResourcePack& pack)
		{
			std::filesystem::path rootPath(pack.rootPath);
			if (rootPath.filename().empty())
			{
				rootPath = rootPath.parent_path();
			}
			return rootPath.filename() == "duplicate-a";
		});
	if (check(duplicateA != packs.end(),
			"resource removal fixture discovers duplicate-id package"))
	{
		const ResourceManager::ResourceRemovalPlan duplicatePlan =
			manager.buildResourceRemovalPlan(static_cast<int>(
				duplicateA - packs.begin()));
		ok = check(
			duplicatePlan.status ==
				ResourceManager::ResourceRemovalStatus::Success &&
				duplicatePlan.entries.size() == 1 &&
				duplicatePlan.entries.front().name == "Duplicate A",
			"deleting one duplicate Game.Id does not delete a dependent that can still use the other owner") && ok;
	}
	else
	{
		ok = false;
	}
	const ResourceManager::ResourceRemovalPlan plan =
		manager.buildResourceRemovalPlan(baseIndex);
	ok = check(
		plan.status == ResourceManager::ResourceRemovalStatus::Success &&
			plan.entries.size() == 2 &&
			plan.entries[0].gameId == "MOD" &&
			plan.entries[1].gameId == "BASE" &&
			plan.entries[0].saveExists &&
			plan.entries[1].saveExists,
		"resource removal plan orders reverse dependencies before the base and identifies saves") && ok;

	const ResourceManager::ResourceRemovalResult unselected =
		manager.removeResourceGroup(
			plan,
			ResourceManager::ResourceRemovalSavePolicy::Unselected);
	ok = check(
		unselected.status ==
			ResourceManager::ResourceRemovalStatus::InvalidSelection &&
			fs::exists(assetsRoot / "base") &&
			fs::exists(assetsRoot / "mod"),
		"resource removal rejects an unselected save policy without changing files") && ok;

	const ResourceManager::ResourceRemovalResult preserved =
		manager.removeResourceGroup(
			plan,
			ResourceManager::ResourceRemovalSavePolicy::Preserve);
	ok = check(
		preserved.status ==
			ResourceManager::ResourceRemovalStatus::Success &&
			!fs::exists(assetsRoot / "base") &&
			!fs::exists(assetsRoot / "mod") &&
			fs::exists(assetsRoot / "other") &&
			fs::exists(saveRoot / "base_save") &&
			fs::exists(saveRoot / "mod_save"),
		"resource removal preserves saves only after an explicit preserve choice") && ok;

	const std::vector<ResourceManager::SaveNamespaceInfo> saves =
		manager.listSaveNamespaces();
	ok = check(
		saves.size() == 2 &&
		std::all_of(
			saves.begin(), saves.end(),
			[](const ResourceManager::SaveNamespaceInfo& info)
			{
				return info.resourceName.empty() &&
					info.saveSlotCount == 1 && info.bytes > 0;
			}),
		"save management lists preserved orphan namespaces without treating system state as a save") && ok;

	const ResourceManager::ResourceRemovalResult savesRemoved =
		manager.removeSaveNamespaces({ "base_save", "mod_save" });
	ok = check(
		savesRemoved.status ==
			ResourceManager::ResourceRemovalStatus::Success &&
			!fs::exists(saveRoot / "base_save") &&
			!fs::exists(saveRoot / "mod_save") &&
			readRawFile(saveRoot / "config.ini") == "config" &&
			fs::exists(saveRoot / "system"),
		"save cleanup removes only selected namespaces and preserves global state") && ok;

	writeRawFile(
		assetsRoot / "base" / "game_profile.ini",
		"[Game]\nId=BASE\nName=Base Game\n"
		"[Save]\nNamespace=base.save\n");
	writeRawFile(
		assetsRoot / "mod" / "game_profile.ini",
		"[Game]\nId=MOD\nName=Dependent Mod\n"
		"[Resource]\nDependencyId=BASE\n"
		"[Save]\nNamespace=mod_save\n");
	writeRawFile(saveRoot / "base_save" / "rpg1" / "game.ini", "base-save");
	writeRawFile(saveRoot / "mod_save" / "rpg2" / "game.ini", "mod-save");
	ResourceManagerPolicyTestAccess::scanCollectionRoot(
		manager, assetsRoot.u8string());
	const auto refreshedBase = std::find_if(
		manager.getDiscoveredPacks().begin(),
		manager.getDiscoveredPacks().end(),
		[](const ResourceManager::ResourcePack& pack)
		{
			return pack.manifest.id == "BASE";
		});
	if (check(refreshedBase != manager.getDiscoveredPacks().end(),
			"resource removal delete-save fixture rediscovers its base"))
	{
		const int refreshedBaseIndex = static_cast<int>(
			refreshedBase - manager.getDiscoveredPacks().begin());
		const ResourceManager::ResourceRemovalPlan deletePlan =
			manager.buildResourceRemovalPlan(refreshedBaseIndex);
		const ResourceManager::ResourceRemovalResult deleted =
			manager.removeResourceGroup(
				deletePlan,
				ResourceManager::ResourceRemovalSavePolicy::Delete);
		ok = check(
			deleted.status ==
				ResourceManager::ResourceRemovalStatus::Success &&
				!fs::exists(assetsRoot / "base") &&
				!fs::exists(assetsRoot / "mod") &&
				!fs::exists(saveRoot / "base_save") &&
				!fs::exists(saveRoot / "mod_save") &&
				fs::exists(assetsRoot / "other") &&
				fs::exists(saveRoot / "system"),
			"resource removal deletes related saves only after the explicit delete choice") && ok;
	}
	else
	{
		ok = false;
	}

	ResourceManagerPolicyTestAccess::reset(manager);
	File::setPlatformStateParentForTests(root.string());
	fs::remove_all(fixtureRoot);
	return ok;
}

#if defined(__MOBILE__) && !defined(__ANDROID__)
bool testPackagedEmptyPrimaryRootSelection(
	const std::filesystem::path& root)
{
	namespace fs = std::filesystem;
	bool ok = true;
	ResourceManager& manager = ResourceManager::instance();
	const fs::path fixtureRoot =
		root / "packaged-empty-primary-root";
	const fs::path packagedWorkingRoot =
		fixtureRoot / "work" / "level";
	const fs::path externalRoot =
		fixtureRoot / "mobile-external-resources";
	fs::remove_all(fixtureRoot);
	writeRawFile(
		packagedWorkingRoot / "game_profile.ini",
		"[Game]\n"
		"Id=PACKAGED_EMPTY_ROOT\n"
		"Name=Packaged Empty Root\n"
		"Type=0\n"
		"[Save]\n"
		"Namespace=packaged_empty_root\n");

	std::error_code pathError;
	const fs::path previousWorkingDirectory =
		fs::current_path(pathError);
	if (!check(!pathError,
			"packaged empty-root fixture captures the working directory"))
	{
		return false;
	}
	fs::current_path(packagedWorkingRoot, pathError);
	if (!check(!pathError,
			"packaged empty-root fixture installs its asset namespace"))
	{
		return false;
	}

	ResourceManagerPolicyTestAccess::reset(manager);
	ResourceManagerPolicyTestAccess::scanCollectionRoot(manager, "");
	ok = check(
		manager.getDiscoveredPacks().size() == 1 &&
			manager.getDiscoveredPacks().front().rootPath.empty() &&
			manager.getDiscoveredPacks().front().catalogEntryKey ==
				"root",
		"packaged primary game_profile.ini is discovered at the empty logical root") && ok;
	ok = check(
		manager.setActiveResourcePack(0) &&
			manager.getActiveResourceRoot().empty() &&
			manager.hasActiveResourceRoot() &&
			!manager.needsSelection() &&
			manager.getActiveManifest().id ==
				"PACKAGED_EMPTY_ROOT" &&
			ResourceManagerPolicyTestAccess::
				activeResourceEntryKey(manager) == "root",
		"an empty packaged root remains a valid active selection with its stable entry key") && ok;

	writeRawFile(
		externalRoot / "peer" / "game_profile.ini",
		"[Game]\n"
		"Id=PACKAGED_EXTERNAL_PEER\n"
		"Name=Packaged External Peer\n"
		"Type=3\n");
	const int externalCount =
		manager.rescanExternalResourceDirectory();
	ok = check(
		externalCount == 1 &&
			manager.getDiscoveredPacks().size() == 2 &&
			manager.getActiveResourceRoot().empty() &&
			manager.hasActiveResourceRoot() &&
			!manager.needsSelection() &&
			manager.getActiveManifest().id ==
				"PACKAGED_EMPTY_ROOT" &&
			ResourceManagerPolicyTestAccess::
				activeResourceEntryKey(manager) == "root",
		"external rescan restores the active empty-root package by stable entry key") && ok;

	fs::current_path(previousWorkingDirectory, pathError);
	ok = check(!pathError,
		"packaged empty-root fixture restores the working directory") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}
#endif

// 启动容错与导入入口回归：覆盖"资源错误不能阻止普通游戏启动"的多个场景。
// 这些场景使用独立的集合目录，避免与 testResourceManagerDependencySelection
// 共享状态互相干扰。
bool testStartupTolerance(const std::filesystem::path& root)
{
	namespace fs = std::filesystem;
	bool ok = true;
	ResourceManager& manager = ResourceManager::instance();

	const auto resetManager = [&manager]()
	{
		ResourceManagerPolicyTestAccess::reset(manager);
	};

	// 1. 空资源目录：普通游戏 initialize 返回 true（不退出），needsSelection 为 true。
	{
		const fs::path emptyRoot = root / "startup-empty";
		fs::remove_all(emptyRoot);
		fs::create_directories(emptyRoot);
		resetManager();
		ok = check(manager.initialize(emptyRoot.string()),
			"empty resource directory does not abort normal startup (initialize returns true)") && ok;
		ok = check(manager.getDiscoveredPacks().empty() &&
			!manager.hasActiveResourceRoot() &&
			manager.needsSelection(),
			"empty resource directory reaches the selection/management UI (needsSelection)") && ok;
	}

	// 2. 单包即使要求更高版本也保持可见，但不能被激活。
	{
		const fs::path tooNewRoot = root / "startup-too-new";
		fs::remove_all(tooNewRoot);
		fs::create_directories(tooNewRoot / "ini");
		writeRawFile(tooNewRoot / "game_profile.ini",
			"[Game]\n"
			"Id=STARTUP_TOO_NEW\n"
			"Name=Startup Too New\n"
			"Type=0\n"
			"[Release]\n"
			"MinimumEngineVersion=99.0.0\n");
		resetManager();
		ok = check(manager.initialize(tooNewRoot.string()),
			"single incompatible pack does not abort normal startup") && ok;
		ok = check(manager.getDiscoveredPacks().size() == 1 &&
			!manager.hasActiveResourceRoot() &&
			manager.needsSelection() &&
			manager.getDiscoveredPacks().front().compatibility.status ==
				ModRelease::CompatibilityStatus::RequiresNewerEngine,
			"single-pack startup keeps a newer-engine resource visible for blocked selection") && ok;
	}

	// 3. 损坏 resources.ini 不退出；单坏索引条目不影响其他有效包。
	{
		const fs::path collectionRoot = root / "startup-corrupt-index";
		fs::remove_all(collectionRoot);
		const fs::path goodRoot = collectionRoot / "good";
		fs::create_directories(goodRoot / "ini");
		writeRawFile(goodRoot / "game_profile.ini",
			"[Game]\n"
			"Id=STARTUP_GOOD\n"
			"Name=Startup Good\n"
			"Type=0\n");
		// resources.ini 包含一个语法可解析但 Path 缺失的坏条目，与一个有效条目共存。
		writeRawFile(collectionRoot / "resources.ini",
			"[Pack.BAD]\n"
			"Enabled=true\n"
			"\n"
			"[Pack.GOOD]\n"
			"Id=STARTUP_GOOD\n"
			"Path=good\n"
			"Manifest=game_profile.ini\n");
		resetManager();
		ok = check(manager.initialize(collectionRoot.string()),
			"corrupt/bad resources.ini entry does not abort normal startup") && ok;
		ok = check(manager.getDiscoveredPacks().size() == 1,
			"a single bad index entry is skipped without invalidating the valid pack") && ok;
		ok = check(!manager.getDiscoveredPacks().empty() &&
			manager.getDiscoveredPacks().front().manifest.id == "STARTUP_GOOD",
			"the valid pack survives a broken sibling entry") && ok;
	}

	// 4. 没有 game_profile.ini 的旧式目录不是已转换资源包。
	{
		const fs::path looseRoot = root / "startup-loose";
		fs::remove_all(looseRoot);
		// 仅创建旧式资源目录，不提供转换完成标志。
		fs::create_directories(looseRoot / "ini");
		fs::create_directories(looseRoot / "map");
		resetManager();
		ok = check(manager.initialize(looseRoot.string()),
			"an unconverted directory without game_profile.ini does not abort startup") && ok;
		ok = check(manager.getDiscoveredPacks().empty() &&
			!manager.hasActiveResourceRoot(),
			"legacy resource markers do not synthesize a resource pack without game_profile.ini") && ok;
	}

	// 7b. 测试集合不提供引擎字体时，资源扫描本身仍可进入资源选择页。
	// 正式程序包由发布工具单独保证 assets/engine/font/font.ttf 存在。
	{
		const fs::path collectionRoot = root / "startup-no-font";
		fs::remove_all(collectionRoot);
		const fs::path packARoot = collectionRoot / "packa";
		const fs::path packBRoot = collectionRoot / "packb";
		fs::create_directories(packARoot / "ini");
		fs::create_directories(packBRoot / "ini");
		writeRawFile(packARoot / "game_profile.ini",
			"[Game]\n"
			"Id=STARTUP_NO_FONT_A\n"
			"Name=Startup No Font A\n"
			"Type=0\n");
		writeRawFile(packBRoot / "game_profile.ini",
			"[Game]\n"
			"Id=STARTUP_NO_FONT_B\n"
			"Name=Startup No Font B\n"
			"Type=0\n");
		writeRawFile(collectionRoot / "resources.ini",
			"[Pack.A]\n"
			"Id=STARTUP_NO_FONT_A\n"
			"Path=packa\n"
			"Manifest=game_profile.ini\n"
			"\n"
			"[Pack.B]\n"
			"Id=STARTUP_NO_FONT_B\n"
			"Path=packb\n"
			"Manifest=game_profile.ini\n");
		resetManager();
		ok = check(manager.initialize(collectionRoot.string()),
			"a multi-pack collection without an engine font does not abort scanning") && ok;
		ok = check(!manager.hasActiveResourceRoot() &&
			manager.needsSelection(),
			"an engine-font-less test collection still reaches resource selection") && ok;
	}

	// 8. 路径越界只拒绝对应条目，普通程序继续启动。
	{
		const fs::path collectionRoot = root / "startup-escape";
		fs::remove_all(collectionRoot);
		const fs::path goodRoot = collectionRoot / "good";
		fs::create_directories(goodRoot / "ini");
		writeRawFile(goodRoot / "game_profile.ini",
			"[Game]\n"
			"Id=STARTUP_ESCAPE_GOOD\n"
			"Name=Startup Escape Good\n"
			"Type=0\n");
		// 一个指向父级跳转路径的条目：应被拒绝，不影响 good 条目。
		writeRawFile(collectionRoot / "resources.ini",
			"[Pack.ESCAPE]\n"
			"Id=STARTUP_ESCAPE\n"
			"Path=../../startup-too-new\n"
			"Manifest=game_profile.ini\n"
			"\n"
			"[Pack.GOOD]\n"
			"Id=STARTUP_ESCAPE_GOOD\n"
			"Path=good\n"
			"Manifest=game_profile.ini\n");
		resetManager();
		ok = check(manager.initialize(collectionRoot.string()),
			"an out-of-bounds index entry does not abort normal startup") && ok;
		ok = check(manager.getDiscoveredPacks().size() == 1 &&
			manager.getDiscoveredPacks().front().manifest.id ==
				"STARTUP_ESCAPE_GOOD",
			"only the out-of-bounds entry is rejected; the valid peer survives") && ok;
	}

	return ok;
}
}

int main(int argc, char* argv[])
{
	namespace fs = std::filesystem;

	bool ok = true;
	fs::path root = makeUniqueTestDirectory("jxqy_file_resource_roots_test");
	fs::remove_all(root);
	std::filesystem::create_directories(root);
	std::filesystem::create_directories(
		root / "UserState");
	File::setSharedApplicationRootForTests(
		(root / "UserState").string());
	File::setPlatformStateParentForTests(root.string());

	if (argc == 2 &&
		std::string(argv[1]) ==
			"--resource-manager-policy-only")
	{
		bool resourcePolicyOk =
			testResourceManagerDependencySelection(root);
		resourcePolicyOk =
			testWritableCommonIsNotResourceCandidate(root) &&
			resourcePolicyOk;
#if defined(__MOBILE__) && !defined(__ANDROID__)
		resourcePolicyOk =
			testPackagedEmptyPrimaryRootSelection(root) &&
			resourcePolicyOk;
#endif
		File::setSharedApplicationRootForTests("");
		File::setPlatformStateParentForTests("");
		fs::remove_all(root);
		return resourcePolicyOk ? 0 : 1;
	}

	ok = testSafeResourceTextFormatting(root) && ok;
	ok = testResourceReadPrefixPolicy() && ok;
	ok = testIMPFormatValidation() && ok;
	ok = testResourceManagerDependencySelection(root) && ok;
	ok = testWritableCommonIsNotResourceCandidate(root) && ok;
	ok = testResourceRemovalAndSaveCleanup(root) && ok;
#if defined(__MOBILE__) && !defined(__ANDROID__)
	ok = testPackagedEmptyPrimaryRootSelection(root) && ok;
#endif
	ok = testStartupTolerance(root) && ok;
	ok = testSharedApplicationFiles(root) && ok;
#if !defined(__MOBILE__)
	ok = testEditorRunFileLayout(root) && ok;
#endif

	fs::path activeRoot = root / "ModPack";
	fs::path dependencyRoot = root / "BasePack";
	fs::path commonRoot = root / "Common";
	fs::path writableCommonRoot = root / "WritableCommon";
	fs::path uiCommonRoot = root / "UiCommon";
	writeRawFile(activeRoot / "config" / "same.txt", "mod");
	writeRawFile(dependencyRoot / "config" / "same.txt", "base");
	writeRawFile(dependencyRoot / "config" / "base-only.txt", "base-only");
	writeRawFile(dependencyRoot / "save" / "game" / "base-only.sav", "base-save");
	writeRawFile(activeRoot / "save" / "game" / "obsolete-active.sav", "obsolete-save");
	writeRawFile(activeRoot / "font" / "font.ttf", "mod-font");
	writeRawFile(dependencyRoot / "font" / "font.ttf", "base-font");
	writeRawFile(commonRoot / "font" / "font.ttf", "common-font");
	writeRawFile(commonRoot / "config" / "common-only.txt", "common-only");
	writeRawFile(
		writableCommonRoot / "font" / "font.ttf",
		"updated-common-font");
	writeRawFile(uiCommonRoot / "ini" / "ui" / "order.txt", "common-ui-order");

	const fs::path logicalCollectionRoot = root / "Assets";
	const fs::path userSaveRoot =
		root / "save" / "modpack";
	fs::create_directories(logicalCollectionRoot);
	writeRawFile(
		logicalCollectionRoot / "engine" / "font" / "font.ttf",
		"engine-font");
	File::setAssetsCollectionRoot(logicalCollectionRoot.string());
	File::setActiveResourceRoot(activeRoot.string());
	File::setCommonResourceRoot(writableCommonRoot.string());
	File::setCommonResourceFallbackRoots({ commonRoot.string() });
	File::setResourceFallbackRoots({ dependencyRoot.string() });
	File::setUiResourceFallbackRoots({ dependencyRoot.string() });
	File::setActiveSaveNamespace("ModPack");
	fs::remove_all(userSaveRoot);

	const fs::path linkedGameRoot = root / "linked-game-root";
	const fs::path linkedAssetsRoot = linkedGameRoot / "assets";
	const fs::path synchronizedAssetsRoot =
		root / "synchronized-resource-root" / "assets";
	fs::create_directories(linkedGameRoot);
	fs::create_directories(synchronizedAssetsRoot);
	std::string linkedAssetsError;
	const bool linkedAssetsCreated = createSymbolicLinkFixture(
		synchronizedAssetsRoot,
		linkedAssetsRoot,
		true,
		linkedAssetsError);
	ok = check(linkedAssetsCreated,
		"logical assets link fixture is required: " +
			linkedAssetsError) && ok;
	if (linkedAssetsCreated)
	{
		File::setAssetsCollectionRoot(linkedAssetsRoot.string());
		File::setActiveSaveNamespace("LinkedPack");
		const char linkedSaveData[] = "linked-save";
		const char linkedConfigData[] = "linked-config";
		ok = check(
			File::writeFileChecked(
				"save/game/linked.sav",
				linkedSaveData,
				static_cast<int>(sizeof(linkedSaveData) - 1)) &&
			File::writeSharedApplicationFile(
				CONFIG_INI,
				linkedConfigData,
				static_cast<int>(sizeof(linkedConfigData) - 1)) &&
			readRawFile(
				root / "save" / "linkedpack" /
					"game" / "linked.sav") == "linked-save" &&
			readRawFile(
				root / "save" / "config.ini") ==
					"linked-config" &&
			!fs::exists(linkedGameRoot / "save") &&
			!fs::exists(linkedGameRoot / "config") &&
			!fs::exists(
				synchronizedAssetsRoot.parent_path() /
					"save") &&
			!fs::exists(
				synchronizedAssetsRoot.parent_path() /
					"config"),
			"save and config stay in the independent state root when assets uses a link") && ok;
		std::error_code linkedAssetsCleanupError;
		fs::remove(linkedAssetsRoot, linkedAssetsCleanupError);
		ok = check(!linkedAssetsCleanupError,
			"logical assets link fixture cleans up") && ok;
	}
	File::setAssetsCollectionRoot(logicalCollectionRoot.string());
	File::setActiveSaveNamespace("ModPack");

	ok = check(readViaFile("config/same.txt") == "mod", "active resource root wins over dependency") && ok;
	ok = check(readViaFile("CONFIG/SAME.TXT") == "mod",
		"ordinary resource reads canonicalize ASCII path letters to lowercase") && ok;
	ok = check(readViaFile("config/base-only.txt") == "base-only", "resource fallback reads dependency root") && ok;
	ok = check(readViaActiveResourceFile("config/same.txt") == "mod",
		"active-package read accesses a local file") && ok;
	ok = check(readViaActiveResourceFile("CONFIG/SAME.TXT") == "mod" &&
		File::activeResourceFileExist("CONFIG/SAME.TXT"),
		"explicit active-resource reads canonicalize ASCII path letters to lowercase") && ok;
	ok = check(!File::activeResourceFileExist("config/base-only.txt") &&
		readViaActiveResourceFile("config/base-only.txt").empty(),
		"active-package reads never use dependency fallback") && ok;
	ok = check(!File::activeResourceFileExist("config/common-only.txt") &&
		readViaActiveResourceFile("config/common-only.txt").empty(),
		"active-package reads never use collection common fallback") && ok;
	ok = check(readViaFile("font/font.ttf") == "mod-font",
		"ordinary font lookup still observes active package priority") && ok;
	ok = check(
		readViaBundledApplicationFile("engine/font/font.ttf") ==
			"engine-font",
		"engine font lookup remains independent of the active package") && ok;
	ok = check(
		readViaBundledApplicationFile("../engine/font/font.ttf").empty(),
		"engine asset lookup rejects parent traversal") && ok;
	ok = check(
		readViaCommonResourceFile("font/font.ttf") ==
			"updated-common-font",
		"common font lookup prefers the writable common override") && ok;
	ok = check(
		readViaCommonResourceFile("config/common-only.txt") ==
			"common-only",
		"common lookup falls back to bundled common for missing files") && ok;
	ok = check(
		readViaCommonResourceFile("FONT/FONT.TTF") ==
			"updated-common-font",
		"explicit common-resource reads canonicalize ASCII path letters to lowercase") && ok;
	ok = check(readViaCommonResourceFile("../font/font.ttf").empty(),
		"common resource lookup rejects parent traversal") && ok;
	ok = check(File::fileExist("config/base-only.txt"), "fileExist checks dependency roots") && ok;
	writeRawFile(activeRoot / "ini" / "ui" / "order.txt", "local-ui-order");
	writeRawFile(dependencyRoot / "ini" / "ui" / "order.txt", "base-ui-order");
	ok = check(readViaFile("ini/ui/order.txt") == "local-ui-order",
		"UI fallback defaults to local override priority") && ok;
	File::setUiResourceFallbackRoots({ dependencyRoot.string() }, false, uiCommonRoot.string());
	ok = check(readViaFile("ini/ui/order.txt") == "base-ui-order",
		"UI PreferLocal=0 places the UI base before local UI") && ok;
	std::filesystem::remove(dependencyRoot / "ini" / "ui" / "order.txt");
	ok = check(readViaFile("ini/ui/order.txt") == "local-ui-order",
		"UI PreferLocal=0 still places local UI before common fallback") && ok;
	std::filesystem::remove(activeRoot / "ini" / "ui" / "order.txt");
	ok = check(readViaFile("ini/ui/order.txt") == "common-ui-order",
		"UI PreferLocal=0 reaches common UI after the UI base and local UI") && ok;
	File::setUiResourceFallbackRoots({ dependencyRoot.string() });
	writeRawFile(activeRoot / "config" / "empty.txt", "");
	std::filesystem::create_directories(activeRoot / "config" / "folder");
	ok = check(File::fileExist("config/empty.txt"), "fileExist accepts empty files") && ok;
	ok = check(File::fileExist("config/folder"), "fileExist accepts directories") && ok;
	ok = check(File::activeResourceFileExist("config/empty.txt"),
		"active-package existence accepts a local empty file") && ok;
	ok = check(!File::activeResourceFileExist("config/folder"),
		"active-package existence rejects a directory") && ok;
	std::unique_ptr<char[]> limitedData;
	int limitedLength = 123;
	writeRawFile(activeRoot / "config" / "limited.txt", std::string(65, 'x'));
	ok = check(!File::readActiveResourceFile("config/limited.txt", limitedData,
		limitedLength, 64) && limitedData == nullptr && limitedLength == 0,
		"active-package reads enforce the caller byte limit and clear outputs") && ok;
	ok = check(!File::activeResourceFileExist("../config/same.txt") &&
		!File::readActiveResourceFile("../config/same.txt", limitedData, limitedLength),
		"active-package access rejects parent traversal") && ok;

	writeRawFile(activeRoot / "asf" / "goods" / std::filesystem::u8path("tm001-\xE6\xAD\xA3\xE7\xA1\xAE.asf"), "image-alias");
	writeRawFile(activeRoot / "asf" / "goods" / std::filesystem::u8path("tm001-\xE5\x9B\xBE\xE6\xA0\x87s.asf"), "icon-alias");
	writeRawFile(activeRoot / "asf" / "goods" / std::filesystem::u8path("tm002-\xE4\xB8\x80.asf"), "ambiguous-a");
	writeRawFile(activeRoot / "asf" / "goods" / std::filesystem::u8path("tm002-\xE4\xBA\x8C.asf"), "ambiguous-b");
	ok = check(readViaFile("asf/goods/tm001-\xE4\xB8\xA2\xE5\xA4\xB1.asf") == "image-alias",
		"readFile resolves unique image resource alias by stable numeric prefix") && ok;
	ok = check(readViaFile("asf/goods/tm001-\xE4\xB8\xA2\xE5\xA4\xB1s.asf") == "icon-alias",
		"readFile keeps icon suffix while resolving image resource alias") && ok;
	ok = check(File::fileExist("asf/goods/tm001-\xE4\xB8\xA2\xE5\xA4\xB1.asf"),
		"fileExist resolves unique image resource alias") && ok;
	ok = check(normalizePath(File::getAssetsName("asf/goods/tm001-\xE4\xB8\xA2\xE5\xA4\xB1.asf")).find("tm001-\xE6\xAD\xA3\xE7\xA1\xAE.asf") != std::string::npos,
		"getAssetsName returns resolved image resource alias") && ok;
	ok = check(readViaFile("asf/goods/tm002-\xE7\xBC\xBA.asf").empty(),
		"readFile refuses ambiguous image resource aliases") && ok;

	std::string resolvedDependencyFile = normalizePath(File::getAssetsName("config/base-only.txt"));
	ok = check(resolvedDependencyFile.find("BasePack/config/base-only.txt") != std::string::npos,
		"getAssetsName returns dependency path when active file is missing") && ok;

	ok = check(
		!File::fileExist("save/game/base-only.sav") &&
		!File::fileExist("save/game/obsolete-active.sav"),
		"mutable saves do not fall back to resource roots") && ok;

	const char saveData[] = "mod-save";
	File::writeFile("save/game/out.sav", saveData, (int)sizeof(saveData) - 1);
	ok = check(fs::exists(userSaveRoot / "game" / "out.sav"),
		"save write lands in the namespace below the independent state root") && ok;
	ok = check(!fs::exists(activeRoot / "save" / "game" / "out.sav"),
		"save write never modifies the active resource root") && ok;
	ok = check(!fs::exists(dependencyRoot / "save" / "game" / "out.sav"),
		"save write never lands in dependency root") && ok;

	fs::path explicitLogPath = root / "logs" / "automation.log";
	GameLog::setLogFilePath(explicitLogPath.string());
	GameLog::use_log_file = true;
	GameLog::write("explicit log path test");
	GameLog::use_log_file = false;
	GameLog::setLogFilePath("");
	ok = check(fs::exists(explicitLogPath), "explicit log path creates log file") && ok;
	std::ifstream explicitLog(explicitLogPath, std::ios::binary);
	std::string explicitLogContent((std::istreambuf_iterator<char>(explicitLog)), std::istreambuf_iterator<char>());
	explicitLog.close();
	ok = check(explicitLogContent.find("explicit log path test") != std::string::npos,
		"explicit log path receives GameLog output") && ok;

	fs::path utf8LogPath = root / "logs" / std::filesystem::u8path(u8"启动器.log");
	GameLog::setLogFilePath(utf8LogPath.u8string());
	GameLog::use_log_file = true;
	GameLog::write("UTF-8 explicit log path test");
	GameLog::use_log_file = false;
	GameLog::setLogFilePath("");
	ok = check(fs::exists(utf8LogPath), "UTF-8 explicit log path creates log file") && ok;
	std::ifstream utf8ExplicitLog(utf8LogPath, std::ios::binary);
	std::string utf8ExplicitLogContent(
		(std::istreambuf_iterator<char>(utf8ExplicitLog)),
		std::istreambuf_iterator<char>());
	utf8ExplicitLog.close();
	ok = check(utf8ExplicitLogContent.find("UTF-8 explicit log path test") != std::string::npos,
		"UTF-8 explicit log path receives GameLog output") && ok;

	writeRawFile(userSaveRoot / "game" / "game.ini", "game");
	writeRawFile(userSaveRoot / "game" / "player.ini", "player");
	writeRawFile(userSaveRoot / "game" / "list.ini", "legacy-list");
	writeRawFile(userSaveRoot / "game" / "empty.dat", "");
	writeRawFile(userSaveRoot / "rpg1" / "old.npc", "stale");
	writeRawFile(userSaveRoot / "rpg1" / "list.ini", "stale-list");
	ok = check(!File::writeFileChecked("save/game/out.sav", nullptr, 1) &&
		readViaFile("save/game/out.sav") == "mod-save",
		"checked writes report invalid data without truncating the existing file") && ok;
	fs::path blockedStateParent = root / "blocked-state-parent";
	writeRawFile(blockedStateParent, "not-a-directory");
	File::setPlatformStateParentForTests(
		blockedStateParent.string());
	INIReader failedSaveIni;
	failedSaveIni.Set("Test", "Value", "new");
	ok = check(!failedSaveIni.saveToFile("save/game/failed.ini"),
		"INI save propagates checked write failure") && ok;
	File::setPlatformStateParentForTests(root.string());
	ok = check(File::copyDirectoryFiles("save/game/", "save/rpg1/", { "list.ini" }),
		"copyDirectoryFiles copies save directory") && ok;
	ok = check(readViaFile("save/rpg1/game.ini") == "game",
		"copyDirectoryFiles copies game.ini") && ok;
	ok = check(readViaFile("save/rpg1/player.ini") == "player",
		"copyDirectoryFiles copies player.ini") && ok;
	ok = check(fs::exists(userSaveRoot / "rpg1" / "empty.dat"),
		"copyDirectoryFiles preserves empty files") && ok;
	ok = check(!fs::exists(userSaveRoot / "rpg1" / "old.npc"),
		"copyDirectoryFiles clears stale files") && ok;
	ok = check(!fs::exists(userSaveRoot / "rpg1" / "list.ini"),
		"copyDirectoryFiles excludes legacy list file") && ok;

	writeRawFile(userSaveRoot / "game" / "game.ini", "new-before-backup");
	writeRawFile(userSaveRoot / "rpg1" / "game.ini", "old-before-backup");
	ok = check(!File::copyDirectoryFiles("save/game/", "save/rpg1/", { "list.ini" },
		[](File::DirectoryCopyPhase phase)
		{
			return phase == File::DirectoryCopyPhase::BeforeBackup;
		}) && readViaFile("save/rpg1/game.ini") == "old-before-backup",
		"staging failure leaves the previous save slot byte-identical") && ok;

	writeRawFile(userSaveRoot / "game" / "game.ini", "new-before-publish");
	writeRawFile(userSaveRoot / "rpg1" / "game.ini", "old-before-publish");
	ok = check(!File::copyDirectoryFiles("save/game/", "save/rpg1/", { "list.ini" },
		[](File::DirectoryCopyPhase phase)
		{
			return phase == File::DirectoryCopyPhase::BeforePublish;
		}) && readViaFile("save/rpg1/game.ini") == "old-before-publish",
		"publish failure rolls the previous save slot back into place") && ok;
	ok = check(!fs::exists(userSaveRoot / ".jxqy-rpg1-staging") &&
		!fs::exists(userSaveRoot / ".jxqy-rpg1-backup"),
		"handled transaction failures do not leave recovery artifacts") && ok;

	fs::remove_all(userSaveRoot / "rpg2");
	writeRawFile(userSaveRoot / ".jxqy-rpg2-backup" / "game.ini", "recovered-old");
	writeRawFile(userSaveRoot / ".jxqy-rpg2-staging" / "game.ini", "unpublished-new");
	ok = check(File::recoverDirectoryCopy("save/rpg2/") &&
		readViaFile("save/rpg2/game.ini") == "recovered-old" &&
		!fs::exists(userSaveRoot / ".jxqy-rpg2-backup") &&
		!fs::exists(userSaveRoot / ".jxqy-rpg2-staging"),
		"startup recovery restores the old slot after interruption between backup and publish") && ok;

	fs::remove_all(userSaveRoot / "rpg3");
	writeRawFile(userSaveRoot / ".jxqy-rpg3-staging" / "game.ini", "partial-new");
	ok = check(File::recoverDirectoryCopy("save/rpg3/") &&
		!fs::exists(userSaveRoot / "rpg3") &&
		!fs::exists(userSaveRoot / ".jxqy-rpg3-staging"),
		"startup recovery discards an incomplete first-save staging directory") && ok;

	fs::remove_all(userSaveRoot / "rpg4");
	writeRawFile(userSaveRoot / ".jxqy-rpg4-staging" / "game.ini", "complete-new");
	writeRawFile(userSaveRoot / ".jxqy-rpg4-staging-ready", "ready");
	ok = check(File::recoverDirectoryCopy("save/rpg4/") &&
		readViaFile("save/rpg4/game.ini") == "complete-new" &&
		!fs::exists(userSaveRoot / ".jxqy-rpg4-staging-ready"),
		"startup recovery publishes a verified complete first-save staging directory") && ok;

	writeRawFile(userSaveRoot / "rpg5" / "game.ini", "published-new");
	writeRawFile(userSaveRoot / ".jxqy-rpg5-backup" / "game.ini", "stale-old");
	ok = check(File::recoverDirectoryCopy("save/rpg5/") &&
		readViaFile("save/rpg5/game.ini") == "published-new" &&
		!fs::exists(userSaveRoot / ".jxqy-rpg5-backup"),
		"startup recovery keeps a published slot and removes its stale backup") && ok;

	writeRawFile(userSaveRoot / "rpg6" / "game.ini", "slot-six-old");
	fs::rename(userSaveRoot / "game" / "game.ini",
		userSaveRoot / "game" / "game.ini.missing");
	ok = check(!SaveFileManager::CopySaveFileTo(6) &&
		readViaFile("save/rpg6/game.ini") == "slot-six-old",
		"save manager propagates a missing source failure without modifying the target slot") && ok;
	fs::rename(userSaveRoot / "game" / "game.ini.missing",
		userSaveRoot / "game" / "game.ini");

	fs::remove(userSaveRoot / "rpg6" / "game.ini");
	writeRawFile(userSaveRoot / "rpg6" / "other.ini", "invalid-slot");
	writeRawFile(userSaveRoot / "game" / "game.ini", "current-before-failed-load");
	ok = check(!SaveFileManager::CopySaveFileFrom(6) &&
		readViaFile("save/game/game.ini") == "current-before-failed-load",
		"save manager propagates a failed load copy without modifying the current game") && ok;

	writeRawFile(activeRoot / "ini" / "save" / "game.ini", "canonical-new-game");
	writeRawFile(activeRoot / "ini" / "save" / "player.ini", "canonical-player");
	writeRawFile(userSaveRoot / "game" / "stale.ini", "stale");
	ok = check(SaveFileManager::CopySaveFileFrom(0) &&
		readViaFile("save/game/game.ini") == "canonical-new-game" &&
		readViaFile("save/game/player.ini") == "canonical-player" &&
		!fs::exists(userSaveRoot / "game" / "stale.ini") &&
		SaveFileManager::HasSaveFile(0) &&
		!SaveFileManager::CopySaveFileTo(0) &&
		!fs::exists(userSaveRoot / "rpg0"),
		"new game loads directly from the canonical resource ini/save template") && ok;

	writeRawFile(activeRoot / "save" / "rpg0" / "game.ini", "legacy-resource-template");
	writeRawFile(userSaveRoot / "rpg0" / "game.ini", "legacy-user-template");
	const char rejectedRpg0Write[] = "rejected";
	ok = check(
		!File::fileExist("save/rpg0/game.ini") &&
		readViaFile("save/rpg0/game.ini").empty() &&
		!File::writeFileChecked(
			"save/rpg0/game.ini",
			rejectedRpg0Write,
			static_cast<int>(sizeof(rejectedRpg0Write) - 1)) &&
		!File::writeFileChecked(
			"save/shot/rpg0.png",
			rejectedRpg0Write,
			static_cast<int>(sizeof(rejectedRpg0Write) - 1)) &&
		!File::recoverDirectoryCopy("save/rpg0"),
		"runtime rejects legacy rpg0 data and screenshots instead of routing them to resources or user state") && ok;

	writeRawFile(userSaveRoot / "game" / "manual.ini", "current");
	writeRawFile(userSaveRoot / "rpg1" / "game.ini", "slot1");
	writeRawFile(userSaveRoot / "rpg7" / "game.ini", "slot7");
	writeRawFile(userSaveRoot / "rpg_auto" / "game.ini", "auto-save");
	writeRawFile(userSaveRoot / "shot" / "rpg1.png", "png-shot1");
	writeRawFile(userSaveRoot / "shot" / "rpg1.bmp", "shot1");
	writeRawFile(userSaveRoot / "shot" / "rpg7.png", "png-shot7");
	writeRawFile(userSaveRoot / "shot" / "rpg7.bmp", "shot7");
	ok = check(SaveFileManager::ClearAllSaveData(),
		"ClearAllSaveData clears user save slots") && ok;
	ok = check(!fs::exists(userSaveRoot / "rpg1" / "game.ini"),
		"ClearAllSaveData removes rpg1 save files") && ok;
	ok = check(!fs::exists(userSaveRoot / "rpg7" / "game.ini"),
		"ClearAllSaveData removes rpg7 save files") && ok;
	ok = check(!fs::exists(userSaveRoot / "shot" / "rpg1.bmp"),
		"ClearAllSaveData removes legacy rpg1 snapshot") && ok;
	ok = check(!fs::exists(userSaveRoot / "shot" / "rpg1.png"),
		"ClearAllSaveData removes PNG rpg1 snapshot") && ok;
	ok = check(!fs::exists(userSaveRoot / "shot" / "rpg7.bmp"),
		"ClearAllSaveData removes legacy rpg7 snapshot") && ok;
	ok = check(!fs::exists(userSaveRoot / "shot" / "rpg7.png"),
		"ClearAllSaveData removes PNG rpg7 snapshot") && ok;
	ok = check(fs::exists(userSaveRoot / "game" / "manual.ini"),
		"ClearAllSaveData keeps current running save") && ok;
	ok = check(fs::exists(activeRoot / "ini" / "save" / "game.ini"),
		"ClearAllSaveData keeps the resource new-game template") && ok;
	ok = check(fs::exists(userSaveRoot / "rpg_auto" / "game.ini"),
		"ClearAllSaveData keeps auto save") && ok;

	ResourceManifest manifest;
	const std::string manifestText =
		"[Game]\n"
		"Id=MOD_A\n"
		"Name=Mod A\n"
		"Author=Mod Author\n"
		"Version=1.041\n"
		"Type=99\n"
		"\n"
		"[Resource]\n"
		"DependencyId=JXQY2, YYCS, jxqy2\n"
		"\n"
		"[UI]\n"
		"BaseId=YYCS\n"
		"Profile=YYCS\n"
		"PreferLocal=0\n"
		"\n"
		"[Features]\n"
		"TopButtonsLayout=1\n"
		"MagicTriggerAtAnimationEnd=0\n"
		"\n"
		"[Save]\n"
		"Namespace=mod_a_save\n"
		"\n"
		"[Release]\n"
		"Date=2026-07-25\n"
		"MinimumEngineVersion=1.4.3-beta.2+build.7\n"
		"Cover=ui/mod-cover.png\n"
		"DescriptionFile=docs/mod-description.txt\n"
		"InstalledArtifactCrc32=ABCDEF12\n"
		"InstalledIncrementalArtifactCrc32=1234ABCD\n"
		"\n"
		"[Team]\n"
		"InfoFile=team.txt\n";
	ok = check(manifest.loadFromBuffer(manifestText.c_str(), (int)manifestText.size()),
		"manifest buffer loads") && ok;
	ok = check(manifest.author == "Mod Author", "manifest parses Game.Author") && ok;
	ok = check(
		manifest.releaseMetadata.displayVersion == "1.041" &&
			manifest.releaseMetadata.releaseDate == "2026-07-25" &&
			manifest.releaseMetadata.minimumEngineVersion ==
				"1.4.3-beta.2+build.7" &&
			manifest.releaseMetadata.coverPath == "ui/mod-cover.png" &&
			manifest.releaseMetadata.descriptionFilePath ==
				"docs/mod-description.txt" &&
			manifest.releaseMetadata.installedArtifactCrc32 ==
				"ABCDEF12" &&
			manifest.releaseMetadata.installedIncrementalArtifactCrc32 ==
				"1234ABCD",
		"manifest parses optional MOD release metadata") && ok;
	ok = check(manifest.dependencyId == "JXQY2, YYCS, jxqy2",
		"manifest preserves the declared Resource.DependencyId list") && ok;
	const auto parsedDependencyIds = manifest.getDependencyIds();
	ok = check(parsedDependencyIds.size() == 2 && parsedDependencyIds[0] == "JXQY2" &&
		parsedDependencyIds[1] == "YYCS",
		"manifest parses ordered comma-separated dependency ids and removes duplicates") && ok;
	ok = check(manifest.uiBaseId == "YYCS", "manifest parses UI.BaseId") && ok;
	ok = check(manifest.uiProfile == "YYCS", "manifest parses UI.Profile") && ok;
	ok = check(!manifest.preferLocalUi, "manifest parses UI.PreferLocal") && ok;
	ok = check(manifest.isFeatureEnabled("topbuttonslayout"),
		"manifest parses enabled feature case-insensitively") && ok;
	ok = check(!manifest.isFeatureEnabled("MagicTriggerAtAnimationEnd", true),
		"manifest preserves explicit disabled feature") && ok;
	ok = check(manifest.isFeatureEnabled("UnknownFeature", true),
		"manifest uses caller default for absent feature") && ok;
	ok = check(manifest.saveNamespace == "mod_a_save", "manifest parses Save.Namespace") && ok;
	ok = check(manifest.teamInfoFile == "team.txt", "manifest parses Team.InfoFile") && ok;
	ok = check(!manifest.isBaseGame(),
		"manifest with content dependencies is not a base game") && ok;
	ResourceManifest defaultManifest = ResourceManifest::createDefault("");
	ok = check(
		defaultManifest.teamInfoFile.empty() &&
			defaultManifest.releaseMetadata.displayVersion.empty() &&
			defaultManifest.releaseMetadata.releaseDate.empty() &&
			defaultManifest.releaseMetadata.minimumEngineVersion.empty() &&
			defaultManifest.releaseMetadata.coverPath.empty() &&
			defaultManifest.releaseMetadata.descriptionFilePath.empty() &&
			defaultManifest.releaseMetadata.installedArtifactCrc32.empty() &&
			defaultManifest.releaseMetadata.
				installedIncrementalArtifactCrc32.empty(),
		"default manifest does not invent MOD team or release information") && ok;
	ok = check(defaultManifest.isBaseGame(),
		"default manifest is recognized as a base game") && ok;
	ResourceManifest inheritedTypeMod = defaultManifest;
	inheritedTypeMod.typeDefined = false;
	ok = check(!inheritedTypeMod.isBaseGame(),
		"inherited Game.Type does not mark a package as a base game") && ok;
	ResourceManager::ResourcePack baseDisplayPack;
	baseDisplayPack.rootPath = "base/";
	baseDisplayPack.manifest.name = "Base Game";
	baseDisplayPack.manifest.author = u8"原版";
	baseDisplayPack.manifest.type = 2;
	baseDisplayPack.manifest.typeDefined = true;
	ok = check(baseDisplayPack.getDisplayName() == "Base Game" &&
		baseDisplayPack.getDisplayAuthorText() == u8"原版",
		"resource selection displays the configured base-game attribution without an author prefix") && ok;
	ResourceManager::ResourcePack modDisplayPack;
	modDisplayPack.rootPath = "mod/";
	modDisplayPack.manifest.name = "Test Mod";
	modDisplayPack.manifest.dependencyId = "XJXQY";
	ok = check(modDisplayPack.getDisplayName() == "Test Mod" &&
		modDisplayPack.getDisplayAuthorText().empty(),
		"resource selection omits author text when the profile leaves Author empty") && ok;
	modDisplayPack.manifest.author = "Mod Author";
	ok = check(modDisplayPack.getDisplayAuthorText() == u8"作者：Mod Author",
		"resource selection displays configured MOD author text without pack-id special cases") && ok;
	ok = check(File::sanitizeSaveNamespace(u8"潇湘行.1/slot") == u8"潇湘行_1_slot",
		"portable save namespace keeps UTF-8 names and neutralizes path separators") && ok;
	const std::string localizedIniText = u8"[自定义]\n名称=剑侠\n[Game]\nMiXeD=Value\n";
	auto localizedIniBuffer = std::make_unique<char[]>(localizedIniText.size() + 1);
	std::memcpy(localizedIniBuffer.get(), localizedIniText.data(), localizedIniText.size());
	localizedIniBuffer[localizedIniText.size()] = '\0';
	INIReader localizedIni(localizedIniBuffer);
	ok = check(localizedIni.Get(u8"自定义", u8"名称", "") == u8"剑侠" &&
		localizedIni.Get("GAME", "mixed", "") == "Value",
		"INI lookup folds ASCII case without corrupting UTF-8 section or key names") && ok;
	ok = check(localizedIni.HasSection(u8"自定义") && localizedIni.HasSection("GAME") &&
		!localizedIni.HasSection("missing"),
		"INI section lookup folds ASCII case and preserves UTF-8 names") && ok;
	localizedIni.Set(u8"自定义", u8"新键", u8"新值");
	ok = check(localizedIni.Get(u8"自定义", u8"新键", "") == u8"新值",
		"INI mutation preserves UTF-8 section and key names") && ok;

	const std::string colorIniText =
		"[Init]\n"
		"Rgb=40,32,24\n"
		"Rgba=10,20,30,64\n"
		"Packed=4278850590\n";
	auto colorIniBuffer = std::make_unique<char[]>(colorIniText.size() + 1);
	std::memcpy(colorIniBuffer.get(), colorIniText.data(), colorIniText.size());
	colorIniBuffer[colorIniText.size()] = '\0';
	INIReader colorIni(colorIniBuffer);
	ok = check(colorIni.GetColor("Init", "Rgb", 0) == 0xFF282018,
		"INI color parser preserves RGB tuple") && ok;
	ok = check(colorIni.GetColor("Init", "Rgba", 0) == 0x400A141E,
		"INI color parser preserves RGBA alpha") && ok;
	ok = check(colorIni.GetColor("Init", "Missing", 0x12345678) == 0x12345678,
		"INI color parser preserves missing-key default") && ok;
	ok = check(colorIni.GetColor("Init", "Packed", 0) == 4278850590U,
		"INI color parser remains compatible with early packed editor values") && ok;
	colorIni.SetColor("Init", "Opaque", 0xFF112233);
	colorIni.SetColor("Init", "Translucent", 0x44112233);
	ok = check(colorIni.Get("Init", "Opaque", "") == "17,34,51" &&
		colorIni.Get("Init", "Translucent", "") == "17,34,51,68",
		"INI color writer emits RGB and RGBA tuples") && ok;

	File::setPlatformStateParentForTests("");
	const auto normalizedStateRoot =
		[](const std::string& value)
		{
			fs::path path = fs::u8path(value).lexically_normal();
			if (path.filename().empty())
			{
				path = path.parent_path();
			}
			return path.lexically_normal();
		};
	const fs::path explicitUserDataRoot =
		root / fs::u8path(u8"explicit user data 中文");
	const fs::path absoluteAssetsRoot =
		fs::absolute(logicalCollectionRoot).lexically_normal();
	const fs::path obsoleteStateRoot =
		explicitUserDataRoot / "collections" / "assets-deadbeef";
	writeRawFile(
		obsoleteStateRoot / "config" / "config.ini",
		"obsolete-config");
	writeRawFile(
		obsoleteStateRoot / "save" / "IgnoredPack" /
			"rpg5" / "obsolete-save.ini",
		"obsolete-save");
	writeRawFile(
		logicalCollectionRoot / "common" / "config" / "config.ini",
		"bundled-config");
	ok = check(
		File::configureUserDataRoot(
			explicitUserDataRoot.u8string(),
			absoluteAssetsRoot.u8string()),
		"explicit user-data root resolves without creating a resource dependency") && ok;
	const fs::path firstStateRoot =
		normalizedStateRoot(File::getUserDataRoot());
	ok = check(
		firstStateRoot == explicitUserDataRoot.lexically_normal(),
		"explicit user-data root is used directly without an assets hash") && ok;
	File::setActiveSaveNamespace("IgnoredPack");
	ok = check(
		!File::fileExist("save/rpg5/obsolete-save.ini") &&
		!fs::exists(
			explicitUserDataRoot / "save" / "IgnoredPack" /
				"rpg5" / "obsolete-save.ini"),
		"obsolete hashed saves are neither read nor migrated") && ok;
	ok = check(
		readViaSharedApplicationFile(CONFIG_INI) ==
			"bundled-config" &&
		!fs::exists(explicitUserDataRoot / "save" / "config.ini") &&
		readRawFile(
			obsoleteStateRoot / "config" / "config.ini") ==
				"obsolete-config",
		"obsolete hashed config is ignored in favor of the bundled default") && ok;
	const std::string updatedPortableConfig = "portable-config";
	ok = check(
		File::writeSharedApplicationFile(
			CONFIG_INI,
			updatedPortableConfig.data(),
			static_cast<int>(updatedPortableConfig.size())) &&
		readRawFile(explicitUserDataRoot / "save" / "config.ini") ==
			updatedPortableConfig &&
		readRawFile(
			obsoleteStateRoot / "config" / "config.ini") ==
				"obsolete-config",
		"saving config uses only save/config.ini and leaves obsolete paths untouched") && ok;

	const fs::path relativeAssetsRoot =
		"assets-identity-fixture";
	const fs::path sameDriveAbsoluteAssetsRoot =
		fs::absolute(relativeAssetsRoot).lexically_normal();
	const bool absoluteIdentityConfigured =
		File::configureUserDataRoot(
			explicitUserDataRoot.u8string(),
			sameDriveAbsoluteAssetsRoot.u8string());
	const fs::path absoluteAssetsStateRoot =
		normalizedStateRoot(File::getUserDataRoot());
	const bool relativeAssetsConfigured =
		File::configureUserDataRoot(
			explicitUserDataRoot.u8string(),
			relativeAssetsRoot.u8string());
	const fs::path relativeAssetsStateRoot =
		normalizedStateRoot(File::getUserDataRoot());
	ok = check(
		absoluteIdentityConfigured &&
		relativeAssetsConfigured &&
		absoluteAssetsStateRoot == explicitUserDataRoot.lexically_normal() &&
		relativeAssetsStateRoot == explicitUserDataRoot.lexically_normal(),
		"assets path spelling does not change the explicit user-data root") && ok;

	const fs::path sameLeafAssetsRoot =
		root / "another-collection" / "Assets";
	fs::create_directories(sameLeafAssetsRoot);
	ok = check(
		File::configureUserDataRoot(
			explicitUserDataRoot.u8string(),
			sameLeafAssetsRoot.u8string()) &&
		normalizedStateRoot(File::getUserDataRoot()) == firstStateRoot,
		"different assets directories do not redirect explicit mutable state") && ok;

#if !defined(__ANDROID__) && !defined(__APPLE__)
	const bool defaultUserDataConfigured =
		File::configureUserDataRoot(
			"", absoluteAssetsRoot.u8string());
	const fs::path expectedPortableStateRoot =
		absoluteAssetsRoot.parent_path().lexically_normal();
	const fs::path defaultStateRoot =
		normalizedStateRoot(File::getUserDataRoot());
	ok = check(
		defaultUserDataConfigured &&
		defaultStateRoot == expectedPortableStateRoot,
		"desktop default state is sibling to bin and assets in the portable root: actual=" +
			defaultStateRoot.u8string() + " expected=" +
			expectedPortableStateRoot.u8string()) && ok;
	const fs::path portableReleaseRoot = root / "portable-release";
	const fs::path portableAssetsRoot = portableReleaseRoot / "assets";
	fs::create_directories(portableReleaseRoot / "bin");
	fs::create_directories(portableAssetsRoot);
	ok = check(
		File::configureUserDataRoot("", portableAssetsRoot.u8string()),
		"portable release resolves its assets parent as the state root") && ok;
	File::setActiveSaveNamespace("PortablePack");
	const char portableSaveData[] = "portable-save";
	const char portableConfigData[] = "portable-config";
	ok = check(
		File::writeFileChecked(
			"save/game/portable.sav",
			portableSaveData,
			static_cast<int>(sizeof(portableSaveData) - 1)) &&
		File::writeSharedApplicationFile(
			CONFIG_INI,
			portableConfigData,
			static_cast<int>(sizeof(portableConfigData) - 1)) &&
		readRawFile(
			portableReleaseRoot / "save" / "portablepack" /
				"game" / "portable.sav") == "portable-save" &&
		readRawFile(portableReleaseRoot / "save" / "config.ini") ==
			"portable-config" &&
		!fs::exists(portableReleaseRoot / "userdata") &&
		!fs::exists(portableReleaseRoot / "config"),
		"portable release writes only sibling bin, assets, and save directories") && ok;
#endif

	ok = check(
		File::configureUserDataRoot(
			explicitUserDataRoot.u8string(),
			absoluteAssetsRoot.u8string()),
		"explicit state root can be restored after default-root characterization") && ok;
	const std::string frozenStateRoot =
		File::getUserDataRoot();
	File::setAssetsCollectionRoot(sameLeafAssetsRoot.u8string());
	ok = check(
		File::getUserDataRoot() == frozenStateRoot,
		"changing formal resource routing after startup cannot redirect mutable state") && ok;

	File::setAssetsCollectionRoot(absoluteAssetsRoot.u8string());

	const fs::path synchronizedStateProbeRoot =
		root / "synchronized-state-probe";
	const fs::path synchronizedStateProbeAssets =
		synchronizedStateProbeRoot / "jxqy-assets";
	fs::create_directories(synchronizedStateProbeAssets);
	ok = check(
		File::configureUserDataRoot(
			explicitUserDataRoot.u8string(),
			synchronizedStateProbeAssets.u8string()),
		"explicit state root remains independent from synchronized assets") && ok;
	File::setAssetsCollectionRoot(
		synchronizedStateProbeAssets.u8string());
	File::setActiveSaveNamespace("SyncPack");
	const char independentSaveData[] = "independent-save";
	const char independentConfigData[] = "independent-config";
	const fs::path synchronizedStateRoot =
		normalizedStateRoot(File::getUserDataRoot());
	const bool independentSaveWritten =
		File::writeFileChecked(
			"save/game/independent.sav",
			independentSaveData,
			static_cast<int>(sizeof(independentSaveData) - 1));
	const bool independentConfigWritten =
		File::writeSharedApplicationFile(
			CONFIG_INI,
			independentConfigData,
			static_cast<int>(sizeof(independentConfigData) - 1));
	ok = check(
		independentSaveWritten,
		"independent state root accepts save writes: " +
			synchronizedStateRoot.u8string()) && ok;
	ok = check(
		independentConfigWritten,
		"independent state root accepts config writes: " +
			synchronizedStateRoot.u8string()) && ok;
	ok = check(
		readRawFile(
			synchronizedStateRoot / "save" /
				"syncpack" / "game" / "independent.sav") ==
				"independent-save",
		"independent state root receives save bytes") && ok;
	ok = check(
		readRawFile(
			synchronizedStateRoot /
				"save" / "config.ini") == "independent-config",
		"independent state root receives config bytes") && ok;
	ok = check(
		!fs::exists(synchronizedStateProbeRoot / "save") &&
		!fs::exists(synchronizedStateProbeRoot / "config"),
		"assets location receives no mutable save or config directories") && ok;

	File::setSharedApplicationRootForTests("");
	File::setPlatformStateParentForTests("");
	fs::remove_all(root);
	return ok ? 0 : 1;
}
