#pragma once
#include "../../Element/Element.h"
#include "../GameManager/GameManager.h"
#include <utility>
#include <vector>

class MainScene :
	public Element
{
	friend class CoreLifecycleTestAccess;
public:
	MainScene(int index);
	MainScene(
		const EditorRun::SceneTarget& target,
		const EditorRun::PreparedResourcePhase& preparedResources,
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr);
	virtual ~MainScene();

	void init();
	void setAutomationHooksEnabled(bool enabled);
	void setStartupIntegerVariables(const std::vector<std::pair<std::string, int>>& variables);
	void setExpectedIntegerVariables(const std::vector<std::pair<std::string, int>>& variables);
	void setExitAfterNewGameScript(bool enabled);
	void setPostNewGameAutomationWaitMilliseconds(UTime milliseconds);
	bool hasAutomationCheckFailed() const;
	bool isEditorRunMode() const noexcept;
	bool hasEditorRunSceneApplicationResult() const noexcept;
	const EditorRun::SceneApplicationResult&
		getEditorRunSceneApplicationResult() const noexcept;

	std::shared_ptr<GameManager> game = nullptr;

private:
	int gameIndex = -1;
	bool editorRunMode = false;
	EditorRun::SceneTarget editorRunTarget;
	EditorRun::PreparedResourcePhase editorRunPreparedResources;
	EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr;
	bool automationHooksEnabled = false;
	std::vector<std::pair<std::string, int>> startupIntegerVariables;
	std::vector<std::pair<std::string, int>> expectedIntegerVariables;
	bool exitAfterNewGameScript = false;
	UTime postNewGameAutomationWaitMilliseconds = 0;
	virtual bool onInitial();
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onUpdate();

};
