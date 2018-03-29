#include "EquipMenu.h"
#include "../GameManager/GameManager.h"


EquipMenu::EquipMenu()
{
	init();
	visible = false;
}


EquipMenu::~EquipMenu()
{
	freeResource();
}

void EquipMenu::updateGoods()
{
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		updateGoods(i);
	}
}

void EquipMenu::updateGoods(int index)
{
	if (index < 0 || index >= GOODS_BODY_COUNT)
	{
		return;
	}
	if (item[index] == NULL)
	{
		return;
	}
	if (item[index]->impImage != NULL)
	{
		IMP::clearIMPImage(item[index]->impImage);
		delete item[index]->impImage;
		item[index]->impImage = NULL;
	}	
	if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].iniFile == "" || gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods == NULL || gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].number <= 0)
	{
		item[index]->setStr(L"");
		gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].iniFile = "";
		if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods != NULL)
		{
			delete gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods;
			gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods = NULL;
		}
		gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].number = 0;
	}
	else
	{	
		item[index]->impImage = gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods->createGoodsIcon();
		item[index]->setStr(convert::GBKToUnicode(convert::formatString("%d", gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].number)));
	}
	gm->player.calInfo();
	gm->stateMenu->updateLabel();
}

int EquipMenu::getPartIndex(const std::string & part)
{
	if (part == "Head")
	{
		return 0;
	}
	else if (part == "Neck")
	{
		return 1;
	}
	else if (part == "Body")
	{
		return 2;
	}
	else if (part == "Back")
	{
		return 3;
	}
	else if (part == "Hand")
	{
		return 4;
	}
	else if (part == "Wrist")
	{
		return 5;
	}
	else if (part == "Foot")
	{
		return 6;
	}
	return -1;
}

void EquipMenu::onEvent()
{
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		unsigned int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].iniFile != "" && gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods != NULL && gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].number > 0)
			{
				gm->toolTip->visible = true;
				gm->toolTip->changeParent(this);
				gm->toolTip->setGoods(gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods);
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
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dropIndex].goods != NULL && gm->goodsManager.goodsList[item[i]->dropIndex].number > 0)
				{
					if (getPartIndex(gm->goodsManager.goodsList[item[i]->dropIndex].goods->part) == i)
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods(i);
					}
					if (item[i]->dropIndex < GOODS_COUNT)
					{
						updateGoods(i);
						gm->goodsMenu->updateGoods();
;					}
					else if (item[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
					{
						updateGoods(i);
						gm->bottomMenu->updateGoodsItem();
					}
					else
					{
						updateGoods();
					}
				}
			}
		}
	}
}

void EquipMenu::init()
{
	freeResource();
	initFromIni("ini\\ui\\equip\\window.ini");
	title = addImageContainer("ini\\ui\\equip\\title.ini");
	image = addImageContainer("ini\\ui\\equip\\image.ini");
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		std::string iniName = convert::formatString("ini\\ui\\equip\\Item%d.ini", i + 1);
		item[i] = addItem(iniName);
		item[i]->dragType = dtGoods;
		item[i]->dragIndex = GOODS_TOOLBAR_COUNT + GOODS_COUNT + i;
		item[i]->canShowHint = true;
	}
	setChildRect();
}

void EquipMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(image);
	freeCom(title);
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		freeCom(item[i]);
	}
}
