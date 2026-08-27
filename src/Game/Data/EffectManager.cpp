#include "EffectManager.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"
#include "NPC.h"
#include "TimeStopUpdateGate.h"
#include <algorithm>

namespace
{
int clampMagicLevelForEffectManager(int level)
{
	if (level < 1)
	{
		return 1;
	}
	if (level > MAGIC_MAX_LEVEL)
	{
		return MAGIC_MAX_LEVEL;
	}
	return level;
}

int getPrimaryDamageForMagic(std::shared_ptr<Magic> magic, int level, std::shared_ptr<GameElement> user)
{
	return Magic::calculatePrimaryEffectAmount(magic, user, level);
}

int getEvadeForMagicUser(std::shared_ptr<GameElement> user)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (caster == nullptr)
	{
		return 0;
	}
	return caster->getEvade();
}

constexpr int MaxPersistedEffectReferences = 4096;

std::string getDetachedCasterSection(int index)
{
	return convert::formatString("DetachedCaster%03d", index + 1);
}

void saveMagicEffectBonusMap(
	INIReader& ini,
	const std::string& section,
	const std::string& prefix,
	const std::unordered_map<std::string, NPCMagicEffectBonus>& bonuses)
{
	ini.SetInteger(section, prefix + "Count", static_cast<long>(bonuses.size()));
	std::vector<std::string> keys;
	keys.reserve(bonuses.size());
	for (const auto& item : bonuses)
	{
		keys.push_back(item.first);
	}
	std::sort(keys.begin(), keys.end());
	int index = 0;
	for (const auto& key : keys)
	{
		const auto& bonus = bonuses.at(key);
		std::string itemPrefix = prefix + std::to_string(++index);
		ini.Set(section, itemPrefix + "Key", key);
		ini.SetInteger(section, itemPrefix + "Percent", bonus.percent);
		ini.SetInteger(section, itemPrefix + "Amount", bonus.amount);
	}
}

void loadMagicEffectBonusMap(
	const INIReader& ini,
	const std::string& section,
	const std::string& prefix,
	std::unordered_map<std::string, NPCMagicEffectBonus>& bonuses)
{
	bonuses.clear();
	int count = std::clamp(
		static_cast<int>(ini.GetInteger(section, prefix + "Count", 0)),
		0,
		MaxPersistedEffectReferences);
	for (int i = 0; i < count; i++)
	{
		std::string itemPrefix = prefix + std::to_string(i + 1);
		std::string key = ini.Get(section, itemPrefix + "Key", "");
		if (key.empty())
		{
			continue;
		}
		NPCMagicEffectBonus bonus;
		bonus.percent = static_cast<int>(ini.GetInteger(section, itemPrefix + "Percent", 0));
		bonus.amount = static_cast<int>(ini.GetInteger(section, itemPrefix + "Amount", 0));
		bonuses[key] = bonus;
	}
}

