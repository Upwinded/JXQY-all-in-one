#include "ResourceDownloadPreparation.h"

#include "ArtifactChecksum.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace
{
bool removeOwnedWorkspace(const std::filesystem::path& workspacePath)
{
	std::error_code error;
	std::filesystem::remove_all(workspacePath, error);
	if (error)
	{
		return false;
	}
	return !std::filesystem::exists(workspacePath, error) && !error;
}

OnlineUpdate::ResourceDownloadPreparationResult cleanupFailure(
	OnlineUpdate::ResourceDownloadPreparationResult result)
{
	result.preparedResources.clear();
	if (!removeOwnedWorkspace(result.workspacePath))
	{
		result.status =
			OnlineUpdate::ResourceDownloadPreparationStatus::CleanupFailed;
	}
	return result;
}

OnlineUpdate::CommonDownloadPreparationResult cleanupFailure(
	OnlineUpdate::CommonDownloadPreparationResult result)
{
	if (!removeOwnedWorkspace(result.workspacePath))
	{
		result.status =
			OnlineUpdate::ResourceDownloadPreparationStatus::CleanupFailed;
	}
	return result;
}

OnlineUpdate::ProgramDownloadPreparationResult cleanupFailure(
	OnlineUpdate::ProgramDownloadPreparationResult result)
{
	if (!removeOwnedWorkspace(result.workspacePath))
	{
		result.status =
			OnlineUpdate::ResourceDownloadPreparationStatus::CleanupFailed;
	}
	return result;
}
}

