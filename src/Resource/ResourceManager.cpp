#include "ResourceManager.h"

#include "../File/INIReader.h"
#include "../File/File.h"
#include "../File/log.h"
#include "../Game/Config/Config.h"
#include "../JxqyEngineVersion.h"
#include "../Platform/AndroidExternalStorage.h"
#include "../Update/ResourceInstallTransaction.h"
#include "../libconvert/libconvert.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace
{
// game_profile.ini 固定文件名。
constexpr char ManifestFileName[] = "game_profile.ini";

// 用户选择记录属于集合级运行数据，不随任何资源包打包。
constexpr char RecentResourceSelectionFile[] = "save/system/resource_selection.ini";
constexpr char RecentResourceSelectionSection[] = "ResourceSelection";
constexpr int MaximumRecentResourceSelectionBytes = 16 * 1024;
constexpr char ExternalResourceSourceTag[] = "android-external";
constexpr char WritableResourceSourceTag[] = "application-resource";

std::string_view currentEngineVersion()
{
	return JxqyBuildVersion::EngineVersion;
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

std::string trimAscii(std::string value);

std::string normalizeRootPath(std::string path)
{
	convert::replaceAllString(path, "\\", "/");
	if (path.empty())
	{
		return path;
	}

	path = std::filesystem::u8path(path).lexically_normal().u8string();
	convert::replaceAllString(path, "\\", "/");
	if (!path.empty() && path.back() != '/')
	{
		path += "/";
	}
	return path;
}

bool resolveCollectionRoot(
	const std::string& requestedRoot,
	std::string& resolvedRoot)
{
	resolvedRoot = normalizeRootPath(requestedRoot);
#if defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	return true;
#else
	if (resolvedRoot.empty())
	{
		return false;
	}

	try
	{
		std::filesystem::path root =
			std::filesystem::u8path(resolvedRoot);
		std::error_code error;
		if (!root.is_absolute())
		{
			root = std::filesystem::absolute(root, error);
		}
		if (error || root.empty() || !root.is_absolute())
		{
			resolvedRoot.clear();
			return false;
		}
		resolvedRoot = normalizeRootPath(
			root.lexically_normal().u8string());
		return !resolvedRoot.empty();
	}
	catch (const std::exception&)
	{
		resolvedRoot.clear();
		return false;
	}
#endif
}

std::string extractFolderName(std::string root)
{
	convert::replaceAllString(root, "\\", "/");
	while (!root.empty() && root.back() == '/')
	{
		root.pop_back();
	}
	size_t pos = root.find_last_of('/');
	if (pos == std::string::npos)
	{
		return root;
	}
	return root.substr(pos + 1);
}

// 获取平台默认集合根（macOS 非 iOS 需要拼接 SDL_GetBasePath）。
std::string getDefaultCollectionRoot()
{
	return File::getDefaultAssetsCollectionRoot();
}

bool isPlainFilesystemDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	return !error && std::filesystem::is_directory(status) &&
		!std::filesystem::is_symlink(status);
}

bool hasResourceManifestEntry(const std::filesystem::path& directory)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(
			directory / ManifestFileName, error);
	if (error == std::errc::no_such_file_or_directory)
	{
		return false;
	}
	// Keep inaccessible or malformed entries as candidates so catalog parsing
	// can report them. A directory with no manifest is simply not a resource.
	return error || std::filesystem::exists(status);
}

bool resolvePlainDirectChildDirectory(
	const std::filesystem::path& requestedParent,
	const std::filesystem::path& requestedChild,
	std::filesystem::path& resolvedChild)
{
	resolvedChild.clear();
	try
	{
		std::error_code error;
		if (!isPlainFilesystemDirectory(requestedParent) ||
			!isPlainFilesystemDirectory(requestedChild))
		{
			return false;
		}
		const std::filesystem::path parent =
			std::filesystem::canonical(requestedParent, error);
		if (error)
		{
			return false;
		}
		resolvedChild = std::filesystem::canonical(requestedChild, error);
		return !error && resolvedChild.parent_path() == parent;
	}
	catch (const std::exception&)
	{
		resolvedChild.clear();
		return false;
	}
}

RuntimeResource::CatalogFileReadResult readPackagedCatalogFile(
	const std::filesystem::path& root,
	std::string_view relativePath,
	std::size_t maximumBytes);

bool readDirectManifestId(
	const std::filesystem::path& resourceRoot,
	std::string& gameId)
{
	gameId.clear();
	const RuntimeResource::CatalogFileReadResult readResult =
		readPackagedCatalogFile(
			resourceRoot,
			ManifestFileName,
			RuntimeResource::MaximumCatalogIniBytes);
	if (readResult.status !=
			RuntimeResource::CatalogFileReadStatus::Success ||
		readResult.bytes.empty() ||
		readResult.bytes.size() >
			static_cast<std::size_t>((std::numeric_limits<int>::max)()))
	{
		return false;
	}
	ResourceManifest manifest;
	if (!manifest.loadFromBuffer(
			reinterpret_cast<const char*>(readResult.bytes.data()),
			static_cast<int>(readResult.bytes.size())) ||
		manifest.id.empty())
	{
		return false;
	}
	gameId = manifest.id;
	return true;
}

std::uint64_t directoryByteSize(const std::filesystem::path& root)
{
	constexpr std::size_t MaximumMeasuredEntries = 50000;
	std::uint64_t total = 0;
	std::size_t entryCount = 0;
	try
	{
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(
			root,
			std::filesystem::directory_options::skip_permission_denied,
			error);
		const std::filesystem::recursive_directory_iterator end;
		while (!error && iterator != end)
		{
			entryCount++;
			if (entryCount > MaximumMeasuredEntries)
			{
				return std::numeric_limits<std::uint64_t>::max();
			}
			const std::filesystem::file_status status =
				iterator->symlink_status(error);
			if (error)
			{
				break;
			}
			if (std::filesystem::is_regular_file(status))
			{
				const std::uintmax_t size = iterator->file_size(error);
				if (error || size >
					std::numeric_limits<std::uint64_t>::max() - total)
				{
					return std::numeric_limits<std::uint64_t>::max();
				}
				total += static_cast<std::uint64_t>(size);
			}
			iterator.increment(error);
		}
		return error
			? std::numeric_limits<std::uint64_t>::max()
			: total;
	}
	catch (const std::exception&)
	{
		return std::numeric_limits<std::uint64_t>::max();
	}
}

bool isValidStoredSaveNamespace(const std::string& saveNamespace)
{
	return !saveNamespace.empty() &&
		toLowerAscii(saveNamespace) != "system" &&
		File::sanitizeSaveNamespace(saveNamespace) == saveNamespace;
}

std::filesystem::path configuredSaveRoot()
{
	const std::string userDataRoot = File::getUserDataRoot();
	return userDataRoot.empty()
		? std::filesystem::path()
		: std::filesystem::u8path(userDataRoot) / "save";
}

int saveSlotCount(const std::filesystem::path& namespaceRoot)
{
	int count = 0;
	for (int index = 1; index <= 7; index++)
	{
		std::error_code error;
		if (std::filesystem::is_regular_file(
				namespaceRoot / ("rpg" + std::to_string(index)) /
					"game.ini",
				error))
		{
			count++;
		}
	}
	std::error_code error;
	if (std::filesystem::is_regular_file(
			namespaceRoot / "rpg_auto" / "game.ini", error))
	{
		count++;
	}
	return count;
}

std::string prepareWritableResourceCollectionRoot(
	const std::string& primaryCollectionRoot)
{
#if defined(__ANDROID__)
	const std::string platformPath =
		AndroidExternalStorage::getApplicationResourceDirectoryPath();
#elif defined(__APPLE__)
	const std::string userDataRoot = File::getUserDataRoot();
	const std::string platformPath = userDataRoot.empty()
		? std::string()
		: (std::filesystem::u8path(userDataRoot) / "assets")
			.generic_u8string();
#else
	return normalizeRootPath(primaryCollectionRoot);
#endif
#if defined(__ANDROID__) || defined(__APPLE__)
	if (platformPath.empty())
	{
		return "";
	}
	try
	{
		std::filesystem::path root =
			std::filesystem::u8path(platformPath).lexically_normal();
		std::error_code error;
		std::filesystem::create_directories(root, error);
		if (error || !isPlainFilesystemDirectory(root))
		{
			return "";
		}
		root = std::filesystem::canonical(root, error);
		return error ? std::string() :
			normalizeRootPath(root.generic_u8string());
	}
	catch (const std::exception&)
	{
		return "";
	}
#endif
}

std::string resolveWritableCommonResourceRoot(
	const std::string& primaryCollectionRoot,
	const std::string& writableCollectionRoot,
	const std::string& bundledCommonRoot)
{
	if (writableCollectionRoot.empty() || bundledCommonRoot.empty() ||
		normalizeRootPath(primaryCollectionRoot) ==
			normalizeRootPath(writableCollectionRoot))
	{
		return normalizeRootPath(bundledCommonRoot);
	}
	try
	{
		const std::filesystem::path primary =
			std::filesystem::u8path(primaryCollectionRoot).lexically_normal();
		const std::filesystem::path bundled =
			std::filesystem::u8path(bundledCommonRoot).lexically_normal();
		std::filesystem::path relative;
		if (primary.empty())
		{
			relative = bundled;
		}
		else
		{
			relative = bundled.lexically_relative(primary);
		}
		if (relative.empty() || relative.is_absolute())
		{
			return "";
		}
		for (const std::filesystem::path& component : relative)
		{
			if (component == "..")
			{
				return "";
			}
		}
		return normalizeRootPath(
			(std::filesystem::u8path(writableCollectionRoot) / relative)
				.lexically_normal().generic_u8string());
	}
	catch (const std::exception&)
	{
		return "";
	}
}

bool resourceInstallWorkspaceExists(const std::string& collectionRoot)
{
	if (collectionRoot.empty())
	{
		return false;
	}
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(
			OnlineUpdate::resourceUpdateWorkspacePath(
				std::filesystem::u8path(collectionRoot)),
				error);
	if (error == std::errc::no_such_file_or_directory)
	{
		return false;
	}
	// Permission and other probe failures are not proof that no transaction
	// exists. Let the transaction reader report the failure and block a scan of
	// a potentially half-switched resource group.
	return error || std::filesystem::exists(status);
}

const char* resourceInstallStatusText(
	OnlineUpdate::ResourceInstallTransactionStatus status)
{
	using Status = OnlineUpdate::ResourceInstallTransactionStatus;
	switch (status)
	{
	case Status::Success:
		return "success";
	case Status::NoTransaction:
		return "no_transaction";
	case Status::InvalidInput:
		return "invalid_input";
	case Status::RecordUnavailable:
		return "record_unavailable";
	case Status::RecordInvalid:
		return "record_invalid";
	case Status::WorkspaceConflict:
		return "workspace_conflict";
	case Status::TargetConflict:
		return "target_conflict";
	case Status::RollbackFailed:
		return "rollback_failed";
	case Status::CleanupFailed:
		return "cleanup_failed";
	default:
		return "unknown";
	}
}

