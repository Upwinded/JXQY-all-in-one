#include "SemanticVersion.h"

#include <algorithm>
#include <limits>

namespace
{
bool isAsciiDigit(char character)
{
	return character >= '0' && character <= '9';
}

bool isAsciiLetter(char character)
{
	return (character >= 'a' && character <= 'z') ||
		(character >= 'A' && character <= 'Z');
}

bool isIdentifierCharacter(char character)
{
	return isAsciiDigit(character) || isAsciiLetter(character) || character == '-';
}

bool isNumericIdentifier(std::string_view identifier)
{
	return !identifier.empty() &&
		std::all_of(identifier.begin(), identifier.end(), isAsciiDigit);
}

ModRelease::SemanticVersionParseError parseCoreNumber(std::string_view text,
	std::uint64_t& value)
{
	if (text.empty() ||
		!std::all_of(text.begin(), text.end(), isAsciiDigit))
	{
		return ModRelease::SemanticVersionParseError::InvalidCore;
	}
	if (text.size() > 1 && text.front() == '0')
	{
		return ModRelease::SemanticVersionParseError::LeadingZeroCore;
	}

	std::uint64_t parsedValue = 0;
	for (char character : text)
	{
		const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
		if (parsedValue >
			(std::numeric_limits<std::uint64_t>::max() - digit) / 10)
		{
			return ModRelease::SemanticVersionParseError::NumericOverflow;
		}
		parsedValue = parsedValue * 10 + digit;
	}
	value = parsedValue;
	return ModRelease::SemanticVersionParseError::None;
}

ModRelease::SemanticVersionParseError parseIdentifiers(std::string_view text,
	bool prerelease, std::vector<std::string>& identifiers)
{
	if (text.empty())
	{
		return ModRelease::SemanticVersionParseError::EmptyIdentifier;
	}

	std::size_t start = 0;
	while (start <= text.size())
	{
		const std::size_t separator = text.find('.', start);
		const std::size_t length = separator == std::string_view::npos
			? text.size() - start
			: separator - start;
		const std::string_view identifier = text.substr(start, length);
		if (identifier.empty())
		{
			return ModRelease::SemanticVersionParseError::EmptyIdentifier;
		}
		if (!std::all_of(identifier.begin(), identifier.end(),
			isIdentifierCharacter))
		{
			return ModRelease::SemanticVersionParseError::InvalidIdentifier;
		}
		if (prerelease && isNumericIdentifier(identifier) &&
			identifier.size() > 1 && identifier.front() == '0')
		{
			return ModRelease::SemanticVersionParseError::
				LeadingZeroPrereleaseNumber;
		}
		identifiers.emplace_back(identifier);
		if (separator == std::string_view::npos)
		{
			break;
		}
		start = separator + 1;
	}
	return ModRelease::SemanticVersionParseError::None;
}

int compareUnsignedDecimalStrings(std::string_view left, std::string_view right)
{
	const std::size_t leftNonZero = left.find_first_not_of('0');
	const std::size_t rightNonZero = right.find_first_not_of('0');
	left = leftNonZero == std::string_view::npos
		? std::string_view("0")
		: left.substr(leftNonZero);
	right = rightNonZero == std::string_view::npos
		? std::string_view("0")
		: right.substr(rightNonZero);

	if (left.size() != right.size())
	{
		return left.size() < right.size() ? -1 : 1;
	}
	if (left == right)
	{
		return 0;
	}
	return left < right ? -1 : 1;
}

void appendIdentifiers(std::string& output,
	const std::vector<std::string>& identifiers, char prefix)
{
	if (identifiers.empty())
	{
		return;
	}
	output.push_back(prefix);
	for (std::size_t index = 0; index < identifiers.size(); ++index)
	{
		if (index > 0)
		{
			output.push_back('.');
		}
		output.append(identifiers[index]);
	}
}
}

