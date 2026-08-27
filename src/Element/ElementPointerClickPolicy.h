#pragma once

#include <cstdint>

namespace ElementPointerClickPolicy
{
inline bool isPointInsideHalfOpenBounds(
	int pointX,
	int pointY,
	int left,
	int top,
	int width,
	int height)
{
	if (width <= 0 || height <= 0)
	{
		return false;
	}

	const std::int64_t x = pointX;
	const std::int64_t y = pointY;
	const std::int64_t boundsLeft = left;
	const std::int64_t boundsTop = top;
	return x >= boundsLeft && x < boundsLeft + width
		&& y >= boundsTop && y < boundsTop + height;
}

inline bool shouldTriggerClick(
	bool pressedBySamePointer,
	bool releasedInside,
	bool withinMaximumClickTime,
	bool dragStarted)
{
	return pressedBySamePointer && releasedInside && withinMaximumClickTime
		&& !dragStarted;
}

inline bool shouldAcquirePointerOnDown(
	std::int64_t activePressedPointer,
	std::int64_t incomingPointer,
	std::int64_t untouchedPointer,
	bool pressedInside)
{
	return pressedInside
		&& (activePressedPointer == untouchedPointer
			|| activePressedPointer == incomingPointer);
}

inline bool shouldPreservePressedPointerOnMotion(
	std::int64_t pressedPointer,
	std::int64_t movingPointer,
	std::int64_t untouchedPointer)
{
	return pressedPointer != untouchedPointer
		&& pressedPointer != movingPointer;
}

inline void cancelPointerState(
	std::int64_t& touchingPointer,
	std::int64_t& pressedPointer,
	std::int64_t untouchedPointer)
{
	touchingPointer = untouchedPointer;
	pressedPointer = untouchedPointer;
}

inline void cancelPendingResult(unsigned int& result, unsigned int emptyResult)
{
	result = emptyResult;
}
}
