#ifndef _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif 

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif 


#include "libconvert.h"

#ifdef _WIN32
#include "io.h"
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
	_splitpath(fileName.c_str(), NULL, c, NULL, NULL);
	std::string path = c;
	return path;
}

std::string convert::extractFileName(const std::string& fileName)
{
	char c[_MAX_FNAME];
	memset(c, 0, _MAX_FNAME);
	_splitpath(fileName.c_str(), NULL, NULL, c, NULL);
	std::string name = c;
	return name;
}

std::string convert::extractFileExt(const std::string & fileName)
{
	char e[_MAX_EXT];
	memset(e, 0, _MAX_EXT);
	_splitpath(fileName.c_str(), NULL, NULL, NULL, e);
	std::string ext = e;
	return ext;
}

std::string convert::extractFullName(const std::string & fileName)
{
	char c[_MAX_FNAME];
	memset(c, 0, _MAX_FNAME);
	char e[_MAX_EXT];
	memset(e, 0, _MAX_EXT);
	_splitpath(fileName.c_str(), NULL, NULL, c, e);
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
	_splitpath(fileName.c_str(), NULL, p, c, e);
	std::vector<std::string> s = {};
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

#ifdef _MSC_VER
#define GBKName ".936"
#else
#define GBKName "zh_CN.GBK"
#endif // _MSC_VER

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

std::string convert::unicodeToGBK(const std::wstring& unicodeStr)
{
#ifdef _USE_LIBICONV
	int len = unicodeStr.length() * sizeof(wchar_t);
	auto c = new char[len + 1];
	c[len] = '\0';
	memcpy(c, unicodeStr.c_str(), len);
	std::string s = c;	
	return conv(s, "utf-16", "cp936");
#else
	std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> cvt(new std::codecvt_byname<wchar_t, char, mbstate_t>(GBKName));
	std::string str = cvt.to_bytes(unicodeStr);
	return str;
#endif // _USE_LIBICONV

}

std::wstring convert::GBKToUnicode(const std::string& str)
{
#ifdef _USE_LIBICONV
	auto s = conv(str, "utf-16", "cp936");
	int len = s.length() / sizeof(wchar_t);
	auto c = new wchar_t[len + 1];
	c[len] = '\0';
	memcpy(c, str.c_str(), len * sizeof(wchar_t));
	std::wstring ret = c;
	return ret;
#else
	std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> cvt(new std::codecvt_byname<wchar_t, char, mbstate_t>(GBKName));
	std::wstring wstr = cvt.from_bytes(str);
	return wstr; 
#endif // _USE_LIBICONV
}

std::string convert::unicodeToUTF8(const std::wstring& unicodeStr)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
	std::string str = cvt.to_bytes(unicodeStr);
	return str;
}

std::wstring convert::UTF8ToUnicode(const std::string& UTF8Str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> cvt;
	std::wstring wstr = cvt.from_bytes(UTF8Str);
	return wstr;
}

std::string convert::GBKToUTF8(const std::string& str)
{
	return unicodeToUTF8(GBKToUnicode(str));
}

std::string convert::UTF8ToGBK(const std::string& str)
{
	return unicodeToGBK(UTF8ToUnicode(str));
}

void convert::deleteAll(const std::string & path)
{
	std::vector<std::string> s = getAllFile(path);
	for (size_t i = 0; i < s.size(); i++)
	{
		std::string fileName = path + extractFullName(s[i]);
		remove(fileName.c_str());
	}
}

void convert::copyTo(const std::string & spath, const std::string & dpath)
{
	std::vector<std::string> s = getAllFile(spath);
	for (size_t i = 0; i < s.size(); i++)
	{
		copyFile(spath + extractFullName(s[i]), dpath + extractFullName(s[i]));
	}
}

bool convert::copyFile(const std::string & oldFile, const std::string & newFile)
{
	int len = 0;
	char * d = NULL;
	FILE * f = fopen(oldFile.c_str(), "rb");
	if (!f)
	{
		return false;
	}
	fseek(f, 0, 2);
	len = ftell(f);
	if (len > 0)
	{
		fseek(f, 0, 0);
		d = new char[len];
		fread(d, len, 1, f);
	}

	fclose(f);
	if (len > 0)
	{
		FILE * fp = fopen(newFile.c_str(), "wb");
		if (!fp)
		{
			delete[] d;
			return false;
		}

		fwrite(d, len, 1, fp);
		fclose(fp);

		delete[] d;		
	}
	return true;
}

std::vector<std::string> convert::getAllFile(const std::string & path, const std::string & fileExt)
{
	std::string fext = fileExt;
	std::transform(fext.begin(), fext.end(), fext.begin(), ::tolower);
	std::vector<std::string> s = {};
	s.resize(0);

#ifdef _WIN32
	std::string firstF = path + "*.*";
	_finddata_t f;
	int handle = _findfirst(firstF.c_str(), &f);
	if (handle == -1)
	{
		return s;
	}
	int ret = 0;
	while (!ret)
	{
		if (f.attrib != _A_SUBDIR && (lowerCase(extractFileExt(f.name)) == fext || fext == ".*"))
		{
			s.push_back(f.name);
		}
		ret = _findnext(handle, &f);
	}
	_findclose(handle);
#else
	DIR *dp;
	struct dirent *entry;
	if ((dp = opendir(path)) == NULL)
	{
		return s;
	}
	while ((entry = readdir(dp)) != NULL)
	{
		if ((strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) && (lowerCase(extractFileExt(f.name)) == fext || fext == ".*"))
		{
			continue;
		}
		s.push_back(entry->d_name);
	}

/*
	struct ffblk _ffblk;
	int ret = _findfirst("*.*", &_ffblk, 2);
	while (!ret)
	{
		if (_ffblk.ff_atrb != 16 && (lowerCase(extractFileExt(_ffblk.ff_name)) == fext || fext == ".*"))
		{
			s.resize(s.size() + 1);
			s[s.size() - 1] = _ffblk.ff_name;
		}	
		ret = _findnext("*.*", &_ffblk, 2);
	}
*/
#endif // _WIN32
	return s;
}


