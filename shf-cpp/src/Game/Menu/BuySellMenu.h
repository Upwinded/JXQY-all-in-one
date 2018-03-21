#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "../Data/GoodsManager.h"

enum BuySellType
{
	bsBuy = 1,
	bsSell = 2,
};

class BuySellMenu :
	public Panel
{
public:
	BuySellMenu();
	~BuySellMenu();
	int bsKind = bsBuy;
	static BuySellMenu * getInstance();

	ImageContainer * title = NULL;
	ImageContainer * image = NULL;
	Button * closeBtn = NULL;

	Scrollbar * scrollbar = NULL;

	Item * item[MENU_ITEM_COUNT] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

	GoodsInfo goodsList[BUYSELL_GOODS_COUNT];

	void clearGoodsList();
	void buy(const std::string & list);
	void sell(const std::string & list = "");
	bool addGoodsItem(const std::string & itemName, int num);
	void setGoodsButtonChecked();
	void clearButtonChecked();

	void updateGoods();
private:
	static BuySellMenu * this_;

	int position = -1;
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);
	virtual void init();
	virtual void freeResource();
};

