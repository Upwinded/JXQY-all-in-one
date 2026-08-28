#include "../Game/Data/MagicBeginPosition.h"

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

bool checkSubPoint(Point origin, int direction, Point expected, const char* rowLabel)
{
	Point actual = getMagicBeginPositionSubPoint(origin, direction);
	if (samePoint(actual, expected))
	{
		return true;
	}

	std::cerr << "FAILED: " << rowLabel << " direction " << direction
		<< " expected (" << expected.x << ", " << expected.y << ")"
		<< " actual (" << actual.x << ", " << actual.y << ")\n";
	return false;
}

bool checkSubPointTable(Point origin, const Point (&expected)[8], const char* rowLabel)
{
	bool ok = true;
	for (int direction = 0; direction < 8; direction++)
	{
		ok = checkSubPoint(origin, direction, expected[direction], rowLabel) && ok;
	}
	return ok;
}
}

int main()
{
	bool ok = true;

	MagicBeginPositionRules beginAtMouse;
	beginAtMouse.beginAtMouse = true;
	Point mouseFrom = { 10, 10 };
	Point mouseTo = { 10, 14 };
	applyMagicBeginPositionRules(beginAtMouse, 4, 0, 0, mouseFrom, mouseTo);
	ok = check(samePoint(mouseFrom, { 10, 12 }), "BeginAtMouse starts one tile back from destination toward source") && ok;
	ok = check(samePoint(mouseTo, { 10, 14 }), "BeginAtMouse keeps destination") && ok;
	Point sameMouseFrom = { 10, 10 };
	Point sameMouseTo = { 10, 10 };
	applyMagicBeginPositionRules(beginAtMouse, 4, 0, 0, sameMouseFrom, sameMouseTo);
	ok = check(samePoint(sameMouseFrom, { 10, 10 }), "BeginAtMouse keeps equal source") && ok;
	ok = check(samePoint(sameMouseTo, { 10, 10 }), "BeginAtMouse keeps equal destination") && ok;

	MagicBeginPositionRules beginAtUser;
	beginAtUser.beginAtUser = true;
	Point userFrom = { 10, 10 };
	Point userTo = { 12, 10 };
	applyMagicBeginPositionRules(beginAtUser, 2, 6, 0, userFrom, userTo);
	ok = check(samePoint(userFrom, { 10, 10 }), "BeginAtUser keeps source") && ok;
	ok = check(samePoint(userTo, { 10, 10 }), "BeginAtUser moves destination to source") && ok;
	ok = check(shouldPreserveMagicZeroMoveDirection(beginAtUser), "BeginAtUser preserves zero move direction") && ok;

	MagicBeginPositionRules directionOffset;
	directionOffset.beginAtUserAddDirectionOffset = true;
	Point offsetFrom = { 10, 10 };
	Point offsetTo = { 10, 16 };
	applyMagicBeginPositionRules(directionOffset, 4, 0, 0, offsetFrom, offsetTo);
	ok = check(samePoint(offsetFrom, { 10, 10 }), "BeginAtUserAddDirectionOffset keeps source") && ok;
	ok = check(samePoint(offsetTo, { 10, 12 }), "BeginAtUserAddDirectionOffset moves destination one tile toward target") && ok;
	ok = check(shouldPreserveMagicZeroMoveDirection(directionOffset), "BeginAtUserAddDirectionOffset preserves zero move direction") && ok;
	Point sameOffsetFrom = { 10, 10 };
	Point sameOffsetTo = { 10, 10 };
	applyMagicBeginPositionRules(directionOffset, 4, 0, 0, sameOffsetFrom, sameOffsetTo);
	ok = check(samePoint(sameOffsetFrom, { 10, 10 }), "BeginAtUserAddDirectionOffset keeps equal source") && ok;
	ok = check(samePoint(sameOffsetTo, { 10, 10 }), "BeginAtUserAddDirectionOffset keeps equal destination") && ok;

	MagicBeginPositionRules userDirectionOffset;
	userDirectionOffset.beginAtUserAddUserDirectionOffset = true;
	Point userDirectionFrom = { 10, 10 };
	Point userDirectionTo = { 8, 8 };
	applyMagicBeginPositionRules(userDirectionOffset, 0, 0, 6, userDirectionFrom, userDirectionTo);
	ok = check(samePoint(userDirectionFrom, { 10, 10 }), "BeginAtUserAddUserDirectionOffset keeps source") && ok;
	ok = check(samePoint(userDirectionTo, { 11, 10 }), "BeginAtUserAddUserDirectionOffset uses caster direction") && ok;
	ok = check(!shouldPreserveMagicZeroMoveDirection(userDirectionOffset), "BeginAtUserAddUserDirectionOffset does not force zero move preservation") && ok;

	const Point evenRowExpected[8] = {
		{ 10, 12 },
		{ 9, 11 },
		{ 9, 10 },
		{ 9, 9 },
		{ 10, 8 },
		{ 10, 9 },
		{ 11, 10 },
		{ 10, 11 },
	};
	const Point oddRowExpected[8] = {
		{ 10, 13 },
		{ 10, 12 },
		{ 9, 11 },
		{ 10, 10 },
		{ 10, 9 },
		{ 11, 10 },
		{ 11, 11 },
		{ 11, 12 },
	};
	ok = checkSubPointTable({ 10, 10 }, evenRowExpected, "even row") && ok;
	ok = checkSubPointTable({ 10, 11 }, oddRowExpected, "odd row") && ok;
	ok = check(samePoint(getMagicBeginPositionSubPoint({ 10, 11 }, 7), { 11, 12 }),
		"begin-position tile step follows odd-row direction geometry") && ok;
	ok = check(samePoint(getMagicBeginPositionSubPoint({ 10, 10 }, -2), { 11, 10 }),
		"begin-position tile step normalizes negative directions") && ok;
	const int minimumInteger = (std::numeric_limits<int>::min)();
	const int maximumInteger = (std::numeric_limits<int>::max)();
	ok = check(samePoint(getMagicBeginPositionSubPoint({ maximumInteger, minimumInteger }, 6),
		{ maximumInteger, minimumInteger }), "begin-position tile step saturates extreme coordinates") && ok;
	ok = check(samePoint(getMagicBeginPositionSubPoint({ 0, minimumInteger }, 4),
		{ 0, minimumInteger }), "begin-position parity handles minimum integer row") && ok;

	return ok ? 0 : 1;
}
