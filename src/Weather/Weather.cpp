#include "Weather.h"
#include "../Engine/Engine.h"
#include "../Engine/AudioDecodeSafety.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Data/MediaPathResolver.h"
#include <cmath>
#include <algorithm>

Weather::Weather()
{
	name = "Weather";
	setPriority(epWeather);
	snowflake = engine->createSnowflake();
	raindrop = engine->createRaindrop();
	lightningMask = engine->createMask(0xFF, 0xFF, 0xFF, 0xFF);
	fadeMask = engine->createMask(0, 0, 0, 0xFF);
	engine->setImageAlpha(fadeMask, 0);
	createLumMask();
}

Weather::~Weather()
{
	clearRainCustom();
	if (snowflake != nullptr)
	{
		snowflake = nullptr;
	}
	if (raindrop != nullptr)
	{
		raindrop = nullptr;
	}
	if (lightningMask != nullptr)
	{
		lightningMask = nullptr;
	}
	if (dayMask != nullptr)
	{
		dayMask = nullptr;
	}
	if (fadeMask != nullptr)
	{
		fadeMask = nullptr;
	}
	if (lumMask != nullptr)
	{
		lumMask = nullptr;
	}
	rainDrops.clear();
	snowDrops.clear();
}

void Weather::resetDay()
{	
	if (dayMask != nullptr)
	{
		dayMask = nullptr;
	}
	if (!isAmbientLumOverlayEnabled())
	{
		return;
	}
	if (dayType == dtDusk)
	{
		dayMask = engine->createMask(80, 70, 0, 255 - lum);
	}
	else if (dayType == dtDawn)
	{
		dayMask = engine->createMask(60, 0, 80, 255 - lum);
	}
	else if (dayType == dtNight)
	{
		dayMask = engine->createMask(0, 0, 30, 255 - lum);
	}
	else if (dayType == dtDay)
	{
		dayMask = engine->createMask(0, 0, 0, 255 - lum);
	}
	if (dayMask != nullptr)
	{
		engine->setImageAlpha(dayMask, 255 - lum);
	}
}

void Weather::createLumMask()
{
	if (lumMask != nullptr)
	{
		lumMask = nullptr;
	}
	lumMask = engine->createLumMask();
}

bool Weather::isAmbientLumOverlayEnabled() const
{
	return gm == nullptr || gm->global.feature.ambientLumOverlay;
}

static void getObjectLumColor(int objectLum, uint8_t& red, uint8_t& green, uint8_t& blue)
{
	switch (objectLum)
	{
	case olRed:
		red = 0xFF; green = 0x60; blue = 0x40;
		break;
	case olGreen:
		red = 0x40; green = 0xFF; blue = 0x60;
		break;
	case olBlue:
		red = 0x40; green = 0x60; blue = 0xFF;
		break;
	case olGray:
		red = 0xCC; green = 0xCC; blue = 0xCC;
		break;
	default:
		red = 0xFF; green = 0xFF; blue = 0xFF;
		break;
	}
}

