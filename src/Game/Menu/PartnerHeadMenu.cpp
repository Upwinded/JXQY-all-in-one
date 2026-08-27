#include "PartnerHeadMenu.h"
#include "../../Engine/Engine.h"
#include "../Data/Global.h"
#include "../Data/NPC.h"
#include "../GameManager/GameManager.h"

#include <utility>

PartnerHeadMenu::PartnerHeadMenu()
{
	name = "PartnerHeadMenu";
	visible = true;
	controllerFocusManager.setInputAwarePresentation();
	init();
}

PartnerHeadMenu::~PartnerHeadMenu()
{
	freeResource();
}

void PartnerHeadMenu::init()
{
	freeResource();

	rect = { 0, 0, 160, 320 };
	align = alNone;
	alignX = 0;
	alignY = 0;
	coverMouse = false;

	refreshPartnerButtons();
}

bool PartnerHeadMenu::isAvailable() const
{
	return gm != nullptr && gm->global.feature.partnerHeadMenu;
}

_shared_imp PartnerHeadMenu::loadPartnerHeadImage(const std::string& partnerName)
{
	if (partnerName.empty())
	{
		return nullptr;
	}

	auto it = headImageCache.find(partnerName);
	if (it != headImageCache.end())
	{
		return it->second;
	}

	auto image = IMP::createIMPImage("asf\\ui\\littlehead\\" + partnerName + ".asf");
	headImageCache[partnerName] = image;
	return image;
}

void PartnerHeadMenu::ensurePartnerControls(size_t count)
{
	while (partnerHeadItems.size() < count)
	{
		std::shared_ptr<Item> item;
		makeHeadItem(item);
		partnerHeadItems.push_back(item);

		std::shared_ptr<Label> label;
		makeLabel(label, { HeadStartX, HeadStartY, LevelTextWidth, LevelTextHeight }, LevelFontSize, 0xFFFFFFFF);
		levelLabels.push_back(label);
	}
}

void PartnerHeadMenu::refreshPartnerButtons()
{
	displayedPartners.clear();

	for (auto& item : partnerHeadItems)
	{
		if (item != nullptr)
		{
			item->visible = false;
		}
	}
	for (auto& label : levelLabels)
	{
		if (label != nullptr)
		{
			label->visible = false;
			label->setStr("");
		}
	}

	if (!isAvailable())
	{
		synchronizeControllerFocus();
		return;
	}

	auto partners = gm->partnerManager.findPartnersFromNPCManager();
	size_t controlIndex = 0;
	int y = HeadStartY;

	for (auto& partner : partners)
	{
		if (partner == nullptr || partner->npcName.empty())
		{
			continue;
		}

		auto headImage = loadPartnerHeadImage(partner->npcName);
		if (headImage == nullptr)
		{
			continue;
		}

		ensurePartnerControls(controlIndex + 1);
		auto& item = partnerHeadItems[controlIndex];
		auto& label = levelLabels[controlIndex];

		int width = 32;
		int height = 32;
		auto firstFrame = IMP::loadImage(headImage, 0);
		if (firstFrame != nullptr)
		{
			engine->getImageSize(firstFrame, width, height);
		}

		if (item != nullptr)
		{
			item->visible = true;
			item->activated = partner->canEquip > 0;
			item->impImage = headImage;
			item->rect = { HeadStartX, y, width, height };
		}
		if (label != nullptr)
		{
			label->rect =
			{
				HeadStartX + width + 3,
				y + height - LevelTextHeight,
				LevelTextWidth,
				LevelTextHeight
			};
			label->visible = partner->canLevelUp > 0;
			label->setStr(label->visible ? "LV" + std::to_string(partner->level) : "");
		}

		displayedPartners.push_back(partner);
		y += height + HeadGapY;
		controlIndex++;
	}

	rect.w = 160;
	rect.h = y + HeadGapY;
	synchronizeControllerFocus();
}

void PartnerHeadMenu::synchronizeControllerFocus()
{
	std::vector<const NPC*> currentIdentities;
	currentIdentities.reserve(displayedPartners.size());
	for (const auto& partner : displayedPartners)
	{
		currentIdentities.push_back(partner.get());
	}
	if (currentIdentities == controllerPartnerIdentities)
	{
		return;
	}

	const std::string previousFocusId =
		controllerFocusManager.getFocusedNodeId();
	const NPC* previousPartner = nullptr;
	for (std::size_t index = 0;
		index < controllerPartnerIdentities.size(); index++)
	{
		if (previousFocusId == "partner-head-" + std::to_string(index))
		{
			previousPartner = controllerPartnerIdentities[index];
			break;
		}
	}
	const bool restoreFocus = controllerFocusActive;
	controllerFocusManager.clear();
	controllerPartnerIdentities = std::move(currentIdentities);
	for (std::size_t index = 0; index < displayedPartners.size(); index++)
	{
		if (index >= partnerHeadItems.size()
			|| partnerHeadItems[index] == nullptr)
		{
			continue;
		}
		const std::string focusId =
			"partner-head-" + std::to_string(index);
		controllerFocusManager.addNode(
			focusId,
			partnerHeadItems[index],
			{ "partner-heads", static_cast<int>(index), 0 },
			[this, index]()
			{
				if (index < displayedPartners.size())
				{
					openPartnerEquipMenu(displayedPartners[index], true);
				}
			});
		if (index == 0)
		{
			controllerFocusManager.setDefaultFocus(focusId);
		}
		if (index > 0)
		{
			const std::string previousId =
				"partner-head-" + std::to_string(index - 1);
			controllerFocusManager.setNeighbour(
				previousId, UIFocusDirection::Down, focusId);
			controllerFocusManager.setNeighbour(
				focusId, UIFocusDirection::Up, previousId);
		}
	}
	if (!restoreFocus)
	{
		return;
	}
	for (std::size_t index = 0;
		index < controllerPartnerIdentities.size(); index++)
	{
		if (controllerPartnerIdentities[index] == previousPartner
			&& controllerFocusManager.focusNode(
				"partner-head-" + std::to_string(index)))
		{
			return;
		}
	}
	if (!controllerFocusManager.restoreFocus())
	{
		controllerFocusActive = false;
	}
}

