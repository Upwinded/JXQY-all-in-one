#include "Goods.h"



Goods::Goods()
{
	reset();
}


Goods::~Goods()
{
}

IMPImage * Goods::createGoodsImage()
{
	if (image == "")
	{
		return nullptr;
	}
	std::string fileName = GOODS_RES_FOLDER + image;
	IMPImage * img = IMP::createIMPImage(fileName);
	return img;
}

IMPImage * Goods::createGoodsIcon()
{
	if (icon == "")
	{
		return nullptr;
	}
	std::string fileName = GOODS_RES_FOLDER + icon;
	IMPImage * img = IMP::createIMPImage(fileName);
	return img;
}

void Goods::reset()
{
	name = "";
	kind = gkNormal;
	cost = 0;
	intro = "";
	effect = "";
	image = "";
	icon = "";
	part = "";
	script = "";
	lifeMax = 0;
	thewMax = 0;
	manaMax = 0;
	attack = 0;
	defend = 0;
	evade = 0;
}

void Goods::initFromIni(const std::string & fileName)
{
	reset();
	std::string iniName = GOODS_INI_FOLDER;
	iniName += fileName;
	char * s = NULL;
	int len = PakFile::readFile(iniName, &s);
	if (s != NULL && len > 0)
	{
		INIReader * ini = new INIReader(s);
		delete[] s;
		std::string section = "Init";
		name = ini->Get(section, "Name", "");
		kind = ini->GetInteger(section, "Kind", gkNormal);
		cost = ini->GetInteger(section, "Cost", 0);
		intro = ini->Get(section, "Intro", "");
		effect = ini->Get(section, "Effect", "");
		image = ini->Get(section, "Image", "");
		icon = ini->Get(section, "Icon", "");
		part = ini->Get(section, "Part", "");
		script = ini->Get(section, "script", "");
		lifeMax = ini->GetInteger(section, "LifeMax", 0);
		thewMax = ini->GetInteger(section, "ThewMax", 0);
		manaMax = ini->GetInteger(section, "ManaMax", 0);
		life = ini->GetInteger(section, "Life", 0);
		thew = ini->GetInteger(section, "Thew", 0);
		mana = ini->GetInteger(section, "Mana", 0);
		attack = ini->GetInteger(section, "Attack", 0);
		defend = ini->GetInteger(section, "Defend", 0);
		evade = ini->GetInteger(section, "Evade", 0);

		delete ini;
	}

}
