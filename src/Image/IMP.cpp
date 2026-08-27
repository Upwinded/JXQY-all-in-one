#include "IMP.h"
#include "../Engine/Engine.h"
#include "EncodedImageSafety.h"
#include "IMPFormatValidation.h"
#include "ImageAnimationPlayback.h"
#include "ImagePackagePathCandidates.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
constexpr int MaxEncodedImageBytes = static_cast<int>(
	EncodedImageSafety::MaxEncodedImageBytes);
constexpr int MaxEmbeddedImageFrames = 100000;

bool addEmbeddedFramePixelBudget(const char* data, int size,
	std::uint64_t& aggregatePixels)
{
	EncodedImageSafety::Dimensions dimensions;
	// The editor writes IMG frames as PNG. Also accept the other header-defined
	// common formats, but do not admit opaque codecs whose allocation size cannot
	// be bounded before SDL_image starts decoding them.
	if (!EncodedImageSafety::inspectSafeDimensions(data,
		static_cast<std::size_t>(size), dimensions))
	{
		return false;
	}
	std::uint64_t framePixels = dimensions.width * dimensions.height;
	if (aggregatePixels > EncodedImageSafety::MaxDecodedImagePixels - framePixels)
	{
		return false;
	}
	aggregatePixels += framePixels;
	return true;
}

int subtractOffsetSaturated(int value, int amount)
{
	const long long adjusted = static_cast<long long>(value) - amount;
	return static_cast<int>(std::max<long long>(
		std::numeric_limits<int>::min(),
		std::min<long long>(std::numeric_limits<int>::max(), adjusted)));
}

void cropTransparentFrame(IMPFrame& frame)
{
	if (frame.pixelWidth <= 0 || frame.pixelHeight <= 0)
	{
		return;
	}
	const std::uint64_t pixelCount = static_cast<std::uint64_t>(frame.pixelWidth)
		* static_cast<std::uint64_t>(frame.pixelHeight);
	if (pixelCount > std::numeric_limits<std::size_t>::max() / 4
		|| frame.pixelData.size() != static_cast<std::size_t>(pixelCount * 4))
	{
		return;
	}

	int left = frame.pixelWidth;
	int top = frame.pixelHeight;
	int right = -1;
	int bottom = -1;
	for (int y = 0; y < frame.pixelHeight; ++y)
	{
		for (int x = 0; x < frame.pixelWidth; ++x)
		{
			const std::size_t alphaIndex =
				(static_cast<std::size_t>(y) * frame.pixelWidth + x) * 4 + 3;
			if (frame.pixelData[alphaIndex] == 0)
			{
				continue;
			}
			left = std::min(left, x);
			top = std::min(top, y);
			right = std::max(right, x);
			bottom = std::max(bottom, y);
		}
	}

	if (right < left || bottom < top)
	{
		frame.pixelData.assign(4, 0);
		frame.pixelWidth = 1;
		frame.pixelHeight = 1;
		frame.image = nullptr;
		return;
	}
	if (left == 0 && top == 0
		&& right == frame.pixelWidth - 1
		&& bottom == frame.pixelHeight - 1)
	{
		return;
	}

	const int croppedWidth = right - left + 1;
	const int croppedHeight = bottom - top + 1;
	std::vector<std::uint8_t> croppedPixels;
	try
	{
		croppedPixels.resize(
			static_cast<std::size_t>(croppedWidth)
			* static_cast<std::size_t>(croppedHeight) * 4);
	}
	catch (const std::bad_alloc&)
	{
		return;
	}
	catch (const std::length_error&)
	{
		return;
	}
	for (int y = 0; y < croppedHeight; ++y)
	{
		const std::size_t sourceOffset =
			(static_cast<std::size_t>(top + y) * frame.pixelWidth + left) * 4;
		const std::size_t destinationOffset =
			static_cast<std::size_t>(y) * croppedWidth * 4;
		std::memcpy(
			croppedPixels.data() + destinationOffset,
			frame.pixelData.data() + sourceOffset,
			static_cast<std::size_t>(croppedWidth) * 4);
	}

	frame.pixelData = std::move(croppedPixels);
	frame.pixelWidth = croppedWidth;
	frame.pixelHeight = croppedHeight;
	frame.xOffset = subtractOffsetSaturated(frame.xOffset, left);
	frame.yOffset = subtractOffsetSaturated(frame.yOffset, top);
	frame.image = nullptr;
}
}