void saveDetachedEffectCaster(INIReader& ini, const std::string& section, const std::shared_ptr<NPC>& caster)
{
	if (caster == nullptr)
	{
		return;
	}
	caster->saveToIni(&ini, section);
	ini.SetBoolean(section, "DetachedEffectCaster", true);
	ini.SetInteger(section, "RuntimeKind", caster->kind);
	ini.SetInteger(section, "RuntimeRelation", caster->relation);
	ini.SetInteger(section, "RuntimeGroup", caster->group);
	ini.SetInteger(section, "RuntimeLevel", caster->level);
	Point casterPosition = caster->getPosition();
	PointEx casterOffset = caster->getOffset();
	ini.SetInteger(section, "RuntimeMapX", casterPosition.x);
	ini.SetInteger(section, "RuntimeMapY", casterPosition.y);
	ini.SetReal(section, "RuntimeOffsetX", casterOffset.x);
	ini.SetReal(section, "RuntimeOffsetY", casterOffset.y);
	ini.SetInteger(section, "RuntimeDirection", caster->direction);
	ini.SetInteger(section, "EffectiveAttack", caster->getAttack());
	ini.SetInteger(section, "EffectiveAttack2", caster->getAttack2());
	ini.SetInteger(section, "EffectiveAttack3", caster->getAttack3());
	ini.SetInteger(section, "EffectiveEvade", caster->getEvade());
	ini.SetInteger(section, "MagicEffectBonusPercent", caster->equipmentAddMagicEffectPercent);
	ini.SetInteger(section, "MagicEffectBonusAmount", caster->equipmentAddMagicEffectAmount);
	saveMagicEffectBonusMap(ini, section, "MagicEffectNameBonus", caster->equipmentAddMagicEffectByName);
	saveMagicEffectBonusMap(ini, section, "MagicEffectTypeBonus", caster->equipmentAddMagicEffectByType);

	ini.SetInteger(section, "ChangeMagicHitCountCount", static_cast<long>(caster->changeMagicHitCounts.size()));
	std::vector<std::string> changeMagicFiles;
	changeMagicFiles.reserve(caster->changeMagicHitCounts.size());
	for (const auto& item : caster->changeMagicHitCounts)
	{
		changeMagicFiles.push_back(item.first);
	}
	std::sort(changeMagicFiles.begin(), changeMagicFiles.end());
	int changeIndex = 0;
	for (const auto& magicFile : changeMagicFiles)
	{
		std::string prefix = "ChangeMagicHitCount" + std::to_string(++changeIndex);
		ini.Set(section, prefix + "MagicFile", magicFile);
		ini.SetInteger(section, prefix + "Value", caster->changeMagicHitCounts.at(magicFile));
	}
}

std::shared_ptr<NPC> loadDetachedEffectCaster(INIReader& source, const std::string& section)
{
	if (!source.GetBoolean(section, "DetachedEffectCaster", false))
	{
		return nullptr;
	}
	auto caster = std::make_shared<NPC>();
	caster->initFromIni(&source, section);
	caster->clearMagicRuntimeStates();
	caster->kind = static_cast<int>(source.GetInteger(section, "RuntimeKind", caster->kind));
	caster->relation = static_cast<int>(source.GetInteger(section, "RuntimeRelation", caster->relation));
	caster->originalRelationBeforeOppositeChange = caster->relation;
	caster->group = static_cast<int>(source.GetInteger(section, "RuntimeGroup", caster->group));
	caster->level = static_cast<int>(source.GetInteger(section, "RuntimeLevel", caster->level));
	Point casterPosition = caster->getPosition();
	casterPosition.x = static_cast<int>(source.GetInteger(section, "RuntimeMapX", casterPosition.x));
	casterPosition.y = static_cast<int>(source.GetInteger(section, "RuntimeMapY", casterPosition.y));
	caster->setPosition(casterPosition, false);
	PointEx casterOffset = caster->getOffset();
	casterOffset.x = source.GetReal(section, "RuntimeOffsetX", casterOffset.x);
	casterOffset.y = source.GetReal(section, "RuntimeOffsetY", casterOffset.y);
	caster->setOffset(casterOffset);
	caster->direction = static_cast<int>(source.GetInteger(section, "RuntimeDirection", caster->direction));
	caster->attack = static_cast<int>(source.GetInteger(section, "EffectiveAttack", caster->getAttack()))
		- caster->equipmentAttributes.attack;
	caster->attack2 = static_cast<int>(source.GetInteger(section, "EffectiveAttack2", caster->getAttack2()))
		- caster->equipmentAttributes.attack2;
	caster->attack3 = static_cast<int>(source.GetInteger(section, "EffectiveAttack3", caster->getAttack3()))
		- caster->equipmentAttributes.attack3;
	caster->evade = static_cast<int>(source.GetInteger(section, "EffectiveEvade", caster->getEvade()))
		- caster->equipmentAttributes.evade;
	caster->equipmentAddMagicEffectPercent = static_cast<int>(source.GetInteger(
		section,
		"MagicEffectBonusPercent",
		caster->equipmentAddMagicEffectPercent));
	caster->equipmentAddMagicEffectAmount = static_cast<int>(source.GetInteger(
		section,
		"MagicEffectBonusAmount",
		caster->equipmentAddMagicEffectAmount));
	loadMagicEffectBonusMap(source, section, "MagicEffectNameBonus", caster->equipmentAddMagicEffectByName);
	loadMagicEffectBonusMap(source, section, "MagicEffectTypeBonus", caster->equipmentAddMagicEffectByType);

	caster->changeMagicHitCounts.clear();
	int changeCount = std::clamp(
		static_cast<int>(source.GetInteger(section, "ChangeMagicHitCountCount", 0)),
		0,
		MaxPersistedEffectReferences);
	for (int i = 0; i < changeCount; i++)
	{
		std::string prefix = "ChangeMagicHitCount" + std::to_string(i + 1);
		std::string magicFile = source.Get(section, prefix + "MagicFile", "");
		if (!magicFile.empty())
		{
			caster->changeMagicHitCounts[magicFile] = std::max(
				0,
				static_cast<int>(source.GetInteger(section, prefix + "Value", 0)));
		}
	}
	caster->detachedEffectCaster = true;
	caster->isVisibleByVariable = false;
	caster->selecting = false;
	caster->clearCombatTargetMemory();
	return caster;
}
}


