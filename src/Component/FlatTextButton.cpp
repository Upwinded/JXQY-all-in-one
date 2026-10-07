#include "FlatTextButton.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

#include <algorithm>

namespace
{
bool registeredFlatTextButton = []
{
	ComponentRegistry::getInstance().registerType("FlatTextButton",
		[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<FlatTextButton>(); });
	return true;
}();

void fillRectangle(Engine* engine, const Rect& rectangle, const FlatButtonColor& color)
{
	if (engine == nullptr || rectangle.w <= 0 || rectangle.h <= 0)
	{
		return;
	}
	engine->fillRect(rectangle.x, rectangle.y, rectangle.w, rectangle.h,
		color.red, color.green, color.blue, color.alpha);
}
}

FlatTextButton::FlatTextButton()
{
	name = "flattextbutton";
	canCallBack = true;
	hoverSoundEnabled = false;
	label.autoShrink = true;
	label.elideOverflow = true;
	label.minimumFontSize = 11;
	label.horizontalAlignment = TextHorizontalAlignment::Center;
	label.verticalAlignment = TextVerticalAlignment::Center;
}

FlatTextButton::~FlatTextButton()
{
}

void FlatTextButton::initFromIni(INIReader& ini)
{
	TextButton::initFromIni(ini);
	const auto readBackground = [&ini](const char* key, FlatButtonVisual& visual)
	{
		const auto& old = visual.background;
		const unsigned int fallback = (static_cast<unsigned int>(old.alpha) << 24)
			| (old.red << 16) | (old.green << 8) | old.blue;
		const unsigned int color = ini.GetColor("Init", key, fallback);
		visual.background = { static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8),
			static_cast<uint8_t>(color), static_cast<uint8_t>(color >> 24) };
		visual.textColor = ini.GetColor("Init", "Color", visual.textColor);
	};
	readBackground("BackgroundColor", style.normal);
	readBackground("HoverBackgroundColor", style.hovered);
	readBackground("PressedBackgroundColor", style.pressed);
}

void FlatTextButton::setStyle(const FlatTextButtonStyle& value)
{
	style = value;
}

const FlatTextButtonStyle& FlatTextButton::getStyle() const
{
	return style;
}

FlatComponentPolicy::VisualState FlatTextButton::getVisualState() const
{
	return FlatComponentPolicy::resolveVisualState(
		touchingDownID != TOUCH_UNTOUCHEDID,
		touchingID != TOUCH_UNTOUCHEDID || isFocused());
}

void FlatTextButton::onDraw()
{
	const FlatButtonVisual* visual = &style.normal;
	switch (getVisualState())
	{
	case FlatComponentPolicy::VisualState::Hovered:
		visual = &style.hovered;
		break;
	case FlatComponentPolicy::VisualState::Pressed:
		visual = &style.pressed;
		break;
	case FlatComponentPolicy::VisualState::Normal:
	default:
		break;
	}

	fillRectangle(engine, rect, visual->border);
	const int borderThickness = std::clamp(style.borderThickness, 0,
		std::max(0, std::min(rect.w, rect.h) / 2));
	Rect backgroundRect =
	{
		rect.x + borderThickness,
		rect.y + borderThickness,
		std::max(0, rect.w - borderThickness * 2),
		std::max(0, rect.h - borderThickness * 2)
	};
	fillRectangle(engine, backgroundRect, visual->background);
	if (image[0] != nullptr) Button::draw();

	const int textPadding = std::max(0, style.textPadding);
	label.rect =
	{
		rect.x + textPadding,
		rect.y + textPadding,
		std::max(1, rect.w - textPadding * 2),
		std::max(1, rect.h - textPadding * 2)
	};
	label.color = visual->textColor;
	label.autoShrink = true;
	label.horizontalAlignment = TextHorizontalAlignment::Center;
	label.verticalAlignment = TextVerticalAlignment::Center;
	label.onDraw();
}
