#include "BuySellMenu.h"
#include "../GameManager/GameManager.h"

BuySellMenu * BuySellMenu::this_ = nullptr;

BuySellMenu::BuySellMenu()
{
	name = "BuySellMenu";
	init();
	priority = epMax;
	this_ = this;
}

BuySellMenu::~BuySellMenu()
{
	freeResource();
	this_ = nullptr;
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
		if (goodsList[i].goods != nullptr)
		{
			goodsList[i].goods = nullptr;
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
	addChild(gm->menu->goodsMenu);
	clearGoodsList();
	std::string listName = BUYSELL_FOLDER + list;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(listName, s);
	if (len > 0 && s != nullptr)
	{
		INIReader ini(s);
		int count = ini.GetInteger("Header", "Count", 0);
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
			goodsList[i].iniFile = ini.Get(section, "IniFile", "");
			goodsList[i].number = ini.GetInteger(section, "Number", 1);
			if (goodsList[i].iniFile != "")
			{
				goodsList[i].goods = std::make_shared<Goods>();
				goodsList[i].goods->initFromIni(goodsList[i].iniFile);
			}
			else
			{
				goodsList[i].number = 0;
			}

		}
	}
	clearButtonChecked();
	setGoodsButtonChecked();
	run();
	clearButtonChecked();
	gm->menu->addChild(gm->menu->goodsMenu);
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
	addChild(gm->menu->goodsMenu);
	clearGoodsList();
	if (list != "")
	{
		std::string listName = BUYSELL_FOLDER + list;
		std::unique_ptr<char[]> s;
		int len = PakFile::readFile(listName, s);
		if (len > 0 && s != nullptr)
		{
			INIReader ini(s);
			int count = ini.GetInteger("Header", "Count", 0);
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
				goodsList[i].iniFile = ini.Get(section, "IniFile", "");
				goodsList[i].number = ini.GetInteger(section, "Number", 1);
				if (goodsList[i].iniFile != "")
				{
					goodsList[i].goods = std::make_shared<Goods>();
					goodsList[i].goods->initFromIni(goodsList[i].iniFile);
				}
				else
				{
					goodsList[i].number = 0;
				}

			}
		}
	}
	clearButtonChecked();
	setGoodsButtonChecked();
	run();
	clearButtonChecked();
	gm->menu->addChild(gm->menu->goodsMenu);
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
				if (goodsList[i].goods != nullptr)
				{

					goodsList[i].goods = nullptr;
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
			goodsList[i].goods = std::make_shared<Goods>();
			goodsList[i].goods->initFromIni(itemName);
			updateGoods();
			return true;
		}
	}
	return false;
}

void BuySellMenu::setGoodsButtonChecked()
{
	gm->menu->goodsMenu->visible = true;
	gm->menu->bottomMenu->goodsBtn->checked = true;
}

void BuySellMenu::clearButtonChecked()
{
	gm->menu->practiceMenu->visible = false;
	gm->menu->equipMenu->visible = false;
	gm->menu->stateMenu->visible = false;
	gm->menu->magicMenu->visible = false;
	gm->menu->memoMenu->visible = false;
	gm->menu->goodsMenu->visible = false;
	gm->menu->bottomMenu->magicBtn->checked = false;
	gm->menu->bottomMenu->goodsBtn->checked = false;
	gm->menu->bottomMenu->notesBtn->checked = false;
	gm->menu->bottomMenu->stateBtn->checked = false;
	gm->menu->bottomMenu->xiulianBtn->checked = false;
	gm->menu->bottomMenu->equipBtn->checked = false;
}

void BuySellMenu::updateGoods()
{
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		item[i]->dragIndex = i + scrollbar->position * scrollbar->lineSize;

		item[i]->impImage = nullptr;

		item[i]->setStr("");
		if (goodsList[i + scrollbar->position * scrollbar->lineSize].iniFile != "" && goodsList[i + scrollbar->position * scrollbar->lineSize].goods != nullptr)
		{
			item[i]->impImage = goodsList[i + scrollbar->position * scrollbar->lineSize].goods->createGoodsIcon();
			if (bsKind == bsSell)
			{
				item[i]->setStr(convert::formatString("%d", goodsList[i + scrollbar->position * scrollbar->lineSize].number));
			}
		}
	}
}

