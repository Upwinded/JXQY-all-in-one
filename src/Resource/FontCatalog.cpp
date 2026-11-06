#include "FontCatalog.h"

#include "../File/File.h"
#include "../File/INIReader.h"
#include "../File/log.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <filesystem>
#include <utility>

namespace ResourceFonts
{
namespace
{
constexpr int MaximumFontBytes = 32 * 1024 * 1024;

bool validFont(const std::unique_ptr<char[]>& data, int length)
{
	if (!data || length <= 0)
	{
		return false;
	}
	auto* stream = SDL_IOFromConstMem(data.get(), static_cast<std::size_t>(length));
	auto* font = stream ? TTF_OpenFontIO(stream, false, 20) : nullptr;
	if (font)
	{
		TTF_CloseFont(font);
	}
	if (stream)
	{
		SDL_CloseIO(stream);
	}
	return font != nullptr;
}
}

std::vector<FontChoice> loadChoices()
{
	std::vector<FontChoice> choices = { { DefaultFontId, u8"默认字体", "" } };
	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readBundledApplicationFile("engine/font/fonts.ini", data, length, 64 * 1024))
	{
		return choices;
	}
	INIReader ini(data);
	const int count = std::clamp(static_cast<int>(ini.GetInteger("Fonts", "Count", 0)), 0, 64);
	for (int index = 0; index < count; ++index)
	{
		const std::string section = "Font" + std::to_string(index);
		FontChoice choice = { ini.Get(section, "Id", ""), ini.Get(section, "Name", ""), ini.Get(section, "File", "") };
		const auto path = std::filesystem::u8path(choice.file);
		if (choice.id.empty() || choice.name.empty() || choice.id == DefaultFontId ||
			choice.file.empty() || path.is_absolute() || path.has_root_name() ||
			choice.file.find(':') != std::string::npos || choice.file.find('\\') != std::string::npos ||
			std::find(path.begin(), path.end(), "..") != path.end() ||
			std::find_if(choices.begin(), choices.end(), [&choice](const FontChoice& item)
				{
					return item.id == choice.id;
				}) != choices.end())
		{
			continue;
		}
		choices.push_back(std::move(choice));
	}
	return choices;
}

bool readChoice(const std::string& id, std::unique_ptr<char[]>& data, int& length)
{
	data.reset();
	length = 0;
	for (const auto& choice : loadChoices())
	{
		if (choice.id == id && !choice.file.empty() &&
			File::readBundledApplicationFile("engine/" + choice.file, data, length, MaximumFontBytes) &&
			validFont(data, length))
		{
			return true;
		}
	}
	data.reset();
	length = 0;
	return false;
}

FontData loadGameFont(const std::string& id, bool preferActiveResource)
{
	FontData result;
	if (preferActiveResource && File::readActiveResourceFile(
		"font/font.ttf", result.data, result.length, MaximumFontBytes) && result.length > 0)
	{
		return result;
	}
	File::readBundledApplicationFile("engine/font/font.ttf", result.data, result.length, MaximumFontBytes);
	// The notice renderer's existing size limit must not turn a MOD override
	// into permission to use the unrelated global choice.
	const bool forcedFontUnavailable = preferActiveResource && File::activeResourceFileExist("font/font.ttf");
	if (id != DefaultFontId && !forcedFontUnavailable)
	{
		std::unique_ptr<char[]> selected;
		int selectedLength = 0;
		if (readChoice(id, selected, selectedLength))
		{
			result.fallback = std::move(result.data);
			result.fallbackLength = result.length;
			result.data = std::move(selected);
			result.length = selectedLength;
		}
		else
		{
			GameLog::write("Font: selected font %s is unavailable; using the original font\n", id.c_str());
		}
	}
	return result;
}
}
