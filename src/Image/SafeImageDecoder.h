#pragma once

#include "EncodedImageSafety.h"

extern "C"
{
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
}

namespace SafeImageDecoder
{
inline SDL_Surface* loadSurface(const void* encodedData, int size,
	EncodedImageSafety::Dimensions* inspectedDimensions = nullptr)
{
	if (encodedData == nullptr || size <= 0 ||
		static_cast<std::size_t>(size) > EncodedImageSafety::MaxEncodedImageBytes)
	{
		return nullptr;
	}

	EncodedImageSafety::Dimensions dimensions;
	if (!EncodedImageSafety::inspectSafeDimensions(encodedData,
		static_cast<std::size_t>(size), dimensions))
	{
		return nullptr;
	}
	SDL_IOStream* stream = SDL_IOFromConstMem(encodedData, static_cast<std::size_t>(size));
	if (stream == nullptr)
	{
		return nullptr;
	}

	SDL_Surface* surface = nullptr;
	switch (dimensions.format)
	{
	case EncodedImageSafety::Format::Png:
		surface = IMG_LoadPNG_IO(stream);
		break;
	case EncodedImageSafety::Format::Bmp:
		surface = IMG_LoadBMP_IO(stream);
		break;
	case EncodedImageSafety::Format::Gif:
		surface = IMG_LoadGIF_IO(stream);
		break;
	case EncodedImageSafety::Format::Jpeg:
		surface = IMG_LoadJPG_IO(stream);
		break;
	case EncodedImageSafety::Format::Qoi:
		surface = IMG_LoadQOI_IO(stream);
		break;
	case EncodedImageSafety::Format::WebP:
		surface = IMG_LoadWEBP_IO(stream);
		break;
	case EncodedImageSafety::Format::Tga:
		surface = IMG_LoadTGA_IO(stream);
		break;
	default:
		break;
	}
	SDL_CloseIO(stream);

	if (surface == nullptr || surface->w <= 0 || surface->h <= 0 ||
		!EncodedImageSafety::isDecodedPixelCountSafe(
			static_cast<std::uint64_t>(surface->w),
			static_cast<std::uint64_t>(surface->h)))
	{
		if (surface != nullptr)
		{
			SDL_DestroySurface(surface);
		}
		return nullptr;
	}
	if (inspectedDimensions != nullptr)
	{
		*inspectedDimensions = dimensions;
	}
	return surface;
}
}
