#include "VideoContainer.h"

VideoContainer::VideoContainer()
{
	elementType = etVideoContainer;
	priority = epVideo;
	result = erNone;
	rect = { 0, 0, 0, 0 };
	name = "VideoContainer";
	coverMouse = true;
}

VideoContainer::~VideoContainer()
{
	freeResource();
}

VideoContainer::VideoContainer(const std::string & fileName)
{
	elementType = etVideoContainer;
	priority = epVideo;
	result = erNone;
	rect = { 0, 0, 0, 0 };
	name = "VideoContainer";
	videoFileName = fileName;
}

void VideoContainer::reopenVideo(const std::string& fileName, int vloop)
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

void VideoContainer::freeResource()
{
	if (v != NULL)
	{
		engine->freeVideo(v);
		v = NULL;
	}
}

bool VideoContainer::onHandleEvent(AEvent * e)
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

bool VideoContainer::onInitial()
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

void VideoContainer::onExit()
{
}

void VideoContainer::onRun()
{
	engine->runVideo(v);
}

void VideoContainer::onUpdate()
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

void VideoContainer::onDraw()
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

void VideoContainer::onDrawDrag()
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

void VideoContainer::onClick()
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

void VideoContainer::onDragEnd(Element * dst)
{
	coverMouse = true;
	int x, y;
	engine->getMouse(&x, &y);
	x -= dragX;
	y -= dragY;
	rect.x = x;
	rect.y = y;
}

void VideoContainer::onDrag(int * param1, int * param2)
{
	coverMouse = false;
}

