#pragma once
#include "Button.h"
#include <vector>
#include "Item.h"

class RoundButton :
	public Button
{
public:
	RoundButton() { stretch = true; }
	virtual ~RoundButton() { freeResource(); }
protected:
	int roundRange = 50;
private:
	std::string _text = u8"";
#define _textImageCount 2
	_shared_image _textImage[_textImageCount] = { nullptr, nullptr };
	const unsigned int _textColor[_textImageCount] = {0xFF000000, 0xFFFF0000};
public:
	void freeResource();
	void setText(const std::string& text);
	void setRange(int range);
	int distanceToCenter(int x, int y);
protected:
	virtual bool mouseInRect(int x, int y);
	virtual void onDraw();
	virtual void onClick();
public:
	virtual void initFromIni(INIReader & ini);
	virtual void onMouseLeftDown(int x, int y);
	std::shared_ptr<Item> drawItem = nullptr;
};

