#include "BuySellMenu.h"
#include "../GameManager/GameManager.h"

BuySellMenu * BuySellMenu::this_ = NULL;

BuySellMenu::BuySellMenu()
{
	init();
	priority = epMax;
	this_ = this;
}

BuySellMenu::~BuySellMenu()
{
	freeResource();
	this_ = NULL;
}

BuySellMenu * BuySellMenu::getInstance()
{
	return this_;
}

void BuySellMenu::clearGoodsList()
{
	for (size_t i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		goodsList[i].iniFile = "";
		goodsList[i].number = 0;
		if (goodsList[i].goods != NULL)
		{
			delete goodsList[i].goods;
			goodsList[i].goods = NULL;
		}
	}
}

void BuySellMenu::buy(const std::string & list)
{
	bsKind = bsBuy;
	scrollbar->setPosition(scrollbar->min);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		item[i]->dragType = dtBuy;
		item[i]->dragIndex = i;
	}
	gm->goodsMenu->changeParent(this);
	clearGoodsList();
	std::string listName = BUYSELL_FOLDER + list;
	char * s = NULL;
	int len = PakFile::readFile(listName, &s);
	if (len > 0 && s != NULL)
	{
		INIReader * ini = new INIReader(s);
		delete[] s;
		int count = ini->GetInteger("Header", "Count", 0);
		if (count < 0)
		{
			count = 0;
		}
		if (count > BUYSELL_GOODS_COUNT)
		{
			count = BUYSELL_GOODS_COUNT;
		}
		for (int i = 0; i < count; i++)
		{
			std::string section = convert::formatString("%d", i + 1);
			goodsList[i].iniFile = ini->Get(section, "IniFile", "");
			goodsList[i].number = ini->GetInteger(section, "Number", 1);
			if (goodsList[i].iniFile != "")
			{
				goodsList[i].goods = new Goods;
				goodsList[i].goods->initFromIni(goodsList[i].iniFile);
			}
			else
			{
				goodsList[i].number = 0;
			}

		}
		delete ini;
	}
	clearButtonChecked();
	setGoodsButtonChecked();
	run();
	clearButtonChecked();
	gm->goodsMenu->changeParent(&gm->menu);
}

void BuySellMenu::sell(const std::string & list)
{
	bsKind = bsSell;
	scrollbar->setPosition(scrollbar->min);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		item[i]->dragType = dtSell;
		item[i]->dragIndex = i;
	}
	gm->goodsMenu->changeParent(this);
	clearGoodsList();
	if (list != "")
	{
		std::string listName = BUYSELL_FOLDER + list;
		char * s = NULL;
		int len = PakFile::readFile(listName, &s);
		if (len > 0 && s != NULL)
		{
			INIReader * ini = new INIReader(s);
			delete[] s;
			int count = ini->GetInteger("Header", "Count", 0);
			if (count < 0)
			{
				count = 0;
			}
			if (count > BUYSELL_GOODS_COUNT)
			{
				count = BUYSELL_GOODS_COUNT;
			}
			for (int i = 0; i < count; i++)
			{
				std::string section = convert::formatString("%d", i + 1);
				goodsList[i].iniFile = ini->Get(section, "IniFile", "");
				goodsList[i].number = ini->GetInteger(section, "Number", 1);
				if (goodsList[i].iniFile != "")
				{
					goodsList[i].goods = new Goods;
					goodsList[i].goods->initFromIni(goodsList[i].iniFile);
				}
				else
				{
					goodsList[i].number = 0;
				}

			}
			delete ini;
		}
	}
	clearButtonChecked();
	setGoodsButtonChecked();
	run();
	clearButtonChecked();
	gm->goodsMenu->changeParent(&gm->menu);
}

