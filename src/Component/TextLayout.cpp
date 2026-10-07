#include "TextLayout.h"

#include <algorithm>
#include <charconv>
#include <string_view>

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

bool equalsAsciiCaseInsensitive(
	std::string_view left,
	std::string_view right)
{
	if (left.size() != right.size())
	{
		return false;
	}
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		unsigned char leftCharacter =
			static_cast<unsigned char>(left[index]);
		unsigned char rightCharacter =
			static_cast<unsigned char>(right[index]);
		if (leftCharacter >= 'A' && leftCharacter <= 'Z')
		{
			leftCharacter = static_cast<unsigned char>(
				leftCharacter - 'A' + 'a');
		}
		if (rightCharacter >= 'A' && rightCharacter <= 'Z')
		{
			rightCharacter = static_cast<unsigned char>(
				rightCharacter - 'A' + 'a');
		}
		if (leftCharacter != rightCharacter)
		{
			return false;
		}
	}
	return true;
}

std::string_view trimAsciiWhitespace(std::string_view value)
{
	while (!value.empty()
		&& (value.front() == ' ' || value.front() == '\t'))
	{
		value.remove_prefix(1);
	}
	while (!value.empty()
		&& (value.back() == ' ' || value.back() == '\t'))
	{
		value.remove_suffix(1);
	}
	return value;
}

bool parseColorComponent(std::string_view text, int& value)
{
	text = trimAsciiWhitespace(text);
	if (text.empty())
	{
		return false;
	}
	const char* const begin = text.data();
	const char* const end = begin + text.size();
	const std::from_chars_result result =
		std::from_chars(begin, end, value);
	return result.ec == std::errc() && result.ptr == end;
}
}

