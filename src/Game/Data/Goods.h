#pragma once
#include <string>
#include <vector>
#include <map>
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
	bool isAllowedForUserName(const std::string& userName) const;
	bool isAllowedForAnyUserName(const std::vector<std::string>& userNameCandidates) const;
	std::string userRestrictionText() const;
	bool hasRandomAttributes() const;
	Goods createNonRandomInstance() const;
	bool saveRuntimeIni(const std::string& fileName) const;
	int getRawCost() const;
	int getBuyPrice(int buyPercent = 100) const;
	int getSellPrice(int recyclePercent = 100) const;
	bool hasExplicitSellPrice() const;

	bool loadSucceeded = false;
	std::string sourceFileName = "";
	std::string name = "";
	int kind = gkNormal;
	std::string intro = "";
	std::string effect = "";
	std::string image = "";
	std::string icon = "";
	std::string part = "";
	std::string script = "";
	int cost = 0;
	int sellPrice = 0;
	int effectType = 0;
	int specialEffect = 0;
	int specialEffectValue = 1;
	int fighterFriendHasDrugEffect = 0;
	int followPartnerHasDrugEffect = 0;
	UTime coldMilliSeconds = 0;
	std::vector<std::string> userNames;
	int minUserLevel = 0;
	int sex = 0;
	int changeMoveSpeedPercent = 0;
	int addMagicEffectPercent = 0;
	int addMagicEffectAmount = 0;
	std::string addMagicEffectName = "";
	std::string addMagicEffectType = "";
	int noNeedToEquip = 0;
	std::string magicName = "";
	std::string magicIniWhenUse = "";
	std::string replaceMagic = "";
	std::string useReplaceMagic = "";
	std::string flyIni = "";
	std::string flyIni2 = "";
	std::string magicToUseWhenBeAttacked = "";
	int magicDirectionWhenBeAttacked = 0;
	int lifeMax = 0;
	int thewMax = 0;
	int manaMax = 0;
	int life = 0;
	int thew = 0;
	int mana = 0;
	int attack = 0;
	int attack2 = 0;
	int attack3 = 0;
	int defend = 0;
	int defend2 = 0;
	int defend3 = 0;
	int evade = 0;

private:
	std::map<std::string, std::string> randomIntegerSources;
	std::map<std::string, std::string> randomStringSources;
	
};
