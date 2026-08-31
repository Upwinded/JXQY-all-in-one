#include "ResourceSelectScene.h"
#include "../Engine/Engine.h"
#include "../Engine/LogicalResolutionPolicy.h"
#include "ModReleaseAssets.h"
#include "../Component/TextLayout.h"
#include "../Game/Config/Config.h"
#include "../Platform/AndroidExternalStorage.h"
#include "../Platform/AndroidProgramUpdate.h"
#include "../Platform/MobileNetwork.h"
#include "../Resource/SemanticVersion.h"
#include "../File/log.h"
#include "../Game/Menu/ControllerPromptPresenter.h"
#include "../Input/PhysicalInputManager.h"
#include "../JxqyEngineVersion.h"
#include "../File/File.h"
#include "../File/ResourcePathSafety.h"
#include "../Update/ArtifactChecksum.h"
#include "../Update/HttpsDownload.h"
#include "../Update/ResourceDownloadPlanner.h"
#include "../../updater/DesktopProgramUpdater.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#if defined(__linux__)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if !TARGET_OS_IOS
#include "../Platform/MacProgramUpdate.h"
#endif
#endif

namespace
{
constexpr int HeaderTitleTopOffset = 8;
constexpr int HeaderSubtitleTopOffset = 41;
constexpr int HeaderActionTopOffset = 66;
constexpr int HeaderSeparatorOffset = 111;
constexpr int HeaderStatusTopOffset = 122;
constexpr int ProgramActionTopOffset = 115;
constexpr int ActionButtonHeight = 36;
constexpr int PanelListTopOffset = 152;
constexpr int CompactMobileHeaderActionTopOffset = 39;
constexpr int CompactMobileHeaderSeparatorOffset = 79;
constexpr int CompactMobileHeaderStatusTopOffset = 85;
constexpr int CompactMobilePanelListTopOffset = 101;
constexpr int CompactMobileExternalFooterHeight = 112;
constexpr int CompactMobileCreditsFooterHeight = 75;
constexpr int CompactMobileCreditsHeight = 30;
constexpr int CompactMobileExternalButtonBottomOffset = 76;
constexpr int CompactMobileExternalLinkBottomOffset = 39;
constexpr int CompactMobileItemGap = 6;
// 底部 footer 预留高度：桌面端包含制作组信息和一行外部链接按钮；
// 移动端在这些内容上方再增加外部资源开关及固定目录提示。
constexpr int DesktopPanelListAndFooterHeight = 298;
constexpr int MobilePanelListAndFooterHeight = 346;
constexpr int DetailGap = 12;
constexpr int MinimumListHeight = 104;
constexpr int NarrowDetailHeight = 96;
// Four 108 px actions, one 128 px update action, four 10 px gaps and the
// regular 32 px panel padding on each side need 664 px in total.
constexpr int HeaderActionCompactPanelWidth = 664;
constexpr int HeaderActionButtonWidth = 108;
constexpr int HeaderCheckUpdatesButtonWidth = 128;
constexpr int ProgramActionButtonWidth = 200;
constexpr int HeaderActionCompactButtonWidth = 88;
constexpr int HeaderActionFontSize = 17;
constexpr int HeaderActionCompactFontSize = 12;
constexpr int DisplaySettingsRowCount = 4;
constexpr int DisplaySettingsFirstRowTopOffset = 118;
constexpr int DisplaySettingsRowHeight = 58;
constexpr int DisplaySettingsArrowButtonSize = 42;
constexpr int DisplaySettingsActionButtonHeight = 42;
constexpr std::array<DesktopDisplayResolution, 7>
	WindowResolutionPresets =
{
	DesktopDisplayResolution{ 640, 480 },
	DesktopDisplayResolution{ 800, 600 },
	DesktopDisplayResolution{ 1024, 768 },
	DesktopDisplayResolution{ 1280, 720 },
	DesktopDisplayResolution{ 1280, 960 },
	DesktopDisplayResolution{ 1600, 900 },
	DesktopDisplayResolution{ 1920, 1080 }
};
constexpr std::uint64_t ResourceDownloadDiskHeadroom =
	64ULL * 1024ULL * 1024ULL;
constexpr int ResourceInstallItemsPerPage = 1;
constexpr const char* UpdateSourceCachePath = "update/source.ini";
constexpr const char* BackgroundImagePath =
	"engine/image/ui/resource_select/background.png";
constexpr const char* ItemFrameImagePath =
	"engine/image/ui/resource_select/item.png";
constexpr const char* SelectedItemFrameImagePath =
	"engine/image/ui/resource_select/item_selected.png";
constexpr const char* ResourceSelectTitle = u8"剑侠情缘 All-in-One";
constexpr const char* ResourceSelectSubtitle = u8"请选择资源包";
constexpr const char* EmptyResourceListPrimary = u8"未发现可用资源包";
constexpr const char* ExternalResourceCollectionInstruction =
	u8"可选，外部 MOD 读取：Android 11+ 需所有文件访问权限";
constexpr const char* CheatHelpText =
	u8"进入游戏后，可打开“系统 → 选项 → 作弊设置”；纯触屏和桌面触屏均可直接操作。\n"
	u8"键盘仍可按 Shift+F12 开关作弊模式，再使用：\n"
	u8"Shift+Q：补满生命、内力和体力\n"
	u8"Shift+W：当前修炼武功提升 1 级（最高 10 级）\n"
	u8"Shift+E：角色提升 1 级\n"
	u8"Shift+R：增加 100000 两银子\n"
	u8"再次按 Shift+F12 可关闭作弊模式。";

bool isPlainDirectory(const std::filesystem::path& path);

bool loadCachedUpdateSources(OnlineUpdate::UpdateSources& sources)
{
	std::unique_ptr<char[]> bytes;
	int length = 0;
	if (!File::readSharedApplicationFile(
			UpdateSourceCachePath,
			bytes,
			length,
			static_cast<int>(OnlineUpdate::MaximumUpdateSourceBytes)) ||
		bytes == nullptr || length <= 0)
	{
		return false;
	}
	return OnlineUpdate::parseUpdateSources(
		std::string_view(bytes.get(), static_cast<std::size_t>(length)),
		sources);
}

void cacheUpdateSources(std::string_view sourceText)
{
	if (!sourceText.empty() &&
		sourceText.size() <= OnlineUpdate::MaximumUpdateSourceBytes)
	{
		(void)File::writeSharedApplicationFile(
			UpdateSourceCachePath,
			sourceText.data(),
			static_cast<int>(sourceText.size()));
	}
}

std::string parseInstalledCommonArtifactCrc32(
	const char* data,
	std::size_t length)
{
	OnlineUpdate::CommonPackageInstallation installation;
	return OnlineUpdate::parseCommonPackageInstallation(
		std::string_view(data, length), installation)
		? installation.installedArtifactCrc32 : std::string();
}

std::string installedCommonArtifactCrc32(
	const std::filesystem::path& writableCollectionRoot)
{
	const std::filesystem::path writableCommonRoot =
		writableCollectionRoot / "common";
	std::error_code commonStatusError;
	const std::filesystem::file_status commonStatus =
		std::filesystem::symlink_status(
			writableCommonRoot, commonStatusError);
	if (!commonStatusError && std::filesystem::exists(commonStatus))
	{
		const std::filesystem::path versionPath =
			writableCommonRoot / "version.ini";
		std::error_code versionStatusError;
		const std::filesystem::file_status versionStatus =
			std::filesystem::symlink_status(
				versionPath, versionStatusError);
		if (versionStatusError ||
			!std::filesystem::is_regular_file(versionStatus) ||
			std::filesystem::is_symlink(versionStatus))
		{
			return {};
		}
		std::error_code sizeError;
		const std::uintmax_t size =
			std::filesystem::file_size(versionPath, sizeError);
		if (sizeError || size == 0 ||
			size > OnlineUpdate::MaximumCommonVersionFileBytes)
		{
			return {};
		}
		std::string text(static_cast<std::size_t>(size), '\0');
		std::ifstream input(versionPath, std::ios::binary);
		if (!input.read(
				text.data(), static_cast<std::streamsize>(text.size())) ||
			input.gcount() != static_cast<std::streamsize>(text.size()))
		{
			return {};
		}
		return parseInstalledCommonArtifactCrc32(
			text.data(), text.size());
	}

	// If no writable override exists, the bundled common package is the
	// installed copy. File keeps the platform-specific bundled fallback here.
	std::unique_ptr<char[]> bytes;
	int length = 0;
	if (!File::readCommonResourceFile(
			"version.ini",
			bytes,
			length,
			static_cast<int>(
				OnlineUpdate::MaximumCommonVersionFileBytes)) ||
		bytes == nullptr || length <= 0)
	{
		return {};
	}
	return parseInstalledCommonArtifactCrc32(
		bytes.get(), static_cast<std::size_t>(length));
}

struct InstalledResourceState
{
	OnlineUpdate::InstalledResourceArtifactMap artifacts;
	OnlineUpdate::InstalledResourceRootMap roots;
};

InstalledResourceState installedResourceState()
{
	const auto& packs = ResourceManager::instance().getDiscoveredPacks();
	std::map<std::string, std::size_t> ownerCounts;
	for (const ResourceManager::ResourcePack& pack : packs)
	{
		const std::string gameId = OnlineUpdate::foldGameId(pack.manifest.id);
		if (!gameId.empty())
		{
			ownerCounts[gameId]++;
		}
	}

	InstalledResourceState state;
	for (const ResourceManager::ResourcePack& pack : packs)
	{
		const std::string gameId = OnlineUpdate::foldGameId(pack.manifest.id);
		if (!gameId.empty() && ownerCounts[gameId] == 1)
		{
			OnlineUpdate::InstalledResourceArtifacts artifacts;
			artifacts.fullArtifactCrc32 =
				pack.manifest.releaseMetadata.installedArtifactCrc32;
			artifacts.incrementalArtifactCrc32 =
				pack.manifest.releaseMetadata.
					installedIncrementalArtifactCrc32;
			try
			{
				std::error_code canonicalError;
				const std::filesystem::path root = std::filesystem::canonical(
					std::filesystem::u8path(pack.rootPath), canonicalError);
				if (!canonicalError && isPlainDirectory(root))
				{
					artifacts.supportsIncrementalUpdate = true;
					state.roots.emplace(gameId, root);
				}
			}
			catch (const std::exception&)
			{
			}
			state.artifacts.emplace(pack.manifest.id, std::move(artifacts));
		}
	}
	return state;
}

const std::string& resourceSelectVersionSubtitle(bool compact)
{
	static const std::string displayVersion =
		std::string(JxqyBuildVersion::EngineVersion) +
		(JxqyBuildVersion::ReleaseStage[0] == '\0'
			? std::string()
			: " " + std::string(JxqyBuildVersion::ReleaseStage));
	static const std::string fullSubtitle =
		std::string(ResourceSelectSubtitle) + u8" · 程序版本 " +
		displayVersion;
	static const std::string compactSubtitle =
		std::string("v") + JxqyBuildVersion::EngineVersion;
	return compact ? compactSubtitle : fullSubtitle;
}

struct ExternalLink
{
	const char* label;
	const char* compactLabel;
	const char* url;
};

constexpr std::array<ExternalLink, 4> ExternalLinks =
{{
	{ "访问作者主页", "作者主页", "https://www.upwinded.com" },
	{ "访问铁血丹心论坛", "铁血丹心", "https://tiexuedanxin.net/" },
	{ "访问剑侠情缘贴吧", "剑侠贴吧", "https://tieba.baidu.com/f?kw=%E5%89%91%E4%BE%A0%E6%83%85%E7%BC%98&ie=utf-8" },
	{ "访问GitHub仓库", "GitHub", "https://github.com/Upwinded/JXQY-all-in-one" }
}};

bool mobileResourceSelectUiEnabled()
{
#if defined(__MOBILE__) || defined(__ANDROID__)
	return true;
#elif defined(__APPLE__) && TARGET_OS_IOS
	return true;
#else
	return false;
#endif
}

bool externalResourceToggleAvailable()
{
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
	return true;
#else
	return false;
#endif
}

const char* emptyResourceListHint()
{
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
	return u8"请检查更新，或启用下方外部 MOD 目录";
#elif defined(__MOBILE__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
	return u8"请检查更新下载资源";
#else
	return u8"请检查更新，或将已转换资源放入 assets 目录";
#endif
}

// 底部 footer 预留高度：Android 有外部资源开关和链接两行，
// 其它平台只保留一行外部链接按钮。
int footerReservedHeight()
{
	return externalResourceToggleAvailable()
		? MobilePanelListAndFooterHeight
		: DesktopPanelListAndFooterHeight;
}

const char* releaseAssetStatusName(ModRelease::AssetReadStatus status)
{
	switch (status)
	{
	case ModRelease::AssetReadStatus::Ready:
		return "ready";
	case ModRelease::AssetReadStatus::NotDeclared:
		return "not-declared";
	case ModRelease::AssetReadStatus::UnsafePath:
		return "unsafe-path";
	case ModRelease::AssetReadStatus::InvalidRoot:
		return "invalid-root";
	case ModRelease::AssetReadStatus::EscapesPackRoot:
		return "escapes-pack-root";
	case ModRelease::AssetReadStatus::NotFound:
		return "not-found";
	case ModRelease::AssetReadStatus::NotRegularFile:
		return "not-regular-file";
	case ModRelease::AssetReadStatus::TooLarge:
		return "too-large";
	case ModRelease::AssetReadStatus::ReadFailed:
		return "read-failed";
	case ModRelease::AssetReadStatus::InvalidUtf8:
		return "invalid-utf8";
	case ModRelease::AssetReadStatus::InvalidText:
		return "invalid-text";
	case ModRelease::AssetReadStatus::InvalidImage:
		return "invalid-image";
	}
	return "unknown";
}

std::string valueOrUndeclared(const std::string& value)
{
	return value.empty() ? u8"未声明" : value;
}

std::string programTargetDisplayName(const std::string& target)
{
	if (target == "windows")
	{
		return "Windows";
	}
	if (target == "android")
	{
		return "Android";
	}
	if (target == "macos")
	{
		return "macOS";
	}
	if (target == "ios")
	{
		return "iOS";
	}
	if (target == "linux")
	{
		return "Linux";
	}
	return valueOrUndeclared(target);
}

std::string resourcePackTitle(
	const ResourceManager::ResourcePack& pack)
{
	return pack.manifest.name.empty()
		? std::string(u8"未命名资源") : pack.manifest.name;
}

std::string formatAuthorAndVersion(
	const std::string& author,
	const std::string& version)
{
	return (author.empty() ? std::string(u8"作者：未声明") : author)
		+ u8"    版本：" + valueOrUndeclared(version);
}

std::string formatOnlineAuthor(const std::string& author)
{
	if (author.empty())
	{
		return {};
	}
	return author == u8"原版" ? author : u8"作者：" + author;
}

int resourceSeriesDisplayPriority(const std::string& gameId)
{
	const std::string foldedGameId = OnlineUpdate::foldGameId(gameId);
	if (foldedGameId == "jxqy2")
	{
		return 0;
	}
	if (foldedGameId == "yycs")
	{
		return 1;
	}
	if (foldedGameId == "xjxqy")
	{
		return 2;
	}
	return 3;
}

const char* catalogDownloadFailureText(
	OnlineUpdate::HttpsDownloadStatus status)
{
	switch (status)
	{
	case OnlineUpdate::HttpsDownloadStatus::UnsupportedPlatform:
		return u8"当前平台尚未接入在线下载";
	case OnlineUpdate::HttpsDownloadStatus::InvalidUrl:
		return u8"在线目录地址无效";
	case OnlineUpdate::HttpsDownloadStatus::InvalidInput:
		return u8"在线检查参数无效";
	case OnlineUpdate::HttpsDownloadStatus::HttpError:
		return u8"服务器返回错误";
	case OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded:
		return u8"在线目录超过大小限制";
	case OnlineUpdate::HttpsDownloadStatus::Cancelled:
		return u8"检查已取消";
	case OnlineUpdate::HttpsDownloadStatus::NetworkError:
		return u8"无法连接更新服务器";
	case OnlineUpdate::HttpsDownloadStatus::DestinationAlreadyExists:
	case OnlineUpdate::HttpsDownloadStatus::DestinationUnavailable:
	case OnlineUpdate::HttpsDownloadStatus::SizeMismatch:
	case OnlineUpdate::HttpsDownloadStatus::WriteFailed:
	case OnlineUpdate::HttpsDownloadStatus::CleanupFailed:
		return u8"在线目录读取失败";
	case OnlineUpdate::HttpsDownloadStatus::UnexpectedError:
		return u8"下载过程发生未分类错误";
	case OnlineUpdate::HttpsDownloadStatus::Success:
		break;
	}
	return u8"在线目录读取失败";
}

// Release.MinimumEngineVersion 只作为作者声明显示，不参与运行门禁。
std::string describePackRunStatus(const ModRelease::CompatibilityResult& compatibility)
{
	switch (compatibility.status)
	{
	case ModRelease::CompatibilityStatus::RequiresNewerEngine:
	{
		std::string text = u8"需要更高引擎版本";
		if (compatibility.minimumVersion.has_value())
		{
			text += u8"（要求 ";
			text += formatSemanticVersion(*compatibility.minimumVersion);
			text += u8"）";
		}
		return text;
	}
	case ModRelease::CompatibilityStatus::InvalidMinimumEngineVersion:
		return u8"最低引擎版本格式无效";
	case ModRelease::CompatibilityStatus::InvalidCurrentEngineVersion:
		return u8"当前引擎版本无法识别";
	case ModRelease::CompatibilityStatus::LegacyCompatible:
	case ModRelease::CompatibilityStatus::Compatible:
		return u8"未声明版本要求";
	}
	return u8"版本声明可读取";
}

std::string singleLineText(std::string text)
{
	for (char& character : text)
	{
		if (character == '\r' || character == '\n' || character == '\t')
		{
			character = ' ';
		}
	}
	return text;
}

int clampInt(int value, int minimum, int maximum)
{
	return std::max(minimum, std::min(value, maximum));
}

FlatTextButtonStyle makeHeaderActionButtonStyle()
{
	FlatTextButtonStyle style;
	style.normal = { { 196, 162, 96, 150 }, { 34, 24, 18, 220 }, 0xFFFFE7B0 };
	style.hovered = { { 224, 190, 116, 190 }, { 58, 35, 26, 230 }, 0xFFFFFFFF };
	style.pressed = { { 238, 204, 130, 210 }, { 82, 46, 34, 230 }, 0xFFFFFFFF };
	style.borderThickness = 2;
	style.textPadding = 8;
	return style;
}

std::string foldAscii(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character + ('a' - 'A'));
		}
	}
	return value;
}

bool isPlainDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error || !std::filesystem::is_directory(status) ||
		std::filesystem::is_symlink(status))
	{
		return false;
	}
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
	return true;
#endif
}

bool removePlainOwnedDirectoryIfPresent(
	const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error == std::errc::no_such_file_or_directory)
	{
		return true;
	}
	if (!error && !std::filesystem::exists(status))
	{
		return true;
	}
	if (error || !isPlainDirectory(path))
	{
		return false;
	}
	std::filesystem::remove_all(path, error);
	if (error)
	{
		return false;
	}
	const bool remains = std::filesystem::exists(path, error);
	return !error && !remains;
}

bool isPlainRegularFile(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error || !std::filesystem::is_regular_file(status) ||
		std::filesystem::is_symlink(status))
	{
		return false;
	}
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
	return true;
#endif
}

bool removePlainOwnedFileIfPresent(
	const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error == std::errc::no_such_file_or_directory ||
		(!error && !std::filesystem::exists(status)))
	{
		return true;
	}
	if (error || !isPlainRegularFile(path))
	{
		return false;
	}
	const bool removed = std::filesystem::remove(path, error);
	return removed && !error;
}

const char* desktopProgramHelperTarget()
{
#if defined(_WIN32)
	return "win32";
#elif defined(__linux__)
	return "linux";
#else
	return "";
#endif
}

bool programUpdatePlatformAvailable()
{
	if (JxqyBuildVersion::ProgramUpdateTarget[0] == '\0')
	{
		return false;
	}
#if defined(__APPLE__) && !TARGET_OS_IOS
	return MacProgramUpdate::isConfigured();
#else
	return true;
#endif
}

bool androidProgramPackageInstallAvailable()
{
#if defined(__ANDROID__)
	return true;
#else
	return false;
#endif
}

bool iosProgramUpdatePageAvailable()
{
#if defined(__APPLE__) && TARGET_OS_IOS
	return true;
#else
	return false;
#endif
}

bool macProgramUpdateAvailable()
{
#if defined(__APPLE__) && !TARGET_OS_IOS
	return true;
#else
	return false;
#endif
}

bool canUseOnlineProgramPackage(
	const OnlineUpdate::ProgramUpdateCheck& update)
{
#if defined(__APPLE__) && !TARGET_OS_IOS
	// Sparkle only offers a package that its appcast considers newer.
	return update.hasUpdate();
#else
	return update.hasOnlinePackage();
#endif
}

struct DesktopProgramUpdatePaths
{
	std::filesystem::path releaseRoot;
	std::filesystem::path binRoot;
	std::filesystem::path helperPath;
	std::filesystem::path stagingHelperPath;
	std::filesystem::path workspacePath;
	std::filesystem::path stagingPath;
	std::filesystem::path stagingProgramPath;
	std::filesystem::path stagingEnginePath;
	std::filesystem::path stagingCommonPath;
	std::filesystem::path previousPath;
	std::filesystem::path executableName;
	std::string helperTarget;
};

bool resolveDesktopProgramUpdatePaths(DesktopProgramUpdatePaths& paths)
{
	paths = {};
	paths.helperTarget = desktopProgramHelperTarget();
	if (paths.helperTarget.empty() ||
		JxqyBuildVersion::ProgramUpdateTarget[0] == '\0')
	{
		return false;
	}

	const std::string writableAssetsRootText =
		ResourceManager::instance().getWritableResourceCollectionRoot();
	if (writableAssetsRootText.empty())
	{
		return false;
	}
	std::filesystem::path writableAssetsRoot;
	try
	{
		writableAssetsRoot = std::filesystem::absolute(
			std::filesystem::u8path(
				writableAssetsRootText)).lexically_normal();
	}
	catch (const std::exception&)
	{
		return false;
	}
	if (writableAssetsRoot.empty() || !writableAssetsRoot.has_parent_path())
	{
		return false;
	}
	paths.releaseRoot =
		ProgramUpdate::desktopProgramReleaseRoot(writableAssetsRoot);
	paths.binRoot = paths.releaseRoot / "bin";
	paths.workspacePath = paths.binRoot / ".jxqy-program-update";
	paths.stagingPath = paths.workspacePath / "staging";
	paths.stagingProgramPath =
		paths.stagingPath / "bin" / paths.helperTarget;
	paths.stagingEnginePath =
		paths.stagingPath / "assets" / "engine";
	paths.stagingCommonPath =
		paths.stagingPath / "assets" / "common";
	paths.previousPath = paths.workspacePath / "previous";
	paths.executableName = paths.helperTarget == "linux"
		? std::filesystem::path("jxqy-all-in-one")
		: std::filesystem::path("jxqy-all-in-one.exe");
	paths.helperPath = paths.binRoot / "updater" /
		paths.helperTarget /
		(paths.helperTarget == "linux"
			? std::filesystem::path("jxqy-program-updater")
			: std::filesystem::path("jxqy-program-updater.exe"));
	paths.stagingHelperPath = paths.stagingPath /
		paths.helperPath.lexically_relative(paths.releaseRoot);
	return true;
}

bool isPreparedDesktopProgramUpdate(
	const DesktopProgramUpdatePaths& paths)
{
	return isPlainDirectory(paths.workspacePath) &&
		isPlainDirectory(paths.stagingPath) &&
		isPlainDirectory(paths.stagingProgramPath) &&
		isPlainRegularFile(
			paths.stagingProgramPath / paths.executableName) &&
		isPlainRegularFile(paths.stagingHelperPath) &&
		isPlainDirectory(paths.stagingEnginePath) &&
		isPlainRegularFile(
			paths.stagingEnginePath / "font" / "font.ttf") &&
		isPlainDirectory(paths.stagingCommonPath) &&
		isPlainRegularFile(
			paths.stagingCommonPath / "version.ini");
}

bool ensurePlainUpdaterDirectory(
	const std::filesystem::path& parent,
	const std::filesystem::path& directory)
{
	if (!isPlainDirectory(parent))
	{
		return false;
	}
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(directory, error);
	if (!error && std::filesystem::exists(status))
	{
		return isPlainDirectory(directory);
	}
	if (error && error != std::errc::no_such_file_or_directory)
	{
		return false;
	}
	error.clear();
	return std::filesystem::create_directory(directory, error) &&
		!error && isPlainDirectory(directory);
}

