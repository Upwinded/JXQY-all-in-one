#include "PaperMenu.h"

#include "../../Engine/Engine.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr unsigned int InkColor = 0xFF30271D;
}

PaperMenu::PaperMenu()
{
	name = "PaperMenu";
	visible = false;
	rectFullScreen = true;
	eventOccupied = true;
	canCallBack = true;
	setPriority(epMax + 3);
	for (auto* button : { &previousButton, &nextButton, &closeButton })
	{
		*button = std::make_shared<FlatTextButton>();
		(*button)->setFontSize(18);
		addChild(*button);
	}
	previousButton->setUTF8Str("上一页");
	closeButton->setUTF8Str("关闭");
}

void PaperMenu::open(int type, const std::vector<std::string>& contents)
{
	if (visible || type < 1 || type > 3 || contents.empty())
	{
		return;
	}
	// ponytail: one active reader; separate overlapping type windows only if a MOD needs them.
	paperType = type;
	sourcePages = type == 1 ? std::vector<std::string>{ contents.front() } : contents;
	pages.clear();
	pageIndex = 0;
	int width = 800;
	int height = 600;
	if (engine != nullptr) engine->getWindowSize(width, height);
	layout(width, height);
	visible = true;
}

void PaperMenu::close()
{
	if (!visible) return;
	cancelPointerInteraction();
	visible = false;
	sourcePages.clear();
	pages.clear();
	textTextures.clear();
	pageImage.reset();
	if (engine != nullptr) engine->releasePhysicalInputsForContextTransition();
}

void PaperMenu::layout(int width, int height)
{
	const std::size_t sourceIndex = pages.empty() ? 0 : pages[pageIndex].sourceIndex;
	rect = { 0, 0, std::max(1, width), std::max(1, height) };
	const int margin = std::min(12, std::min(rect.w, rect.h) / 20);
	paperRect.w = std::max(1, std::min(860, rect.w - margin * 2));
	paperRect.h = std::max(1, std::min(760, rect.h - margin * 2));
	paperRect.x = (rect.w - paperRect.w) / 2;
	paperRect.y = (rect.h - paperRect.h) / 2;
	const int padding = std::min(24, paperRect.w / 16);
	fontSize = std::clamp(paperRect.h / 28, 18, 24);
	lineHeight = fontSize + 7;
	contentRect = { paperRect.x + padding, paperRect.y + 58,
		std::max(1, paperRect.w - padding * 2), std::max(lineHeight, paperRect.h - 166) };
	const int buttonGap = 8;
	const int buttonWidth = std::max(1, (contentRect.w - buttonGap * 2) / 3);
	int buttonX = contentRect.x;
	for (const auto& button : { previousButton, nextButton, closeButton })
	{
		button->rect = { buttonX, paperRect.y + paperRect.h - 92, buttonWidth, 44 };
		buttonX += buttonWidth + buttonGap;
	}
	pages.clear();
	const int linesPerPage = std::max(1, contentRect.h / lineHeight);
	for (std::size_t index = 0; index < sourcePages.size(); ++index)
	{
		auto lines = paperType == 2 ? std::vector<TextLayout::ColorTaggedTextLine>{}
			: TextLayout::wrapColorTaggedUtf8Text(sourcePages[index],
				TextLayout::charactersPerLineForWidth(contentRect.w, fontSize), InkColor);
		if (lines.empty()) lines.emplace_back();
		for (std::size_t start = 0; start < lines.size(); start += linesPerPage)
		{
			const auto end = std::min(lines.size(), start + linesPerPage);
			pages.push_back({ index, { lines.begin() + start, lines.begin() + end } });
		}
	}
	pageIndex = 0;
	while (pageIndex + 1 < pages.size() && pages[pageIndex].sourceIndex < sourceIndex)
	{
		++pageIndex;
	}
	selectPage(pageIndex);
}

void PaperMenu::selectPage(std::size_t index)
{
	if (pages.empty()) return;
	pageIndex = std::min(index, pages.size() - 1);
	textTextures.clear();
	pageImage.reset();
	if (paperType == 2)
	{
		const auto image = IMP::createIMPImageFromFile(
			"asf\\chinesebook\\" + sourcePages[pages[pageIndex].sourceIndex]);
		pageImage = IMP::loadImage(image, 0);
	}
	nextButton->setUTF8Str(pageIndex + 1 == pages.size() ? "读完" : "下一页");
}

void PaperMenu::advance()
{
	if (pageIndex + 1 < pages.size()) selectPage(pageIndex + 1);
	else close();
}

bool PaperMenu::onHandleUIAction(UIAction action)
{
	if (!visible) return false;
	switch (action)
	{
	case UIAction::NavigateLeft:
	case UIAction::PagePrevious:
	case UIAction::ScrollUp:
	case UIAction::ScrollLeft:
		if (pageIndex > 0) selectPage(pageIndex - 1);
		break;
	case UIAction::NavigateRight:
	case UIAction::PageNext:
	case UIAction::ScrollDown:
	case UIAction::ScrollRight:
		if (pageIndex + 1 < pages.size()) selectPage(pageIndex + 1);
		break;
	case UIAction::Confirm:
		advance();
		break;
	case UIAction::Cancel:
		close();
		break;
	default:
		break;
	}
	return true;
}

