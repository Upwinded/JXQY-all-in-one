#include "log.h"
#include <chrono>
#include "../Engine/Engine.h"

#ifdef _MSC_VER
#define vsprintf  vsprintf_s
//#define fopen fopen_s
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

#define logFileName "log.txt"

void GameLog::write(const char* format, ...)
{
#if !defined(USE_LOG_FILE) && (!defined(USE_LOG_FILE_PARAM))
#if (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
    return;
#endif // (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
#endif // !defined(USE_LOG_FILE) && !defined(USE_LOG_FILE_PARAM)

#if !defined(USE_LOG_FILE) && defined(USE_LOG_FILE_PARAM)
#if (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
    if (!use_log_file)
    {
        //return;
    }
#endif // (defined(_WIN32) && !defined(_DEBUG)) || defined(__ANDROID__) || defined(__APPLE__)
#endif // !defined(USE_LOG_FILE) && !defined(USE_LOG_FILE_PARAM)

    char s[1000];
    va_list arg_ptr;
    va_start(arg_ptr, format);
    vsprintf(s, format, arg_ptr);
    va_end(arg_ptr);
    std::string info = s;
    info = "[" + std::to_string(((float)SDL_GetTicks()) / 1000.0) + "s]:" + info;
    if (*info.rbegin() != '\n')
    {
        info += "\n";
    }
    SDL_Log("%s", info.c_str());

//#ifdef __ANDROID__
//    __android_log_print(ANDROID_LOG_INFO, "native-log",  "%s", info.c_str());
//#endif // __ANDROID__

#if defined(USE_LOG_FILE_PARAM) && !defined(USE_LOG_FILE)
    if (!use_log_file)
    {
        return;
    }
#elif !defined(USE_LOG_FILE)
    return;
#endif
    File::appendFile(logFileName, info.c_str(), info.length());
}
