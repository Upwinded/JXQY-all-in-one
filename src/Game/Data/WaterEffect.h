#pragma once
#include <list>
#include <vector>
#include "../../Engine/Engine.h"


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
	WaterRippleParams rippleParams;
	float startTime = 0.0f;
	float lifeTime = 0.0f;
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
	float defaultAlpha = 1.0f;
	float minAlpha = 0.0f;
	float decay = 100.0f;
	float angle = 0.0f;
};

struct WaterEffectParams
{
	int gridSize = 30;
	std::list<WaterWaveCalculatedParams> waves;
	std::list<WaterRippleParams> fixedRipples;
	int maxClickRipple = 5;
	std::list<WaterClickRippleParams> clickRipples;
	WaterClickRippleParams defaultClickRipple;
	WaterLightParams light;
};

class WaterEffect
{
public:
	void setupEffectCanvas();
	void renderEffect(UTime time);

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
	std::vector<Vertex> _vertices;
	std::vector<Vertex> _verticesOrigin;
	std::vector<Vertex> _verticesLast;
	std::vector<int> _indices;

	void _update(float time);
};

