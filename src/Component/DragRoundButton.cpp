
#include "DragRoundButton.h"

void DragRoundButton::setIndicateImage(std::string fileName)
{
	freeImp();
	indicateImp = loadRes(fileName);
}

void DragRoundButton::initFromIni(INIReader & ini)
{
	freeResource();
	RoundButton::initFromIni(ini);
	type = (DragRoundButtonType)ini.GetInteger("Init", "IndicateType", (int)DragRoundButtonType::None);
	auto impName = ini.Get("Init", "IndicateImage", "");
	setIndicateImage(impName);
}

void DragRoundButton::freeResource()
{
	freeImp();
	RoundButton::freeResource();
}

void DragRoundButton::freeImp()
{
	indicateImp = nullptr;
}

void DragRoundButton::onDragEnd(PElement dst, int x, int y)
{
	dragPosition = { x, y };
	dragRealPosition = dragTouchPosition;
	if (parent != nullptr && canCallBack && parent->canCallBack)
	{
		result |= erDragEnd;
		parent->onChildCallBack(getMySharedPtr());
	}
}

void DragRoundButton::onDragging(int x, int y)
{
	dragPosition = { x, y };
	dragRealPosition = dragTouchPosition;
}

void DragRoundButton::onDraw()
{
	RoundButton::onDraw();
}

void DragRoundButton::onDrawDrag(int x, int y)
{

}
