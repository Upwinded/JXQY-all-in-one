#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include "../../Engine/Engine.h"
#include <algorithm>
#include "WaterEffect.h"

namespace
{
constexpr float WaterDisplacementScale = 0.70f;
}


void WaterEffect::setupEffectCanvas()
{
	auto engine = Engine::getInstance();
	if (_effectRenderTargetActive)
	{
		(void)engine->
			restoreImageRenderTargetAfterAcceptedOperation(
				_tempRenderTarget,
				_waterEffectCanvas);
		_effectRenderTargetActive = false;
	}
	_tempRenderTarget = engine->getRenderTarget();
	int width = 0;
	int height = 0;
	engine->getWindowSize(width, height);
	if (_waterEffectCanvas == nullptr || width != _canvasWidth || height != _canvasHeight)
	{
		initGrid();
	}
	if (_waterEffectCanvas == nullptr)
	{
		return;
	}
	_effectRenderTargetActive =
		engine->setSharedImageAsRenderTarget(
			_waterEffectCanvas);
	if (!_effectRenderTargetActive)
	{
		return;
	}
	engine->renderClear();
}

void WaterEffect::renderEffect(UTime time, PointEx cameraPos)
{
	auto engine = Engine::getInstance();
	if (_effectRenderTargetActive)
	{
		(void)engine->
			restoreImageRenderTargetAfterAcceptedOperation(
				_tempRenderTarget,
				_waterEffectCanvas);
		_effectRenderTargetActive = false;
	}
	_update(time, cameraPos);
	engine->drawGeometry(_waterEffectCanvas, _vertices, _indices);
}

void WaterEffect::applyPresetParams()
{
	clearParams();

	setMaxClickRipple(5);
	WaterRippleParams fixedRippleParams;
	fixedRippleParams.amplitude = 25.0f;
	fixedRippleParams.density = 0.015f;
	fixedRippleParams.frequency = 3.0f;
	fixedRippleParams.pos = { -100.0f, 600.0f };
	addFixedRipple(fixedRippleParams);

	WaterWaveParams waveParams;
	waveParams.amplitude = 6.0f;
	waveParams.angle = 5 * M_PI / 6;
	waveParams.density = 0.02f;
	waveParams.frequency = 8.0f;
	waveParams.phi = 1.0f;
	addWave(waveParams);

	waveParams.amplitude = 10.0f;
	waveParams.angle = 7 * M_PI / 6;
	waveParams.density = 0.01f;
	waveParams.frequency = 3.0f;
	waveParams.phi = 0.0f;
	addWave(waveParams);

	WaterLightParams lightParams;
	lightParams.decay = 700.0f;
	lightParams.angle = 5 * M_PI / 4;
	lightParams.minimumBrightness = 0.94f;
	setLightParams(lightParams);

	WaterClickRippleParams waterClickRippleParams;
	waterClickRippleParams.lifeTime =
		AspectFitLayout::PointerRippleDurationMilliseconds;
	setDefaultClickRippleParams(waterClickRippleParams);

	setGridSize(60);
}

void WaterEffect::clearParams()
{
	WaterEffectParams defaultParams;
	_params = defaultParams;
	_lastUpdateTime = 0;
}

void WaterEffect::setGridSize(int gridSize)
{
	if (!WaterEffectSafety::isValidGridSize(gridSize))
	{
		return;
	}
	_params.gridSize = gridSize;
	initGrid();
}

void WaterEffect::addWave(WaterWaveParams params)
{
	WaterWaveCalculatedParams cParams;
	cParams.basicParams = params;
	cParams.A = std::cos(params.angle);
	cParams.B = -std::sin(params.angle);

	_params.waves.push_back(cParams);
}

void WaterEffect::addFixedRipple(WaterRippleParams params)
{
	_params.fixedRipples.push_back(params);
}

void WaterEffect::setDefaultClickRippleParams(WaterClickRippleParams params)
{
	_params.defaultClickRipple = params;
}

void WaterEffect::addClickRipple(WaterClickRippleParams params)
{
	if (_params.clickRipples.size() >= _params.maxClickRipple)
	{
		return;
	}
	_params.clickRipples.push_back(params);
}

void WaterEffect::addDefaultClickRipple(float x, float y, UTime startTime)
{
	WaterClickRippleParams params;
	params = _params.defaultClickRipple;
	params.startTime = startTime;
	params.pos = { x, y };
	addClickRipple(params);
}

void WaterEffect::setLightParams(WaterLightParams params)
{
	params.minimumBrightness = std::clamp(
		params.minimumBrightness,
		0.0f,
		1.0f);
	params.decay = (std::max)(params.decay, 1.0f);
	_params.light = params;
}

