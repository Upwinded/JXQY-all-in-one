#include "CheckBox.h"



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
	std::string impName = ini.Get("Init", "Image", "");
	auto impImage = loadRes(impName);
	if (impImage != nullptr)
	{
		int frame = 0;
		frame = ini.GetInteger("Init", "Up", 0);
		image[0] = IMP::createIMPImageFromFrame(impImage, frame);
		frame = ini.GetInteger("Init", "Down", 1);
		image[2] = IMP::createIMPImageFromFrame(impImage, frame);		
	}
	else
	{
		GameLog::write("%s image file error\n", impName.c_str());
	}

	std::string soundName = ini.Get("Init", "Sound", "");
	loadSound(soundName, 1);

	impImage = nullptr;
}

void CheckBox::onDraw()
{
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
