#pragma once

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>
#include "../GameTypes.h"

struct MagicRegionFixedTile
{
	Point position = { 0, 0 };
	int delayMilliseconds = 0;
};

inline int normalizeMagicRegionDirection(int direction)
{
	int normalized = direction % 8;
	return normalized < 0 ? normalized + 8 : normalized;
}

inline Point getMagicRegionSubPoint(Point from, int direction)
{
	direction = normalizeMagicRegionDirection(direction);
	int line = std::abs(from.y % 2);
	auto addCoordinate = [](int value, int offset)
	{
		const long long result = static_cast<long long>(value) + offset;
		return static_cast<int>(std::clamp(result,
			static_cast<long long>((std::numeric_limits<int>::min)()),
			static_cast<long long>((std::numeric_limits<int>::max)())));
	};
	Point to = from;
	switch (direction)
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

inline int getMagicRegionShapeRange(int level)
{
	level = std::clamp(level, 1, MAGIC_MAX_LEVEL);
	return 3 + ((level - 1) / 3) * 2;
}

inline std::vector<MagicRegionFixedTile> getWaveMagicRegionTiles(Point origin, int direction, int level)
{
	const int forward = normalizeMagicRegionDirection(direction);
	const int side = normalizeMagicRegionDirection(forward + 2);
	const int rows = getMagicRegionShapeRange(level);
	std::vector<MagicRegionFixedTile> tiles;
	tiles.reserve(rows * 5);
	Point row = getMagicRegionSubPoint(origin, forward);
	for (int i = 0; i < 2; ++i) row = getMagicRegionSubPoint(row, side + 4);
	for (int i = 0; i < rows; ++i)
	{
		Point tile = row;
		for (int j = 0; j < 5; ++j)
		{
			tiles.push_back({tile, i * 60});
			tile = getMagicRegionSubPoint(tile, side);
		}
		row = getMagicRegionSubPoint(row, forward);
	}
	return tiles;
}

inline std::vector<MagicRegionFixedTile> getVTypeMagicRegionTiles(Point origin, int direction, int level)
{
	const int normalizedDirection = normalizeMagicRegionDirection(direction);
	const int count = getMagicRegionShapeRange(level);
	const int delayPerStepMilliseconds = 60;

	std::vector<MagicRegionFixedTile> tiles;
	tiles.reserve(count > 0 ? 1 + (count - 1) * 2 : 0);

	Point center = getMagicRegionSubPoint(origin, normalizedDirection);
	tiles.push_back({ center, 0 });

	Point leftTile = center;
	Point rightTile = center;
	for (int i = 1; i < count; i++)
	{
		leftTile = getMagicRegionSubPoint(leftTile, normalizedDirection - 1);
		rightTile = getMagicRegionSubPoint(rightTile, normalizedDirection + 1);
		int delayMilliseconds = i * delayPerStepMilliseconds;
		tiles.push_back({ leftTile, delayMilliseconds });
		tiles.push_back({ rightTile, delayMilliseconds });
	}

	return tiles;
}
