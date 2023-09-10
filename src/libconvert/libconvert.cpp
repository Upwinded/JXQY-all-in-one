
#ifndef _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif 

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif 

#ifndef _WIN32

#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif

#ifndef _MAX_EXT
#define _MAX_EXT 256
#endif

#ifndef _MAX_PATH
#define _MAX_PATH 1024
#endif

#endif // !_WIN32


#include "libconvert.h"

#ifdef _WIN32
#include "io.h"
#include "windows.h"
#else
#include "dirent.h"
#ifdef _findfirst
#undef _findfirst
#endif // _findfirst
#define _findfirst findfirst
#ifdef _findnext
#undef _findnext
#endif // _findnext
#define _findnext findnext
#endif // _WIN32


#ifdef _MSC_VER
#define vsprintf vsprintf_s
//#define fopen fopen_s
#endif

#ifndef _WIN32
const char* convert::rindex(const char *s, char c)
{
	if (s != nullptr)
	{
		for (auto i = strlen(s); i > 0; --i) {
			if (*(s + i - 1) == c)
			{
				return (s + i - 1);
			}
		}
	}
	return nullptr;
}

void convert::_split_whole_name(const char *whole_name, char *fname, char *ext)
{
    const char* p_ext = rindex(whole_name, '.');
    if (nullptr != p_ext)
    {
        if (ext) { strcpy(ext, p_ext); }
        if (fname) { snprintf(fname, p_ext - whole_name + 1, "%s", whole_name); }
    }
    else
    {
        if (ext) { ext[0] = '\0'; }
        if (fname) { strcpy(fname, whole_name); }
    }
}

void convert::_splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
    if (drive) { drive[0] = '\0'; }
    if (nullptr == path)
    {
        if (dir) { dir[0] = '\0'; }
        if (fname) { fname[0] = '\0'; }
        if (ext) { ext[0] = '\0'; }
        return;
    }

    if ('\\' == path[strlen(path)])
    {
        if (dir) { strcpy(dir, path); }
        if (fname) { fname[0] = '\0'; }
        if (ext) { ext[0] = '\0'; }
        return;
    }

    auto p_whole_name = rindex(path, '\\');
    if (nullptr != p_whole_name)
    {
        p_whole_name++;
        _split_whole_name(p_whole_name, fname, ext);

       if (dir) {snprintf(dir, p_whole_name - path, "%s", path); }
    }
    else
    {
        _split_whole_name(path, fname, ext);
        if (dir) { dir[0] = '\0'; }
    }
}


#endif


std::string convert::readStringFromFile(const std::string& filename)
{
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp)
    {
        fprintf(stderr, "Can not open file %s\n", filename.c_str());
        return "";
    }
    fseek(fp, 0, SEEK_END);
    int length = ftell(fp);
    fseek(fp, 0, 0);
    std::string str;
    str.resize(length, '\0');
    fread((void*)str.c_str(), length, 1, fp);
    fclose(fp);
    return str;
}

void convert::writeStringToFile(const std::string& str, const std::string& filename)
{
    FILE* fp = fopen(filename.c_str(), "wb");
    int length = str.length();
    fwrite(str.c_str(), length, 1, fp);
    fclose(fp);
}

int convert::replaceString(std::string& s, const std::string& oldstring, const std::string& newstring, int pos0/*=0*/)
{
    int pos = s.find(oldstring, pos0);
    if (pos >= 0)
    {
        s.erase(pos, oldstring.length());
        s.insert(pos, newstring);
    }
    return pos + newstring.length();
}

int convert::replaceAllString(std::string& s, const std::string& oldstring, const std::string& newstring)
{
    int pos = s.find(oldstring);
    while (pos >= 0)
    {
        s.erase(pos, oldstring.length());
        s.insert(pos, newstring);
        pos = s.find(oldstring, pos + newstring.length());
    }
    return pos + newstring.length();
}

void convert::replaceStringInFile(const std::string& oldfilename, const std::string& newfilename, const std::string& oldstring, const std::string& newstring)
{
    std::string s = readStringFromFile(oldfilename.c_str());
    if (s.length() <= 0) { return; }
    replaceString(s, oldstring, newstring);
    writeStringToFile(s, newfilename.c_str());
}

void convert::replaceAllStringInFile(const std::string& oldfilename, const std::string& newfilename, const std::string& oldstring, const std::string& newstring)
{
    std::string s = readStringFromFile(oldfilename.c_str());
    if (s.length() <= 0) { return; }
    replaceAllString(s, oldstring, newstring);
    writeStringToFile(s, newfilename.c_str());
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

std::string convert::findANumber(const std::string& s)
{
    bool findPoint = false;
    bool findNumber = false;
    bool findE = false;
    std::string n;
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if (c >= '0' && c <= '9' || c == '-' || c == '.' || c == 'e' || c == 'E')
        {
            if (c >= '0' && c <= '9' || c == '-')
            {
                findNumber = true;
                n += c;
            }
            if (c == '.')
            {
                if (!findPoint)
                {
                    n += c;
                }
                findPoint = true;
            }
            if (c == 'e' || c == 'E')
            {
                if (findNumber && !(findE))
                {
                    n += c;
                    findE = true;
                }
            }
        }
        else
        {
            if (findNumber)
            {
                break;
            }
        }
    }
    return n;
}

