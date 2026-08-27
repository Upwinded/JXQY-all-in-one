#include "ResourceDownloadPlanner.h"

#include <limits>
#include <map>

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
		OnlineUpdate::ResourceDownloadPlan& outputValue)
		: catalog(catalogValue),
		  engineVersion(engineVersionValue),
		  output(outputValue)
	{
	}

	bool visit(const std::string& requestedGameId)
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
		for (const std::string& dependencyGameId :
			package.dependencyGameIds)
		{
			if (!visit(dependencyGameId))
			{
				return false;
			}
		}

		if (output.totalDownloadBytes >
			std::numeric_limits<std::uint64_t>::max() - package.artifactSize)
		{
			output.status =
				OnlineUpdate::ResourcePlanStatus::TotalSizeOverflow;
			output.blockingGameId = package.gameId;
			return false;
		}
		output.totalDownloadBytes += package.artifactSize;
		output.downloadOrder.push_back(&package);
		state = VisitState::Complete;
		return true;
	}

private:
	const OnlineUpdate::Catalog& catalog;
	const ModRelease::SemanticVersion& engineVersion;
	OnlineUpdate::ResourceDownloadPlan& output;
	std::map<std::string, VisitState> states;
};
}

namespace OnlineUpdate
{
ResourceDownloadPlan planResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion)
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

	Planner planner(catalog, engineVersion.version, result);
	planner.visit(requestedGameId);
	if (!result.succeeded())
	{
		result.downloadOrder.clear();
		result.totalDownloadBytes = 0;
	}
	return result;
}
}
