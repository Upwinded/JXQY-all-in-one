#include "Scrollbar.h"
#include "../Engine/Engine.h"
#include "../File/log.h"
#include "../libconvert/libconvert.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredScrollbar = []
	{
		ComponentRegistry::getInstance().registerType("Scrollbar",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<Scrollbar>(); });
		return true;
	}();
}

Scrollbar::Scrollbar()
{
	setPriority(epButton);
	coverMouse = true;
	canCallBack = true;
	slideBeginOriginal = slideBegin;
	slideEndOriginal = slideEnd;
}

Scrollbar::~Scrollbar()
{
	freeResource();
}

void Scrollbar::positionChanged(int newPosition)
{
	if (newPosition != position)
	{
		position = newPosition;
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

	style = (ScrollbarStyle)ini.GetInteger("Init", "Style", int(style));
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	min = ini.GetInteger("Init", "Min", min);
	max = ini.GetInteger("Init", "Max", max);
	position = ini.GetInteger("Init", "Position", position);
	lineSize = ini.GetInteger("Init", "LineSize", lineSize);
	pageSize = ini.GetInteger("Init", "PageSize", pageSize);
	slideBegin = ini.GetInteger("Init", "SlideBegin", slideBegin);
	slideEnd = ini.GetInteger("Init", "SlideEnd", slideEnd);
	slideBeginOriginal = slideBegin;
	slideEndOriginal = slideEnd;
	std::string impName = ini.Get("Init", "Image", "");
	impImage = loadRes(impName);
	std::string slideBtnIni = ini.Get("Init", "SlideBtn", "");
	slideBtnIni = convert::extractFilePath(fileName) + slideBtnIni;
	slideBtn = std::make_shared<DragButton>();
	addChild(slideBtn);

	std::unique_ptr<char[]> s;
	int len = 0;
	len = File::readFile(slideBtnIni, s);
	if (s == nullptr || len == 0)
	{
		GameLog::write("no ini file: %s\n", slideBtnIni.c_str());
		return;
	}
	INIReader sbIni(s);

	slideBtn->initFromIni(sbIni);
	slideBtn->hoverSoundEnabled = false;
}

void Scrollbar::limitPos(int& p, int minVal, int maxVal)
{
	if (p < minVal)
	{
		p = minVal;
	}
	else if (p > maxVal)
	{
		p = maxVal;
	}
}

void Scrollbar::setSlideBtnRect()
{
	slideRect = slideBtn->rect;
	if (slideRect.x < rect.x)
	{
		slideRect.x += rect.x;
	}
	if (slideRect.y < rect.y)
	{
		slideRect.y += rect.y;
	}
	lastRect = slideRect;
	if (style == ssVertical)
	{
		slideBegin = slideBeginOriginal + rect.y;
		slideEnd = slideEndOriginal + rect.y;
	}
	else
	{
		slideBegin = slideBeginOriginal + rect.x;
		slideEnd = slideEndOriginal + rect.x;
	}
}

int Scrollbar::getSlidePos() const
{
	return style == ssVertical ? slideBtn->rect.y : slideBtn->rect.x;
}

void Scrollbar::setSlidePos(int pos)
{
	if (style == ssVertical)
	{
		slideBtn->rect.y = pos;
	}
	else
	{
		slideBtn->rect.x = pos;
	}
}

int Scrollbar::getMouseCoord(int x, int y) const
{
	return style == ssVertical ? y : x;
}

void Scrollbar::updatePositionFromSlide()
{
	float pos = 0;
	if (slideEnd > slideBegin)
	{
		pos = ((float)getSlidePos() - slideBegin) / ((float)(slideEnd - slideBegin));
	}
	int newPosition = (int)((pos * (float)(max - min)) + (float)min + 0.5);
	limitPos(newPosition, min, max);
	positionChanged(newPosition);
}

void Scrollbar::setPosition(int pos)
{
	limitPos(pos, min, max);
	position = pos;
	syncSlideButtonPosition();
}

void Scrollbar::syncSlideButtonPosition()
{
	if (slideBtn == nullptr)
	{
		return;
	}
	limitPos(position, min, max);
	if (max <= min)
	{
		setSlidePos(slideBegin);
		if (style == ssVertical)
		{
			slideBtn->rect.x = slideRect.x;
		}
		else
		{
			slideBtn->rect.y = slideRect.y;
		}
		lastRect = slideBtn->rect;
		return;
	}
	float positionRate = (float)(position - min) / (float)(max - min);
	int slidePos = int(positionRate * (float)(slideEnd - slideBegin) + slideBegin + 0.5f);
	limitPos(slidePos, slideBegin, slideEnd);
	setSlidePos(slidePos);
	if (style == ssVertical)
	{
		slideBtn->rect.x = slideRect.x;
	}
	else
	{
		slideBtn->rect.y = slideRect.y;
	}
	lastRect = slideBtn->rect;
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
	if (style == ssVertical)
	{
		slideBtn->rect.x = slideRect.x;
	}
	else
	{
		slideBtn->rect.y = slideRect.y;
	}
	int currentSlidePos = getSlidePos();
	limitPos(currentSlidePos, slideBegin, slideEnd);
	setSlidePos(currentSlidePos);
	updatePositionFromSlide();
}

void Scrollbar::onMouseLeftDown(int x, int y)
{
	if (slideBtn == nullptr)
	{
		return;
	}

	int mouseCoord = getMouseCoord(x, y);
	int otherCoord = style == ssVertical ? x : y;
	int otherRectMin = style == ssVertical ? slideRect.x : slideRect.y;
	int otherRectMax = otherRectMin + (style == ssVertical ? slideRect.w : slideRect.h);
	
	if (mouseCoord > slideEnd + (style == ssVertical ? slideRect.h : slideRect.w) || 
	    mouseCoord < slideBegin || 
	    otherCoord < otherRectMin || 
	    otherCoord > otherRectMax)
	{
		return;
	}

	int newPosition = position;
	int currentSlidePos = getSlidePos();
	if (mouseCoord > currentSlidePos)
	{
		newPosition += pageSize / lineSize;
	}
	else if (mouseCoord < currentSlidePos)
	{
		newPosition -= pageSize / lineSize;
	}
	
	limitPos(newPosition, min, max);
	float pos = 0.0f;
	if (max > min)
	{
		pos = ((float)newPosition - min) / ((float)(max - min));
	}
	int newSlidePos = (int)((pos * (slideEnd - slideBegin)) + slideBegin + 0.5);
	limitPos(newSlidePos, slideBegin, slideEnd);
	setSlidePos(newSlidePos);
	positionChanged(newPosition);
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
	bool slideButtonRectIsAbsolute = slideBtn != nullptr && slideBtn->rect.x >= rect.x && slideBtn->rect.y >= rect.y;
	setSlideBtnRect();
	if (slideButtonRectIsAbsolute)
	{
		syncSlideButtonPosition();
	}
}