bool resourceInstallResultAllowsScan(
	const OnlineUpdate::ResourceInstallTransactionResult& result)
{
	using State = OnlineUpdate::ResourceInstallTransactionState;
	using Status = OnlineUpdate::ResourceInstallTransactionStatus;
	return result.status == Status::Success ||
		result.status == Status::NoTransaction ||
		result.rolledBack ||
		(result.status == Status::CleanupFailed &&
			(result.state == State::None || result.state == State::Committed));
}

bool resourceInstallResultIsCommitted(
	const OnlineUpdate::ResourceInstallTransactionResult& result)
{
	using State = OnlineUpdate::ResourceInstallTransactionState;
	using Status = OnlineUpdate::ResourceInstallTransactionStatus;
	return result.status == Status::Success ||
		(result.status == Status::CleanupFailed &&
			result.state == State::Committed);
}

OnlineUpdate::ResourceInstallTransactionResult
completeValidatedResourceInstall(
	const std::filesystem::path& collectionRoot,
	bool validationSucceeded,
	bool& commitFailed)
{
	commitFailed = false;
	OnlineUpdate::ResourceInstallTransactionResult result =
		OnlineUpdate::completeResourceInstallTransaction(
			collectionRoot, validationSucceeded);
	if (!validationSucceeded || resourceInstallResultIsCommitted(result))
	{
		return result;
	}
	commitFailed = true;
	return OnlineUpdate::completeResourceInstallTransaction(
		collectionRoot, false);
}

bool validateActivatedResourceGroup(
	const std::string& collectionRoot,
	const OnlineUpdate::ResourceInstallTransactionResult& transaction,
	const std::vector<ResourceManager::ResourcePack>& packs,
	bool requirePlayableResourceGroup,
	std::string& failedGameId)
{
	failedGameId.clear();
	for (const OnlineUpdate::ResourceInstallTarget& target :
		transaction.targets)
	{
		failedGameId = target.gameId;
		std::error_code error;
		const std::filesystem::path expectedPath =
			std::filesystem::canonical(
				std::filesystem::u8path(collectionRoot) /
					std::filesystem::u8path(target.targetDirectoryName),
				error);
		if (error)
		{
			return false;
		}
		if (toLowerAscii(target.gameId) == "common" &&
			target.targetDirectoryName == "common")
		{
			if (!OnlineUpdate::isValidCommonResourceRoot(expectedPath))
			{
				return false;
			}
			continue;
		}
		const std::string expectedRoot =
			normalizeRootPath(expectedPath.generic_u8string());
		std::size_t identifierMatchCount = 0;
		std::size_t exactTargetMatchCount = 0;
		const ResourceManager::ResourcePack* exactPack = nullptr;
		for (const ResourceManager::ResourcePack& pack : packs)
		{
			if (toLowerAscii(pack.manifest.id) !=
				toLowerAscii(target.gameId))
			{
				continue;
			}
			identifierMatchCount++;
			if (pack.rootPath == expectedRoot)
			{
				exactTargetMatchCount++;
				exactPack = &pack;
			}
		}
		if (identifierMatchCount != 1 || exactTargetMatchCount != 1 ||
			exactPack == nullptr)
		{
			return false;
		}
		if (!requirePlayableResourceGroup)
		{
			continue;
		}
		for (const std::string& dependencyId :
			exactPack->manifest.getDependencyIds())
		{
			const std::size_t dependencyCount =
				static_cast<std::size_t>(std::count_if(
					packs.begin(),
					packs.end(),
					[&dependencyId](const ResourceManager::ResourcePack& pack)
					{
						return toLowerAscii(pack.manifest.id) ==
							toLowerAscii(dependencyId);
					}));
			if (dependencyCount != 1)
			{
				failedGameId = dependencyId;
				return false;
			}
		}
	}
	failedGameId.clear();
	return true;
}

std::string trimAscii(std::string value)
{
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
	{
		value.erase(value.begin());
	}
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
	{
		value.pop_back();
	}
	return value;
}

std::string joinRelativePath(std::string left, std::string right)
{
	convert::replaceAllString(left, "\\", "/");
	convert::replaceAllString(right, "\\", "/");
	while (!left.empty() && left.back() == '/')
	{
		left.pop_back();
	}
	while (!right.empty() && right.front() == '/')
	{
		right.erase(right.begin());
	}
	if (left.empty())
	{
		return right;
	}
	if (right.empty())
	{
		return left;
	}
	return left + "/" + right;
}

bool isSafeCatalogRelativePath(std::string relativePath)
{
	if (relativePath.empty() ||
		relativePath.find('\0') != std::string::npos)
	{
		return false;
	}
	convert::replaceAllString(relativePath, "\\", "/");
	const std::filesystem::path path =
		std::filesystem::u8path(relativePath);
	const bool driveQualified =
		relativePath.size() >= 2 &&
		std::isalpha(
			static_cast<unsigned char>(relativePath[0])) &&
		relativePath[1] == ':';
	if (path.is_absolute() || driveQualified)
	{
		return false;
	}
	for (const std::filesystem::path& component : path)
	{
		if (component == "..")
		{
			return false;
		}
	}
	return true;
}

RuntimeResource::CatalogFileReadResult readPackagedCatalogFile(
	const std::filesystem::path& root,
	std::string_view relativePath,
	std::size_t maximumBytes)
{
	RuntimeResource::CatalogFileReadResult result;
	const std::string relativeText(relativePath);
	if (!isSafeCatalogRelativePath(relativeText) ||
		root.generic_u8string().find('\0') != std::string::npos)
	{
		result.status =
			RuntimeResource::CatalogFileReadStatus::UnsafePath;
		return result;
	}

	const std::filesystem::path fullPath =
		(root / std::filesystem::u8path(relativeText)).
			lexically_normal();
	const std::string fullPathText =
		fullPath.generic_u8string();
	if (fullPathText.empty())
	{
		result.status =
			RuntimeResource::CatalogFileReadStatus::UnsafePath;
		return result;
	}

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

	std::unique_ptr<SDL_IOStream, SDLIOStreamCloser> input(
		SDL_IOFromFile(fullPathText.c_str(), "rb"));
	if (input == nullptr)
	{
		SDL_PathInfo pathInfo;
		result.status =
			!SDL_GetPathInfo(
				fullPathText.c_str(), &pathInfo) ||
				pathInfo.type == SDL_PATHTYPE_NONE
			? RuntimeResource::CatalogFileReadStatus::NotFound
			: RuntimeResource::CatalogFileReadStatus::Unavailable;
		return result;
	}

	const Sint64 streamSize = SDL_GetIOSize(input.get());
	if (streamSize >= 0)
	{
		if (static_cast<std::uint64_t>(streamSize) >
			static_cast<std::uint64_t>(maximumBytes))
		{
			result.status =
				RuntimeResource::CatalogFileReadStatus::TooLarge;
			return result;
		}
		try
		{
			result.bytes.resize(
				static_cast<std::size_t>(streamSize));
		}
		catch (const std::bad_alloc&)
		{
			result.status =
				RuntimeResource::CatalogFileReadStatus::Unavailable;
			return result;
		}
		catch (const std::length_error&)
		{
			result.status =
				RuntimeResource::CatalogFileReadStatus::Unavailable;
			return result;
		}
		std::size_t bytesRead = 0;
		while (bytesRead < result.bytes.size())
		{
			const std::size_t count = SDL_ReadIO(
				input.get(),
				result.bytes.data() + bytesRead,
				result.bytes.size() - bytesRead);
			if (count == 0)
			{
				result.bytes.clear();
				result.status =
					RuntimeResource::CatalogFileReadStatus::Unavailable;
				return result;
			}
			bytesRead += count;
		}
		result.status =
			RuntimeResource::CatalogFileReadStatus::Success;
		return result;
	}

	std::uint8_t chunk[64 * 1024];
	for (;;)
	{
		const std::size_t remaining =
			result.bytes.size() < maximumBytes
			? maximumBytes - result.bytes.size()
			: 0;
		const std::size_t requested =
			(std::min)(sizeof(chunk), remaining + 1);
		const std::size_t count =
			SDL_ReadIO(input.get(), chunk, requested);
		if (count == 0)
		{
			result.status =
				RuntimeResource::CatalogFileReadStatus::Success;
			return result;
		}
		if (count > remaining)
		{
			result.bytes.clear();
			result.status =
				RuntimeResource::CatalogFileReadStatus::TooLarge;
			return result;
		}
		try
		{
			result.bytes.insert(
				result.bytes.end(), chunk, chunk + count);
		}
		catch (const std::bad_alloc&)
		{
			result.bytes.clear();
			result.status =
				RuntimeResource::CatalogFileReadStatus::Unavailable;
			return result;
		}
		catch (const std::length_error&)
		{
			result.bytes.clear();
			result.status =
				RuntimeResource::CatalogFileReadStatus::Unavailable;
			return result;
		}
	}
}

RuntimeResource::CatalogDirectoryStatus
getPackagedCatalogDirectoryStatus(
	const std::filesystem::path& path)
{
	if (path.empty())
	{
		// Android's asset namespace is represented by an empty logical root.
		return RuntimeResource::CatalogDirectoryStatus::Exists;
	}
	const std::string pathText = path.generic_u8string();
	if (pathText.empty() ||
		pathText.find('\0') != std::string::npos)
	{
		return RuntimeResource::CatalogDirectoryStatus::Missing;
	}
	SDL_PathInfo pathInfo;
	if (SDL_GetPathInfo(pathText.c_str(), &pathInfo))
	{
		return pathInfo.type == SDL_PATHTYPE_DIRECTORY
			? RuntimeResource::CatalogDirectoryStatus::Exists
			: RuntimeResource::CatalogDirectoryStatus::Missing;
	}
	const std::string manifestProbePath =
		joinRelativePath(pathText, ManifestFileName);
	SDL_IOStream* manifestProbe =
		SDL_IOFromFile(manifestProbePath.c_str(), "rb");
	if (manifestProbe != nullptr)
	{
		SDL_CloseIO(manifestProbe);
		return RuntimeResource::CatalogDirectoryStatus::Exists;
	}
#if defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	if (!path.is_absolute())
	{
		// SDL_IOFromFile can address APK/bundle files even when the platform
		// cannot stat their parent directories.
		return RuntimeResource::CatalogDirectoryStatus::Unknown;
	}
#endif
	return RuntimeResource::CatalogDirectoryStatus::Missing;
}

struct PackagedCatalogDirectoryListContext
{
	std::vector<std::string> names;
	bool allocationFailed = false;
};

SDL_EnumerationResult SDLCALL listPackagedCatalogDirectories(
	void* userData,
	const char*,
	const char* entryName)
{
	if (userData == nullptr || entryName == nullptr)
	{
		return SDL_ENUM_CONTINUE;
	}
	const std::string name(entryName);
	if (name.empty() || name == "." || name == "..")
	{
		return SDL_ENUM_CONTINUE;
	}
	auto* context =
		static_cast<PackagedCatalogDirectoryListContext*>(userData);
	try
	{
		context->names.push_back(name);
	}
	catch (const std::bad_alloc&)
	{
		context->allocationFailed = true;
		return SDL_ENUM_FAILURE;
	}
	catch (const std::length_error&)
	{
		context->allocationFailed = true;
		return SDL_ENUM_FAILURE;
	}
	return SDL_ENUM_CONTINUE;
}

