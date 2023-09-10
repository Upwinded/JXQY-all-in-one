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
	money->setStr(convert::formatString("%d", gm->player->money));
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
	if (item[index]->impImage != nullptr)
	{
		IMP::clearIMPImage(item[index]->impImage);
		//delete item[index]->impImage;
		item[index]->impImage = nullptr;
	}
	updateGoodsNumber(index);
	if (gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods != nullptr)
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
	if (gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods != nullptr && gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].number > 0 && gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].iniFile != "")
	{
		item[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].number));
	}
	else
	{
		item[index]->setStr("");
		if (gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods != nullptr)
		{
			delete gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods;
			gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].goods = nullptr;
		}
		gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].iniFile = "";
		gm->goodsManager.goodsList[index + scrollbar->position * scrollbar->lineSize].number = 0;
	}
}

void GoodsMenu::onEvent()
{
	if (scrollbar != nullptr && position != scrollbar->position)
	{
		position = scrollbar->position;
		updateGoods();
	}
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		unsigned int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsListExists(scrollbar->position * scrollbar->lineSize + i))
			{
				gm->menu.toolTip->visible = true;
				gm->menu.toolTip->changeParent(this);
				gm->menu.toolTip->setGoods(gm->goodsManager.goodsList[scrollbar->position * scrollbar->lineSize + i].goods);
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
		if ((ret & erMouseRDown) || (ret & erClick))
#else
		if (ret & erMouseRDown)
#endif
		{
			gm->menu.toolTip->visible = false;
			item[i]->resetHint();
			if (BuySellMenu::getInstance() != nullptr)
			{
				if (BuySellMenu::getInstance()->bsKind == bsSell)
				{
					if (gm->goodsManager.goodsListExists(item[i]->dragIndex) && gm->gm->goodsManager.goodsList[item[i]->dragIndex].goods->cost > 0)
					{
						if (BuySellMenu::getInstance()->addGoodsItem(gm->goodsManager.goodsList[item[i]->dragIndex].iniFile, 1))
						{
							gm->player->money += gm->goodsManager.goodsList[item[i]->dragIndex].goods->cost;
							updateMoney();
							gm->goodsManager.goodsList[item[i]->dragIndex].number--;
							if (gm->goodsManager.goodsList[item[i]->dragIndex].number <= 0)
							{
								gm->goodsManager.goodsList[item[i]->dragIndex].number = 0;
								gm->goodsManager.goodsList[item[i]->dragIndex].iniFile = "";
								if (gm->goodsManager.goodsList[item[i]->dragIndex].goods != nullptr)
								{
									delete gm->goodsManager.goodsList[item[i]->dragIndex].goods;
									gm->goodsManager.goodsList[item[i]->dragIndex].goods = nullptr;
								}
							}
							updateGoods(i);
						}
					}
				}
			}
			else
			{
#ifdef _MOBILE
				if (gm->goodsManager.goodsListExists(item[i]->dragIndex))
				{
					if (gm->goodsManager.goodsList[item[i]->dragIndex].goods->kind == gkDrug)
					{
						for (size_t j = GOODS_COUNT; j < GOODS_COUNT + GOODS_TOOLBAR_COUNT; ++j)
						{
							if (!gm->goodsManager.goodsListExists(j))
							{
								gm->goodsManager.exchange(j, item[i]->dragIndex);
								updateGoods(i);
								gm->menu.bottomMenu->updateGoodsItem(j - GOODS_COUNT);
								break;
							}
						}
					}
					else
					{
#endif
						gm->goodsManager.useItem(item[i]->dragIndex);
#ifdef _MOBILE
					}
				}
#endif
			}
		}
		if (ret & erDropped)
		{
			gm->menu.toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dropIndex].goods != nullptr && gm->goodsManager.goodsList[item[i]->dropIndex].number > 0)
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
						gm->menu.bottomMenu->updateGoodsItem();
					}
					else
					{
						if (gm->goodsManager.goodsList[item[i]->dragIndex].goods == nullptr || gm->goodsManager.goodsList[item[i]->dragIndex].goods->part == gm->goodsManager.goodsList[item[i]->dropIndex].goods->part)
						{
							gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
							updateGoods(i);
							gm->menu.equipMenu->updateGoods();
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
								gm->menu.bottomMenu->updateGoodsItem();
							}
							else
							{
								gm->menu.equipMenu->updateGoods();
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
							if (BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods != nullptr)
							{
								delete BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods;
								BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods = nullptr;
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
								gm->menu.bottomMenu->updateGoodsItem();
							}
							else
							{
								gm->menu.equipMenu->updateGoods();
							}
						}
					}
					else if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile)
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number))
						{
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number = 0;
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile = "";
							if (BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods != nullptr)
							{
								delete BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods;
								BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].goods = nullptr;
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
	title = addImageContainer("ini\\ui\\goods\\title.ini");
	image = addImageContainer("ini\\ui\\goods\\dragbox.ini");
	gold = addImageContainer("ini\\ui\\goods\\gold.ini");
	money = addLabel("ini\\ui\\goods\\money.ini");
	scrollbar = addScrollbar("ini\\ui\\goods\\scrollbar.ini");

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\goods\\item%d.ini", i + 1);
		item[i] = addItem(itemName);
		item[i]->dragType = dtGoods;
		item[i]->canShowHint = true;
	}

	setChildRectReferToParent();
}

void GoodsMenu::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
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