unsigned convert::findTheLast(const std::string& s, const std::string& content)
{
    int pos = 0, prepos = 0;
    while (pos >= 0)
    {
        prepos = pos;
        pos = s.find(content, prepos + 1);
        //printf("%d\n",pos);
    }
    return prepos;
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
	char c[_MAX_PATH];
	memset(c, 0, _MAX_PATH);
	_splitpath(fileName.c_str(), nullptr, c, nullptr, nullptr);
	std::string path = c;
	return path + "\\";
}

std::string convert::extractFileName(const std::string& fileName)
{
	char c[_MAX_FNAME];
	memset(c, 0, _MAX_FNAME);
	_splitpath(fileName.c_str(), nullptr, nullptr, c, nullptr);
	std::string name = c;
	return name;
}

std::string convert::extractFileExt(const std::string & fileName)
{
	char e[_MAX_EXT];
	memset(e, 0, _MAX_EXT);
	_splitpath(fileName.c_str(), nullptr, nullptr, nullptr, e);
	std::string ext = e;
	return ext;
}

std::string convert::extractFullName(const std::string & fileName)
{
	char c[_MAX_FNAME];
	memset(c, 0, _MAX_FNAME);
	char e[_MAX_EXT];
	memset(e, 0, _MAX_EXT);
	_splitpath(fileName.c_str(), nullptr, nullptr, c, e);
	std::string name = c;
	std::string ext = e;
	return name + ext;
}

std::vector<std::string> convert::extractFileAll(const std::string & fileName)
{
	char c[_MAX_FNAME];
	memset(c, 0, _MAX_FNAME);
	char e[_MAX_EXT];
	memset(e, 0, _MAX_EXT);
	char p[_MAX_PATH];
	memset(p, 0, _MAX_PATH);
	_splitpath(fileName.c_str(), nullptr, p, c, e);
	std::vector<std::string> s;
	s.resize(3);
	s[0] = p;
	s[1] = c;
	s[2] = e;
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

int convert::GetUtf8LetterNumber(const char* s)
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

#ifdef _USE_LIBICONV

#define CONV_BUFFER_SIZE 2048

std::string convert::conv(const std::string& src, const char* from, const char* to)
{
	//const char *from_charset, const char *to_charset, const char *inbuf, size_t inlen, char *outbuf;
	size_t inlen = (int)src.length() < CONV_BUFFER_SIZE ? (int)src.length() : CONV_BUFFER_SIZE;
	size_t outlen = CONV_BUFFER_SIZE;

	char in[CONV_BUFFER_SIZE] = { '\0' };
	char out[CONV_BUFFER_SIZE] = { '\0' };

	char* pin = in, *pout = out;
	memcpy(in, src.c_str(), inlen);
	iconv_t cd;
	cd = iconv_open(to, from);
	if (cd == nullptr) { return ""; }
	if (iconv(cd, &pin, &inlen, &pout, &outlen) == -1)
	{
		out[0] = '\0';
	}
	iconv_close(cd);
	return out;
	return src;
}

std::string convert::conv(const std::string& src, const std::string& from, const std::string& to)
{
	return conv(src, from.c_str(), to.c_str());
}
#endif // _USE_LIBICONV

//std::string convert::unicodeToUTF8(const std::wstring& unicodeStr)
//{
//	std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
//	std::string str = cvt.to_bytes(unicodeStr);
//	return str;
//}

//std::wstring convert::UTF8ToUnicode(const std::string& UTF8Str)
//{
//	std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
//	std::wstring wstr = cvt.from_bytes(UTF8Str);
//	return wstr;
//}

//std::string convert::unicodeToGBK(const std::wstring& unicodeStr)
//{
//#ifdef __ANDROID__
//	return nullptr;
//#else
//
//#ifdef _USE_LIBICONV
//	int len = unicodeStr.length() * sizeof(wchar_t);
//	auto c = new char[len + 1];
//	c[len] = '\0';
//	memcpy(c, unicodeStr.c_str(), len);
//	std::string s = c;
//	return conv(s, "utf-16", "cp936");
//#else
//	std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> cvt(new std::codecvt_byname<wchar_t, char, mbstate_t>(GBKName));
//	std::string str = cvt.to_bytes(unicodeStr);
//	return str;
//#endif // _USE_LIBICONV
//
//#endif	
//}
//
//std::wstring convert::GBKToUnicode(const std::string& str)
//{
//#ifdef __ANDROID__
//	return nullptr;
//#else
//#ifdef _USE_LIBICONV
//	auto s = conv(str, "utf-16", "cp936");
//	int len = s.length() / sizeof(wchar_t);
//	auto c = new wchar_t[len + 1];
//	c[len] = '\0';
//	memcpy(c, str.c_str(), len * sizeof(wchar_t));
//	std::wstring ret = c;
//	return ret;
//#else
//	std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> cvt(new std::codecvt_byname<wchar_t, char, mbstate_t>(GBKName));
//	std::wstring wstr = cvt.from_bytes(str);
//	return wstr;
//#endif // _USE_LIBICONV
//#endif
//}

//只有windows下可用
std::string convert::GBKToUTF8_InWinOnly(const std::string& str)
{
#ifdef _WIN32
	int wlen = ::MultiByteToWideChar(CP_ACP, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring wstr(wlen, 0);
	::MultiByteToWideChar(CP_ACP, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], wlen);

	int len = ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8(len, 0);
	::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &utf8[0], len, nullptr, nullptr);

	return utf8;
	//return unicodeToUTF8(GBKToUnicode(str));
	//return str;
#else
	return str;
#endif // _WIN32
	
}

