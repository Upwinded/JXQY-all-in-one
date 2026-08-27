#pragma once

#include "../GameTypes.h"

struct MagicBeginPositionRules
{
	bool beginAtMouse = false;
	bool beginAtUser = false;
	bool beginAtUserAddDirectionOffset = false;
	bool beginAtUserAddUserDirectionOffset = false;
};

bool shouldPreserveMagicZeroMoveDirection(const MagicBeginPositionRules& rules);
Point getMagicBeginPositionSubPoint(Point from, int direction);
void applyMagicBeginPositionRules(
	const MagicBeginPositionRules& rules,
	int directionToSource,
	int directionToDestination,
	int userDirection,
	Point& from,
	Point& to);
