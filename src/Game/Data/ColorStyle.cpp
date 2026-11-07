#include "ColorStyle.h"

#include "../../Engine/Engine.h"

namespace
{
void drawImageWithOptionalAlpha(
	Engine* engine,
	_shared_image image,
	int x,
	int y,
	std::uint8_t alpha,
	float angle,
	const PointEx* center)
{
	if (alpha == 255)
	{
		engine->drawImage(image, x, y, angle, center);
		return;
	}
	engine->drawImageWithAlpha(image, x, y, alpha, angle, center);
}

void drawImageWithColorAndAlpha(
	Engine* engine,
	_shared_image image,
	int x,
	int y,
	std::uint8_t red,
	std::uint8_t green,
	std::uint8_t blue,
	std::uint8_t alpha,
	float angle,
	const PointEx* center)
{
	if (alpha != 255)
	{
		engine->setImageAlpha(image, alpha);
	}
	engine->drawImageWithColor(image, x, y, red, green, blue, angle, center);
	if (alpha != 255)
	{
		engine->setImageAlpha(image, 255);
	}
}
}

void ColorStyle::drawImage(
	Engine* engine,
	_shared_image image,
	int x,
	int y,
	std::uint32_t colorStyle,
	std::uint8_t alpha,
	float angle,
	const PointEx* center)
{
	if (engine == nullptr || image == nullptr)
	{
		return;
	}
	if (isNormal(colorStyle))
	{
		drawImageWithOptionalAlpha(
			engine, image, x, y, alpha, angle, center);
		return;
	}
	if (isGrayscale(colorStyle))
	{
		_shared_image grayscaleImage = engine->getGrayscaleImage(image);
		if (grayscaleImage != nullptr)
		{
			drawImageWithOptionalAlpha(
				engine, grayscaleImage, x, y, alpha, angle, center);
		}
		else
		{
			drawImageWithColorAndAlpha(
				engine, image, x, y, 160, 160, 160, alpha, angle, center);
		}
		return;
	}
	drawImageWithColorAndAlpha(
		engine,
		image,
		x,
		y,
		(colorStyle >> 16) & 0xFF,
		(colorStyle >> 8) & 0xFF,
		colorStyle & 0xFF,
		alpha, angle, center);
}