//只有windows下可用
std::string convert::UTF8ToGBK_InWinOnly(const std::string& str)
{
#ifdef _WIN32
	int wlen = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring wstr(wlen, 0);
	::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], wlen);

	int len = ::WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string gbk(len, 0);
	::WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), static_cast<int>(wstr.size()), &gbk[0], len, nullptr, nullptr);

	return gbk;
	//return unicodeToGBK(UTF8ToUnicode(str));
	//return str;
#else
	return str;
#endif // _WIN32
}


//void convert::deleteAll(const std::string & path)
//{
//	std::vector<std::string> s = getAllFile(path);
//	for (size_t i = 0; i < s.size(); i++)
//	{
//		std::string fileName = path + extractFullName(s[i]);
//		remove(fileName.c_str());
//	}
//}

//void convert::copyTo(const std::string & spath, const std::string & dpath)
//{
//	std::vector<std::string> s = getAllFile(spath);
//	for (size_t i = 0; i < s.size(); i++)
//	{
//		copyFile(spath + extractFullName(s[i]), dpath + extractFullName(s[i]));
//	}
//}
//
//bool convert::copyFile(const std::string & oldFile, const std::string & newFile)
//{
//	int len = 0;
//	char * d = nullptr;
//	FILE * f = fopen(oldFile.c_str(), "rb");
//	if (!f)
//	{
//		return false;
//	}
//	fseek(f, 0, 2);
//	len = ftell(f);
//	if (len > 0)
//	{
//		fseek(f, 0, 0);
//		d = new char[len];
//		fread(d, len, 1, f);
//	}
//
//	fclose(f);
//	if (len > 0)
//	{
//		FILE * fp = fopen(newFile.c_str(), "wb");
//		if (!fp)
//		{
//			delete[] d;
//			return false;
//		}
//
//		fwrite(d, len, 1, fp);
//		fclose(fp);
//
//		delete[] d;		
//	}
//	return true;
//}

//typedef struct ffblk ffblk;
//
//std::vector<std::string> convert::getAllFile(const std::string & path, const std::string & fileExt)
//{
//	std::string fext = fileExt;
//	std::transform(fext.begin(), fext.end(), fext.begin(), ::tolower);
//	std::vector<std::string> s;
//	s.resize(0);
//
//#ifdef _WIN32
//	std::string firstF = path + "*.*";
//	_finddata_t f;
//	int handle = _findfirst(firstF.c_str(), &f);
//	if (handle == -1)
//	{
//		return s;
//	}
//	int ret = 0;
//	while (!ret)
//	{
//		if (f.attrib != _A_SUBDIR && (lowerCase(extractFileExt(f.name)) == fext || fext == ".*"))
//		{
//			s.resize(s.size() + 1);
//			s[s.size() - 1] = f.name;
//		}
//		ret = _findnext(handle, &f);
//	}
//	_findclose(handle);
//#elif _WIN64
//	struct ffblk _ffblk;
//	int ret = findfirst("*.*", &_ffblk, 2);
//	while (!ret)
//	{
//		if (_ffblk.ff_atrb != 16 && (lowerCase(extractFileExt(_ffblk.ff_name)) == fext || fext == ".*"))
//		{
//			s.resize(s.size() + 1);
//			s[s.size() - 1] = _ffblk.ff_name;
//		}	
//		ret = findnext("*.*", &_ffblk, 2);
//	}
//#endif // _WIN32
//	return s;
//}
