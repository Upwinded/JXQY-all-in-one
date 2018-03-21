#pragma once
#include "Goods.h"
#include <vector>

struct GoodsInfo
{
	std::string iniFile = "";
	int number = 0;
	Goods * goods = NULL;
};

class GoodsManager
{
public:
	GoodsManager();
	~GoodsManager();

	void freeResource();

	void load(const std::string & fileName = "");
	void save(const std::string & fileName = "");
	GoodsInfo * findGoods(const std::string & itemName);
	void clearItem();
	int getItemNum(const std::string & itemName);
	void setItemNum(const std::string & itemName, int num);
	void exchange(int index1, int index2);
	void sellItem(const std::string & itemName);
	bool addItem(const std::string & itemName, int num);
	void deleteItem(const std::string & itemName);
	bool buyItem(const std::string & itemName, int num);
	void useItem(int itemIndex);

	void updateMenu(int idx);
	void updateMenu();

	GoodsInfo goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT];
};

