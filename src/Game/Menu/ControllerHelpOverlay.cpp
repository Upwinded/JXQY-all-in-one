#include "ControllerHelpOverlay.h"

#include "../../Engine/Engine.h"

#include <algorithm>

namespace
{
constexpr unsigned int TitleColor = 0xFFFFD88A;
constexpr unsigned int SectionColor = 0xFFFFC76A;
constexpr unsigned int TextColor = 0xFFF2F2F2;
constexpr unsigned int MutedTextColor = 0xFFBFC5CE;
constexpr const char* ControllerImagePath =
	"image/ui/controller_help/controller-help.png";
}

const std::vector<ControllerHelpOverlay::HelpLine>&
	ControllerHelpOverlay::worldHelpLines()
{
	using GameInput::InputAction;
	static const std::vector<ControllerHelpOverlay::HelpLine> lines =
	{
		{ InputAction::Move, "移动；轻推走、推到底跑" },
		{ InputAction::InteractPrimary, "交谈或主互动" },
		{ InputAction::InteractAlternate, "备用互动" },
		{ InputAction::AttackPrimary, "普通攻击" },
		{ InputAction::CastSkill1, "武功一" },
		{ InputAction::CastSkill2, "武功二" },
		{ InputAction::CastSkill3, "武功三" },
		{ InputAction::CastSkill4, "武功四" },
		{ InputAction::CastSkill5, "武功五" },
		{ InputAction::UseQuickItem1, "快捷物品一" },
		{ InputAction::UseQuickItem2, "快捷物品二" },
		{ InputAction::UseQuickItem3, "快捷物品三" },
		{ InputAction::Jump, "向摇杆方向跳跃" },
		{ InputAction::CycleInteractionTarget, "切换附近目标" },
		{ InputAction::ToggleMiniMap, "开关小地图" },
		{ InputAction::ToggleSit, "打坐或起身" },
		{ InputAction::OpenSystemMenu, "打开系统菜单" },
	};
	return lines;
}

const std::vector<ControllerHelpOverlay::HelpLine>&
	ControllerHelpOverlay::menuHelpLines()
{
	using GameInput::InputAction;
	static const std::vector<ControllerHelpOverlay::HelpLine> lines =
	{
		{ InputAction::NavigateUp, "十字键移动焦点" },
		{ InputAction::Confirm, "确认、使用、继续对话" },
		{ InputAction::Secondary, "拿起、交换或次操作" },
		{ InputAction::ShowDetails, "查看详情" },
		{ InputAction::Cancel, "返回或取消" },
		{ InputAction::PreviousPanel, "切换区域（LB / RB）" },
		{ InputAction::PreviousPage, "翻页（LT / RT）" },
		{ InputAction::ScrollUp, "右摇杆滚动或移动准星" },
		{ InputAction::OpenSettings, "系统设置" },
		{ InputAction::OpenMemo, "记事任务" },
		{ InputAction::OpenEquip, "装备" },
		{ InputAction::OpenGoods, "包裹" },
	};
	return lines;
}

ControllerHelpOverlay::ControllerHelpOverlay()
{
	name = "ControllerHelpOverlay";
	drawFullScreen = true;
	rectFullScreen = true;
	eventOccupied = true;
	coverMouse = true;
	needEvents = true;
	setPriority(epMax);

	if (engine != nullptr)
	{
		labelTheme = ControllerPromptPresenter::captureTheme(
			engine->inputActions());
		engine->getWindowSize(windowWidth, windowHeight);
	}
	updateLayout(windowWidth, windowHeight);
}

void ControllerHelpOverlay::dismiss()
{
	logicRunning = false;
}

bool ControllerHelpOverlay::onHandleEvent(AEvent& event)
{
	if (event.eventType != ET_KEYDOWN
		&& event.eventType != ET_FINGERDOWN)
	{
		return false;
	}
	dismiss();
	return true;
}

bool ControllerHelpOverlay::onHandleUIAction(UIAction action)
{
	(void)action;
	dismiss();
	return true;
}

