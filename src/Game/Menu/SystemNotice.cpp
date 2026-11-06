#include "SystemNotice.h"

#include "../../Engine/Engine.h"
#include "../../File/File.h"
#include "../../File/log.h"
#include "../../Component/TextLayout.h"
#include "../../Component/Panel.h"
#include "../../Resource/FontCatalog.h"
#include "../Config/Config.h"

#include <algorithm>

namespace
{
constexpr std::size_t MaximumScriptMessages = 15;
}

SystemNotice::SystemNotice(Mode mode) : mode(mode)
{
	name = mode == Mode::ScriptMessages ? "ScriptMessages" : "SystemNotice";
	visible = false;
	coverMouse = false;
	needEvents = false;
	setPriority(epMax);
	auto selectedFont = ResourceFonts::loadGameFont(Config::fontId);
	fontData = std::move(selectedFont.data);
	fontLength = selectedFont.length;
	fallbackFontData = std::move(selectedFont.fallback);
	fallbackFontLength = selectedFont.fallbackLength;
	if (!fontData || fontLength <= 0)
	{
		GameLog::write(
			"SystemNotice: resource and engine fonts are unavailable\n");
	}
	loadDisplayLayout();
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
			(displayLayout.background >> 16) & 255, (displayLayout.background >> 8) & 255, displayLayout.background & 255, displayLayout.background >> 24);
		engine->fillRect(
			rect.x, rect.y, rect.w, 1,
			(displayLayout.topBorder >> 16) & 255, (displayLayout.topBorder >> 8) & 255, displayLayout.topBorder & 255, displayLayout.topBorder >> 24);
		engine->fillRect(
			rect.x, rect.y + rect.h - 1, rect.w, 1,
			(displayLayout.bottomBorder >> 16) & 255, (displayLayout.bottomBorder >> 8) & 255, displayLayout.bottomBorder & 255, displayLayout.bottomBorder >> 24);
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

void SystemNotice::loadDisplayLayout()
{
    const auto file=SystemNoticeLayout::menuFile(mode==Mode::ScriptMessages);
    if(!File::fileExist(file)) return;
    INIReader menu(file);
    const auto window=menu.Get("menu","window","");
    if(!window.empty()) windowConfiguration=INIReader(window);
    bool found=false;
    for(int index=1;;++index)
    {
        const auto section="component"+std::to_string(index),type=menu.Get(section,"type","");
        if(type.empty()) break;
        if(menu.Get(section,"name","")!="text") continue;
        if(found||type!="Label") {GameLog::write("SystemNotice: invalid fixed text role in %s\n",file);continue;}
        found=true;const auto reference=menu.Get(section,"file","");
        if(!reference.empty()) textConfiguration=INIReader(reference);
    }
}

void SystemNotice::updateLayout(int width, int height)
{
    const bool script=mode==Mode::ScriptMessages;
    const int font=SystemNoticeLayout::integer(textConfiguration.Get("Init","Font",""),18,1,256);
    const int gap=script?SystemNoticeLayout::integer(textConfiguration.Get("Init","LineGap",""),3,0,1024):3;
    displayLayout=SystemNoticeLayout::defaults(width,height,script,font,gap);
    displayLayout.padding=SystemNoticeLayout::integer(textConfiguration.Get("Init","HorizontalPadding",""),14,0);
    displayLayout.color=textConfiguration.GetColor("Init","Color",displayLayout.color);
    displayLayout.background=windowConfiguration.GetColor("Init","BackgroundColor",displayLayout.background);
    displayLayout.topBorder=windowConfiguration.GetColor("Init","TopBorderColor",displayLayout.topBorder);
    displayLayout.bottomBorder=windowConfiguration.GetColor("Init","BottomBorderColor",displayLayout.bottomBorder);
    Panel panel;INIReader geometry;
    for(const auto& field:std::vector<std::pair<const char*,int>>{{"Left",displayLayout.panel.x},{"Top",displayLayout.panel.y},{"Width",displayLayout.panel.w},{"Height",displayLayout.panel.h},{"AlignX",0},{"AlignY",0}})
        geometry.SetInteger("Init",field.first,SystemNoticeLayout::integer(windowConfiguration.Get("Init",field.first,""),field.second,field.first==std::string("Width")||field.first==std::string("Height")?1:-100000));
    geometry.Set("Init","Align",windowConfiguration.Get("Init","Align","alNone"));
    panel.initFromIni(geometry);rect=panel.rect;displayLayout.panel=rect;
    refreshTextImage();
}

void SystemNotice::refreshTextImage()
{
	textImage = nullptr;
	if (engine == nullptr || currentMessage.empty() || !hasFont())
	{
		return;
	}
	const int contentWidth = displayLayout.contentWidth();
	if (mode == Mode::SingleNotice && currentMessage.find("<color=") == std::string::npos)
	{
		textImage = engine->createTextWithFontData(
			fontData.get(),
			static_cast<std::size_t>(fontLength),
			currentMessage,
			displayLayout.font,
			displayLayout.color,
			contentWidth, fallbackFontData.get(), static_cast<std::size_t>(fallbackFontLength));
		return;
	}

    std::vector<std::string> texts;
    if(mode==Mode::ScriptMessages) for(const auto& message:messages) texts.push_back(message.text);
    else texts.push_back(currentMessage);
    const auto lines=displayLayout.lines(texts,mode==Mode::ScriptMessages);
    const int lineHeight=displayLayout.lineHeight(mode==Mode::ScriptMessages);
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
				displayLayout.font,
				run.color,
				0, fallbackFontData.get(), static_cast<std::size_t>(fallbackFontLength));
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
