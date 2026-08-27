#include "FlatScrollbar.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"
#include "FlatComponentPolicy.h"

#include <algorithm>

namespace
{
bool registeredFlatScrollbar = []
{
	ComponentRegistry::getInstance().registerType("FlatScrollbar",
		[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<FlatScrollbar>(); });
	return true;
}();
}

FlatScrollbar::FlatScrollbar()
{
	name = "flatscrollbar";
	setPriority(epMenu);
	elementType = etScrollbar;
	canCallBack = true;
	coverMouse = true;
}

FlatScrollbar::~FlatScrollbar()
{
}

void FlatScrollbar::setRange(int minimumValue, int maximumValue)
{
	minimum = minimumValue;
	maximum = std::max(minimumValue, maximumValue);
	position = std::clamp(position, minimum, maximum);
}

void FlatScrollbar::setPosition(int value)
{
	position = std::clamp(value, minimum, maximum);
}

int FlatScrollbar::getPosition() const
{
	return position;
}

void FlatScrollbar::setPageSize(int value)
{
	pageSize = std::max(1, value);
}

int FlatScrollbar::getPageSize() const
{
	return pageSize;
}

void FlatScrollbar::setMinimumThumbLength(int value)
{
	minimumThumbLength = std::max(1, value);
}

void FlatScrollbar::setVisualTrackWidth(int value)
{
	visualTrackWidth = std::max(1, value);
}

Rect FlatScrollbar::getVisualTrackRect() const
{
	const int trackWidth = std::min(std::max(1, rect.w), visualTrackWidth);
	return { rect.x + (rect.w - trackWidth) / 2, rect.y, trackWidth, rect.h };
}

Rect FlatScrollbar::getThumbRect() const
{
	const Rect trackRect = getVisualTrackRect();
	const auto geometry = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		trackRect.y, trackRect.h, minimumThumbLength,
		minimum, maximum, pageSize, position);
	return { trackRect.x - 2, geometry.thumbStart, trackRect.w + 4, geometry.thumbLength };
}

bool FlatScrollbar::ownsPointerInteraction(EventTouchID pointerID) const
{
	return touchingDownID == pointerID;
}

void FlatScrollbar::onDraw()
{
	if (rect.w <= 0 || rect.h <= 0 || maximum <= minimum)
	{
		return;
	}

	const bool hovered = touchingID != TOUCH_UNTOUCHEDID;
	const bool pressed = touchingDownID != TOUCH_UNTOUCHEDID;
	const Rect trackRect = getVisualTrackRect();
	engine->fillRect(trackRect.x - 2, trackRect.y, trackRect.w + 4, trackRect.h,
		32, 24, 18, 130);
	engine->fillRect(trackRect.x + 2, trackRect.y + 3, std::max(1, trackRect.w - 4),
		std::max(1, trackRect.h - 6), 160, 144, 116, 90);

	const Rect thumbRect = getThumbRect();
	const uint8_t thumbAlpha = pressed ? 255 : (hovered ? 235 : 210);
	engine->fillRect(thumbRect.x, thumbRect.y, thumbRect.w, thumbRect.h,
		230, 202, 134, thumbAlpha);
	engine->fillRect(thumbRect.x + 2, thumbRect.y + 2,
		std::max(1, thumbRect.w - 4), std::max(1, thumbRect.h - 4),
		hovered ? 148 : 120, hovered ? 90 : 70, hovered ? 54 : 44, thumbAlpha);
}

void FlatScrollbar::onMouseLeftDown(int x, int y)
{
	(void)x;
	const Rect thumbRect = getThumbRect();
	if (y >= thumbRect.y && y < thumbRect.y + thumbRect.h)
	{
		pointerThumbOffset = y - thumbRect.y;
	}
	else
	{
		pointerThumbOffset = thumbRect.h / 2;
		updatePositionFromPointer(y);
	}
}

void FlatScrollbar::onMouseMoving(int x, int y)
{
	(void)x;
	if (touchingDownID != TOUCH_UNTOUCHEDID)
	{
		updatePositionFromPointer(y);
	}
}

bool FlatScrollbar::shouldKeepTouchWhenPointerLeaves(int x, int y)
{
	(void)x;
	(void)y;
	return touchingDownID != TOUCH_UNTOUCHEDID;
}

void FlatScrollbar::updatePositionFromPointer(int y)
{
	const Rect trackRect = getVisualTrackRect();
	const auto geometry = FlatComponentPolicy::calculateVerticalScrollbarGeometry(
		trackRect.y, trackRect.h, minimumThumbLength,
		minimum, maximum, pageSize, position);
	const int newPosition = FlatComponentPolicy::calculateVerticalScrollbarPosition(
		y - pointerThumbOffset, trackRect.y, geometry.travel, minimum, maximum);
	positionChanged(newPosition);
}

void FlatScrollbar::positionChanged(int value)
{
	value = std::clamp(value, minimum, maximum);
	if (value == position)
	{
		return;
	}
	position = value;
	result |= erScrollbarSlided;
	if (canCallBack && parent != nullptr)
	{
		parent->onChildCallBack(getMySharedPtr());
		result = erNone;
	}
}
