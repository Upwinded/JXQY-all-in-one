#pragma once

#include <string>
#include <initializer_list>

enum class MagicTimingMode
{
	FixedMilliseconds,
	LogicFrames,
	NativeAnimationFrames,
	CSharpAnimationFrames
};

inline const char* magicTimingModeToString(MagicTimingMode mode)
{
	switch (mode)
	{
	case MagicTimingMode::LogicFrames: return "LogicFrames";
	case MagicTimingMode::NativeAnimationFrames: return "NativeAnimationFrames";
	case MagicTimingMode::CSharpAnimationFrames: return "CSharpAnimationFrames";
	default: return "FixedMilliseconds";
	}
}

inline bool tryParseMagicTimingMode(std::string text, MagicTimingMode& mode)
{
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
	{
		return false;
	}
	text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	for (char& character : text)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character += 'a' - 'A';
		}
	}
	for (const auto candidate : { MagicTimingMode::FixedMilliseconds, MagicTimingMode::LogicFrames,
		MagicTimingMode::NativeAnimationFrames, MagicTimingMode::CSharpAnimationFrames })
	{
		std::string name = magicTimingModeToString(candidate);
		for (char& character : name)
		{
			if (character >= 'A' && character <= 'Z')
			{
				character += 'a' - 'A';
			}
		}
		if (text == name)
		{
			mode = candidate;
			return true;
		}
	}
	return false;
}
