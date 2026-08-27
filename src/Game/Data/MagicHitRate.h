#pragma once

#include <cstdint>

// The legacy runtime intentionally uses different closed random ranges for
// NPC and Player targets. EngineBase::getRand(maximum) includes both endpoints.
inline std::int64_t calculateMagicDamageEvadeDifference(
	int attackerEvade,
	int targetEvade)
{
	return static_cast<std::int64_t>(targetEvade)
		- static_cast<std::int64_t>(attackerEvade);
}

inline bool isMagicDamageHitAgainstNPC(
	int attackerEvade,
	int targetEvade,
	int rollInclusive0To100)
{
	return static_cast<std::int64_t>(rollInclusive0To100)
		> calculateMagicDamageEvadeDifference(attackerEvade, targetEvade);
}

inline int calculatePlayerMagicDamageHitRollMaximum(
	int attackerEvade,
	int targetEvade)
{
	std::int64_t evadeDifference = calculateMagicDamageEvadeDifference(
		attackerEvade, targetEvade);
	if (evadeDifference < 0)
	{
		evadeDifference = 0;
	}
	return evadeDifference < 50
		? static_cast<int>(evadeDifference) + 50
		: 100;
}

inline bool isMagicDamageHitAgainstPlayer(
	int attackerEvade,
	int targetEvade,
	int rollInclusive0ToConfiguredMaximum)
{
	std::int64_t evadeDifference = calculateMagicDamageEvadeDifference(
		attackerEvade, targetEvade);
	if (evadeDifference < 0)
	{
		evadeDifference = 0;
	}
	return static_cast<std::int64_t>(rollInclusive0ToConfiguredMaximum)
		> evadeDifference;
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
