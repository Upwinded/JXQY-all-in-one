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
	void showTalents(bool value);
	bool isShowingTalents() const { return showingTalents; }
	bool showDetails(int listIndex);
	void closeDetails();
	bool isShowingDetails() const { return showingDetails; }
	bool assignDetailedMagic(bool practice);
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
	bool showingDetails = false;
	bool detailFocused = false;
	std::string detailedMagicFile;
	int detailPage = 0;
	std::string detailContent;
	int detailWidth = 0;
	std::vector<std::string> detailLines;
	UIFocusManager detailFocus;
	std::shared_ptr<MemoText> detailText;
	void updateDetails();
	void changeDetailPage(int direction);
	void configureDetailFocus();
	void setDetailVisibility();
	int detailedMagicIndex() const;
	bool showingTalents = false;
	bool tabFocused = false;
	std::shared_ptr<CheckBox> magicTab;
	std::shared_ptr<CheckBox> talentTab;
	int displayBegin() const;
	int displayCount() const;
	bool isDisplayIndex(int index) const;
	int position = -1;
	SlotInteractionController slotController;
	void configureControllerFocus();
	void activateControllerItem(int visibleIndex);
	void showControllerItemDetails(int visibleIndex);
	void hideControllerItemDetails();
	int getControllerItemIndex(int visibleIndex) const;
	void onUpdate() override;
	void onEvent() override;
	bool onHandleUIAction(UIAction action) override;

	void freeResource() override;
};
