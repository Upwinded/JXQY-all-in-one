#pragma once
#include "Button.h"
class DragButton :
	public Button
{
public:
	DragButton();
	~DragButton();

private:
	virtual void onDragEnd(Element * dst);
	virtual void onDragging();
};

