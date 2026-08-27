#pragma once

#include "FlatComponentPolicy.h"
#include "TextButton.h"

#include <cstdint>

struct FlatButtonColor
{
	uint8_t red = 0;
	uint8_t green = 0;
	uint8_t blue = 0;
	uint8_t alpha = 255;
};

struct FlatButtonVisual
{
	FlatButtonColor border;
	FlatButtonColor background;
	unsigned int textColor = 0xFFFFFFFF;
};

struct FlatTextButtonStyle
{
	FlatButtonVisual normal = { { 176, 146, 88, 145 }, { 32, 25, 20, 230 }, 0xFFFFE7B0 };
	FlatButtonVisual hovered = { { 218, 182, 108, 190 }, { 54, 34, 25, 230 }, 0xFFFFFFFF };
	FlatButtonVisual pressed = { { 238, 204, 130, 220 }, { 78, 44, 32, 230 }, 0xFFFFFFFF };
	int borderThickness = 1;
	int textPadding = 4;
};

class FlatTextButton : public TextButton
{
public:
	FlatTextButton();
	virtual ~FlatTextButton();

	void setStyle(const FlatTextButtonStyle& value);
	const FlatTextButtonStyle& getStyle() const;
	FlatComponentPolicy::VisualState getVisualState() const;

protected:
	virtual void onDraw() override;

private:
	FlatTextButtonStyle style;
};
