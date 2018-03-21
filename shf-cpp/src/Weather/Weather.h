#pragma once

#include "../Engine/Engine.h"
#include "../Types/Types.h"
#include "../Element/Element.h"

struct WeatherDrop
{
	WeatherType type = wtNone;
	float x = -1;
	float y = -1;
	float speed = 1.0f;
	int dropAlpha = 0;
};

class Weather :
	public Element
{
public:
	Weather();
	~Weather();

private:
	bool init = false;

	WeatherType weatherType = wtNone;
	DayType dayType = dtDay;
	void resetDay();

	bool fadding = false;
	bool isSleeping = false;
	bool isFadeIn = false;
	unsigned int sleepLastTime = 0;
	unsigned int fadeBeginTime = 0;
	const unsigned int fadeLastTime = 500;

	_image fadeMask = NULL;
	_image dayMask = NULL;
	_image raindrop = NULL;
	_image snowflake = NULL;
	_image lightningMask = NULL;
	_image lumMask = NULL;

	void createLumMask();
	void drawElementLum();

	const unsigned int lightningIntervalMin = 5000;
	unsigned int lastLightningTime = 0;

	bool lightningBegin = false;
	unsigned int lightningBeginTime = 0;
	unsigned int lightningTime = 300;

	const int dropRange = 200;
	const int dropWRange = 100;
	
	const int maxDropNum = 250;
	const int lrainDropNum = 30;
	const int rainDropNum = 90;
	const int hrainDropNum = 250;
	const int lnDropNum = 200;
	const int snowDropNum = 250;

	std::vector<WeatherDrop> drops;

	int getDropNum();
	void resetDrops();
	void resetDrop(WeatherDrop * drop, bool newdrop);

	void updateFade();
public:
	void draw();

	unsigned char nowLum = 255;
	unsigned char fadeLum = 0;
	unsigned char lum = 0;

	void fadeInEx();
	void fadeIn();
	void fadeOut();
	void sleep(unsigned int t);

	void setFadeLum(unsigned char l);

	void setLum(unsigned char l);
	void setTime(unsigned char t);

	void setWeather(WeatherType wType);
	void setDay(DayType dType);

	void drawWeather();
	void updateWeather();

	void reset();

private:
	virtual void onDraw();
	virtual void onUpdate();
};

