#pragma once

#include "../../Resource/ResourceManifest.h"

#include <algorithm>
#include <cmath>
#include <climits>

inline int calculateDefeatedNpcBaseExperience(
	const ResourceManifest& manifest,
	int recipientLevel,
	int defeatedNpcLevel,
	int defeatedNpcStoredExperience,
	int defeatedNpcExperienceBonus,
	bool defeatedNpcIsHostileBattleNpc = false)
{
	if (manifest.resolvedDefeatedNpcExperienceMode() ==
		DefeatedNpcExperienceMode::StoredExperience)
	{
		if (defeatedNpcStoredExperience != 0 ||
			!defeatedNpcIsHostileBattleNpc)
		{
			return std::max(0, defeatedNpcStoredExperience);
		}
	}

	const long long levelProduct =
		static_cast<long long>(std::max(0, recipientLevel)) *
		static_cast<long long>(std::max(0, defeatedNpcLevel));
	const long long withBonus = levelProduct +
		static_cast<long long>(defeatedNpcExperienceBonus);
	constexpr long long MinimumLevelProductExperience = 4;
	return static_cast<int>(std::min<long long>(
		INT_MAX,
		std::max<long long>(MinimumLevelProductExperience, withBonus)));
}

inline double scaleAutomaticExperience(
	int baseExperience,
	double multiplier)
{
	if (baseExperience <= 0 || !std::isfinite(multiplier) || multiplier <= 0.0)
	{
		return 0.0;
	}
	return std::min(
		static_cast<double>(INT_MAX),
		static_cast<double>(baseExperience) * multiplier);
}

inline int roundAutomaticExperience(double scaledExperience)
{
	if (!std::isfinite(scaledExperience) || scaledExperience <= 0.0)
	{
		return 0;
	}
	return static_cast<int>(std::min<long long>(
		INT_MAX,
		std::llround(scaledExperience)));
}

inline int floorAutomaticExperience(
	double scaledExperience,
	double fraction)
{
	if (!std::isfinite(scaledExperience) || scaledExperience <= 0.0 ||
		!std::isfinite(fraction) || fraction <= 0.0)
	{
		return 0;
	}
	return static_cast<int>(std::min<double>(
		INT_MAX,
		std::floor(scaledExperience * fraction)));
}
