#include "../Component/FlatComponentPolicy.h"

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
	using FlatComponentPolicy::VisualState;
	bool ok = true;

	ok = check(FlatComponentPolicy::resolveVisualState(false, false) == VisualState::Normal,
		"an idle component uses its normal visual state") && ok;
	ok = check(FlatComponentPolicy::resolveVisualState(false, true) == VisualState::Hovered,
		"a hovered component uses its hover visual state") && ok;
	ok = check(FlatComponentPolicy::resolveVisualState(true, false) == VisualState::Pressed,
		"a pressed component keeps its pressed visual even without hover") && ok;
	ok = check(FlatComponentPolicy::resolveVisualState(true, true) == VisualState::Pressed,
		"the pressed visual state has priority over hover") && ok;

	const auto noRange = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		10, 100, 28, 0, 0, 3, 0);
	ok = check(noRange.thumbStart == 10 && noRange.thumbLength == 100 && noRange.travel == 0,
		"a scrollbar without a range fills its entire track") && ok;
	const auto reversedRange = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		10, 100, 28, 7, 0, 3, 4);
	ok = check(reversedRange.thumbStart == 10 && reversedRange.thumbLength == 100
		&& reversedRange.travel == 0,
		"a reversed range is normalized to an empty range") && ok;

	const auto atMinimum = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		10, 100, 28, 0, 7, 3, 0);
	ok = check(atMinimum.thumbStart == 10 && atMinimum.thumbLength == 30
		&& atMinimum.travel == 70,
		"scrollbar geometry uses page size and starts at the track minimum") && ok;
	const auto atMaximum = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		10, 100, 28, 0, 7, 3, 7);
	ok = check(atMaximum.thumbStart == 80 && atMaximum.thumbLength == 30,
		"the maximum position places the thumb at the track end") && ok;

	const auto minimumThumb = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		0, 40, 28, 0, 99, 1, 50);
	ok = check(minimumThumb.thumbLength == 28 && minimumThumb.travel == 12,
		"the minimum thumb length is enforced") && ok;

	ok = check(FlatComponentPolicy::calculateVerticalScrollbarPosition(
		10, 10, 70, 0, 7) == 0,
		"the track start maps back to the minimum position") && ok;
	ok = check(FlatComponentPolicy::calculateVerticalScrollbarPosition(
		80, 10, 70, 0, 7) == 7,
		"the track end maps back to the maximum position") && ok;
	ok = check(FlatComponentPolicy::calculateVerticalScrollbarPosition(
		44, 10, 70, 0, 7) == 3,
		"the inverse position calculation rounds to the nearest row") && ok;
	ok = check(FlatComponentPolicy::calculateVerticalScrollbarPosition(
		1000, 10, 70, 0, 7) == 7,
		"the inverse position calculation clamps pointers beyond the track") && ok;

	return ok ? 0 : 1;
}
