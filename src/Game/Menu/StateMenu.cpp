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
	needEvents = false;
	init();
}

StateMenu::~StateMenu()
{
	freeResource();
}

void StateMenu::updateLabel()
{
	updatePanelImage();

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

	if (labLevel) labLevel->setStr(convert::formatString("%d", gm->player->level));
	if (labExp) labExp->setStr(convert::formatString("%d", gm->player->exp));
	if (labExpUp) labExpUp->setStr(convert::formatString("%d", gm->player->levelUpExp));
	if (labAttack) labAttack->setStr(convert::formatString("%d", gm->player->info.attack));
	if (labDefend) labDefend->setStr(convert::formatString("%d", gm->player->info.defend));
	if (labEvade) labEvade->setStr(convert::formatString("%d", gm->player->info.evade));
	if (labLife) labLife->setStr(convert::formatString("%d/%d", gm->player->life, gm->player->info.lifeMax));
	if (labThew) labThew->setStr(convert::formatString("%d/%d", gm->player->thew, gm->player->info.thewMax));
	if (labMana) labMana->setStr(convert::formatString("%d/%d", gm->player->mana, gm->player->info.manaMax));
	if (labRage)
	{
		labRage->visible = gm->global.feature.rageSystem;
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
	image = nullptr;
	loadedPanelIndex = -1;
	ConfigDrivenPanel::freeResource();
}
