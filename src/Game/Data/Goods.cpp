#include "Goods.h"
#include "ImageResourcePathResolver.h"
#include "../../File/INIReader.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <random>
#include <sstream>

namespace
{
std::string trimAscii(std::string value)
{
	size_t begin = 0;
	while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n'))
	{
		begin++;
	}
	size_t end = value.size();
	while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
	{
		end--;
	}
	return value.substr(begin, end - begin);
}

std::vector<std::string> splitUserNames(const std::string& value)
{
	std::vector<std::string> result;
	std::stringstream stream(value);
	std::string item;
	while (std::getline(stream, item, ','))
	{
		item = trimAscii(item);
		if (!item.empty())
		{
			result.push_back(item);
		}
	}
	return result;
}

std::string toLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return value;
}

bool parseStrictInteger(const std::string& text, int& value)
{
	std::string trimmed = trimAscii(text);
	if (trimmed.empty())
	{
		return false;
	}
	char* end = nullptr;
	long parsed = std::strtol(trimmed.c_str(), &end, 0);
	if (end == trimmed.c_str() || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX)
	{
		return false;
	}
	value = static_cast<int>(parsed);
	return true;
}

int randomIntegerInclusive(int minValue, int maxValue)
{
	if (minValue > maxValue)
	{
		std::swap(minValue, maxValue);
	}
	static thread_local std::mt19937 generator(std::random_device{}());
	std::uniform_int_distribution<int> distribution(minValue, maxValue);
	return distribution(generator);
}

std::vector<std::string> splitCommaSeparated(const std::string& value)
{
	std::vector<std::string> result;
	std::stringstream stream(value);
	std::string item;
	while (std::getline(stream, item, ','))
	{
		result.push_back(trimAscii(item));
	}
	return result;
}

bool parseRandomIntegerMax(const std::string& rawValue, int defaultValue, int& value, bool& isRandom)
{
	isRandom = false;
	std::string valueText = trimAscii(rawValue);
	if (valueText.empty())
	{
		value = defaultValue;
		return false;
	}

	size_t rangeSeparator = valueText.find('>');
	if (rangeSeparator != std::string::npos)
	{
		int start = 0;
		int end = 0;
		if (!parseStrictInteger(valueText.substr(0, rangeSeparator), start) ||
			!parseStrictInteger(valueText.substr(rangeSeparator + 1), end))
		{
			value = defaultValue;
			return false;
		}
		if (start > end)
		{
			std::swap(start, end);
		}
		value = end;
		isRandom = true;
		return true;
	}

	if (valueText.find(',') != std::string::npos)
	{
		auto items = splitCommaSeparated(valueText);
		int parsed = defaultValue;
		bool anyValue = false;
		for (const auto& item : items)
		{
			if (item.empty())
			{
				continue;
			}
			if (!parseStrictInteger(item, parsed))
			{
				value = defaultValue;
				return false;
			}
			anyValue = true;
		}
		value = anyValue ? parsed : defaultValue;
		isRandom = anyValue;
		return anyValue;
	}

	if (parseStrictInteger(valueText, value))
	{
		return true;
	}
	value = defaultValue;
	return false;
}

int selectRandomIntegerValue(const std::string& rawValue, int defaultValue)
{
	std::string valueText = trimAscii(rawValue);
	if (valueText.empty())
	{
		return defaultValue;
	}

	size_t rangeSeparator = valueText.find('>');
	if (rangeSeparator != std::string::npos)
	{
		int start = 0;
		int end = 0;
		if (parseStrictInteger(valueText.substr(0, rangeSeparator), start) &&
			parseStrictInteger(valueText.substr(rangeSeparator + 1), end))
		{
			return randomIntegerInclusive(start, end);
		}
		return defaultValue;
	}

	if (valueText.find(',') != std::string::npos)
	{
		std::vector<int> values;
		for (const auto& item : splitCommaSeparated(valueText))
		{
			if (item.empty())
			{
				continue;
			}
			int parsed = 0;
			if (!parseStrictInteger(item, parsed))
			{
				return defaultValue;
			}
			values.push_back(parsed);
		}
		if (!values.empty())
		{
			return values[static_cast<size_t>(randomIntegerInclusive(0, static_cast<int>(values.size()) - 1))];
		}
	}

	int value = defaultValue;
	return parseStrictInteger(valueText, value) ? value : defaultValue;
}

