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
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	removeAllChild();
}

void ImageContainer::initFromIni(const std::string & fileName)
{
	freeResource();
	std::unique_ptr<char[]> s;
	int len = 0;
	len = PakFile::readFile(fileName, s);
	if (s == nullptr || len == 0)
	{
		printf("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini(s);

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	std::string impName = ini.Get("Init", "Image", "");
	impImage = IMP::createIMPImage(impName);
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
