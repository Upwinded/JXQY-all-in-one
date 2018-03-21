#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class MagicMenu :
	public Panel
{
public:
	MagicMenu();
	~MagicMenu();

	ImageContainer * title = NULL;
	ImageContainer * image = NULL;

	Scrollbar * scrollbar = NULL;

	Item * item[MENU_ITEM_COUNT] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

	void updateMagic();
	void updateMagic(int index);

private:
	int position = -1;
	virtual void onEvent();

	virtual void init();
	virtual void freeResource();

};

