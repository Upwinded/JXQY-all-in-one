#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class BottomMenu :
	public Panel
{
public:
	BottomMenu();
	~BottomMenu();

	void updateGoodsItem();
	void updateGoodsItem(int index);
	void updateGoodsNumber();
	void updateGoodsNumber(int index);

	void updateMagicItem();
	void updateMagicItem(int index);

	CheckBox * equipBtn = NULL;
	CheckBox * goodsBtn = NULL;
	CheckBox * magicBtn = NULL;
	CheckBox * notesBtn = NULL;
	CheckBox * optionBtn = NULL;
	CheckBox * stateBtn = NULL;
	CheckBox * xiulianBtn = NULL;

	Item * goodsItem[GOODS_TOOLBAR_COUNT] = { NULL, NULL, NULL };
	Item * magicItem[MAGIC_TOOLBAR_COUNT] = { NULL, NULL, NULL, NULL, NULL };

private:
	virtual void init();

	virtual void onEvent();
	virtual void freeResource();

};

