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
		goodsList[i].iniFile = u8"";
		goodsList[i].number = 0;
		if (goodsList[i].goods != nullptr)
		{
			goodsList[i].goods = nullptr;
		}
	}
}

void GoodsManager::load(int index)
{
	freeResource();
	std::string fName = SAVE_CURRENT_FOLDER;
    fName += GOODS_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += GOODS_INI_EXT;
	INIReader ini(fName);

	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		std::string section = convert::formatString("%d", i + 1);
		goodsList[i].iniFile = ini.Get(section, u8"IniFile", u8"");
		goodsList[i].number = ini.GetInteger(section, u8"Number", 0);
		if (goodsList[i].iniFile.empty())
		{
			goodsList[i].number = 0;
		}
		else
		{
			if (goodsList[i].number <= 0)
			{
				goodsList[i].number = 0;
				goodsList[i].iniFile = u8"";
			}
			else
			{
				goodsList[i].goods = std::make_shared<Goods>();
				goodsList[i].goods->initFromIni(goodsList[i].iniFile);
			}
		}
	}
}

void GoodsManager::save(int index)
{
	INIReader ini;
	std::string section = u8"Head";
	ini.SetInteger(section, u8"Count", 0);
	int count = 0;
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile != u8"" && goodsList[i].number > 0)
		{
			count++;
			section = convert::formatString("%d", i + 1);
			ini.Set(section, u8"IniFile", goodsList[i].iniFile);
			ini.SetInteger(section, u8"Number", goodsList[i].number);
		}
	}
	section = u8"Head";
	ini.SetInteger(section, u8"Count", count);
    std::string fName = GOODS_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += GOODS_INI_EXT;
	ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
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
		goodsList[i].iniFile = u8"";
		goodsList[i].number = 0;
		if (goodsList[i].goods != nullptr)
		{
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
				goodsList[i].iniFile = u8"";
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
			gm->showMessage(convert::formatString(u8"得到%s!", goodsList[i].goods->name.c_str()));
			return true;
		}
	}
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT; i++)
	{
		if (goodsList[i].iniFile.empty())
		{
			goodsList[i].iniFile = itemName;
			goodsList[i].number = num;
			goodsList[i].goods = std::make_shared<Goods>();
			goodsList[i].goods->initFromIni(itemName);
			updateMenu(i);
			gm->showMessage(convert::formatString(u8"得到%s!", goodsList[i].goods->name.c_str()));
			return true;
		}
	}
	return false;
}

void GoodsManager::deleteItem(const std::string & itemName)
{
	if (itemName == u8"")
	{
		return;
	}
	for (size_t i = 0; i < GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName)
		{
			goodsList[i].number = 0;
			goodsList[i].iniFile = u8"";
			if (goodsList[i].goods != nullptr)
			{
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
	Goods goods;
	goods.initFromIni(itemName);
	int cst = goods.cost * num;
	if (gm->player->money < cst)
	{
		gm->showMessage(u8"金钱不足！");
		return false;
	}
	if (addItem(itemName, num))
	{
		gm->player->money -= cst;
		return true;
	}
	gm->showMessage(u8"物品栏位置已满！");
	return false;
}

void GoodsManager::useItem(int itemIndex)
{
	if (!goodsListExists(itemIndex))
	{
		goodsList[itemIndex].iniFile = u8"";
		goodsList[itemIndex].number = 0;
		if (goodsList[itemIndex].goods != nullptr)
		{
			goodsList[itemIndex].goods = nullptr;
		}
		return;
	}
	std::string goodsScript = goodsList[itemIndex].goods->script;
	if (goodsScript != u8"")
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
			goodsList[itemIndex].iniFile = u8"";
			goodsList[itemIndex].number = 0;
			if (goodsList[itemIndex].goods != nullptr)
			{
				goodsList[itemIndex].goods = nullptr;
			}
		}
		if (itemIndex < GOODS_COUNT)
		{
			gm->menu->goodsMenu->updateGoods();
		}
		else if (itemIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
		{
			gm->menu->bottomMenu->updateGoodsItem();
		}
	}
	else if (goodsList[itemIndex].goods->kind == gkEquipment)
	{
		int partIndex = gm->menu->equipMenu->getPartIndex(goodsList[itemIndex].goods->part);
		if (partIndex >= 0)
		{
			exchange(itemIndex, GOODS_COUNT + GOODS_TOOLBAR_COUNT + partIndex);
			gm->menu->equipMenu->updateGoods();
			gm->player->limitAttribute();
			if (itemIndex < GOODS_COUNT)
			{
				gm->menu->goodsMenu->updateGoods();
			}
			else if (itemIndex < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
			{
				gm->menu->bottomMenu->updateGoodsItem();
			}
		}
	}
}

void GoodsManager::updateMenu(int idx)
{
	if (idx < GOODS_COUNT)
	{
		gm->menu->goodsMenu->updateGoods();
	}
	else if (idx < GOODS_COUNT + GOODS_TOOLBAR_COUNT)
	{
		gm->menu->bottomMenu->updateGoodsItem();
	}
	else
	{
		gm->menu->equipMenu->updateGoods();
	}
}

void GoodsManager::updateMenu()
{
	gm->menu->goodsMenu->updateGoods();
	gm->menu->bottomMenu->updateGoodsItem();
	gm->menu->equipMenu->updateGoods();
}

bool GoodsManager::goodsListExists(int index)
{
	if (index >= 0 && index < goosListLength)
	{
		return !gm->goodsManager.goodsList[index].iniFile.empty() && gm->goodsManager.goodsList[index].goods != nullptr && gm->goodsManager.goodsList[index].number > 0;
	}
	return false;
}
