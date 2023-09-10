#include "ColumnMenu.h"
#include "../GameManager/GameManager.h"


ColumnMenu::ColumnMenu()
{
	init();
	visible = true;
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
	initFromIni("ini\\ui\\column\\window.ini");
	columnLife = addColumnImage("ini\\ui\\column\\collife.ini");
	columnThew = addColumnImage("ini\\ui\\column\\colthew.ini");
	columnMana = addColumnImage("ini\\ui\\column\\colmana.ini");
	image = addTransImage("ini\\ui\\column\\column.ini");
	setChildRectReferToParent();
}

void ColumnMenu::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	freeCom(image);
	freeCom(columnLife);
	freeCom(columnThew);
	freeCom(columnMana);
}