bool refreshDesktopProgramUpdater(
	const DesktopProgramUpdatePaths& paths)
{
	if (!isPlainRegularFile(paths.stagingHelperPath))
	{
		return false;
	}
	const std::filesystem::path updaterRoot = paths.binRoot / "updater";
	if (!ensurePlainUpdaterDirectory(paths.binRoot, updaterRoot) ||
		!ensurePlainUpdaterDirectory(
			updaterRoot, paths.helperPath.parent_path()))
	{
		return false;
	}
	std::error_code error;
	const std::filesystem::file_status helperStatus =
		std::filesystem::symlink_status(paths.helperPath, error);
	if ((!error && std::filesystem::exists(helperStatus) &&
			!isPlainRegularFile(paths.helperPath)) ||
		(error && error != std::errc::no_such_file_or_directory))
	{
		return false;
	}

	std::filesystem::path temporaryPath = paths.helperPath;
	temporaryPath += ".next";
	error.clear();
	const std::filesystem::file_status temporaryStatus =
		std::filesystem::symlink_status(temporaryPath, error);
	if (!error && std::filesystem::exists(temporaryStatus))
	{
		if (!isPlainRegularFile(temporaryPath) ||
			!std::filesystem::remove(temporaryPath, error) || error)
		{
			return false;
		}
	}
	else if (error && error != std::errc::no_such_file_or_directory)
	{
		return false;
	}
	error.clear();
	if (!std::filesystem::copy_file(
			paths.stagingHelperPath, temporaryPath,
			std::filesystem::copy_options::none, error) || error)
	{
		return false;
	}
#if defined(__linux__)
	std::filesystem::permissions(
		temporaryPath,
		std::filesystem::perms::owner_all |
			std::filesystem::perms::group_read |
			std::filesystem::perms::group_exec |
			std::filesystem::perms::others_read |
			std::filesystem::perms::others_exec,
		std::filesystem::perm_options::replace,
		error);
	if (error)
	{
		std::error_code cleanupError;
		std::filesystem::remove(temporaryPath, cleanupError);
		return false;
	}
#endif
#if defined(_WIN32)
	if (!MoveFileExW(
			temporaryPath.c_str(),
			paths.helperPath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		const DWORD replaceError = GetLastError();
		GameLog::write(
			"ResourceSelectScene: updater replacement failed: %lu\n",
			static_cast<unsigned long>(replaceError));
		std::error_code cleanupError;
		std::filesystem::remove(temporaryPath, cleanupError);
		return false;
	}
#else
	std::filesystem::rename(temporaryPath, paths.helperPath, error);
	if (error)
	{
		std::error_code cleanupError;
		std::filesystem::remove(temporaryPath, cleanupError);
		return false;
	}
#endif
	return isPlainRegularFile(paths.helperPath);
}

bool startDesktopProgramUpdater(
	const DesktopProgramUpdatePaths& paths)
{
	if (!refreshDesktopProgramUpdater(paths))
	{
		return false;
	}
#if defined(_WIN32)
	const std::wstring commandLine =
		L"\"" + paths.helperPath.wstring() +
		L"\" --release-root \"" + paths.releaseRoot.wstring() +
		L"\" --target " +
		std::wstring(paths.helperTarget.begin(), paths.helperTarget.end()) +
		L" --wait-pid " + std::to_wstring(GetCurrentProcessId());
	std::vector<wchar_t> mutableCommand(
		commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	const BOOL started = CreateProcessW(
		paths.helperPath.c_str(),
		mutableCommand.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
		nullptr,
		paths.helperPath.parent_path().c_str(),
		&startup,
		&process);
	if (!started)
	{
		GameLog::write(
			"ResourceSelectScene: updater launch failed: %lu\n",
			static_cast<unsigned long>(GetLastError()));
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
#elif defined(__linux__)
	int errorPipe[2] = { -1, -1 };
	if (pipe(errorPipe) != 0 ||
		fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC) != 0)
	{
		if (errorPipe[0] >= 0)
		{
			close(errorPipe[0]);
		}
		if (errorPipe[1] >= 0)
		{
			close(errorPipe[1]);
		}
		return false;
	}
	const pid_t child = fork();
	if (child < 0)
	{
		close(errorPipe[0]);
		close(errorPipe[1]);
		return false;
	}
	if (child == 0)
	{
		close(errorPipe[0]);
		const std::string helper = paths.helperPath.native();
		const std::string releaseRoot = paths.releaseRoot.native();
		const std::string processId = std::to_string(getppid());
		char* const arguments[] =
		{
			const_cast<char*>(helper.c_str()),
			const_cast<char*>("--release-root"),
			const_cast<char*>(releaseRoot.c_str()),
			const_cast<char*>("--target"),
			const_cast<char*>(paths.helperTarget.c_str()),
			const_cast<char*>("--wait-pid"),
			const_cast<char*>(processId.c_str()),
			nullptr
		};
		execv(helper.c_str(), arguments);
		const int childError = errno;
		(void)write(errorPipe[1], &childError, sizeof(childError));
		_exit(127);
	}
	close(errorPipe[1]);
	int childError = 0;
	ssize_t bytesRead = -1;
	do
	{
		bytesRead = read(errorPipe[0], &childError, sizeof(childError));
	}
	while (bytesRead < 0 && errno == EINTR);
	close(errorPipe[0]);
	if (bytesRead != 0)
	{
		(void)waitpid(child, nullptr, 0);
		return false;
	}
	return true;
#else
	(void)paths;
	return false;
#endif
}

bool isSafeResourceDirectoryName(const std::string& name)
{
	if (name.empty() || name.size() > 200 || name == "." || name == ".." ||
		name.find('/') != std::string::npos ||
		name.find('\\') != std::string::npos ||
		!ResourcePathSafety::isSafeVirtualResourcePath(name))
	{
		return false;
	}
	const std::string folded = foldAscii(name);
	return folded != ".jxqy-update" && folded != "common" &&
		folded != "save" && folded != ".git" &&
		folded != ".jxqy_editor";
}

std::string formatByteCount(std::uint64_t bytes)
{
	static constexpr std::array<const char*, 4> Units =
		{ "B", "KiB", "MiB", "GiB" };
	double value = static_cast<double>(bytes);
	std::size_t unitIndex = 0;
	while (value >= 1024.0 && unitIndex + 1 < Units.size())
	{
		value /= 1024.0;
		unitIndex++;
	}
	std::ostringstream text;
	if (unitIndex == 0)
	{
		text << bytes;
	}
	else
	{
		text << std::fixed << std::setprecision(value >= 100.0 ? 0 : 1)
			<< value;
	}
	text << ' ' << Units[unitIndex];
	return text.str();
}

std::string resourcePlanFailureText(
	OnlineUpdate::ResourcePlanStatus status,
	const std::string& blockingGameId)
{
	std::string text;
	switch (status)
	{
	case OnlineUpdate::ResourcePlanStatus::TargetNotFound:
		text = u8"线上目录中找不到所选资源";
		break;
	case OnlineUpdate::ResourcePlanStatus::ResourceOnlyTarget:
		text = u8"该资源仅供其他游戏依赖，不能直接下载启动";
		break;
	case OnlineUpdate::ResourcePlanStatus::InvalidCurrentEngineVersion:
		text = u8"当前程序版本无法用于资源兼容检查";
		break;
	case OnlineUpdate::ResourcePlanStatus::DependencyCycle:
		text = u8"线上资源依赖形成循环";
		break;
	case OnlineUpdate::ResourcePlanStatus::RequiresNewerEngine:
		text = u8"资源要求先升级主程序";
		break;
	case OnlineUpdate::ResourcePlanStatus::TotalSizeOverflow:
		text = u8"资源总下载大小无效";
		break;
	case OnlineUpdate::ResourcePlanStatus::Ready:
	default:
		text = u8"无法准备资源下载";
		break;
	}
	if (!blockingGameId.empty())
	{
		text += u8"：" + blockingGameId;
	}
	return text;
}

std::string resourcePreparationFailureText(
	const OnlineUpdate::ResourceDownloadPreparationResult& result)
{
	using Status = OnlineUpdate::ResourceDownloadPreparationStatus;
	switch (result.status)
	{
	case Status::PlanFailed:
		return resourcePlanFailureText(result.planStatus, result.failedGameId);
	case Status::InvalidArtifactUrl:
		return u8"线上资源下载地址无效";
	case Status::WorkspaceAlreadyExists:
		return u8"已有待处理资源更新，请先重启游戏";
	case Status::WorkspaceUnavailable:
		return u8"无法创建资源更新暂存目录";
	case Status::Cancelled:
		return u8"资源下载已取消";
	case Status::DownloadFailed:
		return catalogDownloadFailureText(result.downloadResult.status);
	case Status::ProgramNotAvailable:
		return u8"当前平台没有可用的新程序版本";
	case Status::ArtifactValidationFailed:
		return u8"下载文件的大小或 CRC32 校验失败";
	case Status::PackageValidationFailed:
		return u8"下载的资源包校验或解压失败";
	case Status::CleanupFailed:
		return u8"下载失败且暂存目录清理未完成，请重启后重试";
	case Status::InvalidInput:
		return u8"资源下载参数无效";
	case Status::Success:
	default:
		return u8"资源下载失败";
	}
}

std::string programPackageFailureText(
	OnlineUpdate::ResourcePackageArchiveStatus status)
{
	using Status = OnlineUpdate::ResourcePackageArchiveStatus;
	switch (status)
	{
	case Status::ArtifactMismatch:
		return u8"程序安装包 CRC32 校验不一致";
	case Status::MissingProgramExecutable:
		return u8"程序安装包缺少主程序文件";
	case Status::MissingProgramUpdater:
		return u8"程序安装包缺少独立更新助手";
	case Status::MissingEngineBootstrap:
		return u8"程序安装包缺少引擎启动资源";
	case Status::MissingCommonBootstrap:
		return u8"程序安装包缺少游戏公共资源";
	case Status::InvalidEntryPath:
	case Status::DuplicateEntryPath:
	case Status::UnsupportedEntry:
		return u8"程序安装包包含不安全或重复的文件";
	case Status::DestinationAlreadyExists:
		return u8"已有待安装的主程序，请先完成或清理该更新";
	case Status::CleanupFailed:
		return u8"程序更新暂存目录清理失败，请重启后重试";
	case Status::Success:
	default:
		return u8"程序安装包无法安全解压";
	}
}

std::string commonPreparationFailureText(
	const OnlineUpdate::CommonDownloadPreparationResult& result)
{
	OnlineUpdate::ResourceDownloadPreparationResult shared;
	shared.status = result.status;
	shared.downloadResult = result.downloadResult;
	shared.packageResult = result.packageResult;
	return resourcePreparationFailureText(shared);
}

std::string programPreparationFailureText(
	const OnlineUpdate::ProgramDownloadPreparationResult& result)
{
	OnlineUpdate::ResourceDownloadPreparationResult shared;
	shared.status = result.status;
	shared.downloadResult = result.downloadResult;
	return resourcePreparationFailureText(shared);
}

std::string resourceTransactionFailureText(
	OnlineUpdate::ResourceInstallTransactionStatus status)
{
	using Status = OnlineUpdate::ResourceInstallTransactionStatus;
	switch (status)
	{
	case Status::WorkspaceConflict:
		return u8"资源更新暂存目录存在冲突，请重启后重试";
	case Status::TargetConflict:
		return u8"待替换资源目录在下载期间发生变化";
	case Status::RecordUnavailable:
	case Status::RecordInvalid:
		return u8"无法写入资源更新事务记录";
	case Status::CleanupFailed:
		return u8"资源准备失败且暂存目录清理未完成";
	case Status::RollbackFailed:
		return u8"资源更新回滚失败";
	case Status::InvalidInput:
		return u8"资源安装目标无效";
	case Status::NoTransaction:
	case Status::Success:
	default:
		return u8"无法准备资源安装";
	}
}

}

ResourceSelectScene::ResourceSelectScene()
{
	name = "ResourceSelectScene";
	drawFullScreen = true;
	rectFullScreen = true;
	canCallBack = true;
}

ResourceSelectScene::~ResourceSelectScene()
{
	freeResource();
}

void ResourceSelectScene::freeResource()
{
	if (resourceInstallRunner != nullptr)
	{
		resourceInstallRunner->requestCancellation();
		resourceInstallRunner.reset();
	}
	if (catalogCheckRunner != nullptr)
	{
		catalogCheckRunner->requestCancellation();
		catalogCheckRunner.reset();
	}
	catalogCheckWorkerResult.reset();
	focusManager.clear();
	removeAllChild();
	resourceList = nullptr;
	exitButton = nullptr;
	cheatHelpButton = nullptr;
	checkUpdatesButton = nullptr;
	programActionButton = nullptr;
	onlineActionButton = nullptr;
	resourceRemoveButton = nullptr;
	saveManagementButton = nullptr;
	displaySettingsButton = nullptr;
	for (auto& button : displaySettingsPreviousButtons)
	{
		button = nullptr;
	}
	for (auto& button : displaySettingsNextButtons)
	{
		button = nullptr;
	}
	displaySettingsApplyButton = nullptr;
	displaySettingsDefaultButton = nullptr;
	displaySettingsBackButton = nullptr;
	cheatHelpCloseButton = nullptr;
	externalResourceConfirmButton = nullptr;
	externalResourceCancelButton = nullptr;
	resourceInstallPrimaryButton = nullptr;
	resourceInstallSecondaryButton = nullptr;
	resourceInstallPreviousPageButton = nullptr;
	resourceInstallNextPageButton = nullptr;
	enableExternalButton = nullptr;
	externalLinkButtons.clear();
	semanticFocusVisible = false;
	dispatchingKeyboardUIAction = false;
	keyboardSemanticFocus = false;
	cheatHelpVisible = false;
	externalResourceDialogVisible = false;
	displaySettingsVisible = false;
	desktopDisplays.clear();
	displayResolutionOptions.clear();
	pendingDisplaySettings = {};
	displaySettingsStatusText.clear();
	resourceEntries.clear();
	onlineCatalog = {};
	onlineApplicationCatalog = {};
	onlineResourceCatalogSources = {};
	onlineApplicationCatalogSources = {};
	catalogCheckState = CatalogCheckState::NotChecked;
	catalogStatusText.clear();
	programUpdateDialogPending = false;
	resourceInstallDialogState = ResourceInstallDialogState::Hidden;
	resourceInstallOperation = ResourceInstallOperation::OnlineDownload;
	pendingResourceInstall = {};
	pendingResourceRemoval = {};
	pendingResourceRemovalSavePolicy =
		ResourceManager::ResourceRemovalSavePolicy::Unselected;
	saveNamespaceEntries.clear();
	selectedSaveNamespaceIndex = 0;
	pendingResourceRemoval = {};
	pendingResourceRemovalSavePolicy =
		ResourceManager::ResourceRemovalSavePolicy::Unselected;
	saveNamespaceEntries.clear();
	selectedSaveNamespaceIndex = 0;
	resourceInstallWorkerResult.reset();
	pendingProgramPackagePath.clear();
	resourceInstallDialogMessage.clear();
	resourceInstallConfirmationPage = 0;
	pendingDownloadUsesMeteredNetwork = false;
	pendingMeteredDownloadConfirmed = false;
	resourceUpdatePromptedByEntry = false;
	pendingExternalRescan = false;
	externalResourcePresentationState =
		ExternalResourcePresentationState::Disabled;
	externalResourceDirectoryPath.clear();
	textCache.clear();
	backgroundImage = nullptr;
	itemFrameImage = nullptr;
	selectedItemFrameImage = nullptr;
	detailCoverImage = nullptr;
	resourceListArea = { 0, 0, 0, 0 };
	detailArea = { 0, 0, 0, 0 };
	coverArea = { 0, 0, 0, 0 };
	selectedDetails = {};
}

void ResourceSelectScene::loadSceneImages()
{
	const auto loadEngineImage = [this](const char* path)
	{
		std::unique_ptr<char[]> data;
		int length = 0;
		if (!File::readBundledApplicationFile(path, data, length) ||
			data == nullptr || length <= 0)
		{
			GameLog::write(
				"ResourceSelectScene: engine image is unavailable: %s\n",
				path);
			return _shared_image();
		}
		return engine->loadImageFromMem(data, length);
	};
	backgroundImage = loadEngineImage(BackgroundImagePath);
	itemFrameImage = loadEngineImage(ItemFrameImagePath);
	selectedItemFrameImage = loadEngineImage(SelectedItemFrameImagePath);
}

void ResourceSelectScene::createControls()
{
	resourceList = std::make_shared<ResourcePackList>();
	resourceList->setFrameImages(itemFrameImage, selectedItemFrameImage);
	resourceList->setPointerTakeoverHandler([this]() { hideSemanticFocus(); });
	resourceList->setSelectionChangedHandler(
		[this](int selectedIndex)
		{
			updateSelectedResourceDetails(selectedIndex);
		});
	addChild(resourceList);

	exitButton = std::make_shared<FlatTextButton>();
	exitButton->name = "resource-select-exit";
	exitButton->setFontSize(HeaderActionFontSize);
	exitButton->setUTF8Str("退出");
	exitButton->setStyle(makeHeaderActionButtonStyle());
	addChild(exitButton);

	cheatHelpButton = std::make_shared<FlatTextButton>();
	cheatHelpButton->name = "resource-select-cheat-help";
	cheatHelpButton->setFontSize(HeaderActionFontSize);
	cheatHelpButton->setUTF8Str("作弊说明");
	cheatHelpButton->setStyle(makeHeaderActionButtonStyle());
	addChild(cheatHelpButton);

	checkUpdatesButton = std::make_shared<FlatTextButton>();
	checkUpdatesButton->name = "resource-select-check-updates";
	checkUpdatesButton->setFontSize(HeaderActionFontSize);
	checkUpdatesButton->setStyle(makeHeaderActionButtonStyle());
	addChild(checkUpdatesButton);
	refreshCheckUpdatesButton();

	programActionButton = std::make_shared<FlatTextButton>();
	programActionButton->name = "resource-select-program-action";
	programActionButton->setFontSize(HeaderActionFontSize);
	programActionButton->setStyle(makeHeaderActionButtonStyle());
	programActionButton->visible = false;
	programActionButton->activated = false;
	addChild(programActionButton);
	refreshProgramActionButton();

	onlineActionButton = std::make_shared<FlatTextButton>();
	onlineActionButton->name = "resource-select-online-action";
	onlineActionButton->setFontSize(16);
	onlineActionButton->setStyle(makeHeaderActionButtonStyle());
	onlineActionButton->visible = false;
	onlineActionButton->activated = false;
	addChild(onlineActionButton);

	resourceRemoveButton = std::make_shared<FlatTextButton>();
	resourceRemoveButton->name = "resource-select-remove";
	resourceRemoveButton->setFontSize(16);
	resourceRemoveButton->setUTF8Str(u8"删除游戏");
	resourceRemoveButton->setStyle(makeHeaderActionButtonStyle());
	resourceRemoveButton->visible = false;
	resourceRemoveButton->activated = false;
	addChild(resourceRemoveButton);

	saveManagementButton = std::make_shared<FlatTextButton>();
	saveManagementButton->name = "resource-select-save-management";
	saveManagementButton->setFontSize(HeaderActionFontSize);
	saveManagementButton->setUTF8Str(u8"存档管理");
	saveManagementButton->setStyle(makeHeaderActionButtonStyle());
	addChild(saveManagementButton);

#if !defined(__MOBILE__)
	displaySettingsButton = std::make_shared<FlatTextButton>();
	displaySettingsButton->name = "resource-select-display-settings";
	displaySettingsButton->setFontSize(HeaderActionFontSize);
	displaySettingsButton->setUTF8Str(u8"显示设置");
	displaySettingsButton->setStyle(makeHeaderActionButtonStyle());
	addChild(displaySettingsButton);

	for (int row = 0; row < DisplaySettingsRowCount; row++)
	{
		displaySettingsPreviousButtons[row] =
			std::make_shared<FlatTextButton>();
		displaySettingsPreviousButtons[row]->name =
			"display-settings-previous-" + std::to_string(row);
		displaySettingsPreviousButtons[row]->setFontSize(24);
		displaySettingsPreviousButtons[row]->setUTF8Str(u8"‹");
		displaySettingsPreviousButtons[row]->setStyle(
			makeHeaderActionButtonStyle());
		displaySettingsPreviousButtons[row]->visible = false;
		displaySettingsPreviousButtons[row]->activated = false;
		addChild(displaySettingsPreviousButtons[row]);

		displaySettingsNextButtons[row] =
			std::make_shared<FlatTextButton>();
		displaySettingsNextButtons[row]->name =
			"display-settings-next-" + std::to_string(row);
		displaySettingsNextButtons[row]->setFontSize(24);
		displaySettingsNextButtons[row]->setUTF8Str(u8"›");
		displaySettingsNextButtons[row]->setStyle(
			makeHeaderActionButtonStyle());
		displaySettingsNextButtons[row]->visible = false;
		displaySettingsNextButtons[row]->activated = false;
		addChild(displaySettingsNextButtons[row]);
	}

	displaySettingsApplyButton = std::make_shared<FlatTextButton>();
	displaySettingsApplyButton->name = "display-settings-apply";
	displaySettingsApplyButton->setFontSize(18);
	displaySettingsApplyButton->setUTF8Str(u8"应用");
	displaySettingsApplyButton->setStyle(makeHeaderActionButtonStyle());
	displaySettingsApplyButton->visible = false;
	displaySettingsApplyButton->activated = false;
	addChild(displaySettingsApplyButton);

	displaySettingsDefaultButton = std::make_shared<FlatTextButton>();
	displaySettingsDefaultButton->name = "display-settings-default";
	displaySettingsDefaultButton->setFontSize(18);
	displaySettingsDefaultButton->setUTF8Str(u8"恢复默认");
	displaySettingsDefaultButton->setStyle(makeHeaderActionButtonStyle());
	displaySettingsDefaultButton->visible = false;
	displaySettingsDefaultButton->activated = false;
	addChild(displaySettingsDefaultButton);

	displaySettingsBackButton = std::make_shared<FlatTextButton>();
	displaySettingsBackButton->name = "display-settings-back";
	displaySettingsBackButton->setFontSize(18);
	displaySettingsBackButton->setUTF8Str(u8"返回");
	displaySettingsBackButton->setStyle(makeHeaderActionButtonStyle());
	displaySettingsBackButton->visible = false;
	displaySettingsBackButton->activated = false;
	addChild(displaySettingsBackButton);
#endif

	cheatHelpCloseButton = std::make_shared<FlatTextButton>();
	cheatHelpCloseButton->name = "resource-select-cheat-help-close";
	cheatHelpCloseButton->setFontSize(20);
	cheatHelpCloseButton->setUTF8Str("知道了");
	cheatHelpCloseButton->setStyle(makeHeaderActionButtonStyle());
	cheatHelpCloseButton->visible = false;
	cheatHelpCloseButton->activated = false;
	addChild(cheatHelpCloseButton);

	externalResourceConfirmButton = std::make_shared<FlatTextButton>();
	externalResourceConfirmButton->name =
		"resource-select-external-confirm";
	externalResourceConfirmButton->setFontSize(19);
	externalResourceConfirmButton->setStyle(makeHeaderActionButtonStyle());
	externalResourceConfirmButton->visible = false;
	externalResourceConfirmButton->activated = false;
	addChild(externalResourceConfirmButton);

	externalResourceCancelButton = std::make_shared<FlatTextButton>();
	externalResourceCancelButton->name =
		"resource-select-external-cancel";
	externalResourceCancelButton->setFontSize(19);
	externalResourceCancelButton->setUTF8Str(u8"取消");
	externalResourceCancelButton->setStyle(makeHeaderActionButtonStyle());
	externalResourceCancelButton->visible = false;
	externalResourceCancelButton->activated = false;
	addChild(externalResourceCancelButton);

	resourceInstallPrimaryButton = std::make_shared<FlatTextButton>();
	resourceInstallPrimaryButton->name = "resource-select-install-primary";
	resourceInstallPrimaryButton->setFontSize(19);
	resourceInstallPrimaryButton->setStyle(makeHeaderActionButtonStyle());
	resourceInstallPrimaryButton->visible = false;
	resourceInstallPrimaryButton->activated = false;
	addChild(resourceInstallPrimaryButton);

	resourceInstallSecondaryButton = std::make_shared<FlatTextButton>();
	resourceInstallSecondaryButton->name = "resource-select-install-secondary";
	resourceInstallSecondaryButton->setFontSize(19);
	resourceInstallSecondaryButton->setStyle(makeHeaderActionButtonStyle());
	resourceInstallSecondaryButton->visible = false;
	resourceInstallSecondaryButton->activated = false;
	addChild(resourceInstallSecondaryButton);

	resourceInstallPreviousPageButton = std::make_shared<FlatTextButton>();
	resourceInstallPreviousPageButton->name = "resource-select-install-previous";
	resourceInstallPreviousPageButton->setFontSize(16);
	resourceInstallPreviousPageButton->setUTF8Str(u8"上一页");
	resourceInstallPreviousPageButton->setStyle(makeHeaderActionButtonStyle());
	resourceInstallPreviousPageButton->visible = false;
	resourceInstallPreviousPageButton->activated = false;
	addChild(resourceInstallPreviousPageButton);

	resourceInstallNextPageButton = std::make_shared<FlatTextButton>();
	resourceInstallNextPageButton->name = "resource-select-install-next";
	resourceInstallNextPageButton->setFontSize(16);
	resourceInstallNextPageButton->setUTF8Str(u8"下一页");
	resourceInstallNextPageButton->setStyle(makeHeaderActionButtonStyle());
	resourceInstallNextPageButton->visible = false;
	resourceInstallNextPageButton->activated = false;
	addChild(resourceInstallNextPageButton);

	if (externalResourceToggleAvailable())
	{
		// 移动端：用开关按钮启用固定外部资源目录（需"所有文件访问权限"）。
		refreshExternalResourceDirectoryPath();
		externalResourcePresentationState =
			Config::externalResourcesEnabled &&
				AndroidExternalStorage::isAllFilesAccessGranted()
			? ExternalResourcePresentationState::Enabled
			: (Config::externalResourcesEnabled
				? ExternalResourcePresentationState::PermissionRequired
				: ExternalResourcePresentationState::Disabled);
		enableExternalButton = std::make_shared<FlatTextButton>();
		enableExternalButton->name = "resource-select-enable-external";
		enableExternalButton->setFontSize(20);
		addChild(enableExternalButton);
		refreshExternalResourcePresentation();
	}

	externalLinkButtons.reserve(ExternalLinks.size());
	for (int linkIndex = 0; linkIndex < static_cast<int>(ExternalLinks.size()); linkIndex++)
	{
		auto button = std::make_shared<FlatTextButton>();
		button->name = "resource-select-external-link";
		button->index = linkIndex;
		button->setFontSize(18);
		button->setUTF8Str(ExternalLinks[linkIndex].label);
		addChild(button);
		externalLinkButtons.push_back(button);
	}
	updateControlLayout();
}

void ResourceSelectScene::buildResourceList()
{
	if (resourceList == nullptr)
	{
		return;
	}

	std::string previouslySelectedGameId;
	const int previousIndex = resourceList->getSelectedIndex();
	if (previousIndex >= 0 &&
		previousIndex < static_cast<int>(resourceEntries.size()))
	{
		previouslySelectedGameId =
			OnlineUpdate::foldGameId(resourceEntries[previousIndex].gameId);
	}
	rebuildResourceEntries();
	updateLayout(rect.w, rect.h);

	std::vector<ResourcePackCardContent> items;
	items.reserve(resourceEntries.size());
	for (const ResourceSelectionEntry& entry : resourceEntries)
	{
		ResourcePackCardContent item;
		item.title = entry.title;
		if (entry.configurationError)
		{
			item.authorAndVersion = u8"资源配置错误 · 不能进入";
		}
		else if (entry.isOnlineOnly())
		{
			item.authorAndVersion = formatAuthorAndVersion(
				entry.author, entry.onlineVersion) + u8"    未安装";
		}
		else
		{
			item.authorAndVersion = formatAuthorAndVersion(
				entry.author, entry.localVersion);
		}
		item.showDescriptionAction = mobileResourceSelectUiEnabled();
		item.wasRecentlySelected = entry.wasRecentlySelected;
		item.onlineOnly = entry.isOnlineOnly();
		items.push_back(item);
	}
	resourceList->setItems(items);
	int selectedIndex = 0;
	if (!previouslySelectedGameId.empty())
	{
		for (int entryIndex = 0;
			entryIndex < static_cast<int>(resourceEntries.size());
			entryIndex++)
		{
			if (OnlineUpdate::foldGameId(resourceEntries[entryIndex].gameId) ==
				previouslySelectedGameId)
			{
				selectedIndex = entryIndex;
				break;
			}
		}
	}
	resourceList->setSelectedIndex(selectedIndex);
	resourceList->ensureSelectedVisible();
	updateSelectedResourceDetails(resourceList->getSelectedIndex());
}

void ResourceSelectScene::rebuildResourceEntries()
{
	resourceEntries.clear();
	const ResourceManager& resourceManager = ResourceManager::instance();
	const auto& packs = resourceManager.getDiscoveredPacks();
	const auto& diagnostics =
		resourceManager.getResourceCatalogDiagnostics();
	resourceEntries.reserve(
		packs.size() + diagnostics.size() +
			onlineCatalog.resourcePackages.size());
	std::set<std::string> localGameIds;
	for (int packIndex = 0;
		packIndex < static_cast<int>(packs.size());
		packIndex++)
	{
		const ResourceManager::ResourcePack& pack = packs[packIndex];
		const std::string foldedGameId =
			OnlineUpdate::foldGameId(pack.manifest.id);
		if (!foldedGameId.empty())
		{
			localGameIds.insert(foldedGameId);
		}
		if (!pack.isLaunchable())
		{
			continue;
		}
		ResourceSelectionEntry entry;
		entry.localPackIndex = packIndex;
		entry.gameId = pack.manifest.id;
		entry.title = resourcePackTitle(pack);
		entry.author = pack.getDisplayAuthorText();
		entry.localVersion =
			pack.manifest.releaseMetadata.displayVersion;
		entry.wasRecentlySelected = pack.wasRecentlySelected;
		for (const RuntimeResource::CatalogDiagnostic& diagnostic : diagnostics)
		{
			if (diagnostic.severity !=
					RuntimeResource::CatalogDiagnosticSeverity::Error ||
				diagnostic.stableEntryKey.empty() ||
				OnlineUpdate::foldGameId(diagnostic.stableEntryKey) !=
					OnlineUpdate::foldGameId(pack.catalogEntryKey))
			{
				continue;
			}
			entry.configurationError = true;
			entry.configurationErrorText =
				diagnostic.code == "resource.catalog.duplicate_game_id"
				? std::string(u8"Game.Id 重复；请删除重复资源或修改自定义资源 ID")
				: std::string(u8"资源配置错误；请检查 game_profile.ini");
			break;
		}
		if (!foldedGameId.empty())
		{
			const auto online =
				onlineCatalog.resourcePackages.find(foldedGameId);
			if (online != onlineCatalog.resourcePackages.end())
			{
				entry.onlineAvailable = true;
				entry.onlineVersion = online->second.versionText;
				entry.releaseNotes = online->second.releaseNotes;
			}
		}
		resourceEntries.push_back(std::move(entry));
	}
	sortLocalResourceEntries(resourceEntries.size());

	for (const RuntimeResource::CatalogDiagnostic& diagnostic : diagnostics)
	{
		if (diagnostic.severity !=
				RuntimeResource::CatalogDiagnosticSeverity::Error ||
			(diagnostic.code !=
					"resource.catalog.discovered_manifest_invalid" &&
				diagnostic.code !=
					"resource.catalog.root_manifest_invalid" &&
				diagnostic.code !=
					"resource.catalog.supplemental_manifest_invalid"))
		{
			continue;
		}
		const bool representedByPack = std::any_of(
			packs.begin(), packs.end(),
			[&diagnostic](const ResourceManager::ResourcePack& pack)
			{
				return !diagnostic.stableEntryKey.empty() &&
					OnlineUpdate::foldGameId(diagnostic.stableEntryKey) ==
						OnlineUpdate::foldGameId(pack.catalogEntryKey);
			});
		if (representedByPack)
		{
			continue;
		}
		ResourceSelectionEntry entry;
		entry.configurationError = true;
		entry.title = u8"无效资源";
		std::filesystem::path resourceRoot =
			diagnostic.hostPath.parent_path();
		if (!resourceRoot.empty() &&
			!resourceRoot.filename().empty())
		{
			entry.title = resourceRoot.filename().generic_u8string();
		}
		entry.configurationErrorText =
			u8"无法读取有效的 game_profile.ini";
		if (!diagnostic.hostPath.empty())
		{
			entry.configurationErrorText += u8"：" +
				diagnostic.hostPath.generic_u8string();
		}
		resourceEntries.push_back(std::move(entry));
	}

	const std::size_t firstOnlineEntry = resourceEntries.size();
	for (const auto& onlineEntry : onlineCatalog.resourcePackages)
	{
		if (localGameIds.find(onlineEntry.first) != localGameIds.end())
		{
			continue;
		}
		const OnlineUpdate::ResourcePackage& package = onlineEntry.second;
		if (package.resourceOnly)
		{
			continue;
		}
		ResourceSelectionEntry entry;
		entry.gameId = package.gameId;
		entry.title = package.displayName.empty()
			? package.gameId : package.displayName;
		entry.author = formatOnlineAuthor(package.author);
		entry.onlineVersion = package.versionText;
		entry.releaseNotes = package.releaseNotes;
		entry.onlineAvailable = true;
		resourceEntries.push_back(std::move(entry));
	}
	sortOnlineOnlyResourceEntries(firstOnlineEntry);
}

void ResourceSelectScene::sortLocalResourceEntries(
	std::size_t localEntryCount)
{
	localEntryCount = std::min(localEntryCount, resourceEntries.size());
	if (localEntryCount < 2)
	{
		return;
	}

	std::stable_sort(
		resourceEntries.begin(),
		resourceEntries.begin() + localEntryCount,
		[](const ResourceSelectionEntry& left,
			const ResourceSelectionEntry& right)
		{
			if (left.wasRecentlySelected != right.wasRecentlySelected)
			{
				return left.wasRecentlySelected;
			}
			return resourceSeriesDisplayPriority(left.gameId)
				< resourceSeriesDisplayPriority(right.gameId);
		});
}

void ResourceSelectScene::sortOnlineOnlyResourceEntries(
	std::size_t firstOnlineEntry)
{
	if (firstOnlineEntry >= resourceEntries.size())
	{
		return;
	}

	std::stable_sort(
		resourceEntries.begin() + firstOnlineEntry,
		resourceEntries.end(),
		[](const ResourceSelectionEntry& left,
			const ResourceSelectionEntry& right)
		{
			return resourceSeriesDisplayPriority(left.gameId)
				< resourceSeriesDisplayPriority(right.gameId);
		});
}

void ResourceSelectScene::updateSelectedResourceDetails(int selectedIndex)
{
	const auto& packs = ResourceManager::instance().getDiscoveredPacks();
	selectedDetails = {};
	detailCoverImage = nullptr;
	if (selectedIndex < 0
		|| selectedIndex >= static_cast<int>(resourceEntries.size()))
	{
		refreshOnlineActionButton();
		return;
	}

	const ResourceSelectionEntry& entry = resourceEntries[selectedIndex];
	selectedDetails.packIndex = selectedIndex;
	selectedDetails.localPackIndex = entry.localPackIndex;
	selectedDetails.name = entry.title;
	selectedDetails.resourceId = valueOrUndeclared(entry.gameId);
	selectedDetails.author = entry.author;
	selectedDetails.onlineAvailable = entry.onlineAvailable;
	selectedDetails.onlineOnly = entry.isOnlineOnly();
	selectedDetails.onlineVersion =
		valueOrUndeclared(entry.onlineVersion);
	selectedDetails.onlineVersionMatches = !entry.isOnlineOnly() &&
		!entry.localVersion.empty() &&
		entry.localVersion == entry.onlineVersion;
	selectedDetails.wasRecentlySelected = entry.wasRecentlySelected;
	if (entry.configurationError)
	{
		selectedDetails.resourceId = entry.gameId.empty()
			? std::string(u8"不可用") : entry.gameId;
		selectedDetails.version = u8"不可用";
		selectedDetails.releaseDate = u8"不可用";
		selectedDetails.runStatus = u8"资源配置错误，不能进入";
		selectedDetails.description = entry.configurationErrorText;
		selectedDetails.onlineAvailable = false;
		selectedDetails.onlineOnly = false;
		refreshOnlineActionButton();
		return;
	}
	refreshOnlineActionButton();
	if (entry.isOnlineOnly())
	{
		selectedDetails.version = u8"未安装";
		selectedDetails.releaseDate = u8"未声明";
		selectedDetails.runStatus =
			u8"尚未安装；可获取线上版本 " +
			valueOrUndeclared(entry.onlineVersion);
		selectedDetails.description = entry.releaseNotes.empty()
			? std::string(u8"未提供线上说明") : entry.releaseNotes;
		return;
	}
	if (entry.localPackIndex < 0 ||
		entry.localPackIndex >= static_cast<int>(packs.size()))
	{
		selectedDetails = {};
		refreshOnlineActionButton();
		return;
	}

	const ResourceManager::ResourcePack& pack =
		packs[entry.localPackIndex];
	selectedDetails.name = resourcePackTitle(pack);
	selectedDetails.resourceId = valueOrUndeclared(pack.manifest.id);
	selectedDetails.version = valueOrUndeclared(
		pack.manifest.releaseMetadata.displayVersion);
	selectedDetails.author = pack.getDisplayAuthorText();
	selectedDetails.releaseDate = valueOrUndeclared(
		pack.manifest.releaseMetadata.releaseDate);
	selectedDetails.runStatus = describePackRunStatus(pack.compatibility);
	if (entry.onlineAvailable)
	{
		selectedDetails.runStatus +=
			selectedDetails.onlineVersionMatches
				? std::string(u8"；已与线上版本一致")
				: u8"；线上版本 " +
					valueOrUndeclared(entry.onlineVersion) + u8" 可更新";
	}
	selectedDetails.wasRecentlySelected = pack.wasRecentlySelected;
	selectedDetails.description = u8"未提供简介";

	const bool hasDescription =
		!pack.manifest.releaseMetadata.descriptionFilePath.empty();
	const bool hasCover =
		!pack.manifest.releaseMetadata.coverPath.empty();
	if (!hasDescription && !hasCover)
	{
		return;
	}

	std::filesystem::path packRoot;
	try
	{
		packRoot = std::filesystem::u8path(pack.rootPath);
	}
	catch (const std::exception&)
	{
		return;
	}

	if (hasDescription)
	{
		const ModRelease::DescriptionReadResult description =
			ModRelease::readDescriptionFromPack(
				packRoot, pack.manifest.releaseMetadata);
		if (description.succeeded())
		{
			selectedDetails.description = description.utf8Text.empty()
				? std::string(u8"未提供简介")
				: description.utf8Text;
			selectedDetails.descriptionLoadedFromPack = true;
		}
		else
		{
			GameLog::write(
				"ResourceSelectScene: description load failed"
				" (pack=%s, root=%s, path=%s, status=%s)\n",
				pack.manifest.id.c_str(),
				pack.rootPath.c_str(),
				pack.manifest.releaseMetadata.
					descriptionFilePath.c_str(),
				releaseAssetStatusName(description.status));
		}
	}

	if (!hasCover)
	{
		return;
	}
	const ModRelease::CoverReadResult cover =
		ModRelease::readCoverFromPack(
			packRoot, pack.manifest.releaseMetadata);
	if (!cover.readyForDecode() || cover.encodedBytes.empty()
		|| cover.encodedBytes.size()
			> static_cast<std::size_t>(std::numeric_limits<int>::max())
		|| engine == nullptr)
	{
		GameLog::write(
			"ResourceSelectScene: cover load failed"
				" (pack=%s, root=%s, path=%s, status=%s,"
				" encoded-bytes=%zu)\n",
			pack.manifest.id.c_str(),
			pack.rootPath.c_str(),
			pack.manifest.releaseMetadata.coverPath.c_str(),
			releaseAssetStatusName(cover.status),
			cover.encodedBytes.size());
		return;
	}

	std::unique_ptr<char[]> encodedCover(
		new (std::nothrow) char[cover.encodedBytes.size()]);
	if (encodedCover == nullptr)
	{
		return;
	}
	std::memcpy(encodedCover.get(), cover.encodedBytes.data(),
		cover.encodedBytes.size());
	detailCoverImage = engine->loadImageFromMem(
		encodedCover, static_cast<int>(cover.encodedBytes.size()));
	selectedDetails.coverLoadedFromPack = detailCoverImage != nullptr;
	if (!selectedDetails.coverLoadedFromPack)
	{
		GameLog::write(
			"ResourceSelectScene: cover decode failed"
				" (pack=%s, root=%s, path=%s, dimensions=%llux%llu)\n",
			pack.manifest.id.c_str(),
			pack.rootPath.c_str(),
			pack.manifest.releaseMetadata.coverPath.c_str(),
			static_cast<unsigned long long>(
				cover.dimensions.width),
			static_cast<unsigned long long>(
				cover.dimensions.height));
	}
}

void ResourceSelectScene::beginOnlineCatalogCheck()
{
	if (catalogCheckRunner != nullptr || resourceInstallRunner != nullptr ||
		resourceInstallDialogState != ResourceInstallDialogState::Hidden)
	{
		return;
	}
	const std::string updateSourceUrl =
		ResourceManager::instance().getUpdateSourceUrl();
	const std::string resourceCatalogUrl =
		ResourceManager::instance().getResourceCatalogUrl();
	const std::string applicationCatalogUrl =
		ResourceManager::instance().getApplicationCatalogUrl();
	if (updateSourceUrl.empty() && resourceCatalogUrl.empty() &&
		applicationCatalogUrl.empty())
	{
		catalogCheckState = CatalogCheckState::Failed;
		catalogStatusText =
			u8"未配置资源和主程序在线目录，当前资源仍可直接进入";
		refreshCheckUpdatesButton();
		refreshOnlineActionButton();
		return;
	}

	catalogCheckState = CatalogCheckState::Checking;
	catalogStatusText = u8"正在检查游戏资源和主程序更新…";
	onlineResourceCatalogSources = {};
	onlineApplicationCatalogSources = {};
	programUpdateDialogPending = false;
	refreshCheckUpdatesButton();
	refreshOnlineActionButton();
	catalogCheckWorkerResult =
		std::make_shared<CatalogCheckWorkerResult>();
	const std::shared_ptr<CatalogCheckWorkerResult> workerResult =
		catalogCheckWorkerResult;
	catalogCheckRunner =
		std::make_unique<GameLoading::ExclusiveLoadingRunner>(
		[updateSourceUrl,
			resourceCatalogUrl,
			applicationCatalogUrl,
			workerResult](
			const GameLoading::LoadingCancellationToken& cancellationToken)
		{
			std::vector<std::string> resourceCatalogUrls;
			std::vector<std::string> applicationCatalogUrls;
			auto appendCatalogUrl = [](std::vector<std::string>& urls,
				const std::string& url)
			{
				if (!url.empty() &&
					std::find(urls.begin(), urls.end(), url) == urls.end())
				{
					urls.push_back(url);
				}
			};
			if (!updateSourceUrl.empty())
			{
				bool onlineSourcesLoaded = false;
				const OnlineUpdate::HttpsBufferDownloadResult sourceDownload =
					OnlineUpdate::downloadHttpsToMemory(
						updateSourceUrl,
						OnlineUpdate::MaximumUpdateSourceBytes,
						[cancellationToken](std::uint64_t, std::uint64_t)
						{
							return !cancellationToken.
								isCancellationRequested();
						});
				if (cancellationToken.isCancellationRequested())
				{
					return GameLoading::LoadingTaskResult::cancellation();
				}
				OnlineUpdate::UpdateSources sources;
				const std::string sourceText(
					sourceDownload.bytes.begin(), sourceDownload.bytes.end());
				if (sourceDownload.succeeded() &&
					OnlineUpdate::parseUpdateSources(
						sourceText, sources))
				{
					cacheUpdateSources(sourceText);
					resourceCatalogUrls =
						std::move(sources.resourceCatalogUrls);
					applicationCatalogUrls =
						std::move(sources.applicationCatalogUrls);
					onlineSourcesLoaded = true;
				}
				if (!onlineSourcesLoaded && loadCachedUpdateSources(sources))
				{
					resourceCatalogUrls =
						std::move(sources.resourceCatalogUrls);
					applicationCatalogUrls =
						std::move(sources.applicationCatalogUrls);
				}
			}
			appendCatalogUrl(resourceCatalogUrls, resourceCatalogUrl);
			appendCatalogUrl(applicationCatalogUrls, applicationCatalogUrl);

			const OnlineUpdate::HttpsDownloadProgress catalogProgress =
				[cancellationToken](std::uint64_t, std::uint64_t)
				{
					return !cancellationToken.isCancellationRequested();
				};
			workerResult->resource =
				OnlineUpdate::selectCatalogMirrorSources(
					resourceCatalogUrls, catalogProgress);
			const bool resourceSucceeded = workerResult->resource.succeeded();
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			workerResult->application =
				OnlineUpdate::selectCatalogMirrorSources(
					applicationCatalogUrls, catalogProgress);
			const bool applicationSucceeded =
				workerResult->application.succeeded();
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			if (!resourceSucceeded && !applicationSucceeded)
			{
				return GameLoading::LoadingTaskResult::failure(
					"Online catalogs are unavailable.");
			}
			return GameLoading::LoadingTaskResult::success();
		});
}

void ResourceSelectScene::pollOnlineCatalogCheck()
{
	if (catalogCheckRunner == nullptr)
	{
		return;
	}
	const GameLoading::ExclusiveLoadingPollStatus status =
		catalogCheckRunner->poll(
			{},
			[this](const GameLoading::ExclusiveLoadingCompletion& completion)
			{
				finishOnlineCatalogCheck(completion);
			});
	if (status == GameLoading::ExclusiveLoadingPollStatus::Finalized)
	{
		catalogCheckRunner.reset();
		catalogCheckWorkerResult.reset();
		refreshCheckUpdatesButton();
	}
}

void ResourceSelectScene::finishOnlineCatalogCheck(
	const GameLoading::ExclusiveLoadingCompletion& completion)
{
	if (catalogCheckWorkerResult == nullptr)
	{
		catalogCheckState = CatalogCheckState::Failed;
		catalogStatusText = u8"检查失败，当前资源仍可直接进入";
		refreshCheckUpdatesButton();
		refreshOnlineActionButton();
		return;
	}

	const bool resourceSucceeded =
		catalogCheckWorkerResult->resource.succeeded();
	const bool applicationSucceeded =
		catalogCheckWorkerResult->application.succeeded();
	if (completion.taskResult.succeeded() &&
		(resourceSucceeded || applicationSucceeded))
	{
		onlineCatalog = resourceSucceeded
			? std::move(catalogCheckWorkerResult->resource.parse.catalog)
			: OnlineUpdate::Catalog();
		onlineApplicationCatalog = applicationSucceeded
			? std::move(catalogCheckWorkerResult->application.parse.catalog)
			: OnlineUpdate::Catalog();
		onlineResourceCatalogSources = resourceSucceeded
			? std::move(catalogCheckWorkerResult->resource.sources)
			: OnlineUpdate::CatalogMirrorSources();
		onlineApplicationCatalogSources = applicationSucceeded
			? std::move(catalogCheckWorkerResult->application.sources)
			: OnlineUpdate::CatalogMirrorSources();
		catalogCheckState = CatalogCheckState::Ready;
		buildResourceList();
		configureFocus();
		int localOnlineCount = 0;
		int onlineOnlyCount = 0;
		for (const ResourceSelectionEntry& entry : resourceEntries)
		{
			if (!entry.onlineAvailable)
			{
				continue;
			}
			if (entry.isOnlineOnly())
			{
				onlineOnlyCount++;
			}
			else
			{
				localOnlineCount++;
			}
		}
		catalogStatusText = u8"检查完成：";
		if (resourceSucceeded)
		{
			catalogStatusText += std::to_string(localOnlineCount);
			catalogStatusText += u8" 个本地资源可获取线上版本，";
			catalogStatusText += std::to_string(onlineOnlyCount);
			catalogStatusText += u8" 个线上资源尚未安装";
		}
		else if (catalogCheckWorkerResult->resource.configured)
		{
			catalogStatusText += u8"资源目录检查失败";
		}
		else
		{
			catalogStatusText += u8"未配置资源目录";
		}
		const OnlineUpdate::ProgramUpdateCheck programUpdate =
			OnlineUpdate::checkProgramUpdate(
				onlineApplicationCatalog,
				JxqyBuildVersion::ProgramUpdateTarget,
				JxqyBuildVersion::EngineVersion);
		if (programUpdatePlatformAvailable())
		{
			if (!applicationSucceeded)
			{
				catalogStatusText +=
					catalogCheckWorkerResult->application.configured
					? u8"；主程序目录检查失败"
					: u8"；未配置主程序目录";
			}
			else
			{
				switch (programUpdate.status)
				{
				case OnlineUpdate::ProgramUpdateStatus::UpdateAvailable:
					catalogStatusText += u8"；线上主程序：" +
						programUpdate.package->versionText + u8"，高于当前版本";
					break;
				case OnlineUpdate::ProgramUpdateStatus::UpToDate:
					if (programUpdate.package != nullptr &&
						programUpdate.versionComparison == 0)
					{
						catalogStatusText += u8"；线上主程序：" +
							programUpdate.package->versionText +
							u8"，与当前版本一致，无需更新；可选重装";
					}
					else if (programUpdate.package != nullptr)
					{
						catalogStatusText += u8"；线上主程序：" +
							programUpdate.package->versionText +
							u8"，低于当前版本，无需更新；可选重装";
#if defined(__ANDROID__)
						catalogStatusText +=
							u8"；Android 系统可能拒绝安装";
#endif
					}
					break;
				case OnlineUpdate::ProgramUpdateStatus::TargetNotFound:
					catalogStatusText += u8"；线上暂未提供当前平台主程序";
					break;
				case OnlineUpdate::ProgramUpdateStatus::InvalidInput:
				default:
					catalogStatusText += u8"；无法判断主程序版本";
					break;
				}
			}
		}
		programUpdateDialogPending = applicationSucceeded &&
			programUpdatePlatformAvailable() && programUpdate.hasUpdate();
		GameLog::write(
			"ResourceSelectScene: online catalogs ready"
			" (resource-ready=%d, application-ready=%d, resources=%zu,"
			" local-matches=%d, online-only=%d)\n",
			resourceSucceeded ? 1 : 0,
			applicationSucceeded ? 1 : 0,
			onlineCatalog.resourcePackages.size(),
			localOnlineCount,
			onlineOnlyCount);
	}
	else
	{
		onlineCatalog = {};
		onlineApplicationCatalog = {};
		onlineResourceCatalogSources = {};
		onlineApplicationCatalogSources = {};
		catalogCheckState = CatalogCheckState::Failed;
		programUpdateDialogPending = false;
		const CatalogCheckWorkerResult::Endpoint* failedEndpoint =
			catalogCheckWorkerResult->resource.configured
			? &catalogCheckWorkerResult->resource
			: &catalogCheckWorkerResult->application;
		if (!failedEndpoint->downloadAttempted)
		{
			catalogStatusText = u8"无法启动在线检查，当前资源仍可直接进入";
		}
		else if (!failedEndpoint->download.succeeded())
		{
			catalogStatusText = catalogDownloadFailureText(
				failedEndpoint->download.status);
			if (failedEndpoint->download.httpStatus != 0)
			{
				catalogStatusText += "（HTTP ";
				catalogStatusText += std::to_string(
					failedEndpoint->download.httpStatus);
				catalogStatusText += "）";
			}
		}
		else if (!failedEndpoint->parse.issues.empty())
		{
			const OnlineUpdate::CatalogParseIssue& issue =
				failedEndpoint->parse.issues.front();
			catalogStatusText = u8"在线目录格式错误";
			if (!issue.section.empty())
			{
				catalogStatusText += "：" + issue.section;
				if (!issue.field.empty())
				{
					catalogStatusText += "." + issue.field;
				}
			}
		}
		else
		{
			catalogStatusText = u8"检查失败，当前资源仍可直接进入";
		}
		GameLog::write(
			"ResourceSelectScene: online catalog checks failed"
			" (resource-status=%d, resource-issues=%zu,"
			" application-status=%d, application-issues=%zu)\n",
			static_cast<int>(
				catalogCheckWorkerResult->resource.download.status),
			catalogCheckWorkerResult->resource.parse.issues.size(),
			static_cast<int>(
				catalogCheckWorkerResult->application.download.status),
			catalogCheckWorkerResult->application.parse.issues.size());
	}
	refreshCheckUpdatesButton();
	refreshOnlineActionButton();
}

void ResourceSelectScene::presentPendingProgramUpdateDialog()
{
	if (!programUpdateDialogPending)
	{
		return;
	}
	if (catalogCheckState != CatalogCheckState::Ready ||
		!programUpdatePlatformAvailable())
	{
		programUpdateDialogPending = false;
		return;
	}
	if (catalogCheckRunner != nullptr || resourceInstallRunner != nullptr ||
		displaySettingsVisible || cheatHelpVisible ||
		externalResourceDialogVisible || resourceInstallDialogState !=
			ResourceInstallDialogState::Hidden)
	{
		return;
	}

	const OnlineUpdate::ProgramUpdateCheck update =
		OnlineUpdate::checkProgramUpdate(
			onlineApplicationCatalog,
			JxqyBuildVersion::ProgramUpdateTarget,
			JxqyBuildVersion::EngineVersion);
	programUpdateDialogPending = false;
	if (update.hasUpdate() && canUseOnlineProgramPackage(update))
	{
		beginProgramDownloadConfirmation();
	}
}

void ResourceSelectScene::refreshCheckUpdatesButton()
{
	if (checkUpdatesButton == nullptr)
	{
		return;
	}
	const bool compact = panelWidth < HeaderActionCompactPanelWidth;
	checkUpdatesButton->setFontSize(compact
		? HeaderActionCompactFontSize : HeaderActionFontSize);
	checkUpdatesButton->activated =
		!displaySettingsVisible &&
		resourceInstallDialogState == ResourceInstallDialogState::Hidden &&
		resourceInstallRunner == nullptr && catalogCheckRunner == nullptr &&
		catalogCheckState != CatalogCheckState::Checking;
	switch (catalogCheckState)
	{
	case CatalogCheckState::Checking:
		checkUpdatesButton->setUTF8Str(
			compact ? u8"检查中" : u8"正在检查更新");
		break;
	case CatalogCheckState::Ready:
		checkUpdatesButton->setUTF8Str(
			compact ? u8"查更新" : u8"重新检查更新");
		break;
	case CatalogCheckState::Failed:
		checkUpdatesButton->setUTF8Str(
			compact ? u8"查更新" : u8"重新检查更新");
		break;
	case CatalogCheckState::NotChecked:
	default:
		checkUpdatesButton->setUTF8Str(
			compact ? u8"查更新" : u8"检查更新");
		break;
	}
	refreshProgramActionButton();
}

void ResourceSelectScene::refreshProgramActionButton()
{
	if (programActionButton == nullptr)
	{
		return;
	}
	bool available = false;
	if (!displaySettingsVisible && !cheatHelpVisible &&
		!externalResourceDialogVisible &&
		catalogCheckState == CatalogCheckState::Ready &&
		programUpdatePlatformAvailable() && catalogCheckRunner == nullptr &&
		resourceInstallRunner == nullptr && resourceInstallDialogState ==
			ResourceInstallDialogState::Hidden)
	{
		const OnlineUpdate::ProgramUpdateCheck update =
			OnlineUpdate::checkProgramUpdate(
				onlineApplicationCatalog,
				JxqyBuildVersion::ProgramUpdateTarget,
				JxqyBuildVersion::EngineVersion);
		available = canUseOnlineProgramPackage(update);
	}
	if (!available)
	{
		if (programActionButton->visible || programActionButton->activated)
		{
			programActionButton->cancelPointerInteraction();
		}
		programActionButton->visible = false;
		programActionButton->activated = false;
		return;
	}

	const bool compact = panelWidth < HeaderActionCompactPanelWidth;
	programActionButton->setFontSize(compact
		? HeaderActionCompactFontSize : HeaderActionFontSize);
	const OnlineUpdate::ProgramUpdateCheck update =
		OnlineUpdate::checkProgramUpdate(
			onlineApplicationCatalog,
			JxqyBuildVersion::ProgramUpdateTarget,
			JxqyBuildVersion::EngineVersion);
	const bool updateAvailable = update.hasUpdate();
	programActionButton->setUTF8Str(
		updateAvailable
			? (compact ? u8"主程序" : u8"更新主程序")
			: (compact ? u8"重装" : u8"可选重装线上版本"));
	programActionButton->visible = true;
	programActionButton->activated = true;
	// Keep an active press across layout passes while the action stays available.
}

void ResourceSelectScene::activateCheckUpdatesButton()
{
	if (catalogCheckRunner != nullptr || resourceInstallRunner != nullptr ||
		resourceInstallDialogState != ResourceInstallDialogState::Hidden)
	{
		return;
	}
	beginOnlineCatalogCheck();
}

void ResourceSelectScene::activateProgramActionButton()
{
	if (catalogCheckState != CatalogCheckState::Ready ||
		catalogCheckRunner != nullptr || resourceInstallRunner != nullptr ||
		resourceInstallDialogState != ResourceInstallDialogState::Hidden)
	{
		return;
	}
	const OnlineUpdate::ProgramUpdateCheck update =
		OnlineUpdate::checkProgramUpdate(
			onlineApplicationCatalog,
			JxqyBuildVersion::ProgramUpdateTarget,
			JxqyBuildVersion::EngineVersion);
	if (canUseOnlineProgramPackage(update))
	{
		beginProgramDownloadConfirmation();
	}
}

void ResourceSelectScene::refreshOnlineActionButton()
{
	if (onlineActionButton == nullptr)
	{
		return;
	}
	const bool available = !displaySettingsVisible && !cheatHelpVisible &&
		!externalResourceDialogVisible &&
		resourceInstallDialogState == ResourceInstallDialogState::Hidden &&
		selectedDetails.packIndex >= 0 && selectedDetails.onlineAvailable &&
		catalogCheckState == CatalogCheckState::Ready;
	onlineActionButton->visible = available;
	onlineActionButton->activated = available;
	if (available)
	{
		onlineActionButton->setUTF8Str(
			selectedDetails.onlineOnly
				? u8"下载此游戏"
				: selectedDetails.onlineVersionMatches
					? u8"重新下载" : u8"更新此游戏");
		onlineActionButton->rect = getOnlineActionButtonRect();
	}
	else
	{
		onlineActionButton->cancelPointerInteraction();
	}
	refreshResourceManagementButtons();
}

void ResourceSelectScene::refreshResourceManagementButtons()
{
	const bool dialogHidden = !displaySettingsVisible && !cheatHelpVisible &&
		!externalResourceDialogVisible &&
		resourceInstallDialogState == ResourceInstallDialogState::Hidden;
	if (resourceRemoveButton != nullptr)
	{
		const bool removableSelection = dialogHidden &&
			ResourceManager::instance().isResourcePackRemovable(
				selectedDetails.localPackIndex);
		resourceRemoveButton->visible = removableSelection;
		resourceRemoveButton->activated = removableSelection;
		if (removableSelection)
		{
			resourceRemoveButton->rect = getResourceRemoveButtonRect();
		}
		else
		{
			resourceRemoveButton->cancelPointerInteraction();
		}
	}
	if (saveManagementButton != nullptr)
	{
		saveManagementButton->visible = dialogHidden;
		saveManagementButton->activated = dialogHidden;
		if (dialogHidden)
		{
			saveManagementButton->rect = getSaveManagementButtonRect();
		}
		else
		{
			saveManagementButton->cancelPointerInteraction();
		}
	}
}

bool ResourceSelectScene::buildResourceInstallConfirmation(
	int selectedIndex,
	ResourceInstallConfirmation& confirmation,
	std::string& errorText,
	bool forceReinstallIfCurrent) const
{
	confirmation = {};
	errorText.clear();
	if (selectedIndex < 0 ||
		selectedIndex >= static_cast<int>(resourceEntries.size()))
	{
		errorText = u8"未选择线上资源";
		return false;
	}
	const ResourceSelectionEntry& selectedEntry =
		resourceEntries[selectedIndex];
	if (!selectedEntry.onlineAvailable || selectedEntry.gameId.empty())
	{
		errorText = u8"所选资源没有线上条目";
		return false;
	}
	const bool requestedResourceInstalled = !selectedEntry.isOnlineOnly();
	const bool requestedVersionMatches =
		requestedResourceInstalled && !selectedEntry.localVersion.empty() &&
		selectedEntry.localVersion == selectedEntry.onlineVersion;
	const InstalledResourceState installedState = installedResourceState();
	OnlineUpdate::RequestedResourceDownloadMode requestedDownloadMode =
		OnlineUpdate::RequestedResourceDownloadMode::IfNeeded;
	OnlineUpdate::ResourceDownloadPlan plan =
		OnlineUpdate::planResourceDownload(
			onlineCatalog,
			selectedEntry.gameId,
			JxqyBuildVersion::EngineVersion,
			installedState.artifacts,
			requestedDownloadMode);
	if (forceReinstallIfCurrent && requestedVersionMatches && plan.succeeded() &&
		plan.downloadOrder.empty())
	{
		requestedDownloadMode =
			OnlineUpdate::RequestedResourceDownloadMode::ForceFullPackage;
		plan = OnlineUpdate::planResourceDownload(
			onlineCatalog,
			selectedEntry.gameId,
			JxqyBuildVersion::EngineVersion,
			installedState.artifacts,
			requestedDownloadMode);
	}
	if (!plan.succeeded())
	{
		errorText = resourcePlanFailureText(
			plan.status, plan.blockingGameId);
		return false;
	}

	const std::string collectionRootText =
		ResourceManager::instance().getWritableResourceCollectionRoot();
	if (collectionRootText.empty())
	{
		errorText = u8"当前平台没有可写资源集合";
		return false;
	}
	std::filesystem::path collectionRoot;
	try
	{
		collectionRoot = std::filesystem::u8path(collectionRootText);
	}
	catch (const std::exception&)
	{
		errorText = u8"资源集合路径无效";
		return false;
	}
	std::error_code canonicalError;
	collectionRoot = std::filesystem::canonical(
		collectionRoot, canonicalError);
	if (canonicalError)
	{
		errorText = u8"资源集合路径无法解析";
		return false;
	}
	if (!isPlainDirectory(collectionRoot))
	{
		errorText = u8"资源集合目录不可用";
		return false;
	}
	const bool includeCommon = OnlineUpdate::commonPackageNeedsDownload(
		onlineCatalog, installedCommonArtifactCrc32(collectionRoot));
	std::uint64_t totalDownloadBytes = plan.totalDownloadBytes;
	if (includeCommon)
	{
		const std::uint64_t commonBytes =
			onlineCatalog.commonPackage->artifactSize;
		if (commonBytes >
			std::numeric_limits<std::uint64_t>::max() - totalDownloadBytes)
		{
			errorText = u8"线上资源下载大小无效";
			return false;
		}
		totalDownloadBytes += commonBytes;
		const std::filesystem::path commonRoot = collectionRoot / "common";
		std::error_code statusError;
		const std::filesystem::file_status commonStatus =
			std::filesystem::symlink_status(commonRoot, statusError);
		const bool replacingCommon = !statusError &&
			std::filesystem::exists(commonStatus);
		if ((statusError &&
				statusError != std::errc::no_such_file_or_directory) ||
			(replacingCommon &&
				!OnlineUpdate::isValidCommonResourceRoot(commonRoot)))
		{
			errorText = u8"现有游戏运行文件目录不可安全替换";
			return false;
		}
	}

	std::error_code spaceError;
	const std::filesystem::space_info space =
		std::filesystem::space(collectionRoot, spaceError);
	if (spaceError)
	{
		errorText = u8"无法检查资源目录磁盘空间";
		return false;
	}
	if (totalDownloadBytes >
		std::numeric_limits<std::uint64_t>::max() -
			ResourceDownloadDiskHeadroom ||
		space.available < totalDownloadBytes +
			ResourceDownloadDiskHeadroom)
	{
		errorText = u8"磁盘空间不足：至少需要下载大小加 64 MiB 临时空间";
		return false;
	}

	std::set<std::string> occupiedDirectoryNames;
	std::error_code directoryError;
	for (std::filesystem::directory_iterator iterator(
			collectionRoot,
			std::filesystem::directory_options::skip_permission_denied,
			directoryError), end;
		!directoryError && iterator != end;
		iterator.increment(directoryError))
	{
		occupiedDirectoryNames.insert(foldAscii(
			iterator->path().filename().generic_u8string()));
	}
	if (directoryError)
	{
		errorText = u8"无法读取资源集合目录";
		return false;
	}

	const auto& packs = ResourceManager::instance().getDiscoveredPacks();
	auto findWritableTargetName =
		[&packs, &collectionRoot, &errorText](
			const std::string& gameId,
			std::string& targetDirectoryName) -> bool
		{
			targetDirectoryName.clear();
			for (const ResourceManager::ResourcePack& pack : packs)
			{
				if (OnlineUpdate::foldGameId(pack.manifest.id) !=
					OnlineUpdate::foldGameId(gameId))
				{
					continue;
				}
				std::filesystem::path packRoot;
				try
				{
					packRoot = std::filesystem::u8path(pack.rootPath);
				}
				catch (const std::exception&)
				{
					continue;
				}
				std::error_code canonicalError;
				packRoot = std::filesystem::canonical(
					packRoot, canonicalError);
				if (canonicalError)
				{
					// APK、应用包等只读资源不一定是普通文件系统目录；
					// 它们不能原位替换，后续在可写集合中安装同 ID 副本。
					continue;
				}
				std::error_code equivalentError;
				const bool directChild = std::filesystem::equivalent(
					packRoot.parent_path(), collectionRoot, equivalentError);
				if (equivalentError || !directChild)
				{
					continue;
				}
				if (!isPlainDirectory(packRoot))
				{
					errorText = u8"可写资源目录不是普通目录：" + gameId;
					return false;
				}
				const std::string candidate =
					packRoot.filename().generic_u8string();
				if (!isSafeResourceDirectoryName(candidate))
				{
					errorText = u8"本地资源目录名不安全：" + candidate;
					return false;
				}
				if (!targetDirectoryName.empty() &&
					foldAscii(targetDirectoryName) != foldAscii(candidate))
				{
					errorText = u8"存在多个同 ID 的可写资源，无法确定替换目录：" +
						gameId;
					return false;
				}
				targetDirectoryName = candidate;
			}
			return true;
		};
	std::set<std::string> chosenTargets;
	confirmation.requestedGameId = selectedEntry.gameId;
	confirmation.collectionRoot = collectionRoot.generic_u8string();
	confirmation.totalDownloadBytes = totalDownloadBytes;
	confirmation.installedArtifacts = installedState.artifacts;
	confirmation.installedResourceRoots = installedState.roots;
	confirmation.requestedResourceInstalled = requestedResourceInstalled;
	confirmation.requestedVersionMatches = requestedVersionMatches;
	confirmation.requestedDownloadMode = requestedDownloadMode;
	confirmation.includesCommon = includeCommon;
	confirmation.targets.reserve(
		plan.downloadOrder.size() + (includeCommon ? 1U : 0U));
	confirmation.items.reserve(
		plan.downloadOrder.size() + (includeCommon ? 1U : 0U));
	for (const OnlineUpdate::ResourceDownloadPlan::Item& download :
		plan.downloadOrder)
	{
		const OnlineUpdate::ResourcePackage* package = download.package;
		if (package == nullptr)
		{
			errorText = u8"线上依赖计划包含无效资源";
			return false;
		}
		ResourceInstallConfirmationItem item;
		item.title = package->displayName.empty()
			? package->gameId : package->displayName;
		item.version = package->versionText;
		item.releaseNotes = package->releaseNotes;
		item.artifactKind = download.artifactKind;
		OnlineUpdate::ResourceInstallTarget target;
		target.gameId = package->gameId;
		if (!findWritableTargetName(
				package->gameId, target.targetDirectoryName))
		{
			return false;
		}
		if (!target.targetDirectoryName.empty())
		{
			item.replacing = true;
		}
		else
		{
			std::string baseName;
			try
			{
				baseName = std::filesystem::u8path(
					package->artifactPath).stem().generic_u8string();
			}
			catch (const std::exception&)
			{
			}
			if (!isSafeResourceDirectoryName(baseName))
			{
				baseName = "resource";
			}
			target.targetDirectoryName = baseName;
			for (int suffix = 2;
				occupiedDirectoryNames.find(foldAscii(
					target.targetDirectoryName)) !=
					occupiedDirectoryNames.end(); suffix++)
			{
				target.targetDirectoryName = baseName + "-" +
					std::to_string(suffix);
			}
			occupiedDirectoryNames.insert(
				foldAscii(target.targetDirectoryName));
		}
		if (!chosenTargets.insert(
				foldAscii(target.targetDirectoryName)).second)
		{
			errorText = u8"多个资源指向同一安装目录：" +
				target.targetDirectoryName;
			return false;
		}
		item.targetDirectoryName = target.targetDirectoryName;
		confirmation.targets.push_back(std::move(target));
		confirmation.items.push_back(std::move(item));
	}
	if (includeCommon)
	{
		const std::filesystem::path commonRoot = collectionRoot / "common";
		std::error_code statusError;
		const std::filesystem::file_status commonStatus =
			std::filesystem::symlink_status(commonRoot, statusError);
		const bool replacing = !statusError &&
			std::filesystem::exists(commonStatus);
		confirmation.targets.push_back({"common", "common"});
		ResourceInstallConfirmationItem item;
		item.title = u8"游戏运行文件（自动）";
		item.version = onlineCatalog.commonPackage->versionText;
		item.releaseNotes = onlineCatalog.commonPackage->releaseNotes;
		item.targetDirectoryName = "common";
		item.replacing = replacing;
		confirmation.items.push_back(std::move(item));
	}
	if (confirmation.targets.empty())
	{
		if (forceReinstallIfCurrent)
		{
			errorText = u8"所选游戏、依赖和运行文件均已是当前线上制品";
		}
		return false;
	}
	return true;
}

bool ResourceSelectScene::beginResourceDownloadConfirmation(
	bool promptedByEntry)
{
	if (resourceInstallDialogState != ResourceInstallDialogState::Hidden ||
		resourceInstallRunner != nullptr || resourceList == nullptr)
	{
		return false;
	}
	ResourceInstallConfirmation confirmation;
	std::string errorText;
	if (!buildResourceInstallConfirmation(
			resourceList->getSelectedIndex(),
			confirmation,
			errorText,
			!promptedByEntry))
	{
		if (!errorText.empty())
		{
			catalogStatusText = errorText;
			GameLog::write(
				"ResourceSelectScene: resource download confirmation failed"
				" (entry=%d, reason=%s)\n",
				resourceList->getSelectedIndex(),
				catalogStatusText.c_str());
		}
		return false;
	}
	cancelPointerInteraction();
	resourceInstallOperation = ResourceInstallOperation::OnlineDownload;
	resourceUpdatePromptedByEntry = promptedByEntry;
	pendingResourceInstall = std::move(confirmation);
	pendingDownloadUsesMeteredNetwork =
		pendingResourceInstall.totalDownloadBytes > 0 &&
		MobileNetwork::getActiveConnectionCost() ==
			MobileNetwork::ConnectionCost::Metered;
	pendingMeteredDownloadConfirmed = false;
	resourceInstallDialogMessage.clear();
	resourceInstallConfirmationPage = 0;
	resourceInstallDialogState = ResourceInstallDialogState::Confirming;
	setMainControlsAvailable(false);
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-secondary");
	updateFocusPresentation();
	return true;
}

void ResourceSelectScene::beginResourceRemovalConfirmation()
{
	if (resourceInstallDialogState != ResourceInstallDialogState::Hidden ||
		resourceInstallRunner != nullptr || resourceList == nullptr)
	{
		return;
	}
	const int selectedIndex = resourceList->getSelectedIndex();
	if (selectedIndex < 0 ||
		selectedIndex >= static_cast<int>(resourceEntries.size()) ||
		resourceEntries[selectedIndex].localPackIndex < 0)
	{
		catalogStatusText = u8"所选游戏尚未安装，不能删除";
		return;
	}
	ResourceManager::ResourceRemovalPlan plan =
		ResourceManager::instance().buildResourceRemovalPlan(
			resourceEntries[selectedIndex].localPackIndex);
	if (plan.status != ResourceManager::ResourceRemovalStatus::Success)
	{
		if (plan.status ==
			ResourceManager::ResourceRemovalStatus::DependencyBlocked)
		{
			catalogStatusText = u8"存在不可删除的依赖游戏，未执行删除";
		}
		else if (plan.status ==
			ResourceManager::ResourceRemovalStatus::NotRemovable)
		{
			catalogStatusText = u8"此游戏位于只读资源中，不能删除";
		}
		else
		{
			catalogStatusText = u8"资源目录已变化，请重新选择后再删除";
		}
		return;
	}
	cancelPointerInteraction();
	resourceInstallOperation = ResourceInstallOperation::ResourceRemoval;
	pendingResourceRemoval = std::move(plan);
	pendingResourceRemovalSavePolicy =
		ResourceManager::ResourceRemovalSavePolicy::Unselected;
	resourceInstallDialogMessage.clear();
	resourceInstallDialogState = ResourceInstallDialogState::Confirming;
	setMainControlsAvailable(false);
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-secondary");
	updateFocusPresentation();
}

void ResourceSelectScene::beginSaveManagement()
{
	if (resourceInstallDialogState != ResourceInstallDialogState::Hidden ||
		resourceInstallRunner != nullptr)
	{
		return;
	}
	cancelPointerInteraction();
	resourceInstallOperation = ResourceInstallOperation::SaveManagement;
	saveNamespaceEntries = ResourceManager::instance().listSaveNamespaces();
	selectedSaveNamespaceIndex = 0;
	resourceInstallDialogMessage.clear();
	resourceInstallDialogState = ResourceInstallDialogState::BrowsingSaves;
	setMainControlsAvailable(false);
	refreshResourceInstallDialogControls();
	semanticFocusVisible = saveNamespaceEntries.empty()
		? focusManager.focusNode("install-secondary")
		: focusManager.focusNode("install-primary");
	updateFocusPresentation();
}

void ResourceSelectScene::executeResourceRemoval()
{
	if (resourceInstallOperation != ResourceInstallOperation::ResourceRemoval ||
		resourceInstallDialogState != ResourceInstallDialogState::Confirming ||
		pendingResourceRemovalSavePolicy ==
			ResourceManager::ResourceRemovalSavePolicy::Unselected)
	{
		return;
	}
	const ResourceManager::ResourceRemovalResult result =
		ResourceManager::instance().removeResourceGroup(
			pendingResourceRemoval,
			pendingResourceRemovalSavePolicy);
	if (result.status == ResourceManager::ResourceRemovalStatus::Success)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Completed;
		resourceInstallDialogMessage = u8"已删除 " +
			std::to_string(result.removedGameIds.size()) + u8" 个游戏资源";
		if (pendingResourceRemovalSavePolicy ==
			ResourceManager::ResourceRemovalSavePolicy::Delete)
		{
			resourceInstallDialogMessage += u8"，并删除 " +
				std::to_string(result.removedSaveNamespaces.size()) +
				u8" 个相关存档目录。";
		}
		else
		{
			resourceInstallDialogMessage +=
				u8"；相关存档已保留，可稍后从存档管理清理。";
		}
	}
	else
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage = u8"删除未完成";
		if (!result.failedPath.empty())
		{
			resourceInstallDialogMessage += u8"：" + result.failedPath;
		}
		resourceInstallDialogMessage += u8"。已停止后续删除并重新扫描资源。";
	}
	buildResourceList();
	configureFocus();
	setMainControlsAvailable(false);
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-primary");
	updateFocusPresentation();
}

