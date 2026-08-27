#pragma once

#include "../../Input/ControllerBindingCatalog.h"
#include "../../Input/PhysicalInputManager.h"
#include "UIFocusManager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

class Engine;

struct ControllerPromptItem
{
	GameInput::InputAction action = GameInput::InputAction::Count;
	std::string description;
	std::vector<GameInput::InputAction> alternativeActions;
};

struct ControllerPromptLabelTheme
{
	SDL_GamepadButtonLabel south = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
	SDL_GamepadButtonLabel east = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
	SDL_GamepadButtonLabel west = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
	SDL_GamepadButtonLabel north = SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
};

struct ControllerPromptDrawOptions
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 28;
	int fontSize = 16;
	int horizontalPadding = 8;
	int verticalPadding = 4;
	int itemGap = 14;
	unsigned int textColor = 0xFFFFFFFF;
	unsigned char backgroundRed = 0;
	unsigned char backgroundGreen = 0;
	unsigned char backgroundBlue = 0;
	unsigned char backgroundAlpha = 150;
};

struct ControllerPromptLayoutLine
{
	std::vector<std::string> items;
	int width = 0;
};

struct ControllerPromptLayout
{
	std::vector<ControllerPromptLayoutLine> lines;
	int contentWidth = 0;
	int fontSize = 0;
	int itemGap = 0;
	int lineHeight = 0;
	int contentHeight = 0;
	bool truncated = false;
};

enum class ControllerPromptOwnerPolicy
{
	CurrentRunOwner,
	ActiveNonModalOwner,
};

// Formats semantic actions from their fixed spatial bindings. Menus provide only
// the action meaning and description; device-family labels stay centralized here.
class ControllerPromptPresenter
{
public:
	enum class LayoutFamily
	{
		Xbox,
		PlayStation,
		Nintendo,
	};

	static bool canPresentForOwner(
		const Element* owner,
		ControllerPromptOwnerPolicy policy)
	{
		if (owner == nullptr)
		{
			return false;
		}
		if (Element::isCurrentRunOwner(owner))
		{
			return true;
		}
		return policy == ControllerPromptOwnerPolicy::ActiveNonModalOwner
			&& !Element::currentRunOwnerBlocksParentInput();
	}

	static ControllerPromptLabelTheme captureTheme(
		const GameInput::PhysicalInputManager& inputManager)
	{
		return
		{
			inputManager.presentationButtonLabel(SDL_GAMEPAD_BUTTON_SOUTH),
			inputManager.presentationButtonLabel(SDL_GAMEPAD_BUTTON_EAST),
			inputManager.presentationButtonLabel(SDL_GAMEPAD_BUTTON_WEST),
			inputManager.presentationButtonLabel(SDL_GAMEPAD_BUTTON_NORTH),
		};
	}

	static LayoutFamily detectLayout(const ControllerPromptLabelTheme& theme)
	{
		const std::array<SDL_GamepadButtonLabel, 4> labels =
		{
			theme.south,
			theme.east,
			theme.west,
			theme.north,
		};
		if (std::any_of(labels.begin(), labels.end(),
			[](SDL_GamepadButtonLabel label)
			{
				return label == SDL_GAMEPAD_BUTTON_LABEL_CROSS
					|| label == SDL_GAMEPAD_BUTTON_LABEL_CIRCLE
					|| label == SDL_GAMEPAD_BUTTON_LABEL_SQUARE
					|| label == SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE;
			}))
		{
			return LayoutFamily::PlayStation;
		}
		if (theme.south == SDL_GAMEPAD_BUTTON_LABEL_B
			&& theme.east == SDL_GAMEPAD_BUTTON_LABEL_A)
		{
			return LayoutFamily::Nintendo;
		}
		return LayoutFamily::Xbox;
	}

	static std::string controlLabel(
		GameInput::InputAction action,
		const ControllerPromptLabelTheme& theme)
	{
		const GameInput::ControllerActionBinding* binding =
			GameInput::defaultControllerBinding(action);
		if (binding == nullptr)
		{
			return std::string();
		}
		const LayoutFamily layout = detectLayout(theme);
		const bool combination = binding->modifier
			!= GameInput::ControllerControl::None;
		const std::string primary = controlTokenLabel(
			binding->primary,
			binding->direction,
			binding->showDirectionInPrompt,
			combination,
			theme,
			layout);
		if (!combination)
		{
			return primary;
		}
		const std::string modifier = controlTokenLabel(
			binding->modifier,
			GameInput::ControllerDirection::None,
			false,
			true,
			theme,
			layout);
		if (modifier.empty() || primary.empty())
		{
			return std::string();
		}
		return modifier + "+" + primary;
	}