RuntimeResource::CatalogDirectoryListResult
listPackagedCatalogChildDirectories(
	const std::filesystem::path& path)
{
	RuntimeResource::CatalogDirectoryListResult result;
	const std::string pathText = path.generic_u8string();
	if (pathText.find('\0') != std::string::npos)
	{
		return result;
	}

	PackagedCatalogDirectoryListContext context;
	if (!SDL_EnumerateDirectory(
			pathText.c_str(),
			listPackagedCatalogDirectories,
			&context) ||
		context.allocationFailed)
	{
		return result;
	}
	result.status =
		RuntimeResource::CatalogDirectoryListStatus::Success;
	result.childDirectoryNames = std::move(context.names);
	return result;
}

RuntimeResource::ResourceCatalogFileAccess
makePackagedCatalogFileAccess()
{
	RuntimeResource::ResourceCatalogFileAccess fileAccess;
	fileAccess.readFileFromRoot = readPackagedCatalogFile;
	fileAccess.getDirectoryStatus =
		getPackagedCatalogDirectoryStatus;
	fileAccess.listChildDirectories =
		listPackagedCatalogChildDirectories;
	return fileAccess;
}

RuntimeResource::ResourceCatalogSnapshotResult
loadRuntimeResourceCatalogSnapshot(
	const std::string& collectionRoot)
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	const RuntimeResource::ResourceCatalogFileAccess fileAccess =
		makePackagedCatalogFileAccess();
	return RuntimeResource::loadResourceCatalogSnapshot(
		std::filesystem::u8path(collectionRoot), fileAccess);
#else
	return RuntimeResource::loadResourceCatalogSnapshot(
		std::filesystem::u8path(collectionRoot));
#endif
}

RuntimeResource::ResourceCatalogSnapshotResult
loadRuntimeResourceCatalogSnapshot(
	const RuntimeResource::ResourceCatalogRequest& request)
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	const RuntimeResource::ResourceCatalogFileAccess fileAccess =
		makePackagedCatalogFileAccess();
	return RuntimeResource::loadResourceCatalogSnapshot(
		request, fileAccess);
#else
	return RuntimeResource::loadResourceCatalogSnapshot(request);
#endif
}

// 返回移动端固定的外部资源目录。Android 使用公开下载目录下的
// /storage/emulated/0/Download/jxqy/assets/；iOS 本轮未实现。桌面端返回空。
// 返回的路径为真实文件系统绝对路径，可被 SDL_IOFromFile 直接读取。
std::string getExternalResourceRoot()
{
#if defined(__ANDROID__)
	// Android 产品构建只在用户实际开启该功能后扫描；桌面移动替身保留
	// 独立固定 fixture 根，不为资源策略测试引入完整 Config/Engine 链接依赖。
	if (!Config::externalResourcesEnabled)
	{
		return "";
	}
	// 访问公开外部存储目录（Android 11+）需要"所有文件访问权限"，
	// 未授权时跳过扫描，由 UI 引导用户授权。
	if (!AndroidExternalStorage::isAllFilesAccessGranted())
	{
		GameLog::write(
			"ResourceManager: external resources enabled but all-files access not granted; skipping scan\n");
		return "";
	}
	const std::string root =
		AndroidExternalStorage::getExternalResourceDirectoryPath();
	if (root.empty())
	{
		return "";
	}
	return normalizeRootPath(root);
#elif defined(__APPLE__) && TARGET_OS_IOS
	// TODO: iOS 需用 NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,...)
	// 取 Documents 目录，而该 .cpp 未按 Objective-C++ 编译；本轮暂不实现，留作后续。
	return "";
#elif defined(__MOBILE__)
	// 桌面移动替身：锚定到默认资源集合根的同级目录（assets 旁），并转成绝对路径，
	// 避免依赖进程 CWD 导致 PC 验证时扫到意外目录或静默扫不到。
	try
	{
		const std::filesystem::path collectionRoot =
			std::filesystem::u8path(getDefaultCollectionRoot());
		const std::filesystem::path surrogate =
			collectionRoot / ".." / "mobile-external-resources";
		std::error_code absoluteError;
		std::filesystem::path absolute =
			std::filesystem::absolute(surrogate, absoluteError);
		if (absoluteError || absolute.empty())
		{
			absolute = surrogate;
		}
		return normalizeRootPath(absolute.lexically_normal().generic_u8string());
	}
	catch (const std::exception&)
	{
		return "";
	}
#else
	return "";
#endif
}

RuntimeResource::ExactSelectionResult
resolveRuntimeResourceCatalogEntry(
	const std::string& collectionRoot,
	std::string_view stableEntryKey)
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	const RuntimeResource::ResourceCatalogFileAccess fileAccess =
		makePackagedCatalogFileAccess();
	return RuntimeResource::resolveResourceCatalogEntrySelection(
		std::filesystem::u8path(collectionRoot),
		stableEntryKey,
		fileAccess);
#else
	return RuntimeResource::resolveResourceCatalogEntrySelection(
		std::filesystem::u8path(collectionRoot),
		stableEntryKey);
#endif
}

RuntimeResource::ExactSelectionResult
resolveRuntimeResourceCatalogEntry(
	const RuntimeResource::ResourceCatalogRequest& request,
	std::string_view stableEntryKey)
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	const RuntimeResource::ResourceCatalogFileAccess fileAccess =
		makePackagedCatalogFileAccess();
	return RuntimeResource::resolveResourceCatalogEntrySelection(
		request,
		stableEntryKey,
		fileAccess);
#else
	return RuntimeResource::resolveResourceCatalogEntrySelection(
		request,
		stableEntryKey);
#endif
}

int findPackIndexById(const std::vector<ResourceManager::ResourcePack>& packs, const std::string& id, int excludeIndex = -1)
{
	if (id.empty())
	{
		return -1;
	}
	std::string target = toLowerAscii(id);
	int selectedIndex = -1;
	for (int i = 0; i < (int)packs.size(); i++)
	{
		if (i == excludeIndex)
		{
			continue;
		}
		if (toLowerAscii(packs[i].manifest.id) == target)
		{
			if (selectedIndex < 0 ||
				packs[i].discoveryOrder <
					packs[selectedIndex].discoveryOrder)
			{
				selectedIndex = i;
			}
		}
	}
	return selectedIndex;
}

int findPackIndexByRoot(const std::vector<ResourceManager::ResourcePack>& packs, const std::string& rootPath)
{
	const std::string target = normalizeRootPath(rootPath);
	for (int i = 0; i < (int)packs.size(); i++)
	{
		if (normalizeRootPath(packs[i].rootPath) == target)
		{
			return i;
		}
	}
	return -1;
}

int findPackIndexByCatalogKey(
	const std::vector<ResourceManager::ResourcePack>& packs,
	const std::string& catalogEntryKey)
{
	const std::string target =
		toLowerAscii(trimAscii(catalogEntryKey));
	if (target.empty())
	{
		return -1;
	}
	for (int index = 0;
		index < static_cast<int>(packs.size());
		++index)
	{
		const std::string& candidate =
			packs[index].selectionEntryKey.empty()
				? packs[index].catalogEntryKey
				: packs[index].selectionEntryKey;
		if (toLowerAscii(candidate) ==
			target)
		{
			return index;
		}
	}
	return -1;
}

std::string describePack(const ResourceManager::ResourcePack& pack)
{
	if (!pack.manifest.id.empty())
	{
		return pack.manifest.id;
	}
	return pack.rootPath;
}

std::string stableNamespaceSuffix(std::string value)
{
	std::string result;
	for (char character : value)
	{
		const unsigned char byte =
			static_cast<unsigned char>(character);
		if (byte >= 0x80 || std::isalnum(byte) ||
			character == '-' || character == '_')
		{
			result.push_back(character);
		}
		else if (character == '.' || character == '/' ||
			character == '\\' || character == ':')
		{
			result.push_back('_');
		}
	}
	return result.empty() ? "entry" : result;
}

std::string stableImportRootHash(const std::string& rootPath)
{
	const std::string normalized =
		toLowerAscii(normalizeRootPath(rootPath));
	std::uint64_t hash = UINT64_C(14695981039346656037);
	for (const unsigned char byte : normalized)
	{
		hash ^= byte;
		hash *= UINT64_C(1099511628211);
	}

	constexpr char HexDigits[] = "0123456789abcdef";
	std::string result(8, '0');
	for (int index = 7; index >= 0; --index)
	{
		result[static_cast<std::size_t>(index)] =
			HexDigits[hash & UINT64_C(0xf)];
		hash >>= 4;
	}
	return result;
}

void appendFilesystemResourceCatalogRoots(
	const std::string& filesystemRoot,
	const std::string& stableKeyPrefix,
	const std::string& sourceTag,
	bool replacesPrimaryGameId,
	RuntimeResource::ResourceCatalogRequest& request)
{
	if (filesystemRoot.empty())
	{
		return;
	}
	std::error_code iterateError;
	const std::filesystem::path rootPath =
		std::filesystem::u8path(filesystemRoot);
	std::filesystem::directory_iterator iterator;
	try
	{
		iterator = std::filesystem::directory_iterator(
			rootPath, iterateError);
	}
	catch (const std::exception& exception)
	{
		GameLog::write(
			"ResourceManager: resource directory %s could not be iterated: %s\n",
			filesystemRoot.c_str(), exception.what());
		return;
	}
	if (iterateError)
	{
		std::error_code existsError;
		if (std::filesystem::exists(rootPath, existsError) && !existsError)
		{
			GameLog::write(
				"ResourceManager: resource directory %s exists but could not be iterated: %s\n",
				filesystemRoot.c_str(), iterateError.message().c_str());
		}
		return;
	}

	std::vector<std::string> childRoots;
	const std::filesystem::directory_iterator endIterator;
	while (iterator != endIterator)
	{
		const std::filesystem::directory_entry entry = *iterator;
		std::error_code statusError;
		const std::filesystem::file_status status =
			entry.symlink_status(statusError);
		const std::string folder =
			entry.path().filename().generic_u8string();
		if (!statusError && std::filesystem::is_directory(status) &&
			!std::filesystem::is_symlink(status) && !folder.empty() &&
			folder.front() != '.' &&
			hasResourceManifestEntry(entry.path()))
		{
			const std::string childRoot = normalizeRootPath(
				entry.path().lexically_normal().generic_u8string());
			if (!childRoot.empty())
			{
				childRoots.push_back(childRoot);
			}
		}
		iterator.increment(iterateError);
		if (iterateError)
		{
			GameLog::write(
				"ResourceManager: resource directory %s could not be completely iterated: %s\n",
				filesystemRoot.c_str(), iterateError.message().c_str());
			break;
		}
	}
	std::sort(childRoots.begin(), childRoots.end(),
		[](const std::string& left, const std::string& right)
		{
			const std::string normalizedLeft = toLowerAscii(left);
			const std::string normalizedRight = toLowerAscii(right);
			return normalizedLeft != normalizedRight
				? normalizedLeft < normalizedRight
				: left < right;
		});
	if (childRoots.size() > RuntimeResource::MaximumCatalogPackCount)
	{
		GameLog::write(
			"ResourceManager: resource candidate limit reached under %s; keeping the first %zu of %zu sorted directories\n",
			filesystemRoot.c_str(),
			RuntimeResource::MaximumCatalogPackCount,
			childRoots.size());
		childRoots.resize(RuntimeResource::MaximumCatalogPackCount);
	}

	const std::size_t originalCount = request.supplementalRoots.size();
	for (const std::string& childRoot : childRoots)
	{
		const std::string folder = extractFolderName(childRoot);
		request.supplementalRoots.push_back(
			{
				std::filesystem::u8path(childRoot),
				stableKeyPrefix + ":" + stableNamespaceSuffix(folder) +
					":root:" + stableImportRootHash(childRoot),
				sourceTag,
				replacesPrimaryGameId
			});
	}
	const std::size_t appendedCount =
		request.supplementalRoots.size() - originalCount;
	if (appendedCount != 0)
	{
		GameLog::write(
			"ResourceManager: enumerated %zu resource candidate(s) under %s\n",
			appendedCount, filesystemRoot.c_str());
	}
}

}

