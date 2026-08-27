#include "Element.h"
#include "ElementPointerClickPolicy.h"
#include "../Engine/Engine.h"
#include <set>
#include <algorithm>
#include <iterator>
#include <utility>

namespace
{
thread_local unsigned int activeFrameCallbackDepth = 0;
thread_local unsigned int activeApplicationResizeDepth = 0;

class NoThrowScopeExit final
{
public:
	explicit NoThrowScopeExit(std::function<void()> callback) :
		callback(std::move(callback))
	{
	}

	~NoThrowScopeExit() noexcept
	{
		if (!callback)
		{
			return;
		}
		try
		{
			callback();
		}
		catch (...)
		{
		}
	}

	void dismiss() noexcept
	{
		callback = {};
	}

private:
	std::function<void()> callback;
};
}

std::list<Element*> Element::memList;

PElement Element::currentDragItem = nullptr;
int Element::dragParam[2] = { 0, 0 };
EventTouchID Element::dragging = TOUCH_UNTOUCHEDID;
Point Element::dragTouchPosition = { 0, 0 };
Point Element::dragDownPosition = { 0, 0 };
std::vector<PElement> Element::runningElement;
std::atomic<bool> Element::applicationQuitRequested(false);
Element::WindowCloseConfirmationHandler Element::windowCloseConfirmationHandler;
Element::FrameGlobalInputHandler Element::frameGlobalInputHandler;
Element::FrameInputEventHandler Element::frameInputEventHandler;
Element::FrameSemanticInputHandler Element::frameSemanticInputHandler;
Element::FrameGameplayInputHandler Element::frameGameplayInputHandler;
Element::InputContextTransitionHandler Element::inputContextTransitionHandler;
bool Element::frameSemanticInputBlocked = false;
bool Element::rawPointerInputBlocked = false;
bool Element::inputContextStarted = false;
bool Element::applicationTimersPaused = false;
std::vector<std::weak_ptr<Element>> Element::applicationPausedElements;

int Element::update_deep = 0;

Element::Element()
{
	engine = Engine::getInstance();
	initTime();
#ifdef DEBUG
	memList.push_back(this);
	ShowMemList();
#endif // DEBUG
}

Element::~Element()
{
	freeResource();
	removeAllChild();
	if (currentDragItem.get() == this)
	{
		currentDragItem = nullptr;
	}
#ifdef DEBUG
	memList.remove(this);
	ShowMemList();
#endif // DEBUG
}

void Element::ShowMemList()
{
	GameLog::write("当前对象列表：");
	for (auto iter = memList.begin(); iter != memList.end(); iter++)
	{
		GameLog::write("[%s] : [%s]", typeid(**iter).name(), (*iter)->name.c_str());
	}
	GameLog::write("当前对象总计：%d", memList.size());
}

bool Element::resizeRunningRoots(int width, int height)
{
	if (applicationQuitRequested.load() ||
		Engine::getInstance()->isApplicationQuitRequested())
	{
		requestApplicationQuit();
		return true;
	}

	std::vector<Element*> resizedRoots;
	for (auto& running : runningElement)
	{
		Element* resizeRoot = running.get();
		if (resizeRoot == nullptr)
		{
			continue;
		}
		while (resizeRoot->parent != nullptr)
		{
			resizeRoot = resizeRoot->parent;
		}
		if (std::find(resizedRoots.begin(), resizedRoots.end(), resizeRoot) != resizedRoots.end())
		{
			continue;
		}
		resizeRoot->resizeAll(width, height);
		resizedRoots.push_back(resizeRoot);
		if (resizeRoot->stopForApplicationQuit())
		{
			return true;
		}
	}
	return !resizedRoots.empty();
}

void Element::dispatchFrameGlobalInput(Engine* engine)
{
	if (frameGlobalInputHandler)
	{
		frameGlobalInputHandler(engine);
	}
}

bool Element::dispatchUIAction(UIAction action)
{
	if (runningElement.empty() || runningElement.back() == nullptr)
	{
		return false;
	}

	Element* actionOwner = runningElement.back().get();
	const bool handled = actionOwner->onHandleUIAction(action);
	// A nested run() is a modal boundary. Until that modal explicitly supports
	// semantic input, block the action instead of activating its parent. Root
	// scenes may themselves be nested (Title runs MainScene), so stack depth
	// alone cannot distinguish a modal from a scene transition.
	return handled || actionOwner->parent != nullptr;
}

bool Element::isCurrentRunOwner(const Element* element)
{
	return element != nullptr && !runningElement.empty()
		&& runningElement.back().get() == element;
}

bool Element::currentRunOwnerBlocksParentInput()
{
	return !runningElement.empty() && runningElement.back() != nullptr
		&& runningElement.back()->parent != nullptr;
}

void Element::resetApplicationQuitState()
{
	setRunningElementsPaused(false);
	applicationQuitRequested.store(false);
	inputContextStarted = false;
	rawPointerInputBlocked = false;
	Engine::getInstance()->resetApplicationQuitRequest();
}

void Element::setWindowCloseConfirmationHandler(
	WindowCloseConfirmationHandler handler)
{
	windowCloseConfirmationHandler = std::move(handler);
}

