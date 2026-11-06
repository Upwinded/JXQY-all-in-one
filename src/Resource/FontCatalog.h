#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ResourceFonts
{
inline constexpr char DefaultFontId[] = "default";

struct FontChoice
{
	std::string id;
	std::string name;
	std::string file;
};

struct FontData
{
	std::unique_ptr<char[]> data;
	int length = 0;
	std::unique_ptr<char[]> fallback;
	int fallbackLength = 0;
};

std::vector<FontChoice> loadChoices();
bool readChoice(const std::string& id, std::unique_ptr<char[]>& data, int& length);
// A package-owned font remains authoritative, ahead of the user's global choice.
FontData loadGameFont(const std::string& id, bool preferActiveResource = true);
}
