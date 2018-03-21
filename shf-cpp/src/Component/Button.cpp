#include "Button.h"

Button::Button()
{
	name = "button";
	priority = epButton;
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

#ifdef SOUND_DYNAMIC_LOAD
	if (sound[index] == "")
	{
		return;
	}
	char * s = NULL;
	int len = PakFile::readFile(sound[index], &s);
	if (s != NULL && len > 0)
	{
		engine->playSound(s, len);
		delete[] s;
	}
#else
	engine->playSound(sound[index]);
#endif // SOUND_DYNAMIC_LOAD
	
}

void Button::onClick()
{
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

void Button::onDraw()
{
	int xOffset, yOffset;
	_image img = NULL;
	if (mouseLDownInRect)
	{
		img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		}
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		}
	}
	else if (mouseMoveIn)
	{
		img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		}
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		}
	}
	else
	{
		img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		}
		if (img == NULL)
		{
			img = IMP::loadImageForTime(image[2], getTime(), &xOffset, &yOffset);
		}
	}
	if (stretch)
	{
		engine->drawImage(img, NULL, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);//rect.x - xOffset, rect.y - yOffset);
	}
	
}

void Button::onExit()
{
}

void Button::onMouseMoveIn()
{
	initTime();
	playSound(0);
}

void Button::onMouseMoveOut()
{
	initTime();
}

void Button::onMouseLeftDown()
{
	initTime();
	playSound(1);
	result |= erMouseLDown;
}

void Button::onMouseLeftUp()
{	
	initTime();
	playSound(2);
	result |= erMouseLUp;
}

void Button::freeImage()
{
	for (size_t i = 0; i < 3; i++)
	{
		if (image[i] != NULL)
		{
			IMP::clearIMPImage(image[i]);
			delete image[i];
			image[i] = NULL;
		}
	}
}

void Button::freeSound()
{
	for (size_t i = 0; i < 3; i++)
	{
#ifdef SOUND_DYNAMIC_LOAD
		sound[i] = "";
#else
		if (sound[i] != NULL)
		{
			engine->freeMusic(sound[i]);
			sound[i] = NULL;
		}
#endif
	}
}

void Button::freeResource()
{
	freeImage();
	freeSound();
	removeAllChild();
	result = erNone;
}

void Button::initFromIni(const std::string & fileName)
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
	kind = ini.Get("Init", "Kind", kind);
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	std::string impName = ini.Get("Init", "Image", "");
	IMPImage * impImage = IMP::createIMPImage(impName);
	if (impImage != NULL)
	{
		if (kind == "TrackBtn")
		{
			int frame = 0;
			frame = ini.GetInteger("Init", "Up", 0);
			image[0] = IMP::createIMPImageFromFrame(impImage, frame);
			frame = ini.GetInteger("Init", "Track", 1);
			image[1] = IMP::createIMPImageFromFrame(impImage, frame);
			frame = ini.GetInteger("Init", "Down", 1);
			image[2] = IMP::createIMPImageFromFrame(impImage, frame);

			std::string soundName = ini.Get("Init", "Sound", "");
			loadSound(soundName, 1);
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
	else
	{
		printf("%s image file error\n", impName.c_str());
	}
	
	IMP::clearIMPImage(impImage);
	delete impImage;
	impImage = NULL;
}

void Button::loadSound(const std::string & fileName, int index)
{
	if (index < 0 || index >= 3)
	{
		return;
	}
#ifdef SOUND_DYNAMIC_LOAD
	sound[index] = fileName;
#else
	char * s = NULL;
	int len = PakFile::readFile(fileName, &s);
	if (s != NULL && len > 0)
	{
		if (sound[index] != NULL)
		{
			engine->freeMusic(sound[index]);
			sound[index] = NULL;
		}
		sound[index] = engine->loadSound(s, len);
		delete[] s;
	}
#endif // SOUND_DYNAMIC_LOAD
}

void Button::setRectFromImage()
{
	for (int i = 0; i < 3; i++)
	{
		_image img = IMP::loadImage(image[i], 0);
		int w, h;
		if (img != NULL && engine->getImageSize(img, &w, &h) == 0)
		{
			rect.w = w;
			rect.h = h;
			break;
		}
	}
}
