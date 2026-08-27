#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ControllerFocusParticipant.h"
#include "SlotGridController.h"

class Goods;
class NPC;

class PartnerEquipMenu :
	public Panel,
	public ControllerTransferParticipant
{
public:
	PartnerEquipMenu();
	virtual ~PartnerEquipMenu();

	virtual void init() override;
	void setPartner(std::shared_ptr<NPC> value);
	std::shared_ptr<NPC> getPartner() const;
	void updateGoods();
	void updateGoods(int index);
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	bool focusControllerDefault();
	bool focusControllerPlayerBag();
	bool focusControllerEquipment();
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;
	virtual void refreshControllerTransferHighlight() override;

private:
	std::shared_ptr<NPC> partner = nullptr;
	std::shared_ptr<Label> titleLabel = nullptr;
	std::shared_ptr<Label> attributeLabel = nullptr;
	std::shared_ptr<TextButton> closeButton = nullptr;
	std::shared_ptr<Item> item[GOODS_BODY_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	std::shared_ptr<Label> itemLabel[GOODS_BODY_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	std::shared_ptr<Goods> itemGoods[GOODS_BODY_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	SlotInteractionController playerBagSlotController;
	SlotInteractionController partnerEquipmentSlotController;
	ControllerPaneRouter controllerPaneRouter;

	static constexpr int PlayerBagControllerPaneId = 0;
	static constexpr int PartnerEquipmentControllerPaneId = 1;

	bool equipFromPlayerSlot(int playerGoodsIndex, int slotIndex);
	bool unequipSlot(int slotIndex);
	void configureControllerFocus();
	void showControllerDetails(
		const std::shared_ptr<Goods>& goods,
		const PElement& anchor);
	void showPlayerControllerDetails(int playerIndex, int visibleIndex);
	void showEquipmentControllerDetails(int slotIndex, int visibleIndex);
	void hideControllerDetails();
	void cancelControllerInteraction();
	void updateAttributeLabel();
	void makeLabel(std::shared_ptr<Label>& label, const Rect& labelRect, int fontSize, unsigned int color);
	void makeButton(std::shared_ptr<TextButton>& button, const Rect& buttonRect, const std::string& text);
	void makeItem(std::shared_ptr<Item>& slotItem, const Rect& itemRect);

	virtual void onEvent() override;
	virtual void onUpdate() override;
	virtual bool onHandleEvent(AEvent& e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDraw() override;
	virtual void onWindowResize(int width, int height) override;
	virtual void freeResource() override;
};
