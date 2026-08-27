#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace ResourcePathSafety
{
inline bool isValidUtf8(const std::string& value)
{
	std::size_t index = 0;
	while (index < value.size())
	{
		unsigned char lead = static_cast<unsigned char>(value[index]);
		if (lead <= 0x7F)
		{
			index++;
			continue;
		}
		auto isContinuation = [&value](std::size_t position)
		{
			return position < value.size() &&
				(static_cast<unsigned char>(value[position]) & 0xC0) == 0x80;
		};
		if (lead >= 0xC2 && lead <= 0xDF)
		{
			if (!isContinuation(index + 1))
			{
				return false;
			}
			index += 2;
			continue;
		}
		if (lead >= 0xE0 && lead <= 0xEF)
		{
			if (index + 2 >= value.size() || !isContinuation(index + 2))
			{
				return false;
			}
			unsigned char second = static_cast<unsigned char>(value[index + 1]);
			if ((lead == 0xE0 && (second < 0xA0 || second > 0xBF)) ||
				(lead == 0xED && (second < 0x80 || second > 0x9F)) ||
				(lead != 0xE0 && lead != 0xED && (second < 0x80 || second > 0xBF)))
			{
				return false;
			}
			index += 3;
			continue;
		}
		if (lead >= 0xF0 && lead <= 0xF4)
		{
			if (index + 3 >= value.size() || !isContinuation(index + 2) ||
				!isContinuation(index + 3))
			{
				return false;
			}
			unsigned char second = static_cast<unsigned char>(value[index + 1]);
			if ((lead == 0xF0 && (second < 0x90 || second > 0xBF)) ||
				(lead == 0xF4 && (second < 0x80 || second > 0x8F)) ||
				(lead != 0xF0 && lead != 0xF4 && (second < 0x80 || second > 0xBF)))
			{
				return false;
			}
			index += 4;
			continue;
		}
		return false;
	}
	return true;
}

inline bool isReservedWindowsPathComponent(const std::string& segment)
{
	std::string baseName = segment.substr(0, segment.find('.'));
	while (!baseName.empty() && (baseName.back() == '.' || baseName.back() == ' '))
	{
		baseName.pop_back();
	}
	std::transform(baseName.begin(), baseName.end(), baseName.begin(),
		[](unsigned char character)
		{
			return character >= 'A' && character <= 'Z'
				? static_cast<char>(character + ('a' - 'A'))
				: static_cast<char>(character);
		});
	if (baseName == "con" || baseName == "prn" || baseName == "aux" ||
		baseName == "nul" || baseName == "conin$" || baseName == "conout$")
	{
		return true;
	}
	if (baseName.compare(0, 3, "com") != 0 &&
		baseName.compare(0, 3, "lpt") != 0)
	{
		return false;
	}
	if (baseName.size() == 4 &&
		baseName[3] >= '1' && baseName[3] <= '9')
	{
		return true;
	}

	// Win32 also treats COM¹/²/³ and LPT¹/²/³ as reserved device names.
	// Match their exact UTF-8 encodings after validating the complete path.
	if (baseName.size() != 5 ||
		static_cast<unsigned char>(baseName[3]) != 0xC2)
	{
		return false;
	}
	const unsigned char superscript =
		static_cast<unsigned char>(baseName[4]);
	return superscript == 0xB9 || superscript == 0xB2 ||
		superscript == 0xB3;
}

inline bool isSafeVirtualResourcePath(std::string fileName)
{
	if (fileName.empty() || fileName.find('\0') != std::string::npos ||
		!isValidUtf8(fileName))
	{
		return false;
	}
	std::replace(fileName.begin(), fileName.end(), '\\', '/');
	if (fileName.rfind("//", 0) == 0 || fileName.find(':') != std::string::npos)
	{
		return false;
	}
	if (fileName.front() == '/')
	{
		fileName.erase(fileName.begin());
	}
	if (fileName.empty())
	{
		return false;
	}

	std::size_t start = 0;
	bool hasResourceSegment = false;
	while (start <= fileName.size())
	{
		std::size_t next = fileName.find('/', start);
		std::string segment = next == std::string::npos
			? fileName.substr(start)
			: fileName.substr(start, next - start);
		if (segment == "..")
		{
			return false;
		}
		if (!segment.empty() && segment != ".")
		{
			if (segment.back() == '.' || segment.back() == ' ')
			{
				return false;
			}
			for (unsigned char character : segment)
			{
				if (character < 0x20 || character == '<' || character == '>' ||
					character == '"' || character == '|' || character == '?' ||
					character == '*')
				{
					return false;
				}
			}
			if (isReservedWindowsPathComponent(segment))
			{
				return false;
			}
			hasResourceSegment = true;
		}
		if (next == std::string::npos)
		{
			break;
		}
		start = next + 1;
	}
	return hasResourceSegment;
}
}
