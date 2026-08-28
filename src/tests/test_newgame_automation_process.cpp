#include "../Launch/GameLaunchArguments.h"

#include <iostream>

int main(int argc, char* argv[])
{
	const GameLaunch::Arguments arguments =
		GameLaunch::parseArguments(
			argc,
			const_cast<const char* const*>(argv),
			true);
	if (!arguments.succeeded())
	{
		std::cerr << arguments.errorMessage << "\n";
		return 64;
	}

	const GameLaunch::LegacyArguments& automation =
		arguments.legacy;
	std::cout
		<< "auto-start="
		<< (automation.autoStartNewGame ? 1 : 0)
		<< ";wait-ms="
		<< automation.postNewGameAutomationWaitMilliseconds
		<< ";exit="
		<< (automation.exitAfterNewGameScript ? 1 : 0)
		<< ";hooks="
		<< (automation.automationHooksEnabled ? 1 : 0)
		<< "\n";
	return 0;
}
