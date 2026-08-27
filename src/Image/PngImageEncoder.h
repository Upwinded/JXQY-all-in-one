#pragma once

#include "EncodedImageSafety.h"

extern "C"
{
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
}

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

namespace PngImageEncoder
{
inline int encodeBgra8888(const uint8_t* pixels, int width, int height,
	int pixelBytes, std::unique_ptr<char[]>& encodedData)
{
	encodedData.reset();
	if (pixels == nullptr || width <= 0 || height <= 0 || pixelBytes < 0 ||
		!EncodedImageSafety::isDecodedPixelCountSafe(
			static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height)))
	{
		return -1;
	}
	std::uint64_t requiredPixelBytes = static_cast<std::uint64_t>(width) *
		static_cast<std::uint64_t>(height) * 4;
	if (static_cast<std::uint64_t>(pixelBytes) < requiredPixelBytes)
	{
		return -1;
	}

	SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height,
		SDL_PIXELFORMAT_ARGB8888, const_cast<uint8_t*>(pixels), width * 4);
	if (surface == nullptr)
	{
		return -1;
	}
	SDL_IOStream* output = SDL_IOFromDynamicMem();
	if (output == nullptr)
	{
		SDL_DestroySurface(surface);
		return -1;
	}

	bool encoded = IMG_SavePNG_IO(surface, output, false);
	SDL_DestroySurface(surface);
	Sint64 encodedSize = encoded ? SDL_TellIO(output) : -1;
	void* encodedMemory = nullptr;
	if (encodedSize > 0 &&
		static_cast<std::uint64_t>(encodedSize) <= EncodedImageSafety::MaxEncodedImageBytes)
	{
		SDL_PropertiesID properties = SDL_GetIOProperties(output);
		encodedMemory = SDL_GetPointerProperty(properties,
			SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
	}
	if (encodedMemory == nullptr)
	{
		SDL_CloseIO(output);
		return -1;
	}

	try
	{
		encodedData = std::make_unique<char[]>(static_cast<std::size_t>(encodedSize));
	}
	catch (const std::bad_alloc&)
	{
		SDL_CloseIO(output);
		return -1;
	}
	catch (const std::length_error&)
	{
		SDL_CloseIO(output);
		return -1;
	}
	std::memcpy(encodedData.get(), encodedMemory, static_cast<std::size_t>(encodedSize));
	SDL_CloseIO(output);
	return static_cast<int>(encodedSize);
}
}
