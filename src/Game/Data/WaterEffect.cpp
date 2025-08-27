#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include <algorithm>
#include <cassert>
#include "WaterEffect.h"


void WaterEffect::setupEffectCanvas()
{
	auto engine = Engine::getInstance();
	_tempRenderTarget = engine->getRenderTarget();
	if (_waterEffectCanvas != nullptr)
	{
		engine->setSharedImageAsRenderTarget(_waterEffectCanvas);
	}
	engine->renderClear();
}

void WaterEffect::renderEffect(UTime time)
{
	auto engine = Engine::getInstance();
	engine->setImageAsRenderTarget(_tempRenderTarget);
	_update(static_cast<float>(time) / 1000.0f);
	engine->drawGeometry(_waterEffectCanvas, _vertices, _indices);
}

void WaterEffect::applyPresetParams()
{
	clearParams();

	setMaxClickRipple(5);
	WaterRippleParams fixedRippleParams;
	fixedRippleParams.amplitude = 15.0f;
	fixedRippleParams.density = 0.015f;
	fixedRippleParams.frequency = 5.0f;
	fixedRippleParams.pos = { -100.0f, 600.0f };
	addFixedRipple(fixedRippleParams);

	WaterWaveParams waveParams;
	waveParams.amplitude = 5.0f;
	waveParams.angle = 5 * M_PI / 6;
	waveParams.density = 0.02f;
	waveParams.frequency = 8.0f;
	waveParams.phi = 1.0f;
	addWave(waveParams);

	waveParams.amplitude = 8.0f;
	waveParams.angle = 7 * M_PI / 6;
	waveParams.density = 0.01f;
	waveParams.frequency = 3.0f;
	waveParams.phi = 0.0f;
	addWave(waveParams);

	WaterLightParams lightParams;
	lightParams.decay = 10.0f;
	lightParams.defaultAlpha = 0.9f;
	lightParams.angle = 5 * M_PI / 4;
	lightParams.minAlpha = 0.85f;
	lightParams.minDistance = 0.0f;
	setLightParams(lightParams);

	WaterClickRippleParams waterClickRippleParams;
	waterClickRippleParams.lifeTime = 5.0f;
	waterClickRippleParams.rippleParams.amplitude = 30.0f;
	waterClickRippleParams.rippleParams.density = 0.02f;
	waterClickRippleParams.rippleParams.frequency = 10.0f;
	setDefaultClickRippleParams(waterClickRippleParams);

	setGridSize(30);
}

void WaterEffect::clearParams()
{
	WaterEffectParams defaultParams;
	_params = defaultParams;
}

void WaterEffect::setGridSize(int gridSize)
{
	assert(gridSize > 0);
	if (gridSize <= 0)
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
	params.startTime = static_cast<float>(startTime) / 1000.0f;
	params.rippleParams.pos = { x, y };
	addClickRipple(params);
}

void WaterEffect::setLightParams(WaterLightParams params)
{
	_params.light = params;
}

void WaterEffect::setMaxClickRipple(int count)
{
	_params.maxClickRipple = count;
}

