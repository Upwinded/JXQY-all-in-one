#pragma once

#include "BaseComponent.h"

class FlatScrollbar : public BaseComponent
{
public:
	FlatScrollbar();
	virtual ~FlatScrollbar();

	void setRange(int minimumValue, int maximumValue);
	void setPosition(int value);
	int getPosition() const;
	void setPageSize(int value);
	int getPageSize() const;
	void setMinimumThumbLength(int value);
	void setVisualTrackWidth(int value);
	Rect getVisualTrackRect() const;
	Rect getThumbRect() const;
	bool ownsPointerInteraction(EventTouchID pointerID) const;

protected:
	virtual void onDraw() override;
	virtual void onMouseLeftDown(int x, int y) override;
	virtual void onMouseMoving(int x, int y) override;
	virtual bool shouldKeepTouchWhenPointerLeaves(int x, int y) override;

private:
	void updatePositionFromPointer(int y);
	void positionChanged(int value);

	int minimum = 0;
	int maximum = 0;
	int position = 0;
	int pageSize = 1;
	int minimumThumbLength = 28;
	int visualTrackWidth = 8;
	int pointerThumbOffset = 0;
};