void ResourceSelectScene::executeSaveRemoval()
{
	if (resourceInstallOperation != ResourceInstallOperation::SaveManagement ||
		resourceInstallDialogState !=
			ResourceInstallDialogState::ConfirmingSaveRemoval ||
		selectedSaveNamespaceIndex < 0 ||
		selectedSaveNamespaceIndex >=
			static_cast<int>(saveNamespaceEntries.size()))
	{
		return;
	}
	const std::string saveNamespace =
		saveNamespaceEntries[selectedSaveNamespaceIndex].saveNamespace;
	const ResourceManager::ResourceRemovalResult result =
		ResourceManager::instance().removeSaveNamespaces({ saveNamespace });
	if (result.status == ResourceManager::ResourceRemovalStatus::Success)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Completed;
		resourceInstallDialogMessage = u8"已删除存档：" + saveNamespace;
	}
	else
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage = u8"存档删除失败";
		if (!result.failedPath.empty())
		{
			resourceInstallDialogMessage += u8"：" + result.failedPath;
		}
	}
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-primary");
	updateFocusPresentation();
}

void ResourceSelectScene::activateResourceDialogPrimary()
{
	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming)
	{
		if (resourceInstallOperation == ResourceInstallOperation::ResourceRemoval)
		{
			executeResourceRemoval();
		}
		else
		{
			if (pendingDownloadUsesMeteredNetwork &&
				!pendingMeteredDownloadConfirmed)
			{
				pendingMeteredDownloadConfirmed = true;
				refreshResourceInstallDialogControls();
				updateFocusPresentation();
				return;
			}
#if defined(__APPLE__) && TARGET_OS_IOS
			if (resourceInstallOperation ==
				ResourceInstallOperation::ProgramDownload)
			{
				startPreparedProgramUpdate();
				return;
			}
#endif
			startConfirmedResourceDownload();
		}
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::BrowsingSaves)
	{
		if (!saveNamespaceEntries.empty())
		{
			resourceInstallDialogState =
				ResourceInstallDialogState::ConfirmingSaveRemoval;
			refreshResourceInstallDialogControls();
			semanticFocusVisible = focusManager.focusNode("install-secondary");
			updateFocusPresentation();
		}
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::ConfirmingSaveRemoval)
	{
		executeSaveRemoval();
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::ReadyToRestart)
	{
		if (resourceInstallOperation == ResourceInstallOperation::ProgramDownload)
		{
			startPreparedProgramUpdate();
		}
		else
		{
			stop(erExit);
		}
	}
	else if (resourceInstallDialogState == ResourceInstallDialogState::Failed ||
		resourceInstallDialogState == ResourceInstallDialogState::Completed)
	{
		dismissResourceInstallDialog();
	}
}

