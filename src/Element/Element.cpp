#include "Element.h"

PElement Element::_topParent = std::make_shared<Element>();

PElement Element::currentDragItem = nullptr;
int Element::dragParam[2] = { 0, 0 };
EventTouchID Element::dragging = TOUCH_UNTOUCHEDID;
Point Element::dragTouchPosition = { 0, 0 };
Point Element::dragDownPosition = { 0, 0 };
std::vector<PElement> Element::runningElement;

Element::Element()
{
	engine = Engine::getInstance();
	children.resize(0);
	initTime();
}

Element::~Element()
{
	freeResource();
	removeAllChild();
	if (currentDragItem.get() == this)
	{
		currentDragItem = nullptr;
	}
}

void Element::setAsTop(PElement child)
{
	if (child.get() == nullptr)
	{
		return;
	}
	if (child->parent != nullptr)
	{
		child->parent->removeChild(child->getMySharedPtr());
	}
	_topParent->addChild(child);
}

void Element::removeFromTop(PElement child)
{
	_topParent->removeChild(child);
}

void Element::addChild(PElement child)
{
	if (child.get() != nullptr)
	{
		// 只允许有一个parent，其原parent需要移除此child
		if (child->parent != nullptr)
		{
			child->parent->removeChild(child);
		}

		//不允许重复添加child，先进行查重
		for (size_t i = 0; i < children.size(); i++)
		{
			if (child.get() == children[i].get())
			{
				return;
			}
		}
		child->parent = this;
		children.push_back(child);
		child->timer.setParent(&timer);
		reArrangeChildren();
	}
}

void Element::removeChild(PElement child)
{
	if (child.get() == nullptr || children.size() == 0)
	{
		return;
	}
	auto iter = children.begin();
	while (iter != children.end())
	{
		if (iter->get() == child.get())
		{
			iter->get()->timer.setParent(nullptr);
			iter->get()->parent = nullptr;
			iter = children.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}

void Element::removeAllChild()
{
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->parent = nullptr;
			children[i]->timer.setParent(nullptr);
			children[i] = nullptr;
		}
	}
	children.resize(0);
}

PElement Element::getMySharedPtr()
{
	if (parent != nullptr)
	{
		return parent->getChildSharedPtr(this);
	}
	return PElement(nullptr);
}

PElement Element::getChildSharedPtr(Element* element)
{
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i].get() == element)
		{
			return children[i];
		}
	}
	return PElement(nullptr);
}

void Element::setChildActivated(PElement child, bool activated)
{
	if (child == nullptr)
	{
		return;
	}
	for (size_t i = 0; i < children.size(); i++)
	{
		if (child == children[i])
		{
			children[i]->activated = activated;
		}
	}
}

void Element::setChildRectReferToParent(int setLevel)
{
	for (size_t i = 0; i < children.size(); i++)
	{
		children[i]->rect.x += rect.x;
		children[i]->rect.y += rect.y;
		if (setLevel > 0)
		{
			children[i]->setChildRectReferToParent(setLevel - 1);
		}
		else if (setLevel < 0)
		{
			children[i]->setChildRectReferToParent(setLevel);
		}
	}
	onSetChildRect();
}

unsigned int Element::getResult()
{
	unsigned int r = result;
	result = erNone;
	return r;
}

bool Element::getResult(unsigned int ret)
{
	return ((getResult() & ret) > 0);
}

void Element::initAllTime()
{
	initTime();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->initAllTime();
		}
	}
}

void Element::initTime()
{
	timer.reInit();
	LastFrameTime = getTime();
}

UTime Element::getTime()
{
	return timer.get();
}

UTime Element::getFrameTime()
{
	auto t = getTime();
	auto result = t - LastFrameTime;
	LastFrameTime = t;
	return result;
}

void Element::setPaused(bool paused)
{
	timer.setPaused(paused);
}

void Element::dragEnd()
{
	if (currentDragItem.get() == this)
	{
		//onDragEnd(nullptr, 0, 0);
		dragging = TOUCH_UNTOUCHEDID;
		currentDragItem = nullptr;
	}
}

