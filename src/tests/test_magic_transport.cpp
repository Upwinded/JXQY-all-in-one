#include "../Game/Data/MagicTransport.h"

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

	Point even = { 10, 10 };
	ok = check(samePoint(getMagicTransportNeighbor(even, 0), { 10, 12 }), "even row direction 0 moves two rows down") && ok;
	ok = check(samePoint(getMagicTransportNeighbor(even, 1), { 9, 11 }), "even row direction 1 moves down-left") && ok;
	ok = check(samePoint(getMagicTransportNeighbor(even, 5), { 10, 9 }), "even row direction 5 moves up-right") && ok;
	ok = check(samePoint(getMagicTransportNeighbor(even, 7), { 10, 11 }), "even row direction 7 moves down-right") && ok;

	Point odd = { 10, 11 };
	ok = check(samePoint(getMagicTransportNeighbor(odd, 1), { 10, 12 }), "odd row direction 1 keeps x while moving down-left") && ok;
	ok = check(samePoint(getMagicTransportNeighbor(odd, 5), { 11, 10 }), "odd row direction 5 moves up-right") && ok;
	ok = check(samePoint(getMagicTransportNeighbor(odd, 7), { 11, 12 }), "odd row direction 7 moves down-right") && ok;

	Point preferred = { 5, 6 };
	auto directDestination = resolveMagicTransportDestination(preferred, [preferred](Point candidate)
	{
		return samePoint(candidate, preferred);
	});
	ok = check(directDestination.has_value() && samePoint(*directDestination, preferred), "transport keeps walkable preferred tile") && ok;

	Point thirdNeighbor = getMagicTransportNeighbor(preferred, 3);
	auto fallbackDestination = resolveMagicTransportDestination(preferred, [thirdNeighbor](Point candidate)
	{
		return samePoint(candidate, thirdNeighbor);
	});
	ok = check(fallbackDestination.has_value() && samePoint(*fallbackDestination, thirdNeighbor), "transport uses first walkable neighbor in direction order") && ok;

	auto missingDestination = resolveMagicTransportDestination(preferred, [](Point)
	{
		return false;
	});
	ok = check(!missingDestination.has_value(), "transport returns no destination when all candidates are blocked") && ok;

	const int minimumInteger = (std::numeric_limits<int>::min)();
	const int maximumInteger = (std::numeric_limits<int>::max)();
	ok = check(samePoint(getMagicTransportNeighbor({ maximumInteger, minimumInteger }, 6),
		{ maximumInteger, minimumInteger }), "transport neighbor saturates extreme coordinates") && ok;
	ok = check(samePoint(getMagicTransportNeighbor({ 0, minimumInteger }, 4),
		{ 0, minimumInteger }), "transport neighbor handles minimum integer row parity") && ok;

	return ok ? 0 : 1;
}
