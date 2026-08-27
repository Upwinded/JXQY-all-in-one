#pragma once

#include "ResourceManifest.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace RuntimeResource
{
inline constexpr std::size_t MaximumCatalogPackCount = 1024;
inline constexpr std::size_t MaximumCatalogDependencyDepth = 128;
inline constexpr std::size_t MaximumCatalogIniBytes =
	16 * 1024 * 1024;

enum class CatalogFileReadStatus
{
	Success,
	NotFound,
	UnsafePath,
	Unavailable,
	TooLarge
};

struct CatalogFileReadResult
{
	CatalogFileReadStatus status =
		CatalogFileReadStatus::Unavailable;
	std::vector<std::uint8_t> bytes;
};

enum class CatalogDirectoryStatus
{
	Exists,
	Missing,
	Unknown
};

enum class CatalogDirectoryListStatus
{
	Success,
	Unavailable
};

struct CatalogDirectoryListResult
{
	CatalogDirectoryListStatus status =
		CatalogDirectoryListStatus::Unavailable;
	std::vector<std::string> childDirectoryNames;
};

// Platform storage adapter for packaged assets. It changes only how catalog
// bytes and directory availability are obtained; parsing, dependency/UI
// materialization, diagnostics, and conflict handling remain shared.
struct ResourceCatalogFileAccess
{
	std::function<CatalogFileReadResult(
		const std::filesystem::path& root,
		std::string_view relativePath,
		std::size_t maximumBytes)> readFileFromRoot;
	std::function<CatalogDirectoryStatus(
		const std::filesystem::path& path)> getDirectoryStatus;
	std::function<CatalogDirectoryListResult(
		const std::filesystem::path& path)> listChildDirectories;

	bool valid() const noexcept
	{
		return static_cast<bool>(readFileFromRoot) &&
			static_cast<bool>(getDirectoryStatus);
	}
};

enum class ContentRootKind
{
	Active,
	DependencyId,
	Common
};

struct ContentRoot
{
	ContentRootKind kind = ContentRootKind::Active;
	std::filesystem::path root;
	std::string resourcePackId;
};

enum class ExactSelectionError
{
	None,
	AssetsRootUnavailable,
	ResourceConfigurationInvalid,
	ActiveResourcePackIdNotFound,
	ActiveResourcePackIdAmbiguous,
	DependencyIdNotFound,
	DependencyIdAmbiguous,
	DependencyCycle,
	DependencyRootUnavailable,
	CommonRootUnavailable,
	PortableSaveNamespaceAmbiguous,
	TrilogyBaseUnreachable,
	UiProfileUnsupported,
	UiDependencyNotFound,
	UiDependencyAmbiguous,
	UiDependencyCycle,
	CatalogPackLimitExceeded,
	CatalogDependencyDepthExceeded,
	CatalogIniBudgetExceeded
};

enum class CatalogDiagnosticSeverity
{
	Information,
	Warning,
	Error
};

// One discovery or capability diagnostic. A pack is discovered only through
// its root game_profile.ini. Optional metadata/dependency/UI errors do not
// remove an otherwise valid pack, and one broken manifest does not invalidate
// valid peers.
struct CatalogDiagnostic
{
	CatalogDiagnosticSeverity severity =
		CatalogDiagnosticSeverity::Warning;
	std::string code;
	std::string stableEntryKey;
	std::string resourcePackId;
	std::filesystem::path hostPath;
	std::string message;
};

// Stable, ordered view of one resource entry. stableKey is "root" for a
// collection-root game_profile.ini or "pack.<folder>" for a directly
// discovered child. It remains distinct from Game.Id so conflicts can be
// displayed without guessing which directory should run.
struct ResourceCatalogEntry
{
	std::string stableKey;
	std::filesystem::path root;
	std::filesystem::path manifestPath;
	// Empty for entries declared by the primary collection. Supplemental
	// callers use this opaque tag to identify their platform/source without
	// changing catalog parsing or dependency semantics.
	std::string sourceTag;
	ResourceManifest manifest;
	std::string effectiveSaveNamespace;
	bool saveNamespaceAdjusted = false;
	std::size_t discoveryOrder = 0;
};

// One explicitly enumerated pack root outside the primary collection. The
// root contributes only its fixed root-level game_profile.ini; resources.ini
// below this root is intentionally ignored and no child directory is scanned.
// stableEntryKey must be non-empty and globally unique (ASCII case-insensitive)
// across the primary and supplemental entries in this request.
struct SupplementalResourceRoot
{
	std::filesystem::path root;
	std::string stableEntryKey;
	std::string sourceTag;
	// A platform-writable copy may replace one packaged primary entry with the
	// same Game.Id. This is physical storage precedence only; it does not create
	// a second resource type or change the resource identity.
	bool replacesPrimaryGameId = false;
};

// A primary resources collection plus explicitly enumerated pack roots. The
// primary collection is loaded first. Supplemental roots are normalized and
// sorted by path before all entries are finalized together, so ID conflicts,
// dependency/UI lookup, and save namespaces use one deterministic catalog.
struct ResourceCatalogRequest
{
	std::filesystem::path primaryCollectionRoot;
	std::vector<SupplementalResourceRoot> supplementalRoots;
};

struct ResourceCatalogSnapshot
{
	std::filesystem::path collectionRoot;
	std::filesystem::path commonResourceRoot;
	std::string resourceCatalogUrl;
	std::string applicationCatalogUrl;
	std::vector<ResourceCatalogEntry> entries;
	std::vector<CatalogDiagnostic> diagnostics;
	bool rootManifestDeclared = false;
};

struct ResourceCatalogSnapshotResult
{
	ResourceCatalogSnapshot snapshot;
	ExactSelectionError error = ExactSelectionError::None;
	std::string diagnosticCode;
	std::filesystem::path hostPath;
	std::string message;

	bool succeeded() const noexcept
	{
		return error == ExactSelectionError::None;
	}
};

struct ExactResourceSelection
{
	std::filesystem::path assetsCollectionRoot;
	std::filesystem::path activeResourceRoot;
	// Fully materialized for runtime use, including inherited Game.Type and
	// UI.Profile. Consumers must not reload the manifest after catalog selection.
	ResourceManifest activeManifest;
	std::string canonicalActiveResourcePackId;
	std::vector<ContentRoot> orderedContentRoots;
	// Common is kept separate because File installs it both as the final
	// content fallback and as the final UI fallback.
	std::filesystem::path commonResourceRoot;
	// UI roots exclude the active and Common roots. File::preferLocalUi
	// determines whether these roots precede or follow the active root.
	std::vector<std::filesystem::path> orderedUiFallbackRoots;
	bool preferLocalUi = true;
	// Save.Namespace > canonical Game.Id > active directory name. This is the
	// unsanitized runtime namespace. Catalog conflicts use a deterministic,
	// readable stable-entry suffix instead of hiding every owner.
	std::string effectiveSaveNamespace;
	std::string stableActiveEntryKey;
};

struct ExactSelectionResult
{
	ExactResourceSelection selection;
	std::vector<CatalogDiagnostic> diagnostics;
	ExactSelectionError error = ExactSelectionError::None;
	std::string diagnosticCode;
	std::string resourcePackId;
	std::filesystem::path hostPath;
	std::string message;

	bool succeeded() const noexcept
	{
		return error == ExactSelectionError::None;
	}
};

// Discovers the collection root and each direct child directory that contains
// a readable root-level game_profile.ini with a non-empty Game.Id. Discovery
// never recurses below a direct child. resources.ini supplies collection-wide
// settings only; Pack sections do not participate in discovery. Individual bad
// entries are reported and skipped.
ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const std::filesystem::path& assetsCollectionRoot);

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const std::filesystem::path& assetsCollectionRoot,
	const ResourceCatalogFileAccess& fileAccess);

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const ResourceCatalogRequest& request);

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const ResourceCatalogRequest& request,
	const ResourceCatalogFileAccess& fileAccess);

// Resolves one exact resource entry by stable entry key. Missing dependency or
// UI fallback capabilities are diagnosed and omitted instead of disabling the
// local pack.
ExactSelectionResult resolveResourceCatalogEntrySelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view stableEntryKey);

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view stableEntryKey,
	const ResourceCatalogFileAccess& fileAccess);

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const ResourceCatalogRequest& request,
	std::string_view stableEntryKey);

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const ResourceCatalogRequest& request,
	std::string_view stableEntryKey,
	const ResourceCatalogFileAccess& fileAccess);

// Resolves by Game.Id. Duplicate IDs are configuration errors and are never
// resolved by directory order.
ExactSelectionResult resolveExactResourceSelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view requestedResourcePackId);

ExactSelectionResult resolveExactResourceSelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view requestedResourcePackId,
	const ResourceCatalogFileAccess& fileAccess);

ExactSelectionResult resolveExactResourceSelection(
	const ResourceCatalogRequest& request,
	std::string_view requestedResourcePackId);

ExactSelectionResult resolveExactResourceSelection(
	const ResourceCatalogRequest& request,
	std::string_view requestedResourcePackId,
	const ResourceCatalogFileAccess& fileAccess);
}
