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
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	removeAllChild();
}

void ImageContainer::initFromIni(const std::string & fileName)
{
	freeResource();
	char * s = NULL;
	int len = 0;
	len = PakFile::readFile(fileName, &s);
	if (s == NULL || len == 0)
	{
		printf("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini = INIReader::INIReader(s);
	delete[] s;
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
	_image img = IMP::loadImageForTime(impImage, getTime());
	if (stretch)
	{
		engine->drawImage(img, NULL, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);
	}	
}
