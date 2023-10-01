#include "Panel.h"

Panel::Panel()
{
	//stretch = true;
	coverMouse = false;
}

Panel::~Panel()
{
	freeResource();
}

#define LR_DISTANCE 30

void Panel::setAlign()
{
	int w, h;
	engine->getWindowSize(w, h);
	switch (align)
	{
	case alNone:
		break;
	case alClient:
		drawFullScreen = true;
		stretch = true;
		rect.x = 0;
		rect.y = 0;
		rect.w = w;
		rect.h = h;
		break;
	case alLeft:
		rect.x = 0 + LR_DISTANCE;
		break;
	case alRight:
		rect.x = w - rect.w - LR_DISTANCE;
		break;
	case alTop:
		rect.y = 0;
		break;
	case alBottom:
		rect.y = h - rect.h;
		break;
	case alLTCorner:
		rect.x = 0 + LR_DISTANCE;
		rect.y = 0;
		break;
	case alRTCorner:
		rect.y = 0;
		rect.x = w - rect.w - LR_DISTANCE;
		break;
	case alLBCorner:
		rect.x = 0 + LR_DISTANCE;
		rect.y = h - rect.h;
		break;
	case alRBCorner:
		rect.x = w - rect.w - LR_DISTANCE;
		rect.y = h - rect.h;
		break;
	case alCenter:
		rect.x = w / 2 - rect.w / 2;
		rect.y = h / 2 - rect.h / 2;
		break;
	case alLeftCenter:
		rect.x = 0 + LR_DISTANCE;
		rect.y = h / 2 - rect.h / 2;
		break;
	case alRightCenter:
		rect.x = w - rect.w - LR_DISTANCE;
		rect.y = h / 2 - rect.h / 2;
		break;
	case alTopCenter:
		rect.x = w / 2 - rect.w / 2;
		rect.y = 0;
		break;
	case alBottomCenter:
		rect.x = w / 2 - rect.w / 2;
		rect.y = h - rect.h;
		break;
	default:
		break;
	}
	rect.x += alignX;
	rect.y += alignY;
}

void Panel::freeResource()
{
	impImage = nullptr;
	result = erNone;
	removeAllChild();
	ImageContainer::freeResource();
}

void Panel::initFromIni(INIReader & ini)
{
	freeResource();

	align = alNone;

	std::string alignStr = ini.Get("Init", "Align", convert::formatString("%d", (int)align));
	alignX = ini.GetInteger("Init", "AlignX", 0);
	alignY = ini.GetInteger("Init", "AlignY", 0);

	const char* value = alignStr.c_str();
	if (value != nullptr)
	{
		char * end;
		long n = strtol(value, &end, 0);
		if (end > value)
		{
			align = (Align)n;
		}
		else
		{
#define checkAlign(a) (alignStr == convert::lowerCase(#a)) { align = a; }

			if checkAlign(alNone)
			else if checkAlign(alClient)
			else if checkAlign(alLeft)
			else if checkAlign(alRight)
			else if checkAlign(alTop)
			else if checkAlign(alBottom)
			else if checkAlign(alLTCorner)
			else if checkAlign(alRTCorner)
			else if checkAlign(alLBCorner)
			else if checkAlign(alRBCorner)
			else if checkAlign(alCenter)
			else if checkAlign(alLeftCenter)
			else if checkAlign(alRightCenter)
			else if checkAlign(alTopCenter)
			else if checkAlign(alBottomCenter)
		}	
	}
	
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	std::string impName = ini.Get("Init", "Image", "");
	impImage = loadRes(impName);
	setAlign();
}

void Panel::resetRect(PElement e, int x, int y)
{
	if (e.get() == nullptr)
	{
		return;
	}
	int xp = x, yp = y;
	if (e.get() == this)
	{
		setAlign();
		xp = rect.x - x;
		yp = rect.y - y;
	}
	else
	{
		e->rect.x += x;
		e->rect.y += y;
	}
	for (size_t i = 0; i < e->children.size(); i++)
	{
		resetRect(e->children[i], xp, yp);
	}
}

void Panel::resetRect()
{
	resetRect(getMySharedPtr(), 0, 0);
}
