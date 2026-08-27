#include "ColorStyle.h"

#include "../../Engine/Engine.h"

void ColorStyle::drawImage(
	Engine* engine,
	_shared_image image,
	int x,
	int y,
	std::uint32_t colorStyle)
{
	if (engine == nullptr || image == nullptr)
	{
		return;
	}
	if (isNormal(colorStyle))
	{
		engine->drawImage(image, x, y);
		return;
	}
	if (isGrayscale(colorStyle))
	{
		_shared_image grayscaleImage = engine->getGrayscaleImage(image);
		if (grayscaleImage != nullptr)
		{
			engine->drawImage(grayscaleImage, x, y);
		}
		else
		{
			engine->drawImageWithColor(image, x, y, 160, 160, 160);
		}
		return;
	}
	engine->drawImageWithColor(
		image,
		x,
		y,
		(colorStyle >> 16) & 0xFF,
		(colorStyle >> 8) & 0xFF,
		colorStyle & 0xFF);
}