bool cmp(PElement A, PElement B)
{
	if (A == nullptr)
	{
		return true;
	}
	else if (B == nullptr)
	{
		return false;
	}
	
	return A->priority < B->priority;
}

void Element::reArrangeChildren()
{
	if (!needArrangeChild)
	{
		return;
	}	
	if (children.size() > 1)
	{
		std::sort(children.begin(), children.end(), cmp);
	}

	int index = -1;
	for (int i = 0; i < ((int)children.size()) - 1; i++)
	{
		if (children[i] == nullptr)
		{
			index = (int)i;
			break;
		}
	}
	if (index >= 0)
	{
		children.resize(index);
	}
}

bool Element::mouseInRect(int x, int y)
{
	if (rectFullScreen)
	{
		return true;
	}
	return rect.PointInRect(x, y);
	/*if (x >= rect.x && x < rect.w + rect.x && y >= rect.y && y < rect.y + rect.h)
	{
		return true;
	}*/
	return false;
}

void Element::freeAllChildren()
{
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->freeAllChildren();
			children[i] = nullptr;
		}
	}
	removeAllChild();
}

void Element::clearTouch()
{
	if (touchingID != TOUCH_UNTOUCHEDID)
	{
		if (activated && needEvents)
		{
			onMouseMoveOut();
		}
		touchingID = TOUCH_UNTOUCHEDID;
		touchingDownID = TOUCH_UNTOUCHEDID;
	}
}
void Element::clearAllTouch()
{
    for (size_t i = 0; i < children.size(); ++i)
	{
		if (children[i] != nullptr)
		{
			children[i]->clearAllTouch();
		}
	}
	if (coverMouse)
	{
		clearTouch();
	}
}

void Element::runningElementClearAllTouch()
{
	if (parent == nullptr || (runningElement.size() > 0 && runningElement[runningElement.size() - 1].get() == this))
	{
		clearAllTouch();
	}
	else
	{
		parent->runningElementClearAllTouch();
	}
}

void Element::drawSelf()
{
	//先画自身，再画child，先画优先级低的child，后画优先级高的child
	if (visible && canDraw)
	{	
		onDraw();

		reArrangeChildren();
		for (int i = ((int)children.size()) - 1; i >= 0; i--)
		{
			if (children[i] != nullptr)
			{
				children[i]->drawSelf();
			}
		}
		onDrawEnd();
	}
}

void Element::drawAll()
{
	if (parent == nullptr || drawFullScreen)
	{
		drawSelf();
		if (dragging != TOUCH_UNTOUCHEDID && currentDragItem != nullptr)
		{
			currentDragItem->onDrawDrag(dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
		}
	}
	else
	{
		parent->drawAll();
	}
}

void Element::update()
{
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->update();
		}
	}
	
	onUpdate();
}

void Element::updateAll()
{
	if (parent == nullptr)
	{
		update();
	}
	else
	{
		parent->updateAll();
	}
}

void Element::preTreatment()
{
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->preTreatment();
		}
	}

	onPreTreatment();
}

void Element::preTreatmentAll()
{
	if (parent == nullptr)
	{
		preTreatment();
	}
	else
	{
		parent->preTreatmentAll();
	}
}

void Element::postTreatment()
{
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->postTreatment();
		}
	}

	onPostTreatment();
}

void Element::postTreatmentAll()
{
	if (parent == nullptr)
	{
		postTreatment();
	}
	else
	{
		parent->postTreatmentAll();
	}
}

bool Element::handleEvent(AEvent & e)
{
	bool handled = false;
	if (activated && needEvents)
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			if (children[i]->handleEvent(e))
			{
				handled = true;
				break;
			}
		}

		if (!handled)
		{
			handled = onHandleEvent(e) || eventOccupied;
		}
	}
	return handled;
}

void Element::handleEvents()
{
	if (activated && needEvents)
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			children[i]->handleEvents();
		}
		if (currentDragItem.get() == this)
		{
			onDragging(dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
		}
		onEvent();
	}
}

