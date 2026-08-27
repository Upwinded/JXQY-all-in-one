#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ImageAnimationPlayback
{

inline constexpr int LegacyZeroIntervalMilliseconds = 1000 / 60;

struct Layout
{
	int directions = 1;
	std::size_t framesPerDirection = 0;

	std::size_t playableFrameCount() const
	{
		return static_cast<std::size_t>(directions) * framesPerDirection;
	}
};

inline Layout calculateLayout(std::size_t frameCount, int storedDirections)
{
	Layout layout;
	if (frameCount == 0)
	{
		return layout;
	}

	layout.directions = storedDirections > 0 ? storedDirections : 1;
	if (frameCount < static_cast<std::size_t>(layout.directions))
	{
		layout.directions = 1;
	}
	layout.framesPerDirection = frameCount /
		static_cast<std::size_t>(layout.directions);
	return layout;
}

inline int effectiveFrameInterval(int storedInterval)
{
	// JxqyHD advances Interval=0 sprites once per update. The C++ renderer is
	// time-based, so use the engine's 60 Hz effect cadence for that legacy case.
	return storedInterval > 0
		? storedInterval : LegacyZeroIntervalMilliseconds;
}

inline int normalizeDirection(int requestedDirection, const Layout& layout)
{
	if (layout.framesPerDirection == 0 || requestedDirection < 0)
	{
		return 0;
	}
	if (requestedDirection >= layout.directions)
	{
		return requestedDirection % layout.directions;
	}
	return requestedDirection;
}

inline std::size_t calculateFrameIndex(std::size_t framesPerDirection,
	int normalizedDirection, std::uint64_t elapsedMilliseconds,
	int effectiveIntervalMilliseconds, bool once = false,
	bool reverse = false)
{
	if (framesPerDirection == 0)
	{
		return 0;
	}

	std::size_t localIndex = 0;
	if (effectiveIntervalMilliseconds > 0)
	{
		const std::uint64_t elapsedFrames = elapsedMilliseconds /
			static_cast<std::uint64_t>(effectiveIntervalMilliseconds);
		if (once && elapsedFrames >= framesPerDirection)
		{
			localIndex = reverse ? 0 : framesPerDirection - 1;
		}
		else
		{
			localIndex = static_cast<std::size_t>(
				elapsedFrames % framesPerDirection);
			if (reverse)
			{
				localIndex = framesPerDirection - 1 - localIndex;
			}
		}
	}
	else if (reverse)
	{
		localIndex = framesPerDirection - 1;
	}

	return static_cast<std::size_t>(normalizedDirection) *
		framesPerDirection + localIndex;
}

inline std::optional<std::size_t> frameIndex(std::size_t frameCount,
	int storedDirections, int requestedDirection,
	std::uint64_t elapsedMilliseconds, int storedInterval,
	bool once = false, bool reverse = false)
{
	const Layout layout = calculateLayout(frameCount, storedDirections);
	if (layout.framesPerDirection == 0)
	{
		return std::nullopt;
	}
	const int direction = normalizeDirection(requestedDirection, layout);
	return calculateFrameIndex(layout.framesPerDirection, direction,
		elapsedMilliseconds, effectiveFrameInterval(storedInterval),
		once, reverse);
}

} // namespace ImageAnimationPlayback
