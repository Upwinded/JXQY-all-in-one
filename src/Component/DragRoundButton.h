#pragma once
#include "RoundButton.h"
#include <vector>
#include "Item.h"

class DragRoundButton :
	public RoundButton
{
public:
	DragRoundButton() { canDrag = true; canCallBack = true; }
	DragRoundButton(int range):  RoundButton(range) { canDrag = true; canCallBack = true; }
	virtual ~DragRoundButton() { freeResource(); }
	void setIMP(std::string fileName);
	Point getDragPosition() { return dragPosition; }
private:
	_shared_imp imp = nullptr;
	void freeResource();
	void freeImp();
	UTime dragBeginTime = 0;
protected:
	Point dragPosition = { 0, 0 };

	virtual void onDragEnd(Element* dst, int x, int y);
	virtual void onDragging(int x, int y);
	virtual void onDraw();
	virtual void onDrawDrag(int x, int y);
	virtual void onMouseLeftDown(int x, int y) { dragging = touchingID; currentDragItem = this; onDragBegin(nullptr, nullptr); }
	virtual void onDragBegin(int* param1, int* param2) { dragBeginTime = getTime(); }

};

