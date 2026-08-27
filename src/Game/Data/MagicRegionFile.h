#pragma once

#include <string>
#include <vector>
#include "../GameTypes.h"

struct MagicRegionFileItem
{
	PointEx offset = { 0.0f, 0.0f };
	unsigned int delay = 0;
};

using MagicRegionFile = std::vector<std::vector<MagicRegionFileItem>>;

namespace MagicRegionFileSafety
{
	constexpr int MaximumFileBytes = 4 * 1024 * 1024;
}

bool loadMagicRegionFile(const std::string& fileName, MagicRegionFile& regionFile);
