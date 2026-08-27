#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ControllerFocusParticipant.h"
#include "SlotGridController.h"
#include <vector>

class MagicMenu :
	public ConfigDrivenPanel,
	public ControllerTransferParticipant
{
public:
	MagicMenu();
	virtual ~MagicMenu();

	void init() override;

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<ImageContainer> image = nullptr;

	std::shared_ptr<Scrollbar> scrollbar = nullptr;

	std::vector<std::shared_ptr<Item>> item;

	void updateMagic();
	void updateMagic(int index);
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
	void cancelControllerInteraction();

private:
	int position = -1;
	SlotInteractionController slotController;
	void configureControllerFocus();
	void activateControllerItem(int visibleIndex);
	void showControllerItemDetails(int visibleIndex);
	void hideControllerItemDetails();
	int getControllerItemIndex(int visibleIndex) const;
	void onEvent() override;
	bool onHandleUIAction(UIAction action) override;

	void freeResource() override;
};
