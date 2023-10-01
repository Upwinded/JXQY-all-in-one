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
			for (size_t k = 0; k < gm->map->dataMap.tile[i][j].objIndex.size(); k++)
			{
				if (gm->objectManager->objectList[gm->map->dataMap.tile[i][j].objIndex[k]]->lum > (int)gm->global.data.mainLum)
				{
					drawLumMask = true;
					eOffset = gm->objectManager->objectList[gm->map->dataMap.tile[i][j].objIndex[k]]->offset;
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
	if (weatherType == wtLightRain)
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
	else if (weatherType == wtSnow)
	{
		return snowDropNum;
	}
	return 0;
}

void Weather::resetDrops()
{
	int dropNum = getDropNum();	
	for (int i = 0; i < (int)drops.size(); i++)
	{	
		if (drops[i].type == wtNone && i < dropNum)
		{
			resetDrop(&drops[i], true);
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
		drop->x = (float)((rand() % (w + dropWRange / 4)) - dropWRange / 4);
	}
	else
	{
		drop->x = (float)(rand() % w);	
	}
	
	if (drop->type != weatherType)
	{
		drop->type = weatherType;
		if (drop->type == wtRain || drop->type == wtLightRain
			|| drop->type == wtHeavyRain || drop->type == wtLightning)
		{			
			int alphaRange = 160;
			drop->dropAlpha = rand() % alphaRange + 90;
			drop->speed = 0.8f + 0.005f * (drop->dropAlpha - 90 - alphaRange / 2);			
		}
		else if (drop->type == wtSnow)
		{
			drop->speed = 0.2f;
		}
	}
	if (newdrop)
	{ 
		drop->type = weatherType;
		drop->y = (float)(- dropRange - (rand() % (h + dropRange * 2)));
	}
	else
	{
		drop->y = (float)(- dropRange - (rand() % (dropRange * 3)));
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
				nowLum = (unsigned char)(((double)(t - fadeBeginTime) / fadeLastTime) * (255 - fadeLum) + fadeLum);
			}
			else
			{
				nowLum = (unsigned char)(((double)(fadeLastTime - t + fadeBeginTime) / fadeLastTime) * (255 - fadeLum) + fadeLum);
			}
			engine->setImageAlpha(fadeMask, 255 - nowLum);
		}
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

void Weather::setWeather(WeatherType wType)
{
	if (weatherType == wType)
	{
		return;
	}
	srand((unsigned int)time(0));
	weatherType = wType;
	if (weatherType == wtLightning)
	{
		lastLightningTime = getTime();
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
	for (int i = 0; i < (int)drops.size(); i++)
	{
		if (drops[i].type == wtSnow)
		{
			engine->drawImage(snowflake, (int)round(drops[i].x), (int)round(drops[i].y));
		}
		else if (drops[i].type != wtNone)
		{
			unsigned char a = drops[i].dropAlpha;
			int w, h = 0;
			engine->getWindowSize(w, h);
			if (drops[i].y > 0 && drops[i].y < h)
			{
				unsigned char ta = (unsigned char)((float)200 * float(drops[i].y / float(h)));
				if (a > ta)
				{
					a = a - ta;
				}
				else
				{
					a = 0;
				}
			}
			else if (drops[i].y >= h)
			{
				a = 0;
			}
			engine->setImageAlpha(raindrop, a);
			engine->drawImage(raindrop, (int)round(drops[i].x), (int)round(drops[i].y));
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
	for (int i = 0; i < (int)drops.size(); i++)
	{
		if (drops[i].type == wtSnow)
		{
			drops[i].y = (((double)t) * drops[i].speed) + drops[i].y - gm->camera->differencePosition.y;
			drops[i].x += ((double)(rand() % 4 - 2)) - gm->camera->differencePosition.x;
			int w, h;
			engine->getWindowSize(w, h);
            if (drops[i].x < -DROP_OFF_SCREEN_RANGE)
            {
                drops[i].x += w + DROP_OFF_SCREEN_RANGE * 2;
            }
            else if (drops[i].x > w + DROP_OFF_SCREEN_RANGE)
            {
                drops[i].x -= (w + DROP_OFF_SCREEN_RANGE * 2);
            }
			if (drops[i].y > h + dropRange)
			{
				if (i >= getDropNum())
				{
					drops[i].type = wtNone;
				}
				else if (weatherType == wtSnow)
				{
					resetDrop(&drops[i], false);
				}
				else
				{
					resetDrop(&drops[i], true);
				}
			}
		}
		else if (drops[i].type != wtNone)
		{
			drops[i].y = (((double)t) * (drops[i].speed > 0.4 ? drops[i].speed : 0.4)) + drops[i].y;
			int w, h;
			engine->getWindowSize(w, h);
			if (drops[i].x < - dropRange || drops[i].x > w + dropRange || drops[i].y > h + dropRange)
			{
				if (i >= getDropNum())
				{
					drops[i].type = wtNone;
				}
				else if (weatherType == wtSnow)
				{
					resetDrop(&drops[i], true);
				}
				else
				{
					resetDrop(&drops[i], false);
				}
			}
		}
	}

	if (lightningBegin)
	{
		if (getTime() - lightningBeginTime > lightningTime)
		{
			lightningBegin = false;
			lastLightningTime = getTime();
		}
		else
		{
			if (getTime() - lightningBeginTime > lightningTime / 3)
			{
				engine->setImageAlpha(lightningMask, 160 - (getTime() - lightningBeginTime - lightningTime / 3) * 140 / (lightningTime * 2 / 3));
			}
			else
			{
				engine->setImageAlpha(lightningMask, 160 - (lightningTime / 3 - getTime() + lightningBeginTime) * 140 / (lightningTime / 3));
			}
		}
	}
	else if (weatherType == wtLightning)
	{
		if (getTime() - lastLightningTime > lightningIntervalMin + rand() % lightningIntervalMin )
		{
			lightningBegin = true;
			lightningBeginTime = getTime();
			lastLightningTime = lightningBeginTime;
			engine->setImageAlpha(lightningMask, 120);
		}
	}
}

void Weather::reset()
{
	weatherType = wtNone;
	WeatherDrop initDrop;
	for (size_t i = 0; i < drops.size(); i++)
	{
		drops[i] = initDrop;
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