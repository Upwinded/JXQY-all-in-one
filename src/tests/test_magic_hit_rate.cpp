#include "../Game/Data/MagicHitRate.h"

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

	ok = check(!isMagicDamageHitAgainstNPC(0, 0, 0)
		&& isMagicDamageHitAgainstNPC(0, 0, 1)
		&& isMagicDamageHitAgainstNPC(0, 0, 100),
		"NPC target keeps the original equal-evade closed-roll boundary") && ok;
	ok = check(isMagicDamageHitAgainstNPC(3, 0, 0),
		"JXQY2 home Black Sha evade 3 always hits the zero-evade stone") && ok;
	ok = check(!isMagicDamageHitAgainstNPC(9, 12, 3)
		&& isMagicDamageHitAgainstNPC(9, 12, 4),
		"JXQY2 player evade 9 keeps the Black Sha evade 12 boundary") && ok;
	ok = check(!isMagicDamageHitAgainstNPC(9, 14, 5)
		&& isMagicDamageHitAgainstNPC(9, 14, 6),
		"JXQY2 player evade 9 keeps the White Sha evade 14 boundary") && ok;
	ok = check(!isMagicDamageHitAgainstNPC(50, 100, 50)
		&& isMagicDamageHitAgainstNPC(50, 100, 51),
		"NPC target compares the inclusive 0..100 roll with the evade difference") && ok;
	ok = check(!isMagicDamageHitAgainstNPC(0, 100, 100),
		"NPC target cannot be hit when its evade advantage reaches 100") && ok;
	ok = check(!isMagicDamageHitAgainstNPC(
		std::numeric_limits<int>::min(),
		std::numeric_limits<int>::max(),
		100),
		"NPC target keeps an extreme target evade advantage as a miss") && ok;
	ok = check(isMagicDamageHitAgainstNPC(
		std::numeric_limits<int>::max(),
		std::numeric_limits<int>::min(),
		0),
		"NPC target keeps an extreme attacker evade advantage as a hit") && ok;

	ok = check(calculatePlayerMagicDamageHitRollMaximum(0, 0) == 50
		&& !isMagicDamageHitAgainstPlayer(0, 0, 0)
		&& isMagicDamageHitAgainstPlayer(0, 0, 1)
		&& isMagicDamageHitAgainstPlayer(0, 0, 50),
		"player target keeps the original equal-evade inclusive 0..50 roll") && ok;
	ok = check(calculatePlayerMagicDamageHitRollMaximum(3, 0) == 50
		&& !isMagicDamageHitAgainstPlayer(3, 0, 0)
		&& isMagicDamageHitAgainstPlayer(3, 0, 1),
		"player target clamps an attacker evade advantage to zero difference") && ok;
	ok = check(calculatePlayerMagicDamageHitRollMaximum(0, 50) == 100
		&& !isMagicDamageHitAgainstPlayer(0, 50, 50)
		&& isMagicDamageHitAgainstPlayer(0, 50, 51),
		"player target expands the inclusive roll range with its evade advantage") && ok;
	ok = check(calculatePlayerMagicDamageHitRollMaximum(0, 100) == 100
		&& !isMagicDamageHitAgainstPlayer(0, 100, 100),
		"player target cannot be hit when its evade advantage reaches 100") && ok;
	ok = check(calculatePlayerMagicDamageHitRollMaximum(
		std::numeric_limits<int>::min(),
		std::numeric_limits<int>::max()) == 100
		&& !isMagicDamageHitAgainstPlayer(
			std::numeric_limits<int>::min(),
			std::numeric_limits<int>::max(),
			100),
		"player target keeps an extreme target evade advantage as a miss") && ok;
	ok = check(calculatePlayerMagicDamageHitRollMaximum(
		std::numeric_limits<int>::max(),
		std::numeric_limits<int>::min()) == 50
		&& !isMagicDamageHitAgainstPlayer(
			std::numeric_limits<int>::max(),
			std::numeric_limits<int>::min(),
			0)
		&& isMagicDamageHitAgainstPlayer(
			std::numeric_limits<int>::max(),
			std::numeric_limits<int>::min(),
			1),
		"player target clamps an extreme attacker advantage without overflow") && ok;

	ok = check(shouldBeginHurtActionAfterMagicDamage(0, false, false, false),
		"one of four reference rolls begins the hurt action") && ok;
	ok = check(!shouldBeginHurtActionAfterMagicDamage(1, false, false, false)
		&& !shouldBeginHurtActionAfterMagicDamage(2, false, false, false)
		&& !shouldBeginHurtActionAfterMagicDamage(3, false, false, false),
		"the other three reference rolls keep the current action") && ok;
	ok = check(!shouldBeginHurtActionAfterMagicDamage(0, true, false, false),
		"special-effect reactions keep their dedicated action") && ok;
	ok = check(!shouldBeginHurtActionAfterMagicDamage(0, false, true, false),
		"immobilized targets do not enter the hurt action") && ok;
	ok = check(!shouldBeginHurtActionAfterMagicDamage(0, false, false, true),
		"petrified targets do not enter the hurt action") && ok;

	return ok ? 0 : 1;
}
