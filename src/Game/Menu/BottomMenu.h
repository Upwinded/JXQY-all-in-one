#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ColumnMenu.h"
#include "ControllerFocusParticipant.h"
#include "SlotGridController.h"

class BottomMenu :
	public ConfigDrivenPanel,
	public ControllerTransferParticipant
{
public:
	BottomMenu();
	virtual ~BottomMenu();

	virtual void init() override;

	void updateGoodsItem();
	void updateGoodsItem(int index);
	void updateGoodsNumber();
	void updateGoodsNumber(int index);

	void updateMagicItem();
	void updateMagicItem(int index);
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	bool focusControllerGoodsQuick();
	bool focusControllerMagicQuick();
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;
	virtual void refreshControllerTransferHighlight() override;

	std::shared_ptr<ColumnMenu> columnMenu = nullptr;

	std::shared_ptr<CheckBox> equipBtn = nullptr;
	std::shared_ptr<CheckBox> goodsBtn = nullptr;
	std::shared_ptr<CheckBox> magicBtn = nullptr;
	std::shared_ptr<CheckBox> notesBtn = nullptr;
	std::shared_ptr<CheckBox> optionBtn = nullptr;
	std::shared_ptr<CheckBox> stateBtn = nullptr;
	std::shared_ptr<CheckBox> xiulianBtn = nullptr;

	std::shared_ptr<Item> goodsItem[GOODS_TOOLBAR_COUNT] = { nullptr, nullptr, nullptr };
	std::shared_ptr<Item> magicItem[MAGIC_TOOLBAR_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr };

private:
	SlotInteractionController goodsSlotController;
	SlotInteractionController magicSlotController;
	UIFocusManager menuButtonFocusManager;
	bool menuButtonFocusActive = false;
	ControllerPaneRouter controllerPaneRouter{
		ControllerPaneActionPolicy::PassThrough };

	static constexpr int GoodsControllerPaneId = 0;
	static constexpr int MagicControllerPaneId = 1;

	virtual void onEvent() override;
	virtual bool onHandleUIAction(UIAction action) override;
	bool focusSpatialControllerCandidate(
		UIFocusDirection direction,
		bool requireQuickSlotTarget);
	void configureControllerFocus(
		const std::string& preferredMenuButtonFocusId,
		int preferredPaneId);
	int getControllerGoodsIndex(int visibleIndex) const;
	int getControllerMagicIndex(int visibleIndex) const;
	void activateControllerGoodsItem(int logicalIndex);
	void showControllerGoodsDetails(int logicalIndex, int visibleIndex);
	void showControllerMagicDetails(int logicalIndex, int visibleIndex);
	void hideControllerItemDetails();
	void cancelPointerControllerInteraction();
	void freeResource();
};
