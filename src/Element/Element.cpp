#include "Element.h"

Element * Element::currentDragItem = nullptr;
int Element::dragParam[2] = { 0, 0 };
EventTouchID Element::dragging = TOUCH_UNTOUCHEDID;
Point Element::dragTouchPosition = { 0, 0 };
Point Element::dragDownPosition = { 0, 0 };
std::vector<Element *> Element::runningElement;

Element::Element()
{
	engine = Engine::getInstance();
	children.resize(0);
	initTime();
}

Element::~Element()
{
	freeResource();
	changeParent(nullptr);
	removeAllChild();
	if (currentDragItem == this)
	{
		currentDragItem = nullptr;
	}
}

void Element::addChild(Element * child)
{
	
	if (child != nullptr)
	{
		//不允许重复添加child，先进行查重
		int index = -1;
		for (size_t i = 0; i < children.size(); i++)
		{
			if (child == children[i])
			{
				index = (int)i;
				break;
			}
		}
		if (index < 0)
		{
			child->parent = this;
			children.push_back(child);
			child->timer.setParent(&timer);
			reArrangeChildren();
		}
	}
}

void Element::removeChild(Element * child)
{
	if (child == nullptr || children.size() == 0)
	{
		return;
	}
	while (true)
	{
		//是否有重复的child，一并删除
		int index = -1;
		for (size_t i = 0; i < children.size(); i++)
		{			
			if (children[i] == child)
			{
				children[i]->timer.setParent(nullptr);
				children[i]->parent = nullptr;
				children[i] = nullptr;
				index = (int)i;
				break;
			}
		}
		if (index < 0)
		{
			break;
		}
		else
		{
			for (int i = index; i < ((int)children.size()) - 1; i++)
			{
				children[i] = children[i + 1];
			}
			children.resize(children.size() - 1);
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

void Element::changeParent(Element * p)
{
	if (parent == p)
	{
		return;
	}
	if (parent != nullptr)
	{
		parent->removeChild(this);
	}
	parent = p;
	if (p != nullptr)
	{
		p->addChild(this);
	}
	
}

void Element::setChildActivated(Element * child, bool activated)
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
	if (currentDragItem == this)
	{
		//onDragEnd(nullptr, 0, 0);
		dragging = TOUCH_UNTOUCHEDID;
		currentDragItem = nullptr;
	}
}

bool cmp(Element* A, Element* B)
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
			delete children[i];
			children[i] = nullptr;
		}
	}
	removeAllChild();
}

void Element::clearTouch()
{
	if (touchingID != TOUCH_UNTOUCHEDID)
	{
		onMouseMoveOut();
		touchingID = TOUCH_UNTOUCHEDID;
		touchInRectID = TOUCH_UNTOUCHEDID;
	}
}
void Element::clearAllTouch()
{
	if (activated && needEvents && visible)
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
}

void Element::runningElementClearAllTouch()
{
	if (parent == nullptr || (runningElement.size() > 0 && runningElement[runningElement.size() - 1] == this))
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

bool Element::handleEvent(AEvent * e)
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
		if (currentDragItem == this)
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
		if (touchInRectID != TOUCH_UNTOUCHEDID)
		{
			touchInRectID = TOUCH_UNTOUCHEDID;
			onMouseLeftUp(x, y);
		}
		if (touchingID != TOUCH_UNTOUCHEDID)
		{
			touchingID = TOUCH_UNTOUCHEDID;
			onMouseMoveOut();
		}
	}
	return _touchChecked;
}

bool Element::checkTouchMotion(EventTouchID id, int x, int y, bool touchChecked)
{
	if (mouseInRect(x, y) && !touchChecked)
	{
		if (touchingID != id)
		{
			touchInRectID = TOUCH_UNTOUCHEDID;
			touchingID = id;
			onMouseMoveIn(x, y);
			onMouseMoving(x, y);
			return true;
		}
		else
		{
			onMouseMoving(x, y);
			if (dragging == TOUCH_UNTOUCHEDID && touchInRectID == id && canDrag && (std::abs(x - mouseLDownX) >= dragRange || std::abs(y - mouseLDownY) >= dragRange))
			{
				if (currentDragItem != this || dragging != id)
				{
					currentDragItem = this;
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
            touchInRectID = TOUCH_UNTOUCHEDID;
			onMouseMoveOut();
			if (touchInRectID == id && canDrag && dragging == TOUCH_UNTOUCHEDID)
			{
				if (currentDragItem != this || dragging != id)
				{
					currentDragItem = this;
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
		if (touchInRectID != id)
		{
			touchInRectID = id;
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
		if (touchInRectID == id)
		{
			onClick();
		}
		touchInRectID = TOUCH_UNTOUCHEDID;
#ifdef _MOBILE
        touchingID = TOUCH_UNTOUCHEDID;
        onMouseMoveOut();
#endif
		if (dragging == id)
		{
			if (currentDragItem != nullptr)
			{
				currentDragItem->onDragEnd(this, dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
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
					currentDragItem->onDragEnd(nullptr, dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragTouchPosition.y);
				}
				dragging = TOUCH_UNTOUCHEDID;
				currentDragItem = nullptr;
			}
		}
		handleEvent(&e);
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
	running = true;

	runningElement.push_back(this);

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
			delete children[i];
			children[i] = nullptr;
		}		
	}
	removeAllChild();
	freeResource();
}
