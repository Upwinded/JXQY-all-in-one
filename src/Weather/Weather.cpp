#include "Weather.h"
#include "../Game/GameManager/GameManager.h"

Weather::Weather()
{
	priority = epWeather;
	drops.resize(maxDropNum);
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
		//engine->freeImage(snowflake);
		snowflake = nullptr;
	}
	if (raindrop != nullptr)
	{
		//engine->freeImage(raindrop);
		raindrop = nullptr;
	}
	if (lightningMask != nullptr)
	{
		//engine->freeImage(lightningMask);
		lightningMask = nullptr;
	}
	if (dayMask != nullptr)
	{
		//engine->freeImage(dayMask);
		dayMask = nullptr;
	}
	if (fadeMask != nullptr)
	{
		//engine->freeImage(fadeMask);
		fadeMask = nullptr;
	}
	if (lumMask != nullptr)
	{
		//engine->freeImage(lumMask);
		lumMask = nullptr;
	}
	drops.resize(0);
}

void Weather::resetDay()
{	
	if (dayMask != nullptr)
	{
		//engine->freeImage(dayMask);
		dayMask = nullptr;
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
	engine->setImageAlpha(dayMask, 255 - lum);
}

void Weather::createLumMask()
{
	if (lumMask != nullptr)
	{
		//engine->freeImage(lumMask);
		lumMask = nullptr;
	}
	lumMask = engine->createLumMask();
}

void Weather::drawElementLum()
{
	if (gm == nullptr)
	{
		return;
	}
	if (gm->global.data.mainLum >= 31)
	{
		return;
	}
	int w, h;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 2;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;
	Point cenTile = gm->camera->position;
	PointEx offset = gm->camera->offset;

	EffectMap emap = gm->effectManager->createMap(cenTile.x - xscal, cenTile.y - yscal, xscal * 2, yscal * 2 + tileHeightScal);
	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{		
			if (gm->map->data == nullptr || j < 0 || j >= gm->map->data->head.width || i < 0 || i >= gm->map->data->head.height)
			{
				continue;
			}
			bool drawLumMask = false;
			PointEx eOffset = { 0, 0 };
			for (auto iter = gm->map->dataMap.tile[i][j].objList.begin(); iter != gm->map->dataMap.tile[i][j].objList.end(); iter++)
			{
				if ((*iter)->lum > (int)gm->global.data.mainLum)
				{
					drawLumMask = true;
					eOffset = (*iter)->offset;
					break;
				}
			}	
			if (!drawLumMask)
			{
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->getLum() >(int)gm->global.data.mainLum)
						{
							drawLumMask = true;
							eOffset = gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->offset;
							break;
						}
					}
				}
			}		
			if (drawLumMask)
			{
				Point pos = gm->map->getTilePosition({ j, i }, cenTile, cenScreen, offset);
				engine->drawImage(lumMask, pos.x - LUM_MASK_WIDTH / 2 + (int)eOffset.x, pos.y - LUM_MASK_HEIGHT / 2 - TILE_HEIGHT / 2 + (int)eOffset.y);
			}		
		}
	}
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
	engine->setImageAlpha(dayMask, 255 - lum);
}

void Weather::setTime(unsigned char t)
{
	dayType = (DayType)t;
	resetDay();
}

int Weather::getDropNum()
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

void Weather::resetDrops()
{
	int dropNum = getDropNum();
	int i = 0;
	for (auto iter = drops.begin(); iter != drops.end(); iter++)
	{
		i++;
		if (iter->type == wtNone && i < dropNum)
		{
			resetDrop(&(*iter), true);
		}		
	}
}

