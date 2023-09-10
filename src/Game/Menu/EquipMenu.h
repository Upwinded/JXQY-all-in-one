#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class EquipMenu :
	public Panel
{
public:
	EquipMenu();
	virtual ~EquipMenu();

	ImageContainer * image = nullptr;
	ImageContainer * title = nullptr;
	Item * item[GOODS_BODY_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	void updateGoods();
	void updateGoods(int index);

	int getPartIndex(const std::string & part);

private:
	virtual void onEvent();
	virtual void init();
	void freeResource();
};

