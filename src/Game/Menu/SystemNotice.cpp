#include "SystemNotice.h"

#include "../../Engine/Engine.h"
#include "../../File/File.h"
#include "../../File/log.h"

#include <algorithm>

namespace
{
constexpr char ResourceFont[] = "font/font.ttf";
constexpr char EngineFont[] = "engine/font/font.ttf";
constexpr int MaximumBundledFontBytes = 32 * 1024 * 1024;
constexpr int HorizontalMargin = 16;
constexpr int MaximumNoticeWidth = 720;
constexpr int NoticeHeight = 72;
constexpr int NoticeTop = 20;
constexpr int NoticePadding = 14;
constexpr int NoticeFontSize = 18;
constexpr unsigned int NoticeTextColor = 0xFFF4F4F4;
}

SystemNotice::SystemNotice()
{
	name = "SystemNotice";
	visible = false;
	coverMouse = false;
	needEvents = false;
	setPriority(epMax);
	if (!File::readActiveResourceFile(
			ResourceFont,
			fontData,
			fontLength,
			MaximumBundledFontBytes) &&
		!File::readBundledApplicationFile(
			EngineFont,
			fontData,
			fontLength,
			MaximumBundledFontBytes))
	{
		GameLog::write(
			"SystemNotice: resource and engine fonts are unavailable\n");
	}
	int width = 0;
	int height = 0;
	if (engine != nullptr)
	{
		engine->getWindowSize(width, height);
	}
	updateLayout(width, height);
}

SystemNotice::~SystemNotice()
{
	textImage = nullptr;
	fontData.reset();
}

void SystemNotice::showMessage(
	const std::string& message, UTime duration)
{
	currentMessage = message;
	showDuration = duration;
	beginTime = getTime();
	showing = true;
	visible = true;
	refreshTextImage();
}

void SystemNotice::dismiss()
{
	showing = false;
	visible = false;
	currentMessage.clear();
	textImage = nullptr;
}

bool SystemNotice::hasFont() const
{
	return fontData != nullptr && fontLength > 0;
}

void SystemNotice::onDraw()
{
	if (engine == nullptr)
	{
		return;
	}
	engine->fillRect(
		rect.x, rect.y, rect.w, rect.h,
		24, 27, 32, 232);
	engine->fillRect(
		rect.x, rect.y, rect.w, 1,
		154, 162, 174, 255);
	engine->fillRect(
		rect.x, rect.y + rect.h - 1, rect.w, 1,
		82, 88, 98, 255);
	if (textImage == nullptr)
	{
		refreshTextImage();
	}
	int textWidth = 0;
	int textHeight = 0;
	if (textImage != nullptr &&
		engine->getImageSize(textImage, textWidth, textHeight))
	{
		engine->drawImage(
			textImage,
			rect.x + (rect.w - textWidth) / 2,
			rect.y + (rect.h - textHeight) / 2);
	}
}

void SystemNotice::onUpdate()
{
	if (showing && getTime() - beginTime > showDuration)
	{
		showing = false;
		visible = false;
	}
}

void SystemNotice::onWindowResize(int width, int height)
{
	updateLayout(width, height);
}

void SystemNotice::updateLayout(int width, int height)
{
	(void)height;
	const int availableWidth = std::max(1, width - HorizontalMargin * 2);
	const int noticeWidth = std::min(MaximumNoticeWidth, availableWidth);
	rect =
	{
		std::max(0, (width - noticeWidth) / 2),
		NoticeTop,
		noticeWidth,
		NoticeHeight
	};
	refreshTextImage();
}

void SystemNotice::refreshTextImage()
{
	textImage = nullptr;
	if (engine == nullptr || currentMessage.empty() || !hasFont())
	{
		return;
	}
	textImage = engine->createTextWithFontData(
		fontData.get(),
		static_cast<std::size_t>(fontLength),
		currentMessage,
		NoticeFontSize,
		NoticeTextColor,
		std::max(1, rect.w - NoticePadding * 2));
}
