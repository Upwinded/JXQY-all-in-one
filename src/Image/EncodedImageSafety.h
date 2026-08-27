#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EncodedImageSafety
{
constexpr std::size_t MaxEncodedImageBytes = 128ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaxDecodedImagePixels = 64ULL * 1024ULL * 1024ULL;

enum class Format
{
	Unknown,
	Png,
	Bmp,
	Gif,
	Jpeg,
	Qoi,
	WebP,
	Tga
};

enum class DimensionStatus
{
	Unknown,
	Valid,
	Invalid
};

struct Dimensions
{
	std::uint64_t width = 0;
	std::uint64_t height = 0;
	Format format = Format::Unknown;
};

inline const char* getDecoderType(Format format)
{
	switch (format)
	{
	case Format::Png:
		return "PNG";
	case Format::Bmp:
		return "BMP";
	case Format::Gif:
		return "GIF";
	case Format::Jpeg:
		return "JPG";
	case Format::Qoi:
		return "QOI";
	case Format::WebP:
		return "WEBP";
	case Format::Tga:
		return "TGA";
	default:
		return nullptr;
	}
}

inline std::uint16_t readBigEndian16(const std::uint8_t* data)
{
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8) |
		data[1]);
}

inline std::uint32_t readBigEndian32(const std::uint8_t* data)
{
	return (static_cast<std::uint32_t>(data[0]) << 24) |
		(static_cast<std::uint32_t>(data[1]) << 16) |
		(static_cast<std::uint32_t>(data[2]) << 8) |
		static_cast<std::uint32_t>(data[3]);
}

inline std::uint16_t readLittleEndian16(const std::uint8_t* data)
{
	return static_cast<std::uint16_t>(data[0] |
		(static_cast<std::uint16_t>(data[1]) << 8));
}

inline std::uint32_t readLittleEndian32(const std::uint8_t* data)
{
	return static_cast<std::uint32_t>(data[0]) |
		(static_cast<std::uint32_t>(data[1]) << 8) |
		(static_cast<std::uint32_t>(data[2]) << 16) |
		(static_cast<std::uint32_t>(data[3]) << 24);
}

inline bool isDecodedPixelCountSafe(std::uint64_t width, std::uint64_t height)
{
	return width > 0 && height > 0 &&
		width <= MaxDecodedImagePixels / height;
}

inline bool isJpegStartOfFrameMarker(std::uint8_t marker)
{
	switch (marker)
	{
	case 0xC0:
	case 0xC1:
	case 0xC2:
	case 0xC3:
	case 0xC5:
	case 0xC6:
	case 0xC7:
	case 0xC9:
	case 0xCA:
	case 0xCB:
	case 0xCD:
	case 0xCE:
	case 0xCF:
		return true;
	default:
		return false;
	}
}

inline bool skipGifSubBlocks(const std::uint8_t* data, std::size_t size,
	std::size_t& offset)
{
	while (offset < size)
	{
		std::size_t blockSize = data[offset++];
		if (blockSize == 0)
		{
			return true;
		}
		if (blockSize > size - offset)
		{
			return false;
		}
		offset += blockSize;
	}
	return false;
}

