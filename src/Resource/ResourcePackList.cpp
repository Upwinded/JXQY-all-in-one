#include "ResourcePackList.h"

#include <algorithm>
#include <cmath>
#include <utility>

ResourcePackList::ResourcePackList()
{
	name = "resourcepacklist";
	setPriority(epComponent);
	coverMouse = false;
	canCallBack = true;
	scrollbar = std::make_shared<FlatScrollbar>();
	addChild(scrollbar);
}

ResourcePackList::~ResourcePackList()
{
}

void ResourcePackList::setItems(const std::vector<ResourcePackCardContent>& values)
{
	const int previousSelectedIndex = selectedIndex;
	for (const auto& card : cards)
	{
		removeChild(card);
	}
	cards.clear();
	cards.reserve(values.size());
	for (std::size_t itemIndex = 0; itemIndex < values.size(); itemIndex++)
	{
		auto card = std::make_shared<ResourcePackCard>();
		card->index = static_cast<int>(itemIndex);
		card->setContent(values[itemIndex]);
		card->setFrameImages(normalFrameImage, selectedFrameImage);
		addChild(card);
		cards.push_back(card);
	}
	selectedIndex = cards.empty() ? 0 : std::clamp(selectedIndex, 0, static_cast<int>(cards.size()) - 1);
	firstVisibleIndex = std::clamp(firstVisibleIndex, 0, getMaximumFirstVisibleIndex());
	layoutItems();
	if (selectedIndex != previousSelectedIndex)
	{
		notifySelectionChanged();
	}
}

void ResourcePackList::setFrameImages(_shared_image normal, _shared_image selected)
{
	normalFrameImage = normal;
	selectedFrameImage = selected;
	for (const auto& card : cards)
	{
		card->setFrameImages(normal, selected);
	}
}

void ResourcePackList::setItemMetrics(int height, int gap)
{
	itemHeight = std::max(1, height);
	itemGap = std::clamp(gap, 0, itemHeight - 1);
	firstVisibleIndex = std::clamp(
		firstVisibleIndex, 0, getMaximumFirstVisibleIndex());
	layoutItems();
}

void ResourcePackList::setLayout(const Rect& itemArea, const Rect& scrollbarArea)
{
	rect = itemArea;
	scrollbarRect = scrollbarArea;
	layoutItems();
}

void ResourcePackList::setSelectionIndicatorVisible(bool value)
{
	selectionIndicatorVisible = value;
	for (const auto& card : cards)
	{
		card->setSelected(selectionIndicatorVisible && card->index == selectedIndex);
	}
}

bool ResourcePackList::isSelectionIndicatorVisible() const
{
	return selectionIndicatorVisible;
}

void ResourcePackList::setPointerTakeoverHandler(std::function<void()> handler)
{
	pointerTakeoverHandler = std::move(handler);
}

void ResourcePackList::setSelectionChangedHandler(
	std::function<void(int)> handler)
{
	selectionChangedHandler = std::move(handler);
}

void ResourcePackList::setSelectedIndex(int value)
{
	const int previousIndex = selectedIndex;
	if (cards.empty())
	{
		selectedIndex = 0;
		if (selectedIndex != previousIndex)
		{
			notifySelectionChanged();
		}
		return;
	}
	selectedIndex = std::clamp(value, 0, static_cast<int>(cards.size()) - 1);
	for (const auto& card : cards)
	{
		card->setSelected(selectionIndicatorVisible && card->index == selectedIndex);
	}
	if (selectedIndex != previousIndex)
	{
		notifySelectionChanged();
	}
}

int ResourcePackList::getSelectedIndex() const
{
	return selectedIndex;
}

void ResourcePackList::ensureSelectedVisible()
{
	if (cards.empty())
	{
		return;
	}
	const int visibleCount = getVisibleItemCount();
	if (selectedIndex < firstVisibleIndex)
	{
		scrollToFirstIndex(selectedIndex);
	}
	else if (selectedIndex >= firstVisibleIndex + visibleCount)
	{
		scrollToFirstIndex(selectedIndex - visibleCount + 1);
	}
}