EffectManager::EffectManager()
{
	name = "EffectManager";
	setPriority(epEffectManager);
	effectList.resize(0);
	needArrangeChild = false;
	canDraw = false;
}

EffectManager::~EffectManager()
{
	freeResource();
}

bool EffectManager::isTimeStopperCandidate(std::shared_ptr<Effect> effect) const
{
	if (effect == nullptr)
	{
		return false;
	}
	// Candidate checks run more than once per frame. getResult() consumes the
	// flag and would let an exhausted stopper be rediscovered as active.
	return effect->isTimeStopper() && !(effect->result & erLifeExhaust);
}

std::shared_ptr<Effect> EffectManager::getActiveTimeStopperEffect()
{
	auto active = timeStopperEffect.lock();
	if (isTimeStopperCandidate(active))
	{
		return active;
	}

	timeStopperEffect.reset();
	for (auto& effect : effectList)
	{
		if (isTimeStopperCandidate(effect))
		{
			timeStopperEffect = effect;
			return effect;
		}
	}
	return nullptr;
}

std::shared_ptr<GameElement> EffectManager::getActiveTimeStopperUser()
{
	auto effect = getActiveTimeStopperEffect();
	if (effect == nullptr)
	{
		return nullptr;
	}
	return effect->user.lock();
}

bool EffectManager::hasActiveTimeStopper()
{
	return getActiveTimeStopperEffect() != nullptr;
}

bool EffectManager::isActiveTimeStopper(std::shared_ptr<Effect> effect)
{
	auto active = getActiveTimeStopperEffect();
	return active != nullptr && effect != nullptr && active.get() == effect.get();
}

void EffectManager::pauseAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		effectList[i]->setPaused(true);
	}
}

void EffectManager::resumeAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		effectList[i]->setPaused(false);
	}
}

void EffectManager::addEffect(std::shared_ptr<Effect> effect)
{
	if (effect.get() == nullptr)
	{
		return;
	}
	effectList.push_back(effect);
	addChild(effect);
	effect->initTime();
	if (isTimeStopperCandidate(effect) && getActiveTimeStopperEffect() == nullptr)
	{
		timeStopperEffect = effect;
	}
}

void EffectManager::deleteEffect(std::shared_ptr<Effect> effect)
{
	if (effect == nullptr)
	{
		return;
	}

	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effect == effectList[i])
		{
			effect->releaseRuntimeBindings();
			effectList.erase(effectList.begin() + i);
			removeChild(effect);
			if (isActiveTimeStopper(effect))
			{
				timeStopperEffect.reset();
			}
			break;
		}
	}
}

void EffectManager::clearEffect()
{
	std::vector<std::shared_ptr<Effect>> newList;
	newList.resize(0);
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
		{
			unsigned int ret = effectList[i]->getResult();
			if (ret & erLifeExhaust)
			{
				if (isActiveTimeStopper(effectList[i]))
				{
					timeStopperEffect.reset();
				}
				effectList[i]->releaseRuntimeBindings();
				removeChild(effectList[i]);
				effectList[i] = nullptr;
			}
			else
			{
				newList.push_back(effectList[i]);
			}
		}
	}
	effectList = newList;
}

