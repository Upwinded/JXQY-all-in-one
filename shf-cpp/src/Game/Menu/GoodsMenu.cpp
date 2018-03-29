#include "GoodsMenu.h"
#include "../GameManager/GameManager.h"
#include "BuySellMenu.h"

GoodsMenu::GoodsMenu()
{
	init();
	visible = false;
}

GoodsMenu::~GoodsMenu()
{
	freeResource();
}



void GoodsMenu::updateMoney()
{
	money->setStr(convert::GBKToUnicode(convert::formatString("%d", gm->player.money)));
}

void GoodsMenu::updateGoods()
{
	for (int i = 0; i < MENU_ITEM_COUNT; i++)
	{
		item[i]->dragIndex = i + scrollbar->position * scrollbar->lineSize;
		updateGoods(i);
	}
}

void GoodsMenu::updateGoods(int index)
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
	updateGoodsNumber(index);
	if (gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods != NULL)
	{
		item[index]->impImage = gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods->createGoodsIcon();
	}
	
}

void GoodsMenu::updateGoodsNumber(int index)
{
	if (index < 0 || index >= scrollbar->pageSize)
	{
		return;
	}
	if (gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods != NULL && gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].number > 0 && gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].iniFile != "")
	{
		item[index]->setStr(convert::GBKToUnicode(convert::formatString("%d", gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].number)));
	}
	else
	{
		item[index]->setStr(L"");
		if (gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods != NULL)
		{
			delete gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods;
			gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods = NULL;
		}
		gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].iniFile = "";
		gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].number = 0;
	}
}

void GoodsMenu::onEvent()
{
	if (scrollbar != NULL && position != scrollbar->position)
	{
		position = scrollbar->position;
		updateGoods();
	}
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		unsigned int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && gm->goodsManager.goodsList[scrollbar->position * scrollbar->lineSize + i].goods != NULL && gm->goodsManager.goodsList[scrollbar->position * scrollbar->lineSize + i].number > 0)
			{
				gm->toolTip->visible = true;
				gm->toolTip->changeParent(this);
				gm->toolTip->setGoods(gm->goodsManager.goodsList[scrollbar->position * scrollbar->lineSize + i].goods);
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
			if (BuySellMenu::getInstance() != NULL)
			{
				if (BuySellMenu::getInstance()->bsKind == bsSell)
				{
					if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dragIndex].number > 0 && gm->gm->goodsManager.goodsList[item[i]->dragIndex].goods->cost > 0)
					{
						if (BuySellMenu::getInstance()->addGoodsItem(gm->goodsManager.goodsList[item[i]->dragIndex].iniFile, 1))
						{
							gm->player.money += gm->goodsManager.goodsList[item[i]->dragIndex].goods->cost;
							updateMoney();
							gm->goodsManager.goodsList[item[i]->dragIndex].number--;
							if (gm->goodsManager.goodsList[item[i]->dragIndex].number <= 0)
							{
								gm->goodsManager.goodsList[item[i]->dragIndex].number = 0;
								gm->goodsManager.goodsList[item[i]->dragIndex].iniFile = "";
								if (gm->goodsManager.goodsList[item[i]->dragIndex].goods != NULL)
								{
									delete gm->goodsManager.goodsList[item[i]->dragIndex].goods;
									gm->goodsManager.goodsList[item[i]->dragIndex].goods = NULL;
								}
							}
							updateGoods(i);
						}
					}
				}
			}
			else
			{
				gm->goodsManager.useItem(item[i]->dragIndex);
			}		
		}
		if (ret & erDropped)
		{
			gm->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dropIndex].goods != NULL && gm->goodsManager.goodsList[item[i]->dropIndex].number > 0)
				{
					if (item[i]->dropIndex < GOODS_COUNT)
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods();
					}
					else if (item[i]->dropIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods(i);
						gm->bottomMenu->updateGoodsItem();
					}
					else
					{
						if (gm->goodsManager.goodsList[item[i]->dragIndex].goods == NULL || gm->goodsManager.goodsList[item[i]->dragIndex].goods->part == gm->goodsManager.goodsList[item[i]->dropIndex].goods->part)
						{
							gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
							updateGoods(i);
							gm->equipMenu->updateGoods();
						}
					}
				}
			}
			else if (item[i]->dropType == dtBuy)
			{
				if (BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile != "")
				{
					if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == "")
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, 1))
						{
							std::string iniName = BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile;
							updateMoney();
							int idx = 0;
							for (size_t j = 0; j < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; j++)
							{
								if (gm->goodsManager.goodsList[j].iniFile == iniName)
								{
									idx = j;
									break;
								}
							}
							gm->goodsManager.exchange(i + scrollbar->position * scrollbar->lineSize, idx);
							updateGoods(i);
							if (idx < GOODS_COUNT)
							{
								updateGoods();
							}
							else if (idx < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
							{
								gm->bottomMenu->updateGoodsItem();
							}
							else
							{
								gm->equipMenu->updateGoods();
							}
						}

					}
					else if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile)
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, 1))
						{
							updateGoods(i);
							updateMoney();
						}
					}
				}			
			}
			else if (item[i]->dropType == dtSell)
			{
				if (BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile != "")
				{
					if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == "")
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number))
						{
							std::string iniName = BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile;
							updateMoney();
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number = 0;
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile = "";
							if (BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods != NULL)
							{
								delete BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods;
								BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods = NULL;
							}
							BuySellMenu::getInstance()->updateGoods();


							int idx = 0;
							for (size_t j = 0; j < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; j++)
							{
								if (gm->goodsManager.goodsList[j].iniFile == iniName)
								{
									idx = j;
									break;
								}
							}
							gm->goodsManager.exchange(i + scrollbar->position * scrollbar->lineSize, idx);
							updateGoods(i);
							if (idx < GOODS_COUNT)
							{
								updateGoods();
							}
							else if (idx < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
							{
								gm->bottomMenu->updateGoodsItem();
							}
							else
							{
								gm->equipMenu->updateGoods();
							}
						}
					}
					else if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile)
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number))
						{
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number = 0;
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile = "";
							if (BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods != NULL)
							{
								delete BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods;
								BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods = NULL;
							}
							BuySellMenu::getInstance()->updateGoods();
							updateGoods(i);
							updateMoney();
						}
					}
				}			
			}						
		}
	}
}

void GoodsMenu::init()
{
	freeResource();
	initFromIni("ini\\ui\\goods\\window.ini");
	title = addImageContainer("ini\\ui\\goods\\Title.ini");
	image = addImageContainer("ini\\ui\\goods\\DragBox.ini");
	gold = addImageContainer("ini\\ui\\goods\\Gold.ini");
	money = addLabel("ini\\ui\\goods\\Money.ini");
	scrollbar = addScrollbar("ini\\ui\\goods\\Scrollbar.ini");

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\goods\\item%d.ini", i + 1);
		item[i] = addItem(itemName);
		item[i]->dragType = dtGoods;
		item[i]->canShowHint = true;
	}

	setChildRect();
}

void GoodsMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(title);
	freeCom(image);
	freeCom(gold);
	freeCom(money);
	freeCom(scrollbar);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		freeCom(item[i]);
	}
}

