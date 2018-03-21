#pragma once
#include "ImageContainer.h"
class Item :
	public ImageContainer
{
public:
	Item();
	~Item();

	unsigned int beginShowHintTime = 300;

	bool canShowHint = false;
	bool showHint = false;
	int fontSize = 18;

	_image strImage = NULL;

	unsigned int color = 0xFFFFFFFF;
	virtual void initFromIni(const std::string& fileName);

	virtual void setStr(const std::wstring & ws);
	
	void resetHint();
protected:
	std::wstring wstr = L"";

	virtual void freeResoure();

	virtual void drawItemStr();
	virtual void onDrop(Element * src, int param1, int param2);
	unsigned int moveInTime = 0;
	virtual void onEvent();
	virtual void onUpdate();
	virtual void onMouseMoveIn();
	virtual void onDraw();
	virtual void onDrawDrag();
	virtual bool onHandleEvent(AEvent * e);
};