void WaterEffect::setMaxClickRipple(int count)
{
	_params.maxClickRipple = std::clamp(count, 0, WaterEffectSafety::MaximumClickRippleCount);
	if (_params.clickRipples.size() > static_cast<size_t>(_params.maxClickRipple))
	{
		_params.clickRipples.resize(static_cast<size_t>(_params.maxClickRipple));
	}
}

void WaterEffect::_update(UTime time, PointEx cameraPos)
{
	if (_canvasWidth <= 0 || _canvasHeight <= 0 ||
		_vertices.size() != _verticesOrigin.size() ||
		_vertices.size() != _verticesLast.size())
	{
		return;
	}

	const double timeSeconds = static_cast<double>(time) / 1000.0;
	const UTime safeLastUpdateTime = (std::min)(time, _lastUpdateTime);
	const double lastTimeSeconds =
		static_cast<double>(safeLastUpdateTime) / 1000.0;
	const UTime updateIntervalMilliseconds = time - safeLastUpdateTime;
	const float updateIntervalSeconds =
		static_cast<float>(updateIntervalMilliseconds) / 1000.0f;

	_params.clickRipples.erase(
		std::remove_if(
			_params.clickRipples.begin(),
			_params.clickRipples.end(),
			[time](const WaterClickRippleParams& ripple)
			{
				return !WaterEffectSafety::isClickRippleActive(
					time,
					ripple.startTime,
					ripple.lifeTime);
			}),
		_params.clickRipples.end());

	std::vector<AspectFitPointerRipple> pointerRipples;
	pointerRipples.reserve(_params.clickRipples.size());
	for (const WaterClickRippleParams& ripple : _params.clickRipples)
	{
		AspectFitPointerRipple pointerRipple;
		pointerRipple.normalizedX =
			(ripple.pos.x - cameraPos.x) / _canvasWidth;
		pointerRipple.normalizedY =
			(ripple.pos.y - cameraPos.y) / _canvasHeight;
		pointerRipple.startTimeMilliseconds = ripple.startTime;
		pointerRipple.durationMilliseconds = ripple.lifeTime;
		pointerRipples.push_back(pointerRipple);
	}

	for (std::size_t vertexIndex = 0;
		vertexIndex < _vertices.size();
		++vertexIndex)
	{
		float offsetX = 0.0f;
		float offsetY = 0.0f;
		float motionX = 0.0f;
		float motionY = 0.0f;
		const float vertexX =
			_verticesOrigin[vertexIndex].position.x + cameraPos.x;
		const float vertexY =
			_verticesOrigin[vertexIndex].position.y + cameraPos.y;

		for (const WaterWaveCalculatedParams& wave : _params.waves)
		{
			const float distanceToLine =
				wave.A * vertexX + wave.B * vertexY +
				wave.basicParams.phi;
			const float currentOffset = wave.basicParams.amplitude *
				std::sin(wave.basicParams.frequency * timeSeconds -
					wave.basicParams.density * distanceToLine);
			const float previousOffset = wave.basicParams.amplitude *
				std::sin(wave.basicParams.frequency * lastTimeSeconds -
					wave.basicParams.density * distanceToLine);
			offsetX += currentOffset * wave.A;
			offsetY += currentOffset * wave.B;
			motionX += (currentOffset - previousOffset) * wave.A;
			motionY += (currentOffset - previousOffset) * wave.B;
		}

		for (const WaterRippleParams& ripple : _params.fixedRipples)
		{
			const float deltaX = vertexX - ripple.pos.x;
			const float deltaY = vertexY - ripple.pos.y;
			const float distance = std::sqrt(
				deltaX * deltaX + deltaY * deltaY);
			const float angle = std::atan2(-deltaY, deltaX);
			const float directionX = std::cos(angle);
			const float directionY = -std::sin(angle);
			const float currentOffset = ripple.amplitude * std::cos(
				ripple.frequency * timeSeconds -
				ripple.density * distance);
			const float previousOffset = ripple.amplitude * std::cos(
				ripple.frequency * lastTimeSeconds -
				ripple.density * distance);
			offsetX += currentOffset * directionX;
			offsetY += currentOffset * directionY;
			motionX += (currentOffset - previousOffset) * directionX;
			motionY += (currentOffset - previousOffset) * directionY;
		}

		const float normalizedX =
			_verticesOrigin[vertexIndex].position.x / _canvasWidth;
		const float normalizedY =
			_verticesOrigin[vertexIndex].position.y / _canvasHeight;
		const AspectFitPointerRippleSample pointerSample =
			AspectFitLayout::calculateCombinedPointerRippleSample(
				normalizedX,
				normalizedY,
				_canvasWidth,
				_canvasHeight,
				_canvasHeight,
				time,
				pointerRipples);
		offsetX += pointerSample.offset.x;
		offsetY += pointerSample.offset.y;

		_vertices[vertexIndex].position =
			_verticesOrigin[vertexIndex].position;
		_vertices[vertexIndex].tex_coord.x = std::clamp(
			_verticesOrigin[vertexIndex].tex_coord.x -
				offsetX * WaterDisplacementScale / _canvasWidth,
			0.0f,
			1.0f);
		_vertices[vertexIndex].tex_coord.y = std::clamp(
			_verticesOrigin[vertexIndex].tex_coord.y -
				offsetY * WaterDisplacementScale / _canvasHeight,
			0.0f,
			1.0f);

		float ambientBrightness = 1.0f;
		const float motionDistance = std::sqrt(
			motionX * motionX + motionY * motionY);
		if (updateIntervalMilliseconds > 0 &&
			motionDistance > _params.light.minDistance)
		{
			const float motionDirection = std::atan2(-motionY, motionX);
			const float directionalVelocity = motionDistance *
				std::cos(motionDirection - _params.light.angle) /
				updateIntervalSeconds;
			ambientBrightness = std::clamp(
				1.0f + (std::min)(
					0.0f,
					directionalVelocity / _params.light.decay),
				_params.light.minimumBrightness,
				1.0f);
		}
		const float targetBrightness = (std::min)(
			ambientBrightness,
			pointerSample.brightness);
		const float previousBrightness =
			_verticesLast[vertexIndex].color.r;
		const float maximumBrightnessChange = updateIntervalSeconds;
		const float brightness = std::clamp(
			targetBrightness,
			previousBrightness - maximumBrightnessChange,
			previousBrightness + maximumBrightnessChange);
		const float minimumAllowedBrightness = (std::min)(
			_params.light.minimumBrightness,
			AspectFitLayout::CombinedPointerRippleMinimumBrightness);
		const float clampedBrightness = std::clamp(
			brightness,
			minimumAllowedBrightness,
			1.0f);
		_vertices[vertexIndex].color = {
			clampedBrightness,
			clampedBrightness,
			clampedBrightness,
			1.0f
		};
		_verticesLast[vertexIndex] = _vertices[vertexIndex];
	}
	_lastUpdateTime = time;
}

