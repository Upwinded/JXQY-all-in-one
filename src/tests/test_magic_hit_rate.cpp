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

	struct RatioCase
	{
		int attackerEvade;
		int targetEvade;
		int expectedHits;
	};
	for (const auto& sample : {
		RatioCase{100, 100, 60}, RatioCase{83, 183, 32},
		RatioCase{121, 225, 36}, RatioCase{9, 121, 30},
		RatioCase{100, 300, 30}, RatioCase{100, 500, 30},
		RatioCase{121, 9, 95}, RatioCase{0, 100, 30},
		RatioCase{0, 0, 60}, RatioCase{50, 0, 80},
		RatioCase{-10, 100, 30},
		RatioCase{std::numeric_limits<int>::max(), 1, 95},
		RatioCase{std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 30}})
	{
		int hits = 0;
		int abnormalHits = 0;
		for (int roll = 0; roll < 100; ++roll)
		{
			const bool hit = isMagicDamageHit(sample.attackerEvade, sample.targetEvade, roll);
			const bool abnormalHit = isMagicAbnormalStateHit(sample.attackerEvade, sample.targetEvade, roll);
			hits += hit;
			abnormalHits += abnormalHit;
			ok = check(!abnormalHit || hit, "a shared miss roll cannot apply negative states") && ok;
		}
		ok = check(hits == sample.expectedHits && abnormalHits == sample.expectedHits - 10,
			"damage evasion is 5..70 percent and abnormal evasion is exactly ten points higher") && ok;
	}
	ok = check(isMagicDamageHit(0, 100, 29) && !isMagicDamageHit(0, 100, 30)
		&& isMagicAbnormalStateHit(0, 100, 19) && !isMagicAbnormalStateHit(0, 100, 20),
		"maximum evasion keeps exact 30 and 20 percent minimum hit rates") && ok;
	ok = check(isMagicDamageHit(100, 100, 59) && !isMagicDamageHit(100, 100, 60)
		&& isMagicAbnormalStateHit(100, 100, 49) && !isMagicAbnormalStateHit(100, 100, 50),
		"inclusive 0..99 rolls give exact 60 and 50 percent rates at equal evade") && ok;

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
