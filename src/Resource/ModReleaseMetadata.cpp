#include "ModReleaseMetadata.h"

#include "../File/ResourcePathSafety.h"
#include "../File/StrictRelativeResourcePath.h"

namespace
{
bool hasDisallowedTextControl(const std::string& text)
{
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		const unsigned char character =
			static_cast<unsigned char>(text[index]);
		if (character < 0x20 || character == 0x7F)
		{
			return true;
		}
		if (character == 0xC2 && index + 1 < text.size())
		{
			const unsigned char nextCharacter =
				static_cast<unsigned char>(text[index + 1]);
			if (nextCharacter >= 0x80 && nextCharacter <= 0x9F)
			{
				return true;
			}
		}
	}
	return false;
}

bool isAsciiDigit(char character)
{
	return character >= '0' && character <= '9';
}

bool isLowerAsciiLetter(char character)
{
	return character >= 'a' && character <= 'z';
}

void appendPathIssue(std::vector<ModRelease::MetadataValidationIssue>& issues,
	ModRelease::MetadataField field, const std::string& path)
{
	const ResourcePathSafety::StrictRelativePathResult result =
		ResourcePathSafety::normalizeStrictRelativeResourcePath(path);
	if (result.succeeded())
	{
		return;
	}
	const ModRelease::MetadataValidationError error =
		result.status == ResourcePathSafety::StrictRelativePathStatus::InvalidUtf8
		? ModRelease::MetadataValidationError::InvalidUtf8
		: ModRelease::MetadataValidationError::UnsafeRelativePath;
	issues.push_back({ field, error,
		ModRelease::SemanticVersionParseError::None });
}
}

namespace ModRelease
{
bool isValidUpdateTargetIdentifier(std::string_view text) noexcept
{
	if (text.empty() || text.size() > 128)
	{
		return false;
	}
	if (!isLowerAsciiLetter(text.front()) && !isAsciiDigit(text.front()))
	{
		return false;
	}
	if (!isLowerAsciiLetter(text.back()) && !isAsciiDigit(text.back()))
	{
		return false;
	}
	bool previousWasSeparator = false;
	for (char character : text)
	{
		const bool separator =
			character == '.' || character == '-' || character == '_';
		if (!isLowerAsciiLetter(character) && !isAsciiDigit(character) &&
			!separator)
		{
			return false;
		}
		if (separator && previousWasSeparator)
		{
			return false;
		}
		previousWasSeparator = separator;
	}
	return true;
}

bool isValidIsoReleaseDate(std::string_view text) noexcept
{
	if (text.size() != 10 || text[4] != '-' || text[7] != '-')
	{
		return false;
	}
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		if (index == 4 || index == 7)
		{
			continue;
		}
		if (!isAsciiDigit(text[index]))
		{
			return false;
		}
	}

	const int year = (text[0] - '0') * 1000 +
		(text[1] - '0') * 100 +
		(text[2] - '0') * 10 +
		(text[3] - '0');
	const int month = (text[5] - '0') * 10 + (text[6] - '0');
	const int day = (text[8] - '0') * 10 + (text[9] - '0');
	if (year == 0 || month < 1 || month > 12 || day < 1)
	{
		return false;
	}

	static constexpr int DaysPerMonth[] =
		{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	int maximumDay = DaysPerMonth[month - 1];
	const bool leapYear = year % 4 == 0 &&
		(year % 100 != 0 || year % 400 == 0);
	if (month == 2 && leapYear)
	{
		maximumDay = 29;
	}
	return day <= maximumDay;
}

std::vector<MetadataValidationIssue> validateMetadata(
	const ModReleaseMetadata& metadata)
{
	std::vector<MetadataValidationIssue> issues;

	if (!metadata.displayVersion.empty())
	{
		if (!ResourcePathSafety::isValidUtf8(metadata.displayVersion))
		{
			issues.push_back({ MetadataField::DisplayVersion,
				MetadataValidationError::InvalidUtf8,
				SemanticVersionParseError::None });
		}
		else if (hasDisallowedTextControl(metadata.displayVersion))
		{
			issues.push_back({ MetadataField::DisplayVersion,
				MetadataValidationError::ContainsControlCharacter,
				SemanticVersionParseError::None });
		}
	}

	if (!metadata.releaseDate.empty() &&
		!isValidIsoReleaseDate(metadata.releaseDate))
	{
		issues.push_back({ MetadataField::ReleaseDate,
			MetadataValidationError::InvalidIsoDate,
			SemanticVersionParseError::None });
	}

	if (!metadata.minimumEngineVersion.empty())
	{
		const SemanticVersionParseResult parsed =
			parseSemanticVersion(metadata.minimumEngineVersion);
		if (!parsed.succeeded())
		{
			issues.push_back({ MetadataField::MinimumEngineVersion,
				MetadataValidationError::InvalidSemanticVersion,
				parsed.error });
		}
	}

	if (!metadata.coverPath.empty())
	{
		appendPathIssue(issues, MetadataField::CoverPath,
			metadata.coverPath);
	}
	if (!metadata.descriptionFilePath.empty())
	{
		appendPathIssue(issues, MetadataField::DescriptionFilePath,
			metadata.descriptionFilePath);
	}

	return issues;
}

CompatibilityResult evaluateCompatibility(
	const ModReleaseMetadata& metadata,
	std::string_view currentEngineVersion)
{
	CompatibilityResult result;
	if (metadata.minimumEngineVersion.empty())
	{
		result.status = CompatibilityStatus::LegacyCompatible;
		return result;
	}

	const SemanticVersionParseResult minimum =
		parseSemanticVersion(metadata.minimumEngineVersion);
	if (!minimum.succeeded())
	{
		result.status = CompatibilityStatus::InvalidMinimumEngineVersion;
		return result;
	}
	result.minimumVersion = minimum.version;

	const SemanticVersionParseResult current =
		parseSemanticVersion(currentEngineVersion);
	if (!current.succeeded())
	{
		result.status = CompatibilityStatus::InvalidCurrentEngineVersion;
		return result;
	}
	result.currentVersion = current.version;

	if (compareSemanticVersionPrecedence(minimum.version, current.version) > 0)
	{
		result.status = CompatibilityStatus::RequiresNewerEngine;
	}
	else
	{
		result.status = CompatibilityStatus::Compatible;
	}
	return result;
}
}
