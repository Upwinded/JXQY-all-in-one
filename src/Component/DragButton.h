#pragma once
#include "Button.h"
class DragButton :
	public Button
{
public:
	DragButton();
	virtual ~DragButton();

protected:
	virtual void onDragEnd(Element * dst, int x, int y);
	virtual void onDragging(int x, int y);
};

