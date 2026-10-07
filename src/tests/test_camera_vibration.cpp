#include "../Game/Data/Camera.h"

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
	ok = check(Camera::clampHorizontalWorldPosition(0.0f, 20, 120, 1366) == 640.0f
		&& Camera::clampHorizontalWorldPosition(2000.0f, 20, 120, 1280) == 640.0f,
		"narrow multi-row maps center their half-tile-inset terrain width") && ok;
	ok = check(Camera::clampHorizontalWorldPosition(-100.0f, 40, 120, 1280) == 672.0f
		&& Camera::clampHorizontalWorldPosition(3000.0f, 40, 120, 1280) == 1888.0f,
		"wide maps leave half a tile inside both horizontal pixel edges") && ok;
	ok = check(Camera::clampHorizontalWorldPosition(900.25f, 40, 120, 1365) == 900.25f
		&& Camera::clampHorizontalWorldPosition(-100.0f, 40, 120, 1365) == 714.5f,
		"odd viewport widths retain subpixel bounds and unconstrained movement") && ok;
	ok = check(Camera::clampHorizontalWorldPosition(3000.0f, 20, 120, 1215) == 640.5f
		&& Camera::clampHorizontalWorldPosition(3000.0f, 20, 1, 1366) == 608.0f,
		"near-fit maps still scroll and single-row maps remain centered by columns") && ok;
	ok = check(Camera::clampVerticalWorldPosition(-100.0f, 120, 720) == 376.0f
		&& Camera::clampVerticalWorldPosition(3000.0f, 120, 720) == 1528.0f,
		"720-pixel viewport leaves half a tile inside the top and bottom edges") && ok;
	ok = check(Camera::clampVerticalWorldPosition(-100.0f, 120, 768) == 400.0f
		&& Camera::clampVerticalWorldPosition(3000.0f, 120, 768) == 1504.0f,
		"768-pixel viewport leaves half a tile inside the top and bottom edges") && ok;
	ok = check(Camera::clampVerticalWorldPosition(900.25f, 120, 721) == 900.25f
		&& Camera::clampVerticalWorldPosition(-100.0f, 120, 721) == 376.5f
		&& Camera::clampVerticalWorldPosition(3000.0f, 120, 721) == 1527.5f,
		"odd viewport heights preserve subpixel bounds and unconstrained movement") && ok;
	ok = check(Camera::clampVerticalWorldPosition(3000.0f, 121, 720) == 1544.0f,
		"odd map row counts use the same half-tile inset") && ok;
	ok = check(Camera::clampVerticalWorldPosition(-100.0f, 50, 751) == 391.5f
		&& Camera::clampVerticalWorldPosition(3000.0f, 50, 751) == 392.5f,
		"maps just larger than the viewport can scroll to their pixel boundary") && ok;
	ok = check(Camera::clampVerticalWorldPosition(-100.0f, 43, 720) == 336.0f
		&& Camera::clampVerticalWorldPosition(3000.0f, 46, 688) == 360.0f
		&& Camera::clampVerticalWorldPosition(3000.0f, 45, 736) == 352.0f
		&& Camera::clampVerticalWorldPosition(-100.0f, 1, 720) == 0.0f,
		"small, exact-fit and single-row maps center their rendered bounds") && ok;
	ok = check(Camera::clampVerticalWorldPosition(336.0f, 90, 720) == 376.0f
		&& Camera::clampVerticalWorldPosition(376.25f, 90, 720) == 376.25f,
		"Jianmenguan cannot expose the negative world coordinates above its background") && ok;

	ok = check(Camera::resolveVibrationDegree(0, 0) == 0, "zero request does not start vibration") && ok;
	ok = check(Camera::resolveVibrationDegree(5, 0) == 5, "zero request does not cancel active vibration") && ok;
	ok = check(Camera::resolveVibrationDegree(5, -3) == 5, "negative request does not cancel active vibration") && ok;
	ok = check(Camera::resolveVibrationDegree(0, 6) == 6, "positive request starts vibration at requested degree") && ok;
	ok = check(Camera::resolveVibrationDegree(3, 6) == 6, "stronger request replaces active vibration degree") && ok;
	ok = check(Camera::resolveVibrationDegree(6, 3) == 3, "weaker request replaces active vibration degree") && ok;

	ok = check(Camera::decayVibrationDegreeAfterFrame(0) == 0, "inactive vibration remains inactive after frame") && ok;
	ok = check(Camera::decayVibrationDegreeAfterFrame(-1) == 0, "negative vibration degree normalizes to inactive after frame") && ok;
	ok = check(Camera::decayVibrationDegreeAfterFrame(6) == 5, "active vibration decays one degree per frame") && ok;
	ok = check(Camera::decayVibrationDegreeAfterFrame(1) == 0, "last vibration frame ends vibration") && ok;
	ok = check(Camera::resolveVibrationAddition(7.0f, 6, 2.0f) == -2.0f
		&& Camera::resolveVibrationAddition(-8.0f, 6, -3.0f) == 3.0f,
		"vibration beyond the current degree is corrected toward the camera origin") && ok;
	ok = check(Camera::resolveVibrationAddition(4.0f, 6, 2.0f) == 2.0f
		&& Camera::resolveVibrationAddition(-5.0f, 6, 3.0f) == 3.0f,
		"vibration within the current degree keeps the proposed random direction") && ok;
	PointEx accumulatedOffset = Camera::resolveVibrationOffsetAfterFrame(
		{ 4.0f, -5.0f },
		6,
		{ 2.0f, 3.0f });
	ok = check(accumulatedOffset.x == 6.0f && accumulatedOffset.y == -2.0f,
		"non-final vibration frames accumulate their corrected displacement") && ok;
	PointEx correctedOffset = Camera::resolveVibrationOffsetAfterFrame(
		{ 7.0f, -8.0f },
		6,
		{ 2.0f, -3.0f });
	ok = check(correctedOffset.x == 5.0f && correctedOffset.y == -5.0f,
		"accumulated vibration returns toward the camera origin as the degree decays") && ok;
	PointEx finalOffset = Camera::resolveVibrationOffsetAfterFrame(
		{ 5.0f, -5.0f },
		1,
		{ 1.0f, -1.0f });
	ok = check(finalOffset.x == 0.0f && finalOffset.y == 0.0f,
		"the final vibration frame restores the exact camera origin") && ok;
	ok = check(Camera::normalizeFlySpeed(-1) == 1
		&& Camera::normalizeFlySpeed(0) == 1
		&& Camera::normalizeFlySpeed(5000) == 1000,
		"camera flight speed remains positive and bounded") && ok;
	const Camera::DirectionalFrameMoveStep cardinalMove =
		Camera::calculateDirectionalFrameMoveStep(
			{ 0.0f, 0.0f }, 0, 2);
	ok = check(cardinalMove.distance.x == 0 && cardinalMove.distance.y == 4
		&& cardinalMove.remainder.x == 0.0f
		&& cardinalMove.remainder.y == 0.0f,
		"frame-count camera movement applies speed times two pixels per update") && ok;
	const Camera::DirectionalFrameMoveStep diagonalMove =
		Camera::calculateDirectionalFrameMoveStep(
			{ 0.0f, 0.0f }, 1, 1);
	const Camera::DirectionalFrameMoveStep diagonalMoveNext =
		Camera::calculateDirectionalFrameMoveStep(
			diagonalMove.remainder, 1, 1);
	ok = check(diagonalMove.distance.x == -1 && diagonalMove.distance.y == 1
		&& diagonalMoveNext.distance.x == -1
		&& diagonalMoveNext.distance.y == 1
		&& diagonalMove.remainder.x < 0.0f
		&& diagonalMove.remainder.y > 0.0f,
		"diagonal frame-count movement retains subpixel remainder between updates") && ok;
	const Camera::DirectionalFrameMoveStep invalidDirectionMove =
		Camera::calculateDirectionalFrameMoveStep(
			{ 0.0f, 0.0f }, 99, 1);
	ok = check(invalidDirectionMove.distance.x == 0
		&& invalidDirectionMove.distance.y == 2,
		"JxqyHD frame-count movement maps an invalid direction to direction zero") && ok;
	const Camera::DirectionalFrameMoveStep negativeDirectionAndSpeedMove =
		Camera::calculateDirectionalFrameMoveStep(
			{ 0.0f, 0.0f }, -1, -1);
	ok = check(negativeDirectionAndSpeedMove.distance.x == 0
		&& negativeDirectionAndSpeedMove.distance.y == -2,
		"JxqyHD frame-count movement preserves signed speed after normalizing direction") && ok;
	ok = check(Camera::isZeroFlightDistance({ 0.0f, 0.0f })
		&& !Camera::isZeroFlightDistance({ 1.0f, 0.0f }),
		"camera detects zero-distance flights before normalization") && ok;

	return ok ? 0 : 1;
}
