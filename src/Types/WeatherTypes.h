#pragma once

#include <algorithm>
#include <cmath>

namespace WeatherSafety
{
inline constexpr int MaximumRainDropCount = 500;
inline constexpr int MaximumRainSpeed = 1000;
inline constexpr int MaximumBoltProbability = 10000000;
inline constexpr int MaximumConfigurationBytes = 256 * 1024;

inline int normalizeRainDropCount(int value)
{
	return std::clamp(value, 0, MaximumRainDropCount);
}

inline int normalizeRainSpeed(int value)
{
	return std::clamp(value, 1, MaximumRainSpeed);
}

inline int normalizeBoltProbability(int value)
{
	return std::clamp(value, 1, MaximumBoltProbability);
}

inline bool shouldTriggerBolt(int probabilityDenominator, int randomValue)
{
	return probabilityDenominator > 0 && randomValue == 0;
}
}

enum class WeatherDepthLayer
{
	Far,
	Middle,
	Near
};

struct WeatherLayerStyle
{
	float cameraParallax = 0.0f;
	float speedScale = 1.0f;
	float alphaScale = 1.0f;
	int minimumVisualLength = 1;
	int maximumVisualLength = 1;
	int visualWidth = 1;
};

namespace WeatherParticleMotion
{
inline WeatherDepthLayer selectDepthLayer(int percentile)
{
	const int boundedPercentile = std::clamp(percentile, 0, 99);
	if (boundedPercentile < 45)
	{
		return WeatherDepthLayer::Far;
	}
	if (boundedPercentile < 85)
	{
		return WeatherDepthLayer::Middle;
	}
	return WeatherDepthLayer::Near;
}

inline WeatherLayerStyle getRainLayerStyle(WeatherDepthLayer layer)
{
	switch (layer)
	{
	case WeatherDepthLayer::Far:
		return { 0.15f, 0.70f, 0.32f, 2, 5, 1 };
	case WeatherDepthLayer::Near:
		return { 0.70f, 1.25f, 0.82f, 20, 40, 2 };
	case WeatherDepthLayer::Middle:
	default:
		return { 0.38f, 1.00f, 0.68f, 8, 18, 1 };
	}
}

inline WeatherLayerStyle getSnowLayerStyle(WeatherDepthLayer layer)
{
	switch (layer)
	{
	case WeatherDepthLayer::Far:
		return { 0.15f, 0.55f, 0.42f, 2, 2, 2 };
	case WeatherDepthLayer::Near:
		return { 0.70f, 1.35f, 1.00f, 5, 5, 5 };
	case WeatherDepthLayer::Middle:
	default:
		return { 0.38f, 0.90f, 0.72f, 3, 3, 3 };
	}
}

inline float advanceParticleAxis(
	float position,
	float velocityPerMillisecond,
	float frameTimeMilliseconds,
	float cameraDelta,
	float cameraParallax)
{
	return position
		+ velocityPerMillisecond * (std::max)(frameTimeMilliseconds, 0.0f)
		- cameraDelta * std::clamp(cameraParallax, 0.0f, 1.0f);
}

inline float calculateSnowSwayDelta(
	float phase,
	float phaseAdvance,
	float amplitude)
{
	return (std::sin(phase + phaseAdvance) - std::sin(phase))
		* (std::max)(amplitude, 0.0f);
}

inline float calculateRainStreakAngle(
	float horizontalVelocity,
	float verticalVelocity)
{
	if (std::abs(horizontalVelocity) <= 0.0001f
		&& std::abs(verticalVelocity) <= 0.0001f)
	{
		return 0.0f;
	}
	return -std::atan2(horizontalVelocity, (std::max)(verticalVelocity, 0.0001f));
}
}

enum WeatherType
{
	wtNone = 0,
	wtLightRain,
	wtRain,
	wtLightning,
	wtHeavyRain,
	wtCustomRain,
	wtSnow
};

enum DayType
{
	dtDay = 0,
	dtNight,
	dtDusk,
	dtDawn,
};
