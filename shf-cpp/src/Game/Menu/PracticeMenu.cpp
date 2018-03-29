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
	if (magic->impImage != NULL)
	{
		IMP::clearIMPImage(magic->impImage);
		delete magic->impImage;
		magic->impImage = NULL;
	}
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != NULL && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != "")
	{
		name->setStr(convert::GBKToUnicode(gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->name));
		intro->setStr(convert::GBKToUnicode(gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->intro));
		level->setStr(convert::GBKToUnicode(convert::formatString("%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level)));
		exp->setStr(convert::GBKToUnicode(convert::formatString("%d/%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp, gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->level[gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level].levelupExp)));
		magic->impImage = gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->createMagicIcon();
	}
	else
	{
		name->setStr(L"");
		intro->setStr(L"");
		level->setStr(L"");
		exp->setStr(L"");
	}
}

void PracticeMenu::updateExp()
{
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != NULL && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != "")
	{
		exp->setStr(convert::GBKToUnicode(convert::formatString("%d/%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp, gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->level[gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level].levelupExp)));
	}
	else
	{
		exp->setStr(L"");
	}
}

void PracticeMenu::updateLevel()
{
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != NULL && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].iniFile != "")
	{
		level->setStr(convert::GBKToUnicode(convert::formatString("%d", gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level)));
	}
	else
	{
		level->setStr(L"");
	}
}

void PracticeMenu::onEvent()
{
	
	unsigned int ret = magic->getResult();
	if (ret & erShowHint)
	{
		if (gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].iniFile != "" && gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].magic != NULL)
		{
			gm->toolTip->visible = true;
			gm->toolTip->changeParent(this);
			gm->toolTip->setMagic(gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].magic, gm->magicManager.magicList[MAGIC_TOOLBAR_COUNT + MAGIC_COUNT].level);
		}
		else
		{
			gm->toolTip->visible = false;
		}

	}
	if (ret & erHideHint)
	{
		gm->toolTip->visible = false;
	}
	if (ret & erMouseRDown)
	{
		gm->toolTip->visible = false;
		magic->resetHint();
	}
	if (ret & erDropped)
	{
		gm->toolTip->visible = false;
		magic->resetHint();
		if (magic->dropType == dtMagic)
		{
			if (gm->magicManager.magicList[magic->dropIndex].iniFile != "" && gm->magicManager.magicList[magic->dropIndex].magic != NULL)
			{
				gm->magicManager.exchange(magic->dropIndex, magic->dragIndex);
				updateMagic();
			}
			if (magic->dropIndex < MAGIC_COUNT)
			{
				gm->magicMenu->updateMagic();
			}
			else if (magic->dropIndex < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
			{
				gm->bottomMenu->updateMagicItem();
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

	name = addLabel("ini\\ui\\xiulian\\Name.ini");
	intro = addLabel("ini\\ui\\xiulian\\Intro.ini");
	intro->autoNextLine = true;
	level = addLabel("ini\\ui\\xiulian\\Level.ini");
	exp = addLabel("ini\\ui\\xiulian\\Exp.ini");

	magic = addItem("ini\\ui\\xiulian\\Magic.ini");
	magic->dragIndex = MAGIC_TOOLBAR_COUNT + MAGIC_COUNT;
	magic->dragType = dtMagic;
	magic->canShowHint = true;
	setChildRect();
}

void PracticeMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(image);
	freeCom(title);
	freeCom(level);
	freeCom(exp);
	freeCom(name);
	freeCom(intro);
	freeCom(magic);

}
