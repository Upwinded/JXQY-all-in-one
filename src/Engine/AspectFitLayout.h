#pragma once

#include <cstdint>
#include <vector>

#include "../Types/ElementTypes.h"

namespace AspectFitLayout
{
	constexpr std::uint64_t PointerRippleDurationMilliseconds = 1800;
	constexpr float PointerRippleMinimumBrightness = 0.93f;
	constexpr float CombinedPointerRippleMinimumBrightness = 0.88f;
}

struct AspectFitMirrorSlice
{
	Rect source = { 0, 0, 0, 0 };
	Rect destination = { 0, 0, 0, 0 };
	std::uint8_t alpha = 0;
};

struct AspectFitPointerRipple
{
	float normalizedX = 0.0f;
	float normalizedY = 0.0f;
	std::uint64_t startTimeMilliseconds = 0;
	std::uint64_t durationMilliseconds =
		AspectFitLayout::PointerRippleDurationMilliseconds;
};

struct AspectFitPointerRippleOffset
{
	float x = 0.0f;
	float y = 0.0f;
};

struct AspectFitPointerRippleSample
{
	AspectFitPointerRippleOffset offset;
	float brightness = 1.0f;
};

namespace AspectFitLayout
{
	Rect calculateFittedRect(
		int sourceWidth,
		int sourceHeight,
		int destinationWidth,
		int destinationHeight);

	std::vector<AspectFitMirrorSlice> calculateMirroredSlices(
		const Rect& sourceRect,
		const Rect& destinationRect,
		const Rect& fittedRect,
		int maximumSlices = 32);

	float calculateMirrorWaveNormalOffset(
		float normalizedX,
		float normalizedY,
		int fittedHeight,
		std::uint64_t animationTimeMilliseconds,
		unsigned int disturbanceSeed);

	AspectFitPointerRippleSample calculatePointerRippleSample(
		float normalizedX,
		float normalizedY,
		int destinationWidth,
		int destinationHeight,
		int fittedHeight,
		std::uint64_t animationTimeMilliseconds,
		const AspectFitPointerRipple& ripple);

	AspectFitPointerRippleSample calculateCombinedPointerRippleSample(
		float normalizedX,
		float normalizedY,
		int destinationWidth,
		int destinationHeight,
		int fittedHeight,
		std::uint64_t animationTimeMilliseconds,
		const std::vector<AspectFitPointerRipple>& ripples);
}