bool BuySellMenu::addGoodsItem(const std::string & itemName, int num)
{
	for (size_t i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName)
		{
			goodsList[i].number += num;
			if (goodsList[i].number <= 0)
			{
				goodsList[i].number = 0;
				goodsList[i].iniFile = "";
				if (goodsList[i].goods != NULL)
				{
					delete goodsList[i].goods;
					goodsList[i].goods = NULL;
				}
			}
			updateGoods();
			return true;
		}
	}
	if (num <= 0)
	{
		return false;
	}
	for (size_t i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		if (goodsList[i].iniFile == "")
		{
			goodsList[i].number = num;
			goodsList[i].iniFile = itemName;
			goodsList[i].goods = new Goods;
			goodsList[i].goods->initFromIni(itemName);
			updateGoods();
			return true;
		}
	}
	return false;
}

void BuySellMenu::setGoodsButtonChecked()
{
	gm->goodsMenu->visible = true;
	gm->bottomMenu->goodsBtn->checked = true;
}

void BuySellMenu::clearButtonChecked()
{
	gm->practiceMenu->visible = false;
	gm->equipMenu->visible = false;
	gm->stateMenu->visible = false;
	gm->magicMenu->visible = false;
	gm->memoMenu->visible = false;
	gm->goodsMenu->visible = false;
	gm->bottomMenu->magicBtn->checked = false;
	gm->bottomMenu->goodsBtn->checked = false;
	gm->bottomMenu->notesBtn->checked = false;
	gm->bottomMenu->stateBtn->checked = false;
	gm->bottomMenu->xiulianBtn->checked = false;
	gm->bottomMenu->equipBtn->checked = false;
}

void BuySellMenu::updateGoods()
{
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		item[i]->dragIndex = i + scrollbar->position * scrollbar->lineSize;
		if (item[i]->impImage != NULL)
		{
			IMP::clearIMPImage(item[i]->impImage);
			delete item[i]->impImage;
			item[i]->impImage = NULL;
		}
		item[i]->setStr(L"");
		if (goodsList[i + scrollbar->position * scrollbar->lineSize].iniFile != "" && goodsList[i + scrollbar->position * scrollbar->lineSize].goods != NULL)
		{
			item[i]->impImage = goodsList[i + scrollbar->position * scrollbar->lineSize].goods->createGoodsIcon();
			if (bsKind == bsSell)
			{
				item[i]->setStr(convert::GBKToUnicode(convert::formatString("%d", goodsList[i + scrollbar->position * scrollbar->lineSize].number)));
			}
		}
	}
}

