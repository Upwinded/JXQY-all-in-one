#include "ChooseTextButton.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredChooseTextButton = []
	{
		ComponentRegistry::getInstance().registerType("ChooseTextButton",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<ChooseTextButton>(); });
		return true;
	}();
}

ChooseTextButton::ChooseTextButton()
{
	name = "choosetextbutton";
	normalLabel.color = normalColor;
	hoverLabel.color = hoverColor;
	pressLabel.color = pressColor;
	normalLabel.autoNextLine = true;
	hoverLabel.autoNextLine = true;
	pressLabel.autoNextLine = true;
}

ChooseTextButton::~ChooseTextButton()
{
}

void ChooseTextButton::setNormalColor(unsigned int color)
{
	normalColor = color;
	normalLabel.color = color;
	normalLabel.setStr(label.getStr());
}

void ChooseTextButton::setHoverColor(unsigned int color)
{
	hoverColor = color;
	hoverLabel.color = color;
	hoverLabel.setStr(label.getStr());
}

void ChooseTextButton::setPressColor(unsigned int color)
{
	pressColor = color;
	pressLabel.color = color;
	pressLabel.setStr(label.getStr());
}

void ChooseTextButton::setSelected(bool value)
{
	selected = value;
}

bool ChooseTextButton::isSelected() const
{
	return selected;
}

void ChooseTextButton::setNavigationHighlighted(bool value)
{
	navigationHighlighted = value;
}

bool ChooseTextButton::isNavigationHighlighted() const
{
	return navigationHighlighted;
}

void ChooseTextButton::setStr(const std::string& s)
{
	TextButton::setStr(s);
	normalLabel.fontSize = label.fontSize;
	hoverLabel.fontSize = label.fontSize;
	pressLabel.fontSize = label.fontSize;
	normalLabel.rect = rect;
	hoverLabel.rect = rect;
	pressLabel.rect = rect;
	normalLabel.setStr(s);
	hoverLabel.setStr(s);
	pressLabel.setStr(s);
}

void ChooseTextButton::initFromIni(INIReader & ini)
{
	TextButton::initFromIni(ini);
	normalColor = ini.GetColor("Init", "NormalColor", ini.GetColor("Init", "Color", normalColor));
	hoverColor = ini.GetColor("Init", "HoverColor", hoverColor);
	pressColor = ini.GetColor("Init", "PressColor", pressColor);
	normalLabel.color = normalColor;
	hoverLabel.color = hoverColor;
	pressLabel.color = pressColor;
	normalLabel.fontSize = label.fontSize;
	hoverLabel.fontSize = label.fontSize;
	pressLabel.fontSize = label.fontSize;
	normalLabel.autoNextLine = true;
	hoverLabel.autoNextLine = true;
	pressLabel.autoNextLine = true;
	if (!label.getStr().empty())
	{
		normalLabel.setStr(label.getStr());
		hoverLabel.setStr(label.getStr());
		pressLabel.setStr(label.getStr());
	}
}

void ChooseTextButton::onDraw()
{
	Button::onDraw();
	if (navigationHighlighted && !isFocused())
	{
		drawNavigationHighlight();
	}
	Label* currentLabel = &normalLabel;
	if (touchingDownID != TOUCH_UNTOUCHEDID || (dragging != TOUCH_UNTOUCHEDID && currentDragItem == getMySharedPtr()))
	{
		currentLabel = &pressLabel;
	}
	else if (selected)
	{
		currentLabel = &pressLabel;
	}
	else if (touchingID != TOUCH_UNTOUCHEDID || isFocused()
		|| navigationHighlighted)
	{
		currentLabel = &hoverLabel;
	}
	currentLabel->rect = rect;
	currentLabel->refreshTextLayout();
	int renderedTextHeight = currentLabel->getRenderedTextHeight();
	if (renderedTextHeight < rect.h)
	{
		currentLabel->rect.y += (rect.h - renderedTextHeight) / 2;
	}
	currentLabel->onDraw();
}

void ChooseTextButton::drawNavigationHighlight()
{
	if (rect.w < 2 || rect.h < 2)
	{
		return;
	}
	constexpr int NavigationBorderWidth = 2;
	constexpr uint8_t NavigationRed = 72;
	constexpr uint8_t NavigationGreen = 196;
	constexpr uint8_t NavigationBlue = 255;
	constexpr uint8_t NavigationAlpha = 240;
	engine->fillRect(
		rect.x, rect.y, rect.w, NavigationBorderWidth,
		NavigationRed, NavigationGreen, NavigationBlue, NavigationAlpha);
	engine->fillRect(
		rect.x, rect.y + rect.h - NavigationBorderWidth,
		rect.w, NavigationBorderWidth,
		NavigationRed, NavigationGreen, NavigationBlue, NavigationAlpha);
	engine->fillRect(
		rect.x, rect.y, NavigationBorderWidth, rect.h,
		NavigationRed, NavigationGreen, NavigationBlue, NavigationAlpha);
	engine->fillRect(
		rect.x + rect.w - NavigationBorderWidth, rect.y,
		NavigationBorderWidth, rect.h,
		NavigationRed, NavigationGreen, NavigationBlue, NavigationAlpha);
}

void ChooseTextButton::onMouseMoveIn(int x, int y)
{
	Button::onMouseMoveIn(x, y);
}

void ChooseTextButton::onMouseMoveOut()
{
	Button::onMouseMoveOut();
}

void ChooseTextButton::onMouseLeftDown(int x, int y)
{
	playSound(1);
	result |= erMouseLDown;
}

void ChooseTextButton::onMouseLeftUp(int x, int y)
{
	playSound(2);
	result |= erMouseLUp;
}

void ChooseTextButton::onClick()
{
	result |= erClick;
}
