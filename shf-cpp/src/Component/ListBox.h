#pragma once
#include "ImageContainer.h"
#include "Button.h"

class ListBox :
	public Element
{
public:
	ListBox();
	~ListBox();

	int itemSize = 20;

	Rect rect;
	unsigned int color = 0xFFFFFFFF;
	unsigned int selColor = 0xFFE6C864;
	int itemHeight = 0;
	int itemCount = 0;
	std::string soundName = "";
	std::vector<std::string> itemName = {};
	std::vector<Button *> itemButton = {};
	int index = -1;

	virtual void initFromIni(const std::string & fileName);
	virtual void freeResource();
private:
	virtual void onEvent();
};

