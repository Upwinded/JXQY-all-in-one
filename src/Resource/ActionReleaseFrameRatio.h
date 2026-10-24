#pragma once

#include <charconv>
#include <string>
#include <string_view>

struct ActionReleaseFrameRatio
{
	int numerator;
	int denominator;
};

inline bool tryParseActionReleaseFrameRatio(
	const std::string& text, ActionReleaseFrameRatio& value)
{
	const auto parsePositiveInteger = [](std::string_view part, int& parsed)
	{
		const auto first = part.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos)
		{
			return false;
		}
		part = part.substr(first, part.find_last_not_of(" \t\r\n") - first + 1);
		const auto result = std::from_chars(part.data(), part.data() + part.size(), parsed);
		return result.ec == std::errc() && result.ptr == part.data() + part.size() && parsed > 0;
	};
	const auto separator = text.find('/');
	if (separator == std::string::npos)
	{
		return false;
	}
	ActionReleaseFrameRatio parsed{};
	const std::string_view view(text);
	if (!parsePositiveInteger(view.substr(0, separator), parsed.numerator)
		|| !parsePositiveInteger(view.substr(separator + 1), parsed.denominator)
		|| parsed.numerator > parsed.denominator)
	{
		return false;
	}
	value = parsed;
	return true;
}

inline std::string actionReleaseFrameRatioToString(const ActionReleaseFrameRatio& value)
{
	return std::to_string(value.numerator) + "/" + std::to_string(value.denominator);
}
