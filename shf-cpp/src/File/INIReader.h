// Read an INI file into easy-to-access name/value pairs.

// inih and INIReader are released under the New BSD license (see LICENSE.txt).
// Go to the project home page for more info:
//
// https://github.com/benhoyt/inih
#pragma once

#ifndef __INIREADER_H__
#define __INIREADER_H__

#include <string>
#include <vector>
#include <stdarg.h>

struct IniKey
{
	unsigned int hash = 0;
	std::string key = "";
	std::string value = "";
};

struct IniSection
{
	unsigned int hash = 0;
	std::string section = "";
	std::vector<IniKey> IniKey = {};
};

struct IniMap
{
	std::vector<IniSection> iniSection = {};
};

// Read an INI file into easy-to-access name/value pairs. (Note that I've gone
// for simplicity here rather than speed, but it should be pretty decent.)
class INIReader
{
public:
	INIReader();
    // Construct INIReader and parse given filename. See ini.h for more info
    // about the parsing.
    INIReader(const std::string& filename);
	
	// Construct INIReader from string stream.
	// (Added by Upwinded.)
	INIReader(const char * s);

	//(Added by Upwinded.)
	~INIReader();
	//(Added by Upwinded.)
	std::string saveToString();
	//(Added by Upwinded.)
	void saveToFile(const std::string& fileName);
	//(Added by Upwinded.)
	void Set(const std::string& section, const std::string& name, const std::string& value);
	//(Added by Upwinded.)
	void SetInteger(const std::string& section, const std::string& name, long value);
	//(Added by Upwinded.)
	void SetReal(const std::string& section, const std::string& name, double value);
	//(Added by Upwinded.)
	void SetBoolean(const std::string& section, const std::string& name, bool value);
	//(Added by Upwinded.)
	unsigned int GetColor(const std::string& section, const std::string& name, unsigned int value);


    // Return the result of ini_parse(), i.e., 0 on success, line number of
    // first error on parse error, or -1 on file open error.
    int ParseError() const;

    // Get a string value from INI file, returning default_value if not found.
    std::string Get(const std::string& section, const std::string& name, const std::string& default_value) const;
	
    // Get an integer (long) value from INI file, returning default_value if
    // not found or not a valid integer (decimal "1234", "-1234", or hex "0x4d2").
    long GetInteger(const std::string& section, const std::string& name, long default_value) const;

    // Get a real (floating point double) value from INI file, returning
    // default_value if not found or not a valid floating point value
    // according to strtod().
    double GetReal(const std::string& section, const std::string& name, double default_value) const;

    // Get a boolean value from INI file, returning default_value if not found or if
    // not a valid true/false value. Valid true values are "true", "yes", "on", "1",
    // and valid false values are "false", "no", "off", "0" (not case sensitive).
    bool GetBoolean(const std::string& section, const std::string& name, bool default_value) const;


private:
    int _error;
    //std::map<std::string, std::map<std::string, std::string>> _values;
	IniMap map;

	static unsigned int hashString(const std::string & name)
	{
		unsigned int result = 0;
		if (name.length() > 0)
		{
			char * ch = new char[name.length() + 1];
			memcpy(ch, name.c_str(), name.length());
			ch[name.length()] = 0;
			unsigned int cnt = 0;
			int i = 0;
			while (ch[i] != 0)
			{
				if (ch[i] == '/')
				{
					ch[i] = '\\';
				}
				if ((ch[i] >= 'A') && (ch[i] <= 'Z'))
				{
					ch[i] = 0x20 + ch[i];
				}
				unsigned int u = ch[i];
				result = (u * (cnt + 1) + result) % 0x8000000B;
				result = (((result ^ 0xFFFFFFFF) + 1) << 4) - result;
				cnt++;
				i++;
			}
			result ^= 0x12345678;
			delete[] ch;
		}
		return result;
	}

    static std::string MakeKey(const std::string& section, const std::string& name);
    static int ValueHandler(void* user, const char* section, const char* name,
                            const char* value);

	std::string formatString(const char* format, ...);

};

#endif  // __INIREADER_H__