void Weather::collectLightSources(std::vector<LightSource>& lights, Point cenTile, PointEx offset, Point cenScreen, int xscal, int yscal, int tileHeightScal)
{
	int mainLum = (int)gm->global.data.mainLum;

	const EffectMap& emap = gm->effectManager->createMap(cenTile.x - xscal, cenTile.y - yscal, xscal * 2, yscal * 2 + tileHeightScal);

	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (gm->map->data == nullptr || j < 0 || j >= gm->map->data->head.width || i < 0 || i >= gm->map->data->head.height)
			{
				continue;
			}

			for (auto iter = gm->map->dataMap.tile[i][j].objList.begin(); iter != gm->map->dataMap.tile[i][j].objList.end(); iter++)
			{
				int objectLum = (*iter)->lum;
				if (objectLum <= 0)
				{
					continue;
				}

				if (gm->global.feature.lumAsBrightness)
				{
					if (objectLum > mainLum)
					{
						float strength = std::clamp((float)(objectLum + 10 - mainLum) / 31.0f, 0.0f, 1.0f);
						LightSource light;
						light.position = { j, i };
						light.offset = (*iter)->offset;
						light.intensity = strength;
						lights.push_back(light);
					}
				}
				else
				{
					uint8_t red, green, blue;
					getObjectLumColor(objectLum, red, green, blue);
					int objectLumLevel = 20;
					if (objectLumLevel > mainLum)
					{
						float strength = std::clamp((float)(objectLumLevel - mainLum) / 31.0f, 0.0f, 1.0f);
						if (strength > 0.01f)
						{
							LightSource light;
							light.position = { j, i };
							light.offset = (*iter)->offset;
							light.red = red;
							light.green = green;
							light.blue = blue;
							light.intensity = strength;
							lights.push_back(light);
						}
					}
				}
			}

			int emapRow = i - (cenTile.y - yscal);
			int emapCol = j - (cenTile.x - xscal);
			auto& effectTileIndices = emap.tile[emapRow][emapCol].index;
			for (size_t k = 0; k < effectTileIndices.size(); k++)
			{
				auto effect = gm->effectManager->effectList[effectTileIndices[k]];
				if (effect != nullptr)
				{
					int effectLum = (int)effect->getLum();
					if (effectLum > mainLum)
					{
						float strength = std::clamp((float)(effectLum - mainLum) / 31.0f, 0.0f, 1.0f);
						if (strength > 0.01f)
						{
							LightSource light;
							light.position = { j, i };
							light.offset = effect->offset;
							light.red = 0xFF;
							light.green = 0xF0;
							light.blue = 0xD0;
							light.intensity = strength;
							lights.push_back(light);
						}
					}
				}
			}
		}
	}
}

void Weather::mergeLightSources(std::vector<LightSource>& lights)
{
	if (lights.empty())
	{
		return;
	}

	std::sort(lights.begin(), lights.end(), [](const LightSource& left, const LightSource& right)
	{
		return left.intensity > right.intensity;
	});
}

void Weather::drawLightingOverlay()
{
	if (gm == nullptr)
	{
		return;
	}
	if (!gm->global.feature.ambientLumOverlay)
	{
		return;
	}
	if (gm->global.data.mainLum >= 31)
	{
		return;
	}

	int windowWidth, windowHeight;
	engine->getWindowSize(windowWidth, windowHeight);
	Point cenScreen;
	cenScreen.x = windowWidth / 2;
	cenScreen.y = windowHeight / 2;
	int xscal = cenScreen.x / TILE_WIDTH + 2;
	int yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;
	Point cenTile = gm->camera->position;
	PointEx offset = gm->camera->offset;

	std::vector<LightSource> lights;
	collectLightSources(lights, cenTile, offset, cenScreen, xscal, yscal, tileHeightScal);
	mergeLightSources(lights);

	SDL_BlendMode oldBlendMode;
	SDL_GetTextureBlendMode(lumMask.get(), &oldBlendMode);
	SDL_SetTextureBlendMode(lumMask.get(), SDL_BLENDMODE_ADD);

	for (const auto& light : lights)
	{
		Point pos = gm->map->getTilePosition(light.position, cenTile, cenScreen, offset);
		int drawX = pos.x - LUM_MASK_WIDTH / 2 + (int)light.offset.x;
		int drawY = pos.y - LUM_MASK_HEIGHT / 2 - TILE_HEIGHT / 2 + (int)light.offset.y;

		uint8_t alpha = (uint8_t)std::clamp((int)(255 * powf(light.intensity, 1.5f)), 0, 255);
		if (alpha == 0)
		{
			continue;
		}

		engine->setImageColorMode(lumMask, light.red, light.green, light.blue);
		SDL_SetTextureAlphaMod(lumMask.get(), alpha);
		engine->drawImage(lumMask, drawX, drawY);
	}

	engine->setImageColorMode(lumMask, 0xFF, 0xFF, 0xFF);
	SDL_SetTextureBlendMode(lumMask.get(), oldBlendMode);
}

void Weather::drawElementLum()
{
	drawLightingOverlay();
}