struct RandomStringItem
{
	std::string value;
	int weight = 1;
};

std::vector<RandomStringItem> parseRandomStringItems(const std::string& rawValue)
{
	std::vector<RandomStringItem> items;
	if (rawValue.find(',') == std::string::npos)
	{
		return items;
	}
	for (const auto& rawItem : splitCommaSeparated(rawValue))
	{
		if (rawItem.empty())
		{
			continue;
		}
		RandomStringItem item;
		item.value = rawItem;
		item.weight = 1;
		if (rawItem.size() > 2 && rawItem.back() == ']')
		{
			size_t start = rawItem.rfind('[');
			if (start != std::string::npos && start + 1 < rawItem.size() - 1)
			{
				int parsedWeight = 0;
				if (parseStrictInteger(rawItem.substr(start + 1, rawItem.size() - start - 2), parsedWeight) && parsedWeight > 0)
				{
					item.value = trimAscii(rawItem.substr(0, start));
					item.weight = parsedWeight;
				}
			}
		}
		items.push_back(item);
	}
	return items;
}

std::string selectRandomStringValue(const std::string& rawValue)
{
	auto items = parseRandomStringItems(rawValue);
	if (items.empty())
	{
		return rawValue;
	}
	int totalWeight = 0;
	for (const auto& item : items)
	{
		totalWeight += std::max(1, item.weight);
	}
	int selectedWeight = randomIntegerInclusive(1, totalWeight);
	int currentWeight = 0;
	for (const auto& item : items)
	{
		currentWeight += std::max(1, item.weight);
		if (selectedWeight <= currentWeight)
		{
			return item.value;
		}
	}
	return items.back().value;
}

std::string sanitizeGeneratedFileNamePart(std::string value)
{
	std::string result;
	for (char ch : value)
	{
		unsigned char c = static_cast<unsigned char>(ch);
		if (std::isalnum(c) || ch == '-' || ch == '_' || ch == '.')
		{
			result.push_back(ch);
		}
		else if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
		{
			result.push_back('_');
		}
		else
		{
			result.push_back(ch);
		}
	}
	return result;
}

void setGoodsIntegerField(Goods& goods, const std::string& key, int value)
{
	std::string normalizedKey = toLowerAscii(key);
	if (normalizedKey == "cost") goods.cost = value;
	else if (normalizedKey == "sellprice") goods.sellPrice = value;
	else if (normalizedKey == "effecttype") goods.effectType = value;
	else if (normalizedKey == "specialeffect") goods.specialEffect = value;
	else if (normalizedKey == "specialeffectvalue") goods.specialEffectValue = value;
	else if (normalizedKey == "fighterfriendhasdrugeffect") goods.fighterFriendHasDrugEffect = value;
	else if (normalizedKey == "followpartnerhasdrugeffect") goods.followPartnerHasDrugEffect = value;
	else if (normalizedKey == "coldmilliseconds") goods.coldMilliSeconds = value > 0 ? static_cast<UTime>(value) : 0;
	else if (normalizedKey == "minuserlevel") goods.minUserLevel = value;
	else if (normalizedKey == "sex") goods.sex = value;
	else if (normalizedKey == "changemovespeedpercent") goods.changeMoveSpeedPercent = value;
	else if (normalizedKey == "addmagiceffectpercent") goods.addMagicEffectPercent = value;
	else if (normalizedKey == "addmagiceffectamount") goods.addMagicEffectAmount = value;
	else if (normalizedKey == "noneedtoequip") goods.noNeedToEquip = value;
	else if (normalizedKey == "magicdirectionwhenbeattacked") goods.magicDirectionWhenBeAttacked = value;
	else if (normalizedKey == "lifemax") goods.lifeMax = value;
	else if (normalizedKey == "thewmax") goods.thewMax = value;
	else if (normalizedKey == "manamax") goods.manaMax = value;
	else if (normalizedKey == "life") goods.life = value;
	else if (normalizedKey == "thew") goods.thew = value;
	else if (normalizedKey == "mana") goods.mana = value;
	else if (normalizedKey == "attack") goods.attack = value;
	else if (normalizedKey == "attack2") goods.attack2 = value;
	else if (normalizedKey == "attack3") goods.attack3 = value;
	else if (normalizedKey == "defend") goods.defend = value;
	else if (normalizedKey == "defend2") goods.defend2 = value;
	else if (normalizedKey == "defend3") goods.defend3 = value;
	else if (normalizedKey == "evade") goods.evade = value;
}

