#pragma once

#include "../Engine/Engine.h"
#include "../Types/Types.h"
#include "../Element/Element.h"

#define DROP_OFF_SCREEN_RANGE 50

struct WeatherDrop
{
	WeatherType type = wtNone;
	double x = -1;
	double y = -1;
	float speed = 1.0f;
	int dropAlpha = 0;
};

class Weather :
	public Element
{
public:
	Weather();
	virtual ~Weather();

private:
	bool init = false;

	WeatherType weatherType = wtNone;
	DayType dayType = dtDay;
	void resetDay();

	bool fadding = false;
	bool isSleeping = false;
	bool isFadeIn = false;
	UTime sleepLastTime = 0;
	UTime fadeBeginTime = 0;
	const unsigned int fadeLastTime = 500;

	_shared_image fadeMask = nullptr;
	_shared_image dayMask = nullptr;
	_shared_image raindrop = nullptr;
	_shared_image snowflake = nullptr;
	_shared_image lightningMask = nullptr;
	_shared_image lumMask = nullptr;

	void createLumMask();
	void drawElementLum();

	const UTime lightningIntervalMin = 5000;
	UTime lastLightninUTime = 0;

	bool lightningBegin = false;
	UTime lightningBeginTime = 0;
	UTime lightninUTime = 300;
	UTime lightningInterval = lightningIntervalMin;

	const int dropRange = 200;
	const int dropWRange = 100;
	
	const int maxDropNum = 500;
	const int lrainDropNum = 50;
	const int rainDropNum = 200;
	const int hrainDropNum = 500;
	const int lnDropNum = 200;
	const int snowDropNum = 250;

	std::list<WeatherDrop> drops;

	int getDropNum();
	void resetDrops();
	void resetDrop(WeatherDrop* drop, bool newdrop);

	void updateFade();

	int customRainDropNum = maxDropNum / 2;
	int customRainSpeed = 100;
	int customRainBoltProb = 10000;

	std::string customRainSoundName;
	std::vector<std::string> customRainBoltSoundName;
	_music customRainSound = nullptr;
	_channel customRainSoundChannel = nullptr;

	void setRainCustomFromIni(std::shared_ptr<INIReader> ini);
	void clearRainCustom();

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

	void setWeather(WeatherType wType, const std::string& configFIleName = "");
	void setDay(DayType dType);

	void drawWeather();
	void updateWeather();

	void reset();

private:
	virtual void onDraw();
	virtual void onUpdate();
};

