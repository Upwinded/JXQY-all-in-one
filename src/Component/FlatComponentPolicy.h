#pragma once

#include <algorithm>
#include <cstdint>

namespace FlatComponentPolicy
{
enum class VisualState
{
	Normal,
	Hovered,
	Pressed
};

constexpr VisualState resolveVisualState(bool pressed, bool hovered)
{
	return pressed ? VisualState::Pressed : (hovered ? VisualState::Hovered : VisualState::Normal);
}

struct VerticalScrollbarGeometry
{
	int thumbStart = 0;
	int thumbLength = 0;
	int travel = 0;
};

inline VerticalScrollbarGeometry calculateVerticalScrollbarGeometry(
	int trackStart, int trackLength, int minimumThumbLength,
	int minimumPosition, int maximumPosition, int pageSize, int position)
{
	VerticalScrollbarGeometry geometry;
	if (trackLength <= 0)
	{
		return geometry;
	}

	minimumThumbLength = std::clamp(minimumThumbLength, 1, trackLength);
	pageSize = std::max(1, pageSize);
	maximumPosition = std::max(minimumPosition, maximumPosition);
	const std::int64_t range = static_cast<std::int64_t>(maximumPosition) - minimumPosition;
	const std::int64_t totalSize = range + pageSize;
	geometry.thumbLength = range == 0
		? trackLength
		: std::max(minimumThumbLength, static_cast<int>(
			static_cast<std::int64_t>(trackLength) * pageSize / std::max<std::int64_t>(1, totalSize)));
	geometry.thumbLength = std::min(geometry.thumbLength, trackLength);
	geometry.travel = trackLength - geometry.thumbLength;

	position = std::clamp(position, minimumPosition, maximumPosition);
	geometry.thumbStart = trackStart;
	if (range > 0 && geometry.travel > 0)
	{
		geometry.thumbStart += static_cast<int>(
			(static_cast<std::int64_t>(position) - minimumPosition) * geometry.travel / range);
	}
	return geometry;
}

inline int calculateVerticalScrollbarPosition(
	int thumbStart, int trackStart, int travel,
	int minimumPosition, int maximumPosition)
{
	maximumPosition = std::max(minimumPosition, maximumPosition);
	const std::int64_t range = static_cast<std::int64_t>(maximumPosition) - minimumPosition;
	if (range == 0 || travel <= 0)
	{
		return minimumPosition;
	}
	const int relativeStart = std::clamp(thumbStart - trackStart, 0, travel);
	return minimumPosition + static_cast<int>(
		(static_cast<std::int64_t>(relativeStart) * range + travel / 2) / travel);
}
}
