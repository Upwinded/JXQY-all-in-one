#include "Element.h"

Element * Element::dragItem = NULL;
int Element::dragParam[2] = { 0, 0 };
bool Element::dragging = false;
std::vector<Element *> Element::runningElement = {};

Element::Element()
{
	engine = Engine::getInstance();
	children.resize(0);
	initTime();
}

Element::~Element()
{
	freeResource();
}

void Element::addChild(Element * child)
{
	if (child != NULL)
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
			child->timer.parent = &timer;
			children.push_back(child);
			reArrangeChildren();
		}
	}
}

void Element::removeChild(Element * child)
{
	if (child == NULL || children.size() == 0)
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
				children[i]->parent = NULL;
				children[i]->timer.parent = NULL;
				children[i] = NULL;
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
		if (children[i] != NULL)
		{
			children[i]->parent = NULL;
			children[i]->timer.parent = NULL;
			children[i] = NULL;
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
	if (parent != NULL)
	{
		parent->removeChild(this);
	}
	if (p != NULL)
	{
		p->addChild(this);
	}
}

void Element::setChildActivated(Element * child, bool activated)
{
	if (child == NULL)
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

void Element::setChildRect(int setLevel)
{
	for (size_t i = 0; i < children.size(); i++)
	{
		children[i]->rect.x += rect.x;
		children[i]->rect.y += rect.y;
		if (setLevel > 0)
		{
			children[i]->setChildRect(setLevel - 1);
		}
		else if (setLevel < 0)
		{
			children[i]->setChildRect(setLevel);
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
		if (children[i] != NULL)
		{
			children[i]->initAllTime();
		}
	}
}

void Element::initTime()
{
	engine->initTime(&timer);
	LastTime = getTime();
}

unsigned int Element::getTime()
{
	return engine->getTime(&timer);
}

unsigned int Element::getFrameTime()
{
	unsigned int t = getTime();
	unsigned int result = t - LastTime;
	LastTime = t;
	return result;
}

void Element::setPaused(bool paused)
{
	engine->setTimePaused(&timer, paused);
}

void Element::reArrangeChildren()
{
	if (!needArrangeChild)
	{
		return;
	}
	//按优先级重排child，并清除空的child
	for (int i = 0; i < ((int)children.size()) - 1; i++)
	{
		for (int j = 0; j < ((int)children.size()) - 1 - i; j++)
		{
			if (children[j] == NULL)
			{
				children[j] = children[j + 1];
			}
			else if (children[j + 1] != NULL && children[j]->priority > children[j + 1]->priority)
			{
				Element * element = children[j];
				children[j] = children[j + 1];
				children[j + 1] = element;
			}
		}
	}
	int index = -1;
	for (int i = 0; i < ((int)children.size()) - 1; i++)
	{
		if (children[i] == NULL)
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

bool Element::mouseInRect()
{
	if (rectFullScreen)
	{
		return true;
	}
	int x, y;
	engine->getMouse(&x, &y);
	if (x >= rect.x && x < rect.w + rect.x && y >= rect.y && y < rect.y + rect.h)
	{
		return true;
	}
	return false;
}

void Element::handleEvents(bool vis, bool * canCoverMouse)
{
	/*
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
	if (children[i] != NULL)
	{
	children[i]->handleEvents();
	}
	}
	*/
	if (needEvents)
	{
		if (visible && vis && dragging && dragItem == this)
		{
			onDragging();
		}

		//预先处理鼠标进入、移出，鼠标左键按下、抬起等事件
		if (*canCoverMouse && visible && coverMouse && vis)
		{
			if (mouseInRect())
			{
				if (!mouseMoveIn)
				{
					mouseMoveIn = true;
					if (!onlyCheckRect)
					{
						mouseLDownInRect = false;
						if (engine->getMousePress(MOUSE_LEFT))
						{
							mouseLeftPressed = true;
						}
						else
						{
							mouseLeftPressed = false;
						}
						onMouseMoveIn();
					}
				}
				else
				{
					if (!onlyCheckRect)
					{
						if (!mouseLDownInRect && mouseLeftPressed && !engine->getMousePress(MOUSE_LEFT))
						{
							mouseLDownInRect = false;
							mouseLeftPressed = false;
							onMouseLeftUp();
							if (dragging)
							{
								if (dragItem != NULL)
								{
									dragItem->onDragEnd(this);
								}
								if (canDrop && dragging)
								{
									onDrop(dragItem, dragParam[0], dragParam[1]);
								}
								dragging = false;
							}
						}
						else if (engine->getMousePress(MOUSE_LEFT) != mouseLeftPressed)
						{
							mouseLeftPressed = engine->getMousePress(MOUSE_LEFT);
							if (mouseLDownInRect && !mouseLeftPressed)
							{
								mouseLDownInRect = false;
								onMouseLeftUp();
								if (!dragging)
								{
									onClick();
								}
								if (dragging)
								{
									if (dragItem != NULL)
									{
										dragItem->onDragEnd(this);
									}
									if (canDrop && dragging)
									{
										onDrop(dragItem, dragParam[0], dragParam[1]);
									}
									dragging = false;
								}
							}
							else if (!mouseLDownInRect && mouseLeftPressed)
							{
								mouseLDownInRect = true;
								mouseLDownTime = getTime();
								engine->getMouse(&mouseLDownX, &mouseLDownY);
								onMouseLeftDown();
							}
						}
						else if (!dragging && mouseLDownInRect)
						{
							int x, y;
							engine->getMouse(&x, &y);
							if (canDrag && (std::abs(x - mouseLDownX) >= dragRange || std::abs(y - mouseLDownY) >= dragRange))
							{
								dragItem = this;
								dragging = true;
								dragX = mouseLDownX - rect.x;
								dragY = mouseLDownY - rect.y;
								onDrag(&dragParam[0], &dragParam[1]);
							}
						}
					}
				}
				*canCoverMouse = false;
			}
			else
			{
				if (mouseMoveIn)
				{
					mouseMoveIn = false;
					if (!onlyCheckRect)
					{
						onMouseMoveOut();
						if (mouseLDownInRect && engine->getMousePress(MOUSE_LEFT) && canDrag)
						{
							dragItem = this;
							dragging = true;
							dragX = mouseLDownX - rect.x;
							dragY = mouseLDownY - rect.y;
							onDrag(&dragParam[0], &dragParam[1]);
						}
					}
				}
				mouseLDownInRect = false;
			}
		}
		else
		{
			mouseLDownInRect = false;
			if (mouseMoveIn)
			{
				mouseMoveIn = false;
				if (!onlyCheckRect)
				{
					onMouseMoveOut();
				}
			}
		}

		onEvent();
		if (!onlyCheckRect)
		{
			int eventCount = engine->getEventCount();
			AEvent e;
			for (int i = 0; i < eventCount; i++)
			{
				if (!activated)
				{
					break;
				}

				int ret = engine->getEvent(&e);
				if (ret > 0)
				{
					if (!onHandleEvent(&e) && !eventOccupied)
					{
						//处理事件后，如果没处理则将事件押回队列，供其它元素处理
						engine->pushEvent(&e);
					}

				}
			}
		}
	}
		

	if (!visible || !needEvents || !activated)
	{
		mouseLDownInRect = false;
		if (mouseMoveIn)
		{
			mouseMoveIn = false;
			onMouseMoveOut();
		}
		if (dragging && dragItem == this)
		{
			dragging = false;
			onDragEnd(NULL);
		}
	}
	
}

void Element::checkDrag()
{
	if (dragging)
	{
		if (!engine->getMousePress(MOUSE_LEFT))
		{
			dragging = false;
			if (dragItem != NULL)
			{
				dragItem->onDragEnd(NULL);
			}
		}
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
			if (children[i] != NULL)
			{
				children[i]->drawSelf();
			}
		}	
	}
}

void Element::drawAll()
{
	if (parent == NULL || drawFullScreen)
	{
		drawSelf();
		if (dragging && dragItem != NULL)
		{
			dragItem->onDrawDrag();
		}
	}
	else
	{
		parent->drawAll();
	}
}

void Element::update(bool r, bool events, bool vis, bool * canCoverMouse)
{
	if (!r)
	{
		if (runningElement.size() > 0 && runningElement[runningElement.size() - 1] == this)
		{
			r = true;
		}
	}
	bool e = events && activated;
	reArrangeChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		if (children[i] != NULL)
		{
			children[i]->update(r, e, vis && visible, canCoverMouse);
		}
	}
	
	onUpdate();
	if (e && r)
	{
		handleEvents(vis && visible, canCoverMouse);
	}

	//LastTime = getTime();
}

void Element::updateAll(bool * canCoverMouse)
{
	if (parent == NULL)
	{
		update(false, true, visible, canCoverMouse);
		checkDrag();
	}
	else
	{
		parent->updateAll(canCoverMouse);
	}
}

void Element::frame()
{
	nextFrame = false;

	engine->frameBegin();

	bool canCoverMouse = true;
	updateAll(&canCoverMouse);

	if (!running)
	{
		return;
	}

	if (!nextFrame)
	{
		drawAll();
	}
	
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
		if (children[i] != NULL)
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
		if (children[i] != NULL)
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
		if (children[i] != NULL)
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
		if (children[i] != NULL)
		{
			children[i]->quit();
		}
	}
	running = false;
}

unsigned int Element::run()
{
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
		if (children[i] != NULL)
		{
			children[i]->freeAll();
			delete children[i];
			children[i] = NULL;
		}		
	}
	removeAllChild();
	freeResource();
}