uint32_t IMPImage::IMPImageCount;

bool IMP::cmpIMGHead(_shared_imp img)
{
	if (img == nullptr)
	{
		return false;
	}

	char tempIMGHead[imgHeadLen] = imgHeadString;

	for (int i = 0; i < imgHeadLen; i++)
	{
		if (tempIMGHead[i] != img->head[i])
		{
			return false;
		}
	}

	return true;
}

unsigned int IMP::getIMPImageActionTime(_shared_imp impImage)
{
	if (impImage == nullptr)
	{
		return 0;
	}
	if (impImage->frame.empty())
	{
		return 0;
	}

	const ImageAnimationPlayback::Layout layout =
		ImageAnimationPlayback::calculateLayout(
			impImage->frame.size(), impImage->directions);
	uint64_t interval = static_cast<uint64_t>(
		ImageAnimationPlayback::effectiveFrameInterval(impImage->interval));
	uint64_t duration = interval * layout.framesPerDirection;
	return duration > std::numeric_limits<unsigned int>::max()
		? std::numeric_limits<unsigned int>::max()
		: static_cast<unsigned int>(duration);
}

void IMP::cropTransparentEdges(_shared_imp impImage)
{
	if (impImage == nullptr)
	{
		return;
	}
	for (IMPFrame& frame : impImage->frame)
	{
		cropTransparentFrame(frame);
	}
}

bool IMP::loadIMPImage(_shared_imp impImage, const std::string & fileName, bool directlyLoad)
{
	std::unique_ptr<char[]> data;
	int size = 0;
	if (File::readFile(fileName, data, size, MaxEncodedImageBytes) && size > 0)
	{
		if (loadIMPImageFromMem(impImage, data, size, directlyLoad))
		{
			return true;
		}
	}
	return false;
}

bool IMP::loadIMPImageFromMem(_shared_imp impImage, std::unique_ptr<char[]>& data, int size, bool directlyLoad)
{
	int imageHeadLen = imgHeadLen + 4 * 3 + 4 * imageNullLen;
	if (impImage == nullptr || data == nullptr || size < 16 ||
		size > MaxEncodedImageBytes)
	{
		return false;
	}

	clearIMPImage(impImage);

	auto data_ptr = data.get();

	memcpy(impImage->head, data_ptr, imgHeadLen);
	data_ptr += imgHeadLen;

	if (!cmpIMGHead(impImage))
	{
		if (loadPicImageFromMem(impImage, data, size, directlyLoad))
		{
			return true;
		}
		return loadCommonImageFromMem(impImage, data, size, directlyLoad);
	}

	if (size < imageHeadLen)
	{
		return false;
	}
	if (!IMPFormatValidation::validate(data.get(), size))
	{
		return false;
	}

	int frameCount = 0;
	memcpy(&frameCount, data_ptr, 4);
	data_ptr += 4;
	if (frameCount < 0 || frameCount > MaxEmbeddedImageFrames)
	{
		return false;
	}
	memcpy(&impImage->directions, data_ptr, 4);
	data_ptr += 4;
	memcpy(&impImage->interval, data_ptr, 4);
	data_ptr += 4;

	for (int i = 0; i < imageNullLen; i++)
	{
		memcpy(&impImage->imageNull[i], data_ptr, 4);
		data_ptr += 4;
	}

	size -= imageHeadLen;
	
	try
	{
		impImage->frame.resize(frameCount);
	}
	catch (const std::bad_alloc&)
	{
		clearIMPImage(impImage);
		return false;
	}
	catch (const std::length_error&)
	{
		clearIMPImage(impImage);
		return false;
	}
	std::uint64_t aggregatePixels = 0;
	for (size_t i = 0; i < impImage->frame.size(); i++)
	{
		if (size >= 4 * 3 + 4 * frameNullLen)
		{
			memcpy(&impImage->frame[i].dataLen, data_ptr, 4);
			data_ptr += 4;
			memcpy(&impImage->frame[i].xOffset, data_ptr, 4);
			data_ptr += 4;
			memcpy(&impImage->frame[i].yOffset, data_ptr, 4);
			data_ptr += 4;
			for (size_t j = 0; j < frameNullLen; j++)
			{
				memcpy(&impImage->frame[i].frameNull[j], data_ptr, 4);
				data_ptr += 4;
			}
			size -= 4 * 3 + 4 * frameNullLen;
			if (size >= impImage->frame[i].dataLen && impImage->frame[i].dataLen > 0)
			{
				if (!addEmbeddedFramePixelBudget(data_ptr,
					impImage->frame[i].dataLen, aggregatePixels))
				{
					clearIMPImage(impImage);
					return false;
				}
				if (impImage->frame[i].image != nullptr)
				{
					//Engine::getInstance()->freeImage(impImage->frame[i].image);
					impImage->frame[i].image = nullptr;
				}
				if (impImage->frame[i].data != nullptr)
				{
					//delete[] impImage->frame[i].data;
					impImage->frame[i].data = nullptr;
				}			
				if (impImage->frame[i].dataLen > 0)
				{
					try
					{
						impImage->frame[i].data = std::make_unique<char[]>(
							impImage->frame[i].dataLen);
					}
					catch (const std::bad_alloc&)
					{
						clearIMPImage(impImage);
						return false;
					}
					catch (const std::length_error&)
					{
						clearIMPImage(impImage);
						return false;
					}
					memcpy(&impImage->frame[i].data[0], data_ptr, impImage->frame[i].dataLen);
					size -= impImage->frame[i].dataLen;
					data_ptr += impImage->frame[i].dataLen;
					if (directlyLoad)
					{
						impImage->frame[i].image = Engine::getInstance()->loadImageFromMem(impImage->frame[i].data, impImage->frame[i].dataLen);
						if (impImage->frame[i].image == nullptr)
						{
							clearIMPImage(impImage);
							return false;
						}
						else
						{
							impImage->frame[i].data = nullptr;
							impImage->frame[i].dataLen = 0;
						}
					}
				}
			}
			else
			{
				impImage->frame[i].data = nullptr;
				impImage->frame[i].image = nullptr;
				impImage->frame[i].dataLen = 0;
			}
		}
		else
		{
			impImage->frame[i].data = nullptr;
			impImage->frame[i].image = nullptr;
			impImage->frame[i].dataLen = 0;
			impImage->frame[i].xOffset = 0;
			impImage->frame[i].yOffset = 0;
		}
	}
	return true;
}

