#pragma once
#include "RoundButton.h"
#include <vector>
#include "Item.h"

enum class DragRoundButtonType
{
	None = 0,
	IndicatePoint = 1,
	IndicateDirection = 2
};

class DragRoundButton :
	public RoundButton
{
public:
	DragRoundButton() { canDrag = true; canCallBack = true; dragRange = 10; }

	virtual ~DragRoundButton() { freeResource(); }
	void setIndicateImage(std::string fileName);
	Point getDragPosition() { return dragPosition; }
	Point getDragRealPosition() { return dragRealPosition; }
	virtual void initFromIni(INIReader & ini);
private:
	_shared_imp indicateImp = nullptr;
	void freeResource();
	void freeImp();
	UTime dragBeginTime = 0;
protected:
	Point dragPosition = { 0, 0 };
	Point dragRealPosition = { 0, 0 };
	DragRoundButtonType type = DragRoundButtonType::None;
	std::string imageName = u8"";

	virtual void onDragEnd(PElement dst, int x, int y);
	virtual void onDragging(int x, int y);
	virtual void onDraw();
	virtual void onDrawDrag(int x, int y);
	//virtual void onMouseLeftDown(int x, int y) { dragging = touchingID; currentDragItem = getMySharedPtr(); onDragBegin(nullptr, nullptr); }
	virtual void onDragBegin(int* param1, int* param2) { dragBeginTime = getTime(); }

};

