#include "TitleTeam.h"
#include "../../Engine/Engine.h"
#include "../Menu/UIFocusManager.h"

#include "../../Component/TextLayout.h"
#include "../../Component/Panel.h"

#include <algorithm>
#include <sstream>
#include <limits>

namespace
{
int displayInteger(const INIReader& ini, const char* key, int fallback, int minimum = -100000, int maximum = 100000)
{
	const std::string text = ini.Get("Init", key, "");
	if (text.empty()) return fallback;
	try
	{
		std::size_t end = 0;
		const long value = std::stol(text, &end, 0);
		return text.find_first_not_of(" \t\r\n", end) == std::string::npos && value >= minimum && value <= maximum
			? static_cast<int>(value) : fallback;
	}
	catch (const std::exception&) { return fallback; }
}
unsigned int displayColor(INIReader& ini, const char* key, unsigned int fallback)
{
	const std::string text = ini.Get("Init", key, "");
	if (text.empty()) return fallback;
	try
	{
		std::istringstream stream(text); std::string part; int count = 0;
		const bool tuple = text.find(',') != std::string::npos;
		if (text.back() == ',') return fallback;
		while (std::getline(stream, part, ','))
		{
			const auto first = part.find_first_not_of(" \t\r\n");
			if (first == std::string::npos || part[first] == '-') return fallback;
			std::size_t end = 0; const auto value = std::stoul(part, &end, tuple ? 10 : 0);
			if (part.find_first_not_of(" \t\r\n", end) != std::string::npos || (tuple && value > 255)
				|| (!tuple && value > std::numeric_limits<unsigned int>::max())) return fallback;
			++count;
		}
		if (tuple ? count != 3 && count != 4 : count != 1) return fallback;
		return ini.GetColor("Init", key, fallback);
	}
	catch (const std::exception&) { return fallback; }
}
}

unsigned char TitleTeam::fadeInTextureAlpha(
	unsigned char maximumAlpha,
	unsigned long elapsedMilliseconds)
{
	unsigned int rasterAlpha = static_cast<unsigned int>(maximumAlpha);
	if (elapsedMilliseconds < 1000)
	{
		rasterAlpha = static_cast<unsigned int>(
			static_cast<double>(elapsedMilliseconds) / 1000.0 * maximumAlpha);
	}
	return static_cast<unsigned char>(
		rasterAlpha * rasterAlpha / 0xFFU);
}

TitleTeam::TitleTeam(
	const std::string& videoFileName,
	const std::string& teamInfoText)
	: videoFileName(videoFileName),
	teamInfoText(teamInfoText)
{
	drawFullScreen = true;
	canCallBack = true;
}

TitleTeam::~TitleTeam()
{
	freeResource();
}

void TitleTeam::freeResource()
{
	if (vp != nullptr)
	{
		vp->freeResource();
		vp = nullptr;
	}
	removeAllChild();
	wrappedTeamInfoLines.clear();
	teamInfoTextTextures.clear();
	scrollHintTextTexture.clear();
	closeTextTexture.clear();
	activePointer = TOUCH_UNTOUCHEDID;
}

bool TitleTeam::onInitial()
{
	freeResource();
	firstVisibleLine = 0;
	layoutWindowWidth = -1;
	layoutWindowHeight = -1;
	loadDisplayLayout();

	if (!videoFileName.empty())
	{
		vp = std::make_shared<VideoPage>("video\\" + videoFileName);
		addChild(vp);
	}
	updateTextLayout();
	initTime();
	return true;
}

void TitleTeam::loadDisplayLayout()
{
	for (auto& configuration : displayConfiguration) configuration = INIReader();
	if (teamInfoText.empty() || !File::fileExist(TitleTeamLayout::MenuFile)) return;
	INIReader menu(TitleTeamLayout::MenuFile);
	const std::string window = menu.Get("menu", "window", "");
	if (!window.empty()) displayConfiguration[0] = INIReader(window);
	for (int index = 1; ; ++index)
	{
		const std::string section = "component" + std::to_string(index);
		const std::string type = menu.Get(section, "type", "");
		if (type.empty()) break;
		const std::string name = menu.Get(section, "name", "");
		const int role = name == "body" && type == "Label" ? 1 : name == "scrollHint" && type == "Label" ? 2
			: name == "closeBtn" && type == "FlatTextButton" ? 3 : -1;
		const std::string file = menu.Get(section, "file", "");
		if (role > 0 && !file.empty()) displayConfiguration[role] = INIReader(file);
	}
}

