#include "StrictRelativeResourcePath.h"

#include "ResourcePathSafety.h"

#include <algorithm>
#include <utility>

namespace ResourcePathSafety
{
StrictRelativePathResult normalizeStrictRelativeResourcePath(
	std::string_view path)
{
	StrictRelativePathResult result;
	if (path.empty())
	{
		result.status = StrictRelativePathStatus::Empty;
		return result;
	}

	const std::string original(path);
	if (!isValidUtf8(original))
	{
		result.status = StrictRelativePathStatus::InvalidUtf8;
		return result;
	}
	if (path.front() == '/' || path.front() == '\\' ||
		path.find(':') != std::string_view::npos)
	{
		result.status = StrictRelativePathStatus::AbsoluteOrRooted;
		return result;
	}

	std::string normalized(path);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');

	std::size_t start = 0;
	while (start <= normalized.size())
	{
		const std::size_t separator = normalized.find('/', start);
		const std::string segment = separator == std::string::npos
			? normalized.substr(start)
			: normalized.substr(start, separator - start);
		if (segment.empty() || segment == ".")
		{
			result.status = StrictRelativePathStatus::EmptyOrDotSegment;
			return result;
		}
		if (segment == "..")
		{
			result.status = StrictRelativePathStatus::ParentTraversal;
			return result;
		}
		if (segment.back() == '.' || segment.back() == ' ')
		{
			result.status = StrictRelativePathStatus::InvalidCharacter;
			return result;
		}
		for (std::size_t index = 0; index < segment.size(); ++index)
		{
			const unsigned char character =
				static_cast<unsigned char>(segment[index]);
			if (character < 0x20 || character == 0x7F ||
				character == '<' || character == '>' ||
				character == '"' || character == '|' ||
				character == '?' || character == '*')
			{
				result.status = StrictRelativePathStatus::InvalidCharacter;
				return result;
			}
			if (character == 0xC2 && index + 1 < segment.size())
			{
				const unsigned char nextCharacter =
					static_cast<unsigned char>(segment[index + 1]);
				if (nextCharacter >= 0x80 && nextCharacter <= 0x9F)
				{
					result.status =
						StrictRelativePathStatus::InvalidCharacter;
					return result;
				}
			}
		}
		if (isReservedWindowsPathComponent(segment))
		{
			result.status = StrictRelativePathStatus::ReservedWindowsName;
			return result;
		}
		if (separator == std::string::npos)
		{
			break;
		}
		start = separator + 1;
	}

	result.status = StrictRelativePathStatus::Valid;
	result.normalizedPath = std::move(normalized);
	return result;
}

StrictRelativePathResult normalizeLowercaseStrictRelativeResourcePath(
	std::string_view path)
{
	StrictRelativePathResult result =
		normalizeStrictRelativeResourcePath(path);
	if (!result.succeeded())
	{
		return result;
	}
	for (char& character : result.normalizedPath)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	return result;
}

bool isStrictRelativeResourcePath(std::string_view path)
{
	return normalizeStrictRelativeResourcePath(path).succeeded();
}
}