void setGoodsStringField(Goods& goods, const std::string& key, const std::string& value)
{
	std::string normalizedKey = toLowerAscii(key);
	if (normalizedKey == "flyini") goods.flyIni = value;
	else if (normalizedKey == "flyini2") goods.flyIni2 = value;
	else if (normalizedKey == "magiciniwhenuse") goods.magicIniWhenUse = value;
	else if (normalizedKey == "replacemagic") goods.replaceMagic = value;
	else if (normalizedKey == "usereplacemagic") goods.useReplaceMagic = value;
	else if (normalizedKey == "magictousewhenbeattacked") goods.magicToUseWhenBeAttacked = value;
}

void writeIntegerField(INIReader& ini, const std::string& section, const std::string& key, int value)
{
	ini.SetInteger(section, key, value);
}

int clampNonNegativeInteger(int64_t value)
{
	if (value <= 0)
	{
		return 0;
	}
	if (value > INT_MAX)
	{
		return INT_MAX;
	}
	return static_cast<int>(value);
}
}


Goods::Goods()
{
	reset();
}


Goods::~Goods()
{
}

_shared_imp Goods::createGoodsImage()
{
	if (image.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		image,
		"goods",
		GOODS_RES_FOLDER_ASF,
		GOODS_RES_FOLDER));
}

_shared_imp Goods::createGoodsIcon()
{
	if (icon.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		icon,
		"goods",
		GOODS_RES_FOLDER_ASF,
		GOODS_RES_FOLDER));
}

void Goods::reset()
{
	loadSucceeded = false;
	sourceFileName = "";
	name = "";
	kind = gkNormal;
	cost = 0;
	intro = "";
	effect = "";
	image = "";
	icon = "";
	part = "";
	script = "";
	effectType = 0;
	sellPrice = 0;
	specialEffect = 0;
	specialEffectValue = 1;
	fighterFriendHasDrugEffect = 0;
	followPartnerHasDrugEffect = 0;
	coldMilliSeconds = 0;
	userNames.clear();
	minUserLevel = 0;
	sex = 0;
	changeMoveSpeedPercent = 0;
	addMagicEffectPercent = 0;
	addMagicEffectAmount = 0;
	addMagicEffectName = "";
	addMagicEffectType = "";
	noNeedToEquip = 0;
	magicName = "";
	magicIniWhenUse = "";
	replaceMagic = "";
	useReplaceMagic = "";
	flyIni = "";
	flyIni2 = "";
	magicToUseWhenBeAttacked = "";
	magicDirectionWhenBeAttacked = 0;
	lifeMax = 0;
	thewMax = 0;
	manaMax = 0;
	life = 0;
	thew = 0;
	mana = 0;
	attack = 0;
	attack2 = 0;
	attack3 = 0;
	defend = 0;
	defend2 = 0;
	defend3 = 0;
	evade = 0;
	randomIntegerSources.clear();
	randomStringSources.clear();
}