void Element::setFrameGlobalInputHandler(FrameGlobalInputHandler handler)
{
	frameGlobalInputHandler = std::move(handler);
}

void Element::setFrameInputEventHandler(FrameInputEventHandler handler)
{
	frameInputEventHandler = std::move(handler);
}

void Element::setFrameSemanticInputHandler(FrameSemanticInputHandler handler)
{
	frameSemanticInputHandler = std::move(handler);
}

void Element::setFrameGameplayInputHandler(FrameGameplayInputHandler handler)
{
	frameGameplayInputHandler = std::move(handler);
}

void Element::setRawPointerInputBlocked(bool blocked)
{
	if (rawPointerInputBlocked == blocked)
	{
		return;
	}
	// A nested run() owner is not necessarily part of the GameManager tree
	// (for example, script-driven video playback). Clear every active owner so
	// a press that began before the gate cannot commit after it is released.
	for (const PElement& running : runningElement)
	{
		if (running != nullptr)
		{
			running->cancelPointerInteraction();
		}
	}
	rawPointerInputBlocked = blocked;
}

bool Element::isRawPointerInputBlocked()
{
	return rawPointerInputBlocked;
}

void Element::setInputContextTransitionHandler(InputContextTransitionHandler handler)
{
	inputContextTransitionHandler = std::move(handler);
}

bool Element::isFrameSemanticInputBlocked()
{
	return frameSemanticInputBlocked;
}

void Element::requestApplicationQuit()
{
	applicationQuitRequested.store(true);
	Engine* applicationEngine = Engine::getInstance();
	if (applicationEngine != nullptr)
	{
		// A confirmed scene-level close is terminal too. Keep the engine and
		// Element latches in sync so Lua/native callers cannot continue work
		// after every running root has been stopped.
		applicationEngine->requestApplicationQuit();
	}
	for (auto& running : runningElement)
	{
		if (running != nullptr)
		{
			running->result |= erExit;
			running->logicRunning = false;
		}
	}
}

void Element::setRunningElementsPaused(bool paused)
{
	if (!paused)
	{
		if (!applicationTimersPaused)
		{
			return;
		}
		for (auto& pausedElement : applicationPausedElements)
		{
			auto element = pausedElement.lock();
			if (element != nullptr)
			{
				element->setPaused(false);
			}
		}
		applicationPausedElements.clear();
		applicationTimersPaused = false;
		return;
	}

	for (auto& running : runningElement)
	{
		if (running != nullptr && !running->isPaused())
		{
			running->setPaused(true);
			applicationPausedElements.push_back(running);
		}
	}
	applicationTimersPaused = true;
}

void Element::setPriority(unsigned char value)
{
	if (priority == value)
	{
		return;
	}
	
	priority = value;
	
	if (parent != nullptr)
	{
		parent->childrenNeedRearrange = true;
	}
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
		auto it = std::find_if(children.begin(), children.end(), [&child](const PElement& c) {
			return child.get() == c.get();
		});
		if (it != children.end())
		{
			return;
		}
		child->parent = this;
		children.push_back(child);
		child->timer.setParent(&timer);
		if (child->pointerEventPreviewObserverCount > 0)
		{
			adjustPointerEventPreviewObserverCount(
				static_cast<int>(child->pointerEventPreviewObserverCount));
		}
		childrenNeedRearrange = true;
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
			if (iter->get()->pointerEventPreviewObserverCount > 0)
			{
				adjustPointerEventPreviewObserverCount(
					-static_cast<int>(
						iter->get()->pointerEventPreviewObserverCount));
			}
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
	unsigned int removedPreviewObservers = 0;
	for (auto& child : children)
	{
		removedPreviewObservers += child->pointerEventPreviewObserverCount;
		child->parent = nullptr;
		child->timer.setParent(nullptr);
	}
	children.clear();
	if (removedPreviewObservers > 0)
	{
		adjustPointerEventPreviewObserverCount(
			-static_cast<int>(removedPreviewObservers));
	}
}

PElement Element::getMySharedPtr()
{
	return shared_from_this();
}

void Element::setChildActivated(PElement child, bool activated)
{
	if (child == nullptr)
	{
		return;
	}
	for (auto& c : children)
	{
		if (child == c)
		{
			c->activated = activated;
		}
	}
}

