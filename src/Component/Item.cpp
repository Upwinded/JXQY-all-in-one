#include "Item.h"

Item::Item()
{
	priority = epItem;
	name = "Item";
	elementType = etItem;
	canDrag = true;
	coverMouse = true;
	canDrop = true;
}

Item::~Item()
{
	freeResource();
}

void Item::initFromIni(INIReader & ini)
{
	freeResource();

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	fontSize = ini.GetInteger("Init", "Font", fontSize);
	std::string impName = ini.Get("Init", "Image", "");
	impImage = loadRes(impName);
	color = ini.GetColor("Init", "Color", color);
}

void Item::setStr(const std::string & s)
{
	if (str.compare(s) != 0)
	{
		str = s;
		if (strImage != nullptr)
		{
			//engine->freeImage(strImage);
			strImage = nullptr;
		}
		strImage = engine->createText(str, fontSize, color);
	}
}

void Item::resetHint()
{
	showHint = false;
	moveInTime = getTime();
}

void Item::freeResoure()
{		
	impImage = nullptr;

	if (strImage != nullptr)
	{
		//engine->freeImage(strImage);
		strImage = nullptr;
	}
	ImageContainer::freeResource();
}

void Item::drawItemStr()
{
	if (str != "")
	{
		if (strImage == nullptr)
		{
			strImage = engine->createText(str, fontSize, color);
		}
		engine->drawImage(strImage, rect.x, rect.y);
	}	
}

void Item::onDrop(PElement src, int param1, int param2)
{
	if (src.get() != nullptr && src.get() != this)
	{
		result |= erDropped;
		dropType = src->dragType;
		dropIndex = src->dragIndex;
	}
}

void Item::onUpdate()
{
}

void Item::onEvent()
{
	if (canShowHint && touchingID != TOUCH_UNTOUCHEDID && getTime() - moveInTime > beginShowHintTime)
	{
		if (!showHint)
		{
			showHint = true;
			result |= erShowHint;
		}
		
	}
	else
	{
		if (showHint)
		{
			showHint = false;
			result |= erHideHint;
		}
	}
}

void Item::onMouseMoveIn(int x, int y)
{
	moveInTime = getTime();
}

void Item::onDraw()
{
	if (dragging > TOUCH_UNTOUCHEDID && currentDragItem.get() == this)
	{
		return;
	}
	_shared_image img = IMP::loadImageForTime(impImage, getTime());
	if (stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);
	}
	drawItemStr();
}

void Item::onDrawDrag(int x, int y)
{
	_shared_image img = IMP::loadImageForTime(impImage, getTime());
	if (stretch)
	{
		Rect r = rect;
		r.x = x;
		r.y = y;
		engine->drawImage(img, nullptr, &r);
	}
	else
	{
		engine->drawImage(img, x, y);
	}
}

bool Item::onHandleEvent(AEvent & e)
{
	if (touchingID != TOUCH_UNTOUCHEDID && e.eventType == ET_MOUSEDOWN)
	{
		if (e.eventData == MBC_MOUSE_RIGHT)
		{
			result |= erMouseRDown;
			return true;
		}
	}
	return false;
}

void Item::onClick()
{
	result |= erClick;
}