void TitleTeam::updateTextLayout()
{
	if (teamInfoText.empty())
	{
		return;
	}

	int width = 0;
	int height = 0;
	engine->getWindowSize(width, height);
	if (width == layoutWindowWidth && height == layoutWindowHeight)
	{
		return;
	}
	layoutWindowWidth = width;
	layoutWindowHeight = height;

	displayLayout = TitleTeamLayout::defaults(width, height, !videoFileName.empty());
	Panel panel;
	panel.rect = displayLayout.panel;
	INIReader geometry;
	auto& window = displayConfiguration[0];
	geometry.SetInteger("Init", "Left", displayInteger(window, "Left", panel.rect.x));
	geometry.SetInteger("Init", "Top", displayInteger(window, "Top", panel.rect.y));
	geometry.SetInteger("Init", "Width", displayInteger(window, "Width", panel.rect.w, 1));
	geometry.SetInteger("Init", "Height", displayInteger(window, "Height", panel.rect.h, 1));
	geometry.Set("Init", "Align", window.Get("Init", "Align", "alNone"));
	geometry.SetInteger("Init", "AlignX", displayInteger(window, "AlignX", 0));
	geometry.SetInteger("Init", "AlignY", displayInteger(window, "AlignY", 0));
	panel.initFromIni(geometry);
	displayLayout = TitleTeamLayout::defaults(width, height, !videoFileName.empty(), &panel.rect);
	displayLayout.panelColor = displayColor(window, "BackgroundColor", displayLayout.panelColor);
	const auto rectangle = [](const INIReader& ini, Rect value)
	{
		return Rect{displayInteger(ini, "Left", value.x), displayInteger(ini, "Top", value.y),
			displayInteger(ini, "Width", value.w, 1), displayInteger(ini, "Height", value.h, 1)};
	};
	displayLayout.body = rectangle(displayConfiguration[1], displayLayout.body);
	displayLayout.close = rectangle(displayConfiguration[3], displayLayout.close);
	displayLayout.hint.x = displayInteger(displayConfiguration[2], "Left", displayLayout.body.x);
	displayLayout.hint.y = displayInteger(displayConfiguration[2], "Top", displayLayout.close.y + 7);
	textFontSize = displayInteger(displayConfiguration[1], "Font", displayLayout.font, 1, 256);
	textLineGap = displayInteger(displayConfiguration[1], "LineGap", std::max(4, textFontSize / 4), 0, 1024);
	displayLayout.color = displayColor(displayConfiguration[1], "Color", displayLayout.color);
	displayLayout.hintFont = displayInteger(displayConfiguration[2], "Font", displayLayout.hintFont, 1, 256);
	displayLayout.hintColor = displayColor(displayConfiguration[2], "Color", displayLayout.hintColor);
	displayLayout.closeFont = displayInteger(displayConfiguration[3], "Font", displayLayout.closeFont, 1, 256);
	displayLayout.closeColor = displayColor(displayConfiguration[3], "Color", displayLayout.closeColor);
	displayLayout.closeBackground = displayColor(displayConfiguration[3], "BackgroundColor", displayLayout.closeBackground);
	textRect = displayLayout.body;
	closeRect = displayLayout.close;
	textRect.x += panel.rect.x; textRect.y += panel.rect.y;
	closeRect.x += panel.rect.x; closeRect.y += panel.rect.y;
	wrappedTeamInfoLines = TextLayout::wrapUtf8Text(teamInfoText,
		TextLayout::charactersPerLineForWidth(textRect.w, textFontSize));
	teamInfoTextTextures.clear();
	teamInfoTextTextures.resize(wrappedTeamInfoLines.size());
	visibleLineCount = TextLayout::visibleWrappedLineCount(
		static_cast<int>(wrappedTeamInfoLines.size()), textRect.h,
		textFontSize, textLineGap);
	const int maximumFirstLine = std::max(0,
		static_cast<int>(wrappedTeamInfoLines.size()) - visibleLineCount);
	firstVisibleLine = std::clamp(firstVisibleLine, 0, maximumFirstLine);
}

void TitleTeam::onDrawEnd()
{
	if (teamInfoText.empty())
	{
		return;
	}
	updateTextLayout();
	const auto fill = [this](const Rect& rectangle, unsigned int color)
	{
		engine->fillRect(rectangle.x, rectangle.y, rectangle.w, rectangle.h,
			(color >> 16) & 255, (color >> 8) & 255, color & 255, (color >> 24) & 255);
	};
	fill(displayLayout.panel, displayLayout.panelColor);

	const unsigned long elapsedMilliseconds = getTime();
	const unsigned char textAlpha = fadeInTextureAlpha(
		0xD0, elapsedMilliseconds);
	const int endLine = std::min(static_cast<int>(wrappedTeamInfoLines.size()),
		firstVisibleLine + visibleLineCount);
	int y = textRect.y;
	for (int lineIndex = firstVisibleLine; lineIndex < endLine; ++lineIndex)
	{
		teamInfoTextTextures[static_cast<std::size_t>(lineIndex)].drawWithAlpha(
			engine,
			wrappedTeamInfoLines[static_cast<std::size_t>(lineIndex)],
			textRect.x,
			y,
			textFontSize,
			displayLayout.color,
			textAlpha);
		y += textFontSize + textLineGap;
	}

	if (wrappedTeamInfoLines.size() > static_cast<std::size_t>(visibleLineCount))
	{
		scrollHintTextTexture.drawWithAlpha(
			engine,
			"↑/↓ 或滚轮滚动",
			displayLayout.panel.x + displayLayout.hint.x,
			displayLayout.panel.y + displayLayout.hint.y,
			displayLayout.hintFont,
			displayLayout.hintColor,
			fadeInTextureAlpha(0xA0, elapsedMilliseconds));
	}
	fill(closeRect, displayLayout.closeBackground);
	closeTextTexture.drawWithAlpha(
		engine,
		"返回",
		closeRect.x + (closeRect.w - 2 * displayLayout.closeFont) / 2 - 3,
		closeRect.y + (closeRect.h - displayLayout.closeFont) / 2 - 1,
		displayLayout.closeFont,
		displayLayout.closeColor,
		textAlpha);
}

