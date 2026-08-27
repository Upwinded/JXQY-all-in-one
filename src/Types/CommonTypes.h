#pragma once

#include "PlatformDefinitions.h"

#include <cstdint>

using UTime = std::uint64_t;

#if __cplusplus >= 202002L
#define U8(x) reinterpret_cast<const char*>(u8##x)
#else
#define U8(x) u8##x
#endif

//typedef void* _rect;
//typedef void* _cursor;
//typedef void* _music;
//typedef void* _image;
//typedef void* _channel;
//typedef void* _video;