void ResourcePackList::scrollByRows(int rows)
{
	if (rows != 0)
	{
		scrollToFirstIndex(firstVisibleIndex + rows);
	}
}

int ResourcePackList::getFirstVisibleIndex() const
{
	return firstVisibleIndex;
}

int ResourcePackList::getVisibleItemCount() const
{
	return std::max(1, rect.h / itemHeight);
}

int ResourcePackList::getMaximumFirstVisibleIndex() const
{
	return std::max(0, static_cast<int>(cards.size()) - getVisibleItemCount());
}

void ResourcePackList::onChildCallBack(PElement child)
{
	if (child == scrollbar)
	{
		if ((child->getResult() & erScrollbarSlided) != 0)
		{
			scrollToFirstIndex(scrollbar->getPosition());
		}
		return;
	}

	auto card = std::dynamic_pointer_cast<ResourcePackCard>(child);
	if (card == nullptr)
	{
		return;
	}
	if (card->takeDescriptionActionRequested())
	{
		if (pointerTakeoverHandler)
		{
			pointerTakeoverHandler();
		}
		const bool ownsDescriptionPointer =
			listPointerDown
			&& descriptionPointerCardIndex == card->index
			&& listPointerId != TOUCH_UNTOUCHEDID
			&& card->ownsPointerInteraction(listPointerId);
		if (ownsDescriptionPointer)
		{
			setSelectedIndex(card->index);
			index = card->index;
		}
		return;
	}
	const unsigned int childResult = card->getResult();
	if ((childResult & erClick) == 0)
	{
		return;
	}
	const bool ownsCardPointer =
		listPointerDown
		&& !listPointerDragging
		&& descriptionPointerCardIndex < 0
		&& listPointerId != TOUCH_UNTOUCHEDID
		&& getItemAtPoint(listPointerDownX, listPointerDownY)
			== card->index
		&& card->ownsBodyPointerInteraction(listPointerId);
	if (!ownsCardPointer)
	{
		return;
	}
	setSelectedIndex(card->index);
	index = card->index;
	result |= erClick;
	if (canCallBack && parent != nullptr)
	{
		parent->onChildCallBack(getMySharedPtr());
		result = erNone;
	}
}

bool ResourcePackList::onHandleEvent(AEvent& event)
{
	// EngineBase appends a synthetic mouse-position refresh every frame so
	// Element can keep hover hit state current. That refresh is not pointer
	// input: claiming takeover here would hide keyboard focus, and selecting
	// the card under a stationary cursor would undo the keyboard step that
	// was dispatched earlier in the same frame.
	if (event.synthetic)
	{
		return false;
	}
	notifyPointerTakeover(event);
	if (event.eventType == ET_MOUSEWHEEL)
	{
		scrollByRows(static_cast<int>(event.eventData));
		return getMaximumFirstVisibleIndex() > 0;
	}
	if (event.eventType == ET_MOUSEDOWN && event.eventData == MBC_MOUSE_LEFT)
	{
		if (isScrollbarPoint(event.eventX, event.eventY))
		{
			cancelCardPointerInteractions();
			if (scrollbar->ownsPointerInteraction(TOUCH_MOUSEID))
			{
				resetListPointer();
				scrollbarPointerId = TOUCH_MOUSEID;
			}
			return true;
		}
		return beginListPointer(TOUCH_MOUSEID, event.eventX, event.eventY);
	}
	if (event.eventType == ET_MOUSEMOTION)
	{
		if (scrollbarPointerId == TOUCH_MOUSEID)
		{
			return true;
		}
		if (isActiveListPointer(TOUCH_MOUSEID))
		{
			return updateListPointer(TOUCH_MOUSEID, event.eventX, event.eventY);
		}
		if (isScrollbarPoint(event.eventX, event.eventY))
		{
			return true;
		}
		const int itemIndex = getItemAtPoint(event.eventX, event.eventY);
		if (itemIndex >= 0)
		{
			setSelectedIndex(itemIndex);
		}
		return false;
	}
	if (event.eventType == ET_MOUSEUP && event.eventData == MBC_MOUSE_LEFT)
	{
		if (scrollbarPointerId == TOUCH_MOUSEID)
		{
			scrollbarPointerId = TOUCH_UNTOUCHEDID;
			return true;
		}
		return finishListPointer(TOUCH_MOUSEID);
	}
	if (event.eventType == ET_FINGERDOWN)
	{
		if (isScrollbarPoint(event.eventX, event.eventY))
		{
			cancelCardPointerInteractions();
			if (scrollbar->ownsPointerInteraction(event.eventData))
			{
				resetListPointer();
				scrollbarPointerId = event.eventData;
			}
			return true;
		}
		return beginListPointer(event.eventData, event.eventX, event.eventY);
	}
	if (event.eventType == ET_FINGERMOTION)
	{
		if (scrollbarPointerId == event.eventData)
		{
			return true;
		}
		if (!isActiveListPointer(event.eventData) &&
			isScrollbarPoint(event.eventX, event.eventY))
		{
			return true;
		}
		return updateListPointer(event.eventData, event.eventX, event.eventY);
	}
	if (event.eventType == ET_FINGERUP)
	{
		if (scrollbarPointerId == event.eventData)
		{
			scrollbarPointerId = TOUCH_UNTOUCHEDID;
			return true;
		}
		return finishListPointer(event.eventData);
	}
	return false;
}