bool IMP::loadIMPImageFromFile(_shared_imp impImage, const std::string& fileName, bool directlyLoad)
{
	if (impImage == nullptr)
	{
		return false;
	}

	if (File::fileExist(fileName))
	{
		std::unique_ptr<char[]> data;
		int size;

		if (File::readFile(fileName, data, size, MaxEncodedImageBytes))
		{
			bool result = loadIMPImageFromMem(impImage, data, size, directlyLoad);
			return result;
		}
		return false;
	}
	return false;
}

void IMP::copyIMPImage(_shared_imp dst, _shared_imp src)
{
	if (dst != nullptr && src != nullptr)
	{
		for (int i = 0; i < imgHeadLen; i++)
		{
			dst->head[i] = src->head[i];
		}
		dst->directions = src->directions;
		dst->interval = src->interval;
		for (int i = 0; i < imageNullLen; i++)
		{
			dst->imageNull[i] = src->imageNull[i];
		}
		dst->frame.resize(src->frame.size());
		for (size_t i = 0; i < dst->frame.size(); i++)
		{
			dst->frame[i].dataLen = src->frame[i].dataLen;
			dst->frame[i].xOffset = src->frame[i].xOffset;
			dst->frame[i].yOffset = src->frame[i].yOffset;
			dst->frame[i].pixelWidth = src->frame[i].pixelWidth;
			dst->frame[i].pixelHeight = src->frame[i].pixelHeight;
			dst->frame[i].pixelData = src->frame[i].pixelData;
			if (dst->frame[i].image != nullptr)
			{
				//Engine::getInstance()->freeImage(dst->frame[i].image);
				dst->frame[i].image = nullptr;
			}			
			for (int j = 0; j < frameNullLen; j++)
			{
				dst->frame[i].frameNull[j] = src->frame[i].frameNull[j];
			}
			if (dst->frame[i].data != nullptr)
			{
				//delete[] dst->frame[i].data;
				dst->frame[i].data = nullptr;
			}
			if (src->frame[i].image != nullptr)
			{
				dst->frame[i].image = Engine::getInstance()->createNewImageFromImage(src->frame[i].image);
			}
			else if (src->frame[i].dataLen > 0 && src->frame[i].data != nullptr)
			{
				dst->frame[i].data = std::make_unique<char[]>(dst->frame[i].dataLen);
				memcpy(&(dst->frame[i].data[0]), &(src->frame[i].data[0]), dst->frame[i].dataLen);
			}
		}
	}
}

