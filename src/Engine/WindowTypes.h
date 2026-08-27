#pragma once

#include <string>
#include <vector>

enum class FullScreenMode
{
	window = 0,
	windowFullScreen = 1,
	fullScreen = 2
};

enum class FullScreenSolutionMode
{
	original = 0,
	adjust = 1,
	forceToUseSetting = 2
};

struct DesktopDisplayResolution
{
	int width = 0;
	int height = 0;

	bool operator==(const DesktopDisplayResolution& other) const
	{
		return width == other.width && height == other.height;
	}
};

struct DesktopDisplayInfo
{
	int index = 0;
	std::string name;
	int desktopWidth = 0;
	int desktopHeight = 0;
	int usableWidth = 0;
	int usableHeight = 0;
	std::vector<DesktopDisplayResolution> fullscreenResolutions;
};

struct DesktopDisplaySettings
{
	int displayIndex = 0;
	int width = 1280;
	int height = 720;
	FullScreenMode fullScreenMode = FullScreenMode::window;
	FullScreenSolutionMode fullScreenSolutionMode =
		FullScreenSolutionMode::original;
};