namespace OnlineUpdate
{
ResourceDownloadPreparationResult prepareResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion,
	const std::string& catalogUrl,
	const std::filesystem::path& workspacePath,
	const ResourceDownloadPreparationProgressCallback& progress,
	const ResourceArtifactDownloadFunction& artifactDownloader)
{
	ResourceDownloadPreparationResult result;
	result.workspacePath = workspacePath;
	if (requestedGameId.empty() || currentEngineVersion.empty() ||
		catalogUrl.empty() || workspacePath.empty())
	{
		result.status = ResourceDownloadPreparationStatus::InvalidInput;
		return result;
	}

	const ResourceDownloadPlan plan = planResourceDownload(
		catalog, requestedGameId, currentEngineVersion);
	result.planStatus = plan.status;
	result.failedGameId = plan.blockingGameId;
	result.totalDownloadBytes = plan.totalDownloadBytes;
	if (!plan.succeeded())
	{
		result.status = ResourceDownloadPreparationStatus::PlanFailed;
		return result;
	}

	std::vector<std::string> artifactUrls;
	artifactUrls.reserve(plan.downloadOrder.size());
	for (const ResourcePackage* package : plan.downloadOrder)
	{
		std::string artifactUrl;
		if (package == nullptr || !isValidOnlineGameId(package->gameId))
		{
			result.status = ResourceDownloadPreparationStatus::InvalidInput;
			result.failedGameId = package == nullptr
				? std::string() : package->gameId;
			return result;
		}
		if (!buildHttpsArtifactUrl(
				catalogUrl, package->artifactPath, artifactUrl))
		{
			result.status =
				ResourceDownloadPreparationStatus::InvalidArtifactUrl;
			result.failedGameId = package == nullptr
				? std::string() : package->gameId;
			return result;
		}
		artifactUrls.push_back(std::move(artifactUrl));
	}

	std::error_code error;
	if (std::filesystem::exists(workspacePath, error))
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceAlreadyExists;
		return result;
	}
	if (error || !std::filesystem::create_directory(workspacePath, error) || error)
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceUnavailable;
		return result;
	}
	const std::filesystem::path archivesPath = workspacePath / "archives";
	const std::filesystem::path preparedPath = workspacePath / "prepared";
	if (!std::filesystem::create_directory(archivesPath, error) || error ||
		!std::filesystem::create_directory(preparedPath, error) || error)
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceUnavailable;
		return cleanupFailure(std::move(result));
	}

	const ResourceArtifactDownloadFunction download = artifactDownloader
		? artifactDownloader
		: ResourceArtifactDownloadFunction(
			[](const std::string& url,
				const std::filesystem::path& destinationPath,
				std::uint64_t maximumBytes,
				std::uint64_t expectedBytes,
				const HttpsDownloadProgress& downloadProgress)
			{
				return downloadHttpsToNewFile(
					url,
					destinationPath,
					maximumBytes,
					expectedBytes,
					downloadProgress);
			});

	std::uint64_t completedBytes = 0;
	result.preparedResources.reserve(plan.downloadOrder.size());
	for (std::size_t packageIndex = 0;
		packageIndex < plan.downloadOrder.size(); packageIndex++)
	{
		const ResourcePackage& package = *plan.downloadOrder[packageIndex];
		result.failedGameId = package.gameId;
		ResourceDownloadPreparationProgress currentProgress;
		currentProgress.packageIndex = packageIndex;
		currentProgress.packageCount = plan.downloadOrder.size();
		currentProgress.gameId = package.gameId;
		currentProgress.completedBytes = completedBytes;
		currentProgress.totalBytes = plan.totalDownloadBytes;
		bool continueDownload = true;
		try
		{
			continueDownload = !progress || progress(currentProgress);
		}
		catch (...)
		{
			continueDownload = false;
		}
		if (!continueDownload)
		{
			result.status = ResourceDownloadPreparationStatus::Cancelled;
			return cleanupFailure(std::move(result));
		}

		const std::string fileStem =
			"package-" + std::to_string(packageIndex);
		const std::filesystem::path archivePath =
			archivesPath / (fileStem + ".zip");
		const std::filesystem::path packagePreparedPath =
			preparedPath / fileStem;
		try
		{
			result.downloadResult = download(
				artifactUrls[packageIndex],
				archivePath,
				package.artifactSize,
				package.artifactSize,
				[&progress,
					packageIndex,
					packageCount = plan.downloadOrder.size(),
					gameId = package.gameId,
					completedBytes,
					totalBytes = plan.totalDownloadBytes](
						std::uint64_t transferredBytes,
						std::uint64_t)
				{
					if (!progress)
					{
						return true;
					}
					ResourceDownloadPreparationProgress update;
					update.packageIndex = packageIndex;
					update.packageCount = packageCount;
					update.gameId = gameId;
					update.packageTransferredBytes = transferredBytes;
					update.completedBytes = completedBytes + std::min(
						transferredBytes,
						totalBytes - completedBytes);
					update.totalBytes = totalBytes;
					try
					{
						return progress(update);
					}
					catch (...)
					{
						return false;
					}
				});
		}
		catch (...)
		{
			result.downloadResult.status =
				HttpsDownloadStatus::NetworkError;
		}
		if (!result.downloadResult.succeeded())
		{
			result.status = result.downloadResult.status ==
				HttpsDownloadStatus::Cancelled
				? ResourceDownloadPreparationStatus::Cancelled
				: ResourceDownloadPreparationStatus::DownloadFailed;
			return cleanupFailure(std::move(result));
		}

		currentProgress.packageTransferredBytes = package.artifactSize;
		currentProgress.completedBytes = completedBytes + package.artifactSize;
		currentProgress.stage =
			ResourceDownloadPreparationProgress::Stage::
				ValidatingAndExtracting;
		try
		{
			continueDownload = !progress || progress(currentProgress);
		}
		catch (...)
		{
			continueDownload = false;
		}
		if (!continueDownload)
		{
			result.status = ResourceDownloadPreparationStatus::Cancelled;
			return cleanupFailure(std::move(result));
		}

		result.packageResult = prepareResourcePackageArchive(
			package, archivePath, packagePreparedPath);
		if (!result.packageResult.succeeded())
		{
			result.status =
				ResourceDownloadPreparationStatus::PackageValidationFailed;
			return cleanupFailure(std::move(result));
		}

		PreparedResourceDownload prepared;
		prepared.package = package;
		prepared.archivePath = archivePath;
		prepared.preparedResourcePath = packagePreparedPath;
		result.preparedResources.push_back(std::move(prepared));
		completedBytes += package.artifactSize;
	}

	result.failedGameId.clear();
	result.status = ResourceDownloadPreparationStatus::Success;
	return result;
}