_shared_imp IMP::createIMPImageFromFile(const std::string& fileName, bool directlyLoad)
{
	if (File::fileExist(fileName))
	{
		std::unique_ptr<char[]> data;
		int size;
		if (File::readFile(fileName, data, size, MaxEncodedImageBytes))
		{
			auto result = createIMPImageFromMem(data, size, directlyLoad);
			return result;
		}
	}
	return nullptr;
}

_shared_imp IMP::createIMPImage(const std::string & fileName, bool directlyLoad)
{
	auto impImage = make_shared_imp();
	bool loaded = File::visitReadableResources(ImagePackagePathCandidates::build(fileName),
		MaxEncodedImageBytes,
		[&](const std::string&, std::unique_ptr<char[]>& data, int size)
		{
			return loadIMPImageFromMem(impImage, data, size, directlyLoad);
		});
	if (loaded)
	{
		return impImage;
	}

	return nullptr;
}

_shared_imp IMP::createIMPImageFromMem(std::unique_ptr<char[]>& data, int size, bool directlyLoad)
{
	auto impImage = make_shared_imp();
	if (loadIMPImageFromMem(impImage, data, size, directlyLoad))
	{
		return impImage;
	}
	else
	{
		return nullptr;
	}	
}

_shared_imp IMP::createIMPImageFromImage(_shared_image img)
{
	auto impImage = make_shared_imp();
	impImage->directions = 1;
	impImage->frame.resize(0);
	impImage->interval = 0;
	if (img != nullptr)
	{
		impImage->frame.resize(1);
		impImage->frame[0].data = nullptr;
		impImage->frame[0].dataLen = 0;
		impImage->frame[0].image = img;
		impImage->frame[0].xOffset = 0;
		impImage->frame[0].yOffset = 0;
	}
	return impImage;
}

_shared_imp IMP::createIMPImageFromPNG(std::string pngName, bool directlyLoad)
{
	std::unique_ptr<char[]> s;
	int len = 0;
	if (!File::readFile(pngName, s, len, MaxEncodedImageBytes) || len <= 0 || s == nullptr)
	{
		return nullptr;
	}
	auto impImage = make_shared_imp();
	impImage->directions = 1;
	impImage->interval = 0;
	impImage->frame.resize(1);
	impImage->frame[0].data = std::move(s);
	impImage->frame[0].dataLen = len;
	impImage->frame[0].image = nullptr;
	impImage->frame[0].xOffset = 0;
	impImage->frame[0].yOffset = 0;
	if (directlyLoad)
	{
		impImage->frame[0].image = Engine::getInstance()->loadImageFromMem(impImage->frame[0].data, impImage->frame[0].dataLen);
		if (impImage->frame[0].image != nullptr)
		{
			impImage->frame[0].dataLen = 0;
			impImage->frame[0].data = nullptr;
		}
	}

	return impImage;
}

_shared_imp IMP::createIMPImageFromFrame(_shared_imp impImage, int index)
{
	if (impImage == nullptr || impImage->frame.size() == 0)
	{
		return nullptr;
	}
	if (index < 0 || index >= (int)impImage->frame.size())
	{
		return nullptr;
	}
	auto img = make_shared_imp();
	img->directions = 1;
	img->interval = impImage->interval;
	img->frame.resize(1);
	img->frame[0].dataLen = impImage->frame[index].dataLen;
	img->frame[0].xOffset = impImage->frame[index].xOffset;
	img->frame[0].yOffset = impImage->frame[index].yOffset;
	img->frame[0].pixelWidth = impImage->frame[index].pixelWidth;
	img->frame[0].pixelHeight = impImage->frame[index].pixelHeight;
	img->frame[0].pixelData = impImage->frame[index].pixelData;
	if (impImage->frame[index].image != nullptr)
	{
		img->frame[0].image = impImage->frame[index].image;
	}
	else
	{
		if (img->frame[0].dataLen > 0 && impImage->frame[index].data != nullptr)
		{
			img->frame[0].data = std::make_unique<char[]>(img->frame[0].dataLen);
			memcpy(&img->frame[0].data[0], &impImage->frame[index].data[0], img->frame[0].dataLen);
		}
		else
		{
			img->frame[0].data = nullptr;
		}
	}
	return img;
}

