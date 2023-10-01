#pragma once
#include "ImageContainer.h"
#include "Button.h"

class ListBox :
	public BaseComponent
{
public:
	ListBox();
	virtual ~ListBox();

	int itemSize = 20;

	Rect rect;
	unsigned int color = 0xFFFFFFFF;
	unsigned int selColor = 0xFFE6C864;
	int itemHeight = 0;
	int itemCount = 0;
	std::string soundName = "";
	std::vector<std::string> itemName;
	std::vector<std::shared_ptr<Button>> itemButton;
	int index = -1;

	virtual void initFromIni(INIReader & ini);
	void freeResource();
private:
	virtual void onEvent();
};

