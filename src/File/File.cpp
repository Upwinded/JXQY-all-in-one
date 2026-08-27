#include <algorithm>
#include "../libconvert/libconvert.h"
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#include "File.h"
#include "ResourceReadPrefixPolicy.h"
#include "ResourcePathSafety.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#define PREF_PATH_ORGANIZATION_NAME "Upwinded"
#define PREF_PATH_APPLICATION_NAME "JXQY All In One"
#include "log.h"

using PhysicalPathIdentity =
	EditorRun::DirectoryIdentity;

namespace
{
#if defined(__ANDROID__)
constexpr char AssetsPath[] = "";
constexpr ResourceReadPrefixPolicy::BundledRootMode PlatformBundledRootMode =
	ResourceReadPrefixPolicy::BundledRootMode::AndroidAssetNamespace;
#elif defined(__APPLE__)
constexpr char AssetsPath[] = "assets/";
constexpr ResourceReadPrefixPolicy::BundledRootMode PlatformBundledRootMode =
	ResourceReadPrefixPolicy::BundledRootMode::FilesystemPath;
#else
constexpr char AssetsPath[] = "../../assets/";
constexpr ResourceReadPrefixPolicy::BundledRootMode PlatformBundledRootMode =
	ResourceReadPrefixPolicy::BundledRootMode::FilesystemPath;
#endif

constexpr char GlobalConfigLogicalPath[] =
	"common/config/config.ini";
constexpr char GlobalConfigUserPath[] =
	"save/config.ini";

// 运行时资源路径覆盖（由 ResourceManager 设置）。
// 普通资源读取：主资源根(active；未设置时为 collection/default) > resourceFallbackRoots。
// 存档路径(save/)读取：平台写入根 > activeResourceRoot，不读取 fallback roots。
std::string g_assetsCollectionRoot;
std::string g_activeResourceRoot;
std::string g_commonResourceRoot;
std::vector<std::string> g_commonResourceFallbackRoots;
std::vector<std::string> g_resourceFallbackRoots;
std::vector<std::string> g_uiResourceFallbackRoots;
std::string g_uiCommonResourceRoot;
bool g_uiResourceFallbackConfigured = false;
bool g_preferLocalUi = true;
std::string g_activeSaveNamespace;
std::mutex g_formalResourceRoutingMutex;

struct FrozenEditorRunResourceRouting
{
	std::string assetsCollectionRoot;
	std::string activeResourceRoot;
	std::string commonResourceRoot;
	std::vector<std::string> commonResourceFallbackRoots;
	std::vector<std::string> resourceFallbackRoots;
	std::vector<std::string> uiResourceFallbackRoots;
	std::string uiCommonResourceRoot;
	bool uiResourceFallbackConfigured = false;
	bool preferLocalUi = true;
	std::string activeSaveNamespace;
};

struct InstalledEditorRunFileLayout
{
	std::string overlayRoot;
	std::string isolatedSaveRoot;
	std::string applicationStateRoot;
	std::string diagnosticsRoot;
	std::string diagnosticsPath;
	std::string logPath;
	std::string runtimeTracePath;
	std::string diagnosticsParentPath;
	std::string logParentPath;
	std::string runtimeTraceParentPath;
	PhysicalPathIdentity overlayIdentity;
	PhysicalPathIdentity isolatedSaveIdentity;
	PhysicalPathIdentity applicationStateIdentity;
	PhysicalPathIdentity diagnosticsIdentity;
	PhysicalPathIdentity diagnosticsParentIdentity;
	PhysicalPathIdentity logParentIdentity;
	PhysicalPathIdentity runtimeTraceParentIdentity;
	FrozenEditorRunResourceRouting resourceRouting;
};

struct RoutedResourcePath
{
	std::string root;
	std::string relativePath;
	PhysicalPathIdentity rootIdentity;
	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
	bool anchored = false;
	bool formalResourceRead = false;
};

std::mutex g_editorRunFileLayoutMutex;
std::optional<InstalledEditorRunFileLayout> g_editorRunFileLayout;
uint64_t g_editorRunFileLayoutGeneration = 0;
std::shared_mutex g_editorRunFileLayoutLifecycleMutex;
std::mutex g_editorRunFileLayoutResetHookMutex;
std::vector<std::pair<
	uint64_t, File::EditorRunFileLayoutResetHook>>
	g_editorRunFileLayoutResetHooks;
uint64_t g_editorRunFileLayoutResetHookId = 0;
#if defined(JXQY_ENABLE_TEST_HOOKS)
std::mutex g_editorRunFileOperationTestHookMutex;
File::EditorRunFileOperationTestHook g_editorRunFileOperationTestHook;
std::mutex g_sharedApplicationRootOverrideMutex;
std::string g_sharedApplicationRootOverrideForTests;
bool g_sharedApplicationRootUnavailableForTests = false;
#endif
std::mutex g_platformStateParentOverrideMutex;
#if defined(JXQY_ENABLE_TEST_HOOKS)
std::string g_platformStateParentOverrideForTests;
#endif
std::string g_configuredUserDataRoot;

// Lock order when both are needed:
// g_editorRunFileLayoutMutex, then g_formalResourceRoutingMutex.
// Formal-routing setters never acquire the layout mutex.

#if defined(JXQY_ENABLE_TEST_HOOKS)
void invokeEditorRunFileOperationTestHook(
	File::EditorRunFileOperationPhase phase)
{
	File::EditorRunFileOperationTestHook hook;
	{
		std::lock_guard<std::mutex> lock(
			g_editorRunFileOperationTestHookMutex);
		hook = g_editorRunFileOperationTestHook;
	}
	if (hook)
	{
		hook(phase);
	}
}
#else
#define invokeEditorRunFileOperationTestHook(...) ((void)0)
#endif

FrozenEditorRunResourceRouting currentEditorRunResourceRouting()
{
	std::lock_guard<std::mutex> lock(
		g_formalResourceRoutingMutex);
	FrozenEditorRunResourceRouting routing;
	routing.assetsCollectionRoot = g_assetsCollectionRoot;
	routing.activeResourceRoot = g_activeResourceRoot;
	routing.commonResourceRoot = g_commonResourceRoot;
	routing.commonResourceFallbackRoots =
		g_commonResourceFallbackRoots;
	routing.resourceFallbackRoots = g_resourceFallbackRoots;
	routing.uiResourceFallbackRoots = g_uiResourceFallbackRoots;
	routing.uiCommonResourceRoot = g_uiCommonResourceRoot;
	routing.uiResourceFallbackConfigured =
		g_uiResourceFallbackConfigured;
	routing.preferLocalUi = g_preferLocalUi;
	routing.activeSaveNamespace = g_activeSaveNamespace;
	return routing;
}

std::vector<std::string*> mutableFormalRootValues(
	FrozenEditorRunResourceRouting& routing)
{
	std::vector<std::string*> roots = {
		&routing.assetsCollectionRoot,
		&routing.activeResourceRoot,
		&routing.commonResourceRoot,
		&routing.uiCommonResourceRoot
	};
	for (std::string& root : routing.resourceFallbackRoots)
	{
		roots.push_back(&root);
	}
	for (std::string& root : routing.commonResourceFallbackRoots)
	{
		roots.push_back(&root);
	}
	for (std::string& root : routing.uiResourceFallbackRoots)
	{
		roots.push_back(&root);
	}
	return roots;
}

RoutedResourcePath anchoredRoute(
	const std::string& root,
	const std::string& relativePath,
	const PhysicalPathIdentity& identity,
	File::EditorRunFileLayoutState state,
	uint64_t generation)
{
	RoutedResourcePath route;
	route.root = root;
	route.relativePath = relativePath;
	route.rootIdentity = identity;
	route.routingState = state;
	route.routingGeneration = generation;
	route.anchored = state == File::EditorRunFileLayoutState::Valid &&
		identity.valid;
	return route;
}

RoutedResourcePath formalResourceReadRoute(
	const std::string& root,
	const std::string& relativePath)
{
	RoutedResourcePath route;
	route.root = root;
	route.relativePath = relativePath;
	route.formalResourceRead = true;
	return route;
}

std::string pathToUtf8String(const std::filesystem::path& path);
std::filesystem::path installedIdentityPath(
	const std::string& identity);
bool installedEditorRunFileLayoutIsCurrent(
	const InstalledEditorRunFileLayout& layout);

std::string normalizeRoot(std::string root)
{
	convert::replaceAllString(root, "\\", "/");
	if (!root.empty() && root.back() != '/')
	{
		root += "/";
	}
	return root;
}

std::string normalizeRelativePath(std::string fileName)
{
	convert::replaceAllString(fileName, "\\", "/");
	while (!fileName.empty() && (fileName.front() == '/' || fileName.front() == '\\'))
	{
		fileName.erase(fileName.begin());
	}
	for (char& ch : fileName)
	{
		if (ch >= 'A' && ch <= 'Z')
		{
			ch = static_cast<char>(ch + ('a' - 'A'));
		}
	}
	return fileName;
}

using ResourcePathSafety::isValidUtf8;

std::string normalizePathSegments(std::string path)
{
	convert::replaceAllString(path, "\\", "/");
	if (path.empty())
	{
		return path;
	}

	bool hasTrailingSlash = path.back() == '/';
	std::string prefix;
	size_t start = 0;
	if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
	{
		prefix = path.substr(0, 2);
		start = 2;
		if (start < path.size() && path[start] == '/')
		{
			prefix += "/";
			start++;
		}
	}
	else if (path.front() == '/')
	{
		prefix = "/";
		start = 1;
	}

	std::vector<std::string> segments;
	while (start <= path.size())
	{
		size_t next = path.find('/', start);
		std::string segment = next == std::string::npos ? path.substr(start) : path.substr(start, next - start);
		start = next == std::string::npos ? path.size() + 1 : next + 1;
		if (segment.empty() || segment == ".")
		{
			continue;
		}
		if (segment == "..")
		{
			if (!segments.empty() && segments.back() != "..")
			{
				segments.pop_back();
			}
			else if (prefix.empty())
			{
				segments.push_back(segment);
			}
			continue;
		}
		segments.push_back(segment);
	}

	std::string result = prefix;
	for (const auto& segment : segments)
	{
		if (!result.empty() && result.back() != '/')
		{
			result += "/";
		}
		result += segment;
	}
	if (result.empty() && !prefix.empty())
	{
		result = prefix;
	}
	if (hasTrailingSlash && !result.empty() && result.back() != '/')
	{
		result += "/";
	}
	return result;
}

std::string toLowerAscii(std::string value)
{
	for (char& ch : value)
	{
		if (ch >= 'A' && ch <= 'Z')
		{
			ch = static_cast<char>(ch + ('a' - 'A'));
		}
	}
	return value;
}

bool isSavePath(const std::string& fileName)
{
	std::string normalized = toLowerAscii(normalizeRelativePath(fileName));
	return normalized == "save" || normalized.rfind("save/", 0) == 0;
}

bool isLegacyNewGameSavePath(const std::string& fileName)
{
	const std::string normalized =
		toLowerAscii(normalizeRelativePath(fileName));
	if (normalized == "save/rpg0" ||
		normalized.rfind("save/rpg0/", 0) == 0)
	{
		return true;
	}
	if (normalized.rfind("save/shot/", 0) != 0)
	{
		return false;
	}
	const std::filesystem::path path =
		std::filesystem::u8path(normalized);
	return pathToUtf8String(path.stem()) == "rpg0";
}

std::string sharedApplicationUserRelativePath(
	const std::string& fileName)
{
	const std::string normalized = normalizeRelativePath(fileName);
	return toLowerAscii(normalized) == GlobalConfigLogicalPath
		? std::string(GlobalConfigUserPath)
		: normalized;
}

bool isGlobalConfigPath(const std::string& fileName)
{
	return toLowerAscii(normalizeRelativePath(fileName)) ==
		GlobalConfigLogicalPath;
}

File::EditorRunFileLayoutState getInstalledEditorRunFileLayout(
	InstalledEditorRunFileLayout& layout,
	uint64_t* generation = nullptr)
{
	std::lock_guard<std::mutex> lock(g_editorRunFileLayoutMutex);
	if (generation != nullptr)
	{
		*generation = g_editorRunFileLayoutGeneration;
	}
	if (!g_editorRunFileLayout)
	{
		return File::EditorRunFileLayoutState::NotInstalled;
	}
	layout = *g_editorRunFileLayout;
	return installedEditorRunFileLayoutIsCurrent(layout)
		? File::EditorRunFileLayoutState::Valid
		: File::EditorRunFileLayoutState::Invalid;
}

bool editorRunRoutingSnapshotIsCurrent(
	File::EditorRunFileLayoutState expectedState,
	uint64_t expectedGeneration)
{
	InstalledEditorRunFileLayout layout;
	uint64_t currentGeneration = 0;
	const File::EditorRunFileLayoutState currentState =
		getInstalledEditorRunFileLayout(layout, &currentGeneration);
	return expectedState != File::EditorRunFileLayoutState::Invalid &&
		currentState == expectedState &&
		currentGeneration == expectedGeneration;
}

bool explicitFormalResourceRoute(
	bool activeRoot, RoutedResourcePath& route)
{
	route = {};
	InstalledEditorRunFileLayout layout;
	uint64_t generation = 0;
	const File::EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (state == File::EditorRunFileLayoutState::Invalid)
	{
		return false;
	}
	if (state == File::EditorRunFileLayoutState::Valid)
	{
		route.root = activeRoot
			? layout.resourceRouting.activeResourceRoot
			: layout.resourceRouting.commonResourceRoot;
		route.formalResourceRead = true;
		return !route.root.empty();
	}
	const FrozenEditorRunResourceRouting routing =
		currentEditorRunResourceRouting();
	route.root = activeRoot
		? routing.activeResourceRoot
		: routing.commonResourceRoot;
	route.formalResourceRead = true;
	return !route.root.empty();
}

std::vector<RoutedResourcePath> explicitCommonResourceRoutes()
{
	std::vector<RoutedResourcePath> routes;
	InstalledEditorRunFileLayout layout;
	uint64_t generation = 0;
	const File::EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (state == File::EditorRunFileLayoutState::Invalid)
	{
		return routes;
	}
	const FrozenEditorRunResourceRouting routing =
		state == File::EditorRunFileLayoutState::Valid
			? layout.resourceRouting
			: currentEditorRunResourceRouting();
	const auto appendRoot = [&routes](const std::string& root)
	{
		if (root.empty())
		{
			return;
		}
		for (const RoutedResourcePath& route : routes)
		{
			if (route.root == root)
			{
				return;
			}
		}
		RoutedResourcePath route;
		route.root = root;
		route.formalResourceRead = true;
		routes.push_back(std::move(route));
	};
	appendRoot(routing.commonResourceRoot);
	for (const std::string& fallbackRoot :
		routing.commonResourceFallbackRoots)
	{
		appendRoot(fallbackRoot);
	}
	return routes;
}

std::string editorRunRelativePath(const std::string& fileName)
{
	std::string normalized = normalizeRelativePath(fileName);
	if (isSavePath(normalized))
	{
		if (normalized == "save")
		{
			return "";
		}
		return normalized.substr(std::string("save/").size());
	}
	return normalized;
}

bool isUiResourcePath(const std::string& fileName)
{
	std::string normalized = toLowerAscii(normalizeRelativePath(fileName));
	static const std::vector<std::string> uiRoots = {
		"ini/ui",
		"asf/ui",
		"mpc/ui",
		"bmp/ui",
		"image/ui",
		"sound/ui"
	};
	for (const auto& uiRoot : uiRoots)
	{
		if (normalized == uiRoot || normalized.rfind(uiRoot + "/", 0) == 0)
		{
			return true;
		}
	}
	return false;
}

std::string sanitizeNamespaceValue(std::string value)
{
	convert::replaceAllString(value, "\\", "/");
	std::string result;
	for (char ch : value)
	{
		unsigned char c = static_cast<unsigned char>(ch);
		if (c >= 0x80 || std::isalnum(c) || ch == '-' || ch == '_')
		{
			result.push_back(ch);
		}
		else if (ch == '/' || ch == ':' || ch == '.')
		{
			result.push_back('_');
		}
	}
	if (result.empty())
	{
		result = "default";
	}
	return result;
}

std::string fallbackSaveNamespace()
{
	const FrozenEditorRunResourceRouting routing =
		currentEditorRunResourceRouting();
	if (!routing.activeSaveNamespace.empty())
	{
		return sanitizeNamespaceValue(routing.activeSaveNamespace);
	}
	if (!routing.activeResourceRoot.empty())
	{
		return sanitizeNamespaceValue(routing.activeResourceRoot);
	}
	if (!routing.assetsCollectionRoot.empty())
	{
		return sanitizeNamespaceValue(routing.assetsCollectionRoot);
	}
	return "default";
}

bool pathIsDirectory(const std::string& fullPath)
{
#if defined(__APPLE__)
	try
	{
		std::error_code errorCode;
		const std::filesystem::file_status status =
			std::filesystem::status(
				std::filesystem::u8path(fullPath), errorCode);
		return !errorCode && std::filesystem::is_directory(status);
	}
	catch (const std::exception&)
	{
		return false;
	}
#else
	SDL_PathInfo info;
	return SDL_GetPathInfo(fullPath.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
#endif
}

bool defaultAssetsRootLooksValid(const std::string& root)
{
	return !root.empty() && pathIsDirectory(normalizeRoot(root));
}

std::string buildDefaultAssetsPrefix()
{
	static std::string cachedDefaultAssetsPrefix;
	if (!cachedDefaultAssetsPrefix.empty())
	{
		return cachedDefaultAssetsPrefix;
	}

	std::vector<std::string> candidates = { AssetsPath };
	const char* basePath = SDL_GetBasePath();
	if (basePath != nullptr && basePath[0] != '\0')
	{
		std::string baseRoot = normalizeRoot(basePath);
		candidates.push_back(baseRoot + "assets/");
#if defined(__APPLE__) && TARGET_OS_OSX
		candidates.push_back(baseRoot + "../Resources/assets/");
#endif
		candidates.push_back(baseRoot + "../assets/");
		candidates.push_back(baseRoot + "../../assets/");
		candidates.push_back(baseRoot + "../../../assets/");
	}

	for (const auto& candidate : candidates)
	{
		if (defaultAssetsRootLooksValid(candidate))
		{
			cachedDefaultAssetsPrefix = normalizeRoot(candidate);
			return cachedDefaultAssetsPrefix;
		}
	}

	cachedDefaultAssetsPrefix = AssetsPath;
	return cachedDefaultAssetsPrefix;
}

// 构建主资源路径前缀。普通资源缺失时可继续尝试 fallback roots；save/ 不使用 fallback roots。
// macOS 非 iOS 在使用编译期默认时需要拼接 SDL_GetBasePath。
std::string buildPrimaryAssetsPrefix()
{
	const FrozenEditorRunResourceRouting routing =
		currentEditorRunResourceRouting();
	if (!routing.activeResourceRoot.empty())
	{
		return routing.activeResourceRoot;
	}
	if (!routing.assetsCollectionRoot.empty())
	{
		return routing.assetsCollectionRoot;
	}
	return buildDefaultAssetsPrefix();
}

std::string buildAssetsCollectionPrefix()
{
	const FrozenEditorRunResourceRouting routing =
		currentEditorRunResourceRouting();
	if (!routing.assetsCollectionRoot.empty())
	{
		return routing.assetsCollectionRoot;
	}
	return buildDefaultAssetsPrefix();
}

std::string getPlatformWritableBase()
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
	{
		std::lock_guard<std::mutex> lock(
			g_sharedApplicationRootOverrideMutex);
		if (g_sharedApplicationRootUnavailableForTests)
		{
			return "";
		}
		if (!g_sharedApplicationRootOverrideForTests.empty())
		{
			return normalizeRoot(
				g_sharedApplicationRootOverrideForTests);
		}
	}
#endif
#if defined(__ANDROID__)
	const char* storagePath = SDL_GetAndroidInternalStoragePath();
	std::string path = storagePath != nullptr ? storagePath : "";
	if (!path.empty() && path.back() != '/')
	{
		path += "/";
	}
	return path;
#else
	char* prefPath =
		SDL_GetPrefPath(
			PREF_PATH_ORGANIZATION_NAME,
			PREF_PATH_APPLICATION_NAME);
	std::string path = prefPath != nullptr ? prefPath : "";
	if (prefPath != nullptr)
	{
		SDL_free(prefPath);
	}
	if (!path.empty() && path.back() != '/')
	{
		path += "/";
	}
	return path;
#endif
}

std::string buildExecutableDirectory()
{
	const char* basePath = SDL_GetBasePath();
	if (basePath == nullptr || basePath[0] == '\0')
	{
		return "";
	}
	return normalizeRoot(basePath);
}

std::string buildRelativeUserDataAnchor()
{
#if defined(__ANDROID__) || defined(__APPLE__)
	return getPlatformWritableBase();
#else
	return buildExecutableDirectory();
#endif
}

std::string resolveConfiguredUserDataRoot(
	const std::string& configuredRoot)
{
	if (configuredRoot.empty())
	{
		return "";
	}
	if (!isValidUtf8(configuredRoot))
	{
		return "";
	}

	try
	{
		std::filesystem::path path =
			std::filesystem::u8path(configuredRoot);
		if (path.empty())
		{
			return "";
		}
		if (!path.is_absolute())
		{
			if (path.has_root_name())
			{
				return "";
			}
			const std::string anchor =
				buildRelativeUserDataAnchor();
			if (anchor.empty())
			{
				return "";
			}
			path = std::filesystem::u8path(anchor) / path;
		}
		path = path.lexically_normal();
		if (!path.is_absolute())
		{
			return "";
		}
		return normalizeRoot(pathToUtf8String(path));
	}
	catch (const std::exception&)
	{
		return "";
	}
}

std::string buildDesktopDistributionRoot(
	const std::string& configuredAssetsRoot)
{
	const std::string assetsRoot = configuredAssetsRoot.empty()
		? buildDefaultAssetsPrefix()
		: configuredAssetsRoot;
	if (assetsRoot.empty())
	{
		return "";
	}
	try
	{
		std::filesystem::path assetsPath =
			std::filesystem::u8path(assetsRoot).
				lexically_normal();
		if (!assetsPath.is_absolute())
		{
			std::error_code error;
			assetsPath = std::filesystem::absolute(
				assetsPath, error);
			if (error)
			{
				return "";
			}
			assetsPath = assetsPath.lexically_normal();
		}
		if (assetsPath.filename().empty())
		{
			assetsPath = assetsPath.parent_path();
		}
		const std::filesystem::path distributionRoot =
			assetsPath.parent_path();
		return distributionRoot.empty()
			? std::string()
			: normalizeRoot(pathToUtf8String(distributionRoot));
	}
	catch (const std::exception&)
	{
		return "";
	}
}

std::string buildUserDataRoot(
	const std::string& configuredRoot,
	const std::string& configuredAssetsRoot)
{
	if (!configuredRoot.empty())
	{
		return resolveConfiguredUserDataRoot(configuredRoot);
	}
#if defined(__ANDROID__) || defined(__APPLE__)
	return getPlatformWritableBase();
#else
	return buildDesktopDistributionRoot(configuredAssetsRoot);
#endif
}

std::string buildPlatformStateParent()
{
	{
		std::lock_guard<std::mutex> lock(
			g_platformStateParentOverrideMutex);
#if defined(JXQY_ENABLE_TEST_HOOKS)
		if (!g_platformStateParentOverrideForTests.empty())
		{
			return g_platformStateParentOverrideForTests;
		}
#endif
		if (!g_configuredUserDataRoot.empty())
		{
			return g_configuredUserDataRoot;
		}
	}
	return buildUserDataRoot("", buildAssetsCollectionPrefix());
}

std::string buildUserSaveRelativePath(const std::string& fileName)
{
	return normalizeRelativePath(
		"save/" + fallbackSaveNamespace() + "/" +
		editorRunRelativePath(fileName));
}

std::string buildWritePrefix()
{
#if defined(__ANDROID__) || defined(__APPLE__)
	std::string writableBase = getPlatformWritableBase();
	if (!writableBase.empty())
	{
		return normalizeRoot(writableBase + fallbackSaveNamespace());
	}
#endif
	return buildPrimaryAssetsPrefix();
}

std::string buildSharedApplicationWritePrefix()
{
	std::string writableBase = getPlatformWritableBase();
	if (!writableBase.empty())
	{
		return normalizeRoot(writableBase);
	}
	return "";
}

RoutedResourcePath buildWriteRoute(const std::string& fileName,
	File::EditorRunFileLayoutState* capturedState = nullptr,
	uint64_t* capturedGeneration = nullptr)
{
	// rpg0 is only a legacy source-layout name. Converted resources keep the
	// immutable new-game template in ini/save, and user state must never create
	// an independent save/rpg0 generation.
	if (isLegacyNewGameSavePath(fileName))
	{
		return {};
	}
	InstalledEditorRunFileLayout layout;
	uint64_t generation = 0;
	const File::EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (capturedState != nullptr)
	{
		*capturedState = state;
	}
	if (capturedGeneration != nullptr)
	{
		*capturedGeneration = generation;
	}
	if (state == File::EditorRunFileLayoutState::Invalid)
	{
		return {};
	}
	if (state == File::EditorRunFileLayoutState::Valid)
	{
		if (isSavePath(fileName))
		{
			return anchoredRoute(
				layout.isolatedSaveRoot,
				editorRunRelativePath(fileName),
				layout.isolatedSaveIdentity,
				state, generation);
		}
		return anchoredRoute(
			layout.overlayRoot,
			normalizeRelativePath(fileName),
			layout.overlayIdentity,
			state, generation);
	}
	if (isSavePath(fileName))
	{
		return {
			buildPlatformStateParent(),
			buildUserSaveRelativePath(fileName)
		};
	}
	return { buildWritePrefix(), normalizeRelativePath(fileName) };
}

RoutedResourcePath buildSharedApplicationWriteRoute(
	const std::string& fileName,
	File::EditorRunFileLayoutState* capturedState = nullptr,
	uint64_t* capturedGeneration = nullptr)
{
	InstalledEditorRunFileLayout layout;
	uint64_t generation = 0;
	const File::EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (capturedState != nullptr)
	{
		*capturedState = state;
	}
	if (capturedGeneration != nullptr)
	{
		*capturedGeneration = generation;
	}
	if (state == File::EditorRunFileLayoutState::Invalid)
	{
		return {};
	}
	if (state == File::EditorRunFileLayoutState::Valid)
	{
		return anchoredRoute(
			layout.applicationStateRoot,
			sharedApplicationUserRelativePath(fileName),
			layout.applicationStateIdentity,
			state, generation);
	}
	if (isGlobalConfigPath(fileName))
	{
		return {
			buildPlatformStateParent(),
			GlobalConfigUserPath
		};
	}
	return {
		buildSharedApplicationWritePrefix(),
		normalizeRelativePath(fileName)
	};
}

std::vector<RoutedResourcePath> buildSharedApplicationReadRoutes(
	const std::string& fileName)
{
	InstalledEditorRunFileLayout layout;
	uint64_t generation = 0;
	const File::EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (state == File::EditorRunFileLayoutState::Invalid)
	{
		return {};
	}
	if (state == File::EditorRunFileLayoutState::Valid)
	{
		return {
			anchoredRoute(
				layout.applicationStateRoot,
				sharedApplicationUserRelativePath(fileName),
				layout.applicationStateIdentity,
				state, generation)
		};
	}

	std::vector<RoutedResourcePath> routes;
	const std::string normalizedRelativePath =
		normalizeRelativePath(fileName);
	if (isGlobalConfigPath(fileName))
	{
		const std::string stateParent =
			buildPlatformStateParent();
		if (!stateParent.empty())
		{
				routes.push_back({
				stateParent,
				GlobalConfigUserPath
			});
		}
	}
	else
	{
		const std::string userPrefix =
			buildSharedApplicationWritePrefix();
		if (!userPrefix.empty())
		{
			routes.push_back({
				userPrefix,
				normalizedRelativePath
			});
		}
	}
	std::vector<std::string> bundledPrefixes;
	ResourceReadPrefixPolicy::appendPrimaryPrefix(
		bundledPrefixes,
		buildAssetsCollectionPrefix(),
		PlatformBundledRootMode);
	for (const auto& prefix : bundledPrefixes)
	{
		routes.push_back({ prefix, normalizedRelativePath });
	}
	return routes;
}

std::vector<RoutedResourcePath> buildSaveReadRoutes(
	const std::string& fileName)
{
	const std::string normalized =
		normalizeRelativePath(fileName);
	if (isLegacyNewGameSavePath(normalized))
	{
		return {};
	}
	const std::string stateParent =
		buildPlatformStateParent();
	if (stateParent.empty())
	{
		return {};
	}
	return {{
		stateParent,
		buildUserSaveRelativePath(normalized)
	}};
}

std::vector<std::string> buildReadPrefixes(
	const std::string& fileName,
	const FrozenEditorRunResourceRouting* frozenRouting = nullptr)
{
	const bool useFrozenRouting = frozenRouting != nullptr;
	FrozenEditorRunResourceRouting currentRouting;
	if (frozenRouting == nullptr)
	{
		currentRouting = currentEditorRunResourceRouting();
		frozenRouting = &currentRouting;
	}
	std::vector<std::string> prefixes;
	const std::string primaryPrefix =
		useFrozenRouting
		? frozenRouting->activeResourceRoot
		: (!frozenRouting->activeResourceRoot.empty()
			? frozenRouting->activeResourceRoot
			: (!frozenRouting->assetsCollectionRoot.empty()
				? frozenRouting->assetsCollectionRoot
				: buildDefaultAssetsPrefix()));
	const bool uiResourcePath = isUiResourcePath(fileName);
	const bool useUiFallbackRoots = uiResourcePath &&
		frozenRouting->uiResourceFallbackConfigured;
	const bool preferLocalUi =
		frozenRouting->preferLocalUi;
	const std::vector<std::string>& resourceFallbackRoots =
		frozenRouting->resourceFallbackRoots;
	const std::vector<std::string>& uiResourceFallbackRoots =
		frozenRouting->uiResourceFallbackRoots;
	const std::string& uiCommonResourceRoot =
		frozenRouting->uiCommonResourceRoot;
	const ResourceReadPrefixPolicy::BundledRootMode primaryRootMode =
		useFrozenRouting
		? ResourceReadPrefixPolicy::BundledRootMode::FilesystemPath
		: PlatformBundledRootMode;
	auto appendPrimaryPrefix = [&prefixes, primaryRootMode](
		const std::string& prefix)
	{
		ResourceReadPrefixPolicy::appendPrimaryPrefix(
			prefixes, prefix, primaryRootMode);
	};
	auto appendFallbackPrefix = [&prefixes](const std::string& prefix)
	{
		ResourceReadPrefixPolicy::appendUniquePrefix(
			prefixes, prefix, false);
	};

	if (useUiFallbackRoots)
	{
		if (preferLocalUi)
		{
			appendPrimaryPrefix(primaryPrefix);
		}
		for (const auto& fallbackRoot : uiResourceFallbackRoots)
		{
			appendFallbackPrefix(fallbackRoot);
		}
		if (!preferLocalUi)
		{
			appendPrimaryPrefix(primaryPrefix);
		}
		appendFallbackPrefix(uiCommonResourceRoot);
		return prefixes;
	}

	appendPrimaryPrefix(primaryPrefix);
	for (const auto& fallbackRoot : resourceFallbackRoots)
	{
		appendFallbackPrefix(fallbackRoot);
	}
	return prefixes;
}

std::vector<RoutedResourcePath> buildReadRoutes(const std::string& fileName)
{
	const std::string normalizedResourcePath =
		toLowerAscii(normalizeRelativePath(fileName));
	if (isLegacyNewGameSavePath(normalizedResourcePath))
	{
		return {};
	}
	InstalledEditorRunFileLayout layout;
	uint64_t generation = 0;
	const File::EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (state == File::EditorRunFileLayoutState::Invalid)
	{
		return {};
	}
	if (state == File::EditorRunFileLayoutState::Valid)
	{
		if (isSavePath(fileName))
		{
			return { anchoredRoute(
				layout.isolatedSaveRoot,
				editorRunRelativePath(fileName),
				layout.isolatedSaveIdentity,
				state, generation) };
		}

		std::vector<RoutedResourcePath> routes = {
			anchoredRoute(
				layout.overlayRoot,
				normalizedResourcePath,
				layout.overlayIdentity,
				state, generation)
		};
		for (const auto& prefix :
			buildReadPrefixes(fileName, &layout.resourceRouting))
		{
			if (std::find_if(routes.begin(), routes.end(),
				[&prefix](const RoutedResourcePath& route)
				{
					return route.root == prefix;
				}) == routes.end())
			{
				routes.push_back(formalResourceReadRoute(
					prefix, normalizedResourcePath));
			}
		}
		return routes;
	}
	if (isSavePath(fileName))
	{
		return buildSaveReadRoutes(fileName);
	}

	std::vector<RoutedResourcePath> routes;
	for (const auto& prefix : buildReadPrefixes(fileName))
	{
		routes.push_back(formalResourceReadRoute(
			prefix, normalizedResourcePath));
	}
	return routes;
}

bool isLexicallyContainedByPrefix(
	const std::string& prefix,
	const std::string& fullPath)
{
#if defined(__ANDROID__)
	(void)prefix;
	(void)fullPath;
	return true;
#else
	if (prefix.empty() || fullPath.empty() ||
		!isValidUtf8(prefix) || !isValidUtf8(fullPath))
	{
		return false;
	}
	try
	{
		const auto trimTrailingSeparator =
			[](std::filesystem::path path)
			{
				while (!path.empty() &&
					path != path.root_path() &&
					path.filename().empty())
				{
					path = path.parent_path();
				}
				return path;
			};
		std::error_code rootError;
		std::error_code pathError;
		const std::filesystem::path lexicalRoot =
			trimTrailingSeparator(
				std::filesystem::absolute(
					std::filesystem::u8path(prefix),
					rootError).lexically_normal());
		const std::filesystem::path lexicalPath =
			trimTrailingSeparator(
				std::filesystem::absolute(
					std::filesystem::u8path(fullPath),
					pathError).lexically_normal());
		if (rootError || pathError ||
			lexicalRoot.empty() || lexicalPath.empty())
		{
			return false;
		}

		auto rootIterator = lexicalRoot.begin();
		auto pathIterator = lexicalPath.begin();
		for (; rootIterator != lexicalRoot.end();
			++rootIterator, ++pathIterator)
		{
			if (pathIterator == lexicalPath.end())
			{
				return false;
			}
#if defined(_WIN32)
			if (toLowerAscii(rootIterator->u8string()) !=
				toLowerAscii(pathIterator->u8string()))
#else
			if (*rootIterator != *pathIterator)
#endif
			{
				return false;
			}
		}
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
#endif
}

bool isFullPathContainedByPrefix(const std::string& prefix, const std::string& fullPath)
{
#if defined(__ANDROID__)
	(void)prefix;
	(void)fullPath;
	return true;
#else
	if (prefix.empty() || fullPath.empty() || !isValidUtf8(prefix) || !isValidUtf8(fullPath))
	{
		return false;
	}
	try
	{
		std::error_code rootError;
		std::error_code pathError;
		std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(
			std::filesystem::u8path(prefix), rootError);
		std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(
			std::filesystem::u8path(fullPath), pathError);
		if (rootError || pathError || canonicalRoot.empty() || canonicalPath.empty())
		{
			return false;
		}

		auto rootIterator = canonicalRoot.begin();
		auto pathIterator = canonicalPath.begin();
		for (; rootIterator != canonicalRoot.end(); ++rootIterator, ++pathIterator)
		{
			if (pathIterator == canonicalPath.end())
			{
				return false;
			}
#if defined(_WIN32)
			if (toLowerAscii(rootIterator->u8string()) != toLowerAscii(pathIterator->u8string()))
#else
			if (*rootIterator != *pathIterator)
#endif
			{
				return false;
			}
		}
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
#endif
}

bool isStrictRelativeResourcePath(const std::string& fileName)
{
	if (!ResourcePathSafety::isSafeVirtualResourcePath(fileName))
	{
		return false;
	}
	try
	{
		const std::filesystem::path relativePath =
			std::filesystem::u8path(fileName);
		if (relativePath.empty() ||
			relativePath.is_absolute() ||
			relativePath.has_root_name() ||
			relativePath.has_root_directory())
		{
			return false;
		}
		for (const std::filesystem::path& component : relativePath)
		{
			if (component == "..")
			{
				return false;
			}
		}
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

std::string makeFormalResourceReadPath(
	const std::string& prefix,
	const std::string& fileName)
{
	if (!isStrictRelativeResourcePath(fileName))
	{
		return "";
	}
#if defined(__ANDROID__)
	std::string fullPath =
		normalizeRoot(prefix) +
		normalizeRelativePath(fileName);
	convert::replaceAllString(fullPath, "\\", "/");
	return normalizePathSegments(fullPath);
#else
	if (prefix.empty())
	{
		return "";
	}
	try
	{
		const std::filesystem::path root =
			std::filesystem::u8path(prefix).lexically_normal();
		const std::filesystem::path fullPath =
			(root / std::filesystem::u8path(fileName)).
				lexically_normal();
		const std::string fullPathValue =
			pathToUtf8String(fullPath);
		return isLexicallyContainedByPrefix(
				prefix, fullPathValue)
			? fullPathValue
			: "";
	}
	catch (const std::exception&)
	{
		return "";
	}
#endif
}

std::string makeFullPath(const std::string& prefix, const std::string& fileName)
{
	std::string fullPath = normalizeRoot(prefix) + normalizeRelativePath(fileName);
	convert::replaceAllString(fullPath, "\\", "/");
	fullPath = normalizePathSegments(fullPath);
	return isFullPathContainedByPrefix(prefix, fullPath) ? fullPath : "";
}

std::string makeRoutedReadPath(
	const RoutedResourcePath& route)
{
	if (!route.anchored && route.formalResourceRead)
	{
		return makeFormalResourceReadPath(
			route.root, route.relativePath);
	}
	return makeFullPath(route.root, route.relativePath);
}

bool routedReadPathIsContained(
	const RoutedResourcePath& route,
	const std::string& fullPath)
{
	return !route.anchored && route.formalResourceRead
		? isLexicallyContainedByPrefix(route.root, fullPath)
		: isFullPathContainedByPrefix(route.root, fullPath);
}

bool openFileForRead(const std::string& fullPath, std::unique_ptr<char[]>& s, int& len,
	std::size_t maximumBytes)
{
	struct SDLIOStreamCloser
	{
		void operator()(SDL_IOStream* stream) const
		{
			if (stream != nullptr)
			{
				SDL_CloseIO(stream);
			}
		}
	};

	constexpr std::size_t MaxReadableFileBytes = static_cast<std::size_t>(INT_MAX) - 1;
	maximumBytes = (std::min)(maximumBytes, MaxReadableFileBytes);
	s.reset();
	len = 0;
	std::unique_ptr<SDL_IOStream, SDLIOStreamCloser> fp(
		SDL_IOFromFile(fullPath.c_str(), "rb"));
	if (!fp)
	{
		return false;
	}

	Sint64 streamSize = SDL_GetIOSize(fp.get());
	if (streamSize < 0)
	{
		Sint64 currentPosition = SDL_TellIO(fp.get());
		if (currentPosition >= 0 && SDL_SeekIO(fp.get(), 0, SDL_IO_SEEK_END) >= 0)
		{
			streamSize = SDL_TellIO(fp.get());
			SDL_SeekIO(fp.get(), currentPosition, SDL_IO_SEEK_SET);
		}
	}

	if (streamSize >= 0)
	{
		if (static_cast<uint64_t>(streamSize) > maximumBytes ||
			SDL_SeekIO(fp.get(), 0, SDL_IO_SEEK_SET) < 0)
		{
			return false;
		}
		int length = static_cast<int>(streamSize);
		std::unique_ptr<char[]> result;
		try
		{
			result = std::make_unique<char[]>(static_cast<std::size_t>(length) + 1);
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		catch (const std::length_error&)
		{
			return false;
		}
		memset(result.get(), 0, static_cast<std::size_t>(length) + 1);
		int bytesRead = 0;
		while (bytesRead < length)
		{
			size_t count = SDL_ReadIO(fp.get(), result.get() + bytesRead,
				static_cast<size_t>(length - bytesRead));
			if (count == 0)
			{
				break;
			}
			bytesRead += (int)count;
		}
		len = bytesRead;
		result[bytesRead] = '\0';
		s = std::move(result);
		return true;
	}

	std::vector<char> buffer;
	char chunk[64 * 1024];
	for (;;)
	{
		size_t count = SDL_ReadIO(fp.get(), chunk, sizeof(chunk));
		if (count == 0)
		{
			break;
		}
		if (buffer.size() > maximumBytes ||
			count > maximumBytes - buffer.size())
		{
			return false;
		}
		try
		{
			buffer.insert(buffer.end(), chunk, chunk + count);
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		catch (const std::length_error&)
		{
			return false;
		}
	}
	len = (int)buffer.size();
	try
	{
		s = std::make_unique<char[]>(buffer.size() + 1);
	}
	catch (const std::bad_alloc&)
	{
		len = 0;
		return false;
	}
	catch (const std::length_error&)
	{
		len = 0;
		return false;
	}
	if (!buffer.empty())
	{
		memcpy(s.get(), buffer.data(), buffer.size());
	}
	s[buffer.size()] = '\0';
	return true;
}

bool pathExists(const std::string& fullPath)
{
#if defined(__APPLE__)
	try
	{
		std::error_code errorCode;
		const std::filesystem::file_status status =
			std::filesystem::status(
				std::filesystem::u8path(fullPath), errorCode);
		return !errorCode && std::filesystem::exists(status);
	}
	catch (const std::exception&)
	{
		return false;
	}
#else
	SDL_PathInfo info;
	return SDL_GetPathInfo(fullPath.c_str(), &info) && info.type != SDL_PATHTYPE_NONE;
#endif
}

bool pathIsRegularFile(const std::string& fullPath)
{
#if defined(__APPLE__)
	try
	{
		std::error_code errorCode;
		const std::filesystem::file_status status =
			std::filesystem::status(
				std::filesystem::u8path(fullPath), errorCode);
		return !errorCode && std::filesystem::is_regular_file(status);
	}
	catch (const std::exception&)
	{
		return false;
	}
#else
	SDL_PathInfo info;
	return SDL_GetPathInfo(fullPath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
#endif
}

std::string resolveCaseInsensitiveExistingPath(const std::string& fullPath)
{
	if (fullPath.empty() || !isValidUtf8(fullPath) || pathExists(fullPath))
	{
		return isValidUtf8(fullPath) ? fullPath : "";
	}

	try
	{
		std::filesystem::path requestedPath = std::filesystem::u8path(fullPath);
		std::filesystem::path currentPath = requestedPath.root_path();
		if (currentPath.empty())
		{
			currentPath = std::filesystem::path(".");
		}

		std::error_code errorCode;
		for (const auto& component : requestedPath.relative_path())
		{
			if (component.empty() || component == ".")
			{
				continue;
			}
			if (component == "..")
			{
				currentPath /= component;
				continue;
			}

			std::filesystem::path exactPath = currentPath / component;
			if (std::filesystem::exists(exactPath, errorCode))
			{
				currentPath = exactPath;
				continue;
			}
			errorCode.clear();
			if (!std::filesystem::is_directory(currentPath, errorCode))
			{
				return "";
			}

			std::string requestedKey = toLowerAscii(component.u8string());
			std::filesystem::path matchedPath;
			std::filesystem::directory_iterator iterator(currentPath, errorCode);
			std::filesystem::directory_iterator end;
			while (!errorCode && iterator != end)
			{
				if (toLowerAscii(iterator->path().filename().u8string()) == requestedKey)
				{
					if (!matchedPath.empty())
					{
						return "";
					}
					matchedPath = iterator->path();
				}
				iterator.increment(errorCode);
			}
			if (errorCode || matchedPath.empty())
			{
				return "";
			}
			currentPath = matchedPath;
		}

		std::string resolvedPath = currentPath.lexically_normal().u8string();
		return pathExists(resolvedPath) ? resolvedPath : "";
	}
	catch (const std::exception&)
	{
		return "";
	}
}

bool hasAsciiSuffix(const std::string& value, const std::string& suffix)
{
	if (value.size() < suffix.size())
	{
		return false;
	}
	return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isImagePackagePath(const std::string& fullPath)
{
	std::string normalized = toLowerAscii(fullPath);
	convert::replaceAllString(normalized, "\\", "/");
	if (normalized.find("/asf/") == std::string::npos &&
		normalized.find("/mpc/") == std::string::npos)
	{
		return false;
	}
	return hasAsciiSuffix(normalized, ".asf") ||
		hasAsciiSuffix(normalized, ".mpc") ||
		hasAsciiSuffix(normalized, ".shd") ||
		hasAsciiSuffix(normalized, ".png");
}

std::string pathToUtf8String(const std::filesystem::path& path)
{
	try
	{
		return path.u8string();
	}
	catch (const std::exception&)
	{
		return "";
	}
}

enum class NoFollowPathKind
{
	Missing,
	Directory,
	RegularFile,
	Rejected
};

struct NoFollowPathInformation
{
	NoFollowPathKind kind = NoFollowPathKind::Rejected;
	PhysicalPathIdentity identity;
	std::uintmax_t linkCount = 0;
};

bool physicalPathIdentitiesEqual(
	const PhysicalPathIdentity& first,
	const PhysicalPathIdentity& second)
{
	return EditorRun::sameDirectoryIdentity(
		first, second);
}

#if defined(_WIN32)
bool populatePhysicalPathIdentity(
	HANDLE handle,
	const BY_HANDLE_FILE_INFORMATION& information,
	PhysicalPathIdentity& identity)
{
	identity = {};
	if (handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	FILE_ID_INFO fileIdInformation = {};
	if (GetFileInformationByHandleEx(
			handle,
			FileIdInfo,
			&fileIdInformation,
			sizeof(fileIdInformation)))
	{
		identity.deviceOrVolume =
			fileIdInformation.VolumeSerialNumber;
		std::memcpy(
			&identity.nodeLow,
			fileIdInformation.FileId.Identifier,
			sizeof(identity.nodeLow));
		std::memcpy(
			&identity.nodeHigh,
			fileIdInformation.FileId.Identifier +
				sizeof(identity.nodeLow),
			sizeof(identity.nodeHigh));
	}
	else
	{
		identity.deviceOrVolume =
			information.dwVolumeSerialNumber;
		identity.nodeHigh =
			information.nFileIndexHigh;
		identity.nodeLow =
			information.nFileIndexLow;
	}
	identity.linkCount =
		information.nNumberOfLinks;
	identity.valid = identity.linkCount > 0;
	return identity.valid;
}
#endif

NoFollowPathInformation inspectPathNoFollow(
	const std::filesystem::path& path)
{
	NoFollowPathInformation result;
#if defined(_WIN32)
	try
	{
		const HANDLE handle = CreateFileW(
			path.c_str(),
			FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			const DWORD error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND ||
				error == ERROR_PATH_NOT_FOUND)
			{
				result.kind = NoFollowPathKind::Missing;
			}
			return result;
		}

		BY_HANDLE_FILE_INFORMATION information = {};
		const bool queried =
			GetFileInformationByHandle(handle, &information) != 0;
		if (!queried ||
			(information.dwFileAttributes &
				FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			CloseHandle(handle);
			return result;
		}

		if (!populatePhysicalPathIdentity(
				handle, information,
				result.identity))
		{
			CloseHandle(handle);
			return result;
		}
		result.linkCount = information.nNumberOfLinks;
		CloseHandle(handle);
		result.kind =
			(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
			? NoFollowPathKind::Directory
			: NoFollowPathKind::RegularFile;
		return result;
	}
	catch (const std::exception&)
	{
		return result;
	}
#else
	struct stat information = {};
	if (lstat(path.c_str(), &information) != 0)
	{
		if (errno == ENOENT || errno == ENOTDIR)
		{
			result.kind = NoFollowPathKind::Missing;
		}
		return result;
	}
	if (!S_ISDIR(information.st_mode) &&
		!S_ISREG(information.st_mode))
	{
		return result;
	}
	result.identity.deviceOrVolume =
		static_cast<std::uint64_t>(information.st_dev);
	result.identity.nodeLow =
		static_cast<std::uint64_t>(information.st_ino);
	result.identity.linkCount =
		static_cast<std::uint64_t>(information.st_nlink);
	result.identity.valid = true;
	result.linkCount =
		static_cast<std::uintmax_t>(information.st_nlink);
	result.kind = S_ISDIR(information.st_mode)
		? NoFollowPathKind::Directory
		: NoFollowPathKind::RegularFile;
	return result;
#endif
}

bool captureDirectoryPhysicalIdentity(
	const std::filesystem::path& path,
	PhysicalPathIdentity& identity)
{
	identity = {};
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (information.kind != NoFollowPathKind::Directory)
	{
		return false;
	}
	identity = information.identity;
	return true;
}

#if defined(_WIN32)
using NativePathHandle = HANDLE;
const NativePathHandle InvalidNativePathHandle = INVALID_HANDLE_VALUE;
#else
using NativePathHandle = int;
constexpr NativePathHandle InvalidNativePathHandle = -1;
#endif

void closeNativePathHandle(NativePathHandle handle)
{
#if defined(_WIN32)
	if (handle != InvalidNativePathHandle)
	{
		CloseHandle(handle);
	}
#else
	if (handle != InvalidNativePathHandle)
	{
		close(handle);
	}
#endif
}

class NativeDirectoryHandle
{
public:
	NativeDirectoryHandle() = default;
	explicit NativeDirectoryHandle(NativePathHandle value)
		: handle(value)
	{
	}

	~NativeDirectoryHandle()
	{
		reset();
	}

	NativeDirectoryHandle(const NativeDirectoryHandle&) = delete;
	NativeDirectoryHandle& operator=(const NativeDirectoryHandle&) = delete;

	NativeDirectoryHandle(NativeDirectoryHandle&& other) noexcept
		: handle(other.release())
	{
	}

	NativeDirectoryHandle& operator=(NativeDirectoryHandle&& other) noexcept
	{
		if (this != &other)
		{
			reset(other.release());
		}
		return *this;
	}

	bool valid() const
	{
		return handle != InvalidNativePathHandle;
	}

	NativePathHandle get() const
	{
		return handle;
	}

	NativePathHandle release()
	{
		const NativePathHandle value = handle;
		handle = InvalidNativePathHandle;
		return value;
	}

	void reset(NativePathHandle value = InvalidNativePathHandle)
	{
		closeNativePathHandle(handle);
		handle = value;
	}

private:
	NativePathHandle handle = InvalidNativePathHandle;
};

class NativeFileHandle
{
public:
	NativeFileHandle() = default;
	explicit NativeFileHandle(NativePathHandle value)
		: handle(value)
	{
	}

	~NativeFileHandle()
	{
		reset();
	}

	NativeFileHandle(const NativeFileHandle&) = delete;
	NativeFileHandle& operator=(const NativeFileHandle&) = delete;

	NativeFileHandle(NativeFileHandle&& other) noexcept
		: handle(other.release())
	{
	}

	NativeFileHandle& operator=(NativeFileHandle&& other) noexcept
	{
		if (this != &other)
		{
			reset(other.release());
		}
		return *this;
	}

	bool valid() const
	{
		return handle != InvalidNativePathHandle;
	}

	NativePathHandle get() const
	{
		return handle;
	}

	NativePathHandle release()
	{
		const NativePathHandle value = handle;
		handle = InvalidNativePathHandle;
		return value;
	}

	void reset(NativePathHandle value = InvalidNativePathHandle)
	{
		closeNativePathHandle(handle);
		handle = value;
	}

private:
	NativePathHandle handle = InvalidNativePathHandle;
};

#if defined(_WIN32)
using NtCreateFileFunction = decltype(&NtCreateFile);
using NtSetInformationFileFunction = NTSTATUS (NTAPI*)(
	HANDLE,
	PIO_STATUS_BLOCK,
	PVOID,
	ULONG,
	FILE_INFORMATION_CLASS);

NtCreateFileFunction nativeNtCreateFile()
{
	static const NtCreateFileFunction function = []()
	{
		const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		return ntdll != nullptr
			? reinterpret_cast<NtCreateFileFunction>(
				GetProcAddress(ntdll, "NtCreateFile"))
			: nullptr;
	}();
	return function;
}

NtSetInformationFileFunction nativeNtSetInformationFile()
{
	static const NtSetInformationFileFunction function = []()
	{
		const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		return ntdll != nullptr
			? reinterpret_cast<NtSetInformationFileFunction>(
				GetProcAddress(
					ntdll, "NtSetInformationFile"))
			: nullptr;
	}();
	return function;
}

bool ntOpenRelative(
	HANDLE parent,
	const std::wstring& leaf,
	ACCESS_MASK desiredAccess,
	ULONG shareAccess,
	ULONG disposition,
	ULONG createOptions,
	ULONG fileAttributes,
	HANDLE& openedHandle,
	ULONG_PTR* informationValue = nullptr,
	NTSTATUS* statusValue = nullptr)
{
	openedHandle = INVALID_HANDLE_VALUE;
	if (statusValue != nullptr)
	{
		*statusValue = static_cast<NTSTATUS>(0);
	}
	const NtCreateFileFunction function = nativeNtCreateFile();
	if (function == nullptr || parent == INVALID_HANDLE_VALUE ||
		leaf.empty() ||
		leaf.size() >
			(static_cast<std::size_t>(
				(std::numeric_limits<USHORT>::max)()) /
				sizeof(wchar_t)))
	{
		return false;
	}

	UNICODE_STRING leafName = {};
	leafName.Buffer = const_cast<PWSTR>(leaf.data());
	leafName.Length = static_cast<USHORT>(
		leaf.size() * sizeof(wchar_t));
	leafName.MaximumLength = leafName.Length;
	OBJECT_ATTRIBUTES attributes = {};
	InitializeObjectAttributes(
		&attributes, &leafName, OBJ_CASE_INSENSITIVE,
		parent, nullptr);
	IO_STATUS_BLOCK ioStatus = {};
	const NTSTATUS status = function(
		&openedHandle,
		desiredAccess,
		&attributes,
		&ioStatus,
		nullptr,
		fileAttributes,
		shareAccess,
		disposition,
		createOptions,
		nullptr,
		0);
	if (statusValue != nullptr)
	{
		*statusValue = status;
	}
	if (status < 0 || openedHandle == INVALID_HANDLE_VALUE)
	{
		if (openedHandle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(openedHandle);
			openedHandle = INVALID_HANDLE_VALUE;
		}
		return false;
	}
	if (informationValue != nullptr)
	{
		*informationValue = ioStatus.Information;
	}
	return true;
}
#endif

bool nativeHandleInformation(
	NativePathHandle handle,
	bool expectDirectory,
	PhysicalPathIdentity& identity,
	std::uintmax_t* linkCount = nullptr)
{
	identity = {};
	if (linkCount != nullptr)
	{
		*linkCount = 0;
	}
#if defined(_WIN32)
	BY_HANDLE_FILE_INFORMATION information = {};
	if (handle == INVALID_HANDLE_VALUE ||
		GetFileInformationByHandle(handle, &information) == 0 ||
		GetFileType(handle) != FILE_TYPE_DISK ||
		(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
		((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) !=
			expectDirectory)
	{
		return false;
	}
	if (!populatePhysicalPathIdentity(
			handle, information, identity))
	{
		return false;
	}
	if (linkCount != nullptr)
	{
		*linkCount = information.nNumberOfLinks;
	}
	return true;
#else
	struct stat information = {};
	if (handle < 0 || fstat(handle, &information) != 0 ||
		(expectDirectory
			? !S_ISDIR(information.st_mode)
			: !S_ISREG(information.st_mode)))
	{
		return false;
	}
	identity.deviceOrVolume =
		static_cast<std::uint64_t>(information.st_dev);
	identity.nodeLow =
		static_cast<std::uint64_t>(information.st_ino);
	identity.linkCount =
		static_cast<std::uint64_t>(information.st_nlink);
	identity.valid = true;
	if (linkCount != nullptr)
	{
		*linkCount =
			static_cast<std::uintmax_t>(information.st_nlink);
	}
	return true;
#endif
}

bool nativeRegularFileSize(
	const NativeFileHandle& file,
	std::uint64_t& size)
{
	size = 0;
	if (!file.valid())
	{
		return false;
	}
#if defined(_WIN32)
	LARGE_INTEGER nativeSize = {};
	if (GetFileSizeEx(file.get(), &nativeSize) == 0 ||
		nativeSize.QuadPart < 0)
	{
		return false;
	}
	size = static_cast<std::uint64_t>(
		nativeSize.QuadPart);
#else
	struct stat information = {};
	if (fstat(file.get(), &information) != 0 ||
		information.st_size < 0)
	{
		return false;
	}
	size = static_cast<std::uint64_t>(
		information.st_size);
#endif
	return true;
}

bool openAbsoluteDirectoryNoFollow(
	const std::filesystem::path& path,
	const PhysicalPathIdentity& expectedIdentity,
	NativeDirectoryHandle& directory)
{
	directory.reset();
#if defined(_WIN32)
	const HANDLE handle = CreateFileW(
		path.c_str(),
		FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
			FILE_FLAG_OPEN_REPARSE_POINT,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	directory.reset(handle);
#else
	int flags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	const int descriptor = open(path.c_str(), flags);
	if (descriptor < 0)
	{
		return false;
	}
	directory.reset(descriptor);
#endif
	PhysicalPathIdentity openedIdentity;
	if (!nativeHandleInformation(
			directory.get(), true, openedIdentity) ||
		!physicalPathIdentitiesEqual(
			openedIdentity, expectedIdentity))
	{
		directory.reset();
		return false;
	}
	return true;
}

std::vector<std::string> splitRelativePathComponents(
	const std::string& relativePath)
{
	std::vector<std::string> components;
	std::size_t start = 0;
	while (start < relativePath.size())
	{
		const std::size_t separator =
			relativePath.find('/', start);
		const std::string component =
			separator == std::string::npos
			? relativePath.substr(start)
			: relativePath.substr(start, separator - start);
		if (component.empty() || component == "." ||
			component == "..")
		{
			return {};
		}
		components.push_back(component);
		if (separator == std::string::npos)
		{
			break;
		}
		start = separator + 1;
	}
	return components;
}

bool openChildDirectoryNoFollow(
	const NativeDirectoryHandle& parent,
	const std::string& childName,
	bool createIfMissing,
	NativeDirectoryHandle& child,
	bool* created = nullptr)
{
	child.reset();
	if (created != nullptr)
	{
		*created = false;
	}
	if (!parent.valid() || childName.empty() ||
		childName == "." || childName == "..")
	{
		return false;
	}
#if defined(_WIN32)
	HANDLE handle = INVALID_HANDLE_VALUE;
	ULONG_PTR information = 0;
	if (!ntOpenRelative(
			parent.get(),
			std::filesystem::u8path(childName).wstring(),
			FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES |
				SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			createIfMissing ? FILE_OPEN_IF : FILE_OPEN,
			FILE_DIRECTORY_FILE |
				FILE_OPEN_REPARSE_POINT |
				FILE_SYNCHRONOUS_IO_NONALERT,
			FILE_ATTRIBUTE_DIRECTORY,
			handle,
			&information))
	{
		return false;
	}
	child.reset(handle);
	if (created != nullptr)
	{
		*created = information == FILE_CREATED;
	}
#else
	int flags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	int descriptor =
		openat(parent.get(), childName.c_str(), flags);
	if (descriptor < 0 && createIfMissing && errno == ENOENT)
	{
		if (mkdirat(parent.get(), childName.c_str(), 0700) != 0)
		{
			return false;
		}
		if (created != nullptr)
		{
			*created = true;
		}
		descriptor =
			openat(parent.get(), childName.c_str(), flags);
	}
	if (descriptor < 0)
	{
		return false;
	}
	child.reset(descriptor);
#endif
	PhysicalPathIdentity identity;
	if (!nativeHandleInformation(child.get(), true, identity))
	{
		child.reset();
		return false;
	}
	return true;
}

bool openChildFileNoFollow(
	const NativeDirectoryHandle& parent,
	const std::string& childName,
	bool write,
	bool append,
	bool createIfMissing,
	bool requestDelete,
	NativeFileHandle& file,
	bool* created = nullptr)
{
	file.reset();
	if (created != nullptr)
	{
		*created = false;
	}
	if (!parent.valid() || childName.empty() ||
		childName == "." || childName == "..")
	{
		return false;
	}
#if defined(_WIN32)
	ACCESS_MASK desiredAccess =
		FILE_READ_ATTRIBUTES | SYNCHRONIZE;
	if (write)
	{
		desiredAccess |= append
			? FILE_APPEND_DATA
			: FILE_WRITE_DATA;
	}
	else
	{
		desiredAccess |= FILE_READ_DATA;
	}
	if (requestDelete)
	{
		desiredAccess |= DELETE;
	}
	HANDLE handle = INVALID_HANDLE_VALUE;
	ULONG_PTR information = 0;
	if (!ntOpenRelative(
			parent.get(),
			std::filesystem::u8path(childName).wstring(),
			desiredAccess,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			createIfMissing ? FILE_OPEN_IF : FILE_OPEN,
			FILE_NON_DIRECTORY_FILE |
				FILE_OPEN_REPARSE_POINT |
				FILE_SYNCHRONOUS_IO_NONALERT,
			FILE_ATTRIBUTE_NORMAL,
			handle,
			&information))
	{
		return false;
	}
	file.reset(handle);
	if (created != nullptr)
	{
		*created = information == FILE_CREATED;
	}
#else
	int flags = write ? O_WRONLY : O_RDONLY;
	if (append)
	{
		flags |= O_APPEND;
	}
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	int descriptor = openat(
		parent.get(), childName.c_str(), flags);
	if (descriptor < 0 && createIfMissing && errno == ENOENT)
	{
		descriptor = openat(
			parent.get(), childName.c_str(),
			flags | O_CREAT | O_EXCL, 0600);
		if (descriptor >= 0 && created != nullptr)
		{
			*created = true;
		}
	}
	if (descriptor < 0)
	{
		return false;
	}
	file.reset(descriptor);
#endif
	PhysicalPathIdentity identity;
	std::uintmax_t linkCount = 0;
	if (!nativeHandleInformation(
			file.get(), false, identity, &linkCount) ||
		linkCount != 1)
	{
		file.reset();
		return false;
	}
	return true;
}

bool createChildFileExclusiveNoFollow(
	const NativeDirectoryHandle& parent,
	const std::string& childName,
	NativeFileHandle& file)
{
	file.reset();
	if (!parent.valid() || childName.empty() ||
		childName == "." || childName == "..")
	{
		return false;
	}
#if defined(_WIN32)
	HANDLE handle = INVALID_HANDLE_VALUE;
	if (!ntOpenRelative(
			parent.get(),
			std::filesystem::u8path(childName).wstring(),
			FILE_APPEND_DATA | FILE_READ_ATTRIBUTES |
				SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			FILE_CREATE,
			FILE_NON_DIRECTORY_FILE |
				FILE_OPEN_REPARSE_POINT |
				FILE_SYNCHRONOUS_IO_NONALERT,
			FILE_ATTRIBUTE_NORMAL,
			handle))
	{
		return false;
	}
	file.reset(handle);
#else
	int flags =
		O_WRONLY | O_APPEND | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
	const int descriptor = openat(
		parent.get(), childName.c_str(), flags, 0600);
	if (descriptor < 0)
	{
		return false;
	}
	file.reset(descriptor);
#endif
	PhysicalPathIdentity identity;
	std::uintmax_t linkCount = 0;
	if (!nativeHandleInformation(
			file.get(), false, identity, &linkCount) ||
		linkCount != 1)
	{
		file.reset();
		return false;
	}
	return true;
}

bool duplicateNativeDirectoryHandle(
	NativePathHandle source,
	NativeDirectoryHandle& duplicate)
{
	duplicate.reset();
#if defined(_WIN32)
	HANDLE value = INVALID_HANDLE_VALUE;
	if (source == INVALID_HANDLE_VALUE ||
		DuplicateHandle(
			GetCurrentProcess(), source,
			GetCurrentProcess(), &value,
			0, FALSE, DUPLICATE_SAME_ACCESS) == 0)
	{
		return false;
	}
	duplicate.reset(value);
#else
	const int value = dup(source);
	if (value < 0)
	{
		return false;
	}
	duplicate.reset(value);
#endif
	PhysicalPathIdentity identity;
	if (!nativeHandleInformation(
			duplicate.get(), true, identity))
	{
		duplicate.reset();
		return false;
	}
	return true;
}

bool editorRunRouteIsCurrent(const RoutedResourcePath& route)
{
	return !route.anchored ||
		(route.rootIdentity.valid &&
		 editorRunRoutingSnapshotIsCurrent(
			route.routingState, route.routingGeneration));
}

bool openAnchoredRouteRoot(
	const RoutedResourcePath& route,
	File::EditorRunFileOperationPhase phase,
	NativeDirectoryHandle& root)
{
	if (!route.anchored || route.root.empty() ||
		!editorRunRouteIsCurrent(route))
	{
		return false;
	}
	invokeEditorRunFileOperationTestHook(phase);
	if (!editorRunRouteIsCurrent(route) ||
		!openAbsoluteDirectoryNoFollow(
			installedIdentityPath(route.root),
			route.rootIdentity, root) ||
		!editorRunRouteIsCurrent(route))
	{
		root.reset();
		return false;
	}
	return true;
}

std::vector<std::string> listNativeDirectoryNames(
	const NativeDirectoryHandle& directory,
	bool* succeeded = nullptr,
	std::size_t maximumEntryCount = 0,
	bool* entryCountLimitExceeded = nullptr)
{
	std::vector<std::string> names;
	if (succeeded != nullptr)
	{
		*succeeded = false;
	}
	if (entryCountLimitExceeded != nullptr)
	{
		*entryCountLimitExceeded = false;
	}
	if (!directory.valid())
	{
		return names;
	}
#if defined(_WIN32)
	const DWORD required = GetFinalPathNameByHandleW(
		directory.get(), nullptr, 0,
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (required == 0)
	{
		return names;
	}
	std::vector<wchar_t> buffer(
		static_cast<std::size_t>(required) + 1);
	const DWORD written = GetFinalPathNameByHandleW(
		directory.get(), buffer.data(),
		static_cast<DWORD>(buffer.size()),
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (written == 0 ||
		static_cast<std::size_t>(written) >= buffer.size())
	{
		return names;
	}
	try
	{
		const std::filesystem::path directoryPath(
			std::wstring(buffer.data(), written));
		std::error_code errorCode;
		std::filesystem::directory_iterator iterator(
			directoryPath,
			std::filesystem::directory_options::none,
			errorCode);
		const std::filesystem::directory_iterator end;
		for (; !errorCode && iterator != end;
			iterator.increment(errorCode))
		{
			const std::string name =
				pathToUtf8String(iterator->path().filename());
			if (!name.empty() && name != "." && name != "..")
			{
				if (maximumEntryCount > 0 &&
					names.size() >= maximumEntryCount)
				{
					if (entryCountLimitExceeded != nullptr)
					{
						*entryCountLimitExceeded = true;
					}
					break;
				}
				names.push_back(name);
			}
		}
		if (errorCode)
		{
			names.clear();
			return names;
		}
	}
	catch (const std::exception&)
	{
		names.clear();
		return names;
	}
#else
	const int duplicate = dup(directory.get());
	if (duplicate < 0)
	{
		return names;
	}
	DIR* stream = fdopendir(duplicate);
	if (stream == nullptr)
	{
		close(duplicate);
		return names;
	}
	rewinddir(stream);
	errno = 0;
	for (dirent* entry = readdir(stream);
		entry != nullptr; entry = readdir(stream))
	{
		const std::string name = entry->d_name;
		if (!name.empty() && name != "." && name != "..")
		{
			if (maximumEntryCount > 0 &&
				names.size() >= maximumEntryCount)
			{
				if (entryCountLimitExceeded != nullptr)
				{
					*entryCountLimitExceeded = true;
				}
				break;
			}
			names.push_back(name);
		}
	}
	if (errno != 0)
	{
		names.clear();
		closedir(stream);
		return names;
	}
	closedir(stream);
#endif
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	if (succeeded != nullptr)
	{
		*succeeded = true;
	}
	return names;
}

std::string uniqueCaseInsensitiveNativeChild(
	const NativeDirectoryHandle& directory,
	const std::string& requestedName)
{
	const std::string requestedKey =
		toLowerAscii(requestedName);
	std::string match;
	for (const std::string& name :
		listNativeDirectoryNames(directory))
	{
		if (toLowerAscii(name) != requestedKey)
		{
			continue;
		}
		if (!match.empty())
		{
			return "";
		}
		match = name;
	}
	return match;
}

bool openRelativeDirectoryNoFollow(
	NativeDirectoryHandle root,
	const std::vector<std::string>& components,
	std::size_t componentCount,
	bool createIfMissing,
	bool allowCaseInsensitiveFallback,
	NativeDirectoryHandle& directory,
	std::vector<std::string>* resolvedComponents = nullptr)
{
	directory.reset();
	if (!root.valid() || componentCount > components.size())
	{
		return false;
	}
	if (resolvedComponents != nullptr)
	{
		resolvedComponents->clear();
	}
	NativeDirectoryHandle current = std::move(root);
	for (std::size_t index = 0; index < componentCount; ++index)
	{
		std::string actualName = components[index];
		NativeDirectoryHandle next;
		if (!openChildDirectoryNoFollow(
				current, actualName, createIfMissing, next))
		{
			if (!allowCaseInsensitiveFallback ||
				createIfMissing)
			{
				return false;
			}
			actualName = uniqueCaseInsensitiveNativeChild(
				current, components[index]);
			if (actualName.empty() ||
				!openChildDirectoryNoFollow(
					current, actualName, false, next))
			{
				return false;
			}
		}
		if (resolvedComponents != nullptr)
		{
			resolvedComponents->push_back(actualName);
		}
		current = std::move(next);
	}
	directory = std::move(current);
	return true;
}

std::string joinRelativeComponents(
	const std::vector<std::string>& components)
{
	std::string result;
	for (const std::string& component : components)
	{
		if (!result.empty())
		{
			result += "/";
		}
		result += component;
	}
	return result;
}

bool openAnchoredFileForRead(
	const RoutedResourcePath& route,
	const std::string& relativePath,
	NativeFileHandle& file,
	std::string* resolvedRelativePath = nullptr)
{
	file.reset();
	if (resolvedRelativePath != nullptr)
	{
		resolvedRelativePath->clear();
	}
	const std::vector<std::string> components =
		splitRelativePathComponents(relativePath);
	if (components.empty())
	{
		return false;
	}
	NativeDirectoryHandle root;
	if (!openAnchoredRouteRoot(
			route,
			File::EditorRunFileOperationPhase::BeforeReadRootOpen,
			root))
	{
		return false;
	}
	std::vector<std::string> resolvedComponents;
	NativeDirectoryHandle parent;
	if (!openRelativeDirectoryNoFollow(
			std::move(root), components,
			components.size() - 1,
			false, true, parent,
			&resolvedComponents))
	{
		return false;
	}

	std::string actualLeaf = components.back();
	if (!openChildFileNoFollow(
			parent, actualLeaf, false, false,
			false, false, file))
	{
		actualLeaf = uniqueCaseInsensitiveNativeChild(
			parent, components.back());
		if (actualLeaf.empty() ||
			!openChildFileNoFollow(
				parent, actualLeaf, false, false,
				false, false, file))
		{
			return false;
		}
	}
	resolvedComponents.push_back(actualLeaf);
	if (!editorRunRouteIsCurrent(route))
	{
		file.reset();
		return false;
	}
	if (resolvedRelativePath != nullptr)
	{
		*resolvedRelativePath =
			joinRelativeComponents(resolvedComponents);
	}
	return true;
}

bool readNativeFileBounded(
	const NativeFileHandle& file,
	std::unique_ptr<char[]>& data,
	int& length,
	std::size_t maximumBytes)
{
	data.reset();
	length = 0;
	if (!file.valid())
	{
		return false;
	}
	maximumBytes = (std::min)(
		maximumBytes,
		static_cast<std::size_t>(INT_MAX) - 1);
	std::uintmax_t fileSize = 0;
#if defined(_WIN32)
	LARGE_INTEGER size = {};
	LARGE_INTEGER offset = {};
	if (GetFileSizeEx(file.get(), &size) == 0 ||
		size.QuadPart < 0 ||
		SetFilePointerEx(
			file.get(), offset, nullptr, FILE_BEGIN) == 0)
	{
		return false;
	}
	fileSize = static_cast<std::uintmax_t>(size.QuadPart);
#else
	struct stat information = {};
	if (fstat(file.get(), &information) != 0 ||
		information.st_size < 0 ||
		lseek(file.get(), 0, SEEK_SET) < 0)
	{
		return false;
	}
	fileSize =
		static_cast<std::uintmax_t>(information.st_size);
#endif
	if (fileSize > maximumBytes ||
		fileSize >
			(static_cast<std::uintmax_t>(
				(std::numeric_limits<std::size_t>::max)()) - 1))
	{
		return false;
	}

	const std::size_t requestedSize =
		static_cast<std::size_t>(fileSize);
	std::unique_ptr<char[]> result;
	try
	{
		result = std::make_unique<char[]>(requestedSize + 1);
	}
	catch (const std::exception&)
	{
		return false;
	}
	std::size_t totalRead = 0;
	while (totalRead < requestedSize)
	{
#if defined(_WIN32)
		const DWORD requested = static_cast<DWORD>(
			(std::min)(
				requestedSize - totalRead,
				static_cast<std::size_t>(
					(std::numeric_limits<DWORD>::max)())));
		DWORD currentRead = 0;
		if (ReadFile(
				file.get(), result.get() + totalRead,
				requested, &currentRead, nullptr) == 0 ||
			currentRead == 0)
		{
			return false;
		}
		totalRead += currentRead;
#else
		const ssize_t currentRead = read(
			file.get(), result.get() + totalRead,
			requestedSize - totalRead);
		if (currentRead < 0 && errno == EINTR)
		{
			continue;
		}
		if (currentRead <= 0)
		{
			return false;
		}
		totalRead += static_cast<std::size_t>(currentRead);
#endif
	}
	result[totalRead] = '\0';
	data = std::move(result);
	length = static_cast<int>(totalRead);
	return true;
}

bool hostPathComponentEquals(const std::filesystem::path& first,
	const std::filesystem::path& second)
{
#if defined(_WIN32)
	const std::wstring firstValue = first.native();
	const std::wstring secondValue = second.native();
	if (firstValue.size() >
			static_cast<std::size_t>(
				(std::numeric_limits<int>::max)()) ||
		secondValue.size() >
			static_cast<std::size_t>(
				(std::numeric_limits<int>::max)()))
	{
		return false;
	}
	return CompareStringOrdinal(
			firstValue.c_str(),
			static_cast<int>(firstValue.size()),
			secondValue.c_str(),
			static_cast<int>(secondValue.size()),
			TRUE) == CSTR_EQUAL;
#else
	return first == second;
#endif
}

bool hostPathsEqual(const std::filesystem::path& first,
	const std::filesystem::path& second)
{
	auto firstIterator = first.begin();
	auto secondIterator = second.begin();
	for (; firstIterator != first.end() && secondIterator != second.end();
		++firstIterator, ++secondIterator)
	{
		if (!hostPathComponentEquals(*firstIterator, *secondIterator))
		{
			return false;
		}
	}
	return firstIterator == first.end() && secondIterator == second.end();
}

bool hostPathContains(const std::filesystem::path& root,
	const std::filesystem::path& candidate)
{
	auto rootIterator = root.begin();
	auto candidateIterator = candidate.begin();
	for (; rootIterator != root.end(); ++rootIterator, ++candidateIterator)
	{
		if (candidateIterator == candidate.end() ||
			!hostPathComponentEquals(*rootIterator, *candidateIterator))
		{
			return false;
		}
	}
	return true;
}

bool normalizeExistingDirectoryHostPath(const std::string& value,
	std::string& normalizedValue,
	std::filesystem::path& canonicalPath)
{
	normalizedValue.clear();
	canonicalPath.clear();
	if (value.empty() || value.find('\0') != std::string::npos ||
		!isValidUtf8(value))
	{
		return false;
	}

	try
	{
		const std::filesystem::path inputPath = std::filesystem::u8path(value);
		if (!inputPath.is_absolute())
		{
			return false;
		}
		const std::filesystem::path lexicalPath = inputPath.lexically_normal();
		std::error_code errorCode;
		if (!std::filesystem::is_directory(lexicalPath, errorCode) || errorCode)
		{
			return false;
		}
		const std::filesystem::path resolvedPath =
			std::filesystem::canonical(lexicalPath, errorCode);
		std::filesystem::path comparableLexicalPath = lexicalPath;
		while (comparableLexicalPath !=
				comparableLexicalPath.root_path() &&
			comparableLexicalPath.filename().empty())
		{
			comparableLexicalPath =
				comparableLexicalPath.parent_path();
		}
		if (errorCode || resolvedPath.empty() ||
			!hostPathsEqual(
				comparableLexicalPath, resolvedPath))
		{
			// A different canonical path means a symlink, junction, or other alias
			// component is present in the output root.
			return false;
		}
		normalizedValue = normalizeRoot(pathToUtf8String(resolvedPath));
		canonicalPath = resolvedPath;
		return !normalizedValue.empty();
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool normalizeEditorRunOutputPath(const std::string& value,
	const std::filesystem::path& diagnosticsRoot,
	std::string& normalizedValue,
	std::string& normalizedParentValue)
{
	normalizedValue.clear();
	normalizedParentValue.clear();
	if (value.empty() || value.find('\0') != std::string::npos ||
		!isValidUtf8(value))
	{
		return false;
	}

	try
	{
		const std::filesystem::path inputPath = std::filesystem::u8path(value);
		if (!inputPath.is_absolute())
		{
			return false;
		}
		const std::filesystem::path lexicalPath = inputPath.lexically_normal();
		const std::filesystem::path fileName = lexicalPath.filename();
		if (fileName.empty() || fileName == "." || fileName == "..")
		{
			return false;
		}

		std::string normalizedParent;
		std::filesystem::path canonicalParent;
		if (!normalizeExistingDirectoryHostPath(
			pathToUtf8String(lexicalPath.parent_path()),
			normalizedParent, canonicalParent))
		{
			return false;
		}
		const std::filesystem::path resolvedPath =
			(canonicalParent / fileName).lexically_normal();
		if (!hostPathContains(diagnosticsRoot, resolvedPath) ||
			hostPathsEqual(diagnosticsRoot, resolvedPath))
		{
			return false;
		}

		std::error_code errorCode;
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(lexicalPath, errorCode);
		if (!errorCode && status.type() != std::filesystem::file_type::not_found)
		{
			// The held editor-run logger creates this leaf exclusively on first
			// use. Reusing an existing file cannot prove it is not a hard-link.
			return false;
		}
		if (errorCode != std::errc::no_such_file_or_directory && errorCode)
		{
			return false;
		}

		normalizedValue = pathToUtf8String(resolvedPath);
		normalizedParentValue = normalizeRoot(pathToUtf8String(canonicalParent));
		return !normalizedValue.empty() && !normalizedParentValue.empty();
	}
	catch (const std::exception&)
	{
		return false;
	}
}

std::filesystem::path installedIdentityPath(const std::string& identity)
{
	std::filesystem::path path = std::filesystem::u8path(identity);
	while (path != path.root_path() && path.filename().empty())
	{
		path = path.parent_path();
	}
	return path.lexically_normal();
}

bool currentDirectoryMatchesInstalledIdentity(
	const std::string& identityPath,
	const PhysicalPathIdentity& installedIdentity)
{
	if (identityPath.empty() || !isValidUtf8(identityPath) ||
		!installedIdentity.valid)
	{
		return false;
	}
	try
	{
		const std::filesystem::path installedPath =
			installedIdentityPath(identityPath);
		const NoFollowPathInformation information =
			inspectPathNoFollow(installedPath);
		if (information.kind != NoFollowPathKind::Directory ||
			!physicalPathIdentitiesEqual(
				information.identity, installedIdentity))
		{
			return false;
		}
		std::error_code errorCode;
		const std::filesystem::path currentPath =
			std::filesystem::canonical(installedPath, errorCode);
		return !errorCode && !currentPath.empty() &&
			hostPathsEqual(installedPath, currentPath);
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool currentEditorRunOutputPathMatchesInstalledIdentity(
	const InstalledEditorRunFileLayout& layout,
	const std::string& outputPathValue,
	const std::string& parentPathValue,
	const PhysicalPathIdentity& parentIdentity)
{
	if (!currentDirectoryMatchesInstalledIdentity(
			parentPathValue, parentIdentity))
	{
		return false;
	}
	try
	{
		const std::filesystem::path outputPath =
			std::filesystem::u8path(outputPathValue).lexically_normal();
		const std::filesystem::path diagnosticsPath =
			installedIdentityPath(layout.diagnosticsRoot);
		if (!hostPathContains(diagnosticsPath, outputPath) ||
			hostPathsEqual(diagnosticsPath, outputPath))
		{
			return false;
		}

		std::error_code errorCode;
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(outputPath, errorCode);
		if (errorCode == std::errc::no_such_file_or_directory ||
			(!errorCode && status.type() == std::filesystem::file_type::not_found))
		{
			return true;
		}
		if (errorCode || std::filesystem::is_symlink(status) ||
			!std::filesystem::is_regular_file(status))
		{
			return false;
		}
		const std::uintmax_t linkCount =
			std::filesystem::hard_link_count(outputPath, errorCode);
		if (errorCode || linkCount != 1)
		{
			return false;
		}
		const std::filesystem::path currentPath =
			std::filesystem::canonical(outputPath, errorCode);
		return !errorCode && !currentPath.empty() &&
			hostPathsEqual(outputPath, currentPath);
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool installedEditorRunFileLayoutIsCurrent(
	const InstalledEditorRunFileLayout& layout)
{
	return currentDirectoryMatchesInstalledIdentity(
			layout.overlayRoot, layout.overlayIdentity) &&
		currentDirectoryMatchesInstalledIdentity(
			layout.isolatedSaveRoot, layout.isolatedSaveIdentity) &&
		currentDirectoryMatchesInstalledIdentity(
			layout.applicationStateRoot, layout.applicationStateIdentity) &&
		currentDirectoryMatchesInstalledIdentity(
			layout.diagnosticsRoot, layout.diagnosticsIdentity) &&
		currentEditorRunOutputPathMatchesInstalledIdentity(
			layout, layout.diagnosticsPath,
			layout.diagnosticsParentPath,
			layout.diagnosticsParentIdentity) &&
		currentEditorRunOutputPathMatchesInstalledIdentity(
			layout, layout.logPath, layout.logParentPath,
			layout.logParentIdentity) &&
		currentEditorRunOutputPathMatchesInstalledIdentity(
			layout, layout.runtimeTracePath,
			layout.runtimeTraceParentPath,
			layout.runtimeTraceParentIdentity);
}

bool validateEditorRunFileLayout(
	const File::EditorRunFileLayout& layout,
	const File::EditorRunFileLayoutIdentityProof* proof,
	InstalledEditorRunFileLayout& normalizedLayout)
{
#if defined(__ANDROID__) || defined(__MOBILE__) || \
	(defined(__APPLE__) && TARGET_OS_IPHONE)
	(void)layout;
	(void)proof;
	(void)normalizedLayout;
	return false;
#else
	std::filesystem::path overlayPath;
	std::filesystem::path savePath;
	std::filesystem::path applicationStatePath;
	std::filesystem::path diagnosticsPath;
	if (!normalizeExistingDirectoryHostPath(layout.overlayRoot,
			normalizedLayout.overlayRoot, overlayPath) ||
		!normalizeExistingDirectoryHostPath(layout.isolatedSaveRoot,
			normalizedLayout.isolatedSaveRoot, savePath) ||
		!normalizeExistingDirectoryHostPath(layout.applicationStateRoot,
			normalizedLayout.applicationStateRoot, applicationStatePath) ||
		!normalizeExistingDirectoryHostPath(layout.diagnosticsRoot,
			normalizedLayout.diagnosticsRoot, diagnosticsPath))
	{
		return false;
	}

	const std::vector<std::filesystem::path> roots = {
		overlayPath,
		savePath,
		applicationStatePath,
		diagnosticsPath
	};
	for (std::size_t firstIndex = 0; firstIndex < roots.size(); firstIndex++)
	{
		for (std::size_t secondIndex = firstIndex + 1;
			secondIndex < roots.size(); secondIndex++)
		{
			if (hostPathContains(roots[firstIndex], roots[secondIndex]) ||
				hostPathContains(roots[secondIndex], roots[firstIndex]))
			{
				return false;
			}
		}
	}

	if (!captureDirectoryPhysicalIdentity(
			overlayPath, normalizedLayout.overlayIdentity))
	{
		return false;
	}
	invokeEditorRunFileOperationTestHook(
		File::EditorRunFileOperationPhase::
			AfterLayoutOverlayIdentityCapture);
	if (!captureDirectoryPhysicalIdentity(
			savePath, normalizedLayout.isolatedSaveIdentity) ||
		!captureDirectoryPhysicalIdentity(
			applicationStatePath,
			normalizedLayout.applicationStateIdentity) ||
		!captureDirectoryPhysicalIdentity(
			diagnosticsPath, normalizedLayout.diagnosticsIdentity))
	{
		return false;
	}
	const std::vector<PhysicalPathIdentity> outputIdentities = {
		normalizedLayout.overlayIdentity,
		normalizedLayout.isolatedSaveIdentity,
		normalizedLayout.applicationStateIdentity,
		normalizedLayout.diagnosticsIdentity
	};
	if (proof != nullptr)
	{
		for (std::size_t index = 0;
			index < outputIdentities.size(); ++index)
		{
			if (!EditorRun::sameDirectoryGeneration(
					outputIdentities[index],
					proof->outputRoots[index]))
			{
				return false;
			}
		}
	}
	for (std::size_t firstIndex = 0;
		firstIndex < outputIdentities.size(); ++firstIndex)
	{
		for (std::size_t secondIndex = firstIndex + 1;
			secondIndex < outputIdentities.size(); ++secondIndex)
		{
			if (physicalPathIdentitiesEqual(
					outputIdentities[firstIndex],
					outputIdentities[secondIndex]))
			{
				return false;
			}
		}
	}

	normalizedLayout.resourceRouting =
		currentEditorRunResourceRouting();
	std::vector<std::string*> formalRoots =
		mutableFormalRootValues(
			normalizedLayout.resourceRouting);
	for (std::string* formalRoot : formalRoots)
	{
		if (formalRoot == nullptr)
		{
			return false;
		}
		std::string& formalRootValue = *formalRoot;
		if (formalRootValue.empty())
		{
			continue;
		}
		try
		{
			if (formalRootValue.find('\0') !=
					std::string::npos ||
				!isValidUtf8(formalRootValue))
			{
				return false;
			}
			const std::filesystem::path formalPath =
				std::filesystem::u8path(
					formalRootValue).
						lexically_normal();
			if (formalPath.empty() ||
				!formalPath.is_absolute())
			{
				return false;
			}
			formalRootValue = normalizeRoot(
				pathToUtf8String(formalPath));
			if (formalRootValue.empty())
			{
				return false;
			}
			const std::filesystem::path normalizedFormalPath =
				installedIdentityPath(formalRootValue);
			for (const std::filesystem::path& outputRoot :
				roots)
			{
				if (hostPathContains(
						normalizedFormalPath,
						outputRoot) ||
					hostPathContains(
						outputRoot,
						normalizedFormalPath))
				{
					return false;
				}
			}
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	if (!normalizeEditorRunOutputPath(
			layout.diagnosticsPath, diagnosticsPath,
			normalizedLayout.diagnosticsPath,
			normalizedLayout.diagnosticsParentPath) ||
		!normalizeEditorRunOutputPath(
			layout.logPath, diagnosticsPath, normalizedLayout.logPath,
			normalizedLayout.logParentPath) ||
		!normalizeEditorRunOutputPath(
			layout.runtimeTracePath,
			diagnosticsPath,
			normalizedLayout.runtimeTracePath,
			normalizedLayout.runtimeTraceParentPath) ||
		hostPathsEqual(
			std::filesystem::u8path(normalizedLayout.diagnosticsPath),
			std::filesystem::u8path(normalizedLayout.logPath)) ||
		hostPathsEqual(
			std::filesystem::u8path(normalizedLayout.diagnosticsPath),
			std::filesystem::u8path(normalizedLayout.runtimeTracePath)) ||
		hostPathsEqual(
			std::filesystem::u8path(normalizedLayout.logPath),
			std::filesystem::u8path(normalizedLayout.runtimeTracePath)))
	{
		return false;
	}

	return captureDirectoryPhysicalIdentity(
			installedIdentityPath(
				normalizedLayout.diagnosticsParentPath),
			normalizedLayout.diagnosticsParentIdentity) &&
		captureDirectoryPhysicalIdentity(
			installedIdentityPath(normalizedLayout.logParentPath),
			normalizedLayout.logParentIdentity) &&
		captureDirectoryPhysicalIdentity(
			installedIdentityPath(
				normalizedLayout.runtimeTraceParentPath),
			normalizedLayout.runtimeTraceParentIdentity);
#endif
}

enum class EditorRunExactOutputKind
{
	Diagnostics,
	Log,
	RuntimeTrace
};

bool selectEditorRunExactOutput(
	const InstalledEditorRunFileLayout& layout,
	EditorRunExactOutputKind kind,
	const std::string*& outputPath,
	const std::string*& parentPath,
	const PhysicalPathIdentity*& parentIdentity)
{
	switch (kind)
	{
	case EditorRunExactOutputKind::Diagnostics:
		outputPath = &layout.diagnosticsPath;
		parentPath = &layout.diagnosticsParentPath;
		parentIdentity = &layout.diagnosticsParentIdentity;
		return true;
	case EditorRunExactOutputKind::Log:
		outputPath = &layout.logPath;
		parentPath = &layout.logParentPath;
		parentIdentity = &layout.logParentIdentity;
		return true;
	case EditorRunExactOutputKind::RuntimeTrace:
		outputPath = &layout.runtimeTracePath;
		parentPath = &layout.runtimeTraceParentPath;
		parentIdentity =
			&layout.runtimeTraceParentIdentity;
		return true;
	}
	return false;
}

void closeEditorRunExactOutputParent(std::intptr_t parentToken)
{
	if (parentToken == -1)
	{
		return;
	}
#if defined(_WIN32)
	closeNativePathHandle(
		reinterpret_cast<HANDLE>(parentToken));
#else
	closeNativePathHandle(
		static_cast<int>(parentToken));
#endif
}

bool editorRunExactOutputHandleIsCurrent(
	std::FILE* file,
	std::intptr_t parentToken,
	const std::string& outputPath,
	uint64_t generation,
	EditorRunExactOutputKind kind)
{
	if (file == nullptr || parentToken == -1)
	{
		return false;
	}
	InstalledEditorRunFileLayout layout;
	uint64_t currentGeneration = 0;
	if (getInstalledEditorRunFileLayout(
			layout, &currentGeneration) !=
			File::EditorRunFileLayoutState::Valid ||
		currentGeneration != generation)
	{
		return false;
	}
	const std::string* installedOutputPath = nullptr;
	const std::string* installedParentPath = nullptr;
	const PhysicalPathIdentity* installedParentIdentity = nullptr;
	if (!selectEditorRunExactOutput(
			layout, kind, installedOutputPath,
			installedParentPath, installedParentIdentity) ||
		installedOutputPath == nullptr ||
		installedParentPath == nullptr ||
		installedParentIdentity == nullptr ||
		*installedOutputPath != outputPath)
	{
		return false;
	}
#if defined(_WIN32)
	const NativePathHandle parentHandle =
		reinterpret_cast<HANDLE>(parentToken);
	const int descriptor = _fileno(file);
	const intptr_t fileToken =
		descriptor >= 0
		? _get_osfhandle(descriptor)
		: -1;
	if (fileToken == -1)
	{
		return false;
	}
	const NativePathHandle fileHandle =
		reinterpret_cast<HANDLE>(fileToken);
#else
	const NativePathHandle parentHandle =
		static_cast<int>(parentToken);
	const NativePathHandle fileHandle = fileno(file);
	if (fileHandle < 0)
	{
		return false;
	}
#endif
	PhysicalPathIdentity parentIdentity;
	PhysicalPathIdentity heldIdentity;
	std::uintmax_t heldLinkCount = 0;
	if (!nativeHandleInformation(
			parentHandle, true, parentIdentity) ||
		!physicalPathIdentitiesEqual(
			parentIdentity, *installedParentIdentity) ||
		!nativeHandleInformation(
			fileHandle, false, heldIdentity,
			&heldLinkCount) ||
		heldLinkCount != 1)
	{
		return false;
	}

	NativeDirectoryHandle parentDuplicate;
	if (!duplicateNativeDirectoryHandle(
			parentHandle, parentDuplicate))
	{
		return false;
	}
	const std::string leaf = pathToUtf8String(
		std::filesystem::u8path(outputPath).filename());
	NativeFileHandle currentFile;
	PhysicalPathIdentity currentIdentity;
	std::uintmax_t currentLinkCount = 0;
	if (leaf.empty() ||
		!openChildFileNoFollow(
			parentDuplicate, leaf, false, false,
			false, false, currentFile) ||
		!nativeHandleInformation(
			currentFile.get(), false, currentIdentity,
			&currentLinkCount) ||
		currentLinkCount != 1 ||
		!physicalPathIdentitiesEqual(
			heldIdentity, currentIdentity))
	{
		return false;
	}
	return editorRunRoutingSnapshotIsCurrent(
		File::EditorRunFileLayoutState::Valid, generation);
}

bool openEditorRunExactOutput(
	const std::string& outputPath,
	uint64_t generation,
	std::FILE*& file,
	std::intptr_t& parentToken,
	EditorRunExactOutputKind kind)
{
	file = nullptr;
	parentToken = -1;
	InstalledEditorRunFileLayout layout;
	uint64_t currentGeneration = 0;
	if (getInstalledEditorRunFileLayout(
			layout, &currentGeneration) !=
			File::EditorRunFileLayoutState::Valid ||
		currentGeneration != generation)
	{
		return false;
	}
	const std::string* installedOutputPath = nullptr;
	const std::string* installedParentPath = nullptr;
	const PhysicalPathIdentity* installedParentIdentity = nullptr;
	if (!selectEditorRunExactOutput(
			layout, kind, installedOutputPath,
			installedParentPath, installedParentIdentity) ||
		installedOutputPath == nullptr ||
		installedParentPath == nullptr ||
		installedParentIdentity == nullptr ||
		*installedOutputPath != outputPath)
	{
		return false;
	}

	File::EditorRunFileOperationPhase operationPhase =
		File::EditorRunFileOperationPhase::
			BeforeLogParentOpen;
	if (kind ==
		EditorRunExactOutputKind::Diagnostics)
	{
		operationPhase =
			File::EditorRunFileOperationPhase::
				BeforeDiagnosticsParentOpen;
	}
	else if (kind ==
		EditorRunExactOutputKind::RuntimeTrace)
	{
		operationPhase =
			File::EditorRunFileOperationPhase::
				BeforeRuntimeTraceParentOpen;
	}
	invokeEditorRunFileOperationTestHook(operationPhase);
	if (!editorRunRoutingSnapshotIsCurrent(
			File::EditorRunFileLayoutState::Valid, generation))
	{
		return false;
	}
	NativeDirectoryHandle parent;
	if (!openAbsoluteDirectoryNoFollow(
			installedIdentityPath(*installedParentPath),
			*installedParentIdentity, parent))
	{
		return false;
	}
	const std::string leaf = pathToUtf8String(
		std::filesystem::u8path(outputPath).filename());
	NativeFileHandle openedFile;
	if (leaf.empty() ||
		!createChildFileExclusiveNoFollow(
			parent, leaf, openedFile) ||
		!editorRunRoutingSnapshotIsCurrent(
			File::EditorRunFileLayoutState::Valid, generation))
	{
		return false;
	}

#if defined(_WIN32)
	const HANDLE rawHandle = openedFile.release();
	const int descriptor = _open_osfhandle(
		reinterpret_cast<intptr_t>(rawHandle),
		_O_WRONLY | _O_APPEND | _O_BINARY |
			_O_NOINHERIT);
	if (descriptor < 0)
	{
		CloseHandle(rawHandle);
		return false;
	}
	file = _wfdopen(descriptor, L"ab");
	if (file == nullptr)
	{
		_close(descriptor);
		return false;
	}
	parentToken = reinterpret_cast<std::intptr_t>(
		parent.release());
#else
	const int descriptor = openedFile.release();
	file = fdopen(descriptor, "ab");
	if (file == nullptr)
	{
		close(descriptor);
		return false;
	}
	if (fsync(descriptor) != 0 ||
		fsync(parent.get()) != 0)
	{
		std::fclose(file);
		file = nullptr;
		return false;
	}
	parentToken = static_cast<std::intptr_t>(
		parent.release());
#endif
	if (!editorRunExactOutputHandleIsCurrent(
			file, parentToken, outputPath, generation, kind))
	{
		std::fclose(file);
		file = nullptr;
		closeEditorRunExactOutputParent(parentToken);
		parentToken = -1;
		return false;
	}
	return true;
}

std::string fileStemFromName(const std::string& fileName)
{
	if (!isValidUtf8(fileName))
	{
		return "";
	}
	try
	{
		std::filesystem::path path = std::filesystem::u8path(fileName);
		return pathToUtf8String(path.stem());
	}
	catch (const std::exception&)
	{
		return "";
	}
}

std::string fileExtensionFromName(const std::string& fileName)
{
	if (!isValidUtf8(fileName))
	{
		return "";
	}
	try
	{
		std::filesystem::path path = std::filesystem::u8path(fileName);
		return toLowerAscii(pathToUtf8String(path.extension()));
	}
	catch (const std::exception&)
	{
		return "";
	}
}

std::string extractStableResourceAliasPrefix(const std::string& fileStem)
{
	std::string prefix;
	bool hasDigit = false;
	for (unsigned char character : fileStem)
	{
		if (character >= 0x80)
		{
			break;
		}
		if (std::isalnum(character) || character == '-' || character == '_')
		{
			prefix.push_back(static_cast<char>(std::tolower(character)));
			if (std::isdigit(character))
			{
				hasDigit = true;
			}
			continue;
		}
		break;
	}
	while (!prefix.empty() && (prefix.back() == '-' || prefix.back() == '_'))
	{
		prefix.pop_back();
	}
	if (!hasDigit || prefix.size() < 3)
	{
		return "";
	}
	return prefix;
}

bool asciiStemEndsWithIconSuffix(const std::string& fileStem)
{
	if (fileStem.empty())
	{
		return false;
	}
	unsigned char character = static_cast<unsigned char>(fileStem.back());
	return character < 0x80 && std::tolower(character) == 's';
}

std::string resolveUniqueImagePackageAlias(const std::string& fullPath)
{
	if (!isImagePackagePath(fullPath) || !isValidUtf8(fullPath))
	{
		return "";
	}

	try
	{
		std::filesystem::path requestedPath = std::filesystem::u8path(fullPath);
		std::filesystem::path parentPath = requestedPath.parent_path();
		std::error_code errorCode;
		if (parentPath.empty() || !std::filesystem::is_directory(parentPath, errorCode))
		{
			return "";
		}

		std::string requestedFileName = pathToUtf8String(requestedPath.filename());
		std::string requestedStem = fileStemFromName(requestedFileName);
		std::string requestedExtension = fileExtensionFromName(requestedFileName);
		std::string requestedPrefix = extractStableResourceAliasPrefix(requestedStem);
		if (requestedFileName.empty() || requestedPrefix.empty() || requestedExtension.empty())
		{
			return "";
		}

		bool requestedIconSuffix = asciiStemEndsWithIconSuffix(requestedStem);
		std::vector<std::filesystem::path> matches;
		std::filesystem::directory_iterator iterator(parentPath, errorCode);
		std::filesystem::directory_iterator end;
		while (!errorCode && iterator != end)
		{
			bool regularFile = iterator->is_regular_file(errorCode);
			if (errorCode)
			{
				break;
			}
			if (regularFile)
			{
				std::string candidateName = pathToUtf8String(iterator->path().filename());
				if (!candidateName.empty() &&
					toLowerAscii(candidateName) != toLowerAscii(requestedFileName) &&
					fileExtensionFromName(candidateName) == requestedExtension)
				{
					std::string candidateStem = fileStemFromName(candidateName);
					if (asciiStemEndsWithIconSuffix(candidateStem) == requestedIconSuffix &&
						extractStableResourceAliasPrefix(candidateStem) == requestedPrefix)
					{
						matches.push_back(iterator->path());
					}
				}
			}
			iterator.increment(errorCode);
		}
		if (errorCode || matches.size() != 1)
		{
			return "";
		}
		return pathToUtf8String(matches.front());
	}
	catch (const std::exception&)
	{
		return "";
	}
}

std::vector<std::string> listFilesInFullDirectory(
	const std::string& fullDirectory,
	std::size_t maximumEntryCount = 0,
	bool* entryCountLimitExceeded = nullptr);

std::string resolveAnchoredImagePackageAlias(
	const RoutedResourcePath& route,
	const std::string& requestedRelativePath)
{
	if (!route.anchored ||
		!isImagePackagePath(requestedRelativePath) ||
		!isValidUtf8(requestedRelativePath))
	{
		return "";
	}

	const std::vector<std::string> components =
		splitRelativePathComponents(requestedRelativePath);
	if (components.empty())
	{
		return "";
	}
	const std::string& requestedFileName = components.back();
	const std::string requestedStem =
		fileStemFromName(requestedFileName);
	const std::string requestedExtension =
		fileExtensionFromName(requestedFileName);
	const std::string requestedPrefix =
		extractStableResourceAliasPrefix(requestedStem);
	if (requestedFileName.empty() || requestedPrefix.empty() ||
		requestedExtension.empty())
	{
		return "";
	}

	NativeDirectoryHandle root;
	if (!openAnchoredRouteRoot(
			route,
			File::EditorRunFileOperationPhase::BeforeReadRootOpen,
			root))
	{
		return "";
	}
	std::vector<std::string> resolvedParentComponents;
	NativeDirectoryHandle parent;
	if (!openRelativeDirectoryNoFollow(
			std::move(root), components,
			components.size() - 1,
			false, true, parent,
			&resolvedParentComponents))
	{
		return "";
	}

	const bool requestedIconSuffix =
		asciiStemEndsWithIconSuffix(requestedStem);
	std::vector<std::string> matches;
	for (const std::string& candidateName :
		listNativeDirectoryNames(parent))
	{
		if (toLowerAscii(candidateName) ==
				toLowerAscii(requestedFileName) ||
			fileExtensionFromName(candidateName) !=
				requestedExtension)
		{
			continue;
		}
		const std::string candidateStem =
			fileStemFromName(candidateName);
		if (asciiStemEndsWithIconSuffix(candidateStem) !=
				requestedIconSuffix ||
			extractStableResourceAliasPrefix(candidateStem) !=
				requestedPrefix)
		{
			continue;
		}
		NativeFileHandle candidate;
		if (openChildFileNoFollow(
				parent, candidateName, false, false,
				false, false, candidate))
		{
			matches.push_back(candidateName);
		}
	}
	if (matches.size() != 1 ||
		!editorRunRouteIsCurrent(route))
	{
		return "";
	}
	resolvedParentComponents.push_back(matches.front());
	return joinRelativeComponents(resolvedParentComponents);
}

bool readRoutedResource(
	const RoutedResourcePath& route,
	std::unique_ptr<char[]>& data,
	int& length,
	std::size_t maximumBytes,
	std::string* resolvedRelativePath = nullptr)
{
	data.reset();
	length = 0;
	if (resolvedRelativePath != nullptr)
	{
		resolvedRelativePath->clear();
	}
	if (!route.anchored)
	{
		const std::string fullPath =
			makeRoutedReadPath(route);
		if (openFileForRead(
				fullPath, data, length, maximumBytes))
		{
			if (resolvedRelativePath != nullptr)
			{
				*resolvedRelativePath = route.relativePath;
			}
			return true;
		}
		const std::string caseInsensitivePath =
			resolveCaseInsensitiveExistingPath(fullPath);
		if (!caseInsensitivePath.empty() &&
			caseInsensitivePath != fullPath &&
			routedReadPathIsContained(
				route, caseInsensitivePath) &&
			openFileForRead(
				caseInsensitivePath, data, length,
				maximumBytes))
		{
			if (resolvedRelativePath != nullptr)
			{
				*resolvedRelativePath = caseInsensitivePath;
			}
			return true;
		}
		const std::string aliasPath =
			resolveUniqueImagePackageAlias(fullPath);
		if (!aliasPath.empty() &&
			routedReadPathIsContained(route, aliasPath) &&
			openFileForRead(
				aliasPath, data, length, maximumBytes))
		{
			if (resolvedRelativePath != nullptr)
			{
				*resolvedRelativePath = aliasPath;
			}
			return true;
		}
		return false;
	}

	NativeFileHandle file;
	std::string resolvedPath;
	if (!openAnchoredFileForRead(
			route, route.relativePath, file, &resolvedPath))
	{
		const std::string aliasRelativePath =
			resolveAnchoredImagePackageAlias(
				route, route.relativePath);
		if (aliasRelativePath.empty() ||
			!openAnchoredFileForRead(
				route, aliasRelativePath, file,
				&resolvedPath))
		{
			return false;
		}
	}
	if (!readNativeFileBounded(
			file, data, length, maximumBytes) ||
		!editorRunRouteIsCurrent(route))
	{
		data.reset();
		length = 0;
		return false;
	}
	if (resolvedRelativePath != nullptr)
	{
		*resolvedRelativePath = resolvedPath;
	}
	return true;
}

bool routedRegularFileExists(const RoutedResourcePath& route)
{
	if (!route.anchored)
	{
		const std::string fullPath =
			makeRoutedReadPath(route);
		if (pathIsRegularFile(fullPath))
		{
			return true;
		}
		const std::string caseInsensitivePath =
			resolveCaseInsensitiveExistingPath(fullPath);
		if (!caseInsensitivePath.empty() &&
			routedReadPathIsContained(
				route, caseInsensitivePath) &&
			pathIsRegularFile(caseInsensitivePath))
		{
			return true;
		}
		const std::string aliasPath =
			resolveUniqueImagePackageAlias(fullPath);
		return !aliasPath.empty() &&
			routedReadPathIsContained(route, aliasPath) &&
			pathIsRegularFile(aliasPath);
	}

	NativeFileHandle file;
	if (openAnchoredFileForRead(
			route, route.relativePath, file))
	{
		return editorRunRouteIsCurrent(route);
	}
	const std::string aliasRelativePath =
		resolveAnchoredImagePackageAlias(
			route, route.relativePath);
	return !aliasRelativePath.empty() &&
		openAnchoredFileForRead(
			route, aliasRelativePath, file) &&
		editorRunRouteIsCurrent(route);
}

bool routedPathExists(const RoutedResourcePath& route)
{
	if (!route.anchored)
	{
		return false;
	}
	if (routedRegularFileExists(route))
	{
		return true;
	}
	const std::vector<std::string> components =
		splitRelativePathComponents(route.relativePath);
	if (!route.relativePath.empty() && components.empty())
	{
		return false;
	}
	NativeDirectoryHandle root;
	if (!openAnchoredRouteRoot(
			route,
			File::EditorRunFileOperationPhase::BeforeReadRootOpen,
			root))
	{
		return false;
	}
	NativeDirectoryHandle directory;
	return openRelativeDirectoryNoFollow(
			std::move(root), components, components.size(),
			false, true, directory) &&
		editorRunRouteIsCurrent(route);
}

std::vector<std::string> listRoutedFiles(
	const RoutedResourcePath& route,
	std::size_t maximumEntryCount = 0,
	bool* entryCountLimitExceeded = nullptr)
{
	if (entryCountLimitExceeded != nullptr)
	{
		*entryCountLimitExceeded = false;
	}
	if (!route.anchored)
	{
		std::string fullDirectory =
			makeRoutedReadPath(route);
		const std::string caseInsensitiveDirectory =
			resolveCaseInsensitiveExistingPath(fullDirectory);
		if (!caseInsensitiveDirectory.empty() &&
			routedReadPathIsContained(
				route, caseInsensitiveDirectory))
		{
			fullDirectory = caseInsensitiveDirectory;
		}
		return listFilesInFullDirectory(
			fullDirectory,
			maximumEntryCount,
			entryCountLimitExceeded);
	}

	NativeDirectoryHandle root;
	if (!openAnchoredRouteRoot(
			route,
			File::EditorRunFileOperationPhase::BeforeReadRootOpen,
			root))
	{
		return {};
	}
	const std::vector<std::string> components =
		splitRelativePathComponents(route.relativePath);
	if (!route.relativePath.empty() && components.empty())
	{
		return {};
	}
	NativeDirectoryHandle directory;
	if (!openRelativeDirectoryNoFollow(
			std::move(root), components, components.size(),
			false, true, directory))
	{
		return {};
	}
	std::vector<std::string> files;
	bool nativeEntryLimitExceeded = false;
	for (const std::string& name :
		listNativeDirectoryNames(
			directory,
			nullptr,
			maximumEntryCount,
			&nativeEntryLimitExceeded))
	{
		NativeFileHandle file;
		if (openChildFileNoFollow(
				directory, name, false, false,
				false, false, file))
		{
			files.push_back(name);
		}
	}
	if (nativeEntryLimitExceeded)
	{
		if (entryCountLimitExceeded != nullptr)
		{
			*entryCountLimitExceeded = true;
		}
		return {};
	}
	if (!editorRunRouteIsCurrent(route))
	{
		return {};
	}
	std::sort(files.begin(), files.end());
	return files;
}

std::string normalizeFileNameKey(std::string fileName)
{
	convert::replaceAllString(fileName, "\\", "/");
	return toLowerAscii(fileName);
}

std::string joinFullPath(const std::string& directoryName, const std::string& fileName)
{
	return normalizeRoot(directoryName) + normalizeRelativePath(fileName);
}

struct DirectoryFilesContext
{
	std::vector<std::string> files;
	std::size_t maximumEntryCount = 0;
	std::size_t encounteredEntryCount = 0;
	bool entryCountLimitExceeded = false;
};

extern "C" SDL_EnumerationResult SDLCALL listDirectoryFilesCallback(void* userdata,
	const char* dirname,
	const char* fname)
{
	if (userdata == nullptr || fname == nullptr)
	{
		return SDL_ENUM_CONTINUE;
	}

	std::string fileName = fname;
	if (fileName.empty() || fileName == "." || fileName == "..")
	{
		return SDL_ENUM_CONTINUE;
	}
	auto context = static_cast<DirectoryFilesContext*>(userdata);
	if (context->maximumEntryCount > 0 &&
		context->encounteredEntryCount >=
			context->maximumEntryCount)
	{
		context->entryCountLimitExceeded = true;
		return SDL_ENUM_SUCCESS;
	}
	++context->encounteredEntryCount;

	std::string fullPath = joinFullPath(dirname == nullptr ? "" : dirname, fileName);
	SDL_PathInfo info;
	if (SDL_GetPathInfo(fullPath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
	{
		context->files.push_back(fileName);
	}
	return SDL_ENUM_CONTINUE;
}

std::vector<std::string> listFilesInFullDirectory(
	const std::string& fullDirectory,
	std::size_t maximumEntryCount,
	bool* entryCountLimitExceeded)
{
	std::vector<std::string> files;
	if (entryCountLimitExceeded != nullptr)
	{
		*entryCountLimitExceeded = false;
	}
#if defined(__APPLE__)
	try
	{
		std::error_code errorCode;
		const std::filesystem::path directoryPath =
			std::filesystem::u8path(fullDirectory);
		const std::filesystem::file_status directoryStatus =
			std::filesystem::status(directoryPath, errorCode);
		if (errorCode ||
			!std::filesystem::is_directory(directoryStatus))
		{
			return files;
		}

		DirectoryFilesContext context;
		context.maximumEntryCount = maximumEntryCount;
		std::filesystem::directory_iterator iterator(
			directoryPath, errorCode);
		const std::filesystem::directory_iterator end;
		while (!errorCode && iterator != end)
		{
			const std::string fileName = pathToUtf8String(
				iterator->path().filename());
			if (!fileName.empty() && fileName != "." &&
				fileName != "..")
			{
				if (context.maximumEntryCount > 0 &&
					context.encounteredEntryCount >=
						context.maximumEntryCount)
				{
					context.entryCountLimitExceeded = true;
					break;
				}
				++context.encounteredEntryCount;
				if (iterator->is_regular_file(errorCode))
				{
					context.files.push_back(fileName);
				}
			}
			if (!errorCode)
			{
				iterator.increment(errorCode);
			}
		}
		if (errorCode)
		{
			GameLog::write(
				"Can not enumerate directory %s\n",
				fullDirectory.c_str());
			return {};
		}
		if (context.entryCountLimitExceeded)
		{
			if (entryCountLimitExceeded != nullptr)
			{
				*entryCountLimitExceeded = true;
			}
			return {};
		}

		std::sort(context.files.begin(), context.files.end());
		return context.files;
	}
	catch (const std::exception&)
	{
		GameLog::write(
			"Can not enumerate directory %s\n",
			fullDirectory.c_str());
		return {};
	}
#else
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(fullDirectory.c_str(), &info) || info.type != SDL_PATHTYPE_DIRECTORY)
	{
		return files;
	}

	DirectoryFilesContext context;
	context.maximumEntryCount = maximumEntryCount;
	if (!SDL_EnumerateDirectory(fullDirectory.c_str(), listDirectoryFilesCallback, &context))
	{
		GameLog::write("Can not enumerate directory %s\n", fullDirectory.c_str());
		return files;
	}
	if (context.entryCountLimitExceeded)
	{
		if (entryCountLimitExceeded != nullptr)
		{
			*entryCountLimitExceeded = true;
		}
		return {};
	}

	std::sort(context.files.begin(), context.files.end());
	return context.files;
#endif
}

bool createWriteDirectory(const std::string& fullDirectory)
{
	if (fullDirectory.empty() || !isValidUtf8(fullDirectory))
	{
		return false;
	}
	std::error_code errorCode;
	try
	{
		if (std::filesystem::create_directories(std::filesystem::u8path(fullDirectory), errorCode))
		{
			return true;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	if (!errorCode)
	{
		return pathIsDirectory(fullDirectory);
	}

	SDL_PathInfo info;
	if (SDL_GetPathInfo(fullDirectory.c_str(), &info))
	{
		if (info.type == SDL_PATHTYPE_DIRECTORY)
		{
			return true;
		}
		if (!SDL_RemovePath(fullDirectory.c_str()))
		{
			GameLog::write("Can not remove path %s\n", fullDirectory.c_str());
			return false;
		}
	}
	if (!SDL_CreateDirectory(fullDirectory.c_str()))
	{
		GameLog::write("Can not create directory %s\n", fullDirectory.c_str());
		return false;
	}
	return true;
}

bool isExcludedFileName(const std::string& fileName, const std::vector<std::string>& excludedFileNames)
{
	std::string key = normalizeFileNameKey(fileName);
	for (const auto& excludedFileName : excludedFileNames)
	{
		if (key == normalizeFileNameKey(excludedFileName))
		{
			return true;
		}
	}
	return false;
}

std::mutex g_directoryCopyMutex;

struct DirectoryCopyTransactionPaths
{
	RoutedResourcePath route;
	NativeDirectoryHandle parentHandle;
	bool anchored = false;
	std::string sourceLeaf;
	std::string destinationLeaf;
	std::string stagingLeaf;
	std::string backupLeaf;
	std::string readyLeaf;
	std::filesystem::path routingRoot;
	std::filesystem::path parent;
	std::filesystem::path canonicalParent;
	std::filesystem::path source;
	std::filesystem::path destination;
	std::filesystem::path staging;
	std::filesystem::path backup;
	std::filesystem::path ready;
	PhysicalPathIdentity parentIdentity;
	PhysicalPathIdentity sourceIdentity;
	PhysicalPathIdentity destinationIdentity;
	PhysicalPathIdentity stagingIdentity;
	PhysicalPathIdentity backupIdentity;
	PhysicalPathIdentity readyIdentity;
	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
};

bool getDirectoryCopyTransactionPaths(const std::string& destinationDirectoryName,
	DirectoryCopyTransactionPaths& paths)
{
	const RoutedResourcePath route = buildWriteRoute(destinationDirectoryName,
		&paths.routingState, &paths.routingGeneration);
	if (route.root.empty() || route.relativePath.empty())
	{
		return false;
	}
	std::string fullDirectory = makeFullPath(route.root, route.relativePath);
	while (fullDirectory.size() > 1 && fullDirectory.back() == '/')
	{
		fullDirectory.pop_back();
	}
	if (fullDirectory.empty() || !isFullPathContainedByPrefix(route.root, fullDirectory))
	{
		return false;
	}

	try
	{
		paths.destination = std::filesystem::u8path(fullDirectory).lexically_normal();
		const std::filesystem::path parent = paths.destination.parent_path();
		const std::string leaf = pathToUtf8String(paths.destination.filename());
		if (parent.empty() || leaf.empty() || leaf == "." || leaf == "..")
		{
			return false;
		}
		paths.staging = parent / std::filesystem::u8path(".jxqy-" + leaf + "-staging");
		paths.backup = parent / std::filesystem::u8path(".jxqy-" + leaf + "-backup");
		paths.ready = parent / std::filesystem::u8path(".jxqy-" + leaf + "-staging-ready");
		paths.route = route;
		paths.destinationLeaf = leaf;
		paths.stagingLeaf = ".jxqy-" + leaf + "-staging";
		paths.backupLeaf = ".jxqy-" + leaf + "-backup";
		paths.readyLeaf = ".jxqy-" + leaf + "-staging-ready";
		if (route.anchored)
		{
			const std::vector<std::string> components =
				splitRelativePathComponents(route.relativePath);
			if (components.empty())
			{
				return false;
			}
			NativeDirectoryHandle root;
			if (!openAnchoredRouteRoot(
					route,
					File::EditorRunFileOperationPhase::
						BeforeWriteRootOpen,
					root) ||
				!openRelativeDirectoryNoFollow(
					std::move(root), components,
					components.size() - 1,
					true, false, paths.parentHandle) ||
				!nativeHandleInformation(
					paths.parentHandle.get(), true,
					paths.parentIdentity) ||
				!editorRunRouteIsCurrent(route))
			{
				return false;
			}
			paths.parent = parent.lexically_normal();
			paths.anchored = true;
			return true;
		}
		const std::filesystem::path routeRoot =
			std::filesystem::u8path(route.root).lexically_normal();
		if (!createWriteDirectory(pathToUtf8String(parent)))
		{
			return false;
		}
		std::error_code rootError;
		std::error_code parentError;
		paths.routingRoot =
			std::filesystem::canonical(routeRoot, rootError);
		paths.canonicalParent =
			std::filesystem::canonical(parent, parentError);
		if (rootError || parentError || paths.routingRoot.empty() ||
			paths.canonicalParent.empty() ||
			!hostPathContains(paths.routingRoot, paths.canonicalParent) ||
			!captureDirectoryPhysicalIdentity(
				parent, paths.parentIdentity))
		{
			return false;
		}
		paths.parent = parent.lexically_normal();
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool setDirectoryPromotionSourcePath(
	const DirectoryCopyTransactionPaths& sourcePaths,
	DirectoryCopyTransactionPaths& paths)
{
	if (sourcePaths.routingState != paths.routingState ||
		sourcePaths.routingGeneration != paths.routingGeneration ||
		sourcePaths.anchored != paths.anchored ||
		normalizeRoot(sourcePaths.route.root) !=
			normalizeRoot(paths.route.root) ||
		!hostPathsEqual(sourcePaths.parent, paths.parent) ||
		!physicalPathIdentitiesEqual(
			sourcePaths.parentIdentity,
			paths.parentIdentity) ||
		(sourcePaths.anchored &&
		 !physicalPathIdentitiesEqual(
			sourcePaths.route.rootIdentity,
			paths.route.rootIdentity)))
	{
		return false;
	}
	if (sourcePaths.destinationLeaf.empty() ||
		hostPathsEqual(
			sourcePaths.destination,
			paths.destination) ||
		hostPathsEqual(
			sourcePaths.destination,
			paths.staging) ||
		hostPathsEqual(
			sourcePaths.destination,
			paths.backup) ||
		hostPathsEqual(
			sourcePaths.destination,
			paths.ready))
	{
		return false;
	}
	paths.source = sourcePaths.destination;
	paths.sourceLeaf = sourcePaths.destinationLeaf;
	return true;
}

bool directoryCopyRoutingIsCurrent(
	const DirectoryCopyTransactionPaths& paths)
{
	if (!editorRunRoutingSnapshotIsCurrent(
			paths.routingState, paths.routingGeneration) ||
		!paths.parentIdentity.valid)
	{
		return false;
	}
	if (paths.anchored)
	{
		PhysicalPathIdentity currentIdentity;
		return paths.parentHandle.valid() &&
			nativeHandleInformation(
				paths.parentHandle.get(), true,
				currentIdentity) &&
			physicalPathIdentitiesEqual(
				currentIdentity, paths.parentIdentity);
	}
	const NoFollowPathInformation parentInformation =
		inspectPathNoFollow(paths.parent);
	if (parentInformation.kind != NoFollowPathKind::Directory ||
		!physicalPathIdentitiesEqual(
			parentInformation.identity, paths.parentIdentity))
	{
		return false;
	}
	std::error_code errorCode;
	const std::filesystem::path canonicalParent =
		std::filesystem::canonical(paths.parent, errorCode);
	return !errorCode && !canonicalParent.empty() &&
		hostPathsEqual(canonicalParent, paths.canonicalParent) &&
		hostPathContains(paths.routingRoot, canonicalParent);
}

std::string transactionLeaf(
	const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path)
{
	if (!paths.source.empty() &&
		hostPathsEqual(path, paths.source))
	{
		return paths.sourceLeaf;
	}
	if (hostPathsEqual(path, paths.destination))
	{
		return paths.destinationLeaf;
	}
	if (hostPathsEqual(path, paths.staging))
	{
		return paths.stagingLeaf;
	}
	if (hostPathsEqual(path, paths.backup))
	{
		return paths.backupLeaf;
	}
	if (hostPathsEqual(path, paths.ready))
	{
		return paths.readyLeaf;
	}
	return "";
}

NoFollowPathInformation inspectNativeChildNoFollow(
	const NativeDirectoryHandle& parent,
	const std::string& leaf)
{
	NoFollowPathInformation result;
	if (!parent.valid() || leaf.empty())
	{
		return result;
	}
#if defined(_WIN32)
	HANDLE handle = INVALID_HANDLE_VALUE;
	NTSTATUS status = 0;
	if (!ntOpenRelative(
			parent.get(),
			std::filesystem::u8path(leaf).wstring(),
			FILE_READ_ATTRIBUTES | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			FILE_OPEN,
			FILE_OPEN_REPARSE_POINT |
				FILE_SYNCHRONOUS_IO_NONALERT,
			FILE_ATTRIBUTE_NORMAL,
			handle, nullptr, &status))
	{
		const NTSTATUS nameNotFound =
			static_cast<NTSTATUS>(0xC0000034u);
		const NTSTATUS pathNotFound =
			static_cast<NTSTATUS>(0xC000003Au);
		const NTSTATUS noSuchFile =
			static_cast<NTSTATUS>(0xC000000Fu);
		if (status == nameNotFound ||
			status == pathNotFound ||
			status == noSuchFile)
		{
			result.kind = NoFollowPathKind::Missing;
		}
		return result;
	}
	NativeFileHandle opened(handle);
	BY_HANDLE_FILE_INFORMATION information = {};
	if (GetFileInformationByHandle(
			opened.get(), &information) == 0 ||
		GetFileType(opened.get()) != FILE_TYPE_DISK ||
		(information.dwFileAttributes &
			FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		return result;
	}
	if (!populatePhysicalPathIdentity(
			opened.get(), information,
			result.identity))
	{
		return result;
	}
	result.linkCount = information.nNumberOfLinks;
	result.kind =
		(information.dwFileAttributes &
			FILE_ATTRIBUTE_DIRECTORY) != 0
		? NoFollowPathKind::Directory
		: NoFollowPathKind::RegularFile;
	return result;
#else
	struct stat information = {};
	if (fstatat(
			parent.get(), leaf.c_str(), &information,
			AT_SYMLINK_NOFOLLOW) != 0)
	{
		if (errno == ENOENT || errno == ENOTDIR)
		{
			result.kind = NoFollowPathKind::Missing;
		}
		return result;
	}
	if (!S_ISDIR(information.st_mode) &&
		!S_ISREG(information.st_mode))
	{
		return result;
	}
	result.identity.deviceOrVolume =
		static_cast<std::uint64_t>(information.st_dev);
	result.identity.nodeLow =
		static_cast<std::uint64_t>(information.st_ino);
	result.identity.linkCount =
		static_cast<std::uint64_t>(information.st_nlink);
	result.identity.valid = true;
	result.linkCount =
		static_cast<std::uintmax_t>(information.st_nlink);
	result.kind = S_ISDIR(information.st_mode)
		? NoFollowPathKind::Directory
		: NoFollowPathKind::RegularFile;
	return result;
#endif
}

bool anchoredResourcePathIsDefinitelyMissing(
	const RoutedResourcePath& route)
{
	if (!route.anchored)
	{
		return false;
	}
	const std::vector<std::string> components =
		splitRelativePathComponents(route.relativePath);
	if (components.empty())
	{
		return false;
	}

	NativeDirectoryHandle current;
	if (!openAnchoredRouteRoot(
			route,
			File::EditorRunFileOperationPhase::BeforeReadRootOpen,
			current))
	{
		return false;
	}

	for (std::size_t index = 0;
		index + 1 < components.size();
		++index)
	{
		std::string actualName = components[index];
		NoFollowPathInformation information =
			inspectNativeChildNoFollow(current, actualName);
		if (information.kind == NoFollowPathKind::Missing)
		{
			bool listed = false;
			const std::vector<std::string> names =
				listNativeDirectoryNames(current, &listed);
			if (!listed)
			{
				return false;
			}
			const std::string requestedKey =
				toLowerAscii(actualName);
			std::vector<std::string> matches;
			for (const std::string& name : names)
			{
				if (toLowerAscii(name) == requestedKey)
				{
					matches.push_back(name);
				}
			}
			if (matches.empty())
			{
				return editorRunRouteIsCurrent(route);
			}
			if (matches.size() != 1)
			{
				return false;
			}
			actualName = matches.front();
			information =
				inspectNativeChildNoFollow(current, actualName);
		}
		if (information.kind != NoFollowPathKind::Directory)
		{
			return false;
		}
		NativeDirectoryHandle next;
		if (!openChildDirectoryNoFollow(
				current, actualName, false, next))
		{
			return false;
		}
		current = std::move(next);
	}

	const std::string& requestedFileName = components.back();
	const NoFollowPathInformation exactInformation =
		inspectNativeChildNoFollow(current, requestedFileName);
	if (exactInformation.kind != NoFollowPathKind::Missing)
	{
		return false;
	}

	bool listed = false;
	const std::vector<std::string> names =
		listNativeDirectoryNames(current, &listed);
	if (!listed)
	{
		return false;
	}
	const std::string requestedKey =
		toLowerAscii(requestedFileName);
	for (const std::string& name : names)
	{
		if (toLowerAscii(name) == requestedKey)
		{
			return false;
		}
	}

	if (isImagePackagePath(route.relativePath))
	{
		const std::string requestedStem =
			fileStemFromName(requestedFileName);
		const std::string requestedExtension =
			fileExtensionFromName(requestedFileName);
		const std::string requestedPrefix =
			extractStableResourceAliasPrefix(requestedStem);
		const bool requestedIconSuffix =
			asciiStemEndsWithIconSuffix(requestedStem);
		if (!requestedPrefix.empty() &&
			!requestedExtension.empty())
		{
			for (const std::string& candidateName : names)
			{
				const std::string candidateStem =
					fileStemFromName(candidateName);
				if (fileExtensionFromName(candidateName) ==
						requestedExtension &&
					asciiStemEndsWithIconSuffix(candidateStem) ==
						requestedIconSuffix &&
					extractStableResourceAliasPrefix(
						candidateStem) == requestedPrefix)
				{
					return false;
				}
			}
		}
	}
	return editorRunRouteIsCurrent(route);
}

bool openChildDirectoryForDelete(
	const NativeDirectoryHandle& parent,
	const std::string& leaf,
	NativeDirectoryHandle& directory)
{
	directory.reset();
#if defined(_WIN32)
	HANDLE handle = INVALID_HANDLE_VALUE;
	if (!ntOpenRelative(
			parent.get(),
			std::filesystem::u8path(leaf).wstring(),
			FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES |
				DELETE | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			FILE_OPEN,
			FILE_DIRECTORY_FILE |
				FILE_OPEN_REPARSE_POINT |
				FILE_SYNCHRONOUS_IO_NONALERT,
			FILE_ATTRIBUTE_DIRECTORY,
			handle))
	{
		return false;
	}
	directory.reset(handle);
#else
	if (!openChildDirectoryNoFollow(
			parent, leaf, false, directory))
	{
		return false;
	}
#endif
	PhysicalPathIdentity identity;
	if (!nativeHandleInformation(
			directory.get(), true, identity))
	{
		directory.reset();
		return false;
	}
	return true;
}

bool nativeDirectoryTreeIsSafe(
	const NativeDirectoryHandle& directory)
{
	if (!directory.valid())
	{
		return false;
	}
	bool listed = false;
	const std::vector<std::string> names =
		listNativeDirectoryNames(directory, &listed);
	if (!listed)
	{
		return false;
	}
	for (const std::string& name : names)
	{
		NativeFileHandle file;
		if (openChildFileNoFollow(
				directory, name, false, false,
				false, false, file))
		{
			continue;
		}
		NativeDirectoryHandle child;
		if (!openChildDirectoryNoFollow(
				directory, name, false, child) ||
			!nativeDirectoryTreeIsSafe(child))
		{
			return false;
		}
	}
	return true;
}

bool removeNativeChildRecursively(
	const NativeDirectoryHandle& parent,
	const std::string& leaf,
	const std::function<bool()>& routingIsCurrent)
{
	if (!parent.valid() || leaf.empty() ||
		!routingIsCurrent || !routingIsCurrent())
	{
		return false;
	}

	NativeDirectoryHandle directory;
	if (openChildDirectoryForDelete(
			parent, leaf, directory))
	{
		bool listed = false;
		const std::vector<std::string> childNames =
			listNativeDirectoryNames(directory, &listed);
		if (!listed)
		{
			return false;
		}
		for (const std::string& childName : childNames)
		{
			if (!removeNativeChildRecursively(
					directory, childName,
					routingIsCurrent))
			{
				return false;
			}
		}
		if (!routingIsCurrent())
		{
			return false;
		}
		invokeEditorRunFileOperationTestHook(
			File::EditorRunFileOperationPhase::
				BeforeTransactionMutation);
#if defined(_WIN32)
		FILE_DISPOSITION_INFO disposition = {};
		disposition.DeleteFile = TRUE;
		const bool removed = SetFileInformationByHandle(
			directory.get(), FileDispositionInfo,
			&disposition, sizeof(disposition)) != 0;
		directory.reset();
#else
		const bool removed =
			unlinkat(
				parent.get(), leaf.c_str(),
				AT_REMOVEDIR) == 0;
		directory.reset();
#endif
		return removed &&
			inspectNativeChildNoFollow(
				parent, leaf).kind ==
				NoFollowPathKind::Missing &&
			routingIsCurrent();
	}

	NativeFileHandle file;
	if (!openChildFileNoFollow(
			parent, leaf, false, false,
			false, true, file))
	{
		return inspectNativeChildNoFollow(
			parent, leaf).kind ==
				NoFollowPathKind::Missing &&
			routingIsCurrent();
	}
	PhysicalPathIdentity openedIdentity;
	std::uintmax_t linkCount = 0;
	if (!nativeHandleInformation(
			file.get(), false, openedIdentity,
			&linkCount) ||
		linkCount != 1 || !routingIsCurrent())
	{
		return false;
	}
	invokeEditorRunFileOperationTestHook(
		File::EditorRunFileOperationPhase::
			BeforeTransactionMutation);
#if defined(_WIN32)
	FILE_DISPOSITION_INFO disposition = {};
	disposition.DeleteFile = TRUE;
	const bool removed = SetFileInformationByHandle(
		file.get(), FileDispositionInfo,
		&disposition, sizeof(disposition)) != 0;
	file.reset();
#else
	const NoFollowPathInformation current =
		inspectNativeChildNoFollow(parent, leaf);
	const bool removed =
		current.kind == NoFollowPathKind::RegularFile &&
		current.linkCount == 1 &&
		physicalPathIdentitiesEqual(
			current.identity, openedIdentity) &&
		unlinkat(parent.get(), leaf.c_str(), 0) == 0;
	file.reset();
#endif
	return removed &&
		inspectNativeChildNoFollow(parent, leaf).kind ==
			NoFollowPathKind::Missing &&
		routingIsCurrent();
}

bool removeNativeRegularFile(
	const NativeDirectoryHandle& parent,
	const std::string& leaf,
	const std::function<bool()>& routingIsCurrent)
{
	if (!parent.valid() || leaf.empty() ||
		!routingIsCurrent || !routingIsCurrent())
	{
		return false;
	}
	NativeFileHandle file;
	if (!openChildFileNoFollow(
			parent, leaf, false, false,
			false, true, file))
	{
		return inspectNativeChildNoFollow(
			parent, leaf).kind ==
				NoFollowPathKind::Missing &&
			routingIsCurrent();
	}
	PhysicalPathIdentity openedIdentity;
	std::uintmax_t linkCount = 0;
	if (!nativeHandleInformation(
			file.get(), false, openedIdentity,
			&linkCount) ||
		linkCount != 1 || !routingIsCurrent())
	{
		return false;
	}
#if defined(_WIN32)
	FILE_DISPOSITION_INFO disposition = {};
	disposition.DeleteFile = TRUE;
	const bool removed = SetFileInformationByHandle(
		file.get(), FileDispositionInfo,
		&disposition, sizeof(disposition)) != 0;
	file.reset();
#else
	const NoFollowPathInformation current =
		inspectNativeChildNoFollow(parent, leaf);
	const bool removed =
		current.kind == NoFollowPathKind::RegularFile &&
		current.linkCount == 1 &&
		physicalPathIdentitiesEqual(
			current.identity, openedIdentity) &&
		unlinkat(parent.get(), leaf.c_str(), 0) == 0;
	file.reset();
#endif
	return removed &&
		inspectNativeChildNoFollow(parent, leaf).kind ==
			NoFollowPathKind::Missing &&
		routingIsCurrent();
}

bool openAnchoredRouteDirectory(
	const RoutedResourcePath& route,
	bool createIfMissing,
	NativeDirectoryHandle& directory)
{
	directory.reset();
	if (!route.anchored)
	{
		return false;
	}
	NativeDirectoryHandle root;
	if (!openAnchoredRouteRoot(
			route,
			File::EditorRunFileOperationPhase::
				BeforeWriteRootOpen,
			root))
	{
		return false;
	}
	const std::vector<std::string> components =
		splitRelativePathComponents(route.relativePath);
	if (!route.relativePath.empty() && components.empty())
	{
		return false;
	}
	return openRelativeDirectoryNoFollow(
			std::move(root), components, components.size(),
			createIfMissing, false, directory) &&
		editorRunRouteIsCurrent(route);
}

bool removePathRecursively(const std::filesystem::path& path)
{
	std::error_code errorCode;
	if (!std::filesystem::exists(path, errorCode))
	{
		return !errorCode;
	}
	std::filesystem::remove_all(path, errorCode);
	if (errorCode)
	{
		GameLog::write("Can not remove transaction path %s\n", pathToUtf8String(path).c_str());
		return false;
	}
	return true;
}

bool renamePath(const std::filesystem::path& source, const std::filesystem::path& destination)
{
	std::error_code errorCode;
	std::filesystem::rename(source, destination, errorCode);
	if (errorCode)
	{
		GameLog::write("Can not rename transaction path %s -> %s\n",
			pathToUtf8String(source).c_str(), pathToUtf8String(destination).c_str());
		return false;
	}
	return true;
}

enum class CheckedWriteMode
{
	Truncate,
	Append
};

bool openAnchoredRouteParent(
	const RoutedResourcePath& route,
	File::EditorRunFileOperationPhase phase,
	bool createIfMissing,
	NativeDirectoryHandle& parent,
	std::string& leaf)
{
	parent.reset();
	leaf.clear();
	if (!route.anchored)
	{
		return false;
	}
	const std::vector<std::string> components =
		splitRelativePathComponents(route.relativePath);
	if (components.empty())
	{
		return false;
	}
	NativeDirectoryHandle root;
	if (!openAnchoredRouteRoot(route, phase, root))
	{
		return false;
	}
	if (!openRelativeDirectoryNoFollow(
			std::move(root), components,
			components.size() - 1,
			createIfMissing, false, parent))
	{
		return false;
	}
	leaf = components.back();
	return editorRunRouteIsCurrent(route);
}

bool writeRelativeFileNoFollow(
	const NativeDirectoryHandle& parent,
	const std::string& leaf,
	const std::function<bool()>& routingIsCurrent,
	const void* data,
	int length,
	CheckedWriteMode mode,
	PhysicalPathIdentity* writtenIdentity = nullptr)
{
	if (writtenIdentity != nullptr)
	{
		*writtenIdentity = {};
	}
	if (!parent.valid() || leaf.empty() ||
		length < 0 || (data == nullptr && length > 0) ||
		!routingIsCurrent || !routingIsCurrent())
	{
		return false;
	}

	NativeFileHandle file;
	bool created = false;
	if (!openChildFileNoFollow(
			parent, leaf, true,
			mode == CheckedWriteMode::Append,
			true, true, file, &created))
	{
		return false;
	}
	PhysicalPathIdentity initialIdentity;
	std::uintmax_t initialLinkCount = 0;
	if (!nativeHandleInformation(
			file.get(), false, initialIdentity,
			&initialLinkCount) ||
		initialLinkCount != 1 ||
		!routingIsCurrent())
	{
#if defined(_WIN32)
		if (created)
		{
			FILE_DISPOSITION_INFO disposition = {};
			disposition.DeleteFile = TRUE;
			SetFileInformationByHandle(
				file.get(), FileDispositionInfo,
				&disposition, sizeof(disposition));
		}
#else
		if (created)
		{
			unlinkat(parent.get(), leaf.c_str(), 0);
		}
#endif
		return false;
	}

	bool succeeded = true;
	if (mode == CheckedWriteMode::Truncate)
	{
#if defined(_WIN32)
		LARGE_INTEGER start = {};
		succeeded =
			SetFilePointerEx(
				file.get(), start, nullptr, FILE_BEGIN) != 0 &&
			SetEndOfFile(file.get()) != 0;
#else
		succeeded =
			ftruncate(file.get(), 0) == 0 &&
			lseek(file.get(), 0, SEEK_SET) >= 0;
#endif
	}

	const std::uint8_t* bytes =
		static_cast<const std::uint8_t*>(data);
	std::size_t written = 0;
	while (succeeded &&
		written < static_cast<std::size_t>(length))
	{
#if defined(_WIN32)
		const DWORD requested = static_cast<DWORD>(
			(std::min)(
				static_cast<std::size_t>(length) - written,
				static_cast<std::size_t>(
					(std::numeric_limits<DWORD>::max)())));
		DWORD currentWritten = 0;
		succeeded =
			WriteFile(
				file.get(), bytes + written, requested,
				&currentWritten, nullptr) != 0 &&
			currentWritten > 0;
		if (succeeded)
		{
			written += currentWritten;
		}
#else
		const ssize_t currentWritten = write(
			file.get(), bytes + written,
			static_cast<std::size_t>(length) - written);
		if (currentWritten < 0 && errno == EINTR)
		{
			continue;
		}
		succeeded = currentWritten > 0;
		if (succeeded)
		{
			written +=
				static_cast<std::size_t>(currentWritten);
		}
#endif
	}

	PhysicalPathIdentity finalIdentity;
	std::uintmax_t finalLinkCount = 0;
	succeeded = succeeded &&
		nativeHandleInformation(
			file.get(), false, finalIdentity,
			&finalLinkCount) &&
		finalLinkCount == 1 &&
		physicalPathIdentitiesEqual(
			initialIdentity, finalIdentity) &&
		routingIsCurrent();
	if (!succeeded && created)
	{
#if defined(_WIN32)
		FILE_DISPOSITION_INFO disposition = {};
		disposition.DeleteFile = TRUE;
		SetFileInformationByHandle(
			file.get(), FileDispositionInfo,
			&disposition, sizeof(disposition));
#else
		unlinkat(parent.get(), leaf.c_str(), 0);
#endif
	}
	if (succeeded && writtenIdentity != nullptr)
	{
		*writtenIdentity = finalIdentity;
	}
	return succeeded;
}

bool currentDirectoryMatchesPhysicalIdentity(
	const std::filesystem::path& path,
	const std::filesystem::path& expectedCanonicalPath,
	const PhysicalPathIdentity& expectedIdentity,
	const std::filesystem::path& containmentRoot)
{
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (information.kind != NoFollowPathKind::Directory ||
		!physicalPathIdentitiesEqual(
			information.identity, expectedIdentity))
	{
		return false;
	}
	std::error_code errorCode;
	const std::filesystem::path canonicalPath =
		std::filesystem::canonical(path, errorCode);
	return !errorCode && !canonicalPath.empty() &&
		hostPathsEqual(canonicalPath, expectedCanonicalPath) &&
		hostPathContains(containmentRoot, canonicalPath);
}

bool writeFileNoFollow(
	const std::filesystem::path& path,
	const std::filesystem::path& expectedCanonicalParent,
	const PhysicalPathIdentity& expectedParentIdentity,
	const std::filesystem::path& containmentRoot,
	const std::function<bool()>& routingIsCurrent,
	const void* data,
	int length,
	CheckedWriteMode mode,
	PhysicalPathIdentity* writtenIdentity = nullptr)
{
	if (writtenIdentity != nullptr)
	{
		*writtenIdentity = {};
	}
	if (length < 0 || (data == nullptr && length > 0) ||
		!routingIsCurrent ||
		!routingIsCurrent() ||
		!currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot))
	{
		return false;
	}

	PhysicalPathIdentity openedIdentity;
	bool succeeded = false;
#if defined(_WIN32)
	const HANDLE parentHandle = CreateFileW(
		path.parent_path().c_str(),
		FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
			FILE_FLAG_OPEN_REPARSE_POINT,
		nullptr);
	if (parentHandle == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	BY_HANDLE_FILE_INFORMATION parentInformation = {};
	const PhysicalPathIdentity openedParentIdentity = [&]()
	{
		PhysicalPathIdentity identity;
		if (GetFileInformationByHandle(
				parentHandle, &parentInformation) != 0 &&
			(parentInformation.dwFileAttributes &
				FILE_ATTRIBUTE_DIRECTORY) != 0 &&
			(parentInformation.dwFileAttributes &
				FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
			GetFileType(parentHandle) == FILE_TYPE_DISK)
		{
			populatePhysicalPathIdentity(
				parentHandle,
				parentInformation,
				identity);
		}
		return identity;
	}();
	if (!physicalPathIdentitiesEqual(
			openedParentIdentity, expectedParentIdentity) ||
		!routingIsCurrent())
	{
		CloseHandle(parentHandle);
		return false;
	}

	using NtCreateFileFunction = decltype(&NtCreateFile);
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	const NtCreateFileFunction ntCreateFile =
		ntdll != nullptr
		? reinterpret_cast<NtCreateFileFunction>(
			GetProcAddress(ntdll, "NtCreateFile"))
		: nullptr;
	const std::wstring leaf = path.filename().wstring();
	if (ntCreateFile == nullptr || leaf.empty() ||
		leaf.size() >
			(static_cast<std::size_t>(
				(std::numeric_limits<USHORT>::max)()) /
				sizeof(wchar_t)))
	{
		CloseHandle(parentHandle);
		return false;
	}
	UNICODE_STRING leafName = {};
	leafName.Buffer = const_cast<PWSTR>(leaf.data());
	leafName.Length = static_cast<USHORT>(
		leaf.size() * sizeof(wchar_t));
	leafName.MaximumLength = leafName.Length;
	OBJECT_ATTRIBUTES attributes = {};
	InitializeObjectAttributes(
		&attributes,
		&leafName,
		OBJ_CASE_INSENSITIVE,
		parentHandle,
		nullptr);
	IO_STATUS_BLOCK ioStatus = {};
	HANDLE handle = INVALID_HANDLE_VALUE;
	ACCESS_MASK desiredAccess =
		FILE_READ_ATTRIBUTES | SYNCHRONIZE | DELETE;
	desiredAccess |= mode == CheckedWriteMode::Append
		? FILE_APPEND_DATA
		: FILE_WRITE_DATA;
	const NTSTATUS openStatus = ntCreateFile(
		&handle,
		desiredAccess,
		&attributes,
		&ioStatus,
		nullptr,
		FILE_ATTRIBUTE_NORMAL,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		FILE_OPEN_IF,
		FILE_NON_DIRECTORY_FILE |
			FILE_OPEN_REPARSE_POINT |
			FILE_SYNCHRONOUS_IO_NONALERT,
		nullptr,
		0);
	if (openStatus < 0 || handle == INVALID_HANDLE_VALUE ||
		(ioStatus.Information != FILE_OPENED &&
			ioStatus.Information != FILE_CREATED))
	{
		if (handle != INVALID_HANDLE_VALUE)
		{
			CloseHandle(handle);
		}
		CloseHandle(parentHandle);
		return false;
	}
	const bool created = ioStatus.Information == FILE_CREATED;
	const auto discardCreatedLeaf =
		[handle, created]()
		{
			if (created)
			{
				FILE_DISPOSITION_INFO disposition = {};
				disposition.DeleteFile = TRUE;
				SetFileInformationByHandle(
					handle,
					FileDispositionInfo,
					&disposition,
					sizeof(disposition));
			}
		};

	BY_HANDLE_FILE_INFORMATION information = {};
	if (GetFileInformationByHandle(handle, &information) == 0 ||
		(information.dwFileAttributes &
			(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
		information.nNumberOfLinks != 1 ||
		GetFileType(handle) != FILE_TYPE_DISK)
	{
		discardCreatedLeaf();
		CloseHandle(handle);
		CloseHandle(parentHandle);
		return false;
	}
	if (!populatePhysicalPathIdentity(
			handle, information, openedIdentity))
	{
		discardCreatedLeaf();
		CloseHandle(handle);
		CloseHandle(parentHandle);
		return false;
	}
	if (!routingIsCurrent() ||
		!physicalPathIdentitiesEqual(
			openedParentIdentity, expectedParentIdentity) ||
		!currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot))
	{
		discardCreatedLeaf();
		CloseHandle(handle);
		CloseHandle(parentHandle);
		return false;
	}

	LARGE_INTEGER offset = {};
	offset.QuadPart = 0;
	if (mode == CheckedWriteMode::Truncate)
	{
		succeeded =
			SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN) != 0 &&
			SetEndOfFile(handle) != 0;
	}
	else
	{
		succeeded = true;
	}
	const std::uint8_t* bytes =
		static_cast<const std::uint8_t*>(data);
	std::size_t written = 0;
	while (succeeded && written < static_cast<std::size_t>(length))
	{
		const DWORD requested = static_cast<DWORD>(
			(std::min)(
				static_cast<std::size_t>(length) - written,
				static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
		DWORD currentWritten = 0;
		succeeded = WriteFile(
			handle, bytes + written, requested,
			&currentWritten, nullptr) != 0 &&
			currentWritten > 0;
		written += currentWritten;
	}
	BY_HANDLE_FILE_INFORMATION afterInformation = {};
	succeeded = succeeded &&
		GetFileInformationByHandle(handle, &afterInformation) != 0 &&
		(afterInformation.dwFileAttributes &
			(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
		afterInformation.nNumberOfLinks == 1 &&
		afterInformation.dwVolumeSerialNumber ==
			information.dwVolumeSerialNumber &&
		afterInformation.nFileIndexHigh ==
			information.nFileIndexHigh &&
		afterInformation.nFileIndexLow ==
			information.nFileIndexLow &&
		routingIsCurrent() &&
		physicalPathIdentitiesEqual(
			openedParentIdentity, expectedParentIdentity);
	CloseHandle(handle);
	CloseHandle(parentHandle);
#else
	int parentFlags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
	parentFlags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	parentFlags |= O_NOFOLLOW;
#endif
	const int parentDescriptor =
		open(path.parent_path().c_str(), parentFlags);
	if (parentDescriptor < 0)
	{
		return false;
	}
	struct stat parentInformation = {};
	if (fstat(parentDescriptor, &parentInformation) != 0 ||
		!S_ISDIR(parentInformation.st_mode) ||
		static_cast<std::uintmax_t>(parentInformation.st_dev) !=
			expectedParentIdentity.deviceOrVolume ||
		static_cast<std::uintmax_t>(parentInformation.st_ino) !=
			expectedParentIdentity.nodeLow)
	{
		close(parentDescriptor);
		return false;
	}

	int openFlags = O_WRONLY;
	if (mode == CheckedWriteMode::Append)
	{
		openFlags |= O_APPEND;
	}
#if defined(O_CLOEXEC)
	openFlags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	openFlags |= O_NOFOLLOW;
#endif
	const std::string leaf = path.filename().string();
	int descriptor = openat(
		parentDescriptor, leaf.c_str(), openFlags);
	if (descriptor < 0 && errno == ENOENT)
	{
		descriptor = openat(
			parentDescriptor, leaf.c_str(),
			openFlags | O_CREAT | O_EXCL, 0600);
	}
	if (descriptor < 0)
	{
		close(parentDescriptor);
		return false;
	}

	struct stat information = {};
	if (fstat(descriptor, &information) != 0 ||
		!S_ISREG(information.st_mode) ||
		information.st_nlink != 1)
	{
		close(descriptor);
		close(parentDescriptor);
		return false;
	}
	openedIdentity.deviceOrVolume =
		static_cast<std::uint64_t>(information.st_dev);
	openedIdentity.nodeLow =
		static_cast<std::uint64_t>(information.st_ino);
	openedIdentity.linkCount =
		static_cast<std::uint64_t>(information.st_nlink);
	openedIdentity.valid = true;
	if (!routingIsCurrent() ||
		!currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot))
	{
		close(descriptor);
		close(parentDescriptor);
		return false;
	}

	if (mode == CheckedWriteMode::Truncate)
	{
		succeeded = ftruncate(descriptor, 0) == 0 &&
			lseek(descriptor, 0, SEEK_SET) >= 0;
	}
	else
	{
		succeeded = lseek(descriptor, 0, SEEK_END) >= 0;
	}
	const std::uint8_t* bytes =
		static_cast<const std::uint8_t*>(data);
	std::size_t written = 0;
	while (succeeded && written < static_cast<std::size_t>(length))
	{
		const ssize_t currentWritten = write(
			descriptor, bytes + written,
			static_cast<std::size_t>(length) - written);
		if (currentWritten < 0 && errno == EINTR)
		{
			continue;
		}
		succeeded = currentWritten > 0;
		if (succeeded)
		{
			written += static_cast<std::size_t>(currentWritten);
		}
	}
	struct stat afterInformation = {};
	succeeded = succeeded &&
		fstat(descriptor, &afterInformation) == 0 &&
		S_ISREG(afterInformation.st_mode) &&
		afterInformation.st_nlink == 1 &&
		afterInformation.st_dev == information.st_dev &&
		afterInformation.st_ino == information.st_ino;
	close(descriptor);
	close(parentDescriptor);
#endif

	const NoFollowPathInformation currentInformation =
		inspectPathNoFollow(path);
	succeeded = succeeded &&
		currentInformation.kind == NoFollowPathKind::RegularFile &&
		currentInformation.linkCount == 1 &&
		physicalPathIdentitiesEqual(
			currentInformation.identity, openedIdentity) &&
		routingIsCurrent() &&
		currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot);
	if (succeeded && writtenIdentity != nullptr)
	{
		*writtenIdentity = openedIdentity;
	}
	return succeeded;
}

bool writeFullFileChecked(const std::filesystem::path& path, const void* data, int length)
{
	if (length < 0 || (data == nullptr && length > 0))
	{
		return false;
	}
	const std::string fullPath = pathToUtf8String(path);
	if (fullPath.empty() || !createWriteDirectory(pathToUtf8String(path.parent_path())))
	{
		return false;
	}

	SDL_IOStream* stream = SDL_IOFromFile(fullPath.c_str(), "wb");
	if (stream == nullptr)
	{
		GameLog::write("Can not open file(wb) %s\n", fullPath.c_str());
		return false;
	}
	bool succeeded = length == 0 ||
		SDL_WriteIO(stream, data, static_cast<std::size_t>(length)) == static_cast<std::size_t>(length);
	if (!SDL_CloseIO(stream))
	{
		succeeded = false;
	}
	return succeeded;
}

bool writeRoutedFileChecked(
	const RoutedResourcePath& route,
	File::EditorRunFileLayoutState routingState,
	uint64_t routingGeneration,
	const void* data,
	int length,
	CheckedWriteMode mode)
{
	const std::string fullPath =
		makeFullPath(route.root, route.relativePath);
	if (fullPath.empty())
	{
		return false;
	}
	const std::filesystem::path path =
		std::filesystem::u8path(fullPath).lexically_normal();
	if (routingState != File::EditorRunFileLayoutState::Valid)
	{
		if (mode == CheckedWriteMode::Truncate)
		{
			return writeFullFileChecked(path, data, length);
		}
		const std::string directory =
			pathToUtf8String(path.parent_path());
		if (!createWriteDirectory(directory))
		{
			return false;
		}
		SDL_IOStream* stream =
			SDL_IOFromFile(fullPath.c_str(), "ab");
		if (stream == nullptr)
		{
			return false;
		}
		const bool succeeded = length == 0 ||
			SDL_WriteIO(
				stream, data, static_cast<std::size_t>(length)) ==
				static_cast<std::size_t>(length);
		return SDL_CloseIO(stream) && succeeded;
	}

	const auto routingIsCurrent =
		[routingState, routingGeneration]()
		{
			return editorRunRoutingSnapshotIsCurrent(
				routingState, routingGeneration);
		};
	NativeDirectoryHandle parent;
	std::string leaf;
	if (!route.anchored ||
		!openAnchoredRouteParent(
			route,
			File::EditorRunFileOperationPhase::BeforeWriteRootOpen,
			true, parent, leaf))
	{
		return false;
	}
	return writeRelativeFileNoFollow(
		parent, leaf, routingIsCurrent,
		data, length, mode);
}

bool prepareEditorRunMutationDirectory(
	const RoutedResourcePath& route,
	File::EditorRunFileLayoutState routingState,
	uint64_t routingGeneration,
	const std::filesystem::path& directory,
	bool createIfMissing,
	std::filesystem::path& canonicalRoot,
	std::filesystem::path& canonicalDirectory,
	PhysicalPathIdentity& directoryIdentity)
{
	canonicalRoot.clear();
	canonicalDirectory.clear();
	directoryIdentity = {};
	const auto routingIsCurrent =
		[routingState, routingGeneration]()
		{
			return editorRunRoutingSnapshotIsCurrent(
				routingState, routingGeneration);
		};
	if (routingState != File::EditorRunFileLayoutState::Valid ||
		route.root.empty() || directory.empty() ||
		!routingIsCurrent())
	{
		return false;
	}

	NoFollowPathInformation information =
		inspectPathNoFollow(directory);
	if (information.kind == NoFollowPathKind::Missing && createIfMissing)
	{
		if (!createWriteDirectory(pathToUtf8String(directory)) ||
			!routingIsCurrent())
		{
			return false;
		}
		information = inspectPathNoFollow(directory);
	}
	if (information.kind != NoFollowPathKind::Directory)
	{
		return false;
	}

	std::error_code rootError;
	std::error_code directoryError;
	canonicalRoot = std::filesystem::canonical(
		std::filesystem::u8path(route.root), rootError);
	canonicalDirectory =
		std::filesystem::canonical(directory, directoryError);
	if (rootError || directoryError || canonicalRoot.empty() ||
		canonicalDirectory.empty() ||
		!hostPathContains(canonicalRoot, canonicalDirectory) ||
		!captureDirectoryPhysicalIdentity(
			directory, directoryIdentity) ||
		!physicalPathIdentitiesEqual(
			directoryIdentity, information.identity) ||
		!routingIsCurrent())
	{
		canonicalRoot.clear();
		canonicalDirectory.clear();
		directoryIdentity = {};
		return false;
	}
	return true;
}

bool removeEditorRunFileNoFollow(
	const std::filesystem::path& path,
	const std::filesystem::path& expectedCanonicalParent,
	const PhysicalPathIdentity& expectedParentIdentity,
	const std::filesystem::path& containmentRoot,
	const PhysicalPathIdentity& expectedFileIdentity,
	const std::function<bool()>& routingIsCurrent)
{
	if (!expectedFileIdentity.valid || !routingIsCurrent ||
		!routingIsCurrent() ||
		!currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot))
	{
		return false;
	}

	const NoFollowPathInformation initialInformation =
		inspectPathNoFollow(path);
	if (initialInformation.kind != NoFollowPathKind::RegularFile ||
		initialInformation.linkCount != 1 ||
		!physicalPathIdentitiesEqual(
			initialInformation.identity, expectedFileIdentity))
	{
		return false;
	}

	bool removed = false;
#if defined(_WIN32)
	const HANDLE handle = CreateFileW(
		path.c_str(),
		DELETE | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
		nullptr);
	if (handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	BY_HANDLE_FILE_INFORMATION information = {};
	PhysicalPathIdentity openedIdentity;
	if (GetFileInformationByHandle(handle, &information) != 0 &&
		(information.dwFileAttributes &
			(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
		information.nNumberOfLinks == 1 &&
		GetFileType(handle) == FILE_TYPE_DISK)
	{
		populatePhysicalPathIdentity(
			handle, information, openedIdentity);
	}
	const NoFollowPathInformation currentInformation =
		inspectPathNoFollow(path);
	if (openedIdentity.valid &&
		physicalPathIdentitiesEqual(
			openedIdentity, expectedFileIdentity) &&
		currentInformation.kind == NoFollowPathKind::RegularFile &&
		currentInformation.linkCount == 1 &&
		physicalPathIdentitiesEqual(
			currentInformation.identity, openedIdentity) &&
		routingIsCurrent() &&
		currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot))
	{
		FILE_DISPOSITION_INFO disposition = {};
		disposition.DeleteFile = TRUE;
		removed = SetFileInformationByHandle(
			handle, FileDispositionInfo,
			&disposition, sizeof(disposition)) != 0;
	}
	CloseHandle(handle);
#else
	int parentFlags = O_RDONLY | O_DIRECTORY;
#if defined(O_CLOEXEC)
	parentFlags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	parentFlags |= O_NOFOLLOW;
#endif
	const int parentDescriptor =
		open(path.parent_path().c_str(), parentFlags);
	if (parentDescriptor < 0)
	{
		return false;
	}
	struct stat parentInformation = {};
	if (fstat(parentDescriptor, &parentInformation) != 0 ||
		!S_ISDIR(parentInformation.st_mode) ||
		static_cast<std::uintmax_t>(parentInformation.st_dev) !=
			expectedParentIdentity.deviceOrVolume ||
		static_cast<std::uintmax_t>(parentInformation.st_ino) !=
			expectedParentIdentity.nodeLow)
	{
		close(parentDescriptor);
		return false;
	}

	int openFlags = O_RDONLY;
#if defined(O_CLOEXEC)
	openFlags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
	openFlags |= O_NOFOLLOW;
#endif
	const std::string leaf = path.filename().string();
	const int descriptor =
		openat(parentDescriptor, leaf.c_str(), openFlags);
	if (descriptor < 0)
	{
		close(parentDescriptor);
		return false;
	}
	struct stat information = {};
	struct stat pathInformation = {};
	PhysicalPathIdentity openedIdentity;
	if (fstat(descriptor, &information) == 0 &&
		S_ISREG(information.st_mode) &&
		information.st_nlink == 1)
	{
		openedIdentity.deviceOrVolume =
			static_cast<std::uint64_t>(information.st_dev);
		openedIdentity.nodeLow =
			static_cast<std::uint64_t>(information.st_ino);
		openedIdentity.linkCount =
			static_cast<std::uint64_t>(information.st_nlink);
		openedIdentity.valid = true;
	}
	const bool currentPathMatches =
		fstatat(parentDescriptor, leaf.c_str(), &pathInformation,
			AT_SYMLINK_NOFOLLOW) == 0 &&
		S_ISREG(pathInformation.st_mode) &&
		pathInformation.st_nlink == 1 &&
		openedIdentity.valid &&
		pathInformation.st_dev == information.st_dev &&
		pathInformation.st_ino == information.st_ino;
	if (currentPathMatches &&
		physicalPathIdentitiesEqual(
			openedIdentity, expectedFileIdentity) &&
		routingIsCurrent() &&
		currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot))
	{
		removed =
			unlinkat(parentDescriptor, leaf.c_str(), 0) == 0;
	}
	close(descriptor);
	close(parentDescriptor);
#endif

	return removed &&
		inspectPathNoFollow(path).kind == NoFollowPathKind::Missing &&
		routingIsCurrent() &&
		currentDirectoryMatchesPhysicalIdentity(
			path.parent_path(), expectedCanonicalParent,
			expectedParentIdentity, containmentRoot);
}

bool transactionPathExists(const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path,
	bool& exists)
{
	exists = false;
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty() ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		const NoFollowPathInformation information =
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf);
		if (information.kind == NoFollowPathKind::Missing)
		{
			return directoryCopyRoutingIsCurrent(paths);
		}
		if (information.kind != NoFollowPathKind::Directory &&
			(information.kind != NoFollowPathKind::RegularFile ||
				information.linkCount != 1))
		{
			return false;
		}
		exists = true;
		return directoryCopyRoutingIsCurrent(paths);
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(path.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (information.kind == NoFollowPathKind::Missing)
	{
		return directoryCopyRoutingIsCurrent(paths);
	}
	if (information.kind != NoFollowPathKind::Directory &&
		(information.kind != NoFollowPathKind::RegularFile ||
			information.linkCount != 1))
	{
		return false;
	}
	exists = true;
	return directoryCopyRoutingIsCurrent(paths);
}

bool transactionPathIsDirectory(const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path)
{
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		return !leaf.empty() &&
			directoryCopyRoutingIsCurrent(paths) &&
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf).kind ==
				NoFollowPathKind::Directory &&
			directoryCopyRoutingIsCurrent(paths);
	}
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	if (!hostPathsEqual(path.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	return information.kind == NoFollowPathKind::Directory &&
		directoryCopyRoutingIsCurrent(paths);
}

bool transactionPathIsRegularFile(const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path)
{
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty() ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		const NoFollowPathInformation information =
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf);
		return information.kind ==
				NoFollowPathKind::RegularFile &&
			information.linkCount == 1 &&
			directoryCopyRoutingIsCurrent(paths);
	}
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	if (!hostPathsEqual(path.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	return information.kind == NoFollowPathKind::RegularFile &&
		information.linkCount == 1 &&
		directoryCopyRoutingIsCurrent(paths);
}

bool transactionPathMatchesIdentity(
	const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path,
	NoFollowPathKind expectedKind,
	const PhysicalPathIdentity& expectedIdentity)
{
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty() || !expectedIdentity.valid ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		const NoFollowPathInformation information =
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf);
		return information.kind == expectedKind &&
			(expectedKind != NoFollowPathKind::RegularFile ||
				information.linkCount == 1) &&
			physicalPathIdentitiesEqual(
				information.identity, expectedIdentity) &&
			directoryCopyRoutingIsCurrent(paths);
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(path.parent_path(), paths.parent) ||
		!expectedIdentity.valid)
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	return information.kind == expectedKind &&
		(expectedKind != NoFollowPathKind::RegularFile ||
			information.linkCount == 1) &&
		physicalPathIdentitiesEqual(
			information.identity, expectedIdentity) &&
		directoryCopyRoutingIsCurrent(paths);
}

bool captureTransactionPathIdentity(
	const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path,
	NoFollowPathKind expectedKind,
	PhysicalPathIdentity& identity)
{
	identity = {};
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty() ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		const NoFollowPathInformation information =
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf);
		if (information.kind != expectedKind ||
			(expectedKind == NoFollowPathKind::RegularFile &&
				information.linkCount != 1) ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		identity = information.identity;
		return true;
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(path.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (information.kind != expectedKind ||
		(expectedKind == NoFollowPathKind::RegularFile &&
			information.linkCount != 1) ||
		!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	identity = information.identity;
	return true;
}

bool transactionDirectoryTreeIsSafe(
	const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& directory)
{
	if (paths.anchored)
	{
		const std::string leaf =
			transactionLeaf(paths, directory);
		if (leaf.empty() ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		NativeDirectoryHandle root;
		return openChildDirectoryNoFollow(
				paths.parentHandle, leaf, false, root) &&
			nativeDirectoryTreeIsSafe(root) &&
			directoryCopyRoutingIsCurrent(paths);
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(directory.parent_path(), paths.parent))
	{
		return false;
	}
	try
	{
		const NoFollowPathInformation rootInformation =
			inspectPathNoFollow(directory);
		if (rootInformation.kind != NoFollowPathKind::Directory)
		{
			return false;
		}
		std::error_code errorCode;
		std::filesystem::recursive_directory_iterator iterator(
			directory, std::filesystem::directory_options::none, errorCode);
		const std::filesystem::recursive_directory_iterator end;
		for (; !errorCode && iterator != end;
			iterator.increment(errorCode))
		{
			const NoFollowPathInformation information =
				inspectPathNoFollow(iterator->path());
			if (information.kind != NoFollowPathKind::Directory &&
				(information.kind != NoFollowPathKind::RegularFile ||
					information.linkCount != 1))
			{
				return false;
			}
		}
		return !errorCode && directoryCopyRoutingIsCurrent(paths);
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool openTransactionDirectory(
	const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path,
	const PhysicalPathIdentity& expectedIdentity,
	NativeDirectoryHandle& directory)
{
	directory.reset();
	if (!expectedIdentity.valid ||
		!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		PhysicalPathIdentity openedIdentity;
		return !leaf.empty() &&
			openChildDirectoryNoFollow(
				paths.parentHandle, leaf, false, directory) &&
			nativeHandleInformation(
				directory.get(), true, openedIdentity) &&
			physicalPathIdentitiesEqual(
				openedIdentity, expectedIdentity) &&
			directoryCopyRoutingIsCurrent(paths);
	}
	return hostPathsEqual(path.parent_path(), paths.parent) &&
		openAbsoluteDirectoryNoFollow(
			path, expectedIdentity, directory) &&
		directoryCopyRoutingIsCurrent(paths);
}

bool validatePreparedStagingDirectory(
	DirectoryCopyTransactionPaths& paths,
	const File::DirectoryCopyLimits& limits,
	const std::function<bool()>& cancellationIsRequested)
{
	NativeDirectoryHandle stagingDirectory;
	if (!openTransactionDirectory(
			paths, paths.staging,
			paths.stagingIdentity,
			stagingDirectory))
	{
		return false;
	}
	bool listed = false;
	bool fileCountLimitExceeded = false;
	const std::vector<std::string> sourceNames =
		listNativeDirectoryNames(
			stagingDirectory,
			&listed,
			limits.maximumFileCount,
			&fileCountLimitExceeded);
	if (!listed || fileCountLimitExceeded)
	{
		return false;
	}

	std::unordered_map<std::string, std::string> namesByKey;
	namesByKey.reserve(sourceNames.size());
	std::uint64_t totalBytes = 0;
	for (const std::string& fileName : sourceNames)
	{
		if (cancellationIsRequested() ||
			!isValidUtf8(fileName))
		{
			return false;
		}
		const std::string key =
			normalizeFileNameKey(fileName);
		const auto inserted = namesByKey.emplace(
			key, fileName);
		if (!inserted.second &&
			inserted.first->second != fileName)
		{
			return false;
		}

		NativeFileHandle file;
		std::uint64_t fileSize = 0;
		if (!openChildFileNoFollow(
				stagingDirectory, fileName,
				false, false, false, false, file) ||
			!nativeRegularFileSize(file, fileSize) ||
			(limits.maximumSingleFileBytes > 0 &&
			 fileSize > static_cast<std::uint64_t>(
				limits.maximumSingleFileBytes)) ||
			(limits.maximumTotalBytes > 0 &&
			 (totalBytes > limits.maximumTotalBytes ||
			  fileSize > limits.maximumTotalBytes -
				totalBytes)))
		{
			return false;
		}
		totalBytes += fileSize;
	}

	return !cancellationIsRequested() &&
		transactionPathMatchesIdentity(
			paths, paths.staging,
			NoFollowPathKind::Directory,
			paths.stagingIdentity);
}

bool removeTransactionPathRecursively(
	const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path)
{
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty())
		{
			return false;
		}
		return removeNativeChildRecursively(
			paths.parentHandle, leaf,
			[&paths]()
			{
				return directoryCopyRoutingIsCurrent(paths);
			});
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(path.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (information.kind == NoFollowPathKind::Missing)
	{
		return directoryCopyRoutingIsCurrent(paths);
	}
	if (information.kind == NoFollowPathKind::Directory)
	{
		if (!transactionDirectoryTreeIsSafe(paths, path))
		{
			return false;
		}
	}
	else if (information.kind != NoFollowPathKind::RegularFile ||
		information.linkCount != 1)
	{
		return false;
	}
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	std::error_code errorCode;
	std::filesystem::remove_all(path, errorCode);
	const NoFollowPathInformation after = inspectPathNoFollow(path);
	return !errorCode &&
		after.kind == NoFollowPathKind::Missing &&
		directoryCopyRoutingIsCurrent(paths);
}

bool renameTransactionPath(const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& source,
	const std::filesystem::path& destination)
{
	if (paths.anchored)
	{
		const std::string sourceLeaf =
			transactionLeaf(paths, source);
		const std::string destinationLeaf =
			transactionLeaf(paths, destination);
		if (sourceLeaf.empty() || destinationLeaf.empty() ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		const NoFollowPathInformation sourceInformation =
			inspectNativeChildNoFollow(
				paths.parentHandle, sourceLeaf);
		if ((sourceInformation.kind !=
				NoFollowPathKind::Directory &&
			 (sourceInformation.kind !=
				NoFollowPathKind::RegularFile ||
			  sourceInformation.linkCount != 1)) ||
			inspectNativeChildNoFollow(
				paths.parentHandle, destinationLeaf).kind !=
				NoFollowPathKind::Missing)
		{
			return false;
		}

#if defined(_WIN32)
		NativeDirectoryHandle sourceDirectory;
		NativeFileHandle sourceFile;
		NativePathHandle sourceHandle =
			InvalidNativePathHandle;
		if (sourceInformation.kind ==
			NoFollowPathKind::Directory)
		{
			if (!openChildDirectoryForDelete(
					paths.parentHandle, sourceLeaf,
					sourceDirectory) ||
				!nativeDirectoryTreeIsSafe(sourceDirectory))
			{
				return false;
			}
			sourceHandle = sourceDirectory.get();
		}
		else
		{
			if (!openChildFileNoFollow(
					paths.parentHandle, sourceLeaf,
					false, false, false, true,
					sourceFile))
			{
				return false;
			}
			sourceHandle = sourceFile.get();
		}
		PhysicalPathIdentity openedIdentity;
		if (!nativeHandleInformation(
				sourceHandle,
				sourceInformation.kind ==
					NoFollowPathKind::Directory,
				openedIdentity) ||
			!physicalPathIdentitiesEqual(
				openedIdentity, sourceInformation.identity) ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		const std::wstring destinationName =
			std::filesystem::u8path(
				destinationLeaf).wstring();
		const std::size_t nameBytes =
			destinationName.size() * sizeof(wchar_t);
		if (nameBytes >
			(std::numeric_limits<DWORD>::max)())
		{
			return false;
		}
		const std::size_t renameInformationSize =
			sizeof(FILE_RENAME_INFO) + nameBytes;
		std::vector<std::uint64_t> renameStorage(
			(renameInformationSize +
				sizeof(std::uint64_t) - 1) /
				sizeof(std::uint64_t),
			0);
		auto* renameInformation =
			reinterpret_cast<FILE_RENAME_INFO*>(
				renameStorage.data());
		renameInformation->ReplaceIfExists = FALSE;
		renameInformation->RootDirectory =
			paths.parentHandle.get();
		renameInformation->FileNameLength =
			static_cast<DWORD>(nameBytes);
		std::memcpy(
			renameInformation->FileName,
			destinationName.data(), nameBytes);
		invokeEditorRunFileOperationTestHook(
			File::EditorRunFileOperationPhase::
				BeforeTransactionMutation);
		PhysicalPathIdentity mutationIdentity;
		if (!directoryCopyRoutingIsCurrent(paths) ||
			inspectNativeChildNoFollow(
				paths.parentHandle, destinationLeaf).kind !=
				NoFollowPathKind::Missing ||
			!nativeHandleInformation(
				sourceHandle,
				sourceInformation.kind ==
					NoFollowPathKind::Directory,
				mutationIdentity) ||
			!physicalPathIdentitiesEqual(
				mutationIdentity,
				sourceInformation.identity) ||
			(sourceInformation.kind ==
					NoFollowPathKind::Directory &&
			 !nativeDirectoryTreeIsSafe(
				 sourceDirectory)))
		{
			return false;
		}
		IO_STATUS_BLOCK renameStatus = {};
		const NtSetInformationFileFunction setInformation =
			nativeNtSetInformationFile();
		const NTSTATUS status =
			setInformation != nullptr
			? setInformation(
				sourceHandle, &renameStatus,
				renameInformation,
				static_cast<ULONG>(
					renameInformationSize),
				static_cast<FILE_INFORMATION_CLASS>(10))
			: static_cast<NTSTATUS>(0xC0000002u);
		const bool renamed = status >= 0;
		if (!renamed)
		{
			GameLog::write(
				"Relative transaction rename failed with NT status 0x%08lx\n",
				static_cast<unsigned long>(status));
		}
		sourceDirectory.reset();
		sourceFile.reset();
#else
		NativeDirectoryHandle sourceDirectory;
		if (sourceInformation.kind ==
			NoFollowPathKind::Directory)
		{
			if (!openChildDirectoryNoFollow(
					paths.parentHandle, sourceLeaf,
					false, sourceDirectory) ||
				!nativeDirectoryTreeIsSafe(
					sourceDirectory))
			{
				return false;
			}
		}
		invokeEditorRunFileOperationTestHook(
			File::EditorRunFileOperationPhase::
				BeforeTransactionMutation);
		const NoFollowPathInformation currentSource =
			inspectNativeChildNoFollow(
				paths.parentHandle, sourceLeaf);
		const bool renamed =
			directoryCopyRoutingIsCurrent(paths) &&
			currentSource.kind == sourceInformation.kind &&
			physicalPathIdentitiesEqual(
				currentSource.identity,
				sourceInformation.identity) &&
			inspectNativeChildNoFollow(
				paths.parentHandle,
				destinationLeaf).kind ==
				NoFollowPathKind::Missing &&
			(sourceInformation.kind !=
					NoFollowPathKind::Directory ||
			 nativeDirectoryTreeIsSafe(
				 sourceDirectory)) &&
			renameat(
				paths.parentHandle.get(),
				sourceLeaf.c_str(),
				paths.parentHandle.get(),
				destinationLeaf.c_str()) == 0;
#endif
		const NoFollowPathInformation destinationInformation =
			inspectNativeChildNoFollow(
				paths.parentHandle, destinationLeaf);
		return renamed &&
			physicalPathIdentitiesEqual(
				sourceInformation.identity,
				destinationInformation.identity) &&
			inspectNativeChildNoFollow(
				paths.parentHandle, sourceLeaf).kind ==
				NoFollowPathKind::Missing &&
			directoryCopyRoutingIsCurrent(paths);
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(source.parent_path(), paths.parent) ||
		!hostPathsEqual(destination.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation sourceInformation =
		inspectPathNoFollow(source);
	const NoFollowPathInformation destinationInformation =
		inspectPathNoFollow(destination);
	if ((sourceInformation.kind != NoFollowPathKind::Directory &&
			(sourceInformation.kind != NoFollowPathKind::RegularFile ||
				sourceInformation.linkCount != 1)) ||
		destinationInformation.kind != NoFollowPathKind::Missing)
	{
		return false;
	}
	if (sourceInformation.kind == NoFollowPathKind::Directory &&
		!transactionDirectoryTreeIsSafe(paths, source))
	{
		return false;
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!renamePath(source, destination))
	{
		return false;
	}
	const NoFollowPathInformation renamedInformation =
		inspectPathNoFollow(destination);
	return physicalPathIdentitiesEqual(
			sourceInformation.identity, renamedInformation.identity) &&
		inspectPathNoFollow(source).kind == NoFollowPathKind::Missing &&
		directoryCopyRoutingIsCurrent(paths);
}

bool createTransactionDirectory(DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path)
{
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty() ||
			!directoryCopyRoutingIsCurrent(paths) ||
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf).kind !=
				NoFollowPathKind::Missing)
		{
			return false;
		}
		invokeEditorRunFileOperationTestHook(
			File::EditorRunFileOperationPhase::
				BeforeTransactionMutation);
		NativeDirectoryHandle createdDirectory;
		bool created = false;
		PhysicalPathIdentity identity;
		if (!openChildDirectoryNoFollow(
				paths.parentHandle, leaf, true,
				createdDirectory, &created) ||
			!created ||
			!nativeHandleInformation(
				createdDirectory.get(), true, identity) ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			return false;
		}
		if (hostPathsEqual(path, paths.staging))
		{
			paths.stagingIdentity = identity;
		}
		return true;
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(path.parent_path(), paths.parent) ||
		inspectPathNoFollow(path).kind != NoFollowPathKind::Missing)
	{
		return false;
	}
	std::error_code errorCode;
	const bool created = std::filesystem::create_directory(path, errorCode);
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (!created || errorCode ||
		information.kind != NoFollowPathKind::Directory ||
		!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	if (hostPathsEqual(path, paths.staging))
	{
		paths.stagingIdentity = information.identity;
	}
	return true;
}

bool writeTransactionFileChecked(
	DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path,
	const void* data,
	int length)
{
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	if (paths.anchored)
	{
		NativeDirectoryHandle stagingDirectory;
		const NativeDirectoryHandle* parent =
			&paths.parentHandle;
		std::string leaf;
		if (hostPathsEqual(path.parent_path(), paths.staging))
		{
			if (!paths.stagingIdentity.valid ||
				!openChildDirectoryNoFollow(
					paths.parentHandle,
					paths.stagingLeaf, false,
					stagingDirectory))
			{
				return false;
			}
			PhysicalPathIdentity stagingIdentity;
			if (!nativeHandleInformation(
					stagingDirectory.get(), true,
					stagingIdentity) ||
				!physicalPathIdentitiesEqual(
					stagingIdentity,
					paths.stagingIdentity))
			{
				return false;
			}
			parent = &stagingDirectory;
			leaf = pathToUtf8String(path.filename());
		}
		else if (hostPathsEqual(
				path.parent_path(), paths.parent))
		{
			leaf = transactionLeaf(paths, path);
		}
		if (leaf.empty())
		{
			return false;
		}
		PhysicalPathIdentity identity;
		if (!writeRelativeFileNoFollow(
				*parent, leaf,
				[&paths]()
				{
					return directoryCopyRoutingIsCurrent(
						paths);
				},
				data, length, CheckedWriteMode::Truncate,
				&identity))
		{
			return false;
		}
		if (hostPathsEqual(path, paths.ready))
		{
			paths.readyIdentity = identity;
		}
		return true;
	}
	std::filesystem::path expectedCanonicalParent;
	PhysicalPathIdentity expectedParentIdentity;
	if (hostPathsEqual(path.parent_path(), paths.staging))
	{
		if (!paths.stagingIdentity.valid)
		{
			return false;
		}
		std::error_code errorCode;
		expectedCanonicalParent =
			std::filesystem::canonical(paths.staging, errorCode);
		if (errorCode || expectedCanonicalParent.empty())
		{
			return false;
		}
		expectedParentIdentity = paths.stagingIdentity;
	}
	else if (hostPathsEqual(path.parent_path(), paths.parent))
	{
		expectedCanonicalParent = paths.canonicalParent;
		expectedParentIdentity = paths.parentIdentity;
	}
	else
	{
		return false;
	}
	const auto routingIsCurrent =
		[&paths]()
		{
			return directoryCopyRoutingIsCurrent(paths);
		};
	PhysicalPathIdentity identity;
	if (!writeFileNoFollow(
			path, expectedCanonicalParent, expectedParentIdentity,
			paths.routingRoot, routingIsCurrent, data, length,
			CheckedWriteMode::Truncate, &identity))
	{
		return false;
	}
	if (hostPathsEqual(path, paths.ready))
	{
		paths.readyIdentity = identity;
	}
	return true;
}

bool removeTransactionFile(const DirectoryCopyTransactionPaths& paths,
	const std::filesystem::path& path)
{
	if (paths.anchored)
	{
		const std::string leaf = transactionLeaf(paths, path);
		if (leaf.empty())
		{
			return false;
		}
		const NoFollowPathInformation information =
			inspectNativeChildNoFollow(
				paths.parentHandle, leaf);
		if (information.kind != NoFollowPathKind::RegularFile ||
			information.linkCount != 1)
		{
			return false;
		}
		return removeNativeChildRecursively(
			paths.parentHandle, leaf,
			[&paths]()
			{
				return directoryCopyRoutingIsCurrent(paths);
			});
	}
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!hostPathsEqual(path.parent_path(), paths.parent))
	{
		return false;
	}
	const NoFollowPathInformation information =
		inspectPathNoFollow(path);
	if (information.kind != NoFollowPathKind::RegularFile ||
		information.linkCount != 1)
	{
		return false;
	}
	std::error_code errorCode;
	const bool removed = std::filesystem::remove(path, errorCode);
	return !errorCode && removed &&
		inspectPathNoFollow(path).kind == NoFollowPathKind::Missing &&
		directoryCopyRoutingIsCurrent(paths);
}

bool recoverDirectoryCopyUnlocked(const DirectoryCopyTransactionPaths& paths)
{
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	bool destinationExists = false;
	bool backupExists = false;
	bool stagingExists = false;
	bool readyExists = false;
	if (!transactionPathExists(paths, paths.destination, destinationExists) ||
		!transactionPathExists(paths, paths.backup, backupExists) ||
		!transactionPathExists(paths, paths.staging, stagingExists) ||
		!transactionPathExists(paths, paths.ready, readyExists) ||
		(destinationExists &&
			!transactionPathIsDirectory(paths, paths.destination)) ||
		(backupExists &&
			!transactionPathIsDirectory(paths, paths.backup)) ||
		(stagingExists &&
			!transactionPathIsDirectory(paths, paths.staging)) ||
		(readyExists &&
			!transactionPathIsRegularFile(paths, paths.ready)))
	{
		return false;
	}

	if (destinationExists)
	{
		return removeTransactionPathRecursively(paths, paths.staging) &&
			removeTransactionPathRecursively(paths, paths.backup) &&
			removeTransactionPathRecursively(paths, paths.ready);
	}

	if (backupExists)
	{
		if (!renameTransactionPath(paths, paths.backup, paths.destination))
		{
			GameLog::write("Directory copy recovery could not restore backup\n");
			return false;
		}
		if (!removeTransactionPathRecursively(
				paths, paths.staging))
		{
			GameLog::write(
				"Directory copy recovery could not clean staging after backup restore\n");
			return false;
		}
		if (!removeTransactionPathRecursively(
				paths, paths.ready))
		{
			GameLog::write(
				"Directory copy recovery could not clean ready marker after backup restore\n");
			return false;
		}
		return true;
	}

	if (!stagingExists)
	{
		return removeTransactionPathRecursively(paths, paths.ready);
	}

	if (!readyExists)
	{
		return removeTransactionPathRecursively(paths, paths.staging) &&
			removeTransactionPathRecursively(paths, paths.ready);
	}
	if (!removeTransactionFile(paths, paths.ready))
	{
		return false;
	}
	return renameTransactionPath(paths, paths.staging, paths.destination);
}

bool publishStagedDirectoryTransaction(
	DirectoryCopyTransactionPaths& paths,
	const File::DirectoryCopyFailureInjector& failureInjector,
	const std::function<bool()>& cancellationIsRequested,
	const std::function<bool()>& rollbackStaging)
{
	const auto failTransaction =
		[](const char* stage)
		{
			GameLog::write(
				"Directory transaction failed at %s\n",
				stage);
			return false;
		};
	const auto rollbackUnpublishedStaging =
		[&rollbackStaging]()
		{
			const bool restored =
				rollbackStaging && rollbackStaging();
			if (!restored)
			{
				GameLog::write(
					"Directory transaction could not roll back staging; recovery artifacts were preserved\n");
			}
			return restored;
		};

	const char ready[] = "ready";
	if (!writeTransactionFileChecked(
			paths, paths.ready,
			ready,
			static_cast<int>(sizeof(ready) - 1)))
	{
		(void)rollbackUnpublishedStaging();
		(void)removeTransactionPathRecursively(
			paths, paths.ready);
		return failTransaction("ready write");
	}
	if (cancellationIsRequested())
	{
		(void)removeTransactionPathRecursively(
			paths, paths.ready);
		(void)rollbackUnpublishedStaging();
		return failTransaction("cancellation");
	}
	const bool failBeforeBackup = failureInjector &&
		failureInjector(
			File::DirectoryCopyPhase::BeforeBackup);
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!transactionPathMatchesIdentity(
			paths, paths.staging,
			NoFollowPathKind::Directory,
			paths.stagingIdentity) ||
		!transactionDirectoryTreeIsSafe(
			paths, paths.staging) ||
		!transactionPathMatchesIdentity(
			paths, paths.ready,
			NoFollowPathKind::RegularFile,
			paths.readyIdentity) ||
		failBeforeBackup)
	{
		(void)removeTransactionPathRecursively(
			paths, paths.ready);
		(void)rollbackUnpublishedStaging();
		return failTransaction("pre-backup validation");
	}
	if (!removeTransactionFile(paths, paths.ready))
	{
		(void)rollbackUnpublishedStaging();
		return failTransaction("ready removal");
	}

	bool hadDestination = false;
	bool backupExistsBeforeBackup = false;
	if (!transactionPathExists(
			paths, paths.destination, hadDestination) ||
		!transactionPathExists(
			paths, paths.backup,
			backupExistsBeforeBackup) ||
		backupExistsBeforeBackup)
	{
		(void)rollbackUnpublishedStaging();
		return failTransaction("destination probe");
	}
	if (hadDestination &&
		!captureTransactionPathIdentity(
			paths, paths.destination,
			NoFollowPathKind::Directory,
			paths.destinationIdentity))
	{
		(void)rollbackUnpublishedStaging();
		return failTransaction("destination identity");
	}
	if (hadDestination &&
		!renameTransactionPath(
			paths, paths.destination,
			paths.backup))
	{
		(void)rollbackUnpublishedStaging();
		return failTransaction("destination backup");
	}
	if (hadDestination &&
		(!captureTransactionPathIdentity(
			paths, paths.backup,
			NoFollowPathKind::Directory,
			paths.backupIdentity) ||
		 !physicalPathIdentitiesEqual(
			paths.destinationIdentity,
			paths.backupIdentity)))
	{
		return failTransaction("backup identity");
	}

	const auto restoreBackup =
		[&paths, hadDestination]()
		{
			if (!hadDestination)
			{
				return true;
			}
			bool destinationExists = false;
			return transactionPathMatchesIdentity(
					paths, paths.backup,
					NoFollowPathKind::Directory,
					paths.backupIdentity) &&
				transactionPathExists(
					paths, paths.destination,
					destinationExists) &&
				!destinationExists &&
				renameTransactionPath(
					paths, paths.backup,
					paths.destination) &&
				transactionPathMatchesIdentity(
					paths, paths.destination,
					NoFollowPathKind::Directory,
					paths.destinationIdentity);
		};
	const bool failBeforePublish = failureInjector &&
		failureInjector(
			File::DirectoryCopyPhase::BeforePublish);
	bool destinationExistsBeforePublish = false;
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!transactionPathMatchesIdentity(
			paths, paths.staging,
			NoFollowPathKind::Directory,
			paths.stagingIdentity) ||
		!transactionDirectoryTreeIsSafe(
			paths, paths.staging) ||
		(hadDestination &&
		 !transactionPathMatchesIdentity(
			paths, paths.backup,
			NoFollowPathKind::Directory,
			paths.backupIdentity)) ||
		!transactionPathExists(
			paths, paths.destination,
			destinationExistsBeforePublish) ||
		destinationExistsBeforePublish ||
		failBeforePublish ||
		!renameTransactionPath(
			paths, paths.staging,
			paths.destination))
	{
		const bool backupRestored = restoreBackup();
		const bool stagingRestored =
			rollbackUnpublishedStaging();
		if (!backupRestored || !stagingRestored)
		{
			GameLog::write(
				"Directory transaction rollback was incomplete; recovery artifacts were preserved\n");
		}
		return failTransaction("staging publication");
	}
	if (!transactionPathMatchesIdentity(
			paths, paths.destination,
			NoFollowPathKind::Directory,
			paths.stagingIdentity) ||
		(hadDestination &&
		 !transactionPathMatchesIdentity(
			paths, paths.backup,
			NoFollowPathKind::Directory,
			paths.backupIdentity)))
	{
		return failTransaction("published identity");
	}
	if (hadDestination &&
		!removeTransactionPathRecursively(
			paths, paths.backup))
	{
		GameLog::write(
			"Directory published; stale backup will be cleaned on recovery\n");
	}
	return directoryCopyRoutingIsCurrent(paths);
}

}

struct File::EditorRunFileLayoutUse::State
{
	explicit State(uint64_t expectedGeneration)
		: lifecycleLock(g_editorRunFileLayoutLifecycleMutex)
	{
		InstalledEditorRunFileLayout layout;
		uint64_t currentGeneration = 0;
		current =
			expectedGeneration != 0 &&
			getInstalledEditorRunFileLayout(
				layout, &currentGeneration) ==
				File::EditorRunFileLayoutState::Valid &&
			currentGeneration == expectedGeneration;
		if (!current)
		{
			lifecycleLock.unlock();
		}
	}

	std::shared_lock<std::shared_mutex> lifecycleLock;
	bool current = false;
};

File::EditorRunFileLayoutUse::EditorRunFileLayoutUse(
	uint64_t generation)
	: state(std::make_unique<State>(generation))
{
}

File::EditorRunFileLayoutUse::~EditorRunFileLayoutUse() = default;

bool File::EditorRunFileLayoutUse::valid() const
{
	return state != nullptr && state->current;
}

bool File::isSafeResourcePath(const std::string& fileName)
{
	return ResourcePathSafety::isSafeVirtualResourcePath(fileName);
}

std::string File::sanitizeSaveNamespace(const std::string& saveNamespace)
{
	return sanitizeNamespaceValue(saveNamespace);
}

namespace
{
bool installEditorRunFileLayoutImplementation(
	const File::EditorRunFileLayout& layout,
	const File::EditorRunFileLayoutIdentityProof* proof)
{
	InstalledEditorRunFileLayout normalizedLayout;
	if (!validateEditorRunFileLayout(
			layout, proof, normalizedLayout))
	{
		return false;
	}
	if (!installedEditorRunFileLayoutIsCurrent(
			normalizedLayout))
	{
		return false;
	}

	std::unique_lock<std::shared_mutex> lifecycleLock(
		g_editorRunFileLayoutLifecycleMutex);
	uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(g_editorRunFileLayoutMutex);
		if (g_editorRunFileLayout)
		{
			return false;
		}
		g_editorRunFileLayout = std::move(normalizedLayout);
		g_editorRunFileLayoutGeneration++;
		if (g_editorRunFileLayoutGeneration == 0)
		{
			g_editorRunFileLayoutGeneration++;
		}
		generation = g_editorRunFileLayoutGeneration;
	}
	GameLog::editorRunFileLayoutGenerationChanged(generation);
	return true;
}
}

bool File::installEditorRunFileLayout(
	const EditorRunFileLayout& layout,
	const EditorRunFileLayoutIdentityProof& proof)
{
	return installEditorRunFileLayoutImplementation(
		layout, &proof);
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
bool File::installEditorRunFileLayoutForTests(
	const EditorRunFileLayout& layout)
{
	return installEditorRunFileLayoutImplementation(
		layout, nullptr);
}
#endif

void File::resetEditorRunFileLayout()
{
	std::unique_lock<std::shared_mutex> lifecycleLock(
		g_editorRunFileLayoutLifecycleMutex);
	std::vector<EditorRunFileLayoutResetHook> resetHooks;
	{
		std::lock_guard<std::mutex> lock(
			g_editorRunFileLayoutResetHookMutex);
		resetHooks.reserve(g_editorRunFileLayoutResetHooks.size());
		for (const auto& entry : g_editorRunFileLayoutResetHooks)
		{
			resetHooks.push_back(entry.second);
		}
	}
	for (const EditorRunFileLayoutResetHook& hook : resetHooks)
	{
		try
		{
			hook();
		}
		catch (...)
		{
			// Teardown must still invalidate the generation and close GameLog
			// if an internal sink callback unexpectedly throws.
		}
	}

	uint64_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(g_editorRunFileLayoutMutex);
		g_editorRunFileLayout.reset();
		g_editorRunFileLayoutGeneration++;
		if (g_editorRunFileLayoutGeneration == 0)
		{
			g_editorRunFileLayoutGeneration++;
		}
		generation = g_editorRunFileLayoutGeneration;
	}
	GameLog::editorRunFileLayoutGenerationChanged(generation);
}

bool File::hasEditorRunFileLayout()
{
	std::lock_guard<std::mutex> lock(g_editorRunFileLayoutMutex);
	return g_editorRunFileLayout.has_value();
}

File::EditorRunFileLayoutState File::getEditorRunLogPath(
	std::string& logPath)
{
	uint64_t generation = 0;
	return getEditorRunLogPath(logPath, generation);
}

File::EditorRunFileLayoutState File::getEditorRunLogPath(
	std::string& logPath,
	uint64_t& generation)
{
	InstalledEditorRunFileLayout layout;
	const EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (state != EditorRunFileLayoutState::Valid)
	{
		logPath.clear();
		return state;
	}
	logPath = layout.logPath;
	return EditorRunFileLayoutState::Valid;
}

File::EditorRunFileLayoutState File::getEditorRunDiagnosticsPath(
	std::string& diagnosticsPath)
{
	uint64_t generation = 0;
	return getEditorRunDiagnosticsPath(
		diagnosticsPath, generation);
}

File::EditorRunFileLayoutState File::getEditorRunDiagnosticsPath(
	std::string& diagnosticsPath,
	uint64_t& generation)
{
	InstalledEditorRunFileLayout layout;
	const EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(layout, &generation);
	if (state != EditorRunFileLayoutState::Valid)
	{
		diagnosticsPath.clear();
		return state;
	}
	diagnosticsPath = layout.diagnosticsPath;
	return EditorRunFileLayoutState::Valid;
}

File::EditorRunFileLayoutState File::getEditorRunRuntimeTracePath(
	std::string& runtimeTracePath)
{
	uint64_t generation = 0;
	return getEditorRunRuntimeTracePath(
		runtimeTracePath, generation);
}

File::EditorRunFileLayoutState File::getEditorRunRuntimeTracePath(
	std::string& runtimeTracePath,
	uint64_t& generation)
{
	InstalledEditorRunFileLayout layout;
	const EditorRunFileLayoutState state =
		getInstalledEditorRunFileLayout(
			layout, &generation);
	if (state != EditorRunFileLayoutState::Valid)
	{
		runtimeTracePath.clear();
		return state;
	}
	runtimeTracePath = layout.runtimeTracePath;
	return EditorRunFileLayoutState::Valid;
}

uint64_t File::addEditorRunFileLayoutResetHook(
	const EditorRunFileLayoutResetHook& hook)
{
	if (!hook)
	{
		return 0;
	}

	std::lock_guard<std::mutex> lock(
		g_editorRunFileLayoutResetHookMutex);
	uint64_t hookId = 0;
	do
	{
		g_editorRunFileLayoutResetHookId++;
		if (g_editorRunFileLayoutResetHookId == 0)
		{
			g_editorRunFileLayoutResetHookId++;
		}
		hookId = g_editorRunFileLayoutResetHookId;
	}
	while (std::any_of(
		g_editorRunFileLayoutResetHooks.begin(),
		g_editorRunFileLayoutResetHooks.end(),
		[hookId](const auto& entry)
		{
			return entry.first == hookId;
		}));
	g_editorRunFileLayoutResetHooks.emplace_back(hookId, hook);
	return hookId;
}

void File::removeEditorRunFileLayoutResetHook(uint64_t hookId)
{
	if (hookId == 0)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(
		g_editorRunFileLayoutResetHookMutex);
	g_editorRunFileLayoutResetHooks.erase(
		std::remove_if(
			g_editorRunFileLayoutResetHooks.begin(),
			g_editorRunFileLayoutResetHooks.end(),
			[hookId](const auto& entry)
			{
				return entry.first == hookId;
			}),
		g_editorRunFileLayoutResetHooks.end());
}

bool File::openEditorRunLog(
	const std::string& logPath,
	uint64_t generation,
	std::FILE*& file,
	std::intptr_t& parentToken)
{
	return openEditorRunExactOutput(
		logPath, generation, file, parentToken,
		EditorRunExactOutputKind::Log);
}

bool File::editorRunLogHandleIsCurrent(
	std::FILE* file,
	std::intptr_t parentToken,
	const std::string& logPath,
	uint64_t generation)
{
	return editorRunExactOutputHandleIsCurrent(
		file, parentToken, logPath, generation,
		EditorRunExactOutputKind::Log);
}

void File::closeEditorRunLogParent(
	std::intptr_t parentToken)
{
	closeEditorRunExactOutputParent(parentToken);
}

bool File::openEditorRunDiagnostics(
	const std::string& diagnosticsPath,
	uint64_t generation,
	std::FILE*& file,
	std::intptr_t& parentToken)
{
	return openEditorRunExactOutput(
		diagnosticsPath, generation, file, parentToken,
		EditorRunExactOutputKind::Diagnostics);
}

bool File::editorRunDiagnosticsHandleIsCurrent(
	std::FILE* file,
	std::intptr_t parentToken,
	const std::string& diagnosticsPath,
	uint64_t generation)
{
	return editorRunExactOutputHandleIsCurrent(
		file, parentToken, diagnosticsPath, generation,
		EditorRunExactOutputKind::Diagnostics);
}

void File::closeEditorRunDiagnosticsParent(
	std::intptr_t parentToken)
{
	closeEditorRunExactOutputParent(parentToken);
}

bool File::openEditorRunRuntimeTrace(
	const std::string& runtimeTracePath,
	uint64_t generation,
	std::FILE*& file,
	std::intptr_t& parentToken)
{
	return openEditorRunExactOutput(
		runtimeTracePath, generation, file, parentToken,
		EditorRunExactOutputKind::RuntimeTrace);
}

bool File::editorRunRuntimeTraceHandleIsCurrent(
	std::FILE* file,
	std::intptr_t parentToken,
	const std::string& runtimeTracePath,
	uint64_t generation)
{
	return editorRunExactOutputHandleIsCurrent(
		file, parentToken, runtimeTracePath, generation,
		EditorRunExactOutputKind::RuntimeTrace);
}

void File::closeEditorRunRuntimeTraceParent(
	std::intptr_t parentToken)
{
	closeEditorRunExactOutputParent(parentToken);
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
void File::setEditorRunFileOperationTestHook(
	const EditorRunFileOperationTestHook& hook)
{
	std::lock_guard<std::mutex> lock(
		g_editorRunFileOperationTestHookMutex);
	g_editorRunFileOperationTestHook = hook;
}

bool File::editorRunFileLayoutResetLockIsAvailableForTests()
{
	if (!g_editorRunFileLayoutLifecycleMutex.try_lock())
	{
		return false;
	}
	g_editorRunFileLayoutLifecycleMutex.unlock();
	return true;
}

void File::setSharedApplicationRootForTests(
	const std::string& root)
{
	std::lock_guard<std::mutex> lock(
		g_sharedApplicationRootOverrideMutex);
	g_sharedApplicationRootUnavailableForTests = false;
	g_sharedApplicationRootOverrideForTests =
		root.empty() ? std::string() : normalizeRoot(root);
}

void File::setSharedApplicationRootUnavailableForTests(
	bool unavailable)
{
	std::lock_guard<std::mutex> lock(
		g_sharedApplicationRootOverrideMutex);
	g_sharedApplicationRootUnavailableForTests = unavailable;
}

void File::setPlatformStateParentForTests(
	const std::string& root)
{
	std::lock_guard<std::mutex> lock(
		g_platformStateParentOverrideMutex);
	g_platformStateParentOverrideForTests =
		root.empty() ? std::string() : normalizeRoot(root);
}
#endif

bool File::configureUserDataRoot(
	const std::string& userDataRoot,
	const std::string& assetsRoot)
{
	const std::string resolvedRoot =
		buildUserDataRoot(userDataRoot, assetsRoot);
	if (resolvedRoot.empty())
	{
		return false;
	}
	try
	{
		std::error_code error;
		const std::filesystem::path path =
			std::filesystem::u8path(resolvedRoot).
				lexically_normal();
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(path, error);
		if (error &&
			error != std::errc::no_such_file_or_directory)
		{
			return false;
		}
		if (!error && std::filesystem::exists(status))
		{
			if (!std::filesystem::is_directory(path, error) || error)
			{
				return false;
			}
		}
		else
		{
			error.clear();
			if (!std::filesystem::create_directories(path, error) &&
				(error || !std::filesystem::is_directory(path)))
			{
				return false;
			}
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(
			g_platformStateParentOverrideMutex);
		g_configuredUserDataRoot = resolvedRoot;
	}
	return true;
}

std::string File::getUserDataRoot()
{
	return buildPlatformStateParent();
}

bool File::fileExist(const std::string& fileName)
{
    if (!isSafeResourcePath(fileName))
    {
        return false;
    }
    for (const auto& route : buildReadRoutes(fileName))
    {
        if (!route.anchored)
        {
            const std::string fullPath =
                makeRoutedReadPath(route);
            if (pathExists(fullPath))
            {
                return true;
            }
            const std::string caseInsensitivePath =
                resolveCaseInsensitiveExistingPath(fullPath);
            if (!caseInsensitivePath.empty() &&
                routedReadPathIsContained(
                    route, caseInsensitivePath))
            {
                return true;
            }
            const std::string aliasPath =
                resolveUniqueImagePackageAlias(fullPath);
            if (!aliasPath.empty() &&
                routedReadPathIsContained(route, aliasPath) &&
                pathExists(aliasPath))
            {
                return true;
            }
            continue;
        }
        if (routedPathExists(route))
        {
            return true;
        }
    }
    return false;
    //
    //    std::fstream file;
    //    bool ret = false;
    //    file.open(fileName.c_str(), std::ios::in);
    //    if (file)
    //    {
    //        ret = true;
    //        file.close();
    //    }
    //    return ret;
}

std::string File::resolveFirstExistingResource(const std::vector<std::string>& fileNames)
{
    std::vector<std::string> candidates;
    std::vector<std::vector<RoutedResourcePath>> candidateRoutes;
    std::vector<std::string> orderedRoots;
    for (const auto& fileName : fileNames)
    {
        if (!isSafeResourcePath(fileName) ||
            std::find(candidates.begin(), candidates.end(), fileName) != candidates.end())
        {
            continue;
        }
        candidates.push_back(fileName);
        candidateRoutes.push_back(buildReadRoutes(fileName));
        for (const auto& route : candidateRoutes.back())
        {
            if (std::find(orderedRoots.begin(), orderedRoots.end(), route.root) ==
                orderedRoots.end())
            {
                orderedRoots.push_back(route.root);
            }
        }
    }

    for (const auto& root : orderedRoots)
    {
        for (size_t index = 0; index < candidates.size(); index++)
        {
            const auto& routes = candidateRoutes[index];
            auto route = std::find_if(routes.begin(), routes.end(),
                [&root](const RoutedResourcePath& candidateRoute)
                {
                    return candidateRoute.root == root;
                });
            if (route == routes.end())
            {
                continue;
            }
            if (routedRegularFileExists(*route))
            {
                return candidates[index];
            }
        }
    }
    return "";
}

bool File::visitReadableResources(const std::vector<std::string>& fileNames,
    const ResourceReadVisitor& visitor)
{
    return visitReadableResources(fileNames, (std::numeric_limits<int>::max)(), visitor);
}

bool File::visitReadableResources(const std::vector<std::string>& fileNames,
    int maximumBytes, const ResourceReadVisitor& visitor)
{
    if (maximumBytes < 0 || !visitor)
    {
        return false;
    }

    std::vector<std::string> candidates;
    std::vector<std::vector<RoutedResourcePath>> candidateRoutes;
    std::vector<std::string> orderedRoots;
    for (const auto& fileName : fileNames)
    {
        if (!isSafeResourcePath(fileName) ||
            std::find(candidates.begin(), candidates.end(), fileName) != candidates.end())
        {
            continue;
        }
        candidates.push_back(fileName);
        candidateRoutes.push_back(buildReadRoutes(fileName));
        for (const auto& route : candidateRoutes.back())
        {
            if (std::find(orderedRoots.begin(), orderedRoots.end(), route.root) ==
                orderedRoots.end())
            {
                orderedRoots.push_back(route.root);
            }
        }
    }

    for (const auto& root : orderedRoots)
    {
        for (size_t index = 0; index < candidates.size(); index++)
        {
            const auto& routes = candidateRoutes[index];
            auto route = std::find_if(routes.begin(), routes.end(),
                [&root](const RoutedResourcePath& candidateRoute)
                {
                    return candidateRoute.root == root;
                });
            if (route == routes.end())
            {
                continue;
            }

            std::unique_ptr<char[]> data;
            int length = 0;
            if (readRoutedResource(
                    *route, data, length,
                    static_cast<std::size_t>(maximumBytes)) &&
                visitor(candidates[index], data, length))
            {
                return true;
            }
        }
    }
    return false;
}

int File::readFile(const std::string& fileName, std::unique_ptr<char[]>& s)
{
    s = nullptr;
    if (!isSafeResourcePath(fileName))
    {
        return 0;
    }

    std::string resourceName = fileName;
    if (resourceName.length() > 1 && (resourceName.front() == '\\' || resourceName.front() == '/'))
    {
        resourceName.erase(resourceName.begin());
    }

    int len = 0;
    if (!readFile(resourceName, s, len))
    {
        return 0;
    }
    return len;
}

bool File::readFile(const std::string& fileName, std::unique_ptr<char[]>& s, int& len)
{
    return readFile(fileName, s, len, (std::numeric_limits<int>::max)());
}

bool File::readFile(const std::string& fileName, std::unique_ptr<char[]>& s, int& len,
    int maximumBytes)
{
    s = nullptr;
    len = 0;
    if (maximumBytes < 0 || !isSafeResourcePath(fileName))
    {
        return false;
    }
    std::string firstCandidate;
    for (const auto& route : buildReadRoutes(fileName))
    {
        std::string fullPath = makeRoutedReadPath(route);
        if (firstCandidate.empty())
        {
            firstCandidate = fullPath;
        }
        std::string resolvedRelativePath;
        if (readRoutedResource(
                route, s, len,
                static_cast<std::size_t>(maximumBytes),
                &resolvedRelativePath))
        {
            if (!resolvedRelativePath.empty() &&
                resolvedRelativePath != route.relativePath &&
                isImagePackagePath(route.relativePath))
            {
                GameLog::write(
                    "File: resolved image resource alias %s -> %s\n",
                    fullPath.c_str(),
                    resolvedRelativePath.c_str());
            }
            return true;
        }
    }
    GameLog::write("Can not open file(rb) %s\n", firstCandidate.empty() ? fileName.c_str() : firstCandidate.c_str());
    return false;
}

bool File::activeResourceFileExist(const std::string& fileName)
{
	RoutedResourcePath route;
    if (!explicitFormalResourceRoute(true, route) ||
		!isSafeResourcePath(fileName))
    {
        return false;
    }
	route.relativePath = toLowerAscii(normalizeRelativePath(fileName));
    return routedRegularFileExists(route);
}

bool File::readActiveResourceFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len)
{
    return readActiveResourceFile(fileName, s, len,
        (std::numeric_limits<int>::max)());
}

bool File::readActiveResourceFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len, int maximumBytes)
{
    s.reset();
    len = 0;
	RoutedResourcePath route;
    if (!explicitFormalResourceRoute(true, route) ||
		maximumBytes < 0 ||
		!isSafeResourcePath(fileName))
    {
        return false;
    }
	route.relativePath = toLowerAscii(normalizeRelativePath(fileName));
    return readRoutedResource(
		route, s, len, static_cast<std::size_t>(maximumBytes));
}

bool File::readCommonResourceFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len)
{
    return readCommonResourceFile(fileName, s, len,
        (std::numeric_limits<int>::max)());
}

bool File::readCommonResourceFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len, int maximumBytes)
{
    s.reset();
    len = 0;
	if (maximumBytes < 0 ||
		!isSafeResourcePath(fileName))
    {
        return false;
    }
	const std::string relativePath =
		toLowerAscii(normalizeRelativePath(fileName));
	for (RoutedResourcePath route : explicitCommonResourceRoutes())
	{
		route.relativePath = relativePath;
		if (readRoutedResource(
			route, s, len,
			static_cast<std::size_t>(maximumBytes)))
		{
			return true;
		}
	}
	return false;
}

bool File::readBundledApplicationFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len)
{
    return readBundledApplicationFile(fileName, s, len,
        (std::numeric_limits<int>::max)());
}

bool File::readBundledApplicationFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len, int maximumBytes)
{
    s.reset();
    len = 0;
    if (maximumBytes < 0 || !isSafeResourcePath(fileName))
    {
        return false;
    }
    RoutedResourcePath route;
    route.root = buildAssetsCollectionPrefix();
    route.relativePath = toLowerAscii(normalizeRelativePath(fileName));
    return readRoutedResource(
        route, s, len, static_cast<std::size_t>(maximumBytes));
}

bool File::readSharedApplicationFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len)
{
    return readSharedApplicationFile(fileName, s, len,
        (std::numeric_limits<int>::max)());
}

bool File::readSharedApplicationFile(const std::string& fileName,
    std::unique_ptr<char[]>& s, int& len, int maximumBytes)
{
    s.reset();
    len = 0;
    if (maximumBytes < 0 || !isSafeResourcePath(fileName))
    {
        return false;
    }

    for (const auto& route : buildSharedApplicationReadRoutes(fileName))
    {
        if (readRoutedResource(
                route, s, len,
                static_cast<std::size_t>(maximumBytes)))
        {
            return true;
        }
    }
    return false;
}

bool File::writeSharedApplicationFile(const std::string& fileName, const void* s, int len)
{
    if (len < 0 || (s == nullptr && len > 0) || !isSafeResourcePath(fileName))
    {
        return false;
    }

	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
    const RoutedResourcePath route = buildSharedApplicationWriteRoute(
		fileName, &routingState, &routingGeneration);
	if (route.root.empty())
	{
		GameLog::write(
			"Can not resolve shared application write root for %s\n",
			fileName.c_str());
		return false;
	}
    std::string fullPath = makeFullPath(route.root, route.relativePath);
    if (fullPath.empty())
    {
		GameLog::write(
			"Can not resolve shared application write path for %s root=%s relative=%s\n",
			fileName.c_str(), route.root.c_str(),
			route.relativePath.c_str());
        return false;
    }
	if (routingState == File::EditorRunFileLayoutState::Valid)
	{
		try
		{
			return writeRoutedFileChecked(
				route, routingState, routingGeneration,
				s, len, CheckedWriteMode::Truncate);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

    std::string directory = convert::extractFilePath(fullPath);
    if (!createWriteDirectory(directory))
    {
		GameLog::write(
			"Can not create shared application directory for %s\n",
			fileName.c_str());
        return false;
    }

    SDL_IOStream* stream = SDL_IOFromFile(fullPath.c_str(), "wb");
    if (stream == nullptr)
    {
        GameLog::write("Can not open shared application file(wb) %s\n", fileName.c_str());
        return false;
    }

    bool succeeded = len == 0 ||
        SDL_WriteIO(stream, s, static_cast<std::size_t>(len)) == static_cast<std::size_t>(len);
    if (!SDL_CloseIO(stream))
    {
        succeeded = false;
    }
    return succeeded;
}

void File::appendSharedApplicationFile(
    const std::string& fileName, const void* s, int len)
{
    if (s == nullptr || len < 0 || !isSafeResourcePath(fileName))
    {
        return;
    }

	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
    const RoutedResourcePath route = buildSharedApplicationWriteRoute(
		fileName, &routingState, &routingGeneration);
	if (route.root.empty())
	{
		return;
	}
    const std::string fullPath =
        makeFullPath(route.root, route.relativePath);
    if (fullPath.empty())
    {
        return;
    }
	if (routingState == File::EditorRunFileLayoutState::Valid)
	{
		try
		{
			(void)writeRoutedFileChecked(
				route, routingState, routingGeneration,
				s, len, CheckedWriteMode::Append);
		}
		catch (const std::exception&)
		{
		}
		return;
	}

    const std::string directory =
        convert::extractFilePath(fullPath);
    if (!createWriteDirectory(directory))
    {
        return;
    }
    SDL_IOStream* stream =
        SDL_IOFromFile(fullPath.c_str(), "ab");
    if (stream == nullptr)
    {
        return;
    }
    (void)SDL_SeekIO(stream, 0, SDL_IO_SEEK_END);
    (void)SDL_WriteIO(
        stream, s, static_cast<std::size_t>(len));
    (void)SDL_CloseIO(stream);
}

//void File::readFile(const std::string& fileName, void * s, int len)
//{
//
//    std::string  newFileName = AssetsPath + fileName;
//
//    convert::replaceAllString(newFileName, "\\", "/");
//    auto fp = SDL_IOFromFile(newFileName.c_str(), "rb");
//#ifdef __ANDROID__
//    if (!fp)
//    {
//        std::string path = SDL_AndroidGetInternalStoragePath();
//        if (*path.end() != '/') {path += "/";}
//        convert::replaceAllString(newFileName, "/", "_");
//        newFileName = path + newFileName;
//        fp = SDL_IOFromFile(newFileName.c_str(), "rb");
//    }
//#endif
//    if (!fp)
//    {
//        GameLog::write(stderr, "Can not open file %s\n", newFileName.c_str());
//        return;
//    }
//    SDL_SeekIO(fp, 0, 0);
//    SDL_ReadIO(fp, s, len, 1);
//    SDL_CloseIO(fp);
//}

bool File::writeFileChecked(const std::string& fileName, const void* s, int len)
{
    if (len < 0 || (s == nullptr && len > 0) || !isSafeResourcePath(fileName))
    {
        return false;
    }
	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
    const RoutedResourcePath route = buildWriteRoute(
		fileName, &routingState, &routingGeneration);
    std::string newFileName = makeFullPath(route.root, route.relativePath);
    if (newFileName.empty())
    {
        return false;
    }
	try
	{
		if (routingState == File::EditorRunFileLayoutState::Valid)
		{
			return writeRoutedFileChecked(
				route, routingState, routingGeneration,
				s, len, CheckedWriteMode::Truncate);
		}
		return writeFullFileChecked(std::filesystem::u8path(newFileName), s, len);
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool File::writeFileChecked(const std::string& fileName, const std::unique_ptr<char[]>& s, int len)
{
	return writeFileChecked(fileName, s.get(), len);
}

void File::writeFile(const std::string& fileName, const void* s, int len)
{
	(void)writeFileChecked(fileName, s, len);
}

void File::writeFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len)
{
    writeFile(fileName, s.get(), len);
}

void File::appendFile(const std::string& fileName, const void* s, int len)
{
    if (s == nullptr || len < 0 || !isSafeResourcePath(fileName))
    {
        return;
    }
	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
    const RoutedResourcePath route = buildWriteRoute(
		fileName, &routingState, &routingGeneration);
    std::string newFileName = makeFullPath(route.root, route.relativePath);
    if (newFileName.empty())
    {
        return;
    }
	if (routingState == File::EditorRunFileLayoutState::Valid)
	{
		try
		{
			(void)writeRoutedFileChecked(
				route, routingState, routingGeneration,
				s, len, CheckedWriteMode::Append);
		}
		catch (const std::exception&)
		{
		}
		return;
	}

    {
        auto tempPath = convert::extractFilePath(newFileName);
        createWriteDirectory(tempPath);
    }
    auto fp = SDL_IOFromFile(newFileName.c_str(), "ab");
#if !defined(__ANDROID__) && !defined(__APPLE__)
    if (!fp)
    {
        auto tempPath = convert::extractFilePath(newFileName);
        createWriteDirectory(tempPath);
        fp = SDL_IOFromFile(newFileName.c_str(), "ab");
    }
#endif
    if (!fp)
    {
        fprintf(stderr, "Can not open file(wb+) %s\n", fileName.c_str());
        return;
    }
    SDL_SeekIO(fp, 0, SDL_IO_SEEK_END);
    SDL_WriteIO(fp, s, len);
    SDL_CloseIO(fp);
}

void File::appendFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len)
{
    appendFile(fileName, s.get(), len);
}

void File::copy(const std::string& src, const std::string& dst)
{
    std::unique_ptr<char[]> s;
    int len = 0;
    if (readFile(src, s, len))
    {
        if (s != nullptr && len >= 0)
        {
            writeFile(dst, s, len);
        }
    }
}

std::vector<std::string> File::listFiles(const std::string& directoryName)
{
    std::vector<std::string> files;
    std::vector<std::string> keys;
    if (!isSafeResourcePath(directoryName))
    {
        return files;
    }

    for (const auto& route : buildReadRoutes(directoryName))
    {
        for (const auto& fileName : listRoutedFiles(route))
        {
            std::string key = normalizeFileNameKey(fileName);
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
            {
                keys.push_back(key);
                files.push_back(fileName);
            }
        }
    }
    return files;
}

bool File::listFilesRejectingCaseCollisions(
    const std::string& directoryName,
    std::vector<std::string>& files,
    std::string* collidingFileName,
    std::size_t maximumFileCount,
    bool* fileCountLimitExceeded)
{
    files.clear();
    if (collidingFileName != nullptr)
    {
        collidingFileName->clear();
    }
    if (fileCountLimitExceeded != nullptr)
    {
        *fileCountLimitExceeded = false;
    }
    if (!isSafeResourcePath(directoryName))
    {
        return false;
    }

    std::unordered_map<std::string, std::string> namesByKey;
    if (maximumFileCount > 0)
    {
        constexpr std::size_t MaximumInitialReservation = 4096;
        namesByKey.reserve(
            (std::min)(
                maximumFileCount,
                MaximumInitialReservation));
    }
    for (const auto& route : buildReadRoutes(directoryName))
    {
        bool routeEntryLimitExceeded = false;
        const std::vector<std::string> routeFiles =
            listRoutedFiles(
                route,
                maximumFileCount,
                &routeEntryLimitExceeded);
        if (routeEntryLimitExceeded)
        {
            if (fileCountLimitExceeded != nullptr)
            {
                *fileCountLimitExceeded = true;
            }
            files.clear();
            return false;
        }
        for (const auto& fileName : routeFiles)
        {
            const std::string key =
                normalizeFileNameKey(fileName);
            const auto existing = namesByKey.find(key);
            if (existing == namesByKey.end())
            {
                if (maximumFileCount > 0 &&
                    files.size() >= maximumFileCount)
                {
                    if (fileCountLimitExceeded != nullptr)
                    {
                        *fileCountLimitExceeded = true;
                    }
                    files.clear();
                    return false;
                }
                namesByKey.emplace(key, fileName);
                files.push_back(fileName);
                continue;
            }
            if (existing->second != fileName)
            {
                if (collidingFileName != nullptr)
                {
                    *collidingFileName = fileName;
                }
                files.clear();
                return false;
            }
        }
    }
    return true;
}

bool File::removeFile(const std::string& fileName)
{
    if (!isSafeResourcePath(fileName))
    {
        return false;
    }

	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
    const RoutedResourcePath route = buildWriteRoute(
		fileName, &routingState, &routingGeneration);
    std::string fullPath = makeFullPath(route.root, route.relativePath);
    if (fullPath.empty())
    {
        return false;
    }
	if (routingState == File::EditorRunFileLayoutState::Valid)
	{
		try
		{
			NativeDirectoryHandle parent;
			std::string leaf;
			if (!openAnchoredRouteParent(
					route,
					File::EditorRunFileOperationPhase::
						BeforeWriteRootOpen,
					false, parent, leaf))
			{
				return false;
			}
			const auto routingIsCurrent =
				[routingState, routingGeneration]()
				{
					return editorRunRoutingSnapshotIsCurrent(
						routingState, routingGeneration);
				};
			return removeNativeRegularFile(
				parent, leaf, routingIsCurrent);
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(fullPath.c_str(), &info) || info.type == SDL_PATHTYPE_NONE)
    {
        return true;
    }
    if (info.type != SDL_PATHTYPE_FILE)
    {
        GameLog::write("Can not remove non-file path %s\n", fullPath.c_str());
        return false;
    }
    if (!SDL_RemovePath(fullPath.c_str()))
    {
        GameLog::write("Can not remove file %s\n", fullPath.c_str());
        return false;
    }
    return true;
}

bool File::clearDirectoryFiles(const std::string& directoryName)
{
    if (!isSafeResourcePath(directoryName))
    {
        return false;
    }

	File::EditorRunFileLayoutState routingState =
		File::EditorRunFileLayoutState::NotInstalled;
	uint64_t routingGeneration = 0;
    const RoutedResourcePath route = buildWriteRoute(
		directoryName, &routingState, &routingGeneration);
    std::string fullDirectory = makeFullPath(route.root, route.relativePath);
    if (fullDirectory.empty())
    {
        return false;
    }
	if (routingState == File::EditorRunFileLayoutState::Valid)
	{
		try
		{
			NativeDirectoryHandle directory;
			if (!openAnchoredRouteDirectory(
					route, true, directory))
			{
				return false;
			}
			const auto routingIsCurrent =
				[routingState, routingGeneration]()
				{
					return editorRunRoutingSnapshotIsCurrent(
						routingState, routingGeneration);
				};
			bool listed = false;
			const std::vector<std::string> names =
				listNativeDirectoryNames(
					directory, &listed);
			if (!listed)
			{
				return false;
			}
			for (const std::string& name : names)
			{
				const NoFollowPathInformation information =
					inspectNativeChildNoFollow(
						directory, name);
				if (information.kind ==
					NoFollowPathKind::Directory)
				{
					continue;
				}
				if (information.kind !=
						NoFollowPathKind::RegularFile ||
					information.linkCount != 1 ||
					!removeNativeRegularFile(
						directory, name,
						routingIsCurrent))
				{
					return false;
				}
			}
			return routingIsCurrent();
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
	try
	{
		const NoFollowPathInformation directoryInformation =
			inspectPathNoFollow(
				std::filesystem::u8path(fullDirectory));
		if (directoryInformation.kind == NoFollowPathKind::Missing)
		{
			return true;
		}
		if (directoryInformation.kind != NoFollowPathKind::Directory)
		{
			return false;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}

	bool ok = true;
	for (const auto& fileName : listFilesInFullDirectory(fullDirectory))
	{
		std::string fullPath = joinFullPath(fullDirectory, fileName);
		if (!SDL_RemovePath(fullPath.c_str()))
		{
			GameLog::write("Can not remove file %s\n", fullPath.c_str());
			ok = false;
		}
	}
	return ok;
}

bool File::copyDirectoryFiles(const std::string& srcDirectoryName,
    const std::string& dstDirectoryName,
    const std::vector<std::string>& excludedFileNames,
    const DirectoryCopyFailureInjector& failureInjector,
    const DirectoryCopyLimits& limits)
{
    if (!isSafeResourcePath(srcDirectoryName) || !isSafeResourcePath(dstDirectoryName))
    {
        return false;
    }
    const bool bounded =
        limits.maximumFileCount > 0 ||
        limits.maximumTotalBytes > 0 ||
        limits.maximumSingleFileBytes > 0;
    if (bounded &&
        (limits.maximumFileCount == 0 ||
            limits.maximumTotalBytes == 0 ||
            limits.maximumSingleFileBytes <= 0))
    {
        return false;
    }
	const auto cancellationIsRequested =
		[&limits]() noexcept
		{
			if (!limits.cancellationRequested)
			{
				return false;
			}
			try
			{
				return limits.cancellationRequested();
			}
			catch (...)
			{
				return true;
			}
		};
	if (cancellationIsRequested())
	{
		return false;
	}

	DirectoryCopyTransactionPaths paths;
	if (!getDirectoryCopyTransactionPaths(dstDirectoryName, paths))
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(g_directoryCopyMutex);
	const auto failTransaction =
		[](const char* stage)
		{
			GameLog::write(
				"Directory copy transaction failed at %s\n",
				stage);
			return false;
		};
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return failTransaction("initial route check");
	}
	if (!recoverDirectoryCopyUnlocked(paths))
	{
		return failTransaction("initial recovery");
	}
	if (!removeTransactionPathRecursively(
			paths, paths.staging))
	{
		return failTransaction("staging cleanup");
	}
	if (!removeTransactionPathRecursively(
			paths, paths.backup))
	{
		return failTransaction("backup cleanup");
	}
	if (!removeTransactionPathRecursively(
			paths, paths.ready))
	{
		return failTransaction("ready cleanup");
	}
	if (!createTransactionDirectory(paths, paths.staging))
	{
		return failTransaction("staging creation");
	}
	if (cancellationIsRequested())
	{
		(void)removeTransactionPathRecursively(
			paths, paths.staging);
		return failTransaction("cancellation");
	}

	std::vector<std::string> sourceFiles;
	bool fileCountLimitExceeded = false;
	if (!listFilesRejectingCaseCollisions(
			srcDirectoryName,
			sourceFiles,
			nullptr,
			bounded ? limits.maximumFileCount : 0,
			&fileCountLimitExceeded))
	{
		(void)removeTransactionPathRecursively(
			paths, paths.staging);
		return failTransaction(
			fileCountLimitExceeded
				? "source file-count limit"
				: "source case-collision check");
	}
	if (!directoryCopyRoutingIsCurrent(paths))
	{
		return false;
	}
	std::uint64_t copiedBytes = 0;
    for (const auto& fileName : sourceFiles)
    {
		if (cancellationIsRequested() ||
			!directoryCopyRoutingIsCurrent(paths))
		{
			(void)removeTransactionPathRecursively(
				paths, paths.staging);
			return false;
		}
        if (isExcludedFileName(fileName, excludedFileNames))
        {
            continue;
        }
		if (!isValidUtf8(fileName))
		{
			(void)removeTransactionPathRecursively(paths, paths.staging);
			return false;
		}

        std::unique_ptr<char[]> data;
        int len = 0;
		const bool read =
			bounded
				? readFile(
					joinFullPath(srcDirectoryName, fileName),
					data,
					len,
					limits.maximumSingleFileBytes)
				: readFile(
					joinFullPath(srcDirectoryName, fileName),
					data,
					len);
		if (!read ||
			len < 0 ||
			(bounded &&
				(copiedBytes > limits.maximumTotalBytes ||
					static_cast<std::uint64_t>(len) >
						limits.maximumTotalBytes - copiedBytes)) ||
			cancellationIsRequested() ||
			!directoryCopyRoutingIsCurrent(paths) ||
			(data == nullptr && len > 0) ||
			!writeTransactionFileChecked(paths,
				paths.staging / std::filesystem::u8path(fileName),
				data.get(), len))
        {
			(void)removeTransactionPathRecursively(paths, paths.staging);
			return failTransaction("staging file write");
        }
		copiedBytes += static_cast<std::uint64_t>(len);
    }

	return publishStagedDirectoryTransaction(
		paths,
		failureInjector,
		cancellationIsRequested,
		[&paths]()
		{
			return removeTransactionPathRecursively(
				paths, paths.staging);
		});
}

bool File::promotePreparedScratchDirectory(
	const std::string& srcDirectoryName,
	const std::string& dstDirectoryName,
	const DirectoryCopyFailureInjector& failureInjector,
	const DirectoryCopyLimits& limits)
{
	if (!isSafeResourcePath(srcDirectoryName) ||
		!isSafeResourcePath(dstDirectoryName))
	{
		return false;
	}
	const bool bounded =
		limits.maximumFileCount > 0 ||
		limits.maximumTotalBytes > 0 ||
		limits.maximumSingleFileBytes > 0;
	if (bounded &&
		(limits.maximumFileCount == 0 ||
		 limits.maximumTotalBytes == 0 ||
		 limits.maximumSingleFileBytes <= 0))
	{
		return false;
	}
	const auto cancellationIsRequested =
		[&limits]() noexcept
		{
			if (!limits.cancellationRequested)
			{
				return false;
			}
			try
			{
				return limits.cancellationRequested();
			}
			catch (...)
			{
				return true;
			}
		};
	if (cancellationIsRequested())
	{
		return false;
	}

	DirectoryCopyTransactionPaths paths;
	DirectoryCopyTransactionPaths sourceRecoveryPaths;
	if (!getDirectoryCopyTransactionPaths(
			dstDirectoryName, paths) ||
		!getDirectoryCopyTransactionPaths(
			srcDirectoryName, sourceRecoveryPaths) ||
		!setDirectoryPromotionSourcePath(
			sourceRecoveryPaths, paths))
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(g_directoryCopyMutex);
	const auto failTransaction =
		[](const char* stage)
		{
			GameLog::write(
				"Directory promotion transaction failed at %s\n",
				stage);
			return false;
		};
	if (!directoryCopyRoutingIsCurrent(paths) ||
		!directoryCopyRoutingIsCurrent(
			sourceRecoveryPaths))
	{
		return failTransaction("initial route check");
	}
	if (!recoverDirectoryCopyUnlocked(
			sourceRecoveryPaths) ||
		!recoverDirectoryCopyUnlocked(paths))
	{
		return failTransaction("initial recovery");
	}
	if (!removeTransactionPathRecursively(
			paths, paths.staging) ||
		!removeTransactionPathRecursively(
			paths, paths.backup) ||
		!removeTransactionPathRecursively(
			paths, paths.ready))
	{
		return failTransaction("transaction cleanup");
	}

	if (!captureTransactionPathIdentity(
			paths, paths.source,
			NoFollowPathKind::Directory,
			paths.sourceIdentity))
	{
		return failTransaction("source identity");
	}
	if (cancellationIsRequested())
	{
		return failTransaction("cancellation");
	}
	paths.stagingIdentity = paths.sourceIdentity;
	const auto restoreSource = [&paths]()
	{
		bool sourceExists = false;
		bool stagingExists = false;
		if (!transactionPathExists(
				paths, paths.source, sourceExists) ||
			!transactionPathExists(
				paths, paths.staging, stagingExists))
		{
			return false;
		}
		if (sourceExists)
		{
			return !stagingExists &&
				transactionPathMatchesIdentity(
					paths, paths.source,
					NoFollowPathKind::Directory,
					paths.sourceIdentity);
		}
		return stagingExists &&
			transactionPathMatchesIdentity(
				paths, paths.staging,
				NoFollowPathKind::Directory,
				paths.stagingIdentity) &&
			renameTransactionPath(
				paths, paths.staging, paths.source) &&
			transactionPathMatchesIdentity(
				paths, paths.source,
				NoFollowPathKind::Directory,
				paths.sourceIdentity);
	};
	if (!renameTransactionPath(
			paths, paths.source, paths.staging))
	{
		return failTransaction("source staging rename");
	}
	PhysicalPathIdentity capturedStagingIdentity;
	if (!captureTransactionPathIdentity(
			paths, paths.staging,
			NoFollowPathKind::Directory,
			capturedStagingIdentity) ||
		!physicalPathIdentitiesEqual(
			paths.sourceIdentity,
			capturedStagingIdentity))
	{
		(void)restoreSource();
		return failTransaction("source staging identity");
	}

	if (!validatePreparedStagingDirectory(
			paths,
			limits,
			cancellationIsRequested))
	{
		const bool restored = restoreSource();
		if (!restored)
		{
			GameLog::write(
				"Directory promotion could not restore its source after staging validation failure\n");
		}
		return failTransaction(
			cancellationIsRequested()
				? "cancellation"
				: "staging validation");
	}
	return publishStagedDirectoryTransaction(
		paths,
		failureInjector,
		cancellationIsRequested,
		restoreSource);
}

bool File::recoverDirectoryCopy(const std::string& destinationDirectoryName)
{
	if (!isSafeResourcePath(destinationDirectoryName))
	{
		return false;
	}
	std::string normalizedDirectory =
		toLowerAscii(normalizeRelativePath(
			destinationDirectoryName));
	while (!normalizedDirectory.empty() &&
		normalizedDirectory.back() == '/')
	{
		normalizedDirectory.pop_back();
	}
	if (normalizedDirectory == "save/rpg0")
	{
		return false;
	}
	DirectoryCopyTransactionPaths paths;
	if (!getDirectoryCopyTransactionPaths(destinationDirectoryName, paths))
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(g_directoryCopyMutex);
	return recoverDirectoryCopyUnlocked(paths);
}

std::string File::getAssetsName(const std::string& fileName)
{
    if (!isSafeResourcePath(fileName))
    {
        return "";
    }
    std::string firstCandidate;
    for (const auto& route : buildReadRoutes(fileName))
    {
        if (route.anchored)
        {
            // Returning a lexical host path would let a later FFmpeg/SDL open
            // escape the verified handle-relative read boundary.
            if (!anchoredResourcePathIsDefinitelyMissing(route))
            {
                return "";
            }
            continue;
        }
        std::string fullPath = makeRoutedReadPath(route);
        if (firstCandidate.empty())
        {
            firstCandidate = fullPath;
        }
        if (pathIsRegularFile(fullPath))
        {
            return fullPath;
        }
        std::string caseInsensitivePath = resolveCaseInsensitiveExistingPath(fullPath);
        if (!caseInsensitivePath.empty() &&
            routedReadPathIsContained(route, caseInsensitivePath) &&
            pathIsRegularFile(caseInsensitivePath))
        {
            return caseInsensitivePath;
        }
        std::string aliasPath = resolveUniqueImagePackageAlias(fullPath);
        if (!aliasPath.empty() &&
            routedReadPathIsContained(route, aliasPath) &&
            pathIsRegularFile(aliasPath))
        {
            return aliasPath;
        }
    }
    return firstCandidate;
}

void File::setAssetsCollectionRoot(const std::string& root)
{
    std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
    g_assetsCollectionRoot = normalizeRoot(root);
}

void File::setActiveResourceRoot(const std::string& root)
{
    std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
    g_activeResourceRoot = normalizeRoot(root);
}

void File::setCommonResourceRoot(const std::string& root)
{
    std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
    g_commonResourceRoot = normalizeRoot(root);
	g_commonResourceFallbackRoots.clear();
}

void File::setCommonResourceFallbackRoots(
	const std::vector<std::string>& roots)
{
	std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
	g_commonResourceFallbackRoots.clear();
	for (const std::string& root : roots)
	{
		const std::string normalized = normalizeRoot(root);
		if (!normalized.empty() && normalized != g_commonResourceRoot &&
			std::find(
				g_commonResourceFallbackRoots.begin(),
				g_commonResourceFallbackRoots.end(),
				normalized) == g_commonResourceFallbackRoots.end())
		{
			g_commonResourceFallbackRoots.push_back(normalized);
		}
	}
}

void File::setResourceFallbackRoots(const std::vector<std::string>& roots)
{
    std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
    g_resourceFallbackRoots.clear();
    for (const auto& root : roots)
    {
        std::string normalized = normalizeRoot(root);
        if (!normalized.empty() && normalized != g_activeResourceRoot &&
            std::find(g_resourceFallbackRoots.begin(), g_resourceFallbackRoots.end(), normalized) == g_resourceFallbackRoots.end())
        {
            g_resourceFallbackRoots.push_back(normalized);
        }
    }
}

void File::setUiResourceFallbackRoots(const std::vector<std::string>& roots,
    bool preferLocal,
    const std::string& commonRoot)
{
    std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
    g_uiResourceFallbackRoots.clear();
    g_uiCommonResourceRoot = normalizeRoot(commonRoot);
    g_uiResourceFallbackConfigured = true;
    g_preferLocalUi = preferLocal;
    for (const auto& root : roots)
    {
        std::string normalized = normalizeRoot(root);
        if (!normalized.empty() && normalized != g_activeResourceRoot &&
            std::find(g_uiResourceFallbackRoots.begin(), g_uiResourceFallbackRoots.end(), normalized) == g_uiResourceFallbackRoots.end())
        {
            g_uiResourceFallbackRoots.push_back(normalized);
        }
    }
    if (g_uiCommonResourceRoot == g_activeResourceRoot ||
        std::find(g_uiResourceFallbackRoots.begin(), g_uiResourceFallbackRoots.end(), g_uiCommonResourceRoot) !=
            g_uiResourceFallbackRoots.end())
    {
        g_uiCommonResourceRoot.clear();
    }
}

void File::setActiveSaveNamespace(const std::string& saveNamespace)
{
    std::lock_guard<std::mutex> lock(g_formalResourceRoutingMutex);
    g_activeSaveNamespace = saveNamespace;
}

std::string File::getActiveResourceRoot()
{
    return currentEditorRunResourceRouting().activeResourceRoot;
}

std::string File::getActiveSaveNamespace()
{
    return currentEditorRunResourceRouting().activeSaveNamespace;
}

std::string File::getAssetsCollectionRoot()
{
    return currentEditorRunResourceRouting().assetsCollectionRoot;
}

std::string File::getDefaultAssetsCollectionRoot()
{
    return buildDefaultAssetsPrefix();
}

//std::unique_ptr<char[]> File::getIdxContent(std::string fileName_idx, std::string fileName_grp, std::vector<int>* offset, std::vector<int>* length)
//{
//    std::unique_ptr<char[]> Ridx;
//    int len = 0;
//    if (File::readFile(fileName_idx, Ridx, len))
//    {
//        offset->resize(len / 4 + 1);
//        length->resize(len / 4);
//        offset->at(0) = 0;
//        for (int i = 0; i < len / 4; i++)
//        {
//            (*offset)[i + 1] = ((int*)Ridx.get())[i];
//            (*length)[i] = (*offset)[i + 1] - (*offset)[i];
//        }
//        int total_length = offset->back();
//
//        auto Rgrp = std::make_unique<char[]>(total_length);
//        File::readFile(fileName_grp, Rgrp, total_length);
//        return Rgrp;
//    }
//}
