#pragma once
#include "Item.h"
class Label :
	public Item
{
public:
	Label();
	virtual ~Label();

	bool autoNextLine = false;

	virtual void setStr(const std::string & s);
protected:
	void freeResource();
	std::vector<_shared_image> strImage;
	virtual void drawItemStr();
	virtual void onMouseLeftDown(int x, int y);
};