bool Element::checkAllTouchDown(EventTouchID id, int x, int y)
{
	bool TouchChecked = false;
	if (activated && needEvents && visible)
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			if (children[i]->checkAllTouchDown(id, x, y))
			{
				TouchChecked = true;
				break;
			}
		}
		if (!TouchChecked && coverMouse)
		{
			TouchChecked = checkTouchDown(id, x, y);
		}
	}
	else
	{
		clearAllTouch();
	}
	return TouchChecked;
}

bool Element::checkAllTouchUp(EventTouchID id, int x, int y)
{
	bool TouchChecked = false;
	if (activated && needEvents && visible)
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			if (children[i]->checkAllTouchUp(id, x, y))
			{
				TouchChecked = true;
				break;
			}
		}
		if (!TouchChecked && coverMouse)
		{
			TouchChecked = checkTouchUp(id, x, y);
		}
	}
	else
	{
		clearAllTouch();
	}
	return TouchChecked;
}

bool Element::checkAllTouchMotion(EventTouchID id, int x, int y, bool touchChecked)
{
	bool _touchChecked = touchChecked;
	if (activated && needEvents && visible)
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			if (children[i]->checkAllTouchMotion(id, x, y, _touchChecked))
			{
				_touchChecked = true;
			}
		}
		if (coverMouse)
		{
			_touchChecked = (checkTouchMotion(id, x, y, _touchChecked) || _touchChecked);
		}
	}
	else
	{
		clearAllTouch();
	}
	return _touchChecked;
}