void Weather::fadeInEx()
{
	if (gm != nullptr)
	{
		nowLum = gm->global.data.fadeLum;
	}
	else
	{
		nowLum = 0;
	}

	if (nowLum == 255)
	{
		return;
	}
	fadding = true;
	isFadeIn = true;
	isSleeping = false;
	fadeBeginTime = getTime();
}

void Weather::fadeIn()
{
	if (nowLum == 255)
	{
		return;
	}
	fadding = true;
	isFadeIn = true;
	isSleeping = false;
	fadeBeginTime = getTime();
	run();
}

void Weather::fadeOut()
{

	if (nowLum <= fadeLum)
	{
		return;
	}
	fadding = true;
	isFadeIn = false;
	isSleeping = false;
	fadeBeginTime = getTime();
	run();
}

void Weather::sleep(unsigned int t)
{
	fadding = true;
	isFadeIn = false;
	isSleeping = true;
	fadeBeginTime = getTime();
	sleepLastTime = t;
	run();
}

void Weather::setFadeLum(unsigned char l)
{
	if (l >= 31)
	{
		fadeLum = 255;
	}
	else
	{
		fadeLum = l * 8;
	}
}

void Weather::setLum(unsigned char l)
{
	if (l >= 31)
	{
		lum = 255;
	}
	else
	{
		lum = (l + 1) * 7 + 32;
	}
	if (dayMask == nullptr)
	{
		resetDay();
	}
	if (dayMask != nullptr)
	{
		engine->setImageAlpha(dayMask, 255 - lum);
	}
}

void Weather::setTime(int time)
{
	dayType = static_cast<DayType>(time);
	resetDay();
}

int Weather::getDropNum(WeatherType weatherType) const
{
	if (weatherType == wtCustomRain)
	{
		return customRainDropNum;
	}
	else if (weatherType == wtSnow)
	{
		return snowDropNum;
	}
	else if (weatherType == wtLightRain)
	{
		return lrainDropNum;
	}
	else if (weatherType == wtRain)
	{
		return rainDropNum;
	}
	else if (weatherType == wtLightning)
	{
		return lnDropNum;
	}
	else if (weatherType == wtHeavyRain)
	{
		return hrainDropNum;
	}
	
	return 0;
}

void Weather::resetDrops(
	std::list<WeatherDrop>& weatherDrops,
	WeatherType weatherType)
{
	weatherDrops.clear();
	const int dropNum = getDropNum(weatherType);
	for (int index = 0; index < dropNum; ++index)
	{
		WeatherDrop drop;
		resetDrop(&drop, weatherType, true);
		weatherDrops.push_back(drop);
	}
}

