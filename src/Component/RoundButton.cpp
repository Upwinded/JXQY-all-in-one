
#include "RoundButton.h"

void RoundButton::freeResource()
{
	for (size_t i = 0; i < _textImageCount; i++)
	{
		_textImage[i] = nullptr;
	}
	Button::freeResource();
}

void RoundButton::setText(const std::string& text)
{
	_text = text;
	for (size_t i = 0; i < _textImageCount; i++)
	{
		_textImage[i] = engine->createText(_text, (int)round(roundRange * 0.8), _textColor[i]);
	}
}

void RoundButton::setRange(int range)
{
	roundRange = range;
	rect.w = range;
	rect.h = range;
}

int RoundButton::distanceToCenter(int x, int y)
{
	return (int)round(sqrt(pow(x - rect.w / 2, 2) + pow(y - rect.h / 2, 2)));
}

bool RoundButton::mouseInRect(int x, int y)
{
	if (Element::mouseInRect(x, y))
	{
		return distanceToCenter(x - rect.x, y - rect.y) <= (int)round(roundRange / 2);
	}
	return false;
}

void RoundButton::initFromIni(INIReader & ini)
{
	freeResource();

	Button::initFromIni(ini);

	roundRange = ini.GetInteger("Init", "Range", roundRange);
	_text = ini.Get("Init", "text", _text);
	setText(_text);
}

void RoundButton::onMouseLeftDown(int x, int y)
{
	if (parent != nullptr && parent->canCallBack)
	{
		result = erMouseLDown;
		parent->onChildCallBack(getMySharedPtr());
		result = erNone;
	}
}

void RoundButton::onClick()
{
	if (parent != nullptr && parent->canCallBack)
	{
		result = erClick;
		parent->onChildCallBack(getMySharedPtr());
		result = erNone;
	}
}

void RoundButton::onDraw()
{
	Button::onDraw();
	bool needDrawStr = true;
	if (drawItem != nullptr)
	{
		Rect drawRect = { rect.x + (int)round(rect.w * 0.1), rect.y + (int)round(rect.h * 0.1), (int)round(rect.w * 0.8), (int)round(rect.h * 0.8)};
		if (drawItem->drawImagetoRect(drawRect, true))
		{
			needDrawStr = false;
		}
	}
	if (!needDrawStr) { return; }
	_shared_image img = nullptr;
	if (touchingDownID != TOUCH_UNTOUCHEDID || (dragging != TOUCH_UNTOUCHEDID && currentDragItem.get() == this))
	{
		img = _textImage[1];
	}
	else
	{
		img = _textImage[0];
	}
	if (img == nullptr)
	{
		return;
	}
	int w = 0, h = 0;
	engine->getImageSize(img, w, h);
	int x = rect.x + (int)round((rect.w - w) / 2 - w * 0.05);
	int y = rect.y + (int)round((rect.h - h) / 2);
	engine->drawImage(img, x, y);

	/*if (stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else
	{
		int w = 0, h = 0;
		engine->getImageSize(img, w, h);
		int x = rect.x + (int)round((rect.w - w) / 2);
		int y = rect.y + (int)round((rect.h - h) / 2);
		engine->drawImage(img, x, y);
	}*/
}
