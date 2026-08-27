#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ControllerFocusParticipant.h"
#include "UIFocusManager.h"

#include <string>

class TopMenu :
	public ConfigDrivenPanel,
	public ControllerFocusParticipant
{
public:
	TopMenu();
	virtual ~TopMenu();

	virtual void init() override;
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;

	std::shared_ptr<CheckBox> equipBtn = nullptr;
	std::shared_ptr<CheckBox> goodsBtn = nullptr;
	std::shared_ptr<CheckBox> magicBtn = nullptr;
	std::shared_ptr<CheckBox> notesBtn = nullptr;
	std::shared_ptr<CheckBox> optionBtn = nullptr;
	std::shared_ptr<CheckBox> stateBtn = nullptr;
	std::shared_ptr<CheckBox> xiulianBtn = nullptr;

private:
	UIFocusManager controllerFocusManager;
	bool controllerFocusActive = false;
	std::string pendingControllerFocusNodeId;

	void configureControllerFocus();
	virtual void onEvent() override;
	virtual bool onHandleUIAction(UIAction action) override;
	void freeResource();
};