void Weather::resetDrop(
	WeatherDrop* drop,
	WeatherType weatherType,
	bool newdrop)
{
	if (drop == nullptr)
	{
		return;
	}
	int w, h;
	engine->getWindowSize(w, h);
	const bool initializeDrop = drop->type != weatherType || newdrop;
	if (initializeDrop)
	{
		drop->type = weatherType;
		const WeatherDepthLayer depthLayer =
			WeatherParticleMotion::selectDepthLayer(engine->getRand(99));
		const bool isSnow = weatherType == wtSnow;
		const WeatherLayerStyle style = isSnow
			? WeatherParticleMotion::getSnowLayerStyle(depthLayer)
			: WeatherParticleMotion::getRainLayerStyle(depthLayer);
		drop->cameraParallax = style.cameraParallax;
		drop->visualWidth = style.visualWidth;
		drop->visualLength = style.minimumVisualLength;
		if (style.maximumVisualLength > style.minimumVisualLength)
		{
			drop->visualLength += engine->getRand(
				style.maximumVisualLength - style.minimumVisualLength);
		}

		if (isSnow)
		{
			const int baseAlpha = 140 + engine->getRand(80);
			drop->dropAlpha = std::clamp(
				static_cast<int>(std::round(baseAlpha * style.alphaScale)),
				0,
				255);
			drop->speed = (0.18f + static_cast<float>(engine->getRand(40)) / 1000.0f)
				* style.speedScale;
			drop->horizontalSpeed =
				(-0.018f + static_cast<float>(engine->getRand(16) - 8) / 1000.0f)
				* (0.80f + drop->cameraParallax * 0.50f);
			drop->swayPhase = static_cast<float>(engine->getRand(6283)) / 1000.0f;
			drop->swayAngularSpeed =
				0.0008f + static_cast<float>(engine->getRand(8)) / 10000.0f;
			drop->swayAmplitude =
				static_cast<float>(4 + engine->getRand(8))
				* (0.65f + drop->cameraParallax);
			drop->visualAngle = 0.0f;
		}
		else
		{
			const int alphaRange = 160;
			const int baseAlpha = engine->getRand(alphaRange) + 90;
			drop->dropAlpha = std::clamp(
				static_cast<int>(std::round(baseAlpha * style.alphaScale)),
				0,
				255);
			float baseSpeed = 0.8f
				+ 0.005f * static_cast<float>(baseAlpha - 90 - alphaRange / 2);
			if (weatherType == wtCustomRain)
			{
				baseSpeed = convert_max(
					0.04f * customRainSpeed
						+ 0.005f * static_cast<float>(baseAlpha - 90 - alphaRange / 2),
					0.3f);
			}
			drop->speed = baseSpeed * style.speedScale;
			drop->horizontalSpeed =
				(-0.080f + static_cast<float>(engine->getRand(30) - 15) / 1000.0f)
				* (0.80f + drop->cameraParallax * 0.50f);
			drop->swayPhase = 0.0f;
			drop->swayAngularSpeed = 0.0f;
			drop->swayAmplitude = 0.0f;
			drop->visualAngle = WeatherParticleMotion::calculateRainStreakAngle(
				drop->horizontalSpeed,
				drop->speed);
		}
	}

	if (weatherType == wtSnow)
	{
		drop->x = (float)(engine->getRand(w + dropWRange / 4) - dropWRange / 4);
	}
	else
	{
		drop->x = static_cast<float>(
			engine->getRand(w + DROP_OFF_SCREEN_RANGE * 2)
			- DROP_OFF_SCREEN_RANGE);
	}
	if (newdrop)
	{
		drop->y = (float)(- dropRange - (engine->getRand(h + dropRange * 2)));
	}
	else
	{
		drop->y = (float)(- dropRange - (engine->getRand(dropRange * 3)));
	}
}

void Weather::updateFade()
{
	if (fadding)
	{
		auto t = getTime();
		if (isSleeping && t - fadeBeginTime >= sleepLastTime)
		{
			fadding = false;
			logicRunning = false;
		}
		else if (!isSleeping && t - fadeBeginTime >= fadeLastTime)
		{
			fadding = false;
			logicRunning = false;
			if (isFadeIn)
			{
				nowLum = 255;
				engine->setImageAlpha(fadeMask, 0);
			}
			else
			{
				nowLum = fadeLum;
				engine->setImageAlpha(fadeMask, 255 - fadeLum);
			}
		}
		else if (!isSleeping)
		{
			if (isFadeIn)
			{
				nowLum = (unsigned char)(((float)(t - fadeBeginTime) / fadeLastTime) * (255 - fadeLum) + fadeLum);
			}
			else
			{
				nowLum = (unsigned char)(((float)(fadeLastTime - t + fadeBeginTime) / fadeLastTime) * (255 - fadeLum) + fadeLum);
			}
			engine->setImageAlpha(fadeMask, 255 - nowLum);
		}
	}
}

void Weather::setRainCustomFromIni(std::shared_ptr<INIReader> ini)
{
	if (ini == nullptr)
	{
		return;
	}
	customRainDropNum = WeatherSafety::normalizeRainDropCount(
		ini->GetInteger("Init", "Number", customRainDropNum));
	customRainSpeed = WeatherSafety::normalizeRainSpeed(
		ini->GetInteger("Init", "Speed", customRainSpeed));
	customRainBoltProb = WeatherSafety::normalizeBoltProbability(
		ini->GetInteger("Init", "BoltProb", customRainBoltProb));
	customRainSoundName = ini->Get("RainSound", "1", "");
	if (!customRainSoundName.empty())
	{
		customRainSoundName = resolveSoundAssetPath(customRainSoundName);
		std::unique_ptr<char[]> soundData;
		int soundLength = 0;
		if (File::readFile(customRainSoundName, soundData, soundLength,
			static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes))
			&& soundData != nullptr && soundLength > 0)
		{
			customRainSound = engine->loadCircleSound(soundData, soundLength);
			if (customRainSound != nullptr)
			{
				customRainSoundChannel = engine->playSound(customRainSound);
			}
		}
	}
	customRainBoltSoundName.resize(3);
	for (size_t i = 0; i < customRainBoltSoundName.size(); i++)
	{
		customRainBoltSoundName[i] = ini->Get("BoltSound", std::to_string(i + 1), "");
		if (!customRainBoltSoundName[i].empty())
		{
			customRainBoltSoundName[i] = resolveSoundAssetPath(customRainBoltSoundName[i]);
		}
	}
}

