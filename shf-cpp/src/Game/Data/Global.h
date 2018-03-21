#pragma once

#include <string>
#include <vector>
#include "../GameTypes.h"
#include "../../File/INIReader.h"
#include "../../File/PakFile.h"

enum MapTime
{
	mtDay = 0,
	mtNight = 1,
	mtDusk = 2,
	mtDawn = 3,
};

struct GlobalData
{
	int state = gsNone;
	std::string mapName = "";
	std::string npcName = "";
	std::string objName = "";
	std::string bgmName = "";

	unsigned int mainLum = 31;
	unsigned int fadeLum = 31;
	unsigned int mapTime = mtDay;
	bool snowShow = 0;
	bool rainShow = 0;

	bool NPCAI = true;
	bool canInput = true;
};

class Global
{
public:
	Global();
	~Global();

	GlobalData data;

	void load();
	void save();

private:
	GlobalData globalData;

};

