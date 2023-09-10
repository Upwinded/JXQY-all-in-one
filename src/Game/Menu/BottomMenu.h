#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "ColumnMenu.h"

class BottomMenu :
	public Panel
{
public:
	BottomMenu();
	virtual ~BottomMenu();

	void updateGoodsItem();
	void updateGoodsItem(int index);
	void updateGoodsNumber();
	void updateGoodsNumber(int index);

	void updateMagicItem();
	void updateMagicItem(int index);

	ColumnMenu columnMenu;

	CheckBox * equipBtn = nullptr;
	CheckBox * goodsBtn = nullptr;
	CheckBox * magicBtn = nullptr;
	CheckBox * notesBtn = nullptr;
	CheckBox * optionBtn = nullptr;
	CheckBox * stateBtn = nullptr;
	CheckBox * xiulianBtn = nullptr;

	Item * goodsItem[GOODS_TOOLBAR_COUNT] = { nullptr, nullptr, nullptr };
	Item * magicItem[MAGIC_TOOLBAR_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr };

private:
	virtual void init();

	virtual void onEvent();
	void freeResource();

};