//脟氓鲁媒IMPImage脥录脝卢脣霉脫脨脰隆
void IMP::clearIMPImage(_shared_imp impImage)
{
	if (impImage == nullptr)
	{
		return;
	}
	for (int j = 0; j < (int)impImage->frame.size(); j++)
	{
		impImage->frame[j].dataLen = 0;
		if (impImage->frame[j].image != nullptr)
		{
			//Engine::getInstance()->freeImage((_shared_image)impImage->frame[j].image);
			impImage->frame[j].image = make_shared_image(nullptr);
		}
		if (impImage->frame[j].data != nullptr)
		{
			//delete[] impImage->frame[j].data;
			impImage->frame[j].data = nullptr;
		}
		impImage->frame[j].pixelData.clear();
		impImage->frame[j].pixelWidth = 0;
		impImage->frame[j].pixelHeight = 0;
		impImage->frame[j].xOffset = 0;
		impImage->frame[j].yOffset = 0;

		for (int k = 0; k < frameNullLen; k++)
		{
			impImage->frame[j].frameNull[k] = 0;
		}
	}
	impImage->frame.resize(0);
	impImage->directions = 0;
	impImage->interval = 0;

	for (int j = 0; j < imgHeadLen; j++)
	{
		impImage->head[j] = 0;
	}

	for (int j = 0; j < imageNullLen; j++)
	{
		impImage->imageNull[j] = 0;
	}
}

_shared_image IMP::loadImage(_shared_imp impImage, int index, int * xOffset, int * yOffset)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	if (index >= 0 && index < impImage->frame.size())
	{
		if (xOffset != nullptr)
		{
			*xOffset = impImage->frame[index].xOffset;
		}
		if (yOffset != nullptr)
		{
			*yOffset = impImage->frame[index].yOffset;
		}
		if (impImage->frame[index].image != nullptr)
		{
			return impImage->frame[index].image;
		}
		else if (impImage->frame[index].data != nullptr && impImage->frame[index].dataLen > 0)
		{
			impImage->frame[index].image = Engine::getInstance()->loadImageFromMem(impImage->frame[index].data, impImage->frame[index].dataLen);
			// Decoding is deliberately deferred until the frame is first used.
			// A malformed frame becomes an empty resource at that point and must
			// not be decoded again on every render attempt.
			impImage->frame[index].dataLen = 0;
			impImage->frame[index].data = nullptr;
			return impImage->frame[index].image;
		}
		else if (!impImage->frame[index].pixelData.empty() &&
			impImage->frame[index].pixelWidth > 0 && impImage->frame[index].pixelHeight > 0)
		{
			impImage->frame[index].image = createImageFromPixels(
				impImage->frame[index].pixelData.data(),
				impImage->frame[index].pixelWidth,
				impImage->frame[index].pixelHeight);
			if (impImage->frame[index].image != nullptr)
			{
				std::vector<uint8_t>().swap(impImage->frame[index].pixelData);
				impImage->frame[index].pixelWidth = 0;
				impImage->frame[index].pixelHeight = 0;
			}
			return impImage->frame[index].image;
		}
	}
	return nullptr;
}

_shared_image IMP::loadImageForTime(_shared_imp impImage, UTime time, int * xOffset, int * yOffset, bool once, bool reverse)
{
	if (impImage == nullptr || impImage->frame.empty())
	{
		return nullptr;
	}
	const std::optional<std::size_t> index =
		ImageAnimationPlayback::frameIndex(impImage->frame.size(), 1, 0,
			time, impImage->interval, once, reverse);
	return index.has_value() && *index < impImage->frame.size()
		? loadImage(impImage, static_cast<int>(*index), xOffset, yOffset)
		: nullptr;
}

_shared_image IMP::loadImageForDirection(_shared_imp impImage, int direction, UTime time, int * xOffset, int * yOffset, bool once, bool reverse)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	if (impImage->frame.empty())
	{
		return nullptr;
	}
	const std::optional<std::size_t> index =
		ImageAnimationPlayback::frameIndex(impImage->frame.size(),
			impImage->directions, direction, time, impImage->interval,
			once, reverse);
	if (index.has_value() && *index < impImage->frame.size())
	{
		return loadImage(impImage, static_cast<int>(*index), xOffset, yOffset);
	}
	return nullptr;
}

_shared_image IMP::createImageFromPixels(const uint8_t* pixelData, int width, int height)
{
	return Engine::getInstance()->createImageFromPixelData(pixelData, width, height);
}

