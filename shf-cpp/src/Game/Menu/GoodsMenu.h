#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class GoodsMenu :
	public Panel
{
public:
	GoodsMenu();
	~GoodsMenu();

	ImageContainer * title = NULL;
	ImageContainer * image = NULL;
	ImageContainer * gold = NULL;

	Scrollbar * scrollbar = NULL;

	Label * money = NULL;

	Item * item[MENU_ITEM_COUNT] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

	void updateMoney();
	void updateGoods();
	void updateGoods(int index);
	void updateGoodsNumber(int index);

private:
	int position = -1;

	virtual void onEvent();

	virtual void init();
	virtual void freeResource();
	


};

