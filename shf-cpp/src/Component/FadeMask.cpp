#include "FadeMask.h"

FadeMask::FadeMask()
{
	priority = epFadeMask;
	mask = engine->createMask(0, 0, 0, 255);
}

FadeMask::~FadeMask()
{
	if (mask != NULL)
	{
		engine->freeImage(mask);
		mask = NULL;
	}
}

void FadeMask::sleep(unsigned int t)
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
	unsigned int t = getTime() - fadeBeginTime;
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
			alpha = (int)(((double)t) / (double)fadeTime * 255.0);
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
