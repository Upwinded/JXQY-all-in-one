#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Data/Effect.h"
#include "../Game/Data/EffectManager.h"
#include "../Game/Data/Map.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/ObjectManager.h"
#include "../Game/Data/PartnerManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "../Image/IMP.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

class EffectTestAccess
{
public:
	static void updateFlyMagic(Effect& effect, UTime frameTime)
	{
		effect.updateFlyMagic(frameTime);
	}

	static void updateCarryUserPosition(Effect& effect)
	{
		effect.updateCarryUserPosition();
	}

	static void finishTransportEffect(Effect& effect)
	{
		effect.finishTransportEffect();
	}

	static void update(Effect& effect)
	{
		effect.onUpdate();
	}

	static void setFrameTime(Effect& effect, UTime frameTime)
	{
		effect.frameTime = frameTime;
	}

	static void updateMeteorMove(Effect& effect, UTime frameTime)
	{
		effect.updateMeteorMove(frameTime);
	}

	static void updateRoundMovePosition(Effect& effect, UTime frameTime)
	{
		effect.updateRoundMovePosition(frameTime);
	}

	static int calculateThrowHeightOffset(
		double traveledDistance,
		double totalDistance,
		double moveSpeed)
	{
		return Effect::calculateThrowHeightOffset(
			traveledDistance,
			totalDistance,
			moveSpeed);
	}
};

namespace
{
constexpr char EffectPersistenceSaveNamespace[] =
	"effect-runtime-persistence";

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

class ScopedPlatformStateParent final
{
public:
	explicit ScopedPlatformStateParent(
		const std::filesystem::path& root)
	{
		File::setPlatformStateParentForTests(root.string());
	}

