#include "AspectFitLayout.h"

#include <algorithm>
#include <cmath>

namespace
{
std::uint8_t calculateFadeAlpha(int nearEdge, int visibleSpan)
{
	if (visibleSpan <= 0)
	{
		return 0;
	}

	constexpr long long MaximumAlpha = 220;
	const long long remaining = visibleSpan - nearEdge;
	const long long eased = remaining *
		(2LL * visibleSpan - remaining);
	const long long denominator =
		static_cast<long long>(visibleSpan) * visibleSpan;
	return static_cast<std::uint8_t>(std::max<long long>(
		1,
		MaximumAlpha * eased / denominator));
}
}

Rect AspectFitLayout::calculateFittedRect(
	int sourceWidth,
	int sourceHeight,
	int destinationWidth,
	int destinationHeight)
{
	if (sourceWidth <= 0 || sourceHeight <= 0 ||
		destinationWidth <= 0 || destinationHeight <= 0)
	{
		return { 0, 0, 0, 0 };
	}

	const double scale = std::min(
		static_cast<double>(destinationWidth) / sourceWidth,
		static_cast<double>(destinationHeight) / sourceHeight);
	const int fittedWidth = std::clamp(
		static_cast<int>(std::lround(sourceWidth * scale)),
		1,
		destinationWidth);
	const int fittedHeight = std::clamp(
		static_cast<int>(std::lround(sourceHeight * scale)),
		1,
		destinationHeight);
	return {
		(destinationWidth - fittedWidth) / 2,
		(destinationHeight - fittedHeight) / 2,
		fittedWidth,
		fittedHeight
	};
}

std::vector<AspectFitMirrorSlice> AspectFitLayout::calculateMirroredSlices(
	const Rect& sourceRect,
	const Rect& destinationRect,
	const Rect& fittedRect,
	int maximumSlices)
{
	std::vector<AspectFitMirrorSlice> slices;
	if (sourceRect.w <= 0 || sourceRect.h <= 0 ||
		destinationRect.w <= 0 || destinationRect.h <= 0 ||
		fittedRect.w <= 0 || fittedRect.h <= 0 ||
		maximumSlices <= 0)
	{
		return slices;
	}

	auto appendBar = [&](int gap, bool leading, bool horizontal)
	{
		const int sourceLength = horizontal
			? sourceRect.w
			: sourceRect.h;
		const int fittedLength = horizontal
			? fittedRect.w
			: fittedRect.h;
		const int visibleSpan = std::min(gap, fittedLength);
		const int sliceCount = std::min(maximumSlices, visibleSpan);
		if (sliceCount <= 0)
		{
			return;
		}

		for (int sliceIndex = 0; sliceIndex < sliceCount; ++sliceIndex)
		{
			const int nearEdge =
				visibleSpan * sliceIndex / sliceCount;
			const int farEdge =
				visibleSpan * (sliceIndex + 1) / sliceCount;
			const int sourceNear =
				sourceLength * nearEdge / fittedLength;
			const int sourceFar = std::max(
				sourceNear + 1,
				sourceLength * farEdge / fittedLength);

			AspectFitMirrorSlice slice;
			slice.source = sourceRect;
			slice.destination = fittedRect;
			slice.alpha = calculateFadeAlpha(nearEdge, visibleSpan);
			if (horizontal)
			{
				slice.source.x = leading
					? sourceRect.x + sourceNear
					: sourceRect.x + sourceRect.w - sourceFar;
				slice.source.w = std::min(
					sourceRect.w - sourceNear,
					sourceFar - sourceNear);
				slice.destination.x = leading
					? fittedRect.x - farEdge
					: fittedRect.x + fittedRect.w + nearEdge;
				slice.destination.w = farEdge - nearEdge;
			}
			else
			{
				slice.source.y = leading
					? sourceRect.y + sourceNear
					: sourceRect.y + sourceRect.h - sourceFar;
				slice.source.h = std::min(
					sourceRect.h - sourceNear,
					sourceFar - sourceNear);
				slice.destination.y = leading
					? fittedRect.y - farEdge
					: fittedRect.y + fittedRect.h + nearEdge;
				slice.destination.h = farEdge - nearEdge;
			}
			if (slice.source.w > 0 && slice.source.h > 0 &&
				slice.destination.w > 0 && slice.destination.h > 0)
			{
				slices.push_back(slice);
			}
		}
	};

	const int leftGap = std::max(
		0,
		fittedRect.x - destinationRect.x);
	const int rightGap = std::max(
		0,
		destinationRect.x + destinationRect.w -
			(fittedRect.x + fittedRect.w));
	const int topGap = std::max(
		0,
		fittedRect.y - destinationRect.y);
	const int bottomGap = std::max(
		0,
		destinationRect.y + destinationRect.h -
			(fittedRect.y + fittedRect.h));

	appendBar(leftGap, true, true);
	appendBar(rightGap, false, true);
	appendBar(topGap, true, false);
	appendBar(bottomGap, false, false);
	return slices;
}

