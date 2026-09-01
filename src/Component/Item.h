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
	bool centerImage = false;
	int fontSize = 18;

	_shared_image strImage = nullptr;

	unsigned int color = 0xFFFFFFFF;
	_shared_imp backImage[2] = { nullptr, nullptr };
	virtual void initFromIni(INIReader & ini);

	virtual void setStr(const std::string & s);
	
	void resetHint();
	void setTransferSelected(bool value) { transferSelected = value; }
	bool isTransferSelected() const { return transferSelected; }
	const std::string & getStr() { return str; }
protected:
	std::string str = "";
	bool transferSelected = false;

	virtual void freeResource() override;

	virtual void drawItemStr();
	virtual void onDrop(PElement src, int param1, int param2);
	UTime moveInTime = 0;
	virtual void onEvent();
	virtual void onUpdate();
	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoving(int x, int y) override;
	virtual bool shouldKeepTouchWhenPointerLeaves(int x, int y) override;

	virtual void onDrawDrag(int x, int y);
	virtual bool onHandleEvent(AEvent & e);
	virtual void onClick();
public:
	virtual void onDraw();
};
