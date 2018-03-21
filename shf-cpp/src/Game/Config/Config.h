#pragma once

#include "../../File/INIReader.h"
#include "../GameTypes.h"
#include "../../Engine/Engine.h"

class Config
{
public:
	Config();
	~Config();

	static Config * getInstance();

	bool fullScreen = true;
	bool playerAlpha = true;

	void load();
	void save();
	float getMusicVolume();
	float getSoundVolume();
	float setMusicVolume(float volume);
	float setSoundVolume(float volume);

private:
	static Config config;
	static Config * this_;
};

