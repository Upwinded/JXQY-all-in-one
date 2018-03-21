#include "CheckBox.h"



CheckBox::CheckBox()
{
	coverMouse = true;
}


CheckBox::~CheckBox()
{
}

void CheckBox::initFromIni(const std::string & fileName)
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
	std::string impName = ini.Get("Init", "Image", "");
	IMPImage * impImage = IMP::createIMPImage(impName);
	if (impImage != NULL)
	{
		int frame = 0;
		frame = ini.GetInteger("Init", "Up", 0);
		image[0] = IMP::createIMPImageFromFrame(impImage, frame);
		frame = ini.GetInteger("Init", "Down", 1);
		image[2] = IMP::createIMPImageFromFrame(impImage, frame);		
	}
	else
	{
		printf("%s image file error\n", impName.c_str());
	}

	std::string soundName = ini.Get("Init", "Sound", "");
	loadSound(soundName, 1);

	IMP::clearIMPImage(impImage);
	delete impImage;
	impImage = NULL;
}

void CheckBox::onDraw()
{
	int xOffset, yOffset;
	_image img = NULL;
	if (!checked)
	{
		img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		}
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		}
	}
	else
	{
		img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		}
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		}
	}
	if (stretch)
	{
		engine->drawImage(img, NULL, &rect);
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
		if (parent != NULL)
		{
			parent->onChildCallBack(this);
			result = erNone;
		}
	}
}

void CheckBox::onMouseLeftDown()
{
	playSound(1);
	result |= erMouseLDown;
}

void CheckBox::onMouseLeftUp()
{
	playSound(2);
	result |= erMouseLUp;
}
