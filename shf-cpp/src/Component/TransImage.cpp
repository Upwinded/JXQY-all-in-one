#include "TransImage.h"



TransImage::TransImage()
{
	name = "TransImage";
}


TransImage::~TransImage()
{
	freeResource();
}

void TransImage::onDraw()
{
	_image img = IMP::loadImageForTime(impImage, getTime());
	engine->setImageAlpha(img, alpha);
	if (stretch)
	{
		engine->drawImage(img, NULL, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);
	}
}
