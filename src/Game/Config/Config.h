#pragma once


#include "../../File/INIReader.h"
#include "../GameTypes.h"
#include "../../Engine/Engine.h"
#include "../../Engine/EngineBase.h"


class Config
{
public:
	Config();
	virtual ~Config();

    static FullScreenMode fullScreenMode;
    static FullScreenSolutionMode fullScreenSolutionMode;
	static bool playerAlpha;

	// 防界面卡死的加载方式
	// Android（MIUI14开发版）下多线程会造成卡顿，不清楚原因
	static bool loadWithThread;

	static void load();
	static void save();
	static float getMusicVolume();
	static float getSoundVolume();
	static float setMusicVolume(float volume);
	static float setSoundVolume(float volume);

	static void getWindowSize(int& w, int& h);
	static void setDefaultWindowSize(int w, int h);

	static float getGameSpeed();
	static float setGameSpeed(float speed);

	static int display;

private:
	static int windowWidth;
	static int windowHeight;

	static float gameSpeed;

	static int convSpeedToInt(float speed);
	static float convSpeedTofloat(int speed);
};

