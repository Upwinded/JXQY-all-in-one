#include "ColumnMenu.h"
#include "../GameManager/GameManager.h"
#include "../Data/Global.h"


ColumnMenu::ColumnMenu()
{
	name = "ColumnMenu";
	visible = true;
	needEvents = false;
	// Independent ColumnMenu (YYCS/XJXQY) needs higher draw priority than BottomMenu
	if (GameManager::getInstance()->global.feature.topButtonsLayout)
	{
		setPriority(epImage - 1);
	}
	init();
}

ColumnMenu::~ColumnMenu()
{
	freeResource();
}

void ColumnMenu::updateState()
{
	if (columnLife != nullptr)
	{
		columnLife->percent = (float)gm->player->life / (float)(gm->player->info.lifeMax > 1 ? gm->player->info.lifeMax : 1);
	}
	if (columnThew != nullptr)
	{
		columnThew->percent = (float)gm->player->thew / (float)(gm->player->info.thewMax > 1 ? gm->player->info.thewMax : 1);
	}
	if (columnMana != nullptr)
	{
		columnMana->percent = (float)gm->player->mana / (float)(gm->player->info.manaMax > 1 ? gm->player->info.manaMax : 1);
	}
	if (columnRage != nullptr)
	{
		columnRage->visible = gm->global.feature.rageSystem;
		columnRage->percent = (float)gm->player->rage / (float)(gm->player->rageMax > 1 ? gm->player->rageMax : 1);
	}
	
}

void ColumnMenu::onUpdate()
{
	updateState();
}

void ColumnMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\column\\column.menu.ini");

	columnLife = getComponentByName<ColumnImage>("columnLife");
	columnThew = getComponentByName<ColumnImage>("columnThew");
	columnMana = getComponentByName<ColumnImage>("columnMana");
	columnRage = getComponentByName<ColumnImage>("columnRage");
	image = getComponentByName<TransImage>("image");

	setChildRectReferToParent();
}

void ColumnMenu::freeResource()
{
	image = nullptr;
	columnLife = nullptr;
	columnThew = nullptr;
	columnMana = nullptr;
	columnRage = nullptr;
	ConfigDrivenPanel::freeResource();
}
