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
	virtual ~Goods();

	_shared_imp createGoodsImage();
	_shared_imp createGoodsIcon();

	void reset();

	void initFromIni(const std::string & fileName);

	std::string name = u8"";
	int kind = gkNormal;
	std::string intro = u8"";
	std::string effect = u8"";
	std::string image = u8"";
	std::string icon = u8"";
	std::string part = u8"";
	std::string script = u8"";
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