void TitleTeam::onChildCallBack(PElement child)
{
	if (child != nullptr && (child->getResult() & erVideoStopped) != 0)
	{
		logicRunning = false;
	}
}

void TitleTeam::onExit()
{
	freeResource();
}

void TitleTeam::scrollByLines(int delta)
{
	updateTextLayout();
	const int maximumFirstLine = std::max(0,
		static_cast<int>(wrappedTeamInfoLines.size()) - visibleLineCount);
	firstVisibleLine = std::clamp(firstVisibleLine + delta, 0, maximumFirstLine);
}

void TitleTeam::closePage()
{
	if (vp != nullptr && vp->v != nullptr)
	{
		engine->stopVideo(vp->v);
	}
	logicRunning = false;
}

bool TitleTeam::beginTextPointer(EventTouchID pointerId, int x, int y)
{
	if (teamInfoText.empty())
	{
		return false;
	}
	updateTextLayout();
	if (closeRect.PointInRect(x, y))
	{
		closePage();
		return true;
	}
	activePointer = pointerId;
	pointerStartY = y;
	pointerStartLine = firstVisibleLine;
	return true;
}

bool TitleTeam::updateTextPointer(EventTouchID pointerId, int y)
{
	if (activePointer != pointerId)
	{
		return false;
	}
	const int lineHeight = std::max(1, textFontSize + textLineGap);
	const int maximumFirstLine = std::max(0,
		static_cast<int>(wrappedTeamInfoLines.size()) - visibleLineCount);
	firstVisibleLine = std::clamp(
		pointerStartLine + (pointerStartY - y) / lineHeight,
		0, maximumFirstLine);
	return true;
}

bool TitleTeam::finishTextPointer(EventTouchID pointerId)
{
	if (activePointer != pointerId)
	{
		return false;
	}
	resetTextPointer();
	return true;
}

bool TitleTeam::onPointerInteractionCanceled(EventTouchID pointerID)
{
	if (activePointer != pointerID)
	{
		return false;
	}
	resetTextPointer();
	return true;
}

void TitleTeam::onAllPointerInteractionsCanceled()
{
	resetTextPointer();
}

void TitleTeam::resetTextPointer()
{
	activePointer = TOUCH_UNTOUCHEDID;
	pointerStartY = 0;
	pointerStartLine = firstVisibleLine;
}

bool TitleTeam::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_ESCAPE ||
			e.eventData == KEY_RETURN ||
			e.eventData == KEY_SPACE)
		{
			closePage();
			return true;
		}
		if (!teamInfoText.empty() && e.eventData == KEY_UP)
		{
			scrollByLines(-1);
			return true;
		}
		if (!teamInfoText.empty() && e.eventData == KEY_DOWN)
		{
			scrollByLines(1);
			return true;
		}
	}
	else if (!teamInfoText.empty() && e.eventType == ET_MOUSEWHEEL)
	{
		scrollByLines(e.eventData * 3);
		return true;
	}
	else if (e.eventType == ET_MOUSEDOWN && e.eventData == MBC_MOUSE_LEFT)
	{
		return beginTextPointer(TOUCH_MOUSEID, e.eventX, e.eventY);
	}
	else if (e.eventType == ET_MOUSEMOTION)
	{
		return updateTextPointer(TOUCH_MOUSEID, e.eventY);
	}
	else if (e.eventType == ET_MOUSEUP && e.eventData == MBC_MOUSE_LEFT)
	{
		return finishTextPointer(TOUCH_MOUSEID);
	}
	else if (e.eventType == ET_FINGERDOWN)
	{
		return beginTextPointer(e.eventData, e.eventX, e.eventY);
	}
	else if (e.eventType == ET_FINGERMOTION)
	{
		return updateTextPointer(e.eventData, e.eventY);
	}
	else if (e.eventType == ET_FINGERUP)
	{
		return finishTextPointer(e.eventData);
	}
	return false;
}

bool TitleTeam::onHandleUIAction(UIAction action)
{
	switch (action)
	{
	case UIAction::Cancel:
		closePage();
		return true;
	case UIAction::Confirm:
		closePage();
		return true;
	case UIAction::NavigateUp:
		if (!teamInfoText.empty())
		{
			scrollByLines(-1);
			return true;
		}
		return false;
	case UIAction::NavigateDown:
		if (!teamInfoText.empty())
		{
			scrollByLines(1);
			return true;
		}
		return false;
	default:
		return false;
	}
}

void TitleTeam::onUpdate()
{
	if (vp != nullptr &&
		(vp->v == nullptr || engine->getVideoStopped(vp->v)))
	{
		logicRunning = false;
	}
}
