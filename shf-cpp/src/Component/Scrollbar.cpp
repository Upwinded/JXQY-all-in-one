#include "Scrollbar.h"

Scrollbar::Scrollbar()
{
	priority = epButton;
	coverMouse = true;
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
		if (slideBtn != NULL)
		{
			lastRect = slideBtn->rect;
		}
		result |= erScrollbarSlided;
		if (canCallBack)
		{
			if (parent != NULL)
			{
				parent->onChildCallBack(this);
				result = erNone;
			}
		}
	}
}

void Scrollbar::freeResource()
{
	if (slideBtn != NULL)
	{
		slideBtn->freeResource();
		delete slideBtn;
		slideBtn = NULL;
	}
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	removeAllChild();
	result = erNone;
}

void Scrollbar::initFromIni(const std::string & fileName)
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

	style = (ScrollbarStyle)ini.GetInteger("Init", "Style", int(style));
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	min = ini.GetInteger("Init", "Min", min);
	max = ini.GetInteger("Init", "Max", max);
	lineSize = ini.GetInteger("Init", "LineSize", lineSize);
	pageSize = ini.GetInteger("Init", "PageSize", pageSize);
	slideBegin = ini.GetInteger("Init", "SlideBegin", slideBegin);
	slideEnd = ini.GetInteger("Init", "SlideEnd", slideEnd);
	max = ini.GetInteger("Init", "Max", max);
	std::string impName = ini.Get("Init", "Image", "");
	impImage = IMP::createIMPImage(impName);
	std::string slideBtnIni = ini.Get("Init", "SlideBtn", "");
	slideBtnIni = convert::extractFilePath(fileName) + slideBtnIni;
	slideBtn = new DragButton;
	addChild(slideBtn);
	slideBtn->initFromIni(slideBtnIni);
}

void Scrollbar::limitPos(int * p, int minp, int maxp)
{
	if (p == NULL)
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
	if (slideBtn == NULL)
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

void Scrollbar::onMouseLeftDown()
{
	if (slideBtn == NULL)
	{
		return;
	}
	int x, y;
	engine->getMouse(&x, &y);

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
	_image image = IMP::loadImageForTime(impImage, getTime(), NULL, NULL);
	engine->drawImage(image, rect.x, rect.y);
}

void Scrollbar::onExit()
{
}

void Scrollbar::onSetChildRect()
{
	setSlideBtnRect();
}
