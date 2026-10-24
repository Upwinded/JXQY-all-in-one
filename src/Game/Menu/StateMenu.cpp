#include "StateMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "../Data/Global.h"

#include <algorithm>
#include <array>

namespace
{
void compactStatusLabels(StateMenu& menu)
{
	constexpr int MaximumFontSize = 16;
	constexpr int MinimumFontSize = 12;
	constexpr int RightPadding = 4;
	constexpr std::array<const char*, 10> LabelNames = {
		"labLevel", "labExp", "labExpUp", "labAttack", "labDefend",
		"labEvade", "labLife", "labThew", "labMana", "labRage"
	};

	const int contentRight = std::max(0, menu.rect.w - RightPadding);
	for (const char* labelName : LabelNames)
	{
		auto label = menu.getComponentByName<Label>(labelName);
		if (label == nullptr)
		{
			continue;
		}
		label->fontSize = std::min(label->fontSize, MaximumFontSize);
		label->minimumFontSize = std::min(label->fontSize, MinimumFontSize);
		label->autoShrink = true;
		label->rect.w = std::min(
			label->rect.w,
			std::max(0, contentRight - label->rect.x));
		label->rect.h = std::max(label->rect.h, label->fontSize);
		label->invalidateTextLayout();
	}
}
}

StateMenu::StateMenu()
{
	name = "StateMenu";
	visible = false;
	needEvents = true;
	init();
}

StateMenu::~StateMenu()
{
	freeResource();
}

void StateMenu::updateLabel()
{
	if (!gm || !gm->player) return;
	updateAttributePointEntry();
	updatePanelImage();
	const auto equipment = gm->menu ? gm->menu->equipMenu : nullptr;
	const auto partner = equipment ? equipment->getSelectedPartner() : nullptr;
	const auto character = partner ? partner : std::static_pointer_cast<NPC>(gm->player);
	if (characterSelector && equipment)
	{
		equipment->updateCharacterSelector(*characterSelector);
	}

	auto labLevel = getComponentByName<Label>("labLevel");
	auto labExp = getComponentByName<Label>("labExp");
	auto labExpUp = getComponentByName<Label>("labExpUp");
	auto labAttack = getComponentByName<Label>("labAttack");
	auto labDefend = getComponentByName<Label>("labDefend");
	auto labEvade = getComponentByName<Label>("labEvade");
	auto labLife = getComponentByName<Label>("labLife");
	auto labThew = getComponentByName<Label>("labThew");
	auto labMana = getComponentByName<Label>("labMana");
	auto labRage = getComponentByName<Label>("labRage");

	if (labLevel) labLevel->setStr(convert::formatString("%d", character->level));
	if (labExp) labExp->setStr(convert::formatString("%d", character->exp));
	if (labExpUp) labExpUp->setStr(convert::formatString("%d", character->levelUpExp));
	if (labAttack) labAttack->setStr(convert::formatString("%d", character->getAttack()));
	if (labDefend) labDefend->setStr(convert::formatString("%d", character->getDefend()));
	if (labEvade) labEvade->setStr(convert::formatString("%d", character->getEvade()));
	if (labLife) labLife->setStr(convert::formatString("%d/%d", character->life, character->getLifeMax()));
	if (labThew) labThew->setStr(convert::formatString("%d/%d", character->thew, character->getThewMax()));
	if (labMana) labMana->setStr(convert::formatString("%d/%d", character->mana, character->getManaMax()));
	if (labRage)
	{
		labRage->visible = gm->global.feature.rageSystem && !partner;
		labRage->setStr(convert::formatString("%d/%d", gm->player->rage, gm->player->rageMax));
	}
}

void StateMenu::updatePanelImage()
{
	if (gm == nullptr || !gm->global.feature.characterPanelImages || image == nullptr)
	{
		return;
	}

	int panelIndex = gm->global.data.characterIndex;
	if (panelIndex < 0)
	{
		panelIndex = 0;
	}
	if (loadedPanelIndex == panelIndex)
	{
		return;
	}

	std::string fileName = "panel5.asf";
	if (panelIndex > 0)
	{
		fileName = convert::formatString("panel5%c.asf", (char)('a' + panelIndex));
	}
	image->impImage = IMP::createIMPImage(std::string("asf\\ui\\common\\") + fileName);
	loadedPanelIndex = panelIndex;
}

void StateMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\state\\state.menu.ini");
	image = getComponentByName<ImageContainer>("image");
	characterSelector = getComponentByName<TextButton>("characterSelector");
	if (gm != nullptr && !gm->global.feature.qingyuUi &&
		gm->global.feature.menuResourceProfile == mrpDefault)
	{
		compactStatusLabels(*this);
	}
	updatePanelImage();
	setChildRectReferToParent();
}

void StateMenu::onUpdate()
{
	updateLabel();
}

void StateMenu::freeResource()
{
	attributePointEntryFocused = false;
	attributePointEntry.reset();
	characterSelectorFocused = false;
	characterSelector.reset();
	image = nullptr;
	loadedPanelIndex = -1;
	ConfigDrivenPanel::freeResource();
}

void StateMenu::onEvent()
{
	ConfigDrivenPanel::onEvent();
	if (attributePointEntry && attributePointEntry->visible && attributePointEntry->getResult(erClick))
	{
		gm->menu->openAttributePoints(shared_from_this(), attributePointEntry);
		return;
	}
	if (visible && characterSelector && characterSelector->getResult(erClick)
		&& gm && gm->menu && gm->menu->equipMenu)
	{
		gm->menu->equipMenu->cycleCharacter();
	}
	updateLabel();
}

bool StateMenu::activateControllerFocus(ControllerFocusTarget target)
{
	updateLabel();
	return target == ControllerFocusTarget::Default && focusControllerElement(
		attributePointEntry && attributePointEntry->visible ? PElement(attributePointEntry) : PElement(characterSelector));
}

bool StateMenu::isControllerFocusActive() const
{
	return (attributePointEntryFocused && attributePointEntry && attributePointEntry->visible)
		|| (characterSelectorFocused && characterSelector && characterSelector->visible && characterSelector->activated);
}

void StateMenu::deactivateControllerFocus()
{
	attributePointEntryFocused = false;
	if (attributePointEntry) attributePointEntry->setFocused(false);
	characterSelectorFocused = false;
	if (characterSelector) characterSelector->setFocused(false);
}

PElement StateMenu::controllerFocusedElement() const
{
	if (attributePointEntryFocused) return attributePointEntry;
	return isControllerFocusActive() ? characterSelector : nullptr;
}

std::vector<PElement> StateMenu::controllerFocusCandidates() const
{
	auto candidates = characterSelector && characterSelector->visible && characterSelector->activated
		? std::vector<PElement>{ characterSelector } : std::vector<PElement>{};
	if (attributePointEntry && attributePointEntry->visible) candidates.push_back(attributePointEntry);
	return candidates;
}

bool StateMenu::focusControllerElement(const PElement& element)
{
	if (element && element == attributePointEntry && element->visible && element->activated)
	{
		deactivateControllerFocus();
		attributePointEntryFocused = true;
		element->setFocused(true);
		return true;
	}
	if (!element || element != characterSelector || !element->visible || !element->activated) return false;
	deactivateControllerFocus();
	characterSelectorFocused = true;
	element->setFocused(true);
	return true;
}

bool StateMenu::onHandleUIAction(UIAction action)
{
	if (attributePointEntryFocused && action == UIAction::Confirm && gm && gm->menu)
		return gm->menu->openAttributePoints(shared_from_this(), attributePointEntry);
	if (action != UIAction::Confirm || !isControllerFocusActive()
		|| !gm || !gm->menu || !gm->menu->equipMenu) return false;
	return gm->menu->equipMenu->cycleCharacter();
}

void StateMenu::updateAttributePointEntry()
{
	const bool available = gm && gm->menu && gm->global.attributePointRules.enabled
		&& (!gm->menu->equipMenu || !gm->menu->equipMenu->getSelectedPartner());
	if (!attributePointEntry && available)
	{
		attributePointEntry = std::make_shared<FlatTextButton>();
		attributePointEntry->setUTF8Str(u8"属性分配");
		attributePointEntry->setFontSize(16);
		addChild(attributePointEntry);
	}
	if (attributePointEntry)
	{
		attributePointEntry->visible = attributePointEntry->activated = available;
		attributePointEntry->rect = { rect.x + std::max(0, rect.w - 100), rect.y + 8, 88, 44 };
		if (!available) { attributePointEntryFocused = false; attributePointEntry->setFocused(false); }
	}
}

void StateMenu::onSetChildRect()
{
	updateAttributePointEntry();
}