void BuySellMenu::onEvent()
{
	if (scrollbar != NULL && position != scrollbar->position)
	{
		position = scrollbar->position;
		updateGoods();
	}
	if (closeBtn->getResult(erClick))
	{
		running = false;
	}
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		unsigned int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (bsKind == bsBuy)
			{
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && goodsList[scrollbar->position * scrollbar->lineSize + i].goods != NULL)
				{
					gm->toolTip->visible = true;
					gm->toolTip->changeParent(this);
					gm->toolTip->setGoods(goodsList[scrollbar->position * scrollbar->lineSize + i].goods);
				}
				else
				{
					gm->toolTip->visible = false;
				}
			}
			else
			{
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && goodsList[scrollbar->position * scrollbar->lineSize + i].goods != NULL && goodsList[scrollbar->position * scrollbar->lineSize + i].number > 0)
				{
					gm->toolTip->visible = true;
					gm->toolTip->changeParent(this);
					gm->toolTip->setGoods(goodsList[scrollbar->position * scrollbar->lineSize + i].goods);
				}
				else
				{
					gm->toolTip->visible = false;
				}
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
			if (bsKind == bsBuy)
			{
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "")
				{
					if (gm->goodsManager.buyItem(goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile, 1))
					{
						gm->goodsMenu->updateGoods();
						gm->goodsMenu->updateMoney();
					}
				}		
			}
			else
			{
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && goodsList[scrollbar->position * scrollbar->lineSize + i].number > 0)
				{
					if (gm->goodsManager.buyItem(goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile, 1))
					{
						goodsList[scrollbar->position * scrollbar->lineSize + i].number--;
						if (goodsList[scrollbar->position * scrollbar->lineSize + i].number <= 0)
						{
							goodsList[scrollbar->position * scrollbar->lineSize + i].number = 0;
							goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile = "";
							if (goodsList[scrollbar->position * scrollbar->lineSize + i].goods != NULL)
							{
								delete goodsList[scrollbar->position * scrollbar->lineSize + i].goods;
								goodsList[scrollbar->position * scrollbar->lineSize + i].goods = NULL;
							}
							
						}				
						updateGoods();
						gm->goodsMenu->updateGoods();
						gm->goodsMenu->updateMoney();
					}
				}
			}
		}
		if (ret & erDropped)
		{
			if (bsKind == bsSell)
			{
				if (item[i]->dropType == dtGoods && gm->goodsManager.goodsList[item[i]->dropIndex].number > 0 && gm->goodsManager.goodsList[item[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dropIndex].goods != NULL && gm->goodsManager.goodsList[item[i]->dropIndex].goods->cost > 0)
				{
					if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile == goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile)
					{
						goodsList[scrollbar->position * scrollbar->lineSize + i].number += gm->goodsManager.goodsList[item[i]->dropIndex].number;
						gm->player.money += gm->goodsManager.goodsList[item[i]->dropIndex].number * gm->goodsManager.goodsList[item[i]->dropIndex].goods->cost;
						gm->goodsManager.goodsList[item[i]->dropIndex].number = 0;
						gm->goodsManager.goodsList[item[i]->dropIndex].iniFile = "";
						delete gm->goodsManager.goodsList[item[i]->dropIndex].goods;
						gm->goodsManager.goodsList[item[i]->dropIndex].goods = NULL;
						gm->goodsMenu->updateGoods();
						gm->goodsMenu->updateMoney();
						updateGoods();
						
					}
					else if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile == "")
					{
						goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile = gm->goodsManager.goodsList[item[i]->dropIndex].iniFile;
						goodsList[scrollbar->position * scrollbar->lineSize + i].goods = new Goods;
						goodsList[scrollbar->position * scrollbar->lineSize + i].goods->initFromIni(goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile);
						goodsList[scrollbar->position * scrollbar->lineSize + i].number = gm->goodsManager.goodsList[item[i]->dropIndex].number;
						gm->player.money += gm->goodsManager.goodsList[item[i]->dropIndex].number * gm->goodsManager.goodsList[item[i]->dropIndex].goods->cost;
						gm->goodsManager.goodsList[item[i]->dropIndex].number = 0;
						gm->goodsManager.goodsList[item[i]->dropIndex].iniFile = "";
						delete gm->goodsManager.goodsList[item[i]->dropIndex].goods;
						gm->goodsManager.goodsList[item[i]->dropIndex].goods = NULL;
						gm->goodsMenu->updateGoods();
						gm->goodsMenu->updateMoney();
						updateGoods();
					}
				}
			}
		}
	}

}

bool BuySellMenu::onHandleEvent(AEvent * e)
{
	if (e->eventType == KEYDOWN)
	{
		if (e->eventData == KEY_ESCAPE)
		{
			running = false;
			return true;
		}
	}
	return false;
}

void BuySellMenu::init()
{
	freeResource();
	initFromIni("ini\\ui\\BuySell\\window.ini");
	title = addImageContainer("ini\\ui\\BuySell\\Title.ini");
	image = addImageContainer("ini\\ui\\BuySell\\DragBox.ini");
	closeBtn = addButton("ini\\ui\\BuySell\\CloseBtn.ini");

	scrollbar = addScrollbar("ini\\ui\\BuySell\\Scrollbar.ini");

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\BuySell\\item%d.ini", i + 1);
		item[i] = addItem(itemName);
		item[i]->dragType = dtSell;
		item[i]->canShowHint = true;
	}

	setChildRect();
}

void BuySellMenu::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(title);
	freeCom(image);
	freeCom(closeBtn);
	freeCom(scrollbar);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		freeCom(item[i]);
	}
	clearGoodsList();
}
