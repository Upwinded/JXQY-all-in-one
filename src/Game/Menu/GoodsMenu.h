#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ControllerFocusParticipant.h"
#include "SlotGridController.h"
#include <vector>

class GoodsMenu :
	public ConfigDrivenPanel,
	public ControllerTransferParticipant
{
public:
	GoodsMenu();
	virtual ~GoodsMenu();

	virtual void init() override;

	std::shared_ptr<Scrollbar> scrollbar = nullptr;
	std::shared_ptr<Label> money = nullptr;
	std::vector<std::shared_ptr<Item>> item;

	void updateMoney();
	void updateGoods();
	void updateGoods(int index);
	void updateGoodsNumber(int index);
	SlotGridView controllerBagView();
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
	virtual void onEvent() override;
	virtual bool onHandleUIAction(UIAction action) override;
	void freeResource();
};
