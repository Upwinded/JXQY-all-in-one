#include "../Game/Data/MagicControl.h"

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

	ok = check(canControlMagicTarget(3, 3, true), "target at max level can be controlled") && ok;
	ok = check(canControlMagicTarget(2, 3, true), "target below max level can be controlled") && ok;
	ok = check(!canControlMagicTarget(4, 3, true), "target above max level cannot be controlled") && ok;
	ok = check(!canControlMagicTarget(1, 3, false), "dead target cannot be controlled") && ok;

	return ok ? 0 : 1;
}
