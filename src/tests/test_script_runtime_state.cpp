#include "../File/INIReader.h"
#include "../Game/GameManager/ScriptRuntimeState.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

int main()
{
	bool ok = true;

	ok = check(shouldTriggerTimeScript(10, 10, 10), "time script triggers when set at current second") && ok;
	ok = check(shouldTriggerTimeScript(6, 5, 5), "time script triggers when countdown reaches trigger") && ok;
	ok = check(shouldTriggerTimeScript(6, 4, 5), "time script triggers when frame crosses trigger") && ok;
	ok = check(shouldTriggerTimeScript(1, 0, 0), "time script triggers at zero") && ok;
	ok = check(!shouldTriggerTimeScript(4, 3, 5), "time script does not trigger after trigger was already missed") && ok;
	ok = check(!shouldTriggerTimeScript(10, 9, -1), "negative trigger is ignored") && ok;

	INIReader timerIni;
	ScriptRuntimeTimerState savedTimer;
	savedTimer.timerStarted = true;
	savedTimer.timerHidden = true;
	savedTimer.timerSeconds = 12;
	savedTimer.timerAccumulatedMilliseconds = 1456;
	savedTimer.timeScriptSet = true;
	savedTimer.timeScriptSeconds = 5;
	savedTimer.timeScriptFileName = "deadline.lua";
	writeScriptRuntimeTimerState(timerIni, savedTimer);

	ScriptRuntimeTimerState loadedTimer = readScriptRuntimeTimerState(timerIni);
	ok = check(loadedTimer.timerStarted, "timer state preserves active timer") && ok;
	ok = check(loadedTimer.timerHidden, "timer state preserves hidden window") && ok;
	ok = check(loadedTimer.timerSeconds == 12, "timer state preserves remaining seconds") && ok;
	ok = check(loadedTimer.timerAccumulatedMilliseconds == 456, "timer state clamps accumulated milliseconds") && ok;
	ok = check(loadedTimer.timeScriptSet, "timer state preserves script trigger") && ok;
	ok = check(loadedTimer.timeScriptSeconds == 5, "timer state preserves trigger second") && ok;
	ok = check(loadedTimer.timeScriptFileName == "deadline.lua", "timer state preserves script file") && ok;

	INIReader legacyNumberIni;
	legacyNumberIni.Set("Numbers", "SemicolonSuffix", "12;");
	ok = check(legacyNumberIni.GetInteger("Numbers", "SemicolonSuffix", 77) == 12,
		"general INI integers preserve legacy semicolon-suffix compatibility") && ok;

	INIReader invalidTimerIni;
	invalidTimerIni.SetBoolean("Timer", "IsOn", true);
	invalidTimerIni.Set("Timer", "TotalSecond", "12junk");
	invalidTimerIni.Set("Timer", "AccumulatedMilliseconds", "-1");
	invalidTimerIni.SetBoolean("Timer", "IsScriptSet", true);
	invalidTimerIni.Set("Timer", "TimerScript", "deadline.lua");
	invalidTimerIni.Set("Timer", "TriggerTime", "5seconds");
	ScriptRuntimeTimerState invalidTimer = readScriptRuntimeTimerState(invalidTimerIni);
	ok = check(invalidTimer.timerStarted && invalidTimer.timeScriptSet,
		"invalid runtime numbers are exercised on an active timer state") && ok;
	ok = check(invalidTimer.timerSeconds == 0 && invalidTimer.timerAccumulatedMilliseconds == 0,
		"runtime timer rejects trailing garbage and negative unsigned time") && ok;
	ok = check(invalidTimer.timeScriptSeconds == 0,
		"runtime timer rejects a trigger value with trailing garbage") && ok;

	INIReader parallelIni;
	std::vector<ParallelScriptRuntimeState> savedParallel = {
		{ "later.lua", "m001", 2500 },
		{ "now.lua", "", 0 },
		{ "", "ignored", 99 },
	};
	writeParallelScriptRuntimeStates(parallelIni, savedParallel);
	std::vector<ParallelScriptRuntimeState> loadedParallel = readParallelScriptRuntimeStates(parallelIni);
	ok = check(loadedParallel.size() == 2, "parallel state skips empty script names") && ok;
	if (loadedParallel.size() == 2)
	{
		ok = check(loadedParallel[0].scriptName == "later.lua", "parallel state preserves first script") && ok;
		ok = check(loadedParallel[0].scriptMapName == "m001", "parallel state preserves map name") && ok;
		ok = check(loadedParallel[0].remainingMilliseconds == 2500, "parallel state preserves delay") && ok;
		ok = check(loadedParallel[1].scriptName == "now.lua", "parallel state preserves immediate script") && ok;
		ok = check(loadedParallel[1].remainingMilliseconds == 0, "parallel state preserves zero delay") && ok;
	}

	INIReader invalidStructuredIni;
	invalidStructuredIni.Set("ParallelScript", "Count", "2147483647");
	invalidStructuredIni.Set("ParallelScript", "Script0", "must-not-load.lua");
	ok = check(readParallelScriptRuntimeStates(invalidStructuredIni).empty(),
		"structured parallel state rejects huge count before allocation") && ok;

	INIReader invalidCountTextIni;
	invalidCountTextIni.Set("ParallelScript", "Count", "1entry");
	invalidCountTextIni.Set("ParallelScript", "Script0", "must-not-load.lua");
	ok = check(readParallelScriptRuntimeStates(invalidCountTextIni).empty(),
		"structured parallel state rejects count trailing garbage") && ok;

	INIReader invalidDelayIni;
	invalidDelayIni.SetInteger("ParallelScript", "Count", 2);
	invalidDelayIni.Set("ParallelScript", "Script0", "negative-delay.lua");
	invalidDelayIni.Set("ParallelScript", "Delay0", "-1");
	invalidDelayIni.Set("ParallelScript", "Script1", "garbage-delay.lua");
	invalidDelayIni.Set("ParallelScript", "Delay1", "20ms");
	std::vector<ParallelScriptRuntimeState> invalidDelayStates =
		readParallelScriptRuntimeStates(invalidDelayIni);
	ok = check(invalidDelayStates.size() == 2,
		"invalid delay values do not discard otherwise valid scripts") && ok;
	if (invalidDelayStates.size() == 2)
	{
		ok = check(invalidDelayStates[0].remainingMilliseconds == 0,
			"structured parallel state rejects negative delay") && ok;
		ok = check(invalidDelayStates[1].remainingMilliseconds == 0,
			"structured parallel state rejects delay trailing garbage") && ok;
	}

	INIReader legacyIni;
	legacyIni.Set("ParallelScript", "1", "script/common/second.lua:0");
	legacyIni.Set("ParallelScript", "0", "script\\map\\map001\\first.lua:300");
	legacyIni.Set("ParallelScript", "2", "script\\goods\\third.lua:25");
	std::vector<ParallelScriptRuntimeState> legacyParallel = readParallelScriptRuntimeStates(legacyIni);
	ok = check(legacyParallel.size() == 3, "legacy C# parallel state reads numeric keys") && ok;
	if (legacyParallel.size() == 3)
	{
		ok = check(legacyParallel[0].scriptName == "first.lua", "legacy C# parallel state sorts numeric keys") && ok;
		ok = check(legacyParallel[0].scriptMapName == "map001", "legacy C# parallel state restores map script context") && ok;
		ok = check(legacyParallel[0].remainingMilliseconds == 300, "legacy C# parallel state parses delay") && ok;
		ok = check(legacyParallel[1].scriptName == "second.lua", "legacy C# parallel state reads second script") && ok;
		ok = check(legacyParallel[1].scriptMapName.empty(), "legacy C# parallel state clears common script context") && ok;
		ok = check(legacyParallel[2].scriptName == "third.lua", "legacy C# parallel state strips goods script folder") && ok;
		ok = check(legacyParallel[2].scriptMapName.empty(), "legacy C# parallel state clears goods script context") && ok;
		ok = check(legacyParallel[2].remainingMilliseconds == 25, "legacy C# parallel state reads goods delay") && ok;
	}

	INIReader boundedLegacyIni;
	boundedLegacyIni.Set("ParallelScript", std::string(4096, '9'), "script/common/oversized.lua:1");
	boundedLegacyIni.Set("ParallelScript", std::to_string(MaxParallelScriptStates),
		"script/common/out-of-range.lua:1");
	boundedLegacyIni.Set("ParallelScript", "0", "script/common/negative.lua:-1");
	boundedLegacyIni.Set("ParallelScript", "1", "script/common/garbage.lua:10ms");
	std::vector<ParallelScriptRuntimeState> boundedLegacyStates =
		readParallelScriptRuntimeStates(boundedLegacyIni);
	ok = check(boundedLegacyStates.size() == 2,
		"legacy parallel state skips oversized and out-of-range numeric keys") && ok;
	if (boundedLegacyStates.size() == 2)
	{
		ok = check(boundedLegacyStates[0].remainingMilliseconds == 0,
			"legacy parallel state rejects negative delay") && ok;
		ok = check(boundedLegacyStates[1].remainingMilliseconds == 0,
			"legacy parallel state rejects delay trailing garbage") && ok;
	}

	INIReader duplicateLegacyIni;
	for (std::size_t zeroCount = 0; zeroCount < MaxParallelScriptStates + 32; zeroCount++)
	{
		duplicateLegacyIni.Set("ParallelScript", std::string(zeroCount, '0') + "1",
			"script/common/duplicate.lua:1");
	}
	ok = check(readParallelScriptRuntimeStates(duplicateLegacyIni).size() == 1,
		"legacy parallel state deduplicates leading-zero aliases of one bounded index") && ok;

	std::vector<ParallelScriptRuntimeState> oversizedWriteStates;
	oversizedWriteStates.reserve(MaxParallelScriptStates + 2);
	for (std::size_t index = 0; index < MaxParallelScriptStates + 2; index++)
	{
		oversizedWriteStates.push_back({ "script-" + std::to_string(index) + ".lua", "", index });
	}
	INIReader boundedWriteIni;
	writeParallelScriptRuntimeStates(boundedWriteIni, oversizedWriteStates);
	ok = check(boundedWriteIni.GetInteger("ParallelScript", "Count", -1)
		== static_cast<long>(MaxParallelScriptStates),
		"parallel state writer caps structured count") && ok;
	ok = check(readParallelScriptRuntimeStates(boundedWriteIni).size() == MaxParallelScriptStates,
		"parallel state writer emits a readable bounded state list") && ok;
	ok = check(boundedWriteIni.Get("ParallelScript", "Script" + std::to_string(MaxParallelScriptStates), "").empty(),
		"parallel state writer does not emit entries past the cap") && ok;

	return ok ? 0 : 1;
}
