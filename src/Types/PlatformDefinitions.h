#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if (defined(__APPLE__) && TARGET_OS_IOS) || defined(__ANDROID__)
#ifndef __MOBILE__
#define __MOBILE__
#endif
#endif