bool ResourceManager::ResourcePack::isBaseGame() const
{
	return manifest.isBaseGame();
}

bool ResourceManager::ResourcePack::isLaunchable() const
{
	return !manifest.resourceOnly;
}

std::string ResourceManager::ResourcePack::getDisplayName() const
{
	std::string displayName = manifest.name;
	if (displayName.empty())
	{
		displayName = manifest.id;
	}
	if (displayName.empty())
	{
		displayName = rootPath;
	}
	return displayName;
}

std::string ResourceManager::ResourcePack::getDisplayAuthorText() const
{
	if (manifest.author.empty())
	{
		return std::string();
	}
	if (manifest.author == u8"原版")
	{
		return manifest.author;
	}
	return u8"作者：" + manifest.author;
}

ResourceManager& ResourceManager::instance()
{
	static ResourceManager instance;
	return instance;
}

std::string ResourceManager::normalizeRoot(const std::string& path) const
{
	return normalizeRootPath(path);
}

int ResourceManager::findPackById(const std::string& id, int excludeIndex) const
{
	return findPackIndexById(discoveredPacks, id, excludeIndex);
}

bool ResourceManager::applyActiveResourcePack(int index)
{
	std::string blockingReason;
	if (!canActivateResourcePack(index, &blockingReason))
	{
		GameLog::write(
			"ResourceManager: resource pack cannot be activated: %s\n",
			blockingReason.c_str());
		return false;
	}

	// Every platform materializes dependencies, UI, defaults, diagnostics, and
	// conflicts through ResourceCatalog. Packaged platforms change only the
	// bounded byte/directory adapter used by that shared resolver.
	const ResourcePack& selectedPack = discoveredPacks[index];
	if (selectedPack.catalogEntryKey.empty())
	{
		GameLog::write(
			"ResourceManager: selected pack has no shared catalog entry key\n");
		return false;
	}
	const RuntimeResource::ExactSelectionResult selectionResult =
		!currentCatalogRequestValid
		? resolveRuntimeResourceCatalogEntry(
			selectedPack.catalogCollectionRoot,
			selectedPack.catalogEntryKey)
		: resolveRuntimeResourceCatalogEntry(
			currentCatalogRequest,
			selectedPack.catalogEntryKey);
	if (!selectionResult.succeeded())
	{
		GameLog::write(
			"ResourceManager: shared selection failed [%s] for collection %s entry %s: %s\n",
			selectionResult.diagnosticCode.c_str(),
			(!currentCatalogRequestValid
				? selectedPack.catalogCollectionRoot
				: currentCatalogRequest.primaryCollectionRoot.generic_u8string()).c_str(),
			selectedPack.catalogEntryKey.c_str(),
			selectionResult.message.c_str());
		return false;
	}

	const RuntimeResource::ExactResourceSelection& selection =
		selectionResult.selection;
	activeResourceRoot = normalizeRoot(
		selection.activeResourceRoot.generic_u8string());
	activeResourceEntryKey =
		!selection.stableActiveEntryKey.empty()
		? selection.stableActiveEntryKey
		: (!selectedPack.selectionEntryKey.empty()
			? selectedPack.selectionEntryKey
			: selectedPack.catalogEntryKey);
	activeManifest = selection.activeManifest;
	activeResourceSelectionValid = true;
	const std::string selectedCommonResourceRoot = normalizeRoot(
		selection.commonResourceRoot.generic_u8string());
	// A packaged application may intentionally omit Common while keeping its
	// logical CommonPath in resources.ini. In that layout the catalog snapshot
	// has already mapped the logical bundled path to the platform-writable
	// collection. An exact selection cannot stat the omitted bundled directory,
	// so keep the snapshot value instead of discarding the writable mapping.
	if (!selectedCommonResourceRoot.empty())
	{
		commonResourceRoot = selectedCommonResourceRoot;
	}
	writableCommonResourceRoot = resolveWritableCommonResourceRoot(
		assetsCollectionRoot,
		writableResourceCollectionRoot,
		commonResourceRoot);

	std::vector<std::string> fallbackRoots;
	for (std::size_t rootIndex = 1;
		rootIndex < selection.orderedContentRoots.size();
		++rootIndex)
	{
		if (selection.orderedContentRoots[rootIndex].kind ==
			RuntimeResource::ContentRootKind::Common)
		{
			continue;
		}
		const std::string fallbackRoot = normalizeRoot(
			selection.orderedContentRoots[rootIndex]
				.root.generic_u8string());
		if (!fallbackRoot.empty() &&
			std::find(
				fallbackRoots.begin(),
				fallbackRoots.end(),
				fallbackRoot) == fallbackRoots.end())
		{
			fallbackRoots.push_back(fallbackRoot);
		}
	}
	std::vector<std::string> uiFallbackRoots;
	for (const std::filesystem::path& uiRoot :
		selection.orderedUiFallbackRoots)
	{
		const std::string normalizedUiRoot =
			normalizeRoot(uiRoot.generic_u8string());
		if (!normalizedUiRoot.empty() &&
			std::find(
				uiFallbackRoots.begin(),
				uiFallbackRoots.end(),
				normalizedUiRoot) ==
				uiFallbackRoots.end())
		{
			uiFallbackRoots.push_back(normalizedUiRoot);
		}
	}
	if (!writableCommonResourceRoot.empty() &&
		writableCommonResourceRoot != commonResourceRoot &&
		std::find(
			uiFallbackRoots.begin(),
			uiFallbackRoots.end(),
			writableCommonResourceRoot) == uiFallbackRoots.end())
	{
		uiFallbackRoots.push_back(writableCommonResourceRoot);
	}

	File::setActiveResourceRoot(activeResourceRoot);
	applyCommonResourceRouting();
	File::setResourceFallbackRoots(fallbackRoots);
	File::setUiResourceFallbackRoots(
		uiFallbackRoots,
		selection.preferLocalUi,
		commonResourceRoot);
	File::setActiveSaveNamespace(
		selection.effectiveSaveNamespace);
	const bool writableCommonAvailable =
		!writableCommonResourceRoot.empty() &&
		isPlainFilesystemDirectory(
			std::filesystem::u8path(
				writableCommonResourceRoot));
	for (const RuntimeResource::CatalogDiagnostic& diagnostic :
		selectionResult.diagnostics)
	{
		if (diagnostic.code ==
				"resource.catalog.common_root_unavailable" &&
			writableCommonAvailable)
		{
			continue;
		}
		GameLog::write(
			"ResourceManager: selection diagnostic [%s] entry=%s id=%s: %s\n",
			diagnostic.code.c_str(),
			diagnostic.stableEntryKey.c_str(),
			diagnostic.resourcePackId.c_str(),
			diagnostic.message.c_str());
	}
	return true;
}

void ResourceManager::scanCollectionRoot(const std::string& collectionRoot)
{
	discoveredPacks.clear();
	resourceCatalogDiagnostics.clear();
	commonResourceRoot.clear();
	writableCommonResourceRoot.clear();
	updateSourceUrl.clear();
	resourceCatalogUrl.clear();
	applicationCatalogUrl.clear();

	std::string root = normalizeRoot(collectionRoot);
	currentCatalogRequest = {};
	currentCatalogRequest.primaryCollectionRoot =
		std::filesystem::u8path(root);
	appendWritableResourceCatalogRoots(currentCatalogRequest);
	appendExternalResourceCatalogRoots(currentCatalogRequest);
	currentCatalogRequestValid = true;
	const RuntimeResource::ResourceCatalogSnapshotResult catalogResult =
		loadRuntimeResourceCatalogSnapshot(currentCatalogRequest);
	if (catalogResult.succeeded())
	{
		const RuntimeResource::ResourceCatalogSnapshot& snapshot =
			catalogResult.snapshot;
		resourceCatalogDiagnostics = snapshot.diagnostics;
		updateSourceUrl = snapshot.updateSourceUrl;
		resourceCatalogUrl = snapshot.resourceCatalogUrl;
		applicationCatalogUrl = snapshot.applicationCatalogUrl;
		if (!snapshot.commonResourceRoot.empty())
		{
			commonResourceRoot = normalizeRoot(
				snapshot.commonResourceRoot.generic_u8string());
			writableCommonResourceRoot =
				resolveWritableCommonResourceRoot(
					assetsCollectionRoot,
					writableResourceCollectionRoot,
					commonResourceRoot);
		}
		for (const RuntimeResource::CatalogDiagnostic& diagnostic :
			snapshot.diagnostics)
		{
			if (diagnostic.code ==
					"resource.catalog.release_metadata_defaulted" ||
				diagnostic.code ==
					"resource.catalog.cover_defaulted" ||
				diagnostic.code ==
					"resource.catalog.description_defaulted" ||
				diagnostic.code ==
					"resource.catalog.ui_defaulted")
			{
				// Defer default warnings until the affected entry is selected.
				continue;
			}
			GameLog::write(
				"ResourceManager: catalog diagnostic [%s] entry=%s id=%s path=%s: %s\n",
				diagnostic.code.c_str(),
				diagnostic.stableEntryKey.c_str(),
				diagnostic.resourcePackId.c_str(),
				diagnostic.hostPath.generic_u8string().c_str(),
				diagnostic.message.c_str());
		}
		discoveredPacks.reserve(snapshot.entries.size());
		for (const RuntimeResource::ResourceCatalogEntry& entry :
			snapshot.entries)
		{
			ResourcePack pack;
			pack.rootPath = normalizeRoot(
				entry.root.generic_u8string());
			const std::filesystem::path relativeManifest =
				entry.manifestPath.empty()
					? std::filesystem::path()
					: entry.manifestPath.lexically_relative(
						entry.root);
			pack.manifestPath =
				relativeManifest.generic_u8string();
			pack.manifest = entry.manifest;
			pack.manifest.resourceRoot = pack.rootPath;
			pack.catalogCollectionRoot = normalizeRoot(
				snapshot.collectionRoot.generic_u8string());
			pack.catalogEntryKey = entry.stableKey;
			pack.sourceTag = entry.sourceTag;
			pack.selectionEntryKey = entry.stableKey;
			pack.effectiveSaveNamespace =
				entry.effectiveSaveNamespace;
			pack.saveNamespaceAdjusted =
				entry.saveNamespaceAdjusted;
			pack.discoveryOrder =
				static_cast<int>(entry.discoveryOrder);
			pack.compatibility =
				ModRelease::evaluateCompatibility(
					pack.manifest.releaseMetadata,
					currentEngineVersion());
			// 无效声明只提示未知风险；有效且过高的声明由进入门禁处理。
			if (pack.compatibility.status ==
				ModRelease::CompatibilityStatus::InvalidMinimumEngineVersion)
			{
				GameLog::write(
					"ResourceManager: resource pack %s has invalid MinimumEngineVersion=%s; "
					"allowing launch with unknown compatibility\n",
					pack.manifest.id.c_str(),
					pack.manifest.releaseMetadata.minimumEngineVersion.c_str());
			}
			discoveredPacks.push_back(std::move(pack));
		}
		GameLog::write(
			"ResourceManager: shared catalog discovered %zu retained resource entries\n",
			discoveredPacks.size());
		return;
	}

	GameLog::write(
		"ResourceManager: shared catalog failed [%s] at %s: %s\n",
		catalogResult.diagnosticCode.c_str(),
		catalogResult.hostPath.generic_u8string().c_str(),
		catalogResult.message.c_str());
}

