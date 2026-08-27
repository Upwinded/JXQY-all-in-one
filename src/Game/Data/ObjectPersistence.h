#pragma once

#include "NPCPersistence.h"

namespace ObjectPersistence
{
inline constexpr int MaximumRuntimeObjectCount = 4096;
inline constexpr int MaximumObjectCount = MaximumRuntimeObjectCount;
inline constexpr int MaximumObjectFileBytes = 16 * 1024 * 1024;

inline bool readCount(const INIReader& ini, int& count)
{
	return NPCPersistence::readBoundedInteger(
		ini, "Head", "Count", 0, MaximumObjectCount, count);
}
}
