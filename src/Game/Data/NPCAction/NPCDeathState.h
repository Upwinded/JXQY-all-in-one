#pragma once

enum class NPCSpecialDeathKind
{
	None,
	Frozen,
	Poisoned,
	Petrified,
};

inline NPCSpecialDeathKind selectNpcSpecialDeathKind(
	bool frozen,
	bool frozenVisualEffect,
	bool freezeFeatureEnabled,
	bool poisoned,
	bool poisonedVisualEffect,
	bool poisonFeatureEnabled,
	bool petrified,
	bool petrifiedVisualEffect,
	bool petrifyFeatureEnabled)
{
	if (frozen && frozenVisualEffect && freezeFeatureEnabled)
	{
		return NPCSpecialDeathKind::Frozen;
	}
	if (poisoned && poisonedVisualEffect && poisonFeatureEnabled)
	{
		return NPCSpecialDeathKind::Poisoned;
	}
	if (petrified && petrifiedVisualEffect && petrifyFeatureEnabled)
	{
		return NPCSpecialDeathKind::Petrified;
	}
	return NPCSpecialDeathKind::None;
}

inline bool shouldNpcSpecialDeathSuppressBody(NPCSpecialDeathKind deathKind)
{
	return deathKind != NPCSpecialDeathKind::None;
}
