#include "Goods.h"



Goods::Goods()
{
	reset();
}


Goods::~Goods()
{
}

_shared_imp Goods::createGoodsImage()
{
	if (image == u8"")
	{
		return nullptr;
	}
	std::string fileName = GOODS_RES_FOLDER + image;
	return IMP::createIMPImage(fileName);
}

_shared_imp Goods::createGoodsIcon()
{
	if (icon == u8"")
	{
		return nullptr;
	}
	std::string fileName = GOODS_RES_FOLDER + icon;
	return IMP::createIMPImage(fileName);
}

void Goods::reset()
{
	name = u8"";
	kind = gkNormal;
	cost = 0;
	intro = u8"";
	effect = u8"";
	image = u8"";
	icon = u8"";
	part = u8"";
	script = u8"";
	lifeMax = 0;
	thewMax = 0;
	manaMax = 0;
	attack = 0;
	defend = 0;
	evade = 0;
}

void Goods::initFromIni(const std::string& fileName)
{
	reset();
	std::string iniName = INI_GOODS_FOLDER;
	iniName += fileName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(iniName, s);
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		std::string section = u8"Init";
		name = ini.Get(section, u8"Name", u8"");
		kind = ini.GetInteger(section, u8"Kind", gkNormal);
		cost = ini.GetInteger(section, u8"Cost", 0);
		intro = ini.Get(section, u8"Intro", u8"");
		effect = ini.Get(section, u8"Effect", u8"");
		image = ini.Get(section, u8"Image", u8"");
		icon = ini.Get(section, u8"Icon", u8"");
		part = ini.Get(section, u8"Part", u8"");
		script = ini.Get(section, u8"script", u8"");
		lifeMax = ini.GetInteger(section, u8"LifeMax", 0);
		thewMax = ini.GetInteger(section, u8"ThewMax", 0);
		manaMax = ini.GetInteger(section, u8"ManaMax", 0);
		life = ini.GetInteger(section, u8"Life", 0);
		thew = ini.GetInteger(section, u8"Thew", 0);
		mana = ini.GetInteger(section, u8"Mana", 0);
		attack = ini.GetInteger(section, u8"Attack", 0);
		defend = ini.GetInteger(section, u8"Defend", 0);
		evade = ini.GetInteger(section, u8"Evade", 0);
	}

}
