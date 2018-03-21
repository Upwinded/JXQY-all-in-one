// Read an INI file into easy-to-access name/value pairs.

// inih and INIReader are released under the New BSD license (see LICENSE.txt).
// Go to the project home page for more info:
//
// https://github.com/benhoyt/inih
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif 

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "ini.h"
#include "INIReader.h"
#include "../libconvert/libconvert.h"

using std::string;

INIReader::INIReader()
{
}

INIReader::INIReader(const string& filename)
{
    _error = ini_parse(filename.c_str(), ValueHandler, this);
}

INIReader::INIReader(const char * s)
{
	if (s != NULL)
	{
		_error = ini_parse_string(s, ValueHandler, this);
	}
}

INIReader::~INIReader()
{
	for (size_t i = 0; i < map.iniSection.size(); i++)
	{
		for (size_t j = 0; j < map.iniSection[i].IniKey.size(); j++)
		{
			map.iniSection[i].IniKey[j].hash = 0;
			map.iniSection[i].IniKey[j].key = "";
			map.iniSection[i].IniKey[j].value = "";
		}
		map.iniSection[i].hash = 0;
		map.iniSection[i].section = "";
	}
}

int INIReader::ParseError() const
{
    return _error;
}

string INIReader::Get(const string& section, const string& name, const string& default_value) const
{
    //string key = MakeKey(section, name);
    // Use _values.find() here instead of _values.at() to support pre C++11 compilers
    //string s = _values.count(key) ? _values.find(key)->second : default_value;

	std::string s;
	std::string sn;
	s = section;
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	sn = name;
	std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);
	unsigned int h = hashString(s);
	unsigned int hn = hashString(sn);
	for (size_t i = 0; i < map.iniSection.size(); i++)
	{
		if (h == map.iniSection[i].hash)
		{
			for (size_t j = 0; j < map.iniSection[i].IniKey.size(); j++)
			{
				if (hn == map.iniSection[i].IniKey[j].hash)
				{
					return map.iniSection[i].IniKey[j].value;
				}
			}
		}
	}
	return default_value;
}

void INIReader::Set(const std::string& section, const std::string& name,
	const std::string& value)
{
	int findSection = -1;
	int findName = -1;
	std::string s = section;
	std::string sn = name;
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);
	unsigned int h = hashString(s);
	unsigned int hn = hashString(sn);
	for (size_t i = 0; i < map.iniSection.size(); i++)
	{
		if (map.iniSection[i].hash == h)
		{
			findSection = (int)i;
			for (size_t j = 0; j < map.iniSection[i].IniKey.size(); j++)
			{
				if (hn == map.iniSection[i].IniKey[j].hash)
				{
					findName = j;
					map.iniSection[i].IniKey[j].value = value;
				}
			}
		}
	}
	if (findSection < 0)
	{
		map.iniSection.resize(map.iniSection.size() + 1);
		map.iniSection[map.iniSection.size() - 1].hash = h;
		map.iniSection[map.iniSection.size() - 1].section = section;
		findSection = map.iniSection.size() - 1;
	}
	if (findName < 0)
	{
		map.iniSection[findSection].IniKey.resize(map.iniSection[findSection].IniKey.size() + 1);
		findName = map.iniSection[findSection].IniKey.size() - 1;
		map.iniSection[findSection].IniKey[findName].hash = hn;
		map.iniSection[findSection].IniKey[findName].key = name;
		map.iniSection[findSection].IniKey[findName].value = value;
	}
}


void INIReader::SetInteger(const std::string & section, const std::string & name, long value)
{
	std::string v = formatString("%d", value);
	Set(section, name, v);
}

void INIReader::SetReal(const std::string & section, const std::string & name, double value)
{
	std::string v = formatString("%lf", value);
	Set(section, name, v);
}

void INIReader::SetBoolean(const std::string & section, const std::string & name, bool value)
{
	std::string v = value ? "1" : "0";
	Set(section, name, v);
}

