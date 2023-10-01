#include "BottomMenu.h"
#include "../GameManager/GameManager.h"
#include "Option.h"

BottomMenu::BottomMenu()
{
	init();
	visible = true;
	columnMenu = std::make_shared<ColumnMenu>();
	addChild(columnMenu);
}

BottomMenu::~BottomMenu()
{
	removeAllChild();
	freeResource();
}

void BottomMenu::updateGoodsItem()
{
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		updateGoodsItem(i);
	}
}

void BottomMenu::updateGoodsItem(int index)
{
	if (index >= 0 && index < GOODS_TOOLBAR_COUNT)
	{
		if (goodsItem[index] != nullptr)
		{

			goodsItem[index]->impImage = nullptr;

			updateGoodsNumber(index);
			if (GameManager::getInstance()->goodsManager.goodsList[GOODS_COUNT + index].goods != nullptr)
			{
				goodsItem[index]->impImage = GameManager::getInstance()->goodsManager.goodsList[GOODS_COUNT + index].goods->createGoodsIcon();
				goodsItem[index]->canDrag = true;
			}		
			else
			{
				goodsItem[index]->canDrag = false;
			}
		}
	}
}

void BottomMenu::updateGoodsNumber()
{
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		updateGoodsNumber(i);
	}
}

void BottomMenu::updateGoodsNumber(int index)
{
	if (index >= 0 && index < GOODS_TOOLBAR_COUNT)
	{
		if (goodsItem[index] != nullptr)
		{		
			if (gm->goodsManager.goodsList[GOODS_COUNT + index].goods != nullptr && gm->goodsManager.goodsList[GOODS_COUNT + index].number > 0 && gm->goodsManager.goodsList[GOODS_COUNT + index].iniFile != "")
			{
				goodsItem[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[GOODS_COUNT + index].number));
			}
			else
			{
				goodsItem[index]->setStr("");
				if (gm->goodsManager.goodsList[GOODS_COUNT + index].goods != nullptr)
				{
					gm->goodsManager.goodsList[GOODS_COUNT + index].goods = nullptr;
				}
				gm->goodsManager.goodsList[GOODS_COUNT + index].iniFile = "";
				gm->goodsManager.goodsList[GOODS_COUNT + index].number = 0;
			}
		}
	}
}

void BottomMenu::updateMagicItem()
{
	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		updateMagicItem(i);
	}
}

void BottomMenu::updateMagicItem(int index)
{
	if (index >= 0 && index < MAGIC_TOOLBAR_COUNT)
	{
		if (magicItem[index] != nullptr)
		{

			magicItem[index]->impImage = nullptr;

			if (gm->magicManager.magicList[MAGIC_COUNT + index].magic != nullptr)
			{
				magicItem[index]->impImage = GameManager::getInstance()->magicManager.magicList[MAGIC_COUNT + index].magic->createMagicIcon();
				magicItem[index]->canDrag = true;
			}
			else
			{
				magicItem[index]->canDrag = false;
			}
		}
	}
}

void BottomMenu::init()
{
	freeResource();
	initFromIniFileName("ini\\ui\\bottom\\window.ini");
	equipBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btnequip.ini");
	goodsBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btngoods.ini");
	magicBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btnmagic.ini");
	notesBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btnnotes.ini");
	optionBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btnoption.ini");
	stateBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btnstate.ini");
	xiulianBtn = addComponent<CheckBox>("ini\\ui\\bottom\\btnxiulian.ini");

	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\bottom\\item%d.ini", i + 1);
		goodsItem[i] = addComponent<Item>(itemName);
		goodsItem[i]->dragType = dtGoods;
		goodsItem[i]->dragIndex = GOODS_COUNT + i;
		goodsItem[i]->canShowHint = true;
	}

	for (size_t i = 0; i <  MAGIC_TOOLBAR_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\bottom\\item%d.ini", i + 1 + GOODS_TOOLBAR_COUNT);
		magicItem[i] = addComponent<Item>(itemName);
		magicItem[i]->dragType = dtMagic;
		magicItem[i]->dragIndex = MAGIC_COUNT + i;
		magicItem[i]->canShowHint = true;
	}

	setChildRectReferToParent();
}