bool ResourceManager::initialize(const std::string& assetsArg)
{
	if (initialized)
	{
		return true;
	}
	initialized = true;
	const auto useEmptyResourceRouting = [this]()
	{
		discoveredPacks.clear();
		resourceCatalogDiagnostics.clear();
		currentCatalogRequest = {};
		currentCatalogRequestValid = false;
		assetsCollectionRoot.clear();
		writableResourceCollectionRoot.clear();
		commonResourceRoot.clear();
		writableCommonResourceRoot.clear();
		updateSourceUrl.clear();
		resourceCatalogUrl.clear();
		applicationCatalogUrl.clear();
		activeResourceRoot.clear();
		activeResourceEntryKey.clear();
		activeManifest = ResourceManifest::createDefault("");
		activeResourceSelectionValid = false;
		File::setAssetsCollectionRoot("");
		File::setActiveResourceRoot("");
		File::setCommonResourceRoot("");
		File::setCommonResourceFallbackRoots({});
		File::setResourceFallbackRoots({});
		File::setUiResourceFallbackRoots({}, true, "");
		File::setActiveSaveNamespace("");
	};

	std::string collectionRoot;
	if (!assetsArg.empty())
	{
		if (!resolveCollectionRoot(assetsArg, collectionRoot))
		{
			GameLog::write(
				"ResourceManager: invalid --assets collection root %s\n",
				assetsArg.c_str());
			useEmptyResourceRouting();
			return true;
		}
		GameLog::write(
			"ResourceManager: --assets = %s\n",
			collectionRoot.c_str());
	}
	else
	{
		const std::string defaultCollectionRoot =
			getDefaultCollectionRoot();
		if (!resolveCollectionRoot(
				defaultCollectionRoot, collectionRoot))
		{
			GameLog::write(
				"ResourceManager: invalid default collection root %s\n",
				defaultCollectionRoot.c_str());
			useEmptyResourceRouting();
			return true;
		}
		GameLog::write(
			"ResourceManager: using default collection root %s\n",
			collectionRoot.c_str());
	}
	assetsCollectionRoot = collectionRoot;
	writableResourceCollectionRoot =
		prepareWritableResourceCollectionRoot(collectionRoot);
	if (writableResourceCollectionRoot.empty())
	{
		GameLog::write(
			"ResourceManager: platform writable resource collection is unavailable; online install remains disabled\n");
	}

	// 设置 File 层的 collection root，使后续 fileExist/readFile 能正确解析。
	File::setAssetsCollectionRoot(collectionRoot);

	OnlineUpdate::ResourceInstallTransactionResult installTransaction;
	bool validateInstalledResourceGroup = false;
	if (resourceInstallWorkspaceExists(writableResourceCollectionRoot))
	{
		installTransaction =
			OnlineUpdate::beginResourceInstallTransaction(
				std::filesystem::u8path(
					writableResourceCollectionRoot));
		validateInstalledResourceGroup = installTransaction.needsValidation;
		if (!resourceInstallResultAllowsScan(installTransaction))
		{
			GameLog::write(
				"ResourceManager: resource install recovery failed [%s] id=%s path=%s; resource scan blocked\n",
				resourceInstallStatusText(installTransaction.status),
				installTransaction.failedGameId.c_str(),
				installTransaction.filesystemPath.generic_u8string().c_str());
			useEmptyResourceRouting();
			return true;
		}
		if (installTransaction.status !=
				OnlineUpdate::ResourceInstallTransactionStatus::Success &&
			installTransaction.status !=
				OnlineUpdate::ResourceInstallTransactionStatus::NoTransaction)
		{
			GameLog::write(
				"ResourceManager: resource install recovery completed with warning [%s] path=%s\n",
				resourceInstallStatusText(installTransaction.status),
				installTransaction.filesystemPath.generic_u8string().c_str());
		}
	}

	// 主集合与已启用的移动端外部目录由同一个共享清单一次解析。
	scanCollectionRoot(collectionRoot);
	if (validateInstalledResourceGroup)
	{
		std::string failedGameId;
		const bool resourceGroupValid = validateActivatedResourceGroup(
			writableResourceCollectionRoot,
			installTransaction,
			discoveredPacks,
			true,
			failedGameId);
		bool commitFailed = false;
		const OnlineUpdate::ResourceInstallTransactionResult completion =
			completeValidatedResourceInstall(
				std::filesystem::u8path(
					writableResourceCollectionRoot),
				resourceGroupValid,
				commitFailed);
		if (!resourceGroupValid || commitFailed)
		{
			if (completion.status !=
					OnlineUpdate::ResourceInstallTransactionStatus::Success ||
				!completion.rolledBack)
			{
				GameLog::write(
					"ResourceManager: installed resource %s failed for %s and rollback failed [%s] path=%s; resource scan blocked\n",
					commitFailed ? "commit" : "validation",
					failedGameId.c_str(),
					resourceInstallStatusText(completion.status),
					completion.filesystemPath.generic_u8string().c_str());
				useEmptyResourceRouting();
				return true;
			}
			GameLog::write(
				"ResourceManager: installed resource %s failed for %s; restored previous resource group\n",
				commitFailed ? "commit" : "validation",
				failedGameId.c_str());
			scanCollectionRoot(collectionRoot);
		}
		else if (completion.status !=
			OnlineUpdate::ResourceInstallTransactionStatus::Success)
		{
			// The new group is already valid and live. A record/cleanup warning is
			// retried on the next startup and must not make valid resources unusable.
			GameLog::write(
				"ResourceManager: installed resource group is valid, but transaction cleanup is pending [%s] path=%s\n",
				resourceInstallStatusText(completion.status),
				completion.filesystemPath.generic_u8string().c_str());
		}
	}
	promoteRecentResourcePackSelection();
	applyCommonResourceRouting();
	activeResourceRoot.clear();
	activeResourceEntryKey.clear();
	activeManifest = ResourceManifest();
	activeResourceSelectionValid = false;
	File::setActiveResourceRoot("");
	File::setResourceFallbackRoots({});
	std::vector<std::string> startupUiFallbackRoots;
	if (!writableCommonResourceRoot.empty() &&
		writableCommonResourceRoot != commonResourceRoot)
	{
		startupUiFallbackRoots.push_back(writableCommonResourceRoot);
	}
	File::setUiResourceFallbackRoots(
		startupUiFallbackRoots, true, commonResourceRoot);
	File::setActiveSaveNamespace("");

	if (discoveredPacks.empty())
	{
		// 集合根和 resources.ini 条目都没有有效的 game_profile.ini。此时不
		// 终止进程：让 needsSelection() 为 true，由资源选择/管理界面展示问题
		// 并允许用户导入已经完成转换的资源包目录。
		GameLog::write(
			"ResourceManager: collection root has no valid game_profile.ini; "
			"deferring to resource selection/import UI\n");
		return true;
	}

	// 普通启动始终由资源选择页确认进入；即使只有一个已下载资源，
	// 下载或上次选择也不等于本次已确认进入。命令行和编辑器精确路由仍可
	// 通过 setActiveResourcePackById()/installEditorRunSelection() 显式安装活动路由。
	GameLog::write(
		"ResourceManager: found %zu resource pack(s), explicit selection required\n",
		discoveredPacks.size());
	return true;
}

void ResourceManager::appendWritableResourceCatalogRoots(
	RuntimeResource::ResourceCatalogRequest& request) const
{
	if (writableResourceCollectionRoot.empty() ||
		normalizeRootPath(writableResourceCollectionRoot) ==
			normalizeRootPath(assetsCollectionRoot))
	{
		return;
	}
	appendFilesystemResourceCatalogRoots(
		writableResourceCollectionRoot,
		"application",
		WritableResourceSourceTag,
		true,
		request);
}

void ResourceManager::applyCommonResourceRouting() const
{
	const std::string preferredRoot =
		!writableCommonResourceRoot.empty()
			? writableCommonResourceRoot
			: commonResourceRoot;
	File::setCommonResourceRoot(preferredRoot);
	if (!commonResourceRoot.empty() &&
		commonResourceRoot != preferredRoot)
	{
		File::setCommonResourceFallbackRoots({ commonResourceRoot });
	}
	else
	{
		File::setCommonResourceFallbackRoots({});
	}
}

void ResourceManager::appendExternalResourceCatalogRoots(
	RuntimeResource::ResourceCatalogRequest& request) const
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	const std::string externalRoot = getExternalResourceRoot();
	if (externalRoot.empty())
	{
		return;
	}
	appendFilesystemResourceCatalogRoots(
		externalRoot,
		"external",
		ExternalResourceSourceTag,
		false,
		request);
#else
	// 桌面端无固定外部资源目录：资源通过 assets/ 集合根或显式导入处理。
	(void)request;
#endif
}

