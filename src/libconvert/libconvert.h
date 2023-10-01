#pragma once
#include <string>
#include <vector>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <memory.h>
#include <string.h>

#ifndef convert_max
#define convert_max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef convert_min
#define convert_min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

namespace convert
{
    void split_whole_name(const std::string& whole_name, std::string& fname, std::string& ext);
    void split_path(const std::string& path, std::string& dir, std::string& fname, std::string& ext);

    //string functions

    int replaceString(std::string& s, const std::string& oldstring, const std::string& newstring, int pos0 = 0);
    int replaceAllString(std::string& s, const std::string& oldstring, const std::string& newstring);
    std::string formatString(const char* format, ...);
    void formatAppendString(std::string& str, const char* format, ...);

    std::vector<std::string> splitString(std::string str, std::string pattern);
    std::vector<std::string> splitString(const std::string & str, int length);
    std::vector<std::wstring> splitWString(const std::wstring & wstr, const std::wstring & pattern);
    std::vector<std::wstring> splitWString(const std::wstring & wstr, int length);
    bool isProChar(char c);
    std::string extractFilePath(const std::string& fileName);
    std::string extractFileName(const std::string& fileName);
    std::string extractFileExt(const std::string& fileName);
    std::string extractFullName(const std::string& fileName);
    std::vector<std::string> extractFileAll(const std::string& fileName);

    std::string lowerCase(const std::string & str);
    std::string upperCase(const std::string & str);

    int GetUtf8LetterCount(const char* s);
    int GetUtf8CharLength(const char c);

};
