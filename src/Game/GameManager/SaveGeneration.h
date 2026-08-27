#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class SaveGenerationContentKind
{
	Any,
	NonEmpty,
	Ini
};

enum class SaveGenerationReferenceScope
{
	SourceGeneration,
	Resource
};

enum class SaveGenerationReferenceValueFormat
{
	Direct,
	OptionalNonNegativeInteger
};

using SaveGenerationContentValidator = std::function<bool(
	const std::string& path, const char* data, int length)>;

enum class SaveGenerationError
{
	None,
	InvalidLimits,
	InvalidRule,
	UnsafeSourceDirectory,
	SourceOutsideSaveRoot,
	SourceRecoveryFailed,
	FileCountLimitExceeded,
	CaseCollidingSourceFileName,
	UnsafeSourceFileName,
	SourceFileReadOrLimitFailed,
	TotalByteLimitExceeded,
	GameIniMissing,
	GameIniReadOrLimitFailed,
	GameIniInvalid,
	RequiredFileMissing,
	RequiredFileReadOrLimitFailed,
	RequiredFileInvalid,
	ReferenceKeyMissing,
	ReferenceValueInvalid,
	UnsafeReferencePath,
	ReferencedFileMissing,
	ReferencedFileReadOrLimitFailed,
	ReferencedFileInvalid,
	UnsafeDestinationDirectory,
	DestinationOutsideSaveRoot,
	SourceEqualsDestination,
	Cancelled,
	DestinationRecoveryFailed,
	UnsafeExcludedFileName,
	RequiredFileExcluded,
	PublicationFailed
};

struct SaveGenerationLimits
{
	std::size_t maximumFileCount = 0;
	std::uint64_t maximumTotalBytes = 0;
	int maximumSingleFileBytes = 0;
};

// File rules describe compatibility requirements beyond the mandatory game.ini.
// A listed optional file is validated when present; files not listed here are
// preserved without assuming whether an older save format needs them.
struct SaveGenerationFileRule
{
	std::string fileName;
	bool required = true;
	SaveGenerationContentKind contentKind =
		SaveGenerationContentKind::Any;
	// Zero inherits SaveGenerationLimits::maximumSingleFileBytes.
	int maximumBytes = 0;
	// Optional read-only domain validator for formats such as MAP or bounded
	// NPC/OBJ INI. It must not mutate the live world or write through File.
	SaveGenerationContentValidator validator;
};

struct SaveGenerationReferenceCandidate
{
	SaveGenerationReferenceScope scope =
		SaveGenerationReferenceScope::SourceGeneration;
	std::string prefix;
	std::string suffix;
};

// Reference rules map a game.ini value to ordered candidate files. The first
// existing candidate is authoritative: invalid content does not silently fall
// through to a lower-priority template.
struct SaveGenerationReferenceRule
{
	std::string section;
	std::string key;
	bool required = true;
	// A required key and non-empty safe value may still describe a legacy
	// empty collection by naming no physical file. Existing candidates remain
	// authoritative and must pass their normal read and content validation.
	bool allowMissingReferencedFile = false;
	// Explicit compatibility default for legacy game.ini files that omit a key.
	bool useDefaultValueWhenMissing = false;
	// Legacy INIReader integer fields use their caller default when the stored
	// text is empty or cannot be parsed.
	bool useDefaultValueWhenInvalid = false;
	std::string defaultValue;
	SaveGenerationReferenceValueFormat valueFormat =
		SaveGenerationReferenceValueFormat::Direct;
	SaveGenerationContentKind contentKind =
		SaveGenerationContentKind::Any;
	// Zero inherits SaveGenerationLimits::maximumSingleFileBytes.
	int maximumBytes = 0;
	std::vector<SaveGenerationReferenceCandidate> candidates;
	SaveGenerationContentValidator validator;
};

struct SaveGenerationPreflightPolicy
{
	SaveGenerationLimits limits;
	std::vector<SaveGenerationFileRule> fileRules;
	std::vector<SaveGenerationReferenceRule> referenceRules;
	// Optional cooperative cancellation for background snapshot/preflight work.
	// It is checked only at safe file/rule boundaries and never after publish.
	std::function<bool()> cancellationRequested;
};

struct SaveGenerationResolvedReference
{
	std::string section;
	std::string key;
	std::string value;
	std::string path;
	SaveGenerationReferenceScope scope =
		SaveGenerationReferenceScope::SourceGeneration;
};

struct SaveGenerationResult
{
	SaveGenerationError error = SaveGenerationError::None;
	std::string sourceDirectory;
	std::string destinationDirectory;
	std::string errorPath;
	std::string errorSection;
	std::string errorKey;
	std::size_t fileCount = 0;
	std::uint64_t totalBytes = 0;
	std::vector<SaveGenerationResolvedReference> resolvedReferences;

	bool succeeded() const
	{
		return error == SaveGenerationError::None;
	}
};

class SaveGeneration
{
public:
	// Canonicalizes a virtual save generation as save/<one-or-more
	// non-empty components>. Empty and "." components are rejected so aliases
	// cannot bypass root or source/destination equality checks.
	static bool NormalizeGenerationDirectory(
		const std::string& directoryName,
		std::string& normalizedDirectory);

	// Preflight is confined to a virtual save/** generation. It recovers any
	// interrupted File directory-copy transaction, applies caller-supplied
	// compatibility rules, and never copies into the live save directory.
	static SaveGenerationResult Preflight(
		const std::string& sourceDirectory,
		const SaveGenerationPreflightPolicy& policy);

	// Publish replaces destinationDirectory through File::copyDirectoryFiles().
	// Both directories must remain below the virtual save root so editor-run
	// sessions cannot escape their isolated save directory.
	static SaveGenerationResult Publish(
		const std::string& sourceDirectory,
		const std::string& destinationDirectory,
		const SaveGenerationPreflightPolicy& policy,
		const std::vector<std::string>& excludedFileNames = {});

	static const char* DescribeError(SaveGenerationError error);
};
