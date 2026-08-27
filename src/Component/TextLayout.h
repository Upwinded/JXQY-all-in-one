#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace TextLayout
{
std::size_t countUtf8Characters(const std::string& text);
// maxFullWidthCharactersPerLine is measured in CJK/full-width glyph cells.
// ASCII glyphs consume approximately two fifths of a cell so Latin text does not wrap
// at half of the visibly available width.
std::vector<std::string> wrapUtf8Text(const std::string& text, int maxFullWidthCharactersPerLine);
int charactersPerLineForWidth(int width, int fontSize);
int wrappedLineCount(const std::string& text, int width, int fontSize);
int wrappedTextHeight(const std::string& text, int width, int fontSize, int lineGap = 0);
int visibleWrappedLineCount(int lineCount, int availableHeight, int fontSize, int lineGap = 0);
}