bool PaperMenu::onHandleEvent(AEvent& event)
{
	if (event.eventType == ET_KEYDOWN)
	{
		if (event.eventData == SDL_SCANCODE_PAGEUP || event.eventData == SDL_SCANCODE_PAGEDOWN)
		{
			return handleUIAction(event.eventData == SDL_SCANCODE_PAGEUP
				? UIAction::PagePrevious : UIAction::PageNext);
		}
		return dispatchKeyboardUIAction(event, *this) || visible;
	}
	return false;
}

void PaperMenu::onChildCallBack(PElement child)
{
	if (child == nullptr || !child->getResult(erClick)) return;
	if (child == previousButton) handleUIAction(UIAction::PagePrevious);
	else if (child == nextButton) advance();
	else if (child == closeButton) close();
}

void PaperMenu::onWindowResize(int width, int height)
{
	layout(width, height);
}

void PaperMenu::onDraw()
{
	if (engine == nullptr || pages.empty()) return;
	engine->fillRect(rect.x, rect.y, rect.w, rect.h, 10, 9, 7, 185);
	engine->fillRect(paperRect.x, paperRect.y, paperRect.w, paperRect.h, 126, 99, 60, 255);
	engine->fillRect(paperRect.x + 2, paperRect.y + 2, paperRect.w - 4, paperRect.h - 4,
		242, 227, 192, 255);
	titleTexture.draw(engine, "书信", contentRect.x, paperRect.y + 18, 24, InkColor);
	counterTexture.draw(engine, std::to_string(pageIndex + 1) + " / " + std::to_string(pages.size()),
		contentRect.x + std::max(80, contentRect.w - 100), paperRect.y + 22, 18, InkColor);
	if (paperType == 2)
	{
		int imageWidth = 0;
		int imageHeight = 0;
		if (pageImage != nullptr && engine->getImageSize(pageImage, imageWidth, imageHeight)
			&& imageWidth > 0 && imageHeight > 0)
		{
			const double scale = std::min(static_cast<double>(contentRect.w) / imageWidth,
				static_cast<double>(contentRect.h) / imageHeight);
			Rect destination = { 0, 0, std::max(1, static_cast<int>(imageWidth * scale)),
				std::max(1, static_cast<int>(imageHeight * scale)) };
			destination.x = contentRect.x + (contentRect.w - destination.w) / 2;
			destination.y = contentRect.y + (contentRect.h - destination.h) / 2;
			engine->drawImage(pageImage, nullptr, &destination);
		}
		else
		{
			textTextures.resize(1);
			textTextures[0].draw(engine, "图片未能读取", contentRect.x, contentRect.y, fontSize, InkColor);
		}
		return;
	}
	std::size_t runCount = 0;
	for (const auto& line : pages[pageIndex].lines) runCount += line.size();
	textTextures.resize(runCount);
	std::size_t textureIndex = 0;
	int y = contentRect.y;
	for (const auto& line : pages[pageIndex].lines)
	{
		int width = 0;
		for (std::size_t index = 0; index < line.size(); ++index)
		{
			int runWidth = 0, runHeight = 0;
			const auto& run = line[index];
			engine->getImageSize(textTextures[textureIndex + index].get(engine, run.text, fontSize, run.color),
				runWidth, runHeight);
			width += runWidth;
		}
		const double scale = std::min(1.0, static_cast<double>(contentRect.w) / std::max(1, width));
		int x = contentRect.x;
		for (const auto& run : line)
		{
			const auto& image = textTextures[textureIndex++].get(engine, run.text, fontSize, run.color);
			int runWidth = 0, runHeight = 0;
			engine->getImageSize(image, runWidth, runHeight);
			Rect destination = { x, y, static_cast<int>(std::floor(runWidth * scale)), runHeight };
			engine->drawImage(image, nullptr, &destination);
			x += destination.w;
		}
		y += lineHeight;
	}
}

void PaperMenu::onDrawEnd()
{
	if (engine == nullptr || !shouldPresentGamepadFocus(engine)
		|| Element::currentRunOwnerBlocksParentInput()) return;
	ControllerPromptDrawOptions options;
	options.x = contentRect.x;
	options.y = paperRect.y + paperRect.h - 38;
	options.width = contentRect.w;
	options.height = 28;
	options.fontSize = 13;
	using GameInput::InputAction;
	ControllerPromptPresenter::draw(engine, engine->inputActions(),
		{ { InputAction::PreviousPage, "翻页", { InputAction::NextPage } },
		  { InputAction::Confirm, pageIndex + 1 == pages.size() ? "读完" : "下一页" },
		  { InputAction::Cancel, "关闭" } }, options, promptTextures);
}
