
#include "DragRoundButton.h"
#include "../Game/GameManager/GameManager.h"

void DragRoundButton::setIMP(std::string fileName)
{
	freeImp();
	imp = IMP::createIMPImageFromFile(fileName);
}

void DragRoundButton::freeResource()
{
	freeImp();
	RoundButton::freeResource();
}

void DragRoundButton::freeImp()
{
	if (imp != nullptr)
	{
		IMP::clearIMPImage(imp);
		//delete imp;
		imp = nullptr;
	}
}

void DragRoundButton::onDragEnd(Element* dst, int x, int y)
{
	dragPosition = { x, y };
	if (parent != nullptr && canCallBack && parent->canCallBack)
	{
		result |= erDragEnd;
		parent->onChildCallBack(this);
	}
}

void DragRoundButton::onDragging(int x, int y)
{
	dragPosition = { x, y };
}

void DragRoundButton::onDraw()
{
	if (dragging > TOUCH_UNTOUCHEDID && currentDragItem == this)
	{
		return;
	}
	RoundButton::onDraw();
}

void DragRoundButton::onDrawDrag(int x, int y)
{
	int xOffset = 0, yOffset = 0;
	auto actionTime = IMP::getIMPImageActionTime(imp);
	auto now = getTime() - dragBeginTime;
	auto playerPos = gm->player->position;
	actionTime = actionTime > 0 ? actionTime : 1;
	now = now % actionTime;
	auto actionSplitTime = actionTime / 3;
	actionSplitTime = actionSplitTime > 0 ? actionSplitTime : 1;
	auto state = now / actionSplitTime;
	int dir = 0;
	int w = 0;
	int h = 0;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	Point realPos = { x, y };
	Point realTilePos = gm->map.getMousePosition(realPos, gm->player->position, cenScreen, gm->camera.offset);
	dir = gm->player->calDirection(realTilePos);
	switch (state)
	{
	case 0:
	{
		realPos = gm->map.getTilePosition(gm->player->position, gm->camera.position, cenScreen, gm->camera.offset);
		break;
	}
	case 1:
	{
		playerPos = gm->map.getTilePosition(gm->player->position, gm->camera.position, cenScreen, gm->camera.offset);
		double param = ((double)now - actionSplitTime) / actionSplitTime;
		realPos.x = (int)round(param * (realPos.x - playerPos.x) + playerPos.x);
		realPos.y = (int)round(param * (realPos.y - playerPos.y) + playerPos.y);
		break;
	}
		
	default:
	{
		/*realPos = gm->map.getTilePosition(realTilePos, gm->player->position, cenScreen, gm->camera.offset);*/
		break;
	}
	}
	auto img = IMP::loadImageForDirection(imp, dir, now, &xOffset, &yOffset);
	engine->setImageAlpha(img, 128);
	engine->drawImage(img, realPos.x - xOffset, realPos.y - yOffset);
	//auto tempRect = rect;
	//rect.x = x;
	//rect.y = y;
	//RoundButton::onDraw();
	//rect = tempRect;
}


