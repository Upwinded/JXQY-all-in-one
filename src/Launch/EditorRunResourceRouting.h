#pragma once

#include "EditorRunDescriptor.h"
#include "EditorRunRuntimeTrace.h"
#include "../File/RootedResourceReader.h"
#include "../Resource/ResourceCatalog.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace EditorRun
{
struct RuntimeSession;

enum class SearchRootKind
{
	Overlay,
	Active,
	DependencyId,
	Common
};

struct TraceContentRootIdentity
{
	RuntimeTraceRootKind kind = RuntimeTraceRootKind::Active;
	std::uint64_t ordinal = 0;
	std::optional<std::string> resourcePackId;
};

// An overlay source whose former catalog root no longer has a trustworthy
// current mapping remains traceable without being attributed to another pack.
inline constexpr std::uint64_t UnknownTraceContentRootOrdinal =
	RuntimeTraceMaximumExactJsonInteger;

struct TraceOverlayOrigin
{
	std::string virtualPath;
	TraceContentRootIdentity contentRoot;
};

struct SearchRoot
{
	SearchRootKind kind = SearchRootKind::Active;
	std::filesystem::path root;
	std::string resourcePackId;
	RootedResourceReader::RootAnchor anchor;
	// Only the private overlay retains a native anchor during preparation. Formal
	// roots are reopened from their current paths so an external edit or root
	// replacement is visible to the running game.
	std::optional<TraceContentRootIdentity> traceContentRoot;
	std::vector<TraceOverlayOrigin> traceOverlayOrigins;
};

enum class TargetFileKind
{
	Map,
	Npc,
	Object,
	EntryScript
};

struct ResolvedTargetFile
{
	TargetFileKind kind = TargetFileKind::Map;
	std::string virtualPath;
	std::size_t searchRootIndex = 0;
};

// Resource files are selected when the corresponding loader actually runs.
// This avoids probing every target once during startup and then reading it a
// second time during scene application.
inline constexpr std::size_t SearchAllResourceRoots =
	static_cast<std::size_t>(-1);

struct ResolvedSceneTarget
{
	ResolvedTargetFile map;
	std::optional<ResolvedTargetFile> npc;
	std::optional<ResolvedTargetFile> object;
	std::optional<ResolvedTargetFile> entryScript;
};

enum class ResourcePreparationError
{
	None,
	ResourceRoutingUnavailable,
	OverlayRootUnavailable,
	OverlayOriginMismatch,
	OutputIdentityMismatch
};

struct PreparedResourcePhase
{
	RuntimeResource::ExactResourceSelection resources;
	std::vector<SearchRoot> orderedSearchRoots;
	ResolvedSceneTarget target;
};

struct ResourcePreparationResult
{
	PreparedResourcePhase prepared;
	ResourcePreparationError error = ResourcePreparationError::None;
	std::string diagnosticCode;
	std::string fieldPath;
	std::string resourcePackId;
	std::string virtualPath;
	std::filesystem::path hostPath;
	std::string message;

	bool succeeded() const noexcept
	{
		return error == ResourcePreparationError::None;
	}
};

// Copies the descriptor's logical target paths without probing resource files.
// The runtime loader searches overlay, active MOD and declared dependencies
// when each target is actually consumed.
ResolvedSceneTarget prepareSceneTarget(const SceneTarget& target);

// Pure editor-run routing phase. It resolves the selected catalog entry and
// declared dependencies without evaluating resource completeness or probing
// target files. An existing overlay directory is searched first; a missing
// overlay is left for the later isolation phase.
//
// This is intentionally not the final PreparedEditorRun: private session
// output identity and writability must be validated before wiring it to main().
ResourcePreparationResult prepareEditorRunResources(
	const Descriptor& descriptor,
	std::string_view currentEngineVersion);

// Production overload. Formal-root metadata is advisory and does not constrain
// the current descriptor/catalog selection. Only the private overlay identity
// and optional trace-origin mapping are bound to the session.
ResourcePreparationResult prepareEditorRunResources(
	const RuntimeSession& session,
	std::string_view currentEngineVersion);
}
