#pragma once

#include <SDL3/SDL.h>

#include <utility>

namespace DesktopCursorDpiPolicy
{
constexpr const char* HintName = "SDL_MOUSE_DPI_SCALE_CURSORS";

constexpr bool versionAtLeast(
	int major, int minor, int patch,
	int requiredMajor, int requiredMinor, int requiredPatch)
{
	if (major != requiredMajor)
	{
		return major > requiredMajor;
	}
	if (minor != requiredMinor)
	{
		return minor > requiredMinor;
	}
	return patch >= requiredPatch;
}

template <
	bool MobileBuild,
	int SdlMajor,
	int SdlMinor,
	int SdlPatch,
	typename HintSetter>
bool configureForBuild(HintSetter&& hintSetter)
{
	if constexpr (MobileBuild ||
		!versionAtLeast(SdlMajor, SdlMinor, SdlPatch, 3, 4, 0))
	{
		return true;
	}
	else
	{
		return std::forward<HintSetter>(hintSetter)(HintName, "1");
	}
}

inline bool configure()
{
#ifdef __MOBILE__
	constexpr bool MobileBuild = true;
#else
	constexpr bool MobileBuild = false;
#endif
	return configureForBuild<
		MobileBuild,
		SDL_MAJOR_VERSION,
		SDL_MINOR_VERSION,
		SDL_MICRO_VERSION>(
			[](const char* name, const char* value)
			{
				return SDL_SetHint(name, value);
			});
}
}
