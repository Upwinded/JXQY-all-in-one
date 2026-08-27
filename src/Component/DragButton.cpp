#include "DragButton.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredDragButton = []
	{
		ComponentRegistry::getInstance().registerType("DragButton",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<DragButton>(); });
		return true;
	}();
}

DragButton::DragButton()
{
	canDrag = true;
	name = "DragButton";
}

DragButton::~DragButton()
{
	freeResource();
}

void DragButton::onDragEnd(PElement dst, int x, int y)
{
	rect.x = x;
	rect.y = y;

	result |= erDragEnd;
	if (canCallBack)
	{
		if (parent != nullptr)
		{
			parent->onChildCallBack(getMySharedPtr());
			result = erNone;
		}
	}
}

void DragButton::onDragging(int x, int y)
{
	rect.x = x;
	rect.y = y;
}


