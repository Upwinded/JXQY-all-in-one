#pragma once

#include <string>

namespace SaveSlots
{
constexpr int ManualCount = 9;
constexpr int Automatic = 10;

inline bool isManual(int slot)
{
	return slot >= 1 && slot <= ManualCount;
}

inline std::string directoryName(int slot)
{
	if (slot == Automatic) return "rpg_auto";
	return isManual(slot) ? "rpg" + std::to_string(slot) : "";
}

inline std::string screenshot(int slot, const char* extension = ".png")
{
	const auto directory = directoryName(slot);
	return directory.empty() ? "" : "shot/" + directory + extension;
}
}