unsigned int INIReader::GetColor(const std::string & section, const std::string & name, unsigned int value)
{
	unsigned char colorData[3] = { unsigned char((value & 0xFF0000) >> 16) ,unsigned char((value & 0xFF00) >> 8) , unsigned char((value & 0xFF)) };
	std::string col = convert::formatString("%d", colorData[0]) + "," + convert::formatString("%d", colorData[1]) + "," + convert::formatString("%d", colorData[2]);
	col = Get(section, name, col);
	std::vector<std::string> c = convert::splitString(col, ",");
	for (size_t i = 0; i < (c.size() > 3 ? 3 : c.size()); i++)
	{
		//char* end = NULL;
		//long n = strtol(c[i].c_str(), &end, 0);
		//colorData[i] = (unsigned char)(end > c[i].c_str() ? n : colorData[i]);
		colorData[i] = (unsigned char)atoi(c[i].c_str());
	}
	return 0xFF000000 | ((colorData[0] << 16) + (colorData[1] << 8) + colorData[2]);
}

long INIReader::GetInteger(const string& section, const string& name, long default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
	if (value == NULL)
	{
		return default_value;
	}
    char* end;
    // This parses "1234" (decimal) and also "0x4D2" (hex)
    long n = strtol(value, &end, 0);
    return end > value ? n : default_value;
}

double INIReader::GetReal(const string& section, const string& name, double default_value) const
{
    string valstr = Get(section, name, "");
    const char* value = valstr.c_str();
	if (value == NULL)
	{
		return default_value;
	}
    char* end;
    double n = strtod(value, &end);
    return end > value ? n : default_value;
}

bool INIReader::GetBoolean(const string& section, const string& name, bool default_value) const
{
    string valstr = Get(section, name, "");
    // Convert to lower case to make string comparisons case-insensitive
    std::transform(valstr.begin(), valstr.end(), valstr.begin(), ::tolower);
    if (valstr == "true" || valstr == "yes" || valstr == "on" || valstr == "1")
        return true;
    else if (valstr == "false" || valstr == "no" || valstr == "off" || valstr == "0")
        return false;
    else
        return default_value;
}

std::string INIReader::saveToString()
{
	std::string s = "";
	for (size_t i = 0; i < map.iniSection.size(); i++)
	{
		s += "[" + map.iniSection[i].section + "]\r\n";
		for (size_t j = 0; j < map.iniSection[i].IniKey.size(); j++)
		{
			s += map.iniSection[i].IniKey[j].key + "=" + map.iniSection[i].IniKey[j].value + "\r\n";
		}
		s += "\r\n";
	}
	return s;
}

void INIReader::saveToFile(const std::string & fileName)
{
	std::string s = saveToString();
	FILE * fp = fopen(fileName.c_str(), "wb");
	if (!fp)
	{
		fprintf(stderr, "Can not open file %s\n", fileName.c_str());
		return;
	}
	fseek(fp, 0, 0);
	fwrite(s.c_str(), s.length(), 1, fp);
	fclose(fp);
}

string INIReader::MakeKey(const string& section, const string& name)
{
    string key = section + "=" + name;
    // Convert to lower case to make section/name lookups case-insensitive
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    return key;
}

int INIReader::ValueHandler(void* user, const char* section, const char* name,
                            const char* value)
{
    INIReader* reader = (INIReader*)user;
	std::string strSection = section;
	std::string strName = name;
	std::string strValue = value;
	reader->Set(strSection, strName, strValue);

    //string key = MakeKey(section, name);
    //if (reader->_values[key].size() > 0)
       // reader->_values[key] += "\n";
    //reader->_values[key] += value;
    return 1;
}

#ifdef _MSC_VER
#define vsprintf vsprintf_s
#endif

std::string INIReader::formatString(const char * format, ...)
{
	char s[1000];
	va_list arg_ptr;
	va_start(arg_ptr, format);
	vsprintf(s, format, arg_ptr);
	va_end(arg_ptr);
	return s;
}
