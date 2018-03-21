#include "Global.h"


//Global Global::global;
//Global * Global::this_ = &Global::global;

Global::Global()
{
}


Global::~Global()
{
}

void Global::load()
{
	std::string fileName = GLOBAL_INI;
	fileName = DEFAULT_FOLDER + fileName;
	INIReader ini = INIReader::INIReader(fileName);

	data.mapName = ini.Get("State", "Map", "");
	data.npcName = ini.Get("State", "Npc", "");
	data.objName = ini.Get("State", "Obj", "");
	data.bgmName = ini.Get("State", "Bgm", "");

	data.mainLum = ini.GetInteger("Option", "MainLum", 31);
	data.fadeLum = ini.GetInteger("Option", "FadeLum", 31);
	data.mapTime = ini.GetInteger("Option", "mapTime", mtDay);
	data.snowShow = ini.GetBoolean("Option", "SnowShow", false);
	data.rainShow = ini.GetBoolean("Option", "RainShow", false);
	data.NPCAI = ini.GetBoolean("Option", "NPCAI", true);
	data.canInput = ini.GetBoolean("Option", "CanInput", true);

}
void Global::save()
{
	std::string fileName = GLOBAL_INI;
	fileName = DEFAULT_FOLDER + fileName;
	INIReader ini = INIReader::INIReader(fileName);

	ini.Set("State", "Map", data.mapName);
	ini.Set("State", "Npc", data.npcName);
	ini.Set("State", "Obj", data.objName);
	ini.Set("State", "Bgm", data.bgmName);

	ini.SetInteger("Option", "MainLum", data.mainLum);
	ini.SetInteger("Option", "FadeLum", data.fadeLum);
	ini.SetInteger("Option", "mapTime", data.mapTime);
	ini.SetBoolean("Option", "SnowShow", data.snowShow);
	ini.SetBoolean("Option", "RainShow", data.rainShow);
	ini.SetBoolean("Option", "NPCAI", data.NPCAI);
	ini.SetBoolean("Option", "CanInput", data.canInput);

	ini.saveToFile(fileName);
}
/*
Global * Global::getInstance()
{
	return this_;
}
*/
