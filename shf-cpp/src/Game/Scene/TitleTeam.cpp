#include "TitleTeam.h"
#include <string>


TitleTeam::TitleTeam()
{
	drawFullScreen = true;
}


TitleTeam::~TitleTeam()
{
	freeResource();
}


void TitleTeam::freeResource()
{
	if (vp != NULL)
	{
		vp->freeResource();
		delete vp;
		vp = NULL;
	}
	removeAllChild();
}

bool TitleTeam::onInitial()
{
	freeResource();
	int w = 0, h = 0;
	engine->getWindowSize(&w, &h);
	vp = new VideoPage("video\\team.avi");
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
	engine->getWindowSize(&w, &h);
	unsigned int color = 0xD0FFFFFF;
	if (getTime() < 1000)
	{
		unsigned char alpha = (unsigned char)((float)getTime() / 1000.0f * 0xD0);
		color = 0xFFFFFF + (alpha << 24);
	}
	
	engine->drawUnicodeText(L"½£ÏÀÇéÔµ·¡", w - 370, 50, 55, color);
	engine->drawUnicodeText(L"ÒýÇæÖØÖÆ£º", w - 410, 110, 30, color);
	engine->drawUnicodeText(L"Upwinded", w - 300, 150, 26, color);
	engine->drawUnicodeText(L"ÌØ±ð¸ÐÐ»£º", w - 410, 200, 30, color);
	engine->drawUnicodeText(L"Å¼Ïñ(Weyl¡¢BT¡¢scarsty¡¢SB500)", w - 410, 250, 26, color);
	engine->drawUnicodeText(L"Ð¡ÊÔµ¶½£", w - 290, 300, 26, color);
	engine->drawUnicodeText(L"´óÎäÏÀÂÛÌ³(dawuxia.net)", w - 385, 350, 26, color);
	engine->drawUnicodeText(L"½£ÏÀÇéÔµÌù°É", w - 320, 400, 26, color);
}

void TitleTeam::onExit()
{
	freeResource();
}

bool TitleTeam::onHandleEvent(AEvent * e)
{
	if (e == NULL)
	{
		return false;
	}
	if (e->eventType == KEYDOWN)
	{
		if (e->eventData == KEY_ESCAPE)
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
