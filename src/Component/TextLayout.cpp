#include "TextLayout.h"

#include <algorithm>

namespace
{
constexpr const char* ENTER_MARKER = "<enter>";
constexpr std::size_t ENTER_MARKER_LENGTH = 7;

bool isUtf8ContinuationByte(unsigned char value)
{
	return (value & 0xC0) == 0x80;
}

std::size_t getUtf8CharacterLength(const std::string& text, std::size_t offset)
{
	const unsigned char leadingByte = static_cast<unsigned char>(text[offset]);
	std::size_t expectedLength = 1;
	if ((leadingByte & 0xE0) == 0xC0)
	{
		expectedLength = 2;
	}
	else if ((leadingByte & 0xF0) == 0xE0)
	{
		expectedLength = 3;
	}
	else if ((leadingByte & 0xF8) == 0xF0)
	{
		expectedLength = 4;
	}

	if (offset + expectedLength > text.size())
	{
		return 1;
	}
	for (std::size_t byteIndex = 1; byteIndex < expectedLength; ++byteIndex)
	{
		if (!isUtf8ContinuationByte(static_cast<unsigned char>(text[offset + byteIndex])))
		{
			return 1;
		}
	}
	return expectedLength;
}

std::vector<std::string> splitExplicitLines(const std::string& text)
{
	if (text.empty())
	{
		return {};
	}

	std::vector<std::string> lines;
	std::string currentLine;
	std::size_t offset = 0;
	while (offset < text.size())
	{
		if (text.compare(offset, ENTER_MARKER_LENGTH, ENTER_MARKER) == 0)
		{
			lines.push_back(currentLine);
			currentLine.clear();
			offset += ENTER_MARKER_LENGTH;
			continue;
		}
		if (text[offset] == '\r' || text[offset] == '\n')
		{
			lines.push_back(currentLine);
			currentLine.clear();
			if (text[offset] == '\r' && offset + 1 < text.size() && text[offset + 1] == '\n')
			{
				offset += 2;
			}
			else
			{
				++offset;
			}
			continue;
		}

		const std::size_t characterLength = getUtf8CharacterLength(text, offset);
		currentLine.append(text, offset, characterLength);
		offset += characterLength;
	}
	lines.push_back(currentLine);
	return lines;
}

std::vector<std::string> splitUtf8Characters(const std::string& text)
{
	std::vector<std::string> characters;
	std::size_t offset = 0;
	while (offset < text.size())
	{
		const std::size_t characterLength = getUtf8CharacterLength(text, offset);
		characters.emplace_back(text.substr(offset, characterLength));
		offset += characterLength;
	}
	return characters;
}
}

namespace TextLayout
{
std::size_t countUtf8Characters(const std::string& text)
{
	std::size_t characterCount = 0;
	std::size_t offset = 0;
	while (offset < text.size())
	{
		offset += getUtf8CharacterLength(text, offset);
		++characterCount;
	}
	return characterCount;
}

std::vector<std::string> wrapUtf8Text(const std::string& text, int maxFullWidthCharactersPerLine)
{
	const std::vector<std::string> explicitLines = splitExplicitLines(text);
	if (explicitLines.empty())
	{
		return {};
	}

	const int lineWidthUnitLimit = std::max(1, maxFullWidthCharactersPerLine) * 5;
	std::vector<std::string> wrappedLines;
	for (const std::string& explicitLine : explicitLines)
	{
		const std::vector<std::string> characters = splitUtf8Characters(explicitLine);
		if (characters.empty())
		{
			wrappedLines.emplace_back();
			continue;
		}

		std::string wrappedLine;
		int wrappedWidthUnits = 0;
		for (const std::string& character : characters)
		{
			const int characterWidthUnits = character.size() == 1 ? 2 : 5;
			if (wrappedWidthUnits > 0 &&
				wrappedWidthUnits + characterWidthUnits > lineWidthUnitLimit)
			{
				wrappedLines.push_back(wrappedLine);
				wrappedLine.clear();
				wrappedWidthUnits = 0;
			}
			wrappedLine += character;
			wrappedWidthUnits += characterWidthUnits;
		}
		wrappedLines.push_back(wrappedLine);
	}
	return wrappedLines;
}

int charactersPerLineForWidth(int width, int fontSize)
{
	return std::max(1, width / std::max(1, fontSize));
}

int wrappedLineCount(const std::string& text, int width, int fontSize)
{
	return static_cast<int>(wrapUtf8Text(text, charactersPerLineForWidth(width, fontSize)).size());
}

int wrappedTextHeight(const std::string& text, int width, int fontSize, int lineGap)
{
	const int lineCount = wrappedLineCount(text, width, fontSize);
	if (lineCount == 0)
	{
		return 0;
	}

	const int normalizedFontSize = std::max(1, fontSize);
	const int normalizedLineGap = std::max(0, lineGap);
	return lineCount * normalizedFontSize + (lineCount - 1) * normalizedLineGap;
}

int visibleWrappedLineCount(int lineCount, int availableHeight, int fontSize, int lineGap)
{
	if (lineCount <= 0 || availableHeight <= 0)
	{
		return 0;
	}
	const int normalizedFontSize = std::max(1, fontSize);
	const int normalizedLineGap = std::max(0, lineGap);
	const int capacity = (availableHeight + normalizedLineGap)
		/ (normalizedFontSize + normalizedLineGap);
	return std::max(0, std::min(lineCount, capacity));
}
}
