#include "SaveGeneration.h"

#include "../../File/File.h"
#include "../../File/INIReader.h"
#include "../GameTypes.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace
{
std::string normalizeVirtualPathKey(std::string path)
{
	for (char& character : path)
	{
		if (character == '/')
		{
			character = '\\';
		}
		else if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	while (!path.empty() && path.back() == '\\')
	{
		path.pop_back();
	}
	return path;
}

bool isSafeGenerationFileName(const std::string& fileName)
{
	return !fileName.empty() &&
		fileName.find('\\') == std::string::npos &&
		fileName.find('/') == std::string::npos &&
		File::isSafeResourcePath(fileName);
}

std::string joinVirtualPath(
	const std::string& directoryName,
	const std::string& fileName)
{
	if (directoryName.empty() || fileName.empty())
	{
		return "";
	}
	if (directoryName.back() == '\\' ||
		directoryName.back() == '/')
	{
		return directoryName + fileName;
	}
	return directoryName + "\\" + fileName;
}

bool sameVirtualPath(
	const std::string& first,
	const std::string& second)
{
	std::string normalizedFirst;
	std::string normalizedSecond;
	return SaveGeneration::NormalizeGenerationDirectory(
			first, normalizedFirst) &&
		SaveGeneration::NormalizeGenerationDirectory(
			second, normalizedSecond) &&
		normalizedFirst == normalizedSecond;
}

bool sameGenerationFileName(
	const std::string& first,
	const std::string& second)
{
	return normalizeVirtualPathKey(first) ==
		normalizeVirtualPathKey(second);
}

std::string generationFileNameFromPath(
	const std::string& path)
{
	const std::size_t separator = path.find_last_of("\\/");
	return separator == std::string::npos
		? path
		: path.substr(separator + 1);
}

std::string lowerAscii(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	return value;
}

bool iniHasKey(
	const INIReader& ini,
	const std::string& section,
	const std::string& key)
{
	const std::string normalizedKey = lowerAscii(key);
	const std::vector<std::string> keys =
		ini.GetSectionKeys(section);
	return std::find(keys.begin(), keys.end(),
		normalizedKey) != keys.end();
}

int effectiveMaximumBytes(
	int ruleMaximumBytes,
	const SaveGenerationLimits& limits)
{
	return ruleMaximumBytes > 0
		? ruleMaximumBytes
		: limits.maximumSingleFileBytes;
}

bool iniContentIsValid(
	const std::unique_ptr<char[]>& data,
	int length)
{
	if (length <= 0 || data == nullptr ||
		std::find(
			data.get(),
			data.get() + length,
			'\0') != data.get() + length)
	{
		return false;
	}
	INIReader ini(data);
	return ini.ParseError() == 0;
}

bool contentIsValid(
	const std::string& path,
	const std::unique_ptr<char[]>& data,
	int length,
	SaveGenerationContentKind contentKind,
	const SaveGenerationContentValidator& validator)
{
	bool valid = false;
	switch (contentKind)
	{
	case SaveGenerationContentKind::Any:
		valid = length >= 0 &&
			(data != nullptr || length == 0);
		break;
	case SaveGenerationContentKind::NonEmpty:
		valid = length > 0 && data != nullptr;
		break;
	case SaveGenerationContentKind::Ini:
		valid = iniContentIsValid(data, length);
		break;
	}
	if (!valid || !validator)
	{
		return valid;
	}
	try
	{
		return validator(path, data.get(), length);
	}
	catch (...)
	{
		return false;
	}
}

bool formatReferenceValue(
	const std::string& value,
	SaveGenerationReferenceValueFormat format,
	std::string& formattedValue)
{
	formattedValue.clear();
	if (format == SaveGenerationReferenceValueFormat::Direct)
	{
		formattedValue = value;
		return true;
	}

	if (value.empty())
	{
		return false;
	}
	long parsed = -1;
	try
	{
		// INIReader::GetInteger uses std::stol(base 0) without requiring
		// full-string consumption. Resolve the exact file that runtime load
		// will select for legacy hexadecimal/octal values as well.
		parsed = std::stol(value, nullptr, 0);
	}
	catch (...)
	{
		return false;
	}
	if (parsed <
			(std::numeric_limits<int>::min)() ||
		parsed > (std::numeric_limits<int>::max)())
	{
		return false;
	}
	if (parsed >= 0)
	{
		formattedValue = std::to_string(parsed);
	}
	return true;
}

bool ruleMaximumIsValid(
	int maximumBytes,
	const SaveGenerationLimits& limits)
{
	return maximumBytes >= 0 &&
		effectiveMaximumBytes(maximumBytes, limits) > 0;
}

void setSaveGenerationError(
	SaveGenerationResult& result,
	SaveGenerationError error,
	const std::string& path = "",
	const std::string& section = "",
	const std::string& key = "")
{
	result.error = error;
	result.errorPath = path;
	result.errorSection = section;
	result.errorKey = key;
}

File::DirectoryCopyLimits directoryCopyLimits(
	const SaveGenerationPreflightPolicy& policy)
{
	File::DirectoryCopyLimits copyLimits;
	copyLimits.maximumFileCount =
		policy.limits.maximumFileCount;
	copyLimits.maximumTotalBytes =
		policy.limits.maximumTotalBytes;
	copyLimits.maximumSingleFileBytes =
		policy.limits.maximumSingleFileBytes;
	copyLimits.cancellationRequested =
		policy.cancellationRequested;
	return copyLimits;
}

bool cancellationIsRequested(
	const SaveGenerationPreflightPolicy& policy) noexcept
{
	if (!policy.cancellationRequested)
	{
		return false;
	}
	try
	{
		return policy.cancellationRequested();
	}
	catch (...)
	{
		return true;
	}
}
}

bool SaveGeneration::NormalizeGenerationDirectory(
	const std::string& directoryName,
	std::string& normalizedDirectory)
{
	normalizedDirectory.clear();
	if (!File::isSafeResourcePath(directoryName))
	{
		return false;
	}

	std::string path = normalizeVirtualPathKey(
		directoryName);
	if (path.empty())
	{
		return false;
	}

	std::vector<std::string> components;
	std::size_t componentBegin = 0;
	while (componentBegin < path.size())
	{
		const std::size_t separator =
			path.find('\\', componentBegin);
		const std::size_t componentEnd =
			separator == std::string::npos
				? path.size()
				: separator;
		const std::string component =
			path.substr(
				componentBegin,
				componentEnd - componentBegin);
		if (component.empty() ||
			component == "." ||
			component == "..")
		{
			return false;
		}
		components.push_back(component);
		if (separator == std::string::npos)
		{
			break;
		}
		componentBegin = separator + 1;
	}
	if (components.size() < 2 ||
		components.front() != "save")
	{
		return false;
	}

	normalizedDirectory = components.front();
	for (std::size_t index = 1;
		index < components.size();
		++index)
	{
		normalizedDirectory += "\\";
		normalizedDirectory += components[index];
	}
	return true;
}

SaveGenerationResult SaveGeneration::Preflight(
	const std::string& sourceDirectory,
	const SaveGenerationPreflightPolicy& policy)
{
	SaveGenerationResult result;
	result.sourceDirectory = sourceDirectory;

	if (policy.limits.maximumFileCount == 0 ||
		policy.limits.maximumTotalBytes == 0 ||
		policy.limits.maximumSingleFileBytes <= 0)
	{
		setSaveGenerationError(
			result, SaveGenerationError::InvalidLimits);
		return result;
	}
	if (!File::isSafeResourcePath(sourceDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::UnsafeSourceDirectory,
			sourceDirectory);
		return result;
	}
	std::string normalizedSourceDirectory;
	if (!NormalizeGenerationDirectory(
			sourceDirectory,
			normalizedSourceDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::SourceOutsideSaveRoot,
			sourceDirectory);
		return result;
	}
	for (const SaveGenerationFileRule& rule :
		policy.fileRules)
	{
		if (!isSafeGenerationFileName(rule.fileName) ||
			!ruleMaximumIsValid(
				rule.maximumBytes, policy.limits))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::InvalidRule,
				rule.fileName);
			return result;
		}
	}
	for (const SaveGenerationReferenceRule& rule :
		policy.referenceRules)
	{
		if (rule.section.empty() ||
			rule.key.empty() ||
			rule.candidates.empty() ||
			!ruleMaximumIsValid(
				rule.maximumBytes, policy.limits))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::InvalidRule,
				"",
				rule.section,
				rule.key);
			return result;
		}
	}
	if (cancellationIsRequested(policy))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::Cancelled,
			sourceDirectory);
		return result;
	}

	if (!File::recoverDirectoryCopy(sourceDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::SourceRecoveryFailed,
			sourceDirectory);
		return result;
	}

	std::vector<std::string> sourceFiles;
	std::string collidingFileName;
	bool fileCountLimitExceeded = false;
	if (!File::listFilesRejectingCaseCollisions(
			sourceDirectory,
			sourceFiles,
			&collidingFileName,
			policy.limits.maximumFileCount,
			&fileCountLimitExceeded))
	{
		setSaveGenerationError(
			result,
			fileCountLimitExceeded
				? SaveGenerationError::
					FileCountLimitExceeded
				: SaveGenerationError::
					CaseCollidingSourceFileName,
			fileCountLimitExceeded
				? sourceDirectory
				: collidingFileName);
		return result;
	}
	if (cancellationIsRequested(policy))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::Cancelled,
			sourceDirectory);
		return result;
	}
	result.fileCount = sourceFiles.size();
	if (result.fileCount >
		policy.limits.maximumFileCount)
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::FileCountLimitExceeded,
			sourceDirectory);
		return result;
	}

	for (const std::string& fileName : sourceFiles)
	{
		if (cancellationIsRequested(policy))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::Cancelled,
				sourceDirectory);
			return result;
		}
		if (!isSafeGenerationFileName(fileName))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::UnsafeSourceFileName,
				fileName);
			return result;
		}
		const std::string path =
			joinVirtualPath(sourceDirectory, fileName);
		std::unique_ptr<char[]> data;
		int length = 0;
		if (!File::readFile(
				path,
				data,
				length,
				policy.limits.maximumSingleFileBytes) ||
			length < 0)
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::
					SourceFileReadOrLimitFailed,
				path);
			return result;
		}
		const std::uint64_t fileBytes =
			static_cast<std::uint64_t>(length);
		if (result.totalBytes >
				policy.limits.maximumTotalBytes ||
			fileBytes >
				policy.limits.maximumTotalBytes -
					result.totalBytes)
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::TotalByteLimitExceeded,
				path);
			return result;
		}
		result.totalBytes += fileBytes;
	}

	const std::string gameIniPath =
		joinVirtualPath(sourceDirectory, GLOBAL_INI);
	if (cancellationIsRequested(policy))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::Cancelled,
			sourceDirectory);
		return result;
	}
	if (!File::fileExist(gameIniPath))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::GameIniMissing,
			gameIniPath);
		return result;
	}
	std::unique_ptr<char[]> gameIniData;
	int gameIniLength = 0;
	if (!File::readFile(
			gameIniPath,
			gameIniData,
			gameIniLength,
			policy.limits.maximumSingleFileBytes))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::GameIniReadOrLimitFailed,
			gameIniPath);
		return result;
	}
	if (!iniContentIsValid(gameIniData, gameIniLength))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::GameIniInvalid,
			gameIniPath);
		return result;
	}
	INIReader gameIni(gameIniData);

	for (const SaveGenerationFileRule& rule :
		policy.fileRules)
	{
		if (cancellationIsRequested(policy))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::Cancelled,
				sourceDirectory);
			return result;
		}
		const std::string path =
			joinVirtualPath(sourceDirectory, rule.fileName);
		if (!File::fileExist(path))
		{
			if (rule.required)
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::RequiredFileMissing,
					path);
				return result;
			}
			continue;
		}
		std::unique_ptr<char[]> data;
		int length = 0;
		if (!File::readFile(
				path,
				data,
				length,
				effectiveMaximumBytes(
					rule.maximumBytes, policy.limits)))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::
					RequiredFileReadOrLimitFailed,
				path);
			return result;
		}
		if (!contentIsValid(
				path,
				data,
				length,
				rule.contentKind,
				rule.validator))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::RequiredFileInvalid,
				path);
			return result;
		}
	}

	for (const SaveGenerationReferenceRule& rule :
		policy.referenceRules)
	{
		if (cancellationIsRequested(policy))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::Cancelled,
				sourceDirectory);
			return result;
		}
		const bool keyExists =
			iniHasKey(gameIni, rule.section, rule.key);
		if (!keyExists &&
			!rule.useDefaultValueWhenMissing)
		{
			if (rule.required)
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::ReferenceKeyMissing,
					gameIniPath,
					rule.section,
					rule.key);
				return result;
			}
			continue;
		}
		const std::string storedValue = keyExists
			? gameIni.Get(rule.section, rule.key, "")
			: rule.defaultValue;
		std::string value = storedValue;
		if (value.empty() &&
			rule.useDefaultValueWhenInvalid)
		{
			value = rule.defaultValue;
		}
		if (value.empty())
		{
			if (rule.required)
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::
						ReferenceValueInvalid,
					gameIniPath,
					rule.section,
					rule.key);
				return result;
			}
			continue;
		}
		std::string formattedValue;
		if (!formatReferenceValue(
			value, rule.valueFormat, formattedValue))
		{
			if (rule.useDefaultValueWhenInvalid &&
				value != rule.defaultValue &&
				formatReferenceValue(
					rule.defaultValue,
					rule.valueFormat,
					formattedValue))
			{
				value = rule.defaultValue;
			}
			else
			{
			setSaveGenerationError(
				result,
				SaveGenerationError::ReferenceValueInvalid,
				gameIniPath,
				rule.section,
				rule.key);
			return result;
			}
		}

		bool referenceResolved = false;
		for (const SaveGenerationReferenceCandidate& candidate :
			rule.candidates)
		{
			const std::string relativePath =
				candidate.prefix + formattedValue +
				candidate.suffix;
			const bool pathIsSafe =
				candidate.scope ==
					SaveGenerationReferenceScope::
						SourceGeneration
				? isSafeGenerationFileName(relativePath)
				: File::isSafeResourcePath(relativePath);
			if (!pathIsSafe)
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::UnsafeReferencePath,
					relativePath,
					rule.section,
					rule.key);
				return result;
			}
			const std::string path =
				candidate.scope ==
					SaveGenerationReferenceScope::
						SourceGeneration
				? joinVirtualPath(
					sourceDirectory, relativePath)
				: relativePath;
			if (!File::fileExist(path))
			{
				continue;
			}
			std::unique_ptr<char[]> data;
			int length = 0;
			if (!File::readFile(
					path,
					data,
					length,
					effectiveMaximumBytes(
						rule.maximumBytes,
						policy.limits)))
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::
						ReferencedFileReadOrLimitFailed,
					path,
					rule.section,
					rule.key);
				return result;
			}
			if (!contentIsValid(
					path,
					data,
					length,
					rule.contentKind,
					rule.validator))
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::
						ReferencedFileInvalid,
					path,
					rule.section,
					rule.key);
				return result;
			}
			SaveGenerationResolvedReference reference;
			reference.section = rule.section;
			reference.key = rule.key;
			reference.value = value;
			reference.path = path;
			reference.scope = candidate.scope;
			result.resolvedReferences.push_back(
				std::move(reference));
			referenceResolved = true;
			break;
		}
		if (!referenceResolved &&
			rule.required &&
			!rule.allowMissingReferencedFile)
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::ReferencedFileMissing,
				"",
				rule.section,
				rule.key);
			return result;
		}
	}

	return result;
}

