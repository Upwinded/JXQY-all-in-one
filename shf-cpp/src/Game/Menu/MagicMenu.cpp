#include "MagicMenu.h"
#include "../GameManager/GameManager.h"


MagicMenu::MagicMenu()
{
	init();
	visible = false;
}


MagicMenu::~MagicMenu()
{
	freeResource();
}

void MagicMenu::updateMagic()
{
	for (int i = 0; i < MENU_ITEM_COUNT; i++)
	{
		item[i]->dragIndex = i + scrollbar->position * scrollbar->lineSize;
		updateMagic(i);
	}
}

void MagicMenu::updateMagic(int index)
{
	if (index < 0 || index >= scrollbar->pageSize)
	{
		return;
	}
	if (item[index]->impImage != NULL)
	{
		IMP::clearIMPImage(item[index]->impImage);
		delete item[index]->impImage;
		item[index]->impImage = NULL;
	}
	if (gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + index].magic != NULL)
	{
		item[index]->impImage = gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + index].magic->createMagicIcon();
	}
}

void MagicMenu::onEvent()
{
	if (scrollbar != NULL && position != scrollbar->position)
	{
		position = scrollbar->position;
		updateMagic();
	}

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].magic != NULL)
			{
				gm->toolTip->visible = true;
				gm->toolTip->changeParent(this);
				gm->toolTip->setMagic(gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].magic, gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].level);
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
			item[i]->resetHint();
		}
		if (ret & erDropped)
		{
			gm->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicList[item[i]->dropIndex].iniFile != "" && gm->magicManager.magicList[item[i]->dropIndex].magic != NULL)
				{
					if (item[i]->dropIndex < MAGIC_COUNT)
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic();
					}
					else if (item[i]->dropIndex < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->bottomMenu->updateMagicItem();
					}
					else
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->practiceMenu->updateMagic();
					}
				}
			}
		}
	}
}

void MagicMenu::init()
{
	freeResource();
	initFromIni("ini\\ui\\magic\\window.ini");
	title = addImageContainer("ini\\ui\\magic\\Title.ini");
	image = addImageContainer("ini\\ui\\magic\\DragBox.ini");
	scrollbar = addScrollbar("ini\\ui\\magic\\Scrollbar.ini");

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\magic\\item%d.ini", i + 1);
		item[i] = addItem(itemName);
		item[i]->dragType = dtMagic;
		item[i]->canShowHint = true;
	}

	setChildRect();
}

void MagicMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(title);
	freeCom(image);
	freeCom(scrollbar);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		freeCom(item[i]);
	}
}
