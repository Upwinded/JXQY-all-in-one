#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace GameInput
{
struct TouchRecoveryContact
{
	std::int64_t id = 0;
	int x = 0;
	int y = 0;
};

class TouchControlsRecoveryGesture
{
public:
	static constexpr std::size_t RequiredContactCount = 3;
	static constexpr int MaximumMovementPixels = 24;
	static constexpr std::uint64_t HoldMilliseconds = 1000;

	bool update(
		std::vector<TouchRecoveryContact> contacts,
		std::uint64_t nowMilliseconds,
		bool touchControlsVisible)
	{
		if (touchControlsVisible)
		{
			reset();
			return false;
		}
		std::sort(contacts.begin(), contacts.end(),
			[](const TouchRecoveryContact& left,
				const TouchRecoveryContact& right)
			{
				return left.id < right.id;
			});
		if (contacts.size() != RequiredContactCount)
		{
			reset();
			return false;
		}
		if (triggered)
		{
			return false;
		}

		bool contactSetChanged = startContacts.size() != contacts.size();
		if (!contactSetChanged)
		{
			for (std::size_t index = 0; index < contacts.size(); index++)
			{
				if (startContacts[index].id != contacts[index].id)
				{
					contactSetChanged = true;
					break;
				}
			}
		}
		if (contactSetChanged)
		{
			startContacts = std::move(contacts);
			startedAt = nowMilliseconds;
			return false;
		}

		constexpr std::int64_t maximumMovementSquared =
			static_cast<std::int64_t>(MaximumMovementPixels)
			* MaximumMovementPixels;
		for (std::size_t index = 0; index < contacts.size(); index++)
		{
			const std::int64_t deltaX =
				static_cast<std::int64_t>(contacts[index].x)
				- startContacts[index].x;
			const std::int64_t deltaY =
				static_cast<std::int64_t>(contacts[index].y)
				- startContacts[index].y;
			if (deltaX * deltaX + deltaY * deltaY > maximumMovementSquared)
			{
				startContacts = std::move(contacts);
				startedAt = nowMilliseconds;
				return false;
			}
		}

		if (nowMilliseconds - startedAt < HoldMilliseconds)
		{
			return false;
		}
		triggered = true;
		return true;
	}

	void reset()
	{
		startContacts.clear();
		startedAt = 0;
		triggered = false;
	}

private:
	std::vector<TouchRecoveryContact> startContacts;
	std::uint64_t startedAt = 0;
	bool triggered = false;
};
}