void Weather::clearRainCustom()
{
	customRainDropNum = maxDropNum / 2;
	customRainSpeed = 100;
	customRainBoltProb = 10000;

	customRainSoundName = "";
	customRainBoltSoundName.clear();

	if (customRainSoundChannel != nullptr)
	{
		engine->stopMusic(customRainSoundChannel);
		customRainSoundChannel = nullptr;
	}

	if (customRainSound != nullptr)
	{
		engine->freeMusic(customRainSound);
		customRainSound = nullptr;
	}
}

void Weather::draw()
{
	if (gm != nullptr)
	{
		drawWeather();
	}
	if (nowLum != 255)
	{
		engine->drawMask(fadeMask);
	}
}

void Weather::setWeather(
	WeatherType weatherType,
	const std::string& configFileName)
{
	if (weatherType == wtSnow)
	{
		setRainWeather(wtNone);
		setSnowVisible(true);
		return;
	}
	if (weatherType == wtNone)
	{
		setSnowVisible(false);
		setRainWeather(wtNone);
		return;
	}
	setSnowVisible(false);
	setRainWeather(weatherType, configFileName);
}

void Weather::setRainWeather(
	WeatherType weatherType,
	const std::string& configFileName)
{
	if (weatherType == wtSnow)
	{
		return;
	}
	if (rainWeatherType == weatherType && weatherType != wtCustomRain)
	{
		return;
	}
	if (rainWeatherType == wtCustomRain || weatherType == wtCustomRain)
	{
		clearRainCustom();
	}
	rainWeatherType = weatherType;
	lightningBegin = false;
	if (rainWeatherType == wtCustomRain)
	{
		std::unique_ptr<char[]> s;
		int len = 0;
		if (!File::readFile(INI_MAP_FOLDER + configFileName, s, len,
			WeatherSafety::MaximumConfigurationBytes) || s == nullptr || len == 0)
		{
			GameLog::write("no weather ini file: %s\n", (INI_MAP_FOLDER + configFileName).c_str());
		}
		else
		{
			std::shared_ptr<INIReader> ini = std::make_shared<INIReader>(s);
			if (ini->ParseError() == 0)
			{
				setRainCustomFromIni(ini);
			}
			else
			{
				GameLog::write("invalid weather ini file: %s\n", (INI_MAP_FOLDER + configFileName).c_str());
			}
		}
	}

	if (rainWeatherType == wtLightning)
	{
		lightningInterval = lightningIntervalMin + engine->getRand(lightningIntervalMin * 2);
		lastLightninUTime = getTime();
	}
	resetDrops(rainDrops, rainWeatherType);
}

void Weather::setSnowVisible(bool visible)
{
	if (snowVisible == visible)
	{
		return;
	}
	snowVisible = visible;
	resetDrops(snowDrops, snowVisible ? wtSnow : wtNone);
}

void Weather::setDay(DayType dType)
{
	if (dayType == dType)
	{
		return;
	}
	dayType = dType;
	resetDay();
}

