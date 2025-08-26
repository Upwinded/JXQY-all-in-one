#include "PracticeMenu.h"
#include "../GameManager/GameManager.h"


PracticeMenu::PracticeMenu()
{
	visible = false;
	init();
}


PracticeMenu::~PracticeMenu()
{
	freeResource();
}

void PracticeMenu::updateMagic()
{

	magic->impImage = nullptr;

	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != u8"")
	{
		name->setStr(gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->name);
		intro->setStr(gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->intro);
		level->setStr(convert::formatString("%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level));
		exp->setStr(convert::formatString("%d/%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp, gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->level[gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level].levelupExp));
		magic->impImage = gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->createMagicIcon();
	}
	else
	{
		name->setStr("");
		intro->setStr("");
		level->setStr("");
		exp->setStr("");
	}
}

void PracticeMenu::updateExp()
{
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != u8"")
	{
		exp->setStr(convert::formatString("%d/%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp, gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->level[gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level].levelupExp));
	}
	else
	{
		exp->setStr("");
	}
}

void PracticeMenu::updateLevel()
{
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != u8"")
	{
		level->setStr(convert::formatString("%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level));
	}
	else
	{
		level->setStr("");
	}
}

void PracticeMenu::onEvent()
{
	unsigned int ret = magic->getResult();
	if (ret & erShowHint)
	{
		if (gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].iniFile != u8"" && gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].magic != nullptr)
		{
			gm->menu->toolTip->visible = true;
			addChild(gm->menu->toolTip);
			gm->menu->toolTip->setMagic(gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].magic, gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].level);
		}
		else
		{
			gm->menu->toolTip->visible = false;
		}

	}
	if (ret & erHideHint)
	{
		gm->menu->toolTip->visible = false;
	}
	if (ret & erMouseRDown)
	{
		gm->menu->toolTip->visible = false;
		magic->resetHint();
	}
	if (ret & erDropped)
	{
		gm->menu->toolTip->visible = false;
		magic->resetHint();
		if (magic->dropType == dtMagic)
		{
			if (gm->magicManager.magicList[magic->dropIndex].iniFile != u8"" && gm->magicManager.magicList[magic->dropIndex].magic != nullptr)
			{
				gm->magicManager.exchange(magic->dropIndex, magic->dragIndex);
				updateMagic();
			}
			if (magic->dropIndex < MAGIC_COUNT)
			{
				gm->menu->magicMenu->updateMagic();
			}
			else if (magic->dropIndex < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
			{
				gm->menu->bottomMenu->updateMagicItem();
			}
		}
	}
}

void PracticeMenu::init()
{
	freeResource();
	initFromIniFileName(u8"ini\\ui\\equip\\window.ini");
	title = addComponent<ImageContainer>(u8"ini\\ui\\xiulian\\title.ini");
	image = addComponent<ImageContainer>(u8"ini\\ui\\xiulian\\image.ini");

	name = addComponent<Label>(u8"ini\\ui\\xiulian\\name.ini");
	intro = addComponent<Label>(u8"ini\\ui\\xiulian\\intro.ini");
	intro->autoNextLine = true;
	level = addComponent<Label>(u8"ini\\ui\\xiulian\\level.ini");
	exp = addComponent<Label>(u8"ini\\ui\\xiulian\\exp.ini");

	magic = addComponent<Item>(u8"ini\\ui\\xiulian\\magic.ini");
	magic->dragIndex = MAGIC_TOOLBAR_COUNT + MAGIC_COUNT;
	magic->dragType = dtMagic;
	magic->canShowHint = true;
	setChildRectReferToParent();
}

void PracticeMenu::freeResource()
{

	impImage = nullptr;

	freeCom(image);
	freeCom(title);
	freeCom(level);
	freeCom(exp);
	freeCom(name);
	freeCom(intro);
	freeCom(magic);

}
