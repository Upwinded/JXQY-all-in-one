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
		columnLife->percent = (double)gm->player->life / (double)(gm->player->info.lifeMax > 1 ? gm->player->info.lifeMax : 1);
	}
	if (columnThew != nullptr)
	{
		columnThew->percent = (double)gm->player->thew / (double)(gm->player->info.thewMax > 1 ? gm->player->info.thewMax : 1);
	}
	if (columnMana != nullptr)
	{
		columnMana->percent = (double)gm->player->mana / (double)(gm->player->info.manaMax > 1 ? gm->player->info.manaMax : 1);
	}
	
}

void ColumnMenu::onUpdate()
{
	updateState();
}

void ColumnMenu::init()
{
	freeResource();
	initFromIniFileName("ini\\ui\\column\\window.ini");
	columnLife = addComponent<ColumnImage>("ini\\ui\\column\\collife.ini");
	columnThew = addComponent<ColumnImage>("ini\\ui\\column\\colthew.ini");
	columnMana = addComponent<ColumnImage>("ini\\ui\\column\\colmana.ini");
	image = addComponent<TransImage>("ini\\ui\\column\\column.ini");
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
