// Read an INI file into easy-to-access name/value pairs.

// inih and INIReader are released under the New BSD license (see LICENSE.txt).
// Go to the project home page for more info:
//
// https://github.com/benhoyt/inih

#include <algorithm>
#include "log.h"
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include "File.h"
#include "ini.h"

#include "../libconvert/libconvert.h"
#include "INIReader.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
std::string toLowerAscii(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character + ('a' - 'A'));
		}
	}
	return value;
}

bool isValidUtf8(const char* data, size_t length)
{
	if (data == nullptr)
	{
		return true;
	}

	size_t index = 0;
	while (index < length)
	{
		unsigned char lead = static_cast<unsigned char>(data[index]);
		if (lead < 0x80)
		{
			index++;
			continue;
		}

		size_t extraBytes = 0;
		uint32_t codePoint = 0;
		if ((lead & 0xE0) == 0xC0)
		{
			extraBytes = 1;
			codePoint = lead & 0x1F;
			if (codePoint == 0)
			{
				return false;
			}
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			extraBytes = 2;
			codePoint = lead & 0x0F;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			extraBytes = 3;
			codePoint = lead & 0x07;
		}
		else
		{
			return false;
		}

		if (index + extraBytes >= length)
		{
			return false;
		}

		for (size_t offset = 1; offset <= extraBytes; offset++)
		{
			unsigned char continuation = static_cast<unsigned char>(data[index + offset]);
			if ((continuation & 0xC0) != 0x80)
			{
				return false;
			}
			codePoint = (codePoint << 6) | (continuation & 0x3F);
		}

		if ((extraBytes == 1 && codePoint < 0x80) ||
			(extraBytes == 2 && codePoint < 0x800) ||
			(extraBytes == 3 && codePoint < 0x10000) ||
			codePoint > 0x10FFFF ||
			(codePoint >= 0xD800 && codePoint <= 0xDFFF))
		{
			return false;
		}

		index += extraBytes + 1;
	}

	return true;
}

std::string convertGbkToUtf8(const std::string& text)
{
#if defined(_WIN32)
	if (text.empty())
	{
		return text;
	}

	int wideLength = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (wideLength <= 0)
	{
		return "";
	}

	std::wstring wideText(wideLength, L'\0');
	MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), &wideText[0], wideLength);

	int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideText.data(), wideLength, nullptr, 0, nullptr, nullptr);
	if (utf8Length <= 0)
	{
		return "";
	}

	std::string utf8Text(utf8Length, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wideText.data(), wideLength, &utf8Text[0], utf8Length, nullptr, nullptr);
	return utf8Text;
#else
	return "";
#endif
}

std::string normalizeIniTextEncoding(const char* data, size_t length)
{
	if (data == nullptr)
	{
		return "";
	}

	std::string text(data, length);
	if (isValidUtf8(text.data(), text.size()))
	{
		return text;
	}

	std::string converted = convertGbkToUtf8(text);
	if (!converted.empty() && isValidUtf8(converted.data(), converted.size()))
	{
		return converted;
	}

	return text;
}
}


INIReader::INIReader()
{
}

INIReader::INIReader(const std::string& filename)
{
	fileName = filename;
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
	std::string content = normalizeIniTextEncoding(s.get(), static_cast<size_t>(len));
    _error = ini_parse_string(content.c_str(), ValueHandler, this);
}

