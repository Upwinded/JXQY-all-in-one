#pragma once

#include "OnlineUpdateCatalog.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace OnlineUpdate
{
struct ResourcePackageArchiveLimits
{
	std::size_t maximumEntryCount = 500000;
	std::uint64_t maximumUncompressedBytes =
		256ULL * 1024ULL * 1024ULL * 1024ULL;
	std::size_t maximumManifestBytes = 16 * 1024 * 1024;
	std::uint64_t minimumFreeSpaceAfterExtractionBytes =
		64ULL * 1024ULL * 1024ULL;
};

enum class ResourcePackageArchiveStatus
{
	Success,
	InvalidInput,
	ArchiveUnavailable,
	ArtifactMismatch,
	DestinationAlreadyExists,
	UnsafeDestination,
	ArchiveOpenFailed,
	TooManyEntries,
	UncompressedSizeLimitExceeded,
	InsufficientDiskSpace,
	InvalidEntryPath,
	DuplicateEntryPath,
	UnsupportedEntry,
	MissingManifest,
	UnexpectedManifest,
	MissingCommonBootstrap,
	MissingEngineBootstrap,
	CommonVersionMismatch,
	MissingProgramExecutable,
	MissingProgramUpdater,
	DestinationCreateFailed,
	ExtractionFailed,
	ManifestReadFailed,
	InvalidManifest,
	GameIdMismatch,
	DisplayVersionMismatch,
	MinimumEngineVersionMismatch,
	DependencyMismatch,
	ResourceOnlyMismatch,
	CleanupFailed
};

struct ResourcePackageArchiveResult
{
	ResourcePackageArchiveStatus status =
		ResourcePackageArchiveStatus::InvalidInput;
	std::string archiveEntryPath;
	std::filesystem::path filesystemPath;
	std::size_t fileCount = 0;
	std::uint64_t uncompressedBytes = 0;
	int archiveError = 0;

	bool succeeded() const noexcept
	{
		return status == ResourcePackageArchiveStatus::Success;
	}
};

struct ImportedResourcePackageMetadata
{
	std::string gameId;
	std::string displayName;
	std::string author;
	std::string displayVersion;
	std::string minimumEngineVersion;
	std::vector<std::string> dependencyGameIds;
	std::uint64_t artifactSize = 0;
	std::string artifactCrc32;
	bool common = false;
	bool resourceOnly = false;
};

struct ImportedResourcePackageArchiveResult
{
	ResourcePackageArchiveResult archive;
	ImportedResourcePackageMetadata package;

	bool succeeded() const noexcept
	{
		return archive.succeeded();
	}
};

// Verifies the downloaded artifact, extracts it into a new staging directory,
// and checks that its root manifest matches the online catalog entry. The
// destination must not exist. Any failure removes only the destination created
// by this call; current installed resources are never modified here.
ResourcePackageArchiveResult prepareResourcePackageArchive(
	const ResourcePackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Validates and extracts one user-selected standalone playable resource ZIP.
// Unlike online packages, its identity and release metadata come from the
// archive's root game_profile.ini. The same path, entry and size rules apply;
// successful imports receive a full-package CRC receipt before installation.
ImportedResourcePackageArchiveResult prepareImportedResourcePackageArchive(
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Imports either a playable/resource-only package or common. A package without
// a root game_profile.ini is treated as common and must contain version.ini.
ImportedResourcePackageArchiveResult prepareImportedFullResourcePackageArchive(
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Validates and extracts an arbitrary user-selected incremental overlay. Its
// Game.Id and release metadata come from the root game_profile.ini. The ZIP
// receipt is recorded only when it is materialized over an installed base.
ImportedResourcePackageArchiveResult
	prepareImportedIncrementalResourcePackageArchive(
		const std::filesystem::path& archivePath,
		const std::filesystem::path& destinationPath,
		const ResourcePackageArchiveLimits& limits = {});

// An incremental archive is a validated overlay: it uses the optional
// Incremental* artifact fields, contains a complete game_profile.ini, and may
// add or replace files. Removing files requires publishing a new full package.
ResourcePackageArchiveResult prepareIncrementalResourcePackageArchive(
	const ResourcePackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Validates and extracts one ordered chain overlay. Historical entries only
// need a valid manifest with the expected Game.Id; the last entry must match
// the current catalog metadata and receives the complete chain receipt.
ResourcePackageArchiveResult prepareIncrementalChainPackageArchive(
	const ResourcePackage& expectedPackage,
	std::size_t chainIndex,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Copies an installed tree to a new destination and applies a previously
// validated incremental overlay. Neither input tree is modified.
ResourcePackageArchiveResult materializeIncrementalResourcePackage(
	const ResourcePackage& expectedPackage,
	const std::filesystem::path& installedResourcePath,
	const std::filesystem::path& preparedOverlayPath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Copies one installed/full base and applies an ordered suffix of previously
// validated chain overlays. The last overlay must be the catalog's final chain
// package and supplies the current manifest and complete local receipts.
ResourcePackageArchiveResult materializeIncrementalResourcePackageChain(
	const ResourcePackage& expectedPackage,
	const std::filesystem::path& installedResourcePath,
	const std::vector<std::filesystem::path>& preparedOverlayPaths,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Copies the installed same-ID base and applies one imported overlay. The base
// full-package receipt is preserved, the selected ZIP becomes the incremental
// receipt, and any unverifiable chain receipt is cleared.
ResourcePackageArchiveResult materializeImportedIncrementalResourcePackage(
	const ImportedResourcePackageMetadata& package,
	const std::filesystem::path& installedResourcePath,
	const std::filesystem::path& preparedOverlayPath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// Common archives use the same extraction rules as playable resources, but
// they must not contain a game_profile.ini. Engine bootstrap files are not
// part of common and are shipped with the program package.
ResourcePackageArchiveResult prepareCommonPackageArchive(
	const CommonPackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});

// A desktop program archive is also the complete portable distribution: it
// contains a stable root launcher, bin/<platform>, assets/engine,
// assets/common and the bootstrap resource index, but no playable resource
// packs. Online self-update stages the current platform's bin directory
// together with engine and common. The updater switches all three and leaves
// the running updater, root launcher, save and playable resources alone.
ResourcePackageArchiveResult prepareDesktopProgramPackageArchive(
	const ProgramPackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits = {});
}
