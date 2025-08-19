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

	virtual void init() override;

	void updateGoodsItem();
	void updateGoodsItem(int index);
	void updateGoodsNumber();
	void updateGoodsNumber(int index);

	void updateMagicItem();
	void updateMagicItem(int index);

	std::shared_ptr<ColumnMenu> columnMenu = nullptr;

	std::shared_ptr<CheckBox> equipBtn = nullptr;
	std::shared_ptr<CheckBox> goodsBtn = nullptr;
	std::shared_ptr<CheckBox> magicBtn = nullptr;
	std::shared_ptr<CheckBox> notesBtn = nullptr;
	std::shared_ptr<CheckBox> optionBtn = nullptr;
	std::shared_ptr<CheckBox> stateBtn = nullptr;
	std::shared_ptr<CheckBox> xiulianBtn = nullptr;

	std::shared_ptr<Item> goodsItem[GOODS_TOOLBAR_COUNT] = { nullptr, nullptr, nullptr };
	std::shared_ptr<Item> magicItem[MAGIC_TOOLBAR_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr };

private:

	virtual void onEvent();
	void freeResource();

};