	static std::vector<std::string> formatItems(
		const std::vector<ControllerPromptItem>& items,
		const ControllerPromptLabelTheme& theme)
	{
		std::vector<std::string> formattedItems;
		formattedItems.reserve(items.size());
		for (const ControllerPromptItem& item : items)
		{
			if (item.description.empty())
			{
				continue;
			}

			std::vector<std::string> labels;
			labels.reserve(item.alternativeActions.size() + 1);
			auto appendActionLabel = [&labels, &theme](GameInput::InputAction action)
			{
				const std::string label = controlLabel(action, theme);
				if (!label.empty()
					&& std::find(labels.begin(), labels.end(), label) == labels.end())
				{
					labels.push_back(label);
				}
			};
			appendActionLabel(item.action);
			for (GameInput::InputAction action : item.alternativeActions)
			{
				appendActionLabel(action);
			}
			if (labels.empty())
			{
				continue;
			}

			std::string combinedLabel;
			for (const std::string& label : labels)
			{
				if (!combinedLabel.empty())
				{
					combinedLabel += "/";
				}
				combinedLabel += label;
			}
			formattedItems.push_back(
				"[" + combinedLabel + "] " + item.description);
		}
		return formattedItems;
	}

	static std::string format(
		const std::vector<ControllerPromptItem>& items,
		const ControllerPromptLabelTheme& theme,
		const std::string& separator = "    ")
	{
		const std::vector<std::string> formattedItems = formatItems(items, theme);
		std::string result;
		for (const std::string& item : formattedItems)
		{
			if (!result.empty())
			{
				result += separator;
			}
			result += item;
		}
		return result;
	}

	static std::string format(
		const std::vector<ControllerPromptItem>& items,
		const GameInput::PhysicalInputManager& inputManager,
		const std::string& separator = "    ")
	{
		if (!inputManager.hasActiveGamepad())
		{
			return std::string();
		}
		return format(items, captureTheme(inputManager), separator);
	}

	static ControllerPromptLayout layout(
		const std::vector<ControllerPromptItem>& items,
		const ControllerPromptLabelTheme& theme,
		const ControllerPromptDrawOptions& options)
	{
		return layoutFormattedItems(formatItems(items, theme), options);
	}

	static const std::vector<ControllerPromptItem>& worldPromptItems()
	{
		static const std::vector<ControllerPromptItem> items =
		{
			{ GameInput::InputAction::Move, "移动" },
			{ GameInput::InputAction::InteractPrimary, "交谈/互动" },
			{ GameInput::InputAction::AttackPrimary, "攻击" },
			{ GameInput::InputAction::CastSkill1, "武功一" },
			{ GameInput::InputAction::OpenSystemMenu, "系统" }
		};
		return items;
	}

	static ControllerPromptDrawOptions bottomBarOptions(Engine* engine);

	static void drawBottomBar(
		Engine* engine,
		const GameInput::PhysicalInputManager& inputManager,
		const std::vector<ControllerPromptItem>& items)
	{
		draw(engine, inputManager, items, bottomBarOptions(engine));
	}