	~ScopedPlatformStateParent()
	{
		File::setPlatformStateParentForTests("");
	}
};

bool writeTextFile(const std::filesystem::path& path, const std::string& content)
{
	std::error_code errorCode;
	std::filesystem::create_directories(path.parent_path(), errorCode);
	std::ofstream output(path, std::ios::binary);
	if (!output)
	{
		return false;
	}
	output << content;
	return true;
}

bool runThrowHeightSafetyTest()
{
	const int expectedMidpointHeight = static_cast<int>(
		MAGIC_THROW_HEIGHT * TILE_HEIGHT * 10.0 / 2.0);
	bool ok = true;
	ok = check(EffectTestAccess::calculateThrowHeightOffset(5.0, 10.0, 2.0)
		== expectedMidpointHeight,
		"throw height preserves the midpoint of the original parabola") && ok;
	ok = check(EffectTestAccess::calculateThrowHeightOffset(2.5, 10.0, 2.0)
		== EffectTestAccess::calculateThrowHeightOffset(7.5, 10.0, 2.0),
		"throw height remains symmetric around the midpoint") && ok;
	ok = check(EffectTestAccess::calculateThrowHeightOffset(1.0, 0.0, 2.0) == 0,
		"throw height rejects coincident source and destination") && ok;
	ok = check(EffectTestAccess::calculateThrowHeightOffset(1.0, 10.0, 0.0) == 0,
		"throw height rejects zero speed") && ok;
	ok = check(EffectTestAccess::calculateThrowHeightOffset(
		std::numeric_limits<double>::quiet_NaN(), 10.0, 2.0) == 0,
		"throw height rejects non-finite distance") && ok;
	ok = check(EffectTestAccess::calculateThrowHeightOffset(
		std::numeric_limits<double>::max() / 2.0,
		std::numeric_limits<double>::max(),
		1.0) == std::numeric_limits<int>::max(),
		"throw height saturates an unrepresentable result") && ok;
	return ok;
}

bool prepareMagicFixture(const std::filesystem::path& root)
{
	const std::string magic =
		"[Init]\n"
		"Name=PERSISTENCE\n"
		"MoveKind=2\n"
		"RangeTimeInterval=1000\n"
		"FlyInterval=700\n"
		"Parasitic=1\n"
		"AdditionalEffect=1\n"
		"[Level1]\n"
		"MoveKind=2\n"
		"Speed=24\n"
		"LeapTimes=4\n";
	const std::string heartMagic =
		"[Init]\n"
		"Name=HEART\n"
		"MoveKind=5\n"
		"LifeFrame=20\n"
		"[Level1]\n"
		"MoveKind=5\n"
		"Speed=20\n"
		"LifeFrame=20\n";
	const std::string meteorMagic =
		"[Init]\n"
		"Name=METEOR\n"
		"MoveKind=2\n"
		"MeteorMove=3\n"
		"MeteorMoveDir=0\n"
		"[Level1]\n"
		"MoveKind=2\n"
		"Speed=20\n"
		"LifeFrame=20\n";
	const std::string roundMagic =
		"[Init]\n"
		"Name=ROUND\n"
		"MoveKind=2\n"
		"RoundMoveClockwise=1\n"
		"RoundMoveCount=4\n"
		"RoundMoveDegreeSpeed=120\n"
		"RoundRadius=72\n"
		"[Level1]\n"
		"MoveKind=2\n"
		"Speed=20\n"
		"LifeFrame=100\n";
	const std::string timeStopMagic =
		"[Init]\n"
		"Name=TIME_STOP\n"
		"MoveKind=23\n"
		"SpecialKind=6\n"
		"LifeFrame=250\n"
		"NoExplodeWhenLifeFrameEnd=1\n"
		"[Level1]\n"
		"MoveKind=23\n"
		"SpecialKind=6\n"
		"LifeFrame=250\n";
	const std::string trailMagic =
		"[Init]\n"
		"Name=TRAIL\n"
		"MoveKind=19\n"
		"KeepMilliseconds=5000\n"
		"[Level1]\n"
		"MoveKind=19\n"
		"Speed=10\n";
	const std::string invalidTrailMagic =
		"[Init]\n"
		"Name=INVALID_TRAIL\n"
		"MoveKind=19\n"
		"KeepMilliseconds=-1\n"
		"[Level1]\n"
		"MoveKind=19\n"
		"Speed=10\n";
	const std::string delayedMagic =
		"[Init]\n"
		"Name=DELAYED\n"
		"MoveKind=2\n"
		"[Level1]\n"
		"MoveKind=2\n"
		"Speed=10\n"
		"LifeFrame=10\n";
	const std::string carryMagic =
		"[Init]\n"
		"Name=CARRY\n"
		"MoveKind=2\n"
		"CarryUser=4\n"
		"HideUserWhenCarry=1\n"
		"[Level1]\n"
		"MoveKind=2\n"
		"Speed=10\n"
		"LifeFrame=20\n";
	const std::string damageReduceShieldMagic =
		"[Init]\n"
		"Name=DAMAGE_REDUCE_SHIELD\n"
		"MoveKind=13\n"
		"[Level1]\n"
		"MoveKind=13\n"
		"SpecialKind=3\n"
		"Effect=5\n"
		"LifeFrame=100\n";
	const std::string lifeShieldMagic =
		"[Init]\n"
		"Name=LIFE_SHIELD\n"
		"MoveKind=13\n"
		"[Level1]\n"
		"MoveKind=13\n"
		"SpecialKind=11\n"
		"Effect=50\n"
		"LifeFrame=100\n";
	const std::string detachedChildMagic =
		"[Init]\n"
		"Name=DETACHED_CHILD\n"
		"Type=Fire\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"[Level1]\n"
		"MoveKind=1\n"
		"Effect=5\n"
		"LifeFrame=10\n";
	const std::string detachedParentMagic =
		"[Init]\n"
		"Name=DETACHED_PARENT\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"FlyMagic=detached_child.ini\n"
		"FlyInterval=10\n"
		"ExplodeMagicFile=detached_child.ini\n"
		"CarryUser=1\n"
		"SideEffectProbability=100\n"
		"SideEffectPercent=100\n"
		"SideEffectType=0\n"
		"DieAfterUse=1\n"
		"JumpToTarget=1\n"
		"JumpMoveSpeed=20\n"
		"[Level1]\n"
		"MoveKind=1\n"
		"LifeFrame=10\n";
	const std::string detachedSelfMagic =
		"[Init]\n"
		"Name=DETACHED_SELF\n"
		"MoveKind=13\n"
		"[Level1]\n"
		"MoveKind=13\n"
		"SpecialKind=1\n"
		"Effect=50\n"
		"WaitFrame=1\n"
		"LifeFrame=10\n";
	const std::string detachedTransportMagic =
		"[Init]\n"
		"Name=DETACHED_TRANSPORT\n"
		"MoveKind=20\n"
		"[Level1]\n"
		"MoveKind=20\n"
		"LifeFrame=10\n";
	const std::string summonMagic =
		"[Init]\n"
		"Name=SUMMON_TRANSIENT\n"
		"MoveKind=22\n"
		"NpcFile=summon_transient_npc.ini\n"
		"MaxCount=1\n"
		"KeepMilliseconds=10000\n"
		"[Level1]\n"
		"MoveKind=22\n"
		"LifeFrame=1000\n";
	const std::string summonNpc =
		"[Init]\n"
		"Name=SUMMON_TRANSIENT_NPC\n"
		"Kind=1\n"
		"Relation=1\n"
		"Life=20\n"
		"LifeMax=20\n"
		"BodyIni=summon_transient_body.ini\n"
		"NoDropWhenDie=1\n"
		"MapX=0\n"
		"MapY=0\n";
	const std::string summonBody =
		"[Init]\n"
		"ObjName=SUMMON_TRANSIENT_BODY\n"
		"Kind=2\n"
		"Dir=0\n"
		"MapX=0\n"
		"MapY=0\n"
		"OffX=0\n"
		"OffY=0\n"
		"Damage=0\n"
		"Frame=0\n"
		"Height=4\n"
		"Lum=1\n";
	const std::string detachedEquipment =
		"[Init]\n"
		"Name=DETACHED_EQUIPMENT\n"
		"Kind=1\n"
		"Cost=0\n"
		"Part=Head\n"
		"Attack=6\n"
		"Attack2=2\n"
		"Attack3=3\n"
		"Evade=4\n";
	return writeTextFile(root / "ini" / "magic" / "persistence.ini", magic)
		&& writeTextFile(root / "ini" / "magic" / "heart.ini", heartMagic)
		&& writeTextFile(root / "ini" / "magic" / "meteor.ini", meteorMagic)
		&& writeTextFile(root / "ini" / "magic" / "round.ini", roundMagic)
		&& writeTextFile(root / "ini" / "magic" / "time_stop.ini", timeStopMagic)
		&& writeTextFile(root / "ini" / "magic" / "trail.ini", trailMagic)
		&& writeTextFile(root / "ini" / "magic" / "invalid_trail.ini", invalidTrailMagic)
		&& writeTextFile(root / "ini" / "magic" / "delayed.ini", delayedMagic)
		&& writeTextFile(root / "ini" / "magic" / "carry.ini", carryMagic)
		&& writeTextFile(root / "ini" / "magic" / "damage_reduce_shield.ini", damageReduceShieldMagic)
		&& writeTextFile(root / "ini" / "magic" / "life_shield.ini", lifeShieldMagic)
		&& writeTextFile(root / "ini" / "magic" / "detached_child.ini", detachedChildMagic)
		&& writeTextFile(root / "ini" / "magic" / "detached_parent.ini", detachedParentMagic)
		&& writeTextFile(root / "ini" / "magic" / "detached_self.ini", detachedSelfMagic)
		&& writeTextFile(root / "ini" / "magic" / "detached_transport.ini", detachedTransportMagic)
		&& writeTextFile(root / "ini" / "magic" / "summon_transient.ini", summonMagic)
		&& writeTextFile(
			root / "ini" / "magic" / "malformed.ini",
			"[Init\nName=MALFORMED\n")
		&& writeTextFile(root / "ini" / "npc" / "summon_transient_npc.ini", summonNpc)
		&& writeTextFile(root / "ini" / "obj" / "summon_transient_body.ini", summonBody)
		&& writeTextFile(root / "ini" / "goods" / "detached_equipment.ini", detachedEquipment);
}

bool runMagicManagerLoadCompatibilityTest(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	gameManager.magicManager.clearMagicList();
	const int hiddenSection =
		gameManager.magicManager.hideStartIndex() + 1;
	const std::string saveContent =
		"[Head]\n"
		"Count=3\n"
		"CurrentUseMagicFile=persistence.ini\n"
		"[1]\n"
		"IniFile=persistence.ini\n"
		"Level=-7\n"
		"Exp=-12\n"
		"HideCount=-4\n"
		"LastIndexWhenHide=-9\n"
		"[2]\n"
		"IniFile=missing.ini\n"
		"Level=1\n"
		"[3]\n"
		"IniFile=malformed.ini\n"
		"Level=1\n"
		"[" + std::to_string(hiddenSection) + "]\n"
		"IniFile=heart.ini\n"
		"Level=999\n"
		"Exp=123\n"
		"HideCount=999\n"
		"LastIndexWhenHide=-5\n";
	const std::filesystem::path savePath =
		root / "save" / EffectPersistenceSaveNamespace /
		"game" / "magic.ini";
	if (!check(
		writeTextFile(savePath, saveContent),
		"write tolerant Magic save fixture"))
	{
		return false;
	}

	bool ok = check(
		gameManager.magicManager.load(-1),
		"valid tolerant Magic save data loads");
	const MagicInfo& loaded = gameManager.magicManager.magicList[0];
	const std::string retainedMagicName = loaded.iniFile;
	ok = check(
		gameManager.magicManager.magicListExists(0)
			&& loaded.level == 1
			&& loaded.exp == 0
			&& loaded.hideCount == 1
			&& loaded.lastIndexWhenHide == 0,
		"Magic save loading clamps legacy numeric fields to safe usable values") && ok;
	ok = check(
		!gameManager.magicManager.magicListExists(1)
			&& !gameManager.magicManager.magicListExists(2)
			&& gameManager.magicManager.findPrimaryMagic("missing.ini")
				== nullptr
			&& gameManager.magicManager.findPrimaryMagic("malformed.ini")
				== nullptr,
		"missing and malformed Magic resources are skipped without rejecting the save") && ok;
	ok = check(
		gameManager.magicManager.isMagicHidden("heart.ini"),
		"valid hidden Magic remains available after tolerant save loading") && ok;
	ok = check(
		gameManager.magicManager.addPrimaryMagic(
			"missing-runtime.ini", false, false) == nullptr
			&& gameManager.magicManager.findPrimaryMagic(
				"missing-runtime.ini") == nullptr,
		"runtime Magic insertion does not create an unusable ghost entry") && ok;
	const int replacementBegin =
		gameManager.magicManager.bottomBegin();
	gameManager.magicManager.replaceMagicList(
		"missing-replacement.ini;persistence.ini");
	ok = check(
		!gameManager.magicManager.magicListExists(replacementBegin)
			&& gameManager.magicManager.magicListExists(
				replacementBegin + 1),
		"replacement lists leave missing Magic entries empty while retaining valid entries") && ok;
	gameManager.magicManager.stopReplaceMagicList();

	ok = check(
		gameManager.magicManager.save(-1),
		"tolerantly loaded Magic state can be saved normally") && ok;
	INIReader normalizedSave("save\\game\\magic.ini");
	ok = check(
		normalizedSave.ParseError() == 0
			&& normalizedSave.Get("Head", "CurrentUseMagicFile", "x").empty()
			&& normalizedSave.Get("1", "IniFile", "")
				== "persistence.ini"
			&& normalizedSave.Get("2", "IniFile", "").empty()
			&& normalizedSave.Get("3", "IniFile", "").empty(),
		"normal save output omits skipped Magic records and stale current-use state") && ok;

	MagicInfo* restoredHidden = gameManager.magicManager.setMagicHidden(
		"heart.ini", false, false, false);
	ok = check(
		restoredHidden != nullptr
			&& restoredHidden->magic != nullptr
			&& restoredHidden->magic->loadSucceeded
			&& restoredHidden->level == MAGIC_MAX_LEVEL
			&& restoredHidden->exp == 123
			&& restoredHidden->hideCount == 1
			&& restoredHidden->lastIndexWhenHide == 0,
		"hidden Magic load clamps level and hidden bookkeeping without discarding progress") && ok;

	std::string magicFailureReason;
	const std::string mismatchedMagicCount =
		"[Head]\n"
		"Count=999\n"
		"[1]\n"
		"IniFile=persistence.ini\n"
		"Level=1\n";
	ok = check(
		writeTextFile(savePath, mismatchedMagicCount) &&
			gameManager.magicManager.load(-1) &&
			gameManager.magicManager.magicListExists(0),
		"legacy Magic Count metadata does not have to match the actual section count") && ok;
	ok = check(
		writeTextFile(savePath, "[Head\nCount=1\n") &&
			!gameManager.magicManager.load(
				-1,
				&magicFailureReason) &&
			gameManager.magicManager.findPrimaryMagic(retainedMagicName) !=
				nullptr &&
			magicFailureReason.find("magic.ini") != std::string::npos,
		"malformed Magic save data fails without clearing the live Magic list") && ok;
	std::error_code errorCode;
	std::filesystem::remove(savePath, errorCode);
	ok = check(
		!errorCode && gameManager.magicManager.load(-1) &&
			gameManager.magicManager.findPrimaryMagic(retainedMagicName) ==
				nullptr,
		"a missing historical Magic file remains a compatible empty list") && ok;
	return ok;
}

bool runGoodsManagerLoadContractTest(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	const int slot = gameManager.goodsManager.storeBegin();
	const std::string saveContent =
		"[Head]\n"
		"Count=1\n"
		"[" + std::to_string(slot + 1) + "]\n"
		"IniFile=detached_equipment.ini\n"
		"Number=2\n";
	const std::filesystem::path savePath =
		root / "save" / EffectPersistenceSaveNamespace /
		"game" / "goods.ini";
	bool ok = check(
		writeTextFile(savePath, saveContent) &&
			gameManager.goodsManager.load(-1) &&
			gameManager.goodsManager.goodsListExists(slot) &&
			gameManager.goodsManager.goodsList[slot].number == 2,
		"valid Goods save data loads into the configured inventory layout");
	const std::string mismatchedGoodsCount =
		"[Head]\n"
		"Count=999\n"
		"[" + std::to_string(slot + 1) + "]\n"
		"IniFile=detached_equipment.ini\n"
		"Number=2\n";
	ok = check(
		writeTextFile(savePath, mismatchedGoodsCount) &&
			gameManager.goodsManager.load(-1) &&
			gameManager.goodsManager.goodsListExists(slot),
		"legacy Goods Count metadata does not have to match the actual section count") && ok;

	std::string goodsFailureReason;
	ok = check(
		writeTextFile(savePath, "") &&
			!gameManager.goodsManager.load(
				-1,
				&goodsFailureReason) &&
			gameManager.goodsManager.goodsListExists(slot) &&
			gameManager.goodsManager.goodsList[slot].number == 2 &&
			goodsFailureReason.find("goods.ini") != std::string::npos,
		"an existing empty Goods file fails without clearing the live inventory") && ok;

	std::error_code errorCode;
	std::filesystem::remove(savePath, errorCode);
	ok = check(
		!errorCode && gameManager.goodsManager.load(-1) &&
			!gameManager.goodsManager.goodsListExists(slot),
		"a missing historical Goods file remains a compatible empty inventory") && ok;
	return ok;
}

bool runPlayerChangeCorruptTargetRollbackTest(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	gameManager.magicManager.clearMagicList();
	gameManager.goodsManager.freeResource();
	gameManager.global.data.characterIndex = 0;
	gameManager.player->npcName = "RejectedCharacter";
	gameManager.player->money = 222;
	bool ok = check(
		gameManager.player->save(1) &&
			gameManager.magicManager.save(1) &&
			gameManager.goodsManager.save(1),
		"write target-character player-change fixtures");

	gameManager.player->npcName = "SourceCharacter";
	gameManager.player->money = 111;
	const int sourceGoodsIndex =
		gameManager.goodsManager.storeBegin();
	auto sourceGoods = std::make_shared<Goods>();
	sourceGoods->initFromIni("detached_equipment.ini");
	GoodsInfo& sourceGoodsInfo =
		gameManager.goodsManager.goodsList[sourceGoodsIndex];
	sourceGoodsInfo.iniFile = "detached_equipment.ini";
	sourceGoodsInfo.number = 2;
	sourceGoodsInfo.goods = sourceGoods;
	ok = check(
		gameManager.magicManager.addPrimaryMagic(
			"persistence.ini",
			false,
			false) != nullptr &&
			sourceGoods->loadSucceeded,
		"prepare source-character state for player-change rollback") && ok;

	const std::filesystem::path saveDirectory =
		root / "save" / EffectPersistenceSaveNamespace / "game";
	ok = check(
		writeTextFile(
			saveDirectory / "magic1.ini",
			"[Head\nCount=1\n"),
		"corrupt target-character Magic data") && ok;
	gameManager.scriptAPI.playerChange(1);
	ok = check(
		gameManager.global.data.characterIndex == 0 &&
			gameManager.player->npcName == "SourceCharacter" &&
			gameManager.player->money == 111 &&
			gameManager.magicManager.findPrimaryMagic(
				"persistence.ini") != nullptr &&
			gameManager.goodsManager.getItemNum(
				"detached_equipment.ini") == 2,
		"player change rolls back after malformed target Magic data") && ok;

	ok = check(
		writeTextFile(
			saveDirectory / "magic1.ini",
			"[Head]\nCount=0\nCurrentUseMagicFile=\n") &&
			writeTextFile(saveDirectory / "goods1.ini", ""),
		"prepare target Goods failure after a successful empty Magic load") && ok;
	gameManager.scriptAPI.playerChange(1);
	ok = check(
		gameManager.global.data.characterIndex == 0 &&
			gameManager.player->npcName == "SourceCharacter" &&
			gameManager.player->money == 111 &&
			gameManager.magicManager.findPrimaryMagic(
				"persistence.ini") != nullptr &&
			gameManager.goodsManager.getItemNum(
				"detached_equipment.ini") == 2,
		"player change restores source Magic and Goods after a later target Goods failure") && ok;

	gameManager.magicManager.clearMagicList();
	gameManager.goodsManager.freeResource();
	return ok;
}

bool runDetachedCasterLifetimeTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	auto parentMagic = std::make_shared<Magic>();
	parentMagic->initFromIni("detached_parent.ini");
	if (!check(parentMagic->loadSucceeded
		&& parentMagic->flyMagic != nullptr
		&& parentMagic->getExplodeMagicForLevel(1) != nullptr,
		"detached-caster parent and both derived child fixtures load"))
	{
		return false;
	}

