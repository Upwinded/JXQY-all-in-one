#pragma once

#include <cstddef>
#include <cstdint>

struct AudioBuffer;
using Music_t = AudioBuffer;
using _music = Music_t*;

class EngineBase;

class AudioChannelHandle
{
public:
	constexpr AudioChannelHandle(std::nullptr_t = nullptr)
	{
	}

	AudioChannelHandle& operator=(std::nullptr_t)
	{
		slotIndex = InvalidSlotIndex;
		generation = 0;
		return *this;
	}

	explicit constexpr operator bool() const
	{
		return slotIndex != InvalidSlotIndex && generation != 0;
	}

	friend constexpr bool operator==(
		AudioChannelHandle left, AudioChannelHandle right)
	{
		return left.slotIndex == right.slotIndex &&
			left.generation == right.generation;
	}

	friend constexpr bool operator!=(
		AudioChannelHandle left, AudioChannelHandle right)
	{
		return !(left == right);
	}

	friend constexpr bool operator==(
		AudioChannelHandle handle, std::nullptr_t)
	{
		return !static_cast<bool>(handle);
	}

	friend constexpr bool operator==(
		std::nullptr_t, AudioChannelHandle handle)
	{
		return handle == nullptr;
	}

	friend constexpr bool operator!=(
		AudioChannelHandle handle, std::nullptr_t)
	{
		return static_cast<bool>(handle);
	}

	friend constexpr bool operator!=(
		std::nullptr_t, AudioChannelHandle handle)
	{
		return handle != nullptr;
	}

private:
	static constexpr std::size_t InvalidSlotIndex =
		static_cast<std::size_t>(-1);

	constexpr AudioChannelHandle(
		std::size_t slotIndex, std::uint64_t generation)
		: slotIndex(slotIndex), generation(generation)
	{
	}

	std::size_t slotIndex = InvalidSlotIndex;
	std::uint64_t generation = 0;

	friend class EngineBase;
};

using _channel = AudioChannelHandle;

struct VideoStruct;
using Video_t = VideoStruct;
using _video = Video_t*;
