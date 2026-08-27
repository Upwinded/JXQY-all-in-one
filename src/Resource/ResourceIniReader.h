#pragma once

#include "../File/ResourcePathSafety.h"
#include "../File/ini.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Minimal, read-only INI view used by the exact resource selector and
// ResourceManifest's in-memory parser. It deliberately has no File singleton,
// logging, save, or host-path dependency so desktop tools can link the exact
// selector without pulling in the game runtime.
class ResourceIniReader
{
public:
	ResourceIniReader(const char* data, std::size_t length)
	{
		if (data == nullptr ||
			length == 0 ||
			std::memchr(data, '\0', length) != nullptr)
		{
			parseErrorValue = -1;
			return;
		}
		const std::string text = normalizeTextEncoding(data, length);
		parseErrorValue = ini_parse_string(
			text.c_str(),
			&ResourceIniReader::valueHandler,
			this);
	}

	int parseError() const noexcept
	{
		return parseErrorValue;
	}

	std::string get(
		const std::string& section,
		const std::string& name,
		const std::string& defaultValue) const
	{
		const auto sectionIterator =
			sections.find(foldAsciiCase(section));
		if (sectionIterator == sections.end())
		{
			return defaultValue;
		}
		const auto valueIterator =
			sectionIterator->second.find(foldAsciiCase(name));
		return valueIterator == sectionIterator->second.end()
			? defaultValue
			: valueIterator->second;
	}

	long getInteger(
		const std::string& section,
		const std::string& name,
		long defaultValue) const
	{
		try
		{
			return std::stol(get(section, name, ""), nullptr, 0);
		}
		catch (...)
		{
			return defaultValue;
		}
	}

	bool getBoolean(
		const std::string& section,
		const std::string& name,
		bool defaultValue) const
	{
		const std::string value =
			foldAsciiCase(get(section, name, ""));
		if (value == "true" || value == "yes" ||
			value == "on" || value == "1")
		{
			return true;
		}
		if (value == "false" || value == "no" ||
			value == "off" || value == "0")
		{
			return false;
		}
		return defaultValue;
	}

	bool hasKey(
		const std::string& section,
		const std::string& name) const
	{
		const auto sectionIterator =
			sections.find(foldAsciiCase(section));
		return sectionIterator != sections.end() &&
			sectionIterator->second.find(foldAsciiCase(name)) !=
				sectionIterator->second.end();
	}

	std::vector<std::string> sectionNames() const
	{
		std::vector<std::string> names;
		names.reserve(sections.size());
		for (const auto& section : sections)
		{
			names.push_back(section.first);
		}
		return names;
	}

	std::vector<std::string> sectionKeys(
		const std::string& section) const
	{
		std::vector<std::string> names;
		const auto sectionIterator =
			sections.find(foldAsciiCase(section));
		if (sectionIterator == sections.end())
		{
			return names;
		}
		names.reserve(sectionIterator->second.size());
		for (const auto& value : sectionIterator->second)
		{
			names.push_back(value.first);
		}
		return names;
	}

private:
	using Section = std::map<std::string, std::string>;
	std::map<std::string, Section> sections;
	int parseErrorValue = -1;

	static std::string foldAsciiCase(std::string value)
	{
		for (char& character : value)
		{
			if (character >= 'A' && character <= 'Z')
			{
				character = static_cast<char>(
					character + ('a' - 'A'));
			}
		}
		return value;
	}

	static std::string convertGbkToUtf8(const std::string& text)
	{
#if defined(_WIN32)
		if (text.empty())
		{
			return text;
		}
		const int wideLength = MultiByteToWideChar(
			936,
			0,
			text.data(),
			static_cast<int>(text.size()),
			nullptr,
			0);
		if (wideLength <= 0)
		{
			return {};
		}
		std::wstring wideText(
			static_cast<std::size_t>(wideLength),
			L'\0');
		if (MultiByteToWideChar(
				936,
				0,
				text.data(),
				static_cast<int>(text.size()),
				wideText.data(),
				wideLength) != wideLength)
		{
			return {};
		}
		const int utf8Length = WideCharToMultiByte(
			CP_UTF8,
			0,
			wideText.data(),
			wideLength,
			nullptr,
			0,
			nullptr,
			nullptr);
		if (utf8Length <= 0)
		{
			return {};
		}
		std::string utf8(
			static_cast<std::size_t>(utf8Length),
			'\0');
		if (WideCharToMultiByte(
				CP_UTF8,
				0,
				wideText.data(),
				wideLength,
				utf8.data(),
				utf8Length,
				nullptr,
				nullptr) != utf8Length)
		{
			return {};
		}
		return utf8;
#else
		(void)text;
		return {};
#endif
	}

	static std::string normalizeTextEncoding(
		const char* data,
		std::size_t length)
	{
		const std::string text(data, length);
		if (ResourcePathSafety::isValidUtf8(text))
		{
			return text;
		}
		const std::string converted = convertGbkToUtf8(text);
		return !converted.empty() &&
			ResourcePathSafety::isValidUtf8(converted)
			? converted
			: text;
	}

	static int valueHandler(
		void* user,
		const char* section,
		const char* name,
		const char* value)
	{
		auto* reader =
			static_cast<ResourceIniReader*>(user);
		reader->sections[foldAsciiCase(section)]
			[foldAsciiCase(name)] = value;
		return 1;
	}
};
