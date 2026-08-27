#pragma once
#include "../../Component/Component.h"
#include "ControllerFocusParticipant.h"
#include "UIFocusManager.h"

class GamepadWorldRuntimeTestAccess;

class MemoMenu :
	public ConfigDrivenPanel,
	public ControllerFocusParticipant
{
	friend class GamepadWorldRuntimeTestAccess;
public:
	MemoMenu();
	virtual ~MemoMenu();

	virtual void init() override;

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<MemoText> memoText = nullptr;
	std::shared_ptr<Scrollbar> scrollbar = nullptr;

	void reFresh();
	void reset();
	void reRange(int max);
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;

private:
	int position = -1;
	UIFocusManager controllerFocusManager;
	bool controllerFocusActive = false;

	void freeResource();
	bool scrollBy(int delta);
	void configureControllerFocus();

	virtual void onEvent() override;
	virtual bool onHandleUIAction(UIAction action) override;
};