void Goods::initFromIni(const std::string& fileName)
{
	reset();
	sourceFileName = fileName;
	std::unique_ptr<char[]> s;
	const std::string runtimeGoodsFile =
		SaveFileManager::CurrentPath() + fileName;
	int len = 0;
	if (File::fileExist(runtimeGoodsFile))
	{
		len = File::readFile(runtimeGoodsFile, s);
	}
	if (s == nullptr || len <= 0)
	{
		std::string iniName = INI_GOODS_FOLDER;
		iniName += fileName;
		len = File::readFile(iniName, s);
	}
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		if (ini.ParseError() != 0)
		{
			return;
		}
		std::string section = "Init";
		auto readRandomInteger = [&](const std::string& key, int defaultValue)
		{
			bool isRandom = false;
			int value = defaultValue;
			std::string rawValue = ini.Get(section, key, "");
			parseRandomIntegerMax(rawValue, defaultValue, value, isRandom);
			if (isRandom)
			{
				randomIntegerSources[key] = rawValue;
			}
			return value;
		};
		auto readRandomString = [&](const std::string& key)
		{
			std::string rawValue = ini.Get(section, key, "");
			if (!parseRandomStringItems(rawValue).empty())
			{
				randomStringSources[key] = rawValue;
			}
			return rawValue;
		};
		name = ini.Get(section, "Name", "");
		kind = ini.GetInteger(section, "Kind", gkNormal);
		cost = readRandomInteger("Cost", 0);
		sellPrice = readRandomInteger("SellPrice", 0);
		intro = ini.Get(section, "Intro", "");
		effect = ini.Get(section, "Effect", "");
		image = ini.Get(section, "Image", "");
		icon = ini.Get(section, "Icon", "");
		part = ini.Get(section, "Part", "");
		script = ini.Get(section, "Script", "");
		if (script.empty())
		{
			script = ini.Get(section, "script", "");
		}
		effectType = readRandomInteger("EffectType", 0);
		specialEffect = readRandomInteger("SpecialEffect", 0);
		specialEffectValue = readRandomInteger("SpecialEffectValue", 1);
		fighterFriendHasDrugEffect = readRandomInteger("FighterFriendHasDrugEffect", 0);
		followPartnerHasDrugEffect = readRandomInteger("FollowPartnerHasDrugEffect", 0);
		int coldTime = readRandomInteger("ColdMilliSeconds", 0);
		coldMilliSeconds = coldTime > 0 ? static_cast<UTime>(coldTime) : 0;
		userNames = splitUserNames(ini.Get(section, "User", ""));
		minUserLevel = readRandomInteger("MinUserLevel", 0);
		sex = readRandomInteger("Sex", 0);
		changeMoveSpeedPercent = readRandomInteger("ChangeMoveSpeedPercent", 0);
		addMagicEffectPercent = readRandomInteger("AddMagicEffectPercent", 0);
		addMagicEffectAmount = readRandomInteger("AddMagicEffectAmount", 0);
		addMagicEffectName = ini.Get(section, "AddMagicEffectName", "");
		addMagicEffectType = ini.Get(section, "AddMagicEffectType", "");
		noNeedToEquip = readRandomInteger("NoNeedToEquip", 0);
		magicName = ini.Get(section, "MagicName", "");
		magicIniWhenUse = readRandomString("MagicIniWhenUse");
		replaceMagic = readRandomString("ReplaceMagic");
		useReplaceMagic = readRandomString("UseReplaceMagic");
		flyIni = readRandomString("FlyIni");
		flyIni2 = readRandomString("FlyIni2");
		magicToUseWhenBeAttacked = readRandomString("MagicToUseWhenBeAttacked");
		magicDirectionWhenBeAttacked = readRandomInteger("MagicDirectionWhenBeAttacked", 0);
		lifeMax = readRandomInteger("LifeMax", 0);
		thewMax = readRandomInteger("ThewMax", 0);
		manaMax = readRandomInteger("ManaMax", 0);
		life = readRandomInteger("Life", 0);
		thew = readRandomInteger("Thew", 0);
		mana = readRandomInteger("Mana", 0);
		attack = readRandomInteger("Attack", 0);
		attack2 = readRandomInteger("Attack2", 0);
		attack3 = readRandomInteger("Attack3", 0);
		defend = readRandomInteger("Defend", 0);
		defend2 = readRandomInteger("Defend2", 0);
		defend3 = readRandomInteger("Defend3", 0);
		evade = readRandomInteger("Evade", 0);
		loadSucceeded = true;
	}

}

bool Goods::isAllowedForUserName(const std::string& userName) const
{
	if (userNames.empty())
	{
		return true;
	}
	for (const auto& name : userNames)
	{
		if (name == userName)
		{
			return true;
		}
	}
	return false;
}

