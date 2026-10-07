#include "CheckBox.h"
#include <algorithm>
#include "../Engine/Engine.h"
#include "../File/log.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredCheckBox = []
	{
		ComponentRegistry::getInstance().registerType("CheckBox",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<CheckBox>(); });
		return true;
	}();
}



CheckBox::CheckBox()
{
	coverMouse = true;
}


CheckBox::~CheckBox()
{
	freeResource();
}

void CheckBox::initFromIni(INIReader & ini)
{
	freeResource();

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	stretch = ini.GetBoolean("Init", "Stretch", stretch);
	std::string impName = ini.Get("Init", "Image", "");
	if (impName.empty())
	{
		impName = ini.Get("Init", "Bitmap", "");
	}
	auto impImage = impName.empty() ? nullptr : loadRes(impName);
	if (impImage != nullptr)
	{
		int frame = 0;
		frame = ini.GetInteger("Init", "Up", 0);
		image[0] = IMP::createIMPImageFromFrame(impImage, frame);
		frame = ini.GetInteger("Init", "Down", 1);
		image[2] = IMP::createIMPImageFromFrame(impImage, frame);		
	}
	else if (!impName.empty())
	{
		GameLog::write("CheckBox:%s,%s image file error\n", ini.fileName.c_str(), impName.c_str());
	}

	std::string soundName = ini.Get("Init", "Sound", "");
	loadSound(soundName, 1);
	textLabel.fontSize = ini.GetInteger("Init", "Font", 18);
	textLabel.autoShrink = true;
	textLabel.horizontalAlignment = TextHorizontalAlignment::Center;
	textLabel.verticalAlignment = TextVerticalAlignment::Center;
	textLabel.setStr(ini.Get("Init", "Text", ""));

	impImage = nullptr;
}

void CheckBox::onDraw()
{
	if (!textLabel.getStr().empty())
	{
		const bool highlighted = checked || isFocused() || touchingID != TOUCH_UNTOUCHEDID;
		engine->fillRect(rect.x, rect.y, rect.w, rect.h, 154, 128, 76, 255);
		engine->fillRect(rect.x + 1, rect.y + 1, std::max(0, rect.w - 2), std::max(0, rect.h - 2),
			highlighted ? 36 : 238, highlighted ? 77 : 227, highlighted ? 67 : 197, 255);
		if (image[0] != nullptr)
		{
			engine->drawImage(IMP::loadImageForTime(image[highlighted && image[2] ? 2 : 0], getTime()), nullptr, &rect);
		}
		textLabel.rect = { rect.x + 4, rect.y + 2, std::max(1, rect.w - 8), std::max(1, rect.h - 4) };
		textLabel.color = highlighted ? 0xFFF8E8BC : 0xFF234F43;
		textLabel.onDraw();
		drawFocusBorder();
		return;
	}
	int xOffset, yOffset;
	_shared_image img = nullptr;
	if (!checked)
	{
		img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		if (img == nullptr)
		{
			img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		}
		if (img == nullptr)
		{
			img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		}
	}
	else
	{
		img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		if (img == nullptr)
		{
			img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		}
		if (img == nullptr)
		{
			img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		}
	}
	if (stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);
	}
	drawFocusBorder();
}

void CheckBox::onClick()
{
	initTime();
	checked = !checked;
	result |= erClick;
	if (canCallBack)
	{
		if (parent != nullptr)
		{
			parent->onChildCallBack(getMySharedPtr());
			result = erNone;
		}
	}
}

void CheckBox::onMouseLeftDown(int x, int y)
{
	playSound(1);
	result |= erMouseLDown;
}

void CheckBox::onMouseLeftUp(int x, int y)
{
	playSound(2);
	result |= erMouseLUp;
}
