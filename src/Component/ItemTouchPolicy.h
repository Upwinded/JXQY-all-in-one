#pragma once

#include <algorithm>
#include <cstdint>

namespace ItemTouchPolicy
{
constexpr int MAXIMUM_LONG_PRESS_MOVE_TOLERANCE_PIXELS = 12;

inline int calculateLongPressMoveTolerance(int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		return 0;
	}
	return std::min(
		std::min(width, height) / 4,
		MAXIMUM_LONG_PRESS_MOVE_TOLERANCE_PIXELS);
}

inline bool isWithinLongPressMoveTolerance(
	int moveDeltaX,
	int moveDeltaY,
	int tolerance)
{
	const int64_t safeTolerance = std::max(0, tolerance);
	const int64_t deltaX = moveDeltaX;
	const int64_t deltaY = moveDeltaY;
	return deltaX * deltaX + deltaY * deltaY
		<= safeTolerance * safeTolerance;
}
}
