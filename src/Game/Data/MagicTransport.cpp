#include "MagicTransport.h"

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

Point getMagicTransportNeighbor(Point from, int direction)
{
	const int normalizedDirection = normalizeDirection(direction);
	const int line = std::abs(from.y % 2);
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

std::optional<Point> resolveMagicTransportDestination(Point preferred, const std::function<bool(Point)>& canStand)
{
	if (!canStand)
	{
		return std::nullopt;
	}
	if (canStand(preferred))
	{
		return preferred;
	}

	for (int direction = 0; direction < 8; direction++)
	{
		Point candidate = getMagicTransportNeighbor(preferred, direction);
		if (canStand(candidate))
		{
			return candidate;
		}
	}

	return std::nullopt;
}
