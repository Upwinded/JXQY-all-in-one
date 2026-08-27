#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace JxqyBuildVersion
{
inline constexpr char EngineVersion[] =
#include "../cmake/JxqyEngineVersion.inc"
;
// Display-only release stage. Keep empty for a stable release; changing this
// label does not change update ordering.
inline constexpr char ReleaseStage[] = "Preview";

#if defined(__ANDROID__)
inline constexpr char ProgramUpdateTarget[] = "android";
#elif defined(_WIN32)
inline constexpr char ProgramUpdateTarget[] = "windows";
#elif defined(__linux__)
inline constexpr char ProgramUpdateTarget[] = "linux";
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
inline constexpr char ProgramUpdateTarget[] = "ios";
#else
inline constexpr char ProgramUpdateTarget[] = "macos";
#endif
#else
inline constexpr char ProgramUpdateTarget[] = "";
#endif
}