	auto caster = std::make_shared<NPC>();
	caster->kind = nkBattle;
	caster->relation = nrHostile;
	caster->attack = 77;
	caster->evade = 23;
	caster->setPosition({ 8, 9 }, false);
	gameManager.npcManager->npcList = { caster };

	auto parentEffect = std::make_shared<Effect>();
	parentEffect->level = 1;
	parentEffect->user = caster;
	parentEffect->position = { 10, 10 };
	parentEffect->src = parentEffect->position;
	parentEffect->launcherKind = lkEnemy;
	parentEffect->initFromMagic(parentMagic);
	gameManager.effectManager->addEffect(parentEffect);

	std::weak_ptr<NPC> casterProbe = caster;
	gameManager.npcManager->removeNPCOnlyFromList(caster);
	caster.reset();
	bool ok = true;
	ok = check(!casterProbe.expired()
		&& parentEffect->user.lock() == casterProbe.lock(),
		"active projectile keeps a removed caster alive") && ok;

	parentEffect->beginFly();
	size_t effectCountBeforeFlyCadence = gameManager.effectManager->effectList.size();
	EffectTestAccess::updateFlyMagic(*parentEffect, parentEffect->magic.flyInterval);
	auto flyChildEffect = gameManager.effectManager->effectList.empty()
		? nullptr
		: gameManager.effectManager->effectList.back();
	ok = check(gameManager.effectManager->effectList.size() == effectCountBeforeFlyCadence + 1
		&& flyChildEffect != nullptr
		&& flyChildEffect != parentEffect
		&& flyChildEffect->magic.iniName == "detached_child.ini"
		&& flyChildEffect->user.lock() == casterProbe.lock()
		&& flyChildEffect->damage == 77
		&& flyChildEffect->evade == 23,
		"removed caster still drives FlyMagic cadence with the original owner and combat values") && ok;

	size_t effectCountBeforeExplode = gameManager.effectManager->effectList.size();
	parentEffect->beginExplode(parentEffect->position);
	auto childEffect = gameManager.effectManager->effectList.empty()
		? nullptr
		: gameManager.effectManager->effectList.back();
	ok = check(gameManager.effectManager->effectList.size() == effectCountBeforeExplode + 1
		&& childEffect != nullptr
		&& childEffect != parentEffect
		&& childEffect->magic.iniName == "detached_child.ini"
		&& childEffect->user.lock() == casterProbe.lock()
		&& childEffect->launcherKind == lkEnemy
		&& childEffect->damage == 77
		&& childEffect->evade == 23,
		"removed caster still supplies owner identity and combat values to explode children") && ok;

	gameManager.effectManager->freeResource();
	ok = check(parentEffect->user.expired()
		&& (flyChildEffect == nullptr || flyChildEffect->user.expired())
		&& (childEffect == nullptr || childEffect->user.expired())
		&& casterProbe.expired(),
		"manager cleanup releases removed caster even while external Effect owners remain") && ok;
	return ok;
}

bool runDetachedCasterPersistenceTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	auto parentMagic = std::make_shared<Magic>();
	parentMagic->initFromIni("detached_parent.ini");
	auto trailMagic = std::make_shared<Magic>();
	trailMagic->initFromIni("trail.ini");
	auto delayedMagic = std::make_shared<Magic>();
	delayedMagic->initFromIni("delayed.ini");
	if (!check(parentMagic->loadSucceeded && trailMagic->loadSucceeded && delayedMagic->loadSucceeded,
		"detached-caster persistence fixtures load"))
	{
		return false;
	}

	auto caster = std::make_shared<NPC>();
	caster->npcName = "DetachedOwner";
	caster->showName = "Detached Owner";
	caster->kind = nkBattle;
	caster->relation = nrHostile;
	caster->originalRelationBeforeOppositeChange = nrFriendly;
	caster->group = 17;
	caster->level = 4;
	caster->attack = 64;
	caster->attack2 = 71;
	caster->attack3 = 72;
	caster->evade = 18;
	caster->canEquip = 1;
	caster->headEquip = "detached_equipment.ini";
	caster->updateEquipmentAttributes();
	caster->equipmentAddMagicEffectPercent = 10;
	caster->equipmentAddMagicEffectAmount = 2;
	caster->equipmentAddMagicEffectByName["DETACHED_CHILD"] = { 5, 3 };
	caster->equipmentAddMagicEffectByType["Fire"] = { 7, 4 };
	caster->changeMagicHitCounts["detached_parent.ini"] = 9;
	caster->setPosition({ 31, 42 }, false);
	caster->setOffset({ 2.5f, -3.5f });
	caster->direction = 6;
	auto removedTarget = std::make_shared<NPC>();
	removedTarget->kind = nkBattle;
	gameManager.npcManager->npcList = { caster, removedTarget };

	auto makeEffect = [&]()
	{
		auto effect = std::make_shared<Effect>();
		effect->level = 1;
		effect->user = caster;
		effect->target = removedTarget;
		effect->position = { 12, 13 };
		effect->src = effect->position;
		effect->initFromMagic(parentMagic);
		gameManager.effectManager->addEffect(effect);
		return effect;
	};
	makeEffect();
	makeEffect();
	gameManager.effectManager->addTrailMagic(trailMagic, caster, 1, 70, 22, lkEnemy);
	gameManager.effectManager->addDelayedMagic(
		delayedMagic,
		caster,
		{ 1, 2 },
		{ 3, 4 },
		1,
		lkEnemy,
		removedTarget,
		500);
	gameManager.npcManager->removeNPCOnlyFromList(removedTarget);
	gameManager.npcManager->removeNPCOnlyFromList(caster);
	std::weak_ptr<NPC> originalCasterProbe = caster;
	caster.reset();

	INIReader ini;
	gameManager.effectManager->saveToIni(ini);
	bool ok = true;
	ok = check(ini.GetInteger("Head", "EffectPersistenceVersion", 0) == 2
		&& ini.GetInteger("Head", "DetachedCasterCount", 0) == 1
		&& ini.GetInteger("PRO1", "UserReferenceKind", 0) == 4
		&& ini.GetInteger("PRO2", "UserReferenceKind", 0) == 4
		&& ini.GetInteger("Trail1", "UserReferenceKind", 0) == 4
		&& ini.GetInteger("Delayed1", "UserReferenceKind", 0) == 4
		&& ini.GetInteger("PRO1", "UserReferenceIndex", -1) == 0
		&& ini.GetInteger("PRO2", "UserReferenceIndex", -1) == 0
		&& ini.GetInteger("Trail1", "UserReferenceIndex", -1) == 0
		&& ini.GetInteger("Delayed1", "UserReferenceIndex", -1) == 0,
		"two Effects and both pending queues deduplicate one removed caster tombstone") && ok;
	ok = check(ini.GetInteger("PRO1", "TargetReferenceKind", -1) == 0
		&& ini.GetInteger("Delayed1", "TargetReferenceKind", -1) == 0,
		"removed active-only targets never become caster tombstones") && ok;

	gameManager.effectManager->loadFromIni(ini);
	ok = check(originalCasterProbe.expired(),
		"loading releases the pre-save detached caster graph") && ok;
	auto loadedCaster = gameManager.effectManager->effectList.empty()
		? nullptr
		: std::dynamic_pointer_cast<NPC>(gameManager.effectManager->effectList[0]->user.lock());
	ok = check(gameManager.effectManager->effectList.size() == 2
		&& loadedCaster != nullptr
		&& gameManager.effectManager->effectList[1]->user.lock() == loadedCaster
		&& gameManager.effectManager->getPendingTrailMagicUser(0) == loadedCaster
		&& gameManager.effectManager->getPendingDelayedMagicUser(0) == loadedCaster,
		"all loaded consumers share one detached caster identity") && ok;
	ok = check(loadedCaster != nullptr
		&& loadedCaster->detachedEffectCaster
		&& !NPCManager::isManagedEffectCaster(loadedCaster)
		&& loadedCaster->kind == nkBattle
		&& loadedCaster->relation == nrHostile
		&& loadedCaster->group == 17
		&& loadedCaster->level == 4
		&& loadedCaster->getAttack() == 70
		&& loadedCaster->getAttack2() == 73
		&& loadedCaster->getAttack3() == 75
		&& loadedCaster->getEvade() == 22
		&& loadedCaster->getPosition() == Point{ 31, 42 }
		&& loadedCaster->getOffset().x == 2.5f
		&& loadedCaster->getOffset().y == -3.5f
		&& loadedCaster->direction == 6,
		"detached caster identity, relation, group, combat values, and position survive save/load") && ok;
	auto childMagic = parentMagic->getExplodeMagicForLevel(1);
	ok = check(loadedCaster != nullptr
		&& childMagic != nullptr
		&& loadedCaster->applyMagicEffectBonus(*childMagic, 70) == 85
		&& loadedCaster->equipmentAddMagicEffectByType["Fire"].percent == 7
		&& loadedCaster->equipmentAddMagicEffectByType["Fire"].amount == 4
		&& loadedCaster->changeMagicHitCounts["detached_parent.ini"] == 9,
		"detached caster Magic bonuses and ChangeMagic counter survive save/load") && ok;

	INIReader reserialized;
	gameManager.effectManager->saveToIni(reserialized);
	ok = check(reserialized.GetInteger("Head", "DetachedCasterCount", 0) == 1,
		"reserializing a loaded detached caster keeps one tombstone") && ok;

	INIReader corrupt;
	corrupt.SetInteger("Head", "Count", 1);
	corrupt.SetInteger("Head", "TrailCount", 1);
	corrupt.SetInteger("Head", "DetachedCasterCount", 1);
	corrupt.Set("PRO1", "FileName", "detached_parent.ini");
	corrupt.SetInteger("PRO1", "Level", 1);
	corrupt.SetInteger("PRO1", "UserReferenceKind", 4);
	corrupt.SetInteger("PRO1", "UserReferenceIndex", 9);
	corrupt.Set("Trail1", "MagicFile", "trail.ini");
	corrupt.SetInteger("Trail1", "UserReferenceKind", 4);
	corrupt.SetInteger("Trail1", "UserReferenceIndex", 0);
	gameManager.effectManager->loadFromIni(corrupt);
	ok = check(gameManager.effectManager->effectList.size() == 1
		&& gameManager.effectManager->effectList[0]->user.expired()
		&& gameManager.effectManager->getPendingTrailMagicCount() == 0
		&& gameManager.npcManager->npcList.empty(),
		"bad detached indexes degrade Effect to ownerless visual state and skip dependent queues") && ok;

	gameManager.effectManager->freeResource();
	auto activeCaster = std::make_shared<NPC>();
	activeCaster->kind = nkBattle;
	gameManager.npcManager->npcList = { activeCaster };
	auto activeEffect = std::make_shared<Effect>();
	activeEffect->level = 1;
	activeEffect->user = activeCaster;
	activeEffect->initFromMagic(parentMagic);
	gameManager.effectManager->addEffect(activeEffect);
	INIReader activeIni;
	gameManager.effectManager->saveToIni(activeIni);
	ok = check(activeIni.GetInteger("PRO1", "UserReferenceKind", 0) == 3
		&& activeIni.GetInteger("Head", "DetachedCasterCount", -1) == 0,
		"active NPC caster keeps the legacy ordinal representation") && ok;

	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runPersistenceCollectionBoundsTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("detached_parent.ini");
	if (!check(magic->loadSucceeded, "effect bounds fixture loads"))
	{
		return false;
	}

	auto caster = std::make_shared<NPC>();
	caster->npcName = "BoundsOwner";
	caster->kind = nkBattle;
	for (int i = 0; i < MaximumPersistedEffectCollectionCount; i++)
	{
		caster->changeMagicHitCounts["bounds_" + std::to_string(i)] = i;
	}
	auto effect = std::make_shared<Effect>();
	effect->level = 1;
	effect->user = caster;
	effect->initFromMagic(magic);
	gameManager.effectManager->addEffect(effect);

	const std::string generationDirectory = "save/effect_persistence_bounds";
	SaveFileManager::CurrentPathScope currentPath(generationDirectory);
	if (!check(currentPath.valid(), "effect bounds generation path is valid"))
	{
		return false;
	}
	File::clearDirectoryFiles(generationDirectory);

	bool ok = check(gameManager.effectManager->save(),
		"4096 persisted Effect entries save successfully");
	INIReader saved(SaveFileManager::CurrentPath() + EFFECT_INI);
	ok = check(saved.ParseError() == 0
		&& saved.GetInteger(
			"DetachedCaster001",
			"ChangeMagicHitCountCount",
			-1) == MaximumPersistedEffectCollectionCount,
		"4096 persisted Effect entries remain readable") && ok;
	const std::string validEffectFile = saved.saveToString();

	gameManager.effectManager->loadFromIni(saved);
	auto loadedCaster = gameManager.effectManager->effectList.empty()
		? nullptr
		: std::dynamic_pointer_cast<NPC>(
			gameManager.effectManager->effectList.front()->user.lock());
	ok = check(loadedCaster != nullptr
		&& loadedCaster->changeMagicHitCounts.size()
			== static_cast<size_t>(MaximumPersistedEffectCollectionCount),
		"reader preserves the 4096-entry boundary") && ok;
	if (loadedCaster != nullptr)
	{
		loadedCaster->changeMagicHitCounts["bounds_overflow"] = 1;
		ok = check(!gameManager.effectManager->save(),
			"4097 persisted Effect entries reject the save") && ok;
	}

	INIReader preserved(SaveFileManager::CurrentPath() + EFFECT_INI);
	ok = check(preserved.ParseError() == 0
		&& preserved.GetInteger(
			"DetachedCaster001",
			"ChangeMagicHitCountCount",
			-1) == MaximumPersistedEffectCollectionCount
		&& preserved.saveToString() == validEffectFile,
		"rejected 4097-entry save does not replace the last valid file") && ok;

	gameManager.effectManager->freeResource();
	auto repeatedEffect = std::make_shared<Effect>();
	repeatedEffect->level = 1;
	repeatedEffect->initFromMagic(magic);
	gameManager.effectManager->effectList.assign(
		static_cast<size_t>(MaximumPersistedEffectCollectionCount) + 1,
		repeatedEffect);
	ok = check(!gameManager.effectManager->save(),
		"4097 top-level Effects reject the save") && ok;
	INIReader preservedAfterTopLevelOverflow(
		SaveFileManager::CurrentPath() + EFFECT_INI);
	ok = check(preservedAfterTopLevelOverflow.ParseError() == 0
		&& preservedAfterTopLevelOverflow.GetInteger(
			"DetachedCaster001",
			"ChangeMagicHitCountCount",
			-1) == MaximumPersistedEffectCollectionCount
		&& preservedAfterTopLevelOverflow.saveToString() == validEffectFile,
		"top-level overflow also preserves the last valid file") && ok;
	gameManager.effectManager->effectList.clear();

	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	File::clearDirectoryFiles(generationDirectory);
	return ok;
}

bool runDetachedCasterLiveGateTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto parentMagic = std::make_shared<Magic>();
	parentMagic->initFromIni("detached_parent.ini");
	auto selfMagic = std::make_shared<Magic>();
	selfMagic->initFromIni("detached_self.ini");
	auto transportMagic = std::make_shared<Magic>();
	transportMagic->initFromIni("detached_transport.ini");
	if (!check(parentMagic->loadSucceeded && selfMagic->loadSucceeded && transportMagic->loadSucceeded,
		"detached-caster live-gate fixtures load"))
	{
		return false;
	}

	auto caster = std::make_shared<NPC>();
	caster->kind = nkBattle;
	caster->life = 321;
	caster->lifeMax = 500;
	caster->attack = 25;
	caster->setPosition({ 5, 5 }, false);
	auto target = std::make_shared<NPC>();
	target->kind = nkBattle;
	gameManager.npcManager->npcList = { target };
	bool ok = true;
	auto jumpMagic = std::make_shared<Magic>();
	jumpMagic->copy(*parentMagic);
	parentMagic->jumpToTarget = 0;
	parentMagic->linkedLevel[1].jumpToTarget = 0;

	auto pointEffects = Magic::addEffect(
		parentMagic,
		caster,
		{ 5, 5 },
		{ 6, 5 },
		1,
		25,
		0,
		lkEnemy,
		nullptr);
	ok = check(pointEffects.size() == 1 && pointEffects[0] != nullptr,
		"detached caster can still create a projectile") && ok;
	ok = check(!pointEffects.empty()
		&& pointEffects[0] != nullptr
		&& !pointEffects[0]->carryUserActive,
		"detached caster cannot bind CarryUser") && ok;
	ok = check(caster->life == 321,
		"detached caster cannot receive SideEffect or DieAfterUse") && ok;
	auto jumpEffects = Magic::addEffect(
		jumpMagic,
		caster,
		{ 5, 5 },
		{ 6, 5 },
		1,
		25,
		0,
		lkEnemy,
		nullptr);
	ok = check(jumpEffects.empty() && !caster->isMagicForcedMoving(),
		"detached caster cannot receive JumpToTarget") && ok;
	auto selfEffects = Magic::addEffect(
		selfMagic,
		caster,
		caster->getPosition(),
		caster->getPosition(),
		1,
		50,
		0,
		lkSelf,
		nullptr);
	ok = check(selfEffects.empty() && caster->life == 321,
		"detached caster cannot create or receive a Self effect") && ok;
	auto transportEffects = Magic::addEffect(
		transportMagic,
		caster,
		caster->getPosition(),
		{ 8, 8 },
		1,
		0,
		0,
		lkSelf,
		nullptr);
	ok = check(transportEffects.empty() && !caster->isTransporting(),
		"detached caster cannot bind a Transport effect") && ok;

	auto restoreEffect = std::make_shared<Effect>();
	restoreEffect->level = 1;
	restoreEffect->user = caster;
	restoreEffect->initFromMagic(parentMagic);
	restoreEffect->magic.restoreProbability = 100;
	restoreEffect->magic.restorePercent = 100;
	restoreEffect->magic.restoreType = 0;
	target->applyEffectRestore(restoreEffect, 50);
	target->rememberDamageSource(restoreEffect);
	ok = check(caster->life == 321 && target->lastCombatTarget.expired(),
		"detached caster cannot receive restore hooks or enter a target's combat memory") && ok;

	caster->isVisibleByVariable = false;
	gameManager.npcManager->npcList.push_back(caster);
	auto hiddenManagedSelfEffects = Magic::addEffect(
		selfMagic,
		caster,
		caster->getPosition(),
		caster->getPosition(),
		1,
		50,
		0,
		lkSelf,
		nullptr);
	ok = check(NPCManager::isManagedEffectCaster(caster)
		&& hiddenManagedSelfEffects.size() == 1
		&& caster->life == 371,
		"visibility-variable hiding does not misclassify a still-managed caster as detached") && ok;

	gameManager.effectManager->freeResource();
	restoreEffect->releaseRuntimeBindings();
	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runDetachedCasterTransitionGateTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	auto carryMagic = std::make_shared<Magic>();
	carryMagic->initFromIni("carry.ini");
	auto selfMagic = std::make_shared<Magic>();
	selfMagic->initFromIni("detached_self.ini");
	auto transportMagic = std::make_shared<Magic>();
	transportMagic->initFromIni("detached_transport.ini");
	if (!check(carryMagic->loadSucceeded && selfMagic->loadSucceeded && transportMagic->loadSucceeded,
		"active-to-detached transition fixtures load"))
	{
		return false;
	}

	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 16;
	gameManager.map->data->head.height = 16;
	gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
	gameManager.map->createDataMap();

	auto caster = std::make_shared<NPC>();
	caster->kind = nkBattle;
	caster->life = 300;
	caster->lifeMax = 500;
	caster->setPosition({ 2, 2 }, false);
	gameManager.npcManager->npcList = { caster };
	bool ok = true;

	auto carryEffects = Magic::addEffect(
		carryMagic,
		caster,
		{ 2, 2 },
		{ 4, 2 },
		1,
		25,
		0,
		lkEnemy,
		nullptr);
	auto carryEffect = carryEffects.empty() ? nullptr : carryEffects[0];
	ok = check(carryEffect != nullptr
		&& carryEffect->carryUserActive
		&& caster->isHiddenByCarryMagic(),
		"managed caster binds Carry before leaving the manager") && ok;
	Point carriedPosition = caster->getPosition();
	gameManager.npcManager->removeNPCOnlyFromList(caster);
	if (carryEffect != nullptr)
	{
		carryEffect->position = { 7, 7 };
		EffectTestAccess::updateCarryUserPosition(*carryEffect);
	}
	ok = check(caster->getPosition() == carriedPosition
		&& carryEffect != nullptr
		&& !carryEffect->carryUserActive
		&& !caster->isHiddenByCarryMagic(),
		"Carry releases a caster that becomes detached without moving or hiding it again") && ok;

	gameManager.effectManager->freeResource();
	caster->setPosition({ 2, 2 }, false);
	gameManager.npcManager->npcList = { caster };
	auto transportEffects = Magic::addEffect(
		transportMagic,
		caster,
		caster->getPosition(),
		{ 8, 8 },
		1,
		0,
		0,
		lkSelf,
		nullptr);
	auto transportEffect = transportEffects.empty() ? nullptr : transportEffects[0];
	ok = check(transportEffect != nullptr && caster->isTransporting(),
		"managed caster binds Transport before leaving the manager") && ok;
	Point transportStart = caster->getPosition();
	gameManager.npcManager->removeNPCOnlyFromList(caster);
	if (transportEffect != nullptr)
	{
		EffectTestAccess::finishTransportEffect(*transportEffect);
	}
	ok = check(caster->getPosition() == transportStart
		&& transportEffect != nullptr
		&& transportEffect->transportFinished
		&& !caster->isTransporting(),
		"Transport finishes without relocating a caster that became detached") && ok;

	gameManager.effectManager->freeResource();
	caster->setPosition({ 4, 4 }, false);
	gameManager.npcManager->npcList = { caster };
	auto selfEffects = Magic::addEffect(
		selfMagic,
		caster,
		caster->getPosition(),
		caster->getPosition(),
		1,
		50,
		0,
		lkSelf,
		nullptr);
	auto selfEffect = selfEffects.empty() ? nullptr : selfEffects[0];
	ok = check(selfEffect != nullptr && selfEffect->doing == ekHiding && selfEffect->waitTime > 0,
		"managed caster creates a waiting Self effect before leaving the manager") && ok;
	Point selfAnchor = selfEffect != nullptr ? selfEffect->position : Point{ 0, 0 };
	gameManager.npcManager->removeNPCOnlyFromList(caster);
	caster->setPosition({ 9, 9 }, false);
	if (selfEffect != nullptr)
	{
		selfEffect->setTime(selfEffect->beginTime + selfEffect->waitTime);
		EffectTestAccess::update(*selfEffect);
	}
	ok = check(selfEffect != nullptr && selfEffect->waitTime == 0,
		"waiting Self effect resolves after its caster becomes detached") && ok;
	ok = check(selfEffect != nullptr && selfEffect->position == selfAnchor,
		"waiting Self effect keeps its last live anchor when its caster becomes detached") && ok;

	gameManager.effectManager->freeResource();
	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runSelfShieldBindingRoundTripTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto damageReduceMagic = std::make_shared<Magic>();
	damageReduceMagic->initFromIni("damage_reduce_shield.ini");
	auto lifeShieldMagic = std::make_shared<Magic>();
	lifeShieldMagic->initFromIni("life_shield.ini");
	if (!check(damageReduceMagic->loadSucceeded && lifeShieldMagic->loadSucceeded,
		"self-shield persistence fixtures load"))
	{
		return false;
	}

	Point playerPosition = gameManager.player->getPosition();
	auto damageReduceEffects = Magic::addEffect(
		damageReduceMagic,
		gameManager.player,
		playerPosition,
		playerPosition,
		1,
		5,
		0,
		lkSelf,
		nullptr);
	auto lifeShieldEffects = Magic::addEffect(
		lifeShieldMagic,
		gameManager.player,
		playerPosition,
		playerPosition,
		1,
		50,
		0,
		lkSelf,
		nullptr);
	bool ok = true;
	ok = check(damageReduceEffects.size() == 1
		&& lifeShieldEffects.size() == 1
		&& gameManager.player->hasActiveSelfMagic(mskAddDamageReduceShield),
		"self-shield reverse bindings are active before save") && ok;
	gameManager.player->shieldLife = 23;
	gameManager.player->shieldLastTime = 456;

	INIReader ini;
	gameManager.effectManager->saveToIni(ini);
	gameManager.effectManager->loadFromIni(ini);
	ok = check(gameManager.player->hasActiveSelfMagic(mskAddDamageReduceShield),
		"damage-reduce shield list binding survives save/load") && ok;
	auto loadedLifeShield = gameManager.player->shieldEffect.lock();
	ok = check(loadedLifeShield != nullptr
		&& loadedLifeShield->magic.level[1].specialKind == mskAddShield
		&& gameManager.player->shieldLife == 23
		&& gameManager.player->shieldLastTime == 456,
		"consumable shield identity, remaining life, and duration survive save/load") && ok;
	int lifeBeforeShieldHit = gameManager.player->life;
	int defendBeforeShieldHit = gameManager.player->defend;
	gameManager.player->defend = 0;
	gameManager.player->hurtLife(10);
	ok = check(gameManager.player->life == lifeBeforeShieldHit
		&& gameManager.player->shieldLife == 18,
		"reloaded damage-reduce and consumable shields still absorb actual damage") && ok;
	gameManager.player->defend = defendBeforeShieldHit;

	gameManager.effectManager->freeResource();
	ok = check(gameManager.player->shieldEffects.empty()
		&& gameManager.player->shieldEffect.expired()
		&& gameManager.player->shieldLife == 0,
		"reloaded self-shield reverse bindings clear with their effects") && ok;
	return ok;
}

bool runLegacySelfShieldHidingStateTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto damageReduceMagic = std::make_shared<Magic>();
	damageReduceMagic->initFromIni("damage_reduce_shield.ini");
	if (!check(damageReduceMagic->loadSucceeded,
		"legacy self-shield persistence fixture loads"))
	{
		return false;
	}

	Point playerPosition = gameManager.player->getPosition();
	auto effects = Magic::addEffect(
		damageReduceMagic,
		gameManager.player,
		playerPosition,
		playerPosition,
		1,
		5,
		0,
		lkSelf,
		nullptr);
	if (!check(effects.size() == 1 && effects[0] != nullptr,
		"legacy self-shield fixture creates one effect"))
	{
		return false;
	}

	bool ok = true;
	effects[0]->doing = ekHiding;
	effects[0]->waitTime = 0;
	INIReader terminalIni;
	terminalIni.SetInteger("Head", "Count", 1);
	effects[0]->saveToIni(&terminalIni, "PRO1");
	gameManager.effectManager->loadFromIni(terminalIni);
	ok = check(!gameManager.player->hasActiveSelfMagic(mskAddDamageReduceShield),
		"legacy terminal hiding shield is not rebound as active") && ok;

	effects = Magic::addEffect(
		damageReduceMagic,
		gameManager.player,
		playerPosition,
		playerPosition,
		1,
		5,
		0,
		lkSelf,
		nullptr);
	if (!check(effects.size() == 1 && effects[0] != nullptr,
		"legacy waiting self-shield fixture creates one effect"))
	{
		return false;
	}
	effects[0]->doing = ekHiding;
	effects[0]->waitTime = 250;
	INIReader waitingIni;
	waitingIni.SetInteger("Head", "Count", 1);
	effects[0]->saveToIni(&waitingIni, "PRO1");
	gameManager.effectManager->loadFromIni(waitingIni);
	ok = check(gameManager.player->hasActiveSelfMagic(mskAddDamageReduceShield),
		"legacy hiding shield with remaining wait time is rebound") && ok;

	gameManager.effectManager->freeResource();
	return ok;
}

bool runCarryBindingRoundTripTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto attachedTarget = std::make_shared<NPC>();
	attachedTarget->kind = nkBattle;
	attachedTarget->setPosition({ 12, 11 }, false);
	attachedTarget->setOffset({ 3.0f, -4.0f });
	gameManager.npcManager->npcList = { attachedTarget };

	auto magic = std::make_shared<Magic>();
	magic->initFromIni("carry.ini");
	if (!check(magic->loadSucceeded, "carry persistence fixture magic loads"))
	{
		return false;
	}
	auto effects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	if (!check(effects.size() == 1 && effects[0] != nullptr,
		"carry fixture creates one projectile"))
	{
		return false;
	}
	auto effect = effects[0];
	bool ok = true;
	ok = check(effect->carryUserActive && effect->carriedUser.lock() == gameManager.player,
		"carry projectile owns the caster before save") && ok;
	ok = check(gameManager.player->isHiddenByCarryMagic(),
		"HideUserWhenCarry reverse binding is active before save") && ok;
	ok = check(effect->handleCarryUser4AfterHit(attachedTarget)
		&& effect->getAttachedNPCCount() == 1,
		"CarryUser4 target is attached before save") && ok;

	INIReader ini;
	gameManager.effectManager->saveToIni(ini);
	ok = check(ini.GetInteger("PRO1", "AttachedNPCCount", 0) == 1
		&& ini.GetInteger("PRO1", "AttachedNPC1TileOffsetX", 0) != 0
		&& ini.GetInteger("PRO1", "AttachedNPC1TileOffsetY", 0) != 0
		&& ini.GetReal("PRO1", "AttachedNPC1OffsetDeltaX", 0.0f) != 0.0f
		&& ini.GetReal("PRO1", "AttachedNPC1OffsetDeltaY", 0.0f) != 0.0f
		&& ini.GetInteger("PRO1", "AttachedNPC1InitialMapX", 0) == 12
		&& ini.GetInteger("PRO1", "AttachedNPC1InitialMapY", 0) == 11
		&& ini.GetReal("PRO1", "AttachedNPC1InitialOffsetX", 0.0f) == 3.0f
		&& ini.GetReal("PRO1", "AttachedNPC1InitialOffsetY", 0.0f) == -4.0f
		&& ini.GetBoolean("PRO1", "AttachedNPC1PreserveOffset", false)
		&& ini.GetBoolean("PRO1", "AttachedNPC1DestroyOnObstacle", false),
		"carry attachment relative offset and collision metadata are serialized") && ok;
	ini.SetBoolean("PRO1", "AttachedNPC1HasMoved", true);
	gameManager.effectManager->loadFromIni(ini);
	if (!check(gameManager.effectManager->effectList.size() == 1
		&& gameManager.effectManager->effectList[0] != nullptr,
		"carry projectile reloads through EffectManager"))
	{
		return false;
	}
	auto loadedEffect = gameManager.effectManager->effectList[0];
	ok = check(loadedEffect->carryUserActive
		&& loadedEffect->carriedUser.lock() == gameManager.player,
		"carried caster identity survives save/load") && ok;
	ok = check(loadedEffect->getAttachedNPCCount() == 1
		&& loadedEffect->hasAttachedNPC(attachedTarget),
		"CarryUser4 attached target and metadata survive save/load") && ok;
	INIReader reserializedCarryIni;
	gameManager.effectManager->saveToIni(reserializedCarryIni);
	ok = check(reserializedCarryIni.GetInteger("PRO1", "AttachedNPC1TileOffsetX", 0)
			== ini.GetInteger("PRO1", "AttachedNPC1TileOffsetX", 0)
		&& reserializedCarryIni.GetInteger("PRO1", "AttachedNPC1TileOffsetY", 0)
			== ini.GetInteger("PRO1", "AttachedNPC1TileOffsetY", 0)
		&& reserializedCarryIni.GetReal("PRO1", "AttachedNPC1OffsetDeltaX", 0.0f)
			== ini.GetReal("PRO1", "AttachedNPC1OffsetDeltaX", 0.0f)
		&& reserializedCarryIni.GetReal("PRO1", "AttachedNPC1OffsetDeltaY", 0.0f)
			== ini.GetReal("PRO1", "AttachedNPC1OffsetDeltaY", 0.0f)
		&& reserializedCarryIni.GetInteger("PRO1", "AttachedNPC1InitialMapX", 0)
			== ini.GetInteger("PRO1", "AttachedNPC1InitialMapX", 0)
		&& reserializedCarryIni.GetInteger("PRO1", "AttachedNPC1InitialMapY", 0)
			== ini.GetInteger("PRO1", "AttachedNPC1InitialMapY", 0)
		&& reserializedCarryIni.GetReal("PRO1", "AttachedNPC1InitialOffsetX", 0.0f)
			== ini.GetReal("PRO1", "AttachedNPC1InitialOffsetX", 0.0f)
		&& reserializedCarryIni.GetReal("PRO1", "AttachedNPC1InitialOffsetY", 0.0f)
			== ini.GetReal("PRO1", "AttachedNPC1InitialOffsetY", 0.0f)
		&& reserializedCarryIni.GetBoolean("PRO1", "AttachedNPC1HasMoved", false)
		&& reserializedCarryIni.GetBoolean("PRO1", "AttachedNPC1PreserveOffset", false)
		&& reserializedCarryIni.GetBoolean("PRO1", "AttachedNPC1DestroyOnObstacle", false),
		"carry attachment metadata survives a second serialization") && ok;
	ok = check(gameManager.player->isHiddenByCarryMagic(),
		"HideUserWhenCarry reverse binding is restored after all NPCs load") && ok;
	loadedEffect->clearCarryUser();
	INIReader detachedCarryIni;
	gameManager.effectManager->saveToIni(detachedCarryIni);
	gameManager.effectManager->loadFromIni(detachedCarryIni);
	loadedEffect = gameManager.effectManager->effectList[0];
	ok = check(!loadedEffect->carryUserActive
		&& loadedEffect->getAttachedNPCCount() == 1
		&& loadedEffect->hasAttachedNPC(attachedTarget),
		"CarryUser4 attachments survive even after the original caster detaches") && ok;

	gameManager.effectManager->freeResource();
	ok = check(!gameManager.player->isHiddenByCarryMagic(),
		"reloaded carry binding is cleared with the effect") && ok;
	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runPendingMagicManagerRoundTripTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto target = std::make_shared<NPC>();
	target->kind = nkBattle;
	gameManager.npcManager->npcList = { target };

	auto trailMagic = std::make_shared<Magic>();
	trailMagic->initFromIni("trail.ini");
	auto delayedMagic = std::make_shared<Magic>();
	delayedMagic->initFromIni("delayed.ini");
	delayedMagic->experienceOwnerMagicFile = "parent.ini";
	if (!check(trailMagic->loadSucceeded && delayedMagic->loadSucceeded,
		"pending Magic fixtures load"))
	{
		return false;
	}

	gameManager.effectManager->addTrailMagic(trailMagic, gameManager.player, 1, 12, 3, lkFriend);
	gameManager.effectManager->addDelayedMagic(
		delayedMagic,
		gameManager.player,
		{ 1, 2 },
		{ 3, 4 },
		1,
		lkEnemy,
		target,
		987);
	auto exhaustedEffect = std::make_shared<Effect>();
	exhaustedEffect->level = 1;
	exhaustedEffect->initFromMagic(delayedMagic);
	exhaustedEffect->result = erLifeExhaust;
	gameManager.effectManager->addEffect(exhaustedEffect);
	auto transientVisualEffect = std::make_shared<Effect>();
	transientVisualEffect->result = erNone;
	gameManager.effectManager->addEffect(transientVisualEffect);

	INIReader ini;
	gameManager.effectManager->saveToIni(ini);
	bool ok = true;
	ok = check(ini.GetInteger("Head", "Count", -1) == 0,
		"terminal and source-less transient visual effects are omitted at the save boundary") && ok;
	ok = check(ini.GetInteger("Head", "TrailCount", 0) == 1
		&& ini.GetInteger("Head", "DelayedCount", 0) == 1,
		"pending trail and delayed Magic queues are serialized") && ok;

	gameManager.effectManager->loadFromIni(ini);
	ok = check(gameManager.effectManager->getPendingTrailMagicCount() == 1
		&& gameManager.effectManager->getPendingDelayedMagicCount() == 1,
		"pending Magic queues survive manager reload") && ok;
	INIReader reserialized;
	gameManager.effectManager->saveToIni(reserialized);
	ok = check(reserialized.Get("Trail1", "MagicFile", "") == "trail.ini"
		&& reserialized.GetTime("Trail1", "RemainingTime", 0) == 5000
		&& reserialized.GetInteger("Trail1", "Damage", 0) == 12
		&& reserialized.GetInteger("Trail1", "Evade", 0) == 3
		&& reserialized.GetInteger("Trail1", "Launcher", -1) == lkFriend,
		"trail Magic runtime parameters survive save/load") && ok;
	ok = check(reserialized.Get("Delayed1", "MagicFile", "") == "delayed.ini"
		&& reserialized.Get("Delayed1", "ExperienceOwnerMagicFile", "") == "parent.ini"
		&& reserialized.GetTime("Delayed1", "RemainingTime", 0) == 987
		&& reserialized.GetInteger("Delayed1", "FromMapX", 0) == 1
		&& reserialized.GetInteger("Delayed1", "FromMapY", 0) == 2
		&& reserialized.GetInteger("Delayed1", "ToMapX", 0) == 3
		&& reserialized.GetInteger("Delayed1", "ToMapY", 0) == 4
		&& reserialized.GetInteger("Delayed1", "TargetReferenceKind", 0) == 3,
		"delayed Magic target, owner, timing, and positions survive save/load") && ok;

	INIReader legacyIni;
	gameManager.effectManager->loadFromIni(legacyIni);
	ok = check(gameManager.effectManager->getPendingTrailMagicCount() == 0
		&& gameManager.effectManager->getPendingDelayedMagicCount() == 0,
		"legacy proj.ini without queue sections loads empty pending queues") && ok;
	INIReader missingFileNameIni;
	missingFileNameIni.SetInteger("Head", "Count", 1);
	missingFileNameIni.SetInteger("PRO1", "Level", 1);
	gameManager.effectManager->loadFromIni(missingFileNameIni);
	ok = check(gameManager.effectManager->effectList.empty(),
		"projectile records without a Magic file are skipped safely") && ok;
	INIReader invalidLevelIni;
	invalidLevelIni.SetInteger("Head", "Count", 1);
	invalidLevelIni.Set("PRO1", "FileName", "carry.ini");
	invalidLevelIni.SetInteger("PRO1", "Level", MAGIC_MAX_LEVEL + 1);
	gameManager.effectManager->loadFromIni(invalidLevelIni);
	ok = check(gameManager.effectManager->effectList.empty(),
		"projectile records with an invalid Magic level are skipped safely") && ok;
	INIReader missingMagicIni;
	missingMagicIni.SetInteger("Head", "Count", 1);
	missingMagicIni.Set("PRO1", "FileName", "missing.ini");
	missingMagicIni.SetInteger("PRO1", "Level", 1);
	gameManager.effectManager->loadFromIni(missingMagicIni);
	ok = check(gameManager.effectManager->effectList.empty(),
		"projectile records whose Magic resource fails to load are skipped safely") && ok;
	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runInvalidTrailDurationTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto invalidTrailMagic = std::make_shared<Magic>();
	invalidTrailMagic->initFromIni("invalid_trail.ini");
	bool ok = check(invalidTrailMagic->loadSucceeded
		&& invalidTrailMagic->keepMilliseconds == 0,
		"negative KeepMilliseconds is disabled instead of wrapping to a long-lived Trail");
	gameManager.effectManager->addTrailMagic(
		invalidTrailMagic,
		gameManager.player,
		1,
		0,
		0,
		lkSelf);
	ok = check(gameManager.effectManager->getPendingTrailMagicCount() == 0,
		"disabled Trail duration does not enter the pending Trail queue") && ok;
	return ok;
}

bool runMeteorPathRoundTripTest(GameManager& gameManager)
{
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("meteor.ini");
	if (!check(magic->loadSucceeded, "meteor persistence fixture magic loads"))
	{
		return false;
	}
	auto effects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	if (!check(effects.size() == 1 && effects[0] != nullptr && effects[0]->isEnteringWithMeteor(),
		"meteor fixture creates an active remaining path"))
	{
		return false;
	}

	INIReader ini;
	effects[0]->saveToIni(&ini, "PRO1");
	int savedPathCount = static_cast<int>(ini.GetInteger("PRO1", "MeteorPathCount", 0));
	Effect loadedEffect;
	loadedEffect.initFromIni(&ini, "PRO1");
	INIReader reserialized;
	loadedEffect.saveToIni(&reserialized, "PRO1");
	return check(savedPathCount > 0
		&& loadedEffect.isEnteringWithMeteor()
		&& reserialized.GetInteger("PRO1", "MeteorPathCount", 0) == savedPathCount,
		"meteor active flag and remaining path survive save/load");
}

bool runExclusiveMeteorMovementTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("meteor.ini");
	if (!check(magic->loadSucceeded, "meteor movement fixture magic loads"))
	{
		return false;
	}

	auto expectedEffects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	auto actualEffects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	if (!check(expectedEffects.size() == 1 && actualEffects.size() == 1,
		"meteor movement fixture creates comparable projectiles"))
	{
		return false;
	}

	auto expected = expectedEffects[0];
	auto actual = actualEffects[0];
	expected->waitTime = 0;
	actual->waitTime = 0;
	constexpr UTime FrameMilliseconds = 40;
	EffectTestAccess::updateMeteorMove(*expected, FrameMilliseconds);
	EffectTestAccess::setFrameTime(*actual, FrameMilliseconds);
	actual->setTime(actual->beginTime + FrameMilliseconds);
	EffectTestAccess::update(*actual);

	return check(actual->position == expected->position
		&& std::abs(actual->offset.x - expected->offset.x) < 0.001f
		&& std::abs(actual->offset.y - expected->offset.y) < 0.001f,
		"meteor path movement is not followed by a second ordinary projectile step");
}

bool runExclusiveRoundMovementTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("round.ini");
	if (!check(magic->loadSucceeded, "round movement fixture magic loads"))
	{
		return false;
	}

	auto expectedEffects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	auto actualEffects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	if (!check(expectedEffects.size() == 4 && actualEffects.size() == 4,
		"round movement fixture creates four comparable projectiles"))
	{
		return false;
	}

	auto expected = expectedEffects[0];
	auto actual = actualEffects[0];
	constexpr UTime FrameMilliseconds = 40;
	EffectTestAccess::updateRoundMovePosition(*expected, FrameMilliseconds);
	EffectTestAccess::setFrameTime(*actual, FrameMilliseconds);
	actual->setTime(actual->beginTime + FrameMilliseconds);
	EffectTestAccess::update(*actual);

	return check(actual->position == expected->position
		&& std::abs(actual->offset.x - expected->offset.x) < 0.001f
		&& std::abs(actual->offset.y - expected->offset.y) < 0.001f,
		"round movement stays on the configured orbit without an extra tangent step");
}

bool runTimeStopDurationTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("time_stop.ini");
	if (!check(magic->loadSucceeded, "time-stop duration fixture magic loads"))
	{
		return false;
	}

	auto effects = Magic::addEffect(
		magic,
		gameManager.player,
		gameManager.player->getPosition(),
		gameManager.player->getPosition(),
		1,
		0,
		0,
		lkSelf,
		nullptr);
	if (!check(effects.size() == 1 && gameManager.effectManager->hasActiveTimeStopper(),
		"time-stop fixture starts as the active stopper"))
	{
		return false;
	}

	auto effect = effects[0];
	constexpr UTime ExpectedDurationMilliseconds = 5000;
	effect->setTime(effect->beginTime + ExpectedDurationMilliseconds - 1);
	EffectTestAccess::update(*effect);
	bool ok = check(gameManager.effectManager->hasActiveTimeStopper(),
		"time-stop remains active until its configured five-second duration");
	effect->setTime(effect->beginTime + ExpectedDurationMilliseconds);
	EffectTestAccess::update(*effect);
	ok = check(!gameManager.effectManager->hasActiveTimeStopper()
		&& (effect->result & erLifeExhaust) != 0,
		"time-stop releases the update gate when its configured duration expires") && ok;
	return ok;
}

bool runHeartCircleSpeedRoundTripTest(GameManager& gameManager)
{
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("heart.ini");
	if (!check(magic->loadSucceeded, "heart-circle persistence fixture magic loads"))
	{
		return false;
	}

	auto effects = Magic::addEffect(
		magic,
		gameManager.player,
		{ 10, 10 },
		{ 11, 10 },
		1,
		10,
		0,
		lkSelf,
		nullptr);
	auto variedEffect = std::find_if(effects.begin(), effects.end(), [&](const auto& effect)
	{
		return effect != nullptr && effect->speed != magic->level[1].speed;
	});
	if (!check(variedEffect != effects.end(),
		"heart-circle creates a projectile with per-effect speed"))
	{
		return false;
	}

	const int expectedEffectSpeed = (*variedEffect)->speed;
	INIReader ini;
	(*variedEffect)->saveToIni(&ini, "PRO1");
	Effect loadedEffect;
	loadedEffect.initFromIni(&ini, "PRO1");
	return check(loadedEffect.speed == expectedEffectSpeed
		&& loadedEffect.magic.level[1].speed == magic->level[1].speed,
		"heart-circle projectile keeps its varied speed without mutating the Magic snapshot");
}

