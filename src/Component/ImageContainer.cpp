#include "ImageContainer.h"

ImageContainer::ImageContainer()
{
	priority = epImage;
	name = "ImageContainer";
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

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	std::string impName = ini.Get("Init", "Image", "");
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
