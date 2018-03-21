#pragma once
#include <string>
#include "../GameTypes.h"

enum GoodsKind
{
	gkDrug = 0,
	gkEquipment = 1,
	gkNormal = 2,
};

class Goods
{
public:
	Goods();
	~Goods();

	IMPImage * createGoodsImage();
	IMPImage * createGoodsIcon();

	void reset();

	void initFromIni(const std::string & fileName);

	std::string name = "";
	int kind = gkNormal;
	std::string intro = "";
	std::string effect = "";
	std::string image = "";
	std::string icon = "";
	std::string part = "";
	std::string script = "";
	int cost = 0;
	int lifeMax = 0;
	int thewMax = 0;
	int manaMax = 0;
	int life = 0;
	int thew = 0;
	int mana = 0;
	int attack = 0;
	int defend = 0;
	int evade = 0;
	
};

