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
	fileName = SAVE_CURRENT_FOLDER + fileName;
	INIReader ini(fileName);

	gameType = ini.GetInteger(u8"Game", u8"Type", gameType);

	useWav = ini.GetBoolean(u8"Game", u8"UseWav", false);

	data.mapName = ini.Get(u8"State", u8"Map", u8"");
	data.npcName = ini.Get(u8"State", u8"Npc", u8"");
	data.objName = ini.Get(u8"State", u8"Obj", u8"");
	data.bgmName = ini.Get(u8"State", u8"Bgm", u8"");

    data.characterIndex = ini.GetInteger(u8"State", u8"Chr", -1);

    data.asfStyle = ini.GetColor(u8"Option", u8"AsfStyle", 0xFFFFFF);
    data.mpcStyle = ini.GetColor(u8"Option", u8"MpcStyle", 0xFFFFFF);
    
    data.mainLum = ini.GetInteger(u8"Option", u8"MainLum", 31);
	data.fadeLum = ini.GetInteger(u8"Option", u8"FadeLum", 31);
	data.mapTime = ini.GetInteger(u8"Option", u8"mapTime", mtDay);
	data.snowShow = ini.GetBoolean(u8"Option", u8"SnowShow", false);
	data.rainShow = ini.GetBoolean(u8"Option", u8"RainShow", false);
	data.NPCAI = ini.GetBoolean(u8"Option", u8"NPCAI", true);
	data.canInput = ini.GetBoolean(u8"Option", u8"CanInput", true);
    
    data.water = ini.GetBoolean(u8"Option", u8"Water", false);
    data.rainFile = ini.Get(u8"Option", u8"RainFile", u8"");

}
void Global::save()
{
	std::string fileName = GLOBAL_INI;
	fileName = SAVE_CURRENT_FOLDER + fileName;
	INIReader ini(fileName);

	ini.SetInteger(u8"Game", u8"Type", gameType);
	ini.SetBoolean(u8"Game", u8"UseWav", useWav);

	ini.Set(u8"State", u8"Map", data.mapName);
	ini.Set(u8"State", u8"Npc", data.npcName);
	ini.Set(u8"State", u8"Obj", data.objName);
	ini.Set(u8"State", u8"Bgm", data.bgmName);
    
    ini.SetInteger(u8"State", u8"Chr", data.characterIndex);
    
    ini.SetColor(u8"Option", u8"AsfStyle", data.asfStyle);
    ini.SetColor(u8"Option", u8"MpcStyle", data.mpcStyle);

	ini.SetInteger(u8"Option", u8"MainLum", data.mainLum);
	ini.SetInteger(u8"Option", u8"FadeLum", data.fadeLum);
	ini.SetInteger(u8"Option", u8"mapTime", data.mapTime);
	ini.SetBoolean(u8"Option", u8"SnowShow", data.snowShow);
	ini.SetBoolean(u8"Option", u8"RainShow", data.rainShow);
	ini.SetBoolean(u8"Option", u8"NPCAI", data.NPCAI);
	ini.SetBoolean(u8"Option", u8"CanInput", data.canInput);
    
    ini.SetBoolean(u8"Option", u8"Water", data.water);
    ini.Set(u8"Option", u8"RainFile", data.rainFile);

	ini.saveToFile(fileName);
}
/*
Global * Global::getInstance()
{
	return this_;
}
*/
