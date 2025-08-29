#include "ImageContainer.h"

ImageContainer::ImageContainer()
{
	priority = epImage;
	name = u8"ImageContainer";
	elementType = etImageContainer;
	coverMouse = false;
}

ImageContainer::~ImageContainer()
{
	freeResource();
}

void ImageContainer::freeResource()
{
	impImage = nullptr;
	removeAllChild();
}

void ImageContainer::initFromIni(INIReader & ini)
{
	freeResource();

	rect.x = ini.GetInteger(u8"Init", u8"Left", rect.x);
	rect.y = ini.GetInteger(u8"Init", u8"Top", rect.y);
	rect.w = ini.GetInteger(u8"Init", u8"Width", rect.w);
	rect.h = ini.GetInteger(u8"Init", u8"Height", rect.h);
	name = ini.Get(u8"Init", u8"Name", name);
	std::string impName = ini.Get(u8"Init", u8"Image", u8"");
	impImage = loadRes(impName);
}

void ImageContainer::onDraw()
{
	drawImagetoRect(rect, stretch);
}

bool ImageContainer::drawImagetoRect(Rect destRect, bool drawStretch)
{
	_shared_image img = IMP::loadImageForTime(impImage, getTime());
	if (drawStretch)
	{
		engine->drawImage(img, nullptr, &destRect);
	}
	else
	{
		engine->drawImage(img, destRect.x, destRect.y);
	}
	return img != nullptr;
}
