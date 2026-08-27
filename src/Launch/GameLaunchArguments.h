#pragma once

#include <string>
#include <utility>
#include <vector>

namespace GameLaunch
{
enum class ArgumentMode
{
	Legacy,
	EditorRun
};

enum class ArgumentError
{
	None,
	MissingValue,
	InvalidValue,
	UnauthorizedAutomation,
	DuplicateEditorRun,
	MixedEditorRunArguments
};

struct LegacyArguments
{
	bool useLogFile = false;
	std::string logFilePath;
	std::string assetsPath;
	// Root directory for mutable user data. Runtime writes save/ below it.
	// Relative values are resolved from the executable directory.
	std::string userDataRootPath;
	std::string resourcePackId;
	bool skipStartupVideos = false;
	bool probeResource = false;
	// Test-runner aliases plus startup/expect variable injection are rejected,
	// and ScriptAPI __automation_* consumers remain inert, unless this
	// process-owned capability is explicitly enabled at launch.
	bool automationHooksEnabled = false;
	// New-game automation is an explicit three-stage plan:
	//   autoStartNewGame enters the new-game flow;
	//   postNewGameAutomationWaitMilliseconds keeps real map updates running
	//   after NewGame.Script returns;
	//   exitAfterNewGameScript terminates after the optional wait.
	// --post-newgame-wait-ms implies both auto-start and eventual exit so the
	// option is deterministic when used on its own.
	bool autoStartNewGame = false;
	bool exitAfterNewGameScript = false;
	int postNewGameAutomationWaitMilliseconds = 0;
	std::vector<std::pair<std::string, int>> startupIntegerVariables;
	std::vector<std::pair<std::string, int>> expectedIntegerVariables;
};

struct Arguments
{
	ArgumentMode mode = ArgumentMode::Legacy;
	ArgumentError error = ArgumentError::None;
	std::string errorMessage;
	LegacyArguments legacy;
	std::string editorRunDescriptorPath;

	bool succeeded() const noexcept
	{
		return error == ArgumentError::None;
	}
};

// editorRunEnabled is false on Android/iOS. In that mode --editor-run keeps the
// existing unknown-argument behavior and does not expose the desktop protocol.
Arguments parseArguments(
	int argc, const char* const argv[], bool editorRunEnabled);
}
