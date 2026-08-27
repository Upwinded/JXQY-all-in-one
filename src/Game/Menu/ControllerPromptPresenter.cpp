#include "ControllerPromptPresenter.h"

#include "../../Engine/Engine.h"

ControllerPromptDrawOptions ControllerPromptPresenter::bottomBarOptions(
	Engine* engine)
{
	ControllerPromptDrawOptions options;
	if (engine == nullptr)
	{
		return options;
	}

	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	if (windowWidth <= 0 || windowHeight <= 0)
	{
		return options;
	}

	options.width = std::min(std::max(0, windowWidth - 16), 960);
	options.x = std::max(8, (windowWidth - options.width) / 2);
	options.height = std::min(52, windowHeight);
	options.y = std::max(0, windowHeight - options.height - 8);
	options.fontSize = windowWidth < 720 ? 12 : 14;
	options.itemGap = windowWidth < 720 ? 8 : 12;
	return options;
}

void ControllerPromptPresenter::draw(
	Engine* engine,
	const GameInput::PhysicalInputManager& inputManager,
	const std::vector<ControllerPromptItem>& items,
	const ControllerPromptDrawOptions& options)
{
	if (engine == nullptr || !shouldPresentGamepadFocus(engine)
		|| options.width <= 0 || options.height <= 0 || options.fontSize <= 0)
	{
		return;
	}

	const ControllerPromptLayout promptLayout = layout(
		items, captureTheme(inputManager), options);
	if (promptLayout.lines.empty())
	{
		return;
	}

	const int barHeight = std::min(
		options.height,
		promptLayout.contentHeight
			+ std::max(0, options.verticalPadding) * 2);
	const int barY = options.y
		+ std::max(0, (options.height - barHeight) / 2);
	engine->fillRect(
		options.x,
		barY,
		options.width,
		barHeight,
		options.backgroundRed,
		options.backgroundGreen,
		options.backgroundBlue,
		options.backgroundAlpha);

	int textY = barY + std::max(
		0, (barHeight - promptLayout.contentHeight) / 2);
	const int horizontalPadding = std::clamp(
		std::max(0, options.horizontalPadding),
		0,
		std::max(0, (options.width - 1) / 2));
	for (const ControllerPromptLayoutLine& line : promptLayout.lines)
	{
		int textX = options.x + std::max(
			horizontalPadding,
			(options.width - line.width) / 2);
		textX = std::max(textX, options.x + horizontalPadding);
		for (const std::string& item : line.items)
		{
			engine->drawText(
				item,
				textX,
				textY,
				promptLayout.fontSize,
				options.textColor);
			textX += estimateTextWidth(item, promptLayout.fontSize)
				+ promptLayout.itemGap;
		}
		textY += promptLayout.lineHeight;
	}
}
