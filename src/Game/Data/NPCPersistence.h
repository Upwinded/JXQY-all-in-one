#pragma once

#include "../../File/INIReader.h"

#include <charconv>
#include <cctype>
#include <string>

namespace NPCPersistence
{
inline constexpr int MaximumRuntimeNpcCount = 4096;
inline constexpr int MaximumNpcCount = MaximumRuntimeNpcCount;
inline constexpr int MaximumPartnerCount = MaximumRuntimeNpcCount;

inline bool runtimePopulationFits(
	int npcCount,
	int partnerCount)
{
	return npcCount >= 0 &&
		partnerCount >= 0 &&
		npcCount <= MaximumRuntimeNpcCount &&
		partnerCount <= MaximumRuntimeNpcCount - npcCount;
}
inline constexpr int MaximumLevelCount = 1024;

inline bool readBoundedInteger(
	const INIReader& ini,
	const std::string& section,
	const std::string& name,
	int minimumValue,
	int maximumValue,
	int& value)
{
	std::string text = ini.Get(section, name, "");
	auto begin = text.begin();
	auto end = text.end();
	while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0)
	{
		++begin;
	}
	while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0)
	{
		--end;
	}
	if (begin == end)
	{
		return false;
	}

	int parsedValue = 0;
	const char* first = &*begin;
	const char* last = first + (end - begin);
	auto result = std::from_chars(first, last, parsedValue, 10);
	if (result.ec != std::errc() || result.ptr != last
		|| parsedValue < minimumValue || parsedValue > maximumValue)
	{
		return false;
	}

	value = parsedValue;
	return true;
}

inline bool readCount(const INIReader& ini, int maximumCount, int& count)
{
	return readBoundedInteger(ini, "Head", "Count", 0, maximumCount, count);
}

inline bool readLevelCount(const INIReader& ini, int& count)
{
	return readBoundedInteger(ini, "Head", "Levels", 1, MaximumLevelCount, count);
}
}
