#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class MagicMenu :
	public Panel
{
public:
	MagicMenu();
	virtual ~MagicMenu();

	ImageContainer * title = nullptr;
	ImageContainer * image = nullptr;

	Scrollbar * scrollbar = nullptr;

	Item * item[MENU_ITEM_COUNT] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

	void updateMagic();
	void updateMagic(int index);

private:
	int position = -1;
	virtual void onEvent();

	virtual void init();
	void freeResource();

};