void ControllerHelpOverlay::onDraw()
{
	if (engine == nullptr || windowWidth <= 0 || windowHeight <= 0)
	{
		return;
	}

	engine->fillRect(0, 0, windowWidth, windowHeight, 9, 12, 18, 255);
	const int outerMargin = std::max(12, std::min(windowWidth, windowHeight) / 28);
	engine->fillRect(
		outerMargin,
		outerMargin,
		std::max(1, windowWidth - outerMargin * 2),
		std::max(1, windowHeight - outerMargin * 2),
		25, 31, 43, 255);
	engine->fillRect(
		outerMargin,
		outerMargin,
		std::max(1, windowWidth - outerMargin * 2),
		2,
		218, 174, 89, 255);

	const bool useTwoColumns = windowWidth >= 640;
	const int titleFontSize = std::clamp(windowHeight / 20, 20, 30);
	const int textFontSize = useTwoColumns
		? std::clamp(windowHeight / 36, 13, 18)
		: std::clamp(windowHeight / 48, 11, 16);
	const int lineHeight = textFontSize + std::max(3, textFontSize / 3);
	const int contentLeft = outerMargin + std::max(14, outerMargin / 2);
	const int contentTop = outerMargin + titleFontSize + lineHeight * 2;

	engine->drawText("手柄操作说明", contentLeft,
		outerMargin + std::max(10, outerMargin / 3),
		titleFontSize, TitleColor);
	engine->drawText("游戏已暂停", contentLeft,
		outerMargin + std::max(10, outerMargin / 3) + titleFontSize + 4,
		std::max(12, textFontSize), MutedTextColor);

	if (windowWidth >= 1100 && ensureControllerImageLoaded())
	{
		drawControllerDiagram(
			contentLeft,
			contentTop,
			outerMargin,
			lineHeight);
	}
	else if (useTwoColumns)
	{
		const int columnGap = std::max(20, windowWidth / 28);
		const int columnWidth = std::max(
			1, (windowWidth - contentLeft * 2 - columnGap) / 2);
		drawSection("世界场景", worldHelpLines(),
			contentLeft, contentTop, textFontSize, lineHeight, TextColor);
		drawSection("菜单与剧情", menuHelpLines(),
			contentLeft + columnWidth + columnGap,
			contentTop, textFontSize, lineHeight, TextColor);
	}
	else
	{
		drawSection("世界场景", worldHelpLines(),
			contentLeft, contentTop, textFontSize, lineHeight, TextColor);
		const int secondSectionY = contentTop
			+ lineHeight * (static_cast<int>(worldHelpLines().size()) + 2);
		drawSection("菜单与剧情", menuHelpLines(),
			contentLeft, secondSectionY, textFontSize, lineHeight, TextColor);
	}

	const std::string footer = "按任意键、手柄按钮或轻触屏幕关闭说明";
	engine->drawText(
		footer,
		contentLeft,
		std::max(contentTop, windowHeight - outerMargin - lineHeight - 6),
		textFontSize,
		TitleColor);
}

void ControllerHelpOverlay::onWindowResize(int width, int height)
{
	updateLayout(width, height);
}

void ControllerHelpOverlay::drawSection(
	const std::string& title,
	const std::vector<HelpLine>& lines,
	int x,
	int y,
	int fontSize,
	int lineHeight,
	unsigned int textColor)
{
	if (engine == nullptr)
	{
		return;
	}
	engine->drawText(title, x, y, fontSize + 2, SectionColor);
	int lineY = y + lineHeight;
	for (const HelpLine& line : lines)
	{
		engine->drawText(formatLine(line), x, lineY, fontSize, textColor);
		lineY += lineHeight;
	}
}

bool ControllerHelpOverlay::ensureControllerImageLoaded()
{
	if (!controllerImageLoadAttempted)
	{
		controllerImageLoadAttempted = true;
		if (engine != nullptr)
		{
			controllerImage = engine->loadImageFromFile(ControllerImagePath);
		}
	}
	return controllerImage != nullptr;
}

void ControllerHelpOverlay::drawControllerDiagram(
	int contentLeft,
	int contentTop,
	int outerMargin,
	int lineHeight)
{
	if (engine == nullptr || controllerImage == nullptr)
	{
		return;
	}

	int sourceWidth = 0;
	int sourceHeight = 0;
	if (!engine->getImageSize(controllerImage, sourceWidth, sourceHeight)
		|| sourceWidth <= 0 || sourceHeight <= 0)
	{
		return;
	}

	const int footerY = std::max(
		contentTop,
		windowHeight - outerMargin - lineHeight - 6);
	const int maximumWidth = std::max(1, windowWidth - contentLeft * 2);
	const int maximumHeight = std::max(1, footerY - contentTop - 8);
	const int imageWidth = std::max(
		1,
		std::min(
			maximumWidth,
			maximumHeight * sourceWidth / sourceHeight));
	const int imageHeight = std::max(
		1, imageWidth * sourceHeight / sourceWidth);
	Rect imageDestination =
	{
		(windowWidth - imageWidth) / 2,
		contentTop + std::max(0, (maximumHeight - imageHeight) / 2),
		imageWidth,
		imageHeight,
	};
	engine->drawImage(controllerImage, nullptr, &imageDestination);
}

std::string ControllerHelpOverlay::formatLine(const HelpLine& line) const
{
	const std::string label = ControllerPromptPresenter::controlLabel(
		line.action, labelTheme);
	return label.empty()
		? line.description
		: "[" + label + "]  " + line.description;
}

void ControllerHelpOverlay::updateLayout(int width, int height)
{
	windowWidth = std::max(1, width);
	windowHeight = std::max(1, height);
	rect = { 0, 0, windowWidth, windowHeight };
}
