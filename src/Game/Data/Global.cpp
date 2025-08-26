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

	gameType = ini.GetInteger("Game", u8"Type", gameType);

	useWav = ini.GetBoolean("Game", u8"UseWav", false);

	data.mapName = ini.Get("State", u8"Map", u8"");
	data.npcName = ini.Get("State", u8"Npc", u8"");
	data.objName = ini.Get("State", u8"Obj", u8"");
	data.bgmName = ini.Get("State", u8"Bgm", u8"");

    data.characterIndex = ini.GetInteger("State", u8"Chr", -1);

    data.asfStyle = ini.GetColor("Option", u8"AsfStyle", 0xFFFFFF);
    data.mpcStyle = ini.GetColor("Option", u8"MpcStyle", 0xFFFFFF);
    
    data.mainLum = ini.GetInteger("Option", u8"MainLum", 31);
	data.fadeLum = ini.GetInteger("Option", u8"FadeLum", 31);
	data.mapTime = ini.GetInteger("Option", u8"mapTime", mtDay);
	data.snowShow = ini.GetBoolean("Option", u8"SnowShow", false);
	data.rainShow = ini.GetBoolean("Option", u8"RainShow", false);
	data.NPCAI = ini.GetBoolean("Option", u8"NPCAI", true);
	data.canInput = ini.GetBoolean("Option", u8"CanInput", true);
    
    data.water = ini.GetBoolean("Option", u8"Water", false);
    data.rainFile = ini.Get("Option", u8"RainFile", u8"");

}
void Global::save()
{
	std::string fileName = GLOBAL_INI;
	fileName = SAVE_CURRENT_FOLDER + fileName;
	INIReader ini(fileName);

	ini.SetInteger("Game", u8"Type", gameType);
	ini.SetBoolean("Game", u8"UseWav", useWav);

	ini.Set("State", u8"Map", data.mapName);
	ini.Set("State", u8"Npc", data.npcName);
	ini.Set("State", u8"Obj", data.objName);
	ini.Set("State", u8"Bgm", data.bgmName);
    
    ini.SetInteger("State", u8"Chr", data.characterIndex);
    
    ini.SetColor("Option", u8"AsfStyle", data.asfStyle);
    ini.SetColor("Option", u8"MpcStyle", data.mpcStyle);

	ini.SetInteger("Option", u8"MainLum", data.mainLum);
	ini.SetInteger("Option", u8"FadeLum", data.fadeLum);
	ini.SetInteger("Option", u8"mapTime", data.mapTime);
	ini.SetBoolean("Option", u8"SnowShow", data.snowShow);
	ini.SetBoolean("Option", u8"RainShow", data.rainShow);
	ini.SetBoolean("Option", u8"NPCAI", data.NPCAI);
	ini.SetBoolean("Option", u8"CanInput", data.canInput);
    
    ini.SetBoolean("Option", u8"Water", data.water);
    ini.Set("Option", u8"RainFile", data.rainFile);

	ini.saveToFile(fileName);
}
/*
Global * Global::getInstance()
{
	return this_;
}
*/
