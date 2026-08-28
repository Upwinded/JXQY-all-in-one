#include "../Engine/Engine.h"
#include "../Engine/AspectFitLayout.h"
#include "../Engine/AudioDecodeSafety.h"
#include "../Component/Panel.h"
#include "../Component/VideoPlayer.h"
#include "../File/File.h"
#include "../Game/Script/ScriptAPI.h"
#include "../Image/IMP.h"
#include "../Image/PngImageEncoder.h"
#include "../Image/SafeImageDecoder.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

namespace
{
class TestVideoPlayer : public VideoPlayer
{
public:
	using VideoPlayer::onExit;
	using VideoPlayer::onRun;
};

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

void writeAudioUint16(std::vector<uint8_t>& data, size_t offset, uint16_t value)
{
	data[offset] = static_cast<uint8_t>(value & 0xFF);
	data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeAudioUint32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
	for (size_t byteIndex = 0; byteIndex < 4; byteIndex++)
	{
		data[offset + byteIndex] = static_cast<uint8_t>(value >> (byteIndex * 8));
	}
}

bool runAudioDecodeSafetyTests()
{
	bool ok = true;
	uint8_t bytes[16] = {};
	AudioDecodeSafety::MemoryReader reader{ bytes, 16, 8 };
	ok = check(AudioDecodeSafety::seekPacket(&reader, INT64_MAX, SEEK_CUR) == -1 &&
		reader.position == 8,
		"audio memory seek rejects positive signed overflow") && ok;
	ok = check(AudioDecodeSafety::seekPacket(&reader, INT64_MIN, SEEK_CUR) == -1 &&
		reader.position == 8,
		"audio memory seek rejects negative signed overflow") && ok;
	ok = check(AudioDecodeSafety::seekPacket(&reader, INT64_MAX, SEEK_END) == -1 &&
		reader.position == 8,
		"audio memory end-relative seek rejects signed overflow") && ok;
	ok = check(AudioDecodeSafety::seekPacket(&reader, 16, SEEK_SET) == 16 &&
		AudioDecodeSafety::readPacket(&reader, bytes, 1) < 0,
		"audio memory reader reports EOF at the validated boundary") && ok;
	reader.position = 17;
	ok = check(AudioDecodeSafety::readPacket(&reader, bytes, 1) < 0 &&
		AudioDecodeSafety::seekPacket(&reader, 0, SEEK_SET) == -1,
		"audio memory callbacks reject corrupt reader state") && ok;
	ok = check(AudioDecodeSafety::canAppendDecodedBytes(
		AudioDecodeSafety::MaxDecodedAudioBytes - 1, 1) &&
		!AudioDecodeSafety::canAppendDecodedBytes(
			AudioDecodeSafety::MaxDecodedAudioBytes, 1) &&
		!AudioDecodeSafety::canAppendDecodedBytes(
			AudioDecodeSafety::MaxDecodedAudioBytes + 1, 0),
		"decoded audio budget uses checked cumulative addition") && ok;

	std::vector<uint8_t> wave(46, 0);
	std::memcpy(wave.data(), "RIFF", 4);
	writeAudioUint32(wave, 4, 38);
	std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
	writeAudioUint32(wave, 16, 16);
	writeAudioUint16(wave, 20, 1);
	writeAudioUint16(wave, 22, 1);
	writeAudioUint32(wave, 24, 8000);
	writeAudioUint32(wave, 28, 16000);
	writeAudioUint16(wave, 32, 2);
	writeAudioUint16(wave, 34, 16);
	std::memcpy(wave.data() + 36, "data", 4);
	writeAudioUint32(wave, 40, 2);
	AudioBuffer decodedWave;
	ok = check(AudioDecodeSafety::decodeFromMemory(wave.data(),
		static_cast<int>(wave.size()), false, true, decodedWave) &&
		decodedWave.spec.freq == 8000 && decodedWave.spec.channels == 2 &&
		decodedWave.data.size() == 4,
		"bounded audio decoder converts a minimal PCM wave to stereo S16") && ok;

	namespace fs = std::filesystem;
	fs::path repositoryRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();
	const std::vector<fs::path> compressedAudioPaths = {
		repositoryRoot / "assets" / "xjxqy" / "music" / fs::u8path(u8"满江红.ogg"),
		repositoryRoot / "assets" / "jxqy2" / "music" / "ks69.mp3",
	};
	for (const auto& compressedAudioPath : compressedAudioPaths)
	{
		if (!fs::exists(compressedAudioPath))
		{
			continue;
		}
		std::ifstream input(compressedAudioPath, std::ios::binary);
		std::vector<uint8_t> compressedAudio(
			(std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		AudioBuffer decodedCompressedAudio;
		ok = check(!compressedAudio.empty() &&
			AudioDecodeSafety::decodeFromMemory(compressedAudio.data(),
				static_cast<int>(compressedAudio.size()), true, false,
				decodedCompressedAudio) &&
			!decodedCompressedAudio.data.empty() &&
			decodedCompressedAudio.data.size() <= AudioDecodeSafety::MaxDecodedAudioBytes,
			"custom AVIO decoder loads representative OGG and MP3 resources within budget") && ok;
	}
	return ok;
}

void writeInt32(std::vector<uint8_t>& data, size_t offset, int32_t value)
{
	data[offset + 0] = static_cast<uint8_t>(value & 0xFF);
	data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
	data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
	data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

std::vector<uint8_t> makePngDimensionHeader(uint32_t width, uint32_t height)
{
	std::vector<uint8_t> data(24, 0);
	const uint8_t signature[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::memcpy(data.data(), signature, sizeof(signature));
	data[11] = 13;
	std::memcpy(data.data() + 12, "IHDR", 4);
	for (size_t index = 0; index < 4; index++)
	{
		data[16 + index] = static_cast<uint8_t>(width >> ((3 - index) * 8));
		data[20 + index] = static_cast<uint8_t>(height >> ((3 - index) * 8));
	}
	return data;
}

std::unique_ptr<char[]> copyBytes(const std::vector<uint8_t>& bytes)
{
	auto data = std::make_unique<char[]>(bytes.size());
	std::memcpy(data.get(), bytes.data(), bytes.size());
	return data;
}

std::vector<uint8_t> makeEmbeddedImageContainer(const std::vector<uint8_t>& frameBytes)
{
	std::vector<uint8_t> image(48 + 16 + frameBytes.size(), 0);
	std::memcpy(image.data(), "IMG File Ver1.0", 16);
	writeInt32(image, 16, 1);
	writeInt32(image, 20, 1);
	writeInt32(image, 48, static_cast<int32_t>(frameBytes.size()));
	std::memcpy(image.data() + 64, frameBytes.data(), frameBytes.size());
	return image;
}

std::unique_ptr<char[]> makeOnePixelMpc(int& size)
{
	constexpr size_t HeaderSize = 128;
	constexpr size_t PaletteSize = 4;
	constexpr size_t OffsetTableSize = 4;
	constexpr size_t PictureHeaderSize = 20;
	const std::vector<uint8_t> payload = { 1, 0 };
	size = static_cast<int>(HeaderSize + PaletteSize + OffsetTableSize +
		PictureHeaderSize + payload.size());
	auto data = std::make_unique<char[]>(size);
	std::memset(data.get(), 0, size);
	auto bytes = reinterpret_cast<uint8_t*>(data.get());
	std::memcpy(bytes, "MPC File Ver2.0", 16);
	std::vector<uint8_t> header(bytes, bytes + size);
	writeInt32(header, 68, 1);
	writeInt32(header, 72, 1);
	writeInt32(header, 76, 1);
	writeInt32(header, 80, 8);
	writeInt32(header, 84, 1);
	writeInt32(header, 88, 66);
	const size_t frameOffset = HeaderSize + PaletteSize + OffsetTableSize;
	writeInt32(header, HeaderSize + PaletteSize, 0);
	writeInt32(header, frameOffset, static_cast<int32_t>(PictureHeaderSize + payload.size()));
	writeInt32(header, frameOffset + 4, 1);
	writeInt32(header, frameOffset + 8, 1);
	std::copy(payload.begin(), payload.end(), header.begin() + frameOffset + PictureHeaderSize);
	std::memcpy(data.get(), header.data(), header.size());
	return data;
}

bool runAnimationLayoutTests()
{
	bool ok = true;
	auto singleFrame = std::make_shared<IMPImage>();
	singleFrame->directions = 8;
	singleFrame->interval = 66;
	singleFrame->frame.resize(1);
	singleFrame->frame[0].xOffset = 17;
	ok = check(IMP::getIMPImageActionTime(singleFrame) == 66,
		"one-frame/eight-direction asset keeps one real frame interval") && ok;
	int xOffset = 0;
	IMP::loadImageForDirection(singleFrame, 7, 0, &xOffset);
	ok = check(xOffset == 17,
		"one-frame/eight-direction asset normalizes to one effective direction") && ok;

	auto zeroInterval = std::make_shared<IMPImage>();
	zeroInterval->directions = 1;
	zeroInterval->interval = 0;
	zeroInterval->frame.resize(15);
	for (int index = 0; index < 15; index++)
	{
		zeroInterval->frame[index].xOffset = index;
	}
	ok = check(IMP::getIMPImageActionTime(zeroInterval) == 240,
		"legacy zero-interval animation uses the reference 60 Hz cadence") && ok;
	IMP::loadImageForDirection(zeroInterval, 0, 15, &xOffset);
	ok = check(xOffset == 0, "zero-interval animation stays on its first frame before one legacy tick") && ok;
	IMP::loadImageForDirection(zeroInterval, 0, 16, &xOffset);
	ok = check(xOffset == 1, "zero-interval animation advances after one legacy tick") && ok;
	IMP::loadImageForDirection(zeroInterval, 0, 240, &xOffset, nullptr, true);
	ok = check(xOffset == 14, "once animation clamps to the last frame") && ok;
	IMP::loadImageForDirection(zeroInterval, 0, 0, &xOffset, nullptr, false, true);
	ok = check(xOffset == 14, "reverse animation starts at the last frame") && ok;

	auto oneFrameDirections = std::make_shared<IMPImage>();
	oneFrameDirections->directions = 8;
	oneFrameDirections->interval = 0;
	oneFrameDirections->frame.resize(8);
	for (int index = 0; index < 8; index++)
	{
		oneFrameDirections->frame[index].xOffset = index;
	}
	ok = check(IMP::getIMPImageActionTime(oneFrameDirections) == 16,
		"one-frame-per-direction zero interval has a nonzero action duration") && ok;
	IMP::loadImageForDirection(oneFrameDirections, 5, 200, &xOffset);
	ok = check(xOffset == 5, "direction selection remains stable with one frame per direction") && ok;

	auto overflowingDuration = std::make_shared<IMPImage>();
	overflowingDuration->directions = 1;
	overflowingDuration->interval = INT_MAX;
	overflowingDuration->frame.resize(3);
	ok = check(IMP::getIMPImageActionTime(overflowingDuration) == UINT_MAX,
		"animation duration multiplication saturates instead of wrapping") && ok;

	namespace fs = std::filesystem;
	fs::path repositoryRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();
	fs::path realZeroIntervalPath = repositoryRoot / "assets" / "xjxqy" / "asf" /
		"character" / fs::u8path(u8"npc067_wlk.asf");
	if (fs::exists(realZeroIntervalPath))
	{
		std::ifstream input(realZeroIntervalPath, std::ios::binary);
		std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		auto data = std::make_unique<char[]>(bytes.size());
		if (!bytes.empty())
		{
			std::memcpy(data.get(), bytes.data(), bytes.size());
		}
		auto realZeroInterval = IMP::createIMPImageFromMem(data,
			static_cast<int>(bytes.size()), false);
		ok = check(realZeroInterval != nullptr &&
			realZeroInterval->frame.size() == 232 &&
			realZeroInterval->directions == 8 &&
			realZeroInterval->interval == 0 &&
			IMP::getIMPImageActionTime(realZeroInterval) == 29 * 16,
			"production zero-interval character preserves its 8 direction groups and 60 Hz duration") && ok;
		if (realZeroInterval != nullptr)
		{
			bool hasDistinctOffsets = false;
			for (size_t index = 1; index < realZeroInterval->frame.size(); index++)
			{
				if (realZeroInterval->frame[index].xOffset != realZeroInterval->frame[0].xOffset ||
					realZeroInterval->frame[index].yOffset != realZeroInterval->frame[0].yOffset)
				{
					hasDistinctOffsets = true;
					break;
				}
			}
			ok = check(hasDistinctOffsets,
				"production zero-interval character retains per-frame anchor offsets") && ok;
		}
	}
	return ok;
}

bool runPanelLayoutTests()
{
	bool ok = true;

	const std::string scaledPanelText =
		"[Init]\nLeft=0\nTop=0\nWidth=285\nHeight=27\nScale=2\n";
	auto scaledPanelData = std::make_unique<char[]>(scaledPanelText.size() + 1);
	std::memcpy(scaledPanelData.get(), scaledPanelText.c_str(),
		scaledPanelText.size() + 1);
	INIReader scaledPanelIni(scaledPanelData);
	auto scaledPanel = std::make_shared<Panel>();
	scaledPanel->initFromIni(scaledPanelIni);
	auto scaledChild = std::make_shared<Panel>();
	scaledChild->rect = { 52, 0, 19, 19 };
	scaledPanel->addChild(scaledChild);
	scaledPanel->setChildRectReferToParent();
	ok = check(scaledPanel->rect.w == 570 && scaledPanel->rect.h == 54 &&
		scaledPanel->baseWidth == 285 && scaledPanel->baseHeight == 27 &&
		scaledChild->rect.x == 104 && scaledChild->rect.y == 0 &&
		scaledChild->rect.w == 38 && scaledChild->rect.h == 38,
		"panel Scale enlarges the panel and its child layout from the source geometry") && ok;

	const std::string overflowPanelText =
		"[Init]\nWidth=640\nHeight=480\nScale=100000000\n";
	auto overflowPanelData = std::make_unique<char[]>(overflowPanelText.size() + 1);
	std::memcpy(overflowPanelData.get(), overflowPanelText.c_str(),
		overflowPanelText.size() + 1);
	INIReader overflowPanelIni(overflowPanelData);
	auto overflowPanel = std::make_shared<Panel>();
	overflowPanel->initFromIni(overflowPanelIni);
	ok = check(overflowPanel->scale == 1.0f &&
		overflowPanel->rect.w == 640 && overflowPanel->rect.h == 480,
		"panel ignores a Scale value that would overflow its geometry") && ok;

	const std::string aspectPanelText =
		"[Init]\nWidth=640\nHeight=480\nKeepAspect=true\n"
		"FadeMirroredBars=true\n";
	auto aspectPanelData = std::make_unique<char[]>(aspectPanelText.size() + 1);
	std::memcpy(aspectPanelData.get(), aspectPanelText.c_str(),
		aspectPanelText.size() + 1);
	INIReader aspectPanelIni(aspectPanelData);
	auto aspectPanel = std::make_shared<Panel>();
	aspectPanel->initFromIni(aspectPanelIni);
	aspectPanel->rect = { 0, 0, 1280, 720 };
	auto aspectChild = std::make_shared<Panel>();
	aspectChild->rect = { 327, 112, 81, 66 };
	aspectPanel->addChild(aspectChild);
	aspectPanel->setChildRectReferToParent();
	ok = check(aspectPanel->keepAspect &&
		aspectPanel->fadeMirroredBars &&
		aspectChild->rect.x == 650 && aspectChild->rect.y == 168 &&
		aspectChild->rect.w == 121 && aspectChild->rect.h == 99,
		"aspect-fit panel applies one centered 640x480 transform to its children") && ok;

	auto tallAspectPanelData = std::make_unique<char[]>(aspectPanelText.size() + 1);
	std::memcpy(tallAspectPanelData.get(), aspectPanelText.c_str(),
		aspectPanelText.size() + 1);
	INIReader tallAspectPanelIni(tallAspectPanelData);
	auto tallAspectPanel = std::make_shared<Panel>();
	tallAspectPanel->initFromIni(tallAspectPanelIni);
	tallAspectPanel->rect = { 0, 0, 720, 1280 };
	auto tallAspectChild = std::make_shared<Panel>();
	tallAspectChild->rect = { 0, 0, 640, 480 };
	tallAspectPanel->addChild(tallAspectChild);
	tallAspectPanel->setChildRectReferToParent();
	ok = check(tallAspectChild->rect.x == 0 &&
		tallAspectChild->rect.y == 370 &&
		tallAspectChild->rect.w == 720 &&
		tallAspectChild->rect.h == 540,
		"aspect-fit panel centers its complete composition vertically") && ok;

	return ok;
}

bool runLazyRawImageTest()
{
	int size = 0;
	auto data = makeOnePixelMpc(size);
	auto image = IMP::createIMPImageFromMem(data, size, false);
	bool ok = true;
	ok = check(image != nullptr && image->frame.size() == 1,
		"raw MPC fixture creates an IMP image") && ok;
	if (image == nullptr || image->frame.empty())
	{
		return false;
	}
	ok = check(image->frame[0].image == nullptr,
		"lazy raw MPC load does not create an SDL texture on the loading thread") && ok;
	ok = check(image->frame[0].pixelWidth == 1 && image->frame[0].pixelHeight == 1 &&
		image->frame[0].pixelData.size() == 4,
		"lazy raw MPC load retains decoded pixels for first render") && ok;
	auto frameCopy = IMP::createIMPImageFromFrame(image, 0);
	ok = check(frameCopy != nullptr && frameCopy->frame[0].pixelData.size() == 4,
		"frame extraction preserves lazy decoded pixels") && ok;

	auto oversizedPngBytes = makePngDimensionHeader(100000, 100000);
	auto oversizedPng = copyBytes(oversizedPngBytes);
	ok = check(IMP::createIMPImageFromMem(oversizedPng,
		static_cast<int>(oversizedPngBytes.size()), false) == nullptr,
		"lazy common image rejects oversized PNG dimensions before decode") && ok;
	std::vector<uint8_t> oversizedGifBytes(26, 0);
	std::memcpy(oversizedGifBytes.data(), "GIF89a", 6);
	oversizedGifBytes[6] = 1;
	oversizedGifBytes[8] = 1;
	oversizedGifBytes[13] = 0x2C;
	oversizedGifBytes[18] = 0x20;
	oversizedGifBytes[19] = 0x4E;
	oversizedGifBytes[20] = 0x20;
	oversizedGifBytes[21] = 0x4E;
	oversizedGifBytes[23] = 2;
	oversizedGifBytes[25] = 0x3B;
	auto oversizedGif = copyBytes(oversizedGifBytes);
	ok = check(IMP::createIMPImageFromMem(oversizedGif,
		static_cast<int>(oversizedGifBytes.size()), false) == nullptr,
		"lazy GIF uses image-descriptor dimensions rather than logical screen only") && ok;
	std::vector<uint8_t> oversizedQoiBytes(14, 0);
	std::memcpy(oversizedQoiBytes.data(), "qoif", 4);
	for (size_t index = 0; index < 4; index++)
	{
		oversizedQoiBytes[4 + index] = static_cast<uint8_t>(100000U >> ((3 - index) * 8));
		oversizedQoiBytes[8 + index] = static_cast<uint8_t>(100000U >> ((3 - index) * 8));
	}
	oversizedQoiBytes[12] = 4;
	auto oversizedQoi = copyBytes(oversizedQoiBytes);
	ok = check(Engine::getInstance()->loadImageFromMem(oversizedQoi,
		static_cast<int>(oversizedQoiBytes.size())) == nullptr,
		"direct texture load rejects oversized QOI before renderer allocation") && ok;
	std::vector<uint8_t> jpegSvgPolyglot(261, ' ');
	const uint8_t jpegPrefix[] =
		{ 0xFF, 0xD8, 0xFF, 0xC0, 0x01, 0x01, 0x08, 0x01, 0x01, 0x01, 0x01 };
	std::memcpy(jpegSvgPolyglot.data(), jpegPrefix, sizeof(jpegPrefix));
	const char svgPayload[] = "<svg width='300' height='200'></svg>";
	std::memcpy(jpegSvgPolyglot.data() + sizeof(jpegPrefix), svgPayload,
		sizeof(svgPayload) - 1);
	auto polyglotData = copyBytes(jpegSvgPolyglot);
	ok = check(Engine::getInstance()->loadImageFromMem(polyglotData,
		static_cast<int>(jpegSvgPolyglot.size())) == nullptr,
		"direct image load binds JPEG preflight to JPEG decoder instead of SVG auto-detect") && ok;
	auto lazyPolyglotData = copyBytes(jpegSvgPolyglot);
	auto lazyPolyglot = IMP::createIMPImageFromMem(lazyPolyglotData,
		static_cast<int>(jpegSvgPolyglot.size()), false);
	ok = check(lazyPolyglot != nullptr &&
		lazyPolyglot->frame.size() == 1 &&
		lazyPolyglot->frame[0].dataLen ==
			static_cast<int>(jpegSvgPolyglot.size()),
		"lazy common image accepts bounded encoded bytes without decoding them") && ok;
	ok = check(lazyPolyglot != nullptr &&
		IMP::loadImage(lazyPolyglot, 0) == nullptr &&
		lazyPolyglot->frame[0].data == nullptr &&
		lazyPolyglot->frame[0].dataLen == 0,
		"invalid lazy common image becomes an empty resource on first use without repeated decode") && ok;
	std::vector<uint8_t> validTgaBytes(21, 0);
	validTgaBytes[2] = 2;
	validTgaBytes[12] = 1;
	validTgaBytes[14] = 1;
	validTgaBytes[16] = 24;
	auto validTga = copyBytes(validTgaBytes);
	auto lazyTgaImage = IMP::createIMPImageFromMem(validTga,
		static_cast<int>(validTgaBytes.size()), false);
	ok = check(lazyTgaImage != nullptr && lazyTgaImage->frame.size() == 1 &&
		lazyTgaImage->frame[0].dataLen == static_cast<int>(validTgaBytes.size()),
		"advertised TGA resources remain supported through the TGA-specific decoder") && ok;

	auto maximumFramePng = makePngDimensionHeader(8192, 8192);
	std::vector<uint8_t> aggregateImg(48 + 2 * (16 + maximumFramePng.size()), 0);
	std::memcpy(aggregateImg.data(), "IMG File Ver1.0", 16);
	writeInt32(aggregateImg, 16, 2);
	writeInt32(aggregateImg, 20, 1);
	size_t frameOffset = 48;
	for (int frameIndex = 0; frameIndex < 2; frameIndex++)
	{
		writeInt32(aggregateImg, frameOffset,
			static_cast<int32_t>(maximumFramePng.size()));
		std::memcpy(aggregateImg.data() + frameOffset + 16,
			maximumFramePng.data(), maximumFramePng.size());
		frameOffset += 16 + maximumFramePng.size();
	}
	auto aggregateImgData = copyBytes(aggregateImg);
	ok = check(IMP::createIMPImageFromMem(aggregateImgData,
		static_cast<int>(aggregateImg.size()), false) == nullptr,
		"IMG embedded-frame dimensions use a checked aggregate pixel budget") && ok;

	const uint8_t snapshotPixels[] =
	{
		0x00, 0x00, 0xFF, 0xFF,
		0x00, 0xFF, 0x00, 0xFF,
	};
	std::unique_ptr<char[]> encodedSnapshot;
	int encodedSnapshotSize = PngImageEncoder::encodeBgra8888(snapshotPixels,
		2, 1, static_cast<int>(sizeof(snapshotPixels)), encodedSnapshot);
	const uint8_t pngSignature[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	ok = check(encodedSnapshotSize > static_cast<int>(sizeof(pngSignature)) &&
		encodedSnapshot != nullptr &&
		std::memcmp(encodedSnapshot.get(), pngSignature, sizeof(pngSignature)) == 0,
		"save snapshot encoder writes a standard PNG stream") && ok;
	SDL_Surface* decodedSnapshot = SafeImageDecoder::loadSurface(
		encodedSnapshot.get(), encodedSnapshotSize);
	ok = check(decodedSnapshot != nullptr &&
		decodedSnapshot->w == 2 && decodedSnapshot->h == 1,
		"standard PNG save snapshot round-trips through the bounded runtime decoder") && ok;
	Uint8 red0 = 0;
	Uint8 green0 = 0;
	Uint8 blue0 = 0;
	Uint8 alpha0 = 0;
	Uint8 red1 = 0;
	Uint8 green1 = 0;
	Uint8 blue1 = 0;
	Uint8 alpha1 = 0;
	ok = check(decodedSnapshot != nullptr &&
		SDL_ReadSurfacePixel(decodedSnapshot, 0, 0,
			&red0, &green0, &blue0, &alpha0) &&
		SDL_ReadSurfacePixel(decodedSnapshot, 1, 0,
			&red1, &green1, &blue1, &alpha1) &&
		red0 == 0xFF && green0 == 0 && blue0 == 0 && alpha0 == 0xFF &&
		red1 == 0 && green1 == 0xFF && blue1 == 0 && alpha1 == 0xFF,
		"standard PNG save snapshot preserves BGRA color channels") && ok;
	if (decodedSnapshot != nullptr)
	{
		SDL_DestroySurface(decodedSnapshot);
	}

	namespace fs = std::filesystem;
	fs::path root = makeUniqueTestDirectory("jxqy-image-candidate-runtime-tests");
	fs::path activeRoot = root / "active";
	fs::path parentRoot = root / "parent";
	std::error_code errorCode;
	fs::remove_all(root, errorCode);
	auto writeBinary = [&](const fs::path& path, const char* bytes, int length)
	{
		fs::create_directories(path.parent_path(), errorCode);
		std::ofstream output(path, std::ios::binary);
		output.write(bytes, length);
	};
	writeBinary(activeRoot / "mpc" / "character" / "fallback.mpc", "corrupt", 7);
	writeBinary(activeRoot / "asf" / "character" / "fallback.asf", data.get(), size);
	writeBinary(activeRoot / "mpc" / "character" / "parent-fallback.mpc", "corrupt", 7);
	writeBinary(parentRoot / "mpc" / "character" / "parent-fallback.mpc", data.get(), size);
	auto truncatedEmbeddedPng = makeEmbeddedImageContainer(makePngDimensionHeader(1, 1));
	writeBinary(activeRoot / "mpc" / "character" / "embedded-fallback.mpc",
		reinterpret_cast<const char*>(truncatedEmbeddedPng.data()),
		static_cast<int>(truncatedEmbeddedPng.size()));
	writeBinary(activeRoot / "asf" / "character" / "embedded-fallback.asf", data.get(), size);
	File::setActiveResourceRoot(activeRoot.string());
	File::setResourceFallbackRoots({ parentRoot.string() });
	auto sameRootFallback = IMP::createIMPImage("mpc/character/fallback.mpc", false);
	ok = check(sameRootFallback != nullptr &&
		!sameRootFallback->frame.empty() && sameRootFallback->frame[0].pixelData.size() == 4,
		"corrupt exact image falls through to a valid same-root format alternate") && ok;
	auto parentFallback = IMP::createIMPImage("mpc/character/parent-fallback.mpc", false);
	ok = check(parentFallback != nullptr &&
		!parentFallback->frame.empty() && parentFallback->frame[0].pixelData.size() == 4,
		"corrupt active image exhausts local candidates before dependency fallback") && ok;
	auto embeddedFallback = IMP::createIMPImage(
		"mpc/character/embedded-fallback.mpc", false);
	ok = check(embeddedFallback != nullptr &&
		!embeddedFallback->frame.empty() &&
		embeddedFallback->frame[0].data != nullptr &&
		embeddedFallback->frame[0].dataLen ==
			static_cast<int>(makePngDimensionHeader(1, 1).size()) &&
		embeddedFallback->frame[0].pixelData.empty(),
		"bounded lazy IMG retains its selected embedded frame without eager decode or resource substitution") && ok;
	ok = check(embeddedFallback != nullptr &&
		IMP::loadImage(embeddedFallback, 0) == nullptr &&
		embeddedFallback->frame[0].data == nullptr &&
		embeddedFallback->frame[0].dataLen == 0,
		"invalid lazy IMG frame becomes empty on first use without repeated decode") && ok;
	auto directEmbeddedData = copyBytes(truncatedEmbeddedPng);
	ok = check(IMP::createIMPImageFromMem(
		directEmbeddedData,
		static_cast<int>(truncatedEmbeddedPng.size()),
		true) == nullptr,
		"direct IMG loading still rejects an invalid embedded frame while materializing it") && ok;
	fs::remove_all(root, errorCode);
	return ok;
}

bool runVideoLifecycleTests()
{
	bool ok = true;
	ScriptAPI nullManagerApi(nullptr);
	nullManagerApi.stopMovie();

	VideoStruct stoppedVideo;
	stoppedVideo.time.paused = false;
	Engine* engine = Engine::getInstance();
	engine->stopVideo(&stoppedVideo);
	ok = check(engine->getVideoStopped(&stoppedVideo),
		"StopMovie engine state remains stopped instead of resetting to playing") && ok;
	VideoStruct dynamicAudioVideo;
	dynamicAudioVideo.audioBuffer.resize(192001);
	ok = check(dynamicAudioVideo.audioBuffer.size() == 192001,
		"video audio output uses a dynamic buffer beyond the removed fixed boundary") && ok;
	ok = check(VideoStruct::MaxAudioBufferBytes > 192000 &&
		VideoStruct::MaxAudioFrameSamples > 0,
		"video audio conversion limits are explicit and bounded") && ok;

	VideoStruct focusPausedVideo;
	focusPausedVideo.time.beginTime = static_cast<float>(SDL_GetTicks()) - 100.0f;
	focusPausedVideo.time.paused = false;
	engine->pauseVideo(&focusPausedVideo);
	const float frozenVideoTime = engine->getVideoTime(&focusPausedVideo);
	SDL_Delay(20);
	ok = check(engine->getVideoTime(&focusPausedVideo) == frozenVideoTime,
		"focus-style video pause freezes the media timeline") && ok;
	engine->resumeVideo(&focusPausedVideo);
	SDL_Delay(20);
	ok = check(engine->getVideoTime(&focusPausedVideo) > frozenVideoTime,
		"focus-style video resume advances from the frozen timeline") && ok;

	const bool cursorWasVisibleBeforeTest = engine->getCursorVisible();
	engine->hideCursor();
	VideoStruct hiddenCursorVideo;
	{
		TestVideoPlayer videoPlayer;
		videoPlayer.v = &hiddenCursorVideo;
		videoPlayer.onRun();
		ok = check(engine->getCursorVisible(),
			"video playback shows a cursor that was previously hidden") && ok;
		videoPlayer.onExit();
		ok = check(!engine->getCursorVisible(),
			"video playback restores a previously hidden cursor on exit") && ok;
		videoPlayer.v = nullptr;
	}
	engine->showCursor();
	VideoStruct visibleCursorVideo;
	{
		TestVideoPlayer videoPlayer;
		videoPlayer.v = &visibleCursorVideo;
		videoPlayer.onRun();
		videoPlayer.onExit();
		ok = check(engine->getCursorVisible(),
			"video playback preserves a previously visible cursor on exit") && ok;
		videoPlayer.v = nullptr;
	}
	if (!cursorWasVisibleBeforeTest)
	{
		engine->hideCursor();
	}

	VideoStruct skippedVideo;
	{
		TestVideoPlayer videoPlayer;
		ok = check(videoPlayer.skipLabel != nullptr &&
			videoPlayer.skipLabel->parent == &videoPlayer &&
			videoPlayer.skipLabel->visible &&
			videoPlayer.skipLabel->coverMouse &&
			videoPlayer.skipLabel->rect.x == 30 &&
			videoPlayer.skipLabel->rect.y == 20 &&
			videoPlayer.skipLabel->rect.w == 70 &&
			videoPlayer.skipLabel->rect.h == 35,
			"video playback always exposes the top-left skip control") && ok;
		videoPlayer.v = &skippedVideo;
		videoPlayer.setRunning(true);
		videoPlayer.skipLabel->result = erMouseLDown;
		videoPlayer.onChildCallBack(videoPlayer.skipLabel);
		ok = check(engine->getVideoStopped(&skippedVideo) &&
			(videoPlayer.getResult() & erVideoStopped) != 0,
			"the cross-platform skip control stops video playback") && ok;
		videoPlayer.skipLabel->result = erNone;
		videoPlayer.v = nullptr;
	}

	namespace fs = std::filesystem;
	fs::path root = makeUniqueTestDirectory("jxqy-media-runtime-tests");
	std::error_code errorCode;
	fs::remove_all(root, errorCode);
	fs::create_directories(root / "video", errorCode);
	{
		std::ofstream corruptVideo(root / "video" / "corrupt.avi", std::ios::binary);
		corruptVideo << "not a video";
	}
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	for (int attempt = 0; attempt < 3; attempt++)
	{
		_video video = engine->loadVideo("video/corrupt.avi");
		ok = check(video == nullptr, "corrupt existing video fails closed") && ok;
		if (video != nullptr)
		{
			engine->freeVideo(video);
		}
	}
	fs::remove_all(root, errorCode);
	return ok;
}

bool runTransparentCharacterCropTests()
{
	bool ok = true;
	auto image = std::make_shared<IMPImage>();
	image->frame.resize(1);
	IMPFrame& frame = image->frame[0];
	frame.pixelWidth = 5;
	frame.pixelHeight = 6;
	frame.xOffset = 7;
	frame.yOffset = 11;
	frame.pixelData.assign(5 * 6 * 4, 0);
	auto setOpaque = [&frame](int x, int y, std::uint8_t value)
	{
		const std::size_t offset =
			(static_cast<std::size_t>(y) * frame.pixelWidth + x) * 4;
		frame.pixelData[offset + 0] = value;
		frame.pixelData[offset + 1] = value;
		frame.pixelData[offset + 2] = value;
		frame.pixelData[offset + 3] = 255;
	};
	setOpaque(1, 2, 40);
	setOpaque(3, 4, 90);

	IMP::cropTransparentEdges(image);
	ok = check(frame.pixelWidth == 3 && frame.pixelHeight == 3,
		"legacy character frame crops alpha-zero outer rows and columns") && ok;
	ok = check(frame.xOffset == 6 && frame.yOffset == 9,
		"legacy character crop compensates offsets by the removed left/top pixels") && ok;
	ok = check(frame.pixelData.size() == 3 * 3 * 4
		&& frame.pixelData[3] == 255
		&& frame.pixelData[(2 * 3 + 2) * 4 + 3] == 255,
		"cropped character frame preserves the visible corner pixels") && ok;
	const int anchorY = 300;
	const int originalVisibleTop = anchorY - 11 + 2;
	const int croppedVisibleTop = anchorY - frame.yOffset;
	ok = check(originalVisibleTop == croppedVisibleTop,
		"offset compensation keeps visible character pixels at the same world position") && ok;
	ok = check(anchorY - frame.yOffset - 8 == originalVisibleTop - 8,
		"head life bar is positioned above visible pixels instead of transparent padding") && ok;

	const std::vector<std::uint8_t> onceCroppedPixels = frame.pixelData;
	IMP::cropTransparentEdges(image);
	ok = check(frame.pixelWidth == 3 && frame.pixelHeight == 3
		&& frame.xOffset == 6 && frame.yOffset == 9
		&& frame.pixelData == onceCroppedPixels,
		"transparent character crop is idempotent") && ok;

	auto transparentImage = std::make_shared<IMPImage>();
	transparentImage->frame.resize(1);
	IMPFrame& transparentFrame = transparentImage->frame[0];
	transparentFrame.pixelWidth = 4;
	transparentFrame.pixelHeight = 3;
	transparentFrame.xOffset = -5;
	transparentFrame.yOffset = 12;
	transparentFrame.pixelData.assign(4 * 3 * 4, 0);
	IMP::cropTransparentEdges(transparentImage);
	ok = check(transparentFrame.pixelWidth == 1
		&& transparentFrame.pixelHeight == 1
		&& transparentFrame.pixelData.size() == 4
		&& transparentFrame.xOffset == -5
		&& transparentFrame.yOffset == 12,
		"fully transparent frame collapses safely without changing its anchor") && ok;
	return ok;
}
}

bool runMediaRuntimeTests()
{
	bool ok = true;
	const Rect wideFourByThreeVideo =
		EngineBase::calculateAspectFitVideoRect(640, 480, 1100, 500);
	ok = check(wideFourByThreeVideo.x == 216 && wideFourByThreeVideo.y == 0 &&
		wideFourByThreeVideo.w == 667 && wideFourByThreeVideo.h == 500,
		"full-screen 4:3 video is centered without horizontal stretching") && ok;
	const Rect wideTwoByOneVideo =
		EngineBase::calculateAspectFitVideoRect(1440, 720, 1100, 500);
	ok = check(wideTwoByOneVideo.x == 50 && wideTwoByOneVideo.y == 0 &&
		wideTwoByOneVideo.w == 1000 && wideTwoByOneVideo.h == 500,
		"full-screen 2:1 video is centered without horizontal stretching") && ok;
	const Rect tallFourByThreeVideo =
		EngineBase::calculateAspectFitVideoRect(640, 480, 500, 1100);
	ok = check(tallFourByThreeVideo.x == 0 && tallFourByThreeVideo.y == 362 &&
		tallFourByThreeVideo.w == 500 && tallFourByThreeVideo.h == 375,
		"full-screen video is centered vertically in a tall destination") && ok;
	const Rect invalidVideo =
		EngineBase::calculateAspectFitVideoRect(0, 480, 1100, 500);
	ok = check(invalidVideo.x == 0 && invalidVideo.y == 0 &&
		invalidVideo.w == 0 && invalidVideo.h == 0,
		"invalid video dimensions do not produce drawable geometry") && ok;
	const EngineBase::FullScreenVideoLayout croppedVideo =
		EngineBase::calculateFullScreenVideoLayout(640, 480, 1100, 500);
	ok = check(croppedVideo.source.x == 0 &&
		croppedVideo.source.y == 94 &&
		croppedVideo.source.w == 640 &&
		croppedVideo.source.h == 291 &&
		croppedVideo.destination.x == 0 &&
		croppedVideo.destination.y == 0 &&
		croppedVideo.destination.w == 1100 &&
		croppedVideo.destination.h == 500 &&
		!croppedVideo.needsBlackBackground,
		"full-screen video uses bounded center cropping without bars") && ok;
	const EngineBase::FullScreenVideoLayout fourByThreeOnTwentyOneByNine =
		EngineBase::calculateFullScreenVideoLayout(640, 480, 2520, 1080);
	ok = check(fourByThreeOnTwentyOneByNine.source.x == 0 &&
		fourByThreeOnTwentyOneByNine.source.y == 103 &&
		fourByThreeOnTwentyOneByNine.source.w == 640 &&
		fourByThreeOnTwentyOneByNine.source.h == 274 &&
		fourByThreeOnTwentyOneByNine.destination.x == 0 &&
		fourByThreeOnTwentyOneByNine.destination.y == 0 &&
		fourByThreeOnTwentyOneByNine.destination.w == 2520 &&
		fourByThreeOnTwentyOneByNine.destination.h == 1080 &&
		!fourByThreeOnTwentyOneByNine.needsBlackBackground,
		"4:3 full-screen video fills a 21:9 viewport within the crop limit") && ok;
	const EngineBase::FullScreenVideoLayout blackBarVideo =
		EngineBase::calculateFullScreenVideoLayout(640, 480, 500, 1100);
	ok = check(blackBarVideo.source.x == 0 &&
		blackBarVideo.source.y == 0 &&
		blackBarVideo.source.w == 640 &&
		blackBarVideo.source.h == 480 &&
		blackBarVideo.destination.x == 0 &&
		blackBarVideo.destination.y == 362 &&
		blackBarVideo.destination.w == 500 &&
		blackBarVideo.destination.h == 375 &&
		blackBarVideo.needsBlackBackground,
		"extreme full-screen video aspect ratios use black bars without mirrored fill") && ok;
	const EngineBase::FullScreenVideoLayout exactCropBoundaryVideo =
		EngineBase::calculateFullScreenVideoLayout(1000, 1000, 2000, 1000);
	ok = check(exactCropBoundaryVideo.source.x == 0 &&
		exactCropBoundaryVideo.source.y == 250 &&
		exactCropBoundaryVideo.source.w == 1000 &&
		exactCropBoundaryVideo.source.h == 500 &&
		exactCropBoundaryVideo.destination.x == 0 &&
		exactCropBoundaryVideo.destination.y == 0 &&
		exactCropBoundaryVideo.destination.w == 2000 &&
		exactCropBoundaryVideo.destination.h == 1000 &&
		!exactCropBoundaryVideo.needsBlackBackground,
		"full-screen video allows exactly 25 percent crop per side") && ok;
	const EngineBase::FullScreenVideoLayout overCropBoundaryVideo =
		EngineBase::calculateFullScreenVideoLayout(1000, 1000, 2001, 1000);
	ok = check(overCropBoundaryVideo.source.x == 0 &&
		overCropBoundaryVideo.source.y == 0 &&
		overCropBoundaryVideo.source.w == 1000 &&
		overCropBoundaryVideo.source.h == 1000 &&
		overCropBoundaryVideo.destination.x == 500 &&
		overCropBoundaryVideo.destination.y == 0 &&
		overCropBoundaryVideo.destination.w == 1000 &&
		overCropBoundaryVideo.destination.h == 1000 &&
		overCropBoundaryVideo.needsBlackBackground,
		"full-screen video uses black bars above 25 percent crop per side") && ok;
	const Rect mirrorSource = { 0, 0, 640, 480 };
	const Rect mirrorViewport = { 0, 0, 1100, 500 };
	const auto mirrorSlices =
		AspectFitLayout::calculateMirroredSlices(
			mirrorSource,
			mirrorViewport,
			wideFourByThreeVideo);
	ok = check(mirrorSlices.size() == 64,
		"wide aspect-fit layout creates bounded mirror slices for both sides") && ok;
	if (mirrorSlices.size() == 64)
	{
		const AspectFitMirrorSlice& leftSeam = mirrorSlices.front();
		const AspectFitMirrorSlice& leftOuter = mirrorSlices[31];
		const AspectFitMirrorSlice& rightSeam = mirrorSlices[32];
		ok = check(
			leftSeam.source.x == mirrorSource.x &&
			leftSeam.destination.x + leftSeam.destination.w ==
				wideFourByThreeVideo.x &&
			leftSeam.alpha == 220,
			"left fill preserves the original visible edge treatment") && ok;
		ok = check(
			rightSeam.source.x + rightSeam.source.w ==
				mirrorSource.x + mirrorSource.w &&
			rightSeam.destination.x ==
				wideFourByThreeVideo.x + wideFourByThreeVideo.w &&
			rightSeam.alpha == 220,
			"right fill preserves the original visible edge treatment") && ok;
		ok = check(leftOuter.alpha < leftSeam.alpha,
			"mirrored fill fades smoothly toward the outer edge") && ok;
		const float waveOffsetA =
			AspectFitLayout::calculateMirrorWaveNormalOffset(
				0.1f,
				0.5f,
				wideFourByThreeVideo.h,
				250,
				1U);
		const float waveOffsetB =
			AspectFitLayout::calculateMirrorWaveNormalOffset(
				0.1f,
				0.5f,
				wideFourByThreeVideo.h,
				1250,
				1U);
		const float disturbedOffset =
			AspectFitLayout::calculateMirrorWaveNormalOffset(
				0.55f,
				0.25f,
				wideFourByThreeVideo.h,
				1250,
				2U);
		ok = check(
			std::abs(waveOffsetA) <= 17.0f &&
			std::abs(waveOffsetB) <= 17.0f &&
			waveOffsetA != waveOffsetB &&
			disturbedOffset != waveOffsetB,
			"title mirror geometry uses bounded normal waves with spatial disturbance") && ok;
	}
	AspectFitPointerRipple pointerRipple;
	pointerRipple.normalizedX = 0.5f;
	pointerRipple.normalizedY = 0.5f;
	pointerRipple.startTimeMilliseconds = 1000;
	const AspectFitPointerRippleSample pointerRippleBeforeStart =
		AspectFitLayout::calculatePointerRippleSample(
			0.57f, 0.5f, 1100, 500, 500, 999, pointerRipple);
	const AspectFitPointerRippleSample pointerRippleActive =
		AspectFitLayout::calculatePointerRippleSample(
			0.57f, 0.5f, 1100, 500, 500, 1200, pointerRipple);
	const AspectFitPointerRippleSample pointerRippleNextFrame =
		AspectFitLayout::calculatePointerRippleSample(
			0.57f, 0.5f, 1100, 500, 500, 1240, pointerRipple);
	const AspectFitPointerRippleSample pointerRippleExpired =
		AspectFitLayout::calculatePointerRippleSample(
			0.57f,
			0.5f,
			1100,
			500,
			500,
			1000 + AspectFitLayout::PointerRippleDurationMilliseconds,
			pointerRipple);
	const AspectFitPointerRippleSample combinedPointerRipple =
		AspectFitLayout::calculateCombinedPointerRippleSample(
			0.57f,
			0.5f,
			1100,
			500,
			500,
			1200,
			{ pointerRipple, pointerRipple });
	ok = check(
		pointerRippleBeforeStart.offset.x == 0.0f &&
		pointerRippleBeforeStart.offset.y == 0.0f &&
		pointerRippleBeforeStart.brightness == 1.0f &&
		std::abs(pointerRippleActive.offset.x) > 0.5f &&
		std::abs(pointerRippleActive.offset.x) <= 9.0f &&
		std::abs(pointerRippleActive.offset.y) < 0.001f &&
		pointerRippleActive.brightness >=
			AspectFitLayout::PointerRippleMinimumBrightness &&
		pointerRippleActive.brightness < 1.0f &&
		pointerRippleActive.offset.x * pointerRippleNextFrame.offset.x > 0.0f &&
		std::abs(pointerRippleNextFrame.offset.x -
			pointerRippleActive.offset.x) < 1.5f &&
		pointerRippleExpired.offset.x == 0.0f &&
		pointerRippleExpired.offset.y == 0.0f &&
		pointerRippleExpired.brightness == 1.0f &&
		combinedPointerRipple.brightness >=
			AspectFitLayout::CombinedPointerRippleMinimumBrightness &&
		combinedPointerRipple.brightness <= pointerRippleActive.brightness,
		"title pointer ripple expands without a rapid frame-to-frame reversal"
		" while shared RGB lighting remains bounded and expires cleanly") && ok;
	const Rect portraitViewport = { 0, 0, 500, 1100 };
	const Rect portraitFit = AspectFitLayout::calculateFittedRect(
		mirrorSource.w,
		mirrorSource.h,
		portraitViewport.w,
		portraitViewport.h);
	const auto verticalMirrorSlices =
		AspectFitLayout::calculateMirroredSlices(
			mirrorSource,
			portraitViewport,
			portraitFit);
	ok = check(verticalMirrorSlices.size() == 64,
		"portrait aspect-fit layout creates bounded mirror slices vertically") && ok;
	if (verticalMirrorSlices.size() == 64)
	{
		const AspectFitMirrorSlice& topSeam = verticalMirrorSlices.front();
		const AspectFitMirrorSlice& bottomSeam = verticalMirrorSlices[32];
		ok = check(
			topSeam.source.y == mirrorSource.y &&
			topSeam.destination.y + topSeam.destination.h ==
				portraitFit.y &&
			topSeam.alpha == 220,
			"top fill preserves the original visible edge treatment") && ok;
		ok = check(
			bottomSeam.source.y + bottomSeam.source.h ==
				mirrorSource.y + mirrorSource.h &&
			bottomSeam.destination.y == portraitFit.y + portraitFit.h &&
			bottomSeam.alpha == 220,
			"bottom fill preserves the original visible edge treatment") && ok;
	}
	ok = runAudioDecodeSafetyTests() && ok;
	ok = runAnimationLayoutTests() && ok;
	ok = runPanelLayoutTests() && ok;
	ok = runLazyRawImageTest() && ok;
	ok = runTransparentCharacterCropTests() && ok;
	ok = runVideoLifecycleTests() && ok;

	Engine* engine = Engine::getInstance();
#ifdef SHF_USE_AUDIO
	EngineBase* audioEngineBase = static_cast<EngineBase*>(engine);
	auto createTestAudioChannel = [audioEngineBase](float volume, bool playing = true)
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		auto channel = std::make_unique<Channel_t>();
		channel->volume = volume;
		channel->playing = playing;
		channel->stopped = !playing;
		return audioEngineBase->registerAudioChannel(std::move(channel));
	};
	auto countActiveAudioChannels = [audioEngineBase]()
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		return static_cast<std::size_t>(std::count_if(
			audioEngineBase->channelSlots.begin(), audioEngineBase->channelSlots.end(),
			[](const EngineBase::AudioChannelSlot& slot)
			{
				return slot.channel != nullptr;
			}));
	};
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		audioEngineBase->clearAudioChannels();
	}

	_channel stoppedHandle = createTestAudioChannel(0.5f);
	const std::size_t singleChannelHighWater = audioEngineBase->channelSlots.size();
	engine->stopMusic(stoppedHandle);
	ok = check(!engine->getMusicPlaying(stoppedHandle) && countActiveAudioChannels() == 0,
		"stopped audio handle becomes stale without retaining an active channel") && ok;

	_channel reusedHandle = createTestAudioChannel(0.75f);
	ok = check(reusedHandle != stoppedHandle &&
		audioEngineBase->channelSlots.size() == singleChannelHighWater,
		"released audio slot is reused with a different generation") && ok;
	engine->setMusicVolume(stoppedHandle, 0.1f);
	engine->setMusicPosition(stoppedHandle, 100.0f, 200.0f);
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		auto* reusedChannel = audioEngineBase->resolveAudioChannel(reusedHandle);
		ok = check(reusedChannel != nullptr && reusedChannel->volume == 0.75f &&
			reusedChannel->positionX == 0.0f && reusedChannel->positionY == 0.0f,
			"stale audio handle cannot mutate a reused slot") && ok;
		if (reusedChannel != nullptr)
		{
			reusedChannel->playing = false;
			reusedChannel->stopped = true;
		}
	}
	audioEngineBase->updateSoundSystem();
	ok = check(!engine->getMusicPlaying(reusedHandle) && countActiveAudioChannels() == 0,
		"audio update reclaims a naturally completed channel") && ok;

	_channel keptHandle = createTestAudioChannel(0.45f);
	_channel stoppedByGroupHandle = createTestAudioChannel(0.55f);
	audioEngineBase->stopSoundsExcept(keptHandle, nullptr);
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		ok = check(audioEngineBase->resolveAudioChannel(keptHandle) != nullptr &&
			audioEngineBase->resolveAudioChannel(stoppedByGroupHandle) == nullptr,
			"group stop preserves only the requested live generation") && ok;
	}
	engine->stopMusic(keptHandle);

