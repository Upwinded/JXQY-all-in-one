
#include "RoundButton.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"
#include <algorithm>
#include <cctype>

namespace
{
	std::string normalizeIconName(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
		return value;
	}

	bool isIconPath(const std::string& value)
	{
		return value.find('\\') != std::string::npos ||
			value.find('/') != std::string::npos ||
			value.find('.') != std::string::npos;
	}

	std::string resolveIconImageName(const std::string& icon)
	{
		if (icon.empty())
		{
			return "";
		}

		if (isIconPath(icon))
		{
			return icon;
		}

		std::string iconName = normalizeIconName(icon);
		if (iconName == "minimap")
		{
			iconName = "map";
		}
		return "image\\ui\\mobile\\icon_" + iconName + ".png";
	}

	bool registeredRoundButton = []
	{
		ComponentRegistry::getInstance().registerType("RoundButton",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<RoundButton>(); });
		return true;
	}();
}

void RoundButton::freeResource()
{
	_iconImage = nullptr;
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
		_textImage[i] = _text.empty() ? nullptr : engine->createText(_text, (int)round(roundRange * 0.8), _textColor[i]);
	}
}

void RoundButton::setIcon(const std::string& icon)
{
	_icon = normalizeIconName(icon);
	setIconImage(resolveIconImageName(icon));
}

void RoundButton::setIconImage(const std::string& fileName)
{
	_iconImage = loadRes(fileName);
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
	_text = ini.Get("Init", "text", "");
	setText(_text);
	setIcon(ini.Get("Init", "icon", ""));
	std::string iconImageName = ini.Get("Init", "IconImage", "");
	if (!iconImageName.empty())
	{
		setIconImage(iconImageName);
	}
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
	if (drawIcon())
	{
		return;
	}
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

bool RoundButton::drawIcon()
{
	if (_iconImage == nullptr)
	{
		return false;
	}

	_shared_image img = IMP::loadImageForTime(_iconImage, getTime());
	if (img == nullptr)
	{
		return false;
	}

	engine->drawImage(img, nullptr, &rect);
	return true;
}
