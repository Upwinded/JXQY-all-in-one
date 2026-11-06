#include "ControllerHelpOverlay.h"

#include "../../Engine/Engine.h"
#include "../../File/INIReader.h"

#include <algorithm>

namespace
{
void fillHelpRectangle(Engine* engine,const Rect& rectangle,unsigned int color)
{
    engine->fillRect(rectangle.x,rectangle.y,rectangle.w,rectangle.h,(color>>16)&255,(color>>8)&255,color&255,(color>>24)&255);
}
}

const std::vector<ControllerHelpOverlay::HelpLine>& ControllerHelpOverlay::worldHelpLines()
{
    return ControllerHelpContent::world();
}

const std::vector<ControllerHelpOverlay::HelpLine>& ControllerHelpOverlay::menuHelpLines()
{
    return ControllerHelpContent::menu();
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
    std::unique_ptr<char[]> bytes;
    if(File::readFile(ControllerHelpLayout::File,bytes)>0&&bytes)
    {
        INIReader ini(bytes);
        if(ini.ParseError()==0)
            for(const auto& key:ControllerHelpLayout::keys())display.values[key]=ini.Get("Init",key,"");
        else GameLog::write("ControllerHelp layout parse error %d; using defaults",ini.ParseError());
        for(const auto& key:display.invalid())GameLog::write("ControllerHelp layout field %s invalid; using default",key.c_str());
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
    if(engine==nullptr||windowWidth<=0||windowHeight<=0)return;
    const auto& g=layout;
    fillHelpRectangle(engine,{0,0,windowWidth,windowHeight},g.background);
    fillHelpRectangle(engine,g.panel,g.panelColor);
    fillHelpRectangle(engine,{g.outer,g.outer,g.panel.w,2},g.accent);
    headerAndFooterTextTextures[0].draw(engine,"手柄操作说明",g.contentLeft,g.titleTop,g.titleFont,g.title);
    headerAndFooterTextTextures[1].draw(engine,"游戏已暂停",g.contentLeft,g.subtitleTop,std::max(12,g.textFont),g.muted);
    if(windowWidth>=1100&&ensureControllerImageLoaded())drawControllerDiagram();
    else
    {
        drawSection("世界场景",worldHelpLines(),worldHelpTextTextures,g.contentLeft,g.contentTop,g.textFont,g.lineHeight,g.text);
        drawSection("菜单与剧情",menuHelpLines(),menuHelpTextTextures,g.menuLeft,g.menuTop,g.textFont,g.lineHeight,g.text);
    }
    headerAndFooterTextTextures[2].draw(engine,"按任意键、手柄按钮或轻触屏幕关闭说明",g.contentLeft,g.footerTop,g.textFont,g.title);
}

void ControllerHelpOverlay::onWindowResize(int width, int height)
{
	updateLayout(width, height);
}

void ControllerHelpOverlay::drawSection(
	const std::string& title,
	const std::vector<HelpLine>& lines,
	std::vector<CachedTextTexture>& textTextures,
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
	textTextures.resize(lines.size() + 1);
	textTextures[0].draw(engine, title, x, y, fontSize + 2, layout.section);
	int lineY = y + lineHeight;
	for (std::size_t index = 0; index < lines.size(); ++index)
	{
		textTextures[index + 1].draw(
			engine, formatLine(lines[index]), x, lineY, fontSize, textColor);
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
			controllerImage = engine->loadImageFromFile(display.image());
		}
	}
	return controllerImage != nullptr;
}

void ControllerHelpOverlay::drawControllerDiagram()
{
    if(engine==nullptr||controllerImage==nullptr)return;
    int width=0,height=0;if(!engine->getImageSize(controllerImage,width,height)||width<=0||height<=0)return;
    auto destination=layout.imageRectangle(width,height);engine->drawImage(controllerImage,nullptr,&destination);
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
	layout = display.resolve(windowWidth,windowHeight);
}
