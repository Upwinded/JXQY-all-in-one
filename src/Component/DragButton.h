#pragma once
#include "Button.h"
class DragButton :
	public Button
{
public:
	DragButton();
	virtual ~DragButton();

protected:
	virtual void onDragEnd(PElement dst, int x, int y);
	virtual void onDragging(int x, int y);
};

