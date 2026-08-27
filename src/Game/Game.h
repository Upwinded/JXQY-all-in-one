#pragma once
#include "../Element/Element.h"
#include "../Launch/EditorRunResourceRouting.h"
#include "../Launch/EditorRunSceneApplication.h"
#include "Scene/Title.h"
#include <utility>
#include <vector>

namespace EditorRun
{
class RuntimeTraceWriter;
}

class ScopedGameInputRegistration final
{
public:
	explicit ScopedGameInputRegistration(
		bool confirmWindowClose = true,
		bool enableControllerHelp = false);
	~ScopedGameInputRegistration();

	ScopedGameInputRegistration(
		const ScopedGameInputRegistration&) = delete;
	ScopedGameInputRegistration& operator=(
		const ScopedGameInputRegistration&) = delete;
	ScopedGameInputRegistration(
		ScopedGameInputRegistration&&) = delete;
	ScopedGameInputRegistration& operator=(
		ScopedGameInputRegistration&&) = delete;
};

enum class EditorRunGameFailure
{
	None,
	ResourceInitialization,
	EngineInitialization,
	SceneApplication
};

class Game
{
public:
	Game();
	virtual ~Game();

	std::string gameTitle = u8"剑侠情缘 All-in-One";
public:

	int run();

	// 设置 --assets 启动参数（在 run() 之前调用）。
	void setAssetsArg(const std::string& arg);

	// 设置 --resource-id 启动参数（在 run() 之前调用）。
	void setResourcePackIdArg(const std::string& arg);

	// 设置是否跳过 profile 启动视频（自动化审查使用）。
	void setSkipStartupVideos(bool skip);

	// 设置是否启动后直接进入新游戏脚本（自动化审查使用）。
	void setAutoStartNewGame(bool enabled);
	void setAutomationHooksEnabled(bool enabled);
	void addStartupIntegerVariable(const std::string& name, int value);
	void addExpectedIntegerVariable(const std::string& name, int value);
	void setExitAfterNewGameScript(bool enabled);
	void setPostNewGameAutomationWaitMilliseconds(int milliseconds);
	void setEditorRunScene(
		const EditorRun::SceneTarget& target,
		const EditorRun::PreparedResourcePhase& preparedResources,
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr);
	EditorRunGameFailure getEditorRunFailure() const noexcept;
	const EditorRun::SceneApplicationResult&
		getEditorRunSceneApplicationResult() const noexcept;

private:
	std::string assetsArg;
	std::string resourcePackIdArg;
	bool skipStartupVideos = false;
	bool autoStartNewGame = false;
	bool automationHooksEnabled = false;
	std::vector<std::pair<std::string, int>> startupIntegerVariables;
	std::vector<std::pair<std::string, int>> expectedIntegerVariables;
	bool exitAfterNewGameScript = false;
	int postNewGameAutomationWaitMilliseconds = 0;
	bool editorRunMode = false;
	EditorRun::SceneTarget editorRunTarget;
	EditorRun::PreparedResourcePhase editorRunPreparedResources;
	EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr;
	EditorRunGameFailure editorRunFailure =
		EditorRunGameFailure::None;
	EditorRun::SceneApplicationResult
		editorRunSceneApplicationResult;
};
