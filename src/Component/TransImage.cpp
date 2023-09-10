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
	_shared_image img = IMP::loadImageForTime(impImage, getTime());
	engine->setImageAlpha(img, alpha);
	if (stretch)
	{
		engine->drawImage(img, nullptr, &rect);
	}
	else
	{
		engine->drawImage(img, rect.x, rect.y);
	}
}
