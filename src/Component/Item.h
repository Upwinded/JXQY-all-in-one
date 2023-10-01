#pragma once
#include "ImageContainer.h"
class Item :
	public ImageContainer
{
public:
	Item();
	virtual ~Item();

	unsigned int beginShowHintTime = 300;

	bool canShowHint = false;
	bool showHint = false;
	int fontSize = 18;

	_shared_image strImage = nullptr;

	unsigned int color = 0xFFFFFFFF;
	virtual void initFromIni(INIReader & ini);

	virtual void setStr(const std::string & s);
	
	void resetHint();
	const std::string & getStr() { return str; }
protected:
	std::string str = "";

	virtual void freeResoure();

	virtual void drawItemStr();
	virtual void onDrop(PElement src, int param1, int param2);
	UTime moveInTime = 0;
	virtual void onEvent();
	virtual void onUpdate();
	virtual void onMouseMoveIn(int x, int y);

	virtual void onDrawDrag(int x, int y);
	virtual bool onHandleEvent(AEvent & e);
	virtual void onClick();
public:
	virtual void onDraw();
};

