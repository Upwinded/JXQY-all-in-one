#include "FadeMask.h"

FadeMask::FadeMask()
{
	priority = epFadeMask;
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
			running = false;
		}
	}
	else
	{
		if (t > fadeTime)
		{
			running = false;
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
	fadeBeginTime = getTime();
}

void FadeMask::freeResource()
{
	if (mask != nullptr)
	{
		//engine->freeImage(mask);
		mask = nullptr;
	}
}