CommonDownloadPreparationResult prepareCommonDownload(
	const Catalog& catalog,
	const std::string& catalogUrl,
	const std::filesystem::path& workspacePath,
	const ResourceDownloadPreparationProgressCallback& progress,
	const ResourceArtifactDownloadFunction& artifactDownloader)
{
	CommonDownloadPreparationResult result;
	result.workspacePath = workspacePath;
	if (!catalog.commonPackage.has_value() || catalogUrl.empty() ||
		workspacePath.empty())
	{
		result.status = ResourceDownloadPreparationStatus::InvalidInput;
		return result;
	}
	result.package = *catalog.commonPackage;
	std::string artifactUrl;
	if (!buildHttpsArtifactUrl(
			catalogUrl, result.package.artifactPath, artifactUrl))
	{
		result.status =
			ResourceDownloadPreparationStatus::InvalidArtifactUrl;
		return result;
	}

	std::error_code error;
	if (std::filesystem::exists(workspacePath, error))
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceAlreadyExists;
		return result;
	}
	if (error || !std::filesystem::create_directory(workspacePath, error) ||
		error)
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceUnavailable;
		return result;
	}
	const std::filesystem::path archivesPath = workspacePath / "archives";
	const std::filesystem::path preparedPath = workspacePath / "prepared";
	if (!std::filesystem::create_directory(archivesPath, error) || error ||
		!std::filesystem::create_directory(preparedPath, error) || error)
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceUnavailable;
		return cleanupFailure(std::move(result));
	}
	result.archivePath = archivesPath / "common.zip";
	result.preparedCommonPath = preparedPath / "package-0";

	ResourceDownloadPreparationProgress initial;
	initial.packageCount = 1;
	initial.gameId = "common";
	initial.totalBytes = result.package.artifactSize;
	bool continueDownload = true;
	try
	{
		continueDownload = !progress || progress(initial);
	}
	catch (...)
	{
		continueDownload = false;
	}
	if (!continueDownload)
	{
		result.status = ResourceDownloadPreparationStatus::Cancelled;
		return cleanupFailure(std::move(result));
	}

	const ResourceArtifactDownloadFunction download = artifactDownloader
		? artifactDownloader
		: ResourceArtifactDownloadFunction(
			[](const std::string& url,
				const std::filesystem::path& destinationPath,
				std::uint64_t maximumBytes,
				std::uint64_t expectedBytes,
				const HttpsDownloadProgress& downloadProgress)
			{
				return downloadHttpsToNewFile(
					url, destinationPath, maximumBytes,
					expectedBytes, downloadProgress);
			});
	try
	{
		result.downloadResult = download(
			artifactUrl,
			result.archivePath,
			result.package.artifactSize,
			result.package.artifactSize,
			[&progress, totalBytes = result.package.artifactSize](
				std::uint64_t transferredBytes, std::uint64_t)
			{
				if (!progress)
				{
					return true;
				}
				ResourceDownloadPreparationProgress update;
				update.packageCount = 1;
				update.gameId = "common";
				update.packageTransferredBytes = transferredBytes;
				update.completedBytes = std::min(
					transferredBytes, totalBytes);
				update.totalBytes = totalBytes;
				try
				{
					return progress(update);
				}
				catch (...)
				{
					return false;
				}
			});
	}
	catch (...)
	{
		result.downloadResult.status = HttpsDownloadStatus::NetworkError;
	}
	if (!result.downloadResult.succeeded())
	{
		result.status = result.downloadResult.status ==
			HttpsDownloadStatus::Cancelled
			? ResourceDownloadPreparationStatus::Cancelled
			: ResourceDownloadPreparationStatus::DownloadFailed;
		return cleanupFailure(std::move(result));
	}

	ResourceDownloadPreparationProgress validationProgress;
	validationProgress.packageCount = 1;
	validationProgress.gameId = "common";
	validationProgress.packageTransferredBytes = result.package.artifactSize;
	validationProgress.completedBytes = result.package.artifactSize;
	validationProgress.totalBytes = result.package.artifactSize;
	validationProgress.stage =
		ResourceDownloadPreparationProgress::Stage::
			ValidatingAndExtracting;
	try
	{
		continueDownload = !progress || progress(validationProgress);
	}
	catch (...)
	{
		continueDownload = false;
	}
	if (!continueDownload)
	{
		result.status = ResourceDownloadPreparationStatus::Cancelled;
		return cleanupFailure(std::move(result));
	}

	result.packageResult = prepareCommonPackageArchive(
		result.package, result.archivePath, result.preparedCommonPath);
	if (!result.packageResult.succeeded())
	{
		result.status =
			ResourceDownloadPreparationStatus::PackageValidationFailed;
		return cleanupFailure(std::move(result));
	}
	result.status = ResourceDownloadPreparationStatus::Success;
	return result;
}