	static void draw(
		Engine* engine,
		const GameInput::PhysicalInputManager& inputManager,
		const std::vector<ControllerPromptItem>& items,
		const ControllerPromptDrawOptions& options);

private:
	static std::string controlTokenLabel(
		GameInput::ControllerControl control,
		GameInput::ControllerDirection direction,
		bool showDirection,
		bool combination,
		const ControllerPromptLabelTheme& theme,
		LayoutFamily layout)
	{
		using GameInput::ControllerControl;
		switch (control)
		{
		case ControllerControl::South:
			return faceLabel(theme.south, fallbackFaceLabel(layout,
				SDL_GAMEPAD_BUTTON_SOUTH));
		case ControllerControl::East:
			return faceLabel(theme.east, fallbackFaceLabel(layout,
				SDL_GAMEPAD_BUTTON_EAST));
		case ControllerControl::West:
			return faceLabel(theme.west, fallbackFaceLabel(layout,
				SDL_GAMEPAD_BUTTON_WEST));
		case ControllerControl::North:
			return faceLabel(theme.north, fallbackFaceLabel(layout,
				SDL_GAMEPAD_BUTTON_NORTH));
		case ControllerControl::Back:
			if (layout == LayoutFamily::PlayStation)
			{
				return "Create";
			}
			return layout == LayoutFamily::Nintendo ? "Minus" : "Back";
		case ControllerControl::Start:
			if (layout == LayoutFamily::PlayStation)
			{
				return "Options";
			}
			if (layout == LayoutFamily::Nintendo)
			{
				return combination ? "Plus" : "+";
			}
			return "Start";
		case ControllerControl::LeftShoulder:
			return leftShoulderLabel(layout);
		case ControllerControl::RightShoulder:
			return rightShoulderLabel(layout);
		case ControllerControl::LeftStickButton:
			return layout == LayoutFamily::Nintendo ? "LS" : "L3";
		case ControllerControl::RightStickButton:
			return layout == LayoutFamily::Nintendo ? "RS" : "R3";
		case ControllerControl::DPad:
		{
			std::string label = showDirection
				? "十字键"
				: (layout == LayoutFamily::PlayStation ? "方向键" : "十字键");
			if (showDirection)
			{
				label += directionLabel(direction);
			}
			return label;
		}
		case ControllerControl::LeftTrigger:
			return leftTriggerLabel(layout);
		case ControllerControl::RightTrigger:
			return rightTriggerLabel(layout);
		case ControllerControl::LeftStick:
			return "左摇杆";
		case ControllerControl::RightStick:
			return "右摇杆";
		case ControllerControl::None:
		default:
			return std::string();
		}
	}

	static std::string directionLabel(GameInput::ControllerDirection direction)
	{
		switch (direction)
		{
		case GameInput::ControllerDirection::Up:
			return "上";
		case GameInput::ControllerDirection::Down:
			return "下";
		case GameInput::ControllerDirection::Left:
			return "左";
		case GameInput::ControllerDirection::Right:
			return "右";
		case GameInput::ControllerDirection::None:
		default:
			return std::string();
		}
	}

	static ControllerPromptLayout layoutFormattedItems(
		const std::vector<std::string>& formattedItems,
		const ControllerPromptDrawOptions& options)
	{
		ControllerPromptLayout emptyLayout;
		if (formattedItems.empty() || options.width <= 0
			|| options.height <= 0 || options.fontSize <= 0)
		{
			return emptyLayout;
		}

		const int horizontalPadding = std::clamp(
			std::max(0, options.horizontalPadding),
			0,
			std::max(0, (options.width - 1) / 2));
		const int contentWidth = std::max(
			1, options.width - horizontalPadding * 2);
		const int maximumContentHeight = std::max(
			0, options.height - std::max(0, options.verticalPadding) * 2);
		const int minimumFontSize = std::min(options.fontSize, 10);
		const int maximumItemGap = std::max(0, options.itemGap);
		const int minimumItemGap = std::min(maximumItemGap, 4);
		ControllerPromptLayout bestFittingTruncatedLayout;
		bool hasFittingTruncatedLayout = false;

		for (int fontSize = options.fontSize;
			fontSize >= minimumFontSize; fontSize--)
		{
			for (int itemGap = maximumItemGap;
				itemGap >= minimumItemGap; itemGap--)
			{
				ControllerPromptLayout candidate = buildLayout(
					formattedItems, contentWidth, fontSize, itemGap);
				if (candidate.contentHeight <= maximumContentHeight
					&& !candidate.truncated)
				{
					return candidate;
				}
				if (candidate.contentHeight <= maximumContentHeight
					&& !hasFittingTruncatedLayout)
				{
					bestFittingTruncatedLayout = std::move(candidate);
					hasFittingTruncatedLayout = true;
				}
			}
		}
		if (hasFittingTruncatedLayout)
		{
			return bestFittingTruncatedLayout;
		}

		ControllerPromptLayout fallback = buildLayout(
			formattedItems,
			contentWidth,
			minimumFontSize,
			minimumItemGap);
		const int maximumLineCount = fallback.lineHeight <= 0
			? 0 : maximumContentHeight / fallback.lineHeight;
		if (maximumLineCount <= 0)
		{
			fallback.lines.clear();
			fallback.contentHeight = 0;
			fallback.truncated = true;
			return fallback;
		}
		if (static_cast<int>(fallback.lines.size()) > maximumLineCount)
		{
			fallback.lines.resize(static_cast<std::size_t>(maximumLineCount));
			ControllerPromptLayoutLine& overflowLine = fallback.lines.back();
			appendOverflowIndicator(overflowLine, fallback);
			fallback.contentHeight = maximumLineCount * fallback.lineHeight;
			fallback.truncated = true;
		}
		return fallback;
	}

