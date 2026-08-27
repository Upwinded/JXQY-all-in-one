#pragma once

#include "OnlineUpdateCatalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OnlineUpdate
{
enum class ResourcePlanStatus
{
	Ready,
	TargetNotFound,
	ResourceOnlyTarget,
	InvalidCurrentEngineVersion,
	DependencyCycle,
	RequiresNewerEngine,
	TotalSizeOverflow
};

struct ResourceDownloadPlan
{
	ResourcePlanStatus status = ResourcePlanStatus::Ready;
	std::string requestedGameId;
	std::string blockingGameId;
	std::vector<const ResourcePackage*> downloadOrder;
	std::uint64_t totalDownloadBytes = 0;

	bool succeeded() const noexcept
	{
		return status == ResourcePlanStatus::Ready;
	}
};

// Plans the complete online dependency closure. Installed resources are not
// used to skip entries because the first version intentionally has no resource
// version comparison policy.
ResourceDownloadPlan planResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion);
}
