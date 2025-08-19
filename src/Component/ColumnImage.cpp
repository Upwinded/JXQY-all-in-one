#include "ColumnImage.h"



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
		engine->setImageAlpha(img, 180);
		engine->drawImage(img, &r, &d);
	}
	else
	{
		Rect d = rect;
		d.w = r.w;
		d.h = r.h;
		d.y += r.y;
		engine->setImageAlpha(img, 180);
		engine->drawImage(img, &r, &d);
	}
	
}
