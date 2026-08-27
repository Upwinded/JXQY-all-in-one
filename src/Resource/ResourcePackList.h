#pragma once

#include "../Component/BaseComponent.h"
#include "../Component/FlatScrollbar.h"
#include "ResourcePackCard.h"

#include <functional>
#include <vector>

class ResourcePackList : public BaseComponent
{
	friend class MobileExternalInputRuntimeTestAccess;
	friend class GamepadEssentialUITestAccess;
	friend class GamepadSurfaceContractTestAccess;
public:
	ResourcePackList();
	virtual ~ResourcePackList();

	void setItems(const std::vector<ResourcePackCardContent>& values);
	void setFrameImages(_shared_image normal, _shared_image selected);
	void setItemMetrics(int height, int gap);
	void setLayout(const Rect& itemArea, const Rect& scrollbarArea);
	void setSelectionIndicatorVisible(bool value);
	bool isSelectionIndicatorVisible() const;
	void setPointerTakeoverHandler(std::function<void()> handler);
	void setSelectionChangedHandler(std::function<void(int)> handler);
	void setSelectedIndex(int value);
	int getSelectedIndex() const;
	void ensureSelectedVisible();
	void scrollByRows(int rows);
	int getFirstVisibleIndex() const;
	int getVisibleItemCount() const;
	int getMaximumFirstVisibleIndex() const;

	virtual void onChildCallBack(PElement child) override;

protected:
	virtual bool onHandleEvent(AEvent& event) override;
	virtual bool onPointerInteractionCanceled(EventTouchID pointerID) override;
	virtual void onAllPointerInteractionsCanceled() override;

private:
	void layoutItems();
	void scrollToFirstIndex(int value);
	int getItemAtPoint(int x, int y) const;
	bool isItemAreaPoint(int x, int y) const;
	bool isScrollbarPoint(int x, int y) const;
	bool beginListPointer(EventTouchID activePointerId, int x, int y);
	bool updateListPointer(EventTouchID activePointerId, int x, int y);
	bool finishListPointer(EventTouchID activePointerId);
	bool isActiveListPointer(EventTouchID activePointerId) const;
	void notifyPointerTakeover(const AEvent& event);
	void notifySelectionChanged();
	void resetListPointer();
	void cancelCardPointerInteractions();

	static constexpr int ItemHeight = 104;
	static constexpr int ItemGap = 10;
	static constexpr int DragThresholdPixels = 12;

	std::vector<std::shared_ptr<ResourcePackCard>> cards;
	std::shared_ptr<FlatScrollbar> scrollbar;
	_shared_image normalFrameImage = nullptr;
	_shared_image selectedFrameImage = nullptr;
	Rect scrollbarRect = { 0, 0, 0, 0 };
	int selectedIndex = 0;
	int firstVisibleIndex = 0;
	int itemHeight = ItemHeight;
	int itemGap = ItemGap;
	bool selectionIndicatorVisible = true;
	std::function<void()> pointerTakeoverHandler;
	std::function<void(int)> selectionChangedHandler;
	bool listPointerDown = false;
	bool listPointerDragging = false;
	EventTouchID listPointerId = TOUCH_UNTOUCHEDID;
	EventTouchID scrollbarPointerId = TOUCH_UNTOUCHEDID;
	int descriptionPointerCardIndex = -1;
	int listPointerDownX = 0;
	int listPointerDownY = 0;
	int pointerStartFirstVisibleIndex = 0;
};
