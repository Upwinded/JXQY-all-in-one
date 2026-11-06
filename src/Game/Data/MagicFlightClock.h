#pragma once

#include "../../Resource/MagicTimingMode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

struct MagicFlightStep
{
	double movementFrames = 0.0;
	bool expired = false;
};

struct MagicFlightClock
{
	MagicTimingMode mode = MagicTimingMode::FixedMilliseconds;
	double framesRemaining = 0.0;
	double animationElapsedMilliseconds = 0.0;
	double logicalFrameRemainder = 0.0;
	std::uint64_t animationFrame = 0;

	MagicFlightStep advance(double elapsedMilliseconds, double framesPerMillisecond, int imageInterval)
	{
		if (elapsedMilliseconds <= 0.0 || framesPerMillisecond <= 0.0)
		{
			return {};
		}
		const double requestedFrames = elapsedMilliseconds * framesPerMillisecond;
		if (mode == MagicTimingMode::FixedMilliseconds)
		{
			return { requestedFrames, false };
		}
		if (framesRemaining <= 0.000001)
		{
			// MagicSprite moves before it observes the previous Sprite.Update's end state.
			return { mode == MagicTimingMode::CSharpAnimationFrames ? requestedFrames : 0.0, true };
		}
		if (mode == MagicTimingMode::LogicFrames)
		{
			const double movementFrames = std::min(requestedFrames, framesRemaining);
			framesRemaining = std::max(0.0, framesRemaining - movementFrames);
			return { movementFrames, framesRemaining <= 0.000001 };
		}
		if (mode == MagicTimingMode::CSharpAnimationFrames)
		{
			animationElapsedMilliseconds += elapsedMilliseconds;
			if (animationElapsedMilliseconds > std::max(0, imageInterval))
			{
				animationElapsedMilliseconds -= std::max(0, imageInterval);
				framesRemaining -= 1.0;
				++animationFrame;
			}
			// JxqyHD tests IsInPlaying before Sprite.Update consumes this frame.
			return { requestedFrames, false };
		}

		// Native animation is sampled at logical updates, not display updates.
		// Its timer discards excess time and advances at most once per logical step.
		double remainingMilliseconds = elapsedMilliseconds;
		double movementFrames = 0.0;
		while (remainingMilliseconds > 0.000001)
		{
			const double untilNextStep = (1.0 - logicalFrameRemainder) / framesPerMillisecond;
			const double consumed = std::min(remainingMilliseconds, untilNextStep);
			animationElapsedMilliseconds += consumed;
			logicalFrameRemainder += consumed * framesPerMillisecond;
			remainingMilliseconds -= consumed;
			if (logicalFrameRemainder < 1.0 - 0.000001)
			{
				break;
			}
			logicalFrameRemainder = 0.0;
			const double clockMilliseconds = std::floor(animationElapsedMilliseconds + 0.000001);
			if (clockMilliseconds > std::max(0, imageInterval))
			{
				// GetTickCount is integral; retain only the sub-millisecond phase.
				animationElapsedMilliseconds = std::max(0.0, animationElapsedMilliseconds - clockMilliseconds);
				framesRemaining -= 1.0;
				++animationFrame;
			}
			if (framesRemaining <= 0.0)
			{
				return { movementFrames, true };
			}
			movementFrames += 1.0;
		}
		return { movementFrames, false };
	}
};
