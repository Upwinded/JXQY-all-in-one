/*
	created by Upwinded@www.upwinded.com.

	Special thanks to Scarsty(SunTY, Weyl, BT, SB500), XiaoShiDaoJian, DaWuXiaLunTan(http://www.txdx.net), JianXiaQingYuanTieBa@tieba.baidu.com(https://tieba.baidu.com/f?kw=%E5%89%91%E4%BE%A0%E6%83%85%E7%BC%98&ie=utf-8).

	The source codes are distributed under zlib license, with two additional clauses:
	1.Full right of the codes is granted if they are used in non-KYS related and non-SHF related games.
	2.If the codes are used in KYS related or SHF related games, the game itself shall not involve any sort of profit making aspect.
	(KYS means Kam Yung's Stories, and SHF means Sword Heroes' Fate.)
*/

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "Game/Game.h"
#include "File/log.h"
#include "File/File.h"
#include "Launch/GameLaunchArguments.h"
#include "Resource/ResourceManager.h"
#include "SDL3/SDL_main.h"

#include <iostream>
#include <string>

#if defined(__MOBILE__) || defined(__ANDROID__) || \
	(defined(__APPLE__) && TARGET_OS_IOS)
#define JXQY_DESKTOP_EDITOR_RUN 0
#else
#define JXQY_DESKTOP_EDITOR_RUN 1
#include "Launch/EditorRunOrchestration.h"
#endif

namespace
{
constexpr int CommandLineUsageExitCode = 64;
}


#if defined( __ANDROID__ )
#include <jni.h>
int SDL_main(int argc, char* argv[])
//#elif (TARGET_OS_IOS)
//int main(int argc, char* argv[])
//{
//    return SDL_UIKitRunApp(argc, argv, SDL_main);
//}
//int SDL_main(int argc, char* argv[])
#elif defined(_WIN32) && !defined(_DEBUG)
int SDL_main(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
	const GameLaunch::Arguments launch =
		GameLaunch::parseArguments(
			argc,
			const_cast<const char* const*>(argv),
			JXQY_DESKTOP_EDITOR_RUN != 0);
	if (!launch.succeeded())
	{
		std::cerr << launch.errorMessage << "\n";
		return CommandLineUsageExitCode;
	}
#if JXQY_DESKTOP_EDITOR_RUN
	if (launch.mode == GameLaunch::ArgumentMode::EditorRun)
	{
		return EditorRun::runEditorRun(
			launch.editorRunDescriptorPath);
	}
#endif

	const GameLaunch::LegacyArguments& legacy = launch.legacy;
	if (!File::configureUserDataRoot(
			legacy.userDataRootPath,
			legacy.assetsPath))
	{
		std::cerr << "Cannot resolve user data root";
		if (!legacy.userDataRootPath.empty())
		{
			std::cerr << ": " << legacy.userDataRootPath;
		}
		std::cerr << "\n";
		return CommandLineUsageExitCode;
	}
	if (legacy.useLogFile)
	{
		GameLog::use_log_file = true;
		GameLog::setLogFilePath(legacy.logFilePath);
		GameLog::write("Use Log File");
	}
	if (legacy.probeResource)
	{
		auto& resourceManager = ResourceManager::instance();
		if (!resourceManager.initialize(legacy.assetsPath))
		{
			std::cerr << "Resource probe failed: ResourceManager initialize failed\n";
			return 2;
		}
		if (!legacy.resourcePackId.empty() &&
			!resourceManager.setActiveResourcePackById(
				legacy.resourcePackId))
		{
			std::cerr << "Resource probe failed: resource id was not found: "
				<< legacy.resourcePackId << "\n";
			return 3;
		}
		if (resourceManager.needsSelection())
		{
			std::cerr << "Resource probe failed: multiple resource packs require selection\n";
			return 4;
		}
		if (!resourceManager.hasActiveResourceRoot())
		{
			std::cerr << "Resource probe failed: no active resource pack\n";
			return 5;
		}
		const auto& manifest = resourceManager.getActiveManifest();
		std::cout << "ActiveResourceRoot=" << resourceManager.getActiveResourceRoot() << "\n";
		std::cout << "Id=" << manifest.id << "\n";
		std::cout << "Name=" << manifest.name << "\n";
		std::cout << "Save.Namespace=" << manifest.saveNamespace << "\n";
		std::cout << "Title.Menu=" << manifest.titleMenu << "\n";
		std::cout << "NewGame.Script=" << manifest.newGameScript << "\n";
		return 0;
	}
	Game game;
	game.setAssetsArg(legacy.assetsPath);
	game.setResourcePackIdArg(legacy.resourcePackId);
	game.setSkipStartupVideos(legacy.skipStartupVideos);
	game.setAutoStartNewGame(legacy.autoStartNewGame);
	game.setAutomationHooksEnabled(
		legacy.automationHooksEnabled);
	for (const auto& variable :
		legacy.startupIntegerVariables)
	{
		game.addStartupIntegerVariable(variable.first, variable.second);
	}
	for (const auto& variable :
		legacy.expectedIntegerVariables)
	{
		game.addExpectedIntegerVariable(variable.first, variable.second);
	}
	game.setExitAfterNewGameScript(
		legacy.exitAfterNewGameScript);
	game.setPostNewGameAutomationWaitMilliseconds(
		legacy.postNewGameAutomationWaitMilliseconds);
    auto ret = game.run();
#if TARGET_OS_IOS
    exit(ret);
#else
    return ret;
#endif
}

#undef JXQY_DESKTOP_EDITOR_RUN
