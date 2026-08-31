#include "MemoPersistence.h"
#include "../../File/INIReader.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <utility>

namespace
{
std::string sanitizeLine(const std::string& line)
{
	std::string sanitized = line;
	std::replace_if(
		sanitized.begin(),
		sanitized.end(),
		[](char character)
		{
			return character == '\r' || character == '\n' ||
				character == '\0';
		},
		' ');
	return sanitized;
}

std::string trimAscii(const std::string& value)
{
	size_t begin = 0;
	while (begin < value.size() &&
		(value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n'))
	{
		begin++;
	}
	size_t end = value.size();
	while (end > begin &&
		(value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
	{
		end--;
	}
	return value.substr(begin, end - begin);
}

bool parseLineCount(const std::string& text, int& count)
{
	const std::string trimmed = trimAscii(text);
	if (trimmed.empty())
	{
		return false;
	}
	char* end = nullptr;
	long parsed = std::strtol(trimmed.c_str(), &end, 10);
	if (end == trimmed.c_str() || *end != '\0' || parsed < 0 ||
		parsed > MemoPersistence::MaximumLineCount || parsed > INT_MAX)
	{
		return false;
	}
	count = static_cast<int>(parsed);
	return true;
}
}

namespace MemoPersistence
{
bool parseText(const std::string& text, std::deque<std::string>& lines)
{
	lines.clear();
	if (text.empty() ||
		text.find('\0') != std::string::npos)
	{
		return false;
	}

	auto buffer = std::make_unique<char[]>(text.size() + 1);
	std::memcpy(buffer.get(), text.data(), text.size());
	buffer[text.size()] = '\0';
	INIReader ini(buffer);
	if (ini.ParseError() != 0)
	{
		return false;
	}

	int count = 0;
	if (!parseLineCount(ini.Get("Memo", "Count", ""), count))
	{
		return false;
	}

	for (int index = 0; index < count; index++)
	{
		lines.push_back(ini.Get("Memo", std::to_string(index), ""));
	}
	return true;
}

ContentKind classifyText(
	const std::string& text,
	std::deque<std::string>* memoLines)
{
	std::deque<std::string> parsedMemo;
	if (parseText(text, parsedMemo))
	{
		if (memoLines != nullptr)
		{
			*memoLines = std::move(parsedMemo);
		}
		return ContentKind::Memo;
	}
	if (memoLines != nullptr)
	{
		memoLines->clear();
	}
	if (text.empty() ||
		text.find('\0') != std::string::npos)
	{
		return ContentKind::Invalid;
	}

	auto buffer = std::make_unique<char[]>(text.size() + 1);
	std::memcpy(buffer.get(), text.data(), text.size());
	buffer[text.size()] = '\0';
	INIReader ini(buffer);
	if (ini.ParseError() == 0 &&
		!ini.HasSection("Memo"))
	{
		return ContentKind::OtherIni;
	}
	return ContentKind::Invalid;
}

std::string serializeText(const std::deque<std::string>& lines)
{
	const size_t count = std::min(
		lines.size(), static_cast<size_t>(MaximumLineCount));
	std::ostringstream output;
	output << "[Memo]\r\n";
	output << "Count=" << count << "\r\n";
	for (size_t index = 0; index < count; index++)
	{
		output << index << "=" << sanitizeLine(lines[index]) << "\r\n";
	}
	return output.str();
}

}