float AspectFitLayout::calculateMirrorWaveNormalOffset(
	float normalizedX,
	float normalizedY,
	int fittedHeight,
	std::uint64_t animationTimeMilliseconds,
	unsigned int disturbanceSeed)
{
	if (animationTimeMilliseconds == 0 ||
		fittedHeight <= 0 ||
		!std::isfinite(normalizedX) ||
		!std::isfinite(normalizedY))
	{
		return 0.0f;
	}

	const float maximumNormalOffset = static_cast<float>(
		std::clamp(fittedHeight / 44, 9, 17));
	constexpr double MirrorWaveAnimationSpeedScale = 0.65;
	const double seconds = static_cast<double>(
		animationTimeMilliseconds % 120000ULL) / 1000.0
		* MirrorWaveAnimationSpeedScale;
	constexpr double Pi = 3.14159265358979323846;
	const double positionX =
		static_cast<double>(normalizedX) * fittedHeight;
	const double positionY =
		static_cast<double>(normalizedY) * fittedHeight;
	const double fastWaveAngle = 5.0 * Pi / 6.0;
	const double fastWaveLineDistance =
		std::cos(fastWaveAngle) * positionX -
		std::sin(fastWaveAngle) * positionY + 1.0;
	const double fastStripWave = std::sin(
		seconds * 8.0 - fastWaveLineDistance * 0.02);
	const double slowWaveAngle = 7.0 * Pi / 6.0;
	const double slowWaveLineDistance =
		std::cos(slowWaveAngle) * positionX -
		std::sin(slowWaveAngle) * positionY;
	const double slowStripWave = std::sin(
		seconds * 3.0 - slowWaveLineDistance * 0.01);
	const double pointRippleDeltaX =
		positionX + static_cast<double>(fittedHeight) * 0.12;
	const double pointRippleDeltaY =
		positionY - fittedHeight;
	const double pointRippleDistance = std::sqrt(
		pointRippleDeltaX * pointRippleDeltaX +
		pointRippleDeltaY * pointRippleDeltaY);
	const double pointRipple = std::cos(
		seconds * 3.0 - pointRippleDistance * 0.015);
	const double seed = static_cast<double>(disturbanceSeed) * 0.731;
	const double spatialNoisePhase = std::sin(
		static_cast<double>(normalizedX) * 127.1 +
		static_cast<double>(normalizedY) * 311.7 +
		seed * 19.3) * Pi;
	const double smoothDisturbance = std::sin(
		seconds * 3.3 + spatialNoisePhase +
		static_cast<double>(normalizedY) * 31.0);
	const double normalWave =
		0.38 * fastStripWave +
		0.30 * slowStripWave +
		0.22 * pointRipple +
		0.10 * smoothDisturbance;
	return static_cast<float>(normalWave * maximumNormalOffset);
}

