#pragma once

#include "../../Types/ElementTypes.h"
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

// Geometry shared by the runtime defaults and the editor's unsaved default controls.
namespace OptionLayout
{
inline constexpr const char* Explicit = "explicit";
inline constexpr const char* ResourceFooter = "resource-footer";
struct Control
{
	const char* name;
	const char* type;
	const char* text;
	int font;
	unsigned int color;
	bool cheatPanel;
	bool dynamicText;
};

inline const std::array<Control, 15>& controls()
{
	static const std::array<Control, 15> values = {{
		{ "touchControlsButton", "FlatTextButton", u8"显示触控操作区：关", 16, 0xFFFFE7B0, false, true },
		{ "cheatSettingsButton", "FlatTextButton", u8"作弊设置", 16, 0xFFFFE7B0, false, false },
		{ "experienceMultiplierButton", "FlatTextButton", u8"经验×1", 16, 0xFFFFE7B0, false, true },
		{ "themeButton", "FlatTextButton", u8"界面：原版 · 重进游戏生效", 16, 0xFFFFE7B0, false, true },
		{ "automaticSaveButton", "FlatTextButton", u8"自动存档：开", 18, 0xFFFFE7B0, false, true },
		{ "cheatPanelBackground", "ImageContainer", "", 0, 0xFFFFFFFF, true, false },
		{ "cheatPanelTitle", "Label", u8"作弊设置（仅本次运行）", 18, 0xFFFFE7B0, true, false },
		{ "cheatResultLabel", "Label", u8"作弊模式当前未开启", 16, 0xFFFFFFFF, true, true },
		{ "cheatModeButton", "FlatTextButton", u8"作弊模式：关", 16, 0xFFFFE7B0, true, true },
		{ "invincibilityButton", "FlatTextButton", u8"无敌模式：关", 16, 0xFFFFE7B0, true, true },
		{ "restoreResourcesButton", "FlatTextButton", u8"补满生命、内力、体力", 16, 0xFFFFE7B0, true, false },
		{ "increaseMagicLevelButton", "FlatTextButton", u8"当前修炼武功提升一级", 16, 0xFFFFE7B0, true, false },
		{ "increasePlayerLevelButton", "FlatTextButton", u8"主角提升一级", 16, 0xFFFFE7B0, true, false },
		{ "addMoneyButton", "FlatTextButton", u8"增加100000两银子", 16, 0xFFFFE7B0, true, false },
		{ "cheatPanelBackButton", "FlatTextButton", u8"返回选项", 16, 0xFFFFE7B0, true, false }
	}};
	return values;
}

inline const Control* find(const std::string& name)
{
	for (const auto& control : controls()) if (name == control.name) return &control;
	return nullptr;
}

struct Geometry
{
	Rect rectangle{};
	int font = 0;
};

struct Footer
{
	std::map<std::string, Geometry> controls;
	int windowHeight = 0;
};

inline Footer footer(const Rect& window, const Rect* returnButton, int viewportHeight,
	bool touch, bool theme, bool automaticSave, const Rect* legacyCheat = nullptr)
{
	Footer result;
	result.windowHeight = window.h;
	Rect settings{};
	if (legacyCheat) settings = *legacyCheat;
	else
	{
		constexpr int height = 30, margin = 8, gap = 8, maximumWidth = 220;
		const int available = std::max(1, window.w - margin * 2);
		const int total = std::min(available, touch ? maximumWidth * 2 + gap : maximumWidth);
		const int width = touch ? std::max(1, (total - gap) / 2) : total;
		const int maximumY = window.y + window.h - height - margin;
		const int y = std::min(returnButton ? returnButton->y + returnButton->h + margin : maximumY, maximumY);
		int x = window.x + (window.w - total) / 2;
		if (touch)
		{
			result.controls["touchControlsButton"].rectangle = { x, y, width, height };
			x += width + gap;
		}
		settings = { x, y, window.x + (window.w + total) / 2 - x, height };
		if (!automaticSave)
		{
			if (theme)
			{
				Rect themeRect{ window.x + (window.w - 280) / 2, y + height + 8, 280, 30 };
				if (themeRect.y + themeRect.h > viewportHeight - 8 && returnButton
					&& returnButton->x - window.x >= themeRect.w + 16)
				{
					themeRect.x = returnButton->x - themeRect.w - 16;
					themeRect.y = returnButton->y + (returnButton->h - themeRect.h) / 2;
				}
				result.controls["themeButton"].rectangle = themeRect;
			}
		}
		else
		{
			int secondWidth = total, secondX = window.x + (window.w - total) / 2;
			int secondY = y + height + gap;
			if (secondY + height > viewportHeight - margin && returnButton)
			{
				secondWidth = std::min(secondWidth, returnButton->x - window.x - margin * 3);
				secondX = returnButton->x - secondWidth - margin * 2;
				secondY = returnButton->y + (returnButton->h - height) / 2;
			}
			const int secondButtonWidth = theme ? (secondWidth - gap) / 2 : secondWidth;
			const int font = secondButtonWidth < 180 ? 14 : 16;
			if (theme) result.controls["themeButton"] = {{ secondX, secondY, secondButtonWidth, height }, font};
			result.controls["automaticSaveButton"] = {{ secondX + (theme ? secondButtonWidth + gap : 0), secondY, secondButtonWidth, height }, font};
			result.windowHeight = std::max(window.h, secondY + height + margin - window.y);
		}
	}
	const int width = std::max(1, (settings.w - 8) / 2);
	result.controls["cheatSettingsButton"].rectangle = { settings.x, settings.y, width, settings.h };
	result.controls["experienceMultiplierButton"].rectangle = { settings.x + width + 8, settings.y, std::max(1, settings.w - width - 8), settings.h };
	return result;
}

inline Rect cheatBounds(const Rect& window)
{
	return { window.x + 8, window.y + 8, std::max(1, window.w - 16), std::max(1, window.h - 16) };
}

inline std::map<std::string, Geometry> cheat(const Rect& panel)
{
	std::map<std::string, Geometry> result;
	constexpr int height = 32, gap = 4, titleHeight = 24, resultHeight = 26;
	constexpr int contentHeight = titleHeight + 4 + resultHeight + 6 + 7 * height + 6 * gap;
	const int y = panel.y + std::max(4, (panel.h - contentHeight) / 2);
	const int width = std::min(380, std::max(1, panel.w - 24));
	const int x = panel.x + (panel.w - width) / 2;
	result["cheatPanelTitle"].rectangle = { x, y, width, titleHeight };
	result["cheatResultLabel"].rectangle = { x, y + titleHeight + 4, width, resultHeight };
	int buttonY = y + titleHeight + 4 + resultHeight + 6;
	for (const auto& control : controls())
	{
		if (!control.cheatPanel || std::string(control.type) != "FlatTextButton") continue;
		result[control.name].rectangle = { x, buttonY, width, height };
		buttonY += height + gap;
	}
	return result;
}
}
