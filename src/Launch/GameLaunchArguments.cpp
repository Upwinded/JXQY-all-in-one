#include "GameLaunchArguments.h"

#include <cstddef>
#include <string>

namespace
{
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
bool parseIntegerArgument(const std::string& text, int& value)
{
	try
	{
		std::size_t processed = 0;
		const int parsedValue = std::stoi(text, &processed, 10);
		if (processed != text.size())
		{
			return false;
		}
		value = parsedValue;
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool parseStartupIntegerVariable(
	const std::string& text, std::pair<std::string, int>& variable)
{
	const std::size_t separator = text.find('=');
	if (separator == std::string::npos || separator == 0 ||
		separator + 1 >= text.size())
	{
		return false;
	}

	int value = 0;
	if (!parseIntegerArgument(text.substr(separator + 1), value))
	{
		return false;
	}

	variable = { text.substr(0, separator), value };
	return true;
}
#endif

bool isLegacyArgumentName(const std::string& argument)
{
	return argument == "-lf" ||
		argument == "--log-file" ||
		argument == "--assets" ||
		argument == "--user-data-root" ||
		argument == "--resource-id" ||
		argument == "--pack-id" ||
		argument == "--skip-startup-video" ||
		argument == "--skip-startup-videos" ||
		argument == "--probe-resource"
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
		||
		argument == "--enable-automation-hooks" ||
		argument == "--newgame" ||
		argument == "--run-newgame" ||
		argument == "--open-test-runner" ||
		argument == "--exit-after-newgame-script" ||
		argument == "--post-newgame-wait-ms" ||
		argument == "--startup-int" ||
		argument == "--expect-int" ||
		argument == "--test-scenario-choice" ||
		argument == "--test-scenario"
#endif
		;
}

void setError(
	GameLaunch::Arguments& result,
	GameLaunch::ArgumentError error,
	std::string message)
{
	if (result.error != GameLaunch::ArgumentError::None)
	{
		return;
	}
	result.error = error;
	result.errorMessage = std::move(message);
}
}

namespace GameLaunch
{
Arguments parseArguments(
	int argc, const char* const argv[], bool editorRunEnabled)
{
	Arguments result;
	bool sawEditorRun = false;
	bool sawOtherArgument = false;
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
	bool restrictedAutomationArgumentRequested = false;
#endif

	for (int index = 0; index < argc; ++index)
	{
		const std::string argument =
			argv != nullptr && argv[index] != nullptr ? argv[index] : "";
		// The historical parser examined argv[0]. Preserve that edge behavior
		// only when the executable token itself is an existing legacy option;
		// an ordinary executable path remains outside the argument set.
		if (index == 0 && !isLegacyArgumentName(argument))
		{
			continue;
		}

		if (editorRunEnabled && argument == "--editor-run")
		{
			if (sawEditorRun)
			{
				setError(
					result,
					ArgumentError::DuplicateEditorRun,
					"Duplicate --editor-run argument");
				break;
			}
			if (index + 1 >= argc || argv[index + 1] == nullptr ||
				argv[index + 1][0] == '\0' || argv[index + 1][0] == '-')
			{
				setError(
					result,
					ArgumentError::MissingValue,
					"Missing value for --editor-run");
				break;
			}
			sawEditorRun = true;
			result.editorRunDescriptorPath = argv[++index];
			continue;
		}

		sawOtherArgument = true;
		if (argument == "-lf")
		{
			result.legacy.useLogFile = true;
		}
		else if (argument == "--log-file")
		{
			result.legacy.useLogFile = true;
			if (index + 1 < argc && argv[index + 1] != nullptr &&
				argv[index + 1][0] != '-')
			{
				result.legacy.logFilePath = argv[++index];
			}
		}
		else if (argument == "--assets")
		{
			if (index + 1 >= argc || argv[index + 1] == nullptr ||
				argv[index + 1][0] == '\0' || argv[index + 1][0] == '-')
			{
				setError(
					result,
					ArgumentError::MissingValue,
					"Missing value for --assets");
				break;
			}
			result.legacy.assetsPath = argv[++index];
		}
		else if (argument == "--user-data-root")
		{
			if (index + 1 >= argc || argv[index + 1] == nullptr ||
				argv[index + 1][0] == '\0' || argv[index + 1][0] == '-')
			{
				setError(
					result,
					ArgumentError::MissingValue,
					"Missing value for --user-data-root");
				break;
			}
			result.legacy.userDataRootPath = argv[++index];
		}
		else if (argument == "--resource-id" || argument == "--pack-id")
		{
			if (index + 1 >= argc || argv[index + 1] == nullptr ||
				argv[index + 1][0] == '\0' || argv[index + 1][0] == '-')
			{
				setError(
					result,
					ArgumentError::MissingValue,
					"Missing value for " + argument);
				break;
			}
			result.legacy.resourcePackId = argv[++index];
		}
		else if (argument == "--skip-startup-video" ||
			argument == "--skip-startup-videos")
		{
			result.legacy.skipStartupVideos = true;
		}
		else if (argument == "--probe-resource")
		{
			result.legacy.probeResource = true;
		}
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
		else if (argument == "--enable-automation-hooks")
		{
			result.legacy.automationHooksEnabled = true;
		}
		else if (argument == "--newgame" ||
			argument == "--run-newgame")
		{
			result.legacy.autoStartNewGame = true;
		}
		else if (argument == "--open-test-runner")
		{
			restrictedAutomationArgumentRequested = true;
			result.legacy.autoStartNewGame = true;
		}
		else if (argument == "--exit-after-newgame-script")
		{
			result.legacy.autoStartNewGame = true;
			result.legacy.exitAfterNewGameScript = true;
		}
		else if (argument == "--post-newgame-wait-ms" &&
			index + 1 < argc)
		{
			int waitMilliseconds = 0;
			const std::string value =
				argv[index + 1] != nullptr ? argv[index + 1] : "";
			if (!parseIntegerArgument(value, waitMilliseconds) ||
				waitMilliseconds < 0)
			{
				setError(
					result,
					ArgumentError::InvalidValue,
					"Invalid --post-newgame-wait-ms value, expected "
					"non-negative integer: " + value);
				break;
			}
			result.legacy.postNewGameAutomationWaitMilliseconds =
				waitMilliseconds;
			result.legacy.autoStartNewGame = true;
			result.legacy.exitAfterNewGameScript = true;
			++index;
		}
		else if (argument == "--startup-int" && index + 1 < argc)
		{
			std::pair<std::string, int> variable;
			const std::string value =
				argv[index + 1] != nullptr ? argv[index + 1] : "";
			if (!parseStartupIntegerVariable(value, variable))
			{
				setError(
					result,
					ArgumentError::InvalidValue,
					"Invalid --startup-int value, expected name=integer: " +
					value);
				break;
			}
			restrictedAutomationArgumentRequested = true;
			result.legacy.startupIntegerVariables.push_back(variable);
			++index;
		}
		else if (argument == "--expect-int" && index + 1 < argc)
		{
			std::pair<std::string, int> variable;
			const std::string value =
				argv[index + 1] != nullptr ? argv[index + 1] : "";
			if (!parseStartupIntegerVariable(value, variable))
			{
				setError(
					result,
					ArgumentError::InvalidValue,
					"Invalid --expect-int value, expected name=integer: " +
					value);
				break;
			}
			restrictedAutomationArgumentRequested = true;
			result.legacy.expectedIntegerVariables.push_back(variable);
			result.legacy.autoStartNewGame = true;
			++index;
		}
		else if ((argument == "--test-scenario-choice" ||
			argument == "--test-scenario") && index + 1 < argc)
		{
			int scenarioChoice = 0;
			const std::string value =
				argv[index + 1] != nullptr ? argv[index + 1] : "";
			if (!parseIntegerArgument(value, scenarioChoice) ||
				scenarioChoice < 0)
			{
				setError(
					result,
					ArgumentError::InvalidValue,
					"Invalid --test-scenario-choice value, expected "
					"non-negative integer: " + value);
				break;
			}
			restrictedAutomationArgumentRequested = true;
			result.legacy.startupIntegerVariables.push_back(
				{ "mod_test_auto_scenario_choice", scenarioChoice });
			result.legacy.startupIntegerVariables.push_back(
				{ "mod_test_auto_scenario_enabled", 1 });
			result.legacy.autoStartNewGame = true;
			++index;
		}
		else if (argument == "--startup-int" ||
			argument == "--expect-int" ||
			argument == "--test-scenario-choice" ||
			argument == "--test-scenario" ||
			argument == "--post-newgame-wait-ms")
		{
			setError(
				result,
				ArgumentError::MissingValue,
				"Missing value for " + argument);
			break;
		}
#endif
	}

	if (!result.succeeded())
	{
		return result;
	}
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
	if (restrictedAutomationArgumentRequested &&
		!result.legacy.automationHooksEnabled)
	{
		setError(
			result,
			ArgumentError::UnauthorizedAutomation,
			"Test automation arguments require "
			"--enable-automation-hooks");
		return result;
	}
#endif
	if (sawEditorRun && sawOtherArgument)
	{
		setError(
			result,
			ArgumentError::MixedEditorRunArguments,
			"--editor-run cannot be combined with other arguments");
		return result;
	}
	if (sawEditorRun)
	{
		result.mode = ArgumentMode::EditorRun;
	}
	return result;
}
}