void WaterEffect::_update(float time)
{
	for (size_t i = 0; i < _vertices.size(); i++)
	{
		float offset_x = 0.0f, offset_y = 0.0f;

		for (auto& iter : _params.waves)
		{
			float x = _verticesOrigin[i].position.x;
			float y = _verticesOrigin[i].position.y;
			float distance_to_line = (iter.A * x + iter.B * y + iter.basicParams.phi);

			float line_offset = iter.basicParams.amplitude * std::sin(iter.basicParams.frequency * time - iter.basicParams.density * distance_to_line);
			offset_x += line_offset * iter.A;
			offset_y += line_offset * iter.B;
		}
		for (auto& iter : _params.fixedRipples)
		{
			float nx = iter.pos.x, ny = iter.pos.y;
			float dx = _verticesOrigin[i].position.x - nx;
			float dy = _verticesOrigin[i].position.y - ny;
			float angle = atan2(-dy, dx);
			float distance = std::sqrt(dx * dx + dy * dy);
			offset_x += iter.amplitude * std::cos(iter.frequency * time - iter.density * distance) * cos(angle);
			offset_y += iter.amplitude * std::cos(iter.frequency * time - iter.density * distance) * -sin(angle);
		}

		auto clickRippleParamsIter = _params.clickRipples.begin();
		while (clickRippleParamsIter != _params.clickRipples.end())
		{
			auto iter = clickRippleParamsIter++;
			if (time - iter->startTime > iter->lifeTime)
			{
				clickRippleParamsIter = _params.clickRipples.erase(iter);
				continue;
			}
			float dx = _verticesOrigin[i].position.x - iter->rippleParams.pos.x;
			float dy = _verticesOrigin[i].position.y - iter->rippleParams.pos.y;
			float angle = atan2(dy, dx);
			float distance = std::sqrt(dx * dx + dy * dy);
			float distance_to_ripple = abs((time - iter->startTime) * iter->rippleParams.frequency - distance * iter->rippleParams.density);

			offset_x += iter->rippleParams.amplitude * std::cos(distance_to_ripple) * cos(angle) * exp(-distance_to_ripple) / (time - iter->startTime + 1.0f);
			offset_y += iter->rippleParams.amplitude * std::cos(distance_to_ripple) * sin(angle) * exp(-distance_to_ripple) / (time - iter->startTime + 1.0f);
		}

		_vertices[i].position.y = _verticesOrigin[i].position.y + offset_y;
		_vertices[i].position.x = _verticesOrigin[i].position.x + offset_x;

		int gridSize = _params.gridSize;
		if (i % (gridSize + 1) == 0)
		{
			if (_vertices[i].position.x > 0.0f)
			{
				_vertices[i].position.x = 0.0f;
			}

		}
		else if (i % (gridSize + 1) == gridSize)
		{
			if (_vertices[i].position.x < _verticesOrigin[i].position.x)
			{
				_vertices[i].position.x = _verticesOrigin[i].position.x;
			}
		}

		if (i <= (gridSize + 1))
		{
			if (_vertices[i].position.y > 0.0f)
			{
				_vertices[i].position.y = 0;
			}
		}
		else if (i >= (gridSize + 1) * gridSize)
		{
			if (_vertices[i].position.y < _verticesOrigin[i].position.y)
			{
				_vertices[i].position.y = _verticesOrigin[i].position.y;
			}
		}

		float dx = _vertices[i].position.x - _verticesLast[i].position.x;
		float dy = _vertices[i].position.y - _verticesLast[i].position.y;

		float distance = std::sqrt(dx * dx + dy * dy);

		if (distance > _params.light.minDistance)
		{
			_vertices[i].color.a = (distance - _params.light.minDistance) * cos(atan2(-dy, dx) - _params.light.angle) / _params.light.decay + _params.light.defaultAlpha;
		}
		else
		{
			_vertices[i].color.a = _params.light.defaultAlpha;
		}

		_vertices[i].color.a = std::clamp(_vertices[i].color.a, _verticesLast[i].color.a - 1.0f * (time - LastUpdateTime), _verticesLast[i].color.a + 1.0f * (time - LastUpdateTime));
		_vertices[i].color.a = std::clamp(_vertices[i].color.a, _params.light.minAlpha, 1.0f);

		_verticesLast[i] = _vertices[i];
	}
	LastUpdateTime = time;
}

void WaterEffect::initGrid()
{
	auto engine = Engine::getInstance();
	_verticesOrigin.clear();
	_vertices.clear();
	_indices.clear();
	int gridSize = _params.gridSize;
	int width, height;
	engine->getWindowSize(width, height);
	_waterEffectCanvas = engine->createCanvasImage(width, height);
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
			v.color = { 1.0f, 1.0f, 1.0f, _params.light.defaultAlpha };
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