void ResourceSelectScene::activateResourceDialogSecondary()
{
	if (resourceInstallDialogState ==
		ResourceInstallDialogState::ConfirmingSaveRemoval)
	{
		resourceInstallDialogState = ResourceInstallDialogState::BrowsingSaves;
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming &&
		resourceInstallOperation == ResourceInstallOperation::OnlineDownload &&
		resourceUpdatePromptedByEntry &&
		!(pendingDownloadUsesMeteredNetwork &&
			pendingMeteredDownloadConfirmed))
	{
		const int selectedIndex = resourceList != nullptr
			? resourceList->getSelectedIndex() : -1;
		dismissResourceInstallDialog();
		enterSelectedResource(selectedIndex);
		return;
	}
	cancelResourceInstall();
}

void ResourceSelectScene::beginProgramDownloadConfirmation()
{
	if (resourceInstallDialogState != ResourceInstallDialogState::Hidden ||
		resourceInstallRunner != nullptr)
	{
		return;
	}
	if (catalogCheckState != CatalogCheckState::Ready)
	{
		if (catalogCheckRunner != nullptr ||
			catalogCheckState == CatalogCheckState::Checking)
		{
			catalogStatusText = u8"正在检查主程序版本…";
			return;
		}
		beginOnlineCatalogCheck();
		if (catalogCheckState == CatalogCheckState::Checking)
		{
			catalogStatusText = u8"正在检查主程序版本…";
		}
		return;
	}
	const OnlineUpdate::ProgramUpdateCheck update =
		OnlineUpdate::checkProgramUpdate(
			onlineApplicationCatalog,
			JxqyBuildVersion::ProgramUpdateTarget,
			JxqyBuildVersion::EngineVersion);
	if (!canUseOnlineProgramPackage(update))
	{
		switch (update.status)
		{
		case OnlineUpdate::ProgramUpdateStatus::UpToDate:
			catalogStatusText = u8"macOS 当前没有可安装的线上主程序";
			break;
		case OnlineUpdate::ProgramUpdateStatus::TargetNotFound:
			catalogStatusText = u8"线上暂未提供当前平台的主程序";
			break;
		case OnlineUpdate::ProgramUpdateStatus::InvalidInput:
		default:
			catalogStatusText = u8"无法判断当前主程序版本";
			break;
		}
		return;
	}
	pendingDownloadUsesMeteredNetwork = update.package != nullptr &&
		update.package->artifactSize > 0 &&
		MobileNetwork::getActiveConnectionCost() ==
			MobileNetwork::ConnectionCost::Metered;
	pendingMeteredDownloadConfirmed = false;

#if defined(__ANDROID__)
	const std::string updateDirectoryText =
		AndroidProgramUpdate::getApplicationUpdateDirectoryPath();
	if (updateDirectoryText.empty())
	{
		catalogStatusText = u8"Android 应用专属更新目录不可用";
		return;
	}
	std::filesystem::path updateDirectory;
	try
	{
		updateDirectory = std::filesystem::u8path(
			updateDirectoryText).lexically_normal();
	}
	catch (const std::exception&)
	{
		catalogStatusText = u8"Android 主程序更新路径无效";
		return;
	}
	if (!updateDirectory.is_absolute() ||
		!isPlainDirectory(updateDirectory.parent_path()))
	{
		catalogStatusText = u8"Android 应用专属下载目录不可用";
		return;
	}
	std::error_code error;
	const bool updateDirectoryExists =
		std::filesystem::exists(updateDirectory, error);
	if (error || (updateDirectoryExists &&
		!isPlainDirectory(updateDirectory)))
	{
		catalogStatusText = u8"Android 主程序更新目录不可安全使用";
		return;
	}
	const std::filesystem::path apkPath =
		updateDirectory / "jxqy-update.apk";
	bool apkExists = std::filesystem::exists(apkPath, error);
	if (error || (apkExists && !isPlainRegularFile(apkPath)))
	{
		catalogStatusText = u8"Android 主程序安装包路径存在冲突";
		return;
	}
	if (apkExists)
	{
		std::uint32_t apkChecksum = 0;
		std::uint64_t apkSize = 0;
		if (!OnlineUpdate::calculateFileCrc32(
				apkPath, apkChecksum, apkSize))
		{
			catalogStatusText = u8"无法读取已下载的 Android 安装包";
			return;
		}
		if (apkSize != update.package->artifactSize ||
			OnlineUpdate::crc32ToLowerHex(apkChecksum) !=
				update.package->crc32Hex)
		{
			const bool removed = std::filesystem::remove(apkPath, error);
			if (error || !removed)
			{
				catalogStatusText = u8"无法清理不完整的 Android 安装包";
				return;
			}
			apkExists = false;
		}
	}

	ResourceInstallConfirmation confirmation;
	confirmation.requestedGameId = JxqyBuildVersion::ProgramUpdateTarget;
	confirmation.collectionRoot = updateDirectory.generic_u8string();
	confirmation.totalDownloadBytes = update.package->artifactSize;
	ResourceInstallConfirmationItem item;
	item.title = u8"剑侠情缘 All-in-One Android 程序";
	item.version = update.package->versionText;
	item.releaseNotes = update.package->releaseNotes;
	item.targetDirectoryName = "Downloads/updates/jxqy-update.apk";
	item.replacing = true;
	confirmation.items.push_back(std::move(item));

	cancelPointerInteraction();
	resourceInstallOperation = ResourceInstallOperation::ProgramDownload;
	pendingProgramPackagePath = apkExists
		? apkPath.generic_u8string() : std::string();
	pendingResourceInstall = std::move(confirmation);
	resourceInstallDialogMessage.clear();
	resourceInstallConfirmationPage = 0;
	pendingDownloadUsesMeteredNetwork =
		pendingDownloadUsesMeteredNetwork && !apkExists;
	setMainControlsAvailable(false);
	if (apkExists)
	{
		resourceInstallDialogState =
			ResourceInstallDialogState::ReadyToRestart;
		resourceInstallDialogMessage =
			u8"Android 安装包已下载并校验完成。选择“打开安装确认”"
			u8"后由系统确认安装；当前资源和存档不会改变。";
	}
	else
	{
		std::error_code spaceError;
		const std::filesystem::space_info space =
			std::filesystem::space(
				updateDirectory.parent_path(), spaceError);
		if (spaceError || update.package->artifactSize >
				std::numeric_limits<std::uint64_t>::max() -
					ResourceDownloadDiskHeadroom ||
			space.available < update.package->artifactSize +
				ResourceDownloadDiskHeadroom)
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = spaceError
				? std::string(u8"无法检查 Android 下载目录磁盘空间")
				: std::string(u8"磁盘空间不足：至少需要安装包大小加 64 MiB 临时空间");
		}
		else
		{
			resourceInstallDialogState =
				ResourceInstallDialogState::Confirming;
		}
	}
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode(
		resourceInstallDialogState == ResourceInstallDialogState::Failed
			? "install-primary" : "install-secondary");
	updateFocusPresentation();
#elif defined(__APPLE__)
	std::string installPageUrl;
	if (onlineApplicationCatalogSources.catalogUrls.empty() ||
		!OnlineUpdate::buildHttpsArtifactUrl(
			onlineApplicationCatalogSources.catalogUrls.front(),
			update.package->artifactPath,
			installPageUrl))
	{
		catalogStatusText =
#if TARGET_OS_IOS
			u8"iOS 主程序下载页面地址无效";
#else
			u8"macOS Sparkle 更新目录地址无效";
#endif
		return;
	}

	ResourceInstallConfirmation confirmation;
	confirmation.requestedGameId = JxqyBuildVersion::ProgramUpdateTarget;
	confirmation.collectionRoot = installPageUrl;
	confirmation.totalDownloadBytes =
#if TARGET_OS_IOS
		update.package->artifactSize;
#else
		0;
#endif
	ResourceInstallConfirmationItem item;
	item.title =
#if TARGET_OS_IOS
		u8"剑侠情缘 All-in-One iOS 程序";
#else
		u8"剑侠情缘 All-in-One macOS 程序";
#endif
	item.version = update.package->versionText;
	item.releaseNotes = update.package->releaseNotes;
	item.targetDirectoryName =
#if TARGET_OS_IOS
		u8"程序下载页面";
#else
		u8"Sparkle appcast";
#endif
	item.replacing = true;
	confirmation.items.push_back(std::move(item));

	cancelPointerInteraction();
	resourceInstallOperation = ResourceInstallOperation::ProgramDownload;
	pendingProgramPackagePath = installPageUrl;
	pendingResourceInstall = std::move(confirmation);
	pendingDownloadUsesMeteredNetwork = false;
	resourceInstallDialogMessage =
#if TARGET_OS_IOS
		u8"确认版本和更新内容后，可选择“打开下载页面”交给系统浏览器继续处理。";
#else
		u8"选择“检查并安装”后由 Sparkle 显示标准更新界面，"
		u8"当前资源和存档不会改变，也可以继续当前游戏。";
#endif
	resourceInstallConfirmationPage = 0;
	resourceInstallDialogState =
#if TARGET_OS_IOS
		ResourceInstallDialogState::Confirming;
#else
		ResourceInstallDialogState::ReadyToRestart;
#endif
	setMainControlsAvailable(false);
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-secondary");
	updateFocusPresentation();
#else
	DesktopProgramUpdatePaths paths;
	if (!resolveDesktopProgramUpdatePaths(paths))
	{
		catalogStatusText = u8"无法确定主程序下载位置";
		return;
	}
	std::error_code error;

	ResourceInstallConfirmation confirmation;
	confirmation.requestedGameId = JxqyBuildVersion::ProgramUpdateTarget;
	confirmation.collectionRoot = paths.releaseRoot.generic_u8string();
	confirmation.totalDownloadBytes = update.package->artifactSize;
	ResourceInstallConfirmationItem item;
	item.title = u8"剑侠情缘 All-in-One 主程序";
	item.version = update.package->versionText;
	item.releaseNotes = update.package->releaseNotes;
	item.targetDirectoryName = "bin/" + paths.helperTarget;
	item.replacing = true;
	confirmation.items.push_back(std::move(item));

	cancelPointerInteraction();
	resourceInstallOperation = ResourceInstallOperation::ProgramDownload;
	pendingResourceInstall = std::move(confirmation);
	resourceInstallDialogMessage.clear();
	resourceInstallConfirmationPage = 0;
	setMainControlsAvailable(false);
	if (isPreparedDesktopProgramUpdate(paths))
	{
		resourceInstallDialogState =
			ResourceInstallDialogState::ReadyToRestart;
		resourceInstallDialogMessage =
			u8"主程序已下载并校验完成。选择“安装并退出”后，"
			u8"独立更新助手会更新程序、引擎资源和公共资源；"
			u8"游戏资源与存档不会改变。";
	}
	else
	{
		const bool workspaceExists =
			std::filesystem::exists(paths.workspacePath, error);
		if (error || (workspaceExists &&
			!isPlainDirectory(paths.workspacePath)))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"主程序更新暂存目录不可用";
		}
		else
		{
			std::error_code spaceError;
			const std::filesystem::space_info space =
				std::filesystem::space(paths.releaseRoot, spaceError);
			if (spaceError || update.package->artifactSize >
					std::numeric_limits<std::uint64_t>::max() -
						ResourceDownloadDiskHeadroom ||
				space.available < update.package->artifactSize +
					ResourceDownloadDiskHeadroom)
			{
				resourceInstallDialogState =
					ResourceInstallDialogState::Failed;
				resourceInstallDialogMessage = spaceError
					? std::string(u8"无法检查程序目录磁盘空间")
					: std::string(u8"磁盘空间不足：至少需要安装包大小加 64 MiB 临时空间");
			}
			else
			{
				resourceInstallDialogState =
					ResourceInstallDialogState::Confirming;
			}
		}
	}
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode(
		resourceInstallDialogState == ResourceInstallDialogState::Failed
			? "install-primary" : "install-secondary");
	updateFocusPresentation();
#endif
}

void ResourceSelectScene::startConfirmedResourceDownload()
{
	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming &&
		resourceInstallOperation == ResourceInstallOperation::ProgramDownload &&
		resourceInstallRunner == nullptr)
	{
#if defined(__ANDROID__)
		const std::string updateDirectoryText =
			AndroidProgramUpdate::getApplicationUpdateDirectoryPath();
		std::filesystem::path updateDirectory;
		try
		{
			updateDirectory = std::filesystem::u8path(
				updateDirectoryText).lexically_normal();
		}
		catch (const std::exception&)
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"Android 主程序更新路径无效";
			refreshResourceInstallDialogControls();
			return;
		}
		if (updateDirectoryText.empty() || !updateDirectory.is_absolute() ||
			updateDirectory.generic_u8string() !=
				pendingResourceInstall.collectionRoot ||
			!isPlainDirectory(updateDirectory.parent_path()))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"Android 应用专属下载目录不可用";
			refreshResourceInstallDialogControls();
			return;
		}
		std::error_code error;
		const bool updateDirectoryExists =
			std::filesystem::exists(updateDirectory, error);
		if (error || (!updateDirectoryExists &&
			(!std::filesystem::create_directory(updateDirectory, error) ||
				error)) ||
			!isPlainDirectory(updateDirectory))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"无法创建 Android 主程序更新目录";
			refreshResourceInstallDialogControls();
			return;
		}
		const std::filesystem::path apkPath =
			updateDirectory / "jxqy-update.apk";
		const std::filesystem::path downloadWorkspace =
			updateDirectory / "download";
		if (!removePlainOwnedFileIfPresent(apkPath))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage =
				u8"无法安全清理上次遗留的 Android 安装包";
			refreshResourceInstallDialogControls();
			return;
		}
		if (!removePlainOwnedDirectoryIfPresent(downloadWorkspace))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage =
				u8"无法安全清理上次中断的 Android 下载";
			refreshResourceInstallDialogControls();
			return;
		}

		resourceInstallWorkerResult =
			std::make_shared<ResourceInstallWorkerResult>();
		resourceInstallWorkerResult->operation = resourceInstallOperation;
		resourceInstallWorkerResult->packageCount = 1;
		resourceInstallWorkerResult->totalBytes =
			pendingResourceInstall.totalDownloadBytes;
		const std::shared_ptr<ResourceInstallWorkerResult> workerResult =
			resourceInstallWorkerResult;
		const OnlineUpdate::Catalog catalog = onlineApplicationCatalog;
		const OnlineUpdate::CatalogMirrorSources catalogSources =
			onlineApplicationCatalogSources;
		resourceInstallDialogState = ResourceInstallDialogState::Downloading;
		resourceInstallDialogMessage = u8"正在下载并校验 Android 安装包…";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-secondary");
		resourceInstallRunner =
			std::make_unique<GameLoading::ExclusiveLoadingRunner>(
				[catalog,
					catalogSources,
					downloadWorkspace,
					apkPath,
					workerResult](
						const GameLoading::LoadingCancellationToken&
							cancellationToken)
				{
					workerResult->programPreparation =
						OnlineUpdate::prepareProgramDownload(
							catalog,
							JxqyBuildVersion::ProgramUpdateTarget,
							JxqyBuildVersion::EngineVersion,
							catalogSources,
							downloadWorkspace,
							[workerResult, cancellationToken](
								std::uint64_t transferredBytes,
								std::uint64_t)
							{
								workerResult->completedBytes.store(
									transferredBytes,
									std::memory_order_release);
								return !cancellationToken.
									isCancellationRequested();
							});
					if (workerResult->programPreparation.status ==
							OnlineUpdate::ResourceDownloadPreparationStatus::
								Cancelled ||
						cancellationToken.isCancellationRequested())
					{
						std::error_code cleanupError;
						std::filesystem::remove_all(
							downloadWorkspace, cleanupError);
						return GameLoading::LoadingTaskResult::cancellation();
					}
					if (!workerResult->programPreparation.succeeded())
					{
						return GameLoading::LoadingTaskResult::failure(
							"Android program download preparation failed.");
					}
					std::error_code finalizeError;
					std::filesystem::rename(
						workerResult->programPreparation.artifactPath,
						apkPath,
						finalizeError);
					if (finalizeError)
					{
						workerResult->programFailureMessage =
							u8"无法保存 Android 安装包";
						std::error_code cleanupError;
						std::filesystem::remove_all(
							downloadWorkspace, cleanupError);
						return GameLoading::LoadingTaskResult::failure(
							"Android program package finalization failed.");
					}
					const bool workspaceRemoved = std::filesystem::remove(
						downloadWorkspace, finalizeError);
					if (finalizeError || !workspaceRemoved)
					{
						workerResult->programFailureMessage =
							u8"无法清理 Android 下载暂存目录";
						return GameLoading::LoadingTaskResult::failure(
							"Android program workspace cleanup failed.");
					}
					workerResult->preparedProgramPath =
						apkPath.generic_u8string();
					workerResult->programReady = true;
					workerResult->completedBytes.store(
						workerResult->totalBytes,
						std::memory_order_release);
					return GameLoading::LoadingTaskResult::success();
				});
		return;
#else
		DesktopProgramUpdatePaths paths;
		if (!resolveDesktopProgramUpdatePaths(paths) ||
			paths.releaseRoot.generic_u8string() !=
				pendingResourceInstall.collectionRoot)
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"主程序下载位置已改变，请重新确认";
			refreshResourceInstallDialogControls();
			return;
		}
		std::error_code error;
		const bool workspaceExists =
			std::filesystem::exists(paths.workspacePath, error);
		if (error || (!workspaceExists &&
			(!std::filesystem::create_directories(
				paths.workspacePath, error) || error)) ||
			!isPlainDirectory(paths.workspacePath))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = u8"主程序更新暂存目录存在冲突";
			refreshResourceInstallDialogControls();
			return;
		}
		const bool previousExists =
			std::filesystem::exists(paths.previousPath, error);
		if (error || previousExists)
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage =
				u8"主程序更新存在待恢复的旧版本，请重新运行更新助手";
			refreshResourceInstallDialogControls();
			return;
		}
		const bool stagingExists =
			std::filesystem::exists(paths.stagingPath, error);
		const bool stagingPrepared = stagingExists &&
			isPreparedDesktopProgramUpdate(paths);
		if (error || (stagingExists &&
			(stagingPrepared ||
				!removePlainOwnedDirectoryIfPresent(paths.stagingPath))))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = stagingPrepared
				? std::string(u8"主程序已经准备完成，请重新打开更新确认")
				: std::string(u8"无法安全清理未完成的主程序解压目录");
			refreshResourceInstallDialogControls();
			return;
		}
		const std::filesystem::path downloadWorkspace =
			paths.workspacePath / "download";
		if (!removePlainOwnedDirectoryIfPresent(downloadWorkspace))
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage =
				u8"无法安全清理上次中断的主程序下载";
			refreshResourceInstallDialogControls();
			return;
		}

		resourceInstallWorkerResult =
			std::make_shared<ResourceInstallWorkerResult>();
		resourceInstallWorkerResult->operation = resourceInstallOperation;
		resourceInstallWorkerResult->packageCount = 1;
		resourceInstallWorkerResult->totalBytes =
			pendingResourceInstall.totalDownloadBytes;
		const std::shared_ptr<ResourceInstallWorkerResult> workerResult =
			resourceInstallWorkerResult;
		const OnlineUpdate::Catalog catalog = onlineApplicationCatalog;
		const OnlineUpdate::CatalogMirrorSources catalogSources =
			onlineApplicationCatalogSources;
		resourceInstallDialogState = ResourceInstallDialogState::Downloading;
		resourceInstallDialogMessage = u8"正在下载并校验主程序…";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-secondary");
		resourceInstallRunner =
			std::make_unique<GameLoading::ExclusiveLoadingRunner>(
				[catalog,
					catalogSources,
					downloadWorkspace,
					stagingPath = paths.stagingPath,
					workerResult](
						const GameLoading::LoadingCancellationToken&
							cancellationToken)
				{
					workerResult->programPreparation =
						OnlineUpdate::prepareProgramDownload(
							catalog,
							JxqyBuildVersion::ProgramUpdateTarget,
							JxqyBuildVersion::EngineVersion,
							catalogSources,
							downloadWorkspace,
							[workerResult, cancellationToken](
								std::uint64_t transferredBytes,
								std::uint64_t)
							{
								workerResult->completedBytes.store(
									transferredBytes,
									std::memory_order_release);
								return !cancellationToken.
									isCancellationRequested();
							});
					if (workerResult->programPreparation.status ==
							OnlineUpdate::ResourceDownloadPreparationStatus::
								Cancelled ||
						cancellationToken.isCancellationRequested())
					{
						std::error_code cleanupError;
						std::filesystem::remove_all(
							downloadWorkspace, cleanupError);
						return GameLoading::LoadingTaskResult::cancellation();
					}
					if (!workerResult->programPreparation.succeeded())
					{
						return GameLoading::LoadingTaskResult::failure(
							"Program download preparation failed.");
					}
					workerResult->progressStage.store(
						ResourceInstallProgressStage::ValidatingAndExtracting,
						std::memory_order_release);
					workerResult->programPackageResult =
						OnlineUpdate::prepareDesktopProgramPackageArchive(
							workerResult->programPreparation.package,
							workerResult->programPreparation.artifactPath,
							stagingPath);
					std::error_code cleanupError;
					std::filesystem::remove_all(
						downloadWorkspace, cleanupError);
					if (cancellationToken.isCancellationRequested())
					{
						std::error_code stagingCleanupError;
						std::filesystem::remove_all(
							stagingPath, stagingCleanupError);
						if (cleanupError || stagingCleanupError)
						{
							workerResult->programPackageResult.status =
								OnlineUpdate::ResourcePackageArchiveStatus::
									CleanupFailed;
							return GameLoading::LoadingTaskResult::failure(
								"Cancelled program package cleanup failed.");
						}
						return GameLoading::LoadingTaskResult::cancellation();
					}
					if (cleanupError)
					{
						workerResult->programPackageResult.status =
							OnlineUpdate::ResourcePackageArchiveStatus::
								CleanupFailed;
					}
					if (!workerResult->programPackageResult.succeeded())
					{
						return GameLoading::LoadingTaskResult::failure(
							"Program package extraction failed.");
					}
					workerResult->completedBytes.store(
						workerResult->totalBytes,
						std::memory_order_release);
					return GameLoading::LoadingTaskResult::success();
				});
		return;
#endif
	}
	if (resourceInstallDialogState != ResourceInstallDialogState::Confirming ||
		resourceInstallOperation != ResourceInstallOperation::OnlineDownload ||
		resourceInstallRunner != nullptr ||
		pendingResourceInstall.targets.empty())
	{
		return;
	}
	std::filesystem::path collectionRoot;
	try
	{
		collectionRoot = std::filesystem::u8path(
			pendingResourceInstall.collectionRoot);
	}
	catch (const std::exception&)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage = u8"资源集合路径无效";
		refreshResourceInstallDialogControls();
		return;
	}
	if (!isPlainDirectory(collectionRoot))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage = u8"资源集合目录不可用";
		refreshResourceInstallDialogControls();
		return;
	}
	const std::filesystem::path updateDirectory =
		OnlineUpdate::resourceUpdateDirectoryPath(collectionRoot);
	std::error_code error;
	const bool updateDirectoryExists =
		std::filesystem::exists(updateDirectory, error);
	if (error || (!updateDirectoryExists &&
		(!std::filesystem::create_directory(updateDirectory, error) || error)) ||
		!isPlainDirectory(updateDirectory))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage = u8"无法创建安全的资源更新暂存目录";
		refreshResourceInstallDialogControls();
		return;
	}
	const std::filesystem::path workspacePath =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	if (std::filesystem::exists(workspacePath, error) || error)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"已有待处理资源更新，请退出并重新启动游戏";
		refreshResourceInstallDialogControls();
		return;
	}

	resourceInstallWorkerResult =
		std::make_shared<ResourceInstallWorkerResult>();
	resourceInstallWorkerResult->operation = resourceInstallOperation;
	resourceInstallWorkerResult->packageCount =
		pendingResourceInstall.targets.size();
	resourceInstallWorkerResult->totalBytes =
		pendingResourceInstall.totalDownloadBytes;
	const std::shared_ptr<ResourceInstallWorkerResult> workerResult =
		resourceInstallWorkerResult;
	const OnlineUpdate::Catalog catalog = onlineCatalog;
	const std::string requestedGameId =
		pendingResourceInstall.requestedGameId;
	const OnlineUpdate::CatalogMirrorSources catalogSources =
		onlineResourceCatalogSources;
	const std::vector<OnlineUpdate::ResourceInstallTarget> targets =
		pendingResourceInstall.targets;
	const OnlineUpdate::InstalledResourceArtifactMap installedArtifacts =
		pendingResourceInstall.installedArtifacts;
	const OnlineUpdate::InstalledResourceRootMap installedResourceRoots =
		pendingResourceInstall.installedResourceRoots;
	const OnlineUpdate::RequestedResourceDownloadMode requestedDownloadMode =
		pendingResourceInstall.requestedDownloadMode;
	const bool includesCommon = pendingResourceInstall.includesCommon;
	resourceInstallDialogState = ResourceInstallDialogState::Downloading;
	resourceInstallDialogMessage = u8"正在下载游戏资源…";
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode("install-secondary");

	resourceInstallRunner =
		std::make_unique<GameLoading::ExclusiveLoadingRunner>(
		[catalog,
			requestedGameId,
			catalogSources,
			collectionRoot,
			workspacePath,
			targets,
			installedArtifacts,
			installedResourceRoots,
			requestedDownloadMode,
			includesCommon,
			workerResult](
				const GameLoading::LoadingCancellationToken& cancellationToken)
		{
			workerResult->preparation =
				OnlineUpdate::prepareResourceDownload(
					catalog,
					requestedGameId,
					JxqyBuildVersion::EngineVersion,
					catalogSources,
					workspacePath,
					installedArtifacts,
					installedResourceRoots,
					requestedDownloadMode,
					[workerResult, cancellationToken](
						const OnlineUpdate::ResourceDownloadPreparationProgress&
							progress)
					{
						workerResult->completedBytes.store(
							progress.completedBytes,
							std::memory_order_release);
						workerResult->packageIndex.store(
							progress.packageIndex,
							std::memory_order_release);
						workerResult->progressStage.store(
							progress.stage == OnlineUpdate::
								ResourceDownloadPreparationProgress::Stage::
									ValidatingAndExtracting
								? ResourceInstallProgressStage::
									ValidatingAndExtracting
								: ResourceInstallProgressStage::Downloading,
							std::memory_order_release);
						return !cancellationToken.isCancellationRequested();
					});
			if (workerResult->preparation.status ==
					OnlineUpdate::ResourceDownloadPreparationStatus::Cancelled ||
				cancellationToken.isCancellationRequested())
			{
				std::error_code cleanupError;
				std::filesystem::remove_all(workspacePath, cleanupError);
				if (cleanupError)
				{
					workerResult->preparation.status =
						OnlineUpdate::ResourceDownloadPreparationStatus::
							CleanupFailed;
					return GameLoading::LoadingTaskResult::failure(
						"Cancelled resource download cleanup failed.");
				}
				return GameLoading::LoadingTaskResult::cancellation();
			}
			if (!workerResult->preparation.succeeded())
			{
				return GameLoading::LoadingTaskResult::failure(
					"Resource download preparation failed.");
			}
			const std::size_t resourcePackageCount = targets.size() -
				(includesCommon ? 1U : 0U);
			if (workerResult->preparation.preparedResources.size() !=
				resourcePackageCount)
			{
				if (!removePlainOwnedDirectoryIfPresent(workspacePath))
				{
					workerResult->preparation.status =
						OnlineUpdate::ResourceDownloadPreparationStatus::
							CleanupFailed;
					return GameLoading::LoadingTaskResult::failure(
						"Prepared resource count cleanup failed.");
				}
				return GameLoading::LoadingTaskResult::failure(
					"Prepared resource count does not match install targets.");
			}
			if (includesCommon)
			{
				const std::filesystem::path commonWorkspace =
					workspacePath / "common-download";
				const std::uint64_t resourceBytes =
					workerResult->preparation.totalDownloadBytes;
				workerResult->commonPreparation =
					OnlineUpdate::prepareCommonDownload(
						catalog,
						catalogSources,
						commonWorkspace,
						[workerResult, cancellationToken, resourceBytes](
							const OnlineUpdate::
								ResourceDownloadPreparationProgress& progress)
						{
							workerResult->completedBytes.store(
								resourceBytes + progress.completedBytes,
								std::memory_order_release);
							workerResult->packageIndex.store(
								workerResult->preparation.preparedResources.size(),
								std::memory_order_release);
							workerResult->progressStage.store(
								progress.stage == OnlineUpdate::
									ResourceDownloadPreparationProgress::Stage::
										ValidatingAndExtracting
									? ResourceInstallProgressStage::
										ValidatingAndExtracting
									: ResourceInstallProgressStage::Downloading,
								std::memory_order_release);
							return !cancellationToken.
								isCancellationRequested();
						});
				if (workerResult->commonPreparation.status ==
						OnlineUpdate::ResourceDownloadPreparationStatus::
							Cancelled ||
					cancellationToken.isCancellationRequested())
				{
					std::error_code cleanupError;
					std::filesystem::remove_all(workspacePath, cleanupError);
					if (cleanupError)
					{
						workerResult->commonPreparation.status =
							OnlineUpdate::ResourceDownloadPreparationStatus::
								CleanupFailed;
						return GameLoading::LoadingTaskResult::failure(
							"Cancelled common download cleanup failed.");
					}
					return GameLoading::LoadingTaskResult::cancellation();
				}
				if (!workerResult->commonPreparation.succeeded())
				{
					if (!removePlainOwnedDirectoryIfPresent(workspacePath))
					{
						workerResult->commonPreparation.status =
							OnlineUpdate::ResourceDownloadPreparationStatus::
								CleanupFailed;
					}
					return GameLoading::LoadingTaskResult::failure(
						"Common download preparation failed.");
				}
				const std::filesystem::path combinedCommonPath =
					workspacePath / "prepared" /
						("package-" + std::to_string(resourcePackageCount));
				std::error_code moveError;
				std::filesystem::rename(
					workerResult->commonPreparation.preparedCommonPath,
					combinedCommonPath,
					moveError);
				std::error_code cleanupError;
				std::filesystem::remove_all(commonWorkspace, cleanupError);
				if (moveError || cleanupError)
				{
					workerResult->commonPreparation.status =
						OnlineUpdate::ResourceDownloadPreparationStatus::
							CleanupFailed;
					std::filesystem::remove_all(workspacePath, cleanupError);
					return GameLoading::LoadingTaskResult::failure(
						"Common package merge failed.");
				}
			}
			if (cancellationToken.isCancellationRequested())
			{
				std::error_code cleanupError;
				std::filesystem::remove_all(workspacePath, cleanupError);
				if (cleanupError)
				{
					workerResult->preparation.status =
						OnlineUpdate::ResourceDownloadPreparationStatus::
							CleanupFailed;
					return GameLoading::LoadingTaskResult::failure(
						"Cancelled prepared resource cleanup failed.");
				}
				return GameLoading::LoadingTaskResult::cancellation();
			}
			workerResult->progressStage.store(
				ResourceInstallProgressStage::Staging,
				std::memory_order_release);
			workerResult->transaction =
				OnlineUpdate::stageResourceInstallTransaction(
					collectionRoot, requestedGameId, targets);
			if (workerResult->transaction.status !=
				OnlineUpdate::ResourceInstallTransactionStatus::Success)
			{
				if (!removePlainOwnedDirectoryIfPresent(workspacePath))
				{
					workerResult->transaction.status =
						OnlineUpdate::ResourceInstallTransactionStatus::
							CleanupFailed;
					return GameLoading::LoadingTaskResult::failure(
						"Resource install staging cleanup failed.");
				}
				return GameLoading::LoadingTaskResult::failure(
					"Resource install transaction staging failed.");
			}
			workerResult->completedBytes.store(
				workerResult->totalBytes,
				std::memory_order_release);
			return GameLoading::LoadingTaskResult::success();
		});
}

void ResourceSelectScene::pollResourceInstall()
{
	if (resourceInstallRunner == nullptr)
	{
		return;
	}
	const GameLoading::ExclusiveLoadingPollStatus status =
		resourceInstallRunner->poll(
			{},
			[this](const GameLoading::ExclusiveLoadingCompletion& completion)
			{
				finishResourceInstall(completion);
			});
	if (status == GameLoading::ExclusiveLoadingPollStatus::Finalized)
	{
		resourceInstallRunner.reset();
		resourceInstallWorkerResult.reset();
		refreshCheckUpdatesButton();
		refreshOnlineActionButton();
	}
}

