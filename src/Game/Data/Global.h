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

	std::string mapName = u8"";
	std::string npcName = u8"";
	std::string objName = u8"";
	std::string bgmName = u8"";

	int characterIndex = -1;

	uint32_t asfStyle = 0xFFFFFF;
	uint32_t mpcStyle = 0xFFFFFF;

	uint32_t mainLum = 31;
	uint32_t fadeLum = 31;
	uint32_t mapTime = mtDay;

	std::string rainFile = u8"";
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