INIReader::INIReader(const std::unique_ptr<char[]>& s)
{
	_error = -1;
	if (s != nullptr)
	{
		std::string content = normalizeIniTextEncoding(s.get(), strlen(s.get()));
		_error = ini_parse_string(content.c_str(), ValueHandler, this);
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

std::string INIReader::Get(const std::string& section, const std::string& name, const std::string& default_value) const
{
	std::string s = toLowerAscii(section);
	std::string sn = toLowerAscii(name);

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
	std::string s = toLowerAscii(section);
	std::string sn = toLowerAscii(name);

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

void INIReader::Remove(const std::string& section, const std::string& name)
{
	std::string normalizedSection = toLowerAscii(section);
	std::string normalizedName = toLowerAscii(name);

	auto sectionIterator = map.sections.find(normalizedSection);
	if (sectionIterator == map.sections.end())
	{
		return;
	}

	sectionIterator->second.keys.erase(normalizedName);
}

bool INIReader::HasSection(const std::string& section) const
{
	return map.sections.find(toLowerAscii(section)) != map.sections.end();
}

std::vector<std::string> INIReader::GetSectionNames() const
{
	std::vector<std::string> sectionNames;
	sectionNames.reserve(map.sections.size());
	for (const auto& section : map.sections)
	{
		sectionNames.push_back(section.first);
	}
	return sectionNames;
}

std::vector<std::string> INIReader::GetSectionKeys(const std::string& section) const
{
	std::string normalizedSection = toLowerAscii(section);

	std::vector<std::string> keyNames;
	auto sectionIterator = map.sections.find(normalizedSection);
	if (sectionIterator == map.sections.end())
	{
		return keyNames;
	}

	keyNames.reserve(sectionIterator->second.keys.size());
	for (const auto& key : sectionIterator->second.keys)
	{
		keyNames.push_back(key.first);
	}
	return keyNames;
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
	unsigned char colorData[4] =
	{
		(unsigned char)((value & 0xFF0000) >> 16),
		(unsigned char)((value & 0xFF00) >> 8),
		(unsigned char)(value & 0xFF),
		(unsigned char)((value & 0xFF000000) >> 24)
	};
	std::string col = std::to_string(colorData[0]) + "," +
		std::to_string(colorData[1]) + "," +
		std::to_string(colorData[2]);
	if (colorData[3] != 0xFF)
	{
		col += "," + std::to_string(colorData[3]);
	}
	Set(section, name, col);
}

uint32_t INIReader::GetColor(const std::string & section, const std::string & name, uint32_t value)
{
	std::string col = Get(section, name, "");
	if (col.empty())
	{
		return value;
	}

	std::vector<std::string> c = convert::splitString(col, ",");
	if (c.size() == 1)
	{
		// Compatibility with early editor builds that incorrectly serialized a
		// packed ARGB integer instead of the runtime RGB/RGBA tuple.
		try
		{
			uint32_t packed = (uint32_t)std::stoul(c[0], nullptr, 0);
			return packed <= 0xFFFFFF ? 0xFF000000 | packed : packed;
		}
		catch (const std::exception&)
		{
			return value;
		}
	}

	unsigned char colorData[4] =
	{
		(unsigned char)((value & 0xFF0000) >> 16),
		(unsigned char)((value & 0xFF00) >> 8),
		(unsigned char)(value & 0xFF),
		0xFF
	};
	for (size_t i = 0; i < (c.size() > 4 ? 4 : c.size()); i++)
	{
		if (c[i].empty())
			continue;
		try
		{
			unsigned long component = std::stoul(c[i], nullptr, 0);
			if (component <= 0xFF)
			{
				colorData[i] = (unsigned char)component;
			}
		}
		catch (const std::exception&)
		{
			// Preserve the corresponding default component.
		}
	}
	return ((uint32_t)colorData[3] << 24) |
		((uint32_t)colorData[0] << 16) |
		((uint32_t)colorData[1] << 8) |
		(uint32_t)colorData[2];
}

UTime INIReader::GetTime(const std::string& section, const std::string& name, UTime default_value) const
{
	std::string valstr = Get(section, name, "");
	try
	{
		return (UTime)std::stoll(valstr, nullptr, 0);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
}

long INIReader::GetInteger(const std::string& section, const std::string& name, long default_value) const
{
	std::string valstr = Get(section, name, "");
	try
	{
		return std::stol(valstr, nullptr, 0);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
}

float INIReader::GetReal(const std::string& section, const std::string& name, float default_value) const
{
	std::string valstr = Get(section, name, "");
	try
	{
		return std::stod(valstr);
	}
	catch (const std::exception&)
	{
		return default_value;
	}
}

bool INIReader::GetBoolean(const std::string& section, const std::string& name, bool default_value) const
{
	std::string valstr = Get(section, name, "");
    // Convert to lower case to make std::string comparisons case-insensitive
	valstr = toLowerAscii(valstr);
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

bool INIReader::saveToFile(const std::string & filename)
{
	std::string s = saveToString();
	if (s.size() > static_cast<size_t>(INT_MAX))
	{
		return false;
	}
	return File::writeFileChecked(
		filename, s.data(), static_cast<int>(s.size()));
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

std::string INIReader::MakeKey(const std::string& section, const std::string& name)
{
	return toLowerAscii(section + "=" + name);
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