int ResourceManager::rescanExternalResourceDirectory()
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	const bool previousActiveSelectionValid =
		activeResourceSelectionValid;
	const std::string previousActiveRoot = activeResourceRoot;
	std::string previousActiveEntryKey = activeResourceEntryKey;
	if (previousActiveSelectionValid &&
		previousActiveEntryKey.empty())
	{
		const int previousIndex = findPackIndexByRoot(
			discoveredPacks, previousActiveRoot);
		if (previousIndex >= 0)
		{
			previousActiveEntryKey =
				discoveredPacks[previousIndex].selectionEntryKey;
		}
	}

	// 重新生成完整联合清单，而不是单独增删外部包。这样启用或关闭外部
	// 目录后，DependencyId、UI.BaseId、Game.Type 和存档命名空间都会基于
	// 同一组资源重新得到确定结果。
	scanCollectionRoot(assetsCollectionRoot);
	promoteRecentResourcePackSelection();

	int activeIndex = findPackIndexByCatalogKey(
		discoveredPacks, previousActiveEntryKey);
	if (activeIndex < 0 && previousActiveSelectionValid)
	{
		activeIndex = findPackIndexByRoot(
			discoveredPacks, previousActiveRoot);
	}
	if (previousActiveSelectionValid && activeIndex >= 0 &&
		applyActiveResourcePack(activeIndex))
	{
		// applyActiveResourcePack re-materialized the current shared routing.
	}
	else if (previousActiveSelectionValid)
	{
		GameLog::write(
			"ResourceManager: active resource pack disappeared or could not be restored after external rescan; clearing routing\n");
		activeResourceRoot.clear();
		activeResourceEntryKey.clear();
		activeManifest = ResourceManifest();
		activeResourceSelectionValid = false;
		File::setActiveResourceRoot("");
		applyCommonResourceRouting();
		File::setResourceFallbackRoots({});
		std::vector<std::string> commonUiFallbackRoots;
		if (!writableCommonResourceRoot.empty() &&
			writableCommonResourceRoot != commonResourceRoot)
		{
			commonUiFallbackRoots.push_back(
				writableCommonResourceRoot);
		}
		File::setUiResourceFallbackRoots(
			commonUiFallbackRoots, true, commonResourceRoot);
		File::setActiveSaveNamespace("");
	}
	else
	{
		applyCommonResourceRouting();
	}

	const int externalCount = static_cast<int>(std::count_if(
		discoveredPacks.begin(), discoveredPacks.end(),
		[](const ResourcePack& pack)
		{
			return pack.sourceTag == ExternalResourceSourceTag;
		}));
	if (externalCount > 0)
	{
		GameLog::write(
			"ResourceManager: rescan found %d external resource pack(s)\n",
			externalCount);
	}
	return externalCount;
#else
	return 0;
#endif
}

bool ResourceManager::installEditorRunSelection(
	const RuntimeResource::ExactResourceSelection& selection)
{
#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	(void)selection;
	return false;
#else
	// Editor-run routing is a one-shot startup transition. Refuse to replace an
	// already initialized legacy manager or a File layout that has already
	// frozen its resource-routing snapshot.
	if (initialized || File::hasEditorRunFileLayout())
	{
		return false;
	}
	if (selection.activeManifest.resourceOnly)
	{
		return false;
	}

	const std::string collectionRoot =
		normalizeRoot(
			selection.assetsCollectionRoot.generic_u8string());
	const std::string selectedActiveRoot =
		normalizeRoot(
			selection.activeResourceRoot.generic_u8string());
	if (collectionRoot.empty())
	{
		return false;
	}
	if (selectedActiveRoot.empty())
	{
		File::setAssetsCollectionRoot(collectionRoot);
		File::setActiveResourceRoot("");
		File::setCommonResourceRoot("");
		File::setCommonResourceFallbackRoots({});
		File::setResourceFallbackRoots({});
		File::setUiResourceFallbackRoots({}, true, "");
		File::setActiveSaveNamespace(
			selection.effectiveSaveNamespace.empty()
				? "editor-run"
				: selection.effectiveSaveNamespace);

		discoveredPacks.clear();
		resourceCatalogDiagnostics.clear();
		assetsCollectionRoot = collectionRoot;
		writableResourceCollectionRoot = collectionRoot;
		commonResourceRoot.clear();
		writableCommonResourceRoot.clear();
		activeResourceRoot.clear();
		activeResourceEntryKey.clear();
		activeManifest = selection.activeManifest;
		if (!activeManifest.isValid())
		{
			activeManifest = ResourceManifest::createDefault("");
		}
		activeResourceSelectionValid = false;
		initialized = true;
		return true;
	}
	if (
		selection.canonicalActiveResourcePackId.empty() ||
		selection.effectiveSaveNamespace.empty() ||
		!selection.activeManifest.isValid() ||
		toLowerAscii(selection.activeManifest.id) !=
			toLowerAscii(
				selection.canonicalActiveResourcePackId) ||
		selection.orderedContentRoots.empty())
	{
		return false;
	}

	const RuntimeResource::ContentRoot& activeContentRoot =
		selection.orderedContentRoots.front();
	if (activeContentRoot.kind !=
			RuntimeResource::ContentRootKind::Active ||
		normalizeRoot(
			activeContentRoot.root.generic_u8string()) !=
			selectedActiveRoot)
	{
		return false;
	}

	std::vector<std::string> contentFallbackRoots;
	contentFallbackRoots.reserve(
		selection.orderedContentRoots.size() - 1);
	for (std::size_t index = 1;
		index < selection.orderedContentRoots.size();
		++index)
	{
		const RuntimeResource::ContentRoot& contentRoot =
			selection.orderedContentRoots[index];
		if (contentRoot.kind ==
			RuntimeResource::ContentRootKind::Common)
		{
			continue;
		}
		const std::string normalizedRoot =
			normalizeRoot(contentRoot.root.generic_u8string());
		if (normalizedRoot.empty() ||
			contentRoot.kind ==
				RuntimeResource::ContentRootKind::Active)
		{
			return false;
		}
		if (std::find(
				contentFallbackRoots.begin(),
				contentFallbackRoots.end(),
				normalizedRoot) ==
			contentFallbackRoots.end())
		{
			contentFallbackRoots.push_back(normalizedRoot);
		}
	}

	std::vector<std::string> uiFallbackRoots;
	uiFallbackRoots.reserve(
		selection.orderedUiFallbackRoots.size());
	for (const std::filesystem::path& root :
		selection.orderedUiFallbackRoots)
	{
		const std::string normalizedRoot =
			normalizeRoot(root.generic_u8string());
		if (normalizedRoot.empty())
		{
			return false;
		}
		if (normalizedRoot != selectedActiveRoot &&
			std::find(
				uiFallbackRoots.begin(),
				uiFallbackRoots.end(),
				normalizedRoot) ==
				uiFallbackRoots.end())
		{
			uiFallbackRoots.push_back(normalizedRoot);
		}
	}

	const std::string selectedCommonRoot =
		normalizeRoot(
			selection.commonResourceRoot.generic_u8string());

	// Do not call scanCollectionRoot(), promoteRecentResourcePackSelection(),
	// applyActiveResourcePack(), or any logging API here. Every routing value
	// comes from the shared catalog resolver.
	File::setAssetsCollectionRoot(collectionRoot);
	File::setActiveResourceRoot(selectedActiveRoot);
	File::setCommonResourceRoot(selectedCommonRoot);
	File::setCommonResourceFallbackRoots({});
	File::setResourceFallbackRoots(contentFallbackRoots);
	File::setUiResourceFallbackRoots(
		uiFallbackRoots,
		selection.preferLocalUi,
		selectedCommonRoot);
	File::setActiveSaveNamespace(
		selection.effectiveSaveNamespace);

	discoveredPacks.clear();
	resourceCatalogDiagnostics.clear();
	currentCatalogRequest = {};
	currentCatalogRequestValid = false;
	assetsCollectionRoot = collectionRoot;
	writableResourceCollectionRoot = collectionRoot;
	commonResourceRoot = selectedCommonRoot;
	writableCommonResourceRoot = selectedCommonRoot;
	updateSourceUrl.clear();
	resourceCatalogUrl.clear();
	applicationCatalogUrl.clear();
	activeResourceRoot = selectedActiveRoot;
	activeResourceEntryKey = selection.stableActiveEntryKey;
	activeManifest = selection.activeManifest;
	activeResourceSelectionValid = true;
	initialized = true;
	return true;
#endif
}

const std::string& ResourceManager::getActiveResourceRoot() const
{
	return activeResourceRoot;
}

const ResourceManifest& ResourceManager::getActiveManifest() const
{
	if (!activeResourceSelectionValid)
	{
		// 未选择时返回默认 manifest
		static ResourceManifest defaultManifest = ResourceManifest::createDefault("");
		return defaultManifest;
	}
	return activeManifest;
}

bool ResourceManager::isFeatureEnabled(const std::string& featureName, bool defaultValue) const
{
	return getActiveManifest().isFeatureEnabled(featureName, defaultValue);
}

const std::vector<ResourceManager::ResourcePack>& ResourceManager::getDiscoveredPacks() const
{
	return discoveredPacks;
}

const std::vector<RuntimeResource::CatalogDiagnostic>&
ResourceManager::getResourceCatalogDiagnostics() const
{
	return resourceCatalogDiagnostics;
}

const std::string& ResourceManager::getUpdateSourceUrl() const
{
	return updateSourceUrl;
}

const std::string& ResourceManager::getResourceCatalogUrl() const
{
	return resourceCatalogUrl;
}

const std::string& ResourceManager::getApplicationCatalogUrl() const
{
	return applicationCatalogUrl;
}

const std::string& ResourceManager::getWritableResourceCollectionRoot() const
{
	return writableResourceCollectionRoot;
}

bool ResourceManager::isResourcePackRemovable(int packIndex) const
{
	if (packIndex < 0 ||
		packIndex >= static_cast<int>(discoveredPacks.size()) ||
		writableResourceCollectionRoot.empty())
	{
		return false;
	}
	try
	{
		std::filesystem::path resolvedRoot;
		return resolvePlainDirectChildDirectory(
			std::filesystem::u8path(writableResourceCollectionRoot),
			std::filesystem::u8path(discoveredPacks[packIndex].rootPath),
			resolvedRoot);
	}
	catch (const std::exception&)
	{
		return false;
	}
}