void ResourceSelectScene::finishResourceInstall(
	const GameLoading::ExclusiveLoadingCompletion& completion)
{
	if (resourceInstallWorkerResult == nullptr)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage = u8"资源操作结果不可用";
	}
	else if (resourceInstallWorkerResult->operation ==
		ResourceInstallOperation::ProgramDownload)
	{
		if (completion.taskResult.status ==
				GameLoading::LoadingTaskStatus::Cancelled ||
			resourceInstallWorkerResult->programPreparation.status ==
				OnlineUpdate::ResourceDownloadPreparationStatus::Cancelled)
		{
			catalogStatusText = u8"主程序下载已取消，当前程序未改变";
			resourceInstallDialogState = ResourceInstallDialogState::Hidden;
			pendingResourceInstall = {};
			resourceInstallDialogMessage.clear();
			resourceInstallConfirmationPage = 0;
			refreshResourceInstallDialogControls();
			setMainControlsAvailable(true);
			semanticFocusVisible = focusManager.focusNode("resource-list");
			updateFocusPresentation();
			return;
		}
		const bool programReady =
#if defined(__ANDROID__)
			resourceInstallWorkerResult->programReady;
#else
			resourceInstallWorkerResult->programPackageResult.succeeded();
#endif
		if (completion.taskResult.succeeded() &&
			resourceInstallWorkerResult->programPreparation.succeeded() &&
			programReady)
		{
			resourceInstallDialogState =
				ResourceInstallDialogState::ReadyToRestart;
#if defined(__ANDROID__)
			pendingProgramPackagePath =
				resourceInstallWorkerResult->preparedProgramPath;
			resourceInstallDialogMessage =
				u8"Android 安装包已下载并校验完成。选择“打开安装确认”"
				u8"后由系统确认安装；当前资源和存档不会改变。";
			catalogStatusText =
				u8"Android 安装包已准备完成，可安装或继续当前游戏";
#else
			resourceInstallDialogMessage =
				u8"主程序已下载并校验完成。选择“安装并退出”后，"
				u8"独立更新助手会更新程序、引擎资源和公共资源；"
				u8"游戏资源与存档不会改变。";
			catalogStatusText = u8"主程序已准备完成，可安装或继续当前游戏";
#endif
		}
		else
		{
			resourceInstallDialogState = ResourceInstallDialogState::Failed;
			if (!resourceInstallWorkerResult->programPreparation.succeeded())
			{
				resourceInstallDialogMessage = programPreparationFailureText(
					resourceInstallWorkerResult->programPreparation);
			}
#if defined(__ANDROID__)
			else if (!resourceInstallWorkerResult->programFailureMessage.empty())
			{
				resourceInstallDialogMessage =
					resourceInstallWorkerResult->programFailureMessage;
			}
			else
			{
				resourceInstallDialogMessage =
					u8"Android 安装包未能准备完成，当前程序没有改变";
			}
#else
			else
			{
				resourceInstallDialogMessage = programPackageFailureText(
					resourceInstallWorkerResult->programPackageResult.status);
			}
#endif
		}
	}
	else if (completion.taskResult.status ==
		GameLoading::LoadingTaskStatus::Cancelled ||
		resourceInstallWorkerResult->preparation.status ==
			OnlineUpdate::ResourceDownloadPreparationStatus::Cancelled ||
		(pendingResourceInstall.includesCommon &&
			resourceInstallWorkerResult->commonPreparation.status ==
				OnlineUpdate::ResourceDownloadPreparationStatus::Cancelled))
	{
		catalogStatusText = u8"资源下载已取消，现有资源未改变";
		resourceInstallDialogState = ResourceInstallDialogState::Hidden;
		pendingResourceInstall = {};
		resourceInstallDialogMessage.clear();
		resourceInstallConfirmationPage = 0;
		refreshResourceInstallDialogControls();
		setMainControlsAvailable(true);
		semanticFocusVisible = focusManager.focusNode("resource-list");
		updateFocusPresentation();
		return;
	}
	else if (completion.taskResult.succeeded() &&
		resourceInstallWorkerResult->preparation.succeeded() &&
		(!pendingResourceInstall.includesCommon ||
			resourceInstallWorkerResult->commonPreparation.succeeded()) &&
		resourceInstallWorkerResult->transaction.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success)
	{
		std::string activationError;
		if (ResourceManager::instance().activateStagedResourceInstall(
			activationError))
		{
			buildResourceList();
			resourceInstallDialogState =
				ResourceInstallDialogState::Completed;
			resourceInstallDialogMessage =
				u8"游戏资源及运行所需文件已下载、解压并启用。"
				u8"资源列表已刷新，请关闭此提示后选择要进入的游戏。";
			catalogStatusText = u8"资源已更新，请选择要进入的游戏";
		}
		else
		{
			resourceInstallDialogState =
				ResourceInstallDialogState::Failed;
			resourceInstallDialogMessage = activationError.empty()
				? std::string(u8"资源已下载，但未能刷新资源列表")
				: activationError;
			catalogStatusText =
				u8"资源刷新失败，现有资源仍可继续使用";
		}
	}
	else
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		if (!resourceInstallWorkerResult->preparation.succeeded())
		{
			resourceInstallDialogMessage = resourcePreparationFailureText(
				resourceInstallWorkerResult->preparation);
		}
		else if (pendingResourceInstall.includesCommon &&
			!resourceInstallWorkerResult->commonPreparation.succeeded())
		{
			resourceInstallDialogMessage = commonPreparationFailureText(
				resourceInstallWorkerResult->commonPreparation);
		}
		else
		{
			resourceInstallDialogMessage = resourceTransactionFailureText(
				resourceInstallWorkerResult->transaction.status);
		}
	}
	refreshResourceInstallDialogControls();
	semanticFocusVisible = focusManager.focusNode(
		resourceInstallDialogState == ResourceInstallDialogState::ReadyToRestart
			? "install-secondary" : "install-primary");
	updateFocusPresentation();
}

void ResourceSelectScene::startPreparedProgramUpdate()
{
	const bool readyToStart =
#if defined(__APPLE__) && TARGET_OS_IOS
		resourceInstallDialogState == ResourceInstallDialogState::Confirming ||
		resourceInstallDialogState == ResourceInstallDialogState::ReadyToRestart;
#else
		resourceInstallDialogState == ResourceInstallDialogState::ReadyToRestart;
#endif
	if (!readyToStart ||
		resourceInstallOperation != ResourceInstallOperation::ProgramDownload ||
		resourceInstallRunner != nullptr)
	{
		return;
	}
#if defined(__ANDROID__)
	const std::string expectedDirectoryText =
		AndroidProgramUpdate::getApplicationUpdateDirectoryPath();
	std::filesystem::path expectedPath;
	std::filesystem::path preparedPath;
	try
	{
		expectedPath = (std::filesystem::u8path(expectedDirectoryText) /
			"jxqy-update.apk").lexically_normal();
		preparedPath = std::filesystem::u8path(
			pendingProgramPackagePath).lexically_normal();
	}
	catch (const std::exception&)
	{
		expectedPath.clear();
		preparedPath.clear();
	}
	if (expectedDirectoryText.empty() || !expectedPath.is_absolute() ||
		preparedPath != expectedPath || !isPlainRegularFile(preparedPath))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"已准备的 Android 安装包不可用，当前程序没有改变";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
	if (!AndroidProgramUpdate::requestPackageInstall(
			preparedPath.generic_u8string()))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"无法打开 Android 系统安装流程，当前程序没有改变";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
	GameLog::write(
		"ResourceSelectScene: Android PackageInstaller requested (apk=%s)\n",
		preparedPath.generic_u8string().c_str());
	catalogStatusText =
		u8"已打开 Android 系统安装流程，当前游戏仍可继续";
	dismissResourceInstallDialog();
#elif defined(__APPLE__)
	const OnlineUpdate::ProgramUpdateCheck update =
		OnlineUpdate::checkProgramUpdate(
			onlineApplicationCatalog,
			JxqyBuildVersion::ProgramUpdateTarget,
			JxqyBuildVersion::EngineVersion);
	std::string expectedInstallPageUrl;
	if (!canUseOnlineProgramPackage(update) ||
		onlineApplicationCatalogSources.catalogUrls.empty() ||
		!OnlineUpdate::buildHttpsArtifactUrl(
			onlineApplicationCatalogSources.catalogUrls.front(),
			update.package->artifactPath,
			expectedInstallPageUrl) ||
		expectedInstallPageUrl != pendingProgramPackagePath ||
		expectedInstallPageUrl != pendingResourceInstall.collectionRoot)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
#if TARGET_OS_IOS
			u8"iOS 主程序下载页面已改变，请重新检查更新";
#else
			u8"macOS Sparkle 更新目录已改变，请重新检查更新";
#endif
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
#if TARGET_OS_IOS
	if (!SDL_OpenURL(expectedInstallPageUrl.c_str()))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"无法打开 iOS 主程序下载页面，当前程序没有改变";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		GameLog::write(
			"ResourceSelectScene: failed to open iOS program download page: %s\n",
			SDL_GetError());
		return;
	}
	GameLog::write(
		"ResourceSelectScene: opened iOS program download page (%s)\n",
		expectedInstallPageUrl.c_str());
	catalogStatusText =
		u8"已打开 iOS 主程序下载页面，当前游戏仍可继续";
#else
	if (!MacProgramUpdate::requestUpdateCheck(expectedInstallPageUrl))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"无法启动 Sparkle 更新检查，当前程序没有改变";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
	GameLog::write(
		"ResourceSelectScene: requested Sparkle update check (%s)\n",
		expectedInstallPageUrl.c_str());
	catalogStatusText =
		u8"已打开 Sparkle 更新界面，当前游戏仍可继续";
#endif
	dismissResourceInstallDialog();
#else
	DesktopProgramUpdatePaths paths;
	std::error_code error;
	if (!resolveDesktopProgramUpdatePaths(paths) ||
		paths.releaseRoot.generic_u8string() !=
			pendingResourceInstall.collectionRoot ||
		!isPreparedDesktopProgramUpdate(paths) ||
		std::filesystem::exists(paths.previousPath, error) || error)
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"已准备的主程序或更新目录不可用，当前程序没有改变";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
	if (!startDesktopProgramUpdater(paths))
	{
		resourceInstallDialogState = ResourceInstallDialogState::Failed;
		resourceInstallDialogMessage =
			u8"无法启动独立更新助手，当前程序没有改变";
		refreshResourceInstallDialogControls();
		semanticFocusVisible = focusManager.focusNode("install-primary");
		updateFocusPresentation();
		return;
	}
	GameLog::write(
		"ResourceSelectScene: desktop program updater started"
		" (target=%s, release-root=%s)\n",
		paths.helperTarget.c_str(),
		paths.releaseRoot.generic_u8string().c_str());
	stop(erExit);
#endif
}

void ResourceSelectScene::cancelResourceInstall()
{
	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming &&
		pendingDownloadUsesMeteredNetwork && pendingMeteredDownloadConfirmed)
	{
		pendingMeteredDownloadConfirmed = false;
		refreshResourceInstallDialogControls();
		updateFocusPresentation();
		return;
	}
	if (resourceInstallDialogState == ResourceInstallDialogState::Downloading)
	{
		if (resourceInstallRunner != nullptr)
		{
			resourceInstallRunner->requestCancellation();
			resourceInstallDialogState =
				ResourceInstallDialogState::Cancelling;
			resourceInstallDialogMessage = u8"正在取消下载…";
			refreshResourceInstallDialogControls();
			updateFocusPresentation();
		}
		return;
	}
	if (resourceInstallDialogState == ResourceInstallDialogState::Cancelling)
	{
		return;
	}
	dismissResourceInstallDialog();
}

void ResourceSelectScene::dismissResourceInstallDialog()
{
	if (resourceInstallRunner != nullptr)
	{
		return;
	}
	resourceInstallDialogState = ResourceInstallDialogState::Hidden;
	resourceInstallOperation = ResourceInstallOperation::OnlineDownload;
	pendingResourceInstall = {};
	pendingProgramPackagePath.clear();
	resourceInstallDialogMessage.clear();
	resourceInstallConfirmationPage = 0;
	pendingDownloadUsesMeteredNetwork = false;
	pendingMeteredDownloadConfirmed = false;
	resourceUpdatePromptedByEntry = false;
	resourceInstallWorkerResult.reset();
	refreshResourceInstallDialogControls();
	setMainControlsAvailable(true);
	semanticFocusVisible = focusManager.focusNode("resource-list");
	updateFocusPresentation();
}

void ResourceSelectScene::refreshResourceInstallDialogControls()
{
	if (resourceInstallPrimaryButton == nullptr ||
		resourceInstallSecondaryButton == nullptr ||
		resourceInstallPreviousPageButton == nullptr ||
		resourceInstallNextPageButton == nullptr)
	{
		return;
	}
	const auto setButtonAvailability = [](
		const std::shared_ptr<FlatTextButton>& button,
		bool visible,
		bool activated)
	{
		if ((!visible || !activated) &&
			(button->visible || button->activated))
		{
			button->cancelPointerInteraction();
		}
		button->visible = visible;
		button->activated = activated;
	};
	bool primaryVisible = false;
	bool primaryActivated = false;
	bool secondaryVisible = false;
	bool secondaryActivated = false;
	bool previousVisible = false;
	bool previousActivated = false;
	bool nextVisible = false;
	bool nextActivated = false;
	if (resourceInstallDialogState == ResourceInstallDialogState::Hidden)
	{
		setButtonAvailability(
			resourceInstallPrimaryButton, false, false);
		setButtonAvailability(
			resourceInstallSecondaryButton, false, false);
		setButtonAvailability(
			resourceInstallPreviousPageButton, false, false);
		setButtonAvailability(
			resourceInstallNextPageButton, false, false);
		return;
	}
	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming)
	{
		if (resourceInstallOperation ==
			ResourceInstallOperation::ResourceRemoval)
		{
			resourceInstallPrimaryButton->setUTF8Str(u8"确认删除");
			resourceInstallPreviousPageButton->setUTF8Str(
				pendingResourceRemovalSavePolicy ==
						ResourceManager::ResourceRemovalSavePolicy::Delete
					? u8"● 删除存档" : u8"○ 删除存档");
			resourceInstallNextPageButton->setUTF8Str(
				pendingResourceRemovalSavePolicy ==
						ResourceManager::ResourceRemovalSavePolicy::Preserve
					? u8"● 保留存档" : u8"○ 保留存档");
			primaryActivated = pendingResourceRemovalSavePolicy !=
				ResourceManager::ResourceRemovalSavePolicy::Unselected;
			previousVisible = true;
			previousActivated = true;
			nextVisible = true;
			nextActivated = true;
		}
		else
		{
			if (pendingDownloadUsesMeteredNetwork &&
				pendingMeteredDownloadConfirmed)
			{
				resourceInstallPrimaryButton->setUTF8Str(u8"继续下载");
			}
			else if (resourceInstallOperation ==
				ResourceInstallOperation::ProgramDownload)
			{
#if defined(__APPLE__) && TARGET_OS_IOS
				resourceInstallPrimaryButton->setUTF8Str(u8"打开下载页面");
#else
				const OnlineUpdate::ProgramUpdateCheck update =
					OnlineUpdate::checkProgramUpdate(
						onlineApplicationCatalog,
						JxqyBuildVersion::ProgramUpdateTarget,
						JxqyBuildVersion::EngineVersion);
				resourceInstallPrimaryButton->setUTF8Str(
					update.hasUpdate() ? u8"下载并更新" : u8"确认重装");
#endif
			}
			else
			{
				resourceInstallPrimaryButton->setUTF8Str(
					resourceUpdatePromptedByEntry
						? u8"更新此游戏" : u8"开始下载");
			}
			resourceInstallPreviousPageButton->setUTF8Str(u8"上一页");
			resourceInstallNextPageButton->setUTF8Str(u8"下一页");
			primaryActivated = true;
			const int pageCount = std::max(1,
				(static_cast<int>(pendingResourceInstall.items.size()) +
					ResourceInstallItemsPerPage - 1) /
					ResourceInstallItemsPerPage);
			previousVisible = pageCount > 1;
			previousActivated =
				resourceInstallConfirmationPage > 0;
			nextVisible = pageCount > 1;
			nextActivated =
				resourceInstallConfirmationPage + 1 < pageCount;
		}
		resourceInstallSecondaryButton->setUTF8Str(
			pendingDownloadUsesMeteredNetwork &&
					pendingMeteredDownloadConfirmed
				? u8"返回"
				: resourceInstallOperation ==
						ResourceInstallOperation::ProgramDownload &&
					iosProgramUpdatePageAvailable()
					? u8"继续游戏"
				: resourceUpdatePromptedByEntry
					? u8"仍然进入" : u8"取消");
		primaryVisible = true;
		secondaryVisible = true;
		secondaryActivated = true;
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::BrowsingSaves)
	{
		resourceInstallPrimaryButton->setUTF8Str(u8"删除此存档");
		resourceInstallSecondaryButton->setUTF8Str(u8"关闭");
		resourceInstallPreviousPageButton->setUTF8Str(u8"上一个");
		resourceInstallNextPageButton->setUTF8Str(u8"下一个");
		primaryVisible = true;
		primaryActivated = !saveNamespaceEntries.empty();
		secondaryVisible = true;
		secondaryActivated = true;
		previousVisible = saveNamespaceEntries.size() > 1;
		previousActivated = selectedSaveNamespaceIndex > 0;
		nextVisible = saveNamespaceEntries.size() > 1;
		nextActivated = selectedSaveNamespaceIndex + 1 <
			static_cast<int>(saveNamespaceEntries.size());
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::ConfirmingSaveRemoval)
	{
		resourceInstallPrimaryButton->setUTF8Str(u8"确认删除");
		resourceInstallSecondaryButton->setUTF8Str(u8"返回");
		primaryVisible = true;
		primaryActivated = true;
		secondaryVisible = true;
		secondaryActivated = true;
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::Downloading)
	{
		resourceInstallSecondaryButton->setUTF8Str(u8"取消下载");
		secondaryVisible = true;
		secondaryActivated = true;
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::Cancelling)
	{
		resourceInstallSecondaryButton->setUTF8Str(u8"正在取消…");
		secondaryVisible = true;
	}
	else if (resourceInstallDialogState ==
		ResourceInstallDialogState::ReadyToRestart)
	{
		if (resourceInstallOperation ==
			ResourceInstallOperation::ProgramDownload)
		{
			if (androidProgramPackageInstallAvailable())
			{
				resourceInstallPrimaryButton->setUTF8Str(u8"打开安装确认");
			}
			else if (iosProgramUpdatePageAvailable())
			{
				resourceInstallPrimaryButton->setUTF8Str(u8"打开下载页面");
			}
			else if (macProgramUpdateAvailable())
			{
				resourceInstallPrimaryButton->setUTF8Str(u8"检查并安装");
			}
			else
			{
				resourceInstallPrimaryButton->setUTF8Str(u8"安装并退出");
			}
		}
		else
		{
			resourceInstallPrimaryButton->setUTF8Str(u8"退出游戏");
		}
		resourceInstallSecondaryButton->setUTF8Str(
			resourceInstallOperation ==
					ResourceInstallOperation::ProgramDownload
				? u8"继续游戏" : u8"稍后重启");
		primaryVisible = true;
		primaryActivated = true;
		secondaryVisible = true;
		secondaryActivated = true;
	}
	else
	{
		resourceInstallPrimaryButton->setUTF8Str(u8"关闭");
		primaryVisible = true;
		primaryActivated = true;
	}
	setButtonAvailability(resourceInstallPrimaryButton,
		primaryVisible, primaryActivated);
	setButtonAvailability(resourceInstallSecondaryButton,
		secondaryVisible, secondaryActivated);
	setButtonAvailability(resourceInstallPreviousPageButton,
		previousVisible, previousActivated);
	setButtonAvailability(resourceInstallNextPageButton,
		nextVisible, nextActivated);
	resourceInstallPrimaryButton->rect =
		getResourceInstallPrimaryButtonRect();
	resourceInstallSecondaryButton->rect =
		getResourceInstallSecondaryButtonRect();
	resourceInstallPreviousPageButton->rect =
		getResourceInstallPreviousPageButtonRect();
	resourceInstallNextPageButton->rect =
		getResourceInstallNextPageButtonRect();
}

void ResourceSelectScene::moveResourceInstallConfirmationPage(int offset)
{
	if (offset == 0)
	{
		return;
	}
	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming &&
		resourceInstallOperation == ResourceInstallOperation::ResourceRemoval)
	{
		pendingResourceRemovalSavePolicy = offset < 0
			? ResourceManager::ResourceRemovalSavePolicy::Delete
			: ResourceManager::ResourceRemovalSavePolicy::Preserve;
		refreshResourceInstallDialogControls();
		return;
	}
	if (resourceInstallDialogState ==
		ResourceInstallDialogState::BrowsingSaves)
	{
		if (!saveNamespaceEntries.empty())
		{
			selectedSaveNamespaceIndex = std::clamp(
				selectedSaveNamespaceIndex + offset,
				0,
				static_cast<int>(saveNamespaceEntries.size()) - 1);
			refreshResourceInstallDialogControls();
		}
		return;
	}
	if (resourceInstallDialogState != ResourceInstallDialogState::Confirming)
	{
		return;
	}
	const int pageCount = std::max(1,
		(static_cast<int>(pendingResourceInstall.items.size()) +
			ResourceInstallItemsPerPage - 1) /
			ResourceInstallItemsPerPage);
	resourceInstallConfirmationPage = std::clamp(
		resourceInstallConfirmationPage + offset, 0, pageCount - 1);
	refreshResourceInstallDialogControls();
}

bool ResourceSelectScene::onInitial()
{
	freeResource();

	int width = 0;
	int height = 0;
	engine->getWindowSize(width, height);
	rect.w = width;
	rect.h = height;
	updateLayout(width, height);

	loadSceneImages();
	createControls();
	buildResourceList();
	configureFocus();
	beginOnlineCatalogCheck();

	initTime();
	return true;
}

void ResourceSelectScene::configureFocus()
{
	focusManager.clear();
	if (resourceList == nullptr)
	{
		return;
	}
	focusManager.addNode(
		"resource-list",
		resourceList,
		{ "resource-selection", 0, 0 },
		[this]() { confirmSelection(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction) { return navigateResourceSelection(direction); });
	focusManager.addNode(
		"online-action",
		onlineActionButton,
		{ "resource-selection", 1, 0 },
		[this]() { beginResourceDownloadConfirmation(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left ||
				direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("resource-list");
			}
			if (direction == UIFocusDirection::Right)
			{
				return resourceRemoveButton != nullptr &&
						resourceRemoveButton->visible
					? focusManager.focusNode("resource-remove")
					: focusManager.focusNode("save-management");
			}
			if (direction == UIFocusDirection::Up)
			{
				return focusManager.focusNode("check-updates");
			}
			return false;
		});
	focusManager.addNode(
		"resource-remove",
		resourceRemoveButton,
		{ "resource-selection", 2, 0 },
		[this]() { beginResourceRemovalConfirmation(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return onlineActionButton != nullptr &&
						onlineActionButton->visible
					? focusManager.focusNode("online-action")
					: focusManager.focusNode("resource-list");
			}
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("save-management");
			}
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("resource-list");
			}
			if (direction == UIFocusDirection::Up)
			{
				return focusManager.focusNode("check-updates");
			}
			return false;
		});
	focusManager.addNode(
		"save-management",
		saveManagementButton,
		{ "resource-actions", 1, 0 },
		[this]() { beginSaveManagement(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("cheat-help");
			}
			if (direction == UIFocusDirection::Down)
			{
				return programActionButton != nullptr &&
						programActionButton->visible &&
						programActionButton->activated
					? focusManager.focusNode("program-action")
					: focusManager.focusNode("resource-list");
			}
			if (direction == UIFocusDirection::Right)
			{
				return displaySettingsButton != nullptr
					? focusManager.focusNode("display-settings")
					: focusManager.focusNode("check-updates");
			}
			return false;
		});
	focusManager.addNode(
		"cheat-help",
		cheatHelpButton,
		{ "resource-actions", 0, 0 },
		[this]() { showCheatHelp(true); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("save-management");
			}
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("resource-list");
			}
			return false;
		});
	focusManager.addNode(
		"display-settings",
		displaySettingsButton,
		{ "resource-actions", 2, 0 },
		[this]() { showDisplaySettings(true); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("save-management");
			}
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("check-updates");
			}
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("resource-list");
			}
			return false;
		});
	focusManager.addNode(
		"check-updates",
		checkUpdatesButton,
		{ "resource-actions", 3, 0 },
		[this]() { activateCheckUpdatesButton(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return displaySettingsButton != nullptr
					? focusManager.focusNode("display-settings")
					: focusManager.focusNode("save-management");
			}
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("exit");
			}
			if (direction == UIFocusDirection::Down)
			{
				return programActionButton != nullptr &&
						programActionButton->visible &&
						programActionButton->activated
					? focusManager.focusNode("program-action")
					: focusManager.focusNode("resource-list");
			}
			return false;
		});
	focusManager.addNode(
		"program-action",
		programActionButton,
		{ "program-action", 0, 0 },
		[this]() { activateProgramActionButton(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("resource-list");
			}
			if (direction == UIFocusDirection::Up ||
				direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("check-updates");
			}
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("save-management");
			}
			return false;
		});
	focusManager.addNode(
		"exit",
		exitButton,
		{ "resource-actions", 4, 0 },
		[this]() { stop(erExit); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("check-updates");
			}
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("resource-list");
			}
			return false;
		});
	for (int row = 0; row < DisplaySettingsRowCount; row++)
	{
		const int currentRow = row;
		focusManager.addNode(
			"display-settings-previous-" + std::to_string(row),
			displaySettingsPreviousButtons[row],
			{ "display-settings-rows", row, 0 },
			[this, currentRow]()
			{
				cycleDisplaySetting(currentRow, -1);
			});
		focusManager.addNode(
			"display-settings-next-" + std::to_string(row),
			displaySettingsNextButtons[row],
			{ "display-settings-rows", row, 1 },
			[this, currentRow]()
			{
				cycleDisplaySetting(currentRow, 1);
			});
	}
	focusManager.addNode(
		"display-settings-apply",
		displaySettingsApplyButton,
		{ "display-settings-actions", 0, 0 },
		[this]() { applyPendingDisplaySettings(); });
	focusManager.addNode(
		"display-settings-default",
		displaySettingsDefaultButton,
		{ "display-settings-actions", 0, 1 },
		[this]() { resetPendingDisplaySettings(); });
	focusManager.addNode(
		"display-settings-back",
		displaySettingsBackButton,
		{ "display-settings-actions", 0, 2 },
		[this]() { hideDisplaySettings(true); });
	focusManager.addNode(
		"cheat-help-close",
		cheatHelpCloseButton,
		{ "resource-cheat-help", 0, 0 },
		[this]() { hideCheatHelp(true); },
		[this]() { hideCheatHelp(true); },
		[](UIFocusDirection) { return true; });
	focusManager.addNode(
		"external-resource-confirm",
		externalResourceConfirmButton,
		{ "external-resource-dialog", 0, 0 },
		[this]() { confirmExternalResourceDialog(); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			return direction == UIFocusDirection::Right
				? focusManager.focusNode("external-resource-cancel") : true;
		});
	focusManager.addNode(
		"external-resource-cancel",
		externalResourceCancelButton,
		{ "external-resource-dialog", 1, 0 },
		[this]() { hideExternalResourceDialog(true); },
		[this]() { hideExternalResourceDialog(true); },
		[this](UIFocusDirection direction)
		{
			return direction == UIFocusDirection::Left
				? focusManager.focusNode("external-resource-confirm") : true;
		});
	focusManager.addNode(
		"install-primary",
		resourceInstallPrimaryButton,
		{ "resource-install-actions", 0, 0 },
		[this]()
		{
			activateResourceDialogPrimary();
		},
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("install-secondary");
			}
			if (direction == UIFocusDirection::Up)
			{
				return focusManager.focusNode("install-previous");
			}
			return false;
		});
	focusManager.addNode(
		"install-secondary",
		resourceInstallSecondaryButton,
		{ "resource-install-actions", 1, 0 },
		[this]() { activateResourceDialogSecondary(); },
		[this]() { activateResourceDialogSecondary(); },
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("install-primary");
			}
			if (direction == UIFocusDirection::Up)
			{
				return focusManager.focusNode("install-next");
			}
			return false;
		});
	focusManager.addNode(
		"install-previous",
		resourceInstallPreviousPageButton,
		{ "resource-install-pages", 0, 0 },
		[this]() { moveResourceInstallConfirmationPage(-1); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Right)
			{
				return focusManager.focusNode("install-next");
			}
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("install-primary");
			}
			return false;
		});
	focusManager.addNode(
		"install-next",
		resourceInstallNextPageButton,
		{ "resource-install-pages", 1, 0 },
		[this]() { moveResourceInstallConfirmationPage(1); },
		UIFocusManager::ActionHandler(),
		[this](UIFocusDirection direction)
		{
			if (direction == UIFocusDirection::Left)
			{
				return focusManager.focusNode("install-previous");
			}
			if (direction == UIFocusDirection::Down)
			{
				return focusManager.focusNode("install-secondary");
			}
			return false;
		});
	if (enableExternalButton != nullptr)
	{
		focusManager.addNode(
			"enable-external",
			enableExternalButton,
			{ "resource-actions", 1, 0 },
			[this]() { showExternalResourceDialog(true); },
			UIFocusManager::ActionHandler(),
			[this](UIFocusDirection direction)
			{
				if (direction == UIFocusDirection::Up)
				{
					return focusManager.focusNode("resource-list");
				}
				if (direction == UIFocusDirection::Down)
				{
					return focusManager.focusNode("external-link-0");
				}
				if (direction == UIFocusDirection::Right)
				{
					return focusManager.focusNode("check-updates");
				}
				return false;
			});
	}
	std::vector<UIFocusNodeBinding> externalLinkBindings;
	externalLinkBindings.reserve(externalLinkButtons.size());
	for (int linkIndex = 0;
		linkIndex < static_cast<int>(externalLinkButtons.size());
		linkIndex++)
	{
		externalLinkBindings.push_back(
		{
			"external-link-" + std::to_string(linkIndex),
			externalLinkButtons[linkIndex],
			[this, linkIndex]() { openExternalLink(linkIndex); },
			UIFocusManager::ActionHandler(),
			UIFocusManager::ActionHandler(),
			[this](UIFocusDirection direction)
			{
				if (direction == UIFocusDirection::Up)
				{
					// 移动端竖直顺序：resource-list → enable-external → external-links。
					// 桌面端：resource-list → external-links（无中间节点）。
					if (enableExternalButton != nullptr)
					{
						return focusManager.focusNode("enable-external");
					}
					return focusManager.focusNode("resource-list");
				}
				return false;
			}
		});
	}
	focusManager.addLinearGroup(
		"external-links",
		UIFocusLinearAxis::Horizontal,
		externalLinkBindings,
		false);
	focusManager.setDefaultFocus("resource-list");
	focusManager.setCancelHandler([this]() { stop(erExit); });
	focusManager.setPagePreviousHandler([this]()
	{
		moveSelection(resourceList != nullptr ? -resourceList->getVisibleItemCount() : -1);
	});
	focusManager.setPageNextHandler([this]()
	{
		moveSelection(resourceList != nullptr ? resourceList->getVisibleItemCount() : 1);
	});
	focusManager.setPanelPreviousHandler([this]()
	{
		moveSelection(resourceList != nullptr ? -resourceList->getVisibleItemCount() : -1);
	});
	focusManager.setPanelNextHandler([this]()
	{
		moveSelection(resourceList != nullptr ? resourceList->getVisibleItemCount() : 1);
	});
	semanticFocusVisible = focusManager.focusDefault();
	keyboardSemanticFocus = false;
	if (engine == nullptr || !engine->inputActions().hasActiveGamepad())
	{
		hideSemanticFocus();
	}
	else
	{
		updateFocusPresentation();
	}
}

bool ResourceSelectScene::navigateResourceSelection(UIFocusDirection direction)
{
	if (resourceList == nullptr || resourceEntries.empty())
	{
		return false;
	}

	const int selectedIndex = resourceList->getSelectedIndex();
	if (direction == UIFocusDirection::Up)
	{
		if (selectedIndex > 0)
		{
			moveSelection(-1);
			return true;
		}
		return programActionButton != nullptr &&
				programActionButton->visible &&
				programActionButton->activated
			? focusManager.focusNode("program-action")
			: focusManager.focusNode("check-updates");
	}
	if (direction == UIFocusDirection::Down)
	{
		if (selectedIndex + 1 < static_cast<int>(resourceEntries.size()))
		{
			moveSelection(1);
			return true;
		}
		if (enableExternalButton != nullptr)
		{
			return focusManager.focusNode("enable-external");
		}
		if (!externalLinkButtons.empty())
		{
			return focusManager.focusNode("external-link-0");
		}
		// 列表末尾无可达的下方控件时，保持末张卡片焦点；退出按钮仍可在
		// 首张卡片上方/列表右侧触达。
		return true;
	}
	if (direction == UIFocusDirection::Left)
	{
		return focusManager.focusNode("cheat-help");
	}
	if (direction == UIFocusDirection::Right)
	{
		if (onlineActionButton != nullptr &&
			onlineActionButton->visible && onlineActionButton->activated)
		{
			return focusManager.focusNode("online-action");
		}
		if (resourceRemoveButton != nullptr &&
			resourceRemoveButton->visible && resourceRemoveButton->activated)
		{
			return focusManager.focusNode("resource-remove");
		}
		if (saveManagementButton != nullptr &&
			saveManagementButton->visible && saveManagementButton->activated)
		{
			return focusManager.focusNode("save-management");
		}
		return focusManager.focusNode("check-updates");
	}
	return false;
}

void ResourceSelectScene::showCheatHelp(bool keepSemanticFocus)
{
	if (cheatHelpVisible || cheatHelpCloseButton == nullptr)
	{
		return;
	}
	const bool wasKeyboardSemanticFocus = keyboardSemanticFocus;
	cancelPointerInteraction();
	cheatHelpVisible = true;
	setMainControlsAvailable(false);
	cheatHelpCloseButton->visible = true;
	cheatHelpCloseButton->activated = true;
	cheatHelpCloseButton->rect = getCheatHelpCloseButtonRect();
	semanticFocusVisible = focusManager.focusNode("cheat-help-close");
	keyboardSemanticFocus = keepSemanticFocus
		? wasKeyboardSemanticFocus : false;
	if (!keepSemanticFocus)
	{
		hideSemanticFocus();
	}
	else
	{
		updateFocusPresentation();
	}
}

void ResourceSelectScene::hideCheatHelp(bool keepSemanticFocus)
{
	if (!cheatHelpVisible)
	{
		return;
	}
	const bool wasKeyboardSemanticFocus = keyboardSemanticFocus;
	cancelPointerInteraction();
	cheatHelpVisible = false;
	cheatHelpCloseButton->visible = false;
	cheatHelpCloseButton->activated = false;
	setMainControlsAvailable(true);
	semanticFocusVisible = focusManager.focusNode("cheat-help");
	keyboardSemanticFocus = keepSemanticFocus
		? wasKeyboardSemanticFocus : false;
	if (!keepSemanticFocus)
	{
		hideSemanticFocus();
	}
	else
	{
		updateFocusPresentation();
	}
}

void ResourceSelectScene::setMainControlsAvailable(bool available)
{
	if (resourceList != nullptr)
	{
		resourceList->visible = available;
		resourceList->activated = available;
	}
	if (exitButton != nullptr)
	{
		exitButton->visible = available;
		exitButton->activated = available;
	}
	if (cheatHelpButton != nullptr)
	{
		cheatHelpButton->visible = available;
		cheatHelpButton->activated = available;
	}
	if (checkUpdatesButton != nullptr)
	{
		checkUpdatesButton->visible = available;
		if (available)
		{
			refreshCheckUpdatesButton();
		}
		else
		{
			checkUpdatesButton->activated = false;
			checkUpdatesButton->cancelPointerInteraction();
		}
	}
	if (displaySettingsButton != nullptr)
	{
		displaySettingsButton->visible = available;
		displaySettingsButton->activated = available;
		if (!available)
		{
			displaySettingsButton->cancelPointerInteraction();
		}
	}
	if (programActionButton != nullptr)
	{
		if (available)
		{
			refreshProgramActionButton();
		}
		else
		{
			programActionButton->visible = false;
			programActionButton->activated = false;
			programActionButton->cancelPointerInteraction();
		}
	}
	if (onlineActionButton != nullptr)
	{
		if (available)
		{
			refreshOnlineActionButton();
		}
		else
		{
			onlineActionButton->visible = false;
			onlineActionButton->activated = false;
			onlineActionButton->cancelPointerInteraction();
		}
	}
	if (resourceRemoveButton != nullptr)
	{
		if (available)
		{
			refreshResourceManagementButtons();
		}
		else
		{
			resourceRemoveButton->visible = false;
			resourceRemoveButton->activated = false;
			resourceRemoveButton->cancelPointerInteraction();
		}
	}
	if (saveManagementButton != nullptr)
	{
		if (available)
		{
			refreshResourceManagementButtons();
		}
		else
		{
			saveManagementButton->visible = false;
			saveManagementButton->activated = false;
			saveManagementButton->cancelPointerInteraction();
		}
	}
	if (enableExternalButton != nullptr)
	{
		enableExternalButton->visible = available;
		enableExternalButton->activated = available;
	}
	for (const auto& button : externalLinkButtons)
	{
		if (button != nullptr)
		{
			button->visible = available;
			button->activated = available;
			if (!button->visible)
			{
				button->cancelPointerInteraction();
			}
		}
	}
}