bool Goods::isAllowedForAnyUserName(const std::vector<std::string>& userNameCandidates) const
{
	if (userNames.empty())
	{
		return true;
	}
	for (const auto& userName : userNameCandidates)
	{
		if (!userName.empty() && isAllowedForUserName(userName))
		{
			return true;
		}
	}
	return false;
}

std::string Goods::userRestrictionText() const
{
	std::string text;
	for (size_t i = 0; i < userNames.size(); i++)
	{
		if (i > 0)
		{
			text += "，";
		}
		text += userNames[i];
	}
	return text;
}

bool Goods::hasRandomAttributes() const
{
	return !randomIntegerSources.empty() || !randomStringSources.empty();
}

Goods Goods::createNonRandomInstance() const
{
	Goods instance = *this;
	instance.randomIntegerSources.clear();
	instance.randomStringSources.clear();

	std::vector<std::pair<std::string, std::string>> selectedValues;
	for (const auto& item : randomIntegerSources)
	{
		int selectedValue = selectRandomIntegerValue(item.second, 0);
		setGoodsIntegerField(instance, item.first, selectedValue);
		selectedValues.push_back({ item.first, std::to_string(selectedValue) });
	}
	for (const auto& item : randomStringSources)
	{
		std::string selectedValue = selectRandomStringValue(item.second);
		setGoodsStringField(instance, item.first, selectedValue);
		selectedValues.push_back({ item.first, selectedValue });
	}
	std::sort(selectedValues.begin(), selectedValues.end(),
		[](const auto& left, const auto& right)
		{
			return left.first < right.first;
		});

	std::string baseName = sourceFileName;
	std::string extension;
	size_t slashPosition = baseName.find_last_of("\\/");
	std::string directoryPrefix;
	if (slashPosition != std::string::npos)
	{
		directoryPrefix = baseName.substr(0, slashPosition + 1);
		baseName = baseName.substr(slashPosition + 1);
	}
	size_t extensionPosition = baseName.find_last_of('.');
	if (extensionPosition != std::string::npos)
	{
		extension = baseName.substr(extensionPosition);
		baseName = baseName.substr(0, extensionPosition);
	}
	for (const auto& selectedValue : selectedValues)
	{
		baseName += selectedValue.first;
		baseName += sanitizeGeneratedFileNamePart(selectedValue.second);
	}
	instance.sourceFileName = directoryPrefix + baseName + extension;
	return instance;
}

