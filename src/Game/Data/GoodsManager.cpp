#include "GoodsManager.h"
#include "../GameManager/GameManager.h"


GoodsManager::GoodsManager()
{
}


GoodsManager::~GoodsManager()
{
	freeResource();
}

void GoodsManager::freeResource()
{
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		goodsList[i].iniFile = "";
		goodsList[i].number = 0;
		if (goodsList[i].goods != nullptr)
		{
			delete goodsList[i].goods;
			goodsList[i].goods = nullptr;
		}
	}
}

void GoodsManager::load(const std::string & fileName)
{
	freeResource();
	std::string iniName = SAVE_CURRENT_FOLDER;
	if (fileName.empty())
	{
		iniName += GOODS_INI;
	}
	else
	{
		iniName += fileName;
	}
	INIReader * ini = new INIReader(iniName);

	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		std::string section = convert::formatString("%d", i + 1);
		goodsList[i].iniFile = ini->Get(section, "IniFile", "");
		goodsList[i].number = ini->GetInteger(section, "Number", 0);
		if (goodsList[i].iniFile.empty())
		{
			goodsList[i].number = 0;
		}
		else
		{
			if (goodsList[i].number <= 0)
			{
				goodsList[i].number = 0;
				goodsList[i].iniFile = "";
			}
			else
			{
				goodsList[i].goods = new Goods;
				goodsList[i].goods->initFromIni(goodsList[i].iniFile);
			}
		}
	}

	delete ini;
}

void GoodsManager::save(const std::string & fileName)
{
	INIReader * ini = new INIReader;
	std::string section = "Head";
	ini->SetInteger(section, "Count", 0);
	int count = 0;
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile != "" && goodsList[i].number > 0)
		{
			count++;
			section = convert::formatString("%d", i + 1);
			ini->Set(section, "IniFile", goodsList[i].iniFile);
			ini->SetInteger(section, "Number", goodsList[i].number);
		}
	}
	section = "Head";
	ini->SetInteger(section, "Count", count);
	std::string iniName = SAVE_CURRENT_FOLDER;
	if (fileName.empty())
	{
		iniName += GOODS_INI;
	}
	else
	{
		iniName += fileName;
	}
	ini->saveToFile(iniName);
	delete ini;
}

GoodsInfo * GoodsManager::findGoods(const std::string & itemName)
{
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName)
		{
			return &goodsList[i];
		}
	}
	return nullptr;
}

void GoodsManager::clearItem()
{
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		goodsList[i].iniFile = "";
		goodsList[i].number = 0;
		if (goodsList[i].goods != nullptr)
		{
			delete goodsList[i].goods;
			goodsList[i].goods = nullptr;
		}
	}
	updateMenu();
}

int GoodsManager::getItemNum(const std::string & itemName)
{
	GoodsInfo * g = findGoods(itemName);
	if (g != nullptr)
	{
		return g->number;
	}
	return 0;
}

void GoodsManager::setItemNum(const std::string & itemName, int num)
{
	if (num <= 0)
	{
		deleteItem(itemName);
		updateMenu();
	}
	else
	{
		GoodsInfo * g = findGoods(itemName);
		if (g != nullptr)
		{
			g->number = num;
		}
		else
		{
			addItem(itemName, num);
		}
		updateMenu();
	}
}

void GoodsManager::exchange(int index1, int index2)
{
	if (index1 >= 0 && index1 < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT && index2 >= 0 && index2 < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT)
	{
		GoodsInfo tempInfo = goodsList[index1];
		goodsList[index1] = goodsList[index2];
		goodsList[index2] = tempInfo;
	}
}

void GoodsManager::sellItem(const std::string & itemName)
{
	if (itemName.empty())
	{
		return;
	}
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName && goodsList[i].goods != nullptr)
		{
			goodsList[i].number--;
			gm->player->money += goodsList[i].goods->cost;
			if (goodsList[i].number <= 0)
			{
				goodsList[i].iniFile = "";
				delete goodsList[i].goods;
				goodsList[i].goods = nullptr;
				goodsList[i].number = 0;
			}
			updateMenu(i);
			return;
		}
	}
}

