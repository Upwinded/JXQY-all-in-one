#pragma once
#include "../../Component/Component.h"
#include "ControllerFocusParticipant.h"
#include "SlotGridController.h"

class PracticeMenu :
	public ConfigDrivenPanel,
	public ControllerTransferParticipant
{
public:
	PracticeMenu();
	virtual ~PracticeMenu();

	virtual void init() override;

	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<ImageContainer> title = nullptr;

	std::shared_ptr<Label> name = nullptr;
	std::shared_ptr<Label> intro = nullptr;
	std::shared_ptr<Label> level = nullptr;
	std::shared_ptr<Label> exp = nullptr;

	std::shared_ptr<Item> magic = nullptr;

	void updateMagic();
	void updateExp();
	void updateLevel();
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	bool focusControllerDefault();
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;
	virtual void refreshControllerTransferHighlight() override;

private:
	SlotInteractionController slotController;
	void configureControllerFocus();
	void showControllerMagicDetails(int logicalIndex);
	void hideControllerMagicDetails();
	virtual void onEvent() override;
	virtual bool onHandleUIAction(UIAction action) override;
	void freeResource();
};
