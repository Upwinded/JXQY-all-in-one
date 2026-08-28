#include "Engine/DesktopCursorDpiPolicy.h"

#include <cstring>
#include <iostream>
#include <string>

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}

struct HintSetterProbe
{
	bool result = true;
	int callCount = 0;
	std::string name;
	std::string value;

	bool operator()(const char* hintName, const char* hintValue)
	{
		callCount++;
		name = hintName;
		value = hintValue;
		return result;
	}
};
}

int main()
{
	bool ok = true;

	HintSetterProbe desktopProbe;
	ok = check(
		DesktopCursorDpiPolicy::configureForBuild<false, 3, 4, 0>(
			desktopProbe),
		"SDL 3.4 desktop policy rejected a successful hint setter") && ok;
	ok = check(
		desktopProbe.callCount == 1
			&& desktopProbe.name == DesktopCursorDpiPolicy::HintName
			&& desktopProbe.value == "1",
		"SDL 3.4 desktop policy did not enable display-scale cursor matching")
		&& ok;

	HintSetterProbe failedDesktopProbe;
	failedDesktopProbe.result = false;
	ok = check(
		!DesktopCursorDpiPolicy::configureForBuild<false, 3, 4, 0>(
			failedDesktopProbe),
		"desktop policy did not report a rejected SDL hint") && ok;

	HintSetterProbe mobileProbe;
	ok = check(
		DesktopCursorDpiPolicy::configureForBuild<true, 3, 4, 0>(mobileProbe)
			&& mobileProbe.callCount == 0,
		"mobile policy attempted to change desktop cursor scaling") && ok;

	HintSetterProbe olderSdlProbe;
	ok = check(
		DesktopCursorDpiPolicy::configureForBuild<false, 3, 3, 99>(
			olderSdlProbe)
			&& olderSdlProbe.callCount == 0,
		"pre-3.4 SDL policy attempted to use an unavailable cursor hint") && ok;

#if SDL_VERSION_ATLEAST(3, 4, 0) && !defined(__MOBILE__)
	SDL_ResetHint(DesktopCursorDpiPolicy::HintName);
	ok = check(
		DesktopCursorDpiPolicy::configure(),
		"bound SDL rejected desktop cursor DPI scaling") && ok;
	const char* configuredValue =
		SDL_GetHint(DesktopCursorDpiPolicy::HintName);
	ok = check(
		configuredValue != nullptr
			&& std::strcmp(configuredValue, "1") == 0,
		"bound SDL did not retain the desktop cursor DPI scaling hint") && ok;
	SDL_ResetHint(DesktopCursorDpiPolicy::HintName);
#elif defined(__MOBILE__)
	SDL_SetHint(DesktopCursorDpiPolicy::HintName, "0");
	ok = check(
		DesktopCursorDpiPolicy::configure(),
		"mobile cursor DPI policy unexpectedly failed") && ok;
	const char* configuredValue =
		SDL_GetHint(DesktopCursorDpiPolicy::HintName);
	ok = check(
		configuredValue != nullptr
			&& std::strcmp(configuredValue, "0") == 0,
		"mobile cursor DPI policy changed the desktop-only SDL hint") && ok;
	SDL_ResetHint(DesktopCursorDpiPolicy::HintName);
#endif

	if (!ok)
	{
		return 1;
	}
	std::cout << "desktop cursor DPI policy tests passed\n";
	return 0;
}
