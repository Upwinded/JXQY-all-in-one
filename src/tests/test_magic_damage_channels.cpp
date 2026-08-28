#include "../Game/Data/MagicDamageChannels.h"

#include <iostream>
#include <limits>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

int main()
{
	bool ok = true;

	MagicLevel levelInfo;
	levelInfo.rangeDamage = 40;
	levelInfo.effect2 = 11;
	levelInfo.effect3 = 7;
	levelInfo.effectMana = 99;

	auto rangeChannels = makeRangeDamageChannels(levelInfo);
	ok = check(rangeChannels.damage == 40, "range damage uses RangeDamage as primary channel") && ok;
	ok = check(rangeChannels.damage2 == 11, "range damage uses raw Effect2 channel") && ok;
	ok = check(rangeChannels.damage3 == 7, "range damage uses raw Effect3 channel") && ok;
	ok = check(rangeChannels.damageMana == 0, "range damage does not inherit EffectMana") && ok;

	auto reducedChannels = reduceMagicDamageChannels({ 101, 55, 33, 20 }, 25);
	ok = check(reducedChannels.damage == 76, "leap reduction uses integer primary damage semantics") && ok;
	ok = check(reducedChannels.damage2 == 42, "leap reduction applies to Effect2") && ok;
	ok = check(reducedChannels.damage3 == 25, "leap reduction applies to Effect3") && ok;
	ok = check(reducedChannels.damageMana == 15, "leap reduction applies to EffectMana") && ok;

	auto clampedChannels = reduceMagicDamageChannels({ -1, -2, -3, -4 }, 50);
	ok = check(clampedChannels.damage == 0, "reduced primary damage clamps to zero") && ok;
	ok = check(clampedChannels.damage2 == 0, "reduced Effect2 clamps to zero") && ok;
	ok = check(clampedChannels.damage3 == 0, "reduced Effect3 clamps to zero") && ok;
	ok = check(clampedChannels.damageMana == 0, "reduced EffectMana clamps to zero") && ok;

	const int maximumInteger = (std::numeric_limits<int>::max)();
	auto extremeChannels = reduceMagicDamageChannels(
		{ maximumInteger, maximumInteger, maximumInteger, maximumInteger }, maximumInteger);
	ok = check(extremeChannels.damage == 0 && extremeChannels.damage2 == 0
		&& extremeChannels.damage3 == 0 && extremeChannels.damageMana == 0,
		"extreme reduction avoids signed overflow and clamps all channels") && ok;

	ok = check(isLifeFrameSelfAnchoredSpecialKind(mskAddDamageReduceShield),
		"self damage-reduce shield duration uses LifeFrame") && ok;
	ok = check(isLifeFrameSelfAnchoredSpecialKind(mskBlockDamage),
		"self block-damage buff duration uses LifeFrame") && ok;
	ok = check(isLifeFrameSelfAnchoredSpecialKind(mskAddShield),
		"self shield duration uses LifeFrame") && ok;
	ok = check(!isLifeFrameSelfAnchoredSpecialKind(mskImmobilize),
		"immobilize remains a hit status instead of a self shield duration") && ok;
	ok = check(!isLifeFrameSelfAnchoredSpecialKind(mskClearAbnormalState),
		"self clear-abnormal effect keeps immediate visual duration") && ok;

	return ok ? 0 : 1;
}
