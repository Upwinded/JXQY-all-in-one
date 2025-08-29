#include "ColumnMenu.h"
#include "../GameManager/GameManager.h"


ColumnMenu::ColumnMenu()
{
	visible = true;
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
	
}

void ColumnMenu::onUpdate()
{
	updateState();
}

void ColumnMenu::init()
{
	freeResource();
	initFromIniFileName(u8"ini\\ui\\column\\window.ini");
	columnLife = addComponent<ColumnImage>(u8"ini\\ui\\column\\collife.ini");
	columnThew = addComponent<ColumnImage>(u8"ini\\ui\\column\\colthew.ini");
	columnMana = addComponent<ColumnImage>(u8"ini\\ui\\column\\colmana.ini");
	image = addComponent<TransImage>(u8"ini\\ui\\column\\column.ini");
	setChildRectReferToParent();
}

void ColumnMenu::freeResource()
{

	impImage = nullptr;

	freeCom(image);
	freeCom(columnLife);
	freeCom(columnThew);
	freeCom(columnMana);
}