bool Goods::saveRuntimeIni(const std::string& fileName) const
{
	if (fileName.empty())
	{
		return false;
	}
	INIReader ini;
	const std::string section = "Init";
	if (!name.empty()) ini.Set(section, "Name", name);
	ini.SetInteger(section, "Kind", kind);
	writeIntegerField(ini, section, "Cost", cost);
	writeIntegerField(ini, section, "SellPrice", sellPrice);
	if (!intro.empty()) ini.Set(section, "Intro", intro);
	if (!effect.empty()) ini.Set(section, "Effect", effect);
	if (!image.empty()) ini.Set(section, "Image", image);
	if (!icon.empty()) ini.Set(section, "Icon", icon);
	if (!part.empty()) ini.Set(section, "Part", part);
	if (!script.empty()) ini.Set(section, "Script", script);
	writeIntegerField(ini, section, "EffectType", effectType);
	writeIntegerField(ini, section, "SpecialEffect", specialEffect);
	writeIntegerField(ini, section, "SpecialEffectValue", specialEffectValue);
	writeIntegerField(ini, section, "FighterFriendHasDrugEffect", fighterFriendHasDrugEffect);
	writeIntegerField(ini, section, "FollowPartnerHasDrugEffect", followPartnerHasDrugEffect);
	writeIntegerField(ini, section, "ColdMilliSeconds", static_cast<int>(coldMilliSeconds));
	if (!userNames.empty())
	{
		std::string users;
		for (size_t i = 0; i < userNames.size(); i++)
		{
			if (i > 0) users += ",";
			users += userNames[i];
		}
		ini.Set(section, "User", users);
	}
	writeIntegerField(ini, section, "MinUserLevel", minUserLevel);
	writeIntegerField(ini, section, "Sex", sex);
	writeIntegerField(ini, section, "ChangeMoveSpeedPercent", changeMoveSpeedPercent);
	writeIntegerField(ini, section, "AddMagicEffectPercent", addMagicEffectPercent);
	writeIntegerField(ini, section, "AddMagicEffectAmount", addMagicEffectAmount);
	if (!addMagicEffectName.empty()) ini.Set(section, "AddMagicEffectName", addMagicEffectName);
	if (!addMagicEffectType.empty()) ini.Set(section, "AddMagicEffectType", addMagicEffectType);
	writeIntegerField(ini, section, "NoNeedToEquip", noNeedToEquip);
	if (!magicName.empty()) ini.Set(section, "MagicName", magicName);
	if (!magicIniWhenUse.empty()) ini.Set(section, "MagicIniWhenUse", magicIniWhenUse);
	if (!replaceMagic.empty()) ini.Set(section, "ReplaceMagic", replaceMagic);
	if (!useReplaceMagic.empty()) ini.Set(section, "UseReplaceMagic", useReplaceMagic);
	if (!flyIni.empty()) ini.Set(section, "FlyIni", flyIni);
	if (!flyIni2.empty()) ini.Set(section, "FlyIni2", flyIni2);
	if (!magicToUseWhenBeAttacked.empty()) ini.Set(section, "MagicToUseWhenBeAttacked", magicToUseWhenBeAttacked);
	writeIntegerField(ini, section, "MagicDirectionWhenBeAttacked", magicDirectionWhenBeAttacked);
	writeIntegerField(ini, section, "LifeMax", lifeMax);
	writeIntegerField(ini, section, "ThewMax", thewMax);
	writeIntegerField(ini, section, "ManaMax", manaMax);
	writeIntegerField(ini, section, "Life", life);
	writeIntegerField(ini, section, "Thew", thew);
	writeIntegerField(ini, section, "Mana", mana);
	writeIntegerField(ini, section, "Attack", attack);
	writeIntegerField(ini, section, "Attack2", attack2);
	writeIntegerField(ini, section, "Attack3", attack3);
	writeIntegerField(ini, section, "Defend", defend);
	writeIntegerField(ini, section, "Defend2", defend2);
	writeIntegerField(ini, section, "Defend3", defend3);
	writeIntegerField(ini, section, "Evade", evade);
	return ini.saveToFile(
		SaveFileManager::CurrentPath() + fileName);
}

int Goods::getRawCost() const
{
	if (cost > 0)
	{
		return cost;
	}
	if (kind == gkDrug)
	{
		const int64_t rawCost = static_cast<int64_t>(thew) * 4 +
			static_cast<int64_t>(life) * 2 + static_cast<int64_t>(mana) * 2;
		return clampNonNegativeInteger(rawCost * (effectType == 0 ? 1 : 2));
	}
	if (kind == gkEquipment)
	{
		if (noNeedToEquip > 0)
		{
			return 0;
		}
		const int64_t rawCost = static_cast<int64_t>(attack) * 20 +
			static_cast<int64_t>(attack2) * 20 + static_cast<int64_t>(attack3) * 20 +
			static_cast<int64_t>(defend) * 20 + static_cast<int64_t>(defend2) * 20 +
			static_cast<int64_t>(defend3) * 20 + static_cast<int64_t>(evade) * 40 +
			static_cast<int64_t>(lifeMax) * 2 + static_cast<int64_t>(thewMax) * 3 +
			static_cast<int64_t>(manaMax) * 2;
		return clampNonNegativeInteger(rawCost * (effectType == 0 ? 1 : 2));
	}
	return 0;
}

int Goods::getBuyPrice(int buyPercent) const
{
	return clampNonNegativeInteger(
		static_cast<int64_t>(getRawCost()) * std::max(0, buyPercent) / 100);
}

int Goods::getSellPrice(int recyclePercent) const
{
	const int rawSellPrice = sellPrice > 0 ? sellPrice : getRawCost() / 2;
	return clampNonNegativeInteger(
		static_cast<int64_t>(rawSellPrice) * std::max(0, recyclePercent) / 100);
}

bool Goods::hasExplicitSellPrice() const
{
	return sellPrice > 0;
}
