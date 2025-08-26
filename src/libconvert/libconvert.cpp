
//#ifndef _SCL_SECURE_NO_WARNINGS
//#define _SCL_SECURE_NO_WARNINGS
//#endif 
//
//#ifndef _CRT_SECURE_NO_WARNINGS
//#define _CRT_SECURE_NO_WARNINGS
//#endif 


#ifndef MAX_FNAME_LEN
#define MAX_FNAME_LEN 256
#endif

#ifndef MAX_EXT_LEN
#define MAX_EXT_LEN 256
#endif

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 1024
#endif

#include "libconvert.h"

#ifdef _MSC_VER
#define  vsprintf  vsprintf_s
//#define fopen fopen_s
#endif

void convert::split_whole_name(const std::string& whole_name, std::string& fname, std::string& ext)
{
    if (whole_name.empty())
    {
        fname = u8"";
        ext = u8"";
        return;
    }
    auto pos = whole_name.find_last_of('.');
    if (pos == whole_name.npos)
    {
        fname = whole_name;
        ext = u8"";
        return;
    }
    fname = whole_name.substr(0, pos);
    ext = whole_name.substr(pos, whole_name.size() - pos);
}

void convert::split_path(const std::string& path, std::string& dir, std::string& fname, std::string& ext)
{
    if (path.empty())
    {
        dir = u8"";
        fname = u8"";
        ext = u8"";
        return;
    }

    if (*path.crbegin() == '/')
    {
        dir = path;
        fname = u8"";
        ext = u8"";
        return;
    }

    auto p_whole_name = path.find_last_of('/');
    std::string whole_name;
    if (p_whole_name == path.npos)
    {
        dir = u8"";
        whole_name = path;
    }
    else
    {
        dir = path.substr(0, p_whole_name + 1);
        whole_name = path.substr(p_whole_name + 1, path.size() - p_whole_name - 1);
    }
    split_whole_name(whole_name, fname, ext);
}


int convert::replaceString(std::string& s, const std::string& oldstring, const std::string& newstring, int pos0/*=0*/)
{
    int pos = s.find(oldstring, pos0);
    if (pos != s.npos)
    {
        s.erase(pos, oldstring.length());
        s.insert(pos, newstring);
    }
    return pos + newstring.length();
}

int convert::replaceAllString(std::string& s, const std::string& oldstring, const std::string& newstring)
{
    int pos = s.find(oldstring);
    while (pos != s.npos)
    {
        s.erase(pos, oldstring.length());
        s.insert(pos, newstring);
        pos = s.find(oldstring, pos + newstring.length());
    }
    return pos + newstring.length();
}


std::string convert::formatString(const char* format, ...)
{
    char s[1000];
    va_list arg_ptr;
    va_start(arg_ptr, format);
     vsprintf(s, format, arg_ptr);
    va_end(arg_ptr);
    return s;
}

void convert::formatAppendString(std::string& str, const char* format, ...)
{
    char s[1000];
    va_list arg_ptr;
    va_start(arg_ptr, format);
     vsprintf(s, format, arg_ptr);
    va_end(arg_ptr);
    str += s;
}

std::vector<std::string> convert::splitString(std::string str, std::string pattern)
{
    std::string::size_type pos;
    std::vector<std::string> result;
    str += pattern;
    int size = str.size();
    for (int i = 0; i < size; i++)
    {
        pos = str.find(pattern, i);
        if ((int)pos < size)
        {
            std::string s = str.substr(i, pos - i);
            result.push_back(s);
            i = pos + pattern.size() - 1;
        }
    }
    return result;
}

std::vector<std::string> convert::splitString(const std::string & str, int length)
{
	int len = 0, slen = 0;

	std::vector<std::string> result;
	size_t i = 0;
	while (i < str.length())
	{	
		int ilen = 1;
		unsigned char c = *(str.c_str() + i);
        if ((c & 0xFC) == 0xFC)
        {
            ilen = 6;
        }
        else if ((c & 0xF8) == 0xF8)
        {
            ilen = 5;
        }
		else if ((c & 0xF0) == 0xF0)
		{
			ilen = 4;
		}
		else if ((c & 0xE0) == 0xE0)
		{
			ilen = 3;
		}
		else if ((c & 0xC0) == 0xC0)
		{
			ilen = 2;
		}		
		else if ((c & 0x80) == 0x80)
		{
			ilen = 1;
		}
		len += 1;
		slen += ilen;
		i += convert_max(ilen, 1);
		if (len >= length)
		{		
			result.push_back(str.substr(i - slen, slen));
			len = 0;
			slen = 0;
		}
	}
	if (slen > 0)
	{
		result.push_back(str.substr(str.length() - slen, slen));
	}
	return result;
}