ProgramDownloadPreparationResult prepareProgramDownload(
	const Catalog& catalog,
	const std::string& target,
	const std::string& currentVersion,
	const std::string& catalogUrl,
	const std::filesystem::path& workspacePath,
	const HttpsDownloadProgress& progress,
	const ResourceArtifactDownloadFunction& artifactDownloader)
{
	ProgramDownloadPreparationResult result;
	result.workspacePath = workspacePath;
	if (target.empty() || currentVersion.empty() || catalogUrl.empty() ||
		workspacePath.empty())
	{
		return result;
	}

	const ProgramUpdateCheck update = checkProgramUpdate(
		catalog, target, currentVersion);
	result.updateStatus = update.status;
	if (!update.hasOnlinePackage())
	{
		result.status =
			ResourceDownloadPreparationStatus::ProgramNotAvailable;
		return result;
	}
	result.package = *update.package;

	std::string artifactUrl;
	if (!buildHttpsArtifactUrl(
			catalogUrl, result.package.artifactPath, artifactUrl))
	{
		result.status =
			ResourceDownloadPreparationStatus::InvalidArtifactUrl;
		return result;
	}

	std::error_code error;
	if (std::filesystem::exists(workspacePath, error))
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceAlreadyExists;
		return result;
	}
	if (error || !std::filesystem::create_directory(workspacePath, error) ||
		error)
	{
		result.status =
			ResourceDownloadPreparationStatus::WorkspaceUnavailable;
		return result;
	}
	result.artifactPath = workspacePath / "program-update.download";

	const ResourceArtifactDownloadFunction download = artifactDownloader
		? artifactDownloader
		: ResourceArtifactDownloadFunction(
			[](const std::string& url,
				const std::filesystem::path& destinationPath,
				std::uint64_t maximumBytes,
				std::uint64_t expectedBytes,
				const HttpsDownloadProgress& downloadProgress)
			{
				return downloadHttpsToNewFile(
					url, destinationPath, maximumBytes,
					expectedBytes, downloadProgress);
			});
	try
	{
		result.downloadResult = download(
			artifactUrl,
			result.artifactPath,
			result.package.artifactSize,
			result.package.artifactSize,
			progress);
	}
	catch (...)
	{
		result.downloadResult.status = HttpsDownloadStatus::NetworkError;
	}
	if (!result.downloadResult.succeeded())
	{
		result.status = result.downloadResult.status ==
			HttpsDownloadStatus::Cancelled
			? ResourceDownloadPreparationStatus::Cancelled
			: ResourceDownloadPreparationStatus::DownloadFailed;
		return cleanupFailure(std::move(result));
	}

	std::uint32_t checksum = 0;
	std::uint64_t fileSize = 0;
	if (!calculateFileCrc32(
			result.artifactPath, checksum, fileSize) ||
		fileSize != result.package.artifactSize ||
		crc32ToLowerHex(checksum) != result.package.crc32Hex)
	{
		result.status =
			ResourceDownloadPreparationStatus::ArtifactValidationFailed;
		return cleanupFailure(std::move(result));
	}

	result.status = ResourceDownloadPreparationStatus::Success;
	return result;
}
}
