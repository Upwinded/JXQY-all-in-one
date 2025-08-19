
#include "Joystick.h"

#ifdef pi
#undef pi
#endif // pi
#define pi 3.1415926

float Joystick::calculateAngel(int x, int y)
{
	float angle = atan2(-x, y);
	return angle;
}

int Joystick::calculateDirection(float angle)
{
	if (angle < 0)
	{
		angle += 2 * pi;
	}
	angle += pi / 8;
	if (angle > 2 * pi)
	{
		angle -= 2 * pi;
	}
	return (int)(angle / (pi / 4));
}

int Joystick::normalizeDirection(int dir)
{
	int result = dir % 8;
	while (result < 0)
	{
		result += 8;
	}
	return result;
}

std::vector<int> Joystick::getDirectionList()
{
	std::vector<int> directionList = std::vector<int>(0);
	if (touchPosition.x > OutRange && touchPosition.y > OutRange && distanceToCenter() >= roundRange * JOYSTICK_MIN_RANGE)
	{
		auto angel = calculateAngel(touchPosition.x - rect.w / 2, touchPosition.y - rect.h / 2);
		auto dir = calculateDirection(angel);
		directionList.push_back(normalizeDirection(dir));
		if ((angel - dir * pi / 4)  > (pi / 8))
		{
			directionList.push_back(normalizeDirection(dir + 1));
			directionList.push_back(normalizeDirection(dir - 1));
		}
		else
		{
			directionList.push_back(normalizeDirection(dir - 1));
			directionList.push_back(normalizeDirection(dir + 1));
		}
		
	}
	return directionList;
}

bool Joystick::isRunning()
{
    auto distance = distanceToCenter();
	return distance > roundRange * JOYSTICK_MID_RANGE && touchPosition.x > OutRange && touchPosition.y > OutRange;
}

bool Joystick::isWalking()
{
	auto distance = distanceToCenter();
	return distance > roundRange * JOYSTICK_MIN_RANGE && distance <= roundRange * JOYSTICK_MID_RANGE && touchPosition.x > OutRange && touchPosition.y > OutRange;
}

int Joystick::distanceToCenter()
{
	return RoundButton::distanceToCenter(touchPosition.x, touchPosition.y);
}

bool Joystick::mouseInRect(int x, int y)
{
	return RoundButton::mouseInRect(x, y) || (parent == nullptr ? true : parent->rect.PointInRect(x, y));
}

void Joystick::onMouseMoving(int x, int y)
{
	if (touchingID != dragging)
	{
		touchPosition.x = x - rect.x;
		touchPosition.y = y - rect.y;
	}
}

void Joystick::onMouseMoveIn(int x, int y)
{
	if (touchingID != dragging)
	{
		touchPosition.x = x - rect.x;
		touchPosition.y = y - rect.y;
	}
}

void Joystick::onMouseMoveOut()
{
    touchPosition.x = OutRange;
    touchPosition.y = OutRange;
}

void Joystick::onMouseLeftUp(int x, int y)
{
	touchPosition.x = OutRange;
	touchPosition.y = OutRange;
}

void Joystick::onMouseLeftDown(int x, int y)
{
    touchPosition.x = x - rect.x;
    touchPosition.y = y - rect.y;
	result |= erMouseLDown;
	if (canCallBack && parent != nullptr && parent->canCallBack)
	{
		parent->onChildCallBack(getMySharedPtr());
	}
}

void Joystick::onDraw()
{

	int xOffset = 0, yOffset = 0;
	auto img = IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
	if (stretch)
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
	}

	if (touchPosition.x > OutRange && touchPosition.y > OutRange)
	{
		img = IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		int w = 0, h = 0;
		engine->getImageSize(img, w, h);
		int x = rect.x + touchPosition.x - (int)round(w / 2);
		int y = rect.y + touchPosition.y - (int)round(h / 2);
		engine->drawImage(img, x, y);
	}
}

void Joystick::freeResource()
{
	RoundButton::freeResource();
}

void Joystick::initFromIni(INIReader & ini)
{
	freeResource();
	std::unique_ptr<char[]> s;
	int len = 0;

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	roundRange = ini.GetInteger("Init", "range", roundRange);
	setText("");
	std::string impName = ini.Get("Init", "Image", "");
	auto impImage = IMP::createIMPImage(impName);
	if (impImage != nullptr)
	{
		image[0] = IMP::createIMPImageFromFrame(impImage, 0);
		image[1] = IMP::createIMPImageFromFrame(impImage, 1);
	}
	else
	{
		GameLog::write("%s image file error\n", impName.c_str());
	}
	
	impImage = nullptr;
}