bool Element::checkTouchMotion(EventTouchID id, int x, int y, bool touchChecked)
{
	if (mouseInRect(x, y) && !touchChecked)
	{
		if (touchingID != id)
		{
			touchingDownID = TOUCH_UNTOUCHEDID;
			touchingID = id;
			onMouseMoveIn(x, y);
			onMouseMoving(x, y);
			return true;
		}
		else
		{
			onMouseMoving(x, y);
			if (dragging == TOUCH_UNTOUCHEDID && touchingDownID == id && canDrag && hypot(std::abs(x - mouseLDownX), std::abs(y - mouseLDownY)) >= dragRange )
			{
				if (currentDragItem.get() != this || dragging != id)
				{
					currentDragItem = getMySharedPtr();
					dragging = id;
					dragDownPosition.x = mouseLDownX - rect.x;
					dragDownPosition.y = mouseLDownY - rect.y;
					dragTouchPosition.x = mouseLDownX;
					dragTouchPosition.y = mouseLDownY;
					onDragBegin(&dragParam[0], &dragParam[1]);
					onDragging(dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
				}
			}
			return true;
		}
	}
	else
	{
		if (touchingID == id)
		{
            touchingID = TOUCH_UNTOUCHEDID;
            touchingDownID = TOUCH_UNTOUCHEDID;
			onMouseMoveOut();
			if (touchingDownID == id && canDrag && dragging == TOUCH_UNTOUCHEDID)
			{
				if (currentDragItem.get() != this || dragging != id)
				{
					currentDragItem = getMySharedPtr();
					dragging = id;
					dragDownPosition.x = mouseLDownX - rect.x;
					dragDownPosition.y = mouseLDownY - rect.y;
					dragTouchPosition.x = mouseLDownX;
					dragTouchPosition.y = mouseLDownY;
					onDragBegin(&dragParam[0], &dragParam[1]);
					onDragging(dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
				}
			}
		}
	}
	return false;
}

bool Element::checkTouchDown(EventTouchID id, int x, int y)
{
	if (touchingID == id)
	{
		if (touchingDownID != id)
		{
			touchingDownID = id;
			mouseLDownX = x;
			mouseLDownY = y;
			onMouseLeftDown(x, y);
			return true;
		}
	}
	return false;
}

bool Element::checkTouchUp(EventTouchID id, int x, int y)
{
	if (touchingID == id)
	{
		onMouseLeftUp(x, y);
		if (touchingDownID == id)
		{
			onClick();
		}
		touchingDownID = TOUCH_UNTOUCHEDID;
#ifdef __MOBILE__
        touchingID = TOUCH_UNTOUCHEDID;
        onMouseMoveOut();
#endif
		if (dragging == id)
		{
			if (currentDragItem != nullptr)
			{
				currentDragItem->onDragEnd(getMySharedPtr(), dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
			}
			if (canDrop && dragging == id)
			{
				onDrop(currentDragItem, dragParam[0], dragParam[1]);
			}
			currentDragItem = nullptr;
			dragging = TOUCH_UNTOUCHEDID;
		}
		return true;
	}
	
	return false;
}

void Element::allHandleEvents()
{
	AEvent e;
	while (engine->getEvent(e) > 0)
	{
		if (e.eventType == ET_MOUSEMOTION || e.eventType == ET_FINGERMOTION)
		{		
			if (e.eventType == ET_MOUSEMOTION)
			{
				e.eventData = TOUCH_MOUSEID;
			}
			checkAllTouchMotion(e.eventData, e.eventX, e.eventY, false);
			if (dragging != TOUCH_UNTOUCHEDID && dragging == e.eventData && currentDragItem != nullptr)
			{
				dragTouchPosition.x = e.eventX;
				dragTouchPosition.y = e.eventY;
			}
		}
		else if ((e.eventType == ET_MOUSEDOWN && e.eventData == MBC_MOUSE_LEFT) || e.eventType == ET_FINGERDOWN)
		{
			if (e.eventType == ET_MOUSEDOWN)
			{
				checkAllTouchDown(TOUCH_MOUSEID, e.eventX, e.eventY);
			}
			else
			{
				checkAllTouchDown(e.eventData, e.eventX, e.eventY);
			}
		}
		else if ((e.eventType == ET_MOUSEUP && e.eventData == MBC_MOUSE_LEFT) || e.eventType == ET_FINGERUP)
		{
			auto tempTouchID = e.eventData;
			if (e.eventType == ET_MOUSEUP)
			{
				tempTouchID = TOUCH_MOUSEID;
			}
			checkAllTouchUp(tempTouchID, e.eventX, e.eventY);
			if (dragging == tempTouchID)
			{
				if (currentDragItem != nullptr)
				{
					currentDragItem->onDragEnd(PElement(nullptr), dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragTouchPosition.y);
				}
				dragging = TOUCH_UNTOUCHEDID;
				currentDragItem = nullptr;
			}
		}
		handleEvent(e);
	}
	handleEvents();
}

void Element::frame()
{
	nextFrame = false;

	engine->frameBegin();

	preTreatmentAll();

    allHandleEvents();

	updateAll();

	if (!running)
	{
		return;
	}

	if (!nextFrame)
	{
		drawAll();
	}
	
	postTreatmentAll();

	if (!nextFrame)
	{
		engine->frameEnd();
	}
}

bool Element::initial()
{
	if (!onInitial())
	{
		return false;
	}
	for (size_t i = 0; i < children.size(); i++)
	{	
		if (children[i] != nullptr)
		{ 
			if (!children[i]->initial())
			{
				return false;
			}
		}
	}
	return true;
}

void Element::handleRun()
{
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->handleRun();
		}		
	}
	onRun();
}

void Element::exit()
{	
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->exit();
		}		
	}
	onExit();
}

void Element::quit()
{
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->quit();
		}
	}
	running = false;
}


unsigned int Element::run()
{
	runningElementClearAllTouch();

	runningElement.push_back(getMySharedPtr());

	running = true;

	//engine->initTime(&timer);
	if (!initial())
	{
		result = erInitError;
		running = false;
	}
	if (running)
	{
		handleRun();
	}
	while (running)
	{
		frame();
	}
	exit();
	//返回时调用一次frameBegin，以免在调用的run返回时因没有再次调用frameBegin造成绘制出错
	engine->frameBegin();

	runningElement.resize(runningElement.size() - 1);

	return result;

}

unsigned int Element::stop(int ret)
{
	running = false;
	result = ret;
	return ret;
}

void Element::freeAll()
{
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != nullptr)
		{
			children[i]->freeAll();
			children[i] = nullptr;
		}		
	}
	removeAllChild();
	freeResource();
}

bool Element::isDragging()
{
	return dragging != TOUCH_UNTOUCHEDID && currentDragItem == getMySharedPtr();
}
