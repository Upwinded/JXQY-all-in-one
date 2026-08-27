#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ModRelease
{
enum class SemanticVersionParseError
{
	None,
	Empty,
	InvalidCore,
	LeadingZeroCore,
	NumericOverflow,
	EmptyIdentifier,
	InvalidIdentifier,
	LeadingZeroPrereleaseNumber
};

struct SemanticVersion
{
	std::uint64_t major = 0;
	std::uint64_t minor = 0;
	std::uint64_t patch = 0;
	std::vector<std::string> prereleaseIdentifiers;
	std::vector<std::string> buildIdentifiers;
};

struct SemanticVersionParseResult
{
	SemanticVersionParseError error = SemanticVersionParseError::None;
	SemanticVersion version;

	bool succeeded() const noexcept
	{
		return error == SemanticVersionParseError::None;
	}
};

SemanticVersionParseResult parseSemanticVersion(std::string_view text);

// Returns -1, 0, or 1. Build identifiers do not affect precedence.
int compareSemanticVersionPrecedence(const SemanticVersion& left,
	const SemanticVersion& right) noexcept;

std::string formatSemanticVersion(const SemanticVersion& version);
}
