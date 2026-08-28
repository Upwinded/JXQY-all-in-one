#include "../Game/Data/NPCAction/NPCDeathState.h"

#include <iostream>

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

	ok = check(selectNpcSpecialDeathKind(false, true, true, false, true, true, false, true, true) == NPCSpecialDeathKind::None,
		"normal death does not select a special death kind") && ok;
	ok = check(!shouldNpcSpecialDeathSuppressBody(NPCSpecialDeathKind::None),
		"normal death does not suppress body creation") && ok;

	ok = check(selectNpcSpecialDeathKind(true, true, true, false, true, true, false, true, true) == NPCSpecialDeathKind::Frozen,
		"frozen visual death selects frozen special death") && ok;
	ok = check(selectNpcSpecialDeathKind(true, false, true, false, true, true, false, true, true) == NPCSpecialDeathKind::None,
		"frozen death without visual effect does not select special death") && ok;
	ok = check(selectNpcSpecialDeathKind(true, true, false, false, true, true, false, true, true) == NPCSpecialDeathKind::None,
		"disabled frozen feature does not select special death") && ok;

	ok = check(selectNpcSpecialDeathKind(false, true, true, true, true, true, false, true, true) == NPCSpecialDeathKind::Poisoned,
		"poison visual death selects poison special death") && ok;
	ok = check(selectNpcSpecialDeathKind(false, true, true, false, true, true, true, true, true) == NPCSpecialDeathKind::Petrified,
		"petrified visual death selects petrified special death") && ok;
	ok = check(selectNpcSpecialDeathKind(true, true, true, true, true, true, true, true, true) == NPCSpecialDeathKind::Frozen,
		"frozen death keeps priority over other abnormal deaths") && ok;
	ok = check(selectNpcSpecialDeathKind(false, true, true, true, true, true, true, true, true) == NPCSpecialDeathKind::Poisoned,
		"poison death keeps priority over petrified death") && ok;

	ok = check(shouldNpcSpecialDeathSuppressBody(NPCSpecialDeathKind::Frozen),
		"frozen special death suppresses body creation independent of resource load") && ok;
	ok = check(shouldNpcSpecialDeathSuppressBody(NPCSpecialDeathKind::Poisoned),
		"poison special death suppresses body creation independent of resource load") && ok;
	ok = check(shouldNpcSpecialDeathSuppressBody(NPCSpecialDeathKind::Petrified),
		"petrified special death suppresses body creation independent of resource load") && ok;

	return ok ? 0 : 1;
}
