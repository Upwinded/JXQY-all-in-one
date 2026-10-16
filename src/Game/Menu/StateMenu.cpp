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
	if (gm != nullptr &&
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
	characterSelectorFocused = false;
	characterSelector.reset();
	image = nullptr;
	loadedPanelIndex = -1;
	ConfigDrivenPanel::freeResource();
}

void StateMenu::onEvent()
{
	ConfigDrivenPanel::onEvent();
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
	return target == ControllerFocusTarget::Default && focusControllerElement(characterSelector);
}

bool StateMenu::isControllerFocusActive() const
{
	return characterSelectorFocused && characterSelector && characterSelector->visible && characterSelector->activated;
}

void StateMenu::deactivateControllerFocus()
{
	characterSelectorFocused = false;
	if (characterSelector) characterSelector->setFocused(false);
}

PElement StateMenu::controllerFocusedElement() const
{
	return isControllerFocusActive() ? characterSelector : nullptr;
}

std::vector<PElement> StateMenu::controllerFocusCandidates() const
{
	return characterSelector && characterSelector->visible && characterSelector->activated
		? std::vector<PElement>{ characterSelector } : std::vector<PElement>{};
}

bool StateMenu::focusControllerElement(const PElement& element)
{
	if (!element || element != characterSelector || !element->visible || !element->activated) return false;
	characterSelectorFocused = true;
	element->setFocused(true);
	return true;
}

bool StateMenu::onHandleUIAction(UIAction action)
{
	if (action != UIAction::Confirm || !isControllerFocusActive()
		|| !gm || !gm->menu || !gm->menu->equipMenu) return false;
	return gm->menu->equipMenu->cycleCharacter();
}