void ResourceSelectScene::moveSelection(int offset)
{
	if (resourceList == nullptr || resourceEntries.empty() || offset == 0)
	{
		return;
	}
	const int packCount = static_cast<int>(resourceEntries.size());
	int selectedIndex = resourceList->getSelectedIndex();
	selectedIndex = (selectedIndex + offset) % packCount;
	if (selectedIndex < 0)
	{
		selectedIndex += packCount;
	}
	resourceList->setSelectedIndex(selectedIndex);
	resourceList->ensureSelectedVisible();
}

void ResourceSelectScene::confirmSelection()
{
	if (resourceList == nullptr || resourceEntries.empty())
	{
		// 空列表或无资源包时不做任何动作；退出只能通过退出按钮/窗口关闭。
		return;
	}
	const int selectedIndex = resourceList->getSelectedIndex();
	if (selectedIndex < 0 ||
		selectedIndex >= static_cast<int>(resourceEntries.size()))
	{
		return;
	}
	const ResourceSelectionEntry& entry = resourceEntries[selectedIndex];
	if (entry.configurationError)
	{
		catalogStatusText = u8"资源配置错误，不能进入：" + entry.title;
		return;
	}
	if (entry.isOnlineOnly())
	{
		beginResourceDownloadConfirmation();
		return;
	}
	if (entry.onlineAvailable && beginResourceDownloadConfirmation(true))
	{
		return;
	}
	enterSelectedResource(selectedIndex);
}

void ResourceSelectScene::enterSelectedResource(int selectedIndex)
{
	if (selectedIndex < 0 ||
		selectedIndex >= static_cast<int>(resourceEntries.size()))
	{
		return;
	}
	const ResourceSelectionEntry& entry = resourceEntries[selectedIndex];
	if (entry.configurationError || entry.localPackIndex < 0)
	{
		return;
	}
	if (!ResourceManager::instance().setActiveResourcePack(
			entry.localPackIndex))
	{
		GameLog::write(
			"ResourceSelectScene: failed to install routing"
			" for local index %d (entry=%d)\n",
			entry.localPackIndex,
			selectedIndex);
		return;
	}
	ResourceManager::instance().rememberResourcePackSelection(
		entry.localPackIndex);
	GameLog::write(
		"ResourceSelectScene: user selected local index %d (entry=%d)\n",
		entry.localPackIndex,
		selectedIndex);
	logicRunning = false;
}

void ResourceSelectScene::hideSemanticFocus()
{
	if (semanticFocusVisible)
	{
		focusManager.suspendFocus();
	}
	semanticFocusVisible = false;
	keyboardSemanticFocus = false;
	updateFocusPresentation();
}

void ResourceSelectScene::showExternalResourceDialog(bool keepSemanticFocus)
{
	if (externalResourceDialogVisible || enableExternalButton == nullptr ||
		externalResourceConfirmButton == nullptr ||
		externalResourceCancelButton == nullptr)
	{
		return;
	}
	const bool wasKeyboardSemanticFocus = keyboardSemanticFocus;
	cancelPointerInteraction();
	externalResourceDialogVisible = true;
	setMainControlsAvailable(false);
	externalResourceConfirmButton->setUTF8Str(
		externalResourcePresentationState ==
				ExternalResourcePresentationState::Enabled
			? u8"确认关闭" : u8"确认开启");
	externalResourceConfirmButton->visible = true;
	externalResourceConfirmButton->activated = true;
	externalResourceCancelButton->visible = true;
	externalResourceCancelButton->activated = true;
	semanticFocusVisible = focusManager.focusNode("external-resource-cancel");
	keyboardSemanticFocus = keepSemanticFocus && wasKeyboardSemanticFocus;
	updateFocusPresentation();
}

void ResourceSelectScene::hideExternalResourceDialog(bool keepSemanticFocus)
{
	if (!externalResourceDialogVisible)
	{
		return;
	}
	const bool wasKeyboardSemanticFocus = keyboardSemanticFocus;
	externalResourceDialogVisible = false;
	externalResourceConfirmButton->visible = false;
	externalResourceConfirmButton->activated = false;
	externalResourceConfirmButton->cancelPointerInteraction();
	externalResourceCancelButton->visible = false;
	externalResourceCancelButton->activated = false;
	externalResourceCancelButton->cancelPointerInteraction();
	setMainControlsAvailable(true);
	semanticFocusVisible = focusManager.focusNode("enable-external");
	keyboardSemanticFocus = keepSemanticFocus && wasKeyboardSemanticFocus;
	updateFocusPresentation();
}

void ResourceSelectScene::confirmExternalResourceDialog()
{
	if (!externalResourceDialogVisible)
	{
		return;
	}
	hideExternalResourceDialog(true);
	toggleExternalResources();
}

void ResourceSelectScene::showDisplaySettings(bool keepSemanticFocus)
{
	if (displaySettingsVisible || displaySettingsButton == nullptr ||
		engine == nullptr)
	{
		return;
	}
	const bool wasKeyboardSemanticFocus = keyboardSemanticFocus;
	desktopDisplays = engine->getDesktopDisplays();
	if (desktopDisplays.empty())
	{
		catalogStatusText = u8"未能读取桌面显示器信息";
		return;
	}
	cancelPointerInteraction();
	pendingDisplaySettings = engine->getDesktopDisplaySettings();
	if (pendingDisplaySettings.fullScreenSolutionMode ==
		FullScreenSolutionMode::original)
	{
		pendingDisplaySettings.fullScreenSolutionMode =
			FullScreenSolutionMode::adjust;
	}
	displaySettingsStatusText.clear();
	displaySettingsVisible = true;
	setMainControlsAvailable(false);
	refreshDisplaySettingsOptions();
	setDisplaySettingsControlsVisible(true);
	updateDisplaySettingsControlLayout();
	semanticFocusVisible = focusManager.focusNode(
		desktopDisplays.size() > 1
			? "display-settings-next-0"
			: "display-settings-next-1");
	keyboardSemanticFocus = keepSemanticFocus && wasKeyboardSemanticFocus;
	if (!keepSemanticFocus)
	{
		hideSemanticFocus();
	}
	else
	{
		updateFocusPresentation();
	}
}

void ResourceSelectScene::hideDisplaySettings(bool keepSemanticFocus)
{
	if (!displaySettingsVisible)
	{
		return;
	}
	const bool wasKeyboardSemanticFocus = keyboardSemanticFocus;
	displaySettingsVisible = false;
	setDisplaySettingsControlsVisible(false);
	setMainControlsAvailable(true);
	semanticFocusVisible = focusManager.focusNode("display-settings");
	keyboardSemanticFocus = keepSemanticFocus && wasKeyboardSemanticFocus;
	if (!keepSemanticFocus)
	{
		hideSemanticFocus();
	}
	else
	{
		updateFocusPresentation();
	}
}

void ResourceSelectScene::refreshDisplaySettingsOptions()
{
	if (desktopDisplays.empty())
	{
		return;
	}
	auto displayIterator = std::find_if(
		desktopDisplays.begin(), desktopDisplays.end(),
		[this](const DesktopDisplayInfo& info)
		{
			return info.index == pendingDisplaySettings.displayIndex;
		});
	if (displayIterator == desktopDisplays.end())
	{
		pendingDisplaySettings.displayIndex = desktopDisplays.front().index;
	}
	rebuildDisplayResolutionOptions();
	setDisplaySettingsControlsVisible(displaySettingsVisible);
}

void ResourceSelectScene::rebuildDisplayResolutionOptions()
{
	displayResolutionOptions.clear();
	if (desktopDisplays.empty())
	{
		return;
	}
	const auto displayIterator = std::find_if(
		desktopDisplays.begin(), desktopDisplays.end(),
		[this](const DesktopDisplayInfo& info)
		{
			return info.index == pendingDisplaySettings.displayIndex;
		});
	const DesktopDisplayInfo& display = displayIterator != desktopDisplays.end()
		? *displayIterator : desktopDisplays.front();

	if (pendingDisplaySettings.fullScreenMode ==
		FullScreenMode::windowFullScreen)
	{
		for (const DesktopDisplayResolution& preset : WindowResolutionPresets)
		{
			if ((display.desktopWidth <= 0 ||
				preset.width <= display.desktopWidth) &&
				(display.desktopHeight <= 0 ||
				preset.height <= display.desktopHeight))
			{
				displayResolutionOptions.push_back(preset);
			}
		}
		displayResolutionOptions.push_back(
			{
				LogicalResolutionPolicy::constrainWidth(
					pendingDisplaySettings.width),
				LogicalResolutionPolicy::constrainHeight(
					pendingDisplaySettings.height)
			});
	}
	else if (pendingDisplaySettings.fullScreenMode ==
		FullScreenMode::fullScreen)
	{
		displayResolutionOptions = display.fullscreenResolutions;
		if (displayResolutionOptions.empty())
		{
			pendingDisplaySettings.fullScreenMode =
				FullScreenMode::windowFullScreen;
			rebuildDisplayResolutionOptions();
			return;
		}
	}
	else
	{
		const int maximumWidth = display.usableWidth > 0
			? display.usableWidth : display.desktopWidth;
		const int maximumHeight = display.usableHeight > 0
			? display.usableHeight : display.desktopHeight;
		for (const DesktopDisplayResolution& preset : WindowResolutionPresets)
		{
			if ((maximumWidth <= 0 || preset.width <= maximumWidth) &&
				(maximumHeight <= 0 || preset.height <= maximumHeight))
			{
				displayResolutionOptions.push_back(preset);
			}
		}
		DesktopDisplayResolution current =
		{
			LogicalResolutionPolicy::constrainWidth(
				pendingDisplaySettings.width),
			LogicalResolutionPolicy::constrainHeight(
				pendingDisplaySettings.height)
		};
		displayResolutionOptions.push_back(current);
	}

	std::sort(displayResolutionOptions.begin(),
		displayResolutionOptions.end(),
		[](const DesktopDisplayResolution& left,
			const DesktopDisplayResolution& right)
		{
			return left.width != right.width
				? left.width < right.width : left.height < right.height;
		});
	displayResolutionOptions.erase(
		std::unique(displayResolutionOptions.begin(),
			displayResolutionOptions.end()),
		displayResolutionOptions.end());
	if (displayResolutionOptions.empty())
	{
		return;
	}

	const auto exact = std::find(
		displayResolutionOptions.begin(), displayResolutionOptions.end(),
		DesktopDisplayResolution
		{
			pendingDisplaySettings.width,
			pendingDisplaySettings.height
		});
	if (exact == displayResolutionOptions.end())
	{
		const auto closest = std::min_element(
			displayResolutionOptions.begin(), displayResolutionOptions.end(),
			[this](const DesktopDisplayResolution& left,
				const DesktopDisplayResolution& right)
			{
				const long long leftDistance =
					std::llabs(static_cast<long long>(left.width) -
						pendingDisplaySettings.width) +
					std::llabs(static_cast<long long>(left.height) -
						pendingDisplaySettings.height);
				const long long rightDistance =
					std::llabs(static_cast<long long>(right.width) -
						pendingDisplaySettings.width) +
					std::llabs(static_cast<long long>(right.height) -
						pendingDisplaySettings.height);
				return leftDistance < rightDistance;
			});
		pendingDisplaySettings.width = closest->width;
		pendingDisplaySettings.height = closest->height;
	}
}

void ResourceSelectScene::cycleDisplaySetting(int row, int offset)
{
	if (!displaySettingsVisible || desktopDisplays.empty() || offset == 0)
	{
		return;
	}
	if (row == 0)
	{
		auto iterator = std::find_if(
			desktopDisplays.begin(), desktopDisplays.end(),
			[this](const DesktopDisplayInfo& info)
			{
				return info.index == pendingDisplaySettings.displayIndex;
			});
		int position = iterator == desktopDisplays.end()
			? 0 : static_cast<int>(iterator - desktopDisplays.begin());
		position = (position + offset) % static_cast<int>(desktopDisplays.size());
		if (position < 0)
		{
			position += static_cast<int>(desktopDisplays.size());
		}
		pendingDisplaySettings.displayIndex = desktopDisplays[position].index;
		rebuildDisplayResolutionOptions();
	}
	else if (row == 1)
	{
		int mode = static_cast<int>(pendingDisplaySettings.fullScreenMode);
		for (int attempt = 0; attempt < 3; attempt++)
		{
			mode = (mode + offset) % 3;
			if (mode < 0)
			{
				mode += 3;
			}
			if (mode != static_cast<int>(FullScreenMode::fullScreen))
			{
				break;
			}
			const auto displayIterator = std::find_if(
				desktopDisplays.begin(), desktopDisplays.end(),
				[this](const DesktopDisplayInfo& info)
				{
					return info.index == pendingDisplaySettings.displayIndex;
				});
			if (displayIterator != desktopDisplays.end() &&
				!displayIterator->fullscreenResolutions.empty())
			{
				break;
			}
		}
		pendingDisplaySettings.fullScreenMode =
			static_cast<FullScreenMode>(mode);
		if (pendingDisplaySettings.fullScreenMode != FullScreenMode::window &&
			pendingDisplaySettings.fullScreenSolutionMode ==
				FullScreenSolutionMode::original)
		{
			pendingDisplaySettings.fullScreenSolutionMode =
				FullScreenSolutionMode::adjust;
		}
		rebuildDisplayResolutionOptions();
	}
	else if (row == 2 && !displayResolutionOptions.empty())
	{
		const DesktopDisplayResolution current =
		{
			pendingDisplaySettings.width,
			pendingDisplaySettings.height
		};
		auto iterator = std::find(
			displayResolutionOptions.begin(), displayResolutionOptions.end(),
			current);
		int position = iterator == displayResolutionOptions.end()
			? 0 : static_cast<int>(iterator - displayResolutionOptions.begin());
		position = (position + offset) %
			static_cast<int>(displayResolutionOptions.size());
		if (position < 0)
		{
			position += static_cast<int>(displayResolutionOptions.size());
		}
		pendingDisplaySettings.width =
			displayResolutionOptions[position].width;
		pendingDisplaySettings.height =
			displayResolutionOptions[position].height;
	}
	else if (row == 3)
	{
		pendingDisplaySettings.fullScreenSolutionMode =
			pendingDisplaySettings.fullScreenSolutionMode ==
				FullScreenSolutionMode::forceToUseSetting
			? FullScreenSolutionMode::adjust
			: FullScreenSolutionMode::forceToUseSetting;
	}
	displaySettingsStatusText.clear();
	setDisplaySettingsControlsVisible(true);
}

void ResourceSelectScene::applyPendingDisplaySettings()
{
	if (!displaySettingsVisible || engine == nullptr)
	{
		return;
	}
	if (!engine->applyDesktopDisplaySettings(pendingDisplaySettings))
	{
		displaySettingsStatusText = u8"应用失败，已恢复原来的显示设置";
		return;
	}
	pendingDisplaySettings = engine->getDesktopDisplaySettings();
	Config::setDesktopDisplaySettings(pendingDisplaySettings);
	Config::save();
	desktopDisplays = engine->getDesktopDisplays();
	refreshDisplaySettingsOptions();
	displaySettingsStatusText = u8"显示设置已应用并保存";
}

void ResourceSelectScene::resetPendingDisplaySettings()
{
	if (!displaySettingsVisible || desktopDisplays.empty())
	{
		return;
	}
	pendingDisplaySettings = {};
	pendingDisplaySettings.displayIndex = desktopDisplays.front().index;
	pendingDisplaySettings.width = DEFAULT_WINDOW_WIDTH;
	pendingDisplaySettings.height = DEFAULT_WINDOW_HEIGHT;
	pendingDisplaySettings.fullScreenMode = FullScreenMode::window;
	pendingDisplaySettings.fullScreenSolutionMode =
		FullScreenSolutionMode::adjust;
	rebuildDisplayResolutionOptions();
	displaySettingsStatusText = u8"已恢复默认选项，点击“应用”后生效";
	setDisplaySettingsControlsVisible(true);
}

void ResourceSelectScene::setDisplaySettingsControlsVisible(bool visible)
{
	for (int row = 0; row < DisplaySettingsRowCount; row++)
	{
		const bool rowAvailable = visible &&
			(row != 0 || desktopDisplays.size() > 1) &&
			(row != 2 || displayResolutionOptions.size() > 1);
		if (displaySettingsPreviousButtons[row] != nullptr)
		{
			displaySettingsPreviousButtons[row]->visible = visible;
			displaySettingsPreviousButtons[row]->activated = rowAvailable;
			if (!rowAvailable)
			{
				displaySettingsPreviousButtons[row]->cancelPointerInteraction();
			}
		}
		if (displaySettingsNextButtons[row] != nullptr)
		{
			displaySettingsNextButtons[row]->visible = visible;
			displaySettingsNextButtons[row]->activated = rowAvailable;
			if (!rowAvailable)
			{
				displaySettingsNextButtons[row]->cancelPointerInteraction();
			}
		}
	}
	for (const auto& button :
		{ displaySettingsApplyButton, displaySettingsDefaultButton,
			displaySettingsBackButton })
	{
		if (button != nullptr)
		{
			button->visible = visible;
			button->activated = visible;
			if (!visible)
			{
				button->cancelPointerInteraction();
			}
		}
	}
}

bool ResourceSelectScene::restoreSemanticFocus()
{
	if (semanticFocusVisible)
	{
		return true;
	}
	semanticFocusVisible = focusManager.restoreFocus();
	updateFocusPresentation();
	return semanticFocusVisible;
}

void ResourceSelectScene::updateFocusPresentation()
{
	if (resourceList != nullptr)
	{
		// The selected resource candidate is persistent scene state, not an
		// input-focus decoration. Pointer takeover may suspend keyboard or
		// gamepad focus, but it must not hide the selected card frame.
		resourceList->setSelectionIndicatorVisible(true);
	}
}

void ResourceSelectScene::synchronizeSemanticFocusWithInput()
{
	if (semanticFocusVisible && !keyboardSemanticFocus
		&& (engine == nullptr || !engine->inputActions().hasActiveGamepad()))
	{
		hideSemanticFocus();
	}
}

void ResourceSelectScene::updateLayout(int width, int height)
{
	const int horizontalMargin = width <= 720
		? 24
		: std::min(80, 24 + (width - 720) / 2);
	panelWidth = clampInt(width - horizontalMargin * 2, 360, 780);
	if (panelWidth > width - 24)
	{
		panelWidth = std::max(320, width - 24);
	}

	panelPadding = width < 560 ? 20 : 32;
	contentWidth = panelWidth - panelPadding * 2;
	if (contentWidth < 280)
	{
		contentWidth = 280;
	}

	const std::size_t visibleResourceCount = resourceEntries.empty()
		? ResourceManager::instance().getDiscoveredPacks().size()
		: resourceEntries.size();
	const int packCount = std::max(
		1, static_cast<int>(visibleResourceCount));
	compactVerticalLayout = height < 520;
	compactMobileLayout = compactVerticalLayout &&
		mobileResourceSelectUiEnabled();
	const int resourcePackItemHeight = compactMobileLayout
		? CompactMobileResourcePackItemHeight : ResourcePackItemHeight;
	const int desiredListHeight = packCount * resourcePackItemHeight;
	wideDetailLayout = panelWidth >= 620 && contentWidth >= 556;
	minimumListHeight = compactMobileLayout
		? CompactMobileResourcePackItemHeight
		: compactVerticalLayout ? 64 : MinimumListHeight;
	narrowDetailHeight = compactVerticalLayout ? 64 : NarrowDetailHeight;
	detailGap = compactVerticalLayout ? 8 : DetailGap;
	if (compactMobileLayout && !wideDetailLayout && height < 400)
	{
		// 极矮的窄屏仍保留一张完整资源卡；详情区只显示两行摘要。
		narrowDetailHeight = 48;
		detailGap = 5;
	}
	const int desiredContentHeight = wideDetailLayout
		? std::max(desiredListHeight, 220)
		: desiredListHeight + narrowDetailHeight + detailGap;
	const int minimumContentHeight = wideDetailLayout
		? minimumListHeight
		: minimumListHeight + narrowDetailHeight + detailGap;
	const int reservedFooterHeight = compactMobileLayout
		? CompactMobilePanelListTopOffset +
			(externalResourceToggleAvailable()
				? CompactMobileExternalFooterHeight
				: CompactMobileCreditsFooterHeight)
		: footerReservedHeight() -
			(compactVerticalLayout && externalResourceToggleAvailable()
				? 12
				: 0);
	const int desiredPanelHeight =
		desiredContentHeight + reservedFooterHeight;
	// Landscape mobile windows commonly provide only 480 px. Keep the 3 px
	// panel frame visible while giving the list the remaining height; compact
	// mobile cards then expose several games without reducing their text size.
	const int verticalMargin = height < 520 ? 3 : 48;
	const int maximumPanelHeight = std::max(1, height - verticalMargin * 2);
	const int minimumPanelHeight = std::min(
		minimumContentHeight + reservedFooterHeight,
		maximumPanelHeight);
	panelHeight = std::clamp(desiredPanelHeight, minimumPanelHeight, maximumPanelHeight);
	const int availableContentHeight =
		std::max(1, panelHeight - reservedFooterHeight);
	listHeight = wideDetailLayout
		? availableContentHeight
		: std::max(1,
			availableContentHeight - narrowDetailHeight - detailGap);

	panelX = (width - panelWidth) / 2;
	panelY = std::max(verticalMargin, (height - panelHeight) / 2);
	startX = panelX + panelPadding;
	startY = panelY + (compactMobileLayout
		? CompactMobilePanelListTopOffset : PanelListTopOffset);
	updateControlLayout();
}

void ResourceSelectScene::updateControlLayout()
{
	const bool compactHeader = panelWidth < HeaderActionCompactPanelWidth;
	const int headerFontSize = compactHeader
		? HeaderActionCompactFontSize : HeaderActionFontSize;
	if (exitButton != nullptr)
	{
		exitButton->setFontSize(headerFontSize);
		exitButton->setUTF8Str(u8"退出");
	}
	if (cheatHelpButton != nullptr)
	{
		cheatHelpButton->setFontSize(headerFontSize);
		cheatHelpButton->setUTF8Str(
			compactHeader ? u8"作弊" : u8"作弊说明");
	}
	if (saveManagementButton != nullptr)
	{
		saveManagementButton->setFontSize(headerFontSize);
		saveManagementButton->setUTF8Str(
			compactHeader ? u8"存档" : u8"存档管理");
	}
	if (displaySettingsButton != nullptr)
	{
		displaySettingsButton->setFontSize(headerFontSize);
		displaySettingsButton->setUTF8Str(
			compactHeader ? u8"显示" : u8"显示设置");
	}
	refreshCheckUpdatesButton();

	if (wideDetailLayout)
	{
		const int listColumnWidth = clampInt(
			contentWidth * 45 / 100, 260, 330);
		resourceListArea =
			{ startX, startY, listColumnWidth, listHeight };
		detailArea =
		{
			startX + listColumnWidth + detailGap,
			startY,
			std::max(1, contentWidth - listColumnWidth - detailGap),
			listHeight
		};
	}
	else
	{
		detailArea =
			{ startX, startY, contentWidth, narrowDetailHeight };
		resourceListArea =
		{
			startX,
			startY + narrowDetailHeight + detailGap,
			contentWidth,
			listHeight
		};
	}

	const int detailPadding = wideDetailLayout ? 12 : 8;
	const int maximumCoverSize = wideDetailLayout ? 132 : 80;
	const int coverSize = std::max(1, std::min(
		{
			maximumCoverSize,
			std::max(1, detailArea.w - detailPadding * 2),
			std::max(1, detailArea.h - detailPadding * 2)
		}));
	coverArea =
	{
		detailArea.x + detailPadding,
		detailArea.y + detailPadding,
		coverSize,
		coverSize
	};

	if (resourceList != nullptr)
	{
		resourceList->setItemMetrics(
			compactMobileLayout
				? CompactMobileResourcePackItemHeight : ResourcePackItemHeight,
			compactMobileLayout ? CompactMobileItemGap : 10);
		resourceList->setLayout(
			resourceListArea,
			getScrollbarRect());
		resourceList->ensureSelectedVisible();
	}
	if (exitButton != nullptr)
	{
		exitButton->rect = getExitButtonRect();
	}
	if (cheatHelpButton != nullptr)
	{
		cheatHelpButton->rect = getCheatHelpButtonRect();
	}
	if (checkUpdatesButton != nullptr)
	{
		checkUpdatesButton->rect = getCheckUpdatesButtonRect();
	}
	if (programActionButton != nullptr)
	{
		programActionButton->rect = getProgramActionButtonRect();
	}
	if (onlineActionButton != nullptr)
	{
		onlineActionButton->rect = getOnlineActionButtonRect();
	}
	if (resourceRemoveButton != nullptr)
	{
		resourceRemoveButton->rect = getResourceRemoveButtonRect();
	}
	if (saveManagementButton != nullptr)
	{
		saveManagementButton->rect = getSaveManagementButtonRect();
	}
	if (displaySettingsButton != nullptr)
	{
		displaySettingsButton->rect = getDisplaySettingsButtonRect();
	}
	updateDisplaySettingsControlLayout();
	if (cheatHelpCloseButton != nullptr)
	{
		cheatHelpCloseButton->rect = getCheatHelpCloseButtonRect();
	}
	if (externalResourceConfirmButton != nullptr)
	{
		externalResourceConfirmButton->rect =
			getExternalResourceConfirmButtonRect();
	}
	if (externalResourceCancelButton != nullptr)
	{
		externalResourceCancelButton->rect =
			getExternalResourceCancelButtonRect();
	}
	if (resourceInstallDialogState != ResourceInstallDialogState::Hidden)
	{
		refreshResourceInstallDialogControls();
	}
	if (enableExternalButton != nullptr)
	{
		enableExternalButton->rect = getEnableExternalButtonRect();
	}
	for (int linkIndex = 0; linkIndex < static_cast<int>(externalLinkButtons.size()); linkIndex++)
	{
		const auto& button = externalLinkButtons[linkIndex];
		const bool compactLinkLabel = panelWidth < 460;
		button->setFontSize(compactLinkLabel ? 14 : 18);
		button->setUTF8Str(compactLinkLabel
			? ExternalLinks[linkIndex].compactLabel
			: ExternalLinks[linkIndex].label);
		button->rect = getExternalLinkRect(linkIndex);
		const bool mainControlsVisible = resourceList != nullptr &&
			resourceList->visible;
		button->visible = mainControlsVisible;
		button->activated = mainControlsVisible;
		if (!button->visible)
		{
			button->cancelPointerInteraction();
		}
	}
}

Rect ResourceSelectScene::getExitButtonRect() const
{
	int buttonWidth = HeaderActionButtonWidth;
	if (panelWidth < HeaderActionCompactPanelWidth)
	{
		buttonWidth = HeaderActionCompactButtonWidth;
		constexpr int buttonCount = 5;
		const int gap = 6;
		const int availableWidth = std::max(
			1, panelWidth - panelPadding * 2 - gap * (buttonCount - 1));
		buttonWidth = std::max(
			1, std::min(buttonWidth, availableWidth / buttonCount));
	}
	return
	{
		panelX + panelWidth - panelPadding - buttonWidth,
		panelY + (compactMobileLayout
			? CompactMobileHeaderActionTopOffset : HeaderActionTopOffset),
		buttonWidth,
		ActionButtonHeight
	};
}

Rect ResourceSelectScene::getCheatHelpButtonRect() const
{
	Rect buttonRect = getExitButtonRect();
	buttonRect.x = panelX + panelPadding;
	return buttonRect;
}

Rect ResourceSelectScene::getCheckUpdatesButtonRect() const
{
	Rect buttonRect = getExitButtonRect();
	const int gap = panelWidth < HeaderActionCompactPanelWidth ? 6 : 10;
	const int checkButtonWidth = panelWidth < HeaderActionCompactPanelWidth
		? buttonRect.w : HeaderCheckUpdatesButtonWidth;
	buttonRect.x -= checkButtonWidth + gap;
	buttonRect.w = checkButtonWidth;
	return buttonRect;
}

Rect ResourceSelectScene::getDisplaySettingsButtonRect() const
{
	const Rect checkButtonRect = getCheckUpdatesButtonRect();
	Rect buttonRect = getExitButtonRect();
	const int gap = panelWidth < HeaderActionCompactPanelWidth ? 6 : 10;
	buttonRect.x = checkButtonRect.x - buttonRect.w - gap;
	return buttonRect;
}

Rect ResourceSelectScene::getProgramActionButtonRect() const
{
	if (compactMobileLayout)
	{
		const Rect checkButtonRect = getCheckUpdatesButtonRect();
		Rect buttonRect = getExitButtonRect();
		const int gap = panelWidth < HeaderActionCompactPanelWidth ? 6 : 10;
		buttonRect.x = checkButtonRect.x - buttonRect.w - gap;
		return buttonRect;
	}
	const bool compact = panelWidth < HeaderActionCompactPanelWidth;
	const int buttonWidth = compact
		? getExitButtonRect().w : ProgramActionButtonWidth;
	return
	{
		panelX + panelWidth - panelPadding - buttonWidth,
		panelY + ProgramActionTopOffset,
		buttonWidth,
		ActionButtonHeight
	};
}

Rect ResourceSelectScene::getSaveManagementButtonRect() const
{
	Rect buttonRect = getCheatHelpButtonRect();
	const int gap = panelWidth < HeaderActionCompactPanelWidth ? 6 : 10;
	buttonRect.x += buttonRect.w + gap;
	return buttonRect;
}

Rect ResourceSelectScene::getDisplaySettingsRowValueRect(int row) const
{
	const Rect previous = getDisplaySettingsPreviousButtonRect(row);
	const Rect next = getDisplaySettingsNextButtonRect(row);
	return
	{
		previous.x + previous.w + 8,
		previous.y,
		std::max(1, next.x - previous.x - previous.w - 16),
		previous.h
	};
}

Rect ResourceSelectScene::getDisplaySettingsPreviousButtonRect(int row) const
{
	const int labelWidth = std::min(170, contentWidth * 34 / 100);
	return
	{
		panelX + panelPadding + labelWidth,
		panelY + DisplaySettingsFirstRowTopOffset +
			row * DisplaySettingsRowHeight,
		DisplaySettingsArrowButtonSize,
		DisplaySettingsArrowButtonSize
	};
}

Rect ResourceSelectScene::getDisplaySettingsNextButtonRect(int row) const
{
	return
	{
		panelX + panelWidth - panelPadding - DisplaySettingsArrowButtonSize,
		panelY + DisplaySettingsFirstRowTopOffset +
			row * DisplaySettingsRowHeight,
		DisplaySettingsArrowButtonSize,
		DisplaySettingsArrowButtonSize
	};
}

Rect ResourceSelectScene::getDisplaySettingsApplyButtonRect() const
{
	const int gap = 10;
	const int buttonWidth = std::max(1,
		(contentWidth - gap * 2) / 3);
	return
	{
		panelX + panelPadding,
		panelY + panelHeight - DisplaySettingsActionButtonHeight - 18,
		buttonWidth,
		DisplaySettingsActionButtonHeight
	};
}

Rect ResourceSelectScene::getDisplaySettingsDefaultButtonRect() const
{
	Rect result = getDisplaySettingsApplyButtonRect();
	result.x += result.w + 10;
	return result;
}

Rect ResourceSelectScene::getDisplaySettingsBackButtonRect() const
{
	Rect result = getDisplaySettingsDefaultButtonRect();
	result.x += result.w + 10;
	return result;
}

void ResourceSelectScene::updateDisplaySettingsControlLayout()
{
	for (int row = 0; row < DisplaySettingsRowCount; row++)
	{
		if (displaySettingsPreviousButtons[row] != nullptr)
		{
			displaySettingsPreviousButtons[row]->rect =
				getDisplaySettingsPreviousButtonRect(row);
		}
		if (displaySettingsNextButtons[row] != nullptr)
		{
			displaySettingsNextButtons[row]->rect =
				getDisplaySettingsNextButtonRect(row);
		}
	}
	if (displaySettingsApplyButton != nullptr)
	{
		displaySettingsApplyButton->rect = getDisplaySettingsApplyButtonRect();
	}
	if (displaySettingsDefaultButton != nullptr)
	{
		displaySettingsDefaultButton->rect = getDisplaySettingsDefaultButtonRect();
	}
	if (displaySettingsBackButton != nullptr)
	{
		displaySettingsBackButton->rect = getDisplaySettingsBackButtonRect();
	}
}

Rect ResourceSelectScene::getHeaderTitleRect() const
{
	if (compactMobileLayout)
	{
		const int versionLaneWidth = std::min(84, contentWidth / 4);
		return
		{
			panelX + panelPadding,
			panelY + HeaderTitleTopOffset,
			std::max(1, contentWidth - versionLaneWidth - 6),
			CompactMobileHeaderActionTopOffset - HeaderTitleTopOffset - 4
		};
	}
	return
	{
		panelX + panelPadding,
		panelY + HeaderTitleTopOffset,
		contentWidth,
		HeaderActionTopOffset - HeaderTitleTopOffset - 5
	};
}

Rect ResourceSelectScene::getOnlineActionButtonRect() const
{
	const int horizontalInset = wideDetailLayout ? 12 : 8;
	const int buttonWidth = std::max(1, std::min(
		detailArea.w - horizontalInset * 2,
		detailArea.w < 340 ? 82 : 96));
	const int buttonHeight = std::max(1, std::min(
		36, detailArea.h - horizontalInset * 2));
	return
	{
		detailArea.x + detailArea.w - horizontalInset - buttonWidth,
		detailArea.y + horizontalInset,
		buttonWidth,
		buttonHeight
	};
}

Rect ResourceSelectScene::getResourceRemoveButtonRect() const
{
	Rect buttonRect = getOnlineActionButtonRect();
	if (onlineActionButton != nullptr && onlineActionButton->visible)
	{
		buttonRect.x -= buttonRect.w + 6;
	}
	return buttonRect;
}

Rect ResourceSelectScene::getCheatHelpDialogRect() const
{
	const int dialogWidth = std::max(1, std::min(620, rect.w - 24));
	const int dialogHeight = std::max(1, std::min(360, rect.h - 24));
	return
	{
		(rect.w - dialogWidth) / 2,
		(rect.h - dialogHeight) / 2,
		dialogWidth,
		dialogHeight
	};
}