void EffectManager::addTrailMagic(
	std::shared_ptr<Magic> magic,
	std::shared_ptr<GameElement> user,
	int level,
	int damage,
	int evade,
	int launcher,
	std::shared_ptr<MagicDispatchContext> dispatchContext)
{
	if (magic == nullptr || user == nullptr || magic->keepMilliseconds == 0)
	{
		return;
	}

	if (level < 1)
	{
		level = 1;
	}
	else if (level > MAGIC_MAX_LEVEL)
	{
		level = MAGIC_MAX_LEVEL;
	}

	TrailMagicInfo info;
	info.user = user;
	info.magic = magic;
	info.lastPosition = user->position;
	info.remainingTime = magic->keepMilliseconds;
	info.level = level;
	info.damage = damage;
	info.evade = evade;
	info.launcher = launcher;
	info.dispatchContext = dispatchContext != nullptr
		? dispatchContext
		: Magic::createRootDispatchContext(magic);
	trailMagicList.push_back(info);
}

void EffectManager::addDelayedMagic(
	std::shared_ptr<Magic> magic,
	std::shared_ptr<GameElement> user,
	Point from,
	Point to,
	int level,
	int launcher,
	std::shared_ptr<GameElement> target,
	UTime delayMilliseconds,
	std::shared_ptr<MagicDispatchContext> dispatchContext)
{
	if (magic == nullptr || user == nullptr)
	{
		return;
	}

	DelayedMagicInfo info;
	info.user = user;
	info.target = target;
	info.magic = magic;
	info.from = from;
	info.to = to;
	info.remainingTime = delayMilliseconds;
	info.level = clampMagicLevelForEffectManager(level);
	info.launcher = launcher;
	info.dispatchContext = dispatchContext != nullptr
		? dispatchContext
		: Magic::createRootDispatchContext(magic);
	delayedMagicList.push_back(info);
}

void EffectManager::updateTrailMagic()
{
	UTime elapsedTime = getFrameTime();
	for (size_t i = 0; i < trailMagicList.size();)
	{
		auto& info = trailMagicList[i];
		auto user = info.user.lock();
		if (user == nullptr || info.magic == nullptr)
		{
			trailMagicList.erase(trailMagicList.begin() + i);
			continue;
		}

		if (info.lastPosition != user->position)
		{
			auto effect = Magic::createFixedEffect(
				info.magic,
				user,
				info.lastPosition,
				{ 0.0f, 0.0f },
				info.level,
				info.damage,
				info.evade,
				info.launcher,
				info.dispatchContext);
			addEffect(effect);
			effect->beginTime = effect->getTime();
			info.lastPosition = user->position;
		}

		if (elapsedTime >= info.remainingTime)
		{
			trailMagicList.erase(trailMagicList.begin() + i);
			continue;
		}
		info.remainingTime -= elapsedTime;
		i++;
	}
}

void EffectManager::updateDelayedMagic()
{
	const UTime elapsedTime = getFrameTime();
	const size_t delayedMagicCount = delayedMagicList.size();
	size_t processedCount = 0;
	for (size_t i = 0; i < delayedMagicList.size() && processedCount < delayedMagicCount; processedCount++)
	{
		auto& info = delayedMagicList[i];
		if (info.magic == nullptr)
		{
			delayedMagicList.erase(delayedMagicList.begin() + i);
			continue;
		}

		if (elapsedTime < info.remainingTime)
		{
			info.remainingTime -= elapsedTime;
			i++;
			continue;
		}

		auto item = info;
		delayedMagicList.erase(delayedMagicList.begin() + i);
		auto user = item.user.lock();
		if (user == nullptr)
		{
			continue;
		}
		auto target = item.target.lock();
		const int damage = getPrimaryDamageForMagic(item.magic, item.level, user);
		const int evade = getEvadeForMagicUser(user);
		Magic::addEffect(
			item.magic,
			user,
			item.from,
			item.to,
			item.level,
			damage,
			evade,
			item.launcher,
			target,
			item.dispatchContext);
	}
}

