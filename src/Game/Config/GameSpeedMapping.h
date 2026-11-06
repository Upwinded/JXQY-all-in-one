#pragma once

#include "../GameTypes.h"

#include <algorithm>
#include <cmath>

namespace GameSpeedMapping
{
inline int toPosition(float speed, int minimum, int maximum)
{
	if (maximum <= minimum)
	{
		return minimum;
	}
	if (!std::isfinite(speed))
	{
		speed = SPEED_TIME_DEFAULT;
	}
	if (speed <= SPEED_TIME_MIN) return minimum;
	if (speed >= SPEED_TIME_MAX) return maximum;
	return std::clamp(static_cast<int>(std::round(
		(speed - SPEED_TIME_MIN) / (SPEED_TIME_MAX - SPEED_TIME_MIN)
		* static_cast<float>(maximum - minimum))) + minimum, minimum, maximum);
}

inline float fromPosition(int position, int minimum, int maximum)
{
	if (maximum <= minimum) return SPEED_TIME_DEFAULT;
	if (position <= minimum) return SPEED_TIME_MIN;
	if (position >= maximum) return SPEED_TIME_MAX;
	// Keep existing percentage choices, but represent the default exactly even
	// when a three-position native menu cannot express its linear percentage.
	if (position == toPosition(SPEED_TIME_DEFAULT, minimum, maximum))
	{
		return SPEED_TIME_DEFAULT;
	}
	return static_cast<float>(position - minimum) / static_cast<float>(maximum - minimum)
		* (SPEED_TIME_MAX - SPEED_TIME_MIN) + SPEED_TIME_MIN;
}
}