ResourceManager::ResourceRemovalPlan
ResourceManager::buildResourceRemovalPlan(int packIndex) const
{
	ResourceRemovalPlan plan;
	if (packIndex < 0 ||
		packIndex >= static_cast<int>(discoveredPacks.size()) ||
		writableResourceCollectionRoot.empty())
	{
		return plan;
	}

	const std::filesystem::path writableRoot =
		std::filesystem::u8path(writableResourceCollectionRoot);
	const auto removableRoot = [&writableRoot](
		const ResourcePack& pack,
		std::filesystem::path& resolvedRoot)
	{
		return resolvePlainDirectChildDirectory(
			writableRoot,
			std::filesystem::u8path(pack.rootPath),
			resolvedRoot);
	};
	std::filesystem::path requestedRoot;
	if (!removableRoot(discoveredPacks[packIndex], requestedRoot))
	{
		plan.status = ResourceRemovalStatus::NotRemovable;
		return plan;
	}

	std::vector<bool> included(discoveredPacks.size(), false);
	included[static_cast<std::size_t>(packIndex)] = true;
	bool changed = true;
	while (changed)
	{
		changed = false;
		for (std::size_t candidateIndex = 0;
			candidateIndex < discoveredPacks.size(); candidateIndex++)
		{
			if (included[candidateIndex])
			{
				continue;
			}
			for (const std::string& dependencyId :
				discoveredPacks[candidateIndex].manifest.getDependencyIds())
			{
				bool dependencyFound = false;
				bool dependencyWillBeRemoved = true;
				for (std::size_t dependencyIndex = 0;
					dependencyIndex < discoveredPacks.size();
					dependencyIndex++)
				{
					if (toLowerAscii(
							discoveredPacks[dependencyIndex].manifest.id) !=
							toLowerAscii(dependencyId))
					{
						continue;
					}
					dependencyFound = true;
					dependencyWillBeRemoved =
						dependencyWillBeRemoved && included[dependencyIndex];
				}
				if (dependencyFound && dependencyWillBeRemoved)
				{
					included[candidateIndex] = true;
					changed = true;
					break;
				}
			}
		}
	}

	for (std::size_t index = 0; index < discoveredPacks.size(); index++)
	{
		if (!included[index])
		{
			continue;
		}
		std::filesystem::path resolvedRoot;
		if (removableRoot(discoveredPacks[index], resolvedRoot))
		{
			continue;
		}
		plan.blockingResourceNames.push_back(
			discoveredPacks[index].getDisplayName());
	}
	if (!plan.blockingResourceNames.empty())
	{
		plan.status = ResourceRemovalStatus::DependencyBlocked;
		return plan;
	}

	std::vector<bool> remaining = included;
	std::vector<std::size_t> removalOrder;
	for (;;)
	{
		std::size_t remainingCount = 0;
		for (bool present : remaining)
		{
			remainingCount += present ? 1U : 0U;
		}
		if (remainingCount == 0)
		{
			break;
		}
		std::size_t selected = discoveredPacks.size();
		for (std::size_t candidateIndex = 0;
			candidateIndex < discoveredPacks.size(); candidateIndex++)
		{
			if (!remaining[candidateIndex])
			{
				continue;
			}
			bool hasRemainingDependent = false;
			for (std::size_t dependentIndex = 0;
				dependentIndex < discoveredPacks.size(); dependentIndex++)
			{
				if (!remaining[dependentIndex] ||
					dependentIndex == candidateIndex)
				{
					continue;
				}
				for (const std::string& dependencyId :
					discoveredPacks[dependentIndex].manifest.getDependencyIds())
				{
					if (toLowerAscii(dependencyId) == toLowerAscii(
							discoveredPacks[candidateIndex].manifest.id))
					{
						hasRemainingDependent = true;
						break;
					}
				}
				if (hasRemainingDependent)
				{
					break;
				}
			}
			if (!hasRemainingDependent)
			{
				selected = candidateIndex;
				break;
			}
		}
		if (selected == discoveredPacks.size())
		{
			// A dependency cycle is already unusable. Keep deletion deterministic
			// and remove its last discovered member first.
			for (std::size_t index = discoveredPacks.size(); index > 0; index--)
			{
				if (remaining[index - 1])
				{
					selected = index - 1;
					break;
				}
			}
		}
		remaining[selected] = false;
		removalOrder.push_back(selected);
	}

	const std::filesystem::path saveRoot = configuredSaveRoot();
	for (std::size_t index : removalOrder)
	{
		const ResourcePack& pack = discoveredPacks[index];
		ResourceRemovalEntry entry;
		entry.gameId = pack.manifest.id;
		entry.name = pack.getDisplayName();
		entry.version = pack.manifest.releaseMetadata.displayVersion;
		entry.rootPath = pack.rootPath;
		entry.saveNamespace = File::sanitizeSaveNamespace(
			pack.effectiveSaveNamespace);
		std::filesystem::path resolvedRoot;
		if (!removableRoot(pack, resolvedRoot))
		{
			plan.status = ResourceRemovalStatus::TargetChanged;
			plan.entries.clear();
			return plan;
		}
		if (!saveRoot.empty() &&
			isValidStoredSaveNamespace(entry.saveNamespace))
		{
			std::error_code error;
			const std::filesystem::path savePath =
				saveRoot / std::filesystem::u8path(entry.saveNamespace);
			entry.saveExists = std::filesystem::exists(savePath, error) && !error;
			if (entry.saveExists)
			{
				std::filesystem::path resolvedSave;
				entry.saveRemovable = resolvePlainDirectChildDirectory(
					saveRoot, savePath, resolvedSave);
				entry.saveBytes = entry.saveRemovable
					? directoryByteSize(resolvedSave)
					: 0;
			}
		}
		plan.entries.push_back(std::move(entry));
	}
	plan.status = ResourceRemovalStatus::Success;
	return plan;
}

ResourceManager::ResourceRemovalResult ResourceManager::removeResourceGroup(
	const ResourceRemovalPlan& plan,
	ResourceRemovalSavePolicy savePolicy)
{
	ResourceRemovalResult result;
	if (plan.status != ResourceRemovalStatus::Success ||
		plan.entries.empty() ||
		savePolicy == ResourceRemovalSavePolicy::Unselected)
	{
		return result;
	}
	const std::filesystem::path writableRoot =
		std::filesystem::u8path(writableResourceCollectionRoot);
	const std::filesystem::path saveRoot = configuredSaveRoot();
	for (const ResourceRemovalEntry& entry : plan.entries)
	{
		std::filesystem::path resolvedRoot;
		std::string currentGameId;
		if (!resolvePlainDirectChildDirectory(
				writableRoot,
				std::filesystem::u8path(entry.rootPath),
				resolvedRoot) ||
			normalizeRootPath(resolvedRoot.generic_u8string()) !=
				normalizeRootPath(entry.rootPath) ||
			!readDirectManifestId(resolvedRoot, currentGameId) ||
			toLowerAscii(currentGameId) != toLowerAscii(entry.gameId))
		{
			result.status = ResourceRemovalStatus::TargetChanged;
			result.failedPath = entry.rootPath;
			return result;
		}
		if (savePolicy == ResourceRemovalSavePolicy::Delete &&
			entry.saveExists && !entry.saveRemovable)
		{
			result.status = ResourceRemovalStatus::TargetChanged;
			result.failedPath =
				(saveRoot /
					std::filesystem::u8path(entry.saveNamespace))
					.generic_u8string();
			return result;
		}
	}

	for (const ResourceRemovalEntry& entry : plan.entries)
	{
		std::error_code error;
		const std::filesystem::path root =
			std::filesystem::u8path(entry.rootPath);
		std::filesystem::remove_all(root, error);
		const bool rootStillExists =
			!error && std::filesystem::exists(root, error);
		if (error || rootStillExists)
		{
			result.status = ResourceRemovalStatus::DeleteFailed;
			result.failedPath = entry.rootPath;
			rescanResources();
			return result;
		}
		result.removedGameIds.push_back(entry.gameId);
	}

	if (savePolicy == ResourceRemovalSavePolicy::Delete)
	{
		for (const ResourceRemovalEntry& entry : plan.entries)
		{
			if (!entry.saveExists)
			{
				continue;
			}
			const std::filesystem::path path =
				saveRoot / std::filesystem::u8path(entry.saveNamespace);
			std::error_code existenceError;
			const bool saveStillPresent =
				std::filesystem::exists(path, existenceError);
			if (!existenceError && !saveStillPresent)
			{
				continue;
			}
			std::filesystem::path resolvedPath;
			if (existenceError || !resolvePlainDirectChildDirectory(
					saveRoot, path, resolvedPath))
			{
				result.status = ResourceRemovalStatus::TargetChanged;
				result.failedPath = path.generic_u8string();
				rescanResources();
				return result;
			}
			std::error_code error;
			std::filesystem::remove_all(resolvedPath, error);
			const bool saveStillExists =
				!error && std::filesystem::exists(resolvedPath, error);
			if (error || saveStillExists)
			{
				result.status = ResourceRemovalStatus::DeleteFailed;
				result.failedPath = resolvedPath.generic_u8string();
				rescanResources();
				return result;
			}
			result.removedSaveNamespaces.push_back(entry.saveNamespace);
		}
	}
	result.status = ResourceRemovalStatus::Success;
	rescanResources();
	return result;
}

std::vector<ResourceManager::SaveNamespaceInfo>
ResourceManager::listSaveNamespaces() const
{
	std::vector<SaveNamespaceInfo> result;
	const std::filesystem::path requestedRoot = configuredSaveRoot();
	if (requestedRoot.empty() ||
		!isPlainFilesystemDirectory(requestedRoot))
	{
		return result;
	}
	try
	{
		std::error_code error;
		const std::filesystem::path saveRoot =
			std::filesystem::canonical(requestedRoot, error);
		if (error)
		{
			return result;
		}
		std::filesystem::directory_iterator iterator(saveRoot, error);
		const std::filesystem::directory_iterator end;
		while (!error && iterator != end)
		{
			const std::string saveNamespace =
				iterator->path().filename().generic_u8string();
			std::filesystem::path resolvedPath;
			if (isValidStoredSaveNamespace(saveNamespace) &&
				resolvePlainDirectChildDirectory(
					saveRoot, iterator->path(), resolvedPath))
			{
				SaveNamespaceInfo info;
				info.saveNamespace = saveNamespace;
				info.bytes = directoryByteSize(resolvedPath);
				info.saveSlotCount = saveSlotCount(resolvedPath);
				for (const ResourcePack& pack : discoveredPacks)
				{
					if (toLowerAscii(File::sanitizeSaveNamespace(
							pack.effectiveSaveNamespace)) ==
						toLowerAscii(saveNamespace))
					{
						info.resourceName = pack.getDisplayName();
						break;
					}
				}
				result.push_back(std::move(info));
			}
			iterator.increment(error);
		}
	}
	catch (const std::exception&)
	{
		return {};
	}
	std::sort(
		result.begin(), result.end(),
		[](const SaveNamespaceInfo& left, const SaveNamespaceInfo& right)
		{
			return toLowerAscii(left.saveNamespace) <
				toLowerAscii(right.saveNamespace);
		});
	return result;
}

ResourceManager::ResourceRemovalResult
ResourceManager::removeSaveNamespaces(
	const std::vector<std::string>& saveNamespaces)
{
	ResourceRemovalResult result;
	if (saveNamespaces.empty())
	{
		return result;
	}
	const std::filesystem::path saveRoot = configuredSaveRoot();
	if (!isPlainFilesystemDirectory(saveRoot))
	{
		result.status = ResourceRemovalStatus::TargetChanged;
		result.failedPath = saveRoot.generic_u8string();
		return result;
	}
	std::set<std::string> uniqueNamespaces;
	std::vector<std::filesystem::path> paths;
	for (const std::string& saveNamespace : saveNamespaces)
	{
		std::filesystem::path resolvedPath;
		if (!isValidStoredSaveNamespace(saveNamespace) ||
			!uniqueNamespaces.insert(toLowerAscii(saveNamespace)).second ||
			!resolvePlainDirectChildDirectory(
				saveRoot,
				saveRoot / std::filesystem::u8path(saveNamespace),
				resolvedPath))
		{
			result.status = ResourceRemovalStatus::TargetChanged;
			result.failedPath =
				(saveRoot / std::filesystem::u8path(saveNamespace))
					.generic_u8string();
			return result;
		}
		paths.push_back(std::move(resolvedPath));
	}
	for (std::size_t index = 0; index < paths.size(); index++)
	{
		std::error_code error;
		std::filesystem::remove_all(paths[index], error);
		const bool saveStillExists =
			!error && std::filesystem::exists(paths[index], error);
		if (error || saveStillExists)
		{
			result.status = ResourceRemovalStatus::DeleteFailed;
			result.failedPath = paths[index].generic_u8string();
			return result;
		}
		result.removedSaveNamespaces.push_back(saveNamespaces[index]);
	}
	result.status = ResourceRemovalStatus::Success;
	return result;
}