std::vector<std::wstring> convert::splitWString(const std::wstring & wstr, const std::wstring & pattern)
{
	std::wstring::size_type pos;
	std::vector<std::wstring> result;
	std::wstring twstr = wstr + pattern;
	int size = twstr.size();
	for (int i = 0; i < size; i++)
	{
		pos = twstr.find(pattern, i);
		if ((int)pos < size)
		{
			std::wstring s = twstr.substr(i, pos - i);
			result.push_back(s);
			i = pos + pattern.size() - 1;
		}
	}
	return result;
}

std::vector<std::wstring> convert::splitWString(const std::wstring & wstr, int length)
{	
	std::vector<std::wstring> tws;
	tws.resize(0);
	if (wstr == L"")
	{
		return tws;
	}
	if (length == 0)
	{
		tws.resize(1);
		tws[0] = wstr;
	}
	int line = wstr.length() / length;
	int size = wstr.length();
	int pos = 0;
	if (wstr.length() % length != 0)
	{
		line++;
	}
	tws.resize(line);
	for (int i = 0; i < line; i++)
	{
		if (i == line - 1)
		{
			tws[i] = wstr.substr(i * length, wstr.length() - i * length);
		}
		else
		{
			tws[i] = wstr.substr(i * length, length);
		}		
	}
	return tws;
}

bool convert::isProChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'z') || (c >= '(' && c <= ')');
}

std::string convert::extractFilePath(const std::string& fileName)
{
    std::string tempName = fileName;
    std::string fName, dir, ext;
    replaceAllString(tempName, u8"\\", u8"/");
    split_path(tempName, dir, fName, ext);
	return dir;
}

std::string convert::extractFileName(const std::string& fileName)
{
	//char c[MAX_FNAME_LEN];
	//memset(c, 0, MAX_FNAME_LEN);
	//std::string fName = fileName;
	//replaceAllString(fName, u8"\\", u8"/");
	//split_path(fileName.c_str(), nullptr, nullptr, c, nullptr);
	//std::string name = c;
	//return name;

    std::string tempName = fileName;
    std::string fName, dir, ext;
    replaceAllString(tempName, u8"\\", u8"/");
    split_path(tempName, dir, fName, ext);
    return fName;
}

std::string convert::extractFileExt(const std::string & fileName)
{
    std::string tempName = fileName;
    std::string fName, dir, ext;
    replaceAllString(tempName, u8"\\", u8"/");
    split_path(tempName, dir, fName, ext);
    return ext;
}

std::string convert::extractFullName(const std::string & fileName)
{
    std::string tempName = fileName;
    std::string fName, dir, ext;
    replaceAllString(tempName, u8"\\", u8"/");
    split_path(tempName, dir, fName, ext);
    return fName + ext;
}

std::vector<std::string> convert::extractFileAll(const std::string & fileName)
{
    std::string tempName = fileName;
    std::string fName, dir, ext;
    replaceAllString(tempName, u8"\\", u8"/");
    split_path(tempName, dir, fName, ext);
	std::vector<std::string> s;
    s.push_back(dir);
    s.push_back(fName);
    s.push_back(ext);
	return s;
}

std::string convert::lowerCase(const std::string & str)
{
	std::string s = str;
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s;
}

std::string convert::upperCase(const std::string & str)
{
	std::string s = str;
	std::transform(s.begin(), s.end(), s.begin(), ::toupper);
	return s;
}

int convert::GetUtf8LetterCount(const char* s)
{
    int i = 0, j = 0;
    if (s == nullptr)
    {
        return 0;
    }
    while (s[i])
    {
        //if ((s[i] & 0b11000000) != 0b10000000) j++;
        if ((s[i] & 0xc0) != 0x80) j++;
        i++;
    }
    return j;
}

int convert::GetUtf8CharLength(const char c)
{
	if ((c & 0b10000000) == 0)
	{
		return 1;
	}
	else if ((c & 0b11111100) == 0b11111100)
	{
		return 6;
	}
	else if ((c & 0b11111000) == 0b11111000)
	{
		return 5;
	}
	else if ((c & 0b11110000) == 0b11110000)
	{
		return 4;
	}
	else if ((c & 0b11100000) == 0b11100000)
	{
		return 3;
	}
	else if ((c & 0b11000000) == 0b11000000)
	{
		return 2;
	}
	return 0;
}