inline DimensionStatus inspectGifDimensions(const std::uint8_t* data, std::size_t size,
	Dimensions& dimensions)
{
	if (size < 13)
	{
		return DimensionStatus::Invalid;
	}
	std::uint64_t maximumWidth = readLittleEndian16(data + 6);
	std::uint64_t maximumHeight = readLittleEndian16(data + 8);
	if (maximumWidth == 0 || maximumHeight == 0)
	{
		return DimensionStatus::Invalid;
	}

	std::size_t offset = 13;
	std::uint8_t logicalScreenFlags = data[10];
	if ((logicalScreenFlags & 0x80) != 0)
	{
		std::size_t colorTableBytes = 3ULL *
			(1ULL << ((logicalScreenFlags & 0x07) + 1));
		if (colorTableBytes > size - offset)
		{
			return DimensionStatus::Invalid;
		}
		offset += colorTableBytes;
	}

	bool hasImageDescriptor = false;
	bool hasTrailer = false;
	while (offset < size)
	{
		std::uint8_t marker = data[offset++];
		if (marker == 0x3B)
		{
			hasTrailer = true;
			break;
		}
		if (marker == 0x21)
		{
			if (offset >= size)
			{
				return DimensionStatus::Invalid;
			}
			offset++;
			if (!skipGifSubBlocks(data, size, offset))
			{
				return DimensionStatus::Invalid;
			}
			continue;
		}
		if (marker != 0x2C || size - offset < 9)
		{
			return DimensionStatus::Invalid;
		}

		std::uint64_t left = readLittleEndian16(data + offset);
		std::uint64_t top = readLittleEndian16(data + offset + 2);
		std::uint64_t frameWidth = readLittleEndian16(data + offset + 4);
		std::uint64_t frameHeight = readLittleEndian16(data + offset + 6);
		std::uint8_t imageFlags = data[offset + 8];
		offset += 9;
		if (frameWidth == 0 || frameHeight == 0)
		{
			return DimensionStatus::Invalid;
		}
		std::uint64_t frameRight = left + frameWidth;
		std::uint64_t frameBottom = top + frameHeight;
		if (frameRight > maximumWidth)
		{
			maximumWidth = frameRight;
		}
		if (frameBottom > maximumHeight)
		{
			maximumHeight = frameBottom;
		}

		if ((imageFlags & 0x80) != 0)
		{
			std::size_t colorTableBytes = 3ULL *
				(1ULL << ((imageFlags & 0x07) + 1));
			if (colorTableBytes > size - offset)
			{
				return DimensionStatus::Invalid;
			}
			offset += colorTableBytes;
		}
		if (offset >= size)
		{
			return DimensionStatus::Invalid;
		}
		offset++;
		if (!skipGifSubBlocks(data, size, offset))
		{
			return DimensionStatus::Invalid;
		}
		hasImageDescriptor = true;
	}
	if (!hasImageDescriptor || !hasTrailer)
	{
		return DimensionStatus::Invalid;
	}
	dimensions.width = maximumWidth;
	dimensions.height = maximumHeight;
	dimensions.format = Format::Gif;
	return DimensionStatus::Valid;
}

inline DimensionStatus inspectTgaDimensions(const std::uint8_t* data, std::size_t size,
	Dimensions& dimensions)
{
	if (size < 18 || data[1] > 1)
	{
		return DimensionStatus::Unknown;
	}
	std::uint8_t imageType = data[2];
	bool runLengthEncoded = imageType == 9 || imageType == 10 || imageType == 11;
	bool indexed = imageType == 1 || imageType == 9;
	bool trueColor = imageType == 2 || imageType == 10;
	bool grayscale = imageType == 3 || imageType == 11;
	if (!indexed && !trueColor && !grayscale)
	{
		return DimensionStatus::Unknown;
	}

	std::uint16_t colorMapLength = readLittleEndian16(data + 5);
	std::uint8_t colorMapBits = data[7];
	std::uint8_t pixelBits = data[16];
	if ((indexed && (data[1] == 0 || pixelBits != 8 || colorMapLength == 0 ||
		colorMapLength > 256)) ||
		(trueColor && pixelBits != 15 && pixelBits != 16 && pixelBits != 24 &&
			pixelBits != 32) ||
		(grayscale && pixelBits != 8))
	{
		return DimensionStatus::Invalid;
	}
	if (data[1] != 0 && colorMapBits != 15 && colorMapBits != 16 &&
		colorMapBits != 24 && colorMapBits != 32)
	{
		return DimensionStatus::Invalid;
	}

	std::size_t offset = 18;
	if (data[0] > size - offset)
	{
		return DimensionStatus::Invalid;
	}
	offset += data[0];
	if (data[1] != 0)
	{
		std::size_t colorMapEntryBytes = (colorMapBits + 7) / 8;
		std::size_t colorMapBytes = static_cast<std::size_t>(colorMapLength) *
			colorMapEntryBytes;
		if (colorMapBytes > size - offset)
		{
			return DimensionStatus::Invalid;
		}
		offset += colorMapBytes;
	}
	if (offset >= size)
	{
		return DimensionStatus::Invalid;
	}

	dimensions.width = readLittleEndian16(data + 12);
	dimensions.height = readLittleEndian16(data + 14);
	if (dimensions.width == 0 || dimensions.height == 0)
	{
		return DimensionStatus::Invalid;
	}
	if (!runLengthEncoded)
	{
		std::uint64_t bytesPerPixel = (pixelBits + 7) / 8;
		std::uint64_t pixelBytes = dimensions.width * dimensions.height * bytesPerPixel;
		if (pixelBytes > size - offset)
		{
			return DimensionStatus::Invalid;
		}
	}
	dimensions.format = Format::Tga;
	return DimensionStatus::Valid;
}

