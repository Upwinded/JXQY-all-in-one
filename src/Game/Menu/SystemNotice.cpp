#include "SystemNotice.h"

#include "../../Engine/Engine.h"
#include "../../File/File.h"
#include "../../File/log.h"
#include "../../Component/TextLayout.h"

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
constexpr std::size_t MaximumScriptMessages = 15;
constexpr int ScriptLineHeight = NoticeFontSize + 3;
}

SystemNotice::SystemNotice(Mode mode) : mode(mode)
{
	name = mode == Mode::ScriptMessages ? "ScriptMessages" : "SystemNotice";
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
	if (mode == Mode::ScriptMessages)
	{
		onUpdate();
		if (messages.size() >= MaximumScriptMessages)
		{
			messages.erase(messages.begin());
		}
		messages.push_back({ message, getTime(), duration });
		refreshMessageText();
		return;
	}
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
	messages.clear();
	textImage = nullptr;
}

void SystemNotice::refreshMessageText()
{
	currentMessage.clear();
	for (const auto& message : messages)
	{
		if (!currentMessage.empty())
		{
			currentMessage += '\n';
		}
		currentMessage += message.text;
	}
	showing = visible = !messages.empty();
	refreshTextImage();
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
	if (mode == Mode::SingleNotice)
	{
		engine->fillRect(
			rect.x, rect.y, rect.w, rect.h,
			24, 27, 32, 232);
		engine->fillRect(
			rect.x, rect.y, rect.w, 1,
			154, 162, 174, 255);
		engine->fillRect(
			rect.x, rect.y + rect.h - 1, rect.w, 1,
			82, 88, 98, 255);
	}
	if (textImage == nullptr)
	{
		refreshTextImage();
	}
	int textWidth = 0;
	int textHeight = 0;
	if (textImage != nullptr &&
		engine->getImageSize(textImage, textWidth, textHeight))
	{
		if (mode == Mode::ScriptMessages)
		{
			engine->drawImage(textImage, rect.x, rect.y + rect.h - textHeight);
			return;
		}
		engine->drawImage(
			textImage,
			rect.x + (rect.w - textWidth) / 2,
			rect.y + (rect.h - textHeight) / 2);
	}
}

void SystemNotice::onUpdate()
{
	if (mode == Mode::ScriptMessages)
	{
		const auto previousSize = messages.size();
		const UTime now = getTime();
		messages.erase(std::remove_if(messages.begin(), messages.end(), [now](TimedMessage& message)
		{
			if (now < message.beginTime)
			{
				message.beginTime = now;
			}
			return now - message.beginTime >= message.duration;
		}), messages.end());
		if (messages.size() != previousSize)
		{
			refreshMessageText();
		}
		return;
	}
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
	const int availableWidth = std::max(1, width - HorizontalMargin * 2);
	const int noticeWidth = std::min(MaximumNoticeWidth, availableWidth);
	if (mode == Mode::ScriptMessages)
	{
		const int bottom = std::max(1, height - 110);
		const int top = std::min(bottom - 1, NoticeTop + NoticeHeight + 12);
		const int stackHeight = std::min(static_cast<int>(MaximumScriptMessages) * ScriptLineHeight, bottom - top);
		rect = { std::min(HorizontalMargin, std::max(0, width - 1)), bottom - stackHeight,
			availableWidth, stackHeight };
		refreshTextImage();
		return;
	}
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
	const int contentWidth = std::max(1, rect.w - NoticePadding * 2);
	if (mode == Mode::SingleNotice && currentMessage.find("<color=") == std::string::npos)
	{
		textImage = engine->createTextWithFontData(
			fontData.get(),
			static_cast<std::size_t>(fontLength),
			currentMessage,
			NoticeFontSize,
			NoticeTextColor,
			contentWidth);
		return;
	}

	std::vector<TextLayout::ColorTaggedTextLine> lines;
	const int capacity = TextLayout::charactersPerLineForWidth(contentWidth, NoticeFontSize);
	if (mode == Mode::ScriptMessages)
	{
		for (const auto& message : messages)
		{
			// Each message starts with the default color, including after an unclosed range tag.
			auto messageLines = TextLayout::wrapColorTaggedUtf8Text(message.text, capacity, NoticeTextColor);
			lines.insert(lines.end(), messageLines.begin(), messageLines.end());
		}
		const auto visibleLines = static_cast<std::size_t>(std::max(0, rect.h / ScriptLineHeight));
		if (lines.size() > visibleLines)
		{
			lines.erase(lines.begin(), lines.end() - visibleLines);
		}
	}
	else
	{
		lines = TextLayout::wrapColorTaggedUtf8Text(currentMessage, capacity, NoticeTextColor);
	}
	const int lineHeight = mode == Mode::ScriptMessages ? ScriptLineHeight : NoticeFontSize;
	std::vector<std::vector<_shared_image>> lineImages;
	std::vector<int> lineWidths;
	int imageWidth = 0;
	for (const auto& line : lines)
	{
		std::vector<_shared_image> runImages;
		int lineWidth = 0;
		for (const auto& run : line)
		{
			_shared_image runImage = engine->createTextWithFontData(
				fontData.get(),
				static_cast<std::size_t>(fontLength),
				run.text,
				NoticeFontSize,
				run.color,
				0);
			int runWidth = 0;
			int runHeight = 0;
			if (runImage != nullptr
				&& engine->getImageSize(runImage, runWidth, runHeight))
			{
				lineWidth += runWidth;
			}
			runImages.push_back(std::move(runImage));
		}
		imageWidth = std::max(imageWidth, lineWidth);
		lineWidths.push_back(lineWidth);
		lineImages.push_back(std::move(runImages));
	}
	if (mode == Mode::ScriptMessages)
	{
		imageWidth = std::min(imageWidth, rect.w);
	}
	if (imageWidth <= 0 || lineImages.empty()
		|| !engine->beginDrawTalk(
			imageWidth,
			std::max(1,
				static_cast<int>(lineImages.size()) * lineHeight)))
	{
		return;
	}
	for (std::size_t lineIndex = 0;
		lineIndex < lineImages.size(); ++lineIndex)
	{
		int drawX = mode == Mode::ScriptMessages ? 0 : (imageWidth - lineWidths[lineIndex]) / 2;
		for (const auto& runImage : lineImages[lineIndex])
		{
			int runWidth = 0;
			int runHeight = 0;
			if (runImage != nullptr
				&& engine->getImageSize(runImage, runWidth, runHeight))
			{
				engine->drawImage(
					runImage,
					drawX,
					static_cast<int>(lineIndex) * lineHeight);
				drawX += runWidth;
			}
		}
	}
	textImage = engine->endDrawTalk();
}
