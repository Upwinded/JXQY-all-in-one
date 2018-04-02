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
	if (columnLife != NULL)
	{
		columnLife->percent = (double)gm->player.life / (double)(gm->player.info.lifeMax > 1 ? gm->player.info.lifeMax : 1);
	}
	if (columnThew != NULL)
	{
		columnThew->percent = (double)gm->player.thew / (double)(gm->player.info.thewMax > 1 ? gm->player.info.thewMax : 1);
	}
	if (columnMana != NULL)
	{
		columnMana->percent = (double)gm->player.mana / (double)(gm->player.info.manaMax > 1 ? gm->player.info.manaMax : 1);
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
	columnLife = addColumnImage("ini\\ui\\column\\colLife.ini");
	columnThew = addColumnImage("ini\\ui\\column\\colThew.ini");
	columnMana = addColumnImage("ini\\ui\\column\\colMana.ini");
	image = addTransImage("ini\\ui\\column\\column.ini");

	setChildRect();
}

void ColumnMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(image);
	freeCom(columnLife);
	freeCom(columnThew);
	freeCom(columnMana);
}
