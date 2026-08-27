#include "Item.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

#include <algorithm>
#include <cmath>

namespace
{
	bool registeredItem = []
	{
		ComponentRegistry::getInstance().registerType("Item",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<Item>(); });
		return true;
	}();
}

Item::Item()
{
	setPriority(epItem);
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
	centerImage = false;

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	fontSize = ini.GetInteger("Init", "Font", fontSize);
	stretch = ini.GetBoolean("Init", "Stretch", stretch);
	keepAspect = ini.GetBoolean("Init", "KeepAspect", keepAspect);
	centerImage = ini.GetBoolean("Init", "CenterImage", centerImage);
	frameIndex = ini.GetInteger("Init", "Frame", frameIndex);
	std::string impName = ini.Get("Init", "Image", "");
	if (impName.empty())
	{
		impName = ini.Get("Init", "Bitmap", "");
	}
	impImage = loadRes(impName);
	backImage[0] = loadRes(ini.Get("Init", "BackImage1", ""));
	backImage[1] = loadRes(ini.Get("Init", "BackImage2", ""));
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

void Item::freeResource()
{
	transferSelected = false;
	impImage = nullptr;
	backImage[0] = nullptr;
	backImage[1] = nullptr;

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
		int textWidth = 0;
		int textHeight = 0;
		engine->getImageSize(strImage, textWidth, textHeight);
		if (backImage[0] != nullptr && textWidth > 0 && textHeight > 0)
		{
			engine->drawImage(strImage, rect.x + rect.w - textWidth, rect.y + rect.h - textHeight);
		}
		else
		{
			engine->drawImage(strImage, rect.x, rect.y);
		}
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

	_shared_imp currentBackImage = nullptr;
	if (touchingID != TOUCH_UNTOUCHEDID && backImage[1] != nullptr)
	{
		currentBackImage = backImage[1];
	}
	else
	{
		currentBackImage = backImage[0];
	}
	if (currentBackImage != nullptr)
	{
		auto backgroundImage = IMP::loadImageForTime(currentBackImage, getTime());
		engine->drawImage(backgroundImage, nullptr, &rect);
	}

	_shared_image img = frameIndex >= 0
		? IMP::loadImage(impImage, frameIndex)
		: IMP::loadImageForTime(impImage, getTime());
	if (img != nullptr)
	{
		if (stretch)
		{
			engine->drawImage(img, nullptr, &rect);
		}
		else
		{
			int imageWidth = 0;
			int imageHeight = 0;
			engine->getImageSize(img, imageWidth, imageHeight);
			if (imageWidth > rect.w || imageHeight > rect.h)
			{
				double scale = std::min(
					static_cast<double>(rect.w) / static_cast<double>(imageWidth),
					static_cast<double>(rect.h) / static_cast<double>(imageHeight));
				Rect drawRect =
				{
					rect.x + (rect.w - std::max(1, static_cast<int>(std::round(imageWidth * scale)))) / 2,
					rect.y + (rect.h - std::max(1, static_cast<int>(std::round(imageHeight * scale)))) / 2,
					std::max(1, static_cast<int>(std::round(imageWidth * scale))),
					std::max(1, static_cast<int>(std::round(imageHeight * scale)))
				};
				engine->drawImage(img, nullptr, &drawRect);
			}
			else if (backImage[0] != nullptr || centerImage)
			{
				engine->drawImage(img, rect.x + (rect.w - imageWidth) / 2, rect.y + (rect.h - imageHeight) / 2);
			}
			else
			{
				engine->drawImage(img, rect.x, rect.y);
			}
		}
	}
	drawItemStr();
	if (transferSelected && rect.w >= 4 && rect.h >= 4)
	{
		constexpr int TransferBorderWidth = 3;
		constexpr uint8_t TransferRed = 82;
		constexpr uint8_t TransferGreen = 210;
		constexpr uint8_t TransferBlue = 255;
		constexpr uint8_t TransferAlpha = 245;
		engine->fillRect(rect.x, rect.y, rect.w, TransferBorderWidth,
			TransferRed, TransferGreen, TransferBlue, TransferAlpha);
		engine->fillRect(rect.x, rect.y + rect.h - TransferBorderWidth,
			rect.w, TransferBorderWidth,
			TransferRed, TransferGreen, TransferBlue, TransferAlpha);
		engine->fillRect(rect.x, rect.y, TransferBorderWidth, rect.h,
			TransferRed, TransferGreen, TransferBlue, TransferAlpha);
		engine->fillRect(rect.x + rect.w - TransferBorderWidth, rect.y,
			TransferBorderWidth, rect.h,
			TransferRed, TransferGreen, TransferBlue, TransferAlpha);
	}
	if (isFocused() && rect.w >= 6 && rect.h >= 6)
	{
		constexpr int FocusBorderWidth = 2;
		constexpr int FocusInset = 3;
		constexpr uint8_t FocusRed = 255;
		constexpr uint8_t FocusGreen = 214;
		constexpr uint8_t FocusBlue = 92;
		constexpr uint8_t FocusAlpha = 240;
		const int focusWidth = rect.w - FocusInset * 2;
		const int focusHeight = rect.h - FocusInset * 2;
		engine->fillRect(rect.x + FocusInset, rect.y + FocusInset,
			focusWidth, FocusBorderWidth,
			FocusRed, FocusGreen, FocusBlue, FocusAlpha);
		engine->fillRect(rect.x + FocusInset,
			rect.y + rect.h - FocusInset - FocusBorderWidth,
			focusWidth, FocusBorderWidth,
			FocusRed, FocusGreen, FocusBlue, FocusAlpha);
		engine->fillRect(rect.x + FocusInset, rect.y + FocusInset,
			FocusBorderWidth, focusHeight,
			FocusRed, FocusGreen, FocusBlue, FocusAlpha);
		engine->fillRect(rect.x + rect.w - FocusInset - FocusBorderWidth,
			rect.y + FocusInset, FocusBorderWidth, focusHeight,
			FocusRed, FocusGreen, FocusBlue, FocusAlpha);
	}
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
