#include "Scrollbar.h"

Scrollbar::Scrollbar()
{
	priority = epButton;
	coverMouse = true;
	canCallBack = true;
}

Scrollbar::~Scrollbar()
{
	freeResource();
}

void Scrollbar::positionChanged(int tempPosition)
{
	if (tempPosition != position)
	{
		position = tempPosition;
		if (slideBtn != nullptr)
		{
			lastRect = slideBtn->rect;
		}
		result |= erScrollbarSlided;
		if (canCallBack)
		{
			if (parent != nullptr)
			{
				parent->onChildCallBack(getMySharedPtr());
				result = erNone;
			}
		}
	}
}

void Scrollbar::freeResource()
{
	if (slideBtn != nullptr)
	{
		slideBtn->freeResource();
		slideBtn = nullptr;
	}

	impImage = nullptr;

	removeAllChild();
	result = erNone;
}

void Scrollbar::initFromIniWithName(INIReader & ini, const std::string& fileName)
{
	freeResource();

	style = (ScrollbarStyle)ini.GetInteger(u8"Init", u8"Style", int(style));
	rect.x = ini.GetInteger(u8"Init", u8"Left", rect.x);
	rect.y = ini.GetInteger(u8"Init", u8"Top", rect.y);
	rect.w = ini.GetInteger(u8"Init", u8"Width", rect.w);
	rect.h = ini.GetInteger(u8"Init", u8"Height", rect.h);
	min = ini.GetInteger(u8"Init", u8"Min", min);
	max = ini.GetInteger(u8"Init", u8"Max", max);
	lineSize = ini.GetInteger(u8"Init", u8"LineSize", lineSize);
	pageSize = ini.GetInteger(u8"Init", u8"PageSize", pageSize);
	slideBegin = ini.GetInteger(u8"Init", u8"SlideBegin", slideBegin);
	slideEnd = ini.GetInteger(u8"Init", u8"SlideEnd", slideEnd);
	max = ini.GetInteger(u8"Init", u8"Max", max);
	std::string impName = ini.Get(u8"Init", u8"Image", u8"");
	impImage = loadRes(impName);
	std::string slideBtnIni = ini.Get(u8"Init", u8"SlideBtn", u8"");
	slideBtnIni = convert::extractFilePath(fileName) + slideBtnIni;
	slideBtn = std::make_shared<DragButton>();
	addChild(slideBtn);

	std::unique_ptr<char[]> s;
	int len = 0;
	len = PakFile::readFile(slideBtnIni, s);
	if (s == nullptr || len == 0)
	{
		GameLog::write(u8"no ini file: %s\n", slideBtnIni.c_str());
		return;
	}
	INIReader sbIni(s);

	slideBtn->initFromIni(sbIni);
}

void Scrollbar::limitPos(int * p, int minp, int maxp)
{
	if (p == nullptr)
	{
		return;
	}
	if (*p < minp)
	{
		*p = minp;
	}
	else if (*p > maxp)
	{
		*p = maxp;
	}
}

void Scrollbar::setSlideBtnRect()
{
	slideRect = slideBtn->rect;
	lastRect = slideBtn->rect;
	if (style == ssVertical)
	{
		slideBegin += rect.y;
		slideEnd += rect.y;
	}
	else
	{
		slideBegin += rect.x;
		slideEnd += rect.x;
	}
}

void Scrollbar::setPosition(int pos)
{
	if (pos == position)
	{
		return;
	}
	if (pos < min)
	{
		pos = min;
	}
	else if (pos > max)
	{
		pos = max;
	}
	if (style == ssVertical)
	{
		slideBtn->rect.y  = int(((float)pos) / (float)(max - min) * ((float)(slideEnd - slideBegin)) + slideBegin);
	}
	else
	{
		slideBtn->rect.x = int(((float)pos) / (float)(max - min) * ((float)(slideEnd - slideBegin)) + slideBegin);
	}

}

void Scrollbar::onUpdate()
{	
	if (slideBtn == nullptr)
	{
		return;
	}
	if (lastRect.x == slideBtn->rect.x && lastRect.y == slideBtn->rect.y)
	{
		return;
	}
	float pos = 0;
	if (style == ssVertical)
	{
		slideBtn->rect.x = slideRect.x;
		limitPos(&slideBtn->rect.y, slideBegin, slideEnd);	
		if (slideEnd > slideBegin)
		{ 
			pos = ((float)slideBtn->rect.y - slideBegin) / ((float)(slideEnd - slideBegin));
		}
	}
	else
	{
		slideBtn->rect.y = slideRect.y;
		limitPos(&slideBtn->rect.x, slideBegin, slideEnd);
		if (slideEnd > slideBegin)
		{
			pos = ((float)slideBtn->rect.x - slideBegin) / ((float)(slideEnd - slideBegin));
		}
	}
	
	int tempPosition = (int)((pos * (float)(max - min)) + (float)min + 0.5);
	limitPos(&tempPosition, min, max);
	
	positionChanged(tempPosition);
	
}

void Scrollbar::onMouseLeftDown(int x, int y)
{
	if (slideBtn == nullptr)
	{
		return;
	}

	int tempPosition = position;
	if (style == ssVertical)
	{
		if (y > slideEnd + slideRect.h || y < slideBegin || x < slideRect.x || x > slideRect.w + slideRect.x)
		{
			return;
		}
		if (y > slideBtn->rect.y)
		{
			tempPosition += pageSize / lineSize;
		}
		else if (y < slideBtn->rect.y)
		{
			tempPosition -= pageSize / lineSize;
		}
		limitPos(&tempPosition, min, max);
		float pos = 0.0f;
		if (max > min)
		{
			pos = ((float)tempPosition - min) / ((float)(max - min));
		}
		slideBtn->rect.y = (int)((pos * (slideEnd - slideBegin)) + slideBegin + 0.5);
		limitPos(&slideBtn->rect.y, slideBegin, slideEnd);
	}
	else
	{
		if (x > slideEnd + slideRect.w || x < slideBegin || y < slideRect.y || y > slideRect.h + slideRect.y)
		{
			return;
		}
		if (x > slideBtn->rect.x)
		{
			tempPosition += pageSize / lineSize;
		}
		else if (x < slideBtn->rect.x)
		{
			tempPosition -= pageSize / lineSize;
		}
		limitPos(&tempPosition, min, max);
		float pos = 0.0f;
		if (max > min)
		{
			pos = ((float)tempPosition - min) / ((float)(max - min));
		}
		slideBtn->rect.x = (int)((pos * (slideEnd - slideBegin)) + slideBegin + 0.5);
		limitPos(&slideBtn->rect.x, slideBegin, slideEnd);
	}
	positionChanged(tempPosition);
}

void Scrollbar::onDraw()
{
	_shared_image image = IMP::loadImageForTime(impImage, getTime(), nullptr, nullptr);
	engine->drawImage(image, rect.x, rect.y);
}

void Scrollbar::onExit()
{
}

void Scrollbar::onSetChildRect()
{
	setSlideBtnRect();
}
