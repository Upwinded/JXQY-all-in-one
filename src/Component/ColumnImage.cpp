#include "ColumnImage.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredColumnImage = []
	{
		ComponentRegistry::getInstance().registerType("ColumnImage",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<ColumnImage>(); });
		return true;
	}();
}



ColumnImage::ColumnImage()
{
}


ColumnImage::~ColumnImage()
{
	freeResource();
}

void ColumnImage::onDraw()
{
	_shared_image img = IMP::loadImageForTime(impImage, getTime());
	if (img == nullptr)
	{
		return;
	}
	int h = 0, w = 0;
	engine->getImageSize(img, w, h);

	if (percent < 0)
	{
		percent = 0;
	}
	if (percent > 1)
	{
		percent = 1;
	}

	if (lagPercent > percent)
	{
		// 血条渐变速度：每秒减少的百分比，值越大渐变越快
		float lagSpeed = 0.6f * (getFrameTime() / 1000.0f);
		lagPercent -= lagSpeed;
		if (lagPercent < percent)
		{
			lagPercent = percent;
		}
	}
	else
	{
		lagPercent = percent;
	}

	if (lagPercent > percent)
	{
		Rect lagR;
		lagR.w = w;
		lagR.x = 0;
		lagR.h = (int)(lagPercent * (float)h) - (int)(percent * (float)h);
		lagR.y = h - (int)(lagPercent * (float)h);

		if (stretch)
		{
			Rect d = rect;
			int fullH = d.h;
			d.h = lagR.h;
			d.y = rect.y + fullH - (int)(lagPercent * (float)fullH);
			engine->setImageAlpha(img, 80);
			engine->drawImage(img, &lagR, &d);
		}
		else
		{
			Rect d = rect;
			d.w = lagR.w;
			d.h = lagR.h;
			d.y = rect.y + lagR.y;
			engine->setImageAlpha(img, 80);
			engine->drawImage(img, &lagR, &d);
		}
	}

	Rect r;
	r.w = w;
	r.x = 0;
	r.h = (int)(percent * (float)h);
	r.y = h - r.h;
	if (stretch)
	{
		Rect d = rect;
		h = d.h;
		d.h = (int)(percent * (float)h);
		d.y += h - d.h;
		engine->setImageAlpha(img, 255);
		engine->drawImage(img, &r, &d);
	}
	else
	{
		Rect d = rect;
		d.w = r.w;
		d.h = r.h;
		d.y += r.y;
		engine->setImageAlpha(img, 255);
		engine->drawImage(img, &r, &d);
	}
	engine->setImageAlpha(img, 255);
	
}
