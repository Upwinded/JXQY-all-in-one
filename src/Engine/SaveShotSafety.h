#pragma once

#include "../Image/EncodedImageSafety.h"

#include <climits>
#include <cstdint>
#include <cstring>

namespace SaveShotSafety
{
// Read-only compatibility parser for the former SAVESHOT + raw BGRA format.
constexpr char Signature[] = "SAVESHOT";
constexpr int SignatureLength = 8;
constexpr int MetadataLength = 12;
constexpr int HeaderLength = SignatureLength + MetadataLength;
constexpr int PixelBytes = 4;
constexpr int MaximumFileBytes = static_cast<int>(
	EncodedImageSafety::MaxEncodedImageBytes);

struct View
{
	int width = 0;
	int height = 0;
	const char* pixels = nullptr;
	int pixelBytes = 0;
};

inline bool calculatePixelBytes(int width, int height, int& pixelBytes)
{
	pixelBytes = 0;
	if (width <= 0 || height <= 0 ||
		!EncodedImageSafety::isDecodedPixelCountSafe(
			static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height)))
	{
		return false;
	}

	const std::uint64_t byteCount = static_cast<std::uint64_t>(width) *
		static_cast<std::uint64_t>(height) * PixelBytes;
	if (byteCount > INT_MAX)
	{
		return false;
	}
	pixelBytes = static_cast<int>(byteCount);
	return true;
}

inline bool isPixelBufferValid(int width, int height, int size)
{
	int requiredBytes = 0;
	return size >= 0 && calculatePixelBytes(width, height, requiredBytes) &&
		requiredBytes <= size;
}

inline bool parse(const char* data, int size, View& view)
{
	view = {};
	if (data == nullptr || size < HeaderLength || size > MaximumFileBytes ||
		std::memcmp(data, Signature, SignatureLength) != 0)
	{
		return false;
	}

	std::int32_t width = 0;
	std::int32_t height = 0;
	std::memcpy(&width, data + SignatureLength, sizeof(width));
	std::memcpy(&height, data + SignatureLength + sizeof(width), sizeof(height));

	int pixelBytes = 0;
	if (!calculatePixelBytes(width, height, pixelBytes) ||
		pixelBytes > size - HeaderLength)
	{
		return false;
	}

	view.width = width;
	view.height = height;
	view.pixels = data + HeaderLength;
	view.pixelBytes = pixelBytes;
	return true;
}
}