bool IMP::loadCommonImageFromMem(_shared_imp impImage, std::unique_ptr<char[]>& data,
	int size, bool directlyLoad)
{
	EncodedImageSafety::Dimensions dimensions;
	if (impImage == nullptr || data == nullptr || size <= 0 ||
		size > MaxEncodedImageBytes ||
		!EncodedImageSafety::inspectSafeDimensions(data.get(),
			static_cast<std::size_t>(size), dimensions))
	{
		return false;
	}

	clearIMPImage(impImage);
	memcpy(impImage->head, imgHeadString, imgHeadLen);
	impImage->directions = 1;
	impImage->interval = 0;

	impImage->frame.resize(1);
	impImage->frame[0].xOffset = 0;
	impImage->frame[0].yOffset = 0;
	impImage->frame[0].dataLen = size;
	try
	{
		impImage->frame[0].data = std::make_unique<char[]>(size);
	}
	catch (const std::bad_alloc&)
	{
		clearIMPImage(impImage);
		return false;
	}
	catch (const std::length_error&)
	{
		clearIMPImage(impImage);
		return false;
	}
	memcpy(impImage->frame[0].data.get(), data.get(), size);
	impImage->frame[0].image = nullptr;
	if (directlyLoad)
	{
		impImage->frame[0].image = Engine::getInstance()->loadImageFromMem(
			impImage->frame[0].data, impImage->frame[0].dataLen);
		if (impImage->frame[0].image == nullptr)
		{
			clearIMPImage(impImage);
			return false;
		}
		impImage->frame[0].dataLen = 0;
		impImage->frame[0].data = nullptr;
	}

	return true;
}

bool IMP::loadPicImageFromMem(_shared_imp impImage, std::unique_ptr<char[]>& data, int size, bool directlyLoad)
{
	if (impImage == nullptr || data == nullptr || size < 16)
	{
		return false;
	}

	PicDecodedFile decodedFile;
	if (!PicDecoder::decodeToPixels(reinterpret_cast<const uint8_t*>(data.get()), size, decodedFile))
	{
		return false;
	}

	if (decodedFile.frames.empty())
	{
		return false;
	}

	clearIMPImage(impImage);

	memcpy(impImage->head, imgHeadString, imgHeadLen);
	impImage->directions = decodedFile.directions;
	impImage->interval = decodedFile.interval;

	impImage->frame.resize(decodedFile.picCount);
	for (int i = 0; i < decodedFile.picCount; i++)
	{
		impImage->frame[i].xOffset = decodedFile.frames[i].xOffset;
		impImage->frame[i].yOffset = decodedFile.frames[i].yOffset;
		impImage->frame[i].dataLen = 0;
		impImage->frame[i].data = nullptr;
		impImage->frame[i].pixelWidth = decodedFile.frames[i].width;
		impImage->frame[i].pixelHeight = decodedFile.frames[i].height;
		impImage->frame[i].pixelData = std::move(decodedFile.frames[i].pixelData);

		if (directlyLoad && !impImage->frame[i].pixelData.empty())
		{
			impImage->frame[i].image = createImageFromPixels(
				impImage->frame[i].pixelData.data(),
				impImage->frame[i].pixelWidth,
				impImage->frame[i].pixelHeight);
			if (impImage->frame[i].image != nullptr)
			{
				std::vector<uint8_t>().swap(impImage->frame[i].pixelData);
				impImage->frame[i].pixelWidth = 0;
				impImage->frame[i].pixelHeight = 0;
			}
		}
		else
		{
			impImage->frame[i].image = nullptr;
		}
	}

	return true;
}

_shared_image IMP::loadImageForLastFrame(_shared_imp impImage, int direction, int * xOffset, int * yOffset, bool reverse)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	if (impImage->frame.empty())
	{
		return nullptr;
	}
	const ImageAnimationPlayback::Layout layout =
		ImageAnimationPlayback::calculateLayout(
			impImage->frame.size(), impImage->directions);
	direction = ImageAnimationPlayback::normalizeDirection(direction, layout);
	size_t index = static_cast<size_t>(direction) * layout.framesPerDirection;
	if (!reverse)
	{
		index += layout.framesPerDirection - 1;
	}
	if (index < impImage->frame.size())
	{
		return loadImage(impImage, static_cast<int>(index), xOffset, yOffset);
	}
	return nullptr;
}
