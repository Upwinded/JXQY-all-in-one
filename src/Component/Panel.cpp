#include "Panel.h"
#include "../Engine/Engine.h"
#include "../libconvert/libconvert.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
	result = erNone;
	ImageContainer::freeResource();
}

void Panel::onWindowResize(int width, int height)
{
	Rect oldRect = rect;
	setAlign();
	Rect newRect = rect;
	int dx = newRect.x - oldRect.x;
	int dy = newRect.y - oldRect.y;
	rect = oldRect;
	if (dx != 0 || dy != 0)
	{
		offsetRectTree(dx, dy);
	}
	rect.w = newRect.w;
	rect.h = newRect.h;
}

void Panel::initFromIni(INIReader & ini)
{
	freeResource();

	align = alNone;
	stretch = false;
	keepAspect = false;
	fadeMirroredBars = false;
	scale = 1.0f;

	std::string alignStr = ini.Get("Init", "Align", convert::formatString("%d", (int)align));
	alignStr = convert::lowerCase(alignStr);
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
			// 注意：这个宏语法特殊，展开后是 if-else 链
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
	baseWidth = rect.w;
	baseHeight = rect.h;
	scale = ini.GetReal("Init", "Scale", 1.0f);
	if (!std::isfinite(scale) || scale <= 0.0f)
	{
		scale = 1.0f;
	}
	const double scaledWidth = static_cast<double>(rect.w) * scale;
	const double scaledHeight = static_cast<double>(rect.h) * scale;
	if (!std::isfinite(scaledWidth) || !std::isfinite(scaledHeight) ||
		scaledWidth < std::numeric_limits<int>::min() ||
		scaledWidth > std::numeric_limits<int>::max() ||
		scaledHeight < std::numeric_limits<int>::min() ||
		scaledHeight > std::numeric_limits<int>::max())
	{
		scale = 1.0f;
	}
	else
	{
		rect.w = static_cast<int>(std::lround(scaledWidth));
		rect.h = static_cast<int>(std::lround(scaledHeight));
	}
	name = ini.Get("Init", "Name", name);
	std::string impName = ini.Get("Init", "Image", "");
	if (impName.empty())
	{
		impName = ini.Get("Init", "Bitmap", "");
	}
	impImage = loadRes(impName);
	stretch = ini.GetBoolean("Init", "Stretch", stretch);
	keepAspect = ini.GetBoolean("Init", "KeepAspect", keepAspect);
	fadeMirroredBars = ini.GetBoolean(
		"Init", "FadeMirroredBars", fadeMirroredBars);
	setAlign();
}

void Panel::getChildScaleFactor(float& scaleX, float& scaleY)
{
	if (baseWidth > 0 && baseHeight > 0 &&
		(baseWidth != rect.w || baseHeight != rect.h))
	{
		scaleX = (float)rect.w / baseWidth;
		scaleY = (float)rect.h / baseHeight;
		if (keepAspect)
		{
			const float uniformScale = std::min(scaleX, scaleY);
			scaleX = uniformScale;
			scaleY = uniformScale;
		}
	}
	else
	{
		scaleX = 1.0f;
		scaleY = 1.0f;
	}
}

void Panel::getChildLayoutOffset(int& offsetX, int& offsetY)
{
	if (keepAspect && baseWidth > 0 && baseHeight > 0 &&
		rect.w > 0 && rect.h > 0)
	{
		const double uniformScale = std::min(
			static_cast<double>(rect.w) / baseWidth,
			static_cast<double>(rect.h) / baseHeight);
		const int contentWidth = static_cast<int>(
			std::round(baseWidth * uniformScale));
		const int contentHeight = static_cast<int>(
			std::round(baseHeight * uniformScale));
		offsetX = (rect.w - contentWidth) / 2;
		// Match ImageContainer's aspect-fit drawing alignment.
		offsetY = (rect.h - contentHeight) / 2;
	}
	else
	{
		offsetX = 0;
		offsetY = 0;
	}
}

void Panel::resetRect(PElement e, int x, int y)
{
	if (e.get() != this)
	{
		return;
	}
	Rect oldRect = rect;
	setAlign();
	offsetRectTree(rect.x - oldRect.x + x, rect.y - oldRect.y + y);
	rect = oldRect;
	setAlign();
}

void Panel::resetRect()
{
	resetRect(getMySharedPtr(), 0, 0);
}
