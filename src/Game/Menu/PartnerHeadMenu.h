#pragma once
#include "../../Component/Component.h"
#include "../../Component/ConfigDrivenPanel.h"
#include "PartnerHeadLayout.h"
#include "ControllerFocusParticipant.h"
#include "UIFocusManager.h"
#include <map>
#include <string>
#include <vector>

class NPC;

class PartnerHeadMenu :
	public ConfigDrivenPanel,
	public ControllerFocusParticipant
{
public:
	PartnerHeadMenu();
	virtual ~PartnerHeadMenu();

	virtual void init() override;
	void refreshPartnerButtons();
	bool openFirstPartnerEquipMenu();
	bool hasControllerPartners();
	virtual bool activateControllerFocus(
		ControllerFocusTarget target) override;
	bool focusControllerDefault();
	virtual bool isControllerFocusActive() const override;
	virtual void deactivateControllerFocus() override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(
		const PElement& element) override;

private:
	PartnerHeadLayout layout;

	std::vector<std::shared_ptr<Item>> partnerHeadItems;
	std::vector<std::shared_ptr<Label>> levelLabels;
	std::vector<std::shared_ptr<NPC>> displayedPartners;
	std::vector<const NPC*> controllerPartnerIdentities;
	std::map<std::string, _shared_imp> headImageCache;
	UIFocusManager controllerFocusManager;
	bool controllerFocusActive = false;

	bool isAvailable() const;
	_shared_imp loadPartnerHeadImage(const std::string& partnerName);
	void ensurePartnerControls(size_t count);
	void synchronizeControllerFocus();
	bool openPartnerEquipMenu(
		std::shared_ptr<NPC> partner,
		bool returnToPartnerList);
	void makeHeadItem(std::shared_ptr<Item>& item);
	void makeLabel(std::shared_ptr<Label>& label, const Rect& labelRect, int fontSize, unsigned int color);

	virtual void onUpdate() override;
	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent& e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDraw() override;
	virtual void onWindowResize(int width, int height) override;
	virtual void freeResource() override;
};
