#pragma once
#include "../../Component/Component.h"
#include "UIFocusManager.h"

class GamepadEssentialUITestAccess;

class YesNo :
	public ConfigDrivenPanel
{
	friend class GamepadEssentialUITestAccess;
	friend class UIFocusTestAccess;
public:
	YesNo(const std::string & s = "");
	virtual ~YesNo();

	std::shared_ptr<Button> yes = nullptr;
	std::shared_ptr<Button> no = nullptr;
	std::shared_ptr<Label> label = nullptr;

	virtual void init() override;
	void init(const std::string& s);

private:
	std::string promptText;
	UIFocusManager focusManager;

	void configureFocus(const std::string& preferredFocusId);
	void selectYes();
	void selectNo();
	void freeResource();
	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDrawEnd() override;
	virtual void onRun() override;
};
