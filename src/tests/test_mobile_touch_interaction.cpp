#include "../Game/Data/MobileTouchInteraction.h"
#include "../Game/Data/PlayerMovementIntent.h"

#include <iostream>
#include <vector>

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

bool checkDirectionList(
	const std::vector<int>& actual,
	const std::vector<int>& expected,
	const char* message)
{
	if (actual == expected)
	{
		return true;
	}

	std::cerr << "FAILED: " << message << " actual:";
	for (int direction : actual)
	{
		std::cerr << ' ' << direction;
	}
	std::cerr << " expected:";
	for (int direction : expected)
	{
		std::cerr << ' ' << direction;
	}
	std::cerr << '\n';
	return false;
}
}

int main()
{
	bool ok = true;

	ok = check(shouldDeferMobileRightScriptChoice("primary.lua", "right.lua"),
		"both primary and right scripts defer touch choice until finger-up") && ok;
	ok = check(!shouldDeferMobileRightScriptChoice("", "right.lua"),
		"right-only script keeps immediate primary fallback path") && ok;
	ok = check(!shouldDeferMobileRightScriptChoice("primary.lua", ""),
		"primary-only script does not defer") && ok;

	ok = check(!shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS - 1, 0, 0),
		"short touch keeps primary script") && ok;
	ok = check(shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS, 0, 0),
		"long press without movement uses right script") && ok;
	ok = check(shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS, MOBILE_RIGHT_SCRIPT_MOVE_TOLERANCE_PIXELS, 0),
		"movement exactly at tolerance still uses right script") && ok;
	ok = check(!shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS, MOBILE_RIGHT_SCRIPT_MOVE_TOLERANCE_PIXELS + 1, 0),
		"movement beyond tolerance keeps primary script") && ok;
	ok = check(!shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS, 50000, 50000),
		"large movement does not overflow into right-script selection") && ok;

	ok = check(!isMobileJoystickDirectionActive(0, 4, 100),
		"mobile joystick ignores movement inside the dead zone") && ok;
	ok = check(isMobileJoystickDirectionActive(0, 5, 100),
		"mobile joystick direction activates at the current dead-zone edge") && ok;
	ok = check(!isMobileJoystickWalking(0, 5, 100),
		"mobile joystick exact dead-zone edge is not yet walking") && ok;
	ok = check(isMobileJoystickWalking(0, 6, 100),
		"mobile joystick movement past the dead-zone edge is walking") && ok;
	ok = check(isMobileJoystickWalking(0, 15, 100),
		"mobile joystick run threshold edge remains walking") && ok;
	ok = check(!isMobileJoystickRunning(0, 15, 100),
		"mobile joystick run threshold requires movement beyond the edge") && ok;
	ok = check(isMobileJoystickRunning(0, 16, 100),
		"mobile joystick movement beyond the run threshold is running") && ok;
	ok = check(!isMobileJoystickDirectionActive(0, 20, 0),
		"mobile joystick ignores invalid range") && ok;

	ok = checkDirectionList(getMobileJoystickDirectionCandidates(0, 15, 100), { 0, 1, 7 },
		"mobile joystick keeps current down direction candidate order") && ok;
	ok = checkDirectionList(getMobileJoystickDirectionCandidates(-20, 0, 100), { 2, 3, 1 },
		"mobile joystick keeps current left direction candidate order") && ok;
	ok = checkDirectionList(getMobileJoystickDirectionCandidates(0, -20, 100), { 4, 5, 3 },
		"mobile joystick keeps current up direction candidate order") && ok;
	ok = checkDirectionList(getMobileJoystickDirectionCandidates(20, 0, 100), { 6, 7, 5 },
		"mobile joystick keeps current right direction candidate order") && ok;
	ok = checkDirectionList(getMobileJoystickDirectionCandidates(4, 0, 100), {},
		"mobile joystick direction list is empty inside the dead zone") && ok;

	ok = check(shouldUseRunForPlayerMoveIntent(false, 1, true, true),
		"WalkIsRun promotes a walk intent when running is available") && ok;
	ok = check(!shouldUseRunForPlayerMoveIntent(false, 0, true, true),
		"disabled WalkIsRun keeps the original walk intent") && ok;
	ok = check(shouldUseRunForPlayerMoveIntent(true, 0, true, true),
		"an explicit run intent remains running when available") && ok;
	ok = check(!shouldUseRunForPlayerMoveIntent(false, 1, false, true),
		"DisableRun keeps WalkIsRun movement as a walk intent") && ok;
	ok = check(!shouldUseRunForPlayerMoveIntent(true, 1, false, true),
		"DisableRun downgrades an explicit run intent to walking") && ok;
	ok = check(!shouldUseRunForPlayerMoveIntent(false, 1, true, false),
		"insufficient thew keeps WalkIsRun movement as walking") && ok;
	ok = check(!shouldUseRunForPlayerMoveIntent(true, 0, true, false),
		"insufficient thew downgrades an explicit run intent to walking") && ok;

	return ok ? 0 : 1;
}
