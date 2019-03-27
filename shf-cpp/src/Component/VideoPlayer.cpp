#include "VideoPlayer.h"

VideoPlayer::VideoPlayer()
{
	elementType = etVideoPlayer;
	priority = epVideo;
	result = erNone;
	rect = { 0, 0, 0, 0 };
	name = "VideoPlayer";
	coverMouse = true;
}

VideoPlayer::~VideoPlayer()
{
	freeResource();
}

VideoPlayer::VideoPlayer(const std::string & fileName)
{
	elementType = etVideoPlayer;
	priority = epVideo;
	result = erNone;
	rect = { 0, 0, 0, 0 };
	name = "VideoPlayer";
	videoFileName = fileName;
}

void VideoPlayer::reopenVideo(const std::string& fileName, int vloop)
{
	if (v != NULL)
	{
		engine->freeVideo(v);
		v = NULL;
	}
	videoFileName = fileName;	
	v = engine->createNewVideo(fileName);
	loop = vloop;
	engine->setVideoLoop(v, vloop);
	engine->runVideo(v);
}

void VideoPlayer::freeResource()
{
	if (v != NULL)
	{
		engine->freeVideo(v);
		v = NULL;
	}
}

bool VideoPlayer::onHandleEvent(AEvent * e)
{
	if (e->eventType == KEYUP)
	{
		if (e->eventData == KEY_ESCAPE)
		{
			engine->stopVideo(v);
			running = false;
		}
	}
	return false;
}

bool VideoPlayer::onInitial()
{
	if (v != NULL)
	{
		engine->freeVideo(v);
		v = NULL;
	}
	v = engine->createNewVideo(videoFileName);
	engine->setVideoLoop(v, loop);
	return true;
}

void VideoPlayer::onExit()
{
}

void VideoPlayer::onRun()
{
	engine->runVideo(v);
}

void VideoPlayer::onUpdate()
{
	engine->updateVideo(v);
	if (engine->getVideoStopped(v))
	{
		result |= erVideoStopped;
		if (canCallBack)
		{
			if (parent != NULL)
			{
				parent->onChildCallBack(this);
				result = erNone;
			}		
		}
		if (running)
		{
			running = false;
		}
	}
}

void VideoPlayer::onDraw()
{
	if (dragging && dragItem == this)
	{
		return;
	}
	if (v == NULL)
	{
		_image m =engine->createMask(0, 0, 0, 255);
		if (drawFullScreen)
		{
			engine->drawImage(m, nullptr, nullptr);
		}
		else
		{
			engine->drawImage(m, nullptr, &rect);
		}
		engine->freeImage(m);
	}
	else
	{
		if (drawFullScreen)
		{
			engine->setVideoRect(v, nullptr);
			engine->drawVideoFrame(v);
		}
		else
		{
			engine->setVideoRect(v, &rect);
			engine->drawVideoFrame(v);
		}	
	}
}

void VideoPlayer::onDrawDrag()
{
	Rect r = rect;
	int x, y;
	engine->getMouse(&x, &y);
	x -= dragX;
	y -= dragY;
	r.x = x;
	r.y = y;
	if (v == NULL)
	{		
		_image m = engine->createMask(0, 0, 0, 255);
		engine->drawImage(m, NULL, &r);
		engine->freeImage(m);
	}
	else
	{
		engine->setVideoRect(v, &r);
		engine->drawVideoFrame(v);
	}
}

void VideoPlayer::onClick()
{
	result |= erClick;
	if (canCallBack)
	{
		if (parent != NULL)
		{
			parent->onChildCallBack(this);
			result = erNone;
		}
	}
}

void VideoPlayer::onDragEnd(Element * dst)
{
	coverMouse = true;
	int x, y;
	engine->getMouse(&x, &y);
	x -= dragX;
	y -= dragY;
	rect.x = x;
	rect.y = y;
}

void VideoPlayer::onDrag(int * param1, int * param2)
{
	coverMouse = false;
}

