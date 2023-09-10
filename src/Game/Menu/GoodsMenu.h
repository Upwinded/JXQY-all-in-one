#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class GoodsMenu :
	public Panel
{
public:
	GoodsMenu();
	virtual ~GoodsMenu();

	ImageContainer * title = nullptr;
	ImageContainer * image = nullptr;
	ImageContainer * gold = nullptr;

	Scrollbar * scrollbar = nullptr;

	Label * money = nullptr;

	Item * item[MENU_ITEM_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	void updateMoney();
	void updateGoods();
	void updateGoods(int index);
	void updateGoodsNumber(int index);

private:
	int position = -1;

	virtual void onEvent();

	virtual void init();
	void freeResource();
	


};

