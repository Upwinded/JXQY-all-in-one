#pragma once

#include <cstddef>
#include <cstdint>

namespace GameInput
{
class GamepadConnectionObserver
{
public:
	bool update(
		std::size_t registeredGamepadCount,
		std::uint64_t gamepadAdditionRevision)
	{
		const bool connected = registeredGamepadCount > 0
			&& (initialized
				? gamepadAdditionRevision != observedGamepadAdditionRevision
				: true);
		initialized = true;
		observedGamepadAdditionRevision = gamepadAdditionRevision;
		return connected;
	}

	void reset()
	{
		initialized = false;
		observedGamepadAdditionRevision = 0;
	}

private:
	bool initialized = false;
	std::uint64_t observedGamepadAdditionRevision = 0;
};
}
