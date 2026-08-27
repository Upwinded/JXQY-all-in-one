#include "PicDecoder.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

static const int MPC_FILE_HEAD_SIZE = 128;
static const int LEGACY_MPC_FILE_HEAD_SIZE = 124;
static const int MPC_PIC_HEAD_SIZE = 20;
static const int ASF_FILE_HEAD_SIZE = 64;
static const int LEGACY_ASF_FILE_HEAD_SIZE = 60;
static const int COLOR_ARGB_SIZE = 4;
static const uint64_t MAX_DECODED_PIXEL_COUNT = 64ULL * 1024ULL * 1024ULL;

static const char PIC_MPC_HEAD[] = "MPC File Ver2.0";
static const char PIC_SHD_HEAD[] = "SHD File Ver2.0";
static const char PIC_ASF100_HEAD[] = "ASF 1.00";
static const char PIC_ASF101_HEAD[] = "ASF 1.01";

namespace
{
bool hasValidImageDimensions(int width, int height)
{
	return width > 0 &&
		height > 0 &&
		static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <=
			MAX_DECODED_PIXEL_COUNT;
}

void advancePixelPosition(int& x, int& y, int width, int count)
{
	if (width <= 0 || count <= 0)
	{
		return;
	}
	int64_t linearPosition = static_cast<int64_t>(y) * width + x + count;
	if (linearPosition > std::numeric_limits<int>::max())
	{
		y = std::numeric_limits<int>::max();
		x = 0;
		return;
	}
	y = static_cast<int>(linearPosition / width);
	x = static_cast<int>(linearPosition % width);
}

bool addDecodedPixelCount(uint64_t& totalPixelCount, int width, int height)
{
	if (!hasValidImageDimensions(width, height))
	{
		return false;
	}
	uint64_t framePixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
	if (framePixelCount > MAX_DECODED_PIXEL_COUNT - totalPixelCount)
	{
		return false;
	}
	totalPixelCount += framePixelCount;
	return true;
}

bool getMpcFramePayloadLength(int32_t storedDataLength, size_t frameSpan,
	int& payloadLength)
{
	if (frameSpan < static_cast<size_t>(MPC_PIC_HEAD_SIZE) || storedDataLength < 0)
	{
		return false;
	}
	size_t storedLength = static_cast<size_t>(storedDataLength);
	if (storedLength <= frameSpan - MPC_PIC_HEAD_SIZE &&
		storedLength + MPC_PIC_HEAD_SIZE == frameSpan)
	{
		// Some original XJXQY MPC files store payload length only.
		payloadLength = storedDataLength;
		return true;
	}
	if (storedLength >= static_cast<size_t>(MPC_PIC_HEAD_SIZE) && storedLength <= frameSpan)
	{
		// Standard MPC/SHD stores picture-header + payload length.
		payloadLength = storedDataLength - MPC_PIC_HEAD_SIZE;
		return true;
	}
	return false;
}
}

int32_t PicDecoder::readInt32(const uint8_t* data)
{
	return static_cast<int32_t>(
		static_cast<uint32_t>(data[0]) |
		(static_cast<uint32_t>(data[1]) << 8) |
		(static_cast<uint32_t>(data[2]) << 16) |
		(static_cast<uint32_t>(data[3]) << 24)
	);
}

void PicDecoder::readColorARGB(const uint8_t* data, PicColorARGB& color)
{
	color.blue = data[0];
	color.green = data[1];
	color.red = data[2];
	color.alpha = data[3];
}

void PicDecoder::readMPCFileHead(const uint8_t* data, PicMPCFileHead& head)
{
	int offset = 0;
	memcpy(head.head, data + offset, 16);
	offset += 16;
	for (int i = 0; i < 12; i++)
	{
		head.mpcNull1[i] = readInt32(data + offset);
		offset += 4;
	}
	head.dataLen = readInt32(data + offset);
	offset += 4;
	head.maxWidth = readInt32(data + offset);
	offset += 4;
	head.maxHeight = readInt32(data + offset);
	offset += 4;
	head.picCount = readInt32(data + offset);
	offset += 4;
	head.directions = readInt32(data + offset);
	offset += 4;
	head.paletteLen = readInt32(data + offset);
	offset += 4;
	head.interval = readInt32(data + offset);
	offset += 4;
	head.yMove = readInt32(data + offset);
	offset += 4;
	for (int i = 0; i < 8; i++)
	{
		head.mpcNull2[i] = readInt32(data + offset);
		offset += 4;
	}
}

