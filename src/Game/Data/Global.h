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

	int characterIndex = -1;

	Uint32 asfStyle = 0xFFFFFF00;
	Uint32 mpcStyle = 0xFFFFFF00;

	Uint32 mainLum = 31;
	Uint32 fadeLum = 31;
	Uint32 mapTime = mtDay;

	std::string rainFile = "";
	bool water = false;
	bool snowShow = false;
	bool rainShow = false;

	bool NPCAI = true;
	bool canInput = true;

};

#define GAME_JXQY2 0
#define GAME_YYCS 1
#define GAME_XJXQY 2

class Global
{
public:
	Global();
	virtual ~Global();

	int gameType = GAME_JXQY2;
	bool useWav = false;

	GlobalData data;

	void load();
	void save();

};

