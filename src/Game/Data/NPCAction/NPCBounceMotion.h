#pragma once

#include "../../GameTypes.h"
#include "../ProjectedMovement.h"

struct NPCBounceMotion
{
	PointEx direction = { 0.0f, 0.0f };
	float velocity = 0.0f;
};

inline float getBounceVectorLength(PointEx value)
{
	return getProjectedMovementLength(value);
}

inline PointEx normalizeBounceVector(PointEx value)
{
	return normalizeProjectedMovement(value);
}

inline float getBounceFrameDistance(float velocity, float frameTime, float gameSpeed)
{
	return getProjectedFrameDistance(velocity, frameTime, gameSpeed);
}

inline float getBounceFrameFriction(float frameTime)
{
	if (frameTime <= 0.0f)
	{
		return 0.0f;
	}
	return 4.0f * frameTime / (float)EFFECT_FRAME_TIME;
}

inline NPCBounceMotion composeBounceMotion(PointEx currentDirection, float currentVelocity, PointEx incomingDirection, float incomingVelocity)
{
	if (incomingVelocity <= 0.0f)
	{
		return { normalizeBounceVector(currentDirection), currentVelocity > 0.0f ? currentVelocity : 0.0f };
	}

	PointEx normalizedIncoming = normalizeBounceVector(incomingDirection);
	if (getBounceVectorLength(normalizedIncoming) <= 0.0f)
	{
		return { normalizeBounceVector(currentDirection), currentVelocity > 0.0f ? currentVelocity : 0.0f };
	}

	if (currentVelocity > 0.0f)
	{
		PointEx normalizedCurrent = normalizeBounceVector(currentDirection);
		PointEx combinedDirection =
		{
			normalizedCurrent.x * currentVelocity + normalizedIncoming.x * incomingVelocity,
			normalizedCurrent.y * currentVelocity + normalizedIncoming.y * incomingVelocity
		};
		float combinedVelocity = getBounceVectorLength(combinedDirection);
		return { normalizeBounceVector(combinedDirection), combinedVelocity };
	}

	return { normalizedIncoming, incomingVelocity };
}
