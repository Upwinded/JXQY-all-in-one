#include "../Engine/LogicalResolutionPolicy.h"

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
	int width = 320;
	int height = 240;
	LogicalResolutionPolicy::constrain(width, height);
	ok = check(width == 640 && height == 480,
		"dimensions below the minimum are clamped") && ok;

	width = 640;
	height = 480;
	LogicalResolutionPolicy::constrain(width, height);
	ok = check(width == 640 && height == 480,
		"the minimum resolution remains unchanged") && ok;

	width = 1920;
	height = 1080;
	LogicalResolutionPolicy::constrain(width, height);
	ok = check(width == 1920 && height == 1080,
		"resolutions above the minimum remain unchanged") && ok;

	width = 800;
	height = 360;
	LogicalResolutionPolicy::constrain(width, height);
	ok = check(width == 800 && height == 480,
		"each logical dimension is constrained independently") && ok;

	return ok ? 0 : 1;
}
