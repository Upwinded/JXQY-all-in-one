#include "TitleTeam.h"
#include <string>


TitleTeam::TitleTeam()
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
}

bool TitleTeam::onInitial()
{
	freeResource();
	int w = 0, h = 0;
	engine->getWindowSize(w, h);
	vp = std::make_shared<VideoPage>(u8"video\\team.avi");
	vp->rect.w = w - 440;
	vp->rect.x = 0;
	vp->rect.y = 0;
	vp->rect.h = h;
	vp->drawFullScreen = false;
	addChild(vp);
	initTime();
	return true;
}

void TitleTeam::onDraw()
{
	int w = 0, h = 0;
	engine->getWindowSize(w, h);
	unsigned int color = 0xD0FFFFFF;
	if (getTime() < 1000)
	{
		unsigned char alpha = (unsigned char)((float)getTime() / 1000.0f * 0xD0);
		color = 0xFFFFFF + (alpha << 24);
	}

	engine->drawText(u8"剑侠情缘All_in_One", w - 410, 50, 45, color);
	engine->drawText(u8"引擎重制：", w - 410, 110, 30, color);
	engine->drawText(u8"Upwinded", w - 300, 150, 26, color);
	engine->drawText(u8"特别感谢：", w - 410, 200, 30, color);
	engine->drawText(u8"偶像(Weyl、BT、scarsty、SB500)", w - 410, 250, 26, color);
	engine->drawText(u8"小试刀剑", w - 290, 300, 26, color);
	engine->drawText(u8"大武侠论坛(dawuxia.net)", w - 385, 350, 26, color);
	engine->drawText(u8"剑侠情缘贴吧", w - 320, 400, 26, color);

}

void TitleTeam::onChildCallBack(PElement child)
{
	if (child != nullptr)
	{
		if (child->getResult() & erVideoStopped)
		{
			running = false;
		}
	}
}

void TitleTeam::onExit()
{
	freeResource();
}

bool TitleTeam::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_ESCAPE)
		{
			running = false;
		}
	}
	return false;
}

void TitleTeam::onUpdate()
{
	if (engine->getVideoStopped(vp->v))
	{
		running = false;
	}
}
