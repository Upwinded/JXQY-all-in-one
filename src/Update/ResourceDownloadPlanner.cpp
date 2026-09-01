#include "ResourceDownloadPlanner.h"

#include <limits>
#include <map>
#include <set>
#include <utility>

namespace
{
enum class VisitState
{
	Unvisited,
	Visiting,
	Complete
};

class Planner
{
public:
	Planner(
		const OnlineUpdate::Catalog& catalogValue,
		const ModRelease::SemanticVersion& engineVersionValue,
		const OnlineUpdate::InstalledResourceArtifactMap& installedArtifactsValue,
		OnlineUpdate::ResourceDownloadPlan& outputValue)
		: catalog(catalogValue),
		  engineVersion(engineVersionValue),
		  installedArtifacts(installedArtifactsValue),
		  output(outputValue)
	{
	}

	bool visit(
		const std::string& requestedGameId,
		bool forceFullPackage = false)
	{
		const std::string key = OnlineUpdate::foldGameId(requestedGameId);
		const auto catalogIterator = catalog.resourcePackages.find(key);
		if (catalogIterator == catalog.resourcePackages.end())
		{
			output.status = OnlineUpdate::ResourcePlanStatus::TargetNotFound;
			output.blockingGameId = requestedGameId;
			return false;
		}
		const OnlineUpdate::ResourcePackage& package = catalogIterator->second;

		if (ModRelease::compareSemanticVersionPrecedence(
				package.minimumEngineVersion, engineVersion) > 0)
		{
			output.status =
				OnlineUpdate::ResourcePlanStatus::RequiresNewerEngine;
			output.blockingGameId = package.gameId;
			return false;
		}

		VisitState& state = states[key];
		if (state == VisitState::Visiting)
		{
			output.status = OnlineUpdate::ResourcePlanStatus::DependencyCycle;
			output.blockingGameId = package.gameId;
			return false;
		}
		if (state == VisitState::Complete)
		{
			return true;
		}
		state = VisitState::Visiting;
		for (const std::string& dependencyGameId : package.dependencyGameIds)
		{
			if (!visit(dependencyGameId))
			{
				return false;
			}
		}
		state = VisitState::Complete;

		const auto installed = installedArtifacts.find(key);
		const bool fullArtifactMatches =
			installed != installedArtifacts.end() &&
			installed->second.fullArtifactCrc32 == package.crc32Hex;
		OnlineUpdate::ResourceDownloadPlan::Item item;
		item.package = &package;
		const auto addArtifactSize =
			[this, &item, &package](std::uint64_t artifactSize) -> bool
		{
			if (item.downloadSize >
				std::numeric_limits<std::uint64_t>::max() - artifactSize)
			{
				output.status = OnlineUpdate::ResourcePlanStatus::
					TotalSizeOverflow;
				output.blockingGameId = package.gameId;
				return false;
			}
			item.downloadSize += artifactSize;
			return true;
		};

		if (!package.incrementalChain.empty())
		{
			std::size_t chainStartIndex = 0;
			if (installed != installedArtifacts.end())
			{
				const auto& receipts =
					installed->second.incrementalChainCrc32s;
				const std::size_t comparable = std::min(
					receipts.size(), package.incrementalChain.size());
				while (chainStartIndex < comparable &&
					receipts[chainStartIndex] ==
						package.incrementalChain[chainStartIndex].crc32Hex)
				{
					chainStartIndex++;
				}
				if (receipts.size() > package.incrementalChain.size())
				{
					chainStartIndex = 0;
				}
			}
			const bool chainMatches = installed != installedArtifacts.end() &&
				chainStartIndex == package.incrementalChain.size() &&
				installed->second.incrementalChainCrc32s.size() ==
					package.incrementalChain.size();
			const bool legacyReceiptMatches =
				installed != installedArtifacts.end() &&
				installed->second.incrementalArtifactCrc32 ==
					package.incrementalChain.back().crc32Hex;
			if (chainMatches && !legacyReceiptMatches)
			{
				chainStartIndex = package.incrementalChain.size() - 1;
			}
			if (forceFullPackage || !fullArtifactMatches)
			{
				item.artifactKind = OnlineUpdate::ResourceDownloadPlan::
					ArtifactKind::FullAndIncremental;
				item.incrementalChainStartIndex = 0;
				if (!addArtifactSize(package.artifactSize))
				{
					return false;
				}
			}
			else if (!chainMatches || !legacyReceiptMatches)
			{
				if (installed->second.supportsIncrementalUpdate)
				{
					item.artifactKind = OnlineUpdate::ResourceDownloadPlan::
						ArtifactKind::Incremental;
					item.incrementalChainStartIndex = chainStartIndex;
				}
				else
				{
					item.artifactKind = OnlineUpdate::ResourceDownloadPlan::
						ArtifactKind::FullAndIncremental;
					item.incrementalChainStartIndex = 0;
					if (!addArtifactSize(package.artifactSize))
					{
						return false;
					}
				}
			}
			else
			{
				return true;
			}
			for (std::size_t index = item.incrementalChainStartIndex;
				index < package.incrementalChain.size(); index++)
			{
				if (!addArtifactSize(
						package.incrementalChain[index].artifactSize))
				{
					return false;
				}
			}
		}
		else if (forceFullPackage || !fullArtifactMatches)
		{
			if (!addArtifactSize(package.artifactSize))
			{
				return false;
			}
			if (package.incrementalPackage.has_value())
			{
				item.artifactKind = OnlineUpdate::ResourceDownloadPlan::
					ArtifactKind::FullAndIncremental;
				if (!addArtifactSize(
						package.incrementalPackage->artifactSize))
				{
					return false;
				}
			}
		}
		else if (package.incrementalPackage.has_value() &&
			installed->second.incrementalArtifactCrc32 !=
				package.incrementalPackage->crc32Hex)
		{
			if (installed->second.supportsIncrementalUpdate)
			{
				item.artifactKind = OnlineUpdate::ResourceDownloadPlan::
					ArtifactKind::Incremental;
			}
			else
			{
				item.artifactKind = OnlineUpdate::ResourceDownloadPlan::
					ArtifactKind::FullAndIncremental;
				if (!addArtifactSize(package.artifactSize))
				{
					return false;
				}
			}
			if (!addArtifactSize(package.incrementalPackage->artifactSize))
			{
				return false;
			}
		}
		else
		{
			return true;
		}

		if (output.totalDownloadBytes >
			std::numeric_limits<std::uint64_t>::max() - item.downloadSize)
		{
			output.status = OnlineUpdate::ResourcePlanStatus::TotalSizeOverflow;
			output.blockingGameId = package.gameId;
			return false;
		}
		output.totalDownloadBytes += item.downloadSize;
		output.downloadOrder.push_back(std::move(item));
		return true;
	}

private:
	const OnlineUpdate::Catalog& catalog;
	const ModRelease::SemanticVersion& engineVersion;
	const OnlineUpdate::InstalledResourceArtifactMap& installedArtifacts;
	OnlineUpdate::ResourceDownloadPlan& output;
	std::map<std::string, VisitState> states;
};
}

