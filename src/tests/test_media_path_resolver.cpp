#include "../File/File.h"
#include "../Engine/SaveShotSafety.h"
#include "../Game/Data/MediaPathResolver.h"
#include "../Game/GameTypes.h"
#include "../Image/EncodedImageSafety.h"
#include "../Image/ImagePackagePathCandidates.h"
#include "../Image/PicDecoder.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << "\n";
		return false;
	}
	return true;
}

bool checkEqual(const std::string& actual, const std::string& expected, const char* message)
{
	if (actual != expected)
	{
		std::cerr << "FAILED: " << message << " actual=" << actual << " expected=" << expected << "\n";
		return false;
	}
	return true;
}

bool checkOneOf(const std::string& actual, const std::set<std::string>& expected, const char* message)
{
	if (expected.find(actual) == expected.end())
	{
		std::cerr << "FAILED: " << message << " actual=" << actual << "\n";
		return false;
	}
	return true;
}

std::string normalizePath(std::string value)
{
	for (char& ch : value)
	{
		if (ch == '\\')
		{
			ch = '/';
		}
	}
	return value;
}

void writeFile(const std::filesystem::path& path)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << "x";
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << content;
}

void writeInt32(std::vector<uint8_t>& data, size_t offset, int32_t value)
{
	data[offset + 0] = static_cast<uint8_t>(value & 0xFF);
	data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
	data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
	data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void writeBigEndian32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
	data[offset + 0] = static_cast<uint8_t>(value >> 24);
	data[offset + 1] = static_cast<uint8_t>(value >> 16);
	data[offset + 2] = static_cast<uint8_t>(value >> 8);
	data[offset + 3] = static_cast<uint8_t>(value);
}

std::vector<uint8_t> makeMpcLikeFile(const char* signature, int picCount,
	int width, int height, const std::vector<uint8_t>& framePayload,
	bool withPalette, bool payloadOnlyLength = false)
{
	const int paletteLength = withPalette ? 1 : 0;
	const size_t frameDataStart = 128 + static_cast<size_t>(paletteLength) * 4 +
		static_cast<size_t>(picCount) * 4;
	std::vector<uint8_t> data(frameDataStart + 20 + framePayload.size(), 0);
	std::memcpy(data.data(), signature, 16);
	writeInt32(data, 68, width);
	writeInt32(data, 72, height);
	writeInt32(data, 76, picCount);
	writeInt32(data, 80, 1);
	writeInt32(data, 84, paletteLength);
	writeInt32(data, 88, 66);
	for (int i = 0; i < picCount; i++)
	{
		writeInt32(data, 128 + static_cast<size_t>(paletteLength) * 4 +
			static_cast<size_t>(i) * 4, 0);
	}
	writeInt32(data, frameDataStart, static_cast<int32_t>(
		(payloadOnlyLength ? 0 : 20) + framePayload.size()));
	writeInt32(data, frameDataStart + 4, width);
	writeInt32(data, frameDataStart + 8, height);
	std::copy(framePayload.begin(), framePayload.end(), data.begin() + frameDataStart + 20);
	return data;
}

std::vector<uint8_t> makeAsfFile(int picCount, int width, int height,
	const std::vector<uint8_t>& framePayload)
{
	const int paletteLength = 1;
	const size_t frameDataStart = 64 + 4 + static_cast<size_t>(picCount) * 8;
	std::vector<uint8_t> data(frameDataStart + framePayload.size(), 0);
	std::memcpy(data.data(), "ASF 1.00", 9);
	writeInt32(data, 16, width);
	writeInt32(data, 20, height);
	writeInt32(data, 24, picCount);
	writeInt32(data, 28, 1);
	writeInt32(data, 32, paletteLength);
	writeInt32(data, 36, 66);
	for (int i = 0; i < picCount; i++)
	{
		size_t tableOffset = 68 + static_cast<size_t>(i) * 8;
		writeInt32(data, tableOffset, static_cast<int32_t>(frameDataStart));
		writeInt32(data, tableOffset + 4, static_cast<int32_t>(framePayload.size()));
	}
	std::copy(framePayload.begin(), framePayload.end(), data.begin() + frameDataStart);
	return data;
}
}

