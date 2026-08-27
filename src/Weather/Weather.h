#pragma once

#include "../Types/Types.h"
#include "../Element/Element.h"

#define DROP_OFF_SCREEN_RANGE 50

struct WeatherDrop
{
	WeatherType type = wtNone;
	float x = -1;
	float y = -1;
	float speed = 1.0f;
	float horizontalSpeed = 0.0f;
	float cameraParallax = 0.0f;
	float swayPhase = 0.0f;
	float swayAngularSpeed = 0.0f;
	float swayAmplitude = 0.0f;
	float visualAngle = 0.0f;
	int visualWidth = 1;
	int visualLength = 1;
	int dropAlpha = 0;
};

struct LightSource
{
	Point position;
	PointEx offset;
	uint8_t red = 0xFF;
	uint8_t green = 0xFF;
	uint8_t blue = 0xFF;
	float intensity = 1.0f;
};

class Weather :
	public Element
{
public:
	Weather();
	virtual ~Weather();

private:
	bool init = false;

	WeatherType rainWeatherType = wtNone;
	bool snowVisible = false;
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
	bool isAmbientLumOverlayEnabled() const;
	void collectLightSources(std::vector<LightSource>& lights, Point cenTile, PointEx offset, Point cenScreen, int xscal, int yscal, int tileHeightScal);
	void drawLightingOverlay();
	void mergeLightSources(std::vector<LightSource>& lights);

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

	std::list<WeatherDrop> rainDrops;
	std::list<WeatherDrop> snowDrops;

	int getDropNum(WeatherType weatherType) const;
	void resetDrops(std::list<WeatherDrop>& weatherDrops, WeatherType weatherType);
	void resetDrop(WeatherDrop* drop, WeatherType weatherType, bool newdrop);
	void drawDrops(const std::list<WeatherDrop>& weatherDrops);
	void updateDrops(
		std::list<WeatherDrop>& weatherDrops,
		WeatherType weatherType,
		PointEx cameraDelta);

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
	void drawElementLum();

	unsigned char nowLum = 255;
	unsigned char fadeLum = 0;
	unsigned char lum = 0;

	void fadeInEx();
	void fadeIn();
	void fadeOut();
	void sleep(unsigned int t);

	void setFadeLum(unsigned char l);

	void setLum(unsigned char l);
	void setTime(int time);

	void setWeather(WeatherType weatherType, const std::string& configFileName = "");
	void setRainWeather(WeatherType weatherType, const std::string& configFileName = "");
	void setSnowVisible(bool visible);
	void setDay(DayType dType);

	void drawWeather();
	void updateWeather();

	void reset();
	int getConfiguredRainDropCount() const { return customRainDropNum; }
	int getConfiguredRainSpeed() const { return customRainSpeed; }
	int getConfiguredBoltProbability() const { return customRainBoltProb; }
	const std::string& getConfiguredRainSoundName() const { return customRainSoundName; }
	bool hasCustomRainSoundChannel() const { return customRainSoundChannel != nullptr; }
	bool isRaining() const { return rainWeatherType != wtNone; }
	bool isSnowing() const { return snowVisible; }

private:
	virtual void onDraw();
	virtual void onUpdate();
};