void PicDecoder::readMPCPicHead(const uint8_t* data, PicMPCPicHead& head)
{
	int offset = 0;
	head.dataLen = readInt32(data + offset);
	offset += 4;
	head.width = readInt32(data + offset);
	offset += 4;
	head.height = readInt32(data + offset);
	offset += 4;
	head.picNull[0] = readInt32(data + offset);
	offset += 4;
	head.picNull[1] = readInt32(data + offset);
	offset += 4;
}

void PicDecoder::readASFFileHead(const uint8_t* data, PicASFFileHead& head)
{
	int offset = 0;
	memcpy(head.head, data + offset, 16);
	offset += 16;
	head.width = readInt32(data + offset);
	offset += 4;
	head.height = readInt32(data + offset);
	offset += 4;
	head.picCount = readInt32(data + offset);
	offset += 4;
	head.directions = readInt32(data + offset);
	offset += 4;
	head.paletteLen = readInt32(data + offset);
	offset += 4;
	head.interval = readInt32(data + offset);
	offset += 4;
	head.xMove = readInt32(data + offset);
	offset += 4;
	head.yMove = readInt32(data + offset);
	offset += 4;
	for (int i = 0; i < 4; i++)
	{
		head.asfNull2[i] = readInt32(data + offset);
		offset += 4;
	}
}

PicFormat PicDecoder::detectFormat(const uint8_t* data, int size)
{
	if (data == nullptr || size < 16)
	{
		return PicFormat::None;
	}

	if (memcmp(data, PIC_MPC_HEAD, 16) == 0)
	{
		return PicFormat::Mpc;
	}
	if (memcmp(data, PIC_SHD_HEAD, 16) == 0)
	{
		return PicFormat::Shd;
	}
	if (memcmp(data, PIC_ASF100_HEAD, 9) == 0)
	{
		return PicFormat::Asf100;
	}
	if (memcmp(data, PIC_ASF101_HEAD, 9) == 0)
	{
		return PicFormat::Asf101;
	}

	return PicFormat::None;
}

