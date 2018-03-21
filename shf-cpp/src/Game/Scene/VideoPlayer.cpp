#include "VideoPlayer.h"

VideoPlayer::VideoPlayer()
{
	int w = 0 , h = 0;
	engine->getWindowSize(&w, &h);
	rect.w = w;
	rect.h = h;
	drawFullScreen = true;
}

VideoPlayer::VideoPlayer(const std::string & fileName)
{
	int w = 0, h = 0;
	engine->getWindowSize(&w, &h);
	rect.w = w;
	rect.h = h;
	drawFullScreen = true;
	videoFileName = fileName;
}


VideoPlayer::~VideoPlayer()
{
	freeResource();
}
