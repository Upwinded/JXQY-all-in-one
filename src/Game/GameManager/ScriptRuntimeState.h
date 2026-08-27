#pragma once

#include "../../File/File.h"

#include <cstddef>
#include <string>
#include <vector>

class INIReader;

// Real resource packs currently schedule only a handful of parallel scripts.
// 1024 leaves ample headroom while bounding allocations from untrusted saves.
inline constexpr std::size_t MaxParallelScriptStates = 1024;

struct ScriptRuntimeTimerState
{
	bool timerStarted = false;
	bool timerHidden = false;
	int timerSeconds = 0;
	UTime timerAccumulatedMilliseconds = 0;
	bool timeScriptSet = false;
	int timeScriptSeconds = 0;
	std::string timeScriptFileName = "";
};

struct ParallelScriptRuntimeState
{
	std::string scriptName = "";
	std::string scriptMapName = "";
	UTime remainingMilliseconds = 0;
};

bool shouldTriggerTimeScript(int previousSeconds, int currentSeconds, int triggerSeconds);
ScriptRuntimeTimerState readScriptRuntimeTimerState(const INIReader& ini);
void writeScriptRuntimeTimerState(INIReader& ini, const ScriptRuntimeTimerState& state);
std::vector<ParallelScriptRuntimeState> readParallelScriptRuntimeStates(const INIReader& ini);
void writeParallelScriptRuntimeStates(INIReader& ini, const std::vector<ParallelScriptRuntimeState>& states);