AspectFitPointerRippleSample AspectFitLayout::calculatePointerRippleSample(
	float normalizedX,
	float normalizedY,
	int destinationWidth,
	int destinationHeight,
	int fittedHeight,
	std::uint64_t animationTimeMilliseconds,
	const AspectFitPointerRipple& ripple)
{
	if (destinationWidth <= 0 || destinationHeight <= 0 || fittedHeight <= 0 ||
		!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
		!std::isfinite(ripple.normalizedX) ||
		!std::isfinite(ripple.normalizedY) ||
		ripple.durationMilliseconds == 0 ||
		animationTimeMilliseconds < ripple.startTimeMilliseconds)
	{
		return {};
	}

	const std::uint64_t elapsedMilliseconds =
		animationTimeMilliseconds - ripple.startTimeMilliseconds;
	if (elapsedMilliseconds >= ripple.durationMilliseconds)
	{
		return {};
	}

	const double deltaX = static_cast<double>(
		normalizedX - ripple.normalizedX) * destinationWidth;
	const double deltaY = static_cast<double>(
		normalizedY - ripple.normalizedY) * destinationHeight;
	const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
	if (distance < 0.001)
	{
		return {};
	}

	const double elapsedSeconds =
		static_cast<double>(elapsedMilliseconds) / 1000.0;
	const double propagationSpeed = std::clamp(
		static_cast<double>(fittedHeight) * 0.30,
		130.0,
		260.0);
	const double propagationRadius = propagationSpeed * elapsedSeconds;
	const double wavelength = std::clamp(
		static_cast<double>(fittedHeight) / 11.0,
		40.0,
		76.0);
	const double envelopeWidth = wavelength * 1.6;
	const double distanceFromWave = distance - propagationRadius;
	const double envelope = std::exp(
		-(distanceFromWave * distanceFromWave) /
		(2.0 * envelopeWidth * envelopeWidth));
	const double lifetimeProgress =
		static_cast<double>(elapsedMilliseconds) /
		ripple.durationMilliseconds;
	const double lifetimeFade = std::pow(1.0 - lifetimeProgress, 1.25);
	constexpr double Pi = 3.14159265358979323846;
	const double phase = distanceFromWave * 2.0 * Pi / wavelength;
	double oscillation = std::cos(phase);
	if (oscillation < 0.0)
	{
		oscillation *= 0.35;
	}
	const double maximumDisplacement = std::clamp(
		static_cast<double>(fittedHeight) / 90.0,
		5.0,
		9.0);
	const double displacement = oscillation * envelope * lifetimeFade *
		maximumDisplacement;
	const double radialX = deltaX / distance;
	const double radialY = deltaY / distance;
	constexpr double LightDirectionX = -0.7071067811865475;
	constexpr double LightDirectionY = -0.7071067811865475;
	const double directionalSlope = std::clamp(
		(radialX * LightDirectionX + radialY * LightDirectionY) *
			std::sin(phase),
		-1.0,
		1.0);
	constexpr double MaximumDarkening =
		1.0 - AspectFitLayout::PointerRippleMinimumBrightness;
	const double brightness = std::clamp(
		1.0 - MaximumDarkening * envelope * lifetimeFade *
			(0.5 - 0.5 * directionalSlope),
		static_cast<double>(
			AspectFitLayout::PointerRippleMinimumBrightness),
		1.0);
	return {
		{
			static_cast<float>(displacement * radialX),
			static_cast<float>(displacement * radialY)
		},
		static_cast<float>(brightness)
	};
}

AspectFitPointerRippleSample
AspectFitLayout::calculateCombinedPointerRippleSample(
	float normalizedX,
	float normalizedY,
	int destinationWidth,
	int destinationHeight,
	int fittedHeight,
	std::uint64_t animationTimeMilliseconds,
	const std::vector<AspectFitPointerRipple>& ripples)
{
	AspectFitPointerRippleSample combinedSample;
	for (const AspectFitPointerRipple& ripple : ripples)
	{
		const AspectFitPointerRippleSample sample =
			calculatePointerRippleSample(
				normalizedX,
				normalizedY,
				destinationWidth,
				destinationHeight,
				fittedHeight,
				animationTimeMilliseconds,
				ripple);
		combinedSample.offset.x += sample.offset.x;
		combinedSample.offset.y += sample.offset.y;
		combinedSample.brightness *= sample.brightness;
	}

	const float combinedLength = std::sqrt(
		combinedSample.offset.x * combinedSample.offset.x +
		combinedSample.offset.y * combinedSample.offset.y);
	const float maximumCombinedOffset = static_cast<float>(
		std::clamp(fittedHeight / 40, 10, 18));
	if (combinedLength > maximumCombinedOffset)
	{
		const float scale = maximumCombinedOffset / combinedLength;
		combinedSample.offset.x *= scale;
		combinedSample.offset.y *= scale;
	}
	combinedSample.brightness = std::clamp(
		combinedSample.brightness,
		AspectFitLayout::CombinedPointerRippleMinimumBrightness,
		1.0f);
	return combinedSample;
}
