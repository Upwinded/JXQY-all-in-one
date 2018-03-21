#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class EquipMenu :
	public Panel
{
public:
	EquipMenu();
	~EquipMenu();

	ImageContainer * image = NULL;
	ImageContainer * title = NULL;
	Item * item[GOODS_BODY_COUNT] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };

	void updateGoods();
	void updateGoods(int index);



	int getPartIndex(const std::string & part);

private:
	virtual void onEvent();
	virtual void init();
	virtual void freeResource();
};

