#include "MagicControl.h"

bool canControlMagicTarget(int targetLevel, int magicMaxLevel, bool targetAlive)
{
	return targetAlive && targetLevel <= magicMaxLevel;
}
