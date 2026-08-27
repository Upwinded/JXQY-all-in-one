#include "Game.h"
#include "../Engine/Engine.h"
#include "Config/Config.h"
#include "GameManager/GameManager.h"
#include "Menu/ControllerHelpOverlay.h"
#include "Menu/UIFocusManager.h"
#include "Menu/YesNo.h"
#include "Scene/MainScene.h"
#include "../File/File.h"
#include "../Input/GamepadConnectionObserver.h"
#include "../Input/InputAction.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/ResourceSelectScene.h"

namespace
{
GameInput::GamepadConnectionObserver controllerHelpConnectionObserver;
bool controllerHelpEnabled = false;
bool controllerHelpPending = false;
std::weak_ptr<ControllerHelpOverlay> activeControllerHelpOverlay;
std::uint64_t controllerHelpButtonRevision = 0;
constexpr char GameFontPath[] = "font/font.ttf";
constexpr char EngineFontPath[] = "engine/font/font.ttf";

class CloseConfirmationScope final
{
public:
	CloseConfirmationScope(
		bool& confirmationActive,
		Element& sceneRoot) :
		confirmationActive(confirmationActive),
		sceneRoot(sceneRoot)
	{
	}

	~CloseConfirmationScope() noexcept
	{
		if (confirmation != nullptr)
		{
			try
			{
				sceneRoot.removeChild(confirmation);
			}
			catch (...)
			{
			}
			confirmation = nullptr;
		}
		confirmationActive = false;
	}

	void attach(const PElement& attachedConfirmation) noexcept
	{
		confirmation = attachedConfirmation;
	}

private:
	bool& confirmationActive;
	Element& sceneRoot;
	PElement confirmation;
};

void resetControllerHelpState(bool enabled)
{
	controllerHelpConnectionObserver.reset();
	controllerHelpEnabled = enabled;
	controllerHelpPending = false;
	activeControllerHelpOverlay.reset();
	controllerHelpButtonRevision = 0;
}

bool canShowControllerHelp()
{
	GameManager* gameManager = GameManager::getInstance();
	return gameManager == nullptr || !gameManager->inThread.load();
}

class ControllerHelpRunScope final
{
public:
	explicit ControllerHelpRunScope(GameManager* gameManager) :
		gameManager(gameManager),
		resumeGameplayAfterHelp(
			gameManager != nullptr && !gameManager->isGameplayPaused())
	{
		if (resumeGameplayAfterHelp)
		{
			gameManager->setGameplayPaused(true);
		}
	}

	~ControllerHelpRunScope()
	{
		activeControllerHelpOverlay.reset();
		if (resumeGameplayAfterHelp
			&& GameManager::getInstance() == gameManager)
		{
			gameManager->setGameplayPaused(false);
		}
	}

private:
	GameManager* gameManager = nullptr;
	bool resumeGameplayAfterHelp = false;
};

void showControllerHelp()
{
	auto helpOverlay = std::make_shared<ControllerHelpOverlay>();
	activeControllerHelpOverlay = helpOverlay;
	Engine* engine = Engine::getInstance();
	if (engine != nullptr)
	{
		controllerHelpButtonRevision =
			engine->inputActions().gamepadButtonPressRevision();
	}

	ControllerHelpRunScope helpRunScope(GameManager::getInstance());
	(void)helpOverlay->run();
}

void handleGlobalInputActions(Engine* engine)
{
	if (engine == nullptr)
	{
		return;
	}

	const GameInput::PhysicalInputManager& inputManager =
		engine->inputActions();
	if (auto helpOverlay = activeControllerHelpOverlay.lock())
	{
		const std::uint64_t buttonRevision =
			inputManager.gamepadButtonPressRevision();
		if (Element::isCurrentRunOwner(helpOverlay.get())
			&& buttonRevision != controllerHelpButtonRevision)
		{
			controllerHelpButtonRevision = buttonRevision;
			helpOverlay->dismiss();
		}
	}

	if (controllerHelpEnabled
		&& controllerHelpConnectionObserver.update(
			inputManager.registeredGamepadCount(),
			inputManager.gamepadAdditionRevision()))
	{
		controllerHelpPending = true;
	}
	if (controllerHelpEnabled && controllerHelpPending
		&& activeControllerHelpOverlay.expired()
		&& canShowControllerHelp())
	{
		controllerHelpPending = false;
		showControllerHelp();
	}

	const bool toggleTouchControls = engine->consumeInputAction(
		GameInput::InputAction::ToggleTouchControls);

	GameManager* gameManager = GameManager::getInstance();
	if (gameManager != nullptr && gameManager->controller != nullptr)
	{
		// Visibility policy, recovery contacts and an explicit toggle are applied
		// as one frame transaction before queued pointer events are dispatched.
		gameManager->processGlobalInputFrame(toggleTouchControls);
	}
}

bool showApplicationCloseConfirmation(Element& sceneRoot)
{
	static bool confirmationActive = false;
	if (confirmationActive)
	{
		return false;
	}

	confirmationActive = true;
	CloseConfirmationScope confirmationScope(
		confirmationActive,
		sceneRoot);
	auto confirmation = std::make_shared<YesNo>("是否退出游戏？");
	if (confirmation->yes == nullptr || confirmation->no == nullptr ||
		confirmation->label == nullptr)
	{
		GameLog::write("Game: application close confirmation UI is unavailable\n");
		return true;
	}

	confirmationScope.attach(confirmation);
	sceneRoot.addChild(confirmation);
	const unsigned int confirmationResult = confirmation->run();
	return (confirmationResult & erOK) != 0;
}
}

