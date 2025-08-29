#include "MagicMenu.h"
#include "../GameManager/GameManager.h"


MagicMenu::MagicMenu()
{
	visible = false;
	init();
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

	item[index]->impImage = nullptr;

	if (gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + index].magic != nullptr)
	{
		item[index]->impImage = gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + index].magic->createMagicIcon();
	}
}

void MagicMenu::onEvent()
{
	if (scrollbar != nullptr && position != scrollbar->position)
	{
		position = scrollbar->position;
		updateMagic();
	}

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].iniFile != u8"" && gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].magic != nullptr)
			{
				gm->menu->toolTip->visible = true;
				addChild(gm->menu->toolTip);
				gm->menu->toolTip->setMagic(gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].magic, gm->magicManager.magicList[scrollbar->position * scrollbar->lineSize + i].level);
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
#ifdef __MOBILE__
		if (ret & erClick || ret & erMouseRDown)
#else
		if (ret & erMouseRDown)
#endif
		{
			if (gm->magicManager.magicListExists(item[i]->dragIndex))
			{
				if (gm->menu->practiceMenu != nullptr && gm->menu->practiceMenu->visible == true)
				{
					gm->magicManager.exchange(item[i]->dragIndex, MAGIC_TOOLBAR_COUNT + MAGIC_COUNT);
					updateMagic(i);
					gm->menu->practiceMenu->updateMagic();
				}
				else if (gm->menu->bottomMenu != nullptr)
				{
					for (size_t j = MAGIC_COUNT; j < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT; ++j)
					{
						if (!gm->magicManager.magicListExists(j))
						{
							gm->magicManager.exchange(item[i]->dragIndex, j);
							updateMagic(i);
							gm->menu->bottomMenu->updateMagicItem(j - MAGIC_COUNT);
							break;
						}
					}
				}
			}

			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
		}
		if (ret & erDropped)
		{
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicList[item[i]->dropIndex].iniFile != u8"" && gm->magicManager.magicList[item[i]->dropIndex].magic != nullptr)
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
						gm->menu->bottomMenu->updateMagicItem();
					}
					else
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->menu->practiceMenu->updateMagic();
					}
				}
			}
		}
	}
}

void MagicMenu::init()
{
	freeResource();
	initFromIniFileName(u8"ini\\ui\\magic\\window.ini");
	title = addComponent<ImageContainer>(u8"ini\\ui\\magic\\title.ini");
	image = addComponent<ImageContainer>(u8"ini\\ui\\magic\\dragbox.ini");
	scrollbar = addComponent<Scrollbar>(u8"ini\\ui\\magic\\scrollbar.ini");

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		std::string itemName = convert::formatString(u8"ini\\ui\\magic\\item%d.ini", i + 1);
		item[i] = addComponent<Item>(itemName);
		item[i]->dragType = dtMagic;
		item[i]->canShowHint = true;
	}

	setChildRectReferToParent();
}

void MagicMenu::freeResource()
{
	impImage = nullptr;
	freeCom(title);
	freeCom(image);
	freeCom(scrollbar);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		freeCom(item[i]);
	}
}