void BuySellMenu::onEvent()
{
	if (scrollbar != nullptr && position != scrollbar->position)
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
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && goodsList[scrollbar->position * scrollbar->lineSize + i].goods != nullptr)
				{
					gm->menu->toolTip->visible = true;
					addChild(gm->menu->toolTip);
					gm->menu->toolTip->setGoods(goodsList[scrollbar->position * scrollbar->lineSize + i].goods);
				}
				else
				{
					gm->menu->toolTip->visible = false;
				}
			}
			else
			{
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "" && goodsList[scrollbar->position * scrollbar->lineSize + i].goods != nullptr && goodsList[scrollbar->position * scrollbar->lineSize + i].number > 0)
				{
					gm->menu->toolTip->visible = true;
					addChild(gm->menu->toolTip);
					gm->menu->toolTip->setGoods(goodsList[scrollbar->position * scrollbar->lineSize + i].goods);
				}
				else
				{
					gm->menu->toolTip->visible = false;
				}
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
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (bsKind == bsBuy)
			{
				if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile != "")
				{
					if (gm->goodsManager.buyItem(goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile, 1))
					{
						gm->menu->goodsMenu->updateGoods();
						gm->menu->goodsMenu->updateMoney();
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
							if (goodsList[scrollbar->position * scrollbar->lineSize + i].goods != nullptr)
							{
								goodsList[scrollbar->position * scrollbar->lineSize + i].goods = nullptr;
							}
							
						}				
						updateGoods();
						gm->menu->goodsMenu->updateGoods();
						gm->menu->goodsMenu->updateMoney();
					}
				}
			}
		}
		if (ret & erDropped)
		{
			if (bsKind == bsSell)
			{
				if (item[i]->dropType == dtGoods && gm->goodsManager.goodsList[item[i]->dropIndex].number > 0 && gm->goodsManager.goodsList[item[i]->dropIndex].iniFile != "" && gm->goodsManager.goodsList[item[i]->dropIndex].goods != nullptr && gm->goodsManager.goodsList[item[i]->dropIndex].goods->cost > 0)
				{
					if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile == goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile)
					{
						goodsList[scrollbar->position * scrollbar->lineSize + i].number += gm->goodsManager.goodsList[item[i]->dropIndex].number;
						gm->player->money += gm->goodsManager.goodsList[item[i]->dropIndex].number * gm->goodsManager.goodsList[item[i]->dropIndex].goods->cost;
						gm->goodsManager.goodsList[item[i]->dropIndex].number = 0;
						gm->goodsManager.goodsList[item[i]->dropIndex].iniFile = "";
						gm->goodsManager.goodsList[item[i]->dropIndex].goods = nullptr;
						gm->menu->goodsMenu->updateGoods();
						gm->menu->goodsMenu->updateMoney();
						updateGoods();
						
					}
					else if (goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile == "")
					{
						goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile = gm->goodsManager.goodsList[item[i]->dropIndex].iniFile;
						goodsList[scrollbar->position * scrollbar->lineSize + i].goods = std::make_shared<Goods>();
						goodsList[scrollbar->position * scrollbar->lineSize + i].goods->initFromIni(goodsList[scrollbar->position * scrollbar->lineSize + i].iniFile);
						goodsList[scrollbar->position * scrollbar->lineSize + i].number = gm->goodsManager.goodsList[item[i]->dropIndex].number;
						gm->player->money += gm->goodsManager.goodsList[item[i]->dropIndex].number * gm->goodsManager.goodsList[item[i]->dropIndex].goods->cost;
						gm->goodsManager.goodsList[item[i]->dropIndex].number = 0;
						gm->goodsManager.goodsList[item[i]->dropIndex].iniFile = "";
						gm->goodsManager.goodsList[item[i]->dropIndex].goods = nullptr;
						gm->menu->goodsMenu->updateGoods();
						gm->menu->goodsMenu->updateMoney();
						updateGoods();
					}
				}
			}
		}
	}

}

bool BuySellMenu::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_ESCAPE)
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
	initFromIniFileName("ini\\ui\\buysell\\window.ini");
	title = addComponent<ImageContainer>("ini\\ui\\buysell\\title.ini");
	image = addComponent<ImageContainer>("ini\\ui\\buysell\\dragbox.ini");
	closeBtn = addComponent<Button>("ini\\ui\\buysell\\closebtn.ini");

	scrollbar = addComponent<Scrollbar>("ini\\ui\\buysell\\scrollbar.ini");

	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		std::string itemName = convert::formatString("ini\\ui\\buysell\\item%d.ini", i + 1);
		item[i] = addComponent<Item>(itemName);
		item[i]->dragType = dtSell;
		item[i]->canShowHint = true;
	}

	setChildRectReferToParent();
}

void BuySellMenu::freeResource()
{

	impImage = nullptr;

	freeCom(title);
	freeCom(image);
	freeCom(closeBtn);
	freeCom(scrollbar);
	for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
	{
		freeCom(item[i]);
	}
	clearGoodsList();
	removeAllChild();
}