ScopedGameInputRegistration::ScopedGameInputRegistration(
	bool confirmWindowClose,
	bool enableControllerHelp)
{
	resetUIFocusInputPresentation();
	resetControllerHelpState(enableControllerHelp);
	Element::setWindowCloseConfirmationHandler(
		confirmWindowClose
			? Element::WindowCloseConfirmationHandler(
				  showApplicationCloseConfirmation)
			: Element::WindowCloseConfirmationHandler(
				  [](Element&)
				  {
					  return true;
				  }));
	Element::setFrameGlobalInputHandler(
		handleGlobalInputActions);
	Element::setFrameInputEventHandler(
		[](const AEvent& event, Engine* engine)
		{
			notifyUIFocusInputEvent(event, engine);
		});
	Element::setFrameSemanticInputHandler(
		[](Engine* engine)
		{
			return dispatchPhysicalUIActions(engine);
		});
	Element::setFrameGameplayInputHandler(
		[](Engine*)
		{
			GameManager* gameManager = GameManager::getInstance();
			if (gameManager != nullptr && gameManager->controller != nullptr)
			{
				gameManager->controller->processPhysicalInputFrame();
			}
		});
	Element::setInputContextTransitionHandler(
		[]()
		{
			Engine* engine = Engine::getInstance();
			if (engine != nullptr)
			{
				engine->releasePhysicalInputsForContextTransition();
			}
		});
}

ScopedGameInputRegistration::~ScopedGameInputRegistration()
{
	resetControllerHelpState(false);
	resetUIFocusInputPresentation();
	Element::setInputContextTransitionHandler({});
	Element::setFrameGameplayInputHandler({});
	Element::setFrameSemanticInputHandler({});
	Element::setFrameInputEventHandler({});
	Element::setFrameGlobalInputHandler({});
	Element::setWindowCloseConfirmationHandler({});
}

Game::Game()
{
}

Game::~Game()
{
}

void Game::setAssetsArg(const std::string& arg)
{
	assetsArg = arg;
}

void Game::setResourcePackIdArg(const std::string& arg)
{
	resourcePackIdArg = arg;
}

void Game::setSkipStartupVideos(bool skip)
{
	skipStartupVideos = skip;
}

void Game::setAutoStartNewGame(bool enabled)
{
	autoStartNewGame = enabled;
}

void Game::setAutomationHooksEnabled(bool enabled)
{
	automationHooksEnabled = enabled;
}

void Game::addStartupIntegerVariable(const std::string& name, int value)
{
	startupIntegerVariables.push_back({ name, value });
}

void Game::addExpectedIntegerVariable(const std::string& name, int value)
{
	expectedIntegerVariables.push_back({ name, value });
}

void Game::setExitAfterNewGameScript(bool enabled)
{
	exitAfterNewGameScript = enabled;
}

