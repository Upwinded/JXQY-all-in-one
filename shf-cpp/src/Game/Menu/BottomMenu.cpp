#include "BottomMenu.h"
#include "../GameManager/GameManager.h"
#include "Option.h"

BottomMenu::BottomMenu()
{
	init();
	visible = true;
}

BottomMenu::~BottomMenu()
{
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
		if (goodsItem[index] != NULL)
		{
			if (goodsItem[index]->impImage != NULL)
			{
				IMP::clearIMPImage(goodsItem[index]->impImage);
				delete goodsItem[index]->impImage;
				goodsItem[index]->impImage = NULL;
			}
			updateGoodsNumber(index);
			if (GameManager::getInstance()->goodsManager.goodsList[GOODS_COUNT + index].goods != NULL)
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
		if (goodsItem[index] != NULL)
		{		
			if (gm->goodsManager.goodsList[GOODS_COUNT + index].goods != NULL && gm->goodsManager.goodsList[GOODS_COUNT + index].number > 0 && gm->goodsManager.goodsList[GOODS_COUNT + index].iniFile != "")
			{
				goodsItem[index]->setStr(convert::GBKToUnicode(convert::formatString("%d", gm->goodsManager.goodsList[GOODS_COUNT + index].number)));
			}
			else
			{
				goodsItem[index]->setStr(L"");
				if (gm->goodsManager.goodsList[GOODS_COUNT + index].goods != NULL)
				{
					delete gm->goodsManager.goodsList[GOODS_COUNT + index].goods;
					gm->goodsManager.goodsList[GOODS_COUNT + index].goods = NULL;
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
		if (magicItem[index] != NULL)
		{
			if (magicItem[index]->impImage != NULL)
			{
				IMP::clearIMPImage(magicItem[index]->impImage);
				delete magicItem[index]->impImage;
				magicItem[index]->impImage = NULL;
			}
			if (gm->magicManager.magicList[MAGIC_COUNT + index].magic != NULL)
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
	initFromIni("ini\\ui\\bottom\\window.ini");
	equipBtn = addCheckBox("ini\\ui\\bottom\\BtnEquip.ini");
	goodsBtn = addCheckBox("ini\\ui\\bottom\\BtnGoods.ini");
	magicBtn = addCheckBox("ini\\ui\\bottom\\BtnMagic.ini");
	notesBtn = addCheckBox("ini\\ui\\bottom\\BtnNotes.ini");
	optionBtn = addCheckBox("ini\\ui\\bottom\\BtnOption.ini");
	stateBtn = addCheckBox("ini\\ui\\bottom\\BtnState.ini");
	xiulianBtn = addCheckBox("ini\\ui\\bottom\\BtnXiuLian.ini");

	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\bottom\\Item%d.ini", i + 1);
		goodsItem[i] = addItem(itemName);
		goodsItem[i]->dragType = dtGoods;
		goodsItem[i]->dragIndex = GOODS_COUNT + i;
		goodsItem[i]->canShowHint = true;
	}

	for (size_t i = 0; i <  MAGIC_TOOLBAR_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\bottom\\Item%d.ini", i + 1 + GOODS_TOOLBAR_COUNT);
		magicItem[i] = addItem(itemName);
		magicItem[i]->dragType = dtMagic;
		magicItem[i]->dragIndex = MAGIC_COUNT + i;
		magicItem[i]->canShowHint = true;
	}

	setChildRect();
}

void BottomMenu::onEvent()
{
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		int ret = goodsItem[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsList[GOODS_COUNT + i].iniFile != "" && gm->goodsManager.goodsList[GOODS_COUNT + i].goods != NULL && gm->goodsManager.goodsList[GOODS_COUNT + i].number > 0)
			{
				gm->toolTip->visible = true;
				gm->toolTip->changeParent(this);
				gm->toolTip->setGoods(gm->goodsManager.goodsList[GOODS_COUNT + i].goods);
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
			goodsItem[i]->resetHint();
			gm->goodsManager.useItem(goodsItem[i]->dragIndex);
		}
		if (ret & erDropped)
		{
			gm->toolTip->visible = false;
			goodsItem[i]->resetHint();
			
			if (goodsItem[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsList[goodsItem[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[goodsItem[i]->dropIndex].goods != NULL && gm->goodsManager.goodsList[goodsItem[i]->dropIndex].number > 0)
				{	
					if (goodsItem[i]->dropIndex < GOODS_COUNT)
					{						
						gm->goodsManager.exchange(i + GOODS_COUNT, goodsItem[i]->dropIndex);
						gm->goodsMenu->updateGoods();
						updateGoodsItem(i);
					}
					else if (goodsItem[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
					{
						gm->goodsManager.exchange(i + GOODS_COUNT, goodsItem[i]->dropIndex);
						updateGoodsItem();			
					}
					else if (goodsItem[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT)
					{
						if (gm->goodsManager.goodsList[i + GOODS_COUNT].goods == NULL || (gm->goodsManager.goodsList[i + GOODS_COUNT].goods->part == gm->goodsManager.goodsList[goodsItem[i]->dropIndex].goods->part))
						{
							gm->goodsManager.exchange(i + GOODS_COUNT, goodsItem[i]->dropIndex);
							gm->equipMenu->updateGoods(goodsItem[i]->dropIndex - GOODS_COUNT - GOODS_TOOLBAR_COUNT);
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
			if (gm->magicManager.magicList[MAGIC_COUNT + i].iniFile != "" && gm->magicManager.magicList[MAGIC_COUNT + i].magic != NULL)
			{
				gm->toolTip->visible = true;
				gm->toolTip->changeParent(this);
				gm->toolTip->setMagic(gm->magicManager.magicList[MAGIC_COUNT + i].magic, gm->magicManager.magicList[MAGIC_COUNT + i].level);
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
			magicItem[i]->resetHint();
		}
		if (ret & erDropped)
		{	
			gm->toolTip->visible = false;
			magicItem[i]->resetHint();
			if (magicItem[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicList[magicItem[i]->dropIndex].iniFile != "" && gm->magicManager.magicList[magicItem[i]->dropIndex].magic != NULL)
				{
					if (magicItem[i]->dropIndex < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + 1)
					{
						gm->magicManager.exchange(MAGIC_COUNT + i, magicItem[i]->dropIndex);
						updateMagicItem();
					}
					if (magicItem[i]->dropIndex < MAGIC_COUNT)
					{
						gm->magicMenu->updateMagic();
					}
					else if (magicItem[i]->dropIndex == MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
					{
						gm->practiceMenu->updateMagic();
					}
				}
			}	
		}
	}

	if (optionBtn->getResult(erClick))
	{
		if (dragging)
		{
			dragging = false;
		}
		optionBtn->checked = true;
		Option * option = new Option;
		option->priority = epMax;
		addChild(option);
		option->run();
		removeChild(option);
		delete option;
		option = NULL;
		optionBtn->checked = false;
	}
	if (equipBtn->getResult(erClick))
	{
		if (equipBtn->checked)
		{
			gm->equipMenu->visible = true;
			gm->stateMenu->visible = false;
			gm->practiceMenu->visible = false;
			stateBtn->checked = false;
			xiulianBtn->checked = false;
		}
		else
		{
			gm->equipMenu->visible = false;
		}
	}
	if (stateBtn->getResult(erClick))
	{
		if (stateBtn->checked)
		{
			gm->stateMenu->visible = true;
			gm->equipMenu->visible = false;
			gm->practiceMenu->visible = false;
			equipBtn->checked = false;
			xiulianBtn->checked = false;
		}
		else
		{
			gm->stateMenu->visible = false;
		}
	}
	if (xiulianBtn->getResult(erClick))
	{
		if (xiulianBtn->checked)
		{
			gm->practiceMenu->visible = true;
			gm->equipMenu->visible = false;
			gm->stateMenu->visible = false;
			equipBtn->checked = false;
			stateBtn->checked = false;
		}
		else
		{
			gm->practiceMenu->visible = false;
		}
	}
	if (goodsBtn->getResult(erClick))
	{
		if (goodsBtn->checked)
		{
			gm->goodsMenu->visible = true;
			gm->magicMenu->visible = false;
			gm->memoMenu->visible = false;
			magicBtn->checked = false;
			notesBtn->checked = false;
		}
		else
		{
			gm->goodsMenu->visible = false;
		}
	}
	if (magicBtn->getResult(erClick))
	{
		if (magicBtn->checked)
		{
			gm->magicMenu->visible = true;
			gm->goodsMenu->visible = false;
			gm->memoMenu->visible = false;
			goodsBtn->checked = false;
			notesBtn->checked = false;
		}
		else
		{
			gm->magicMenu->visible = false;
		}
	}
	if (notesBtn->getResult(erClick))
	{
		if (notesBtn->checked)
		{
			gm->memoMenu->visible = true;
			gm->goodsMenu->visible = false;
			gm->magicMenu->visible = false;
			goodsBtn->checked = false;
			magicBtn->checked = false;
		}
		else
		{
			gm->memoMenu->visible = false;
		}
	}
}

void BottomMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
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