namespace OnlineUpdate
{
ResourceDownloadPlan planResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion,
	const InstalledResourceArtifactMap& installedArtifacts,
	RequestedResourceDownloadMode requestedMode)
{
	ResourceDownloadPlan result;
	result.requestedGameId = requestedGameId;
	const auto requestedPackage = catalog.resourcePackages.find(
		foldGameId(requestedGameId));
	if (requestedPackage == catalog.resourcePackages.end())
	{
		result.status = ResourcePlanStatus::TargetNotFound;
		result.blockingGameId = requestedGameId;
		return result;
	}
	if (requestedPackage->second.resourceOnly)
	{
		result.status = ResourcePlanStatus::ResourceOnlyTarget;
		result.blockingGameId = requestedGameId;
		return result;
	}
	const ModRelease::SemanticVersionParseResult engineVersion =
		ModRelease::parseSemanticVersion(currentEngineVersion);
	if (!engineVersion.succeeded())
	{
		result.status = ResourcePlanStatus::InvalidCurrentEngineVersion;
		return result;
	}

	InstalledResourceArtifactMap normalizedInstalledArtifacts;
	std::set<std::string> conflictedGameIds;
	for (const auto& installed : installedArtifacts)
	{
		const std::string gameId = foldGameId(installed.first);
		if (gameId.empty() || conflictedGameIds.find(gameId) !=
				conflictedGameIds.end())
		{
			continue;
		}
		InstalledResourceArtifacts normalized = installed.second;
		if (isValidCrc32Hex(normalized.fullArtifactCrc32))
		{
			normalized.fullArtifactCrc32 =
				foldGameId(normalized.fullArtifactCrc32);
		}
		else
		{
			normalized.fullArtifactCrc32.clear();
		}
		if (isValidCrc32Hex(normalized.incrementalArtifactCrc32))
		{
			normalized.incrementalArtifactCrc32 =
				foldGameId(normalized.incrementalArtifactCrc32);
		}
		else
		{
			normalized.incrementalArtifactCrc32.clear();
		}
		bool validChainReceipts = true;
		for (std::string& receipt : normalized.incrementalChainCrc32s)
		{
			if (!isValidCrc32Hex(receipt))
			{
				validChainReceipts = false;
				break;
			}
			receipt = foldGameId(receipt);
		}
		if (!validChainReceipts || normalized.incrementalChainCrc32s.size() >
			MaximumIncrementalChainPackageCount)
		{
			normalized.incrementalChainCrc32s.clear();
		}
		const auto existing = normalizedInstalledArtifacts.find(gameId);
		if (existing == normalizedInstalledArtifacts.end())
		{
			normalizedInstalledArtifacts.emplace(
				gameId, std::move(normalized));
		}
		else if (existing->second.fullArtifactCrc32 !=
				normalized.fullArtifactCrc32 ||
			existing->second.incrementalArtifactCrc32 !=
				normalized.incrementalArtifactCrc32 ||
			existing->second.incrementalChainCrc32s !=
				normalized.incrementalChainCrc32s ||
			existing->second.supportsIncrementalUpdate !=
				normalized.supportsIncrementalUpdate)
		{
			normalizedInstalledArtifacts.erase(existing);
			conflictedGameIds.insert(gameId);
		}
	}

	Planner planner(
		catalog, engineVersion.version, normalizedInstalledArtifacts, result);
	planner.visit(
		requestedGameId,
		requestedMode == RequestedResourceDownloadMode::ForceFullPackage);
	if (!result.succeeded())
	{
		result.downloadOrder.clear();
		result.totalDownloadBytes = 0;
	}
	return result;
}
}