void Game::setPostNewGameAutomationWaitMilliseconds(int milliseconds)
{
	postNewGameAutomationWaitMilliseconds = milliseconds > 0 ? milliseconds : 0;
}

void Game::setEditorRunScene(
	const EditorRun::SceneTarget& target,
	const EditorRun::PreparedResourcePhase& preparedResources,
	EditorRun::RuntimeTraceWriter* writer)
{
	editorRunMode = true;
	editorRunTarget = target;
	editorRunPreparedResources = preparedResources;
	runtimeTraceWriter = writer;
}

EditorRunGameFailure Game::getEditorRunFailure() const noexcept
{
	return editorRunFailure;
}

const EditorRun::SceneApplicationResult&
	Game::getEditorRunSceneApplicationResult() const noexcept
{
	return editorRunSceneApplicationResult;
}

int Game::run()
{
	editorRunFailure = EditorRunGameFailure::None;
	editorRunSceneApplicationResult = {};
	Element::resetApplicationQuitState();
	GameLog::write("Game Run!\n");
	GameLog::write("Init Game Engine\n");
	int w = DEFAULT_WINDOW_WIDTH, h = DEFAULT_WINDOW_HEIGHT;
	if (!assetsArg.empty())
	{
		File::setAssetsCollectionRoot(assetsArg);
	}
	Config::load();
#ifdef __MOBILE__
	w = MOBILE_DEFAULT_WINDOW_WIDTH;
	h = MOBILE_DEFAULT_WINDOW_HEIGHT;
	Config::setDefaultWindowSize(w, h);
#endif

	if (Engine::getInstance() == nullptr)
	{
		if (editorRunMode)
		{
			editorRunFailure =
				EditorRunGameFailure::EngineInitialization;
		}
		return -1;
	}

	Config::getWindowSize(w, h);

	if (Engine::getInstance()->init(gameTitle, w, h, Config::fullScreenMode, Config::fullScreenSolutionMode, Config::display) != initOK)
	{
		if (editorRunMode)
		{
			editorRunFailure =
				EditorRunGameFailure::EngineInitialization;
		}
		return -1;
	}

	// 初始化资源管理器：扫描资源包、读取 manifest、确定 active resource root。
	GameLog::write("Init ResourceManager\n");
	auto& resourceManager = ResourceManager::instance();
	if (!resourceManager.initialize(assetsArg))
	{
		GameLog::write("ResourceManager: initialization did not produce a runnable resource state\n");
		if (editorRunMode)
		{
			editorRunFailure =
				EditorRunGameFailure::ResourceInitialization;
		}
		return -1;
	}

	if (!resourcePackIdArg.empty())
	{
		ModRelease::CompatibilityResult compatibility;
		if (!resourceManager.setActiveResourcePackById(
			resourcePackIdArg, &compatibility))
		{
			GameLog::write(
				"ResourceManager: failed to auto-select resource pack id %s (compatibility=%d)\n",
				resourcePackIdArg.c_str(),
				static_cast<int>(compatibility.status));
			GameLog::write(
				"ResourceManager: continuing without the requested pack; available routing remains active\n");
		}
	}

	const auto installGameFont = [&resourceManager](bool preferActiveResource)
	{
		std::unique_ptr<char[]> fontData;
		int fontLength = 0;
		if (preferActiveResource && resourceManager.hasActiveResourceRoot() &&
			File::readActiveResourceFile(
				GameFontPath, fontData, fontLength) &&
			fontData != nullptr && fontLength > 0)
		{
			Engine::getInstance()->setFontFromMem(fontData, fontLength);
			GameLog::write("Game: loaded active resource font\n");
			return true;
		}
		fontData.reset();
		fontLength = 0;
		if (File::readBundledApplicationFile(
				EngineFontPath, fontData, fontLength) &&
			fontData != nullptr && fontLength > 0)
		{
			Engine::getInstance()->setFontFromMem(fontData, fontLength);
			GameLog::write("Game: loaded bundled engine font\n");
			return true;
		}
		GameLog::write(
			"Game: engine font is unavailable (%s); continuing without a font\n",
			EngineFontPath);
		return false;
	};

	GameLog::write("Init Game Font\n");
	installGameFont(resourceManager.hasActiveResourceRoot());
	// Editor-controlled termination must not block on the ordinary interactive
	// "exit game" modal. The editor already owns the stop decision and escalates
	// to a forced kill only when graceful shutdown does not complete.
	ScopedGameInputRegistration gameInputRegistration(
		!editorRunMode,
		!editorRunMode && !automationHooksEnabled);

	// 如果发现多个资源包且尚未选择，需要先显示资源选择界面。
	const bool selectionWasRequired = resourceManager.needsSelection();
	if (selectionWasRequired)
	{
		if (editorRunMode)
		{
			GameLog::write(
				"ResourceManager: editor-run formal resources are empty; continuing with the editor overlay\n");
		}
		else
		{
			GameLog::write("Resource selection is required, showing selection scene\n");
			// 显示资源选择/导入界面。
			auto selectScene = std::make_shared<ResourceSelectScene>();
			unsigned int selectResult = selectScene->run();
			if ((selectResult & erExit) != 0)
			{
				return erExit;
			}
		}
	}
	if (!resourceManager.hasActiveResourceRoot())
	{
		// 编辑器运行允许只使用隔离 overlay；普通游戏在用户退出资源管理页且
		// 未选择任何包时按正常结束处理。
		GameLog::write(
			"ResourceManager: selection ended without a runnable active resource pack\n");
		if (!editorRunMode)
		{
			return 0;
		}
	}
	else if (selectionWasRequired)
	{
		// Selection can change the active package after the startup font was
		// installed. A package-owned font takes precedence for the game itself;
		// the engine font remains the fallback.
		installGameFont(true);
	}

	GameLog::write("Init Cursor\n");
	//设置鼠标样式
	auto cursorImage = IMP::createIMPImage("asf\\ui\\common\\mouse.asf", false);
	if (cursorImage == nullptr)
	{
		cursorImage = IMP::createIMPImage("mpc\\ui\\common\\mouse.mpc", false);
	}
	Engine::getInstance()->setMouseFromImpImage(cursorImage);

	if (editorRunMode)
	{
		GameLog::write("Game: apply editor-run saved scene\n");
		auto mainScene = std::make_shared<MainScene>(
			editorRunTarget,
			editorRunPreparedResources,
			runtimeTraceWriter);
		(void)mainScene->run();
		if (mainScene->hasEditorRunSceneApplicationResult())
		{
			editorRunSceneApplicationResult =
				mainScene->getEditorRunSceneApplicationResult();
		}
		else
		{
			editorRunSceneApplicationResult.error =
				EditorRun::SceneApplicationError::
					MissingRuntimeCallback;
			editorRunSceneApplicationResult.diagnosticCode =
				"editor_run.target.application_failed";
			editorRunSceneApplicationResult.message =
				"Editor-run scene application did not produce a result";
		}
		GameLog::write("Game End!\n");
		if (!editorRunSceneApplicationResult.succeeded())
		{
			editorRunFailure =
				EditorRunGameFailure::SceneApplication;
			return -1;
		}
		return 0;
	}

	if (autoStartNewGame)
	{
		GameLog::write("Game: auto start new game by launch argument\n");
		auto ms = std::make_shared<MainScene>(0);
		ms->setAutomationHooksEnabled(automationHooksEnabled);
		ms->setStartupIntegerVariables(startupIntegerVariables);
		ms->setExpectedIntegerVariables(expectedIntegerVariables);
		ms->setExitAfterNewGameScript(exitAfterNewGameScript);
		ms->setPostNewGameAutomationWaitMilliseconds(static_cast<UTime>(postNewGameAutomationWaitMilliseconds));
		int ret = ms->run();
		GameLog::write("Game End!\n");
		if (ms->hasAutomationCheckFailed())
		{
			return 65;
		}
		return ret == erOK ? 0 : ret;
	}

	GameLog::write("Begin Game Title\n");
	PElement title = std::make_shared<Title>(skipStartupVideos);
	int ret = title->run();

	//GameLog::write("Release Engine!\n");
	//Engine::getInstance()->destroyEngine();
	GameLog::write("Game End!\n");
	return ret;
}