SaveGenerationResult SaveGeneration::Publish(
	const std::string& sourceDirectory,
	const std::string& destinationDirectory,
	const SaveGenerationPreflightPolicy& policy,
	const std::vector<std::string>& excludedFileNames)
{
	SaveGenerationResult result =
		Preflight(sourceDirectory, policy);
	result.destinationDirectory = destinationDirectory;
	if (!result.succeeded())
	{
		return result;
	}
	if (!File::isSafeResourcePath(destinationDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::UnsafeDestinationDirectory,
			destinationDirectory);
		return result;
	}
	std::string normalizedDestinationDirectory;
	if (!NormalizeGenerationDirectory(
			destinationDirectory,
			normalizedDestinationDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::DestinationOutsideSaveRoot,
			destinationDirectory);
		return result;
	}
	if (sameVirtualPath(
			sourceDirectory, destinationDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::SourceEqualsDestination,
			destinationDirectory);
		return result;
	}

	for (const std::string& excludedFileName :
		excludedFileNames)
	{
		if (!isSafeGenerationFileName(excludedFileName))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::UnsafeExcludedFileName,
				excludedFileName);
			return result;
		}
		if (sameGenerationFileName(
				excludedFileName, GLOBAL_INI))
		{
			setSaveGenerationError(
				result,
				SaveGenerationError::RequiredFileExcluded,
				excludedFileName);
			return result;
		}
		for (const SaveGenerationFileRule& rule :
			policy.fileRules)
		{
			if (rule.required &&
				sameGenerationFileName(
					excludedFileName, rule.fileName))
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::
						RequiredFileExcluded,
					excludedFileName);
				return result;
			}
		}
		for (const SaveGenerationResolvedReference& reference :
			result.resolvedReferences)
		{
			if (reference.scope ==
					SaveGenerationReferenceScope::
						SourceGeneration &&
				sameGenerationFileName(
					excludedFileName,
					generationFileNameFromPath(
						reference.path)))
			{
				setSaveGenerationError(
					result,
					SaveGenerationError::
						RequiredFileExcluded,
					excludedFileName,
					reference.section,
					reference.key);
				return result;
			}
		}
	}

	if (!File::recoverDirectoryCopy(destinationDirectory))
	{
		setSaveGenerationError(
			result,
			SaveGenerationError::DestinationRecoveryFailed,
			destinationDirectory);
		return result;
	}
	if (!File::copyDirectoryFiles(
			sourceDirectory,
			destinationDirectory,
			excludedFileNames,
			{},
			directoryCopyLimits(policy)))
	{
		setSaveGenerationError(
			result,
			cancellationIsRequested(policy)
				? SaveGenerationError::Cancelled
				: SaveGenerationError::PublicationFailed,
			destinationDirectory);
		return result;
	}
	return result;
}

