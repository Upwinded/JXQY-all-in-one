#include "TitleTeam.h"
#include "../../Engine/Engine.h"
#include "../Menu/UIFocusManager.h"

#include "../../Component/TextLayout.h"

#include <algorithm>

namespace
{
constexpr int TextPanelWidth = 440;

unsigned int fadeInColor(unsigned int maximumAlpha, unsigned long elapsedMilliseconds)
{
	unsigned int alpha = maximumAlpha;
	if (elapsedMilliseconds < 1000)
	{
		alpha = static_cast<unsigned int>(
			static_cast<double>(elapsedMilliseconds) / 1000.0 * maximumAlpha);
	}
	return 0x00FFFFFFU | (alpha << 24);
}
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
	activePointer = TOUCH_UNTOUCHEDID;
}

bool TitleTeam::onInitial()
{
	freeResource();
	firstVisibleLine = 0;
	layoutWindowWidth = -1;
	layoutWindowHeight = -1;

	if (!videoFileName.empty())
	{
		vp = std::make_shared<VideoPage>("video\\" + videoFileName);
		addChild(vp);
	}
	updateTextLayout();
	initTime();
	return true;
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

	textFontSize = height < 540 ? 22 : 26;
	textLineGap = std::max(4, textFontSize / 4);
	const int leftMargin = videoFileName.empty()
		? std::max(24, width / 14)
		: std::max(20, width - TextPanelWidth + 30);
	const int rightMargin = std::max(20, width / 30);
	const int topMargin = std::max(28, height / 14);
	const int bottomControlsHeight = std::max(66, height / 7);
	textRect = {
		leftMargin,
		topMargin,
		std::max(1, width - leftMargin - rightMargin),
		std::max(1, height - topMargin - bottomControlsHeight)
	};
	closeRect = {
		std::max(10, width - rightMargin - 116),
		std::max(10, height - 48),
		116,
		34
	};
	wrappedTeamInfoLines = TextLayout::wrapUtf8Text(teamInfoText,
		TextLayout::charactersPerLineForWidth(textRect.w, textFontSize));
	visibleLineCount = TextLayout::visibleWrappedLineCount(
		static_cast<int>(wrappedTeamInfoLines.size()), textRect.h,
		textFontSize, textLineGap);
	const int maximumFirstLine = std::max(0,
		static_cast<int>(wrappedTeamInfoLines.size()) - visibleLineCount);
	firstVisibleLine = std::clamp(firstVisibleLine, 0, maximumFirstLine);
}

void TitleTeam::onDrawEnd()
{
	int width = 0;
	int height = 0;
	engine->getWindowSize(width, height);
	if (teamInfoText.empty())
	{
		return;
	}
	const int panelLeft =
		videoFileName.empty() ? 0 : std::max(0, width - TextPanelWidth);
	engine->fillRect(
		panelLeft, 0, width - panelLeft, height, 8, 10, 14, 245);

	const unsigned int color = fadeInColor(0xD0, getTime());
	updateTextLayout();
	const int endLine = std::min(static_cast<int>(wrappedTeamInfoLines.size()),
		firstVisibleLine + visibleLineCount);
	int y = textRect.y;
	for (int lineIndex = firstVisibleLine; lineIndex < endLine; ++lineIndex)
	{
		engine->drawText(wrappedTeamInfoLines[lineIndex], textRect.x, y,
			textFontSize, color);
		y += textFontSize + textLineGap;
	}

	if (wrappedTeamInfoLines.size() > static_cast<std::size_t>(visibleLineCount))
	{
		engine->drawText("↑/↓ 或滚轮滚动", textRect.x,
			closeRect.y + 7, 18, fadeInColor(0xA0, getTime()));
	}
	engine->fillRect(closeRect.x, closeRect.y, closeRect.w, closeRect.h,
		35, 43, 55, 220);
	engine->drawText("返回", closeRect.x + 35, closeRect.y + 6,
		20, color);
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