bool GoodsManager::addItem(const std::string & itemName, int num)
{
	if (itemName.empty())
	{
		return false;
	}
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName)
		{
			goodsList[i].number += num;
			updateMenu(i);
			gm->showMessage(convert::formatString(convert::GBKToUTF8_InWinOnly("得到%s!").c_str(), goodsList[i].goods->name.c_str()));
			return true;
		}
	}
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT; i++)
	{
		if (goodsList[i].iniFile.empty())
		{
			goodsList[i].iniFile = itemName;
			goodsList[i].number = num;
			goodsList[i].goods = new Goods;
			goodsList[i].goods->initFromIni(itemName);
			updateMenu(i);
			gm->showMessage(convert::formatString(convert::GBKToUTF8_InWinOnly("得到%s!").c_str(), goodsList[i].goods->name.c_str()));
			return true;
		}
	}
	return false;
}

void GoodsManager::deleteItem(const std::string & itemName)
{
	if (itemName == "")
	{
		return;
	}
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName)
		{
			goodsList[i].number = 0;
			goodsList[i].iniFile = "";
			if (goodsList[i].goods != nullptr)
			{
				delete goodsList[i].goods;
				goodsList[i].goods = nullptr;
			}
			updateMenu(i);
			return;
		}
	}
}

bool GoodsManager::buyItem(const std::string & itemName, int num)
{
	if (num <= 0)
	{
		return false;
	}
	Goods * goods = new Goods;
	goods->initFromIni(itemName);
	int cst = goods->cost * num;
	delete goods;
	if (gm->player->money < cst)
	{
		gm->showMessage(convert::GBKToUTF8_InWinOnly("金钱不足！"));
		return false;
	}
	if (addItem(itemName, num))
	{
		gm->player->money -= cst;
		return true;
	}
	gm->showMessage(convert::GBKToUTF8_InWinOnly("物品栏位置已满！"));
	return false;
}

void GoodsManager::useItem(int itemIndex)
{
	if (!goodsListExists(itemIndex))
	{
		goodsList[itemIndex].iniFile = "";
		goodsList[itemIndex].number = 0;
		if (goodsList[itemIndex].goods != nullptr)
		{
			delete goodsList[itemIndex].goods;
			goodsList[itemIndex].goods = nullptr;
		}
		return;
	}
	std::string goodsScript = goodsList[itemIndex].goods->script;
	if (goodsScript != "")
	{
		gm->runGoodsScript(goodsList[itemIndex].goods);
	}
	else if (goodsList[itemIndex].goods->kind == gkDrug)
	{
		gm->player->life += goodsList[itemIndex].goods->life;
		gm->player->thew += goodsList[itemIndex].goods->thew;
		gm->player->mana += goodsList[itemIndex].goods->mana;
		gm->player->limitAttribute();
		goodsList[itemIndex].number--;
		if (goodsList[itemIndex].number <= 0)
		{
			goodsList[itemIndex].iniFile = "";
			goodsList[itemIndex].number = 0;
			if (goodsList[itemIndex].goods != nullptr)
			{
				delete goodsList[itemIndex].goods;
				goodsList[itemIndex].goods = nullptr;
			}
		}
		if (itemIndex < GOODS_COUNT)
		{
			gm->menu.goodsMenu->updateGoods();
		}
		else if (itemIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
		{
			gm->menu.bottomMenu->updateGoodsItem();
		}
	}
	else if (goodsList[itemIndex].goods->kind == gkEquipment)
	{
		int partIndex = gm->menu.equipMenu->getPartIndex(goodsList[itemIndex].goods->part);
		if (partIndex >= 0)
		{
			exchange(itemIndex, GOODS_COUNT + GOODS_TOOLBAR_COUNT + partIndex);
			gm->menu.equipMenu->updateGoods();
			gm->player->limitAttribute();
			if (itemIndex < GOODS_COUNT)
			{
				gm->menu.goodsMenu->updateGoods();
			}
			else if (itemIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
			{
				gm->menu.bottomMenu->updateGoodsItem();
			}
		}
	}
}

void GoodsManager::updateMenu(int idx)
{
	if (idx < GOODS_COUNT)
	{
		gm->menu.goodsMenu->updateGoods();
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

void GoodsManager::updateMenu()
{
	gm->menu.goodsMenu->updateGoods();
	gm->menu.bottomMenu->updateGoodsItem();
	gm->menu.equipMenu->updateGoods();
}

bool GoodsManager::goodsListExists(int index)
{
	if (index >= 0 && index < goosListLength)
	{
		return !gm->goodsManager.goodsList[index].iniFile.empty() && gm->goodsManager.goodsList[index].goods != nullptr && gm->goodsManager.goodsList[index].number > 0;
	}
	return false;
}
