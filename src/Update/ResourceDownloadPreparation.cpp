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

bool mirrorRetryable(OnlineUpdate::HttpsDownloadStatus status)
{
	using Status = OnlineUpdate::HttpsDownloadStatus;
	return status == Status::NetworkError || status == Status::HttpError ||
		status == Status::SizeLimitExceeded || status == Status::SizeMismatch;
}

OnlineUpdate::HttpsBufferDownloadResult downloadCatalog(
	const OnlineUpdate::CatalogDownloadFunction& catalogDownloader,
	const std::string& url,
	const OnlineUpdate::HttpsDownloadProgress& progress)
{
	if (catalogDownloader)
	{
		return catalogDownloader(
			url, OnlineUpdate::MaximumCatalogBytes, progress);
	}
	return OnlineUpdate::downloadHttpsToMemory(
		url, OnlineUpdate::MaximumCatalogBytes, progress);
}

OnlineUpdate::ResourceDownloadPreparationStatus downloadArtifactFromMirrors(
	const OnlineUpdate::CatalogMirrorSources& catalogSources,
	const std::string& artifactPath,
	const std::filesystem::path& destinationPath,
	std::uint64_t artifactSize,
	const std::string& expectedCrc32,
	const OnlineUpdate::ResourceArtifactDownloadFunction& download,
	const OnlineUpdate::CatalogDownloadFunction& catalogDownloader,
	const OnlineUpdate::HttpsDownloadProgress& progress,
	OnlineUpdate::HttpsDownloadResult& downloadResult)
{
	using PreparationStatus =
		OnlineUpdate::ResourceDownloadPreparationStatus;
	using DownloadStatus = OnlineUpdate::HttpsDownloadStatus;
	if (catalogSources.catalogUrls.empty())
	{
		return PreparationStatus::InvalidInput;
	}
	PreparationStatus failure = PreparationStatus::DownloadFailed;
	for (std::size_t mirrorIndex = 0;
		mirrorIndex < catalogSources.catalogUrls.size(); mirrorIndex++)
	{
		if (mirrorIndex != 0)
		{
			if (catalogSources.catalogBytes.empty())
			{
				continue;
			}
			OnlineUpdate::HttpsBufferDownloadResult candidateCatalog;
			try
			{
				candidateCatalog = downloadCatalog(
					catalogDownloader,
					catalogSources.catalogUrls[mirrorIndex],
					[&progress, artifactSize](std::uint64_t, std::uint64_t)
					{
						return !progress || progress(0, artifactSize);
					});
			}
			catch (...)
			{
				downloadResult = {};
				downloadResult.status = DownloadStatus::UnexpectedError;
				return PreparationStatus::DownloadFailed;
			}
			if (candidateCatalog.status == DownloadStatus::Cancelled)
			{
				return PreparationStatus::Cancelled;
			}
			if (!candidateCatalog.succeeded() ||
				candidateCatalog.bytes != catalogSources.catalogBytes)
			{
				continue;
			}
		}
		std::string artifactUrl;
		if (!OnlineUpdate::buildHttpsArtifactUrl(
				catalogSources.catalogUrls[mirrorIndex], artifactPath, artifactUrl))
		{
			return PreparationStatus::InvalidArtifactUrl;
		}
		try
		{
			downloadResult = download(
				artifactUrl,
				destinationPath,
				artifactSize,
				artifactSize,
				progress);
		}
		catch (...)
		{
			downloadResult = {};
			downloadResult.status = DownloadStatus::UnexpectedError;
			return PreparationStatus::DownloadFailed;
		}
		if (downloadResult.status == DownloadStatus::Cancelled)
		{
			return PreparationStatus::Cancelled;
		}
		if (downloadResult.succeeded())
		{
			std::uint32_t checksum = 0;
			std::uint64_t fileSize = 0;
			if (OnlineUpdate::calculateFileCrc32(
					destinationPath, checksum, fileSize) &&
				fileSize == artifactSize &&
				OnlineUpdate::crc32ToLowerHex(checksum) == expectedCrc32)
			{
				return PreparationStatus::Success;
			}
			failure = PreparationStatus::ArtifactValidationFailed;
		}
		else if (!mirrorRetryable(downloadResult.status))
		{
			return PreparationStatus::DownloadFailed;
		}
		else
		{
			failure = PreparationStatus::DownloadFailed;
		}
		if (mirrorIndex + 1 < catalogSources.catalogUrls.size())
		{
			std::error_code removeError;
			std::filesystem::remove(destinationPath, removeError);
			if (removeError)
			{
				return PreparationStatus::CleanupFailed;
			}
		}
	}
	return failure;
}
}