void Element::setChildRectReferToParent(int setLevel)
{
	float scaleX = 1.0f, scaleY = 1.0f;
	getChildScaleFactor(scaleX, scaleY);
	int layoutOffsetX = 0, layoutOffsetY = 0;
	getChildLayoutOffset(layoutOffsetX, layoutOffsetY);

	for (auto& child : children)
	{
		if (scaleX != 1.0f || scaleY != 1.0f)
		{
			child->rect.x = (int)(child->rect.x * scaleX);
			child->rect.y = (int)(child->rect.y * scaleY);
			child->rect.w = (int)(child->rect.w * scaleX);
			child->rect.h = (int)(child->rect.h * scaleY);
		}
		child->rect.x += rect.x + layoutOffsetX;
		child->rect.y += rect.y + layoutOffsetY;
		child->onSetChildRect();
		if (setLevel > 0)
		{
			child->setChildRectReferToParent(setLevel - 1);
		}
		else if (setLevel < 0)
		{
			child->setChildRectReferToParent(setLevel);
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

void Element::updateFrameTime()
{
	auto lastTime = unifiedTime;
	unifiedTime = getTimerTime();
	if (unifiedTime - lastTime > MAX_FRAME_TIME)
	{
		unifiedTime = lastTime + MAX_FRAME_TIME;
		timer.set(unifiedTime);
	}
	frameTime = unifiedTime - lastTime;
}

void Element::initAllTime()
{
	initTime();
	for (auto& child : children)
	{
		child->initAllTime();
	}
}

void Element::initTime()
{
	timer.reInit();
	unifiedTime = getTimerTime();
}

void Element::setTime(UTime t)
{
	timer.set(timer.get() + t - unifiedTime);
	unifiedTime = t;
}

UTime Element::getTimerTime()
{
	return timer.get();
}

UTime Element::getTime() const
{
    return unifiedTime;
}

UTime Element::getFrameTime()
{
	return frameTime;
}

void Element::setPaused(bool paused)
{
	timer.setPaused(paused);
}

bool Element::isPaused()
{
	return timer.getPaused();
}

void Element::dragEnd()
{
	if (currentDragItem.get() == this)
	{
		dragging = TOUCH_UNTOUCHEDID;
		currentDragItem = nullptr;
	}
}

void Element::cancelPointerInteraction()
{
	bool ownsCurrentDrag = dragging != TOUCH_UNTOUCHEDID
		&& currentDragItem == nullptr;
	if (dragging != TOUCH_UNTOUCHEDID && currentDragItem != nullptr)
	{
		Element* dragTreeNode = currentDragItem.get();
		while (dragTreeNode != nullptr && dragTreeNode != this)
		{
			dragTreeNode = dragTreeNode->parent;
		}
		if (dragTreeNode == this)
		{
			ownsCurrentDrag = true;
		}
	}
	cancelAllPointerInteractionsTree();
	if (ownsCurrentDrag)
	{
		// Cancellation must not call onDragEnd(): skill and jump drag
		// handlers treat that callback as a committed action.
		currentDragItem = nullptr;
		dragging = TOUCH_UNTOUCHEDID;
	}
}

void Element::cancelPointerInteraction(EventTouchID pointerID)
{
	if (pointerID == TOUCH_UNTOUCHEDID)
	{
		return;
	}
	cancelPointerInteractionTree(pointerID);
	if (dragging == pointerID)
	{
		// A finger cancellation invalidates the global drag transaction for that
		// exact contact, even if the drag owner has already left this subtree.
		currentDragItem = nullptr;
		dragging = TOUCH_UNTOUCHEDID;
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
	
	return A->getPriority() < B->getPriority();
}

void Element::reArrangeChildren()
{
	if (!needArrangeChild || !childrenNeedRearrange)
	{
		return;
	}	
	if (children.size() > 1)
	{
		std::sort(children.begin(), children.end(), cmp);
	}
	childrenNeedRearrange = false;
}

bool Element::mouseInRect(int x, int y)
{
	if (rectFullScreen)
	{
		return true;
	}
	return ElementPointerClickPolicy::isPointInsideHalfOpenBounds(
		x, y, rect.x, rect.y, rect.w, rect.h);
}

bool Element::shouldKeepTouchWhenPointerLeaves(int x, int y)
{
	return false;
}

void Element::freeAllChildren()
{
	for (auto& child : children)
	{
		child->freeAllChildren();
	}
	removeAllChild();
}

void Element::offsetRectTree(int dx, int dy)
{
	rect.x += dx;
	rect.y += dy;
	for (auto& child : children)
	{
		child->offsetRectTree(dx, dy);
	}
}

void Element::clearTouch()
{
	if (touchingID != TOUCH_UNTOUCHEDID)
	{
		if (activated && needEvents)
		{
			onMouseMoveOut();
		}
		ElementPointerClickPolicy::cancelPointerState(
			touchingID,
			touchingDownID,
			TOUCH_UNTOUCHEDID);
	}
}
void Element::clearAllTouch()
{
	if (stopForApplicationQuit())
	{
		return;
	}

	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		child->clearAllTouch();
		if (stopForApplicationQuit())
		{
			return;
		}
	}
	if (coverMouse)
	{
		clearTouch();
	}
}

void Element::clearAllResults()
{
	for (auto& child : children)
	{
		child->clearAllResults();
	}
	ElementPointerClickPolicy::cancelPendingResult(result, static_cast<unsigned int>(erNone));
}

namespace
{
constexpr unsigned int PointerResultMask =
	static_cast<unsigned int>(erClick)
	| static_cast<unsigned int>(erMouseLDown)
	| static_cast<unsigned int>(erMouseLUp)
	| static_cast<unsigned int>(erMouseRDown)
	| static_cast<unsigned int>(erMouseRUp)
	| static_cast<unsigned int>(erDragEnd)
	| static_cast<unsigned int>(erDragging)
	| static_cast<unsigned int>(erDropped)
	| static_cast<unsigned int>(erScrollbarSlided)
	| static_cast<unsigned int>(erShowHint)
	| static_cast<unsigned int>(erHideHint);

bool isRawPointerEvent(EventType eventType)
{
	switch (eventType)
	{
	case ET_MOUSEMOTION:
	case ET_MOUSEDOWN:
	case ET_MOUSEUP:
	case ET_MOUSEWHEEL:
	case ET_FINGERDOWN:
	case ET_FINGERUP:
	case ET_FINGERMOTION:
	case ET_FINGERCANCEL:
		return true;
	default:
		return false;
	}
}
}

void Element::cancelPointerInteractionTree(EventTouchID pointerID)
{
	for (auto& child : children)
	{
		child->cancelPointerInteractionTree(pointerID);
	}

	const bool ownsStandardPointer = touchingID == pointerID
		|| touchingDownID == pointerID
		|| (dragging == pointerID && currentDragItem.get() == this);
	const bool ownsDerivedPointer = onPointerInteractionCanceled(pointerID);
	if (!ownsStandardPointer && !ownsDerivedPointer)
	{
		return;
	}

	if (touchingID == pointerID)
	{
		if (activated && needEvents)
		{
			onMouseMoveOut();
		}
		touchingID = TOUCH_UNTOUCHEDID;
	}
	if (touchingDownID == pointerID)
	{
		touchingDownID = TOUCH_UNTOUCHEDID;
	}
	result &= ~PointerResultMask;
}

void Element::cancelAllPointerInteractionsTree()
{
	for (auto& child : children)
	{
		child->cancelAllPointerInteractionsTree();
	}
	onAllPointerInteractionsCanceled();
	if (touchingID != TOUCH_UNTOUCHEDID && activated && needEvents)
	{
		onMouseMoveOut();
	}
	touchingID = TOUCH_UNTOUCHEDID;
	touchingDownID = TOUCH_UNTOUCHEDID;
	result &= ~PointerResultMask;
}

void Element::runningElementClearAllTouch()
{
	if (parent == nullptr || (runningElement.size() > 0 && runningElement[runningElement.size() - 1].get() == this))
	{
		cancelPointerInteraction();
	}
	else
	{
		parent->runningElementClearAllTouch();
	}
}

void Element::drawSelf()
{
	if (stopForApplicationQuit())
	{
		return;
	}
	//先画自身，再画child，先画优先级低的child，后画优先级高的child
	if (visible && canDraw)
	{
		const bool compositionStarted = onBeginDrawComposition();
		NoThrowScopeExit compositionCleanup([&]()
		{
			if (compositionStarted)
			{
				onEndDrawComposition(false);
			}
		});

		onDraw();
		if (stopForApplicationQuit())
		{
			return;
		}

		reArrangeChildren();
		std::vector<PElement> childrenAfterComposition;
		for (int i = ((int)children.size()) - 1; i >= 0; i--)
		{
			if (compositionStarted &&
				shouldDrawChildAfterComposition(children[i]))
			{
				childrenAfterComposition.push_back(children[i]);
				continue;
			}
			children[i]->drawSelf();
			if (stopForApplicationQuit())
			{
				return;
			}
		}
		if (compositionStarted)
		{
			onEndDrawComposition(true);
			compositionCleanup.dismiss();
			if (stopForApplicationQuit())
			{
				return;
			}
			for (const PElement& child : childrenAfterComposition)
			{
				child->drawSelf();
				if (stopForApplicationQuit())
				{
					return;
				}
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
		if (stopForApplicationQuit())
		{
			return;
		}
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
	if (stopForApplicationQuit())
	{
		return;
	}
	//std::string update_deep_str = "";
	//for (int i = 0; i < update_deep; i++)
	//{
	//	update_deep_str += " ";
	//}
	//update_deep_str += std::to_string(update_deep);
	//GameLog::write("%s I'm %s, update running 1!", update_deep_str.c_str(), name.c_str());
	reArrangeChildren();
	update_deep++;
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		if (shouldUpdateChild(child))
		{
			child->update();
			if (stopForApplicationQuit())
			{
				update_deep--;
				return;
			}
		}
	}
	//GameLog::write("%s I'm %s, update running 2!", update_deep_str.c_str(), name.c_str());
	update_deep--;
	
	if (!stopForApplicationQuit())
	{
		onUpdate();
	}

	//GameLog::write("%s I'm %s, update running 3!", update_deep_str.c_str(), name.c_str());
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
	if (stopForApplicationQuit())
	{
		return;
	}
	reArrangeChildren();
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		if (shouldUpdateChild(child))
		{
			child->preTreatment();
			if (stopForApplicationQuit())
			{
				return;
			}
		}
	}
	updateFrameTime();
	if (!stopForApplicationQuit())
	{
		onPreTreatment();
	}
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

void Element::resizeAll(int width, int height)
{
	if (stopForApplicationQuit())
	{
		return;
	}

	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		child->resizeAll(width, height);
		if (stopForApplicationQuit())
		{
			return;
		}
	}
	clearAllTouch();
	if (stopForApplicationQuit())
	{
		return;
	}
	onWindowResize(width, height);
}

void Element::postTreatment()
{
	if (stopForApplicationQuit())
	{
		return;
	}
	reArrangeChildren();
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		child->postTreatment();
		if (stopForApplicationQuit())
		{
			return;
		}
	}

	if (!stopForApplicationQuit())
	{
		onPostTreatment();
	}
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

void Element::previewPointerEvent(AEvent& e)
{
	if (stopForApplicationQuit() ||
		pointerEventPreviewObserverCount == 0
		|| !activated || !needEvents || !visible)
	{
		return;
	}
	if (pointerEventPreviewEnabled)
	{
		onPreviewPointerEvent(e);
		if (stopForApplicationQuit())
		{
			return;
		}
	}
	if (pointerEventPreviewObserverCount
		== (pointerEventPreviewEnabled ? 1U : 0U))
	{
		return;
	}
	for (const PElement& child : children)
	{
		if (child != nullptr
			&& child->pointerEventPreviewObserverCount > 0)
		{
			child->previewPointerEvent(e);
			if (stopForApplicationQuit())
			{
				return;
			}
		}
	}
}

void Element::setPointerEventPreviewEnabled(bool enabled)
{
	if (pointerEventPreviewEnabled == enabled)
	{
		return;
	}
	pointerEventPreviewEnabled = enabled;
	adjustPointerEventPreviewObserverCount(enabled ? 1 : -1);
}

void Element::adjustPointerEventPreviewObserverCount(int delta)
{
	for (Element* element = this; element != nullptr;
		element = element->parent)
	{
		if (delta < 0)
		{
			const unsigned int decrement =
				static_cast<unsigned int>(-delta);
			if (element->pointerEventPreviewObserverCount < decrement)
			{
				element->pointerEventPreviewObserverCount = 0;
				continue;
			}
			element->pointerEventPreviewObserverCount -= decrement;
		}
		else
		{
			element->pointerEventPreviewObserverCount +=
				static_cast<unsigned int>(delta);
		}
	}
}

bool Element::handleEvent(AEvent & e)
{
	if (stopForApplicationQuit())
	{
		return true;
	}
	bool handled = false;
	if (activated && needEvents && visible)
	{
		for (auto& child : children)
		{
			if (child->handleEvent(e))
			{
				handled = true;
				break;
			}
			if (stopForApplicationQuit())
			{
				return true;
			}
		}

		if (!handled &&
			!stopForApplicationQuit())
		{
			handled = onHandleEvent(e) || eventOccupied;
		}
	}
	return stopForApplicationQuit() || handled;
}

void Element::handleEvents()
{
	if (stopForApplicationQuit())
	{
		return;
	}
	if (activated && needEvents && visible)
	{
		auto childrenCopy = children;
		for (auto& child : childrenCopy)
		{
			child->handleEvents();
			if (stopForApplicationQuit())
			{
				return;
			}
		}
		if (currentDragItem.get() == this)
		{
			onDragging(dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
			if (stopForApplicationQuit())
			{
				return;
			}
		}
		if (!stopForApplicationQuit())
		{
			onEvent();
		}
	}
}

bool Element::checkAllTouchDown(EventTouchID id, int x, int y)
{
	if (stopForApplicationQuit())
	{
		return true;
	}
	bool TouchChecked = false;
	if (activated && needEvents && visible)
	{
		for (auto& child : children)
		{
			if (child->checkAllTouchDown(id, x, y))
			{
				TouchChecked = true;
				break;
			}
			if (stopForApplicationQuit())
			{
				return true;
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
	return stopForApplicationQuit() || TouchChecked;
}

bool Element::hasPointerDownInTree(EventTouchID pointerID) const
{
	if (!activated || !needEvents || !visible)
	{
		return false;
	}
	for (const auto& child : children)
	{
		if (child != nullptr && child->hasPointerDownInTree(pointerID))
		{
			return true;
		}
	}
	return touchingDownID == pointerID;
}

Element* Element::findPointerHitTargetInTree(int x, int y)
{
	if (!activated || !needEvents || !visible)
	{
		return nullptr;
	}
	for (const PElement& child : children)
	{
		if (child == nullptr)
		{
			continue;
		}
		if (Element* target = child->findPointerHitTargetInTree(x, y))
		{
			return target;
		}
	}
	return coverMouse && mouseInRect(x, y) ? this : nullptr;
}

bool Element::checkAllTouchUp(EventTouchID id, int x, int y)
{
	if (stopForApplicationQuit())
	{
		return true;
	}
	bool TouchChecked = false;
	if (activated && needEvents && visible)
	{
		for (auto& child : children)
		{
			if (child->checkAllTouchUp(id, x, y))
			{
				TouchChecked = true;
				break;
			}
			if (stopForApplicationQuit())
			{
				return true;
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
	return stopForApplicationQuit() || TouchChecked;
}

bool Element::checkAllTouchMotion(EventTouchID id, int x, int y, bool touchChecked)
{
	if (stopForApplicationQuit())
	{
		return true;
	}
	bool _touchChecked = touchChecked;
	if (activated && needEvents && visible)
	{
		for (auto& child : children)
		{
			if (child->checkAllTouchMotion(id, x, y, _touchChecked))
			{
				_touchChecked = true;
			}
			if (stopForApplicationQuit())
			{
				return true;
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
	return stopForApplicationQuit() || _touchChecked;
}

bool Element::checkTouchMotion(EventTouchID id, int x, int y, bool touchChecked)
{
	if (mouseInRect(x, y) && !touchChecked)
	{
		if (ElementPointerClickPolicy::shouldPreservePressedPointerOnMotion(
			touchingDownID, id, TOUCH_UNTOUCHEDID))
		{
			// A second pointer may hover elsewhere, but it cannot replace the
			// active press owner on this control. In particular, Engine's
			// per-frame synthetic mouse refresh runs after touch events and must
			// not cancel a real finger transaction when both hit this element.
			return true;
		}
		if (touchingID != id)
		{
			touchingDownID = TOUCH_UNTOUCHEDID;
			touchingID = id;
			onMouseMoveIn(x, y);
			if (stopForApplicationQuit())
			{
				return true;
			}
			onMouseMoving(x, y);
			return true;
		}
		else
		{
			onMouseMoving(x, y);
			if (stopForApplicationQuit())
			{
				return true;
			}
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
					if (stopForApplicationQuit())
					{
						return true;
					}
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
			if (touchingDownID == id && shouldKeepTouchWhenPointerLeaves(x, y))
			{
				onMouseMoving(x, y);
				return true;
			}
			EventTouchID savedTouchingDownID = touchingDownID;
			touchingID = TOUCH_UNTOUCHEDID;
            touchingDownID = TOUCH_UNTOUCHEDID;
			onMouseMoveOut();
			if (stopForApplicationQuit())
			{
				return true;
			}
			if (savedTouchingDownID == id && canDrag && dragging == TOUCH_UNTOUCHEDID)
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
					if (stopForApplicationQuit())
					{
						return true;
					}
					onDragging(dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
				}
			}
		}
	}
	return false;
}

bool Element::checkTouchDown(EventTouchID id, int x, int y)
{
	if (ElementPointerClickPolicy::shouldAcquirePointerOnDown(
		touchingDownID, id, TOUCH_UNTOUCHEDID, mouseInRect(x, y))
		&& touchingID != id)
	{
		touchingID = id;
		onMouseMoveIn(x, y);
		if (stopForApplicationQuit())
		{
			return true;
		}
		onMouseMoving(x, y);
	}
	if (stopForApplicationQuit())
	{
		return true;
	}
	if (touchingID == id)
	{
		touchingDownID = id;
		touchingDownTime = getTime();
		mouseLDownX = x;
		mouseLDownY = y;
		onMouseLeftDown(x, y);
		return true;
	}
	return false;
}

bool Element::checkTouchUp(EventTouchID id, int x, int y)
{
	if (touchingID == id
		&& (touchingDownID == id || (dragging == id && canDrop)))
	{
		const bool releasedInside = mouseInRect(x, y);
		const bool withinMaximumClickTime = id == TOUCH_MOUSEID
			|| getTime() - touchingDownTime <= clickCheckMaxTime;
		onMouseLeftUp(x, y);
		if (stopForApplicationQuit())
		{
			return true;
		}
		if (ElementPointerClickPolicy::shouldTriggerClick(
			touchingDownID == id,
			releasedInside,
			withinMaximumClickTime,
			dragging == id))
		{
			onClick();
			if (stopForApplicationQuit())
			{
				return true;
			}
		}
		touchingDownID = TOUCH_UNTOUCHEDID;
		if (id != TOUCH_MOUSEID)
		{
			touchingID = TOUCH_UNTOUCHEDID;
			onMouseMoveOut();
		}
		if (dragging == id)
		{
			if (currentDragItem != nullptr)
			{
				currentDragItem->onDragEnd(getMySharedPtr(), dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
				if (stopForApplicationQuit())
				{
					return true;
				}
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

bool Element::dispatchApplicationEvent(AEvent& e)
{
	if (e.eventType == ET_QUIT)
	{
		requestApplicationQuit();
		return true;
	}
	if (e.eventType == ET_WINDOWCLOSE)
	{
		// 模态界面通过 run() 建立自己的事件循环。窗口关闭确认属于
		// 应用全局策略，必须覆盖当前场景或任意嵌套事件层。
		Element* sceneRoot = this;
		while (sceneRoot->parent != nullptr)
		{
			sceneRoot = sceneRoot->parent;
		}
		if (windowCloseConfirmationHandler)
		{
			if (windowCloseConfirmationHandler(*sceneRoot))
			{
				requestApplicationQuit();
			}
			return true;
		}
		if (!sceneRoot->onHandleEvent(e))
		{
			// 没有声明关闭策略的独立场景仍按传统行为退出。
			requestApplicationQuit();
		}
		return true;
	}
	if (e.eventType == ET_WINDOWRESIZE)
	{
		const bool trackedEngineResize =
			e.eventData > 0;
		if (trackedEngineResize)
		{
			activeApplicationResizeDepth++;
		}
		NoThrowScopeExit resizeCallbackScope(
			[trackedEngineResize]()
			{
				if (trackedEngineResize &&
					activeApplicationResizeDepth > 0)
				{
					activeApplicationResizeDepth--;
				}
			});
		if (!resizeRunningRoots(e.eventX, e.eventY))
		{
			resizeAll(e.eventX, e.eventY);
		}
		if (trackedEngineResize &&
			!stopForApplicationQuit())
		{
			engine->acknowledgeLogicalResizeEvent(
				static_cast<std::uint32_t>(
					e.eventData),
				e.eventX,
				e.eventY);
		}
		return true;
	}
	return false;
}

void Element::allHandleEvents()
{
	AEvent e;
	bool mouseMoved = false;
	std::set<EventTouchID> fingerSet;
	while (engine->getEvent(e) > 0)
	{
		if (frameInputEventHandler)
		{
			frameInputEventHandler(e, engine);
			if (stopForApplicationQuit())
			{
				return;
			}
		}
		if (dispatchApplicationEvent(e))
		{
			if (stopForApplicationQuit() ||
				!logicRunning)
			{
				return;
			}
			continue;
		}
		if (rawPointerInputBlocked && isRawPointerEvent(e.eventType))
		{
			continue;
		}
		if (e.eventType == ET_MOUSEMOTION || e.eventType == ET_FINGERMOTION)
		{		
			if (e.eventType == ET_MOUSEMOTION)
			{
				mouseMoved = true;
				e.eventData = TOUCH_MOUSEID;
			}
			else
			{
				fingerSet.insert(e.eventData);
			}
			checkAllTouchMotion(e.eventData, e.eventX, e.eventY, false);
			if (stopForApplicationQuit())
			{
				return;
			}
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
			if (stopForApplicationQuit())
			{
				return;
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
			if (stopForApplicationQuit())
			{
				return;
			}
			if (dragging == tempTouchID)
			{
				if (currentDragItem != nullptr)
				{
					currentDragItem->onDragEnd(PElement(nullptr), dragTouchPosition.x - dragDownPosition.x, dragTouchPosition.y - dragDownPosition.y);
					if (stopForApplicationQuit())
					{
						return;
					}
				}
				dragging = TOUCH_UNTOUCHEDID;
				currentDragItem = nullptr;
			}
		}
		else if (e.eventType == ET_FINGERCANCEL)
		{
			// Cancel only the contact named by SDL. Other simultaneous contacts and
			// non-pointer result bits remain intact, and no release callback runs.
			// Route the raw cancel after clearing touch state so transaction owners
			// can release their input latch without synthesizing a click.
			cancelPointerInteraction(e.eventData);
			previewPointerEvent(e);
			if (stopForApplicationQuit())
			{
				return;
			}
			handleEvent(e);
			if (stopForApplicationQuit())
			{
				return;
			}
			continue;
		}
		if (isRawPointerEvent(e.eventType))
		{
			previewPointerEvent(e);
			if (stopForApplicationQuit())
			{
				return;
			}
		}
		handleEvent(e);
		if (stopForApplicationQuit())
		{
			return;
		}
	}
	if (stopForApplicationQuit())
	{
		return;
	}
	if (!rawPointerInputBlocked && !mouseMoved)
	{
		int mouseX, mouseY;
		engine->getMousePosition(mouseX, mouseY);
		checkAllTouchMotion(TOUCH_MOUSEID, mouseX, mouseY, false);
		if (stopForApplicationQuit())
		{
			return;
		}
	}
	auto fingers = engine->getAllFingersPosition();
	for (size_t i = 0; !rawPointerInputBlocked && i < fingers.size(); i++)
	{
		if (fingerSet.find(fingers[i].eventData) != fingerSet.end())
		{
			checkAllTouchMotion(fingers[i].eventData, fingers[i].eventX, fingers[i].eventY, false);
			if (stopForApplicationQuit())
			{
				return;
			}
		}
	}
	if (!stopForApplicationQuit())
	{
		handleEvents();
	}
}

void Element::frame()
{
	nextFrame = false;
	frameSemanticInputBlocked = currentRunOwnerBlocksParentInput();

	engine->frameBegin();
	if (stopForApplicationQuit())
	{
		return;
	}

	const bool applicationActive = engine->isApplicationActive();
	const bool frameReady = engine->isFrameReady();
	setRunningElementsPaused(!applicationActive || !frameReady);
	if (!applicationActive || !frameReady)
	{
		engine->delay(16);
		return;
	}

	activeFrameCallbackDepth++;
	NoThrowScopeExit frameCallbackScope(
		[]()
		{
			if (activeFrameCallbackDepth > 0)
			{
				activeFrameCallbackDepth--;
			}
		});
	if (stopForApplicationQuit())
	{
		return;
	}
	if (frameGlobalInputHandler)
	{
		// Global actions run before keyboard/pointer UI events and semantic UI
		// actions so a modal closing in this frame cannot discard their edges.
		dispatchFrameGlobalInput(engine);
		if (stopForApplicationQuit() || !logicRunning)
		{
			return;
		}
	}
	preTreatmentAll();
	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}

    allHandleEvents();
	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}
	if (frameSemanticInputHandler)
	{
		frameSemanticInputBlocked = frameSemanticInputHandler(engine)
			|| frameSemanticInputBlocked;
	}
	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}
	if (frameGameplayInputHandler)
	{
		// Gameplay input runs after UI events and semantic UI dispatch, but
		// before world children update and can execute queued interactions.
		frameGameplayInputHandler(engine);
	}
	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}

	updateAll();

	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}

	if (!nextFrame)
	{
		drawAll();
	}
	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}
	
	postTreatmentAll();
	if (stopForApplicationQuit() || !logicRunning)
	{
		return;
	}

	if (!nextFrame)
	{
		engine->frameEnd();
	}
}

bool Element::stopForApplicationQuit()
{
	if (applicationQuitRequested.load())
	{
		return true;
	}
	if (engine != nullptr &&
		engine->isApplicationQuitRequested())
	{
		requestApplicationQuit();
		return true;
	}
	if ((activeFrameCallbackDepth > 0 ||
			activeApplicationResizeDepth > 0) &&
		engine != nullptr &&
		(!engine->isApplicationActive() ||
			!engine->isFrameReady()))
	{
		// Lifecycle filters can close render admission from another thread.
		// Stop after the current callback without treating suspension as exit.
		return true;
	}
	return false;
}

bool Element::initial()
{
	if (!onInitial())
	{
		return false;
	}
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{	
		if (!child->initial())
		{
			return false;
		}
	}
	return true;
}

void Element::handleRun()
{
	reArrangeChildren();
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		child->handleRun();
	}
	onRun();
}

void Element::exit()
{	
	reArrangeChildren();
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		child->exit();
	}
	onExit();
}

void Element::quit()
{
	auto childrenCopy = children;
	for (auto& child : childrenCopy)
	{
		child->quit();
	}
	logicRunning = false;
}


unsigned int Element::run()
{
	if (applicationQuitRequested.load() || engine->isApplicationQuitRequested())
	{
		result |= erExit;
		return result;
	}

	const bool nestedInputContext = !runningElement.empty();
	const bool inputContextTransitionAtEntry =
		nestedInputContext || inputContextStarted;
	runningElementClearAllTouch();
	if (inputContextTransitionAtEntry && inputContextTransitionHandler)
	{
		// Nested runs and a new top-level run after a completed scene both
		// change the input context. The very first run keeps the AwaitNeutral
		// gate established when the device was opened.
		inputContextTransitionHandler();
	}
	inputContextStarted = true;

	bool runningStackEntryActive = false;
	bool exitInputTransitionAttempted = false;
	NoThrowScopeExit runningStackCleanup(
		[this,
		 nestedInputContext,
		 &runningStackEntryActive,
		 &exitInputTransitionAttempted]()
		{
			if (!runningStackEntryActive)
			{
				return;
			}
			logicRunning = false;
			if (nestedInputContext &&
				!exitInputTransitionAttempted &&
				inputContextTransitionHandler)
			{
				try
				{
					inputContextTransitionHandler();
				}
				catch (...)
				{
				}
			}
			if (!runningElement.empty() &&
				runningElement.back().get() == this)
			{
				runningElement.pop_back();
				runningStackEntryActive = false;
				return;
			}
			auto runOwner = std::find_if(
				runningElement.rbegin(),
				runningElement.rend(),
				[this](const PElement& running)
				{
					return running.get() == this;
				});
			if (runOwner != runningElement.rend())
			{
				runningElement.erase(std::next(runOwner).base());
			}
			runningStackEntryActive = false;
		});
	runningElement.push_back(getMySharedPtr());
	runningStackEntryActive = true;

	logicRunning = true;

	//engine->initTime(&timer);
	if (!initial())
	{
		result = erInitError;
		logicRunning = false;
	}
	if (logicRunning)
	{
		handleRun();
	}
	while (logicRunning)
	{
		frame();
	}
	exit();
	// 返回时调用一次 frameBegin，以免普通嵌套 run 返回时因没有再次调用
	// frameBegin 造成绘制出错。应用退出时不再启动新帧。
	if (!applicationQuitRequested.load() && !engine->isApplicationQuitRequested())
	{
		engine->frameBegin();
		if (frameGlobalInputHandler)
		{
			// This exit pump can create a global shortcut edge after the nested
			// frame's normal dispatch point. Consume it before the context
			// transition clears ordinary input edges.
			dispatchFrameGlobalInput(engine);
		}
	}
	if (nestedInputContext && inputContextTransitionHandler)
	{
		// Input may have become held while the child context was active. Clear
		// it again and require neutral before the parent accepts fresh input.
		exitInputTransitionAttempted = true;
		inputContextTransitionHandler();
	}

	if (!runningElement.empty() &&
		runningElement.back().get() == this)
	{
		runningElement.pop_back();
	}
	else
	{
		auto runOwner = std::find_if(
			runningElement.rbegin(),
			runningElement.rend(),
			[this](const PElement& running)
			{
				return running.get() == this;
			});
		if (runOwner != runningElement.rend())
		{
			runningElement.erase(std::next(runOwner).base());
		}
	}
	runningStackEntryActive = false;
	runningStackCleanup.dismiss();

	return result;
}

unsigned int Element::stop(int ret)
{
	logicRunning = false;
	result = ret;
	return ret;
}

void Element::freeAll()
{
	for (auto& child : children)
	{
		child->freeAll();
	}
	removeAllChild();
	freeResource();
}

bool Element::isDragging()
{
	return dragging != TOUCH_UNTOUCHEDID && currentDragItem == getMySharedPtr();
}