bool runStateRoundTripTest(GameManager& gameManager)
{
	auto mapNpcA = std::make_shared<NPC>();
	mapNpcA->kind = nkBattle;
	auto mapNpcB = std::make_shared<NPC>();
	mapNpcB->kind = nkBattle;
	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	gameManager.npcManager->npcList = { mapNpcA, partner, mapNpcB };

	auto magic = std::make_shared<Magic>();
	magic->initFromIni("persistence.ini");
	if (!check(magic->loadSucceeded, "effect persistence fixture magic loads"))
	{
		return false;
	}

	Effect savedEffect;
	savedEffect.level = 1;
	savedEffect.user = gameManager.player;
	savedEffect.target = mapNpcB;
	savedEffect.initFromMagic(magic);
	savedEffect.speed = 37;
	savedEffect.additionalEffect = maePoison;
	savedEffect.rangeElapsedMilliseconds = 321;
	savedEffect.flyMagicElapsedMilliseconds = 432;
	savedEffect.leapTimesRemaining = 2;
	savedEffect.leapFlying = true;
	savedEffect.leapHitTargets = { mapNpcB, partner };
	savedEffect.passThroughHitTargets = { mapNpcA };
	savedEffect.parasiticTarget = mapNpcB;
	savedEffect.parasiticElapsedMilliseconds = 543;
	savedEffect.parasiticTotalEffect = 77;
	savedEffect.magicWhenNewPositionInitialized = true;
	savedEffect.magicWhenNewPositionLastTile = { 17, 29 };
	savedEffect.explodeMagicTriggered = true;
	savedEffect.moveBackActive = true;
	savedEffect.circleMoveBaseDirectionInitialized = true;
	savedEffect.circleMoveBaseDirection = { 101, -202 };
	savedEffect.roundMoveActive = true;
	savedEffect.roundMoveDegree = 73.5f;
	savedEffect.vibrationTriggered = true;
	savedEffect.vanishing = true;

	INIReader ini;
	savedEffect.saveToIni(&ini, "PRO1");
	gameManager.npcManager->npcList = { partner, mapNpcA, mapNpcB };
	Effect loadedEffect;
	loadedEffect.initFromIni(&ini, "PRO1");

	bool ok = true;
	ok = check(loadedEffect.speed == 37 && loadedEffect.magic.level[1].speed == 24,
		"per-projectile speed survives without overwriting the Magic level snapshot") && ok;
	ok = check(loadedEffect.additionalEffect == maePoison,
		"dynamic attack additional effect survives save/load") && ok;
	ok = check(loadedEffect.rangeElapsedMilliseconds == 321,
		"range cadence survives save/load without an extra immediate tick") && ok;
	ok = check(loadedEffect.flyMagicElapsedMilliseconds == 432,
		"fly child cadence survives save/load") && ok;
	ok = check(loadedEffect.leapTimesRemaining == 2 && loadedEffect.leapFlying,
		"leap remaining count and phase survive save/load") && ok;
	ok = check(loadedEffect.leapHitTargets.size() == 2
		&& loadedEffect.hasLeapHitTarget(mapNpcB)
		&& loadedEffect.hasLeapHitTarget(partner),
		"leap hit history survives NPC/partner load-group reordering") && ok;
	ok = check(loadedEffect.passThroughHitTargets.size() == 1
		&& loadedEffect.hasPassThroughHitTarget(mapNpcA),
		"pass-through hit history survives save/load") && ok;
	ok = check(loadedEffect.parasiticTarget.lock() == mapNpcB
		&& loadedEffect.parasiticElapsedMilliseconds == 543
		&& loadedEffect.parasiticTotalEffect == 77,
		"parasitic target, cadence, and accumulated effect survive save/load") && ok;
	ok = check(loadedEffect.magicWhenNewPositionInitialized
		&& loadedEffect.magicWhenNewPositionLastTile == Point{ 17, 29 },
		"MagicWhenNewPosition tile cursor survives save/load") && ok;
	ok = check(loadedEffect.explodeMagicTriggered,
		"explode child one-shot state survives save/load") && ok;
	ok = check(loadedEffect.moveBackActive,
		"move-back phase survives save/load") && ok;
	ok = check(loadedEffect.circleMoveBaseDirectionInitialized
		&& loadedEffect.circleMoveBaseDirection == Point{ 101, -202 },
		"circle movement base direction survives save/load") && ok;
	ok = check(loadedEffect.roundMoveActive && loadedEffect.roundMoveDegree == 73.5f,
		"round movement phase survives save/load") && ok;
	ok = check(loadedEffect.vibrationTriggered,
		"screen vibration one-shot state survives save/load") && ok;
	ok = check(loadedEffect.vanishing,
		"vanishing phase survives save/load") && ok;

	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runSummonTransientPersistenceTest(GameManager& gameManager)
{
	gameManager.effectManager->freeResource();
	gameManager.npcManager->freeResource();
	gameManager.objectManager->freeResource();
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 16;
	gameManager.map->data->head.height = 16;
	gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
	gameManager.map->createDataMap();
	gameManager.global.data.mapName = "summon_transient.map";
	gameManager.player->setPosition({ 1, 1 }, false);

	auto addPersistentNpc = [&](const std::string& name, Point position)
	{
		auto npc = std::make_shared<NPC>();
		npc->npcName = name;
		npc->kind = nkBattle;
		npc->relation = nrHostile;
		npc->life = 20;
		npc->lifeMax = 20;
		npc->setPosition(position, false);
		gameManager.npcManager->addNPC(npc);
		return npc;
	};
	auto prepareDeathAction = [](const std::shared_ptr<NPC>& npc)
	{
		npc->res.death.imagePackage = std::make_shared<IMPImage>();
		npc->res.death.imagePackage->directions = 1;
		npc->res.death.imagePackage->interval = 50;
		npc->res.death.imagePackage->frame.resize(2);
	};

	addPersistentNpc("PERSISTENT_BEFORE", { 2, 2 });
	auto summonMagic = std::make_shared<Magic>();
	summonMagic->initFromIni("summon_transient.ini");
	bool ok = check(summonMagic->loadSucceeded
		&& summonMagic->level[1].moveKind == mmkSummon
		&& summonMagic->maxCount == 1,
		"summon persistence fixture loads MoveKind and MaxCount");

	auto summonEffects = Magic::addSummonEffect(
		summonMagic,
		gameManager.player,
		gameManager.player->getPosition(),
		{ 5, 5 },
		1,
		10,
		0,
		lkSelf);
	auto originalSummonEffect = summonEffects.empty() ? nullptr : summonEffects.front();
	auto originalSummonedNpc = originalSummonEffect == nullptr
		? nullptr
		: originalSummonEffect->summonedNPC.lock();
	ok = check(originalSummonEffect != nullptr
		&& originalSummonedNpc != nullptr
		&& originalSummonedNpc->transientSummonedNPC,
		"created summon Effect marks its NPC with durable transient provenance") && ok;
	auto normalAfter = addPersistentNpc("PERSISTENT_AFTER", { 8, 8 });
	auto persistentPartner = std::make_shared<NPC>();
	persistentPartner->npcName = "PERSISTENT_PARTNER";
	persistentPartner->kind = nkPartner;
	persistentPartner->relation = nrFriendly;
	persistentPartner->setPosition({ 9, 8 }, false);
	gameManager.npcManager->addNPC(persistentPartner);
	auto transientPartner = std::make_shared<NPC>();
	transientPartner->npcName = "TRANSIENT_SUMMON_PARTNER";
	transientPartner->kind = nkPartner;
	transientPartner->relation = nrFriendly;
	transientPartner->transientSummonedNPC = true;
	transientPartner->setPosition({ 10, 8 }, false);
	gameManager.npcManager->addNPC(transientPartner);
	gameManager.partnerManager.save(7);
	INIReader partnerSave("save\\game\\partner7.ini");
	ok = check(partnerSave.GetInteger("Head", "Count", 0) == 1
		&& partnerSave.Get("Partner000", "Name", "") == "PERSISTENT_PARTNER",
		"partner save uses the same transient summon filter as normal NPC save") && ok;
	gameManager.npcManager->deleteNPC(std::vector<int>{ 3, 4 });

	auto persistentMagic = std::make_shared<Magic>();
	persistentMagic->initFromIni("persistence.ini");
	auto persistentEffect = std::make_shared<Effect>();
	persistentEffect->level = 1;
	persistentEffect->user = gameManager.player;
	persistentEffect->target = normalAfter;
	persistentEffect->position = { 7, 7 };
	persistentEffect->src = persistentEffect->position;
	persistentEffect->initFromMagic(persistentMagic);
	gameManager.effectManager->addEffect(persistentEffect);

	INIReader effectSave;
	gameManager.effectManager->saveToIni(effectSave);
	ok = check(effectSave.GetInteger("Head", "Count", 0) == 1
		&& effectSave.Get("PRO1", "FileName", "") == "persistence.ini",
		"Effect save excludes the active Summon Effect and compacts PRO indices") && ok;
	ok = check(effectSave.GetInteger("PRO1", "TargetReferenceKind", 0) == 3
		&& effectSave.GetInteger("PRO1", "TargetReferenceIndex", -1) == 1,
		"persistent Effect target ordinal skips the transient summon between normal NPCs") && ok;

	gameManager.npcManager->save("summon_roundtrip.npc");
	INIReader npcSave("save\\game\\summon_roundtrip.npc");
	ok = check(npcSave.ParseError() == 0
		&& npcSave.GetInteger("Head", "Count", 0) == 2
		&& npcSave.Get("NPC000", "Name", "") == "PERSISTENT_BEFORE"
		&& npcSave.Get("NPC001", "Name", "") == "PERSISTENT_AFTER",
		"NPC save excludes the summon and preserves normal NPC ordering") && ok;

	std::weak_ptr<NPC> originalSummonProbe = originalSummonedNpc;
	originalSummonedNpc.reset();
	originalSummonEffect.reset();
	summonEffects.clear();
	gameManager.npcManager->load("summon_roundtrip.npc", true);
	ok = check(originalSummonProbe.expired()
		&& gameManager.npcManager->npcList.size() == 2,
		"NPC load drops the previous transient summon and restores only normal NPCs") && ok;
	gameManager.effectManager->loadFromIni(effectSave);
	auto loadedPersistentEffect = gameManager.effectManager->effectList.size() == 1
		? gameManager.effectManager->effectList.front()
		: nullptr;
	auto loadedTarget = loadedPersistentEffect == nullptr
		? nullptr
		: std::dynamic_pointer_cast<NPC>(loadedPersistentEffect->target.lock());
	ok = check(loadedTarget != nullptr && loadedTarget->npcName == "PERSISTENT_AFTER",
		"Effect load resolves the post-summon normal NPC ordinal to the original target") && ok;
	ok = check(gameManager.player->summonedNpcsCount(*summonMagic) == 0,
		"loading the save leaves no live summon in the caster MaxCount queue") && ok;

	auto firstRecast = Magic::addSummonEffect(
		summonMagic,
		gameManager.player,
		gameManager.player->getPosition(),
		{ 6, 5 },
		1,
		10,
		0,
		lkSelf);
	auto firstRecastEffect = firstRecast.empty() ? nullptr : firstRecast.front();
	auto firstRecastNpc = firstRecastEffect == nullptr ? nullptr : firstRecastEffect->summonedNPC.lock();
	ok = check(firstRecastNpc != nullptr
		&& gameManager.player->summonedNpcsCount(*summonMagic) == 1,
		"Summon can be cast again after load and repopulates the MaxCount queue") && ok;
	if (firstRecastNpc != nullptr)
	{
		prepareDeathAction(firstRecastNpc);
	}

	auto secondRecast = Magic::addSummonEffect(
		summonMagic,
		gameManager.player,
		gameManager.player->getPosition(),
		{ 7, 5 },
		1,
		10,
		0,
		lkSelf);
	auto secondRecastEffect = secondRecast.empty() ? nullptr : secondRecast.front();
	auto secondRecastNpc = secondRecastEffect == nullptr ? nullptr : secondRecastEffect->summonedNPC.lock();
	ok = check(secondRecastNpc != nullptr
		&& secondRecastNpc != firstRecastNpc
		&& firstRecastNpc != nullptr
		&& firstRecastNpc->isDying()
		&& firstRecastNpc->transientSummonedNPC
		&& gameManager.player->summonedNpcsCount(*summonMagic) == 1,
		"MaxCount replacement kills the oldest summon without losing its transient provenance") && ok;

	if (secondRecastNpc != nullptr)
	{
		prepareDeathAction(secondRecastNpc);
		secondRecastNpc->life = 0;
		secondRecastNpc->handleDeath();
	}
	ok = check(secondRecastNpc != nullptr
		&& secondRecastNpc->isDying()
		&& secondRecastNpc->transientSummonedNPC
		&& secondRecastNpc->summonedByMagicEffect.expired()
		&& secondRecastEffect != nullptr
		&& secondRecastEffect->vanishing,
		"summon death detaches both runtime directions while retaining transient save provenance") && ok;

	gameManager.npcManager->save("summon_after_death.npc");
	INIReader npcAfterDeath("save\\game\\summon_after_death.npc");
	ok = check(npcAfterDeath.GetInteger("Head", "Count", 0) == 2,
		"active and dying summons remain excluded from NPC saves after owner detachment") && ok;
	INIReader effectAfterDeath;
	gameManager.effectManager->saveToIni(effectAfterDeath);
	ok = check(effectAfterDeath.GetInteger("Head", "Count", 0) == 1,
		"active and vanishing Summon Effects remain excluded from Effect saves") && ok;
	if (secondRecastNpc != nullptr)
	{
		secondRecastNpc->result |= erLifeExhaust;
	}
	gameManager.npcManager->onUpdate();
	ok = check(std::find(
		gameManager.npcManager->npcList.begin(),
		gameManager.npcManager->npcList.end(),
		secondRecastNpc) == gameManager.npcManager->npcList.end()
		&& gameManager.objectManager->objectList.empty(),
		"summon death cleanup removes the NPC without creating a persistent body object") && ok;

	INIReader legacySummonSave;
	legacySummonSave.SetInteger("Head", "Count", 1);
	legacySummonSave.Set("PRO1", "FileName", "summon_transient.ini");
	legacySummonSave.SetInteger("PRO1", "Level", 1);
	gameManager.effectManager->loadFromIni(legacySummonSave);
	ok = check(gameManager.effectManager->effectList.empty(),
		"legacy proj.ini Summon records are ignored instead of restoring a half-linked Effect") && ok;

	gameManager.effectManager->freeResource();
	gameManager.npcManager->freeResource();
	gameManager.objectManager->freeResource();
	return ok;
}

bool runLegacySaveFallbackTest()
{
	INIReader legacyIni;
	legacyIni.Set("PRO1", "FileName", "persistence.ini");
	legacyIni.SetInteger("PRO1", "Level", 1);
	Effect loadedEffect;
	loadedEffect.initFromIni(&legacyIni, "PRO1");

	bool ok = true;
	ok = check(loadedEffect.additionalEffect == maeFrozen,
		"legacy save derives AdditionalEffect from the Magic file") && ok;
	ok = check(loadedEffect.speed == 24,
		"legacy save derives runtime speed from the Magic level") && ok;
	ok = check(loadedEffect.rangeElapsedMilliseconds == 1000,
		"legacy save keeps the initial immediate RangeEffect cadence") && ok;
	ok = check(loadedEffect.flyMagicElapsedMilliseconds == 0,
		"legacy save keeps the initial FlyMagic cadence") && ok;
	ok = check(loadedEffect.leapTimesRemaining == 4 && !loadedEffect.leapFlying,
		"legacy save keeps the configured initial leap state") && ok;
	ok = check(loadedEffect.leapHitTargets.empty() && loadedEffect.passThroughHitTargets.empty(),
		"legacy save has no fabricated hit history") && ok;
	ok = check(!loadedEffect.vanishing,
		"legacy save defaults to the active phase") && ok;
	return ok;
}
}

bool runEffectRuntimePersistenceTests()
{
	auto root = makeUniqueTestDirectory("jxqy_effect_runtime_persistence_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "magic", errorCode);
	ScopedPlatformStateParent platformStateParent(root);
	File::setAssetsCollectionRoot(root.string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(EffectPersistenceSaveNamespace);
	if (!prepareMagicFixture(root))
	{
		return check(false, "write effect persistence fixture");
	}

	GameManager gameManager;
	bool ok = runThrowHeightSafetyTest();
	ok = runMagicManagerLoadCompatibilityTest(
		gameManager,
		root) && ok;
	ok = runGoodsManagerLoadContractTest(
		gameManager,
		root) && ok;
	ok = runPlayerChangeCorruptTargetRollbackTest(
		gameManager,
		root) && ok;
	ok = runSummonTransientPersistenceTest(gameManager) && ok;
	ok = runMeteorPathRoundTripTest(gameManager) && ok;
	ok = runExclusiveMeteorMovementTest(gameManager) && ok;
	ok = runExclusiveRoundMovementTest(gameManager) && ok;
	ok = runTimeStopDurationTest(gameManager) && ok;
	ok = runHeartCircleSpeedRoundTripTest(gameManager) && ok;
	ok = runStateRoundTripTest(gameManager) && ok;
	ok = runLegacySaveFallbackTest() && ok;
	ok = runCarryBindingRoundTripTest(gameManager) && ok;
	ok = runSelfShieldBindingRoundTripTest(gameManager) && ok;
	ok = runLegacySelfShieldHidingStateTest(gameManager) && ok;
	ok = runInvalidTrailDurationTest(gameManager) && ok;
	ok = runPendingMagicManagerRoundTripTest(gameManager) && ok;
	ok = runDetachedCasterLifetimeTest(gameManager) && ok;
	ok = runDetachedCasterPersistenceTest(gameManager) && ok;
	ok = runPersistenceCollectionBoundsTest(gameManager) && ok;
	ok = runDetachedCasterLiveGateTest(gameManager) && ok;
	ok = runDetachedCasterTransitionGateTest(gameManager) && ok;
	std::filesystem::remove_all(root, errorCode);
	return ok;
}
