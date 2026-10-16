#pragma once
#include "../../Component/ConfigDrivenPanel.h"
#include "../../Component/TextButton.h"
#include "ControllerFocusParticipant.h"

class StateMenu :
	public ConfigDrivenPanel,
	public ControllerFocusParticipant
{
public:
	StateMenu();
	virtual ~StateMenu();

	virtual void updateLabel();
	void updatePanelImage();

	virtual void init() override;
	std::shared_ptr<TextButton> characterSelector;
	bool activateControllerFocus(ControllerFocusTarget target) override;
	bool isControllerFocusActive() const override;
	void deactivateControllerFocus() override;
	PElement controllerFocusedElement() const override;
	std::vector<PElement> controllerFocusCandidates() const override;
	bool focusControllerElement(const PElement& element) override;

private:
	std::shared_ptr<ImageContainer> image = nullptr;
	int loadedPanelIndex = -1;
	bool characterSelectorFocused = false;
	virtual void onUpdate() override;
	void onEvent() override;
	bool onHandleUIAction(UIAction action) override;
	void freeResource();
};
