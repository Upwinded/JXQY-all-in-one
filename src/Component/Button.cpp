#include "Button.h"
#include "../Engine/Engine.h"
#include "../File/log.h"
#include "../libconvert/libconvert.h"
#include "ComponentRegistry.h"
#include "../Game/Data/MediaPathResolver.h"

namespace
{
	bool registeredButton = []
	{
		ComponentRegistry::getInstance().registerType("Button",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<Button>(); });
		return true;
	}();
}

Button::Button()
{
	name = "button";
	setPriority(epButton);
	elementType = etButton;
	result = erNone;
}

Button::~Button()
{
	freeResource();
}

void Button::playSound(int index)
{
	if (index < 0 || index >= 3)
	{
		return;
	}

	if (sound[index].empty())
	{
		return;
	}
	engine->playCachedSoundFile(sound[index]);
	
}

void Button::draw()
{
	draw(rect.x, rect.y);
}

_shared_image Button::loadButtonImage(int& xOffset, int& yOffset, int first, int second, int third)
{
	_shared_image img = IMP::loadImageForTime(image[first], getTime(), &xOffset, &yOffset);
	if (img == nullptr)
	{
		img = IMP::loadImageForTime(image[second], getTime(), &xOffset, &yOffset);
	}
	if (img == nullptr)
	{
		img = IMP::loadImageForTime(image[third], getTime(), &xOffset, &yOffset);
	}
	return img;
}

void Button::draw(int x, int y)
{
	int xOffset, yOffset;
	_shared_image img = nullptr;
	if (touchingDownID != TOUCH_UNTOUCHEDID || (dragging != TOUCH_UNTOUCHEDID && currentDragItem == getMySharedPtr()))
	{
		img = loadButtonImage(xOffset, yOffset, 2, 1, 0);
	}
	else if (touchingID != TOUCH_UNTOUCHEDID || isFocused())
	{
		img = loadButtonImage(xOffset, yOffset, 1, 0, 2);
	}
	else
	{
		img = loadButtonImage(xOffset, yOffset, 0, 1, 2);
	}
	if (stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else
	{
		engine->drawImage(img, x, y);
	}
}

void Button::onClick()
{
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

void Button::onDraw()
{
	if (flat)
	{
		engine->fillRect(rect.x, rect.y, rect.w, rect.h, 171, 146, 93, 255);
		if (rect.w > 2 && rect.h > 2)
		{
			engine->fillRect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2, 44, 84, 70, 255);
		}
		drawFocusBorder();
		return;
	}
	draw();
	drawFocusBorder();
}

void Button::drawFocusBorder()
{
	if (isFocused() && rect.w >= 2 && rect.h >= 2)
	{
		constexpr int FocusBorderWidth = 2;
		constexpr uint8_t FocusRed = 255;
		constexpr uint8_t FocusGreen = 214;
		constexpr uint8_t FocusBlue = 92;
		constexpr uint8_t FocusAlpha = 240;
		engine->fillRect(rect.x, rect.y, rect.w, FocusBorderWidth,
			FocusRed, FocusGreen, FocusBlue, FocusAlpha);
		engine->fillRect(rect.x, rect.y + rect.h - FocusBorderWidth,
			rect.w, FocusBorderWidth, FocusRed, FocusGreen, FocusBlue, FocusAlpha);
		engine->fillRect(rect.x, rect.y, FocusBorderWidth, rect.h,
			FocusRed, FocusGreen, FocusBlue, FocusAlpha);
		engine->fillRect(rect.x + rect.w - FocusBorderWidth, rect.y,
			FocusBorderWidth, rect.h, FocusRed, FocusGreen, FocusBlue, FocusAlpha);
	}
}

void Button::onExit()
{
}

void Button::onMouseMoveIn(int x, int y)
{
	initTime();
	if (hoverSoundEnabled)
	{
		playSound(0);
	}
}

void Button::onMouseMoveOut()
{
	initTime();
}

void Button::onMouseLeftDown(int x, int y)
{
	//initTime();
	playSound(1);
	result |= erMouseLDown;
}

void Button::onMouseLeftUp(int x, int y)
{	
	//initTime();
	playSound(2);
	result |= erMouseLUp;
}

void Button::freeImage()
{
	for (size_t i = 0; i < 3; i++)
	{
		image[i] = nullptr;
	}
}

void Button::freeSound()
{
	for (size_t i = 0; i < 3; i++)
	{
		sound[i] = "";
	}
}

void Button::freeResource()
{
	freeImage();
	freeSound();
	removeAllChild();
	result = erNone;
}

void Button::initFromIni(INIReader & ini)
{
	freeResource();
	animateFrames = false;
	hoverSoundEnabled = ini.GetBoolean("Init", "HoverSound", true);
	flat = ini.GetBoolean("Init", "Flat", false);
	kind = ini.Get("Init", "Kind", kind);
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	animateFrames = ini.GetBoolean("Init", "Animate", animateFrames);
	std::string impName = ini.Get("Init", "Image", "");
	if (impName.empty())
	{
		impName = ini.Get("Init", "Bitmap", "");
	}
	auto impImage = IMP::createIMPImage(impName);
	if (impImage != nullptr)
	{
		if (animateFrames)
		{
			image[0] = impImage;
			image[1] = impImage;
			image[2] = impImage;

			std::string soundName = ini.Get("Init", "Sound", "");
			loadSound(soundName, 1);
		}
		else if (convert::lowerCase(kind) == "trackbtn")
		{
			int frame = 0;
			frame = ini.GetInteger("Init", "Up", 0);
			image[0] = IMP::createIMPImageFromFrame(impImage, frame);
			frame = ini.GetInteger("Init", "Track", 1);
			image[1] = IMP::createIMPImageFromFrame(impImage, frame);
			frame = ini.GetInteger("Init", "Down", 1);
			image[2] = IMP::createIMPImageFromFrame(impImage, frame);

			std::string soundName = ini.Get("Init", "Sound", "");
			loadSound(soundName, 0);
		}
		else
		{
			int frame = 0;
			frame = ini.GetInteger("Init", "Up", 0);
			image[0] = IMP::createIMPImageFromFrame(impImage, frame);
			frame = ini.GetInteger("Init", "Down", 1);
			image[2] = IMP::createIMPImageFromFrame(impImage, frame);

			std::string soundName = ini.Get("Init", "Sound", "");
			loadSound(soundName, 1);
		}
	}
	else if (!impName.empty())
	{
		GameLog::write("Button:%s,%s image file error\n", ini.fileName.c_str(), impName.c_str());
	}

	stretch = ini.GetBoolean("Init", "Stretch", stretch);
	impImage = nullptr;
}

void Button::loadSound(const std::string & fileName, int index)
{
	if (index < 0 || index >= 3)
	{
		return;
	}
	if (fileName.empty())
	{
		return;
	}
	std::string soundPath = buildSoundAssetPath(fileName);
	sound[index] = soundPath;
	engine->preloadCachedSoundFile(soundPath);
}

void Button::setRectFromImage()
{
	for (int i = 0; i < 3; i++)
	{
		_shared_image img = IMP::loadImage(image[i], 0);
		int w, h;
		if (img != nullptr && engine->getImageSize(img, w, h))
		{
			rect.w = w;
			rect.h = h;
			break;
		}
	}
}
