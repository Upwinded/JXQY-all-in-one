#include "TransImage.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"

namespace
{
	bool registeredTransImage = []
	{
		ComponentRegistry::getInstance().registerType("TransImage",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<TransImage>(); });
		return true;
	}();
}



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