Rect ResourceSelectScene::getCheatHelpCloseButtonRect() const
{
	const Rect dialogRect = getCheatHelpDialogRect();
	const int buttonWidth = std::max(1, std::min(180, dialogRect.w - 32));
	const int buttonHeight = std::max(1, std::min(40, dialogRect.h - 16));
	return
	{
		dialogRect.x + (dialogRect.w - buttonWidth) / 2,
		dialogRect.y + dialogRect.h - buttonHeight - 12,
		buttonWidth,
		buttonHeight
	};
}

Rect ResourceSelectScene::getExternalResourceDialogRect() const
{
	const int dialogWidth = std::max(1, std::min(620, rect.w - 24));
	const int dialogHeight = std::max(1, std::min(330, rect.h - 24));
	return
	{
		(rect.w - dialogWidth) / 2,
		(rect.h - dialogHeight) / 2,
		dialogWidth,
		dialogHeight
	};
}

Rect ResourceSelectScene::getExternalResourceConfirmButtonRect() const
{
	const Rect dialog = getExternalResourceDialogRect();
	const int gap = 12;
	const int buttonWidth = std::max(1, std::min(
		160, (dialog.w - 48 - gap) / 2));
	const int buttonHeight = std::max(1, std::min(42, dialog.h - 16));
	return
	{
		dialog.x + dialog.w / 2 - gap / 2 - buttonWidth,
		dialog.y + dialog.h - buttonHeight - 14,
		buttonWidth,
		buttonHeight
	};
}

Rect ResourceSelectScene::getExternalResourceCancelButtonRect() const
{
	Rect button = getExternalResourceConfirmButtonRect();
	button.x = getExternalResourceDialogRect().x +
		getExternalResourceDialogRect().w / 2 + 6;
	return button;
}

Rect ResourceSelectScene::getResourceInstallDialogRect() const
{
	const int dialogWidth = std::max(1, std::min(660, rect.w - 24));
	const int dialogHeight = std::max(1, std::min(440, rect.h - 24));
	return
	{
		(rect.w - dialogWidth) / 2,
		(rect.h - dialogHeight) / 2,
		dialogWidth,
		dialogHeight
	};
}

Rect ResourceSelectScene::getResourceInstallPrimaryButtonRect() const
{
	const Rect dialog = getResourceInstallDialogRect();
	const int gap = 12;
	const int buttonWidth = std::max(1, std::min(
		160, (dialog.w - 48 - gap) / 2));
	const int buttonHeight = std::max(1, std::min(42, dialog.h - 16));
	const bool centered = resourceInstallSecondaryButton == nullptr ||
		!resourceInstallSecondaryButton->visible;
	return
	{
		centered
			? dialog.x + (dialog.w - buttonWidth) / 2
			: dialog.x + dialog.w / 2 - gap / 2 - buttonWidth,
		dialog.y + dialog.h - buttonHeight - 14,
		buttonWidth,
		buttonHeight
	};
}

Rect ResourceSelectScene::getResourceInstallSecondaryButtonRect() const
{
	const Rect dialog = getResourceInstallDialogRect();
	const int gap = 12;
	const int buttonWidth = std::max(1, std::min(
		160, (dialog.w - 48 - gap) / 2));
	const int buttonHeight = std::max(1, std::min(42, dialog.h - 16));
	return
	{
		dialog.x + dialog.w / 2 + gap / 2,
		dialog.y + dialog.h - buttonHeight - 14,
		buttonWidth,
		buttonHeight
	};
}

Rect ResourceSelectScene::getResourceInstallPreviousPageButtonRect() const
{
	const Rect dialog = getResourceInstallDialogRect();
	const int width = std::max(1, std::min(96, (dialog.w - 48) / 2));
	return
	{
		dialog.x + dialog.w / 2 - width - 6,
		getResourceInstallPrimaryButtonRect().y - 38,
		width,
		30
	};
}

Rect ResourceSelectScene::getResourceInstallNextPageButtonRect() const
{
	const Rect dialog = getResourceInstallDialogRect();
	const int width = std::max(1, std::min(96, (dialog.w - 48) / 2));
	return
	{
		dialog.x + dialog.w / 2 + 6,
		getResourceInstallSecondaryButtonRect().y - 38,
		width,
		30
	};
}

Rect ResourceSelectScene::getEnableExternalButtonRect() const
{
	// 先说明权限类型和固定目录，再提供授权/开关操作，避免用户误以为
	// 必须授权才能游玩内置资源。
	const int buttonWidth = std::min(
		contentWidth,
		panelWidth < HeaderActionCompactPanelWidth ? 190 : 220);
	const int x = startX + (contentWidth - buttonWidth) / 2;
	if (compactMobileLayout)
	{
		return
		{
			std::max(panelX + panelPadding, x),
			panelY + panelHeight -
				CompactMobileExternalButtonBottomOffset,
			std::max(1, buttonWidth),
			34
		};
	}
	const int compactShift = compactVerticalLayout ? 8 : 0;
	return
	{
		std::max(panelX + panelPadding, x),
		panelY + panelHeight - (139 - compactShift),
		std::max(1, buttonWidth),
		34
	};
}

Rect ResourceSelectScene::getExternalResourcePathHintRect() const
{
	if (compactMobileLayout)
	{
		return { 0, 0, 0, 0 };
	}
	const int compactShift = compactVerticalLayout ? 8 : 0;
	return
	{
		startX,
		panelY + panelHeight - (188 - compactShift),
		std::max(1, contentWidth),
		42
	};
}

Rect ResourceSelectScene::getCreditsTextAreaRect() const
{
	if (compactMobileLayout)
	{
		const int footerHeight = enableExternalButton != nullptr
			? CompactMobileExternalFooterHeight
			: CompactMobileCreditsFooterHeight;
		return
		{
			startX,
			panelY + panelHeight - footerHeight + 4,
			std::max(1, contentWidth),
			CompactMobileCreditsHeight
		};
	}
	const int compactShift =
		enableExternalButton != nullptr && compactVerticalLayout ? 8 : 0;
	return
	{
		startX,
		panelY + panelHeight -
			(enableExternalButton != nullptr
				? 98 - compactShift
				: 136),
		std::max(1, contentWidth),
		enableExternalButton != nullptr ? 50 : 61
	};
}

Rect ResourceSelectScene::getExternalLinkRect(int index) const
{
	if (index < 0 || index >= static_cast<int>(ExternalLinks.size()))
	{
		return { 0, 0, 0, 0 };
	}
	const int gap = panelWidth < 460 ? 5 : 9;
	const int linkCount = static_cast<int>(ExternalLinks.size());
	const int totalGap = gap * (linkCount - 1);
	const int availableWidth = std::max(linkCount, contentWidth - totalGap);
	const int baseWidth = availableWidth / linkCount;
	const int remainder = availableWidth % linkCount;
	const int linkWidth = baseWidth + (index < remainder ? 1 : 0);
	const int linkX = startX + index * baseWidth + std::min(index, remainder) + index * gap;
	// 四个外部入口始终固定在主界面底部；窄屏只缩短标签，不隐藏入口。
	const int linkY = compactMobileLayout
		? panelY + panelHeight - CompactMobileExternalLinkBottomOffset
		: panelY + panelHeight -
			(48 - (enableExternalButton != nullptr && compactVerticalLayout
				? 8 : 0));
	return { linkX, linkY, linkWidth, ActionButtonHeight };
}

void ResourceSelectScene::openExternalLink(int index)
{
	if (index < 0 || index >= static_cast<int>(ExternalLinks.size()))
	{
		return;
	}

	const char* url = ExternalLinks[index].url;
	const std::string securePrefix = "https://";
	if (std::string(url).compare(0, securePrefix.size(), securePrefix) != 0)
	{
		GameLog::write("ResourceSelectScene: rejected non-HTTPS external link\n");
		return;
	}

	if (!SDL_OpenURL(url))
	{
		GameLog::write("ResourceSelectScene: failed to open %s: %s\n",
			ExternalLinks[index].label, SDL_GetError());
	}
}

Rect ResourceSelectScene::getScrollbarRect() const
{
	const int resourcePackItemHeight = compactMobileLayout
		? CompactMobileResourcePackItemHeight : ResourcePackItemHeight;
	const int visibleCount =
		std::max(1, listHeight / resourcePackItemHeight);
	const int trackHeight = std::max(
		24, visibleCount * resourcePackItemHeight -
			(compactMobileLayout ? CompactMobileItemGap : 10));
	const int trackWidth = 8;
	const int touchPadding = 12;
	const int trackX = resourceListArea.x + resourceListArea.w
		- std::max(10, panelPadding / 3);
	return
	{
		trackX - touchPadding,
		resourceListArea.y,
		trackWidth + touchPadding * 2,
		trackHeight
	};
}

void ResourceSelectScene::toggleExternalResources()
{
	pendingExternalRescan = false;
	if (externalResourcePresentationState ==
		ExternalResourcePresentationState::Enabled)
	{
		Config::externalResourcesEnabled = false;
		Config::save();
		externalResourcePresentationState =
			ExternalResourcePresentationState::Disabled;
		refreshExternalResourcePresentation();
		performExternalRescan();
		return;
	}

	if (AndroidExternalStorage::isAllFilesAccessGranted())
	{
		completeExternalPermissionRequest(true);
		return;
	}

	// 权限尚未授予时，配置保持关闭；按钮显示“等待授权”，而不是提前
	// 显示“已开启”。用户从系统设置返回后才提交实际开启状态。
	if (Config::externalResourcesEnabled)
	{
		Config::externalResourcesEnabled = false;
		Config::save();
	}
	externalResourcePresentationState =
		ExternalResourcePresentationState::WaitingForPermission;
	refreshExternalResourcePresentation();
	GameLog::write(
		"ResourceSelectScene: external resources require all-files access; place packs under %s\n",
		externalResourceDirectoryPath.empty()
			? "<unknown>" : externalResourceDirectoryPath.c_str());
	pendingExternalRescan = true;
	AndroidExternalStorage::requestAllFilesAccess();
}

void ResourceSelectScene::completeExternalPermissionRequest(
	bool permissionGranted)
{
	pendingExternalRescan = false;
	if (Config::externalResourcesEnabled != permissionGranted)
	{
		Config::externalResourcesEnabled = permissionGranted;
		Config::save();
	}
	externalResourcePresentationState = permissionGranted
		? ExternalResourcePresentationState::Enabled
		: ExternalResourcePresentationState::PermissionRequired;
	refreshExternalResourceDirectoryPath();
	refreshExternalResourcePresentation();
	performExternalRescan();
}

void ResourceSelectScene::refreshExternalResourcePresentation()
{
	if (enableExternalButton == nullptr)
	{
		return;
	}
	switch (externalResourcePresentationState)
	{
	case ExternalResourcePresentationState::Enabled:
		enableExternalButton->setUTF8Str(u8"外部资源：已开启");
		break;
	case ExternalResourcePresentationState::WaitingForPermission:
		enableExternalButton->setUTF8Str(u8"外部资源：等待权限");
		break;
	case ExternalResourcePresentationState::PermissionRequired:
		enableExternalButton->setUTF8Str(u8"外部资源：需要授权");
		break;
	case ExternalResourcePresentationState::Disabled:
	default:
		enableExternalButton->setUTF8Str(u8"外部资源：未开启");
		break;
	}
}

void ResourceSelectScene::refreshExternalResourceDirectoryPath()
{
	externalResourceDirectoryPath =
		AndroidExternalStorage::getExternalResourceDirectoryPath();
	if (externalResourceDirectoryPath.empty())
	{
		// 桌面移动替身无法查询 Android 的公开存储根；仍展示标准设备端
		// 固定绝对路径，真实 Android 构建会显示系统返回的公开存储根。
		externalResourceDirectoryPath =
			"/storage/emulated/0/Download/jxqy/assets/";
	}
}

void ResourceSelectScene::performExternalRescan()
{
	ResourceManager::instance().rescanExternalResourceDirectory();
	buildResourceList();
	configureFocus();
}

void ResourceSelectScene::onDraw()
{
	pollOnlineCatalogCheck();
	presentPendingProgramUpdateDialog();
	pollResourceInstall();
	// 权限设置页返回后只检查一次；只有实际授权成功才提交“已开启”。
	if (pendingExternalRescan &&
		AndroidExternalStorage::consumeAllFilesAccessRequestCompleted())
	{
		completeExternalPermissionRequest(
			AndroidExternalStorage::isAllFilesAccessGranted());
	}
	synchronizeSemanticFocusWithInput();
	int width = 0;
	int height = 0;
	engine->getWindowSize(width, height);
	rect.w = width;
	rect.h = height;
	updateLayout(width, height);

	drawBackground(width, height);
	if (displaySettingsVisible)
	{
		drawDisplaySettingsPage();
	}
	else
	{
		drawPanel();
	}
	if (cheatHelpVisible)
	{
		drawCheatHelpOverlay();
	}
	else if (externalResourceDialogVisible)
	{
		drawExternalResourceOverlay();
	}
	else if (resourceInstallDialogState !=
		ResourceInstallDialogState::Hidden)
	{
		drawResourceInstallOverlay();
	}
}

void ResourceSelectScene::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !Element::isCurrentRunOwner(this)
		|| !semanticFocusVisible || keyboardSemanticFocus)
	{
		return;
	}
	using GameInput::InputAction;
	const bool resourceInstallDialogVisible =
		resourceInstallDialogState != ResourceInstallDialogState::Hidden;
	std::vector<ControllerPromptItem> items;
	if (cheatHelpVisible)
	{
		items =
		{
			{ InputAction::Confirm, "关闭", { InputAction::Cancel } }
		};
	}
	else if (displaySettingsVisible)
	{
		items =
		{
			{ InputAction::NavigateUp, "选择" },
			{ InputAction::Confirm, "确认" },
			{ InputAction::Cancel, "返回" }
		};
	}
	else if (externalResourceDialogVisible)
	{
		items =
		{
			{ InputAction::NavigateLeft, "选择" },
			{ InputAction::Confirm, "确认" },
			{ InputAction::Cancel, "返回" }
		};
	}
	else if (resourceInstallDialogVisible &&
		resourceInstallDialogState != ResourceInstallDialogState::Cancelling)
	{
		items =
			{
				{ InputAction::NavigateUp, "选择" },
				{ InputAction::Confirm, "确认" },
				{ InputAction::Cancel,
					resourceInstallDialogState ==
						ResourceInstallDialogState::Downloading
						? "取消下载" : "返回" }
			};
	}
	else if (!resourceInstallDialogVisible)
	{
		items =
		{
			{ InputAction::NavigateUp, "选择" },
			{ InputAction::Confirm, "确认" },
			{ InputAction::PreviousPage, "翻页",
				{ InputAction::NextPage } },
			{ InputAction::Cancel, "退出" }
		};
	}
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items);
}

void ResourceSelectScene::drawBackground(int width, int height)
{
	if (backgroundImage != nullptr)
	{
		Rect backgroundRect = { 0, 0, width, height };
		engine->drawImage(backgroundImage, nullptr, &backgroundRect);
		engine->fillRect(0, 0, width, height, 0, 0, 0, 48);
		engine->fillRect(0, 0, width, 3, 214, 184, 112, 180);
		engine->fillRect(0, height - 3, width, 3, 174, 132, 78, 150);
		return;
	}

	engine->fillRect(0, 0, width, height, 18, 14, 12, 255);
	const int bandHeight = std::max(1, height / 18);
	for (int y = 0; y < height; y += bandHeight)
	{
		const int shade = y * 42 / std::max(1, height);
		engine->fillRect(0, y, width, bandHeight,
			static_cast<uint8_t>(26 + shade / 4),
			static_cast<uint8_t>(20 + shade / 5),
			static_cast<uint8_t>(16 + shade / 6), 255);
	}
	const int sideWidth = clampInt(width / 12, 48, 112);
	engine->fillRect(0, 0, sideWidth, height, 80, 28, 22, 88);
	engine->fillRect(width - sideWidth, 0, sideWidth, height, 80, 28, 22, 72);
	engine->fillRect(0, 0, width, height, 0, 0, 0, 64);
	engine->fillRect(0, 0, width, 3, 214, 184, 112, 220);
	engine->fillRect(0, height - 3, width, 3, 174, 132, 78, 180);
}

void ResourceSelectScene::drawPanel()
{
	engine->fillRect(panelX - 3, panelY - 3, panelWidth + 6, panelHeight + 6, 216, 184, 112, 120);
	engine->fillRect(panelX, panelY, panelWidth, panelHeight, 18, 14, 12, 174);
	engine->fillRect(panelX + 1, panelY + 1, panelWidth - 2, panelHeight - 2, 56, 42, 28, 96);
	engine->fillRect(panelX + panelPadding,
		panelY + (compactMobileLayout
			? CompactMobileHeaderSeparatorOffset : HeaderSeparatorOffset),
		contentWidth, 1, 216, 184, 112, 120);
	const bool hasExternalResourceControls = enableExternalButton != nullptr;
	const int centerX = panelX + panelWidth / 2;
	if (compactMobileLayout)
	{
		const int compactFooterHeight = hasExternalResourceControls
			? CompactMobileExternalFooterHeight
			: CompactMobileCreditsFooterHeight;
		engine->fillRect(panelX + panelPadding,
			panelY + panelHeight - compactFooterHeight,
			contentWidth, 1, 216, 184, 112, 90);
		drawResourceDetails();
		const Rect titleRect = getHeaderTitleRect();
		drawCenteredText(
			ResourceSelectTitle,
			titleRect.x + titleRect.w / 2,
			panelY + HeaderTitleTopOffset,
			26,
			0xFFFFE7B0,
			titleRect.w,
			14);
		const int versionLaneX = titleRect.x + titleRect.w + 6;
		const int versionLaneWidth = std::max(
			1, startX + contentWidth - versionLaneX);
		drawCenteredText(
			resourceSelectVersionSubtitle(true),
			versionLaneX + versionLaneWidth / 2,
			panelY + 10,
			16,
			0xFFFFFFFF,
			versionLaneWidth,
			14);
		drawCenteredText(
			JxqyBuildVersion::ReleaseStage,
			versionLaneX + versionLaneWidth / 2,
			panelY + 27,
			11,
			0xFFD8C59A,
			versionLaneWidth,
			10);
		const Rect creditsRect = getCreditsTextAreaRect();
		drawCenteredText(
			u8"引擎作者：Upwinded",
			centerX,
			creditsRect.y,
			11,
			0xFFD8C59A,
			creditsRect.w,
			10);
		drawCenteredText(
			u8"感谢：偶像（Weyl、BT、scarsty、SB500）、小试刀剑",
			centerX,
			creditsRect.y + 10,
			11,
			0xFFB9AA87,
			creditsRect.w,
			10);
		drawCenteredText(
			u8"铁血丹心论坛、剑侠情缘贴吧",
			centerX,
			creditsRect.y + 20,
			11,
			0xFFB9AA87,
			creditsRect.w,
			10);
	}
	else
	{
		const int compactFooterShift =
			hasExternalResourceControls && compactVerticalLayout ? 8 : 0;
		const int footerSeparatorOffset =
			hasExternalResourceControls
				? 102 - compactFooterShift
				: 145;
		engine->fillRect(panelX + panelPadding,
			panelY + panelHeight - footerSeparatorOffset,
			contentWidth, 1, 216, 184, 112, 90);
		drawResourceDetails();

		const Rect titleRect = getHeaderTitleRect();
		const int titleCenterX = titleRect.x + titleRect.w / 2;
		const int titleMaximumWidth = titleRect.w;
		drawCenteredText(
			ResourceSelectTitle, titleCenterX,
			panelY + HeaderTitleTopOffset, 28,
			0xFFFFE7B0, titleMaximumWidth, 14);
		drawCenteredText(resourceSelectVersionSubtitle(
			panelWidth < HeaderActionCompactPanelWidth),
			titleCenterX, panelY + HeaderSubtitleTopOffset, 16,
			0xFFFFFFFF, titleMaximumWidth, 14);
		const int authorOffset = hasExternalResourceControls
			? 98 - compactFooterShift
			: 136;
		const int specialThanksOffset = hasExternalResourceControls
			? 79 - compactFooterShift
			: 111;
		const int communityThanksOffset = hasExternalResourceControls
			? 62 - compactFooterShift
			: 91;
		drawCenteredText("引擎作者：Upwinded", centerX,
			panelY + panelHeight - authorOffset,
			hasExternalResourceControls ? 18 : 20,
			0xFFD8C59A, contentWidth,
			hasExternalResourceControls ? 14 : 16);
		drawCenteredText("特别感谢：偶像（Weyl、BT、scarsty、SB500）、小试刀剑",
			centerX, panelY + panelHeight - specialThanksOffset,
			hasExternalResourceControls ? 14 : 16,
			0xFFB9AA87, contentWidth,
			14);
		drawCenteredText("铁血丹心论坛、剑侠情缘贴吧",
			centerX, panelY + panelHeight - communityThanksOffset,
			hasExternalResourceControls ? 14 : 16,
			0xFFB9AA87, contentWidth,
			14);
		if (enableExternalButton != nullptr)
		{
			const Rect pathHintRect = getExternalResourcePathHintRect();
			drawCenteredText(
				ExternalResourceCollectionInstruction,
				centerX,
				pathHintRect.y,
				17,
				0xFFD8C59A,
				pathHintRect.w,
				14);
			drawCenteredText(
				externalResourceDirectoryPath,
				centerX,
				pathHintRect.y + 21,
				18,
				0xFFFFE7B0,
				pathHintRect.w,
				14);
		}
	}

	if (resourceList != nullptr
		&& static_cast<int>(resourceEntries.size()) >
			resourceList->getVisibleItemCount())
	{
		const int firstIndex = resourceList->getFirstVisibleIndex();
		const int endIndex = std::min(
			static_cast<int>(resourceEntries.size()),
			firstIndex + resourceList->getVisibleItemCount());
		const std::string pageText = std::to_string(firstIndex + 1) + "-" +
			std::to_string(endIndex) + " / " +
			std::to_string(resourceEntries.size());
		if (catalogStatusText.empty())
		{
			const Rect programRect = getProgramActionButtonRect();
			const int statusRight = !compactMobileLayout &&
				programActionButton != nullptr &&
					programActionButton->visible
				? programRect.x - 8 : startX + contentWidth;
			const int statusWidth = std::max(1, statusRight - startX);
			drawCenteredText(pageText, startX + statusWidth / 2,
				panelY + (compactMobileLayout
					? CompactMobileHeaderStatusTopOffset : HeaderStatusTopOffset), 15,
				0xFF9BA4AF, statusWidth, 14);
		}
	}
	if (!catalogStatusText.empty())
	{
		const Rect programRect = getProgramActionButtonRect();
		const int statusRight = !compactMobileLayout &&
			programActionButton != nullptr &&
				programActionButton->visible
			? programRect.x - 8 : startX + contentWidth;
		const int statusWidth = std::max(1, statusRight - startX);
		drawCenteredText(catalogStatusText, startX + statusWidth / 2,
			panelY + (compactMobileLayout
				? CompactMobileHeaderStatusTopOffset : HeaderStatusTopOffset), 15,
			catalogCheckState == CatalogCheckState::Failed
				? 0xFFFFB0A0 : 0xFFC8D8B0,
			statusWidth, 14);
	}
	if (resourceEntries.empty())
	{
		// 零包时仍保留统一的更新检查和退出入口。
		const int listCenterY = resourceListArea.y +
			std::max(minimumListHeight, resourceListArea.h) / 2;
		drawCenteredText(EmptyResourceListPrimary, centerX, listCenterY - 18,
			22, 0xFFFFE7B0, contentWidth, 14);
		drawCenteredText(emptyResourceListHint(), centerX, listCenterY + 16,
			16, 0xFFFFFFFF, contentWidth, 14);
	}
}

void ResourceSelectScene::drawDisplaySettingsPage()
{
	engine->fillRect(panelX - 3, panelY - 3,
		panelWidth + 6, panelHeight + 6, 216, 184, 112, 120);
	engine->fillRect(panelX, panelY,
		panelWidth, panelHeight, 18, 14, 12, 226);
	engine->fillRect(panelX + 1, panelY + 1,
		panelWidth - 2, panelHeight - 2, 56, 42, 28, 96);
	drawCenteredText(u8"显示设置",
		panelX + panelWidth / 2, panelY + 18,
		28, 0xFFFFE7B0, contentWidth, 18);
	drawCenteredText(
		u8"点击“应用”后立即生效；窗口模式也可直接拖动窗口得到自定义尺寸",
		panelX + panelWidth / 2, panelY + 58,
		16, 0xFFD8C59A, contentWidth, 14);
	engine->fillRect(panelX + panelPadding, panelY + 91,
		contentWidth, 1, 216, 184, 112, 120);

	std::array<std::string, DisplaySettingsRowCount> labels =
	{
		u8"显示器", u8"显示模式", u8"分辨率", u8"画面适配"
	};
	std::array<std::string, DisplaySettingsRowCount> values;
	auto displayIterator = std::find_if(
		desktopDisplays.begin(), desktopDisplays.end(),
		[this](const DesktopDisplayInfo& info)
		{
			return info.index == pendingDisplaySettings.displayIndex;
		});
	if (displayIterator != desktopDisplays.end())
	{
		values[0] = std::to_string(displayIterator->index + 1);
		if (!displayIterator->name.empty())
		{
			values[0] += "  " + displayIterator->name;
		}
	}
	else
	{
		values[0] = u8"默认显示器";
	}
	switch (pendingDisplaySettings.fullScreenMode)
	{
	case FullScreenMode::windowFullScreen:
		values[1] = u8"无边框全屏";
		break;
	case FullScreenMode::fullScreen:
		values[1] = u8"独占全屏";
		break;
	case FullScreenMode::window:
	default:
		values[1] = u8"窗口";
		break;
	}
	values[2] = std::to_string(pendingDisplaySettings.width) + u8" × " +
		std::to_string(pendingDisplaySettings.height);
	if (pendingDisplaySettings.fullScreenMode != FullScreenMode::fullScreen &&
		std::find(WindowResolutionPresets.begin(),
			WindowResolutionPresets.end(), DesktopDisplayResolution
			{
				pendingDisplaySettings.width,
				pendingDisplaySettings.height
			}) == WindowResolutionPresets.end())
	{
		values[2] += u8"（自定义）";
	}
	switch (pendingDisplaySettings.fullScreenSolutionMode)
	{
	case FullScreenSolutionMode::adjust:
		values[3] = u8"保持设定比例";
		break;
	case FullScreenSolutionMode::forceToUseSetting:
		values[3] = u8"固定设定分辨率";
		break;
	case FullScreenSolutionMode::original:
	default:
		values[3] = u8"保持设定比例";
		break;
	}

	for (int row = 0; row < DisplaySettingsRowCount; row++)
	{
		const Rect previous = getDisplaySettingsPreviousButtonRect(row);
		const Rect valueRect = getDisplaySettingsRowValueRect(row);
		const int labelX = panelX + panelPadding;
		const int labelWidth = std::max(1, previous.x - labelX - 12);
		drawTextLine(labels[row], labelX, previous.y + 9,
			18, 0xFFD8C59A, labelWidth, 14);
		engine->fillRect(valueRect.x, valueRect.y,
			valueRect.w, valueRect.h, 12, 10, 9, 150);
		drawCenteredText(values[row],
			valueRect.x + valueRect.w / 2, valueRect.y + 9,
			17, 0xFFFFFFFF, valueRect.w - 10, 13);
	}

	if (!displaySettingsStatusText.empty())
	{
		const Rect applyRect = getDisplaySettingsApplyButtonRect();
		drawCenteredText(displaySettingsStatusText,
			panelX + panelWidth / 2, applyRect.y - 29,
			16,
			displaySettingsStatusText.find(u8"失败") != std::string::npos
				? 0xFFFFB0A0 : 0xFFC8D8B0,
			contentWidth, 14);
	}
}

void ResourceSelectScene::drawCheatHelpOverlay()
{
	const Rect dialogRect = getCheatHelpDialogRect();
	const Rect closeButtonRect = getCheatHelpCloseButtonRect();
	engine->fillRect(0, 0, rect.w, rect.h, 0, 0, 0, 196);
	engine->fillRect(dialogRect.x - 3, dialogRect.y - 3,
		dialogRect.w + 6, dialogRect.h + 6, 216, 184, 112, 220);
	engine->fillRect(dialogRect.x, dialogRect.y,
		dialogRect.w, dialogRect.h, 24, 18, 14, 250);
	drawCenteredText(u8"作弊说明",
		dialogRect.x + dialogRect.w / 2,
		dialogRect.y + 16,
		26, 0xFFFFE7B0, std::max(1, dialogRect.w - 32), 18);
	engine->fillRect(dialogRect.x + 16, dialogRect.y + 52,
		std::max(1, dialogRect.w - 32), 1, 216, 184, 112, 120);
	const Rect textRect =
	{
		dialogRect.x + 24,
		dialogRect.y + 68,
		std::max(1, dialogRect.w - 48),
		std::max(1, closeButtonRect.y - dialogRect.y - 80)
	};
	drawWrappedDescription(
		CheatHelpText,
		textRect,
		dialogRect.h < 320 ? 14 : 16,
		0xFFFFFFFF);
}

void ResourceSelectScene::drawExternalResourceOverlay()
{
	const Rect dialog = getExternalResourceDialogRect();
	engine->fillRect(0, 0, rect.w, rect.h, 0, 0, 0, 196);
	engine->fillRect(dialog.x - 3, dialog.y - 3,
		dialog.w + 6, dialog.h + 6, 216, 184, 112, 220);
	engine->fillRect(dialog.x, dialog.y,
		dialog.w, dialog.h, 24, 18, 14, 250);
	drawCenteredText(
		u8"外部资源读取",
		dialog.x + dialog.w / 2,
		dialog.y + 14,
		25,
		0xFFFFE7B0,
		std::max(1, dialog.w - 32),
		17);
	engine->fillRect(dialog.x + 16, dialog.y + 50,
		std::max(1, dialog.w - 32), 1, 216, 184, 112, 120);

	std::string explanation =
		u8"此功能只读取你手动放入固定目录的外部 MOD，不影响程序下载的线上游戏资源。\n"
		u8"读取目录：" + externalResourceDirectoryPath + u8"\n";
	if (externalResourcePresentationState ==
		ExternalResourcePresentationState::Enabled)
	{
		explanation +=
			u8"当前已开启。关闭后只停止扫描外部目录，不会删除目录中的文件。";
	}
	else
	{
		explanation +=
			u8"开启后会申请 Android 所有文件访问权限，并重新扫描该目录；"
			u8"拒绝授权不会影响内置或在线下载的游戏资源。";
	}
	drawWrappedDescription(
		explanation,
		{
			dialog.x + 24,
			dialog.y + 68,
			std::max(1, dialog.w - 48),
			std::max(1,
				getExternalResourceConfirmButtonRect().y - dialog.y - 82)
		},
		17,
		0xFFFFFFFF);
}

