#pragma once

#include "../Resource/SemanticVersion.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OnlineUpdate
{
constexpr std::size_t MaximumCatalogBytes = 1 * 1024 * 1024;
constexpr std::size_t MaximumResourcePackageCount = 512;
constexpr std::size_t MaximumProgramPackageCount = 128;
constexpr std::size_t MaximumDependencyCountPerPackage = 64;
constexpr std::size_t MaximumCommonVersionFileBytes = 4 * 1024;

struct IncrementalResourcePackage
{
	std::string artifactPath;
	std::uint64_t artifactSize = 0;
	std::string crc32Hex;
};

struct ResourcePackage
{
	std::string gameId;
	std::string displayName;
	std::string author;
	std::string versionText;
	std::string minimumEngineVersionText;
	ModRelease::SemanticVersion minimumEngineVersion;
	std::string artifactPath;
	std::uint64_t artifactSize = 0;
	std::string crc32Hex;
	// Optional overlay package applied after a changed full package, or by itself
	// when the full-package receipt already matches. Absence is the Schema 1
	// default.
	std::optional<IncrementalResourcePackage> incrementalPackage;
	std::vector<std::string> dependencyGameIds;
	bool resourceOnly = false;
	std::string releaseNotes;
};

struct ProgramPackage
{
	std::string target;
	std::string versionText;
	ModRelease::SemanticVersion version;
	std::string artifactPath;
	std::uint64_t artifactSize = 0;
	std::string crc32Hex;
	std::string releaseNotes;
};

struct CommonPackage
{
	std::string versionText;
	std::string artifactPath;
	std::uint64_t artifactSize = 0;
	std::string crc32Hex;
	std::string releaseNotes;
};

struct CommonPackageInstallation
{
	std::string versionText;
	std::string installedArtifactCrc32;
};

struct Catalog
{
	int schemaVersion = 0;
	// Resource keys are ASCII-case-folded Game.Id values. ResourcePackage keeps
	// the original spelling declared by the section name.
	std::map<std::string, ResourcePackage> resourcePackages;
	// Each platform target has exactly one online program package.
	std::map<std::string, ProgramPackage> programPackages;
	std::optional<CommonPackage> commonPackage;
};

enum class CatalogParseError
{
	None,
	Empty,
	TooLarge,
	InvalidUtf8,
	InvalidIni,
	UnsupportedSchema,
	TooManyPackages,
	InvalidList,
	DuplicateIdentifier,
	MissingField,
	InvalidIdentifier,
	InvalidDisplayText,
	InvalidVersion,
	InvalidArtifactSize,
	UnsafeArtifactPath,
	InvalidCrc32,
	InvalidDependency,
	UnknownDependency,
	DependencyCycle
};

struct CatalogParseIssue
{
	CatalogParseError error = CatalogParseError::None;
	std::string section;
	std::string field;
	std::string value;
};

struct CatalogParseResult
{
	Catalog catalog;
	std::vector<CatalogParseIssue> issues;

	bool succeeded() const noexcept
	{
		return issues.empty();
	}
};

enum class ProgramUpdateStatus
{
	UpdateAvailable,
	UpToDate,
	TargetNotFound,
	InvalidInput
};

struct ProgramUpdateCheck
{
	ProgramUpdateStatus status = ProgramUpdateStatus::InvalidInput;
	const ProgramPackage* package = nullptr;
	// Positive when the online version is newer, zero when it has the same
	// precedence, and negative when it is older than the running version.
	int versionComparison = 0;

	bool hasUpdate() const noexcept
	{
		return status == ProgramUpdateStatus::UpdateAvailable &&
			package != nullptr;
	}

	bool hasOnlinePackage() const noexcept
	{
		return package != nullptr;
	}
};

CatalogParseResult parseCatalog(const char* data, std::size_t length);
CatalogParseResult parseCatalog(std::string_view utf8Text);

std::string foldGameId(std::string_view gameId);
bool isValidOnlineGameId(std::string_view gameId);
bool isSafeArtifactPath(std::string_view path) noexcept;
bool isValidCrc32Hex(std::string_view text) noexcept;
bool parseCommonPackageVersion(
	std::string_view utf8Text,
	std::string& versionText);
// Parses the usable common version and its optional local installation receipt.
// A missing or malformed receipt is returned as an empty string so the caller
// can download common again without making the installed content unusable.
bool parseCommonPackageInstallation(
	std::string_view utf8Text,
	CommonPackageInstallation& installation);
bool commonPackageNeedsDownload(
	const Catalog& catalog,
	std::string_view installedArtifactCrc32) noexcept;

// The public semantic Version describes whether the sole online package is
// newer than the running program. It does not select between multiple online
// packages.
ProgramUpdateCheck checkProgramUpdate(
	const Catalog& catalog,
	std::string_view target,
	std::string_view currentVersion) noexcept;
}
