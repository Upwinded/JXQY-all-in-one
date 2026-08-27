#include "Config.h"
#include "../../Engine/Engine.h"
#include "../../File/log.h"
#include "../../File/File.h"
#include "../../Engine/LogicalResolutionPolicy.h"

FullScreenMode Config::fullScreenMode = FullScreenMode::window;
FullScreenSolutionMode Config::fullScreenSolutionMode = FullScreenSolutionMode::original;
bool Config::playerAlpha = true;
#ifdef __ANDROID__
bool Config::loadAsync = false;
#else
bool Config::loadAsync = true;
#endif
bool Config::externalResourcesEnabled = false;
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
	GameLog::write("load config \n");
	std::unique_ptr<char[]> configData;
	int configLength = 0;
	File::readSharedApplicationFile(CONFIG_INI, configData, configLength);
	INIReader ini(configData);
#ifdef __MOBILE__
    fullScreenMode = FullScreenMode::fullScreen;
    fullScreenSolutionMode = FullScreenSolutionMode::adjust;
    windowWidth = MOBILE_DEFAULT_WINDOW_WIDTH;
    windowHeight = MOBILE_DEFAULT_WINDOW_HEIGHT;
#else
	long fullScreenModeValue = ini.GetInteger("game", "fullscreenmode", (int)fullScreenMode);
	if (fullScreenModeValue >= (int)FullScreenMode::window &&
		fullScreenModeValue <= (int)FullScreenMode::fullScreen)
	{
		fullScreenMode = (FullScreenMode)fullScreenModeValue;
	}
	long fullScreenSolutionModeValue = ini.GetInteger("game", "fullscreensolutionmode",
		(int)fullScreenSolutionMode);
	if (fullScreenSolutionModeValue >= (int)FullScreenSolutionMode::original &&
		fullScreenSolutionModeValue <= (int)FullScreenSolutionMode::forceToUseSetting)
	{
		fullScreenSolutionMode = (FullScreenSolutionMode)fullScreenSolutionModeValue;
	}
    windowWidth = ini.GetInteger("game", "windowwidth", windowWidth);
    windowHeight = ini.GetInteger("game", "windowheight", windowHeight);
	display = ini.GetInteger("game", "display", display);
#endif
	LogicalResolutionPolicy::constrain(windowWidth, windowHeight);
	playerAlpha = ini.GetBoolean("game", "playeralpha", playerAlpha);
	loadAsync = ini.GetBoolean("game", "loadAsync", loadAsync);
	externalResourcesEnabled = ini.GetBoolean("game", "externalresources", externalResourcesEnabled);

	auto speed = ini.GetInteger("game", "speed", convSpeedToInt(gameSpeed));
	gameSpeed = convSpeedTofloat(speed);

	float musicVolume = ((float)ini.GetInteger("game", "musicvolume", 100)) / 100.0f;
	float soundVolume = ((float)ini.GetInteger("game", "soundvolume", 100)) / 100.0f;
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
	LogicalResolutionPolicy::constrain(windowWidth, windowHeight);
	std::unique_ptr<char[]> configData;
	int configLength = 0;
	File::readSharedApplicationFile(CONFIG_INI, configData, configLength);
	INIReader ini(configData);
	ini.SetInteger("game", "fullscreenmode", (int)fullScreenMode);
    ini.SetInteger("game", "fullscreensolutionmode", (int)fullScreenSolutionMode);
	ini.SetBoolean("game", "loadAsync", loadAsync);
	ini.SetBoolean("game", "playeralpha", playerAlpha);
	ini.SetBoolean("game", "externalresources", externalResourcesEnabled);
	float musicVolume = Engine::getInstance()->getBGMVolume();
	float soundVolume = Engine::getInstance()->getSoundVolume();
	//ini.SetReal("game", "speed", gameSpeed);
	ini.SetInteger("game", "speed", convSpeedToInt(gameSpeed));
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
	ini.SetInteger("game", "musicvolume", (int)(musicVolume * 100));
	ini.SetInteger("game", "soundvolume", (int)(soundVolume * 100));
	ini.SetInteger("game", "windowwidth", windowWidth);
	ini.SetInteger("game", "windowheight", windowHeight);
	ini.SetInteger("game", "display", display);
	std::string configText = ini.saveToString();
	if (!File::writeSharedApplicationFile(CONFIG_INI, configText.data(),
		static_cast<int>(configText.size())))
	{
		GameLog::write("Can not save shared application config %s\n", CONFIG_INI);
	}
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
	LogicalResolutionPolicy::constrain(windowWidth, windowHeight);
	w = windowWidth;
	h = windowHeight;
}

void Config::setDefaultWindowSize(int w, int h)
{
	windowWidth = LogicalResolutionPolicy::constrainWidth(w);
	windowHeight = LogicalResolutionPolicy::constrainHeight(h);
}

void Config::setDesktopDisplaySettings(
	const DesktopDisplaySettings& settings)
{
	display = settings.displayIndex < 0 ? 0 : settings.displayIndex;
	windowWidth = LogicalResolutionPolicy::constrainWidth(settings.width);
	windowHeight = LogicalResolutionPolicy::constrainHeight(settings.height);
	fullScreenMode = settings.fullScreenMode;
	fullScreenSolutionMode = settings.fullScreenSolutionMode;
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
