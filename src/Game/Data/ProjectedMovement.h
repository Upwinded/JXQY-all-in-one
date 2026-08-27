#pragma once

#include "../GameTypes.h"
#include "../../Types/ElementTypes.h"

#include <algorithm>
#include <cmath>

inline float getProjectedMovementLength(PointEx value)
{
	return std::hypot(value.x / MapXRatio, value.y);
}

inline float getProjectedTileUnitDistance(PointEx value)
{
	constexpr float diagonalTileStepLength = (float)TILE_HEIGHT / 1.41421356237f;
	return getProjectedMovementLength(value) / diagonalTileStepLength;
}

inline float getProjectedMovementSpeedForDuration(PointEx value, float duration, float gameSpeed)
{
	if (duration <= 0.0f || gameSpeed <= 0.0f)
	{
		return 0.0f;
	}
	return getProjectedMovementLength(value) / duration / gameSpeed;
}

inline float getProjectedFrameDistance(float speed, float frameTime, float gameSpeed)
{
	if (speed <= 0.0f || frameTime <= 0.0f || gameSpeed <= 0.0f)
	{
		return 0.0f;
	}
	return speed * gameSpeed * frameTime;
}

inline float getProjectedMagicFrameDistance(float speed, float frameTime, float gameSpeed)
{
	return getProjectedFrameDistance(speed, frameTime, gameSpeed) * MAGIC_FLYING_SPEED_SCALE * (float)TILE_HEIGHT / 2.0f;
}

inline bool isWithinProjectedMovementDistance(PointEx value, float distance)
{
	if (distance < 0.0f)
	{
		return false;
	}
	return getProjectedMovementLength(value) <= distance;
}

inline PointEx normalizeProjectedMovement(PointEx value)
{
	float length = getProjectedMovementLength(value);
	if (length <= 0.0f)
	{
		return { 0.0f, 0.0f };
	}
	return { value.x / length, value.y / length };
}

inline PointEx getEffectProjectedMovementVector(Point flyingDirection)
{
	return
	{
		(float)flyingDirection.x * (float)TILE_HEIGHT * MapXRatio,
		(float)flyingDirection.y * (float)TILE_HEIGHT
	};
}

inline PointEx advanceProjectedMovement(PointEx currentOffset, PointEx remainingOffset, float stepLength)
{
	float remainingLength = getProjectedMovementLength(remainingOffset);
	if (remainingLength <= 0.0f || stepLength >= remainingLength)
	{
		return currentOffset + remainingOffset;
	}

	float ratio = stepLength / remainingLength;
	return
	{
		currentOffset.x + remainingOffset.x * ratio,
		currentOffset.y + remainingOffset.y * ratio
	};
}

inline float clampMovementProgress(float progress)
{
	return std::max(0.0f, std::min(progress, 1.0f));
}

inline int getMovementProgressPermille(float movedLength, float totalLength)
{
	if (totalLength <= 0.0f)
	{
		return 1000;
	}
	return static_cast<int>(std::lround(clampMovementProgress(movedLength / totalLength) * 1000.0f));
}

inline PointEx getBezierForcedMoveDrawOffset(PointEx lineOffset, float progress)
{
	progress = clampMovementProgress(progress);
	if (progress <= 0.0f || progress >= 1.0f)
	{
		return { 0.0f, 0.0f };
	}

	PointEx perpendicular = lineOffset.x < 0.0f
		? PointEx{ -lineOffset.y, lineOffset.x }
		: PointEx{ lineOffset.y, -lineOffset.x };
	float length = std::hypot(perpendicular.x, perpendicular.y);
	if (length <= 0.0f)
	{
		return { 0.0f, 0.0f };
	}

	perpendicular.x /= length;
	perpendicular.y /= length;
	float height = std::max(std::abs(perpendicular.y) * 100.0f, 20.0f);
	float amount = 2.0f * (1.0f - progress) * progress * height;
	return { perpendicular.x * amount, perpendicular.y * amount };
}
