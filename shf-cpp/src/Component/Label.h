#pragma once
#include "Item.h"
class Label :
	public Item
{
public:
	Label();
	~Label();

	bool autoNextLine = false;

	virtual void setStr(const std::wstring & ws);
private:
	virtual void freeResource();
	std::vector<_image> strImage = {};
	virtual void drawItemStr();
};