namespace TextLayout
{
bool parseColorTagValue(
	std::string_view value,
	unsigned int defaultColor,
	unsigned int& parsedColor)
{
	value = trimAsciiWhitespace(value);
	const unsigned int defaultAlpha = defaultColor & 0xFF000000U;
	if (equalsAsciiCaseInsensitive(value, "red"))
	{
		parsedColor = defaultAlpha | 0x00FF0000U;
		return true;
	}
	if (equalsAsciiCaseInsensitive(value, "green"))
	{
		parsedColor = defaultAlpha | 0x0000FF00U;
		return true;
	}
	if (equalsAsciiCaseInsensitive(value, "blue"))
	{
		parsedColor = defaultAlpha | 0x000000FFU;
		return true;
	}
	if (equalsAsciiCaseInsensitive(value, "yellow"))
	{
		parsedColor = defaultAlpha | 0x00FFFF00U;
		return true;
	}
	if (equalsAsciiCaseInsensitive(value, "white"))
	{
		parsedColor = defaultAlpha | 0x00FFFFFFU;
		return true;
	}
	if (equalsAsciiCaseInsensitive(value, "black"))
	{
		parsedColor = defaultAlpha;
		return true;
	}
	if (equalsAsciiCaseInsensitive(value, "default"))
	{
		parsedColor = defaultColor;
		return true;
	}

	int components[4] = { 0, 0, 0, 255 };
	int componentCount = 0;
	std::size_t offset = 0;
	while (offset <= value.size() && componentCount < 4)
	{
		const std::size_t separator = value.find(',', offset);
		const std::size_t length = separator == std::string_view::npos
			? value.size() - offset
			: separator - offset;
		if (!parseColorComponent(value.substr(offset, length),
			components[componentCount]))
		{
			return false;
		}
		++componentCount;
		if (separator == std::string_view::npos)
		{
			offset = value.size();
			break;
		}
		offset = separator + 1;
	}
	if ((componentCount != 3 && componentCount != 4)
		|| offset < value.size())
	{
		return false;
	}

	const unsigned int red =
		static_cast<unsigned int>(components[0]) & 0xFFU;
	const unsigned int green =
		static_cast<unsigned int>(components[1]) & 0xFFU;
	const unsigned int blue =
		static_cast<unsigned int>(components[2]) & 0xFFU;
	const unsigned int alpha = componentCount == 4
		? static_cast<unsigned int>(components[3]) & 0xFFU
		: (defaultColor >> 24) & 0xFFU;
	parsedColor = (alpha << 24) | (red << 16) | (green << 8) | blue;
	return true;
}

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

std::vector<ColorTaggedTextLine> wrapColorTaggedUtf8Text(
	const std::string& text,
	int maxFullWidthCharactersPerLine,
	unsigned int defaultColor)
{
	if (text.empty())
	{
		return {};
	}

	const int lineWidthUnitLimit =
		std::max(1, maxFullWidthCharactersPerLine) * 5;
	std::vector<ColorTaggedTextLine> lines(1);
	int lineWidthUnits = 0;
	unsigned int currentColor = defaultColor;
	unsigned int rangeDefaultColor = defaultColor;
	bool rangeDefaultActive = false;

	auto startLine = [&]()
	{
		lines.emplace_back();
		lineWidthUnits = 0;
	};
	auto appendCharacter = [&](const std::string& character)
	{
		const int characterWidthUnits = character.size() == 1 ? 2 : 5;
		if (lineWidthUnits > 0
			&& lineWidthUnits + characterWidthUnits > lineWidthUnitLimit)
		{
			startLine();
		}
		ColorTaggedTextLine& line = lines.back();
		if (line.empty() || line.back().color != currentColor)
		{
			line.push_back({ {}, currentColor });
		}
		line.back().text += character;
		lineWidthUnits += characterWidthUnits;
	};

	std::size_t offset = 0;
	while (offset < text.size())
	{
		if (offset + ENTER_MARKER_LENGTH <= text.size()
			&& equalsAsciiCaseInsensitive(
				std::string_view(text).substr(offset, ENTER_MARKER_LENGTH),
				ENTER_MARKER))
		{
			startLine();
			offset += ENTER_MARKER_LENGTH;
			continue;
		}
		if (text[offset] == '\r' || text[offset] == '\n')
		{
			startLine();
			if (text[offset] == '\r' && offset + 1 < text.size()
				&& text[offset + 1] == '\n')
			{
				offset += 2;
			}
			else
			{
				++offset;
			}
			continue;
		}
		if (text[offset] == '<')
		{
			const std::size_t tagEnd = text.find('>', offset + 1);
			if (tagEnd != std::string::npos)
			{
				const std::string_view tag = std::string_view(text).substr(
					offset + 1, tagEnd - offset - 1);
				constexpr std::string_view ColorPrefix = "color=";
				if (tag.size() >= ColorPrefix.size()
					&& equalsAsciiCaseInsensitive(
						tag.substr(0, ColorPrefix.size()), ColorPrefix))
				{
					const std::string_view value =
						trimAsciiWhitespace(tag.substr(ColorPrefix.size()));
					if (equalsAsciiCaseInsensitive(
						value, "BeginRangeDefault"))
					{
						rangeDefaultColor = currentColor;
						rangeDefaultActive = true;
						offset = tagEnd + 1;
						continue;
					}
					if (equalsAsciiCaseInsensitive(
						value, "EndRangeDefault"))
					{
						rangeDefaultActive = false;
						offset = tagEnd + 1;
						continue;
					}
					unsigned int parsedColor = currentColor;
					if (parseColorTagValue(
						value,
						rangeDefaultActive
							? rangeDefaultColor
							: defaultColor,
						parsedColor))
					{
						currentColor = parsedColor;
					}
					offset = tagEnd + 1;
					continue;
				}
			}
		}

		const std::size_t characterLength =
			getUtf8CharacterLength(text, offset);
		appendCharacter(text.substr(offset, characterLength));
		offset += characterLength;
	}
	return lines;
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
