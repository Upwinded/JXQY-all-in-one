#pragma once
#include "Goods.h"
#include <vector>

struct GoodsInfo
{
	std::string iniFile = u8"";
	int number = 0;
	std::shared_ptr<Goods> goods = nullptr;
};

class GoodsManager
{
public:
	GoodsManager();
	virtual ~GoodsManager();

	void freeResource();

	void load(int index);
	void save(int index);
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
	const int goosListLength = GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT;
	GoodsInfo goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT];
	bool goodsListExists(int index);
};

