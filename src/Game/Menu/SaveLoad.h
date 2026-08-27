#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "UIFocusManager.h"

class GamepadEssentialUITestAccess;

class SaveLoad :
	public ConfigDrivenPanel
{
	friend class CoreLifecycleTestAccess;
	friend class GamepadEssentialUITestAccess;
	friend class UIFocusTestAccess;
public:
	SaveLoad(bool s, bool l);
	virtual ~SaveLoad();

private:
	bool save = true;
	bool load = true;
	UIFocusManager focusManager;

public:
	int index = -1;

	std::shared_ptr<ListBox> listBox = nullptr;

	std::shared_ptr<ImageContainer> snap = nullptr;
	std::shared_ptr<Button> loadBtn = nullptr;
	std::shared_ptr<Button> saveBtn = nullptr;
	std::shared_ptr<Button> exitBtn = nullptr;

	virtual void init() override;

private:
	void configureFocus(const std::string& preferredFocusId);
	bool moveSlotSelection(int delta);
	bool selectSlot(int selectedIndex);
	void refreshSelectedSlotPreview();
	void requestSave();
	void requestLoad();
	void closeMenu();
	virtual void onEvent() override;
	void freeResource();
	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDrawEnd() override;
	virtual void onRun() override;
};