void Weather::drawDrops(const std::list<WeatherDrop>& weatherDrops)
{
	int viewportWidth = 0;
	int viewportHeight = 0;
	engine->getWindowSize(viewportWidth, viewportHeight);
	(void)viewportWidth;

	for (const WeatherDrop& drop : weatherDrops)
	{
		if (drop.type == wtSnow && snowflake != nullptr)
		{
			const int size = std::max(drop.visualLength, 1);
			Rect destination =
			{
				static_cast<int>(std::round(drop.x)) - size / 2,
				static_cast<int>(std::round(drop.y)) - size / 2,
				size,
				size
			};
			engine->setImageAlpha(
				snowflake,
				static_cast<unsigned char>(std::clamp(drop.dropAlpha, 0, 255)));
			engine->drawImageEx(snowflake, nullptr, &destination, 0.0f, nullptr);
		}
		else if (drop.type != wtNone && raindrop != nullptr)
		{
			const int visualWidth = std::max(drop.visualWidth, 1);
			const int visualLength = std::max(drop.visualLength, 1);
			const float topFade = std::clamp(
				(drop.y + static_cast<float>(visualLength)) / 60.0f,
				0.0f,
				1.0f);
			const int alpha = std::clamp(
				static_cast<int>(std::round(drop.dropAlpha * topFade)),
				0,
				255);
			if (alpha == 0 || drop.y >= viewportHeight + visualLength)
			{
				continue;
			}
			Rect destination =
			{
				static_cast<int>(std::round(drop.x)) - visualWidth / 2,
				static_cast<int>(std::round(drop.y)) - visualLength / 2,
				visualWidth,
				visualLength
			};
			engine->setImageAlpha(raindrop, static_cast<unsigned char>(alpha));
			engine->drawImageEx(
				raindrop,
				nullptr,
				&destination,
				drop.visualAngle,
				nullptr);
		}
	}

	if (raindrop != nullptr)
	{
		engine->setImageAlpha(raindrop, 255);
	}
	if (snowflake != nullptr)
	{
		engine->setImageAlpha(snowflake, 0xB0);
	}
}

void Weather::drawWeather()
{
	drawDrops(rainDrops);
	drawDrops(snowDrops);

	if (isAmbientLumOverlayEnabled() && dayMask == nullptr)
	{
		resetDay();
	}
	if (dayMask != nullptr)
	{
		engine->drawMask(dayMask);
	}

	if (lightningBegin)
	{
		engine->drawMask(lightningMask);
	}
}

void Weather::updateDrops(
	std::list<WeatherDrop>& weatherDrops,
	WeatherType weatherType,
	PointEx cameraDelta)
{
	const float frameTime = static_cast<float>(getFrameTime());
	const float velocityFrameTime = std::max(frameTime, 1.0f);
	const float cameraVelocityX = std::clamp(
		cameraDelta.x / velocityFrameTime,
		-0.35f,
		0.35f);
	const float cameraVelocityY = std::clamp(
		cameraDelta.y / velocityFrameTime,
		-0.35f,
		0.35f);
	const UTime windCycleMilliseconds = 25133;
	const float sharedWindPulse = std::sin(
		static_cast<float>(getTime() % windCycleMilliseconds) * 0.00025f) * 0.018f;
	int viewportWidth = 0;
	int viewportHeight = 0;
	engine->getWindowSize(viewportWidth, viewportHeight);
	const float horizontalWrapRange = static_cast<float>(
		viewportWidth + DROP_OFF_SCREEN_RANGE * 2);

	for (WeatherDrop& drop : weatherDrops)
	{
		const float effectiveHorizontalSpeed = drop.horizontalSpeed
			+ sharedWindPulse * (0.65f + drop.cameraParallax);
		if (weatherType == wtSnow)
		{
			const float phaseAdvance = drop.swayAngularSpeed * frameTime;
			const float swayDelta = WeatherParticleMotion::calculateSnowSwayDelta(
				drop.swayPhase,
				phaseAdvance,
				drop.swayAmplitude);
			drop.swayPhase = std::fmod(
				drop.swayPhase + phaseAdvance,
				6.28318531f);
			drop.x = WeatherParticleMotion::advanceParticleAxis(
				drop.x,
				effectiveHorizontalSpeed,
				frameTime,
				cameraDelta.x,
				drop.cameraParallax) + swayDelta;
		}
		else
		{
			drop.x = WeatherParticleMotion::advanceParticleAxis(
				drop.x,
				effectiveHorizontalSpeed,
				frameTime,
				cameraDelta.x,
				drop.cameraParallax);
			drop.visualAngle = WeatherParticleMotion::calculateRainStreakAngle(
				effectiveHorizontalSpeed - cameraVelocityX * drop.cameraParallax,
				std::max(
					drop.speed - cameraVelocityY * drop.cameraParallax,
					0.10f));
		}

		drop.y = WeatherParticleMotion::advanceParticleAxis(
			drop.y,
			drop.speed,
			frameTime,
			cameraDelta.y,
			drop.cameraParallax);
		if (horizontalWrapRange > 0.0f
			&& (drop.x < -DROP_OFF_SCREEN_RANGE
				|| drop.x > viewportWidth + DROP_OFF_SCREEN_RANGE))
		{
			float wrappedX = std::fmod(
				drop.x + static_cast<float>(DROP_OFF_SCREEN_RANGE),
				horizontalWrapRange);
			if (wrappedX < 0.0f)
			{
				wrappedX += horizontalWrapRange;
			}
			drop.x = wrappedX - static_cast<float>(DROP_OFF_SCREEN_RANGE);
		}
		if (drop.y > viewportHeight + dropRange)
		{
			resetDrop(&drop, weatherType, false);
		}
	}
}