	auto* autoReleaseMusic = new AudioBuffer;
	_channel autoReleaseHandle = createTestAudioChannel(1.0f);
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		auto* autoReleaseChannel = audioEngineBase->resolveAudioChannel(autoReleaseHandle);
		if (autoReleaseChannel != nullptr)
		{
			autoReleaseChannel->music = autoReleaseMusic;
		}
	}
	ok = check(engine->soundAutoRelease(autoReleaseMusic, autoReleaseHandle),
		"auto-release accepts a live generation handle") && ok;
	audioEngineBase->updateSoundSystem();
	audioEngineBase->checkSoundRelease();
	ok = check(!engine->getMusicPlaying(autoReleaseHandle) &&
		EngineBase::soundList.empty() && countActiveAudioChannels() == 0,
		"natural completion releases both the channel slot and owned sound buffer") && ok;

	_channel clearHandleA = createTestAudioChannel(0.4f);
	_channel clearHandleB = createTestAudioChannel(0.6f);
	const std::size_t twoChannelHighWater = audioEngineBase->channelSlots.size();
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		audioEngineBase->clearAudioChannels();
	}
	ok = check(!engine->getMusicPlaying(clearHandleA) &&
		!engine->getMusicPlaying(clearHandleB) && countActiveAudioChannels() == 0,
		"clearing audio channels invalidates every outstanding generation") && ok;

	for (int iteration = 0; iteration < 10000; iteration++)
	{
		_channel handle = createTestAudioChannel(0.5f);
		engine->stopMusic(handle);
	}
	ok = check(countActiveAudioChannels() == 0 &&
		audioEngineBase->channelSlots.size() == twoChannelHighWater,
		"long sequential playback stress remains bounded by peak concurrency") && ok;

	_channel staleConcurrentHandle = createTestAudioChannel(0.25f);
	engine->stopMusic(staleConcurrentHandle);
	_channel protectedLiveHandle = createTestAudioChannel(0.875f);
	std::atomic<bool> beginConcurrentAccess = false;
	std::vector<std::thread> staleHandleThreads;
	for (int threadIndex = 0; threadIndex < 4; threadIndex++)
	{
		staleHandleThreads.emplace_back([&]()
		{
			while (!beginConcurrentAccess.load())
			{
				std::this_thread::yield();
			}
			for (int iteration = 0; iteration < 5000; iteration++)
			{
				engine->setMusicVolume(staleConcurrentHandle, 0.1f);
				engine->setMusicPosition(staleConcurrentHandle, 10.0f, 20.0f);
				engine->pauseMusic(staleConcurrentHandle);
				engine->resumeMusic(staleConcurrentHandle);
				engine->getMusicPlaying(staleConcurrentHandle);
			}
		});
	}
	beginConcurrentAccess.store(true);
	for (int iteration = 0; iteration < 5000; iteration++)
	{
		_channel temporaryHandle = createTestAudioChannel(0.3f);
		engine->stopMusic(temporaryHandle);
	}
	for (auto& staleHandleThread : staleHandleThreads)
	{
		staleHandleThread.join();
	}
	{
		std::lock_guard<std::recursive_mutex> locker(audioEngineBase->soundMutex);
		auto* protectedLiveChannel = audioEngineBase->resolveAudioChannel(protectedLiveHandle);
		ok = check(protectedLiveChannel != nullptr &&
			protectedLiveChannel->volume == 0.875f &&
			protectedLiveChannel->positionX == 0.0f &&
			protectedLiveChannel->positionY == 0.0f,
			"concurrent stale-handle access cannot affect a reused live channel") && ok;
	}
	ok = check(audioEngineBase->channelSlots.size() == twoChannelHighWater,
		"concurrent playback churn reuses the bounded slot pool") && ok;
	engine->stopMusic(protectedLiveHandle);
	ok = check(countActiveAudioChannels() == 0,
		"concurrent handle fixture releases its final live channel") && ok;
