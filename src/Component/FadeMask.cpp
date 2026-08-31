#include "FadeMask.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredFadeMask = []
	{
		ComponentRegistry::getInstance().registerType("FadeMask",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<FadeMask>(); });
		return true;
	}();
}

FadeMask::FadeMask()
{
	setPriority(epFadeMask);
	mask = engine->createMask(0, 0, 0, 255);
}

FadeMask::~FadeMask()
{
	freeResource();
}

void FadeMask::sleep(UTime t)
{
	isSleep = true;
	sleepTime = t;
	run();
}

void FadeMask::setFadeTime(UTime t)
{
	fadeTime = t > 0 ? t : 1;
}

void FadeMask::fadeIn()
{
	isFadeIn = true;
	isSleep = false;
	run();
}

void FadeMask::fadeOut()
{
	isFadeIn = false;
	isSleep = false;
	run();
}

void FadeMask::onUpdate()
{
	auto t = getTime() - fadeBeginTime;
	if (isSleep)
	{
		if (t > sleepTime)
		{
			logicRunning = false;
		}
	}
	else
	{
		if (fadeCompleted)
		{
			logicRunning = false;
		}
		else if (t >= fadeTime)
		{
			alpha = isFadeIn ? 0 : 255;
			fadeCompleted = true;
		}
		else
		{
			alpha = (int)(((float)t) / (float)fadeTime * 255.0);
			if (isFadeIn)
			{
				alpha = 255 - alpha;
			}
		}
	}
	
}

void FadeMask::onDraw()
{
	if (!isSleep)
	{
		engine->setImageAlpha(mask, alpha);
		engine->drawMask(mask);
	}
}

void FadeMask::onRun()
{
	fadeCompleted = false;
	fadeBeginTime = getTime();
	if (!isSleep)
	{
		alpha = isFadeIn ? 255 : 0;
	}
}

void FadeMask::freeResource()
{
	if (mask != nullptr)
	{
		//engine->freeImage(mask);
		mask = nullptr;
	}
}