inline DimensionStatus inspectDimensions(const void* encodedData, std::size_t size,
	Dimensions& dimensions)
{
	dimensions = {};
	if (encodedData == nullptr || size == 0)
	{
		return DimensionStatus::Invalid;
	}
	const auto* data = static_cast<const std::uint8_t*>(encodedData);

	static constexpr std::uint8_t PngSignature[] =
		{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	if (size >= sizeof(PngSignature) &&
		std::memcmp(data, PngSignature, sizeof(PngSignature)) == 0)
	{
		if (size < 24 || readBigEndian32(data + 8) != 13 ||
			std::memcmp(data + 12, "IHDR", 4) != 0)
		{
			return DimensionStatus::Invalid;
		}
		dimensions.width = readBigEndian32(data + 16);
		dimensions.height = readBigEndian32(data + 20);
		dimensions.format = Format::Png;
		return DimensionStatus::Valid;
	}

	if (size >= 2 && data[0] == 'B' && data[1] == 'M')
	{
		if (size < 26)
		{
			return DimensionStatus::Invalid;
		}
		std::uint32_t dibHeaderSize = readLittleEndian32(data + 14);
		if (dibHeaderSize == 12)
		{
			dimensions.width = readLittleEndian16(data + 18);
			dimensions.height = readLittleEndian16(data + 20);
			dimensions.format = Format::Bmp;
			return DimensionStatus::Valid;
		}
		if (dibHeaderSize < 40)
		{
			return DimensionStatus::Invalid;
		}
		std::uint32_t rawWidth = readLittleEndian32(data + 18);
		std::uint32_t rawHeight = readLittleEndian32(data + 22);
		if (rawHeight == 0x80000000U)
		{
			return DimensionStatus::Invalid;
		}
		std::int32_t signedWidth = static_cast<std::int32_t>(rawWidth);
		std::int32_t signedHeight = static_cast<std::int32_t>(rawHeight);
		if (signedWidth <= 0 || signedHeight == 0)
		{
			return DimensionStatus::Invalid;
		}
		dimensions.width = static_cast<std::uint64_t>(signedWidth);
		dimensions.height = signedHeight < 0
			? static_cast<std::uint64_t>(-static_cast<std::int64_t>(signedHeight))
			: static_cast<std::uint64_t>(signedHeight);
		dimensions.format = Format::Bmp;
		return DimensionStatus::Valid;
	}

	if (size >= 6 &&
		(std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0))
	{
		return inspectGifDimensions(data, size, dimensions);
	}

	if (size >= 4 && std::memcmp(data, "qoif", 4) == 0)
	{
		if (size < 14)
		{
			return DimensionStatus::Invalid;
		}
		dimensions.width = readBigEndian32(data + 4);
		dimensions.height = readBigEndian32(data + 8);
		dimensions.format = Format::Qoi;
		return DimensionStatus::Valid;
	}

	if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 &&
		std::memcmp(data + 8, "WEBP", 4) == 0)
	{
		if (size < 20 || readLittleEndian32(data + 4) > size - 8)
		{
			return DimensionStatus::Invalid;
		}
		std::uint32_t chunkSize = readLittleEndian32(data + 16);
		if (chunkSize > size - 20)
		{
			return DimensionStatus::Invalid;
		}
		if (std::memcmp(data + 12, "VP8X", 4) == 0)
		{
			if (chunkSize < 10 || size < 30)
			{
				return DimensionStatus::Invalid;
			}
			dimensions.width = 1ULL + data[24] +
				(static_cast<std::uint64_t>(data[25]) << 8) +
				(static_cast<std::uint64_t>(data[26]) << 16);
			dimensions.height = 1ULL + data[27] +
				(static_cast<std::uint64_t>(data[28]) << 8) +
				(static_cast<std::uint64_t>(data[29]) << 16);
			dimensions.format = Format::WebP;
			return DimensionStatus::Valid;
		}
		if (std::memcmp(data + 12, "VP8L", 4) == 0)
		{
			if (chunkSize < 5 || size < 25 || data[20] != 0x2F)
			{
				return DimensionStatus::Invalid;
			}
			dimensions.width = 1ULL + data[21] +
				((static_cast<std::uint64_t>(data[22]) & 0x3F) << 8);
			dimensions.height = 1ULL + (data[22] >> 6) +
				(static_cast<std::uint64_t>(data[23]) << 2) +
				((static_cast<std::uint64_t>(data[24]) & 0x0F) << 10);
			dimensions.format = Format::WebP;
			return DimensionStatus::Valid;
		}
		if (std::memcmp(data + 12, "VP8 ", 4) == 0)
		{
			if (chunkSize < 10 || size < 30 || data[23] != 0x9D ||
				data[24] != 0x01 || data[25] != 0x2A)
			{
				return DimensionStatus::Invalid;
			}
			dimensions.width = readLittleEndian16(data + 26) & 0x3FFF;
			dimensions.height = readLittleEndian16(data + 28) & 0x3FFF;
			dimensions.format = Format::WebP;
			return DimensionStatus::Valid;
		}
		return DimensionStatus::Invalid;
	}

	if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8)
	{
		std::size_t offset = 2;
		while (offset < size)
		{
			while (offset < size && data[offset] == 0xFF)
			{
				offset++;
			}
			if (offset >= size)
			{
				return DimensionStatus::Invalid;
			}
			std::uint8_t marker = data[offset++];
			if (marker == 0x00)
			{
				return DimensionStatus::Invalid;
			}
			if (marker == 0xD8 || marker == 0x01 ||
				(marker >= 0xD0 && marker <= 0xD7))
			{
				continue;
			}
			if (marker == 0xD9 || marker == 0xDA || offset + 2 > size)
			{
				return DimensionStatus::Invalid;
			}
			std::uint16_t segmentLength = readBigEndian16(data + offset);
			if (segmentLength < 2 || segmentLength > size - offset)
			{
				return DimensionStatus::Invalid;
			}
			if (isJpegStartOfFrameMarker(marker))
			{
				if (segmentLength < 7)
				{
					return DimensionStatus::Invalid;
				}
				dimensions.height = readBigEndian16(data + offset + 3);
				dimensions.width = readBigEndian16(data + offset + 5);
				dimensions.format = Format::Jpeg;
				return DimensionStatus::Valid;
			}
			offset += segmentLength;
		}
		return DimensionStatus::Invalid;
	}

	DimensionStatus tgaStatus = inspectTgaDimensions(data, size, dimensions);
	if (tgaStatus != DimensionStatus::Unknown)
	{
		return tgaStatus;
	}

	return DimensionStatus::Unknown;
}

inline bool hasSafeDimensions(const void* encodedData, std::size_t size)
{
	Dimensions dimensions;
	DimensionStatus status = inspectDimensions(encodedData, size, dimensions);
	return status == DimensionStatus::Valid &&
		getDecoderType(dimensions.format) != nullptr &&
		isDecodedPixelCountSafe(dimensions.width, dimensions.height);
}

inline bool inspectSafeDimensions(const void* encodedData, std::size_t size,
	Dimensions& dimensions)
{
	return inspectDimensions(encodedData, size, dimensions) == DimensionStatus::Valid &&
		getDecoderType(dimensions.format) != nullptr &&
		isDecodedPixelCountSafe(dimensions.width, dimensions.height);
}
}
