#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <algorithm>
#include "../../Engine/Engine.h"
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>
#include "Magic.h"
#include "ImageResourcePathResolver.h"
#include "Map.h"
#include "Effect.h"
#include "EffectManager.h"
#include "MagicBeginPosition.h"
#include "MagicControl.h"
#include "MagicRegionShape.h"
#include "NPCManager.h"
#include "Player.h"
#include "../GameManager/GameManager.h"
#include "../../File/log.h"

struct MagicLoadCacheEntry
{
	std::string normalizedFileName;
	std::string experienceOwnerMagicFile;
	bool loadAttackFile = true;
	std::shared_ptr<Magic> magic = nullptr;
};

struct MagicLoadContext
{
	std::vector<std::string> activeFiles;
	std::vector<MagicLoadCacheEntry> cache;
	size_t nodeCount = 0;
	std::unordered_set<std::string> warnings;
};

struct MagicDispatchBudget
{
	size_t nodeCount = 0;
	std::unordered_set<std::string> warnings;
};

struct MagicDispatchContext
{
	std::shared_ptr<MagicDispatchBudget> budget = nullptr;
	std::vector<std::string> ancestry;
};

namespace
{
constexpr UTime DefaultParasiticIntervalMilliseconds = 1000;

int clampMagicLevel(int level)
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

bool isPlayerCaster(const std::shared_ptr<NPC>& caster)
{
	return caster != nullptr && caster->kind == nkPlayer;
}

UTime readNonNegativeTime(const INIReader& ini, const std::string& section, const std::string& name, UTime defaultValue)
{
	long value = ini.GetInteger(section, name, static_cast<long>(defaultValue));
	return value > 0 ? static_cast<UTime>(value) : 0;
}

UTime readNonNegativeTime(const INIReader& ini, const std::string& section, const std::string& name)
{
	return readNonNegativeTime(ini, section, name, 0);
}

std::string toLowerIniKey(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
	{
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

std::string normalizeMagicFileName(std::string value)
{
	std::replace(value.begin(), value.end(), '\\', '/');
	return toLowerIniKey(value);
}

std::string getMagicRuntimeIdentity(const std::shared_ptr<Magic>& magic)
{
	if (magic == nullptr)
	{
		return "<null>";
	}
	if (!magic->iniName.empty())
	{
		return normalizeMagicFileName(magic->iniName);
	}
	return convert::formatString("<memory:%p>", static_cast<void*>(magic.get()));
}

void warnMagicLoadOnce(MagicLoadContext& context, const std::string& key, const std::string& message)
{
	if (context.warnings.insert(key).second)
	{
		GameLog::write("Magic: %s\n", message.c_str());
	}
}

void warnMagicDispatchOnce(
	const std::shared_ptr<MagicDispatchContext>& context,
	const std::string& key,
	const std::string& message)
{
	if (context != nullptr
		&& context->budget != nullptr
		&& context->budget->warnings.insert(key).second)
	{
		GameLog::write("Magic: %s\n", message.c_str());
	}
}

bool hasIniKey(const INIReader& ini, const std::string& section, const std::string& name)
{
	std::vector<std::string> keys = ini.GetSectionKeys(section);
	return std::find(keys.begin(), keys.end(), toLowerIniKey(name)) != keys.end();
}

int getSideEffectSecondaryAmount(const Magic& magic, int level, const std::shared_ptr<NPC>& caster, bool thirdChannel)
{
	if (caster == nullptr)
	{
		return 0;
	}
	const int effectLevel = clampMagicLevel(level);
	int effect = thirdChannel ? magic.level[effectLevel].effect3 : magic.level[effectLevel].effect2;
	if (effect == 0 || !isPlayerCaster(caster))
	{
		return thirdChannel ? caster->getAttack3() : caster->getAttack2();
	}
	return effect;
}

int applyCasterMagicEffectBonus(std::shared_ptr<GameElement> user, const Magic& magic, int effect)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (caster == nullptr)
	{
		return effect;
	}
	return caster->applyMagicEffectBonus(magic, effect);
}

int getPrimaryDamageForMagic(std::shared_ptr<Magic> magic, std::shared_ptr<GameElement> user, int level, int fallbackDamage)
{
	return Magic::calculatePrimaryEffectAmount(
		magic, user, level, fallbackDamage);
}

UTime getSelfMagicInvisibilityDuration(const Magic& magic, std::shared_ptr<GameElement> user, int level)
{
	int duration = magic.level[level].effect;
	if (duration == 0)
	{
		auto caster = std::dynamic_pointer_cast<NPC>(user);
		if (caster != nullptr)
		{
			duration = caster->getAttack();
		}
	}
	duration += magic.level[level].effectExt;
	return duration > 0 ? static_cast<UTime>(duration) : 0;
}

UTime getSelfMagicMorphDuration(const Magic& magic, int level)
{
	int duration = magic.level[level].effect;
	return duration > 0 ? static_cast<UTime>(duration) : 0;
}

std::shared_ptr<Magic> resolveChangeMagic(std::shared_ptr<Magic> magic, std::shared_ptr<GameElement> user, int level)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (magic == nullptr || caster == nullptr)
	{
		return magic;
	}
	const auto& linked = magic->getLinkedLevel(level);
	if (linked.hitCountToChangeMagic <= 0 || linked.changeMagic == nullptr || !linked.changeMagic->loadSucceeded)
	{
		return magic;
	}
	if (!caster->shouldUseChangeMagic(*magic, level))
	{
		return magic;
	}

	return linked.changeMagic;
}

void applyMagicSideEffect(std::shared_ptr<Magic> magic, std::shared_ptr<GameElement> user, int level, int primaryDamage)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (magic == nullptr
		|| caster == nullptr
		|| !NPCManager::isManagedEffectCaster(caster)
		|| magic->sideEffectProbability <= 0
		|| magic->sideEffectPercent == 0)
	{
		return;
	}
	if (Engine::getInstance()->getRand(99) >= magic->sideEffectProbability)
	{
		return;
	}

	int amount = (primaryDamage
		+ getSideEffectSecondaryAmount(*magic, level, caster, false)
		+ getSideEffectSecondaryAmount(*magic, level, caster, true)) * magic->sideEffectPercent / 100;
	if (amount <= 0)
	{
		return;
	}

	caster->applySideEffectDamage(magic->sideEffectType, amount);
}

void applyMagicDieAfterUse(std::shared_ptr<Magic> magic, std::shared_ptr<GameElement> user)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (magic == nullptr
		|| caster == nullptr
		|| !NPCManager::isManagedEffectCaster(caster)
		|| magic->dieAfterUse <= 0)
	{
		return;
	}

	caster->life = 0;
	caster->handleDeath();
}

void applyMagicJumpToTarget(
	std::shared_ptr<Magic> magic,
	std::shared_ptr<GameElement> user,
	Point destination,
	int level,
	int launcher,
	const std::shared_ptr<MagicDispatchContext>& dispatchContext)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (magic == nullptr)
	{
		return;
	}
	const auto& linked = magic->getLinkedLevel(level);
	if (caster == nullptr
		|| !NPCManager::isManagedEffectCaster(caster)
		|| linked.jumpToTarget <= 0
		|| gm == nullptr
		|| gm->map == nullptr)
	{
		return;
	}
	if (!gm->map->isInMap(destination))
	{
		return;
	}

	auto endMagicContext = linked.jumpEndMagic != nullptr
		? Magic::createDerivedDispatchContext(dispatchContext, linked.jumpEndMagic, "JumpEndMagic")
		: nullptr;
	caster->beginMagicForcedMove(destination,
		(float)linked.jumpMoveSpeed,
		user,
		endMagicContext != nullptr ? linked.jumpEndMagic : nullptr,
		level,
		launcher,
		0,
		0,
		0,
		0,
		false,
		{ 0.0f, 0.0f },
		endMagicContext);
}

void applyRandMagic(std::shared_ptr<Magic> magic,
	std::shared_ptr<GameElement> user,
	Point from,
	Point to,
	int level,
	int evade,
	int launcher,
	std::shared_ptr<GameElement> target,
	const std::shared_ptr<MagicDispatchContext>& dispatchContext)
{
	if (magic == nullptr)
	{
		return;
	}
	const auto& linked = magic->getLinkedLevel(level);
	if (linked.randMagic == nullptr || !linked.randMagic->loadSucceeded || linked.randMagicProbability <= 0)
	{
		return;
	}
	if (Engine::getInstance()->getRand(99) >= linked.randMagicProbability)
	{
		return;
	}

	auto childContext = Magic::createDerivedDispatchContext(dispatchContext, linked.randMagic, "RandMagic");
	if (childContext == nullptr)
	{
		return;
	}
	int childDamage = getPrimaryDamageForMagic(linked.randMagic, user, level, 0);
	Magic::addEffect(linked.randMagic, user, from, to, level, childDamage, evade, launcher, target, childContext);
}

void attachCarryUserToEffect(std::shared_ptr<Magic> magic,
	std::vector<std::shared_ptr<Effect>>& effects,
	std::shared_ptr<GameElement> user)
{
	if (magic == nullptr || magic->carryUser <= 0 || effects.empty())
	{
		return;
	}

	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (caster == nullptr
		|| !NPCManager::isManagedEffectCaster(caster)
		|| caster->isDying()
		|| caster->isHiding())
	{
		return;
	}

	int effectIndex = magic->carryUserSpriteIndex;
	if (effectIndex < 0)
	{
		effectIndex = 0;
	}
	if (effectIndex >= static_cast<int>(effects.size()))
	{
		effectIndex = static_cast<int>(effects.size()) - 1;
	}

	auto effect = effects[effectIndex];
	if (effect != nullptr)
	{
		effect->attachCarryUser(caster);
	}
}

bool preserveZeroMoveDirection(const Magic& magic)
{
	return shouldPreserveMagicZeroMoveDirection({
		magic.beginAtMouse > 0,
		magic.beginAtUser > 0,
		magic.beginAtUserAddDirectionOffset > 0,
		magic.beginAtUserAddUserDirectionOffset > 0,
	});
}

bool hasRoundMove(const Magic& magic)
{
	return magic.roundMoveColockwise > 0 || magic.roundMoveAnticlockwise > 0;
}

void applyBeginPositionRules(const Magic& magic, std::shared_ptr<GameElement> user, Point& from, Point& to)
{
	int directionToSource = from != to ? NPC::getDirection(to, from) : 0;
	int directionToDestination = from != to ? NPC::getDirection(from, to) : 0;
	int userDirection = user != nullptr ? GameElement::normalizeDir(user->direction) : 0;
	applyMagicBeginPositionRules({
		magic.beginAtMouse > 0,
		magic.beginAtUser > 0,
		magic.beginAtUserAddDirectionOffset > 0,
		magic.beginAtUserAddUserDirectionOffset > 0,
	}, directionToSource, directionToDestination, userDirection, from, to);
}
}

int Magic::calculatePrimaryEffectAmount(
	const std::shared_ptr<Magic>& magic,
	const std::shared_ptr<GameElement>& user,
	int level,
	int fallbackAmount)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (magic == nullptr || caster == nullptr)
	{
		return fallbackAmount;
	}

	const int effectLevel = clampMagicLevel(level);
	const MagicLevel& levelInfo = magic->level[effectLevel];
	const bool selfSpecialUsesEffect = levelInfo.moveKind == mmkSelf
		&& (levelInfo.specialKind == mskAddLife
			|| levelInfo.specialKind == mskAddThew
			|| levelInfo.specialKind == mskAddShield
			|| levelInfo.specialKind == mskAddDamageReduceShield);
	if (selfSpecialUsesEffect)
	{
		return levelInfo.effect;
	}
	if (!isPlayerCaster(caster) || levelInfo.effect == 0)
	{
		return caster->getAttack();
	}
	if (gm->global.magicEffectCalculationMode !=
		MagicEffectCalculationMode::AddToAttack)
	{
		return levelInfo.effect;
	}

	const long long combinedAmount = static_cast<long long>(caster->getAttack())
		+ static_cast<long long>(levelInfo.effect);
	return static_cast<int>(std::clamp(
		combinedAmount,
		static_cast<long long>(std::numeric_limits<int>::min()),
		static_cast<long long>(std::numeric_limits<int>::max())));
}

Magic::Magic()
{
}

Magic::~Magic()
{
	freeResource();
}