bool PartnerHeadMenu::openPartnerEquipMenu(
	std::shared_ptr<NPC> partner,
	bool returnToPartnerList)
{
	if (partner == nullptr || partner->canEquip <= 0 || gm->menu->partnerEquipMenu == nullptr)
	{
		return false;
	}
	return gm->menu->openPartnerEquipment(partner, returnToPartnerList);
}

bool PartnerHeadMenu::openFirstPartnerEquipMenu()
{
	refreshPartnerButtons();
	if (!isAvailable() || displayedPartners.empty())
	{
		return false;
	}

	for (auto& partner : displayedPartners)
	{
		if (partner != nullptr && partner->canEquip > 0)
		{
			return openPartnerEquipMenu(partner, false);
		}
	}
	return false;
}

void PartnerHeadMenu::makeHeadItem(std::shared_ptr<Item>& item)
{
	item = std::make_shared<Item>();
	item->rect = { HeadStartX, HeadStartY, 32, 32 };
	item->canDrag = false;
	item->canDrop = false;
	item->canShowHint = false;
	item->coverMouse = true;
	item->centerImage = false;
	item->stretch = false;
	item->frameIndex = -1;
	item->visible = false;
	addChild(item);
}

void PartnerHeadMenu::makeLabel(std::shared_ptr<Label>& label, const Rect& labelRect, int fontSize, unsigned int color)
{
	label = std::make_shared<Label>();
	label->rect = labelRect;
	label->fontSize = fontSize;
	label->color = color;
	label->coverMouse = false;
	label->visible = false;
	addChild(label);
}

void PartnerHeadMenu::onUpdate()
{
	if (!visible || !isAvailable())
	{
		return;
	}
	refreshPartnerButtons();
}

void PartnerHeadMenu::onEvent()
{
	if (!visible || !isAvailable())
	{
		return;
	}

	for (size_t i = 0; i < displayedPartners.size(); i++)
	{
		if (i >= partnerHeadItems.size() || partnerHeadItems[i] == nullptr)
		{
			continue;
		}
		if (partnerHeadItems[i]->getResult(erClick))
		{
			deactivateControllerFocus();
			openPartnerEquipMenu(displayedPartners[i], false);
			return;
		}
	}
}

bool PartnerHeadMenu::hasControllerPartners()
{
	refreshPartnerButtons();
	for (const auto& partner : displayedPartners)
	{
		if (partner != nullptr && partner->canEquip > 0)
		{
			return true;
		}
	}
	return false;
}

bool PartnerHeadMenu::activateControllerFocus(ControllerFocusTarget target)
{
	return (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::PartnerList)
		&& focusControllerDefault();
}

bool PartnerHeadMenu::focusControllerDefault()
{
	refreshPartnerButtons();
	controllerFocusActive = true;
	if (controllerFocusManager.restoreFocus())
	{
		return true;
	}
	controllerFocusActive = false;
	return false;
}

bool PartnerHeadMenu::isControllerFocusActive() const
{
	return controllerFocusActive
		&& isUIFocusElementAvailable(
			controllerFocusManager.getFocusedElement());
}

void PartnerHeadMenu::deactivateControllerFocus()
{
	controllerFocusActive = false;
	controllerFocusManager.suspendFocus();
}

PElement PartnerHeadMenu::controllerFocusedElement() const
{
	PElement focusedElement = controllerFocusManager.getFocusedElement();
	return controllerFocusActive
		&& isUIFocusElementAvailable(focusedElement)
		? focusedElement
		: nullptr;
}

std::vector<PElement> PartnerHeadMenu::controllerFocusCandidates() const
{
	return controllerFocusManager.getAvailableFocusElements();
}

bool PartnerHeadMenu::focusControllerElement(const PElement& element)
{
	controllerFocusActive = true;
	controllerFocusManager.prepareForSemanticActivation();
	if (controllerFocusManager.focusElement(element))
	{
		return true;
	}
	controllerFocusActive = false;
	return false;
}

bool PartnerHeadMenu::onHandleEvent(AEvent& e)
{
	(void)e;
	if (!visible || !isAvailable())
	{
		return false;
	}
	return false;
}

bool PartnerHeadMenu::onHandleUIAction(UIAction action)
{
	if (!visible || !isAvailable())
	{
		return false;
	}
	if (!controllerFocusActive && !focusControllerDefault())
	{
		return false;
	}
	return controllerFocusManager.handleAction(action);
}

void PartnerHeadMenu::onDraw()
{
}

void PartnerHeadMenu::onWindowResize(int width, int height)
{
	const bool restoreFocus = controllerFocusActive;
	controllerFocusManager.suspendFocus();
	Panel::onWindowResize(width, height);
	if (restoreFocus && !controllerFocusManager.restoreFocus())
	{
		controllerFocusActive = false;
	}
}

void PartnerHeadMenu::freeResource()
{
	controllerFocusManager.clear();
	controllerFocusActive = false;
	partnerHeadItems.clear();
	levelLabels.clear();
	displayedPartners.clear();
	controllerPartnerIdentities.clear();
	headImageCache.clear();
	Panel::freeResource();
	removeAllChild();
}
