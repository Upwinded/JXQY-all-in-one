// Read an INI file into easy-to-access name/value pairs.

// inih and INIReader are released under the New BSD license (see LICENSE.txt).
// Go to the project home page for more info:
//
// https://github.com/benhoyt/inih

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "File.h"
#include "ini.h"

#include "../libconvert/libconvert.h"
#include "INIReader.h"
using std::string;

INIReader::INIReader()
{
}

INIReader::INIReader(const string& filename)
{
	std::unique_ptr<char[]> s;
	int len;

	std::string fName = filename;

	if (fName.length() > 1 && *fName.c_str() == '\\')
	{
		fName.erase(fName.begin());
	}
	std::string newfName = fName;

#ifndef _WIN32
	convert::replaceAllString(newfName, "\\", "/");
//	newfName = convert::lowerCase(newfName);
#endif
	File::readFile(newfName.c_str(), s, len);
	if (s == nullptr || len == 0)
	{
		_error = -1;
		return;
	}
    _error = ini_parse_string(s.get(), ValueHandler, this);
}

INIReader::INIReader(const std::unique_ptr<char[]>& s)
{
	_error = -1;
	if (s != nullptr)
	{
		_error = ini_parse_string(s.get(), ValueHandler, this);
	}
}

INIReader::~INIReader()
{
	for (auto it = map.sections.begin(); it != map.sections.end(); ++it)
	{
		it->second.keys.clear();
	}
	map.sections.clear();
}

int INIReader::ParseError() const
{
    return _error;
}

string INIReader::Get(const string& section, const string& name, const string& default_value) const
{
	std::string s = section;
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	auto sn = name;
	std::transform(sn.begin(), sn.end(), sn.begin(), ::tolower);

	auto sec = map.sections.find(s);
	if (sec != map.sections.end())
	{
		auto key = sec->second.keys.find(sn);
		if (key != sec->second.keys.end())
		{
			return key->second;
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

	auto sec = map.sections.find(s);
	if (sec == map.sections.end())
	{
		IniSection newSection;
		newSection.keys[sn] = value;
		map.sections[s] = newSection;
	}
	else
	{
		sec->second.keys[sn] = value;
	}

}

void INIReader::SetTime(const std::string& section, const std::string& name, UTime value)
{
	std::string v = std::to_string(value);
	Set(section, name, v);
}

void INIReader::SetInteger(const std::string & section, const std::string & name, long value)
{
	std::string v = std::to_string(value);
	Set(section, name, v);
}

void INIReader::SetReal(const std::string & section, const std::string & name, float value)
{
	std::string v = std::to_string(value);
	Set(section, name, v);
}

void INIReader::SetBoolean(const std::string & section, const std::string & name, bool value)
{
	std::string v = value ? "1" : "0";
	Set(section, name, v);
}

void INIReader::SetColor(const std::string& section, const std::string& name, uint32_t value)
{
	unsigned char colorData[3] = { (unsigned char)((value & 0xFF0000) >> 16) ,(unsigned char)((value & 0xFF00) >> 8) , (unsigned char)((value & 0xFF)) };
	std::string col = std::to_string(colorData[0]) + "," + std::to_string(colorData[1]) + "," + std::to_string(colorData[2]); 
	Set(section, name, col);
}

uint32_t INIReader::GetColor(const std::string & section, const std::string & name, uint32_t value)
{
	unsigned char colorData[3] = { (unsigned char)((value & 0xFF0000) >> 16) ,(unsigned char)((value & 0xFF00) >> 8) , (unsigned char)((value & 0xFF)) };
	std::string col = std::to_string(colorData[0]) + "," + std::to_string(colorData[1]) + "," + std::to_string(colorData[2]);
	col = Get(section, name, col);
	std::vector<std::string> c = convert::splitString(col, ",");
	for (size_t i = 0; i < (c.size() > 3 ? 3 : c.size()); i++)
	{
		//char* end = nullptr;
		//long n = strtol(c[i].c_str(), &end, 0);
		//colorData[i] = (unsigned char)(end > c[i].c_str() ? n : colorData[i]);
		if (!c[i].empty())
		{
			try
			{
				colorData[i] = (unsigned char)std::stoul(c[i]);
			}
			catch (const std::exception&)
			{

			}
		}
	}
	return 0xFF000000 | ((colorData[0] << 16) + (colorData[1] << 8) + colorData[2]);
}

UTime INIReader::GetTime(const std::string& section, const std::string& name, UTime default_value) const
{
	string valstr = Get(section, name, "");
	try
	{
		return (UTime)std::stoll(valstr, nullptr, 0);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
}

long INIReader::GetInteger(const string& section, const string& name, long default_value) const
{
    string valstr = Get(section, name, "");
	try
	{
		return std::stol(valstr, nullptr, 0);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
}

float INIReader::GetReal(const string& section, const string& name, float default_value) const
{
    string valstr = Get(section, name, "");
	try
	{
		return std::stod(valstr);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
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
	for (auto it = map.sections.begin(); it != map.sections.end(); ++it)
	{
		s += "[" + it->first + "]\r\n";
		for (auto it_2 = it->second.keys.begin(); it_2 != it->second.keys.end(); ++it_2)
		{
			s += it_2->first + "=" + it_2->second + "\r\n";
		}
		s += "\r\n";
	}

	return s;
}

void INIReader::saveToFile(const std::string & fileName)
{
	std::string s = saveToString();
	File::writeFile(fileName, s.c_str(), s.size());
	/*auto fp = SDL_IOFromFile(fileName.c_str(), "wb");
	if (!fp)
	{
		GameLog::write(stderr, "Can not open file %s\n", fileName.c_str());
		return;
	}
	SDL_SeekIO(fp, 0, 0);
	SDL_WriteIO(fp, s.c_str(), s.length(), 1);
	SDL_CloseIO(fp);*/
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

    return 1;
}

