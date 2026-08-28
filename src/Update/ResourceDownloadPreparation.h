#pragma once

#include "HttpsDownload.h"
#include "ResourceDownloadPlanner.h"
#include "ResourcePackageArchive.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace OnlineUpdate
{
enum class ResourceDownloadPreparationStatus
{
	Success,
	InvalidInput,
	PlanFailed,
	InvalidArtifactUrl,
	WorkspaceAlreadyExists,
	WorkspaceUnavailable,
	Cancelled,
	DownloadFailed,
	ProgramNotAvailable,
	ArtifactValidationFailed,
	PackageValidationFailed,
	CleanupFailed
};

struct ResourceDownloadPreparationProgress
{
	enum class Stage
	{
		Downloading,
		ValidatingAndExtracting
	};

	std::size_t packageIndex = 0;
	std::size_t packageCount = 0;
	std::string gameId;
	std::uint64_t packageTransferredBytes = 0;
	std::uint64_t completedBytes = 0;
	std::uint64_t totalBytes = 0;
	Stage stage = Stage::Downloading;
};

using ResourceDownloadPreparationProgressCallback =
	std::function<bool(const ResourceDownloadPreparationProgress& progress)>;

using ResourceArtifactDownloadFunction = std::function<HttpsDownloadResult(
	const std::string& url,
	const std::filesystem::path& destinationPath,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const HttpsDownloadProgress& progress)>;

using InstalledResourceRootMap =
	std::map<std::string, std::filesystem::path>;

struct PreparedResourceDownload
{
	ResourcePackage package;
	ResourceDownloadPlan::ArtifactKind artifactKind =
		ResourceDownloadPlan::ArtifactKind::Full;
	std::filesystem::path archivePath;
	std::filesystem::path preparedResourcePath;
};

struct ResourceDownloadPreparationResult
{
	ResourceDownloadPreparationStatus status =
		ResourceDownloadPreparationStatus::InvalidInput;
	ResourcePlanStatus planStatus = ResourcePlanStatus::Ready;
	std::string failedGameId;
	std::filesystem::path workspacePath;
	HttpsDownloadResult downloadResult;
	ResourcePackageArchiveResult packageResult;
	std::vector<PreparedResourceDownload> preparedResources;
	std::uint64_t totalDownloadBytes = 0;

	bool succeeded() const noexcept
	{
		return status == ResourceDownloadPreparationStatus::Success;
	}
};

struct CommonDownloadPreparationResult
{
	ResourceDownloadPreparationStatus status =
		ResourceDownloadPreparationStatus::InvalidInput;
	std::filesystem::path workspacePath;
	HttpsDownloadResult downloadResult;
	ResourcePackageArchiveResult packageResult;
	CommonPackage package;
	std::filesystem::path archivePath;
	std::filesystem::path preparedCommonPath;

	bool succeeded() const noexcept
	{
		return status == ResourceDownloadPreparationStatus::Success;
	}
};

struct ProgramDownloadPreparationResult
{
	ResourceDownloadPreparationStatus status =
		ResourceDownloadPreparationStatus::InvalidInput;
	ProgramUpdateStatus updateStatus = ProgramUpdateStatus::InvalidInput;
	std::filesystem::path workspacePath;
	std::filesystem::path artifactPath;
	HttpsDownloadResult downloadResult;
	ProgramPackage package;

	bool succeeded() const noexcept
	{
		return status == ResourceDownloadPreparationStatus::Success;
	}
};

// Downloads and validates the needed members of one online dependency closure
// into a new private workspace. The workspace must not exist. Any failure
// removes only the workspace created by this call; installed resources are
// never modified.
ResourceDownloadPreparationResult prepareResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion,
	const std::string& catalogUrl,
	const std::filesystem::path& workspacePath,
	const InstalledResourceArtifactMap& installedArtifacts,
	const InstalledResourceRootMap& installedResourceRoots,
	RequestedResourceDownloadMode requestedMode,
	const ResourceDownloadPreparationProgressCallback& progress = {},
	const ResourceArtifactDownloadFunction& artifactDownloader = {});

// Downloads and validates the optional catalog [Common] package into a new
// private workspace. It never replaces the installed assets/common directory.
CommonDownloadPreparationResult prepareCommonDownload(
	const Catalog& catalog,
	const std::string& catalogUrl,
	const std::filesystem::path& workspacePath,
	const ResourceDownloadPreparationProgressCallback& progress = {},
	const ResourceArtifactDownloadFunction& artifactDownloader = {});

// Downloads the selected platform's sole online program artifact into a new
// private workspace. The public semantic Version is comparison information
// only and never selects a different online artifact. This validates catalog
// size and CRC32 but never starts an installer or changes the current program
// directory.
ProgramDownloadPreparationResult prepareProgramDownload(
	const Catalog& catalog,
	const std::string& target,
	const std::string& currentVersion,
	const std::string& catalogUrl,
	const std::filesystem::path& workspacePath,
	const HttpsDownloadProgress& progress = {},
	const ResourceArtifactDownloadFunction& artifactDownloader = {});
}
