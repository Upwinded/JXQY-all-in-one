#pragma once

#include <algorithm>

// Damage evasion stays within 5..70 percent on the shared ratio curve.
inline int calculateMagicDamageHitPercent(int attackerEvade, int targetEvade)
{
	attackerEvade = std::max(attackerEvade, 0);
	targetEvade = std::max(targetEvade, 0);
	float hitRatio = 0.1f;
	if (targetEvade >= attackerEvade)
	{
		// Equal zero evades use the equal-evade rate without dividing by zero.
		hitRatio += targetEvade > 0
			? static_cast<float>(attackerEvade) / static_cast<float>(targetEvade) * 0.5f
			: 0.5f;
	}
	else
	{
		const float advantage = (static_cast<float>(attackerEvade)
			- static_cast<float>(targetEvade)) / 100.0f;
		hitRatio += 0.5f + std::min(advantage, 1.0f) * 0.4f;
	}
	return std::clamp(static_cast<int>(hitRatio * 100.0f), 30, 95);
}

inline bool isMagicDamageHit(int attackerEvade, int targetEvade, int rollInclusive0To99)
{
	return rollInclusive0To99 < calculateMagicDamageHitPercent(attackerEvade, targetEvade);
}

inline int calculateMagicAbnormalStateHitPercent(int attackerEvade, int targetEvade)
{
	return calculateMagicDamageHitPercent(attackerEvade, targetEvade) - 10;
}

inline bool isMagicAbnormalStateHit(int attackerEvade, int targetEvade, int rollInclusive0To99)
{
	return rollInclusive0To99 < calculateMagicAbnormalStateHitPercent(attackerEvade, targetEvade);
}

inline bool shouldBeginHurtActionAfterMagicDamage(
	int rollInclusive0To3,
	bool handledSpecialEffect,
	bool immobilized,
	bool petrified)
{
	return rollInclusive0To3 == 0
		&& !handledSpecialEffect
		&& !immobilized
		&& !petrified;
}