int main()
{
	namespace fs = std::filesystem;
	fs::path root = makeUniqueTestDirectory("jxqy-media-path-resolver-tests");
	fs::remove_all(root);
	fs::path activeRoot = root / "active";
	fs::path firstParentRoot = root / "parent-a";
	fs::path secondParentRoot = root / "parent-b";
	fs::path platformStateRoot = root / "state";
	const std::string saveNamespace = "media_path_resolver";
	writeFile(activeRoot / "sound" / "attack04.wav");
	writeFile(activeRoot / "sound" / "voice.xnb");
	writeFile(activeRoot / "sound" / "voice.wav");
	writeFile(activeRoot / "sound" / "ui" / "click.wav");
	writeFile(activeRoot / "sound" / fs::u8path(u8"中文 文件.v1.wav"));
	writeFile(activeRoot / "music" / "mc001.mp3");
	writeFile(activeRoot / "music" / "mc023.xnb");
	writeFile(activeRoot / "music" / "mc023.wma");
	writeFile(activeRoot / "music" / "bgm" / "mc002.mp3");
	writeFile(activeRoot / "music" / "cross.ogg");
	fs::create_directories(activeRoot / "music" / "directory-block.mp3");
	writeFile(activeRoot / "music" / "directory-block.ogg");
	writeFile(activeRoot / "asf" / "character" / "hero.asf");
	writeFile(activeRoot / "video" / "Open.avi");
	writeFile(activeRoot / "video" / "intro" / "Open.avi");
	writeFile(firstParentRoot / "music" / "cross.mp3");
	writeFile(firstParentRoot / "music" / "parent-order.ogg");
	writeFile(firstParentRoot / "mpc" / "character" / "hero.mpc");
	writeFile(secondParentRoot / "music" / "parent-order.mp3");
	writeFile(root / "outside.wav");
	writeFile(root / "outside-directory" / "secret.wav");
	writeFile(activeRoot / "fallback" / "corrupt.bin", "corrupt");
	writeFile(activeRoot / "fallback" / "valid.bin", "valid");
	writeFile(activeRoot / "fallback" / "also-corrupt.bin", "corrupt");
	writeFile(activeRoot / "oversized.bin");
	writeFile(activeRoot / "budget.bin", "0123456789abcdef");
	writeFile(platformStateRoot / "save" / saveNamespace /
		"negative.bin", "keep");
	std::error_code oversizedFileError;
	fs::resize_file(activeRoot / "oversized.bin",
		static_cast<std::uintmax_t>((std::numeric_limits<int>::max)()),
		oversizedFileError);
	writeFile(firstParentRoot / "fallback" / "corrupt.bin", "parent-valid");

	File::setActiveResourceRoot(activeRoot.string());
	File::setResourceFallbackRoots({ firstParentRoot.string(), secondParentRoot.string() });
	File::setPlatformStateParentForTests(platformStateRoot.string());
	File::setActiveSaveNamespace(saveNamespace);

	bool ok = true;
	ok = check(!oversizedFileError,
		"test fixture creates an oversized sparse resource") && ok;
	if (!oversizedFileError)
	{
		std::unique_ptr<char[]> oversizedData;
		int oversizedLength = 1;
		ok = check(!File::readFile("oversized.bin", oversizedData, oversizedLength) &&
			oversizedData == nullptr && oversizedLength == 0,
			"resource reader rejects INT_MAX bytes before length plus terminator overflows") && ok;
	}
	ok = check(File::fileExist("sound\\attack04.wav"), "test fixture sound file exists through File") && ok;
	ok = check(File::fileExist("music\\mc001.mp3"), "test fixture music file exists through File") && ok;
	ok = check(File::fileExist("video\\open.avi"), "test fixture video file exists through File") && ok;
	ok = check(File::fileExist("\\sound\\attack04.wav"), "legacy single leading separator stays virtual-root relative") && ok;
	ok = check(File::fileExist(u8"sound\\中文 文件.v1.wav"), "UTF-8 resource name with spaces and internal dots remains valid") && ok;
	std::string invalidUtf8 = "sound/";
	invalidUtf8.push_back(static_cast<char>(0xFF));
	invalidUtf8 += ".wav";
	ok = check(!File::isSafeResourcePath(invalidUtf8) && !File::fileExist(invalidUtf8),
		"invalid UTF-8 resource names fail closed without filesystem exceptions") && ok;
	std::string embeddedNull("sound/hidden\0.wav", 17);
	ok = check(!File::isSafeResourcePath(embeddedNull),
		"embedded NUL cannot truncate a virtual resource path") && ok;
	ok = check(!File::isSafeResourcePath("save/NUL.sav") &&
		!File::isSafeResourcePath("save/con.txt") &&
		!File::isSafeResourcePath("save/COM1.log") &&
		!File::isSafeResourcePath("save/lpt9"),
		"Win32 device-name components are rejected before file access") && ok;
	ok = check(!File::fileExist("../outside.wav"), "parent traversal cannot escape the active resource root") && ok;
	ok = check(!File::fileExist(".. /outside.wav"), "Win32 trailing-space parent traversal spelling is rejected") && ok;
	ok = check(!File::fileExist(".../outside.wav"), "Win32 trailing-dot path component is rejected") && ok;
	ok = check(!File::fileExist("C:\\outside.wav"), "drive-qualified resource path is rejected") && ok;
	ok = check(!File::fileExist("\\\\server\\share\\outside.wav"), "UNC resource path is rejected") && ok;
	ok = check(File::listFiles("").empty(), "empty directory name cannot enumerate a resource root") && ok;
	ok = check(!File::fileExist("/"), "separator-only resource name is rejected") && ok;
	ok = check(File::fileExist("music/directory-block.mp3"),
		"fileExist preserves the legacy directory-probe contract") && ok;
	std::unique_ptr<char[]> budgetData;
	int budgetLength = 1;
	ok = check(!File::readFile("budget.bin", budgetData, budgetLength, 8) &&
		budgetData == nullptr && budgetLength == 0,
		"caller read budget rejects a known oversized file before allocation") && ok;
	bool budgetVisitorCalled = false;
	ok = check(!File::visitReadableResources({ "budget.bin" }, 8,
		[&](const std::string&, std::unique_ptr<char[]>&, int)
		{
			budgetVisitorCalled = true;
			return true;
		}) && !budgetVisitorCalled,
		"bounded resource visitor does not invoke decoders for oversized input") && ok;
	const char replacement = 'x';
	File::writeFile("save/negative.bin", &replacement, -1);
	File::appendFile("save/negative.bin", &replacement, -1);
	std::unique_ptr<char[]> negativeWriteData;
	int negativeWriteLength = 0;
	ok = check(File::readFile("save/negative.bin", negativeWriteData, negativeWriteLength) &&
		negativeWriteLength == 4 && std::memcmp(negativeWriteData.get(), "keep", 4) == 0,
		"negative write lengths are rejected without truncating or appending") && ok;
	std::unique_ptr<char[]> escapedData;
	int escapedLength = 0;
	ok = check(!File::readFile("../outside.wav", escapedData, escapedLength),
		"parent traversal cannot be read through File") && ok;
	std::error_code symlinkError;
	fs::create_directory_symlink(root / "outside-directory", activeRoot / "linked-sound", symlinkError);
	if (!symlinkError)
	{
		ok = check(File::fileExist("linked-sound/secret.wav"),
			"open formal resources follow the current descendant directory link") && ok;
		ok = check(
			File::readFile(
				"linked-sound/secret.wav",
				escapedData,
				escapedLength) &&
			escapedLength == 1 &&
			escapedData != nullptr &&
			escapedData[0] == 'x',
			"open formal resources read through the current descendant directory link") && ok;
		File::setResourceFallbackRoots({});
		ok = checkEqual(
			normalizePath(
				File::getAssetsName(
					"linked-sound/secret.wav")),
			normalizePath(
				(activeRoot / "linked-sound" /
					"secret.wav").string()),
			"formal media keeps the logical linked path") && ok;
		File::setResourceFallbackRoots({ firstParentRoot.string(), secondParentRoot.string() });
	}
	ok = check(buildMediaAssetCandidates(SOUND_FOLDER, "../outside", { ".wav" }).empty(),
		"unsafe media name produces no candidates") && ok;
	ok = check(ImagePackagePathCandidates::build("../outside.asf").empty(),
		"unsafe image package name produces no candidates") && ok;
	std::vector<std::string> visitedCandidates;
	bool acceptedResource = File::visitReadableResources(
		{ "fallback/corrupt.bin", "fallback/valid.bin" },
		[&](const std::string& resourceName, std::unique_ptr<char[]>& data, int length)
		{
			visitedCandidates.push_back(normalizePath(resourceName));
			return length == 5 && std::string(data.get(), static_cast<size_t>(length)) == "valid";
		});
	ok = check(acceptedResource && visitedCandidates.size() == 2 &&
		visitedCandidates[0] == "fallback/corrupt.bin" &&
		visitedCandidates[1] == "fallback/valid.bin",
		"read visitor continues to a valid alternate after a corrupt same-root candidate") && ok;
	visitedCandidates.clear();
	acceptedResource = File::visitReadableResources(
		{ "fallback/also-corrupt.bin", "fallback/corrupt.bin" },
		[&](const std::string& resourceName, std::unique_ptr<char[]>& data, int length)
		{
			visitedCandidates.push_back(normalizePath(resourceName));
			return length == 12 && std::string(data.get(), static_cast<size_t>(length)) == "parent-valid";
		});
	ok = check(acceptedResource && visitedCandidates.size() == 3 &&
		visitedCandidates[0] == "fallback/also-corrupt.bin" &&
		visitedCandidates[1] == "fallback/corrupt.bin" &&
		visitedCandidates[2] == "fallback/corrupt.bin",
		"read visitor exhausts active candidates before continuing in the parent root") && ok;

	std::vector<std::string> soundCandidates = buildMediaAssetCandidates(SOUND_FOLDER, "attack04", { ".wav" });
	ok = check(soundCandidates.size() == 4, "extensionless sound generates direct and case-variant wav candidates") && ok;
	ok = checkEqual(normalizePath(soundCandidates[0]), "sound/attack04", "sound direct candidate keeps original name") && ok;
	ok = checkEqual(normalizePath(soundCandidates[1]), "sound/attack04.wav", "sound fallback appends wav") && ok;

	ok = checkEqual(normalizePath(resolveSoundAssetPath("attack04")), "sound/attack04.wav",
		"extensionless sound resolves wav file") && ok;
	ok = checkEqual(normalizePath(resolveSoundAssetPath("sound\\attack04")), "sound/attack04.wav",
		"explicit sound folder does not get duplicated") && ok;
	ok = checkEqual(normalizePath(resolveSoundAssetPath("ui\\click")), "sound/ui/click.wav",
		"sound resolver preserves subdirectories for extension fallback") && ok;
	ok = checkEqual(normalizePath(resolveSoundAssetPath("voice.xnb")), "sound/voice.wav",
		"sound resolver treats XNB suffix as legacy name and resolves wav") && ok;
	std::string resolvedMusic = normalizePath(resolveMediaAssetPath(MUSIC_FOLDER, "MC001.wav", { ".mp3", ".ogg", ".wma", ".wav" }));
	ok = checkOneOf(resolvedMusic, { "music/MC001.mp3", "music/mc001.mp3" },
		"music resolver falls back across ASCII case variants") && ok;
	ok = check(File::fileExist(resolvedMusic), "resolved music path is readable") && ok;
	std::string resolvedXnbMusic = normalizePath(resolveMediaAssetPath(MUSIC_FOLDER, "Mc023.xnb", { ".mp3", ".ogg", ".wma", ".wav" }));
	ok = checkOneOf(resolvedXnbMusic, { "music/Mc023.wma", "music/mc023.wma" },
		"music resolver treats XNB suffix as legacy name and resolves playable audio") && ok;
	ok = check(File::fileExist(resolvedXnbMusic), "resolved XNB-suffixed music fallback is readable") && ok;
	std::string resolvedNestedMusic = normalizePath(resolveMediaAssetPath(MUSIC_FOLDER, "bgm\\MC002.wav", { ".mp3", ".ogg", ".wma", ".wav" }));
	ok = checkOneOf(resolvedNestedMusic, { "music/bgm/MC002.mp3", "music/bgm/mc002.mp3" },
		"music resolver preserves subdirectories for fallback candidates") && ok;
	ok = check(File::fileExist(resolvedNestedMusic), "resolved nested music path is readable") && ok;
	std::string resolvedVideo = normalizePath(resolveMediaAssetPath(VIDEO_FOLDER, "open", { ".avi", ".mp4" }));
	ok = checkOneOf(resolvedVideo, { "video/open.avi", "video/Open.avi" },
		"video resolver tries capitalized ASCII basename") && ok;
	ok = check(File::fileExist(resolvedVideo), "resolved video path is readable") && ok;
	std::string resolvedNestedVideo = normalizePath(resolveMediaAssetPath(VIDEO_FOLDER, "video\\intro\\open", { ".avi", ".mp4" }));
	ok = checkOneOf(resolvedNestedVideo, { "video/intro/open.avi", "video/intro/Open.avi" },
		"explicit video folder and nested fallback resolve together") && ok;
	ok = check(File::fileExist(resolvedNestedVideo), "resolved nested video path is readable") && ok;
	std::string rootMajorMusic = normalizePath(resolveMediaAssetPath(MUSIC_FOLDER,
		"cross", { ".mp3", ".ogg" }));
	ok = checkEqual(rootMajorMusic, "music/cross.ogg",
		"active package alternate extension wins over parent exact-priority extension") && ok;
	std::string parentOrderMusic = normalizePath(resolveMediaAssetPath(MUSIC_FOLDER,
		"parent-order", { ".mp3", ".ogg" }));
	ok = checkEqual(parentOrderMusic, "music/parent-order.ogg",
		"earlier dependency root wins before candidate extension order") && ok;
	std::string directoryFallbackMusic = normalizePath(resolveMediaAssetPath(MUSIC_FOLDER,
		"directory-block", { ".mp3", ".ogg" }));
	ok = checkEqual(directoryFallbackMusic, "music/directory-block.ogg",
		"a directory-shaped candidate cannot block a readable extension fallback") && ok;
	std::string rootMajorImage = normalizePath(File::resolveFirstExistingResource(
		ImagePackagePathCandidates::build("mpc/character/hero.mpc")));
	ok = checkEqual(rootMajorImage, "asf/character/hero.asf",
		"active ASF fallback wins over parent MPC exact candidate") && ok;
	ok = checkEqual(normalizePath(resolveSoundAssetPath("missing")), "sound/missing",
		"missing sound returns direct path for existing read-file logging") && ok;

	PicDecodedFile decoded;
	auto validShadow = makeMpcLikeFile("SHD File Ver2.0", 1, 1, 1, { 1 }, false);
	ok = check(PicDecoder::decodeSHD(validShadow.data(), static_cast<int>(validShadow.size()), decoded),
		"one-pixel SHD fixture decodes") && ok;
	ok = check(decoded.frames.size() == 1 && decoded.frames[0].pixelData.size() == 4 &&
		decoded.frames[0].pixelData[3] == 128,
		"SHD alpha keeps the repository conversion-chain value 128") && ok;
	auto truncatedMpc = makeMpcLikeFile("MPC File Ver2.0", 1, 2, 1, { 2, 0 }, true);
	ok = check(!PicDecoder::decodeMPC(truncatedMpc.data(), static_cast<int>(truncatedMpc.size()), decoded),
		"truncated MPC palette run is rejected") && ok;
	auto payloadLengthMpc = makeMpcLikeFile("MPC File Ver2.0", 1, 1, 1,
		{ 1, 0 }, true, true);
	ok = check(PicDecoder::decodeMPC(payloadLengthMpc.data(),
		static_cast<int>(payloadLengthMpc.size()), decoded),
		"legacy MPC payload-only data length is decoded without truncation") && ok;
	auto truncatedAsf = makeAsfFile(1, 1, 1, { 1, 255 });
	ok = check(!PicDecoder::decodeASF(truncatedAsf.data(), static_cast<int>(truncatedAsf.size()), decoded),
		"truncated ASF palette run is rejected") && ok;
	auto aggregateMpc = makeMpcLikeFile("MPC File Ver2.0", 2, 8192, 8192, { 0 }, true);
	ok = check(!PicDecoder::decodeMPC(aggregateMpc.data(), static_cast<int>(aggregateMpc.size()), decoded),
		"aggregate MPC decoded-pixel budget is enforced before allocation") && ok;
	auto aggregateAsf = makeAsfFile(2, 8192, 8192, {});
	ok = check(!PicDecoder::decodeASF(aggregateAsf.data(), static_cast<int>(aggregateAsf.size()), decoded),
		"aggregate ASF decoded-pixel budget is enforced") && ok;
	std::vector<uint8_t> oversizedPng(24, 0);
	const uint8_t pngSignature[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::memcpy(oversizedPng.data(), pngSignature, sizeof(pngSignature));
	writeBigEndian32(oversizedPng, 8, 13);
	std::memcpy(oversizedPng.data() + 12, "IHDR", 4);
	writeBigEndian32(oversizedPng, 16, 100000);
	writeBigEndian32(oversizedPng, 20, 100000);
	ok = check(!EncodedImageSafety::hasSafeDimensions(
		oversizedPng.data(), oversizedPng.size()),
		"oversized PNG dimensions are rejected before SDL_image allocation") && ok;
	writeBigEndian32(oversizedPng, 16, 1);
	writeBigEndian32(oversizedPng, 20, 1);
	ok = check(EncodedImageSafety::hasSafeDimensions(
		oversizedPng.data(), oversizedPng.size()),
		"bounded PNG dimensions pass allocation preflight") && ok;
	std::vector<uint8_t> oversizedGif(26, 0);
	std::memcpy(oversizedGif.data(), "GIF89a", 6);
	oversizedGif[6] = 1;
	oversizedGif[8] = 1;
	oversizedGif[13] = 0x2C;
	oversizedGif[18] = 0x20;
	oversizedGif[19] = 0x4E;
	oversizedGif[20] = 0x20;
	oversizedGif[21] = 0x4E;
	oversizedGif[23] = 2;
	oversizedGif[24] = 0;
	oversizedGif[25] = 0x3B;
	ok = check(!EncodedImageSafety::hasSafeDimensions(
		oversizedGif.data(), oversizedGif.size()),
		"GIF frame descriptor cannot hide behind a tiny logical screen") && ok;
	oversizedGif[18] = 1;
	oversizedGif[19] = 0;
	oversizedGif[20] = 1;
	oversizedGif[21] = 0;
	ok = check(EncodedImageSafety::hasSafeDimensions(
		oversizedGif.data(), oversizedGif.size()),
		"bounded GIF descriptor passes structural allocation preflight") && ok;
	std::vector<uint8_t> oversizedQoi(14, 0);
	std::memcpy(oversizedQoi.data(), "qoif", 4);
	writeBigEndian32(oversizedQoi, 4, 100000);
	writeBigEndian32(oversizedQoi, 8, 100000);
	oversizedQoi[12] = 4;
	ok = check(!EncodedImageSafety::hasSafeDimensions(
		oversizedQoi.data(), oversizedQoi.size()),
		"QOI dimensions are bounded before SDL_image allocation") && ok;
	writeBigEndian32(oversizedQoi, 4, 1);
	writeBigEndian32(oversizedQoi, 8, 1);
	ok = check(EncodedImageSafety::hasSafeDimensions(
		oversizedQoi.data(), oversizedQoi.size()),
		"bounded QOI dimensions pass allocation preflight") && ok;
	std::vector<uint8_t> webpHeader(30, 0);
	std::memcpy(webpHeader.data(), "RIFF", 4);
	writeInt32(webpHeader, 4, 22);
	std::memcpy(webpHeader.data() + 8, "WEBPVP8X", 8);
	writeInt32(webpHeader, 16, 10);
	ok = check(EncodedImageSafety::hasSafeDimensions(webpHeader.data(), webpHeader.size()),
		"bounded VP8X WebP dimensions pass allocation preflight") && ok;
	webpHeader[24] = 0x9F;
	webpHeader[25] = 0x86;
	webpHeader[26] = 0x01;
	webpHeader[27] = 0x9F;
	webpHeader[28] = 0x86;
	webpHeader[29] = 0x01;
	ok = check(!EncodedImageSafety::hasSafeDimensions(webpHeader.data(), webpHeader.size()),
		"oversized VP8X WebP dimensions are rejected before allocation") && ok;
	std::vector<uint8_t> tgaHeader(21, 0);
	tgaHeader[2] = 2;
	tgaHeader[12] = 1;
	tgaHeader[14] = 1;
	tgaHeader[16] = 24;
	ok = check(EncodedImageSafety::hasSafeDimensions(tgaHeader.data(), tgaHeader.size()),
		"bounded uncompressed TGA dimensions pass allocation preflight") && ok;
	std::vector<uint8_t> oversizedTga(22, 0);
	oversizedTga[2] = 10;
	oversizedTga[12] = 0xFF;
	oversizedTga[13] = 0xFF;
	oversizedTga[14] = 0xFF;
	oversizedTga[15] = 0xFF;
	oversizedTga[16] = 24;
	ok = check(!EncodedImageSafety::hasSafeDimensions(oversizedTga.data(), oversizedTga.size()),
		"oversized RLE TGA dimensions are rejected before allocation") && ok;
	const char unsupportedImage[] = "<svg width='100000' height='100000'/>";
	ok = check(!EncodedImageSafety::hasSafeDimensions(
		unsupportedImage, sizeof(unsupportedImage) - 1),
		"formats without a reliable allocation preflight fail closed") && ok;
	std::vector<uint8_t> jpegSvgPolyglot(261, ' ');
	const uint8_t jpegPrefix[] =
		{ 0xFF, 0xD8, 0xFF, 0xC0, 0x01, 0x01, 0x08, 0x01, 0x01, 0x01, 0x01 };
	std::memcpy(jpegSvgPolyglot.data(), jpegPrefix, sizeof(jpegPrefix));
	const char svgPayload[] = "<svg width='9000' height='9000'></svg>";
	std::memcpy(jpegSvgPolyglot.data() + sizeof(jpegPrefix), svgPayload,
		sizeof(svgPayload) - 1);
	EncodedImageSafety::Dimensions polyglotDimensions;
	ok = check(EncodedImageSafety::inspectSafeDimensions(jpegSvgPolyglot.data(),
		jpegSvgPolyglot.size(), polyglotDimensions) &&
		polyglotDimensions.format == EncodedImageSafety::Format::Jpeg &&
		std::strcmp(EncodedImageSafety::getDecoderType(polyglotDimensions.format), "JPG") == 0,
		"preflight returns the exact typed decoder for polyglot-safe codec binding") && ok;

	std::vector<uint8_t> saveShot(SaveShotSafety::HeaderLength + 12, 0);
	std::memcpy(saveShot.data(), SaveShotSafety::Signature,
		SaveShotSafety::SignatureLength);
	writeInt32(saveShot, SaveShotSafety::SignatureLength, 2);
	writeInt32(saveShot, SaveShotSafety::SignatureLength + 4, 1);
	writeInt32(saveShot, SaveShotSafety::SignatureLength + 8, 0xFFFF);
	SaveShotSafety::View saveShotView;
	ok = check(SaveShotSafety::parse(
		reinterpret_cast<const char*>(saveShot.data()), static_cast<int>(saveShot.size()),
		saveShotView) && saveShotView.width == 2 && saveShotView.height == 1 &&
		saveShotView.pixelBytes == 8 &&
		saveShotView.pixels == reinterpret_cast<const char*>(saveShot.data()) +
			SaveShotSafety::HeaderLength,
		"private save-shot parser accepts the legacy header and ignores trailing bytes") && ok;
	std::vector<uint8_t> shortSaveShotHeader(SaveShotSafety::HeaderLength - 1, 0);
	std::memcpy(shortSaveShotHeader.data(), SaveShotSafety::Signature,
		SaveShotSafety::SignatureLength);
	ok = check(!SaveShotSafety::parse(
		reinterpret_cast<const char*>(shortSaveShotHeader.data()),
		static_cast<int>(shortSaveShotHeader.size()), saveShotView),
		"save-shot parser rejects a truncated metadata header before memcpy") && ok;
	std::vector<uint8_t> truncatedSaveShot(SaveShotSafety::HeaderLength + 7, 0);
	std::memcpy(truncatedSaveShot.data(), SaveShotSafety::Signature,
		SaveShotSafety::SignatureLength);
	writeInt32(truncatedSaveShot, SaveShotSafety::SignatureLength, 2);
	writeInt32(truncatedSaveShot, SaveShotSafety::SignatureLength + 4, 1);
	ok = check(!SaveShotSafety::parse(
		reinterpret_cast<const char*>(truncatedSaveShot.data()),
		static_cast<int>(truncatedSaveShot.size()), saveShotView),
		"save-shot parser rejects a truncated raw-pixel payload") && ok;
	std::vector<uint8_t> overflowingSaveShot(SaveShotSafety::HeaderLength, 0);
	std::memcpy(overflowingSaveShot.data(), SaveShotSafety::Signature,
		SaveShotSafety::SignatureLength);
	writeInt32(overflowingSaveShot, SaveShotSafety::SignatureLength, 65536);
	writeInt32(overflowingSaveShot, SaveShotSafety::SignatureLength + 4, 65536);
	ok = check(!SaveShotSafety::parse(
		reinterpret_cast<const char*>(overflowingSaveShot.data()),
		static_cast<int>(overflowingSaveShot.size()), saveShotView) &&
		!SaveShotSafety::isPixelBufferValid(65536, 65536, 0),
		"save-shot dimensions cannot wrap signed pixel-byte multiplication") && ok;
	ok = check(SaveShotSafety::isPixelBufferValid(2, 1, 8) &&
		!SaveShotSafety::isPixelBufferValid(2, 1, 7) &&
		!SaveShotSafety::isPixelBufferValid(-1, 1, 8),
		"save-shot texture boundary requires positive dimensions and complete pixels") && ok;

	File::setPlatformStateParentForTests("");
	File::setActiveSaveNamespace("");
	fs::remove_all(root);
	return ok ? 0 : 1;
}