namespace ModRelease
{
SemanticVersionParseResult parseSemanticVersion(std::string_view text)
{
	SemanticVersionParseResult result;
	if (text.empty())
	{
		result.error = SemanticVersionParseError::Empty;
		return result;
	}

	std::string_view versionAndPrerelease = text;
	std::string_view buildText;
	const std::size_t buildSeparator = text.find('+');
	if (buildSeparator != std::string_view::npos)
	{
		versionAndPrerelease = text.substr(0, buildSeparator);
		buildText = text.substr(buildSeparator + 1);
		if (buildText.find('+') != std::string_view::npos)
		{
			result.error = SemanticVersionParseError::InvalidIdentifier;
			return result;
		}
		result.error = parseIdentifiers(buildText, false,
			result.version.buildIdentifiers);
		if (result.error != SemanticVersionParseError::None)
		{
			return result;
		}
	}

	std::string_view coreText = versionAndPrerelease;
	std::string_view prereleaseText;
	const std::size_t prereleaseSeparator = versionAndPrerelease.find('-');
	if (prereleaseSeparator != std::string_view::npos)
	{
		coreText = versionAndPrerelease.substr(0, prereleaseSeparator);
		prereleaseText = versionAndPrerelease.substr(prereleaseSeparator + 1);
		result.error = parseIdentifiers(prereleaseText, true,
			result.version.prereleaseIdentifiers);
		if (result.error != SemanticVersionParseError::None)
		{
			return result;
		}
	}

	const std::size_t firstDot = coreText.find('.');
	const std::size_t secondDot = firstDot == std::string_view::npos
		? std::string_view::npos
		: coreText.find('.', firstDot + 1);
	if (firstDot == std::string_view::npos ||
		secondDot == std::string_view::npos ||
		coreText.find('.', secondDot + 1) != std::string_view::npos)
	{
		result.error = SemanticVersionParseError::InvalidCore;
		return result;
	}

	result.error = parseCoreNumber(coreText.substr(0, firstDot),
		result.version.major);
	if (result.error != SemanticVersionParseError::None)
	{
		return result;
	}
	result.error = parseCoreNumber(
		coreText.substr(firstDot + 1, secondDot - firstDot - 1),
		result.version.minor);
	if (result.error != SemanticVersionParseError::None)
	{
		return result;
	}
	result.error = parseCoreNumber(coreText.substr(secondDot + 1),
		result.version.patch);
	return result;
}

int compareSemanticVersionPrecedence(const SemanticVersion& left,
	const SemanticVersion& right) noexcept
{
	if (left.major != right.major)
	{
		return left.major < right.major ? -1 : 1;
	}
	if (left.minor != right.minor)
	{
		return left.minor < right.minor ? -1 : 1;
	}
	if (left.patch != right.patch)
	{
		return left.patch < right.patch ? -1 : 1;
	}

	if (left.prereleaseIdentifiers.empty() ||
		right.prereleaseIdentifiers.empty())
	{
		if (left.prereleaseIdentifiers.empty() &&
			right.prereleaseIdentifiers.empty())
		{
			return 0;
		}
		return left.prereleaseIdentifiers.empty() ? 1 : -1;
	}

	const std::size_t sharedCount = std::min(
		left.prereleaseIdentifiers.size(),
		right.prereleaseIdentifiers.size());
	for (std::size_t index = 0; index < sharedCount; ++index)
	{
		const std::string& leftIdentifier =
			left.prereleaseIdentifiers[index];
		const std::string& rightIdentifier =
			right.prereleaseIdentifiers[index];
		const bool leftNumeric = isNumericIdentifier(leftIdentifier);
		const bool rightNumeric = isNumericIdentifier(rightIdentifier);

		int comparison = 0;
		if (leftNumeric && rightNumeric)
		{
			comparison = compareUnsignedDecimalStrings(leftIdentifier,
				rightIdentifier);
		}
		else if (leftNumeric != rightNumeric)
		{
			comparison = leftNumeric ? -1 : 1;
		}
		else if (leftIdentifier != rightIdentifier)
		{
			comparison = leftIdentifier < rightIdentifier ? -1 : 1;
		}
		if (comparison != 0)
		{
			return comparison;
		}
	}

	if (left.prereleaseIdentifiers.size() ==
		right.prereleaseIdentifiers.size())
	{
		return 0;
	}
	return left.prereleaseIdentifiers.size() <
		right.prereleaseIdentifiers.size() ? -1 : 1;
}

std::string formatSemanticVersion(const SemanticVersion& version)
{
	std::string output = std::to_string(version.major) + "." +
		std::to_string(version.minor) + "." +
		std::to_string(version.patch);
	appendIdentifiers(output, version.prereleaseIdentifiers, '-');
	appendIdentifiers(output, version.buildIdentifiers, '+');
	return output;
}
}
