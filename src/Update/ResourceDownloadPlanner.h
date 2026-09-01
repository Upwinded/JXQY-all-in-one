#pragma once

#include "OnlineUpdateCatalog.h"

#include <cstdint>
#include <map>
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
	enum class ArtifactKind
	{
		Full,
		Incremental,
		FullAndIncremental
	};

	struct Item
	{
		const ResourcePackage* package = nullptr;
		ArtifactKind artifactKind = ArtifactKind::Full;
		std::size_t incrementalChainStartIndex = 0;
		std::uint64_t downloadSize = 0;
	};

	std::vector<Item> downloadOrder;
	std::uint64_t totalDownloadBytes = 0;

	bool succeeded() const noexcept
	{
		return status == ResourcePlanStatus::Ready;
	}
};

struct InstalledResourceArtifacts
{
	std::string fullArtifactCrc32;
	std::string incrementalArtifactCrc32;
	// True when the installed tree can be copied into a private staging area
	// before applying an incremental overlay.
	bool supportsIncrementalUpdate = false;
	std::vector<std::string> incrementalChainCrc32s;
};

// Game.Id -> local full/incremental receipts. Keys and CRCs are normalized by
// the planner; malformed or conflicting entries are treated conservatively.
using InstalledResourceArtifactMap =
	std::map<std::string, InstalledResourceArtifacts>;

enum class RequestedResourceDownloadMode
{
	IfNeeded,
	ForceFullPackage
};

// Plans the complete dependency closure. When an incremental artifact is
// declared, every changed full installation is followed by that incremental
// overlay. A matching full artifact selects only a differing incremental
// artifact. Packages whose applicable receipts already match remain in closure
// validation but are omitted from the download order.
ResourceDownloadPlan planResourceDownload(
	const Catalog& catalog,
	const std::string& requestedGameId,
	const std::string& currentEngineVersion,
	const InstalledResourceArtifactMap& installedArtifacts = {},
	RequestedResourceDownloadMode requestedMode =
		RequestedResourceDownloadMode::IfNeeded);
}
