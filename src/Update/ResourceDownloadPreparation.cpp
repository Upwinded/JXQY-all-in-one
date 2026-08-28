#include "ResourceDownloadPreparation.h"

#include "ArtifactChecksum.h"

#include <algorithm>
#include <set>
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
	const InstalledResourceArtifactMap& installedArtifacts,
	const InstalledResourceRootMap& installedResourceRoots,
	RequestedResourceDownloadMode requestedMode,
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
		catalog,
		requestedGameId,
		currentEngineVersion,
		installedArtifacts,
		requestedMode);
	result.planStatus = plan.status;
	result.failedGameId = plan.blockingGameId;
	result.totalDownloadBytes = plan.totalDownloadBytes;
	if (!plan.succeeded())
	{
		result.status = ResourceDownloadPreparationStatus::PlanFailed;
		return result;
	}
	InstalledResourceRootMap normalizedInstalledRoots;
	std::set<std::string> conflictedInstalledRoots;
	for (const auto& installedRoot : installedResourceRoots)
	{
		const std::string gameId = foldGameId(installedRoot.first);
		if (gameId.empty() || installedRoot.second.empty() ||
			conflictedInstalledRoots.find(gameId) !=
				conflictedInstalledRoots.end())
		{
			continue;
		}
		const auto existing = normalizedInstalledRoots.find(gameId);
		if (existing == normalizedInstalledRoots.end())
		{
			normalizedInstalledRoots.emplace(gameId, installedRoot.second);
		}
		else if (existing->second != installedRoot.second)
		{
			normalizedInstalledRoots.erase(existing);
			conflictedInstalledRoots.insert(gameId);
		}
	}

	struct ArtifactUrls
	{
		std::string full;
		std::string incremental;
	};
	std::vector<ArtifactUrls> artifactUrls;
	artifactUrls.reserve(plan.downloadOrder.size());
	for (const ResourceDownloadPlan::Item& item : plan.downloadOrder)
	{
		if (item.package == nullptr ||
			!isValidOnlineGameId(item.package->gameId))
		{
			result.status = ResourceDownloadPreparationStatus::InvalidInput;
			result.failedGameId = item.package == nullptr
				? std::string() : item.package->gameId;
			return result;
		}
		ArtifactUrls urls;
		if (item.artifactKind != ResourceDownloadPlan::ArtifactKind::Incremental &&
			!buildHttpsArtifactUrl(
				catalogUrl, item.package->artifactPath, urls.full))
		{
			result.status =
				ResourceDownloadPreparationStatus::InvalidArtifactUrl;
			result.failedGameId = item.package->gameId;
			return result;
		}
		if (item.artifactKind != ResourceDownloadPlan::ArtifactKind::Full)
		{
			if (!item.package->incrementalPackage.has_value() ||
				!buildHttpsArtifactUrl(
					catalogUrl,
					item.package->incrementalPackage->artifactPath,
					urls.incremental))
			{
				result.status = item.package->incrementalPackage.has_value()
					? ResourceDownloadPreparationStatus::InvalidArtifactUrl
					: ResourceDownloadPreparationStatus::InvalidInput;
				result.failedGameId = item.package->gameId;
				return result;
			}
		}
		artifactUrls.push_back(std::move(urls));
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
		const ResourceDownloadPlan::Item& item =
			plan.downloadOrder[packageIndex];
		const ResourcePackage& package = *item.package;
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
		const std::filesystem::path fullArchivePath =
			archivesPath / (fileStem + "-full.zip");
		const std::filesystem::path incrementalArchivePath =
			archivesPath / (fileStem + "-incremental.zip");
		const std::filesystem::path packagePreparedPath =
			preparedPath / fileStem;
		const std::filesystem::path fullPreparedPath =
			preparedPath / (fileStem + "-full");
		const std::filesystem::path incrementalOverlayPath =
			preparedPath / (fileStem + "-overlay");
		const auto downloadArtifact =
			[&download,
				&progress,
				&result,
				packageIndex,
				packageCount = plan.downloadOrder.size(),
				gameId = package.gameId,
				completedBytes,
				totalBytes = plan.totalDownloadBytes,
				packageDownloadBytes = item.downloadSize](
					const std::string& url,
					const std::filesystem::path& destination,
					std::uint64_t artifactSize,
					std::uint64_t artifactOffset) -> bool
		{
			try
			{
				result.downloadResult = download(
					url,
					destination,
					artifactSize,
					artifactSize,
					[&progress,
						packageIndex,
						packageCount,
						gameId,
						completedBytes,
						totalBytes,
						packageDownloadBytes,
						artifactSize,
						artifactOffset](
							std::uint64_t transferredBytes,
							std::uint64_t)
					{
						if (!progress)
						{
							return true;
						}
						const std::uint64_t packageTransferred = std::min(
							packageDownloadBytes,
							artifactOffset + std::min(
								transferredBytes, artifactSize));
						ResourceDownloadPreparationProgress update;
						update.packageIndex = packageIndex;
						update.packageCount = packageCount;
						update.gameId = gameId;
						update.packageTransferredBytes = packageTransferred;
						update.completedBytes = completedBytes +
							packageTransferred;
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
			if (result.downloadResult.succeeded())
			{
				return true;
			}
			result.status = result.downloadResult.status ==
				HttpsDownloadStatus::Cancelled
				? ResourceDownloadPreparationStatus::Cancelled
				: ResourceDownloadPreparationStatus::DownloadFailed;
			return false;
		};

		std::uint64_t artifactOffset = 0;
		if (item.artifactKind !=
				ResourceDownloadPlan::ArtifactKind::Incremental)
		{
			if (!downloadArtifact(
					artifactUrls[packageIndex].full,
					fullArchivePath,
					package.artifactSize,
					artifactOffset))
			{
				return cleanupFailure(std::move(result));
			}
			artifactOffset = package.artifactSize;
		}
		if (item.artifactKind != ResourceDownloadPlan::ArtifactKind::Full)
		{
			const IncrementalResourcePackage& incremental =
				*package.incrementalPackage;
			if (!downloadArtifact(
					artifactUrls[packageIndex].incremental,
					incrementalArchivePath,
					incremental.artifactSize,
					artifactOffset))
			{
				return cleanupFailure(std::move(result));
			}
		}

		currentProgress.packageTransferredBytes = item.downloadSize;
		currentProgress.completedBytes = completedBytes + item.downloadSize;
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

		if (item.artifactKind ==
			ResourceDownloadPlan::ArtifactKind::Incremental)
		{
			const auto installedRoot = normalizedInstalledRoots.find(
				foldGameId(package.gameId));
			if (installedRoot == normalizedInstalledRoots.end())
			{
				result.status =
					ResourceDownloadPreparationStatus::InvalidInput;
				return cleanupFailure(std::move(result));
			}
			result.packageResult = prepareIncrementalResourcePackageArchive(
				package, incrementalArchivePath, incrementalOverlayPath);
			if (result.packageResult.succeeded())
			{
				result.packageResult = materializeIncrementalResourcePackage(
					package,
					installedRoot->second,
					incrementalOverlayPath,
					packagePreparedPath);
			}
			if (result.packageResult.succeeded())
			{
				std::error_code cleanupError;
				std::filesystem::remove_all(
					incrementalOverlayPath, cleanupError);
				if (cleanupError)
				{
					result.status =
						ResourceDownloadPreparationStatus::CleanupFailed;
					return cleanupFailure(std::move(result));
				}
			}
		}
		else if (item.artifactKind ==
			ResourceDownloadPlan::ArtifactKind::FullAndIncremental)
		{
			result.packageResult = prepareResourcePackageArchive(
				package, fullArchivePath, fullPreparedPath);
			if (result.packageResult.succeeded())
			{
				result.packageResult = prepareIncrementalResourcePackageArchive(
					package, incrementalArchivePath, incrementalOverlayPath);
			}
			if (result.packageResult.succeeded())
			{
				result.packageResult = materializeIncrementalResourcePackage(
					package,
					fullPreparedPath,
					incrementalOverlayPath,
					packagePreparedPath);
			}
			if (result.packageResult.succeeded())
			{
				std::error_code cleanupError;
				std::filesystem::remove_all(fullPreparedPath, cleanupError);
				if (!cleanupError)
				{
					std::filesystem::remove_all(
						incrementalOverlayPath, cleanupError);
				}
				if (cleanupError)
				{
					result.status =
						ResourceDownloadPreparationStatus::CleanupFailed;
					return cleanupFailure(std::move(result));
				}
			}
		}
		else
		{
			result.packageResult = prepareResourcePackageArchive(
				package, fullArchivePath, packagePreparedPath);
		}
		if (!result.packageResult.succeeded())
		{
			result.status =
				ResourceDownloadPreparationStatus::PackageValidationFailed;
			return cleanupFailure(std::move(result));
		}

		PreparedResourceDownload prepared;
		prepared.package = package;
		prepared.artifactKind = item.artifactKind;
		prepared.archivePath = item.artifactKind ==
			ResourceDownloadPlan::ArtifactKind::Incremental
			? incrementalArchivePath : fullArchivePath;
		prepared.preparedResourcePath = packagePreparedPath;
		result.preparedResources.push_back(std::move(prepared));
		completedBytes += item.downloadSize;
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