void BottomMenu::onEvent()
{
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		int ret = goodsItem[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsList[GOODS_COUNT + i].iniFile != "" && gm->goodsManager.goodsList[GOODS_COUNT + i].goods != nullptr && gm->goodsManager.goodsList[GOODS_COUNT + i].number > 0)
			{
				gm->menu->toolTip->visible = true;
				addChild(gm->menu->toolTip);
				gm->menu->toolTip->setGoods(gm->goodsManager.goodsList[GOODS_COUNT + i].goods);
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
		if (ret & erMouseRDown || ret & erClick)
#else
		if (ret & erMouseRDown)
#endif
		{
			gm->menu->toolTip->visible = false;
			goodsItem[i]->resetHint();
			gm->goodsManager.useItem(goodsItem[i]->dragIndex);
		}
		if (ret & erDropped)
		{
			gm->menu->toolTip->visible = false;
			goodsItem[i]->resetHint();
			
			if (goodsItem[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsList[goodsItem[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[goodsItem[i]->dropIndex].goods != nullptr && gm->goodsManager.goodsList[goodsItem[i]->dropIndex].number > 0)
				{	
					if (goodsItem[i]->dropIndex < GOODS_COUNT)
					{						
						gm->goodsManager.exchange(i + GOODS_COUNT, goodsItem[i]->dropIndex);
						gm->menu->goodsMenu->updateGoods();
						updateGoodsItem(i);
					}
					else if (goodsItem[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
					{
						gm->goodsManager.exchange(i + GOODS_COUNT, goodsItem[i]->dropIndex);
						updateGoodsItem();			
					}
					else if (goodsItem[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT)
					{
						if (gm->goodsManager.goodsList[i + GOODS_COUNT].goods == nullptr || (gm->goodsManager.goodsList[i + GOODS_COUNT].goods->part == gm->goodsManager.goodsList[goodsItem[i]->dropIndex].goods->part))
						{
							gm->goodsManager.exchange(i + GOODS_COUNT, goodsItem[i]->dropIndex);
							gm->menu->equipMenu->updateGoods(goodsItem[i]->dropIndex - GOODS_COUNT - GOODS_TOOLBAR_COUNT);
							updateGoodsItem(i);
						}
					}
				}			
			}
		}	
	}
	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		int ret = magicItem[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->magicManager.magicListExists(MAGIC_COUNT + i))
			{
				gm->menu->toolTip->visible = true;
				addChild(gm->menu->toolTip);
				gm->menu->toolTip->setMagic(gm->magicManager.magicList[MAGIC_COUNT + i].magic, gm->magicManager.magicList[MAGIC_COUNT + i].level);
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
			magicItem[i]->resetHint();
		}
		if (ret & erDropped)
		{	
			gm->menu->toolTip->visible = false;
			magicItem[i]->resetHint();
			if (magicItem[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicListExists(magicItem[i]->dropIndex))
				{
					if (magicItem[i]->dropIndex < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + 1)
					{
						gm->magicManager.exchange(MAGIC_COUNT + i, magicItem[i]->dropIndex);
						updateMagicItem();
					}
					if (magicItem[i]->dropIndex < MAGIC_COUNT)
					{
						gm->menu->magicMenu->updateMagic();
					}
					else if (magicItem[i]->dropIndex == MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
					{
						gm->menu->practiceMenu->updateMagic();
					}
				}
			}	
		}
	}

	if (optionBtn->getResult(erClick))
	{
		optionBtn->checked = true;
		gm->controller->setPaused(true);
		if (currentDragItem != nullptr)
		{
			currentDragItem->dragEnd();
		}
		if (dragging != TOUCH_UNTOUCHEDID)
		{
			dragging = TOUCH_UNTOUCHEDID;
		}
		bool vis = gm->menu->visible;
		gm->menu->visible = false;
		removeChild(gm->menu->toolTip);
		std::shared_ptr<System> system = std::make_shared<System>();
		gm->addChild(system);
		unsigned int ret = system->run();
		gm->removeChild(system);
		system = nullptr;
		if ((ret & erExit) != 0)
		{
			gm->result = erExit;
			gm->setRunning(false);
		}
		gm->controller->setPaused(false);
		gm->menu->visible = vis;
		optionBtn->checked = false;

		/*std::shared_ptr<Option> option = std::make_shared<Option>();
		option->priority = epMax;
		addChild(option.get());
		option->run();
		removeChild(option.get());
		option = nullptr;
		optionBtn->checked = false;*/
	}
	if (equipBtn->getResult(erClick))
	{
		if (equipBtn->checked)
		{
			gm->menu->equipMenu->visible = true;
			gm->menu->stateMenu->visible = false;
			gm->menu->practiceMenu->visible = false;
			stateBtn->checked = false;
			xiulianBtn->checked = false;
		}
		else
		{
			gm->menu->equipMenu->visible = false;
		}
	}
	if (stateBtn->getResult(erClick))
	{
		if (stateBtn->checked)
		{
			gm->menu->stateMenu->visible = true;
			gm->menu->equipMenu->visible = false;
			gm->menu->practiceMenu->visible = false;
			equipBtn->checked = false;
			xiulianBtn->checked = false;
		}
		else
		{
			gm->menu->stateMenu->visible = false;
		}
	}
	if (xiulianBtn->getResult(erClick))
	{
		if (xiulianBtn->checked)
		{
			gm->menu->practiceMenu->visible = true;
			gm->menu->equipMenu->visible = false;
			gm->menu->stateMenu->visible = false;
			equipBtn->checked = false;
			stateBtn->checked = false;
		}
		else
		{
			gm->menu->practiceMenu->visible = false;
		}
	}
	if (goodsBtn->getResult(erClick))
	{
		if (goodsBtn->checked)
		{
			gm->menu->goodsMenu->visible = true;
			gm->menu->magicMenu->visible = false;
			gm->menu->memoMenu->visible = false;
			magicBtn->checked = false;
			notesBtn->checked = false;
		}
		else
		{
			gm->menu->goodsMenu->visible = false;
		}
	}
	if (magicBtn->getResult(erClick))
	{
		if (magicBtn->checked)
		{
			gm->menu->magicMenu->visible = true;
			gm->menu->goodsMenu->visible = false;
			gm->menu->memoMenu->visible = false;
			goodsBtn->checked = false;
			notesBtn->checked = false;
		}
		else
		{
			gm->menu->magicMenu->visible = false;
		}
	}
	if (notesBtn->getResult(erClick))
	{
		if (notesBtn->checked)
		{
			gm->menu->memoMenu->visible = true;
			gm->menu->goodsMenu->visible = false;
			gm->menu->magicMenu->visible = false;
			goodsBtn->checked = false;
			magicBtn->checked = false;
		}
		else
		{
			gm->menu->memoMenu->visible = false;
		}
	}
}

void BottomMenu::freeResource()
{

	impImage = nullptr;

	freeCom(equipBtn);
	freeCom(goodsBtn);
	freeCom(magicBtn);
	freeCom(optionBtn);
	freeCom(notesBtn);
	freeCom(stateBtn);
	freeCom(xiulianBtn);
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		freeCom(goodsItem[i]);
	}
	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		freeCom(magicItem[i]);
	}
}