void ResourceManager::rescanResources()
{
	const bool previousActiveSelectionValid = activeResourceSelectionValid;
	const std::string previousActiveRoot = activeResourceRoot;
	std::string previousActiveEntryKey = activeResourceEntryKey;
	if (previousActiveSelectionValid && previousActiveEntryKey.empty())
	{
		const int previousIndex = findPackIndexByRoot(
			discoveredPacks, previousActiveRoot);
		if (previousIndex >= 0)
		{
			previousActiveEntryKey =
				discoveredPacks[previousIndex].selectionEntryKey;
		}
	}
	scanCollectionRoot(assetsCollectionRoot);
	promoteRecentResourcePackSelection();
	int activeIndex = findPackIndexByCatalogKey(
		discoveredPacks, previousActiveEntryKey);
	if (activeIndex < 0 && previousActiveSelectionValid)
	{
		activeIndex = findPackIndexByRoot(
			discoveredPacks, previousActiveRoot);
	}
	if (previousActiveSelectionValid && activeIndex >= 0 &&
		applyActiveResourcePack(activeIndex))
	{
		return;
	}
	activeResourceRoot.clear();
	activeResourceEntryKey.clear();
	activeManifest = ResourceManifest();
	activeResourceSelectionValid = false;
	File::setActiveResourceRoot("");
	applyCommonResourceRouting();
	File::setResourceFallbackRoots({});
	std::vector<std::string> commonUiFallbackRoots;
	if (!writableCommonResourceRoot.empty() &&
		writableCommonResourceRoot != commonResourceRoot)
	{
		commonUiFallbackRoots.push_back(writableCommonResourceRoot);
	}
	File::setUiResourceFallbackRoots(
		commonUiFallbackRoots, true, commonResourceRoot);
	File::setActiveSaveNamespace("");
}

bool ResourceManager::activateStagedResourceInstall(
	std::string& errorText,
	bool allowUnplayableImportedResource)
{
	errorText.clear();
	if (activeResourceSelectionValid)
	{
		errorText = u8"游戏资源已经载入，不能在当前场景切换资源文件";
		return false;
	}
	if (writableResourceCollectionRoot.empty())
	{
		errorText = u8"可写资源目录不可用";
		return false;
	}

	const OnlineUpdate::ResourceInstallTransactionResult activation =
		OnlineUpdate::beginResourceInstallTransaction(
			std::filesystem::u8path(writableResourceCollectionRoot));
	if (!resourceInstallResultAllowsScan(activation) ||
		!activation.needsValidation)
	{
		errorText = activation.status ==
				OnlineUpdate::ResourceInstallTransactionStatus::NoTransaction
			? std::string(u8"没有已准备的资源更新")
			: std::string(u8"无法启用已下载资源：") +
				resourceInstallStatusText(activation.status);
		return false;
	}

	rescanResources();
	std::string failedGameId;
	const bool resourceGroupValid = validateActivatedResourceGroup(
		writableResourceCollectionRoot,
		activation,
		discoveredPacks,
		!allowUnplayableImportedResource,
		failedGameId);
	bool commitFailed = false;
	const OnlineUpdate::ResourceInstallTransactionResult completion =
		completeValidatedResourceInstall(
			std::filesystem::u8path(writableResourceCollectionRoot),
			resourceGroupValid,
			commitFailed);
	if (!resourceGroupValid || commitFailed)
	{
		if (completion.rolledBack)
		{
			rescanResources();
		}
		if (commitFailed)
		{
			errorText = completion.rolledBack
				? std::string(u8"资源安装失败，旧资源已恢复")
				: std::string(u8"资源恢复未完成，请重启后重试");
		}
		else
		{
			errorText = u8"已下载资源验证失败";
			if (!failedGameId.empty())
			{
				errorText += u8"：" + failedGameId;
			}
			if (!completion.rolledBack)
			{
				errorText += u8"；资源恢复未完成，请重启后重试";
			}
		}
		return false;
	}

	if (completion.status ==
		OnlineUpdate::ResourceInstallTransactionStatus::CleanupFailed)
	{
		GameLog::write(
			"ResourceManager: live resource activation committed; cleanup will retry on next startup\n");
	}
	return true;
}

bool ResourceManager::hasActiveResourceRoot() const
{
	return activeResourceSelectionValid;
}

bool ResourceManager::canActivateResourcePack(
	int index, std::string* blockingReason) const
{
	if (blockingReason != nullptr)
	{
		blockingReason->clear();
	}
	const auto block = [blockingReason](const std::string& reason)
	{
		if (blockingReason != nullptr)
		{
			*blockingReason = reason;
		}
		return false;
	};
	if (index < 0 || index >= static_cast<int>(discoveredPacks.size()))
	{
		return block(u8"资源索引无效");
	}
	const ResourcePack& pack = discoveredPacks[index];
	if (!pack.isLaunchable())
	{
		return block(u8"ResourceOnly 资源不能独立进入");
	}
	if (pack.compatibility.status ==
		ModRelease::CompatibilityStatus::RequiresNewerEngine)
	{
		return block(u8"当前引擎版本低于资源要求");
	}
	const auto identifierCount = [this](const std::string& gameId)
	{
		return static_cast<std::size_t>(std::count_if(
			discoveredPacks.begin(),
			discoveredPacks.end(),
			[&gameId](const ResourcePack& candidate)
			{
				return toLowerAscii(candidate.manifest.id) ==
					toLowerAscii(gameId);
			}));
	};
	if (identifierCount(pack.manifest.id) != 1)
	{
		return block(u8"Game.Id 重复");
	}
	for (const std::string& dependencyId : pack.manifest.getDependencyIds())
	{
		const std::size_t dependencyCount = identifierCount(dependencyId);
		if (dependencyCount == 0)
		{
			return block(u8"缺少依赖：" + dependencyId);
		}
		if (dependencyCount > 1)
		{
			return block(u8"依赖 ID 重复：" + dependencyId);
		}
	}
	return true;
}

bool ResourceManager::needsSelection() const
{
	// 路径为空在 Android APK asset namespace 中是合法活动根。
	return !activeResourceSelectionValid;
}

// 供 ResourceSelectScene 在用户选择后调用，设置 active resource root 和 manifest。
bool ResourceManager::setActiveResourcePack(int index,
	ModRelease::CompatibilityResult* compatibilityResult)
{
	if (index < 0 || index >= (int)discoveredPacks.size())
	{
		return false;
	}

	const ResourcePack& pack = discoveredPacks[index];
	if (compatibilityResult != nullptr)
	{
		*compatibilityResult = pack.compatibility;
	}
	if (!applyActiveResourcePack(index))
	{
		GameLog::write(
			"ResourceManager: failed to apply shared resource routing for %s\n",
			describePack(pack).c_str());
		return false;
	}
	GameLog::write("ResourceManager: user selected resource pack %s (Id=%s, Name=%s)\n",
		activeResourceRoot.c_str(), activeManifest.id.c_str(), activeManifest.name.c_str());
	return true;
}

bool ResourceManager::rememberResourcePackSelection(int index) const
{
	if (index < 0 || index >= static_cast<int>(discoveredPacks.size()))
	{
		return false;
	}

	const ResourcePack& pack = discoveredPacks[index];
	if (!canActivateResourcePack(index))
	{
		return false;
	}
	INIReader selection;
	selection.Set(
		RecentResourceSelectionSection,
		"EntryKey",
		pack.selectionEntryKey.empty()
			? pack.catalogEntryKey
			: pack.selectionEntryKey);
	selection.Set(RecentResourceSelectionSection, "Id", pack.manifest.id);
	selection.Set(RecentResourceSelectionSection, "RootPath", pack.rootPath);
	const std::string content = selection.saveToString();
	if (!File::writeSharedApplicationFile(RecentResourceSelectionFile, content.data(),
		static_cast<int>(content.size())))
	{
		GameLog::write("ResourceManager: failed to save recent resource selection %s\n",
			describePack(pack).c_str());
		return false;
	}

	GameLog::write("ResourceManager: remembered recent resource selection %s\n",
		describePack(pack).c_str());
	return true;
}

void ResourceManager::promoteRecentResourcePackSelection()
{
	for (auto& pack : discoveredPacks)
	{
		pack.wasRecentlySelected = false;
	}
	if (discoveredPacks.size() < 2)
	{
		return;
	}

	std::unique_ptr<char[]> selectionData;
	int selectionLength = 0;
	if (!File::readSharedApplicationFile(RecentResourceSelectionFile, selectionData,
		selectionLength, MaximumRecentResourceSelectionBytes))
	{
		return;
	}

	INIReader selection(selectionData);
	const std::string recentEntryKey = trimAscii(
		selection.Get(
			RecentResourceSelectionSection,
			"EntryKey",
			""));
	const std::string recentId = trimAscii(
		selection.Get(RecentResourceSelectionSection, "Id", ""));
	const std::string recentRootPath = trimAscii(
		selection.Get(RecentResourceSelectionSection, "RootPath", ""));
	int recentIndex = findPackIndexByCatalogKey(
		discoveredPacks, recentEntryKey);
	if (recentIndex < 0 && !recentRootPath.empty())
	{
		recentIndex = findPackIndexByRoot(discoveredPacks, recentRootPath);
	}
	if (recentIndex < 0)
	{
		recentIndex = findPackById(recentId);
	}
	if (recentIndex < 0)
	{
		return;
	}
	if (!discoveredPacks[recentIndex].isLaunchable())
	{
		return;
	}
	discoveredPacks[recentIndex].wasRecentlySelected = true;
	if (recentIndex == 0)
	{
		return;
	}

	std::rotate(discoveredPacks.begin(), discoveredPacks.begin() + recentIndex,
		discoveredPacks.begin() + recentIndex + 1);
	GameLog::write("ResourceManager: promoted recent resource selection %s to index 0\n",
		describePack(discoveredPacks.front()).c_str());
}

bool ResourceManager::setActiveResourcePackById(const std::string& id,
	ModRelease::CompatibilityResult* compatibilityResult)
{
	int index = findPackById(id);
	if (index < 0)
	{
		GameLog::write("ResourceManager: resource pack id %s not found\n", id.c_str());
		return false;
	}

	if (!setActiveResourcePack(index, compatibilityResult))
	{
		GameLog::write("ResourceManager: rejected resource pack id %s\n", id.c_str());
		return false;
	}
	GameLog::write("ResourceManager: selected resource pack by id %s -> %s (Id=%s, Name=%s)\n",
		id.c_str(), activeResourceRoot.c_str(), activeManifest.id.c_str(), activeManifest.name.c_str());
	return true;
}