void Weather::resetDrop(WeatherDrop * drop, bool newdrop)
{
	if (drop == nullptr)
	{
		return;
	}
	int w, h;
	engine->getWindowSize(w, h);
	if (drop->type == wtSnow)
	{
		drop->x = (float)(engine->getRand(w + dropWRange / 4) - dropWRange / 4);
	}
	else
	{
		drop->x = (float)(engine->getRand(w));
	}
	
	if (drop->type != weatherType)
	{
		drop->type = weatherType;
		if (drop->type == wtRain || drop->type == wtLightRain
			|| drop->type == wtHeavyRain || drop->type == wtLightning || drop->type == wtCustomRain)
		{			
			int alphaRange = 160;
			drop->dropAlpha = engine->getRand(alphaRange) + 90;
			if (drop->type == wtCustomRain)
			{
				drop->speed = convert_max(0.04f * customRainSpeed + 0.005f * (drop->dropAlpha - 90 - alphaRange / 2), 0.3f);
			}
			else
			{
				drop->speed = 0.8f + 0.005f * (drop->dropAlpha - 90 - alphaRange / 2);
			}
					
		}
		else if (drop->type == wtSnow)
		{
			drop->speed = 0.2f;
		}
	}
	if (newdrop)
	{ 
		drop->type = weatherType;
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
			running = false;
		}
		else if (!isSleeping && t - fadeBeginTime >= fadeLastTime)
		{
			fadding = false;
			running = false;
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
	int customRainDropNum = maxDropNum / 2;
	int customRainSpeed = 100;
	int customRainBoltProb = 10000;

	std::string customRainSoundName;
	std::vector<std::string> customRainBoltSoundName;
	_music customRainSound = nullptr;
	_channel customRainSoundChannel = nullptr;
	std::vector<_music> customRainBoltSound;
}

void Weather::clearRainCustom()
{
	customRainDropNum = maxDropNum / 2;
	customRainSpeed = 100;
	customRainBoltProb = 10000;

	customRainSoundName = u8"";
	customRainBoltSoundName.clear();

	if (customRainSoundChannel != nullptr)
	{
		engine->stopMusic(customRainSoundChannel);
		customRainSoundChannel = nullptr;
	}

	if (customRainSound = nullptr)
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
		drawElementLum();
	}
	if (nowLum != 255)
	{
		engine->drawMask(fadeMask);
	}
}