const char* SaveGeneration::DescribeError(
	SaveGenerationError error)
{
	switch (error)
	{
	case SaveGenerationError::None:
		return "none";
	case SaveGenerationError::InvalidLimits:
		return "invalid limits";
	case SaveGenerationError::InvalidRule:
		return "invalid rule";
	case SaveGenerationError::UnsafeSourceDirectory:
		return "unsafe source directory";
	case SaveGenerationError::SourceOutsideSaveRoot:
		return "source is outside save root";
	case SaveGenerationError::SourceRecoveryFailed:
		return "source recovery failed";
	case SaveGenerationError::FileCountLimitExceeded:
		return "file count limit exceeded";
	case SaveGenerationError::CaseCollidingSourceFileName:
		return "source contains case-colliding file names";
	case SaveGenerationError::UnsafeSourceFileName:
		return "unsafe source file name";
	case SaveGenerationError::SourceFileReadOrLimitFailed:
		return "source file read or size limit failed";
	case SaveGenerationError::TotalByteLimitExceeded:
		return "total byte limit exceeded";
	case SaveGenerationError::GameIniMissing:
		return "game.ini is missing";
	case SaveGenerationError::GameIniReadOrLimitFailed:
		return "game.ini read or size limit failed";
	case SaveGenerationError::GameIniInvalid:
		return "game.ini is invalid";
	case SaveGenerationError::RequiredFileMissing:
		return "required file is missing";
	case SaveGenerationError::RequiredFileReadOrLimitFailed:
		return "required file read or size limit failed";
	case SaveGenerationError::RequiredFileInvalid:
		return "required file is invalid";
	case SaveGenerationError::ReferenceKeyMissing:
		return "game.ini reference key is missing";
	case SaveGenerationError::ReferenceValueInvalid:
		return "game.ini reference value is invalid";
	case SaveGenerationError::UnsafeReferencePath:
		return "reference path is unsafe";
	case SaveGenerationError::ReferencedFileMissing:
		return "referenced file is missing";
	case SaveGenerationError::ReferencedFileReadOrLimitFailed:
		return "referenced file read or size limit failed";
	case SaveGenerationError::ReferencedFileInvalid:
		return "referenced file is invalid";
	case SaveGenerationError::UnsafeDestinationDirectory:
		return "unsafe destination directory";
	case SaveGenerationError::DestinationOutsideSaveRoot:
		return "destination is outside save root";
	case SaveGenerationError::SourceEqualsDestination:
		return "source equals destination";
	case SaveGenerationError::Cancelled:
		return "operation cancelled";
	case SaveGenerationError::DestinationRecoveryFailed:
		return "destination recovery failed";
	case SaveGenerationError::UnsafeExcludedFileName:
		return "unsafe excluded file name";
	case SaveGenerationError::RequiredFileExcluded:
		return "required file is excluded";
	case SaveGenerationError::PublicationFailed:
		return "publication failed";
	}
	return "unknown";
}
