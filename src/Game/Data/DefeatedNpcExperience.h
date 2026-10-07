#pragma once

#include "../../Resource/ResourceManifest.h"

#include <algorithm>
#include <cmath>
#include <climits>

// 存储经验缺失（exp=0 的敌对战斗 NPC）时整套借用的月影击杀规则：
// 人物拿等级乘积原值，武功按下述比例获得经验。
inline constexpr float FallbackPracticeKillFraction = 0.2222f;
inline constexpr float FallbackUseKillFraction = 0.0333f;

inline int calculateDefeatedNpcBaseExperience(
	const ResourceManifest& manifest,
	int recipientLevel,
	int defeatedNpcLevel,
	int defeatedNpcStoredExperience,
	int defeatedNpcExperienceBonus,
	bool defeatedNpcIsHostileBattleNpc = false,
	bool* usedLevelProductFallback = nullptr)
{
	if (usedLevelProductFallback != nullptr)
	{
		*usedLevelProductFallback = false;
	}
	if (manifest.resolvedDefeatedNpcExperienceMode() ==
		DefeatedNpcExperienceMode::StoredExperience)
	{
		if (defeatedNpcStoredExperience != 0 ||
			!defeatedNpcIsHostileBattleNpc)
		{
			return std::max(0, defeatedNpcStoredExperience);
		}
		if (usedLevelProductFallback != nullptr)
		{
			*usedLevelProductFallback = true;
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
