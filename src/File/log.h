#pragma once
#include "File.h"
#include "../libconvert/libconvert.h"

#ifndef USE_LOG_FILE_PARAM
#define USE_LOG_FILE_PARAM
#endif


namespace GameLog
{
	static bool use_log_file = false;
	void write(const char* format, ...);
};

