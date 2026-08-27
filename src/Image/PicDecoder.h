#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

struct SDL_Texture;

struct PicColorARGB
{
	uint8_t blue = 0;
	uint8_t green = 0;
	uint8_t red = 0;
	uint8_t alpha = 0;
};

struct PicMPCPicHead
{
	int32_t dataLen = 0;
	int32_t width = 0;
	int32_t height = 0;
	int32_t picNull[2] = {0};
};

struct PicMPCFileHead
{
	char head[16] = {0};
	int32_t mpcNull1[12] = {0};
	int32_t dataLen = 0;
	int32_t maxWidth = 0;
	int32_t maxHeight = 0;
	int32_t picCount = 0;
	int32_t directions = 0;
	int32_t paletteLen = 0;
	int32_t interval = 0;
	int32_t yMove = 0;
	int32_t mpcNull2[8] = {0};
};

static_assert(sizeof(PicMPCFileHead) == 128, "PicMPCFileHead must be 128 bytes");

struct PicASFFileHead
{
	char head[16] = {0};
	int32_t width = 0;
	int32_t height = 0;
	int32_t picCount = 0;
	int32_t directions = 0;
	int32_t paletteLen = 0;
	int32_t interval = 0;
	int32_t xMove = 0;
	int32_t yMove = 0;
	int32_t asfNull2[4] = {0};
};

static_assert(sizeof(PicASFFileHead) == 64, "PicASFFileHead must be 64 bytes");

enum class PicFormat
{
	None,
	Mpc,
	Shd,
	Asf100,
	Asf101,
};

struct PicDecodedFrame
{
	int32_t xOffset = 0;
	int32_t yOffset = 0;
	int32_t width = 0;
	int32_t height = 0;
	std::vector<uint8_t> pixelData;
};

struct PicDecodedFile
{
	PicFormat format = PicFormat::None;
	int32_t picCount = 0;
	int32_t directions = 1;
	int32_t interval = 0;
	int32_t yMove = 0;
	std::vector<PicDecodedFrame> frames;
};

class PicDecoder
{
public:
	static PicFormat detectFormat(const uint8_t* data, int size);

	static bool decodeMPC(const uint8_t* data, int size, PicDecodedFile& result);
	static bool decodeSHD(const uint8_t* data, int size, PicDecodedFile& result);
	static bool decodeASF(const uint8_t* data, int size, PicDecodedFile& result);

	static bool decodeToPixels(const uint8_t* data, int size, PicDecodedFile& result);

private:
	static void readMPCFileHead(const uint8_t* data, PicMPCFileHead& head);
	static void readMPCPicHead(const uint8_t* data, PicMPCPicHead& head);
	static void readASFFileHead(const uint8_t* data, PicASFFileHead& head);
	static void readColorARGB(const uint8_t* data, PicColorARGB& color);
	static int32_t readInt32(const uint8_t* data);

	static bool decodeMPCFrameToPixels(const uint8_t* frameData, int frameDataLen,
		int width, int height, const PicColorARGB* palette, int paletteLen,
		uint8_t* pixelData);
	static void decodeSHDFrameToPixels(const uint8_t* frameData, int frameDataLen,
		int width, int height, uint8_t* pixelData);
	static bool decodeASFFrameToPixels(const uint8_t* frameData, int frameDataLen,
		int width, int height, const PicColorARGB* palette, int paletteLen,
		uint8_t* pixelData);
};
