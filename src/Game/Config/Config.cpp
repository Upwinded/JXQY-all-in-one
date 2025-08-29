#include "Config.h"

FullScreenMode Config::fullScreenMode = FullScreenMode::window;
FullScreenSolutionMode Config::fullScreenSolutionMode = FullScreenSolutionMode::original;
bool Config::playerAlpha = true;
#ifdef __ANDROID__
bool Config::loadWithThread = false;
#else
bool Config::loadWithThread = true;
#endif
int Config::windowWidth = DEFAULT_WINDOW_WIDTH;
int Config::windowHeight = DEFAULT_WINDOW_HEIGHT;
float Config::gameSpeed = SPEED_TIME_DEFAULT;
int Config::display = 0;

Config::Config()
{
}

Config::~Config()
{
}

void Config::load()
{
	std::string fileName = CONFIG_INI;
	GameLog::write(u8"load config \n");
	INIReader ini(fileName);
#ifdef __MOBILE__
    fullScreenMode = FullScreenMode::fullScreen;
    fullScreenSolutionMode = FullScreenSolutionMode::adjust;
    windowWidth = MOBILE_DEFAULT_WINDOW_WIDTH;
    windowHeight = MOBILE_DEFAULT_WINDOW_HEIGHT;
#else
	fullScreenMode = (FullScreenMode)ini.GetInteger(u8"game", u8"fullscreenmode", (int)fullScreenMode);
    fullScreenSolutionMode = (FullScreenSolutionMode)ini.GetInteger(u8"game", u8"fullscreensolutionmode", (int)fullScreenSolutionMode);
    windowWidth = ini.GetInteger(u8"game", u8"windowwidth", windowWidth);
    windowHeight = ini.GetInteger(u8"game", u8"windowheight", windowHeight);
	display = ini.GetInteger(u8"game", u8"display", display);
#endif
	playerAlpha = ini.GetBoolean(u8"game", u8"playeralpha", playerAlpha);
	loadWithThread = ini.GetBoolean(u8"game", u8"loadwiththread", loadWithThread);

	auto speed = ini.GetInteger(u8"game", u8"speed", convSpeedToInt(gameSpeed));
	gameSpeed = convSpeedTofloat(speed);

	float musicVolume = ((float)ini.GetInteger(u8"game", u8"musicvolume", 100)) / 100.0f;
	float soundVolume = ((float)ini.GetInteger(u8"game", u8"soundvolume", 100)) / 100.0f;
	if (musicVolume < 0.0f)
	{
		musicVolume = 0.0f;
	}
	else if (musicVolume > 1.0f)
	{
		musicVolume = 1.0f;
	}
	Engine::getInstance()->setBGMVolume(musicVolume);
	if (soundVolume < 0.0f)
	{
		soundVolume = 0.0f;
	}
	else if (soundVolume > 1.0f)
	{
		soundVolume = 1.0f;
	}
	Engine::getInstance()->setSoundVolume(soundVolume);
}

void Config::save()
{
	std::string fileName = CONFIG_INI;
	INIReader ini(fileName);
	ini.SetInteger(u8"game", u8"fullscreenmode", (int)fullScreenMode);
    ini.SetInteger(u8"game", u8"fullscreensolutionmode", (int)fullScreenSolutionMode);
	ini.SetBoolean(u8"game", u8"loadWithThread", loadWithThread);
	ini.SetBoolean(u8"game", u8"playeralpha", playerAlpha);
	float musicVolume = Engine::getInstance()->getBGMVolume();
	float soundVolume = Engine::getInstance()->getSoundVolume();
	//ini.SetReal(u8"game", u8"speed", gameSpeed);
	ini.SetInteger(u8"game", u8"speed", convSpeedToInt(gameSpeed));
	if (musicVolume < 0.0f)
	{
		musicVolume = 0.0f;
	}
	else if (musicVolume > 1.0f)
	{
		musicVolume = 1.0f;
	}
	if (soundVolume < 0.0f)
	{
		soundVolume = 0.0f;
	}
	else if (soundVolume > 1.0f)
	{
		soundVolume = 1.0f;
	}
	ini.SetInteger(u8"game", u8"musicvolume", (int)(musicVolume * 100));
	ini.SetInteger(u8"game", u8"soundvolume", (int)(soundVolume * 100));
	ini.SetInteger(u8"game", u8"windowwidth", windowWidth);
	ini.SetInteger(u8"game", u8"windowheight", windowHeight);
	ini.SetInteger(u8"game", u8"display", display);
	ini.saveToFile(fileName);
}

float Config::getMusicVolume()
{
	return Engine::getInstance()->getBGMVolume();
}

float Config::getSoundVolume()
{
	return Engine::getInstance()->getSoundVolume();
}

float Config::setMusicVolume(float volume)
{
	Engine::getInstance()->setBGMVolume(volume);
	return getMusicVolume();
}

float Config::setSoundVolume(float volume)
{
	Engine::getInstance()->setSoundVolume(volume);
	return getSoundVolume();
}

void Config::getWindowSize(int& w, int& h)
{
	w = windowWidth;
	h = windowHeight;
}

void Config::setDefaultWindowSize(int w, int h)
{
    windowWidth = w;
    windowHeight = h;
}

float Config::getGameSpeed()
{
	return gameSpeed;
}

float Config::setGameSpeed(float speed)
{
	if (speed <= SPEED_TIME_MIN)
	{
		gameSpeed = SPEED_TIME_MIN;
	}
	else if(speed >= SPEED_TIME_MAX)
	{
		gameSpeed = SPEED_TIME_MAX;
	}
	else
	{
		gameSpeed = speed;
	}
	return gameSpeed;
}

float Config::convSpeedTofloat(int speed)
{
	const int speed_min_int = 0;
	const int speed_max_int = 100;
	if (speed <= speed_min_int)
	{
		return SPEED_TIME_MIN;
	}
	if (speed >= speed_max_int)
	{
		return SPEED_TIME_MAX;
	}
	auto ret = ((float)(speed - speed_min_int)) / (speed_max_int - speed_min_int) * (SPEED_TIME_MAX - SPEED_TIME_MIN) + SPEED_TIME_MIN;
	if (ret <= SPEED_TIME_MIN)
	{
		return SPEED_TIME_MIN;
	}
	else if (ret >= SPEED_TIME_MAX)
	{
		return SPEED_TIME_MAX;
	}
	return ret;
}

int Config::convSpeedToInt(float speed)
{
	const int speed_min_int = 0;
	const int speed_max_int = 100;
	if (speed <= SPEED_TIME_MIN)
	{
		return speed_min_int;
	}
	else if (speed >= SPEED_TIME_MAX)
	{
		return speed_max_int;
	}
	auto ret = (int)round((speed - SPEED_TIME_MIN) / (SPEED_TIME_MAX - SPEED_TIME_MIN) * (speed_max_int - speed_min_int)) + speed_min_int;
	if (ret <= speed_min_int)
	{
		return speed_min_int;
	}
	if (ret >= speed_max_int)
	{
		return speed_max_int;
	}
	return ret;
}

