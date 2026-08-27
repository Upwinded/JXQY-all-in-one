#include "MainScene.h"

MainScene::MainScene(int idx)
{
	name = "MainScene";
	gameIndex = idx;
	init();
}

MainScene::MainScene(
	const EditorRun::SceneTarget& target,
	const EditorRun::PreparedResourcePhase& preparedResources,
	EditorRun::RuntimeTraceWriter* writer)
{
	name = "MainScene";
	editorRunMode = true;
	editorRunTarget = target;
	editorRunPreparedResources = preparedResources;
	runtimeTraceWriter = writer;
	init();
}

MainScene::~MainScene()
{
	removeAllChild();
	game = nullptr;
}

void MainScene::init()
{
	if (editorRunMode)
	{
		game = std::make_shared<GameManager>(
			editorRunTarget,
			editorRunPreparedResources,
			runtimeTraceWriter);
	}
	else
	{
		game = std::make_shared<GameManager>();
	}
	addChild(game);
}

void MainScene::setAutomationHooksEnabled(bool enabled)
{
	automationHooksEnabled = enabled;
}

void MainScene::setStartupIntegerVariables(const std::vector<std::pair<std::string, int>>& variables)
{
	startupIntegerVariables = variables;
}

void MainScene::setExpectedIntegerVariables(const std::vector<std::pair<std::string, int>>& variables)
{
	expectedIntegerVariables = variables;
}

void MainScene::setExitAfterNewGameScript(bool enabled)
{
	exitAfterNewGameScript = enabled;
}

void MainScene::setPostNewGameAutomationWaitMilliseconds(UTime milliseconds)
{
	postNewGameAutomationWaitMilliseconds = milliseconds;
}

bool MainScene::hasAutomationCheckFailed() const
{
	return game != nullptr && game->hasAutomationCheckFailed();
}

bool MainScene::isEditorRunMode() const noexcept
{
	return editorRunMode;
}

bool MainScene::hasEditorRunSceneApplicationResult() const noexcept
{
	return game != nullptr &&
		game->hasEditorRunSceneApplicationResult();
}

const EditorRun::SceneApplicationResult&
	MainScene::getEditorRunSceneApplicationResult() const noexcept
{
	static const EditorRun::SceneApplicationResult noResult;
	return game != nullptr
		? game->getEditorRunSceneApplicationResult()
		: noResult;
}

bool MainScene::onInitial()
{
	if (game == nullptr)
	{
		return true;
	}
	if (editorRunMode)
	{
		return true;
	}
	game->gameIndex = gameIndex;
	game->setAutomationHooksEnabled(automationHooksEnabled);
	game->setStartupIntegerVariables(startupIntegerVariables);
	game->setExpectedIntegerVariables(expectedIntegerVariables);
	game->setExitAfterNewGameScript(exitAfterNewGameScript);
	game->setPostNewGameAutomationWaitMilliseconds(postNewGameAutomationWaitMilliseconds);

	return true; 
}

bool MainScene::onHandleUIAction(UIAction action)
{
	return game != nullptr && game->handleUIAction(action);
}

void MainScene::onUpdate()
{
	if (game == nullptr)
	{
		logicRunning = false;
		return;
	}
	unsigned int ret = game->getResult();
	unsigned int completionResult = ret & (erExit | erOK | erReturnToTitle);
	if (completionResult != 0)
	{
		result |= completionResult;
		logicRunning = false;
	}
}