	static void appendOverflowIndicator(
		ControllerPromptLayoutLine& line,
		const ControllerPromptLayout& layout)
	{
		const std::string ellipsis = "…";
		const int ellipsisWidth = estimateTextWidth(ellipsis, layout.fontSize);
		auto recalculateWidth = [&line, &layout]()
		{
			line.width = 0;
			for (const std::string& item : line.items)
			{
				if (line.width > 0)
				{
					line.width += layout.itemGap;
				}
				line.width += estimateTextWidth(item, layout.fontSize);
			}
		};

		while (line.items.size() > 1
			&& line.width + layout.itemGap + ellipsisWidth
				> layout.contentWidth)
		{
			line.items.pop_back();
			recalculateWidth();
		}
		if (!line.items.empty()
			&& line.width + layout.itemGap + ellipsisWidth
				> layout.contentWidth)
		{
			const int itemWidth = layout.contentWidth
				- layout.itemGap - ellipsisWidth;
			line.items.back() = truncateToWidth(
				line.items.back(), layout.fontSize, itemWidth);
			if (line.items.back().empty())
			{
				line.items.pop_back();
			}
			recalculateWidth();
		}
		if (line.items.empty())
		{
			line.items.push_back(ellipsis);
			line.width = ellipsisWidth;
			return;
		}
		line.items.push_back(ellipsis);
		line.width += layout.itemGap + ellipsisWidth;
	}

	static ControllerPromptLayout buildLayout(
		const std::vector<std::string>& formattedItems,
		int contentWidth,
		int fontSize,
		int itemGap)
	{
		ControllerPromptLayout result;
		result.contentWidth = contentWidth;
		result.fontSize = fontSize;
		result.itemGap = itemGap;
		result.lineHeight = fontSize + 2;
		for (const std::string& item : formattedItems)
		{
			const std::string visibleItem = truncateToWidth(
				item, fontSize, contentWidth);
			if (visibleItem.empty())
			{
				result.truncated = true;
				continue;
			}
			if (visibleItem != item)
			{
				result.truncated = true;
			}
			const int itemWidth = estimateTextWidth(visibleItem, fontSize);
			if (result.lines.empty())
			{
				result.lines.push_back({});
			}
			ControllerPromptLayoutLine* line = &result.lines.back();
			const int requiredWidth = line->width
				+ (line->items.empty() ? 0 : itemGap)
				+ itemWidth;
			if (!line->items.empty() && requiredWidth > contentWidth)
			{
				result.lines.push_back({});
				line = &result.lines.back();
			}
			if (!line->items.empty())
			{
				line->width += itemGap;
			}
			line->items.push_back(visibleItem);
			line->width += itemWidth;
		}
		result.contentHeight = static_cast<int>(result.lines.size())
			* result.lineHeight;
		return result;
	}

	static std::string truncateToWidth(
		const std::string& text,
		int fontSize,
		int maximumWidth)
	{
		if (estimateTextWidth(text, fontSize) <= maximumWidth)
		{
			return text;
		}
		const std::string ellipsis = "…";
		if (estimateTextWidth(ellipsis, fontSize) > maximumWidth)
		{
			return std::string();
		}

		std::string visible;
		for (std::size_t index = 0; index < text.size();)
		{
			const std::size_t characterLength = utf8CharacterLength(
				static_cast<unsigned char>(text[index]));
			const std::size_t nextIndex = std::min(
				text.size(), index + characterLength);
			const std::string candidate = visible
				+ text.substr(index, nextIndex - index) + ellipsis;
			if (estimateTextWidth(candidate, fontSize) > maximumWidth)
			{
				break;
			}
			visible.append(text, index, nextIndex - index);
			index = nextIndex;
		}
		return visible + ellipsis;
	}

