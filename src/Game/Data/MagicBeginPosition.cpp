#include "MagicBeginPosition.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace
{
int normalizeDirection(int direction)
{
	return ((direction % 8) + 8) % 8;
}

int addCoordinate(int value, int offset)
{
	const long long result = static_cast<long long>(value) + offset;
	return static_cast<int>(std::clamp(result,
		static_cast<long long>((std::numeric_limits<int>::min)()),
		static_cast<long long>((std::numeric_limits<int>::max)())));
}
}

bool shouldPreserveMagicZeroMoveDirection(const MagicBeginPositionRules& rules)
{
	return rules.beginAtUser || rules.beginAtUserAddDirectionOffset;
}

Point getMagicBeginPositionSubPoint(Point from, int direction)
{
	int normalizedDirection = normalizeDirection(direction);
	int line = std::abs(from.y % 2);
	Point to = from;
	switch (normalizedDirection)
	{
	case 0:
		to.y = addCoordinate(to.y, 2);
		break;
	case 1:
		to.y = addCoordinate(to.y, 1);
		to.x = addCoordinate(to.x, line - 1);
		break;
	case 2:
		to.x = addCoordinate(to.x, -1);
		break;
	case 3:
		to.y = addCoordinate(to.y, -1);
		to.x = addCoordinate(to.x, line - 1);
		break;
	case 4:
		to.y = addCoordinate(to.y, -2);
		break;
	case 5:
		to.y = addCoordinate(to.y, -1);
		to.x = addCoordinate(to.x, line);
		break;
	case 6:
		to.x = addCoordinate(to.x, 1);
		break;
	case 7:
		to.y = addCoordinate(to.y, 1);
		to.x = addCoordinate(to.x, line);
		break;
	default:
		break;
	}
	return to;
}

void applyMagicBeginPositionRules(
	const MagicBeginPositionRules& rules,
	int directionToSource,
	int directionToDestination,
	int userDirection,
	Point& from,
	Point& to)
{
	if (rules.beginAtMouse)
	{
		if (from != to)
		{
			from = getMagicBeginPositionSubPoint(to, directionToSource);
		}
	}
	else if (rules.beginAtUser)
	{
		to = from;
	}
	else if (rules.beginAtUserAddDirectionOffset)
	{
		if (from != to)
		{
			to = getMagicBeginPositionSubPoint(from, directionToDestination);
		}
	}
	else if (rules.beginAtUserAddUserDirectionOffset)
	{
		to = getMagicBeginPositionSubPoint(from, userDirection);
	}
}
