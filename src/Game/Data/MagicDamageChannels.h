#pragma once

#include "Magic.h"

#include <algorithm>
#include <cstdint>

struct MagicDamageChannels
{
	int damage = 0;
	int damage2 = 0;
	int damage3 = 0;
	int damageMana = 0;
};

inline MagicDamageChannels makeRangeDamageChannels(const MagicLevel& levelInfo)
{
	return { levelInfo.rangeDamage, levelInfo.effect2, levelInfo.effect3, 0 };
}

inline MagicDamageChannels reduceMagicDamageChannels(MagicDamageChannels channels, int percentage)
{
	auto reduceChannel = [percentage](int value)
	{
		if (value <= 0)
		{
			return 0;
		}
		if (percentage <= 0)
		{
			return value;
		}

		const int64_t reduced = static_cast<int64_t>(value)
			- static_cast<int64_t>(value) * static_cast<int64_t>(percentage) / 100;
		return reduced > 0 ? static_cast<int>(reduced) : 0;
	};

	channels.damage = reduceChannel(channels.damage);
	channels.damage2 = reduceChannel(channels.damage2);
	channels.damage3 = reduceChannel(channels.damage3);
	channels.damageMana = reduceChannel(channels.damageMana);
	return channels;
}
