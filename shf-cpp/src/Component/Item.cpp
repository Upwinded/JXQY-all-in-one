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

void Item::initFromIni(const std::string & fileName)
{
	freeResource();
	char * s = NULL;
	int len = 0;
	len = PakFile::readFile(fileName, &s);
	if (s == NULL || len == 0)
	{
		printf("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini = INIReader::INIReader(s);
	delete[] s;
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	fontSize = ini.GetInteger("Init", "Font", fontSize);
	std::string impName = ini.Get("Init", "Image", "");
	impImage = IMP::createIMPImage(impName);
	color = ini.GetColor("Init", "Color", color);
	
}

void Item::setStr(const std::wstring & ws)
{
	if (ws != wstr)
	{
		wstr = ws;
		if (strImage != NULL)
		{
			engine->freeImage(strImage);
			strImage = NULL;
		}
		strImage = engine->createUnicodeText(wstr, fontSize, color);
	}
}

void Item::resetHint()
{
	showHint = false;
	moveInTime = getTime();
}

void Item::freeResoure()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	if (strImage != NULL)
	{
		engine->freeImage(strImage);
		strImage = NULL;
	}
}

void Item::drawItemStr()
{
	if (wstr != L"")
	{
		if (strImage == NULL)
		{
			strImage = engine->createUnicodeText(wstr, fontSize, color);
		}
		engine->drawImage(strImage, rect.x, rect.y);
		//engine->drawUnicodeText(wstr, rect.x, rect.y, fontSize, color);
	}	
}

void Item::onDrop(Element * src, int param1, int param2)
{
	if (src != NULL && src != this)
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
	if (canShowHint && mouseMoveIn && getTime() - moveInTime > beginShowHintTime)
	{
		if (!showHint)
		{
			showHint = true;
			result = erShowHint;
		}
		
	}
	else
	{
		if (showHint)
		{
			showHint = false;
			result = erHideHint;
		}
	}
}

void Item::onMouseMoveIn()
{
	moveInTime = getTime();
}

void Item::onDraw()
{
	if (dragging && dragItem == this)
	{
		return;
	}
	_image img = IMP::loadImageForTime(impImage, getTime());
	if (stretch)
	{
		engine->drawImage(img, NULL, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);
	}
	drawItemStr();
}

void Item::onDrawDrag()
{
	int x, y;
	engine->getMouse(&x, &y);
	x -= dragX;
	y -= dragY;
	_image img = IMP::loadImageForTime(impImage, getTime());
	if (stretch)
	{
		Rect r = rect;
		r.x = x;
		r.y = y;
		engine->drawImage(img, NULL, &r);
	}
	else
	{
		engine->drawImage(img, x, y);
	}
}

bool Item::onHandleEvent(AEvent * e)
{
	if (mouseMoveIn && e->eventType == MOUSEDOWN)
	{
		if (e->eventData == MOUSE_RIGHT)
		{
			result = erMouseRDown;
			return true;
		}
	}
	return false;
}
