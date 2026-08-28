#include "../Element/ElementPointerClickPolicy.h"

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

	const int left = 10;
	const int top = 20;
	const int width = 30;
	const int height = 40;

	ok = check(ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left, top, left, top, width, height),
		"left and top edges are inside") && ok;
	ok = check(ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left + width - 1, top + height - 1, left, top, width, height),
		"last pixels before right and bottom edges are inside") && ok;
	ok = check(!ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left + width, top, left, top, width, height),
		"right edge is outside") && ok;
	ok = check(!ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left, top + height, left, top, width, height),
		"bottom edge is outside") && ok;
	ok = check(!ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left - 1, top, left, top, width, height),
		"point before left edge is outside") && ok;
	ok = check(!ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left, top - 1, left, top, width, height),
		"point before top edge is outside") && ok;
	ok = check(!ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left, top, left, top, 0, height),
		"zero-width bounds are not clickable") && ok;
	ok = check(!ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left, top, left, top, width, -1),
		"negative-height bounds are not clickable") && ok;

	const bool pressedInside = ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left + 1, top + 1, left, top, width, height);
	const bool releasedInside = ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left + 2, top + 2, left, top, width, height);
	const bool releasedOutside = ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		left + width, top + 2, left, top, width, height);

	ok = check(ElementPointerClickPolicy::shouldTriggerClick(
		pressedInside, releasedInside, true, false),
		"inside press followed by inside release clicks") && ok;
	ok = check(!ElementPointerClickPolicy::shouldTriggerClick(
		pressedInside, releasedOutside, true, false),
		"inside press followed by outside release does not click") && ok;
	ok = check(!ElementPointerClickPolicy::shouldTriggerClick(
		false, releasedInside, true, false),
		"release from another pointer does not click") && ok;
	ok = check(!ElementPointerClickPolicy::shouldTriggerClick(
		pressedInside, releasedInside, false, false),
		"release after the maximum click time does not click") && ok;
	ok = check(!ElementPointerClickPolicy::shouldTriggerClick(
		pressedInside, releasedInside, true, true),
		"release after crossing the drag threshold does not also click") && ok;
	ok = check(ElementPointerClickPolicy::shouldAcquirePointerOnDown(
		-2, 41, -2, true),
		"touch down inside acquires a component without an active press") && ok;
	ok = check(!ElementPointerClickPolicy::shouldAcquirePointerOnDown(
		17, 41, -2, true),
		"a second pointer cannot steal a component with an active press") && ok;
	ok = check(ElementPointerClickPolicy::shouldAcquirePointerOnDown(
		41, 41, -2, true),
		"the active pointer can continue its own press transaction") && ok;
	ok = check(!ElementPointerClickPolicy::shouldAcquirePointerOnDown(
		-2, 41, -2, false),
		"touch down outside does not acquire the component") && ok;
	ok = check(ElementPointerClickPolicy::shouldPreservePressedPointerOnMotion(
		41, -1, -2),
		"mouse motion cannot steal a component pressed by a finger") && ok;
	ok = check(ElementPointerClickPolicy::shouldPreservePressedPointerOnMotion(
		-1, 41, -2),
		"finger motion cannot steal a component pressed by the mouse") && ok;
	ok = check(!ElementPointerClickPolicy::shouldPreservePressedPointerOnMotion(
		41, 41, -2),
		"the active pointer can continue moving its pressed component") && ok;
	ok = check(!ElementPointerClickPolicy::shouldPreservePressedPointerOnMotion(
		-2, -1, -2),
		"motion remains available when no pointer has pressed the component") && ok;

	std::int64_t touchingPointer = 41;
	std::int64_t pressedPointer = 41;
	ElementPointerClickPolicy::cancelPointerState(touchingPointer, pressedPointer, -2);
	ok = check(touchingPointer == -2 && pressedPointer == -2,
		"layout reindex cancellation clears hover and press pointer state") && ok;
	ok = check(!ElementPointerClickPolicy::shouldTriggerClick(
		pressedPointer == 41, releasedInside, true, false),
		"a release after layout reindex cancellation cannot click a reused control") && ok;
	unsigned int pendingResult = 0x20;
	ElementPointerClickPolicy::cancelPendingResult(pendingResult, 0u);
	ok = check(pendingResult == 0,
		"layout reindex cancellation drains a pending click before controls are reused") && ok;

	return ok ? 0 : 1;
}