void Weather::setWeather(WeatherType wType, const std::string& configFIleName)
{
	if (weatherType == wType && wType != wtCustomRain)
	{
		return;
	}
	weatherType = wType;
	if (weatherType == wtCustomRain)
	{
		std::shared_ptr<INIReader> ini = std::make_shared<INIReader>();;
		std::unique_ptr<char[]> s;
		int len = 0;
		len = PakFile::readFile(INI_MAP_FOLDER + configFIleName, s);
		if (s == nullptr || len == 0)
		{
			GameLog::write("no weather ini file: %s\n", (INI_MAP_FOLDER + configFIleName).c_str());
		}
		else
		{
			ini = std::make_shared<INIReader>(s);
		}
		customRainDropNum = ini->GetInteger("Init", u8"Number", customRainDropNum);
		customRainSpeed = ini->GetInteger("Init", u8"Speed", customRainSpeed);
		customRainBoltProb = ini->GetInteger("Init", u8"Speed", customRainBoltProb);
		customRainSoundName = ini->Get("RainSound", u8"1", u8"");
		if (!customRainSoundName.empty())
		{
			customRainSoundName = SOUND_FOLDER + customRainSoundName;
		}
		customRainBoltSoundName.resize(3);
		for (size_t i = 0; i < customRainBoltSoundName.size(); i++)
		{
			customRainBoltSoundName[i] = ini->Get("BoltSound", std::to_string(i + 1), u8""); 
			if (!customRainBoltSoundName[i].empty())
			{
				customRainBoltSoundName[i] = SOUND_FOLDER + customRainBoltSoundName[i];
			}
		}
	}

	if (weatherType == wtLightning)
	{
		lightningInterval = lightningIntervalMin + engine->getRand(lightningIntervalMin * 2);
		lastLightninUTime = getTime();
	}
	resetDrops();
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

void Weather::drawWeather()
{
	for (auto iter = drops.begin(); iter != drops.end(); iter++)
	{
		if (iter->type == wtSnow)
		{
			engine->drawImage(snowflake, (int)round(iter->x), (int)round(iter->y));
		}
		else if (iter->type != wtNone)
		{
			unsigned char a = iter->dropAlpha;
			int w, h = 0;
			engine->getWindowSize(w, h);
			if (iter->y > 0 && iter->y < h)
			{
				unsigned char ta = (unsigned char)((float)200 * float(iter->y / float(h)));
				if (a > ta)
				{
					a = a - ta;
				}
				else
				{
					a = 0;
				}
			}
			else if (iter->y >= h)
			{
				a = 0;
			}
			engine->setImageAlpha(raindrop, a);
			engine->drawImage(raindrop, (int)round(iter->x), (int)round(iter->y));
		}
	}
	
	if (dayMask == nullptr)
	{
		resetDay();
	}
	engine->drawMask(dayMask);

	if (lightningBegin)
	{
		engine->drawMask(lightningMask);
	}
}

void Weather::updateWeather()
{
	auto t = getFrameTime();
	auto iter = drops.begin();
	while (iter != drops.end())
	{
		if (iter->type == wtSnow)
		{
			iter->y = (((float)t) * iter->speed) + iter->y - gm->camera->differencePosition.y;

			iter->x += ((float)(engine->getRand(4) - 2)) - gm->camera->differencePosition.x;
			int w, h;
			engine->getWindowSize(w, h);
            if (iter->x < -DROP_OFF_SCREEN_RANGE)
            {
                iter->x += w + DROP_OFF_SCREEN_RANGE * 2;
            }
            else if (iter->x > w + DROP_OFF_SCREEN_RANGE)
            {
                iter->x -= (w + DROP_OFF_SCREEN_RANGE * 2);
            }
			if (iter->y > h + dropRange)
			{
				if (drops.size() > getDropNum())
				{
					//iter->type = wtNone;
					iter = drops.erase(iter);
					continue;
				}
				else if (weatherType == wtSnow)
				{
					resetDrop(&(*iter), false);
				}
				else
				{
					resetDrop(&(*iter), true);
				}
			}
		}
		else if (iter->type != wtNone)
		{
			iter->y = (((float)t) * (iter->speed > 0.4 ? iter->speed : 0.4)) + iter->y;
			int w, h;
			engine->getWindowSize(w, h);
			if (iter->x < - dropRange || iter->x > w + dropRange || iter->y > h + dropRange)
			{
				if (drops.size() > getDropNum())
				{
					//iter->type = wtNone;
					iter = drops.erase(iter);
					continue;
				}
				else if (weatherType == wtSnow)
				{
					resetDrop(&(*iter), true);
				}
				else
				{
					resetDrop(&(*iter), false);
				}
			}
		}
		else
		{
			if (drops.size() > getDropNum())
			{
				iter = drops.erase(iter);
				continue;
			}
			else
			{
				resetDrop(&(*iter), true);
			}
		}
		iter++;
	}

	while (drops.size() < getDropNum())
	{
		WeatherDrop initDrop;
		resetDrop(&initDrop, true);
		drops.push_back(initDrop);
	}

	if (lightningBegin)
	{
		// ���翪ʼ����Ҫ����
		if (getTime() - lightningBeginTime > lightninUTime)
		{
			lightningBegin = false;
			lastLightninUTime = getTime();
			if (weatherType == wtLightning)
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
		if (weatherType == wtLightning)
		{
			if (getTime() - lastLightninUTime > lightningInterval)
			{
				lightningBegin = true;
			}
		}
		else if (weatherType == wtCustomRain)
		{
			if (getTime() - lastLightninUTime > convert_max(lightningIntervalMin, customRainBoltProb))
			{
				if (customRainBoltSoundName.size() > 0)
				{
					auto index = engine->getRand(customRainBoltSoundName.size());
					if (!customRainBoltSoundName[index].empty())
					{
						lightningBegin = true;

						std::unique_ptr<char[]> s;
						int len = PakFile::readFile(customRainBoltSoundName[index], s);
						if (s != nullptr && len > 0)
						{
							engine->playSound(s, len);
						}
					}
				}

				if (!lightningBegin)
				{
					lastLightninUTime = getTime();
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
	weatherType = wtNone;
	WeatherDrop initDrop;
	for (auto iter = drops.begin(); iter != drops.end(); iter++)
	{
		*iter = initDrop;
	}
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