void Magic::reset()
{
	experienceOwnerMagicFile = "";
	name = "";
	type = "";
	injuryType = "";
	hasInjuryType = false;
	spriteType = 0;
	hasSpriteType = false;
	attribute = 0;
	hasAttribute = false;
	scriptFile = "";
	hasScriptFile = false;
	intro = "";
	loadSucceeded = false;

	image = "";
	icon = "";

	flyingImage = "";
	flyingSound = "";
	vanishImage = "";
	vanishSound = "";
	leapImageFile = "";
	actionFile = "";
	actionShadowFile = "";
	useActionFile = "";
	attackFile = "";
	flyMagicFile = "";
	explodeMagicFile = "";
	parasiticMagicFile = "";
	randMagicFile = "";
	secondMagicFile = "";
	magicWhenNewPositionFile = "";
	magicToUseWhenKillEnemyFile = "";
	bounceFlyEndMagicFile = "";
	changeMagicFile = "";
	jumpEndMagicFile = "";
	replaceMagic = "";
	specialKind9ReplaceFlyIni = "";
	specialKind9ReplaceFlyIni2 = "";
	flyIni = "";
	flyIni2 = "";
	magicToUseWhenBeAttackedFile = "";
	magicDirectionWhenBeAttacked = 0;
	hitCountFlyingImageFile = "";
	hitCountVanishImageFile = "";
	flyInterval = 0;
	secondMagicDelay = 0;
	superModeImage = "";
	superModeSound = "";
	regionFileName = "";
	regionFile.clear();
	regionFileLoaded = false;
	keepMilliseconds = 0;
	maxLevel = 0;
	goodsName = "";
	npcFile = "";
	npcIni = "";
	maxCount = 0;
	bodyRadius = 0;
	disableUse = 0;
	lifeFullToUse = 0;
	vibratingScreen = 0;
	additionalEffect = maeNone;
	belong = 0;
	bounce = 0;
	bounceHurt = 0;
	bounceFly = 0;
	bounceFlySpeed = 32;
	bounceFlyEndHurt = 0;
	bounceFlyTouchHurt = 0;
	magicDirectionWhenBounceFlyEnd = 0;
	carryUser = 0;
	carryUserSpriteIndex = 0;
	hideUserWhenCarry = 0;
	ball = 0;
	sticky = 0;
	solid = 0;
	discardOppositeMagic = 0;
	exchangeUser = 0;
	noSpecialKindEffect = 0;
	randMagicProbability = 0;
	sideEffectType = 0;
	sideEffectPercent = 0;
	sideEffectProbability = 0;
	noInterruption = 0;
	disableMoveMilliseconds = 0;
	disableSkillMilliseconds = 0;
	coldMilliSeconds = 0;
	dieAfterUse = 0;
	restoreType = 0;
	restorePercent = 0;
	restoreProbability = 0;
	attackAddPercent = 0;
	defendAddPercent = 0;
	evadeAddPercent = 0;
	speedAddPercent = 0;
	morphMilliseconds = 0;
	weakMilliseconds = 0;
	weakAttackPercent = 0;
	weakDefendPercent = 0;
	blindMilliseconds = 0;
	parasitic = 0;
	parasiticInterval = DefaultParasiticIntervalMilliseconds;
	parasiticMaxEffect = 0;
	magicDirectionWhenKillEnemy = 0;
	changeToFriendMilliseconds = 0;
	attackAll = 0;
	traceEnemy = 0;
	traceSpeed = 0;
	traceEnemyDelayMilliseconds = 0;
	followMouse = 0;
	moveImitateUser = 0;
	moveBack = 0;
	randomMoveDegree = 0;
	meteorMove = 0;
	meteorMoveDir = 5;
	circleMoveColockwise = 0;
	circleMoveAnticlockwise = 0;
	roundMoveColockwise = 0;
	roundMoveAnticlockwise = 0;
	roundMoveCount = 1;
	roundMoveDegreeSpeed = 1;
	roundRadius = 0;
	beginAtMouse = 0;
	beginAtUser = 0;
	beginAtUserAddDirectionOffset = 0;
	beginAtUserAddUserDirectionOffset = 0;
	noExplodeWhenLifeFrameEnd = 0;
	explodeWhenLifeFrameEnd = 0;
	passThrough = 0;
	passThroughWithDestroyEffect = 0;
	passThroughWall = 0;
	reviveBodyRadius = 0;
	reviveBodyMaxCount = 0;
	reviveBodyLifeMilliseconds = 0;
	rangeEffect = 0;
	rangeRadius = 0;
	rangeSpeedUp = 0;
	rangeTimeInterval = 0;
	jumpToTarget = 0;
	jumpMoveSpeed = 32;
	hitCountToChangeMagic = 0;
	hitCountFlyRadius = 0;
	hitCountFlyAngleSpeed = 0;

	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		explodeMagicFilesByLevel[i] = "";
		explodeMagicsByLevel[i] = nullptr;
		linkedLevel[i] = MagicLinkedLevel{};
		level[i].effect = 0;
		level[i].effectExt = 0;
		level[i].effect2 = 0;
		level[i].effect3 = 0;
		level[i].effectMana = 0;
		level[i].lifeMax = 0;
		level[i].thewMax = 0;
		level[i].manaMax = 0;
		level[i].attack = 0;
		level[i].attack2 = 0;
		level[i].attack3 = 0;
		level[i].defend = 0;
		level[i].defend2 = 0;
		level[i].defend3 = 0;
		level[i].evade = 0;
		level[i].addThewRestorePercent = 0;
		level[i].addManaRestorePercent = 0;
		level[i].addLifeRestorePercent = 0;
		level[i].rangeAddLife = 0;
		level[i].rangeAddMana = 0;
		level[i].rangeAddThew = 0;
		level[i].rangeAddRage = 0;
		level[i].hasRangeAddRage = false;
		level[i].rangeFreezeMilliseconds = 0;
		level[i].rangePoisonMilliseconds = 0;
		level[i].rangePetrifyMilliseconds = 0;
		level[i].rangeDamage = 0;
		level[i].leapTimes = 0;
		level[i].leapFrame = 0;
		level[i].effectReducePercentage = 0;
		level[i].levelupExp = 0;
		level[i].lifeCost = 0;
		level[i].manaCost = 0;
		level[i].thewCost = 0;
		level[i].rageCost = 0;
		level[i].hasRageCost = false;
		level[i].critChanceAddValue = 0;
		level[i].hasCritChanceAddValue = false;
		level[i].critDamageAddPercent = 0;
		level[i].hasCritDamageAddPercent = false;
		level[i].count = 0;
		level[i].moveKind = mmkPoint;
		level[i].specialKind = 0;
		level[i].specialKindValue = 0;
		level[i].specialKindMilliseconds = 0;
		level[i].alphaBlend = 0;
		level[i].region = 0;
		level[i].speed = 0;
		level[i].flyingLum = 0;
		level[i].vanishLum = 0;
		level[i].waitFrame = 0;
		level[i].lifeFrame = 0;
		level[i].attackRadius = 0;
	}
}

void Magic::initFromIni(const std::string& fileName)
{
	initFromIni(fileName, true);
}

void Magic::initFromIni(const std::string & fileName, bool loadLinkedMagic)
{
	MagicLoadContext loadContext;
	loadContext.nodeCount = 1;
	loadContext.activeFiles.push_back(normalizeMagicFileName(fileName));
	initFromIniWithContext(fileName, loadLinkedMagic, true, "", loadContext, 1);
}