#endif

	bool previousMultiThreadedMode = EngineBase::multiThreadedMode.exchange(true);
	ok = check(!engine->beginDrawTalk(0, 16),
		"talk drawing rejects invalid dimensions") && ok;
	bool invalidFailureReleasedLock = EngineBase::_mutex.try_lock();
	if (invalidFailureReleasedLock)
	{
		EngineBase::_mutex.unlock();
	}
	ok = check(invalidFailureReleasedLock,
		"failed talk drawing releases the lock acquired at begin") && ok;
	ok = check(engine->endDrawTalk() == nullptr,
		"ending a failed talk drawing is a no-op") && ok;

	SDL_Surface* talkSurface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888);
	SDL_Renderer* talkRenderer = talkSurface != nullptr
		? SDL_CreateSoftwareRenderer(talkSurface)
		: nullptr;
	ok = check(talkSurface != nullptr && talkRenderer != nullptr,
		"talk drawing fixture creates a software render target") && ok;
	if (talkRenderer != nullptr)
	{
		SDL_Renderer* previousRenderer = EngineBase::renderer.exchange(talkRenderer);
		auto originalRenderTarget = make_shared_image(
			SDL_CreateTexture(
				talkRenderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET,
				16,
				16));
		ok = check(
			originalRenderTarget != nullptr &&
				SDL_SetRenderTarget(
					talkRenderer,
					originalRenderTarget.get()),
			"render session fixture binds an explicit original target") &&
			ok;

		EngineBase::multiThreadedMode.store(true);
		bool beganLockedTalk = engine->beginDrawTalk(8, 8);
		ok = check(beganLockedTalk,
			"valid talk drawing can begin after an invalid-size failure") && ok;
		if (beganLockedTalk)
		{
			ok = check(!engine->beginDrawTalk(8, 8),
				"repeated talk begin is rejected without relocking") && ok;
			bool crossThreadBegin = true;
			_shared_image crossThreadEnd;
			std::thread competingTalkThread([&]()
			{
				crossThreadBegin = engine->beginDrawTalk(8, 8);
				crossThreadEnd = engine->endDrawTalk();
			});
			competingTalkThread.join();
			ok = check(!crossThreadBegin && crossThreadEnd == nullptr,
				"talk drawing rejects begin/end from a competing thread") && ok;
			EngineBase::multiThreadedMode.store(false);
			auto lockedTalkImage = engine->endDrawTalk();
			ok = check(lockedTalkImage != nullptr,
				"talk end succeeds after the threading mode is disabled") && ok;
			ok = check(engine->endDrawTalk() == nullptr,
				"repeated talk end is rejected without unlocking again") && ok;
			lockedTalkImage.reset();
		}
		bool lockedBeginReleasedOriginalLock = EngineBase::_mutex.try_lock();
		if (lockedBeginReleasedOriginalLock)
		{
			EngineBase::_mutex.unlock();
		}
		ok = check(lockedBeginReleasedOriginalLock,
			"talk end releases a lock held by begin even after mode is disabled") && ok;

		EngineBase::multiThreadedMode.store(false);
		bool beganUnlockedTalk = engine->beginDrawTalk(8, 8);
		ok = check(beganUnlockedTalk,
			"talk drawing can begin without the global lock") && ok;
		if (beganUnlockedTalk)
		{
			EngineBase::multiThreadedMode.store(true);
			auto unlockedTalkImage = engine->endDrawTalk();
			ok = check(unlockedTalkImage != nullptr,
				"talk end does not unlock solely because the mode became enabled") && ok;
			unlockedTalkImage.reset();
		}
		bool unlockedBeginLeftMutexUsable = EngineBase::_mutex.try_lock();
		if (unlockedBeginLeftMutexUsable)
		{
			EngineBase::_mutex.unlock();
		}
		ok = check(unlockedBeginLeftMutexUsable,
			"mode switching preserves mutex usability for an unlocked begin") && ok;

		EngineBase* engineBase = static_cast<EngineBase*>(engine);
		const int renderSessionPreviousWidth =
			engineBase->width;
		const int renderSessionPreviousHeight =
			engineBase->height;
		engineBase->width = 16;
		engineBase->height = 16;
		EngineBase::multiThreadedMode.store(false);
		engine->resetApplicationQuitRequest();

		bool beganSuspendedTalk =
			engine->beginDrawTalk(8, 8);
		ok = check(
			beganSuspendedTalk &&
				!engine->beginDrawTalk(8, 8) &&
				!engine->beginSaveScreen() &&
				engine->endSaveScreen() == nullptr &&
				SDL_GetRenderTarget(talkRenderer) !=
					originalRenderTarget.get(),
			"an active talk session rejects reentry, cross-kind begin, and a mismatched end") &&
			ok;
		const bool sessionBackgroundState =
			EngineBase::isBackGround.exchange(true);
		auto suspendedTalkImage =
			engine->endDrawTalk();
		ok = check(
			suspendedTalkImage == nullptr &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"talk end restores the original target after background closes new render admission") &&
			ok;
		EngineBase::isBackGround.store(
			sessionBackgroundState);

		bool beganRecoveredTalk =
			engine->beginDrawTalk(8, 8);
		auto recoveredTalkImage =
			beganRecoveredTalk
				? engine->endDrawTalk()
				: nullptr;
		ok = check(
			beganRecoveredTalk &&
				recoveredTalkImage != nullptr &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"a completed suspended talk session permits a later foreground session") &&
			ok;

		bool beganTerminatingSave =
			engine->beginSaveScreen();
		ok = check(
			beganTerminatingSave &&
				!engine->beginSaveScreen() &&
				!engine->beginDrawTalk(8, 8) &&
				engine->endDrawTalk() == nullptr &&
				SDL_GetRenderTarget(talkRenderer) !=
					originalRenderTarget.get(),
			"an active save-screen session rejects reentry, cross-kind begin, and a mismatched end") &&
			ok;
		engine->requestApplicationQuit();
		auto terminatingSaveImage =
			engine->endSaveScreen();
		ok = check(
			terminatingSaveImage == nullptr &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"save-screen end restores the original target after termination closes new render admission") &&
			ok;
		engine->resetApplicationQuitRequest();
		bool beganRecoveredSave =
			engine->beginSaveScreen();
		auto recoveredSaveImage =
			beganRecoveredSave
				? engine->endSaveScreen()
				: nullptr;
		ok = check(
			beganRecoveredSave &&
				recoveredSaveImage != nullptr &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"a completed terminating save-screen session permits a later active session") &&
			ok;

		auto renderSessionPreviousLogicalScreen =
			engine->realScreen;
		const bool renderSessionPreviousPendingResize =
			engine->pendingLogicalScreenTextureResize;
		engine->realScreen = make_shared_image(
			SDL_CreateTexture(
				talkRenderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET,
				16,
				16));
		SDL_Texture* sessionLogicalScreen =
			engine->realScreen.get();
		const bool sessionLogicalScreenBound =
			sessionLogicalScreen != nullptr &&
			SDL_SetRenderTarget(
				talkRenderer,
				sessionLogicalScreen);
		bool beganResizeSave =
			sessionLogicalScreenBound &&
			engine->beginSaveScreen();
		bool resizeDeferredDuringSession =
			beganResizeSave &&
			engine->resizeLogicalScreen(
				800,
				600,
				false);
		ok = check(
			resizeDeferredDuringSession &&
				engine->pendingLogicalScreenTextureResize &&
				engine->realScreen.get() ==
					sessionLogicalScreen &&
				SDL_GetRenderTarget(talkRenderer) !=
					sessionLogicalScreen,
			"logical-screen recreation remains deferred while a persistent render-target session owns the old screen as its restore target") &&
			ok;
		auto resizeSaveImage =
			beganResizeSave
				? engine->endSaveScreen()
				: nullptr;
		ok = check(
			resizeSaveImage != nullptr &&
				SDL_GetRenderTarget(talkRenderer) ==
					sessionLogicalScreen,
			"ending a resized save-screen session restores the still-owned logical screen") &&
			ok;
		const bool recreatedAfterSession =
			engine->recreateLogicalScreenTexture();
		ok = check(
			recreatedAfterSession &&
				engine->realScreen.get() !=
					sessionLogicalScreen,
			"the deferred logical-screen recreation completes after the persistent session ends") &&
			ok;
		auto rebuiltSessionLogicalScreen =
			engine->realScreen;
		SDL_SetRenderTarget(
			talkRenderer,
			originalRenderTarget.get());
		engine->realScreen =
			renderSessionPreviousLogicalScreen;
		rebuiltSessionLogicalScreen.reset();
		engine->pendingLogicalScreenTextureResize =
			renderSessionPreviousPendingResize;
		engineBase->width = 16;
		engineBase->height = 16;

		const bool closedBeginBackgroundState =
			EngineBase::isBackGround.exchange(true);
		ok = check(
			!engine->beginDrawTalk(8, 8) &&
				!engine->beginSaveScreen() &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"a closed render gate rejects new target sessions without changing the current target") &&
			ok;
		EngineBase::isBackGround.store(
			closedBeginBackgroundState);

		auto acceptedOperationTarget =
			make_shared_image(
				SDL_CreateTexture(
					talkRenderer,
					SDL_PIXELFORMAT_ARGB8888,
					SDL_TEXTUREACCESS_TARGET,
					4,
					4));
		const bool acceptedOperationBound =
			acceptedOperationTarget != nullptr &&
			engine->setSharedImageAsRenderTarget(
				acceptedOperationTarget);
		const bool acceptedOperationBackgroundState =
			EngineBase::isBackGround.exchange(true);
		const bool acceptedOperationRestored =
			acceptedOperationBound &&
			engine->
				restoreImageRenderTargetAfterAcceptedOperation(
					originalRenderTarget.get(),
					acceptedOperationTarget);
		ok = check(
			acceptedOperationRestored &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"an accepted function-local target operation can restore its original target after lifecycle admission closes") &&
			ok;
		EngineBase::isBackGround.store(
			acceptedOperationBackgroundState);

		auto renderTargetProbe = make_shared_image(
			SDL_CreateTexture(
				talkRenderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_TARGET,
				4,
				4));
		const bool probeBound =
			renderTargetProbe != nullptr &&
			SDL_SetRenderTarget(
				talkRenderer,
				renderTargetProbe.get());
		const bool originalTargetRestored =
			probeBound &&
			SDL_SetRenderTarget(
				talkRenderer,
				originalRenderTarget.get());
		renderTargetProbe.reset();
		ok = check(
			probeBound &&
				originalTargetRestored &&
				SDL_GetRenderTarget(talkRenderer) ==
					originalRenderTarget.get(),
			"renderer target remains usable after suspended and terminating sessions release their temporary textures") &&
			ok;

		engineBase->width =
			renderSessionPreviousWidth;
		engineBase->height =
			renderSessionPreviousHeight;
		const int previousLogicalWidth = engineBase->width;
		const int previousLogicalHeight = engineBase->height;
		const bool previousPendingTextureResize = engine->pendingLogicalScreenTextureResize;
		auto previousLogicalScreen = engine->realScreen;
		const bool previousBackgroundState = EngineBase::isBackGround.exchange(true);
		SDL_Texture* backgroundTarget = SDL_GetRenderTarget(talkRenderer);
		bool backgroundResizeAccepted = engine->resizeLogicalScreen(12, 10, false);
		ok = check(backgroundResizeAccepted && engineBase->width == 640 && engineBase->height == 480,
			"logical resize clamps both dimensions to the 640x480 minimum") && ok;
		ok = check(engine->pendingLogicalScreenTextureResize,
			"background logical resize is recorded for foreground texture recreation") && ok;
		ok = check(engine->realScreen == previousLogicalScreen &&
			SDL_GetRenderTarget(talkRenderer) == backgroundTarget,
			"background logical resize does not call the renderer or replace its target") && ok;
		engineBase->currentFrameReady.store(true);
		engine->frameBegin();
		ok = check(!engine->isFrameReady(),
			"a background frame is not exposed to loading or element draw callers") && ok;

		EngineBase::isBackGround.store(false);
		engine->pendingLogicalScreenTextureResize = !engine->recreateLogicalScreenTexture();
		ok = check(!engine->pendingLogicalScreenTextureResize && engine->realScreen != nullptr,
			"foreground restoration recreates a deferred logical screen texture") && ok;
		SDL_ScaleMode logicalScreenScaleMode = SDL_SCALEMODE_INVALID;
		bool queriedLogicalScreenScaleMode = engine->realScreen != nullptr
			&& SDL_GetTextureScaleMode(engine->realScreen.get(), &logicalScreenScaleMode);
#if SDL_VERSION_ATLEAST(3, 4, 0)
		constexpr SDL_ScaleMode ExpectedLogicalScreenScaleMode = SDL_SCALEMODE_PIXELART;
#else
		constexpr SDL_ScaleMode ExpectedLogicalScreenScaleMode = SDL_SCALEMODE_NEAREST;
#endif
		ok = check(queriedLogicalScreenScaleMode
			&& logicalScreenScaleMode == ExpectedLogicalScreenScaleMode,
			"logical screen uses the highest-quality available pixel-art scale mode") && ok;
		engine->realScreen = previousLogicalScreen;
		engineBase->width = previousLogicalWidth;
		engineBase->height = previousLogicalHeight;
		engine->pendingLogicalScreenTextureResize = previousPendingTextureResize;
		EngineBase::isBackGround.store(previousBackgroundState);

		SDL_SetRenderTarget(talkRenderer, nullptr);
		originalRenderTarget.reset();
		EngineBase::renderer.store(previousRenderer);
		SDL_DestroyRenderer(talkRenderer);
	}
	if (talkSurface != nullptr)
	{
		SDL_DestroySurface(talkSurface);
	}
	EngineBase::multiThreadedMode.store(previousMultiThreadedMode);

	VideoStruct unsortedVideo;
	unsortedVideo.videoImage = { { nullptr, 30.0f }, { nullptr, 10.0f }, { nullptr, 20.0f } };
	engine->rearrangeVideoFrame(&unsortedVideo);
	ok = check(unsortedVideo.videoImage[0].t == 10.0f &&
		unsortedVideo.videoImage[1].t == 20.0f &&
		unsortedVideo.videoImage[2].t == 30.0f,
		"video frame rearrangement terminates and preserves sorted frame records") && ok;

	AVCodecContext* codecContext = avcodec_alloc_context3(nullptr);
	AVFrame* audioFrame = av_frame_alloc();
	ok = check(codecContext != nullptr && audioFrame != nullptr,
		"FFmpeg audio conversion fixture allocates") && ok;
	if (codecContext != nullptr && audioFrame != nullptr)
	{
		constexpr int SampleCount = 100000;
		codecContext->sample_rate = 44100;
		codecContext->sample_fmt = AV_SAMPLE_FMT_S16;
		audioFrame->format = AV_SAMPLE_FMT_S16;
		audioFrame->sample_rate = 44100;
		audioFrame->nb_samples = SampleCount;
#if defined(USE_FFMPEG4)
		codecContext->channels = 2;
		codecContext->channel_layout = AV_CH_LAYOUT_STEREO;
		audioFrame->channels = 2;
		audioFrame->channel_layout = AV_CH_LAYOUT_STEREO;
#else
		av_channel_layout_default(&codecContext->ch_layout, 2);
		av_channel_layout_default(&audioFrame->ch_layout, 2);
#endif
		int frameBufferResult = av_frame_get_buffer(audioFrame, 0);
		ok = check(frameBufferResult >= 0, "FFmpeg audio conversion fixture has sample storage") && ok;
		if (frameBufferResult >= 0)
		{
			std::vector<uint8_t> convertedAudio;
			int convertedLength = Engine::getInstance()->convert(codecContext, audioFrame,
				AV_SAMPLE_FMT_S16, 44100, 2, convertedAudio);
			ok = check(convertedLength == SampleCount * 2 * 2 &&
				convertedAudio.size() == static_cast<size_t>(convertedLength) &&
				convertedAudio.size() > 192000,
				"audio conversion safely grows beyond the removed fixed 192000-byte buffer") && ok;
			audioFrame->nb_samples = VideoStruct::MaxAudioFrameSamples + 1;
			convertedLength = Engine::getInstance()->convert(codecContext, audioFrame,
				AV_SAMPLE_FMT_S16, 44100, 2, convertedAudio);
			ok = check(convertedLength < 0 && convertedAudio.empty(),
				"oversized decoded audio frame is rejected before allocation or copy") && ok;
		}
	}
	av_frame_free(&audioFrame);
	avcodec_free_context(&codecContext);

	namespace fs = std::filesystem;
	fs::path repositoryRoot = fs::path(__FILE__).parent_path().parent_path().parent_path();
	fs::path realPackRoot = repositoryRoot / "assets" / "jxqy2";
	fs::path delayedVideoPath = realPackRoot / "video" / "begin.avi";
	if (fs::exists(delayedVideoPath))
	{
		File::setActiveResourceRoot(realPackRoot.string());
		File::setResourceFallbackRoots({});
		_video delayedVideo = Engine::getInstance()->loadVideo("video/begin.avi");
		ok = check(delayedVideo != nullptr, "real H.264 video opens through runtime loader") && ok;
		if (delayedVideo != nullptr)
		{
			int decodeCalls = 0;
			while (!delayedVideo->videoStream.decodeEnd && decodeCalls < 2000)
			{
				Engine::getInstance()->decodeNextVideo(delayedVideo);
				decodeCalls++;
			}
			ok = check(delayedVideo->videoStream.decodeEnd && decodeCalls < 2000,
				"real H.264 video reaches decoder EOF") && ok;
			ok = check(delayedVideo->decodedVideoFrameCount > 0 &&
				delayedVideo->drainedVideoFrameCount > 0,
				"runtime drains delayed H.264 tail frames") && ok;
			ok = check(delayedVideo->firstDecodedVideoTime >= 0.0f &&
				delayedVideo->lastDecodedVideoTime >= delayedVideo->firstDecodedVideoTime,
				"runtime normalizes best-effort frame timestamps to a monotonic media timeline") && ok;
			Engine::getInstance()->freeVideo(delayedVideo);
		}
	}
	return ok;
}
