#include "PracticeMenu.h"
#include "../GameManager/GameManager.h"


PracticeMenu::PracticeMenu()
{
	init();
	visible = false;
}


PracticeMenu::~PracticeMenu()
{
	freeResource();
}

void PracticeMenu::updateMagic()
{
	if (magic->impImage != nullptr)
	{
		IMP::clearIMPImage(magic->impImage);
		//delete magic->impImage;
		magic->impImage = nullptr;
	}
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != "")
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
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != "")
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
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != "")
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
		if (gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].iniFile != "" && gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].magic != nullptr)
		{
			gm->menu.toolTip->visible = true;
			gm->menu.toolTip->changeParent(this);
			gm->menu.toolTip->setMagic(gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].magic, gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].level);
		}
		else
		{
			gm->menu.toolTip->visible = false;
		}

	}
	if (ret & erHideHint)
	{
		gm->menu.toolTip->visible = false;
	}
	if (ret & erMouseRDown)
	{
		gm->menu.toolTip->visible = false;
		magic->resetHint();
	}
	if (ret & erDropped)
	{
		gm->menu.toolTip->visible = false;
		magic->resetHint();
		if (magic->dropType == dtMagic)
		{
			if (gm->magicManager.magicList[magic->dropIndex].iniFile != "" && gm->magicManager.magicList[magic->dropIndex].magic != nullptr)
			{
				gm->magicManager.exchange(magic->dropIndex, magic->dragIndex);
				updateMagic();
			}
			if (magic->dropIndex < MAGIC_COUNT)
			{
				gm->menu.magicMenu->updateMagic();
			}
			else if (magic->dropIndex < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
			{
				gm->menu.bottomMenu->updateMagicItem();
			}
		}
	}
}

void PracticeMenu::init()
{
	freeResource();
	initFromIni("ini\\ui\\equip\\window.ini");
	title = addImageContainer("ini\\ui\\xiulian\\title.ini");
	image = addImageContainer("ini\\ui\\xiulian\\image.ini");

	name = addLabel("ini\\ui\\xiulian\\name.ini");
	intro = addLabel("ini\\ui\\xiulian\\intro.ini");
	intro->autoNextLine = true;
	level = addLabel("ini\\ui\\xiulian\\level.ini");
	exp = addLabel("ini\\ui\\xiulian\\exp.ini");

	magic = addItem("ini\\ui\\xiulian\\magic.ini");
	magic->dragIndex = MAGIC_TOOLBAR_COUNT + MAGIC_COUNT;
	magic->dragType = dtMagic;
	magic->canShowHint = true;
	setChildRectReferToParent();
}

void PracticeMenu::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	freeCom(image);
	freeCom(title);
	freeCom(level);
	freeCom(exp);
	freeCom(name);
	freeCom(intro);
	freeCom(magic);

}
