#include "VideoPlayer.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"
#include "../Game/Data/MediaPathResolver.h"
#include "../Game/GameTypes.h"
#include "../Game/Menu/UIFocusManager.h"

namespace
{
	bool registeredVideoPlayer = []
	{
		ComponentRegistry::getInstance().registerType("VideoPlayer",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<VideoPlayer>(); });
		return true;
	}();

std::string resolveVideoFileName(const std::string& fileName)
{
	return resolveMediaAssetPath(VIDEO_FOLDER, fileName,
		{ ".avi", ".mp4", ".wmv", ".mpg", ".mpeg" });
}
}

VideoPlayer::VideoPlayer()
{
	elementType = etVideoPlayer;
	setPriority(epVideo);
	result = erNone;
	rect = { 0, 0, 0, 0 };
	name = "VideoPlayer";
	coverMouse = true;
	canCallBack = true;
	skipLabel->rect = {30, 20, 70, 35};
	skipLabel->fontSize = 30;
	skipLabel->setStr("跳过");
	skipLabel->coverMouse = true;
	addChild(skipLabel);
}

VideoPlayer::~VideoPlayer()
{
	freeResource();
}

VideoPlayer::VideoPlayer(const std::string & fileName) : VideoPlayer()
{
	videoFileName = resolveVideoFileName(fileName);
}

void VideoPlayer::reopenVideo(const std::string& fileName, int vloop)
{
	if (v != nullptr)
	{
		engine->freeVideo(v);
		v = nullptr;
	}
	videoFileName = resolveVideoFileName(fileName);
	v = engine->loadVideo(videoFileName);
	loop = vloop;
	engine->setVideoLoop(v, vloop);
	engine->runVideo(v);
}

void VideoPlayer::freeResource()
{
	if (v != nullptr)
	{
		engine->freeVideo(v);
		v = nullptr;
	}
}

void VideoPlayer::onChildCallBack(PElement child)
{
	if (child != nullptr)
	{
		if (child->getResult() & erMouseLDown)
		{
			engine->stopVideo(v);
			logicRunning = false;
            result |= erVideoStopped;
            if (parent != nullptr && parent->canCallBack)
            {
                parent->onChildCallBack(getMySharedPtr());
            }
		}
	}
}

bool VideoPlayer::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN || e.eventType == ET_KEYUP)
	{
		if (e.eventData == KEY_ESCAPE)
		{
			engine->stopVideo(v);
			logicRunning = false;
			result |= erVideoStopped;
			if (canCallBack && parent != nullptr)
			{
				parent->onChildCallBack(getMySharedPtr());
				result = erNone;
			}
			return true;
		}
	}
	return false;
}

bool VideoPlayer::onHandleUIAction(UIAction action)
{
	if (action != UIAction::Cancel && action != UIAction::Confirm)
	{
		return false;
	}
	if (v != nullptr)
	{
		engine->stopVideo(v);
	}
	logicRunning = false;
	result |= erVideoStopped;
	if (canCallBack && parent != nullptr)
	{
		parent->onChildCallBack(getMySharedPtr());
		result = erNone;
	}
	return true;
}

bool VideoPlayer::onInitial()
{
	if (v != nullptr)
	{
		engine->freeVideo(v);
		v = nullptr;
	}
	videoFileName = resolveVideoFileName(videoFileName);
	v = engine->loadVideo(videoFileName);
	if (v == nullptr)
	{
		result |= erVideoStopped;
		logicRunning = false;
		return true;
	}
	engine->setVideoLoop(v, loop);
	return true;
}

void VideoPlayer::onExit()
{
	if (restoreHiddenCursorOnExit)
	{
		engine->hideCursor();
		restoreHiddenCursorOnExit = false;
	}
}

void VideoPlayer::onRun()
{
	if (v != nullptr)
	{
		if (!engine->getCursorVisible())
		{
			restoreHiddenCursorOnExit = true;
		}
		engine->showCursor();
	}
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
			if (parent != nullptr)
			{
				parent->onChildCallBack(getMySharedPtr());
				result = erNone;
			}		
		}
		if (logicRunning)
		{
			logicRunning = false;
		}
	}
}

void VideoPlayer::onDraw()
{
	if (dragging && currentDragItem.get() == this)
	{
		return;
	}
	if (v == nullptr)
	{
		_shared_image m =engine->createMask(0, 0, 0, 255);
		if (drawFullScreen)
		{
			engine->drawImage(m, nullptr, nullptr);
		}
		else
		{
			engine->drawImage(m, nullptr, &rect);
		}
		//engine->freeImage(m);
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

void VideoPlayer::onDrawDrag(int x, int y)
{
	Rect r = rect;
	r.x = x;
	r.y = y;
	if (v == nullptr)
	{		
		_shared_image m = engine->createMask(0, 0, 0, 255);
		engine->drawImage(m, nullptr, &r);
		//engine->freeImage(m);
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
		if (parent != nullptr)
		{
			parent->onChildCallBack(getMySharedPtr());
			result = erNone;
		}
	}
}

void VideoPlayer::onDragEnd(PElement dst, int x, int y)
{
	coverMouse = true;
	rect.x = x;
	rect.y = y;
}

void VideoPlayer::onDragBegin(int * param1, int * param2)
{
	coverMouse = false;
}
