#pragma once

#include "OnlineUpdateCatalog.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace OnlineUpdate
{
struct ResourcePackageArchiveLimits
{
	std::size_t maximumEntryCount = 500000;
	std::uint64_t maximumUncompressedBytes =
		256ULL * 1024ULL * 1024ULL * 1024ULL;
	std::size_t maximumManifestBytes = 16 * 1024 * 1024;
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
	InvalidEntryPath,
	DuplicateEntryPath,
	UnsupportedEntry,
	MissingManifest,
	UnexpectedManifest,
	MissingCommonBootstrap,
	MissingEngineBootstrap,
	CommonVersionMismatch,
	MissingProgramExecutable,
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

// Verifies the downloaded artifact, extracts it into a new staging directory,
// and checks that its root manifest matches the online catalog entry. The
// destination must not exist. Any failure removes only the destination created
// by this call; current installed resources are never modified here.
ResourcePackageArchiveResult prepareResourcePackageArchive(
	const ResourcePackage& expectedPackage,
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

// Copies an installed tree to a new destination and applies a previously
// validated incremental overlay. Neither input tree is modified.
ResourcePackageArchiveResult materializeIncrementalResourcePackage(
	const ResourcePackage& expectedPackage,
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
