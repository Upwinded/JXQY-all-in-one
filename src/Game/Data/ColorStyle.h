#pragma once

#include <cstdint>

#include "../../Engine/ImageTypes.h"

class Engine;

namespace ColorStyle
{
	constexpr uint32_t Normal = 0x00FFFFFF;
	constexpr uint32_t Grayscale = 0x01000000;

	inline bool isGrayscale(uint32_t colorStyle)
	{
		return colorStyle == Grayscale;
	}

	inline bool isNormal(uint32_t colorStyle)
	{
		return !isGrayscale(colorStyle) && (colorStyle & 0x00FFFFFF) == Normal;
	}

	void drawImage(
		Engine* engine,
		_shared_image image,
		int x,
		int y,
		std::uint32_t colorStyle,
		std::uint8_t alpha = 255);
}