void EffectManager::load()
{
	const std::string iniName =
		SaveFileManager::CurrentPath() + EFFECT_INI;
	INIReader ini(iniName);
	loadFromIni(ini);
}

void EffectManager::loadFromIni(INIReader& ini)
{
	freeResource();
	EffectReferenceLoadContext referenceContext;
	int detachedCasterCount = std::clamp(
		static_cast<int>(ini.GetInteger("Head", "DetachedCasterCount", 0)),
		0,
		MaxPersistedEffectReferences);
	referenceContext.resizeDetachedCasters(static_cast<size_t>(detachedCasterCount));
	for (int i = 0; i < detachedCasterCount; i++)
	{
		referenceContext.setDetachedCaster(
			static_cast<size_t>(i),
			loadDetachedEffectCaster(ini, getDetachedCasterSection(i)));
	}

	std::string section = "Head";
	int count = std::clamp(
		static_cast<int>(ini.GetInteger(section, "Count", 0)),
		0,
		MaxPersistedEffectReferences);
	for (int i = 0; i < count; i++)
	{
		section = convert::formatString("PRO%d", i + 1);
		std::string fileName = ini.Get(section, "FileName", "");
		int level = static_cast<int>(ini.GetInteger(section, "Level", 0));
		if (fileName.empty() || level < 1 || level > MAGIC_MAX_LEVEL)
		{
			continue;
		}
		auto effect = std::make_shared<Effect>();
		effect->initFromIni(&ini, section, &referenceContext);
		if (!effect->magic.loadSucceeded || effect->level < 1 || effect->level > MAGIC_MAX_LEVEL)
		{
			continue;
		}
		if (effect->getMoveKind() == mmkSummon)
		{
			continue;
		}
		addChild(effect);
		effectList.push_back(effect);
		effect->restoreRuntimeBindingsAfterLoad();
		if (isTimeStopperCandidate(effect) && getActiveTimeStopperEffect() == nullptr)
		{
			timeStopperEffect = effect;
		}
	}

	auto loadPendingMagic = [&](const std::string& pendingSection)
	{
		std::string fileName = ini.Get(pendingSection, "MagicFile", "");
		if (fileName.empty())
		{
			return std::shared_ptr<Magic>();
		}
		auto magic = std::make_shared<Magic>();
		magic->initFromIni(fileName);
		if (!magic->loadSucceeded)
		{
			return std::shared_ptr<Magic>();
		}
		std::string experienceOwner = ini.Get(pendingSection, "ExperienceOwnerMagicFile", "");
		if (!experienceOwner.empty())
		{
			magic->experienceOwnerMagicFile = experienceOwner;
		}
		return magic;
	};

	int trailCount = std::clamp(
		static_cast<int>(ini.GetInteger("Head", "TrailCount", 0)),
		0,
		MaxPersistedEffectReferences);
	for (int i = 0; i < trailCount; i++)
	{
		section = convert::formatString("Trail%d", i + 1);
		auto user = Effect::loadElementReference(
			ini,
			section,
			"User",
			EffectElementReferenceRole::Caster,
			&referenceContext);
		auto magic = loadPendingMagic(section);
		if (user == nullptr || magic == nullptr)
		{
			continue;
		}

		TrailMagicInfo info;
		info.user = user;
		info.magic = magic;
		info.lastPosition.x = static_cast<int>(ini.GetInteger(section, "LastMapX", user->position.x));
		info.lastPosition.y = static_cast<int>(ini.GetInteger(section, "LastMapY", user->position.y));
		info.remainingTime = ini.GetTime(section, "RemainingTime", magic->keepMilliseconds);
		info.level = clampMagicLevelForEffectManager(static_cast<int>(ini.GetInteger(section, "Level", 1)));
		info.damage = static_cast<int>(ini.GetInteger(section, "Damage", 0));
		info.evade = static_cast<int>(ini.GetInteger(section, "Evade", 0));
		info.launcher = static_cast<int>(ini.GetInteger(section, "Launcher", lkSelf));
		info.dispatchContext = Magic::createRootDispatchContext(magic);
		trailMagicList.push_back(info);
	}

	int delayedCount = std::clamp(
		static_cast<int>(ini.GetInteger("Head", "DelayedCount", 0)),
		0,
		MaxPersistedEffectReferences);
	for (int i = 0; i < delayedCount; i++)
	{
		section = convert::formatString("Delayed%d", i + 1);
		auto user = Effect::loadElementReference(
			ini,
			section,
			"User",
			EffectElementReferenceRole::Caster,
			&referenceContext);
		auto magic = loadPendingMagic(section);
		if (user == nullptr || magic == nullptr)
		{
			continue;
		}

		DelayedMagicInfo info;
		info.user = user;
		info.target = Effect::loadElementReference(ini, section, "Target");
		info.magic = magic;
		info.from.x = static_cast<int>(ini.GetInteger(section, "FromMapX", 0));
		info.from.y = static_cast<int>(ini.GetInteger(section, "FromMapY", 0));
		info.to.x = static_cast<int>(ini.GetInteger(section, "ToMapX", 0));
		info.to.y = static_cast<int>(ini.GetInteger(section, "ToMapY", 0));
		info.remainingTime = ini.GetTime(section, "RemainingTime", 0);
		info.level = clampMagicLevelForEffectManager(static_cast<int>(ini.GetInteger(section, "Level", 1)));
		info.launcher = static_cast<int>(ini.GetInteger(section, "Launcher", lkSelf));
		info.dispatchContext = Magic::createRootDispatchContext(magic);
		delayedMagicList.push_back(info);
	}
}