bool ResourcePackList::onPointerInteractionCanceled(EventTouchID pointerID)
{
	if (scrollbarPointerId == pointerID)
	{
		scrollbarPointerId = TOUCH_UNTOUCHEDID;
		return true;
	}
	if (!isActiveListPointer(pointerID))
	{
		return false;
	}
	resetListPointer();
	return true;
}

void ResourcePackList::onAllPointerInteractionsCanceled()
{
	scrollbarPointerId = TOUCH_UNTOUCHEDID;
	resetListPointer();
}

void ResourcePackList::layoutItems()
{
	firstVisibleIndex = std::clamp(firstVisibleIndex, 0, getMaximumFirstVisibleIndex());
	const int visibleCount = getVisibleItemCount();
	for (const auto& card : cards)
	{
		const int row = card->index - firstVisibleIndex;
		const bool itemVisible = row >= 0 && row < visibleCount;
		card->visible = itemVisible;
		card->activated = itemVisible;
		card->setSelected(selectionIndicatorVisible && card->index == selectedIndex);
		if (itemVisible)
		{
			card->setLayout(
				{ rect.x, rect.y + row * itemHeight,
					rect.w, itemHeight - itemGap });
		}
		else
		{
			card->cancelPointerInteraction();
		}
	}

	const int maximumFirstIndex = getMaximumFirstVisibleIndex();
	scrollbar->rect = scrollbarRect;
	scrollbar->setRange(0, maximumFirstIndex);
	scrollbar->setPageSize(visibleCount);
	scrollbar->setPosition(firstVisibleIndex);
	scrollbar->visible = maximumFirstIndex > 0;
	scrollbar->activated = maximumFirstIndex > 0;
}

void ResourcePackList::scrollToFirstIndex(int value)
{
	const int previousSelectedIndex = selectedIndex;
	firstVisibleIndex = std::clamp(value, 0, getMaximumFirstVisibleIndex());
	if (!cards.empty())
	{
		const int visibleCount = getVisibleItemCount();
		if (selectedIndex < firstVisibleIndex)
		{
			selectedIndex = firstVisibleIndex;
		}
		else if (selectedIndex >= firstVisibleIndex + visibleCount)
		{
			selectedIndex = std::min(
				static_cast<int>(cards.size()) - 1,
				firstVisibleIndex + visibleCount - 1);
		}
	}
	layoutItems();
	if (selectedIndex != previousSelectedIndex)
	{
		notifySelectionChanged();
	}
}

int ResourcePackList::getItemAtPoint(int x, int y) const
{
	if (!isItemAreaPoint(x, y))
	{
		return -1;
	}
	const int row = (y - rect.y) / itemHeight;
	if (row < 0 || row >= getVisibleItemCount())
	{
		return -1;
	}
	const int itemY = rect.y + row * itemHeight;
	if (y >= itemY + itemHeight - itemGap)
	{
		return -1;
	}
	const int itemIndex = firstVisibleIndex + row;
	return itemIndex >= 0 && itemIndex < static_cast<int>(cards.size()) ? itemIndex : -1;
}

