#include "DragButton.h"

DragButton::DragButton()
{
	canDrag = true;
	name = "DragButton";
}

DragButton::~DragButton()
{
	freeResource();
}

void DragButton::onDragEnd(PElement dst, int x, int y)
{
	rect.x = x;
	rect.y = y;

	result |= erDragEnd;
	if (canCallBack)
	{
		if (parent != nullptr)
		{
			parent->onChildCallBack(getMySharedPtr());
			result = erNone;
		}
	}
}

void DragButton::onDragging(int x, int y)
{
	rect.x = x;
	rect.y = y;
}

