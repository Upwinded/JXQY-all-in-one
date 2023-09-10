
#include "RoundButton.h"

RoundButton::RoundButton(int range): RoundButton()
{
	setRange(range);
}

void RoundButton::freeResource()
{
	for (size_t i = 0; i < _textImageCount; i++)
	{
		if (_textImage[i] != nullptr)
		{
			//engine->freeImage(_textImage[i]);
			_textImage[i] = nullptr;
		}
	}
	Button::freeResource();
}

void RoundButton::setText(const std::string& text)
{
	freeResource();
	_text = text;
	for (size_t i = 0; i < _textImageCount; i++)
	{
		_textImage[i] = engine->createText(_text, (int)round(roundRange * 0.8), _textColor[i]);
	}
}

void RoundButton::setRange(int range)
{
	roundRange = range;
	rect.w = range;
	rect.h = range;
}

int RoundButton::distanceToCenter(int x, int y)
{
	return (int)round(sqrt(pow(x - rect.w / 2, 2) + pow(y - rect.h / 2, 2)));
}

bool RoundButton::mouseInRect(int x, int y)
{
	if (Element::mouseInRect(x, y))
	{
		return distanceToCenter(x - rect.x, y - rect.y) <= (int)round(roundRange / 2);
	}
	return false;
}

void RoundButton::initFromIni(const std::string& fileName)
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

	kind = ini.Get("Init", "Kind", kind);
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	roundRange = ini.GetInteger("Init", "Range", roundRange);
	_text = ini.Get("Init", "text", _text);
	setText(_text);
	std::string impName = ini.Get("Init", "Image", "");
	auto impImage = IMP::createIMPImage(impName);
	if (impImage != nullptr)
	{
		if (convert::lowerCase(kind) == "trackbtn")
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
	//delete impImage;
	impImage = nullptr;
}

void RoundButton::onMouseLeftDown(int x, int y)
{
	if (parent != nullptr && parent->canCallBack)
	{
		result = erMouseLDown;
		parent->onChildCallBack(this);
		result = erNone;
	}
}

void RoundButton::onDraw()
{
	Button::onDraw();
	bool needDrawStr = true;
	if (drawItem != nullptr)
	{
		Rect drawRect = { rect.x + (int)round(rect.w * 0.1), rect.y + (int)round(rect.h * 0.1), (int)round(rect.w * 0.8), (int)round(rect.h * 0.8)};
		if (drawItem->drawImagetoRect(drawRect, true))
		{
			needDrawStr = false;
		}
	}
	if (!needDrawStr) { return; }
	_shared_image img = nullptr;
	if (touchInRectID != TOUCH_UNTOUCHEDID || (dragging != TOUCH_UNTOUCHEDID && currentDragItem == this))
	{
		img = _textImage[1];
	}
	else
	{
		img = _textImage[0];
	}
	if (img == nullptr)
	{
		return;
	}
	int w = 0, h = 0;
	engine->getImageSize(img, w, h);
	int x = rect.x + (int)round((rect.w - w) / 2 - w * 0.05);
	int y = rect.y + (int)round((rect.h - h) / 2);
	engine->drawImage(img, x, y);

	/*if (stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else
	{
		int w = 0, h = 0;
		engine->getImageSize(img, w, h);
		int x = rect.x + (int)round((rect.w - w) / 2);
		int y = rect.y + (int)round((rect.h - h) / 2);
		engine->drawImage(img, x, y);
	}*/
}
