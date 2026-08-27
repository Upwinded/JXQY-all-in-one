#pragma once
#include "../../Component/Component.h"
#include "UIFocusManager.h"

class System :
	public ConfigDrivenPanel
{
	friend class CoreLifecycleTestAccess;
public:
	explicit System(bool focusOptions = false);
	virtual ~System();

	virtual void init() override;

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<Button> returnBtn = nullptr;
	std::shared_ptr<Button> saveloadBtn = nullptr;
	std::shared_ptr<Button> optionBtn = nullptr;
	std::shared_ptr<Button> quitBtn = nullptr;
	UIFocusManager focusManager;

private:
	bool focusOptions = false;
	void configureFocus(const std::string& preferredFocusId);
	void closeToGame();
	void openSaveLoad();
	void openOptions();
	void returnToTitle();
	void freeResource();
	void handleSaveFailure();
	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDrawEnd() override;
	virtual void onRun() override;
	virtual void saveScreen();
};
