#include "Config.h"

Config Config::config;
Config * Config::this_ = &Config::config;

Config::Config()
{
	load();
}


Config::~Config()
{
}

Config * Config::getInstance()
{
	return this_;
}

void Config::load()
{
	std::string fileName = CONFIG_INI;
	INIReader ini = INIReader::INIReader(fileName);
	fullScreen = ini.GetBoolean("Game", "FullScreen", fullScreen);
	playerAlpha = ini.GetBoolean("Game", "PlayerAlpha", playerAlpha);
	canChangeDisplayMode = ini.GetBoolean("Game", "CanChangeDisplayMode", canChangeDisplayMode);

	windowWidth = ini.GetInteger("Game", "WindowWidth", windowWidth);
	windowHeight = ini.GetInteger("Game", "WindowHeight", windowHeight);

	float musicVolume = ((float)ini.GetInteger("Game", "MusicVolume", 100)) / 100.0f;
	float soundVolume = ((float)ini.GetInteger("Game", "SoundVolume", 100)) / 100.0f;
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
	INIReader ini = INIReader::INIReader(fileName);
	ini.SetBoolean("Game", "FullScreen", fullScreen);
	ini.SetBoolean("Game", "CanChangeDisplayMode", canChangeDisplayMode);
	ini.SetBoolean("Game", "PlayerAlpha", playerAlpha);
	float musicVolume = Engine::getInstance()->getBGMVolume();
	float soundVolume = Engine::getInstance()->getSoundVolume();
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
	ini.SetInteger("Game", "MusicVolume", (int)(musicVolume * 100));
	ini.SetInteger("Game", "SoundVolume", (int)(soundVolume * 100));
	ini.SetInteger("Game", "WindowWidth", windowWidth);
	ini.SetInteger("Game", "WindowHeight", windowHeight);
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

void Config::getWindowSize(int * w, int * h)
{
	if (w != NULL)
	{
		*w = windowWidth;
	}
	if (h != NULL)
	{
		*h = windowHeight;
	}
}

