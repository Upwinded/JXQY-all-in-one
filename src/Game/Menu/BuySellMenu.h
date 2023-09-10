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
	virtual ~BuySellMenu();
	int bsKind = bsBuy;
	static BuySellMenu * getInstance();

	ImageContainer * title = nullptr;
	ImageContainer * image = nullptr;
	Button * closeBtn = nullptr;

	Scrollbar * scrollbar = nullptr;

	Item * item[MENU_ITEM_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

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
	void freeResource();
};