bool ResourcePackList::isItemAreaPoint(int x, int y) const
{
	return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

bool ResourcePackList::isScrollbarPoint(int x, int y) const
{
	return scrollbar != nullptr && scrollbar->visible &&
		x >= scrollbarRect.x && x < scrollbarRect.x + scrollbarRect.w &&
		y >= scrollbarRect.y && y < scrollbarRect.y + scrollbarRect.h;
}

bool ResourcePackList::beginListPointer(EventTouchID activePointerId, int x, int y)
{
	if (listPointerDown || !isItemAreaPoint(x, y))
	{
		return false;
	}
	listPointerDown = true;
	listPointerDragging = false;
	listPointerId = activePointerId;
	listPointerDownX = x;
	listPointerDownY = y;
	pointerStartFirstVisibleIndex = firstVisibleIndex;
	const int itemIndex = getItemAtPoint(x, y);
	if (itemIndex >= 0)
	{
		const bool descriptionPointer =
			itemIndex < static_cast<int>(cards.size())
			&& cards[itemIndex] != nullptr
			&& cards[itemIndex]->isDescriptionActionPoint(x, y);
		descriptionPointerCardIndex =
			descriptionPointer ? itemIndex : -1;
		if (!descriptionPointer)
		{
			setSelectedIndex(itemIndex);
		}
	}
	return true;
}

bool ResourcePackList::updateListPointer(EventTouchID activePointerId, int x, int y)
{
	if (!isActiveListPointer(activePointerId))
	{
		return false;
	}
	const int deltaX = x - listPointerDownX;
	const int deltaY = listPointerDownY - y;
	if (std::abs(deltaX) > DragThresholdPixels || std::abs(deltaY) > DragThresholdPixels)
	{
		listPointerDragging = true;
	}
	if (listPointerDragging)
	{
		cancelCardPointerInteractions();
		descriptionPointerCardIndex = -1;
		scrollToFirstIndex(pointerStartFirstVisibleIndex + deltaY / itemHeight);
		return true;
	}
	if (descriptionPointerCardIndex >= 0)
	{
		return true;
	}
	const int itemIndex = getItemAtPoint(x, y);
	if (itemIndex >= 0)
	{
		setSelectedIndex(itemIndex);
	}
	return true;
}

bool ResourcePackList::finishListPointer(EventTouchID activePointerId)
{
	if (!isActiveListPointer(activePointerId))
	{
		return false;
	}
	const bool wasDragging = listPointerDragging;
	resetListPointer();
	return wasDragging;
}

bool ResourcePackList::isActiveListPointer(EventTouchID activePointerId) const
{
	return listPointerDown && listPointerId == activePointerId;
}

void ResourcePackList::notifyPointerTakeover(const AEvent& event)
{
	if (!pointerTakeoverHandler)
	{
		return;
	}
	switch (event.eventType)
	{
	case ET_MOUSEMOTION:
	case ET_MOUSEDOWN:
	case ET_MOUSEUP:
	case ET_MOUSEWHEEL:
	case ET_FINGERDOWN:
	case ET_FINGERUP:
	case ET_FINGERMOTION:
		pointerTakeoverHandler();
		break;
	default:
		break;
	}
}

void ResourcePackList::notifySelectionChanged()
{
	if (selectionChangedHandler)
	{
		selectionChangedHandler(selectedIndex);
	}
}

void ResourcePackList::resetListPointer()
{
	listPointerDown = false;
	listPointerDragging = false;
	listPointerId = TOUCH_UNTOUCHEDID;
	descriptionPointerCardIndex = -1;
	listPointerDownX = 0;
	listPointerDownY = 0;
	pointerStartFirstVisibleIndex = firstVisibleIndex;
}

void ResourcePackList::cancelCardPointerInteractions()
{
	for (const auto& card : cards)
	{
		card->cancelPointerInteraction();
	}
}