bool PicDecoder::decodeMPC(const uint8_t* data, int size, PicDecodedFile& result)
{
	result = PicDecodedFile();
	if (data == nullptr || size < MPC_FILE_HEAD_SIZE)
	{
		return false;
	}

	PicMPCFileHead fileHead;
	readMPCFileHead(data, fileHead);

	if (fileHead.picCount <= 0 || fileHead.picCount > 10000)
	{
		return false;
	}
	if (fileHead.paletteLen < 0 || fileHead.paletteLen > 256)
	{
		return false;
	}

	int paletteBytes = fileHead.paletteLen * COLOR_ARGB_SIZE;
	int offsetTableBytes = fileHead.picCount * 4;
	int fileHeadSize = MPC_FILE_HEAD_SIZE;
	bool absoluteOffsets = false;
	auto selectLayout = [&](int candidateHeadSize) {
		int tableOffset = candidateHeadSize + paletteBytes;
		if (tableOffset < 0 ||
			tableOffset > size ||
			offsetTableBytes > size - tableOffset)
		{
			return false;
		}

		int dataStart = tableOffset + offsetTableBytes;
		int32_t firstOffset = readInt32(data + tableOffset);
		if (firstOffset == 0)
		{
			fileHeadSize = candidateHeadSize;
			absoluteOffsets = false;
			return true;
		}
		if (firstOffset == dataStart)
		{
			fileHeadSize = candidateHeadSize;
			absoluteOffsets = true;
			return true;
		}
		return false;
	};
	if (!selectLayout(MPC_FILE_HEAD_SIZE) &&
		!selectLayout(LEGACY_MPC_FILE_HEAD_SIZE))
	{
		return false;
	}

	const uint8_t* readPtr = data + fileHeadSize;

	std::vector<PicColorARGB> palette;
	if (fileHead.paletteLen > 0 && fileHead.paletteLen <= 256)
	{
		palette.resize(fileHead.paletteLen);
		if (readPtr + paletteBytes > data + size)
		{
			return false;
		}
		for (int i = 0; i < fileHead.paletteLen; i++)
		{
			readColorARGB(readPtr + i * COLOR_ARGB_SIZE, palette[i]);
		}
		readPtr += paletteBytes;
	}

	if (readPtr + fileHead.picCount * 4 > data + size)
	{
		return false;
	}

	std::vector<int32_t> frameOffsets(fileHead.picCount);
	for (int i = 0; i < fileHead.picCount; i++)
	{
		frameOffsets[i] = readInt32(readPtr + i * 4);
		if (frameOffsets[i] < 0)
		{
			return false;
		}
	}
	readPtr += fileHead.picCount * 4;
	const uint8_t* frameDataStart = readPtr;

	std::vector<PicMPCPicHead> frameHeads(fileHead.picCount);
	std::vector<size_t> framePositions(fileHead.picCount);
	std::vector<int> frameDataLens(fileHead.picCount);
	uint64_t totalPixelCount = 0;
	for (int i = 0; i < fileHead.picCount; i++)
	{
		framePositions[i] = absoluteOffsets
			? static_cast<size_t>(frameOffsets[i])
			: static_cast<size_t>(frameDataStart - data) +
				static_cast<size_t>(frameOffsets[i]);
		if (framePositions[i] < static_cast<size_t>(frameDataStart - data) ||
			framePositions[i] > static_cast<size_t>(size) ||
			static_cast<size_t>(size) - framePositions[i] < MPC_PIC_HEAD_SIZE)
		{
			return false;
		}
	}
	std::vector<size_t> sortedFramePositions = framePositions;
	std::sort(sortedFramePositions.begin(), sortedFramePositions.end());
	sortedFramePositions.erase(
		std::unique(sortedFramePositions.begin(), sortedFramePositions.end()),
		sortedFramePositions.end());
	for (int i = 0; i < fileHead.picCount; i++)
	{
		const uint8_t* framePtr = data + framePositions[i];
		readMPCPicHead(framePtr, frameHeads[i]);
		size_t frameEnd = static_cast<size_t>(size);
		auto nextFrame = std::upper_bound(sortedFramePositions.begin(),
			sortedFramePositions.end(), framePositions[i]);
		if (nextFrame != sortedFramePositions.end())
		{
			frameEnd = *nextFrame;
		}
		if (!getMpcFramePayloadLength(frameHeads[i].dataLen,
			frameEnd - framePositions[i], frameDataLens[i]))
		{
			return false;
		}
		if (frameDataLens[i] > 0 &&
			!addDecodedPixelCount(totalPixelCount, frameHeads[i].width, frameHeads[i].height))
		{
			return false;
		}
	}

	result.format = PicFormat::Mpc;
	result.picCount = fileHead.picCount;
	result.directions = fileHead.directions > 0 ? fileHead.directions : 1;
	result.interval = fileHead.interval;
	result.yMove = fileHead.yMove;
	result.frames.resize(fileHead.picCount);
	for (int i = 0; i < fileHead.picCount; i++)
	{
		if (frameDataLens[i] <= 0)
		{
			continue;
		}
		const PicMPCPicHead& picHead = frameHeads[i];
		result.frames[i].width = picHead.width;
		result.frames[i].height = picHead.height;
		result.frames[i].xOffset = picHead.width / 2;
		result.frames[i].yOffset = picHead.height - fileHead.yMove;
		size_t pixelBytes = static_cast<size_t>(picHead.width) * picHead.height * 4;
		try
		{
			result.frames[i].pixelData.resize(pixelBytes);
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		const uint8_t* framePtr = data + framePositions[i];
		if (!decodeMPCFrameToPixels(framePtr + MPC_PIC_HEAD_SIZE, frameDataLens[i],
			picHead.width, picHead.height, palette.data(), fileHead.paletteLen,
			result.frames[i].pixelData.data()))
		{
			return false;
		}
	}

	return true;
}

bool PicDecoder::decodeSHD(const uint8_t* data, int size, PicDecodedFile& result)
{
	result = PicDecodedFile();
	if (data == nullptr || size < MPC_FILE_HEAD_SIZE)
	{
		return false;
	}

	PicMPCFileHead fileHead;
	readMPCFileHead(data, fileHead);

	if (fileHead.picCount <= 0 || fileHead.picCount > 10000)
	{
		return false;
	}

	int offsetTableBytes = fileHead.picCount * 4;
	int fileHeadSize = MPC_FILE_HEAD_SIZE;
	bool absoluteOffsets = false;
	auto selectLayout = [&](int candidateHeadSize) {
		if (candidateHeadSize > size ||
			offsetTableBytes > size - candidateHeadSize)
		{
			return false;
		}

		int dataStart = candidateHeadSize + offsetTableBytes;
		int32_t firstOffset = readInt32(data + candidateHeadSize);
		if (firstOffset == 0)
		{
			fileHeadSize = candidateHeadSize;
			absoluteOffsets = false;
			return true;
		}
		if (firstOffset == dataStart)
		{
			fileHeadSize = candidateHeadSize;
			absoluteOffsets = true;
			return true;
		}
		return false;
	};
	if (!selectLayout(MPC_FILE_HEAD_SIZE) &&
		!selectLayout(LEGACY_MPC_FILE_HEAD_SIZE))
	{
		return false;
	}

	const uint8_t* readPtr = data + fileHeadSize;
	std::vector<int32_t> frameOffsets(fileHead.picCount);
	for (int i = 0; i < fileHead.picCount; i++)
	{
		frameOffsets[i] = readInt32(readPtr + i * 4);
		if (frameOffsets[i] < 0)
		{
			return false;
		}
	}
	readPtr += fileHead.picCount * 4;
	const uint8_t* frameDataStart = readPtr;

	std::vector<PicMPCPicHead> frameHeads(fileHead.picCount);
	std::vector<size_t> framePositions(fileHead.picCount);
	std::vector<int> frameDataLens(fileHead.picCount);
	uint64_t totalPixelCount = 0;
	for (int i = 0; i < fileHead.picCount; i++)
	{
		framePositions[i] = absoluteOffsets
			? static_cast<size_t>(frameOffsets[i])
			: static_cast<size_t>(frameDataStart - data) +
				static_cast<size_t>(frameOffsets[i]);
		if (framePositions[i] < static_cast<size_t>(frameDataStart - data) ||
			framePositions[i] > static_cast<size_t>(size) ||
			static_cast<size_t>(size) - framePositions[i] < MPC_PIC_HEAD_SIZE)
		{
			return false;
		}
	}
	std::vector<size_t> sortedFramePositions = framePositions;
	std::sort(sortedFramePositions.begin(), sortedFramePositions.end());
	sortedFramePositions.erase(
		std::unique(sortedFramePositions.begin(), sortedFramePositions.end()),
		sortedFramePositions.end());
	for (int i = 0; i < fileHead.picCount; i++)
	{
		const uint8_t* framePtr = data + framePositions[i];
		readMPCPicHead(framePtr, frameHeads[i]);
		size_t frameEnd = static_cast<size_t>(size);
		auto nextFrame = std::upper_bound(sortedFramePositions.begin(),
			sortedFramePositions.end(), framePositions[i]);
		if (nextFrame != sortedFramePositions.end())
		{
			frameEnd = *nextFrame;
		}
		if (!getMpcFramePayloadLength(frameHeads[i].dataLen,
			frameEnd - framePositions[i], frameDataLens[i]))
		{
			return false;
		}
		if (frameDataLens[i] > 0 &&
			!addDecodedPixelCount(totalPixelCount, frameHeads[i].width, frameHeads[i].height))
		{
			return false;
		}
	}

	result.format = PicFormat::Shd;
	result.picCount = fileHead.picCount;
	result.directions = fileHead.directions > 0 ? fileHead.directions : 1;
	result.interval = fileHead.interval;
	result.yMove = fileHead.yMove;
	result.frames.resize(fileHead.picCount);
	for (int i = 0; i < fileHead.picCount; i++)
	{
		if (frameDataLens[i] <= 0)
		{
			continue;
		}
		const PicMPCPicHead& picHead = frameHeads[i];
		result.frames[i].width = picHead.width;
		result.frames[i].height = picHead.height;
		result.frames[i].xOffset = picHead.width / 2;
		result.frames[i].yOffset = picHead.height - fileHead.yMove;
		size_t pixelBytes = static_cast<size_t>(picHead.width) * picHead.height * 4;
		try
		{
			result.frames[i].pixelData.resize(pixelBytes);
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
		const uint8_t* framePtr = data + framePositions[i];
		decodeSHDFrameToPixels(framePtr + MPC_PIC_HEAD_SIZE, frameDataLens[i],
			picHead.width, picHead.height, result.frames[i].pixelData.data());
	}

	return true;
}

bool PicDecoder::decodeASF(const uint8_t* data, int size, PicDecodedFile& result)
{
	result = PicDecodedFile();
	if (data == nullptr || size < ASF_FILE_HEAD_SIZE)
	{
		return false;
	}

	PicASFFileHead fileHead;
	readASFFileHead(data, fileHead);

	if (fileHead.picCount <= 0 || fileHead.picCount > 10000)
	{
		return false;
	}
	if (fileHead.paletteLen < 0 || fileHead.paletteLen > 256)
	{
		return false;
	}
	if (!hasValidImageDimensions(fileHead.width, fileHead.height))
	{
		return false;
	}
	uint64_t framePixelCount = static_cast<uint64_t>(fileHead.width) * fileHead.height;
	if (framePixelCount > MAX_DECODED_PIXEL_COUNT / static_cast<uint64_t>(fileHead.picCount))
	{
		return false;
	}

	int paletteBytes = fileHead.paletteLen * COLOR_ARGB_SIZE;
	int tableBytes = fileHead.picCount * 8;
	int fileHeadSize = ASF_FILE_HEAD_SIZE;
	auto selectLayout = [&](int candidateHeadSize) {
		int tableOffset = candidateHeadSize + paletteBytes;
		if (tableOffset < 0 ||
			tableOffset > size ||
			tableBytes > size - tableOffset)
		{
			return false;
		}

		int dataStart = tableOffset + tableBytes;
		return readInt32(data + tableOffset) == dataStart;
	};
	if (selectLayout(ASF_FILE_HEAD_SIZE))
	{
		fileHeadSize = ASF_FILE_HEAD_SIZE;
	}
	else if (selectLayout(LEGACY_ASF_FILE_HEAD_SIZE))
	{
		fileHeadSize = LEGACY_ASF_FILE_HEAD_SIZE;
	}
	else
	{
		return false;
	}

	const uint8_t* readPtr = data + fileHeadSize;

	std::vector<PicColorARGB> palette;
	if (fileHead.paletteLen > 0 && fileHead.paletteLen <= 256)
	{
		palette.resize(fileHead.paletteLen);
		if (readPtr + paletteBytes > data + size)
		{
			return false;
		}
		for (int i = 0; i < fileHead.paletteLen; i++)
		{
			readColorARGB(readPtr + i * COLOR_ARGB_SIZE, palette[i]);
		}
		readPtr += paletteBytes;
	}

	result.format = (memcmp(data, PIC_ASF101_HEAD, 9) == 0) ? PicFormat::Asf101 : PicFormat::Asf100;
	result.picCount = fileHead.picCount;
	result.directions = fileHead.directions > 0 ? fileHead.directions : 1;
	result.interval = fileHead.interval;
	result.yMove = fileHead.yMove;
	result.frames.resize(fileHead.picCount);

	std::vector<int32_t> frameOffsets(fileHead.picCount);
	std::vector<int32_t> frameDataLens(fileHead.picCount);
	for (int i = 0; i < fileHead.picCount; i++)
	{
		if (readPtr + 8 > data + size)
		{
			return false;
		}

		frameOffsets[i] = readInt32(readPtr);
		readPtr += 4;

		frameDataLens[i] = readInt32(readPtr);
		readPtr += 4;
		if (frameOffsets[i] < 0 || frameDataLens[i] < 0)
		{
			return false;
		}

		result.frames[i].width = fileHead.width;
		result.frames[i].height = fileHead.height;
		result.frames[i].xOffset = fileHead.xMove;
		result.frames[i].yOffset = fileHead.yMove + 16;
	}

	size_t frameDataStart = static_cast<size_t>(readPtr - data);
	for (int i = 0; i < fileHead.picCount; i++)
	{
		if (static_cast<size_t>(frameOffsets[i]) < frameDataStart)
		{
			return false;
		}
	}

	for (int i = 0; i < fileHead.picCount; i++)
	{
		int dataLen = frameDataLens[i];
		size_t framePosition = static_cast<size_t>(frameOffsets[i]);
		if (framePosition > static_cast<size_t>(size) ||
			static_cast<size_t>(dataLen) >
				static_cast<size_t>(size) - framePosition)
		{
			return false;
		}
		const uint8_t* framePtr = data + framePosition;

		if (dataLen > 0)
		{
			size_t pixelBytes =
				static_cast<size_t>(fileHead.width) * fileHead.height * 4;
			try
			{
				result.frames[i].pixelData.resize(pixelBytes);
			}
			catch (const std::bad_alloc&)
			{
				return false;
			}
			if (!decodeASFFrameToPixels(framePtr, dataLen,
				fileHead.width, fileHead.height,
				palette.data(), fileHead.paletteLen,
				result.frames[i].pixelData.data()))
			{
				return false;
			}
		}
	}

	return true;
}

bool PicDecoder::decodeToPixels(const uint8_t* data, int size, PicDecodedFile& result)
{
	PicFormat format = detectFormat(data, size);

	switch (format)
	{
	case PicFormat::Mpc:
		return decodeMPC(data, size, result);
	case PicFormat::Shd:
		return decodeSHD(data, size, result);
	case PicFormat::Asf100:
	case PicFormat::Asf101:
		return decodeASF(data, size, result);
	default:
		return false;
	}
}

bool PicDecoder::decodeMPCFrameToPixels(const uint8_t* frameData, int frameDataLen,
	int width, int height, const PicColorARGB* palette, int paletteLen,
	uint8_t* pixelData)
{
	memset(pixelData, 0, width * height * 4);

	int x = 0;
	int y = 0;
	int state = 0;

	for (int i = 0; i < frameDataLen && y < height; i++)
	{
		uint8_t byteValue = frameData[i];

		if (state == 0)
		{
			if (byteValue <= 128)
			{
				state = byteValue;
			}
			else
			{
				advancePixelPosition(x, y, width, byteValue - 128);
			}
		}
		else
		{
			if (x >= 0 && y >= 0 && x < width && y < height)
			{
				int pixelOffset = (y * width + x) * 4;
				if (static_cast<int>(byteValue) < paletteLen)
				{
					const PicColorARGB& color = palette[byteValue];
					pixelData[pixelOffset + 0] = color.blue;
					pixelData[pixelOffset + 1] = color.green;
					pixelData[pixelOffset + 2] = color.red;
					pixelData[pixelOffset + 3] = 255;
				}
			}
			advancePixelPosition(x, y, width, 1);
			state--;
		}
	}
	return state == 0;
}

void PicDecoder::decodeSHDFrameToPixels(const uint8_t* frameData, int frameDataLen,
	int width, int height, uint8_t* pixelData)
{
	memset(pixelData, 0, width * height * 4);

	int x = 0;
	int y = 0;

	for (int i = 0; i < frameDataLen && y < height; i++)
	{
		uint8_t byteValue = frameData[i];

		if (byteValue <= 128)
		{
			for (int state = 0; state < byteValue; state++)
			{
				if (x >= 0 && y >= 0 && x < width && y < height)
				{
					int pixelOffset = (y * width + x) * 4;
					pixelData[pixelOffset + 0] = 0;
					pixelData[pixelOffset + 1] = 0;
					pixelData[pixelOffset + 2] = 0;
					pixelData[pixelOffset + 3] = 128;
				}
				advancePixelPosition(x, y, width, 1);
			}
		}
		else
		{
			advancePixelPosition(x, y, width, byteValue - 128);
		}
	}
}

bool PicDecoder::decodeASFFrameToPixels(const uint8_t* frameData, int frameDataLen,
	int width, int height, const PicColorARGB* palette, int paletteLen,
	uint8_t* pixelData)
{
	memset(pixelData, 0, width * height * 4);

	int x = 0;
	int y = 0;
	int i = 0;

	while (i < frameDataLen - 1)
	{
		uint8_t count = frameData[i];
		uint8_t alpha = frameData[i + 1];
		i += 2;

		if (alpha == 0)
		{
			advancePixelPosition(x, y, width, count);
		}
		else
		{
			if (count > frameDataLen - i)
			{
				return false;
			}
			for (int j = 0; j < count; j++)
			{
				uint8_t paletteIndex = frameData[i];
				i++;

				if (x >= 0 && y >= 0 && x < width && y < height)
				{
					int pixelOffset = (y * width + x) * 4;
					if (static_cast<int>(paletteIndex) < paletteLen)
					{
						const PicColorARGB& color = palette[paletteIndex];
						pixelData[pixelOffset + 0] = color.blue;
						pixelData[pixelOffset + 1] = color.green;
						pixelData[pixelOffset + 2] = color.red;
						pixelData[pixelOffset + 3] = alpha;
					}
				}
				advancePixelPosition(x, y, width, 1);
			}
		}
	}
	return true;
}