	static std::size_t utf8CharacterLength(unsigned char byte)
	{
		if ((byte & 0x80) == 0)
		{
			return 1;
		}
		if ((byte & 0xE0) == 0xC0)
		{
			return 2;
		}
		if ((byte & 0xF0) == 0xE0)
		{
			return 3;
		}
		if ((byte & 0xF8) == 0xF0)
		{
			return 4;
		}
		return 1;
	}

	static std::string faceLabel(
		SDL_GamepadButtonLabel label,
		const std::string& fallback)
	{
		switch (label)
		{
		case SDL_GAMEPAD_BUTTON_LABEL_A:
			return "A";
		case SDL_GAMEPAD_BUTTON_LABEL_B:
			return "B";
		case SDL_GAMEPAD_BUTTON_LABEL_X:
			return "X";
		case SDL_GAMEPAD_BUTTON_LABEL_Y:
			return "Y";
		case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
			return "×";
		case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
			return "○";
		case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
			return "□";
		case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
			return "△";
		case SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN:
		default:
			return fallback;
		}
	}

	static std::string fallbackFaceLabel(
		LayoutFamily layout,
		SDL_GamepadButton button)
	{
		if (layout == LayoutFamily::PlayStation)
		{
			switch (button)
			{
			case SDL_GAMEPAD_BUTTON_SOUTH:
				return "×";
			case SDL_GAMEPAD_BUTTON_EAST:
				return "○";
			case SDL_GAMEPAD_BUTTON_WEST:
				return "□";
			case SDL_GAMEPAD_BUTTON_NORTH:
				return "△";
			default:
				break;
			}
		}
		if (layout == LayoutFamily::Nintendo)
		{
			switch (button)
			{
			case SDL_GAMEPAD_BUTTON_SOUTH:
				return "B";
			case SDL_GAMEPAD_BUTTON_EAST:
				return "A";
			case SDL_GAMEPAD_BUTTON_WEST:
				return "Y";
			case SDL_GAMEPAD_BUTTON_NORTH:
				return "X";
			default:
				break;
			}
		}
		switch (button)
		{
		case SDL_GAMEPAD_BUTTON_SOUTH:
			return "A";
		case SDL_GAMEPAD_BUTTON_EAST:
			return "B";
		case SDL_GAMEPAD_BUTTON_WEST:
			return "X";
		case SDL_GAMEPAD_BUTTON_NORTH:
			return "Y";
		default:
			return std::string();
		}
	}

	static std::string leftShoulderLabel(LayoutFamily layout)
	{
		if (layout == LayoutFamily::PlayStation)
		{
			return "L1";
		}
		return layout == LayoutFamily::Nintendo ? "L" : "LB";
	}

	static std::string rightShoulderLabel(LayoutFamily layout)
	{
		if (layout == LayoutFamily::PlayStation)
		{
			return "R1";
		}
		return layout == LayoutFamily::Nintendo ? "R" : "RB";
	}

	static std::string leftTriggerLabel(LayoutFamily layout)
	{
		if (layout == LayoutFamily::PlayStation)
		{
			return "L2";
		}
		return layout == LayoutFamily::Nintendo ? "ZL" : "LT";
	}

	static std::string rightTriggerLabel(LayoutFamily layout)
	{
		if (layout == LayoutFamily::PlayStation)
		{
			return "R2";
		}
		return layout == LayoutFamily::Nintendo ? "ZR" : "RT";
	}

	static int estimateTextWidth(const std::string& text, int fontSize)
	{
		double width = 0.0;
		for (std::size_t index = 0; index < text.size();)
		{
			const unsigned char byte = static_cast<unsigned char>(text[index]);
			if ((byte & 0x80) == 0)
			{
				width += static_cast<double>(fontSize) * 0.58;
				index++;
			}
			else
			{
				width += fontSize;
				if ((byte & 0xE0) == 0xC0)
				{
					index += std::min<std::size_t>(2, text.size() - index);
				}
				else if ((byte & 0xF0) == 0xE0)
				{
					index += std::min<std::size_t>(3, text.size() - index);
				}
				else if ((byte & 0xF8) == 0xF0)
				{
					index += std::min<std::size_t>(4, text.size() - index);
				}
				else
				{
					index++;
				}
			}
		}
		return std::max(1, static_cast<int>(std::ceil(width)));
	}
};
