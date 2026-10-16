#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ControllerFocusParticipant.h"
#include "SlotGridController.h"
#include <vector>

class NPC;
class Goods;

class EquipMenu :
	public ConfigDrivenPanel,
	public ControllerTransferParticipant
{
public:
	EquipMenu();
	virtual ~EquipMenu();

	virtual void init() override;

	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<Item> item[GOODS_BODY_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	std::vector<std::shared_ptr<Item>> magicDisplayItem;
	std::shared_ptr<Scrollbar> magicScrollbar = nullptr;
	std::vector<std::shared_ptr<ImageContainer>> playerNameImages;
	std::vector<std::shared_ptr<Button>> playerNameButtons;
	bool openPartnerByCharacterIndex(int index);
	bool selectPartner(const std::shared_ptr<NPC>& partner);
	bool cycleCharacter();
	void updateCharacterSelector(TextButton& button) const;
	std::shared_ptr<TextButton> characterSelector;
	std::shared_ptr<NPC> getSelectedPartner() const;
	bool equipOnSelectedPartner(int goodsIndex, int partIndex);
	bool unequipSelectedPartner(int partIndex);

	void updateGoods();
	void updateGoods(int index);
	void updateMagicDisplay();
	void updateMagicDisplay(int index);
	void updatePanelImage();
	void showAttributes(bool value);
	bool isShowingAttributes() const { return showingAttributes; }
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	bool focusControllerEquipment();
	bool focusControllerMagicList();
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;
	virtual bool controllerFocusElementMatchesTarget(
		const PElement& element,
		ControllerFocusTarget target) const override;
	virtual void refreshControllerTransferHighlight() override;

	int getPartIndex(const std::string & part);

private:
	std::weak_ptr<NPC> selectedPartner;
	bool showingPartner = false;
	bool characterSelectorFocused = false;
	std::shared_ptr<Goods> partnerGoods[GOODS_BODY_COUNT];
	void updateCharacterBindings();
	bool showingAttributes = false;
	bool tabFocused = false;
	int focusedPlayerName = -1;
	std::shared_ptr<CheckBox> equipmentTab;
	std::shared_ptr<CheckBox> attributesTab;
	void updateAttributes();
	std::vector<std::string> magicDisplayIniFile;
	std::vector<std::string> newSwordPartnerNames;
	int loadedPanelIndex = -1;
	SlotInteractionController equipmentSlotController;
	SlotInteractionController magicSlotController;
	ControllerPaneRouter controllerPaneRouter{
		ControllerPaneActionPolicy::PassThrough };

	static constexpr int EquipmentControllerPaneId = 0;
	static constexpr int MagicControllerPaneId = 1;

	virtual void onUpdate() override;
	virtual void onEvent() override;
	virtual void onWindowResize(int width, int height) override;
	virtual bool onHandleUIAction(UIAction action) override;
	void freeResource();
	void configureControllerFocus();
	void configureEquipmentControllerNeighbours();
	void activateControllerEquipment(int logicalIndex, int visibleIndex);
	void showControllerEquipmentDetails(int logicalIndex, int visibleIndex);
	void activateControllerMagic(int logicalIndex, int visibleIndex);
	void showControllerMagicDetails(int logicalIndex, int visibleIndex);
	void hideControllerDetails();
	int getControllerMagicIndex(int visibleIndex) const;
	int getMagicDisplayStartIndex() const;
	void quickEquipMagic(int sourceIndex);
	void updateMagicRelatedMenus(int sourceIndex);
	int getNewSwordPartnerIndex(const std::string& partnerName) const;
	void loadNewSwordPartnerNames();
	void updatePlayerNameDisplay();
};
