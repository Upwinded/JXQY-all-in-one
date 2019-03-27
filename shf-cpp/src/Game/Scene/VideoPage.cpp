#include "VideoPage.h"

VideoPage::VideoPage()
{
	int w = 0 , h = 0;
	engine->getWindowSize(&w, &h);
	rect.w = w;
	rect.h = h;
	drawFullScreen = true;
}

VideoPage::VideoPage(const std::string & fileName)
{
	int w = 0, h = 0;
	engine->getWindowSize(&w, &h);
	rect.w = w;
	rect.h = h;
	drawFullScreen = true;
	videoFileName = fileName;
}


VideoPage::~VideoPage()
{
	freeResource();
}
