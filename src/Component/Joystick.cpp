#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include "Joystick.h"
#include "../Engine/Engine.h"
#include "../File/log.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredJoystick = []
	{
		ComponentRegistry::getInstance().registerType("Joystick",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<Joystick>(); });
		return true;
	}();
}

std::vector<int> Joystick::getDirectionList()
{
	if (touchPosition.x <= OutRange || touchPosition.y <= OutRange)
	{
		return std::vector<int>();
	}
	return getMobileJoystickDirectionCandidates(
		touchPosition.x - rect.w / 2,
		touchPosition.y - rect.h / 2,
		roundRange);
}

bool Joystick::isRunning()
{
	updateMovementState();
	return movementState == MobileJoystickMovementState::Run;
}

bool Joystick::isWalking()
{
	updateMovementState();
	return movementState == MobileJoystickMovementState::Walk;
}

void Joystick::updateMovementState()
{
	if (touchPosition.x <= OutRange || touchPosition.y <= OutRange)
	{
		movementState = MobileJoystickMovementState::Idle;
		return;
	}
	movementState = getMobileJoystickMovementState(
		movementState,
		touchPosition.x - rect.w / 2,
		touchPosition.y - rect.h / 2,
		roundRange);
}

int Joystick::distanceToCenter()
{
	return getMobileJoystickDistance(
		touchPosition.x - rect.w / 2,
		touchPosition.y - rect.h / 2);
}

void Joystick::resetInput()
{
	touchPosition = { OutRange, OutRange };
	movementState = MobileJoystickMovementState::Idle;
}

bool Joystick::mouseInRect(int x, int y)
{
	return RoundButton::mouseInRect(x, y);
}

bool Joystick::shouldKeepTouchWhenPointerLeaves(int x, int y)
{
	return touchingDownID != TOUCH_UNTOUCHEDID;
}

bool Joystick::onPointerInteractionCanceled(EventTouchID pointerID)
{
	if (touchingID != pointerID && touchingDownID != pointerID)
	{
		return false;
	}
	resetInput();
	return true;
}

void Joystick::onAllPointerInteractionsCanceled()
{
	resetInput();
}

void Joystick::onMouseMoving(int x, int y)
{
	if (touchingDownID != TOUCH_UNTOUCHEDID
		&& touchingDownID == touchingID
		&& touchingID != dragging)
	{
		touchPosition.x = x - rect.x;
		touchPosition.y = y - rect.y;
	}
}

void Joystick::onMouseMoveIn(int x, int y)
{
	if (touchingDownID != TOUCH_UNTOUCHEDID
		&& touchingDownID == touchingID
		&& touchingID != dragging)
	{
		touchPosition.x = x - rect.x;
		touchPosition.y = y - rect.y;
	}
}

void Joystick::onMouseMoveOut()
{
	if (touchingDownID == TOUCH_UNTOUCHEDID)
	{
		resetInput();
	}
}

void Joystick::onMouseLeftUp(int x, int y)
{
	resetInput();
}

void Joystick::onMouseLeftDown(int x, int y)
{
	resetInput();
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
	auto img = useStaticImages
		? IMP::loadImage(image[0], 0, &xOffset, &yOffset)
		: IMP::loadImageForTime(image[0], getTime(), &xOffset, &yOffset);
	if (img != nullptr && stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else if (img != nullptr)
	{
		int w = 0, h = 0;
		engine->getImageSize(img, w, h);
		int x = rect.x + (int)round((rect.w - w) / 2);
		int y = rect.y + (int)round((rect.h - h) / 2);
		engine->drawImage(img, x, y);
	}

	if (touchPosition.x > OutRange && touchPosition.y > OutRange)
	{
		img = useStaticImages
			? IMP::loadImage(image[1], 0, &xOffset, &yOffset)
			: IMP::loadImageForTime(image[1], getTime(), &xOffset, &yOffset);
		if (img != nullptr)
		{
			int w = 0, h = 0;
			engine->getImageSize(img, w, h);
			int x = rect.x + touchPosition.x - (int)round(w / 2);
			int y = rect.y + touchPosition.y - (int)round(h / 2);
			engine->drawImage(img, x, y);
		}
	}
}

void Joystick::freeResource()
{
	useStaticImages = false;
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
	std::string baseImageName = ini.Get("Init", "BaseImage", "");
	std::string thumbImageName = ini.Get("Init", "ThumbImage", "");
	if (!baseImageName.empty() || !thumbImageName.empty())
	{
		useStaticImages = true;
		image[0] = loadRes(baseImageName);
		image[1] = loadRes(thumbImageName);
		if (image[0] == nullptr || image[1] == nullptr)
		{
			GameLog::write("Joystick:%s base/thumb image file error:%s,%s\n",
				ini.fileName.c_str(), baseImageName.c_str(), thumbImageName.c_str());
		}
		return;
	}

	std::string impName = ini.Get("Init", "Image", "");
	auto impImage = IMP::createIMPImage(impName);
	if (impImage != nullptr)
	{
		image[0] = IMP::createIMPImageFromFrame(impImage, 0);
		image[1] = IMP::createIMPImageFromFrame(impImage, 1);
	}
	else
	{
		GameLog::write("Joystick:%s,%s image file error\n", ini.fileName.c_str(), impName.c_str());
	}
	
	impImage = nullptr;
}