bool EffectManager::save()
{
	INIReader ini;
	saveToIni(ini);
	
	std::string iniName = EFFECT_INI;
	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + iniName);
    
    SaveFileManager::AppendFile(iniName);
	return saved;
}

void EffectManager::saveToIni(INIReader& ini)
{
	EffectReferenceSaveContext referenceContext;
	int effectIndex = 0;
	for (const auto& effect : effectList)
	{
		if (effect == nullptr
			|| effect->fileName.empty()
			|| (effect->result & erLifeExhaust)
			|| effect->getMoveKind() == mmkSummon)
		{
			continue;
		}
		std::string section = convert::formatString("PRO%d", ++effectIndex);
		effect->saveToIni(&ini, section, &referenceContext);
	}
	ini.SetInteger("Head", "Count", effectIndex);

	int trailIndex = 0;
	for (const auto& info : trailMagicList)
	{
		auto user = info.user.lock();
		if (user == nullptr || info.magic == nullptr || !info.magic->loadSucceeded)
		{
			continue;
		}
		std::string section = convert::formatString("Trail%d", ++trailIndex);
		Effect::saveElementReference(
			ini,
			section,
			"User",
			user,
			EffectElementReferenceRole::Caster,
			&referenceContext);
		ini.Set(section, "MagicFile", info.magic->iniName);
		ini.Set(section, "ExperienceOwnerMagicFile", info.magic->experienceOwnerMagicFile);
		ini.SetInteger(section, "LastMapX", info.lastPosition.x);
		ini.SetInteger(section, "LastMapY", info.lastPosition.y);
		ini.SetTime(section, "RemainingTime", info.remainingTime);
		ini.SetInteger(section, "Level", info.level);
		ini.SetInteger(section, "Damage", info.damage);
		ini.SetInteger(section, "Evade", info.evade);
		ini.SetInteger(section, "Launcher", info.launcher);
	}
	ini.SetInteger("Head", "TrailCount", trailIndex);

	int delayedIndex = 0;
	for (const auto& info : delayedMagicList)
	{
		auto user = info.user.lock();
		if (user == nullptr || info.magic == nullptr || !info.magic->loadSucceeded)
		{
			continue;
		}
		std::string section = convert::formatString("Delayed%d", ++delayedIndex);
		Effect::saveElementReference(
			ini,
			section,
			"User",
			user,
			EffectElementReferenceRole::Caster,
			&referenceContext);
		Effect::saveElementReference(ini, section, "Target", info.target.lock());
		ini.Set(section, "MagicFile", info.magic->iniName);
		ini.Set(section, "ExperienceOwnerMagicFile", info.magic->experienceOwnerMagicFile);
		ini.SetInteger(section, "FromMapX", info.from.x);
		ini.SetInteger(section, "FromMapY", info.from.y);
		ini.SetInteger(section, "ToMapX", info.to.x);
		ini.SetInteger(section, "ToMapY", info.to.y);
		ini.SetTime(section, "RemainingTime", info.remainingTime);
		ini.SetInteger(section, "Level", info.level);
		ini.SetInteger(section, "Launcher", info.launcher);
	}
	ini.SetInteger("Head", "DelayedCount", delayedIndex);
	ini.SetInteger("Head", "EffectPersistenceVersion", 2);
	const auto& detachedCasters = referenceContext.getDetachedCasters();
	ini.SetInteger("Head", "DetachedCasterCount", static_cast<long>(detachedCasters.size()));
	for (size_t i = 0; i < detachedCasters.size(); i++)
	{
		saveDetachedEffectCaster(
			ini,
			getDetachedCasterSection(static_cast<int>(i)),
			detachedCasters[i]);
	}
}

