#include "../Game/Data/MagicRegionShape.h"

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

bool samePoint(Point actual, Point expected)
{
	return actual.x == expected.x && actual.y == expected.y;
}
}

int main()
{
	bool ok = true;

	ok = check(getMagicRegionShapeRange(1) == 3, "level 1 region shape range is 3") && ok;
	ok = check(getMagicRegionShapeRange(3) == 3, "level 3 region shape range is 3") && ok;
	ok = check(getMagicRegionShapeRange(4) == 5, "level 4 region shape range expands to 5") && ok;
	ok = check(getMagicRegionShapeRange((std::numeric_limits<int>::max)()) == 9,
		"region shape clamps levels to the runtime maximum") && ok;
	ok = check(normalizeMagicRegionDirection(-1) == 7, "negative direction wraps to 7") && ok;
	ok = check(normalizeMagicRegionDirection(9) == 1, "large direction wraps to 1") && ok;

	auto southTiles = getVTypeMagicRegionTiles({ 10, 10 }, 0, 1);
	ok = check(southTiles.size() == 5, "level 1 V type creates center plus two two-tile arms") && ok;
	if (southTiles.size() == 5)
	{
		ok = check(samePoint(southTiles[0].position, { 10, 12 }) && southTiles[0].delayMilliseconds == 0,
			"V type starts at the tile in front of the caster") && ok;
		ok = check(samePoint(southTiles[1].position, { 10, 13 }) && southTiles[1].delayMilliseconds == 60,
			"left arm first tile follows direction -1") && ok;
		ok = check(samePoint(southTiles[2].position, { 9, 13 }) && southTiles[2].delayMilliseconds == 60,
			"right arm first tile follows direction +1") && ok;
		ok = check(samePoint(southTiles[3].position, { 11, 14 }) && southTiles[3].delayMilliseconds == 120,
			"left arm keeps walking outward with row parity") && ok;
		ok = check(samePoint(southTiles[4].position, { 9, 14 }) && southTiles[4].delayMilliseconds == 120,
			"right arm keeps walking outward with row parity") && ok;
	}

	auto westTiles = getVTypeMagicRegionTiles({ 10, 10 }, 2, 4);
	ok = check(westTiles.size() == 9, "level 4 V type creates five-step arms") && ok;
	if (westTiles.size() >= 3)
	{
		ok = check(samePoint(westTiles[0].position, { 9, 10 }), "west-facing V starts west of caster") && ok;
		ok = check(samePoint(westTiles[1].position, { 8, 11 }), "west-facing left arm follows direction 1") && ok;
		ok = check(samePoint(westTiles[2].position, { 8, 9 }), "west-facing right arm follows direction 3") && ok;
	}
	const int minimumInteger = (std::numeric_limits<int>::min)();
	const int maximumInteger = (std::numeric_limits<int>::max)();
	ok = check(samePoint(getMagicRegionSubPoint({ maximumInteger, minimumInteger }, 6),
		{ maximumInteger, minimumInteger }), "region tile step saturates extreme coordinates") && ok;
	ok = check(getVTypeMagicRegionTiles({ 0, 0 }, 0, maximumInteger).size() == 17,
		"region tile generation stays bounded for extreme levels") && ok;

	return ok ? 0 : 1;
}
