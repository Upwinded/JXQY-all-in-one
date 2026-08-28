#pragma once

#include "SemanticVersion.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ModRelease
{
struct ModReleaseMetadata
{
	std::string displayVersion;
	std::string releaseDate;
	std::string minimumEngineVersion;
	std::string coverPath;
	std::string descriptionFilePath;
	// Local updater receipt for the last successfully installed online ZIP.
	// It is not release identity and is removed again when a package is published.
	std::string installedArtifactCrc32;
	// Local receipt for the optional incremental overlay corresponding to the
	// installed full package. Publication removes this field as well.
	std::string installedIncrementalArtifactCrc32;
};

enum class MetadataField
{
	DisplayVersion,
	ReleaseDate,
	MinimumEngineVersion,
	CoverPath,
	DescriptionFilePath
};

enum class MetadataValidationError
{
	InvalidUtf8,
	ContainsControlCharacter,
	InvalidIsoDate,
	InvalidSemanticVersion,
	UnsafeRelativePath
};

struct MetadataValidationIssue
{
	MetadataField field;
	MetadataValidationError error;
	SemanticVersionParseError semanticVersionError =
		SemanticVersionParseError::None;
};

std::vector<MetadataValidationIssue> validateMetadata(
	const ModReleaseMetadata& metadata);

// Identifier syntax helper for program update targets and generated file names.
bool isValidUpdateTargetIdentifier(std::string_view text) noexcept;

// Empty means "not declared" to validateMetadata and is therefore checked by
// the caller before invoking this strict YYYY-MM-DD validator.
bool isValidIsoReleaseDate(std::string_view text) noexcept;

enum class CompatibilityStatus
{
	LegacyCompatible,
	Compatible,
	RequiresNewerEngine,
	InvalidMinimumEngineVersion,
	InvalidCurrentEngineVersion
};

struct CompatibilityResult
{
	CompatibilityStatus status = CompatibilityStatus::LegacyCompatible;
	std::optional<SemanticVersion> minimumVersion;
	std::optional<SemanticVersion> currentVersion;

};

CompatibilityResult evaluateCompatibility(
	const ModReleaseMetadata& metadata,
	std::string_view currentEngineVersion);
}
