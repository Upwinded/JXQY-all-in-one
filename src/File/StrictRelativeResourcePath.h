#pragma once

#include <string>
#include <string_view>

namespace ResourcePathSafety
{
enum class StrictRelativePathStatus
{
	Valid,
	Empty,
	InvalidUtf8,
	AbsoluteOrRooted,
	ParentTraversal,
	EmptyOrDotSegment,
	InvalidCharacter,
	ReservedWindowsName
};

struct StrictRelativePathResult
{
	StrictRelativePathStatus status = StrictRelativePathStatus::Empty;
	std::string normalizedPath;

	bool succeeded() const noexcept
	{
		return status == StrictRelativePathStatus::Valid;
	}
};

// Accepts either path separator but returns a canonical virtual spelling with '/'.
// Unlike legacy resource paths, a leading separator and dot segments are rejected.
StrictRelativePathResult normalizeStrictRelativeResourcePath(
	std::string_view path);

// Applies the same portable-path validation and then canonicalizes ASCII path
// letters to lowercase. Resource files are published with this spelling on
// every platform, so callers do not depend on host filesystem case behavior.
StrictRelativePathResult normalizeLowercaseStrictRelativeResourcePath(
	std::string_view path);

bool isStrictRelativeResourcePath(std::string_view path);
}