std::shared_ptr<GameElement> EffectManager::getPendingTrailMagicUser(size_t index) const
{
	return index < trailMagicList.size() ? trailMagicList[index].user.lock() : nullptr;
}

std::shared_ptr<GameElement> EffectManager::getPendingDelayedMagicUser(size_t index) const
{
	return index < delayedMagicList.size() ? delayedMagicList[index].user.lock() : nullptr;
}

void EffectManager::disableAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
		{
			effectList[i]->damage = 0;
			effectList[i]->damage2 = 0;
			effectList[i]->damage3 = 0;
			effectList[i]->damageMana = 0;
		}
	}
}

bool EffectManager::hasSolidEffectAt(Point position) const
{
	for (const auto& effect : effectList)
	{
		if (effect != nullptr && effect->position == position && effect->isSolidObstacle())
		{
			return true;
		}
	}
	return false;
}

const EffectMap& EffectManager::createMap(int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
	{
		cachedEffectMap.tile.clear();
		return cachedEffectMap;
	}

	if (cachedWidth != w || cachedHeight != h)
	{
		cachedEffectMap.tile.resize(h);
		for (int i = 0; i < h; i++)
		{
			cachedEffectMap.tile[i].resize(w);
		}
		cachedWidth = w;
		cachedHeight = h;
	}

	for (int i = 0; i < cachedHeight; i++)
	{
		for (int j = 0; j < cachedWidth; j++)
		{
			cachedEffectMap.tile[i][j].index.clear();
		}
	}

	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr && effectList[i]->position.x >= x && effectList[i]->position.y >= y && effectList[i]->position.x < x + w && effectList[i]->position.y < y + h)
		{
			cachedEffectMap.tile[effectList[i]->position.y - y][effectList[i]->position.x - x].index.push_back(i);
		}
	}
	return cachedEffectMap;
}

void EffectManager::freeResource()
{
	timeStopperEffect.reset();
	removeAllChild();
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
		{
			effectList[i]->releaseRuntimeBindings();
			removeChild(effectList[i]);
			effectList[i] = nullptr;
		}
	}
	effectList.resize(0);
	trailMagicList.clear();
	delayedMagicList.clear();
}

bool EffectManager::shouldUpdateChild(PElement child)
{
	auto active = getActiveTimeStopperEffect();
	return shouldUpdateEffectManagerChildDuringTimeStop(active != nullptr,
		child != nullptr && child.get() == active.get());
}

void EffectManager::onUpdate()
{
	if (hasActiveTimeStopper())
	{
		clearEffect();
		return;
	}
	updateDelayedMagic();
	updateTrailMagic();
	CollisionDetector::detectCollision();
	clearEffect();
}
