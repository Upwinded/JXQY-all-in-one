#include "DragButton.h"

DragButton::DragButton()
{
	canDrag = true;
	name = "DragButton";
}

DragButton::~DragButton()
{
}

void DragButton::onDragEnd(Element * dst)
{
	int x, y;
	engine->getMouse(&x, &y);
	x -= dragX;
	y -= dragY;
	rect.x = x;
	rect.y = y;

	result |= erDragEnd;
	if (canCallBack)
	{
		if (parent != NULL)
		{
			parent->onChildCallBack(this);
			result = erNone;
		}
	}
}

void DragButton::onDragging()
{
	int x, y;
	engine->getMouse(&x, &y);
	x -= dragX;
	y -= dragY;
	rect.x = x;
	rect.y = y;
}