void Weather::updateWeather()
{
	PointEx cameraDelta = { 0.0f, 0.0f };
	if (gm != nullptr && gm->camera != nullptr)
	{
		cameraDelta = gm->camera->differencePosition;
	}
	updateDrops(rainDrops, rainWeatherType, cameraDelta);
	updateDrops(snowDrops, wtSnow, cameraDelta);

	if (lightningBegin)
	{
		if (getTime() - lightningBeginTime > lightninUTime)
		{
			lightningBegin = false;
			lastLightninUTime = getTime();
			if (rainWeatherType == wtLightning)
			{
				lightningInterval = lightningIntervalMin + engine->getRand(lightningIntervalMin * 2);
			}
		}
		else
		{
			if (getTime() - lightningBeginTime > lightninUTime / 3)
			{
				engine->setImageAlpha(lightningMask, 160 - (getTime() - lightningBeginTime - lightninUTime / 3) * 140 / (lightninUTime * 2 / 3));
			}
			else
			{
				engine->setImageAlpha(lightningMask, 160 - (lightninUTime / 3 - getTime() + lightningBeginTime) * 140 / (lightninUTime / 3));
			}
		}
	}
	else
	{
		if (rainWeatherType == wtLightning)
		{
			if (getTime() - lastLightninUTime > lightningInterval)
			{
				lightningBegin = true;
			}
		}
		else if (rainWeatherType == wtCustomRain)
		{
			std::vector<std::string> boltSounds;
			for (const auto& soundName : customRainBoltSoundName)
			{
				if (!soundName.empty())
				{
					boltSounds.push_back(soundName);
				}
			}

			if (!boltSounds.empty()
				&& WeatherSafety::shouldTriggerBolt(customRainBoltProb,
					engine->getRand(customRainBoltProb - 1)))
			{
				int index = boltSounds.size() > 1 ? engine->getRand((int)boltSounds.size() - 1) : 0;
				if (!boltSounds[index].empty())
				{
					lightningBegin = true;

					std::unique_ptr<char[]> s;
					int len = 0;
					if (File::readFile(boltSounds[index], s, len,
						static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes)) &&
						s != nullptr && len > 0)
					{
						engine->playSound(s, len);
					}
				}
			}
		}
		
		if (lightningBegin)
		{
			lightningBeginTime = getTime();
			lastLightninUTime = lightningBeginTime;
			engine->setImageAlpha(lightningMask, 120);
		}
	}
}

void Weather::reset()
{
	clearRainCustom();
	rainWeatherType = wtNone;
	snowVisible = false;
	rainDrops.clear();
	snowDrops.clear();
	lightningBegin = false;
	nowLum = 255;
}

void Weather::onDraw()
{
	draw();
}

void Weather::onUpdate()
{
	if (gm != nullptr)
	{
		updateWeather();
	}
	updateFade();
}
