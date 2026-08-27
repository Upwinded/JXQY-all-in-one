#pragma once

#include <cstddef>
#include <cstdint>

struct AudioBuffer;

namespace AudioDecodeSafety
{
constexpr std::size_t MaxEncodedAudioBytes = 128ULL * 1024ULL * 1024ULL;
constexpr std::size_t MaxDecodedAudioBytes = 128ULL * 1024ULL * 1024ULL;
constexpr int MaxAudioSampleRate = 192000;
constexpr int MaxAudioFrameSamples = MaxAudioSampleRate * 4;

struct MemoryReader
{
	const uint8_t* data = nullptr;
	int64_t size = 0;
	int64_t position = 0;
};

inline bool canAppendDecodedBytes(std::size_t currentSize, std::size_t additionalSize)
{
	return currentSize <= MaxDecodedAudioBytes &&
		additionalSize <= MaxDecodedAudioBytes - currentSize;
}

int readPacket(void* opaque, uint8_t* buffer, int bufferSize);
int64_t seekPacket(void* opaque, int64_t offset, int whence);
bool decodeFromMemory(const uint8_t* data, int size, bool loop, bool positional,
	AudioBuffer& audio);
}