void WaterEffect::initGrid()
{
	auto engine = Engine::getInstance();
	_verticesOrigin.clear();
	_vertices.clear();
	_verticesLast.clear();
	_indices.clear();
	int gridSize = _params.gridSize;
	int width, height;
	engine->getWindowSize(width, height);
	if (!WaterEffectSafety::isValidGridSize(gridSize) || width <= 0 || height <= 0)
	{
		_waterEffectCanvas = nullptr;
		_canvasWidth = 0;
		_canvasHeight = 0;
		return;
	}
	_waterEffectCanvas = engine->createCanvasImage(width, height);
	_canvasWidth = width;
	_canvasHeight = height;
	float cellW = static_cast<float>(width) / gridSize;
	float cellH = static_cast<float>(height) / gridSize;
	float rowY = 0.0f;
	float coordY = 0.0f;
	for (int y = 0; y <= gridSize; ++y)
	{
		if (y == gridSize)
		{
			rowY = static_cast<float>(height);
			coordY = 1.0f;
		}
		float columnX = 0.0f;
		float coordX = 0.0f;
		for (int x = 0; x <= gridSize; ++x) {
			if (x == gridSize)
			{
				columnX = static_cast<float>(width);
				coordX = 1.0f;
			}
			SDL_Vertex v;
			v.position = { columnX, rowY };
			v.tex_coord = { coordX, coordY };
			v.color = { 1.0f, 1.0f, 1.0f, 1.0f };
			_vertices.push_back(v);
			_verticesOrigin.push_back(v);
			_verticesLast.push_back(v);
			columnX += cellW;
			coordX += 1.0f / gridSize;
		}
		rowY += cellH;
		coordY += 1.0f / gridSize;
	}

	for (int y = 0; y < gridSize; ++y) {
		for (int x = 0; x < gridSize; ++x) {
			int topLeft = y * (gridSize + 1) + x;
			int topRight = topLeft + 1;
			int bottomLeft = (y + 1) * (gridSize + 1) + x;
			int bottomRight = bottomLeft + 1;

			_indices.push_back(topLeft);
			_indices.push_back(topRight);
			_indices.push_back(bottomLeft);

			_indices.push_back(topRight);
			_indices.push_back(bottomRight);
			_indices.push_back(bottomLeft);
		}
	}
}
