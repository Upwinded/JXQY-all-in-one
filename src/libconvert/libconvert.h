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


//#define _USE_LIBICONV

#ifdef _USE_LIBICONV
extern "C"
{
#include <iconv.h>
}
#endif // _USE_LIBICONV

#ifndef convert_max
#define convert_max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef convert_min
#define convert_min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

namespace convert
{
#ifdef _MSC_VER
#define GBKName ".936"
#else
#define GBKName "zh_CN.GBK"


    class codecvt_mine:public std::codecvt_byname<wchar_t, char, mbstate_t>{
    public:
        codecvt_mine():codecvt_byname<wchar_t, char, mbstate_t>(GBKName) {}
        ~codecvt_mine() {}
    };

#endif // _MSC_VER
#ifndef _WIN32


const char* rindex(const char *s, char c);
void _split_whole_name(const char *whole_name, char *fname, char *ext);
void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext);
#endif

//string functions
std::string readStringFromFile(const std::string& filename);
void writeStringToFile(const std::string& str, const std::string& filename);
int replaceString(std::string& s, const std::string& oldstring, const std::string& newstring, int pos0 = 0);
int replaceAllString(std::string& s, const std::string& oldstring, const std::string& newstring);
void replaceStringInFile(const std::string& oldfilename, const std::string& newfilename, const std::string& oldstring, const std::string& newstring);
void replaceAllStringInFile(const std::string& oldfilename, const std::string& newfilename, const std::string& oldstring, const std::string& newstring);
std::string formatString(const char* format, ...);
void formatAppendString(std::string& str, const char* format, ...);
std::string findANumber(const std::string& s);
unsigned findTheLast(const std::string& s, const std::string& content);
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

int GetUtf8LetterNumber(const char* s);
int GetUtf8CharLength(const char c);

//std::string unicodeToUTF8(const std::wstring& unicodeStr);
//std::wstring UTF8ToUnicode(const std::string& UTF8Str);
//std::string unicodeToGBK(const std::wstring& unicodeStr);
//std::wstring GBKToUnicode(const std::string& str);
std::string GBKToUTF8_InWinOnly(const std::string& str);
std::string UTF8ToGBK_InWinOnly(const std::string& str);

#ifdef _USE_LIBICONV
static std::string conv(const std::string& src, const char* from, const char* to);
static std::string conv(const std::string& src, const std::string& from, const std::string& to);
#endif // _USE_LIBICONV

//void deleteAll(const std::string & path);
//void copyTo(const std::string & spath, const std::string & dpath);
//bool copyFile(const std::string & oldFile, const std::string & newFile);
//
//std::vector<std::string> getAllFile(const std::string & path, const std::string & fileExt = ".*");

template<typename T>
int findNumbers(const std::string& s, std::vector<T>* data)
{
    int n = 0;
    std::string str = "";
    bool haveNum = false;
    for (int i = 0; i < s.length(); i++)
    {
        char c = s[i];
        bool findNumChar = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'E' || c == 'e';
        if (findNumChar)
        {
            str += c;
            if (c >= '0' && c <= '9')
            { haveNum = true; }
        }
        if (!findNumChar || i == s.length() - 1)
        {
            if (str != "" && haveNum)
            {
                auto f = T(atof(str.c_str()));
                data->push_back(f);
                n++;
            }
            str = "";
            haveNum = false;
        }
    }
    return n;
}
};
