#pragma once

#include "../../Element/Element.h"

// Wheel ownership follows the displayed rectangles, including decoration and
// disabled controls. Click eligibility and rectFullScreen are deliberately not
// involved. Floating tooltips can be excluded from their physical parent.
inline bool menuMouseWheelContains(
	const PElement& element, int x, int y, const Element* excluded = nullptr)
{
	if (element == nullptr || element.get() == excluded
		|| !element->visible || !element->canDraw)
	{
		return false;
	}
	for (const auto& child : element->children)
	{
		if (menuMouseWheelContains(child, x, y, excluded))
		{
			return true;
		}
	}
	const Rect& rect = element->rect;
	return rect.w > 0 && rect.h > 0 && x >= rect.x && y >= rect.y
		&& x - rect.x < rect.w && y - rect.y < rect.h;
}

inline bool menuMouseWheelSourceBelongsTo(
	const PElement& source, const PElement& owner)
{
	for (const Element* element = source.get(); element != nullptr;
		element = element->parent)
	{
		if (!element->visible || !element->canDraw)
		{
			return false;
		}
		if (element == owner.get())
		{
			return true;
		}
	}
	return false;
}

inline int menuMouseWheelSteps(const AEvent& event)
{
	// Engine events already normalize integer wheel steps: positive is down.
	return event.eventData;
}
