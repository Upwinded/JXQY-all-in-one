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
	if (item[index] == nullptr)
	{
		return;
	}
	if (item[index]->impImage != nullptr)
	{
		IMP::clearIMPImage(item[index]->impImage);
		//delete item[index]->impImage;
		item[index]->impImage = nullptr;
	}	
	if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].iniFile == "" || gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods == nullptr || gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].number <= 0)
	{
		item[index]->setStr("");
		gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].iniFile = "";
		if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods != nullptr)
		{
			delete gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods;
			gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods = nullptr;
		}
		gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].number = 0;
	}
	else
	{	
		item[index]->impImage = gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].goods->createGoodsIcon();
		item[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + index].number));
	}
	gm->player->calInfo();
	gm->menu.stateMenu->updateLabel();
}

int EquipMenu::getPartIndex(const std::string & part)
{
	if (part == "head")
	{
		return 0;
	}
	else if (part == "neck")
	{
		return 1;
	}
	else if (part == "body")
	{
		return 2;
	}
	else if (part == "back")
	{
		return 3;
	}
	else if (part == "hand")
	{
		return 4;
	}
	else if (part == "wrist")
	{
		return 5;
	}
	else if (part == "foot")
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
			if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].iniFile != "" && gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods != nullptr && gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].number > 0)
			{
				gm->menu.toolTip->visible = true;
				gm->menu.toolTip->changeParent(this);
				gm->menu.toolTip->setGoods(gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods);
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
#ifdef _MOBILE
		if ((ret & erClick) && (ret & erMouseRDown) && false)
		{
			for (int j = 0; j < GOODS_COUNT; ++j)
			{
				if (gm->goodsManager.goodsList[j].iniFile == "")
				{
					gm->goodsManager.exchange(j, item[i]->dragIndex);
					updateGoods(i);
					gm->menu.goodsMenu->updateGoods();
					break;
				}
			}
			gm->menu.toolTip->visible = false;
			item[i]->resetHint();
		}
#else
		if (ret & erMouseRDown)
		{
			gm->menu.toolTip->visible = false;
			item[i]->resetHint();
		}
#endif

		if (ret & erDropped)
		{
			gm->menu.toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dropIndex].goods != nullptr && gm->goodsManager.goodsList[item[i]->dropIndex].number > 0)
				{
					if (getPartIndex(gm->goodsManager.goodsList[item[i]->dropIndex].goods->part) == i)
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods(i);
					}
					if (item[i]->dropIndex < GOODS_COUNT)
					{
						updateGoods(i);
						gm->menu.goodsMenu->updateGoods();
;					}
					else if (item[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
					{
						updateGoods(i);
						gm->menu.bottomMenu->updateGoodsItem();
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
		std::string iniName = convert::formatString("ini\\ui\\equip\\item%d.ini", i + 1);
		item[i] = addItem(iniName);
		item[i]->dragType = dtGoods;
		item[i]->dragIndex = GOODS_TOOLBAR_COUNT + GOODS_COUNT + i;
		item[i]->canShowHint = true;
	}
	setChildRectReferToParent();
}

void EquipMenu::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	freeCom(image);
	freeCom(title);
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		freeCom(item[i]);
	}
}