void Magic::initFromIniWithContext(
	const std::string& fileName,
	bool loadLinkedMagic,
	bool loadAttackFile,
	const std::string& experienceOwnerOverride,
	MagicLoadContext& loadContext,
	int loadDepth)
{
	reset();
	freeResource();
	iniName = fileName;
	experienceOwnerMagicFile = experienceOwnerOverride.empty()
		? fileName
		: experienceOwnerOverride;
	std::string tempName = INI_MAGIC_FOLDER + iniName;
	std::unique_ptr<char[]> s;
	int len = File::readFile(tempName, s);
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		if (ini.ParseError() != 0)
		{
			GameLog::write(
				"Magic: invalid ini file %s\n",
				tempName.c_str());
			return;
		}
		std::string section = "Init";
		name = ini.Get(section, "Name", "");
		type = ini.Get(section, "Type", "");
		injuryType = ini.Get(section, "InjuryType", "");
		hasInjuryType = hasIniKey(ini, section, "InjuryType");
		spriteType = ini.GetInteger(section, "SpriteType", 0);
		hasSpriteType = hasIniKey(ini, section, "SpriteType");
		attribute = ini.GetInteger(section, "Attribute", 0);
		hasAttribute = hasIniKey(ini, section, "Attribute");
		scriptFile = ini.Get(section, "ScriptFile", "");
		hasScriptFile = hasIniKey(ini, section, "ScriptFile");
		intro = ini.Get(section, "Intro", "");
		image = ini.Get(section, "Image", "");
		icon = ini.Get(section, "Icon", "");
		flyingImage = ini.Get(section, "FlyingImage", "");
		flyingSound = ini.Get(section, "FlyingSound", "");
		vanishImage = ini.Get(section, "VanishImage", "");
		vanishSound = ini.Get(section, "VanishSound", "");
		leapImageFile = ini.Get(section, "LeapImage", "");
		actionFile = ini.Get(section, "ActionFile", "");
		actionShadowFile = ini.Get(section, "ActionShadowFile", "");
		useActionFile = ini.Get(section, "UseActionFile", "");
		attackFile = ini.Get(section, "AttackFile", "");
		flyMagicFile = ini.Get(section, "FlyMagic", "");
		explodeMagicFile = ini.Get(section, "ExplodeMagicFile", "");
		parasiticMagicFile = ini.Get(section, "ParasiticMagic", "");
		randMagicFile = ini.Get(section, "RandMagicFile", "");
		secondMagicFile = ini.Get(section, "SecondMagicFile", "");
		magicWhenNewPositionFile = ini.Get(section, "MagicWhenNewPos", "");
		magicToUseWhenKillEnemyFile = ini.Get(section, "MagicToUseWhenKillEnemy", "");
		bounceFlyEndMagicFile = ini.Get(section, "BounceFlyEndMagic", "");
		changeMagicFile = ini.Get(section, "ChangeMagic", "");
		jumpEndMagicFile = ini.Get(section, "JumpEndMagic", "");
		replaceMagic = ini.Get(section, "ReplaceMagic", "");
		specialKind9ReplaceFlyIni = ini.Get(section, "SpecialKind9ReplaceFlyIni", "");
		specialKind9ReplaceFlyIni2 = ini.Get(section, "SpecialKind9ReplaceFlyIni2", "");
		flyIni = ini.Get(section, "FlyIni", "");
		flyIni2 = ini.Get(section, "FlyIni2", "");
		magicToUseWhenBeAttackedFile = ini.Get(section, "MagicToUseWhenBeAttacked", "");
		magicDirectionWhenBeAttacked = ini.GetInteger(section, "MagicDirectionWhenBeAttacked", 0);
		hitCountFlyingImageFile = ini.Get(section, "HitCountFlyingImage", "");
		hitCountVanishImageFile = ini.Get(section, "HitCountVanishImage", "");
		flyInterval = ini.GetTime(section, "FlyInterval", 0);
		secondMagicDelay = ini.GetTime(section, "SecondMagicDelay", 0);
		superModeImage = ini.Get(section, "SuperModeImage", "");
		superModeSound = ini.Get(section, "SuperModeSound", "");
		regionFileName = ini.Get(section, "RegionFile", "");
		if (!regionFileName.empty())
		{
			regionFileLoaded = loadMagicRegionFile(INI_MAGIC_FOLDER + regionFileName, regionFile);
		}
		const long parsedKeepMilliseconds = ini.GetInteger(section, "KeepMilliseconds", 0);
		keepMilliseconds = static_cast<unsigned int>(std::clamp<long>(
			parsedKeepMilliseconds,
			0,
			std::numeric_limits<int>::max()));
		maxLevel = ini.GetInteger(section, "MaxLevel", 0);
		goodsName = ini.Get(section, "GoodsName", "");
		npcFile = ini.Get(section, "NpcFile", "");
		npcIni = ini.Get(section, "NpcIni", "");
		maxCount = ini.GetInteger(section, "MaxCount", 0);
		if (maxCount < 0)
		{
			maxCount = 0;
		}
		bodyRadius = ini.GetInteger(section, "BodyRadius", 0);
		if (bodyRadius < 0)
		{
			bodyRadius = 0;
		}
		disableUse = ini.GetInteger(section, "DisableUse", 0);
		lifeFullToUse = ini.GetInteger(section, "LifeFullToUse", 0);
		vibratingScreen = ini.GetInteger(section, "VibratingScreen", 0);
		if (vibratingScreen < 0)
		{
			vibratingScreen = 0;
		}
		additionalEffect = ini.GetInteger(section, "AdditionalEffect", maeNone);
		if (additionalEffect < maeNone || additionalEffect > maePetrified)
		{
			additionalEffect = maeNone;
		}
		belong = ini.GetInteger(section, "Belong", 0);
		bounce = ini.GetInteger(section, "Bounce", 0);
		bounceHurt = ini.GetInteger(section, "BounceHurt", 0);
		bounceFly = ini.GetInteger(section, "BounceFly", 0);
		bounceFlySpeed = ini.GetInteger(section, "BounceFlySpeed", 32);
		if (bounceFlySpeed <= 0)
		{
			bounceFlySpeed = 32;
		}
		bounceFlyEndHurt = ini.GetInteger(section, "BounceFlyEndHurt", 0);
		bounceFlyTouchHurt = ini.GetInteger(section, "BounceFlyTouchHurt", 0);
		magicDirectionWhenBounceFlyEnd = ini.GetInteger(section, "MagicDirectionWhenBounceFlyEnd", 0);
		carryUser = ini.GetInteger(section, "CarryUser", 0);
		carryUserSpriteIndex = ini.GetInteger(section, "CarryUserSpriteIndex", 0);
		hideUserWhenCarry = ini.GetInteger(section, "HideUserWhenCarry", 0);
		ball = ini.GetInteger(section, "Ball", 0);
		sticky = ini.GetInteger(section, "Sticky", 0);
		solid = ini.GetInteger(section, "Solid", 0);
		discardOppositeMagic = ini.GetInteger(section, "DiscardOppositeMagic", 0);
		exchangeUser = ini.GetInteger(section, "ExchangeUser", 0);
		noSpecialKindEffect = ini.GetInteger(section, "NoSpecialKindEffect", ini.GetInteger(section, "NoSpecialKindEffectExt", 0));
		randMagicProbability = ini.GetInteger(section, "RandMagicProbability", 0);
		if (randMagicProbability < 0)
		{
			randMagicProbability = 0;
		}
		else if (randMagicProbability > 100)
		{
			randMagicProbability = 100;
		}
		sideEffectType = ini.GetInteger(section, "SideEffectType", 0);
		sideEffectPercent = ini.GetInteger(section, "SideEffectPercent", 0);
		sideEffectProbability = ini.GetInteger(section, "SideEffectProbability", 0);
		noInterruption = ini.GetInteger(section, "NoInterruption", 0);
		disableMoveMilliseconds = readNonNegativeTime(ini, section, "DisableMoveMilliseconds");
		disableSkillMilliseconds = readNonNegativeTime(ini, section, "DisableSkillMilliseconds");
		coldMilliSeconds = readNonNegativeTime(ini, section, "ColdMilliSeconds");
		dieAfterUse = ini.GetInteger(section, "DieAfterUse", 0);
		if (sideEffectType < 0 || sideEffectType > 2)
		{
			sideEffectType = 0;
		}
		if (sideEffectProbability < 0)
		{
			sideEffectProbability = 0;
		}
		else if (sideEffectProbability > 100)
		{
			sideEffectProbability = 100;
		}
		restoreType = ini.GetInteger(section, "RestoreType", 0);
		restorePercent = ini.GetInteger(section, "RestorePercent", 0);
		restoreProbability = ini.GetInteger(section, "RestoreProbability", 0);
		attackAddPercent = ini.GetInteger(section, "AttackAddPercent", 0);
		defendAddPercent = ini.GetInteger(section, "DefendAddPercent", 0);
		evadeAddPercent = ini.GetInteger(section, "EvadeAddPercent", 0);
		speedAddPercent = ini.GetInteger(section, "SpeedAddPercent", 0);
		morphMilliseconds = readNonNegativeTime(ini, section, "MorphMilliseconds");
		weakMilliseconds = readNonNegativeTime(ini, section, "WeakMilliseconds");
		weakAttackPercent = ini.GetInteger(section, "WeakAttackPercent", 0);
		weakDefendPercent = ini.GetInteger(section, "WeakDefendPercent", 0);
		blindMilliseconds = readNonNegativeTime(ini, section, "BlindMilliseconds");
		parasitic = ini.GetInteger(section, "Parasitic", 0);
		parasiticInterval = readNonNegativeTime(ini, section, "ParasiticInterval", DefaultParasiticIntervalMilliseconds);
		parasiticMaxEffect = ini.GetInteger(section, "ParasiticMaxEffect", 0);
		magicDirectionWhenKillEnemy = ini.GetInteger(section, "MagicDirectionWhenKillEnemy", 0);
		changeToFriendMilliseconds = readNonNegativeTime(ini, section, "ChangeToFriendMilliseconds");
		attackAll = ini.GetInteger(section, "AttackAll", 0);
		traceEnemy = ini.GetInteger(section, "TraceEnemy", 0);
		traceSpeed = ini.GetInteger(section, "TraceSpeed", 0);
		traceEnemyDelayMilliseconds = readNonNegativeTime(ini, section, "TraceEnemyDelayMilliseconds");
		followMouse = ini.GetInteger(section, "FollowMouse", 0);
		moveImitateUser = ini.GetInteger(section, "MoveImitateUser", 0);
		moveBack = ini.GetInteger(section, "MoveBack", 0);
		randomMoveDegree = ini.GetInteger(section, "RandomMoveDegree", 0);
		if (randomMoveDegree < 0)
		{
			randomMoveDegree = 0;
		}
		meteorMove = ini.GetInteger(section, "MeteorMove", 0);
		if (meteorMove < 0)
		{
			meteorMove = 0;
		}
		meteorMoveDir = ini.GetInteger(section, "MeteorMoveDir", 5);
		if (meteorMoveDir < 0)
		{
			meteorMoveDir = 5;
		}
		circleMoveColockwise = ini.GetInteger(section, "CircleMoveColockwise", ini.GetInteger(section, "CircleMoveClockwise", 0));
		circleMoveAnticlockwise = ini.GetInteger(section, "CircleMoveAnticlockwise", 0);
		roundMoveColockwise = ini.GetInteger(section, "RoundMoveColockwise", ini.GetInteger(section, "RoundMoveClockwise", 0));
		roundMoveAnticlockwise = ini.GetInteger(section, "RoundMoveAnticlockwise", 0);
		roundMoveCount = ini.GetInteger(section, "RoundMoveCount", 1);
		if (roundMoveCount <= 0)
		{
			roundMoveCount = 1;
		}
		roundMoveDegreeSpeed = ini.GetInteger(section, "RoundMoveDegreeSpeed", 1);
		if (roundMoveDegreeSpeed <= 0)
		{
			roundMoveDegreeSpeed = 1;
		}
		roundRadius = ini.GetInteger(section, "RoundRadius", 0);
		if (roundRadius < 0)
		{
			roundRadius = 0;
		}
		beginAtMouse = ini.GetInteger(section, "BeginAtMouse", 0);
		beginAtUser = ini.GetInteger(section, "BeginAtUser", 0);
		beginAtUserAddDirectionOffset = ini.GetInteger(section, "BeginAtUserAddDirectionOffset", 0);
		beginAtUserAddUserDirectionOffset = ini.GetInteger(section, "BeginAtUserAddUserDirectionOffset", 0);
		noExplodeWhenLifeFrameEnd = ini.GetInteger(section, "NoExplodeWhenLifeFrameEnd", 0);
		explodeWhenLifeFrameEnd = ini.GetInteger(section, "ExplodeWhenLifeFrameEnd", 0);
		if (restoreType < 0 || restoreType > 2)
		{
			restoreType = 0;
		}
		if (restoreProbability < 0)
		{
			restoreProbability = 0;
		}
		else if (restoreProbability > 100)
		{
			restoreProbability = 100;
		}
		if (weakAttackPercent < 0)
		{
			weakAttackPercent = 0;
		}
		else if (weakAttackPercent > 100)
		{
			weakAttackPercent = 100;
		}
		if (weakDefendPercent < 0)
		{
			weakDefendPercent = 0;
		}
		else if (weakDefendPercent > 100)
		{
			weakDefendPercent = 100;
		}
		passThrough = ini.GetInteger(section, "PassThrough", 0);
		passThroughWithDestroyEffect = ini.GetInteger(section, "PassThroughWithDestroyEffect", 0);
		passThroughWall = ini.GetInteger(section, "PassThroughWall", 0);
		reviveBodyRadius = ini.GetInteger(section, "ReviveBodyRadius", 0);
		reviveBodyMaxCount = ini.GetInteger(section, "ReviveBodyMaxCount", 0);
		reviveBodyLifeMilliseconds = ini.GetTime(section, "ReviveBodyLifeMilliSeconds", 0);
		rangeEffect = ini.GetInteger(section, "RangeEffect", 0);
		rangeRadius = ini.GetInteger(section, "RangeRadius", 0);
		rangeSpeedUp = ini.GetInteger(section, "RangeSpeedUp", 0);
		rangeTimeInterval = ini.GetTime(section, "RangeTimeInerval", ini.GetTime(section, "RangeTimeInterval", 0));
		jumpToTarget = ini.GetInteger(section, "JumpToTarget", 0);
		jumpMoveSpeed = ini.GetInteger(section, "JumpMoveSpeed", 32);
		if (jumpMoveSpeed <= 0)
		{
			jumpMoveSpeed = 32;
		}
		hitCountToChangeMagic = ini.GetInteger(section, "HitCountToChangeMagic", 0);
		hitCountFlyRadius = ini.GetInteger(section, "HitCountFlyRadius", 0);
		hitCountFlyAngleSpeed = ini.GetInteger(section, "HitCountFlyAngleSpeed", 0);

		MagicLinkedLevel initLinkedLevel;
		initLinkedLevel.attackFile = attackFile;
		initLinkedLevel.flyMagicFile = flyMagicFile;
		initLinkedLevel.parasiticMagicFile = parasiticMagicFile;
		initLinkedLevel.randMagicFile = randMagicFile;
		initLinkedLevel.secondMagicFile = secondMagicFile;
		initLinkedLevel.magicWhenNewPositionFile = magicWhenNewPositionFile;
		initLinkedLevel.magicToUseWhenKillEnemyFile = magicToUseWhenKillEnemyFile;
		initLinkedLevel.bounceFlyEndMagicFile = bounceFlyEndMagicFile;
		initLinkedLevel.changeMagicFile = changeMagicFile;
		initLinkedLevel.jumpEndMagicFile = jumpEndMagicFile;
		initLinkedLevel.flyInterval = flyInterval;
		initLinkedLevel.parasitic = parasitic;
		initLinkedLevel.parasiticInterval = parasiticInterval;
		initLinkedLevel.parasiticMaxEffect = parasiticMaxEffect;
		initLinkedLevel.randMagicProbability = randMagicProbability;
		initLinkedLevel.secondMagicDelay = secondMagicDelay;
		initLinkedLevel.magicDirectionWhenKillEnemy = magicDirectionWhenKillEnemy;
		initLinkedLevel.bounceFly = bounceFly;
		initLinkedLevel.bounceFlySpeed = bounceFlySpeed;
		initLinkedLevel.bounceFlyEndHurt = bounceFlyEndHurt;
		initLinkedLevel.bounceFlyTouchHurt = bounceFlyTouchHurt;
		initLinkedLevel.magicDirectionWhenBounceFlyEnd = magicDirectionWhenBounceFlyEnd;
		initLinkedLevel.jumpToTarget = jumpToTarget;
		initLinkedLevel.jumpMoveSpeed = jumpMoveSpeed;
		initLinkedLevel.hitCountToChangeMagic = hitCountToChangeMagic;
		initLinkedLevel.hitCountFlyRadius = hitCountFlyRadius;
		initLinkedLevel.hitCountFlyAngleSpeed = hitCountFlyAngleSpeed;

		for (int i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
		{
			linkedLevel[i] = initLinkedLevel;
			if (i == 0)
			{
				continue;
			}

			const std::string linkedSection = convert::formatString("Level%d", i);
			if (hasIniKey(ini, linkedSection, "AttackFile"))
			{
				const std::string levelAttackFile = ini.Get(linkedSection, "AttackFile", "");
				if (!levelAttackFile.empty())
				{
					linkedLevel[i].attackFile = levelAttackFile;
				}
			}
			linkedLevel[i].flyMagicFile = ini.Get(linkedSection, "FlyMagic", initLinkedLevel.flyMagicFile);
			linkedLevel[i].parasiticMagicFile = ini.Get(linkedSection, "ParasiticMagic", initLinkedLevel.parasiticMagicFile);
			linkedLevel[i].randMagicFile = ini.Get(linkedSection, "RandMagicFile", initLinkedLevel.randMagicFile);
			linkedLevel[i].secondMagicFile = ini.Get(linkedSection, "SecondMagicFile", initLinkedLevel.secondMagicFile);
			linkedLevel[i].magicWhenNewPositionFile = ini.Get(linkedSection, "MagicWhenNewPos", initLinkedLevel.magicWhenNewPositionFile);
			linkedLevel[i].magicToUseWhenKillEnemyFile = ini.Get(linkedSection, "MagicToUseWhenKillEnemy", initLinkedLevel.magicToUseWhenKillEnemyFile);
			linkedLevel[i].bounceFlyEndMagicFile = ini.Get(linkedSection, "BounceFlyEndMagic", initLinkedLevel.bounceFlyEndMagicFile);
			linkedLevel[i].changeMagicFile = ini.Get(linkedSection, "ChangeMagic", initLinkedLevel.changeMagicFile);
			linkedLevel[i].jumpEndMagicFile = ini.Get(linkedSection, "JumpEndMagic", initLinkedLevel.jumpEndMagicFile);
			linkedLevel[i].flyInterval = ini.GetTime(linkedSection, "FlyInterval", initLinkedLevel.flyInterval);
			linkedLevel[i].parasitic = ini.GetInteger(linkedSection, "Parasitic", initLinkedLevel.parasitic);
			linkedLevel[i].parasiticInterval = readNonNegativeTime(ini, linkedSection, "ParasiticInterval", initLinkedLevel.parasiticInterval);
			linkedLevel[i].parasiticMaxEffect = ini.GetInteger(linkedSection, "ParasiticMaxEffect", initLinkedLevel.parasiticMaxEffect);
			linkedLevel[i].randMagicProbability = std::clamp(
				static_cast<int>(ini.GetInteger(linkedSection, "RandMagicProbability", initLinkedLevel.randMagicProbability)),
				0,
				100);
			linkedLevel[i].secondMagicDelay = ini.GetTime(linkedSection, "SecondMagicDelay", initLinkedLevel.secondMagicDelay);
			linkedLevel[i].magicDirectionWhenKillEnemy = ini.GetInteger(linkedSection, "MagicDirectionWhenKillEnemy", initLinkedLevel.magicDirectionWhenKillEnemy);
			linkedLevel[i].bounceFly = ini.GetInteger(linkedSection, "BounceFly", initLinkedLevel.bounceFly);
			linkedLevel[i].bounceFlySpeed = ini.GetInteger(linkedSection, "BounceFlySpeed", initLinkedLevel.bounceFlySpeed);
			if (linkedLevel[i].bounceFlySpeed <= 0)
			{
				linkedLevel[i].bounceFlySpeed = 32;
			}
			linkedLevel[i].bounceFlyEndHurt = ini.GetInteger(linkedSection, "BounceFlyEndHurt", initLinkedLevel.bounceFlyEndHurt);
			linkedLevel[i].bounceFlyTouchHurt = ini.GetInteger(linkedSection, "BounceFlyTouchHurt", initLinkedLevel.bounceFlyTouchHurt);
			linkedLevel[i].magicDirectionWhenBounceFlyEnd = ini.GetInteger(linkedSection, "MagicDirectionWhenBounceFlyEnd", initLinkedLevel.magicDirectionWhenBounceFlyEnd);
			linkedLevel[i].jumpToTarget = ini.GetInteger(linkedSection, "JumpToTarget", initLinkedLevel.jumpToTarget);
			linkedLevel[i].jumpMoveSpeed = ini.GetInteger(linkedSection, "JumpMoveSpeed", initLinkedLevel.jumpMoveSpeed);
			if (linkedLevel[i].jumpMoveSpeed <= 0)
			{
				linkedLevel[i].jumpMoveSpeed = 32;
			}
			linkedLevel[i].hitCountToChangeMagic = ini.GetInteger(linkedSection, "HitCountToChangeMagic", initLinkedLevel.hitCountToChangeMagic);
			linkedLevel[i].hitCountFlyRadius = ini.GetInteger(linkedSection, "HitCountFlyRadius", initLinkedLevel.hitCountFlyRadius);
			linkedLevel[i].hitCountFlyAngleSpeed = ini.GetInteger(linkedSection, "HitCountFlyAngleSpeed", initLinkedLevel.hitCountFlyAngleSpeed);
		}

		for (int i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
		{
			if (i == 0)
			{
				section = "Init";
			}
			else
			{
				section = convert::formatString("Level%d", i);
			}
			int idx = i - 1 < 0 ? 0 : i - 1;
			level[i].effect = ini.GetInteger(section, "Effect", level[idx].effect);
			level[i].effectExt = ini.GetInteger(section, "EffectExt", level[idx].effectExt);
			level[i].effect2 = ini.GetInteger(section, "Effect2", level[idx].effect2);
			level[i].effect3 = ini.GetInteger(section, "Effect3", level[idx].effect3);
			level[i].effectMana = ini.GetInteger(section, "EffectMana", level[idx].effectMana);
			level[i].lifeMax = ini.GetInteger(section, "LifeMax", level[idx].lifeMax);
			level[i].thewMax = ini.GetInteger(section, "ThewMax", level[idx].thewMax);
			level[i].manaMax = ini.GetInteger(section, "ManaMax", level[idx].manaMax);
			level[i].attack = ini.GetInteger(section, "Attack", level[idx].attack);
			level[i].attack2 = ini.GetInteger(section, "Attack2", level[idx].attack2);
			level[i].attack3 = ini.GetInteger(section, "Attack3", level[idx].attack3);
			level[i].defend = ini.GetInteger(section, "Defend", level[idx].defend);
			level[i].defend2 = ini.GetInteger(section, "Defend2", level[idx].defend2);
			level[i].defend3 = ini.GetInteger(section, "Defend3", level[idx].defend3);
			level[i].evade = ini.GetInteger(section, "Evade", level[idx].evade);
			level[i].addThewRestorePercent = ini.GetInteger(section, "AddThewRestorePercent", level[idx].addThewRestorePercent);
			level[i].addManaRestorePercent = ini.GetInteger(section, "AddManaRestorePercent", level[idx].addManaRestorePercent);
			level[i].addLifeRestorePercent = ini.GetInteger(section, "AddLifeRestorePercent", level[idx].addLifeRestorePercent);
			level[i].rangeAddLife = ini.GetInteger(section, "RangeAddLife", level[idx].rangeAddLife);
			level[i].rangeAddMana = ini.GetInteger(section, "RangeAddMana", level[idx].rangeAddMana);
			level[i].rangeAddThew = ini.GetInteger(section, "RangeAddThew", level[idx].rangeAddThew);
			level[i].rangeAddRage = ini.GetInteger(section, "RangeAddRage", level[idx].rangeAddRage);
			level[i].hasRangeAddRage = hasIniKey(ini, section, "RangeAddRage") || level[idx].hasRangeAddRage;
			level[i].rangeFreezeMilliseconds = ini.GetTime(section, "RangeFreeze", level[idx].rangeFreezeMilliseconds);
			level[i].rangePoisonMilliseconds = ini.GetTime(section, "RangePoison", level[idx].rangePoisonMilliseconds);
			level[i].rangePetrifyMilliseconds = ini.GetTime(section, "RangePetrify", level[idx].rangePetrifyMilliseconds);
			level[i].rangeDamage = ini.GetInteger(section, "RangeDamage", level[idx].rangeDamage);
			level[i].leapTimes = ini.GetInteger(section, "LeapTimes", level[idx].leapTimes);
			level[i].leapFrame = ini.GetInteger(section, "LeapFrame", level[idx].leapFrame);
			level[i].effectReducePercentage = ini.GetInteger(section, "EffectReducePercentage", level[idx].effectReducePercentage);
			level[i].levelupExp = ini.GetInteger(section, "LevelupExp", level[idx].levelupExp);
			level[i].lifeCost = ini.GetInteger(section, "LifeCost", level[idx].lifeCost);
			level[i].manaCost = ini.GetInteger(section, "ManaCost", level[idx].manaCost);
			level[i].thewCost = ini.GetInteger(section, "ThewCost", level[idx].thewCost);
			level[i].rageCost = ini.GetInteger(section, "RageCost", level[idx].rageCost);
			level[i].hasRageCost = hasIniKey(ini, section, "RageCost") || level[idx].hasRageCost;
			level[i].critChanceAddValue = ini.GetInteger(section, "CritChanceAddValue", level[idx].critChanceAddValue);
			level[i].hasCritChanceAddValue = hasIniKey(ini, section, "CritChanceAddValue") || level[idx].hasCritChanceAddValue;
			level[i].critDamageAddPercent = ini.GetInteger(section, "CritDamageAddPercent", level[idx].critDamageAddPercent);
			level[i].hasCritDamageAddPercent = hasIniKey(ini, section, "CritDamageAddPercent") || level[idx].hasCritDamageAddPercent;
			level[i].count = ini.GetInteger(section, "Count", level[idx].count);

			level[i].moveKind = ini.GetInteger(section, "MoveKind", level[idx].moveKind);
			level[i].specialKind = ini.GetInteger(section, "SpecialKind", level[idx].specialKind);
			level[i].specialKindValue = ini.GetInteger(section, "SpecialKindValue", level[idx].specialKindValue);
			level[i].specialKindMilliseconds = readNonNegativeTime(ini, section, "SpecialKindMilliSeconds", level[idx].specialKindMilliseconds);
			level[i].alphaBlend = ini.GetInteger(section, "AlphaBlend", level[idx].alphaBlend);
			level[i].region = ini.GetInteger(section, "Region", level[idx].region);
			level[i].speed = ini.GetInteger(section, "Speed", level[idx].speed);
			level[i].flyingLum = ini.GetInteger(section, "FlyingLum", level[idx].flyingLum);
			level[i].vanishLum = ini.GetInteger(section, "VanishLum", level[idx].vanishLum);
			level[i].waitFrame = ini.GetInteger(section, "WaitFrame", level[idx].waitFrame);
			level[i].lifeFrame = ini.GetInteger(section, "LifeFrame", level[idx].lifeFrame);

			level[i].attackRadius = ini.GetInteger(section, "AttackRadius", level[idx].attackRadius);
			explodeMagicFilesByLevel[i] = ini.Get(section, "ExplodeMagicFile", explodeMagicFile);

		}

		loadRes();
		loadSucceeded = true;

		if (loadLinkedMagic)
		{
			auto loadCachedChild = [&](const std::string& childFile,
				const std::string& experienceOwner,
				bool childLoadsAttackFile,
				const char* relationship) -> std::shared_ptr<Magic>
			{
				if (childFile.empty())
				{
					return nullptr;
				}
				const std::string normalizedChildFile = normalizeMagicFileName(childFile);
				if (std::find(loadContext.activeFiles.begin(), loadContext.activeFiles.end(), normalizedChildFile)
					!= loadContext.activeFiles.end())
				{
					const std::string key = "cycle:" + normalizedChildFile;
					warnMagicLoadOnce(loadContext, key, convert::formatString(
						"linked magic cycle truncated at %s -> %s (%s)",
						fileName.c_str(), childFile.c_str(), relationship));
					return nullptr;
				}

				auto cached = std::find_if(
					loadContext.cache.begin(),
					loadContext.cache.end(),
					[&](const MagicLoadCacheEntry& entry)
					{
						return entry.normalizedFileName == normalizedChildFile
							&& entry.experienceOwnerMagicFile == experienceOwner
							&& entry.loadAttackFile == childLoadsAttackFile;
					});
				if (cached != loadContext.cache.end())
				{
					return cached->magic;
				}
				if (loadDepth >= MaxLinkedMagicLoadDepth)
				{
					const std::string key = "depth:" + normalizedChildFile;
					warnMagicLoadOnce(loadContext, key, convert::formatString(
						"linked magic depth limit %d truncated %s -> %s (%s)",
						MaxLinkedMagicLoadDepth, fileName.c_str(), childFile.c_str(), relationship));
					return nullptr;
				}
				if (loadContext.nodeCount >= MaxLinkedMagicLoadNodes)
				{
					warnMagicLoadOnce(loadContext, "node-budget", convert::formatString(
						"linked magic node limit %zu truncated remaining branches at %s -> %s (%s)",
						MaxLinkedMagicLoadNodes, fileName.c_str(), childFile.c_str(), relationship));
					return nullptr;
				}

				loadContext.nodeCount++;
				loadContext.activeFiles.push_back(normalizedChildFile);
				auto childMagic = std::make_shared<Magic>();
				childMagic->initFromIniWithContext(
					childFile,
					true,
					childLoadsAttackFile,
					experienceOwner,
					loadContext,
					loadDepth + 1);
				loadContext.activeFiles.pop_back();
				if (!childMagic->loadSucceeded)
				{
					childMagic = nullptr;
				}
				loadContext.cache.push_back({
					normalizedChildFile,
					experienceOwner,
					childLoadsAttackFile,
					childMagic });
				return childMagic;
			};

			for (int i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
			{
				auto& linked = linkedLevel[i];
				if (i > 0
					&& linked.attackFile != linkedLevel[0].attackFile
					&& !File::fileExist(INI_MAGIC_FOLDER + linked.attackFile))
				{
					linked.attackFile = linkedLevel[0].attackFile;
				}
				linked.specialMagic = loadAttackFile
					? loadCachedChild(linked.attackFile, "", false, "AttackFile")
					: nullptr;
				if (i > 0 && linked.attackFile != linkedLevel[0].attackFile && linked.specialMagic == nullptr)
				{
					linked.attackFile = linkedLevel[0].attackFile;
					linked.specialMagic = linkedLevel[0].specialMagic;
				}
				linked.flyMagic = loadCachedChild(linked.flyMagicFile, experienceOwnerMagicFile, true, "FlyMagic");
				linked.parasiticMagic = loadCachedChild(linked.parasiticMagicFile, experienceOwnerMagicFile, true, "ParasiticMagic");
				linked.randMagic = loadCachedChild(linked.randMagicFile, "", true, "RandMagicFile");
				linked.secondMagic = loadCachedChild(linked.secondMagicFile, "", true, "SecondMagicFile");
				linked.magicWhenNewPosition = loadCachedChild(linked.magicWhenNewPositionFile, "", true, "MagicWhenNewPos");
				linked.magicToUseWhenKillEnemy = loadCachedChild(linked.magicToUseWhenKillEnemyFile, "", true, "MagicToUseWhenKillEnemy");
				linked.bounceFlyEndMagic = loadCachedChild(linked.bounceFlyEndMagicFile, "", true, "BounceFlyEndMagic");
				linked.changeMagic = loadCachedChild(linked.changeMagicFile, "", true, "ChangeMagic");
				linked.jumpEndMagic = loadCachedChild(linked.jumpEndMagicFile, experienceOwnerMagicFile, true, "JumpEndMagic");
			}

			specialMagic = linkedLevel[0].specialMagic;
			flyMagic = linkedLevel[0].flyMagic;
			parasiticMagic = linkedLevel[0].parasiticMagic;
			randMagic = linkedLevel[0].randMagic;
			secondMagic = linkedLevel[0].secondMagic;
			magicWhenNewPosition = linkedLevel[0].magicWhenNewPosition;
			magicToUseWhenKillEnemy = linkedLevel[0].magicToUseWhenKillEnemy;
			bounceFlyEndMagic = linkedLevel[0].bounceFlyEndMagic;
			changeMagic = linkedLevel[0].changeMagic;
			jumpEndMagic = linkedLevel[0].jumpEndMagic;

			for (int i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
			{
				const std::string& childFile = explodeMagicFilesByLevel[i];
				if (childFile.empty())
				{
					explodeMagicsByLevel[i] = nullptr;
					continue;
				}

				explodeMagicsByLevel[i] = loadCachedChild(
					childFile,
					experienceOwnerMagicFile,
					true,
					"ExplodeMagicFile");
			}
			explodeMagic = explodeMagicsByLevel[0];
		}
	}
}

std::shared_ptr<MagicDispatchContext> Magic::createRootDispatchContext(
	const std::shared_ptr<Magic>& magic)
{
	if (magic == nullptr)
	{
		return nullptr;
	}
	auto context = std::make_shared<MagicDispatchContext>();
	context->budget = std::make_shared<MagicDispatchBudget>();
	context->budget->nodeCount = 1;
	context->ancestry.push_back(getMagicRuntimeIdentity(magic));
	return context;
}

std::shared_ptr<MagicDispatchContext> Magic::createDerivedDispatchContext(
	const std::shared_ptr<MagicDispatchContext>& parentContext,
	const std::shared_ptr<Magic>& childMagic,
	const char* relationship)
{
	if (childMagic == nullptr || !childMagic->loadSucceeded)
	{
		return nullptr;
	}
	if (parentContext == nullptr || parentContext->budget == nullptr)
	{
		return createRootDispatchContext(childMagic);
	}

	const std::string childIdentity = getMagicRuntimeIdentity(childMagic);
	const std::string relationshipName = relationship != nullptr ? relationship : "linked magic";
	if (std::find(parentContext->ancestry.begin(), parentContext->ancestry.end(), childIdentity)
		!= parentContext->ancestry.end())
	{
		warnMagicDispatchOnce(parentContext,
			"cycle:" + childIdentity,
			convert::formatString("runtime linked magic cycle truncated at %s (%s)",
				childIdentity.c_str(), relationshipName.c_str()));
		return nullptr;
	}
	if (parentContext->ancestry.size() >= static_cast<size_t>(MaxDerivedMagicRuntimeDepth))
	{
		warnMagicDispatchOnce(parentContext,
			"depth:" + childIdentity,
			convert::formatString("runtime linked magic depth limit %d truncated %s (%s)",
				MaxDerivedMagicRuntimeDepth, childIdentity.c_str(), relationshipName.c_str()));
		return nullptr;
	}
	if (parentContext->budget->nodeCount >= MaxDerivedMagicRuntimeNodes)
	{
		warnMagicDispatchOnce(parentContext,
			"node-budget",
			convert::formatString("runtime linked magic node limit %zu truncated remaining branches at %s (%s)",
				MaxDerivedMagicRuntimeNodes, childIdentity.c_str(), relationshipName.c_str()));
		return nullptr;
	}

	parentContext->budget->nodeCount++;
	auto childContext = std::make_shared<MagicDispatchContext>();
	childContext->budget = parentContext->budget;
	childContext->ancestry = parentContext->ancestry;
	childContext->ancestry.push_back(childIdentity);
	return childContext;
}

std::vector<std::shared_ptr<Effect>> Magic::addEffect(
	std::shared_ptr<Magic> srcMagic,
	std::shared_ptr<GameElement> user,
	Point from,
	Point to,
	int lvl,
	int damage,
	int evade,
	int launcher,
	std::shared_ptr<GameElement> target,
	std::shared_ptr<MagicDispatchContext> dispatchContext)
{
	if (srcMagic == nullptr)
	{
		return {};
	}
	if (lvl < 1)
	{
		lvl = 1;
	}
	else if (lvl > MAGIC_MAX_LEVEL)
	{
		lvl = MAGIC_MAX_LEVEL;
	}
	if (dispatchContext == nullptr)
	{
		dispatchContext = createRootDispatchContext(srcMagic);
	}
	auto originalMagic = srcMagic;
	auto changeMagic = resolveChangeMagic(srcMagic, user, lvl);
	if (changeMagic != originalMagic)
	{
		auto changeContext = createDerivedDispatchContext(dispatchContext, changeMagic, "ChangeMagic");
		if (changeContext != nullptr)
		{
			auto caster = std::dynamic_pointer_cast<NPC>(user);
			if (caster != nullptr)
			{
				caster->consumeChangeMagicHitCount(*srcMagic);
			}
			srcMagic = changeMagic;
			dispatchContext = changeContext;
			damage = getPrimaryDamageForMagic(srcMagic, user, lvl, damage);
		}
	}
	damage += srcMagic->level[lvl].effectExt;
	damage = applyCasterMagicEffectBonus(user, *srcMagic, damage);
	auto finish = [&](std::vector<std::shared_ptr<Effect>> effects) -> std::vector<std::shared_ptr<Effect>>
	{
		for (auto& effect : effects)
		{
			if (effect != nullptr)
			{
				effect->magicDispatchContext = dispatchContext;
			}
		}
		attachCarryUserToEffect(srcMagic, effects, user);
		const auto& linked = srcMagic->getLinkedLevel(lvl);
		if (linked.secondMagic != nullptr && gm != nullptr && gm->effectManager != nullptr)
		{
			auto secondContext = createDerivedDispatchContext(dispatchContext, linked.secondMagic, "SecondMagic");
			if (secondContext != nullptr)
			{
				gm->effectManager->addDelayedMagic(
					linked.secondMagic,
					user,
					from,
					to,
					lvl,
					launcher,
					target,
					linked.secondMagicDelay,
					secondContext);
			}
		}
		applyRandMagic(srcMagic, user, from, to, lvl, evade, launcher, target, dispatchContext);
		applyMagicSideEffect(srcMagic, user, lvl, damage);
		applyMagicJumpToTarget(srcMagic, user, to, lvl, launcher, dispatchContext);
		applyMagicDieAfterUse(srcMagic, user);
		return effects;
	};

	if (srcMagic->level[lvl].moveKind != mmkSelf && srcMagic->level[lvl].moveKind != mmkTimeStop && srcMagic->level[lvl].moveKind != mmkFullScreen)
	{
		int castDistance = gm->map->calDistance(from, to);
		if (castDistance > MAGIC_MAX_CAST_DISTANCE)
		{
			Point pos = from;
			for (int i = 0; i < MAGIC_MAX_CAST_DISTANCE; i++)
			{
				if (pos == to)
				{
					break;
				}
				pos = Map::getSubPoint(pos, NPC::getDirection(pos, to));
			}
			to = pos;
		}
	}

	if (srcMagic->bodyRadius > 0 && target != nullptr && gm != nullptr && gm->objectManager != nullptr)
	{
		auto bodies = gm->objectManager->takeBodiesInRadius(target->position, srcMagic->bodyRadius);
		if (bodies.empty())
		{
			return finish({});
		}

		auto bodyMagic = std::make_shared<Magic>();
		bodyMagic->copy(*srcMagic);
		bodyMagic->bodyRadius = 0;

		std::vector<std::shared_ptr<Effect>> ret;
		for (auto body : bodies)
		{
			if (body == nullptr)
			{
				continue;
			}
			auto bodyEffects = Magic::addEffect(
				bodyMagic,
				user,
				body->getPosition(),
				to,
				lvl,
				damage,
				evade,
				launcher,
				target,
				dispatchContext);
			ret.insert(ret.end(), bodyEffects.begin(), bodyEffects.end());
		}
		return finish(ret);
	}

	if (srcMagic->reviveBodyRadius > 0 && gm != nullptr && gm->objectManager != nullptr && gm->npcManager != nullptr)
	{
		auto bodies = gm->objectManager->takeBodiesInRadius(to, srcMagic->reviveBodyRadius);
		int revivedCount = 0;
		for (auto body : bodies)
		{
			if (body == nullptr || body->reviveNpcIni.empty())
			{
				continue;
			}
			if (srcMagic->reviveBodyMaxCount > 0 && revivedCount >= srcMagic->reviveBodyMaxCount)
			{
				break;
			}

			auto npc = std::make_shared<NPC>();
			std::unique_ptr<char[]> data;
			int len = File::readFile(NPC_INI_FOLDER + body->reviveNpcIni, data);
			if (data != nullptr && len > 0)
			{
				INIReader ini(data);
				npc->initFromIni(&ini, "Init");
			}

			auto playerUser = std::dynamic_pointer_cast<Player>(user);
			auto npcUser = std::dynamic_pointer_cast<NPC>(user);
			npc->relation = (playerUser != nullptr || (npcUser != nullptr && npcUser->relation == nrFriendly)) ? nrFriendly : nrHostile;
			npc->direction = body->direction;
			npc->lifeMilliseconds = srcMagic->reviveBodyLifeMilliseconds;
			gm->npcManager->addNPC(npc);
			npc->setPosition(body->getPosition(), false);
			revivedCount++;
		}
		return finish({});
	}

	applyBeginPositionRules(*srcMagic, user, from, to);
	if (srcMagic->meteorMove > 0)
	{
		from = to;
	}
	if (hasRoundMove(*srcMagic))
	{
		return finish(addRoundMoveEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
	}
	if (srcMagic->getLinkedLevel(lvl).jumpToTarget > 0)
	{
		return finish({});
	}

	switch (srcMagic->level[lvl].moveKind)
	{
	case mmkPoint:
	{
		{
			return finish(addPointEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;	
	}
	case mmkFly:
	{
		{
			return finish(addFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkFlyContinuous:
	{	
		{
			return finish(addContinuousFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkCircle:
	{
		{
			return finish(addCircleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkHeartCircle:
	{
		{
			return finish(addHeartCircleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkHelixCircle:
	{
		{
			return finish(addHelixCircleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkSector:
	{
		{
			return finish(addSectorEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, false));
		}
		break;
	}
	case mmkRandSector:
	{
		{
			return finish(addSectorEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, true));
		}
		break;
	}
	case mmkLine:
	{
		{
			return finish(addLineEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkMoveLine:
	{
		{
			return finish(addMoveLineEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkVMove:
	{
		{
			return finish(addVMoveEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkRegion:
	{
		{
			switch (srcMagic->level[lvl].region)
			{
			case mrSquare:
			{
				return finish(addSquareEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
				break;
			}
			case mrWave:
			{
				return finish(addWaveEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
				break;
			}
			case mrCross:
			{
				return finish(addCrossEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
				break;
			}
			case mrTriangle:
			{
				return finish(addTriangleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
				break;
			}
			case mrVType:
			{
				return finish(addVTypeEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
				break;
			}
			case mrRegionFile:
			{
				return finish(addRegionFileEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
				break;
			}
			default:
				break;
			}
		}
		break;
	}	
	case mmkSelf:
	{
		{
			return finish(addSelfEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, srcMagic->level[lvl].specialKind));
		}
		break;
	}
	case mmkTimeStop:
	{
		{
			return finish(addSelfEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, srcMagic->level[lvl].specialKind));
		}
		break;
	}
	case mmkFullScreen:
	{
		{
			return finish(addFullScreenEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkFollow:
	{
		{
			return finish(addFollowEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, target));
		}
		break;
	}
	case mmkThrow:
	{
		{
			return finish(addThrowEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkTrail:
	{
		{
			gm->effectManager->addTrailMagic(srcMagic, user, lvl, damage, evade, launcher, dispatchContext);
		}
		break;
	}
	case mmkTransport:
	{
		{
			return finish(addTransportEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	case mmkControl:
	{
		{
			return finish(addControlEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, target));
		}
		break;
	}
	case mmkSummon:
	{
		{
			return finish(addSummonEffect(srcMagic, user, from, to, lvl, damage, evade, launcher));
		}
		break;
	}
	default:
		break;
	}
	return finish({});
}

std::vector<std::shared_ptr<Effect>> Magic::addPointEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->level = lvl;
	e->user = user;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = to;
	e->src = e->position;
    int dir = 0;
    if (srcMagic->flyImage != nullptr)
    {
        dir = getDirection(from, to, srcMagic->flyImage->directions);
    }
    else
    {
        dir = getDirection(from, to);
    }
	e->direction = dir;
	if (srcMagic->level[lvl].lifeFrame <= 0)
	{
		e->lifeTime = e->getFlyingImageTime();
	}
	else
	{
		e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
	}
	e->launcherKind = launcher;
	e->damage = damage;
	e->evade = evade;

	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		e->beginFly();
	}
	e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addFlyEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->level = lvl;
	e->user = user;
	e->initFromMagic(srcMagic);

	bool zeroMoveDirection = from == to && preserveZeroMoveDirection(*srcMagic);
	if (from == to && !zeroMoveDirection)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = getDirection(from, to);
	Point src = from;
	if (!zeroMoveDirection)
	{
		from = Map::getSubPoint(from, NPC::getDirection(from, to));
	}
	if (from != to)
	{
        if (srcMagic->flyImage != nullptr)
        {
            dir = getDirection(from, to, srcMagic->flyImage->directions);
        }
		else
        {
            dir = getDirection(from, to);
        }
		e->flyingDirection = Map::getTilePosition(to, from);
	}
	else
	{
        if (srcMagic->flyImage != nullptr)
        {
            dir = getDirection(src, to, srcMagic->flyImage->directions);
        }
        else
        {
            dir = getDirection(src, to);
        }
		e->flyingDirection = Map::getTilePosition(to, src);
	}
	if (srcMagic->level[lvl].lifeFrame <= 0)
	{
		e->lifeTime = e->getFlyingImageTime();
	}
	else
	{
		e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
	}
	e->direction = dir;
	e->position = from;
	e->src = from;
	e->damage = damage;
	e->evade = evade;

	e->launcherKind = launcher;
	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		//e->doing = ekFlying;
		e->beginFly();
	}
	e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addContinuousFlyEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	bool zeroMoveDirection = from == to && preserveZeroMoveDirection(*srcMagic);
	if (from == to && !zeroMoveDirection)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = getDirection(from, to);
	Point src = from;
	if (!zeroMoveDirection)
	{
		from = Map::getSubPoint(from, NPC::getDirection(from, to));
	}
	if (from != to)
	{
		dir = getDirection(from, to);
	}
	gm->effectManager->setPaused(true);
	for (int i = 0; i < lvl; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);

        if (from != to)
        {
            if (srcMagic->flyImage != nullptr)
            {
                dir = getDirection(from, to, srcMagic->flyImage->directions);
            }
            else
            {
                dir = getDirection(from, to);
            }
            e->flyingDirection = Map::getTilePosition(to, from);
        }
        else
        {
            if (srcMagic->flyImage != nullptr)
            {
                dir = getDirection(src, to, srcMagic->flyImage->directions);
            }
            else
            {
                dir = getDirection(src, to);
            }
            e->flyingDirection = Map::getTilePosition(to, src);
        }
		e->direction = dir;
		e->position = from;
		e->src = e->position;
		e->damage = damage;
		e->evade = evade;

		e->launcherKind = launcher;
		e->waitTime += i * MAGIC_CONTINUOUS_INTERVAL;
		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}
		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	float angle = -M_PI;
	gm->effectManager->setPaused(true);
	for (size_t i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);
        if (srcMagic->flyImage != nullptr)
        {
            e->direction = getDirection(-angle - M_PI / 2, srcMagic->flyImage->directions);
        }
        else
        {
            e->direction = getDirection(-angle - M_PI / 2);
        }
		e->flyingDirection.x = (int)(cos(angle) * 1000.0);// / TILE_HEIGHT);
		e->flyingDirection.y = (int)(-sin(angle) * 1000.0);// / TILE_WIDTH);
		e->position = from;// = Map::getSubPoint(from, NPC::getDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addHeartCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	float angle = -M_PI;
	gm->effectManager->setPaused(true);
	for (size_t i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);

        if (srcMagic->flyImage != nullptr)
        {
            e->direction = getDirection(-angle - M_PI / 2, srcMagic->flyImage->directions);
        }
        else
        {
            e->direction = getDirection(-angle - M_PI / 2);
        }
		e->flyingDirection.x = (int)(cos(angle) * 1000.0);// / TILE_HEIGHT);
		e->flyingDirection.y = (int)(-sin(angle) * 1000.0);// / TILE_WIDTH);
		e->position = from;// = Map::getSubPoint(from, NPC::getDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}

		if (i < MAGIC_CIRCLE_COUNT / 4)
		{
			int count = i;
			e->waitTime += (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DELAY;
			e->speed = int(floor(e->speed * (1.0 - count * MAGIC_HEART_DECAY) + 0.5));
		}
		else if (i < MAGIC_CIRCLE_COUNT / 2)
		{
			int count = i - MAGIC_CIRCLE_COUNT / 4;
			e->waitTime += count * MAGIC_HEART_DELAY;
			e->speed = int(floor(e->speed * (1.0 - (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DECAY) + 0.5));
		}
		else if (i < 3 * MAGIC_CIRCLE_COUNT / 4)
		{
			int count = i - MAGIC_CIRCLE_COUNT / 2;
			e->waitTime += count * MAGIC_HEART_DELAY + MAGIC_HEART_DELAY * MAGIC_CIRCLE_COUNT / 4;
			e->speed = int(floor(e->speed * (1.0 + count * MAGIC_HEART_DECAY) + 0.5));
		}
		else
		{
			int count = i - 3 * MAGIC_CIRCLE_COUNT / 4;
			e->waitTime += (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DELAY + MAGIC_HEART_DELAY * MAGIC_CIRCLE_COUNT / 4;
			e->speed = int(floor(e->speed * (1.0 + (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DECAY) + 0.5));
		}

		if (e->speed <= 0)
		{
			e->speed = 1;
		}

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addHelixCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int startDir = getDirection(from, to, MAGIC_CIRCLE_COUNT);
	startDir -= MAGIC_CIRCLE_COUNT / 4;
	if (startDir < 0)
	{
		startDir += MAGIC_CIRCLE_COUNT;
	}
	startDir = MAGIC_CIRCLE_COUNT - startDir;
	float angle = -M_PI;
	for (int i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);

        if (srcMagic->flyImage != nullptr)
        {
            e->direction = getDirection(-angle - M_PI / 2, srcMagic->flyImage->directions);
        }
        else
        {
            e->direction = getDirection(-angle - M_PI / 2);
        }
		e->flyingDirection.x = (int)(cos(angle) * 1000);
		e->flyingDirection.y = (int)(-sin(angle) * 1000);
		e->position = from; //Map::getSubPoint(from, NPC::getDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}
		int count = i - startDir;
		if (count < 0)
		{
			count += MAGIC_CIRCLE_COUNT;
		}
		e->waitTime += count * MAGIC_CIRCLE_HELIX_INTERVAL;

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addSectorEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, bool randTime)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	if (from == to && !preserveZeroMoveDirection(*srcMagic))
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = NPC::getDirection(from, to);
	Point src = from;
	//from = Map::getSubPoint(from, NPC::getDirection(from, to));
	auto tempPos = Map::getTilePosition(to, from);
	tempPos.y = (int)round(MapXRatio * tempPos.y);
	float angle = atan2(-tempPos.x, tempPos.y); //dir * M_PI / 4;
	if (lvl < 4)
	{
		angle -= M_PI / 12;
		for (size_t i = 0; i < 3; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
            if (srcMagic->flyImage != nullptr)
            {
                e->direction = getDirection(normalizeAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = getDirection(normalizeAngle(angle));
            }
//			e->direction = getDirection(normalizeAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}
			if (srcMagic->level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();

			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 12;

			ret.push_back(e);
		}
	}
	else if (lvl < 7)
	{
		angle -= M_PI / 10;
		for (size_t i = 0; i < 5; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
            if (srcMagic->flyImage != nullptr)
            {
                e->direction = getDirection(normalizeAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = getDirection(normalizeAngle(angle));
            }
//			e->direction = getDirection(normalizeAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 20;
			ret.push_back(e);
		}
	}
	else if (lvl < MAGIC_MAX_LEVEL)
	{
		angle -= M_PI / 5;
		for (size_t i = 0; i < 7; i++)
		{

			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);

            if (srcMagic->flyImage != nullptr)
            {
                e->direction = getDirection(normalizeAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = getDirection(normalizeAngle(angle));
            }
            //			e->direction = getDirection(normalizeAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 15;
			ret.push_back(e);
		}
	}
	else
	{
		angle -= M_PI / 4;
		for (size_t i = 0; i < 9; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
            if (srcMagic->flyImage != nullptr)
            {
                e->direction = getDirection(normalizeAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = getDirection(normalizeAngle(angle));
            }
//			e->direction = getDirection(normalizeAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 16;
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addLineEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	{
		Point tempTo = to;
		if (from == to && !preserveZeroMoveDirection(*srcMagic))
		{
			tempTo = Map::getSubPoint(to, 0);
		}
		int dir = NPC::getDirection(from, tempTo);
		int magicDir = dir * 2;
		dir += 2;
		if (dir > 7)
		{
			dir -= 8;
		}
		int dir2 = dir + 4;
		if (dir2 > 7)
		{
			dir2 -= 8;
		}
		Point pos = to;
		for (int i = 0; i < lvl; i++)
		{
			pos = Map::getSubPoint(pos, dir2);
		}
		for (int i = 0; i < lvl * 2 + 1; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
			e->direction = magicDir;
			e->flyingDirection = { 0, 0 };
			if (srcMagic->level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			e->position = pos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (launcher == lkSelf)
			{
				e->waitTime += PLAYER_MAGIC_DELAY;
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			pos = Map::getSubPoint(pos, dir);
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addMoveLineEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	{
		Point tempTo = to;
		if (from == to && !preserveZeroMoveDirection(*srcMagic))
		{
			tempTo = Map::getSubPoint(to, 0);
		}
		int srcDir = NPC::getDirection(from, tempTo);
		int dir = srcDir;
		int magicDir = dir * 2;
		dir += 2;
		if (dir > 7)
		{
			dir -= 8;
		}
		int dir2 = dir + 4;
		if (dir2 > 7)
		{
			dir2 -= 8;
		}
		Point pos = from;
		for (int i = 0; i < lvl; i++)
		{
			pos = Map::getSubPoint(pos, dir2);
		}
		for (int i = 0; i < lvl * 2 + 1; i++)
		{

			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
			e->direction = magicDir;
			e->flyingDirection = Map::getTilePosition(Map::getSubPoint({ 0, 0 }, srcDir), { 0, 0 });
			e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
			e->position = pos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (srcMagic->level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			pos = Map::getSubPoint(pos, dir);
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addVMoveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	Point tempTo = to;
	if (from == to && !preserveZeroMoveDirection(*srcMagic))
	{
		tempTo = Map::getSubPoint(to, 0);
	}
	int srcDir = NPC::getDirection(from, tempTo);
	int magicDir = srcDir * 2;
	if (srcMagic->flyImage != nullptr)
	{
		magicDir = getDirection(from, tempTo, srcMagic->flyImage->directions);
	}
	else
	{
		magicDir = getDirection(from, tempTo);
	}
	Point flyingDirection = Map::getTilePosition(Map::getSubPoint({ 0, 0 }, srcDir), { 0, 0 });
	flyingDirection.y = (int)round(MapXRatio * flyingDirection.y);

	auto addMovingEffect = [&](PointEx startOffset)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);
		e->direction = magicDir;
		e->flyingDirection = flyingDirection;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;
		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}

		Point startPosition = from;
		PointEx normalizedOffset = startOffset;
		e->getNewPosition(from, startOffset, &startPosition, &normalizedOffset);
		e->position = startPosition;
		e->offset = normalizedOffset;
		e->src = startPosition;
		e->srcOffset = normalizedOffset;

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	};

	gm->effectManager->setPaused(true);
	addMovingEffect({ 0.0f, 0.0f });
	for (int i = 1; i <= lvl; i++)
	{
		float distance = (float)i;
		switch (srcDir)
		{
		case 0:
			addMovingEffect({ -32.0f * distance, -16.0f * distance });
			addMovingEffect({ 32.0f * distance, -16.0f * distance });
			break;
		case 1:
			addMovingEffect({ 0.0f, -32.0f * distance });
			addMovingEffect({ 64.0f * distance, 0.0f });
			break;
		case 2:
			addMovingEffect({ 32.0f * distance, -16.0f * distance });
			addMovingEffect({ 32.0f * distance, 16.0f * distance });
			break;
		case 3:
			addMovingEffect({ 0.0f, 32.0f * distance });
			addMovingEffect({ 64.0f * distance, 0.0f });
			break;
		case 4:
			addMovingEffect({ -32.0f * distance, 16.0f * distance });
			addMovingEffect({ 32.0f * distance, 16.0f * distance });
			break;
		case 5:
			addMovingEffect({ -64.0f * distance, 0.0f });
			addMovingEffect({ 0.0f, 32.0f * distance });
			break;
		case 6:
			addMovingEffect({ -32.0f * distance, -16.0f * distance });
			addMovingEffect({ -32.0f * distance, 16.0f * distance });
			break;
		case 7:
			addMovingEffect({ 0.0f, -32.0f * distance });
			addMovingEffect({ -64.0f * distance, 0.0f });
			break;
		default:
			break;
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addRoundMoveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (srcMagic == nullptr || gm == nullptr || gm->effectManager == nullptr)
	{
		return ret;
	}

	int count = srcMagic->roundMoveCount;
	if (count <= 0)
	{
		count = 1;
	}

	gm->effectManager->setPaused(true);
	for (int i = 0; i < count; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);
		e->flyingDirection = { 0, 0 };
		e->position = user != nullptr ? user->position : from;
		e->offset = user != nullptr ? user->offset : PointEx{ 0.0f, 0.0f };
		e->src = e->position;
		e->srcOffset = e->offset;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;
		e->initRoundMove(i);
		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}
		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addSquareEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, int range)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int mrange = 3 + ((lvl - 1) / 3) * 2;
	if (range >= 0)
	{
		mrange = range;
	}	
	Point pos = to;
	for (int i = 0; i < (int)(mrange / 2); i++)
	{
		pos = Map::getSubPoint(pos, 0);
	}
	for (int i = 0; i < mrange; i++)
	{
		Point newPos = pos;
		for (int j = 0; j < mrange; j++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = newPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			e->lifeTime = e->getFlyinUTime();
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			newPos = Map::getSubPoint(newPos, 5);
			ret.push_back(e);
		}
		pos = Map::getSubPoint(pos, 3);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addWaveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int hrange = 3 + ((lvl - 1) / 3) * 2;
	int wrange = 2;
	Point tempTo = to;
	if (from == to && !preserveZeroMoveDirection(*srcMagic))
	{
		tempTo = Map::getSubPoint(to, 0);
	}
	int srcDir = NPC::getDirection(from, tempTo);
	//int magicDir = getDirection(from, tempTo);
	int dir = srcDir;
	int magicDir = dir * 2;
	dir += 2;
	if (dir > 7)
	{
		dir -= 8;
	}
	int dir2 = dir + 4;
	if (dir2 > 7)
	{
		dir2 -= 8;
	}
	Point pos = Map::getSubPoint(from, srcDir);
	for (int i = 0; i < wrange; i++)
	{
		pos = Map::getSubPoint(pos, dir2);
	}
	for (int i = 0; i < hrange; i++)
	{
		Point newPos = pos;
		for (int j = 0; j < wrange * 2 + 1; j++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = newPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			e->lifeTime = e->getFlyinUTime();
			e->waitTime += i * 60;
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(EFFECT_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			newPos = Map::getSubPoint(newPos, dir);
			ret.push_back(e);
		}
		pos = Map::getSubPoint(pos, srcDir);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addCrossEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int mrange = 3 + ((lvl - 1) / 3) * 2;
	Point pos[4] = { from, from, from, from };
	for (int i = 0; i < mrange; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			pos[j] = Map::getSubPoint(pos[j], j * 2 + 1);
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = pos[j];
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			e->lifeTime = e->getFlyinUTime();
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(EFFECT_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addTriangleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	Point tempTo = to;
	if (from == to && !preserveZeroMoveDirection(*srcMagic))
	{
		tempTo = Map::getSubPoint(to, 0);
	}
	int srcDir = NPC::getDirection(from, tempTo);
	int dir = srcDir;
	int columnDir = dir + 2;
	if (columnDir > 7)
	{
		columnDir -= 8;
	}
	int rowCount = 3 + ((lvl - 1) / 3) * 2;
	Point rowPos = from;
	const int magicDelayPerRow = 60;
	for (int i = 0; i < rowCount; i++)
	{
		rowPos = Map::getSubPoint(rowPos, srcDir);
		int columnCount = 1 + i * 2;
		int halfColumn = columnCount / 2;
		Point colPos = rowPos;
		for (int j = 0; j < halfColumn; j++)
		{
			colPos = Map::getSubPoint(colPos, columnDir);
		}
		for (int j = 0; j < columnCount; j++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = colPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			e->lifeTime = e->getFlyinUTime();
			e->waitTime += i * magicDelayPerRow;
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(EFFECT_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			int oppositeDir = columnDir + 4;
			if (oppositeDir > 7)
			{
				oppositeDir -= 8;
			}
			colPos = Map::getSubPoint(colPos, oppositeDir);
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addVTypeEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	Point tempTo = to;
	if (from == to && !preserveZeroMoveDirection(*srcMagic))
	{
		tempTo = Map::getSubPoint(to, 0);
	}
	int srcDir = NPC::getDirection(from, tempTo);
	auto tiles = getVTypeMagicRegionTiles(from, srcDir, lvl);

	gm->effectManager->setPaused(true);
	for (const auto& tile : tiles)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->user = user;
		e->level = lvl;
		e->initFromMagic(srcMagic);
		e->direction = 0;
		e->flyingDirection = { 0, 0 };
		e->position = tile.position;
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;
		e->lifeTime = e->getFlyinUTime();
		e->waitTime += tile.delayMilliseconds;
		if (e->lifeTime == 0)
		{
			e->lifeTime = (unsigned int)(EFFECT_FRAME_TIME * 2);
		}
		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addRegionFileEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (!srcMagic->regionFileLoaded || srcMagic->regionFile.empty())
	{
		return ret;
	}

	Point tempTo = to;
	if (from == to && !preserveZeroMoveDirection(*srcMagic))
	{
		tempTo = Map::getSubPoint(to, 0);
	}
	int srcDir = NPC::getDirection(from, tempTo);
	const std::vector<MagicRegionFileItem>* items = nullptr;
	if (srcDir >= 0 && srcDir < (int)srcMagic->regionFile.size() && !srcMagic->regionFile[srcDir].empty())
	{
		items = &srcMagic->regionFile[srcDir];
	}
	else if (!srcMagic->regionFile[0].empty())
	{
		items = &srcMagic->regionFile[0];
	}
	if (items == nullptr)
	{
		return ret;
	}

	gm->effectManager->setPaused(true);
	for (const auto& item : *items)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->user = user;
		e->level = lvl;
		e->initFromMagic(srcMagic);
		e->direction = 0;
		e->flyingDirection = { 0, 0 };
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;
		e->lifeTime = e->getFlyinUTime();
		if (e->lifeTime == 0)
		{
			e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
		}
		e->waitTime += item.delay;

		Point effectPosition = to;
		PointEx effectOffset = item.offset;
		e->getNewPosition(to, item.offset, &effectPosition, &effectOffset);
		e->position = effectPosition;
		e->offset = effectOffset;
		e->src = effectPosition;
		e->srcOffset = effectOffset;

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::shared_ptr<Effect> Magic::createFixedEffect(
	std::shared_ptr<Magic> srcMagic,
	std::shared_ptr<GameElement> user,
	Point position,
	PointEx offset,
	int lvl,
	int damage,
	int evade,
	int launcher,
	std::shared_ptr<MagicDispatchContext> dispatchContext)
{
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->user = user;
	e->level = lvl;
	e->initFromMagic(srcMagic, dispatchContext);
	e->direction = 0;
	e->flyingDirection = { 0, 0 };
	e->launcherKind = launcher;
	e->damage = applyCasterMagicEffectBonus(user, *srcMagic, damage);
	e->evade = evade;
	e->lifeTime = e->getFlyinUTime();

	e->position = position;
	e->offset = offset;
	e->src = position;
	e->srcOffset = offset;

	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		e->beginFly();
	}
	if (e->lifeTime == 0)
	{
		e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
	}
	e->calDest();
	return e;
}

std::vector<std::shared_ptr<Effect>> Magic::addSelfEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, int specialKind)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (!NPCManager::isManagedEffectCaster(user))
	{
		return ret;
	}
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->user = user;
	e->level = lvl;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = user->position;
	e->src = e->position;
	e->offset = user->offset;
	e->direction = 0;
	e->lifeTime = 0;
	e->launcherKind = launcher;

	e->damage = damage;
	e->evade = evade;
	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		e->doing = ekExploding;
		e->lifeTime = e->getExplodinUTime();
	}
	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	switch (specialKind)
	{
	case mskAddLife:
		if (auto target = std::dynamic_pointer_cast<NPC>(user))
		{
			if (damage < 0)
			{
				target->addLifeWithoutDeath(damage);
			}
			else
			{
				target->addLife(damage);
			}
		}
		break;
	case mskAddThew:
		(std::dynamic_pointer_cast<NPC>(user))->addThew(damage);
		break;
	case mskAddShield:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			auto shieldDuration = e->getExplodinUTime() + e->waitTime;
			if (auto oldShield = npc->shieldEffect.lock())
			{
				if (oldShield->magic.iniName == srcMagic->iniName)
				{
					auto refreshDuration = e->getExplodinUTime();
					npc->shieldBeginTime = npc->getTime();
					npc->shieldLastTime = refreshDuration;
					npc->shieldLife = damage;
					oldShield->vanishing = false;
					oldShield->beginExplode(oldShield->position);
					gm->effectManager->deleteEffect(e);
					ret.clear();
					break;
				}
				oldShield->vanishing = true;
				oldShield->beginExplode(oldShield->position);
			}
			npc->shieldBeginTime = npc->getTime();
			npc->shieldLastTime = shieldDuration;
			npc->shieldLife = damage;
			npc->shieldEffect = e;
		}
		break;
	}
	case mskAddDamageReduceShield:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			bool refreshed = false;
			for (auto it = npc->shieldEffects.begin(); it != npc->shieldEffects.end(); )
			{
				if (auto oldShield = it->lock())
				{
					if (oldShield->magic.iniName == srcMagic->iniName)
					{
						oldShield->vanishing = false;
						oldShield->beginExplode(oldShield->position);
						refreshed = true;
						break;
					}
					++it;
				}
				else
				{
					it = npc->shieldEffects.erase(it);
				}
			}
			if (refreshed)
			{
				gm->effectManager->deleteEffect(e);
				ret.clear();
			}
			else
			{
				for (auto it = npc->shieldEffects.begin(); it != npc->shieldEffects.end(); ++it)
				{
					if (auto oldShield = it->lock())
					{
						oldShield->vanishing = true;
						oldShield->beginExplode(oldShield->position);
					}
				}
				npc->shieldEffects.clear();
				npc->shieldEffects.push_back(e);
			}
		}
		break;
	}
	case mskInvisibleKeepHidden:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			npc->applyMagicInvisibility(getSelfMagicInvisibilityDuration(*srcMagic, user, lvl), false);
		}
		break;
	}
	case mskInvisibleVisibleWhenAttack:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			npc->applyMagicInvisibility(getSelfMagicInvisibilityDuration(*srcMagic, user, lvl), true);
		}
		break;
	}
	case mskBlockDamage:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			bool refreshed = false;
			for (auto it = npc->shieldEffects.begin(); it != npc->shieldEffects.end(); )
			{
				if (auto oldShield = it->lock())
				{
					if (oldShield->magic.iniName == srcMagic->iniName)
					{
						oldShield->vanishing = false;
						oldShield->beginExplode(oldShield->position);
						refreshed = true;
						break;
					}
					++it;
				}
				else
				{
					it = npc->shieldEffects.erase(it);
				}
			}
			if (refreshed)
			{
				gm->effectManager->deleteEffect(e);
				ret.clear();
			}
			else
			{
				npc->shieldEffects.push_back(e);
			}
		}
		break;
	}
	case mskMorphByEffect:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			npc->applyTemporaryMorph(*srcMagic, getSelfMagicMorphDuration(*srcMagic, lvl));
		}
		break;
	}
	case mskClearAbnormalState:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			npc->clearAbnormalState();
		}
		break;
	}
	case mskChangeFlyIni:
	{
		auto npc = std::dynamic_pointer_cast<NPC>(user);
		if (npc)
		{
			npc->applyTemporaryFlyIniChange(e);
		}
		break;
	}
	case mskChangeAttributes:
	{
		auto player = std::dynamic_pointer_cast<Player>(user);
		if (player != nullptr && gm != nullptr && gm->global.feature.rageSystem)
		{
			player->attributeChangeEffect = e;
		}
		break;
	}
	default:
		break;
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addFullScreenEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	Point effectPosition = user != nullptr ? user->position : from;
	PointEx effectOffset = user != nullptr ? user->offset : PointEx{ 0, 0 };
	e->user = user;
	e->level = lvl;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = effectPosition;
	e->src = e->position;
	e->offset = effectOffset;
	e->direction = 0;
	e->lifeTime = 0;
	e->launcherKind = launcher;

	e->damage = damage;
	e->evade = evade;
	e->lifeTime = e->getSuperImageTime();
	e->calDest();
	gm->npcManager->setPaused(true);
	gm->player->setPaused(true);
	gm->objectManager->setPaused(true);
	gm->effectManager->pauseAllEffect();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	e->doing = ekSuperMode;
	e->playSound(ekSuperMode);
	e->run();
	gm->npcManager->setPaused(false);
	gm->player->setPaused(false);
	gm->objectManager->setPaused(false);
	gm->effectManager->resumeAllEffect();
	auto caster = std::dynamic_pointer_cast<NPC>(user);

	std::vector<std::shared_ptr<NPC>> damageList;
	auto addDamageTarget = [&](std::shared_ptr<NPC> npc) {
		if (npc == nullptr || npc == caster || npc->nowAction == acDeath || npc->nowAction == acHide)
		{
			return;
		}
		if (!NPCManager::canLauncherHitNPC(launcher, npc, user))
		{
			return;
		}
		if (gm->map->calDistance(npc->getPosition(), effectPosition) <= MAGIC_MAX_CAST_DISTANCE)
		{
			damageList.push_back(npc);
		}
	};

	addDamageTarget(gm->player);
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		auto npc = gm->npcManager->npcList[i];
		if (npc != nullptr && (npc->kind == nkBattle || (npc->kind == nkPartner && gm->global.data.PartnerCombat)))
		{
			addDamageTarget(npc);
		}
	}
	std::shared_ptr<Effect> oriE = e;
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	for (size_t i = 0; i < damageList.size(); i++)
	{
		damageList[i]->directHurt(oriE);
		e = std::make_shared<Effect>();
		e->user = user;
		e->level = lvl;
		e->initFromMagic(srcMagic);
		e->flyingDirection = { 0, 0 };
		e->position = damageList[i]->getPosition();
		e->src = e->position;
		e->offset = damageList[i]->getOffset();
		e->direction = 0;
		e->lifeTime = 0;
		e->launcherKind = launcher;

		e->damage = damage;
		e->evade = evade;
		e->lifeTime = e->getExplodinUTime();
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginExplode(e->position);		
	}
	gm->effectManager->setPaused(false);
	gm->effectManager->deleteEffect(oriE);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addFollowEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, std::shared_ptr<GameElement> target)
{
	auto targetNPC = std::dynamic_pointer_cast<NPC>(target);
	if (targetNPC != nullptr && !NPCManager::canLauncherHitNPC(launcher, targetNPC, user))
	{
		target.reset();
	}

	auto ret = addFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
	for (size_t i = 0; i < ret.size(); i++)
	{
		(ret[i])->target = target;
		(ret[i])->dest = to;
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addThrowEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	auto ret = addFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
	for (size_t i = 0; i < ret.size(); i++)
	{
		if ((ret[i])->doing == ekFlying)
		{
			(ret[i])->doing = ekThrowing;
		}
		(ret[i])->dest = to;
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addThrowExplodeEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	auto ret = addSquareEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, (int)((lvl - 1) / 3 + 1));
	for (size_t i = 0; i < ret.size(); i++)
	{
		auto e = ret[i];
		e->waitTime = 0;
		e->doing = ekFlying;
		e->lifeTime = e->getFlyinUTime();
		if (e->lifeTime == 0)
		{
			e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
		}
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addTransportEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (srcMagic == nullptr || gm == nullptr || gm->effectManager == nullptr)
	{
		return ret;
	}

	auto caster = std::dynamic_pointer_cast<NPC>(user);
	if (caster == nullptr
		|| !NPCManager::isManagedEffectCaster(caster)
		|| caster->isTransporting())
	{
		return ret;
	}

	auto e = createFixedEffect(srcMagic, user, caster->getPosition(), { 0.0f, 0.0f }, lvl, damage, evade, launcher);
	if (e == nullptr)
	{
		return ret;
	}

	e->dest = to;
	e->destOffset = { 0.0f, 0.0f };
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	caster->applyTransportEffect(e);
	ret.push_back(e);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addControlEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, std::shared_ptr<GameElement> target)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (srcMagic == nullptr || gm == nullptr || gm->effectManager == nullptr)
	{
		return ret;
	}

	auto player = std::dynamic_pointer_cast<Player>(user);
	auto targetNPC = std::dynamic_pointer_cast<NPC>(target);
	if (player == nullptr
		|| targetNPC == nullptr
		|| !NPCManager::isManagedEffectCaster(player))
	{
		return ret;
	}
	if (!canControlMagicTarget(targetNPC->level, srcMagic->maxLevel, !targetNPC->isDying() && !targetNPC->isHiding()))
	{
		return ret;
	}
	if (gm->npcManager == nullptr || !gm->npcManager->findNPC(targetNPC))
	{
		return ret;
	}

	auto e = createFixedEffect(srcMagic, user, player->getPosition(), player->getOffset(), lvl, damage, evade, launcher);
	if (e == nullptr)
	{
		return ret;
	}

	e->dest = player->getPosition();
	e->destOffset = player->getOffset();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	player->beginControlCharacter(targetNPC, e);
	ret.push_back(e);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addSummonEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (srcMagic == nullptr
		|| !NPCManager::isManagedEffectCaster(user)
		|| gm == nullptr
		|| gm->effectManager == nullptr)
	{
		return ret;
	}

	auto e = std::make_shared<Effect>();
	e->level = lvl;
	e->user = user;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = to;
	e->src = to;
	e->dest = to;
	e->direction = getDirection(from, to);
	e->launcherKind = launcher;
	e->damage = damage;
	e->evade = evade;
	e->lifeTime = srcMagic->keepMilliseconds;
	e->doing = ekExploding;
	if (!e->beginSummon(to))
	{
		return ret;
	}

	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

float Magic::getAngle(Point from, Point to)
{
	Point pos = Map::getTilePosition(to, from, { 0, 0 }, { 0, 0 });
	PointEx dir;
	dir.x = ((float)pos.x) / TILE_WIDTH * MapXRatio;
	dir.y = ((float)pos.y) / TILE_HEIGHT;
	dir.x = -dir.x;
	return atan2(dir.x, dir.y);
}

int Magic::getDirection(Point from, Point to)
{
	return getDirection(getAngle(from, to));
}

int Magic::getDirection(float angle)
{
	return getDirection(angle, 16);
}

int Magic::getDirection(Point from, Point to, int maxDir)
{
	return getDirection(getAngle(from, to), maxDir);
}

int Magic::getDirection(float angle, int maxDir)
{
	if (angle < 0)
	{
		angle += 2 * M_PI;
	}
	angle += M_PI / maxDir;
	int result = (int)(angle / (2 * M_PI / maxDir));
	if (result > maxDir)
	{
		result -= maxDir;
	}
    else if (result < 0)
    {
        result += maxDir;
    }
	return result;
}

UTime Magic::getSpecialKindDurationMilliseconds(int lvl) const
{
	int effectLevel = clampMagicLevel(lvl);
	if (level[effectLevel].specialKindMilliseconds > 0)
	{
		return level[effectLevel].specialKindMilliseconds;
	}
	return static_cast<UTime>(effectLevel + 1) * 1000;
}

const MagicLinkedLevel& Magic::getLinkedLevel(int lvl) const
{
	return linkedLevel[clampMagicLevel(lvl)];
}

const std::string& Magic::getExplodeMagicFileForLevel(int lvl) const
{
	return explodeMagicFilesByLevel[clampMagicLevel(lvl)];
}

std::shared_ptr<Magic> Magic::getExplodeMagicForLevel(int lvl) const
{
	return explodeMagicsByLevel[clampMagicLevel(lvl)];
}

void Magic::copy(Magic & magic)
{
#define copyData(a); a = magic.a;

	copyData(iniName);
	copyData(experienceOwnerMagicFile);
	copyData(name);
	copyData(type);
	copyData(injuryType);
	copyData(hasInjuryType);
	copyData(spriteType);
	copyData(hasSpriteType);
	copyData(attribute);
	copyData(hasAttribute);
	copyData(scriptFile);
	copyData(hasScriptFile);
	copyData(intro);
	copyData(image);
	copyData(icon);
	copyData(flyingImage);
	copyData(flyingSound);
	copyData(vanishImage);
	copyData(vanishSound);
	copyData(leapImageFile);
	copyData(explodeMagicFile);
	copyData(parasiticMagicFile);
	copyData(randMagicFile);
	copyData(secondMagicFile);
	copyData(magicWhenNewPositionFile);
	copyData(magicToUseWhenKillEnemyFile);
	copyData(bounceFlyEndMagicFile);
	copyData(changeMagicFile);
	copyData(jumpEndMagicFile);
	copyData(replaceMagic);
	copyData(specialKind9ReplaceFlyIni);
	copyData(specialKind9ReplaceFlyIni2);
	copyData(flyIni);
	copyData(flyIni2);
	copyData(magicToUseWhenBeAttackedFile);
	copyData(magicDirectionWhenBeAttacked);
	copyData(hitCountFlyingImageFile);
	copyData(hitCountVanishImageFile);
	copyData(superModeImage);
	copyData(superModeSound);
	copyData(regionFileName);
	copyData(regionFile);
	copyData(regionFileLoaded);
	copyData(keepMilliseconds);
	copyData(maxLevel);
	copyData(goodsName);
	copyData(npcFile);
	copyData(npcIni);
	copyData(maxCount);
	copyData(bodyRadius);
	copyData(disableUse);
	copyData(lifeFullToUse);
	copyData(vibratingScreen);
	copyData(additionalEffect);
	copyData(belong);
	copyData(bounce);
	copyData(bounceHurt);
	copyData(bounceFly);
	copyData(bounceFlySpeed);
	copyData(bounceFlyEndHurt);
	copyData(bounceFlyTouchHurt);
	copyData(magicDirectionWhenBounceFlyEnd);
	copyData(carryUser);
	copyData(carryUserSpriteIndex);
	copyData(hideUserWhenCarry);
	copyData(ball);
	copyData(sticky);
	copyData(solid);
	copyData(discardOppositeMagic);
	copyData(exchangeUser);
	copyData(noSpecialKindEffect);
	copyData(randMagicProbability);
	copyData(sideEffectType);
	copyData(sideEffectPercent);
	copyData(sideEffectProbability);
	copyData(noInterruption);
	copyData(disableMoveMilliseconds);
	copyData(disableSkillMilliseconds);
	copyData(coldMilliSeconds);
	copyData(dieAfterUse);
	copyData(restoreType);
	copyData(restorePercent);
	copyData(restoreProbability);
	copyData(attackAddPercent);
	copyData(defendAddPercent);
	copyData(evadeAddPercent);
	copyData(speedAddPercent);
	copyData(morphMilliseconds);
	copyData(weakMilliseconds);
	copyData(weakAttackPercent);
	copyData(weakDefendPercent);
	copyData(blindMilliseconds);
	copyData(parasitic);
	copyData(parasiticInterval);
	copyData(parasiticMaxEffect);
	copyData(magicDirectionWhenKillEnemy);
	copyData(changeToFriendMilliseconds);
	copyData(attackAll);
	copyData(traceEnemy);
	copyData(traceSpeed);
	copyData(traceEnemyDelayMilliseconds);
	copyData(followMouse);
	copyData(moveImitateUser);
	copyData(moveBack);
	copyData(randomMoveDegree);
	copyData(meteorMove);
	copyData(meteorMoveDir);
	copyData(circleMoveColockwise);
	copyData(circleMoveAnticlockwise);
	copyData(roundMoveColockwise);
	copyData(roundMoveAnticlockwise);
	copyData(roundMoveCount);
	copyData(roundMoveDegreeSpeed);
	copyData(roundRadius);
	copyData(beginAtMouse);
	copyData(beginAtUser);
	copyData(beginAtUserAddDirectionOffset);
	copyData(beginAtUserAddUserDirectionOffset);
	copyData(noExplodeWhenLifeFrameEnd);
	copyData(explodeWhenLifeFrameEnd);
	copyData(passThrough);
	copyData(passThroughWithDestroyEffect);
	copyData(passThroughWall);
	copyData(reviveBodyRadius);
	copyData(reviveBodyMaxCount);
	copyData(reviveBodyLifeMilliseconds);
	copyData(rangeEffect);
	copyData(rangeRadius);
	copyData(rangeSpeedUp);
	copyData(rangeTimeInterval);
	copyData(jumpToTarget);
	copyData(jumpMoveSpeed);
	copyData(hitCountToChangeMagic);
	copyData(hitCountFlyRadius);
	copyData(hitCountFlyAngleSpeed);
	copyData(actionFile);
	copyData(actionShadowFile)
	copyData(useActionFile);
	copyData(attackFile);
	copyData(flyMagicFile);
	copyData(flyInterval);
	copyData(secondMagicDelay);

	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		copyData(explodeMagicFilesByLevel[i]);
		copyData(level[i].effect);
		copyData(level[i].effectExt);
		copyData(level[i].effect2);
		copyData(level[i].effect3);
		copyData(level[i].effectMana);
		copyData(level[i].lifeMax);
		copyData(level[i].thewMax);
		copyData(level[i].manaMax);
		copyData(level[i].attack);
		copyData(level[i].attack2);
		copyData(level[i].attack3);
		copyData(level[i].defend);
		copyData(level[i].defend2);
		copyData(level[i].defend3);
		copyData(level[i].evade);
		copyData(level[i].addThewRestorePercent);
		copyData(level[i].addManaRestorePercent);
		copyData(level[i].addLifeRestorePercent);
		copyData(level[i].rangeAddLife);
		copyData(level[i].rangeAddMana);
		copyData(level[i].rangeAddThew);
		copyData(level[i].rangeAddRage);
		copyData(level[i].hasRangeAddRage);
		copyData(level[i].rangeFreezeMilliseconds);
		copyData(level[i].rangePoisonMilliseconds);
		copyData(level[i].rangePetrifyMilliseconds);
		copyData(level[i].rangeDamage);
		copyData(level[i].leapTimes);
		copyData(level[i].leapFrame);
		copyData(level[i].effectReducePercentage);
		copyData(level[i].levelupExp);
		copyData(level[i].lifeCost);
		copyData(level[i].manaCost);
		copyData(level[i].thewCost);
		copyData(level[i].rageCost);
		copyData(level[i].hasRageCost);
		copyData(level[i].critChanceAddValue);
		copyData(level[i].hasCritChanceAddValue);
		copyData(level[i].critDamageAddPercent);
		copyData(level[i].hasCritDamageAddPercent);
		copyData(level[i].count);
		copyData(level[i].moveKind);
		copyData(level[i].specialKind);
		copyData(level[i].specialKindValue);
		copyData(level[i].specialKindMilliseconds);
		copyData(level[i].alphaBlend);
		copyData(level[i].region);
		copyData(level[i].speed);
		copyData(level[i].flyingLum);
		copyData(level[i].vanishLum);
		copyData(level[i].waitFrame);
		copyData(level[i].lifeFrame);
		copyData(level[i].attackRadius);
	}
	freeResource();
	imageSelfCreated = false;
	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		copyData(linkedLevel[i]);
	}
	copyData(flyImage);
	copyData(explodeImage);
	copyData(superImage);
	copyData(leapImage);
	copyData(useActionImage);
	copyData(flyMagic);
	copyData(explodeMagic);
	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		copyData(explodeMagicsByLevel[i]);
	}
	copyData(parasiticMagic);
	copyData(randMagic);
	copyData(secondMagic);
	copyData(magicWhenNewPosition);
	copyData(magicToUseWhenKillEnemy);
	copyData(bounceFlyEndMagic);
	copyData(changeMagic);
	copyData(jumpEndMagic);
	copyData(hitCountFlyingImage);
	copyData(hitCountVanishImage);
}

void Magic::freeResource()
{
	actionImage = nullptr;

	actionShadow = nullptr;

	useActionImage = nullptr;

	specialMagic = nullptr;
	flyMagic = nullptr;
	explodeMagic = nullptr;
	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		explodeMagicsByLevel[i] = nullptr;
		linkedLevel[i].specialMagic = nullptr;
		linkedLevel[i].flyMagic = nullptr;
		linkedLevel[i].parasiticMagic = nullptr;
		linkedLevel[i].randMagic = nullptr;
		linkedLevel[i].secondMagic = nullptr;
		linkedLevel[i].magicWhenNewPosition = nullptr;
		linkedLevel[i].magicToUseWhenKillEnemy = nullptr;
		linkedLevel[i].bounceFlyEndMagic = nullptr;
		linkedLevel[i].changeMagic = nullptr;
		linkedLevel[i].jumpEndMagic = nullptr;
	}
	parasiticMagic = nullptr;
	randMagic = nullptr;
	secondMagic = nullptr;
	magicWhenNewPosition = nullptr;
	magicToUseWhenKillEnemy = nullptr;
	bounceFlyEndMagic = nullptr;
	changeMagic = nullptr;
	jumpEndMagic = nullptr;

	flyImage = nullptr;
		
	explodeImage = nullptr;
		
	superImage = nullptr;

	leapImage = nullptr;

	hitCountFlyingImage = nullptr;

	hitCountVanishImage = nullptr;
	
}

void Magic::loadRes()
{
	freeResource();
	imageSelfCreated = true;
	flyImage = loadFlyingImage();
	explodeImage = loadVanishImage();
	superImage = loadSuperModeImage();
	leapImage = loadLeapImage();
	hitCountFlyingImage = loadHitCountFlyingImage();
	hitCountVanishImage = loadHitCountVanishImage();
	actionImage = loadActionImage();
	actionShadow = loadActionShadow();
	useActionImage = loadUseActionImage();
}

_shared_imp Magic::loadActionImage()
{
	if (actionFile.empty())
	{
		return nullptr;
	}
	_shared_imp image = loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		actionFile,
		"character",
		NPC_RES_FOLDER_ASF,
		NPC_RES_FOLDER));
	IMP::cropTransparentEdges(image);
	return image;
}

_shared_imp Magic::loadActionShadow()
{
	if (actionShadowFile.empty())
	{
		return nullptr;
	}
	_shared_imp image = loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		actionShadowFile,
		"character",
		NPC_RES_FOLDER_ASF,
		NPC_RES_FOLDER));
	IMP::cropTransparentEdges(image);
	return image;
}

_shared_imp Magic::loadUseActionImage()
{
	if (useActionFile.empty())
	{
		return nullptr;
	}
	_shared_imp image = loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		useActionFile,
		"character",
		NPC_RES_FOLDER_ASF,
		NPC_RES_FOLDER));
	IMP::cropTransparentEdges(image);
	return image;
}

_shared_imp Magic::loadFlyingImage()
{
	if (flyingImage.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		flyingImage,
		"effect",
		EFFECT_RES_FOLDER_ASF,
		EFFECT_RES_FOLDER));
}

_shared_imp Magic::loadVanishImage()
{
	if (vanishImage.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		vanishImage,
		"effect",
		EFFECT_RES_FOLDER_ASF,
		EFFECT_RES_FOLDER));
}

_shared_imp Magic::loadSuperModeImage()
{
	if (superModeImage.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		superModeImage,
		"effect",
		EFFECT_RES_FOLDER_ASF,
		EFFECT_RES_FOLDER));
}

_shared_imp Magic::loadLeapImage()
{
	if (leapImageFile.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		leapImageFile,
		"effect",
		EFFECT_RES_FOLDER_ASF,
		EFFECT_RES_FOLDER));
}

_shared_imp Magic::loadHitCountFlyingImage()
{
	if (hitCountFlyingImageFile.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		hitCountFlyingImageFile,
		"effect",
		EFFECT_RES_FOLDER_ASF,
		EFFECT_RES_FOLDER));
}

_shared_imp Magic::loadHitCountVanishImage()
{
	if (hitCountVanishImageFile.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		hitCountVanishImageFile,
		"effect",
		EFFECT_RES_FOLDER_ASF,
		EFFECT_RES_FOLDER));
}

_shared_imp Magic::loadImage()
{
	if (image.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		image,
		"magic",
		MAGIC_RES_FOLDER_ASF,
		MAGIC_RES_FOLDER));
}

_shared_imp Magic::loadIcon()
{
	if (icon.empty())
	{
		return nullptr;
	}
	return loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		icon,
		"magic",
		MAGIC_RES_FOLDER_ASF,
		MAGIC_RES_FOLDER));
}

float Magic::normalizeAngle(float angle)
{
	while (angle < 0)
	{
		angle += 2 * M_PI;
	}
	while (angle > 2 * M_PI)
	{
		angle -= 2 * M_PI;
	}
	return angle;
}
