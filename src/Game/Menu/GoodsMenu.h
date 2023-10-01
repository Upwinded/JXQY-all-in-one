#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class GoodsMenu :
	public Panel
{
public:
	GoodsMenu();
	virtual ~GoodsMenu();

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<ImageContainer> gold = nullptr;

	std::shared_ptr<Scrollbar> scrollbar = nullptr;

	std::shared_ptr<Label> money = nullptr;

	std::shared_ptr<Item> item[MENU_ITEM_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

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

