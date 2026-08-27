#pragma once
#include <vector>
#include <algorithm>
#include "../../Engine/AspectFitLayout.h"
#include "../../Engine/ImageTypes.h"
#include "../../Types/Types.h"

namespace WaterEffectSafety
{
inline constexpr int MaximumGridSize = 256;
inline constexpr int MaximumClickRippleCount = 64;

inline bool isValidGridSize(int gridSize)
{
	return gridSize > 0 && gridSize <= MaximumGridSize;
}

inline bool isClickRippleActive(
	UTime currentTime,
	UTime startTime,
	UTime lifeTime)
{
	return lifeTime > 0 &&
		(currentTime < startTime || currentTime - startTime < lifeTime);
}
}


struct FPoint
{
	float x = 0.0f;
	float y = 0.0f;
};

struct WaterRippleParams
{
	float amplitude = 0.0f;
	float frequency = 0.0f;
	float density = 0.0f;
	FPoint pos = { 0.0f, 0.0f };
};

struct WaterClickRippleParams
{
	FPoint pos = { 0.0f, 0.0f };
	UTime startTime = 0;
	UTime lifeTime = AspectFitLayout::PointerRippleDurationMilliseconds;
};

struct WaterWaveParams
{
	float amplitude = 0.0f;
	float frequency = 0.0f;
	float density = 0.0f;
	float angle = 0.0f;
	float phi = 0.0f;
};

struct WaterWaveCalculatedParams
{
	float A = 0.0f;
	float B = 0.0f;
	WaterWaveParams basicParams;
};

struct WaterLightParams
{
	float minDistance = 0.0f;
	float minimumBrightness = 0.94f;
	float decay = 1000.0f;
	float angle = 0.0f;
};

struct WaterEffectParams
{
	int gridSize = 50;
	std::vector<WaterWaveCalculatedParams> waves;
	std::vector<WaterRippleParams> fixedRipples;
	int maxClickRipple = 5;
	std::vector<WaterClickRippleParams> clickRipples;
	WaterClickRippleParams defaultClickRipple;
	WaterLightParams light;
};

class WaterEffect
{
public:
	void setupEffectCanvas();
	void renderEffect(UTime time, PointEx cameraPos);

	void applyPresetParams();

	void clearParams();
	void initGrid();
	void setGridSize(int gridSize);
	void addWave(WaterWaveParams params);
	void addFixedRipple(WaterRippleParams params);
	void setDefaultClickRippleParams(WaterClickRippleParams params);
	void addClickRipple(WaterClickRippleParams params);
	void addDefaultClickRipple(float x, float y, UTime startTime);
	void setLightParams(WaterLightParams params);
	void setMaxClickRipple(int count);

private:
	WaterEffectParams _params;

	_image _tempRenderTarget = nullptr;
	_shared_image _waterEffectCanvas = nullptr;
	bool _effectRenderTargetActive = false;
	std::vector<Vertex> _vertices;
	std::vector<Vertex> _verticesOrigin;
	std::vector<Vertex> _verticesLast;
	std::vector<int> _indices;
	UTime _lastUpdateTime = 0;
	int _canvasWidth = 0;
	int _canvasHeight = 0;

	void _update(UTime time, PointEx cameraPos);
};
