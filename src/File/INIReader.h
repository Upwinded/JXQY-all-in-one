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
#include <map>
#include <memory>
#include "File.h"


struct iniKey
{
	unsigned int hash = 0;
	std::string key = u8"";
	std::string value = u8"";
};

struct IniSection
{
	std::map<std::string, std::string> keys;
};

struct IniMap
{
	std::map<std::string, IniSection> sections;
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
	
	// Construct INIReader from std::string stream.
	// (Added by Upwinded.)
	INIReader(const std::unique_ptr<char[]>& s);

	//(Added by Upwinded.)
	virtual ~INIReader();
	//(Added by Upwinded.)
	std::string saveToString();
	//(Added by Upwinded.)
	void saveToFile(const std::string& fileName);
	//(Added by Upwinded.)
	void Set(const std::string& section, const std::string& name, const std::string& value);
	//(Added by Upwinded.)
	void SetTime(const std::string& section, const std::string& name, UTime value);
	//(Added by Upwinded.)
	void SetInteger(const std::string& section, const std::string& name, long value);
	//(Added by Upwinded.)
	void SetReal(const std::string& section, const std::string& name, float value);
	//(Added by Upwinded.)
	void SetBoolean(const std::string& section, const std::string& name, bool value);
	//(Added by Upwinded.)
	void SetColor(const std::string& section, const std::string& name, uint32_t value);

	//(Added by Upwinded.)
	uint32_t GetColor(const std::string& section, const std::string& name, uint32_t value);


    // Return the result of ini_parse(), i.e., 0 on success, line number of
    // first error on parse error, or -1 on file open error.
    int ParseError() const;

    // Get a std::string value from INI file, returning default_value if not found.
	std::string Get(const std::string& section, const std::string& name, const std::string& default_value) const;

	UTime GetTime(const std::string& section, const std::string& name, UTime default_value) const;

    // Get an integer (long) value from INI file, returning default_value if
    // not found or not a valid integer (decimal u8"1234", u8"-1234", or hex u8"0x4d2").
    long GetInteger(const std::string& section, const std::string& name, long default_value) const;

    // Get a real (floating point float) value from INI file, returning
    // default_value if not found or not a valid floating point value
    // according to strtod().
    float GetReal(const std::string& section, const std::string& name, float default_value) const;

    // Get a boolean value from INI file, returning default_value if not found or if
    // not a valid true/false value. Valid true values are u8"true", u8"yes", u8"on", u8"1",
    // and valid false values are u8"false", u8"no", u8"off", u8"0" (not case sensitive).
    bool GetBoolean(const std::string& section, const std::string& name, bool default_value) const;



private:

    int _error = 0;
    //std::map<std::string, std::map<std::string, std::string>> _values;
	IniMap map;

    static std::string MakeKey(const std::string& section, const std::string& name);
    static int ValueHandler(void* user, const char* section, const char* name,
                            const char* value);
};

#endif  // __INIREADER_H__