namespace OnlineUpdate
{
CatalogMirrorSelectionResult selectCatalogMirrorSources(
	const std::vector<std::string>& catalogUrls,
	const HttpsDownloadProgress& progress,
	const CatalogDownloadFunction& catalogDownloader)
{
	CatalogMirrorSelectionResult result;
	result.configured = !catalogUrls.empty();
	for (std::size_t index = 0; index < catalogUrls.size(); index++)
	{
		result.downloadAttempted = true;
		try
		{
			result.download = downloadCatalog(
				catalogDownloader, catalogUrls[index], progress);
		}
		catch (...)
		{
			result.download = {};
			result.download.status = HttpsDownloadStatus::UnexpectedError;
			return result;
		}
		if (result.download.status == HttpsDownloadStatus::Cancelled)
		{
			return result;
		}
		if (!result.download.succeeded())
		{
			result.parse = {};
			continue;
		}
		result.parse = parseCatalog(
			result.download.bytes.data(), result.download.bytes.size());
		if (!result.parse.succeeded())
		{
			continue;
		}
		result.sources.catalogUrls.assign(
			catalogUrls.begin() + index, catalogUrls.end());
		result.sources.catalogBytes = std::move(result.download.bytes);
		return result;
	}
	return result;
}

ResourceDownloadPreparationResult prepareResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion,
	const CatalogMirrorSources& catalogSources,
	const std::filesystem::path& workspacePath,
	const InstalledResourceArtifactMap& installedArtifacts,
	const InstalledResourceRootMap& installedResourceRoots,
	RequestedResourceDownloadMode requestedMode,
	const ResourceDownloadPreparationProgressCallback& progress,
	const ResourceArtifactDownloadFunction& artifactDownloader,
	const CatalogDownloadFunction& catalogDownloader)
{
	ResourceDownloadPreparationResult result;
	result.workspacePath = workspacePath;
	if (requestedGameId.empty() || currentEngineVersion.empty() ||
		catalogSources.catalogUrls.empty() || workspacePath.empty())
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
		if (item.artifactKind != ResourceDownloadPlan::ArtifactKind::Full)
		{
			if (!item.package->incrementalPackage.has_value())
			{
				result.status =
					ResourceDownloadPreparationStatus::InvalidInput;
				result.failedGameId = item.package->gameId;
				return result;
			}
		}
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
			[&catalogSources,
				&catalogDownloader,
				&download,
				&progress,
				&result,
				packageIndex,
				packageCount = plan.downloadOrder.size(),
				gameId = package.gameId,
				completedBytes,
				totalBytes = plan.totalDownloadBytes,
				packageDownloadBytes = item.downloadSize](
					const std::string& artifactPath,
					const std::filesystem::path& destination,
					std::uint64_t artifactSize,
					const std::string& expectedCrc32,
					std::uint64_t artifactOffset) -> bool
		{
			result.status = downloadArtifactFromMirrors(
				catalogSources,
				artifactPath,
				destination,
				artifactSize,
				expectedCrc32,
				download,
				catalogDownloader,
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
					},
				result.downloadResult);
			return result.status ==
				ResourceDownloadPreparationStatus::Success;
		};

		std::uint64_t artifactOffset = 0;
		if (item.artifactKind !=
				ResourceDownloadPlan::ArtifactKind::Incremental)
		{
			if (!downloadArtifact(
					package.artifactPath,
					fullArchivePath,
					package.artifactSize,
					package.crc32Hex,
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
					incremental.artifactPath,
					incrementalArchivePath,
					incremental.artifactSize,
					incremental.crc32Hex,
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
	const CatalogMirrorSources& catalogSources,
	const std::filesystem::path& workspacePath,
	const ResourceDownloadPreparationProgressCallback& progress,
	const ResourceArtifactDownloadFunction& artifactDownloader,
	const CatalogDownloadFunction& catalogDownloader)
{
	CommonDownloadPreparationResult result;
	result.workspacePath = workspacePath;
	if (!catalog.commonPackage.has_value() ||
		catalogSources.catalogUrls.empty() ||
		workspacePath.empty())
	{
		result.status = ResourceDownloadPreparationStatus::InvalidInput;
		return result;
	}
	result.package = *catalog.commonPackage;
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
	result.status = downloadArtifactFromMirrors(
		catalogSources,
		result.package.artifactPath,
		result.archivePath,
		result.package.artifactSize,
		result.package.crc32Hex,
		download,
		catalogDownloader,
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
			},
		result.downloadResult);
	if (result.status != ResourceDownloadPreparationStatus::Success)
	{
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
	const CatalogMirrorSources& catalogSources,
	const std::filesystem::path& workspacePath,
	const HttpsDownloadProgress& progress,
	const ResourceArtifactDownloadFunction& artifactDownloader,
	const CatalogDownloadFunction& catalogDownloader)
{
	ProgramDownloadPreparationResult result;
	result.workspacePath = workspacePath;
	if (target.empty() || currentVersion.empty() ||
		catalogSources.catalogUrls.empty() ||
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
	result.status = downloadArtifactFromMirrors(
		catalogSources,
		result.package.artifactPath,
		result.artifactPath,
		result.package.artifactSize,
		result.package.crc32Hex,
		download,
		catalogDownloader,
		progress,
		result.downloadResult);
	if (result.status != ResourceDownloadPreparationStatus::Success)
	{
		return cleanupFailure(std::move(result));
	}

	result.status = ResourceDownloadPreparationStatus::Success;
	return result;
}
}
