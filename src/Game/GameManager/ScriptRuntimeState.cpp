#include "ScriptRuntimeState.h"
#include "../../File/INIReader.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
#include <set>

namespace
{
const char* TimerSection = "Timer";
const char* ParallelScriptSection = "ParallelScript";

std::string indexedKey(const std::string& prefix, size_t index)
{
	return prefix + std::to_string(index);
}

bool parseBoundedLegacyIndex(const std::string& text, std::size_t& value)
{
	if (text.empty())
	{
		return false;
	}

	std::size_t parsedValue = 0;
	for (char ch : text)
	{
		if (!std::isdigit(static_cast<unsigned char>(ch)))
		{
			return false;
		}
		std::size_t digit = static_cast<std::size_t>(ch - '0');
		if (parsedValue > (MaxParallelScriptStates - 1 - digit) / 10)
		{
			return false;
		}
		parsedValue = parsedValue * 10 + digit;
	}
	value = parsedValue;
	return true;
}

bool hasOnlyTrailingWhitespace(const std::string& text, std::size_t position)
{
	for (; position < text.size(); position++)
	{
		if (!std::isspace(static_cast<unsigned char>(text[position])))
		{
			return false;
		}
	}
	return true;
}

bool parseNonNegativeTime(const std::string& text, UTime& value)
{
	std::size_t firstPosition = 0;
	while (firstPosition < text.size()
		&& std::isspace(static_cast<unsigned char>(text[firstPosition])))
	{
		firstPosition++;
	}
	if (firstPosition == text.size() || text[firstPosition] == '-')
	{
		return false;
	}

	try
	{
		std::size_t parsedLength = 0;
		unsigned long long parsedValue = std::stoull(text, &parsedLength, 0);
		if (!hasOnlyTrailingWhitespace(text, parsedLength)
			|| parsedValue > std::numeric_limits<UTime>::max())
		{
			return false;
		}
		value = static_cast<UTime>(parsedValue);
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool parseStrictLong(const std::string& text, long& value)
{
	try
	{
		std::size_t parsedLength = 0;
		long parsedValue = std::stol(text, &parsedLength, 0);
		if (!hasOnlyTrailingWhitespace(text, parsedLength))
		{
			return false;
		}
		value = parsedValue;
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

std::string toLowerAscii(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
	return text;
}

void normalizeLegacyParallelScriptPath(ParallelScriptRuntimeState& state)
{
	std::replace(state.scriptName.begin(), state.scriptName.end(), '\\', '/');
	while (state.scriptName.rfind("./", 0) == 0)
	{
		state.scriptName.erase(0, 2);
	}

	std::string lowerName = toLowerAscii(state.scriptName);
	size_t scriptFolderPosition = lowerName.rfind("script/");
	if (scriptFolderPosition != std::string::npos)
	{
		state.scriptName = state.scriptName.substr(scriptFolderPosition);
		lowerName = lowerName.substr(scriptFolderPosition);
	}

	const std::string mapPrefix = "script/map/";
	const std::string commonPrefix = "script/common/";
	const std::string goodsPrefix = "script/goods/";
	if (lowerName.rfind(mapPrefix, 0) == 0)
	{
		std::string rest = state.scriptName.substr(mapPrefix.size());
		size_t separator = rest.find('/');
		if (separator != std::string::npos)
		{
			state.scriptMapName = rest.substr(0, separator);
			state.scriptName = rest.substr(separator + 1);
		}
		return;
	}
	if (lowerName.rfind(commonPrefix, 0) == 0)
	{
		state.scriptMapName.clear();
		state.scriptName = state.scriptName.substr(commonPrefix.size());
		return;
	}
	if (lowerName.rfind(goodsPrefix, 0) == 0)
	{
		state.scriptMapName.clear();
		state.scriptName = state.scriptName.substr(goodsPrefix.size());
		return;
	}
}

int readNonNegativeInteger(const INIReader& ini,
	const std::string& section,
	const std::string& key,
	int defaultValue)
{
	long value = 0;
	if (!parseStrictLong(ini.Get(section, key, ""), value) ||
		value < 0 || value > std::numeric_limits<int>::max())
	{
		return defaultValue;
	}
	return static_cast<int>(value);
}

bool readBoundedParallelScriptCount(const INIReader& ini, std::size_t& count)
{
	long parsedCount = -1;
	if (!parseStrictLong(ini.Get(ParallelScriptSection, "Count", ""), parsedCount) ||
		parsedCount < 0 || static_cast<std::size_t>(parsedCount) > MaxParallelScriptStates)
	{
		return false;
	}
	count = static_cast<std::size_t>(parsedCount);
	return true;
}

UTime readNonNegativeTime(const INIReader& ini,
	const std::string& section,
	const std::string& key,
	UTime defaultValue)
{
	UTime value = 0;
	if (!parseNonNegativeTime(ini.Get(section, key, ""), value))
	{
		return defaultValue;
	}
	return value;
}

bool parseLegacyParallelScriptValue(const std::string& value, ParallelScriptRuntimeState& state)
{
	if (value.empty())
	{
		return false;
	}

	size_t separator = value.rfind(':');
	if (separator == std::string::npos)
	{
		state.scriptName = value;
		state.remainingMilliseconds = 0;
		normalizeLegacyParallelScriptPath(state);
		return !state.scriptName.empty();
	}

	state.scriptName = value.substr(0, separator);
	std::string delayText = value.substr(separator + 1);
	if (!parseNonNegativeTime(delayText, state.remainingMilliseconds))
	{
		state.remainingMilliseconds = 0;
	}
	normalizeLegacyParallelScriptPath(state);
	return !state.scriptName.empty();
}
}

bool shouldTriggerTimeScript(int previousSeconds, int currentSeconds, int triggerSeconds)
{
	if (triggerSeconds < 0)
	{
		return false;
	}
	return previousSeconds >= triggerSeconds && currentSeconds <= triggerSeconds;
}

ScriptRuntimeTimerState readScriptRuntimeTimerState(const INIReader& ini)
{
	ScriptRuntimeTimerState state;
	state.timerStarted = ini.GetBoolean(TimerSection, "IsOn", false);
	if (!state.timerStarted)
	{
		return state;
	}

	state.timerSeconds = readNonNegativeInteger(ini, TimerSection, "TotalSecond", 0);
	state.timerHidden = !ini.GetBoolean(TimerSection, "IsTimerWindowShow", true)
		|| ini.GetBoolean(TimerSection, "IsHidden", false);
	state.timerAccumulatedMilliseconds =
		readNonNegativeTime(ini, TimerSection, "AccumulatedMilliseconds", 0) % 1000;

	state.timeScriptSet = ini.GetBoolean(TimerSection, "IsScriptSet", false);
	state.timeScriptFileName = ini.Get(TimerSection, "TimerScript", "");
	state.timeScriptSeconds = readNonNegativeInteger(ini, TimerSection, "TriggerTime", 0);
	if (state.timeScriptFileName.empty())
	{
		state.timeScriptSet = false;
	}
	return state;
}

void writeScriptRuntimeTimerState(INIReader& ini, const ScriptRuntimeTimerState& state)
{
	ini.SetBoolean(TimerSection, "IsOn", state.timerStarted);
	ini.SetInteger(TimerSection, "TotalSecond", std::max(0, state.timerSeconds));
	ini.SetBoolean(TimerSection, "IsTimerWindowShow", state.timerStarted && !state.timerHidden);
	ini.SetBoolean(TimerSection, "IsScriptSet", state.timerStarted && state.timeScriptSet);
	ini.Set(TimerSection, "TimerScript", state.timeScriptFileName);
	ini.SetInteger(TimerSection, "TriggerTime", std::max(0, state.timeScriptSeconds));
	ini.SetTime(TimerSection, "AccumulatedMilliseconds", state.timerAccumulatedMilliseconds % 1000);
}

std::vector<ParallelScriptRuntimeState> readParallelScriptRuntimeStates(const INIReader& ini)
{
	std::vector<ParallelScriptRuntimeState> states;

	const std::vector<std::string> sectionKeys = ini.GetSectionKeys(ParallelScriptSection);
	const bool hasStructuredCount =
		std::find(sectionKeys.begin(), sectionKeys.end(), "count") != sectionKeys.end();
	if (hasStructuredCount)
	{
		std::size_t count = 0;
		if (!readBoundedParallelScriptCount(ini, count))
		{
			return states;
		}
		states.reserve(count);
		for (std::size_t i = 0; i < count; i++)
		{
			ParallelScriptRuntimeState state;
			state.scriptName = ini.Get(ParallelScriptSection, indexedKey("Script", i), "");
			state.scriptMapName = ini.Get(ParallelScriptSection, indexedKey("Map", i), "");
			state.remainingMilliseconds =
				readNonNegativeTime(ini, ParallelScriptSection, indexedKey("Delay", i), 0);
			if (!state.scriptName.empty())
			{
				states.push_back(state);
			}
		}
		return states;
	}

	std::vector<std::pair<std::size_t, std::string>> legacyKeys;
	legacyKeys.reserve(std::min(sectionKeys.size(), MaxParallelScriptStates));
	std::set<std::size_t> legacyIndexes;
	for (const auto& key : sectionKeys)
	{
		std::size_t index = 0;
		if (parseBoundedLegacyIndex(key, index) && legacyIndexes.insert(index).second)
		{
			legacyKeys.push_back({ index, key });
			if (legacyKeys.size() == MaxParallelScriptStates)
			{
				break;
			}
		}
	}
	std::sort(legacyKeys.begin(), legacyKeys.end());

	for (const auto& key : legacyKeys)
	{
		ParallelScriptRuntimeState state;
		if (parseLegacyParallelScriptValue(ini.Get(ParallelScriptSection, key.second, ""), state))
		{
			states.push_back(state);
		}
	}
	return states;
}

void writeParallelScriptRuntimeStates(INIReader& ini, const std::vector<ParallelScriptRuntimeState>& states)
{
	std::vector<ParallelScriptRuntimeState> validStates;
	validStates.reserve(std::min(states.size(), MaxParallelScriptStates));
	for (const auto& state : states)
	{
		if (!state.scriptName.empty())
		{
			validStates.push_back(state);
			if (validStates.size() == MaxParallelScriptStates)
			{
				break;
			}
		}
	}

	ini.SetInteger(ParallelScriptSection, "Count", static_cast<long>(validStates.size()));
	for (size_t i = 0; i < validStates.size(); i++)
	{
		ini.Set(ParallelScriptSection, indexedKey("Script", i), validStates[i].scriptName);
		ini.Set(ParallelScriptSection, indexedKey("Map", i), validStates[i].scriptMapName);
		ini.SetTime(ParallelScriptSection, indexedKey("Delay", i), validStates[i].remainingMilliseconds);
	}
}
