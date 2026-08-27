#pragma once

#include "GamepadConnectionObserver.h"

#include <cstddef>
#include <cstdint>

namespace GameInput
{
struct TouchControlsVisibilityDecision
{
	bool showGamepadConnectedMessage = false;
	bool restoreTouchControls = false;
};

class TouchControlsVisibilityPolicy
{
public:
	TouchControlsVisibilityDecision update(
		std::size_t registeredGamepadCount,
		std::uint64_t gamepadAdditionRevision,
		std::uint64_t activeGamepadRemovalRevision,
		bool touchControlsVisible,
		bool restoreAfterActiveGamepadLoss = true)
	{
		TouchControlsVisibilityDecision decision;
		decision.showGamepadConnectedMessage =
			gamepadConnectionObserver.update(
				registeredGamepadCount,
				gamepadAdditionRevision);
		if (!initialized)
		{
			initialized = true;
		}
		else
		{
			decision.restoreTouchControls = restoreAfterActiveGamepadLoss
				&& !touchControlsVisible
				&& activeGamepadRemovalRevision
					!= observedActiveGamepadRemovalRevision;
		}

		observedActiveGamepadRemovalRevision =
			activeGamepadRemovalRevision;
		return decision;
	}

	void reset()
	{
		initialized = false;
		gamepadConnectionObserver.reset();
		observedActiveGamepadRemovalRevision = 0;
	}

private:
	bool initialized = false;
	GamepadConnectionObserver gamepadConnectionObserver;
	std::uint64_t observedActiveGamepadRemovalRevision = 0;
};
}