void ResourceSelectScene::drawResourceInstallOverlay()
{
	const Rect dialog = getResourceInstallDialogRect();
	engine->fillRect(0, 0, rect.w, rect.h, 0, 0, 0, 196);
	engine->fillRect(dialog.x - 3, dialog.y - 3,
		dialog.w + 6, dialog.h + 6, 216, 184, 112, 220);
	engine->fillRect(dialog.x, dialog.y,
		dialog.w, dialog.h, 24, 18, 14, 250);

	std::string title;
	switch (resourceInstallDialogState)
	{
	case ResourceInstallDialogState::Confirming:
		if (pendingDownloadUsesMeteredNetwork &&
			pendingMeteredDownloadConfirmed &&
			resourceInstallOperation !=
				ResourceInstallOperation::ResourceRemoval)
		{
			title = u8"再次确认移动网络下载";
			break;
		}
		if (resourceInstallOperation ==
			ResourceInstallOperation::ResourceRemoval)
		{
			title = u8"确认删除游戏资源";
		}
		else if (resourceInstallOperation ==
			ResourceInstallOperation::ProgramDownload)
		{
			const OnlineUpdate::ProgramUpdateCheck update =
				OnlineUpdate::checkProgramUpdate(
					onlineApplicationCatalog,
					JxqyBuildVersion::ProgramUpdateTarget,
					JxqyBuildVersion::EngineVersion);
			title = update.hasUpdate()
				? u8"确认更新主程序" : u8"确认可选重装";
		}
		else if (!pendingResourceInstall.requestedResourceInstalled)
		{
			title = u8"确认下载此游戏";
		}
		else if (pendingResourceInstall.requestedVersionMatches &&
			pendingResourceInstall.requestedDownloadMode ==
				OnlineUpdate::RequestedResourceDownloadMode::ForceFullPackage)
		{
			title = u8"确认重新下载此游戏";
		}
		else
		{
			title = resourceUpdatePromptedByEntry
				? u8"发现游戏资源更新" : u8"确认更新此游戏";
		}
		break;
	case ResourceInstallDialogState::BrowsingSaves:
		title = u8"存档管理";
		break;
	case ResourceInstallDialogState::ConfirmingSaveRemoval:
		title = u8"确认删除存档";
		break;
	case ResourceInstallDialogState::Downloading:
		title = resourceInstallOperation ==
					ResourceInstallOperation::ProgramDownload
			? u8"正在准备主程序"
			: u8"正在准备游戏资源";
		break;
	case ResourceInstallDialogState::Cancelling:
		title = u8"正在取消下载";
		break;
	case ResourceInstallDialogState::ReadyToRestart:
		title = resourceInstallOperation ==
				ResourceInstallOperation::ProgramDownload
			? u8"主程序准备完成" : u8"资源准备完成";
		break;
	case ResourceInstallDialogState::Completed:
		if (resourceInstallOperation ==
			ResourceInstallOperation::SaveManagement)
		{
			title = u8"存档删除完成";
		}
		else if (resourceInstallOperation ==
			ResourceInstallOperation::ResourceRemoval)
		{
			title = u8"资源删除完成";
		}
		else
		{
			title = u8"资源更新完成";
		}
		break;
	case ResourceInstallDialogState::Failed:
		if (resourceInstallOperation ==
			ResourceInstallOperation::ProgramDownload)
		{
			title = u8"主程序处理失败";
		}
		else if (resourceInstallOperation ==
			ResourceInstallOperation::ResourceRemoval)
		{
			title = u8"资源删除失败";
		}
		else if (resourceInstallOperation ==
			ResourceInstallOperation::SaveManagement)
		{
			title = u8"存档删除失败";
		}
		else
		{
			title = u8"资源准备失败";
		}
		break;
	case ResourceInstallDialogState::Hidden:
	default:
		return;
	}
	drawCenteredText(title,
		dialog.x + dialog.w / 2,
		dialog.y + 14,
		25, 0xFFFFE7B0, std::max(1, dialog.w - 32), 17);
	engine->fillRect(dialog.x + 16, dialog.y + 50,
		std::max(1, dialog.w - 32), 1, 216, 184, 112, 120);

	if (resourceInstallDialogState == ResourceInstallDialogState::Confirming)
	{
		if (pendingDownloadUsesMeteredNetwork &&
			pendingMeteredDownloadConfirmed &&
			resourceInstallOperation !=
				ResourceInstallOperation::ResourceRemoval)
		{
			drawCenteredText(
				u8"当前正在使用移动网络",
				dialog.x + dialog.w / 2,
				dialog.y + 86,
				20, 0xFFFFB080, dialog.w - 44, 16);
			drawCenteredText(
				u8"继续后将使用 " + formatByteCount(
					pendingResourceInstall.totalDownloadBytes) +
					u8" 移动数据。",
				dialog.x + dialog.w / 2,
				dialog.y + 126,
				17, 0xFFFFFFFF, dialog.w - 44, 14);
			drawCenteredText(
				u8"请再次确认是否继续下载。",
				dialog.x + dialog.w / 2,
				dialog.y + 166,
				17, 0xFFBFE2B4, dialog.w - 44, 14);
			return;
		}
		if (resourceInstallOperation ==
			ResourceInstallOperation::ResourceRemoval)
		{
			drawTextLine(
				u8"以下游戏会按依赖顺序直接删除。请另外选择是否删除相关存档。",
				dialog.x + 22, dialog.y + 62, 16, 0xFFFFFFFF,
				std::max(1, dialog.w - 44), 14);
			int lineY = dialog.y + 92;
			const int maximumVisibleEntries = dialog.h < 360 ? 3 : 6;
			const int visibleCount = std::min(
				static_cast<int>(pendingResourceRemoval.entries.size()),
				maximumVisibleEntries);
			for (int index = 0; index < visibleCount; index++)
			{
				const ResourceManager::ResourceRemovalEntry& entry =
					pendingResourceRemoval.entries[index];
				std::string line = u8"删除：" + entry.name;
				if (!entry.version.empty())
				{
					line += u8"    版本：" + entry.version;
				}
				if (entry.saveExists)
				{
					line += u8"    存档：" + entry.saveNamespace;
				}
				drawTextLine(line,
					dialog.x + 26, lineY, 15, 0xFFFFD39A,
					std::max(1, dialog.w - 52), 13);
				lineY += 24;
			}
			if (visibleCount <
				static_cast<int>(pendingResourceRemoval.entries.size()))
			{
				drawTextLine(
					u8"其余 " + std::to_string(
						pendingResourceRemoval.entries.size() - visibleCount) +
						u8" 个依赖游戏也会删除",
					dialog.x + 26, lineY, 14, 0xFFB9AA87,
					std::max(1, dialog.w - 52), 13);
			}
			drawCenteredText(
				u8"必须手动选择一项，程序不会替你决定是否保留存档。",
				dialog.x + dialog.w / 2,
				getResourceInstallPreviousPageButtonRect().y - 26,
				14, 0xFFBFE2B4, dialog.w - 40, 13);
			return;
		}
		if (resourceInstallOperation ==
			ResourceInstallOperation::ProgramDownload)
		{
			const ResourceInstallConfirmationItem* item =
				pendingResourceInstall.items.empty()
					? nullptr : &pendingResourceInstall.items.front();
			const OnlineUpdate::ProgramUpdateCheck update =
				OnlineUpdate::checkProgramUpdate(
					onlineApplicationCatalog,
					JxqyBuildVersion::ProgramUpdateTarget,
					JxqyBuildVersion::EngineVersion);
			drawTextLine(
				update.hasUpdate()
					? std::string(u8"线上版本高于当前版本；确认后下载并更新主程序。")
					: std::string(
						u8"当前版本不低于线上版本，无需更新；仅在你主动确认时重装线上版本。"),
				dialog.x + 22, dialog.y + 62, 15, 0xFFFFFFFF,
				std::max(1, dialog.w - 44), 14);
			int lineY = dialog.y + 96;
			drawTextLine(
				u8"当前：" + std::string(JxqyBuildVersion::EngineVersion),
				dialog.x + 26, lineY, 14, 0xFFD8C59A,
				std::max(1, dialog.w - 52), 14);
			lineY += 22;
			if (item != nullptr)
			{
				drawTextLine(
					u8"线上：" + valueOrUndeclared(item->version),
					dialog.x + 26, lineY, 14, 0xFFFFD39A,
					std::max(1, dialog.w - 52), 14);
				lineY += 22;
				drawTextLine(
					u8"平台：" + programTargetDisplayName(
						pendingResourceInstall.requestedGameId),
					dialog.x + 26, lineY, 14, 0xFFB9AA87,
					std::max(1, dialog.w - 52), 14);
				lineY += 22;
			}
			drawTextLine(
				u8"下载：" + formatByteCount(
					pendingResourceInstall.totalDownloadBytes),
				dialog.x + 26, lineY, 14, 0xFFB9AA87,
				std::max(1, dialog.w - 52), 14);
			lineY += 24;
			if (pendingDownloadUsesMeteredNetwork)
			{
				drawTextLine(
					u8"当前为移动网络，本次下载会使用 " +
						formatByteCount(
							pendingResourceInstall.totalDownloadBytes) +
						u8" 移动数据。",
					dialog.x + 26, lineY, 14, 0xFFFFB080,
					std::max(1, dialog.w - 52), 14);
				lineY += 22;
			}
			drawTextLine(u8"主要更新：",
				dialog.x + 26, lineY, 14, 0xFFFFD39A,
				std::max(1, dialog.w - 52), 14);
			lineY += 20;
			const int footerY = getResourceInstallPrimaryButtonRect().y - 28;
			drawWrappedDescription(
				item == nullptr || item->releaseNotes.empty()
					? std::string(u8"未提供当前平台的更新说明。")
					: item->releaseNotes,
				{ dialog.x + 38, lineY,
					std::max(1, dialog.w - 64),
					std::max(1, footerY - lineY - 8) },
				14, 0xFFFFFFFF);
			drawTextLine(
				iosProgramUpdatePageAvailable()
					? std::string(
						u8"确认后打开系统浏览器下载页面；游戏不会自行下载或替换主程序。")
					: androidProgramPackageInstallAvailable()
					? std::string(
						u8"下载完成后由 Android 系统确认是否允许安装；当前游戏仍可继续。")
					: std::string(
						u8"下载完成后仍由你决定是否安装；assets 和 save 不会改变。"),
				dialog.x + 26, footerY, 13, 0xFFBFE2B4,
				std::max(1, dialog.w - 52), 12);
			return;
		}
		std::string actionDescription;
		if (!pendingResourceInstall.requestedResourceInstalled)
		{
			actionDescription = u8"将下载此游戏及缺失依赖";
		}
		else if (pendingResourceInstall.requestedDownloadMode ==
			OnlineUpdate::RequestedResourceDownloadMode::ForceFullPackage)
		{
			actionDescription = u8"将重新下载此游戏的当前线上版本";
		}
		else if (pendingResourceInstall.requestedVersionMatches)
		{
			actionDescription = u8"将补齐此游戏及依赖所需的线上制品";
		}
		else
		{
			actionDescription = u8"将更新此游戏及版本不足的依赖";
		}
		if (pendingResourceInstall.includesCommon)
		{
			actionDescription += u8"，并自动更新运行所需文件";
		}
		const std::string summary = actionDescription + u8"；共 " +
			std::to_string(pendingResourceInstall.items.size()) +
			u8" 项、" +
			formatByteCount(pendingResourceInstall.totalDownloadBytes) +
			u8"。下载完成后立即启用并刷新资源列表。";
		drawTextLine(summary,
			dialog.x + 22, dialog.y + 62, 16, 0xFFFFFFFF,
			std::max(1, dialog.w - 44), 14);

		const int pageCount = std::max(1,
			(static_cast<int>(pendingResourceInstall.items.size()) +
				ResourceInstallItemsPerPage - 1) /
				ResourceInstallItemsPerPage);
		const int firstItem = resourceInstallConfirmationPage *
			ResourceInstallItemsPerPage;
		const int endItem = std::min(
			static_cast<int>(pendingResourceInstall.items.size()),
			firstItem + ResourceInstallItemsPerPage);
		int lineY = dialog.y + 90;
		if (pendingDownloadUsesMeteredNetwork)
		{
			drawTextLine(
				u8"当前为移动网络，确认后将使用 " +
					formatByteCount(
						pendingResourceInstall.totalDownloadBytes) +
					u8" 移动数据。",
				dialog.x + 26, lineY, 14, 0xFFFFB080,
				std::max(1, dialog.w - 52), 14);
			lineY += 22;
		}
		for (int itemIndex = firstItem; itemIndex < endItem; itemIndex++)
		{
			const ResourceInstallConfirmationItem& item =
				pendingResourceInstall.items[itemIndex];
			std::string operationPrefix;
			if (item.artifactKind == OnlineUpdate::ResourceDownloadPlan::
				ArtifactKind::Incremental)
			{
				operationPrefix = u8"增量：";
			}
			else if (item.artifactKind == OnlineUpdate::ResourceDownloadPlan::
				ArtifactKind::FullAndIncremental)
			{
				operationPrefix = u8"完整+增量：";
			}
			else
			{
				operationPrefix = item.replacing ? u8"替换：" : u8"新增：";
			}
			const std::string firstLine = operationPrefix +
				item.title + u8"    版本：" + valueOrUndeclared(item.version);
			drawTextLine(firstLine,
				dialog.x + 26, lineY, 15,
				item.replacing ? 0xFFFFD39A : 0xFFBFE2B4,
				std::max(1, dialog.w - 52), 14);
			lineY += 20;
			drawTextLine(
				u8"目录：assets/" + item.targetDirectoryName + "/",
				dialog.x + 38, lineY, 14, 0xFFB9AA87,
				std::max(1, dialog.w - 64), 14);
			lineY += 24;
			drawTextLine(u8"本项主要更新：",
				dialog.x + 38, lineY, 14, 0xFFFFD39A,
				std::max(1, dialog.w - 64), 14);
			lineY += 20;
			const int notesBottom = pageCount > 1
				? getResourceInstallPreviousPageButtonRect().y - 30
				: getResourceInstallPrimaryButtonRect().y - 16;
			drawWrappedDescription(
				item.releaseNotes.empty()
					? std::string(u8"未提供本项更新说明。")
					: item.releaseNotes,
				{ dialog.x + 38, lineY,
					std::max(1, dialog.w - 64),
					std::max(1, notesBottom - lineY) },
				14, 0xFFFFFFFF);
		}
		if (pageCount > 1)
		{
			drawCenteredText(
				u8"第 " + std::to_string(resourceInstallConfirmationPage + 1) +
				u8" / " + std::to_string(pageCount) + u8" 页",
				dialog.x + dialog.w / 2,
				getResourceInstallPreviousPageButtonRect().y - 18,
				14, 0xFFB9AA87, dialog.w - 40, 14);
		}
		return;
	}

	if (resourceInstallDialogState == ResourceInstallDialogState::BrowsingSaves)
	{
		if (saveNamespaceEntries.empty())
		{
			drawCenteredText(
				u8"没有可清理的游戏存档。",
				dialog.x + dialog.w / 2,
				dialog.y + 112,
				18, 0xFFFFFFFF, dialog.w - 48, 14);
			return;
		}
		const ResourceManager::SaveNamespaceInfo& info =
			saveNamespaceEntries[selectedSaveNamespaceIndex];
		const std::string displayName = info.resourceName.empty()
			? std::string(u8"已删除或未识别的游戏") : info.resourceName;
		drawCenteredText(displayName,
			dialog.x + dialog.w / 2, dialog.y + 76,
			22, 0xFFFFE7B0, dialog.w - 48, 14);
		drawCenteredText(
			u8"存档目录：" + info.saveNamespace,
			dialog.x + dialog.w / 2, dialog.y + 116,
			16, 0xFFD8C59A, dialog.w - 48, 14);
		drawCenteredText(
			u8"存档槽：" + std::to_string(info.saveSlotCount) +
				u8"    占用：" +
				(info.bytes == std::numeric_limits<std::uint64_t>::max()
					? std::string(u8"大小未知")
					: formatByteCount(info.bytes)),
			dialog.x + dialog.w / 2, dialog.y + 148,
			16, 0xFFB9AA87, dialog.w - 48, 14);
		drawCenteredText(
			u8"第 " + std::to_string(selectedSaveNamespaceIndex + 1) +
				u8" / " + std::to_string(saveNamespaceEntries.size()) + u8" 项",
			dialog.x + dialog.w / 2,
			getResourceInstallPreviousPageButtonRect().y - 24,
			14, 0xFFB9AA87, dialog.w - 40, 13);
		return;
	}

	if (resourceInstallDialogState ==
		ResourceInstallDialogState::ConfirmingSaveRemoval)
	{
		if (selectedSaveNamespaceIndex >= 0 &&
			selectedSaveNamespaceIndex <
				static_cast<int>(saveNamespaceEntries.size()))
		{
			const ResourceManager::SaveNamespaceInfo& info =
				saveNamespaceEntries[selectedSaveNamespaceIndex];
			drawWrappedDescription(
				u8"将永久删除存档目录“" + info.saveNamespace +
					u8"”。资源文件、其他游戏存档和全局设置不会被删除。",
				{ dialog.x + 32, dialog.y + 88,
					std::max(1, dialog.w - 64), 120 },
				18, 0xFFFFB0A0);
		}
		return;
	}

	if (resourceInstallDialogState == ResourceInstallDialogState::Downloading ||
		resourceInstallDialogState == ResourceInstallDialogState::Cancelling)
	{
		std::uint64_t completedBytes = 0;
		std::uint64_t totalBytes = pendingResourceInstall.totalDownloadBytes;
		std::size_t packageIndex = 0;
		std::size_t packageCount = pendingResourceInstall.items.size();
		ResourceInstallProgressStage progressStage =
			ResourceInstallProgressStage::Downloading;
		if (resourceInstallWorkerResult != nullptr)
		{
			completedBytes = resourceInstallWorkerResult->completedBytes.load(
				std::memory_order_acquire);
			totalBytes = resourceInstallWorkerResult->totalBytes;
			packageIndex = resourceInstallWorkerResult->packageIndex.load(
				std::memory_order_acquire);
			packageCount = resourceInstallWorkerResult->packageCount;
			progressStage = resourceInstallWorkerResult->progressStage.load(
				std::memory_order_acquire);
		}
		completedBytes = std::min(completedBytes, totalBytes);
		std::string progressMessage = resourceInstallDialogMessage;
		if (resourceInstallDialogState ==
				ResourceInstallDialogState::Downloading)
		{
			if (progressStage ==
				ResourceInstallProgressStage::ValidatingAndExtracting)
			{
				progressMessage = resourceInstallOperation ==
						ResourceInstallOperation::ProgramDownload
					? std::string(u8"下载完成，正在校验并解压主程序…")
					: std::string(u8"下载完成，正在校验并解压当前资源…");
			}
			else if (progressStage == ResourceInstallProgressStage::Staging)
			{
				progressMessage = u8"解压完成，正在启用并刷新资源列表…";
			}
		}
		drawCenteredText(progressMessage,
			dialog.x + dialog.w / 2, dialog.y + 88,
			18, 0xFFFFFFFF, dialog.w - 48, 14);
		const std::string progressText =
			formatByteCount(completedBytes) + " / " + formatByteCount(totalBytes) +
			(resourceInstallOperation ==
					ResourceInstallOperation::ProgramDownload
				? std::string(u8"    程序包 ") : std::string(u8"    资源 ")) +
			std::to_string(std::min(packageIndex + 1, packageCount)) +
			" / " + std::to_string(packageCount);
		drawCenteredText(progressText,
			dialog.x + dialog.w / 2, dialog.y + 126,
			16, 0xFFD8C59A, dialog.w - 48, 14);
		const Rect bar =
		{
			dialog.x + 32,
			dialog.y + 164,
			std::max(1, dialog.w - 64),
			18
		};
		engine->fillRect(bar.x, bar.y, bar.w, bar.h, 66, 52, 40, 255);
		const int filledWidth = totalBytes == 0 ? 0 : static_cast<int>(
			static_cast<long double>(completedBytes) * bar.w / totalBytes);
		if (filledWidth > 0)
		{
			engine->fillRect(bar.x, bar.y,
				std::min(bar.w, filledWidth), bar.h,
				210, 170, 88, 255);
		}
		return;
	}

	const Rect messageArea =
	{
		dialog.x + 32,
		dialog.y + 84,
		std::max(1, dialog.w - 64),
		std::max(1,
			getResourceInstallPrimaryButtonRect().y - dialog.y - 104)
	};
	drawWrappedDescription(
		resourceInstallDialogMessage,
		messageArea,
		17,
		resourceInstallDialogState == ResourceInstallDialogState::Failed
			? 0xFFFFB0A0 : 0xFFFFFFFF);
}

void ResourceSelectScene::drawResourceDetails()
{
	if (detailArea.w <= 0 || detailArea.h <= 0
		|| selectedDetails.packIndex < 0)
	{
		return;
	}

	engine->fillRect(detailArea.x - 1, detailArea.y - 1,
		detailArea.w + 2, detailArea.h + 2, 152, 126, 78, 120);
	engine->fillRect(detailArea.x, detailArea.y,
		detailArea.w, detailArea.h, 24, 20, 18, 226);
	drawCover();

	const int detailPadding = wideDetailLayout ? 12 : 8;
	const int textX = coverArea.x + coverArea.w
		+ (wideDetailLayout ? 12 : 10);
	const int metadataTextRight =
		detailArea.x + detailArea.w - detailPadding;
	int titleTextRight = metadataTextRight;
	const std::array<std::shared_ptr<FlatTextButton>, 2> detailButtons =
	{
		onlineActionButton,
		resourceRemoveButton
	};
	for (const auto& button : detailButtons)
	{
		if (button != nullptr && button->visible)
		{
			titleTextRight = std::min(titleTextRight, button->rect.x - 8);
		}
	}
	const int titleTextWidth = std::max(1, titleTextRight - textX);
	const int metadataTextWidth = std::max(1, metadataTextRight - textX);
	const std::string author = selectedDetails.author.empty()
		? std::string(u8"作者：未声明") : selectedDetails.author;
	std::string versionComparison;
	if (selectedDetails.onlineOnly)
	{
		versionComparison = u8"线上版本：" + selectedDetails.onlineVersion +
			u8"    当前未安装";
	}
	else if (selectedDetails.onlineAvailable)
	{
		versionComparison = u8"当前版本：" + selectedDetails.version +
			u8"    线上版本：" + selectedDetails.onlineVersion;
	}
	else
	{
		versionComparison = u8"当前版本：" + selectedDetails.version;
	}
	const std::string releaseMetadata =
		u8"ID：" + selectedDetails.resourceId
		+ u8"    发布：" + selectedDetails.releaseDate
		+ (selectedDetails.wasRecentlySelected
			? std::string(u8"    上次选择") : std::string());
	const std::string statusMetadata =
		u8"状态：" + selectedDetails.runStatus;

	if (!wideDetailLayout)
	{
		int lineY = detailArea.y + detailPadding;
		drawTextLine(
			u8"简介：" + singleLineText(selectedDetails.description),
			textX, lineY, 15, 0xFFFFF2C8, titleTextWidth, 14);
		lineY += 20;
		if (detailArea.h < 60)
		{
			drawTextLine(statusMetadata,
				textX, lineY, 14, 0xFFAAA28E, metadataTextWidth, 14);
			return;
		}
		drawTextLine(releaseMetadata,
			textX, lineY, 14, 0xFFC4B48E, titleTextWidth, 14);
		lineY += 19;
		drawTextLine(statusMetadata,
			textX, lineY, 14, 0xFFAAA28E, metadataTextWidth, 14);
		return;
	}

	int lineY = detailArea.y + detailPadding;
	drawTextLine(selectedDetails.name,
		textX, lineY, 20, 0xFFFFF2C8, titleTextWidth, 13);
	lineY += 24;
	for (const auto& button : detailButtons)
	{
		if (button != nullptr && button->visible)
		{
			lineY = std::max(
				lineY,
				button->rect.y + button->rect.h + 6);
		}
	}
	drawTextLine(author,
		textX, lineY, 15, 0xFFD8C59A, metadataTextWidth, 14);
	lineY += 20;
	drawTextLine(versionComparison,
		textX, lineY, 15, 0xFFD8C59A, metadataTextWidth, 14);
	lineY += 20;
	drawTextLine(releaseMetadata,
		textX, lineY, 14, 0xFFC4B48E, metadataTextWidth, 14);
	lineY += 19;
	drawTextLine(statusMetadata,
		textX, lineY, 14, 0xFFAAA28E, metadataTextWidth, 14);
	lineY += 19;

	const int descriptionY = std::max(
		coverArea.y + coverArea.h + 8, lineY);
	const Rect descriptionArea =
	{
		detailArea.x + detailPadding,
		descriptionY,
		std::max(1, detailArea.w - detailPadding * 2),
		std::max(0,
			detailArea.y + detailArea.h - detailPadding - descriptionY)
	};
	drawWrappedDescription(
		u8"简介：" + selectedDetails.description,
		descriptionArea, 15, 0xFFB9AA87);
}

void ResourceSelectScene::drawCover()
{
	if (coverArea.w <= 0 || coverArea.h <= 0)
	{
		return;
	}

	engine->fillRect(coverArea.x - 1, coverArea.y - 1,
		coverArea.w + 2, coverArea.h + 2, 176, 145, 88, 180);
	engine->fillRect(coverArea.x, coverArea.y,
		coverArea.w, coverArea.h, 38, 32, 28, 255);

	int imageWidth = 0;
	int imageHeight = 0;
	if (detailCoverImage != nullptr
		&& engine->getImageSize(
			detailCoverImage, imageWidth, imageHeight)
		&& imageWidth > 0 && imageHeight > 0)
	{
		int drawWidth = coverArea.w;
		int drawHeight = static_cast<int>(
			static_cast<long long>(imageHeight) * drawWidth / imageWidth);
		if (drawHeight > coverArea.h)
		{
			drawHeight = coverArea.h;
			drawWidth = static_cast<int>(
				static_cast<long long>(imageWidth) * drawHeight / imageHeight);
		}
		Rect target =
		{
			coverArea.x + (coverArea.w - drawWidth) / 2,
			coverArea.y + (coverArea.h - drawHeight) / 2,
			std::max(1, drawWidth),
			std::max(1, drawHeight)
		};
		engine->drawImage(detailCoverImage, nullptr, &target);
		return;
	}

	drawCenteredText(u8"暂无封面",
		coverArea.x + coverArea.w / 2,
		coverArea.y + std::max(0, (coverArea.h - 14) / 2),
		14, 0xFF9B927F, std::max(1, coverArea.w - 8), 10);
}

void ResourceSelectScene::drawTextLine(
	const std::string& text, int x, int y, int fontSize,
	unsigned int color, int maxWidth, int minimumFontSize)
{
	const CachedTextImage* cached = getCachedTextImage(
		text, fontSize, color, maxWidth, minimumFontSize);
	if (cached != nullptr)
	{
		engine->drawImage(cached->image, x, y);
	}
}

void ResourceSelectScene::drawWrappedDescription(
	const std::string& text, const Rect& target,
	int fontSize, unsigned int color)
{
	if (text.empty() || target.w <= 0 || target.h <= 0)
	{
		return;
	}

	const int lineGap = 2;
	const int lineStep = fontSize + lineGap;
	const int maximumLines = std::max(0, target.h / lineStep);
	if (maximumLines == 0)
	{
		return;
	}
	std::vector<std::string> lines = TextLayout::wrapUtf8Text(
		text, TextLayout::charactersPerLineForWidth(target.w, fontSize));
	const bool truncated =
		static_cast<int>(lines.size()) > maximumLines;
	if (static_cast<int>(lines.size()) > maximumLines)
	{
		lines.resize(static_cast<std::size_t>(maximumLines));
	}
	if (truncated && !lines.empty())
	{
		lines.back() += u8"…";
	}
	for (int lineIndex = 0;
		lineIndex < static_cast<int>(lines.size());
		lineIndex++)
	{
		drawTextLine(lines[lineIndex],
			target.x, target.y + lineIndex * lineStep,
			fontSize, color, target.w, std::max(9, fontSize - 2));
	}
}

const ResourceSelectScene::CachedTextImage* ResourceSelectScene::getCachedTextImage(
	const std::string& text, int fontSize, unsigned int color, int maxWidth, int minimumFontSize)
{
	if (text.empty())
	{
		return nullptr;
	}

	int drawSize = fontSize;
	_shared_image image = nullptr;
	int textWidth = 0;
	int textHeight = 0;
	minimumFontSize = std::max(1, std::min(fontSize, minimumFontSize));
	const std::string cacheKey = text + "\n" + std::to_string(fontSize) + "\n" +
		std::to_string(color) + "\n" + std::to_string(maxWidth) + "\n" +
		std::to_string(minimumFontSize);
	const auto cacheIterator = textCache.find(cacheKey);
	if (cacheIterator != textCache.end())
	{
		return cacheIterator->second.image != nullptr ? &cacheIterator->second : nullptr;
	}

	while (true)
	{
		image = engine->createText(text, drawSize, color);
		if (image == nullptr || !engine->getImageSize(image, textWidth, textHeight))
		{
			return nullptr;
		}
		if (maxWidth <= 0 || textWidth <= maxWidth || drawSize <= minimumFontSize)
		{
			break;
		}
		drawSize = std::max(minimumFontSize, drawSize - 2);
	}
	if (maxWidth > 0 && textWidth > maxWidth)
	{
		std::vector<size_t> characterEnds;
		for (size_t offset = 0; offset < text.size();)
		{
			offset++;
			while (offset < text.size()
				&& (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80)
			{
				offset++;
			}
			characterEnds.push_back(offset);
		}

		const std::string ellipsis = u8"…";
		_shared_image fittedImage = nullptr;
		int fittedWidth = 0;
		int fittedHeight = 0;
		int low = 0;
		int high = static_cast<int>(characterEnds.size());
		while (low <= high)
		{
			const int characterCount = low + (high - low) / 2;
			const size_t prefixLength = characterCount > 0 ? characterEnds[characterCount - 1] : 0;
			const std::string candidate = text.substr(0, prefixLength) + ellipsis;
			_shared_image candidateImage = engine->createText(candidate, drawSize, color);
			int candidateWidth = 0;
			int candidateHeight = 0;
			if (candidateImage == nullptr
				|| !engine->getImageSize(candidateImage, candidateWidth, candidateHeight))
			{
				return nullptr;
			}
			if (candidateWidth <= maxWidth)
			{
				fittedImage = candidateImage;
				fittedWidth = candidateWidth;
				fittedHeight = candidateHeight;
				low = characterCount + 1;
			}
			else
			{
				high = characterCount - 1;
			}
		}
		if (fittedImage == nullptr)
		{
			return nullptr;
		}
		image = fittedImage;
		textWidth = fittedWidth;
		textHeight = fittedHeight;
	}

	CachedTextImage cached;
	cached.image = image;
	cached.width = textWidth;
	cached.height = textHeight;
	const auto inserted = textCache.emplace(cacheKey, cached);
	return &inserted.first->second;
}

void ResourceSelectScene::drawCenteredText(const std::string& text, int centerX, int y,
	int fontSize, unsigned int color, int maxWidth, int minimumFontSize)
{
	const CachedTextImage* cached = getCachedTextImage(
		text, fontSize, color, maxWidth, minimumFontSize);
	if (cached != nullptr)
	{
		engine->drawImage(cached->image, centerX - cached->width / 2, y);
	}
}

void ResourceSelectScene::onChildCallBack(PElement child)
{
	if (child == nullptr)
	{
		return;
	}
	const unsigned int childResult = child->getResult();
	if ((childResult & erClick) == 0)
	{
		return;
	}
	if (child == exitButton)
	{
		stop(erExit);
		return;
	}
	if (child == cheatHelpButton)
	{
		showCheatHelp(false);
		return;
	}
	if (child == checkUpdatesButton)
	{
		activateCheckUpdatesButton();
		return;
	}
	if (child == programActionButton)
	{
		activateProgramActionButton();
		return;
	}
	if (child == onlineActionButton)
	{
		beginResourceDownloadConfirmation();
		return;
	}
	if (child == resourceRemoveButton)
	{
		beginResourceRemovalConfirmation();
		return;
	}
	if (child == saveManagementButton)
	{
		beginSaveManagement();
		return;
	}
	if (child == displaySettingsButton)
	{
		showDisplaySettings(false);
		return;
	}
	for (int row = 0; row < DisplaySettingsRowCount; row++)
	{
		if (child == displaySettingsPreviousButtons[row])
		{
			cycleDisplaySetting(row, -1);
			return;
		}
		if (child == displaySettingsNextButtons[row])
		{
			cycleDisplaySetting(row, 1);
			return;
		}
	}
	if (child == displaySettingsApplyButton)
	{
		applyPendingDisplaySettings();
		return;
	}
	if (child == displaySettingsDefaultButton)
	{
		resetPendingDisplaySettings();
		return;
	}
	if (child == displaySettingsBackButton)
	{
		hideDisplaySettings(false);
		return;
	}
	if (child == cheatHelpCloseButton)
	{
		hideCheatHelp(false);
		return;
	}
	if (child == externalResourceConfirmButton)
	{
		confirmExternalResourceDialog();
		return;
	}
	if (child == externalResourceCancelButton)
	{
		hideExternalResourceDialog(false);
		return;
	}
	if (child == resourceInstallPrimaryButton)
	{
		activateResourceDialogPrimary();
		return;
	}
	if (child == resourceInstallSecondaryButton)
	{
		activateResourceDialogSecondary();
		return;
	}
	if (child == resourceInstallPreviousPageButton)
	{
		moveResourceInstallConfirmationPage(-1);
		return;
	}
	if (child == resourceInstallNextPageButton)
	{
		moveResourceInstallConfirmationPage(1);
		return;
	}
	if (child == resourceList)
	{
		confirmSelection();
		return;
	}
	if (child == enableExternalButton)
	{
		showExternalResourceDialog(false);
		return;
	}
	for (int linkIndex = 0; linkIndex < static_cast<int>(externalLinkButtons.size()); linkIndex++)
	{
		if (child == externalLinkButtons[linkIndex])
		{
			openExternalLink(linkIndex);
			return;
		}
	}
}

bool ResourceSelectScene::onHandleEvent(AEvent& event)
{
	if (isPointerTakeoverEvent(event))
	{
		hideSemanticFocus();
	}
	// 零包时不直接退出场景：用户仍可检查线上资源或主程序版本。
	// 只有窗口关闭/退出请求才结束场景。
	if (event.eventType == ET_QUIT || event.eventType == ET_WINDOWCLOSE)
	{
		if (resourceInstallRunner != nullptr)
		{
			resourceInstallRunner->requestCancellation();
		}
		stop(erExit);
		return true;
	}

	if (resourceList != nullptr)
	{
		dispatchingKeyboardUIAction = event.eventType == ET_KEYDOWN;
		const bool keyboardActionHandled = dispatchKeyboardUIAction(event, *this);
		dispatchingKeyboardUIAction = false;
		if (keyboardActionHandled)
		{
			return true;
		}
	}
	return cheatHelpVisible || externalResourceDialogVisible ||
		displaySettingsVisible ||
		resourceInstallDialogState != ResourceInstallDialogState::Hidden;
}

bool ResourceSelectScene::onHandleUIAction(UIAction action)
{
	keyboardSemanticFocus = dispatchingKeyboardUIAction;
	if (displaySettingsVisible)
	{
		if (action == UIAction::Cancel)
		{
			hideDisplaySettings(true);
			return true;
		}
		if (!restoreSemanticFocus())
		{
			return false;
		}
		const bool handled = focusManager.handleAction(action);
		updateFocusPresentation();
		return handled;
	}
	if (cheatHelpVisible)
	{
		if (action == UIAction::Confirm || action == UIAction::Cancel)
		{
			hideCheatHelp(true);
		}
		return true;
	}
	if (externalResourceDialogVisible)
	{
		if (action == UIAction::Cancel)
		{
			hideExternalResourceDialog(true);
			return true;
		}
		if (!restoreSemanticFocus())
		{
			return false;
		}
		const bool handled = focusManager.handleAction(action);
		updateFocusPresentation();
		return handled;
	}
	if (resourceInstallDialogState != ResourceInstallDialogState::Hidden)
	{
		if (resourceInstallDialogState ==
				ResourceInstallDialogState::Confirming ||
			resourceInstallDialogState ==
				ResourceInstallDialogState::BrowsingSaves)
		{
			if (action == UIAction::PagePrevious ||
				action == UIAction::PanelPrevious)
			{
				moveResourceInstallConfirmationPage(-1);
				return true;
			}
			if (action == UIAction::PageNext ||
				action == UIAction::PanelNext)
			{
				moveResourceInstallConfirmationPage(1);
				return true;
			}
		}
		if (action == UIAction::Cancel)
		{
			if (resourceInstallDialogState ==
					ResourceInstallDialogState::Confirming &&
				resourceInstallOperation ==
					ResourceInstallOperation::OnlineDownload &&
				resourceUpdatePromptedByEntry &&
				!(pendingDownloadUsesMeteredNetwork &&
					pendingMeteredDownloadConfirmed))
			{
				dismissResourceInstallDialog();
				return true;
			}
			activateResourceDialogSecondary();
			return true;
		}
		if (!restoreSemanticFocus())
		{
			return false;
		}
		const bool handled = focusManager.handleAction(action);
		updateFocusPresentation();
		return handled;
	}
	if (!restoreSemanticFocus())
	{
		return false;
	}
	const bool handled = focusManager.handleAction(action);
	updateFocusPresentation();
	return handled;
}

void ResourceSelectScene::onWindowResize(int width, int height)
{
	rect.w = width;
	rect.h = height;
	textCache.clear();
	if (displaySettingsVisible && engine != nullptr &&
		pendingDisplaySettings.fullScreenMode == FullScreenMode::window)
	{
		const DesktopDisplaySettings actualSettings =
			engine->getDesktopDisplaySettings();
		if (actualSettings.fullScreenMode == FullScreenMode::window)
		{
			pendingDisplaySettings.width = actualSettings.width;
			pendingDisplaySettings.height = actualSettings.height;
			rebuildDisplayResolutionOptions();
		}
	}
	updateLayout(width, height);
}

void ResourceSelectScene::onExit()
{
	freeResource();
}
