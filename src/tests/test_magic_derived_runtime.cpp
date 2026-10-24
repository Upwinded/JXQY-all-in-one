#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Engine/Engine.h"
#include "../Game/Config/Config.h"
#include "../Game/Data/Effect.h"
#include "../Game/Data/EffectManager.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/MagicManager.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/GoodsMenu.h"
#include "../Game/Menu/MsgBox.h"
#include "../Game/Menu/PracticeMenu.h"
#include "../Game/Menu/StateMenu.h"
#include "../Game/Menu/SystemNotice.h"
#include "../Resource/ResourceManager.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

class RageSystemTestAccess
{
public:
	static void updatePlayer(Player& player)
	{
		player.onUpdate();
	}
	static void advancePlayerMagic(Player& player)
	{
		player.setTime(player.getTime() + player.actionLastTime);
		player.actionManager->update(0);
	}
	static void updateEffect(Effect& effect)
	{
		effect.onUpdate();
	}
	static void advanceEffect(Effect& effect, UTime elapsed)
	{
		effect.setTime(effect.getTime() + elapsed);
		effect.frameTime = elapsed;
		effect.onUpdate();
	}
	static void updateCarryUserPosition(Effect& effect)
	{
		effect.updateCarryUserPosition();
	}
	static void updateAttachedNPCs(Effect& effect)
	{
		effect.updateAttachedNPCs();
	}
	static void updateRangeEffect(Effect& effect, UTime frameTime)
	{
		effect.updateRangeEffect(frameTime);
	}

	static void updateFlyMagic(Effect& effect, UTime frameTime)
	{
		effect.updateFlyMagic(frameTime);
	}

	static void updateParasiticEffect(Effect& effect, UTime frameTime)
	{
		effect.updateParasiticEffect(frameTime);
	}

	static void updateMagicWhenNewPosition(Effect& effect)
	{
		effect.updateMagicWhenNewPosition();
	}

	static void recordActualDamage(Player& player, int damage)
	{
		player.recordActualDamageForRage(damage);
	}
};

class MagicDerivedRuntimeTestAccess
{
public:
	static void updateTrailMagic(EffectManager& manager)
	{
		manager.updateTrailMagic();
	}

	static void updateDelayedMagic(EffectManager& manager)
	{
		manager.updateDelayedMagic();
	}
};

namespace
{
constexpr char MagicDerivedSaveNamespace[] =
	"magic-derived-runtime";

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool runEffectProjectedDirectionTest()
{
	Effect effect;
	effect.magic.flyImage = std::make_shared<IMPImage>();
	effect.magic.flyImage->directions = 16;
	bool ok = check(effect.getDirection({ 1000, 1000 }) == 14,
		"effect direction uses the unprojected angle of the animation frames");
	effect.magic.flyImage->directions = 32;
	ok = check(effect.getDirection({ 1000, 1000 }) == 28,
		"effect direction uses the actual 32-direction flying image") && ok;
	effect.magic.flyImage->directions = 8;
	ok = check(effect.getDirection({ 1000, 1000 }) == 7,
		"effect direction uses the actual 8-direction flying image") && ok;
	effect.magic.flyImage->directions = 32;
	ok = check(effect.getDirection({ 1000, 2000 }) == 30,
		"target direction is not projected a second time for animation selection") && ok;
	const Point origin = { 100, 100 };
	for (int directionCount : { 8, 16, 32 })
	{
		effect.magic.flyImage->directions = directionCount;
		for (int expectedDirection = 0; expectedDirection < directionCount; ++expectedDirection)
		{
			const double angle = 2.0 * std::acos(-1.0) * expectedDirection / directionCount;
			const Point target =
			{
				origin.x - (int)std::lround(std::sin(angle) * 1000.0),
				origin.y + (int)std::lround(std::cos(angle) * 1000.0) * 2
			};
			const Point projected = Map::getTilePosition(target, origin);
			const Point flyingDirection = { projected.x, (int)(projected.y * MapXRatio) };
			ok = check(Magic::getDirection(origin, target, directionCount) == expectedDirection,
				"tile-target animation direction uses the unprojected angle in every quadrant") && ok;
			ok = check(effect.getDirection(flyingDirection) == expectedDirection,
				"moving-effect animation direction agrees with the tile-target direction") && ok;
		}
		ok = check(Magic::getDirection(-0.01f, directionCount) == 0
			&& Magic::getDirection((float)(2.0 * std::acos(-1.0) - 0.01), directionCount) == 0,
			"angles just before a full turn wrap to the first animation direction") && ok;
	}
	ok = check(Magic::getDirection(1.0f, 0) == 0 && Magic::getDirection(-0.01f, 1) == 0,
		"single-direction and absent direction metadata select the first frame") && ok;
	return ok;
}

bool runEffectPathWidthTest()
{
	Effect effect;
	bool ok = true;
	for (float width : { 0.0f, 0.5f, 1.0f })
	{
		effect.width = width;
		for (int direction = 0; direction < 8; ++direction)
		{
			const Point projected = Map::getTilePosition(Map::getSubPoint({ 0, 0 }, direction), { 0, 0 });
			effect.flyingDirection = { projected.x, (int)(projected.y * MapXRatio) };
			const auto offsets = effect.getPathOffsets();
			const PointEx side = offsets[1];
			ok = check(std::abs(Map::getTileDistance({ 0, 0 }, { 0, 0 }, { 0, 0 }, side) - width * 0.5f) < 0.0001f,
				"wall edge paths use the same tile radius as character and projectile collisions") && ok;
			ok = check(std::abs(side.x / MapXRatio * effect.flyingDirection.x
				+ side.y * effect.flyingDirection.y) < 0.0001f,
				"wall edge offset is perpendicular before the map projection") && ok;
			effect.flyingDirection = { -effect.flyingDirection.x, -effect.flyingDirection.y };
			const auto reversedOffsets = effect.getPathOffsets();
			ok = check(offsets[0].x == 0 && offsets[0].y == 0
				&& offsets[1].x == -offsets[2].x && offsets[1].y == -offsets[2].y
				&& offsets[1].x == reversedOffsets[2].x && offsets[1].y == reversedOffsets[2].y,
				"reversing flight preserves the same center and two edge paths") && ok;
		}
	}
	for (int carryMode : { 3, 4 })
	{
		effect.magic.carryUser = carryMode;
		for (const auto offset : effect.getPathOffsets())
		{
			ok = check(offset.x == 0 && offset.y == 0,
				"point-path carry modes retain zero wall edge offsets") && ok;
		}
	}
	return ok;
}

bool runEffectInitialDirectionTest(GameManager& gameManager)
{
	const auto originalMap = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 64;
	gameManager.map->data->tile.assign(64, std::vector<MapTile>(64));
	gameManager.map->createDataMap();
	const Point origin = { 20, 20 };
	const Point target = { 24, 24 };
	bool ok = true;
	for (int directionCount : { 8, 16, 32 })
	{
		auto magic = std::make_shared<Magic>();
		magic->flyImage = std::make_shared<IMPImage>();
		magic->flyImage->directions = directionCount;
		magic->level[1].lifeFrame = 100;
		for (int moveKind : { mmkLine, mmkMoveLine, mmkVMove })
		{
			magic->level[1].moveKind = moveKind;
			auto effects = moveKind == mmkLine
				? Magic::addLineEffect(magic, gameManager.player, origin, target, 1, 0, 0, lkSelf)
				: moveKind == mmkMoveLine
					? Magic::addMoveLineEffect(magic, gameManager.player, origin, target, 1, 0, 0, lkSelf)
					: Magic::addVMoveEffect(magic, gameManager.player, origin, target, 1, 0, 0, lkSelf);
			ok = check(!effects.empty(), "line and V-shaped magic create their initial effects") && ok;
			for (const auto& effect : effects)
			{
				ok = check(effect->direction == 7 * directionCount / 8,
					"line and V-shaped animations follow the actual eight-way cast or flight direction") && ok;
			}
			gameManager.effectManager->freeResource();
		}

		auto effect = std::make_shared<Effect>();
		effect->magic.flyImage = magic->flyImage;
		effect->magic.ball = 1;
		effect->doing = ekFlying;
		effect->position = origin;
		effect->flyingDirection = { 1000, 0 };
		auto hitTarget = std::make_shared<NPC>();
		hitTarget->setPosition({ origin.x + 1, origin.y }, false);
		ok = check(effect->handleBallAfterHit(hitTarget, origin)
			&& effect->position == Point{ origin.x - 1, origin.y }
			&& effect->direction == directionCount / 4,
			"ball reflection steps to the same neighboring tile for 8, 16 and 32 animation directions") && ok;
		gameManager.effectManager->freeResource();
	}
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

bool runDirectDamageShieldTest(GameManager& gameManager)
{
	gameManager.global.feature.rageSystem = false;
	gameManager.setCheatModeEnabled(false);
	auto caster = std::make_shared<NPC>();
	caster->kind = nkBattle;
	caster->relation = nrHostile;
	caster->lifeMax = 1000;
	caster->attack = 100;
	gameManager.npcManager->npcList = { caster };
	bool ok = true;
	for (int kind : { nkPlayer, nkBattle, nkPartner })
	{
		std::shared_ptr<NPC> target = kind == nkPlayer ? gameManager.player : std::make_shared<NPC>();
		target->kind = kind;
		target->relation = nrFriendly;
		target->lifeMax = target->life = 1000;
		target->manaMax = target->mana = 100;
		target->defend = target->defend2 = target->defend3 = 0;
		target->evade = 100000;
		target->invincible = 0;
		target->disableMoveMilliseconds = 0;
		if (kind == nkPlayer) gameManager.player->calInfo();
		gameManager.npcManager->npcList = { caster };
		if (kind != nkPlayer) gameManager.npcManager->npcList.push_back(target);
		auto block = std::make_shared<Effect>();
		block->level = 1;
		block->doing = ekFlying;
		block->magic.level[1].moveKind = mmkSelf;
		block->magic.level[1].specialKind = mskBlockDamage;
		target->shieldEffects = { block };
		target->shieldLife = 300;
		auto incoming = std::make_shared<Effect>();
		incoming->user = caster;
		incoming->level = 1;
		incoming->launcherKind = lkEnemy;
		incoming->damage = incoming->magic.level[1].effect = 100;
		incoming->damageMana = incoming->magic.level[1].effectMana = 10;
		incoming->magic.level[1].moveKind = mmkFly;
		incoming->magic.disableMoveMilliseconds = 1000;
		incoming->magic.restoreProbability = 100;
		incoming->magic.restorePercent = 50;
		incoming->magic.restoreType = 0;
		incoming->magic.linkedLevel[1].parasitic = 1;
		incoming->magic.linkedLevel[1].parasiticInterval = 100;
		auto counter = std::make_shared<Magic>();
		counter->loadSucceeded = true;
		for (auto& level : counter->level) level.moveKind = mmkFly;
		target->magicToUseWhenBeAttacked = counter;
		target->equipmentMagicToUseWhenAttacked.clear();
		gameManager.effectManager->clearEffect();
		const size_t effectCount = gameManager.effectManager->effectList.size();
		caster->life = 500;
		target->hurt(incoming, 0);
		target->directHurt(incoming);
		ok = check(incoming->beginParasitic(target, target->getPosition()), "attach a real parasitic effect") && ok;
		RageSystemTestAccess::updateParasiticEffect(*incoming, 100);
		ok = check(target->life == 1000 && target->mana == 100 && target->shieldLife == 300
			&& target->disableMoveMilliseconds == 0 && caster->life == 500,
			"immunity blocks ordinary/direct/initial-parasitic/tick health, mana, states and leech for player, NPC and partner") && ok;
		ok = check(gameManager.effectManager->effectList.size() == effectCount + 4,
			"blocked hits still trigger one counterattack each") && ok;
		block->vanishing = true;
		const int loss = kind == nkPlayer ? static_cast<int>(std::round(100 * DAMAGE_RATE)) : 100;
		RageSystemTestAccess::updateParasiticEffect(*incoming, 100);
		ok = check(target->life == 1000 - loss && target->mana == 90 && caster->life == 550
			&& target->shieldLife == 300,
			"parasitic damage resumes after immunity expires, bypasses hit rolls and retains capacity-shield behavior") && ok;
		block->vanishing = false;
		block->magic.level[1].specialKind = mskAddDamageReduceShield;
		block->magic.level[1].effect = 20;
		target->shieldEffects = { block };
		target->directHurt(incoming);
		const int reducedLoss = kind == nkPlayer ? static_cast<int>(std::round(80 * DAMAGE_RATE)) : 80;
		ok = check(target->life == 1000 - loss - reducedLoss && target->shieldLife == 300,
			"direct damage retains numeric reduction without consuming capacity shields") && ok;
		target->attack = 60;
		target->attack2 = 12;
		target->attack3 = 7;
		if (kind == nkPlayer)
		{
			gameManager.player->info.lifeMax = 1000;
			gameManager.player->info.attack = 60;
			gameManager.player->calInfo();
		}
		block->magic.level[1].effect = 0;
		block->magic.level[1].effectExt = 10;
		incoming->damage2 = 20;
		incoming->damage3 = 10;
		const int primaryReduction = target->getAttack() + 10;
		const int expectedDamage = std::max(
			100 - primaryReduction
				+ std::max(0, 20 - target->getAttack2())
				+ std::max(0, 10 - target->getAttack3()),
			gameManager.global.minimumMagicDamage);
		ok = check(target->calculateEffectDamage(incoming) == expectedDamage,
			"zero-Effect reduction uses the current beneficiary attack channels and primary extension") && ok;
		target->life = 1000;
		target->shieldLife = 0;
		target->hurtLife(100, true);
		const int rawLoss = std::max(0, 100 - primaryReduction);
		const int reducedRawLoss = kind == nkPlayer
			? static_cast<int>(std::round(rawLoss * DAMAGE_RATE)) : rawLoss;
		ok = check(target->life == 1000 - reducedRawLoss,
			"raw life damage uses the same zero-Effect reduction for player, NPC and partner") && ok;
		block->level = 2;
		block->magic.level[2].effect = 25;
		block->magic.level[2].effectExt = 5;
		target->life = 1000;
		target->hurtLife(100, true);
		const int explicitLoss = kind == nkPlayer
			? static_cast<int>(std::round(70 * DAMAGE_RATE)) : 70;
		ok = check(target->life == 1000 - explicitLoss,
			"explicit numeric reduction keeps its amount and adds only the current level extension") && ok;
		block->vanishing = true;
		target->life = 1000;
		target->hurtLife(100, true);
		ok = check(target->life == 1000 - loss,
			"expired reduction never applies an attack fallback or extension") && ok;
		target->magicToUseWhenBeAttacked.reset();
		target->shieldEffects.clear();
	}
	return ok;
}

bool runSelfMagicLifetimeContract(GameManager& gameManager)
{
	Effect effect;
	effect.level = 1;
	effect.magic.flyImage = std::make_shared<IMPImage>();
	effect.magic.flyImage->frame.resize(3);
	effect.magic.flyImage->directions = 1;
	effect.magic.flyImage->interval = 50;
	effect.magic.explodeImage = std::make_shared<IMPImage>();
	effect.magic.explodeImage->frame.resize(2);
	effect.magic.explodeImage->directions = 1;
	effect.magic.explodeImage->interval = 40;
	const bool originalRageSystem = gameManager.global.feature.rageSystem;
	bool ok = true;
	for (bool rageSystem : { false, true })
	{
		gameManager.global.feature.rageSystem = rageSystem;
		for (int specialKind : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 99 })
		{
			effect.magic.level[1].moveKind = mmkSelf;
			effect.magic.level[1].specialKind = specialKind;
			for (int lifeFrame : { 0, 1, 31, 1000, (std::numeric_limits<int>::max)() })
			{
				effect.magic.level[1].lifeFrame = lifeFrame;
				effect.vanishing = false;
				const UTime expected = lifeFrame == 0 ? 150 : static_cast<UTime>(lifeFrame) * 10;
				ok = check(effect.getExplodinUTime() == expected,
					"all self magic kinds use 10ms LifeFrame units, or one image cycle for zero, without overflow") && ok;
				effect.vanishing = true;
				ok = check(effect.getExplodinUTime() == 80,
					"self magic disappearance uses its own animation, never the active LifeFrame duration") && ok;
			}
		}
	}
	gameManager.global.feature.rageSystem = originalRageSystem;
	effect.vanishing = false;
	effect.magic.level[1].moveKind = mmkFly;
	effect.magic.level[1].lifeFrame = 1000;
	ok = check(effect.getFlyinUTime() == 20000 && effect.getExplodinUTime() == 80,
		"the self magic correction preserves projectile LifeFrame and disappearance timing") && ok;
	effect.magic.level[1].moveKind = mmkTimeStop;
	effect.magic.level[1].specialKind = 0;
	ok = check(effect.getExplodinUTime() == 150,
		"time-stop remains outside the MoveKind 13 lifetime correction") && ok;
	return ok;
}

bool runExplicitAttackDistanceContract()
{
	NPC caster;
	caster.attackRadius = 6;
	caster.attackLevel = 1;
	NPCAttackOption option;
	option.magic = std::make_shared<Magic>();
	option.magic->loadSucceeded = true;
	bool ok = true;
	for (int moveKind : { mmkPoint, mmkSelf, mmkSummon, mmkFullScreen })
	{
		option.moveKind = option.magic->level[1].moveKind = moveKind;
		for (int distance : { 3, 12 })
		{
			option.configuredUseDistance = distance;
			option.hasExplicitUseDistance = true;
			ok = check(caster.calcEffectiveUseDistance(option) == distance,
				"explicit attack distance replaces the NPC default radius, including a larger distance") && ok;
			option.hasExplicitUseDistance = false;
			const int expected = moveKind == mmkPoint ? 6 : std::min(distance, 6);
			ok = check(caster.calcEffectiveUseDistance(option) == expected,
				"inferred attack distance retains the existing NPC-radius cap") && ok;
		}
	}
	option.moveKind = option.magic->level[1].moveKind = mmkFly;
	option.magic->level[1].speed = 1;
	option.magic->level[1].lifeFrame = 1;
	option.configuredUseDistance = 12;
	option.hasExplicitUseDistance = true;
	const int physicalReach = caster.estimatePhysicalReach(*option.magic, 1);
	ok = check(physicalReach > 0 && physicalReach < caster.attackRadius
		&& caster.calcEffectiveUseDistance(option) == physicalReach,
		"an explicit distance still cannot extend a short-lived projectile's physical reach") && ok;
	option.moveKind = option.magic->level[1].moveKind = mmkRegion;
	option.region = mrCross;
	option.shapeRange = 4;
	ok = check(caster.calcEffectiveUseDistance(option) == 4,
		"an explicit distance retains the exact region shape's range restriction") && ok;
	return ok;
}

bool runSelfMagicSelectionContract(GameManager& gameManager)
{
	const auto originalMap = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 32;
	gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
	gameManager.map->createDataMap();
	auto caster = std::make_shared<NPC>();
	caster->kind = nkBattle;
	caster->setPosition({ 10, 10 }, false);
	caster->attackRadius = 12;
	caster->visionRadius = 20;
	caster->attackLevel = 1;
	caster->lifeMax = caster->thewMax = 100;
	caster->life = caster->thew = 25;
	caster->shieldLife = 0;
	caster->frozen = true;
	NPCAttackOption option;
	option.magic = std::make_shared<Magic>();
	option.magic->loadSucceeded = true;
	option.magic->level[1].moveKind = mmkSelf;
	option.moveKind = mmkSelf;
	option.isTargetAttack = false;
	option.hasExplicitUseDistance = true;
	option.configuredUseDistance = 6;
	const Point nearTarget = { 10, 16 };
	const Point farTarget = { 10, 24 };
	bool ok = true;
	for (int specialKind : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 99 })
	{
		option.magic->level[1].specialKind = specialKind;
		caster->attackOptions = { option };
		ok = check(caster->calcEffectiveUseDistance(option) == 6,
			"self magic respects configured combat distance instead of expanding to vision radius") && ok;
		ok = check(caster->canMagicHitTarget(option, caster->getPosition(), nearTarget, 1)
			&& !caster->canMagicHitTarget(option, caster->getPosition(), farTarget, 1),
			"eligible self magic can be chosen against a separate nearby target, but not beyond use distance") && ok;
		const auto candidate = caster->findReadyAttackOption(nearTarget);
		ok = check(candidate.has_value() && candidate->magic == option.magic,
			"every eligible self magic special kind participates in the real ready-option selector") && ok;
	}
	for (int specialKind : { mskAddLife, mskAddThew, mskAddShield, mskClearAbnormalState })
	{
		option.magic->level[1].specialKind = specialKind;
		caster->attackOptions = { option };
		caster->life = caster->thew = 100;
		caster->shieldLife = 100;
		caster->frozen = false;
		ok = check(!caster->findReadyAttackOption(nearTarget).has_value(),
			"combat selection preserves existing healing, stamina, shield and cleanse need checks") && ok;
	}
	SkillScore previousAttack;
	SkillScore anotherAttack;
	previousAttack.canHitNow = anotherAttack.canHitNow = true;
	previousAttack.moveCost = anotherAttack.moveCost = 0;
	previousAttack.isInertia = true;
	ok = check(!previousAttack.isBetterThan(anotherAttack) && !anotherAttack.isBetterThan(previousAttack),
		"equally ready attacks remain tied instead of permanently preferring the last released magic") && ok;
	previousAttack.canHitNow = anotherAttack.canHitNow = false;
	ok = check(previousAttack.isBetterThan(anotherAttack),
		"approach planning retains its existing inertia tie-breaker") && ok;
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

bool runPositionCastRangeContract(GameManager& gameManager)
{
	const auto originalMap = gameManager.map->data;
	const auto originalPosition = gameManager.player->getPosition();
	const bool originalNativeAttack = gameManager.global.feature.nativeNpcAttackAtAnimationEnd;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 64;
	gameManager.map->data->tile.assign(64, std::vector<MapTile>(64));
	gameManager.map->createDataMap();
	gameManager.player->setPosition({ 10, 10 }, false);
	NPC caster;
	caster.attackRadius = 40;
	caster.attackLevel = 1;
	NPCAttackOption option;
	option.magic = std::make_shared<Magic>();
	option.magic->loadSucceeded = true;
	option.configuredUseDistance = 40;
	bool ok = true;
	for (int moveKind : { mmkPoint, mmkLine, mmkRegion, mmkWarningRegion, mmkSummon, mmkTransport, mmkControl })
	{
		option.moveKind = option.magic->level[1].moveKind = moveKind;
		option.region = option.magic->level[1].region = mrRegionFile;
		for (bool nativeAttack : { false, true })
		{
			gameManager.global.feature.nativeNpcAttackAtAnimationEnd = nativeAttack;
			for (bool explicitDistance : { false, true })
			{
				option.hasExplicitUseDistance = explicitDistance;
				ok = check(caster.calcEffectiveUseDistance(option) == MAGIC_MAX_CAST_DISTANCE
					&& caster.canMagicHitTarget(option, { 10, 10 }, { 30, 10 }, 1)
					&& !caster.canMagicHitTarget(option, { 10, 10 }, { 31, 10 }, 1),
					"position casts cap both native and planned NPC attacks at the dispatcher's aim limit") && ok;
			}
		}
	}
	option.magic->level[1].moveKind = option.moveKind = mmkFly;
	option.magic->level[1].speed = 10;
	option.magic->level[1].lifeFrame = 1000;
	option.hasExplicitUseDistance = true;
	ok = check(!option.magic->hasPositionCastLimit(1) && caster.calcEffectiveUseDistance(option) == 40,
		"ordinary projectiles retain their configured and physical reach beyond the aim-position limit") && ok;
	option.magic->regionFileLoaded = true;
	option.magic->regionFile.resize(8);
	option.magic->regionFile[0].push_back({ { 0.0f, 0.0f }, 0 });
	for (int moveKind : { mmkPoint, mmkLine, mmkWarningRegion })
	{
		option.magic->level[1].moveKind = moveKind;
		for (int distance : { 19, 20, 21, 40 })
		{
			const auto effects = Magic::addEffect(option.magic, gameManager.player,
				{ 10, 10 }, { 10 + distance, 10 }, 1, 0, 0, lkSelf, nullptr);
			const bool line = moveKind == mmkLine;
			ok = check(effects.size() == (line ? 3u : 1u)
				&& effects[line ? 1 : 0]->position == Point{ 10 + std::min(distance, MAGIC_MAX_CAST_DISTANCE), 10 },
				"real fixed, fixed-line and region-file effects clamp their center and preserve their shape") && ok;
			gameManager.effectManager->clearEffect();
		}
	}
	auto target = std::make_shared<NPC>();
	target->npcName = "POSITION_CAST_CONTROL_TARGET";
	target->kind = nkBattle;
	target->life = target->lifeMax = 100;
	target->level = 1;
	target->setPosition({ 31, 10 }, false);
	gameManager.npcManager->addNPC(target);
	option.magic->level[1].moveKind = mmkControl;
	option.magic->maxLevel = 100;
	ok = check(Magic::addEffect(option.magic, gameManager.player, { 10, 10 }, { 31, 10 },
		1, 0, 0, lkSelf, target).empty() && !gameManager.player->isControllingCharacter(),
		"control cannot bypass the clamped aim by retaining a distant target pointer") && ok;
	target->setPosition({ 30, 10 }, false);
	ok = check(Magic::addEffect(option.magic, gameManager.player, { 10, 10 }, { 30, 10 },
		1, 0, 0, lkSelf, target).size() == 1 && gameManager.player->isControllingCharacter(),
		"control remains available at the inclusive aim boundary") && ok;
	gameManager.player->endControlCharacter();
	gameManager.effectManager->clearEffect();
	const auto originalBodies = gameManager.objectManager->objectList;
	auto distantBody = std::make_shared<Object>();
	distantBody->kind = okBody;
	distantBody->position = { 50, 10 };
	gameManager.objectManager->objectList = { distantBody };
	target->setPosition({ 50, 10 }, false);
	option.magic->level[1].moveKind = mmkPoint;
	option.magic->bodyRadius = 1;
	ok = check(Magic::addEffect(option.magic, gameManager.player, { 10, 10 }, { 50, 10 },
		1, 0, 0, lkSelf, target).empty() && gameManager.objectManager->objectList.size() == 1,
		"body magic cannot consume a distant corpse through the original target pointer") && ok;
	gameManager.objectManager->objectList = originalBodies;
	gameManager.npcManager->deleteNPC(target->npcName);
	gameManager.player->setPosition(originalPosition, false);
	gameManager.global.feature.nativeNpcAttackAtAnimationEnd = originalNativeAttack;
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

bool runMagicAdmissionContract(GameManager& gameManager)
{
	auto magic = std::make_shared<Magic>();
	magic->loadSucceeded = true;
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskClearAbnormalState;
	magic->level[1].lifeFrame = 100;
	magic->disableUse = 1;
	auto npc = std::make_shared<NPC>();
	npc->npcName = "ADMISSION_TEST_NPC";
	npc->kind = nkBattle;
	npc->lifeMax = 100;
	npc->life = 50;
	gameManager.npcManager->addNPC(npc);
	bool ok = check(npc->canUseMagicByState(magic, false),
		"DisableUse limits player selection, not the NPC combat admission gate");
	gameManager.effectManager->freeResource();
	npc->useMagic(magic, npc->getPosition(), 1, npc);
	ok = check(gameManager.effectManager->effectList.size() == 1,
		"NPC direct magic remains available when DisableUse is set") && ok;
	gameManager.effectManager->freeResource();
	magic->lifeFullToUse = 1;
	ok = check(!npc->canUseMagicByState(magic, false),
		"LifeFullToUse still rejects normal combat admission below maximum life") && ok;
	npc->useMagic(magic, npc->getPosition(), 1, npc);
	ok = check(gameManager.effectManager->effectList.size() == 1,
		"the direct NPC magic dispatch does not add admission checks absent from published UseMagicNpc") && ok;
	gameManager.effectManager->freeResource();
	npc->life = 100;
	ok = check(npc->canUseMagicByState(magic, false), "full life admits normal NPC casting") && ok;
	npc->setPreparedAttackMagic(magic, false);
	npc->life = 90;
	ok = check(npc->releasePreparedAttackMagic(npc->getPosition(), npc)
		&& gameManager.effectManager->effectList.size() == 1,
		"an admitted NPC attack still releases if life changes before the animation release point") && ok;
	gameManager.effectManager->freeResource();

	auto player = gameManager.player;
	const auto originalInfo = player->info;
	const int originalLife = player->life;
	const int originalMana = player->mana;
	const int originalThew = player->thew;
	const bool originalCanUseMana = player->canUseMana;
	const auto originalMessageBox = gameManager.menu->messageBox;
	gameManager.menu->messageBox = std::make_shared<MsgBox>();
	player->info.lifeMax = player->info.manaMax = player->info.thewMax = 100;
	player->life = player->mana = player->thew = 100;
	player->canUseMana = true;
	MagicInfo info;
	info.magic = magic;
	info.level = 1;
	for (int disabled : { 1, -1 })
	{
		magic->disableUse = disabled;
		player->beginMagic(info, player->getPosition(), player);
		ok = check(gameManager.effectManager->effectList.size() == 1,
			"explicit player casting does not inherit the manual selection DisableUse restriction") && ok;
		gameManager.effectManager->freeResource();
	}
	magic->disableUse = 0;
	magic->level[1].lifeCost = 10;
	magic->level[1].manaCost = 7;
	player->beginMagic(info, player->getPosition(), player);
	ok = check(player->life == 90 && player->mana == 93 && gameManager.effectManager->effectList.size() == 1,
		"an admitted full-life self cure dispatches after paying LifeCost instead of charging for no effect") && ok;
	gameManager.effectManager->freeResource();
	player->beginMagic(info, player->getPosition(), player);
	ok = check(player->life == 90 && player->mana == 93 && gameManager.effectManager->effectList.empty(),
		"a new player cast below full life is rejected before paying any costs") && ok;
	player->life = 80;
	ok = check(player->tryConsumeMagicCost(magic, 1, false) && player->life == 70 && player->mana == 86,
		"release-time cost payment does not repeat the admission-only full-life condition") && ok;
	player->info = originalInfo;
	player->life = originalLife;
	player->mana = originalMana;
	player->thew = originalThew;
	player->canUseMana = originalCanUseMana;
	gameManager.menu->messageBox = originalMessageBox;
	gameManager.npcManager->deleteNPC("ADMISSION_TEST_NPC");
	return ok;
}

bool runAutomaticSelfMagicAdmissionContract(GameManager& gameManager)
{
	const auto originalMap = gameManager.map->data;
	const auto originalPlayerPosition = gameManager.player->getPosition();
	const int originalPlayerLife = gameManager.player->life;
	const bool originalNativeAttack = gameManager.global.feature.nativeNpcAttackAtAnimationEnd;
	gameManager.global.feature.nativeNpcAttackAtAnimationEnd = false;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 32;
	gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
	gameManager.player->life = 100;
	gameManager.player->setPosition({ 13, 10 }, false);
	bool ok = true;
	for (int lifeFullToUse : { 1, 0 })
	{
		for (int life : { 50, 100 })
		{
			auto npc = std::make_shared<NPC>();
			npc->npcName = "AUTO_SELF_MAGIC_ADMISSION_TEST_NPC";
			npc->kind = nkBattle;
			npc->relation = nrHostile;
			npc->lifeMax = npc->thewMax = 100;
			npc->life = life;
			npc->thew = 50;
			npc->shieldLife = 20;
			npc->attackRadius = npc->visionRadius = 8;
			npc->attackLevel = 1;
			npc->setPosition({ 10, 10 }, false);
			NPCAttackOption option;
			option.magic = std::make_shared<Magic>();
			option.magic->loadSucceeded = true;
			option.magic->lifeFullToUse = lifeFullToUse;
			option.magic->level[1].moveKind = option.moveKind = mmkSelf;
			option.magic->level[1].specialKind = mskAddThew;
			option.magic->level[1].effect = 20;
			option.magic->level[1].lifeFrame = 100;
			option.isTargetAttack = false;
			option.hasExplicitUseDistance = true;
			option.configuredUseDistance = 1;
			npc->attackOptions.push_back(option);
			// An already active, longer-range shield keeps combat in the self-buff fallback.
			option.magic = std::make_shared<Magic>(*option.magic);
			option.magic->level[1].specialKind = mskAddShield;
			option.configuredUseDistance = 6;
			npc->attackOptions.push_back(option);
			gameManager.npcManager->addNPC(npc);
			gameManager.map->createDataMap();
			npc->idledFrame = npc->idle;
			const bool shouldCast = lifeFullToUse == 0 || life == npc->lifeMax;
			ok = check(!npc->canAnyAttackOptionHitTarget(gameManager.player->getPosition())
				&& gameManager.npcManager->scheduleBattleAction(npc)
				&& npc->thew == (shouldCast ? 70 : 50)
				&& gameManager.effectManager->effectList.size() == (shouldCast ? 1u : 0u),
				"automatic NPC self buffs enforce LifeFullToUse before dispatch, while full life or no restriction permits casting") && ok;
			gameManager.effectManager->freeResource();
			gameManager.npcManager->deleteNPC(npc->npcName);
		}
	}
	gameManager.player->setPosition(originalPlayerPosition, false);
	gameManager.player->life = originalPlayerLife;
	gameManager.global.feature.nativeNpcAttackAtAnimationEnd = originalNativeAttack;
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

bool runInvisibleRangeContract(GameManager& gameManager)
{
	class RangeTarget : public NPC
	{
	public:
		int hitCount = 0;
		void hurt(std::shared_ptr<Effect> effect, int = -1) override
		{
			++hitCount;
			NPC::hurt(effect, 0);
		}
	};
	auto target = std::make_shared<RangeTarget>();
	target->npcName = "INVISIBLE_RANGE_ENEMY";
	target->kind = nkBattle;
	target->relation = nrHostile;
	target->life = target->lifeMax = 1000;
	target->invisibleMilliseconds = 10000;
	target->setPosition(gameManager.player->getPosition(), false);
	auto ally = std::make_shared<NPC>();
	ally->npcName = "INVISIBLE_RANGE_ALLY";
	ally->kind = nkBattle;
	ally->relation = nrFriendly;
	ally->life = 50;
	ally->lifeMax = 100;
	ally->invisibleMilliseconds = 10000;
	ally->setPosition(target->getPosition(), false);
	gameManager.npcManager->addNPC(target);
	gameManager.npcManager->addNPC(ally);
	auto effect = std::make_shared<Effect>();
	effect->level = 1;
	effect->user = gameManager.player;
	effect->launcherKind = lkSelf;
	effect->position = target->getPosition();
	effect->magic.rangeEffect = 1;
	effect->magic.rangeRadius = 4;
	effect->magic.level[1].rangeDamage = 20;
	effect->magic.level[1].rangeAddLife = 10;
	RageSystemTestAccess::updateRangeEffect(*effect, 1);
	bool ok = check(target->hitCount == 1 && ally->life == 60,
		"range damage and allied recovery affect magic-invisible characters");
	target->scriptHidden = ally->scriptHidden = true;
	RageSystemTestAccess::updateRangeEffect(*effect, 1);
	ok = check(target->hitCount == 1 && ally->life == 60,
		"range effects still exclude characters hidden by a script") && ok;
	gameManager.npcManager->deleteNPC(target->npcName);
	gameManager.npcManager->deleteNPC(ally->npcName);
	return ok;
}

bool runCarryFlyMagicDamageContract(GameManager& gameManager)
{
	class CollisionTargetNPC : public NPC
	{
	public:
		void hurt(std::shared_ptr<Effect> effect, int = -1) override
		{
			NPC::hurt(effect, 0);
		}
	};
	const auto originalMap = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 32;
	gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
	gameManager.map->createDataMap();
	bool ok = true;
	for (Point targetPosition : { Point{ 9, 10 }, Point{ 10, 10 }, Point{ 8, 8 }, Point{ 12, 27 } })
	{
		gameManager.effectManager->clearEffect();
		const bool corridor = targetPosition == Point{ 12, 27 };
		gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
		if (corridor) gameManager.map->data->tile[28][13].obstacle = toObstacle;
		const Point origin = corridor ? Point{ 12, 29 } : Point{ 8, 10 };
		gameManager.player->setPosition(origin, false);
		gameManager.player->life = gameManager.player->lifeMax = 1000;
		auto target = std::make_shared<CollisionTargetNPC>();
		target->npcName = "CARRY_FLY_MAGIC_TARGET";
		target->kind = nkBattle;
		target->relation = nrHostile;
		target->life = target->lifeMax = 1000;
		target->defend = target->defend2 = target->defend3 = 0;
		target->setPosition(targetPosition, false);
		gameManager.npcManager->addNPC(target);
		gameManager.map->createDataMap();
		auto child = std::make_shared<Magic>();
		child->loadSucceeded = true;
		child->iniName = "carry_fly_child.ini";
		child->passThrough = 1;
		child->bounce = 30;
		child->noSpecialKindEffect = 1;
		child->level[1].moveKind = mmkCircle;
		child->level[1].speed = 7;
		child->level[1].lifeFrame = 5;
		child->level[1].effectExt = 340;
		auto parent = std::make_shared<Magic>();
		parent->loadSucceeded = true;
		parent->iniName = "carry_fly_parent.ini";
		parent->carryUser = 3;
		parent->bounce = 500;
		parent->level[1].moveKind = mmkFly;
		parent->level[1].speed = 8;
		parent->level[1].waitFrame = 4;
		parent->level[1].lifeFrame = 10;
		parent->linkedLevel[1].flyMagic = child;
		parent->linkedLevel[1].flyInterval = 100;
		const auto effects = Magic::addEffect(parent, gameManager.player, origin, targetPosition,
			1, 100, 1000, lkSelf, target);
		bool childSpawned = false;
		for (UTime elapsed = 0; elapsed < 600; elapsed += 20)
		{
			const auto active = gameManager.effectManager->effectList;
			for (const auto& effect : active)
			{
				childSpawned = childSpawned || effect->magic.iniName == child->iniName;
				RageSystemTestAccess::advanceEffect(*effect, 20);
			}
			gameManager.effectManager->onUpdate();
		}
		if (!childSpawned || target->life == 1000)
		{
			std::cerr << "CarryFly target=" << targetPosition.x << ',' << targetPosition.y
				<< " spawned=" << childSpawned << " life=" << target->life << '\n';
		}
		ok = check(effects.size() == 1 && childSpawned && target->life < 1000,
			"CarryUser3 dispatches short-lived invisible circle children that damage nearby targets") && ok;
		gameManager.effectManager->clearEffect();
		gameManager.npcManager->deleteNPC(target->npcName);
	}
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

bool runCarriedVictimTranslationContract(GameManager& gameManager)
{
	const auto originalMap = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 32;
	bool ok = true;
	for (bool crossedWall : { false, true })
	{
		for (int row : { 10, 11 })
		{
			gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
			if (crossedWall) gameManager.map->data->tile[row + 1][11].obstacle = toObstacle;
			auto caster = std::make_shared<NPC>();
			caster->npcName = "CARRIED_TRANSLATION_CASTER";
			caster->kind = nkBattle;
			caster->relation = nrFriendly;
			caster->life = caster->lifeMax = 100;
			caster->setPosition({ 10, row }, false);
			gameManager.npcManager->addNPC(caster);
			auto victim = std::make_shared<NPC>();
			victim->npcName = "CARRIED_TRANSLATION_VICTIM";
			victim->kind = nkBattle;
			victim->relation = nrHostile;
			victim->life = victim->lifeMax = 100;
			victim->setPosition({ 10, row + 1 }, false);
			victim->setOffset({ 5, -2 });
			gameManager.npcManager->addNPC(victim);
			gameManager.map->createDataMap();
			auto effect = std::make_shared<Effect>();
			effect->level = 1;
			effect->magic.carryUser = 4;
			effect->magic.level[1].moveKind = mmkFly;
			effect->user = caster;
			effect->position = effect->src = { 10, row };
			effect->offset = { -4, 1 };
			effect->doing = ekFlying;
			gameManager.effectManager->addEffect(effect);
			effect->attachCarryUser(caster);
			effect->handleCarryUser4AfterHit(victim);
			auto relativePixels = [&]()
			{
				const auto tiles = Map::getTilePositionEx(victim->getPosition(), effect->position, { 0, 0 }, { 0, 0 });
				const auto offset = victim->getOffset();
				return PointEx{ tiles.x + offset.x - effect->offset.x, tiles.y + offset.y - effect->offset.y };
			};
			const auto before = relativePixels();
			effect->position = crossedWall ? Point{ 13, row } : Point{ 10, row + 1 };
			RageSystemTestAccess::updateCarryUserPosition(*effect);
			RageSystemTestAccess::updateAttachedNPCs(*effect);
			const auto after = relativePixels();
			ok = check(crossedWall
				? victim->getPosition() == Point{ 10, row + 1 } && effect->doing != ekFlying
				: std::abs(before.x - after.x) < 0.01f && std::abs(before.y - after.y) < 0.01f,
				crossedWall ? "carried victim cannot skip a wall into an open destination in one update"
				: "carried victim preserves its pixel displacement across both row parities") && ok;
			gameManager.effectManager->freeResource();
			gameManager.npcManager->deleteNPC(caster->npcName);
			gameManager.npcManager->deleteNPC(victim->npcName);
		}
	}
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

bool runCarryWallCollisionContract(GameManager& gameManager)
{
	class CollisionTargetNPC : public NPC
	{
	public:
		void hurt(std::shared_ptr<Effect> effect, int = -1) override
		{
			// This contract checks collision reach; damage hit rates are tested separately.
			NPC::hurt(effect, 0);
		}
	};
	enum class Scenario { OpenFloor, SolidWall, PassThroughWall, SpawnInsideWall, HiddenAtWall, Throw, Attached, AttachedOpen,
		TransparentWall, JumpTransparentWall, SpawnInsideTransparentWall, BoxWall };
	const auto originalMap = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 32;
	bool ok = true;
	for (int carryUser : { 1, 2, 3, 4 })
	{
		for (UTime frameTime : { 20u, 200u })
		{
			for (Scenario scenario : { Scenario::OpenFloor, Scenario::SolidWall, Scenario::PassThroughWall,
				Scenario::SpawnInsideWall, Scenario::HiddenAtWall, Scenario::Throw, Scenario::Attached, Scenario::AttachedOpen,
				Scenario::TransparentWall, Scenario::JumpTransparentWall, Scenario::SpawnInsideTransparentWall, Scenario::BoxWall })
			{
				const bool attached = scenario == Scenario::Attached || scenario == Scenario::AttachedOpen;
				const bool characterOnlyWall = scenario == Scenario::TransparentWall || scenario == Scenario::JumpTransparentWall
					|| scenario == Scenario::SpawnInsideTransparentWall || scenario == Scenario::BoxWall;
				if (attached && carryUser != 4) continue;
				gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
				if (scenario != Scenario::OpenFloor && scenario != Scenario::AttachedOpen && scenario != Scenario::BoxWall)
				{
					for (auto& row : gameManager.map->data->tile)
					{
						row[10].obstacle = scenario == Scenario::JumpTransparentWall ? toJumpTrans
							: characterOnlyWall ? toTrans : toObstacle;
					}
				}
				auto caster = std::make_shared<NPC>();
				caster->npcName = "CARRY_WALL_CASTER";
				caster->kind = nkBattle;
				caster->relation = nrFriendly;
				caster->life = caster->lifeMax = 100;
				caster->setPosition({ scenario == Scenario::SpawnInsideWall || scenario == Scenario::SpawnInsideTransparentWall ? 9 : 8, 10 }, false);
				gameManager.npcManager->addNPC(caster);
				auto target = std::make_shared<CollisionTargetNPC>();
				target->npcName = "CARRY_WALL_TARGET";
				target->kind = nkBattle;
				target->relation = nrHostile;
				target->life = target->lifeMax = 100;
				target->setPosition({ attached ? 8 : 12, 10 }, false);
				gameManager.npcManager->addNPC(target);
				gameManager.map->createDataMap();
				if (scenario == Scenario::BoxWall)
				{
					auto box = std::make_shared<Object>();
					box->kind = okBox;
					for (auto& row : gameManager.map->dataMap.tile) row[10].objList.push_back(box);
				}
				auto magic = std::make_shared<Magic>();
				magic->loadSucceeded = true;
				magic->carryUser = carryUser;
				magic->passThroughWall = scenario == Scenario::PassThroughWall ? 1 : 0;
				magic->hideUserWhenCarry = scenario == Scenario::HiddenAtWall ? 1 : 0;
				magic->level[1].moveKind = scenario == Scenario::Throw ? mmkThrow : mmkFly;
				magic->level[1].speed = 8;
				magic->level[1].lifeFrame = 100;
				if (scenario == Scenario::HiddenAtWall)
				{
					magic->explodeImage = std::make_shared<IMPImage>();
					magic->explodeImage->frame.resize(2);
					magic->explodeImage->directions = 1;
					magic->explodeImage->interval = 20;
				}
				auto effects = Magic::addEffect(magic, caster, caster->getPosition(), { 14, 10 },
					1, 20, 1000, lkFriend, nullptr);
				const bool shouldStopAtWall = scenario == Scenario::SolidWall || scenario == Scenario::SpawnInsideWall
					|| scenario == Scenario::HiddenAtWall || scenario == Scenario::Attached;
				const int targetLifeAfterCast = target->life;
				if (attached)
				{
					ok = check(effects.size() == 1 && effects[0]->hasAttachedNPC(target),
						"CarryUser4 starts with the real adjacent target attached") && ok;
				}
				bool stayedBeforeWall = caster->getPosition().x < 10;
				bool movedPastWall = false;
				bool attachedStayedBeforeWall = true;
				bool attachedMovedPastWall = false;
				if (scenario == Scenario::Throw && effects.size() == 1)
				{
					// Throw ignores crossed tiles, but its current tile still collides with walls.
					effects[0]->doing = ekThrowing;
					effects[0]->position = { 12, 10 };
					effects[0]->passPath = { { 9, 10 }, { 10, 10 }, { 11, 10 }, { 12, 10 } };
					RageSystemTestAccess::updateCarryUserPosition(*effects[0]);
					movedPastWall = caster->getPosition() == Point{ 12, 10 };
					effects[0]->position = { 10, 10 };
					RageSystemTestAccess::updateCarryUserPosition(*effects[0]);
					gameManager.effectManager->onUpdate();
					movedPastWall = movedPastWall && caster->getPosition() == Point{ 12, 10 };
				}
				for (UTime elapsed = 0; scenario != Scenario::Throw && elapsed < 600
					&& !gameManager.effectManager->effectList.empty(); elapsed += frameTime)
				{
					const auto activeEffects = gameManager.effectManager->effectList;
					for (const auto& effect : activeEffects) RageSystemTestAccess::advanceEffect(*effect, frameTime);
					stayedBeforeWall = stayedBeforeWall && caster->getPosition().x < 10;
					movedPastWall = movedPastWall || caster->getPosition().x > 10;
					attachedStayedBeforeWall = attachedStayedBeforeWall && target->getPosition().x < 10;
					attachedMovedPastWall = attachedMovedPastWall || target->getPosition().x > 10;
					gameManager.effectManager->onUpdate();
					stayedBeforeWall = stayedBeforeWall && caster->getPosition().x < 10;
				}
				const bool characterWallPassed = stayedBeforeWall && (carryUser == 1
					? target->life < targetLifeAfterCast
					: target->life == targetLifeAfterCast && gameManager.effectManager->effectList.empty());
				const bool passed = check(effects.size() == 1 && (characterOnlyWall ? characterWallPassed : shouldStopAtWall
					? stayedBeforeWall && target->life == targetLifeAfterCast
						&& (attached ? attachedStayedBeforeWall : !effects[0]->hasAttachedNPC(target))
						&& gameManager.effectManager->effectList.empty()
					: movedPastWall && (!attached || attachedMovedPastWall)),
					"CarryUser modes stop before solid walls before and after collision, preserve targets behind them, and retain open or permitted wall traversal");
				if (!passed)
				{
					std::cerr << "carryUser=" << carryUser << " frameTime=" << frameTime << " scenario=" << static_cast<int>(scenario)
						<< " caster=" << caster->getPosition().x << "," << caster->getPosition().y << " targetLife=" << target->life << '\n';
				}
				ok = passed && ok;
				gameManager.effectManager->freeResource();
				gameManager.npcManager->deleteNPC(caster->npcName);
				gameManager.npcManager->deleteNPC(target->npcName);
			}
		}
	}
	for (const auto obstacle : {toTrans, toJumpTrans})
	{
		gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
		gameManager.map->data->tile[11][11].obstacle = obstacle;
		auto caster = std::make_shared<NPC>();
		caster->npcName = "CARRY_SIDE_CASTER";
		caster->kind = nkBattle;
		caster->relation = nrFriendly;
		caster->life = caster->lifeMax = 100;
		caster->setPosition({10, 10}, false);
		gameManager.npcManager->addNPC(caster);
		auto target = std::make_shared<NPC>();
		target->npcName = "CARRY_SIDE_TARGET";
		target->kind = nkBattle;
		target->relation = nrHostile;
		target->life = target->lifeMax = 100;
		target->setPosition({10, 11}, false);
		gameManager.npcManager->addNPC(target);
		gameManager.map->createDataMap();
		auto effect = std::make_shared<Effect>();
		effect->magic.carryUser = 4;
		effect->magic.level[1].moveKind = mmkFly;
		effect->user = caster;
		effect->position = effect->src = {10, 10};
		effect->doing = ekFlying;
		gameManager.effectManager->addEffect(effect);
		effect->attachCarryUser(caster);
		if (!effect->hasAttachedNPC(target))
		{
			effect->handleCarryUser4AfterHit(target);
		}
		ok = check(effect->hasAttachedNPC(target), "offset CarryUser4 victim attaches") && ok;
		effect->position = {11, 10};
		RageSystemTestAccess::updateCarryUserPosition(*effect);
		RageSystemTestAccess::updateAttachedNPCs(*effect);
		ok = check(caster->getPosition() == Point{11, 10}
			&& target->getPosition() == Point{10, 11} && effect->doing != ekFlying,
			"CarryUser4 stops when an offset victim reaches water while the caster stays on open ground") && ok;
		gameManager.effectManager->freeResource();
		gameManager.npcManager->deleteNPC(caster->npcName);
		gameManager.npcManager->deleteNPC(target->npcName);
	}
	gameManager.map->data = originalMap;
	gameManager.map->createDataMap();
	return ok;
}

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

bool runMagicLevelRuntimeTest(const std::filesystem::path& root)
{
	// 剑之缘 2 高清 1.10 分身术的 Init、Level1、Level2、Level10 数值。
	const std::string summonDefinition =
		"[Init]\nName=LevelSummon\nMoveKind=22\nNpcFile=level-summon-1.ini\n"
		"KeepMilliseconds=40000\nMaxCount=5\n"
		"[Level1]\nNpcFile=level-summon-1.ini\nKeepMilliseconds=40000\nMaxCount=1\n"
		"[Level2]\nNpcFile=level-summon-2.ini\nKeepMilliseconds=50000\nMaxCount=2\n"
		"[Level10]\nNpcFile=level-summon-10.ini\nKeepMilliseconds=260000\nMaxCount=10\n";
	const std::string stateDefinition =
		"[Init]\nName=LevelStates\nMoveKind=1\nMaxLevel=5\n"
		"NpcIni=init-morph.ini\nReplaceMagic=init-replacement.ini\n"
		"SpecialKind9ReplaceFlyIni=init-fly.ini\nSpecialKind9ReplaceFlyIni2=init-fly2.ini\n"
		"DisableMoveMilliseconds=100\nDisableSkillMilliseconds=200\nBlindMilliseconds=300\n"
		"WeakMilliseconds=400\nWeakAttackPercent=10\nWeakDefendPercent=20\n"
		"MorphMilliseconds=0\nChangeToFriendMilliseconds=500\n"
		"AttackAddPercent=10\nDefendAddPercent=20\nEvadeAddPercent=30\nSpeedAddPercent=40\n"
		"[Level2]\nMoveKind=21\nMaxLevel=50\n"
		"NpcIni=\nReplaceMagic=\nSpecialKind9ReplaceFlyIni=level-fly.ini\nSpecialKind9ReplaceFlyIni2=\n"
		"DisableMoveMilliseconds=5500\nDisableSkillMilliseconds=5600\nBlindMilliseconds=5700\n"
		"WeakMilliseconds=6000\nWeakAttackPercent=30\nWeakDefendPercent=40\n"
		"MorphMilliseconds=7000\nChangeToFriendMilliseconds=8000\n"
		"AttackAddPercent=100\nDefendAddPercent=110\nEvadeAddPercent=120\nSpeedAddPercent=130\n"
		"[Level3]\nEffect=111\n"
		"[Level4]\nKeepMilliseconds=-1\nNpcFile=\nMaxCount=-1\n"
		"DisableMoveMilliseconds=0\nDisableSkillMilliseconds=0\nBlindMilliseconds=0\n"
		"WeakMilliseconds=0\nWeakAttackPercent=105\nWeakDefendPercent=-2\n"
		"MorphMilliseconds=-1\nChangeToFriendMilliseconds=0\n";
	if (!check(writeTextFile(root / "ini/magic/level-summon.ini", summonDefinition)
		&& writeTextFile(root / "ini/magic/level-states.ini", stateDefinition)
		&& writeTextFile(root / "ini/magic/level-trail.ini",
			"[Init]\nName=LevelTrail\nMoveKind=19\nKeepMilliseconds=0\n[Level2]\nKeepMilliseconds=9000\n"),
		"write level runtime definitions")) return false;
	for (int level : { 1, 2, 10 })
	{
		if (!check(writeTextFile(root / "ini/npc" / ("level-summon-" + std::to_string(level) + ".ini"),
			"[Init]\nName=LevelSummonNpc\nKind=3\nRelation=0\nLife=" + std::to_string(level * 100)
			+ "\nLifeMax=" + std::to_string(level * 100) + "\n"), "write distinct summon NPC levels")) return false;
	}
	GameManager game;
	game.map->data = std::make_shared<MapData>();
	game.map->data->head.width = game.map->data->head.height = 40;
	game.map->data->tile.assign(40, std::vector<MapTile>(40));
	game.map->createDataMap();
	game.player->setPosition({ 20, 20 }, false);
	auto summon = std::make_shared<Magic>();
	summon->initFromIni("level-summon.ini");
	bool ok = check(summon->loadSucceeded, "load summon level definition");
	std::shared_ptr<Effect> levelTwoSnapshot;
	for (int level : { 1, 2, 10 })
	{
		game.effectManager->clearEffect();
		game.npcManager->npcList.clear();
		const UTime duration = level == 10 ? 260000 : level == 2 ? 50000 : 40000;
		for (int count = 0; count <= level; ++count)
		{
			auto effects = Magic::addEffect(summon, game.player, { 20, 20 }, { 22, 20 }, level, 0, 0, lkSelf, nullptr);
			if (!check(effects.size() == 1, "a summon level creates its effect")) return false;
			auto npc = effects.front()->summonedNPC.lock();
			ok = check(npc != nullptr && npc->lifeMax == level * 100
				&& effects.front()->lifeTime == duration && effects.front()->magic.maxCount == level,
				"summon level selects its NPC file, duration and cap instead of Init values") && ok;
			if (level == 2) levelTwoSnapshot = effects.front();
		}
		ok = check(game.player->summonedNpcsCount(*summon) == level,
			"summoning again replaces the oldest NPC at the current level cap") && ok;
	}
	ok = check(summon->npcFile == "level-summon-1.ini" && summon->keepMilliseconds == 40000
		&& summon->maxCount == 5 && levelTwoSnapshot != nullptr
		&& levelTwoSnapshot->magic.npcFile == "level-summon-2.ini"
		&& levelTwoSnapshot->magic.keepMilliseconds == 50000,
		"level casts preserve both the shared Init definition and an earlier level's effect snapshot") && ok;
	game.effectManager->clearEffect();
	game.npcManager->npcList.clear();
	auto timedSummon = Magic::addEffect(summon, game.player, { 20, 20 }, { 22, 20 }, 2, 0, 0, lkSelf, nullptr);
	if (!check(timedSummon.size() == 1, "create a level-two summon for expiry verification")) return false;
	auto timedNpc = timedSummon.front()->summonedNPC.lock();
	RageSystemTestAccess::advanceEffect(*timedSummon.front(), 40001);
	ok = check(timedNpc != nullptr && timedNpc->life > 0,
		"a level-two summon survives the shorter Init duration") && ok;
	RageSystemTestAccess::advanceEffect(*timedSummon.front(), 10000);
	ok = check(timedNpc != nullptr && timedNpc->life == 0,
		"a level-two summon expires at its own configured duration") && ok;
	game.effectManager->clearEffect();
	game.npcManager->npcList.clear();
	auto states = std::make_shared<Magic>();
	states->initFromIni("level-states.ini");
	auto target = std::make_shared<NPC>();
	target->kind = nkBattle;
	target->relation = nrHostile;
	target->level = 40;
	target->setPosition({ 22, 20 }, false);
	game.npcManager->npcList = { target };
	auto controlled = Magic::addEffect(states, game.player, { 20, 20 }, { 22, 20 }, 2, 0, 0, lkSelf, target);
	ok = check(controlled.size() == 1 && game.player->isControllingCharacter(),
		"control admission uses the current spell level's target MaxLevel") && ok;
	game.effectManager->clearEffect();
	Effect stateEffect;
	stateEffect.level = 2;
	stateEffect.user = game.player;
	stateEffect.initFromMagic(states);
	target->applyEffectRuntimeStates(stateEffect);
	ok = check(target->disableMoveMilliseconds == 5500 && target->disableSkillMilliseconds == 5600
		&& target->blindMilliseconds == 5700 && target->weakMilliseconds == 6000
		&& target->morphMilliseconds == 7000 && target->changeToOppositeMilliseconds == 8000
		&& target->relation == nrFriendly && target->weakMagic->weakAttackPercent == 30
		&& target->weakMagic->weakDefendPercent == 40 && target->morphMagic->attackAddPercent == 100
		&& target->morphMagic->defendAddPercent == 110 && target->morphMagic->evadeAddPercent == 120
		&& target->morphMagic->speedAddPercent == 130 && target->morphMagic->npcIni.empty()
		&& target->morphMagic->replaceMagic.empty() && stateEffect.magic.specialKind9ReplaceFlyIni == "level-fly.ini"
		&& stateEffect.magic.specialKind9ReplaceFlyIni2.empty(),
		"the current level reaches action locks, blind, weak, morph attributes and temporary relation") && ok;
	INIReader saved;
	stateEffect.saveToIni(&saved, "Effect1");
	Effect restored;
	restored.initFromIni(&saved, "Effect1");
	ok = check(restored.level == 2 && restored.magic.disableSkillMilliseconds == 5600
		&& restored.magic.maxLevel == 50 && restored.magic.weakAttackPercent == 30,
		"effect reload reapplies the saved spell level without adding a new save format") && ok;
	auto copiedState = std::make_shared<Magic>();
	copiedState->copy(stateEffect.magic);
	Effect sparseCopy;
	sparseCopy.level = 3;
	sparseCopy.initFromMagic(copiedState);
	ok = check(sparseCopy.magic.disableSkillMilliseconds == 200 && sparseCopy.magic.maxLevel == 5
		&& sparseCopy.magic.npcIni == "init-morph.ini" && sparseCopy.magic.replaceMagic == "init-replacement.ini",
		"copying a resolved level still preserves the independent Init defaults for another level") && ok;
	stateEffect.level = 3;
	stateEffect.initFromMagic(states);
	ok = check(stateEffect.magic.disableSkillMilliseconds == 200 && stateEffect.magic.maxLevel == 5
		&& stateEffect.magic.weakAttackPercent == 10 && stateEffect.magic.attackAddPercent == 10,
		"a sparse level starts from Init instead of inheriting another level's runtime overrides") && ok;
	stateEffect.level = 4;
	stateEffect.initFromMagic(states);
	ok = check(stateEffect.magic.disableMoveMilliseconds == 0 && stateEffect.magic.disableSkillMilliseconds == 0
		&& stateEffect.magic.keepMilliseconds == 0 && stateEffect.magic.npcFile.empty()
		&& stateEffect.magic.maxCount == 0 && stateEffect.magic.morphMilliseconds == 0
		&& stateEffect.magic.changeToFriendMilliseconds == 0 && stateEffect.magic.weakAttackPercent == 100
		&& stateEffect.magic.weakDefendPercent == 0,
		"explicit zero, empty and negative level values preserve disabling and existing numeric bounds") && ok;
	auto trail = std::make_shared<Magic>();
	trail->initFromIni("level-trail.ini");
	Magic::addEffect(trail, game.player, { 20, 20 }, { 22, 20 }, 2, 0, 0, lkSelf, nullptr);
	INIReader trailSave;
	game.effectManager->saveToIni(trailSave);
	ok = check(game.effectManager->getPendingTrailMagicCount() == 1
		&& trailSave.GetTime("Trail1", "RemainingTime", 0) == 9000,
		"a trail can enable its level duration when Init disables the duration") && ok;
	return ok;
}

bool prepareMagicFixtures(const std::filesystem::path& root)
{
	const std::string childMagic =
		"[Init]\n"
		"Name=CHILD\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"[Level1]\n"
		"Effect=1\n";
	const std::string parentMagic =
		"[Init]\n"
		"Name=PARENT\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"ExplodeMagicFile=base_child.ini\n"
		"AttackFile=base_child.ini\n"
		"FlyMagic=base_child.ini\n"
		"FlyInterval=100\n"
		"ParasiticMagic=base_child.ini\n"
		"Parasitic=1\n"
		"ParasiticInterval=200\n"
		"ParasiticMaxEffect=300\n"
		"RandMagicFile=base_child.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=base_child.ini\n"
		"SecondMagicDelay=400\n"
		"MagicWhenNewPos=base_child.ini\n"
		"MagicToUseWhenKillEnemy=base_child.ini\n"
		"MagicDirectionWhenKillEnemy=1\n"
		"BounceFlyEndMagic=base_child.ini\n"
		"BounceFly=2\n"
		"BounceFlySpeed=30\n"
		"BounceFlyEndHurt=5\n"
		"BounceFlyTouchHurt=6\n"
		"MagicDirectionWhenBounceFlyEnd=1\n"
		"ChangeMagic=base_child.ini\n"
		"HitCountToChangeMagic=2\n"
		"HitCountFlyRadius=20\n"
		"HitCountFlyAngleSpeed=10\n"
		"JumpEndMagic=base_child.ini\n"
		"JumpToTarget=1\n"
		"JumpMoveSpeed=40\n"
		"[Level1]\n"
		"MoveKind=1\n"
		"[Level2]\n"
		"MoveKind=1\n"
		"ExplodeMagicFile=alternate_child.ini\n"
		"AttackFile=alternate_child.ini\n"
		"FlyMagic=alternate_child.ini\n"
		"FlyInterval=110\n"
		"ParasiticMagic=alternate_child.ini\n"
		"Parasitic=2\n"
		"ParasiticInterval=210\n"
		"ParasiticMaxEffect=310\n"
		"RandMagicFile=alternate_child.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=alternate_child.ini\n"
		"SecondMagicDelay=410\n"
		"MagicWhenNewPos=alternate_child.ini\n"
		"MagicToUseWhenKillEnemy=alternate_child.ini\n"
		"MagicDirectionWhenKillEnemy=2\n"
		"BounceFlyEndMagic=alternate_child.ini\n"
		"BounceFly=3\n"
		"BounceFlySpeed=31\n"
		"BounceFlyEndHurt=7\n"
		"BounceFlyTouchHurt=8\n"
		"MagicDirectionWhenBounceFlyEnd=2\n"
		"ChangeMagic=alternate_child.ini\n"
		"HitCountToChangeMagic=3\n"
		"HitCountFlyRadius=21\n"
		"HitCountFlyAngleSpeed=11\n"
		"JumpEndMagic=alternate_child.ini\n"
		"JumpToTarget=2\n"
		"JumpMoveSpeed=41\n"
		"[Level3]\n"
		"MoveKind=1\n"
		"ExplodeMagicFile=\n"
		"AttackFile=\n"
		"FlyMagic=\n"
		"FlyInterval=0\n"
		"ParasiticMagic=\n"
		"Parasitic=0\n"
		"ParasiticInterval=0\n"
		"ParasiticMaxEffect=0\n"
		"RandMagicFile=\n"
		"RandMagicProbability=0\n"
		"SecondMagicFile=\n"
		"SecondMagicDelay=0\n"
		"MagicWhenNewPos=\n"
		"MagicToUseWhenKillEnemy=\n"
		"MagicDirectionWhenKillEnemy=0\n"
		"BounceFlyEndMagic=\n"
		"BounceFly=0\n"
		"ChangeMagic=\n"
		"HitCountToChangeMagic=0\n"
		"JumpEndMagic=\n"
		"JumpToTarget=0\n"
		"[Level4]\n"
		"MoveKind=1\n"
		"AttackFile=missing_attack.ini\n";
	const std::string rageMagic =
		"[Init]\n"
		"Name=RAGE\n"
		"MoveKind=13\n"
		"SpecialKind=99\n"
		"RangeEffect=1\n"
		"RangeRadius=0\n"
		"RangeTimeInerval=500\n"
		"[Level1]\n"
		"MoveKind=13\n"
		"SpecialKind=99\n"
		"RageCost=100\n"
		"RangeAddRage=5\n"
		"CritChanceAddValue=30\n"
		"CritDamageAddPercent=50\n"
		"LifeFrame=1000\n"
		"[Level2]\n"
		"MoveKind=13\n";
	const std::string magicExperience =
		"[HitMagicExp]\n"
		"LevelFactor=3\n"
		"[XiuLianMagicExp]\n"
		"Fraction=0.2222\n"
		"[UseMagicExp]\n"
		"Fraction=0.0333\n";

	return writeTextFile(root / "ini" / "magic" / "base_child.ini", childMagic)
		&& writeTextFile(root / "ini" / "magic" / "alternate_child.ini", childMagic)
		&& writeTextFile(root / "ini" / "magic" / "parent.ini", parentMagic)
		&& writeTextFile(root / "ini" / "magic" / "rage.ini", rageMagic)
		&& writeTextFile(root / "ini" / "level" / "MagicExp.ini", magicExperience);
}

bool prepareLinkedMagicGraphFixtures(
	const std::filesystem::path& root,
	const std::filesystem::path& firstFallbackRoot,
	const std::filesystem::path& secondFallbackRoot)
{
	const std::string leafMagic =
		"[Init]\n"
		"Name=LEAF\n"
		"MoveKind=1\n"
		"LifeFrame=10\n";
	const std::string selfMagic =
		"[Init]\n"
		"Name=SELF\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"RandMagicFile=self.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=graph_leaf.ini\n";
	const std::string mutualA =
		"[Init]\n"
		"Name=MUTUAL_A\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"RandMagicFile=mutual_b.ini\n"
		"RandMagicProbability=100\n";
	const std::string mutualB =
		"[Init]\n"
		"Name=MUTUAL_B\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"SecondMagicFile=mutual_a.ini\n"
		"FlyMagic=graph_leaf.ini\n"
		"FlyInterval=1\n";
	const std::string attackParent =
		"[Init]\n"
		"Name=ATTACK_PARENT\n"
		"MoveKind=1\n"
		"AttackFile=attack_child.ini\n";
	const std::string attackChild =
		"[Init]\n"
		"Name=ATTACK_CHILD\n"
		"MoveKind=1\n"
		"AttackFile=graph_leaf.ini\n"
		"RandMagicFile=graph_leaf.ini\n"
		"RandMagicProbability=100\n";
	const std::string dependencyParent =
		"[Init]\n"
		"Name=DEPENDENCY_PARENT\n"
		"MoveKind=1\n"
		"FlyMagic=dependency_child.ini\n"
		"FlyInterval=1\n";
	const std::string dependencyChild =
		"[Init]\n"
		"Name=DEPENDENCY_CHILD\n"
		"MoveKind=1\n"
		"ExplodeMagicFile=dependency_grandchild.ini\n";

	bool ok = writeTextFile(root / "ini" / "magic" / "graph_leaf.ini", leafMagic)
		&& writeTextFile(root / "ini" / "magic" / "self.ini", selfMagic)
		&& writeTextFile(root / "ini" / "magic" / "mutual_a.ini", mutualA)
		&& writeTextFile(root / "ini" / "magic" / "mutual_b.ini", mutualB)
		&& writeTextFile(root / "ini" / "magic" / "attack_parent.ini", attackParent)
		&& writeTextFile(root / "ini" / "magic" / "attack_child.ini", attackChild)
		&& writeTextFile(root / "ini" / "magic" / "dependency_parent.ini", dependencyParent)
		&& writeTextFile(firstFallbackRoot / "ini" / "magic" / "dependency_child.ini", dependencyChild)
		&& writeTextFile(
			secondFallbackRoot / "ini" / "magic" / "dependency_child.ini",
			"[Init]\nName=WRONG_PRECEDENCE\nMoveKind=1\n")
		&& writeTextFile(secondFallbackRoot / "ini" / "magic" / "dependency_grandchild.ini", leafMagic);

	for (int index = 0; index <= Magic::MaxLinkedMagicLoadDepth + 1; index++)
	{
		std::string content =
			"[Init]\n"
			"Name=DEPTH_" + std::to_string(index) + "\n"
			"MoveKind=1\n";
		if (index <= Magic::MaxLinkedMagicLoadDepth)
		{
			content += "RandMagicFile=depth_" + std::to_string(index + 1) + ".ini\n";
			content += "RandMagicProbability=100\n";
		}
		ok = writeTextFile(
			root / "ini" / "magic" / ("depth_" + std::to_string(index) + ".ini"),
			content) && ok;
	}

	for (size_t index = 1; index <= Magic::MaxLinkedMagicLoadNodes; index++)
	{
		std::string content =
			"[Init]\n"
			"Name=BUDGET_" + std::to_string(index) + "\n"
			"MoveKind=1\n";
		const size_t left = index * 2;
		const size_t right = left + 1;
		if (left < Magic::MaxLinkedMagicLoadNodes)
		{
			content += "RandMagicFile=budget_" + std::to_string(left) + ".ini\n";
			content += "RandMagicProbability=100\n";
		}
		if (right < Magic::MaxLinkedMagicLoadNodes)
		{
			content += "SecondMagicFile=budget_" + std::to_string(right) + ".ini\n";
		}
		ok = writeTextFile(
			root / "ini" / "magic" / ("budget_" + std::to_string(index) + ".ini"),
			content) && ok;
	}
	const std::string budgetRoot =
		"[Init]\n"
		"Name=BUDGET_ROOT\n"
		"MoveKind=1\n"
		"RandMagicFile=budget_1.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=budget_256.ini\n";
	return writeTextFile(root / "ini" / "magic" / "budget_root.ini", budgetRoot) && ok;
}

size_t countLinkedMagicNodes(
	const std::shared_ptr<Magic>& magic,
	std::unordered_set<const Magic*>& visited)
{
	if (magic == nullptr || !visited.insert(magic.get()).second)
	{
		return 0;
	}
	const auto& linked = magic->getLinkedLevel(1);
	return 1
		+ countLinkedMagicNodes(linked.randMagic, visited)
		+ countLinkedMagicNodes(linked.secondMagic, visited);
}

bool runLinkedMagicGraphLoadingTest(
	const std::filesystem::path& root,
	const std::filesystem::path& firstFallbackRoot,
	const std::filesystem::path& secondFallbackRoot)
{
	if (!prepareLinkedMagicGraphFixtures(root, firstFallbackRoot, secondFallbackRoot))
	{
		return check(false, "write linked magic graph fixtures");
	}

	bool ok = true;
	Magic self;
	self.initFromIni("self.ini");
	ok = check(self.loadSucceeded
		&& self.getLinkedLevel(1).randMagic == nullptr
		&& self.getLinkedLevel(1).secondMagic != nullptr,
		"self cycle truncates only the cyclic branch") && ok;

	Magic mutual;
	mutual.initFromIni("mutual_a.ini");
	auto mutualChild = mutual.getLinkedLevel(1).randMagic;
	ok = check(mutualChild != nullptr
		&& mutualChild->getLinkedLevel(1).secondMagic == nullptr
		&& mutualChild->getLinkedLevel(1).flyMagic != nullptr,
		"mutual cycle truncates the back edge and keeps a valid sibling") && ok;

	Magic attackParent;
	attackParent.initFromIni("attack_parent.ini");
	auto attackChild = attackParent.getLinkedLevel(1).specialMagic;
	ok = check(attackChild != nullptr
		&& attackChild->getLinkedLevel(1).specialMagic == nullptr
		&& attackChild->getLinkedLevel(1).randMagic != nullptr,
		"AttackFile child suppresses nested AttackFile but keeps other linked magic") && ok;

	Magic depthRoot;
	depthRoot.initFromIni("depth_0.ini");
	const Magic* depthNode = &depthRoot;
	int loadedDepth = 1;
	while (depthNode != nullptr && depthNode->getLinkedLevel(1).randMagic != nullptr)
	{
		depthNode = depthNode->getLinkedLevel(1).randMagic.get();
		loadedDepth++;
	}
	ok = check(loadedDepth == Magic::MaxLinkedMagicLoadDepth
		&& depthNode != nullptr
		&& depthNode->getLinkedLevel(1).randMagic == nullptr,
		"linked magic load depth limit truncates only the over-depth edge") && ok;

	Magic budgetRoot;
	budgetRoot.initFromIni("budget_root.ini");
	std::unordered_set<const Magic*> visited;
	const size_t loadedNodes = 1
		+ countLinkedMagicNodes(budgetRoot.getLinkedLevel(1).randMagic, visited)
		+ countLinkedMagicNodes(budgetRoot.getLinkedLevel(1).secondMagic, visited);
	ok = check(loadedNodes == Magic::MaxLinkedMagicLoadNodes
		&& budgetRoot.getLinkedLevel(1).randMagic != nullptr
		&& budgetRoot.getLinkedLevel(1).secondMagic == nullptr,
		"linked magic node budget preserves loaded branches and truncates the next branch") && ok;

	File::setResourceFallbackRoots({ firstFallbackRoot.string(), secondFallbackRoot.string() });
	Magic dependencyParent;
	dependencyParent.initFromIni("dependency_parent.ini");
	auto dependencyChild = dependencyParent.getLinkedLevel(1).flyMagic;
	ok = check(dependencyChild != nullptr
		&& dependencyChild->iniName == "dependency_child.ini"
		&& dependencyChild->name == "DEPENDENCY_CHILD"
		&& dependencyChild->experienceOwnerMagicFile == "dependency_parent.ini"
		&& dependencyChild->getExplodeMagicForLevel(1) != nullptr
		&& dependencyChild->getExplodeMagicForLevel(1)->iniName == "dependency_grandchild.ini"
		&& dependencyChild->getExplodeMagicForLevel(1)->experienceOwnerMagicFile == "dependency_parent.ini",
		"linked magic recursion resolves child and grandchild through dependency roots with root experience ownership") && ok;
	File::setResourceFallbackRoots({});
	return ok;
}

std::shared_ptr<Magic> makeRuntimeMagic(const std::string& fileName)
{
	auto magic = std::make_shared<Magic>();
	magic->iniName = fileName;
	magic->experienceOwnerMagicFile = fileName;
	magic->loadSucceeded = true;
	magic->level[1].moveKind = mmkPoint;
	magic->level[1].lifeFrame = 10;
	return magic;
}

bool runLinkedMagicRuntimeBudgetTest(GameManager& gameManager)
{
	auto originalMapData = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 64;
	gameManager.map->data->head.height = 64;
	gameManager.player->setPosition({ 20, 20 }, false);
	const Point from = gameManager.player->getPosition();
	const Point to = Map::getSubPoint(from, 0);
	bool ok = true;

	gameManager.effectManager->freeResource();
	auto randA = makeRuntimeMagic("runtime_rand_a.ini");
	auto randB = makeRuntimeMagic("runtime_rand_b.ini");
	randA->linkedLevel[1].randMagic = randB;
	randA->linkedLevel[1].randMagicProbability = 100;
	randB->linkedLevel[1].randMagic = randA;
	randB->linkedLevel[1].randMagicProbability = 100;
	Magic::addEffect(randA, gameManager.player, from, to, 1, 1, 0, lkSelf, nullptr);
	ok = check(gameManager.effectManager->effectList.size() == 2,
		"runtime RandMagic mutual cycle stops after the valid child") && ok;
	randA->linkedLevel[1].randMagic = nullptr;
	randB->linkedLevel[1].randMagic = nullptr;
	gameManager.effectManager->freeResource();

	auto secondA = makeRuntimeMagic("runtime_second_a.ini");
	auto secondB = makeRuntimeMagic("runtime_second_b.ini");
	secondA->linkedLevel[1].secondMagic = secondB;
	secondA->linkedLevel[1].secondMagicDelay = 0;
	secondB->linkedLevel[1].secondMagic = secondA;
	secondB->linkedLevel[1].secondMagicDelay = 0;
	Magic::addEffect(secondA, gameManager.player, from, to, 1, 1, 0, lkSelf, nullptr);
	ok = check(gameManager.effectManager->effectList.size() == 1
		&& gameManager.effectManager->getPendingDelayedMagicCount() == 1,
		"runtime SecondMagic queues the first valid child") && ok;
	MagicDerivedRuntimeTestAccess::updateDelayedMagic(*gameManager.effectManager);
	ok = check(gameManager.effectManager->effectList.size() == 2
		&& gameManager.effectManager->getPendingDelayedMagicCount() == 0,
		"runtime SecondMagic mutual cycle does not requeue its ancestor") && ok;
	secondA->linkedLevel[1].secondMagic = nullptr;
	secondB->linkedLevel[1].secondMagic = nullptr;
	gameManager.effectManager->freeResource();

	auto contextRootMagic = makeRuntimeMagic("runtime_budget_root.ini");
	auto contextChildMagic = makeRuntimeMagic("runtime_budget_child.ini");
	auto rootContext = Magic::createRootDispatchContext(contextRootMagic);
	for (size_t index = 1; index < Magic::MaxDerivedMagicRuntimeNodes; index++)
	{
		ok = check(Magic::createDerivedDispatchContext(
			rootContext,
			contextChildMagic,
			"RuntimeBudgetSibling") != nullptr,
			"runtime linked magic accepts a child within the shared node budget") && ok;
		if (!ok)
		{
			break;
		}
	}
	ok = check(Magic::createDerivedDispatchContext(
		rootContext,
		contextChildMagic,
		"RuntimeBudgetSibling") == nullptr,
		"runtime linked magic rejects the first child beyond the shared node budget") && ok;

	auto depthContext = Magic::createRootDispatchContext(makeRuntimeMagic("runtime_depth_0.ini"));
	for (int depth = 1; depth < Magic::MaxDerivedMagicRuntimeDepth; depth++)
	{
		depthContext = Magic::createDerivedDispatchContext(
			depthContext,
			makeRuntimeMagic("runtime_depth_" + std::to_string(depth) + ".ini"),
			"RuntimeDepth");
		ok = check(depthContext != nullptr,
			"runtime linked magic accepts a child within the depth budget") && ok;
	}
	ok = check(Magic::createDerivedDispatchContext(
		depthContext,
		makeRuntimeMagic("runtime_depth_over.ini"),
		"RuntimeDepth") == nullptr,
		"runtime linked magic rejects the first over-depth child") && ok;

	gameManager.effectManager->freeResource();
	gameManager.map->data = originalMapData;
	return ok;
}

bool runRageSystemTest(GameManager& gameManager, const std::filesystem::path& root)
{
	auto rageMagic = std::make_shared<Magic>();
	rageMagic->initFromIni("rage.ini");
	bool ok = true;
	ok = check(rageMagic->loadSucceeded
		&& rageMagic->level[1].rageCost == 100
		&& rageMagic->level[1].rangeAddRage == 5
		&& rageMagic->level[1].critChanceAddValue == 30
		&& rageMagic->level[1].critDamageAddPercent == 50,
		"MG rage fields load together") && ok;
	ok = check(rageMagic->level[2].rageCost == 100
		&& rageMagic->level[2].hasRageCost
		&& rageMagic->level[2].rangeAddRage == 5
		&& rageMagic->level[2].hasRangeAddRage
		&& rageMagic->level[2].critChanceAddValue == 30
		&& rageMagic->level[2].hasCritChanceAddValue
		&& rageMagic->level[2].critDamageAddPercent == 50
		&& rageMagic->level[2].hasCritDamageAddPercent,
		"MG rage and critical fields inherit as one level capability group") && ok;
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getMagicState("rage.ini", "CritChanceAddValue", "rage_crit_chance", 2);
	gameManager.scriptAPI.getMagicState("rage.ini", "HasCritChanceAddValue", "rage_has_crit_chance", 2);
	gameManager.scriptAPI.getMagicState("rage.ini", "CritDamageAddPercent", "rage_crit_damage", 2);
	ok = check(gameManager.varList.getInteger("rage_crit_chance") == 30
		&& gameManager.varList.getInteger("rage_has_crit_chance") == 1
		&& gameManager.varList.getInteger("rage_crit_damage") == 50,
		"GetMagicState exposes inherited MG critical fields") && ok;

	auto player = gameManager.player;
	player->level = 10;
	player->life = 100;
	player->defend = 0;
	player->rageMax = 100;
	gameManager.global.feature.rageSystem = false;
	player->setRage(0);
	ok = check(player->tryConsumeMagicCost(rageMagic, 1, false),
		"disabled RageSystem leaves trilogy magic-cost behavior unchanged") && ok;
	ok = check(player->applyCriticalDamage(100, 0) == 100,
		"disabled RageSystem leaves trilogy damage behavior unchanged") && ok;

	gameManager.global.feature.rageSystem = true;
	player->setRage(99);
	ok = check(!player->tryConsumeMagicCost(rageMagic, 1, false),
		"RageCost rejects a cast below the required rage") && ok;
	player->setRage(100);
	ok = check(player->tryConsumeMagicCost(rageMagic, 1, false) && player->rage == 100,
		"RageCost is a gate and does not deduct rage at the cast entry") && ok;

	RageSystemTestAccess::recordActualDamage(*player, 10);
	ok = check(player->rage == 100,
		"actual player hurt adds one rage and clamps at RageMax") && ok;
	player->setRage(50);
	RageSystemTestAccess::recordActualDamage(*player, 10);
	ok = check(player->rage == 51,
		"each actual player hurt event adds one rage") && ok;
	gameManager.scriptAPI.getPlayerState("Rage", "player_rage");
	gameManager.scriptAPI.getPlayerState("RageMax", "player_rage_max");
	ok = check(gameManager.varList.getInteger("player_rage") == 51
		&& gameManager.varList.getInteger("player_rage_max") == 100
		&& gameManager.getBindValue("player.rage") == 51
		&& gameManager.getBindValue("player.rageMax") == 100,
		"script and config-driven UI bindings expose Rage/RageMax") && ok;

	Point playerPosition = player->getPosition();
	auto effects = Magic::addEffect(rageMagic, player, playerPosition, playerPosition, 1, 0, 0, lkSelf, player);
	ok = check(effects.size() == 1 && player->attributeChangeEffect.lock() == effects.front(),
		"SpecialKind 99 binds its active self Effect to the player") && ok;
	ok = check(effects.size() == 1 && effects.front()->getExplodinUTime() == 10000,
		"SpecialKind 99 uses LifeFrame as its active duration") && ok;
	ok = check(std::abs(player->getCriticalChancePercent() - 31.0f) < 0.001f
		&& player->getCriticalDamagePercent() == 60,
		"SpecialKind 99 combines MG level defaults with critical additions") && ok;
	bool wasCritical = false;
	int criticalDamage = player->applyCriticalDamage(100, 31, &wasCritical);
	bool wasNormalHit = true;
	int normalDamage = player->applyCriticalDamage(100, 32, &wasNormalHit);
	ok = check(criticalDamage == 160
		&& wasCritical
		&& normalDamage == 100
		&& !wasNormalHit,
		"critical chance uses the inclusive 0..100 roll and reports MG critical-tip state") && ok;

	auto rangeEffect = std::make_shared<Effect>();
	rangeEffect->level = 1;
	rangeEffect->user = player;
	rangeEffect->position = playerPosition;
	rangeEffect->initFromMagic(rageMagic);
	player->setRage(100);
	RageSystemTestAccess::updateRangeEffect(*rangeEffect, 500);
	ok = check(player->rage == 95,
		"RangeAddRage follows the MG contract and drains rage on the range cadence") && ok;
	gameManager.global.feature.rageSystem = false;
	RageSystemTestAccess::updateRangeEffect(*rangeEffect, 500);
	ok = check(player->rage == 95,
		"disabled RageSystem prevents RangeAddRage side effects") && ok;

	gameManager.global.feature.rageSystem = true;
	INIReader savedAttributeEffect;
	effects.front()->saveToIni(&savedAttributeEffect, "RAGE1");
	effects.front()->releaseRuntimeBindings();
	ok = check(player->attributeChangeEffect.expired()
		&& std::abs(player->getCriticalChancePercent() - 1.0f) < 0.001f,
		"releasing the SpecialKind 99 Effect removes its temporary critical additions") && ok;
	auto loadedAttributeEffect = std::make_shared<Effect>();
	loadedAttributeEffect->initFromIni(&savedAttributeEffect, "RAGE1");
	loadedAttributeEffect->restoreRuntimeBindingsAfterLoad();
	ok = check(player->attributeChangeEffect.lock() == loadedAttributeEffect
		&& std::abs(player->getCriticalChancePercent() - 31.0f) < 0.001f,
		"Effect save/load restores the active SpecialKind 99 critical binding") && ok;
	loadedAttributeEffect->releaseRuntimeBindings();

	std::filesystem::path originalWorkingDirectory = std::filesystem::current_path();
	std::error_code fileError;
	std::filesystem::create_directories(
		root / "save" / MagicDerivedSaveNamespace / "game",
		fileError);
	if (!check(!fileError, "create isolated RageSystem save directory"))
	{
		gameManager.global.feature.rageSystem = false;
		return false;
	}
	std::filesystem::current_path(root, fileError);
	if (!check(!fileError, "enter isolated RageSystem save directory"))
	{
		gameManager.global.feature.rageSystem = false;
		return false;
	}
	player->setRage(73);
	player->levelIni = "";
	player->save();
	auto savedPlayerPath = root / "save" /
		MagicDerivedSaveNamespace / "game" / "player.ini";
	ok = check(std::filesystem::exists(savedPlayerPath),
		"RageSystem player save writes the current player file") && ok;
	auto loadedPlayer = std::make_shared<Player>();
	loadedPlayer->load();
	ok = check(loadedPlayer->rage == 73 && loadedPlayer->rageMax == 100,
		"player save/load restores Rage and keeps the MG RageMax default") && ok;

	player->actionManager->restartActionIgnoringTransitions(acStand);
	player->setTime(5000);
	player->actionBeginTime = player->getTime();
	ok = check(player->save(),
		"standing player regression fixture writes a direct-load save") && ok;
	player->setTime(25);
	player->actionBeginTime = 5000;
	player->haveAsyncDest = true;
	player->stepList.push_back({ 1, 1 });
	player->setOffset({ 3.0f, -2.0f });
	player->load();
	const UTime loadedStandBeginTime = player->actionBeginTime;
	player->actionLastTime = 100;
	player->actionManager->update(16);
	player->actionManager->update(16);
	ok = check(
		player->actionManager->getCurrentActionType() == acStand
			&& player->isStanding()
			&& player->actionBeginTime == loadedStandBeginTime
			&& loadedStandBeginTime == player->getTime()
			&& !player->haveAsyncDest
			&& player->stepList.empty()
			&& player->getOffset().x == 0.0f
			&& player->getOffset().y == 0.0f,
		"standing direct load restarts a stable idle action without requiring movement") && ok;

	player->actionLastTime = 100;
	player->actionBeginTime = player->getTime() + 500;
	const UTime futureActionBeginTime = player->actionBeginTime;
	player->actionManager->update(16);
	ok = check(player->actionBeginTime == futureActionBeginTime,
		"standing animation ignores elapsed time while the action clock is ahead") && ok;
	player->actionManager->restartActionIgnoringTransitions(acStand);
	std::filesystem::current_path(originalWorkingDirectory, fileError);
	ok = check(!fileError, "restore working directory after RageSystem save test") && ok;
	gameManager.global.feature.rageSystem = false;
	return ok;
}

bool runInsufficientResourceMessageTest(GameManager& gameManager)
{
	gameManager.menu->messageBox = std::make_shared<MsgBox>();
	auto magic = std::make_shared<Magic>();
	magic->level[1].manaCost = 10;
	magic->level[1].thewCost = 10;
	magic->level[1].lifeCost = 10;

	auto& player = *gameManager.player;
	player.canUseMana = true;
	player.mana = 0;
	player.thew = 100;
	player.life = 100;
	bool ok = check(!player.tryConsumeMagicCost(magic, 1, true)
		&& gameManager.menu->messageBox->currentMessage == "内力不足!",
		"magic cost reports insufficient mana");

	player.mana = 100;
	player.thew = 0;
	ok = check(!player.tryConsumeMagicCost(magic, 1, true)
		&& gameManager.menu->messageBox->currentMessage == "体力不足!",
		"shared trilogy magic cost reports insufficient stamina") && ok;

	player.thew = 100;
	player.canUseMana = false;
	ok = check(!player.tryConsumeMagicCost(magic, 1, true)
		&& gameManager.menu->messageBox->currentMessage == "内力不足!",
		"LimitMana reports why magic use is blocked") && ok;
	player.canUseMana = true;
	return ok;
}

bool runExplodeMagicLevelLoadingTest(const std::filesystem::path& root, Magic& copiedParent)
{
	if (!prepareMagicFixtures(root))
	{
		std::cerr << "FAILED: write derived magic fixtures\n";
		return false;
	}

	Magic parent;
	parent.initFromIni("parent.ini");
	bool ok = true;
	ok = check(parent.loadSucceeded, "parent magic loads") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(1) == "base_child.ini",
		"Level1 inherits ExplodeMagicFile from Init") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(2) == "alternate_child.ini",
		"Level2 overrides ExplodeMagicFile") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(3).empty(),
		"explicit empty Level3 ExplodeMagicFile disables the Init child") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(4) == "base_child.ini",
		"Level4 falls back to Init instead of inheriting Level2") && ok;

	auto level1Child = parent.getExplodeMagicForLevel(1);
	auto level2Child = parent.getExplodeMagicForLevel(2);
	auto level3Child = parent.getExplodeMagicForLevel(3);
	auto level4Child = parent.getExplodeMagicForLevel(4);
	ok = check(level1Child != nullptr && level1Child->iniName == "base_child.ini",
		"Level1 resolves the Init explode child") && ok;
	ok = check(level2Child != nullptr && level2Child->iniName == "alternate_child.ini",
		"Level2 resolves its alternate explode child") && ok;
	ok = check(level3Child == nullptr, "Level3 resolves no explode child") && ok;
	ok = check(level4Child == level1Child,
		"equal per-level child names reuse one loaded Magic object") && ok;

	const auto& linked1 = parent.getLinkedLevel(1);
	const auto& linked2 = parent.getLinkedLevel(2);
	const auto& linked3 = parent.getLinkedLevel(3);
	const auto& linked4 = parent.getLinkedLevel(4);
	ok = check(linked1.specialMagic != nullptr && linked1.specialMagic->iniName == "base_child.ini"
		&& linked2.specialMagic != nullptr && linked2.specialMagic->iniName == "alternate_child.ini",
		"AttackFile resolves the Init child and the Level2 override") && ok;
	ok = check(linked3.specialMagic == linked1.specialMagic
		&& linked3.attackFile == "base_child.ini",
		"explicit empty AttackFile preserves the Init attack child") && ok;
	ok = check(linked4.specialMagic == linked1.specialMagic
		&& linked4.attackFile == "base_child.ini",
		"missing Level4 AttackFile target preserves the Init attack child") && ok;
	ok = check(linked2.flyMagic != nullptr && linked2.flyMagic->iniName == "alternate_child.ini"
		&& linked2.flyInterval == 110
		&& linked2.parasiticMagic != nullptr
		&& linked2.parasitic == 2
		&& linked2.parasiticInterval == 210
		&& linked2.parasiticMaxEffect == 310
		&& linked2.randMagic != nullptr
		&& linked2.randMagicProbability == 100
		&& linked2.secondMagic != nullptr
		&& linked2.secondMagicDelay == 410,
		"Level2 overrides linked magic files and timing/probability companions as one group") && ok;
	ok = check(linked2.magicWhenNewPosition != nullptr
		&& linked2.magicToUseWhenKillEnemy != nullptr
		&& linked2.magicDirectionWhenKillEnemy == 2
		&& linked2.bounceFlyEndMagic != nullptr
		&& linked2.bounceFly == 3
		&& linked2.bounceFlySpeed == 31
		&& linked2.bounceFlyEndHurt == 7
		&& linked2.bounceFlyTouchHurt == 8
		&& linked2.magicDirectionWhenBounceFlyEnd == 2
		&& linked2.changeMagic != nullptr
		&& linked2.hitCountToChangeMagic == 3
		&& linked2.hitCountFlyRadius == 21
		&& linked2.hitCountFlyAngleSpeed == 11
		&& linked2.jumpEndMagic != nullptr
		&& linked2.jumpToTarget == 2
		&& linked2.jumpMoveSpeed == 41,
		"Level2 overrides position, kill, bounce, change and jump linked groups") && ok;
	ok = check(linked3.flyMagic == nullptr
		&& linked3.parasiticMagic == nullptr
		&& linked3.randMagic == nullptr
		&& linked3.secondMagic == nullptr
		&& linked3.magicWhenNewPosition == nullptr
		&& linked3.magicToUseWhenKillEnemy == nullptr
		&& linked3.bounceFlyEndMagic == nullptr
		&& linked3.changeMagic == nullptr
		&& linked3.jumpEndMagic == nullptr
		&& linked3.flyInterval == 0
		&& linked3.parasitic == 0
		&& linked3.randMagicProbability == 0
		&& linked3.secondMagicDelay == 0,
		"explicit empty Level3 linked fields disable that level without affecting AttackFile") && ok;
	ok = check(linked4.flyMagic == linked1.flyMagic
		&& linked4.parasiticMagic == linked1.parasiticMagic
		&& linked4.randMagic == linked1.randMagic
		&& linked4.secondMagic == linked1.secondMagic
		&& linked4.magicWhenNewPosition == linked1.magicWhenNewPosition
		&& linked4.magicToUseWhenKillEnemy == linked1.magicToUseWhenKillEnemy
		&& linked4.bounceFlyEndMagic == linked1.bounceFlyEndMagic
		&& linked4.changeMagic == linked1.changeMagic
		&& linked4.jumpEndMagic == linked1.jumpEndMagic
		&& linked4.flyInterval == 100
		&& linked4.secondMagicDelay == 400,
		"Level4 falls back to Init instead of inheriting Level2 linked overrides") && ok;
	ok = check(&parent.getLinkedLevel(0) == &parent.getLinkedLevel(1)
		&& parent.getLinkedLevel(MAGIC_MAX_LEVEL + 1).flyMagic == linked1.flyMagic,
		"linked level lookup clamps below and above the supported level range") && ok;
	ok = check(level2Child != nullptr && level2Child->experienceOwnerMagicFile == "parent.ini",
		"explode child inherits the parent experience owner") && ok;
	ok = check(linked2.flyMagic->experienceOwnerMagicFile == "parent.ini"
		&& linked2.parasiticMagic->experienceOwnerMagicFile == "parent.ini"
		&& linked2.jumpEndMagic->experienceOwnerMagicFile == "parent.ini"
		&& linked2.randMagic->experienceOwnerMagicFile == "alternate_child.ini"
		&& linked2.secondMagic->experienceOwnerMagicFile == "alternate_child.ini"
		&& linked2.changeMagic->experienceOwnerMagicFile == "alternate_child.ini",
		"experience ownership propagates only to reference-supported child groups") && ok;

	copiedParent.copy(parent);
	parent.freeResource();
	ok = check(copiedParent.getExplodeMagicForLevel(2) != nullptr,
		"Magic::copy keeps per-level explode children alive after the source releases resources") && ok;
	ok = check(copiedParent.getLinkedLevel(2).flyMagic != nullptr
		&& copiedParent.getLinkedLevel(2).secondMagic != nullptr
		&& copiedParent.getLinkedLevel(3).flyMagic == nullptr,
		"Magic::copy keeps per-level linked children and explicit disables") && ok;
	return ok;
}

bool runEffectReferencePersistenceTest(GameManager& gameManager)
{
	auto mapNpcA = std::make_shared<NPC>();
	mapNpcA->kind = nkBattle;
	auto mapNpcB = std::make_shared<NPC>();
	mapNpcB->kind = nkBattle;
	auto partnerA = std::make_shared<NPC>();
	partnerA->kind = nkPartner;
	auto partnerB = std::make_shared<NPC>();
	partnerB->kind = nkPartner;

	gameManager.npcManager->npcList = { mapNpcA, partnerA, mapNpcB, partnerB };
	INIReader ini;
	Effect savedEffect;
	savedEffect.level = 1;
	savedEffect.user = partnerB;
	savedEffect.target = mapNpcB;
	savedEffect.saveToIni(&ini, "PRO1");

	bool ok = true;
	ok = check(ini.GetInteger("PRO1", "UserReferenceKind", 0) == 2
		&& ini.GetInteger("PRO1", "UserReferenceIndex", -1) == 1,
		"partner caster is saved by partner ordinal") && ok;
	ok = check(ini.GetInteger("PRO1", "TargetReferenceKind", 0) == 3
		&& ini.GetInteger("PRO1", "TargetReferenceIndex", -1) == 1,
		"map target is saved by persisted NPC ordinal") && ok;

	gameManager.npcManager->npcList = { partnerA, partnerB, mapNpcA, mapNpcB };
	Effect loadedEffect;
	loadedEffect.initFromIni(&ini, "PRO1");
	ok = check(loadedEffect.user.lock() == partnerB,
		"partner caster survives the partner-first load ordering") && ok;
	ok = check(loadedEffect.target.lock() == mapNpcB,
		"map target survives the partner-first load ordering") && ok;

	Effect savedPlayerEffect;
	savedPlayerEffect.level = 1;
	savedPlayerEffect.user = gameManager.player;
	savedPlayerEffect.target = partnerA;
	savedPlayerEffect.saveToIni(&ini, "PRO2");
	Effect loadedPlayerEffect;
	loadedPlayerEffect.initFromIni(&ini, "PRO2");
	ok = check(loadedPlayerEffect.user.lock() == gameManager.player,
		"player caster survives Effect save/load") && ok;
	ok = check(loadedPlayerEffect.target.lock() == partnerA,
		"partner target survives Effect save/load") && ok;

	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runExplodeMagicLevelDispatchTest(GameManager& gameManager, Magic& copiedParent)
{
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level1_has_explode", 1);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level2_has_explode", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level3_has_explode", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level4_has_explode", 4);
	bool ok = true;
	ok = check(gameManager.varList.getInteger("level1_has_explode") == 1
		&& gameManager.varList.getInteger("level2_has_explode") == 1
		&& gameManager.varList.getInteger("level3_has_explode") == 0
		&& gameManager.varList.getInteger("level4_has_explode") == 1,
		"GetMagicState reports per-level ExplodeMagicFile availability") && ok;

	auto parentMagic = std::make_shared<Magic>();
	parentMagic->copy(copiedParent);
	auto dispatchAtLevel = [&](int level, const std::string& expectedChildFile, bool shouldDispatch)
	{
		size_t beforeCount = gameManager.effectManager->effectList.size();
		auto parentEffect = std::make_shared<Effect>();
		parentEffect->level = level;
		parentEffect->user = gameManager.player;
		parentEffect->position = { 5, 5 };
		parentEffect->src = parentEffect->position;
		parentEffect->initFromMagic(parentMagic);
		parentEffect->beginExplode(parentEffect->position);
		size_t afterCount = gameManager.effectManager->effectList.size();
		if (!shouldDispatch)
		{
			return afterCount == beforeCount;
		}
		return afterCount == beforeCount + 1
			&& gameManager.effectManager->effectList.back() != nullptr
			&& gameManager.effectManager->effectList.back()->magic.iniName == expectedChildFile;
	};

	ok = check(dispatchAtLevel(2, "alternate_child.ini", true),
		"Level2 dispatches its alternate explode child") && ok;
	ok = check(dispatchAtLevel(3, "", false),
		"explicit empty Level3 dispatches no explode child") && ok;
	ok = check(dispatchAtLevel(4, "base_child.ini", true),
		"Level4 dispatches the Init explode child") && ok;
	return ok;
}

bool runPublishedIniCommentContract(const std::filesystem::path& root)
{
	if (!check(writeTextFile(root / "ini/magic/slash-comments.ini",
		"\xef\xbb\xbf// published MG comment\n[Init]\nName=Comment\n"
		"; existing comment\n# existing comment\n  //MoveKind=1\nMoveKind=22\n"
		"//\nIntro=https://example.test/a // literal value\nPath=//server/share\n"
		"Value=3\nvalue=4\n")
		&& writeTextFile(root / "ini/magic/malformed-comments.ini", "[Init]\nMissingDelimiter\n"),
		"write published INI comment and malformed-line fixtures")) return false;
	INIReader ini("ini/magic/slash-comments.ini", IniKeyCaseSensitivity::Sensitive);
	bool ok = check(ini.ParseError() == 0 && ini.GetInteger("Init", "MoveKind", 0) == 22
		&& ini.Get("Init", "Intro", "") == "https://example.test/a // literal value"
		&& ini.Get("Init", "Path", "") == "//server/share"
		&& ini.GetInteger("Init", "Value", 0) == 3 && ini.GetInteger("Init", "value", 0) == 4,
		"full-line slash comments coexist with existing comments, literal slash values and case-sensitive keys");
	Magic magic;
	magic.initFromIni("slash-comments.ini");
	ok = check(magic.loadSucceeded && magic.level[1].moveKind == 22,
		"published slash comments do not reject the entire magic definition") && ok;
	INIReader malformed("ini/magic/malformed-comments.ini");
	ok = check(malformed.ParseError() == 2, "unrelated malformed INI lines still report their original line number") && ok;
	return ok;
}

bool runWarningRegionContract(GameManager& gameManager, const std::filesystem::path& root)
{
	bool ok = check(writeTextFile(root / "ini/magic/warning-region.ini",
		"[Init]\nName=Warning\nMoveKind=999\nRegion=6\nLifeFrame=20\nWaitFrame=0\n"
		"RegionFile=warning-region.json\nExplodeMagicFile=warning-impact.ini\n[Level1]\nEffect=500\n")
		&& writeTextFile(root / "ini/magic/warning-impact.ini",
		"[Init]\nName=Impact\nMoveKind=1\nLifeFrame=10\n[Level1]\nEffect=200\n")
		&& writeTextFile(root / "ini/magic/warning-region.json",
		R"({"width":1,"height":1,"layers":[{"name":"8","width":1,"height":1,"data":[1]},{"name":"0","width":1,"height":1,"data":[1]}]})"),
		"write minimal persisted warning-region and impact definitions");
	if (!ok) return false;
	auto warning = std::make_shared<Magic>();
	warning->initFromIni("warning-region.ini");
	if (!check(warning->loadSucceeded && warning->regionFileLoaded && warning->getExplodeMagicForLevel(1),
		"warning contract loads the real INI, region parser and linked impact")) return false;
	gameManager.effectManager->clearEffect();
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 32;
	gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
	gameManager.map->createDataMap();
	gameManager.player->setPosition({ 4, 4 }, false);
	const Point destination{ 10, 10 };
	auto warnings = Magic::addEffect(warning, gameManager.player, { 4, 4 }, destination, 1, 500, 0, lkSelf, nullptr);
	ok = check(warnings.size() == 1 && warnings.front()->position == destination,
		"MG MoveKind 999 uses the configured region instead of silently producing no effects") && ok;
	// Independently probe collision and dispatch even before the spawn branch is implemented.
	auto marker = std::make_shared<Effect>();
	marker->level = 1;
	marker->user = gameManager.player;
	marker->initFromMagic(warning);
	marker->position = marker->src = destination;
	marker->launcherKind = lkSelf;
	marker->doing = ekFlying;
	ok = check(marker->skipsCharacterCollision() && marker->canPassThroughWall(),
		"a warning marker ignores actors and obstacles until its impact is dispatched") && ok;
	INIReader saved;
	marker->saveToIni(&saved, "Warning");
	auto restored = std::make_shared<Effect>();
	restored->initFromIni(&saved, "Warning");
	ok = check(restored->getMoveKind() == 999 && restored->skipsCharacterCollision() && restored->canPassThroughWall(),
		"warning collision policy survives actual effect serialization and reconstruction") && ok;
	const auto before = gameManager.effectManager->effectList.size();
	restored->beginExplode(destination);
	ok = check(gameManager.effectManager->effectList.size() == before + 1
		&& gameManager.effectManager->effectList.back()->position == destination,
		"warning destruction dispatches its linked fixed impact at the marker, not beyond it") && ok;
	restored->beginExplode(destination);
	ok = check(gameManager.effectManager->effectList.size() == before + 1,
		"repeated warning destruction cannot duplicate the impact") && ok;
	if (warnings.size() == 1)
	{
		const auto marker = warnings.front();
		const auto count = gameManager.effectManager->effectList.size();
		marker->setTime(marker->beginTime + marker->lifeTime - 1);
		RageSystemTestAccess::updateEffect(*marker);
		ok = check(gameManager.effectManager->effectList.size() == count,
			"warning impact is not dispatched before the configured lifetime") && ok;
		marker->setTime(marker->beginTime + marker->lifeTime + 1);
		RageSystemTestAccess::updateEffect(*marker);
		ok = check(gameManager.effectManager->effectList.size() == count + 1
			&& gameManager.effectManager->effectList.back()->position == destination,
			"normal warning expiration dispatches exactly one impact at its marked location") && ok;
	}
	gameManager.effectManager->clearEffect();
	return ok;
}

bool runLinkedMagicLevelDispatchTest(GameManager& gameManager, Magic& copiedParent)
{
	auto originalMapData = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 64;
	gameManager.map->data->head.height = 64;
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getMagicState("parent.ini", "HasAttackFile", "level2_has_attack", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasAttackFile", "level3_has_attack", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasAttackFile", "level4_has_attack", 4);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasFlyMagic", "level2_has_fly", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasFlyMagic", "level3_has_fly", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasFlyMagic", "level4_has_fly", 4);
	gameManager.scriptAPI.getMagicState("parent.ini", "FlyInterval", "level2_fly_interval", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "FlyInterval", "level3_fly_interval", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "FlyInterval", "level4_fly_interval", 4);
	gameManager.scriptAPI.getMagicState("parent.ini", "BounceFly", "level2_bounce_fly", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "BounceFlySpeed", "level3_bounce_speed", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "MagicDirectionWhenBounceFlyEnd", "level4_bounce_direction", 4);
	bool ok = true;
	ok = check(gameManager.varList.getInteger("level2_has_attack") == 1
		&& gameManager.varList.getInteger("level3_has_attack") == 1
		&& gameManager.varList.getInteger("level4_has_attack") == 1,
		"GetMagicState follows AttackFile preserve-on-empty and preserve-on-invalid semantics") && ok;
	ok = check(gameManager.varList.getInteger("level2_has_fly") == 1
		&& gameManager.varList.getInteger("level3_has_fly") == 0
		&& gameManager.varList.getInteger("level4_has_fly") == 1
		&& gameManager.varList.getInteger("level2_fly_interval") == 110
		&& gameManager.varList.getInteger("level3_fly_interval") == 0
		&& gameManager.varList.getInteger("level4_fly_interval") == 100
		&& gameManager.varList.getInteger("level2_bounce_fly") == 3
		&& gameManager.varList.getInteger("level3_bounce_speed") == 30
		&& gameManager.varList.getInteger("level4_bounce_direction") == 1,
		"GetMagicState exposes per-level Fly and Bounce companion values") && ok;

	auto parentMagic = std::make_shared<Magic>();
	parentMagic->copy(copiedParent);
	parentMagic->linkedLevel[2].jumpToTarget = 0;
	parentMagic->linkedLevel[3].jumpToTarget = 0;
	parentMagic->linkedLevel[4].jumpToTarget = 0;
	Point from = { 20, 20 };
	Point to = Map::getSubPoint(from, 0);
	auto castAtLevel = [&](int level, const std::string& expectedChild, int expectedEffectDelta, int expectedDelayedDelta)
	{
		const size_t effectCount = gameManager.effectManager->effectList.size();
		const size_t delayedCount = gameManager.effectManager->getPendingDelayedMagicCount();
		Magic::addEffect(parentMagic, gameManager.player, from, to, level, 1, 0, lkSelf, nullptr);
		const size_t actualEffectDelta = gameManager.effectManager->effectList.size() - effectCount;
		const size_t actualDelayedDelta = gameManager.effectManager->getPendingDelayedMagicCount() - delayedCount;
		const bool countsMatch = actualEffectDelta == static_cast<size_t>(expectedEffectDelta)
			&& actualDelayedDelta == static_cast<size_t>(expectedDelayedDelta);
		if (expectedEffectDelta <= 1)
		{
			return countsMatch;
		}
		return countsMatch
			&& gameManager.effectManager->effectList.back() != nullptr
			&& gameManager.effectManager->effectList.back()->magic.iniName == expectedChild;
	};
	ok = check(castAtLevel(2, "alternate_child.ini", 2, 1),
		"Level2 dispatches its RandMagic and schedules its SecondMagic") && ok;
	ok = check(castAtLevel(3, "", 1, 0),
		"explicit empty Level3 dispatches neither RandMagic nor SecondMagic") && ok;
	ok = check(castAtLevel(4, "base_child.ini", 2, 1),
		"Level4 dispatches Init RandMagic and SecondMagic instead of Level2 overrides") && ok;
	INIReader savedManager;
	gameManager.effectManager->saveToIni(savedManager);
	const int delayedCount = static_cast<int>(savedManager.GetInteger("Head", "DelayedCount", 0));
	ok = check(delayedCount >= 2
		&& savedManager.Get("Delayed1", "MagicFile", "") == "alternate_child.ini"
		&& savedManager.Get("Delayed" + std::to_string(delayedCount), "MagicFile", "") == "base_child.ini",
		"per-level SecondMagic selection survives the delayed dispatch queue") && ok;

	auto changeParent = std::make_shared<Magic>();
	changeParent->copy(copiedParent);
	changeParent->linkedLevel[2].jumpToTarget = 0;
	changeParent->linkedLevel[2].randMagic = nullptr;
	changeParent->linkedLevel[2].secondMagic = nullptr;
	gameManager.player->changeMagicHitCounts[changeParent->iniName] = 3;
	const size_t changeEffectCount = gameManager.effectManager->effectList.size();
	Magic::addEffect(changeParent, gameManager.player, from, to, 2, 1, 0, lkSelf, nullptr);
	ok = check(gameManager.effectManager->effectList.size() == changeEffectCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini"
		&& gameManager.player->changeMagicHitCounts.find(changeParent->iniName) == gameManager.player->changeMagicHitCounts.end(),
		"Level2 ChangeMagic uses its per-level threshold and child, then consumes the hit counter") && ok;

	auto jumpParent = std::make_shared<Magic>();
	jumpParent->copy(copiedParent);
	jumpParent->linkedLevel[2].randMagic = nullptr;
	jumpParent->linkedLevel[2].secondMagic = nullptr;
	const auto jumpEffects = Magic::addEffect(
		jumpParent,
		gameManager.player,
		from,
		to,
		2,
		1,
		0,
		lkSelf,
		nullptr);
	ok = check(jumpEffects.empty()
		&& gameManager.player->magicForcedMove.active
		&& gameManager.player->magicForcedMove.speed == 41.0f
		&& gameManager.player->magicForcedMove.endMagic != nullptr
		&& gameManager.player->magicForcedMove.endMagic->iniName == "alternate_child.ini"
		&& gameManager.player->magicForcedMove.level == 2,
		"Level2 JumpToTarget uses its per-level speed and JumpEndMagic child") && ok;
	gameManager.player->clearMagicForcedMoveState();

	auto flyEffect = std::make_shared<Effect>();
	flyEffect->level = 2;
	flyEffect->user = gameManager.player;
	flyEffect->position = from;
	flyEffect->dest = to;
	flyEffect->initFromMagic(parentMagic);
	flyEffect->doing = ekFlying;
	const size_t flyEffectCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateFlyMagic(*flyEffect, 109);
	ok = check(gameManager.effectManager->effectList.size() == flyEffectCount,
		"Level2 FlyInterval waits for its per-level cadence") && ok;
	RageSystemTestAccess::updateFlyMagic(*flyEffect, 1);
	ok = check(gameManager.effectManager->effectList.size() == flyEffectCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 FlyMagic dispatches its per-level child at its per-level cadence") && ok;

	auto disabledFlyEffect = std::make_shared<Effect>();
	disabledFlyEffect->level = 3;
	disabledFlyEffect->user = gameManager.player;
	disabledFlyEffect->position = from;
	disabledFlyEffect->dest = to;
	disabledFlyEffect->initFromMagic(parentMagic);
	disabledFlyEffect->doing = ekFlying;
	const size_t disabledFlyCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateFlyMagic(*disabledFlyEffect, 1000);
	ok = check(gameManager.effectManager->effectList.size() == disabledFlyCount,
		"explicit empty Level3 FlyMagic suppresses runtime dispatch") && ok;

	auto positionEffect = std::make_shared<Effect>();
	positionEffect->level = 2;
	positionEffect->user = gameManager.player;
	positionEffect->initFromMagic(parentMagic);
	positionEffect->magicWhenNewPositionInitialized = true;
	positionEffect->magicWhenNewPositionLastTile = from;
	positionEffect->position = to;
	const size_t positionEffectCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateMagicWhenNewPosition(*positionEffect);
	ok = check(gameManager.effectManager->effectList.size() == positionEffectCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 MagicWhenNewPos dispatches its per-level child") && ok;

	auto disabledPositionEffect = std::make_shared<Effect>();
	disabledPositionEffect->level = 3;
	disabledPositionEffect->user = gameManager.player;
	disabledPositionEffect->initFromMagic(parentMagic);
	disabledPositionEffect->magicWhenNewPositionInitialized = true;
	disabledPositionEffect->magicWhenNewPositionLastTile = from;
	disabledPositionEffect->position = to;
	const size_t disabledPositionCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateMagicWhenNewPosition(*disabledPositionEffect);
	ok = check(gameManager.effectManager->effectList.size() == disabledPositionCount,
		"explicit empty Level3 MagicWhenNewPos suppresses runtime dispatch") && ok;
	ok = check(flyEffect->canParasitic() && !disabledFlyEffect->canParasitic(),
		"Parasitic activation follows the effect level") && ok;

	auto triggerEffect = std::make_shared<Effect>();
	triggerEffect->level = 2;
	triggerEffect->user = gameManager.player;
	triggerEffect->launcherKind = lkSelf;
	triggerEffect->initFromMagic(parentMagic);
	auto defeatedTarget = std::make_shared<NPC>();
	defeatedTarget->setPosition(to, false);
	const size_t killMagicCount = gameManager.effectManager->effectList.size();
	defeatedTarget->triggerMagicWhenKillEnemy(*triggerEffect);
	ok = check(gameManager.effectManager->effectList.size() == killMagicCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 MagicToUseWhenKillEnemy dispatches its per-level child") && ok;
	triggerEffect->level = 3;
	const size_t disabledKillMagicCount = gameManager.effectManager->effectList.size();
	defeatedTarget->triggerMagicWhenKillEnemy(*triggerEffect);
	ok = check(gameManager.effectManager->effectList.size() == disabledKillMagicCount,
		"explicit empty Level3 MagicToUseWhenKillEnemy suppresses runtime dispatch") && ok;

	auto bounceTarget = std::make_shared<NPC>();
	bounceTarget->setPosition(to, false);
	triggerEffect->level = 2;
	triggerEffect->flyingDirection = { 1, 0 };
	bounceTarget->applyBounceFlyFromEffect(*triggerEffect);
	ok = check(bounceTarget->magicForcedMove.active
		&& bounceTarget->magicForcedMove.speed == 31.0f
		&& bounceTarget->magicForcedMove.endMagic != nullptr
		&& bounceTarget->magicForcedMove.endMagic->iniName == "alternate_child.ini"
		&& bounceTarget->magicForcedMove.endDirectionMode == 2
		&& bounceTarget->magicForcedMove.endHurt == 7
		&& bounceTarget->magicForcedMove.touchHurt == 8
		&& bounceTarget->magicForcedMove.touchDistance == 3,
		"Level2 BounceFly uses its per-level child and movement companions") && ok;
	bounceTarget->clearMagicForcedMoveState();
	triggerEffect->level = 3;
	bounceTarget->applyBounceFlyFromEffect(*triggerEffect);
	ok = check(!bounceTarget->magicForcedMove.active,
		"explicit zero Level3 BounceFly suppresses forced movement") && ok;

	auto movingBounceTarget = std::make_shared<NPC>();
	Point movingBouncePosition = { to.x + 4, to.y + 4 };
	movingBounceTarget->setPosition(movingBouncePosition, false);
	movingBounceTarget->stepList.push_back({ movingBouncePosition.x, movingBouncePosition.y - 1 });
	movingBounceTarget->actionManager->forceChangeAction(acWalk);
	movingBounceTarget->setOffset({ 7.0f, -3.0f });
	Effect bounceEffect;
	bounceEffect.magic.bounce = 120;
	bounceEffect.flyingDirection = { 0, -1 };
	bounceEffect.user = gameManager.player;
	bounceEffect.launcherKind = lkSelf;
	movingBounceTarget->applyBounceFromEffect(bounceEffect);
	ok = check(movingBounceTarget->actionManager->getCurrentActionType() == acBounce
		&& movingBounceTarget->getOffset().x == 7.0f
		&& movingBounceTarget->getOffset().y == -3.0f,
		"Bounce preserves the in-progress world offset instead of snapping to the tile center") && ok;

	auto movingBounceFlyTarget = std::make_shared<NPC>();
	Point movingBounceFlyPosition = { to.x + 8, to.y + 8 };
	movingBounceFlyTarget->setPosition(movingBounceFlyPosition, false);
	movingBounceFlyTarget->stepList.push_back({ movingBounceFlyPosition.x, movingBounceFlyPosition.y - 1 });
	movingBounceFlyTarget->actionManager->forceChangeAction(acWalk);
	movingBounceFlyTarget->setOffset({ -5.0f, 4.0f });
	triggerEffect->level = 2;
	movingBounceFlyTarget->applyBounceFlyFromEffect(*triggerEffect);
	ok = check(movingBounceFlyTarget->magicForcedMove.active
		&& movingBounceFlyTarget->magicForcedMove.startOffset.x == -5.0f
		&& movingBounceFlyTarget->magicForcedMove.startOffset.y == 4.0f
		&& movingBounceFlyTarget->getOffset().x == -5.0f
		&& movingBounceFlyTarget->getOffset().y == 4.0f,
		"BounceFly starts its Bezier move at the in-progress world offset") && ok;

	const int practiceIndex = gameManager.magicManager.practiceIndex();
	gameManager.magicManager.magicList.clear();
	gameManager.magicManager.magicList.resize(static_cast<size_t>(practiceIndex + 1));
	gameManager.magicManager.magicList[practiceIndex].magic = parentMagic;
	gameManager.magicManager.magicList[practiceIndex].iniFile = parentMagic->iniName;
	gameManager.magicManager.magicList[practiceIndex].level = 1;
	gameManager.player->attackLevel = 2;
	const size_t level2AttackCount = gameManager.effectManager->effectList.size();
	ok = check(gameManager.player->doSpecialAttack(to)
		&& gameManager.effectManager->effectList.size() == level2AttackCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 special attack dispatches its AttackFile override") && ok;
	gameManager.player->attackLevel = 3;
	const size_t level3AttackCount = gameManager.effectManager->effectList.size();
	ok = check(gameManager.player->doSpecialAttack(to)
		&& gameManager.effectManager->effectList.size() == level3AttackCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "base_child.ini",
		"explicit empty Level3 AttackFile preserves and dispatches the Init attack child") && ok;
	gameManager.map->data = originalMapData;
	return ok;
}

bool runDerivedExperienceTest(GameManager& gameManager, const Magic& copiedParent)
{
	auto childMagic = copiedParent.getExplodeMagicForLevel(2);
	if (childMagic == nullptr)
	{
		return check(false, "derived experience test has a Level2 child");
	}

	gameManager.magicManager.magicList.assign(
		static_cast<size_t>(gameManager.magicManager.listLength()), MagicInfo());
	const int currentUseIndex = gameManager.magicManager.bottomIndex(0);
	MagicInfo parentInfo;
	parentInfo.iniFile = "PARENT.INI";
	parentInfo.level = 1;
	parentInfo.magic = std::make_shared<Magic>();
	parentInfo.magic->level[1].levelupExp = 1000;
	gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)] = parentInfo;

	auto childEffect = std::make_shared<Effect>();
	childEffect->level = 2;
	childEffect->user = gameManager.player;
	childEffect->initFromMagic(childMagic);
	INIReader savedEffect;
	childEffect->saveToIni(&savedEffect, "PRO1");
	auto loadedChildEffect = std::make_shared<Effect>();
	loadedChildEffect->initFromIni(&savedEffect, "PRO1");
	bool ok = true;
	ok = check(loadedChildEffect->magic.experienceOwnerMagicFile == "parent.ini",
		"derived experience owner survives Effect save/load") && ok;
	gameManager.magicManager.addUseExp(loadedChildEffect, 7);
	ok = check(gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 7,
		"saved derived explode hit experience is credited to the parent magic case-insensitively") && ok;
	gameManager.magicManager.addHitExp(loadedChildEffect, 2);
	ok = check(gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 13,
		"configured target-level factor is credited to the effect owner") && ok;

	gameManager.magicManager.recordCurrentUseMagic(currentUseIndex);
	auto ordinaryAttackEffect = std::make_shared<Effect>();
	ordinaryAttackEffect->magic.iniName = "ordinary_attack.ini";
	gameManager.magicManager.addHitExp(ordinaryAttackEffect, 2);
	gameManager.magicManager.addKillExp(ordinaryAttackEffect, 100);
	ok = check(gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 22,
		"ordinary attack hit and kill experience uses the last magic and configured fraction") && ok;
	return ok;
}

bool runTerminalMagicExperienceTest(GameManager& gameManager, const std::filesystem::path& root)
{
	auto& manager = gameManager.magicManager;
	manager.configureLayout();
	gameManager.menu->practiceMenu = std::make_shared<PracticeMenu>();
	gameManager.varList.ensureInitialized();
	bool ok = true;
	int cases = 0;
	for (int terminalLevel : { 1, 3, MAGIC_MAX_LEVEL })
	{
		std::string definition = "[Init]\nName=TerminalExperience\n";
		for (int level = 1; level < terminalLevel; ++level)
		{
			definition += "[Level" + std::to_string(level) + "]\nLevelupExp="
				+ std::to_string(100 + level * 50) + "\n";
		}
		definition += "[Level" + std::to_string(terminalLevel) + "]\nLevelupExp=0\n";
		if (!check(writeTextFile(root / "ini/magic/terminal-experience.ini", definition),
			"write file-backed terminal experience levels")) return false;
		for (int route : { 0, 1, 2 })
		{
			const auto reset = [&](int level, int experience)
			{
				manager.clearMagicList();
				auto* info = manager.addPrimaryMagic("terminal-experience.ini", false, false);
				if (info != nullptr && route == 2)
				{
					const MagicInfo copy = *info;
					*info = MagicInfo();
					info = &manager.magicList[manager.practiceIndex()];
					*info = copy;
				}
				if (info != nullptr)
				{
					info->level = level;
					info->exp = experience;
					info->remainColdMilliseconds = 77;
				}
				return info;
			};
			const auto award = [&](int amount)
			{
				if (route == 0)
				{
					const std::string command = "addmagicexp('terminal-experience.ini'," + std::to_string(amount) + ");";
					auto bytes = std::make_unique<char[]>(command.size());
					std::copy(command.begin(), command.end(), bytes.get());
					return check(gameManager.script.runScript(bytes, static_cast<int>(command.size())) == LUA_OK,
						"terminal experience executes the actual Lua AddMagicExp entry");
				}
				if (route == 1)
				{
					auto effect = std::make_shared<Effect>();
					effect->magic.experienceOwnerMagicFile = "terminal-experience.ini";
					manager.addUseExp(effect, amount);
				}
				else manager.addPracticeExp(amount);
				return true;
			};
			for (int amount : { -10, 0, 25, (std::numeric_limits<int>::max)() })
			{
				auto* info = reset(terminalLevel, 321);
				if (!check(info != nullptr, "learn the terminal experience fixture") || !award(amount)) return false;
				const int expectedExperience = static_cast<int>(std::min<int64_t>(
					static_cast<int64_t>(321) + amount, (std::numeric_limits<int>::max)()));
				ok = check(info->level == terminalLevel && info->exp == expectedExperience && info->remainColdMilliseconds == 77,
					"zero-threshold awards accumulate without advancing levels or changing cooldown") && ok;
				++cases;
				if (terminalLevel == 1) continue;
				const int threshold = 100 + (terminalLevel - 1) * 50;
				info = reset(terminalLevel - 1, threshold - 1);
				if (!check(info != nullptr, "learn the pre-terminal experience fixture") || !award(amount)) return false;
				const bool advances = amount > 0;
				ok = check(info->level == terminalLevel - (advances ? 0 : 1)
					&& info->exp == static_cast<int>(std::min<int64_t>(
						static_cast<int64_t>(threshold) - 1 + amount, (std::numeric_limits<int>::max)()))
					&& info->remainColdMilliseconds == 77,
					"entering a zero-threshold level preserves the accumulated award") && ok;
				++cases;
			}
			if (terminalLevel == 3)
			{
				auto* info = reset(1, 0);
				if (!check(info != nullptr, "learn the multi-level terminal fixture") || !award(500)) return false;
				ok = check(info->level == 3 && info->exp == 500,
					"continuous advancement stops at the terminal level without discarding excess experience") && ok;
				++cases;
			}
		}
	}
	std::cout << "Terminal magic experience:\troutes=3\tcases=" << cases << "\tpassed=" << ok << std::endl;
	return ok;
}

bool runExperienceSaturationTest(GameManager& gameManager)
{
	const int savedPlayerExperience = gameManager.player->exp;
	const int savedPlayerLevel = gameManager.player->level;
	const int savedPlayerLevelUpExperience =
		gameManager.player->levelUpExp;
	const std::vector<LevelInfo> savedPlayerLevels =
		gameManager.player->levelList;
	LevelInfo maximumThresholdLevel;
	maximumThresholdLevel.levelUpExp =
		std::numeric_limits<int>::max();
	gameManager.player->levelList = { maximumThresholdLevel };
	gameManager.player->level = 1;
	gameManager.player->levelUpExp =
		std::numeric_limits<int>::max();
	gameManager.player->exp =
		std::numeric_limits<int>::max() - 4;
	gameManager.player->addExp(10);
	bool ok = check(
		gameManager.player->exp
			== std::numeric_limits<int>::max(),
		"player experience saturates at the integer maximum");
	gameManager.player->exp =
		std::numeric_limits<int>::min() + 4;
	gameManager.player->addExp(-10);
	ok = check(
		gameManager.player->exp
			== std::numeric_limits<int>::min(),
		"negative player experience changes saturate without signed underflow") && ok;
	gameManager.player->exp = savedPlayerExperience;
	gameManager.player->level = savedPlayerLevel;
	gameManager.player->levelUpExp = savedPlayerLevelUpExperience;
	gameManager.player->levelList = savedPlayerLevels;

	gameManager.magicManager.configureLayout();
	if (gameManager.menu->practiceMenu == nullptr)
	{
		gameManager.menu->practiceMenu =
			std::make_shared<PracticeMenu>();
	}
	auto experienceMagic = std::make_shared<Magic>();
	experienceMagic->initFromIni("parent.ini");
	if (!check(
		experienceMagic->loadSucceeded,
		"experience saturation fixture Magic loads"))
	{
		return false;
	}

	const int practiceIndex =
		gameManager.magicManager.practiceIndex();
	// Exercise arithmetic with a positive threshold; terminal gating has its own matrix.
	experienceMagic->level[MAGIC_MAX_LEVEL].levelupExp = (std::numeric_limits<int>::max)();
	MagicInfo practiceInfo;
	practiceInfo.iniFile = "practice-saturation.ini";
	practiceInfo.level = MAGIC_MAX_LEVEL;
	practiceInfo.exp = std::numeric_limits<int>::max() - 4;
	practiceInfo.magic = experienceMagic;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)] = practiceInfo;
	gameManager.magicManager.addPracticeExp(10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(practiceIndex)].exp
			== std::numeric_limits<int>::max(),
		"practice Magic experience saturates at the integer maximum") && ok;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)].exp =
		std::numeric_limits<int>::min() + 4;
	gameManager.magicManager.addPracticeExp(-10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(practiceIndex)].exp
			== std::numeric_limits<int>::min(),
		"practice Magic experience saturates without signed underflow") && ok;

	const int useIndex = gameManager.magicManager.bottomIndex(0);
	MagicInfo useInfo;
	useInfo.iniFile = "use-saturation.ini";
	useInfo.level = MAGIC_MAX_LEVEL;
	useInfo.exp = std::numeric_limits<int>::max() - 4;
	useInfo.magic = experienceMagic;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(useIndex)] = useInfo;
	auto useEffect = std::make_shared<Effect>();
	useEffect->magic.experienceOwnerMagicFile =
		"use-saturation.ini";
	gameManager.magicManager.addUseExp(useEffect, 10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(useIndex)].exp
			== std::numeric_limits<int>::max(),
		"used Magic experience saturates at the integer maximum") && ok;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(useIndex)].exp =
		std::numeric_limits<int>::min() + 4;
	gameManager.magicManager.addMagicExp(
		"use-saturation.ini", -10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(useIndex)].exp
			== std::numeric_limits<int>::min(),
		"scripted Magic experience saturates without signed underflow") && ok;
	return ok;
}

bool runResourceConfiguredMinimumMagicDamageTest(GameManager& gameManager)
{
	const int originalMinimumMagicDamage =
		gameManager.global.minimumMagicDamage;
	NPC target;
	target.defend = 100;
	target.defend2 = 100;
	target.defend3 = 100;
	auto effect = std::make_shared<Effect>();
	effect->damage = 1;
	effect->damage2 = 1;
	effect->damage3 = 1;

	bool ok = true;
	for (int gameType : { GAME_JXQY2, GAME_YYCS, GAME_XJXQY, GAME_CUSTOM })
	{
		ResourceManifest behavior;
		behavior.type = gameType;
		behavior.typeDefined = true;
		behavior.minimumMagicDamage = 5;
		behavior.minimumMagicDamageDefined = true;
		gameManager.global.applyResourceManifestFeatures(behavior);
		ok = check(
			target.calculateEffectDamage(effect) == 5,
			"resource-configured minimum magic damage is independent from game type") && ok;
	}
	gameManager.global.minimumMagicDamage = 10;
	ok = check(
		target.calculateEffectDamage(effect) == 10,
		"resource configuration can restore a minimum magic damage of ten") && ok;

	target.defend = 3;
	target.defend2 = 4;
	target.defend3 = 10;
	effect->damage = 20;
	effect->damage2 = 9;
	effect->damage3 = 8;
	gameManager.global.minimumMagicDamage = 5;
	ok = check(
		target.calculateEffectDamage(effect) == 22,
		"configured minimum does not replace a larger calculated damage") && ok;
	gameManager.global.minimumMagicDamage = originalMinimumMagicDamage;
	return ok;
}

bool runResourceConfiguredMagicEffectCalculationTest(GameManager& gameManager)
{
	const MagicEffectCalculationMode originalMode =
		gameManager.global.magicEffectCalculationMode;
	const int originalAttack = gameManager.player->attack;
	gameManager.player->attack = 160;
	const int effectiveAttack = gameManager.player->getAttack();
	auto magic = std::make_shared<Magic>();
	magic->level[1].effect = 80;

	gameManager.global.magicEffectCalculationMode =
		MagicEffectCalculationMode::ReplaceAttack;
	bool ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == 80,
		"replace mode uses the player's configured magic effect");

	gameManager.global.magicEffectCalculationMode =
		MagicEffectCalculationMode::AddToAttack;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == effectiveAttack + 80,
		"additive mode combines the player's attack and magic effect") && ok;
	magic->level[1].effect = -220;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == effectiveAttack - 220,
		"additive mode preserves signed higher-level magic adjustments") && ok;
	magic->level[1].effect = 0;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == effectiveAttack,
		"zero magic effect continues to use the player's attack") && ok;

	magic->level[1].effect = 80;
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskAddLife;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == 80,
		"self healing keeps its explicit effect in additive mode") && ok;

	auto enemy = std::make_shared<NPC>();
	enemy->kind = nkBattle;
	enemy->attack = 75;
	magic->level[1].moveKind = mmkPoint;
	magic->level[1].specialKind = 0;
	ok = check(
		Magic::calculatePrimaryEffectAmount(magic, enemy, 1) == 75,
		"NPC magic continues to use the NPC attack in additive mode") && ok;

	gameManager.player->attack = originalAttack;
	gameManager.global.magicEffectCalculationMode = originalMode;
	return ok;
}

bool runFriendlySelfMagicTargetTest(GameManager& gameManager)
{
	const auto originalPlayer = gameManager.player;
	auto player = std::make_shared<Player>();
	gameManager.player = player;
	const auto originalNpcs = gameManager.npcManager->npcList;
	const auto originalEffects = gameManager.effectManager->effectList;
	player->info.lifeMax = 100;
	player->life = 20;

	auto recipient = std::make_shared<NPC>();
	recipient->npcName = "FRIENDLY_SELF_MAGIC_RECIPIENT";
	recipient->kind = nkPartner;
	recipient->relation = nrFriendly;
	recipient->lifeMax = 100;
	recipient->life = 30;
	recipient->setPosition({ 12, 12 }, false);
	gameManager.npcManager->npcList = { recipient };

	auto magic = std::make_shared<Magic>();
	magic->iniName = "friendly-self-magic.ini";
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskAddLife;
	magic->level[1].effect = 25;
	magic->level[1].effectExt = 5;
	const auto cast = [&](std::shared_ptr<GameElement> target)
	{
		return Magic::addEffect(magic, player, player->getPosition(),
			recipient->getPosition(), 1, 25, 0, lkSelf, target);
	};

	auto effects = cast(recipient);
	bool ok = check(player->life == 20 && recipient->life == 60,
		"a player self-heal with an explicit friendly partner target heals that partner")
		&& check(effects.size() == 1 && effects.front()->user.lock() == recipient
			&& effects.front()->position == recipient->getPosition(),
			"the friendly recipient owns the attached effect and its position");

	for (int specialKind = mskAddLife; specialKind <= mskChangeFlyIni; ++specialKind)
	{
		recipient->invisibleMilliseconds = 0;
		recipient->shieldEffects.clear();
		recipient->life = 30;
		magic->level[1].specialKind = specialKind;
		effects = cast(recipient);
		ok = check(effects.size() == 1 && effects.front()->user.lock() == recipient,
			"all nine CSharp friendly self-effect kinds attach to the explicit recipient") && ok;
	}

	magic->level[1].specialKind = mskAddLife;
	for (int kind : { nkBattle, nkPartner })
	{
		recipient->kind = kind;
		recipient->relation = nrFriendly;
		recipient->life = 30;
		player->life = 20;
		cast(recipient);
		ok = check(recipient->life == 60 && player->life == 20,
			"friendly fighters and partners both accept a player's self-effect target") && ok;
	}

	for (int relation : { nrNeutral, nrHostile })
	{
		recipient->relation = relation;
		recipient->life = 30;
		player->life = 20;
		cast(recipient);
		ok = check(recipient->life == 30 && player->life == 50,
			"neutral and hostile targets preserve the original self recipient") && ok;
	}

	recipient->relation = nrFriendly;
	recipient->life = 0;
	player->life = 20;
	cast(recipient);
	ok = check(recipient->life == 0 && player->life == 50,
		"a dead friendly target is never revived by redirecting a self effect") && ok;
	recipient->life = 30;
	recipient->nowAction = acHide;
	player->life = 20;
	cast(recipient);
	ok = check(recipient->life == 30 && player->life == 50,
		"a hidden friendly target cannot receive a redirected effect") && ok;
	recipient->nowAction = acStand;
	gameManager.npcManager->npcList.clear();
	player->life = 20;
	cast(recipient);
	ok = check(recipient->life == 30 && player->life == 50,
		"a retained pointer to a removed character cannot receive a redirected effect") && ok;
	gameManager.npcManager->npcList = { recipient };

	player->life = 20;
	cast(nullptr);
	ok = check(player->life == 50 && recipient->life == 30,
		"a self effect without a target continues to affect the caster") && ok;
	player->life = 20;
	recipient->life = 30;
	Magic::addEffect(magic, recipient, recipient->getPosition(), player->getPosition(),
		1, 25, 0, lkFriend, player);
	ok = check(recipient->life == 60 && player->life == 20,
		"an NPC caster keeps its own self effect instead of redirecting to another actor") && ok;

	magic->level[1].moveKind = mmkTimeStop;
	effects = cast(recipient);
	ok = check(effects.size() == 1 && effects.front()->user.lock() == player,
		"time-stop effects retain their caster even with an explicit friendly target") && ok;
	magic->level[1].moveKind = mmkSelf;
	effects = Magic::addSelfEffect(magic, player, player->getPosition(),
		player->getPosition(), 1, 0, 0, lkSelf, 0);
	ok = check(effects.size() == 1 && effects.front()->user.lock() == player,
		"direct self-effect callers such as level-up effects retain their original owner") && ok;

	gameManager.effectManager->effectList = originalEffects;
	gameManager.npcManager->npcList = originalNpcs;
	gameManager.player = originalPlayer;
	return ok;
}

bool runSelfMagicRecoveryAmountsTest(GameManager& gameManager, const std::filesystem::path& root)
{
	// 剑之缘 1 高清清心术使用 EffectExt，未填写 Effect。
	if (!check(writeTextFile(root / "ini/magic/zero-recovery.ini",
		"[Init]\nName=ZeroRecovery\nMoveKind=13\nSpecialKind=1\n"
		"[Level1]\nEffectExt=25\nManaCost=6\n"
		"[Level2]\nEffectExt=40\nManaCost=8\n"), "write the actual zero-Effect recovery definition")) return false;
	const auto originalPlayer = gameManager.player;
	const auto originalNpcs = gameManager.npcManager->npcList;
	const auto originalEffects = gameManager.effectManager->effectList;
	const auto originalMode = gameManager.global.magicEffectCalculationMode;
	auto player = std::make_shared<Player>();
	player->info.attack = 90;
	player->info.lifeMax = player->info.thewMax = 1000;
	gameManager.player = player;
	auto recipient = std::make_shared<NPC>();
	recipient->kind = nkPartner;
	recipient->relation = nrFriendly;
	recipient->attack = 40;
	recipient->lifeMax = recipient->thewMax = 1000;
	gameManager.npcManager->npcList = { recipient };
	auto magic = std::make_shared<Magic>();
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].effectExt = 5;
	bool ok = true;
	for (auto mode : { MagicEffectCalculationMode::ReplaceAttack, MagicEffectCalculationMode::AddToAttack })
	{
		gameManager.global.magicEffectCalculationMode = mode;
		for (int specialKind : { mskAddLife, mskAddThew })
		{
			magic->level[1].specialKind = specialKind;
			for (int effect : { 0, 25, -10 })
			{
				magic->level[1].effect = effect;
				for (bool useFriendlyTarget : { false, true })
				{
					player->life = player->thew = 300;
					recipient->life = recipient->thew = 200;
					auto beneficiary = useFriendlyTarget ? recipient : std::static_pointer_cast<NPC>(player);
					const int amount = (effect == 0 ? beneficiary->getAttack() : effect) + 5;
					auto effects = Magic::addEffect(magic, player, player->getPosition(),
						recipient->getPosition(), 1, Magic::calculatePrimaryEffectAmount(magic, player, 1),
						0, lkSelf, useFriendlyTarget ? recipient : nullptr);
					const int expected = (useFriendlyTarget ? 200 : 300) + amount;
					ok = check((specialKind == mskAddLife ? beneficiary->life : beneficiary->thew) == expected,
						"self and friendly recovery use the beneficiary attack for zero Effect, and add EffectExt once") && ok;
					ok = check(effects.size() == 1 && effects.front()->damage == amount
						&& effects.front()->user.lock() == beneficiary,
						"the attached recovery effect records the same amount and beneficiary as its settlement") && ok;
					ok = check(useFriendlyTarget ? player->life == 300 && player->thew == 300
						: recipient->life == 200 && recipient->thew == 200,
						"recovery never changes the other character's life or stamina") && ok;
				}
			}
			magic->level[1].effect = 0;
			recipient->life = recipient->thew = 200;
			Magic::addEffect(magic, recipient, recipient->getPosition(), player->getPosition(), 1,
				Magic::calculatePrimaryEffectAmount(magic, recipient, 1), 0, lkFriend, player);
			ok = check((specialKind == mskAddLife ? recipient->life : recipient->thew) == 245,
				"an NPC's zero-Effect recovery continues to use its own attack") && ok;
		}
	}
	magic->level[1].specialKind = mskAddLife;
	magic->sideEffectProbability = 100;
	magic->sideEffectPercent = 100;
	magic->sideEffectType = 1;
	player->info.manaMax = 1000;
	player->mana = 300;
	recipient->life = 200;
	Magic::addEffect(magic, player, player->getPosition(), recipient->getPosition(), 1,
		Magic::calculatePrimaryEffectAmount(magic, player, 1), 0, lkSelf, recipient);
	ok = check(recipient->life == 245 && player->mana == 205,
		"friendly zero-Effect recovery keeps the caster amount for the caster's side effect") && ok;
	auto loadedMagic = std::make_shared<Magic>();
	loadedMagic->initFromIni("zero-recovery.ini");
	ok = check(loadedMagic->loadSucceeded && loadedMagic->level[1].effect == 0
		&& loadedMagic->level[1].effectExt == 25 && loadedMagic->level[2].effectExt == 40,
		"the actual MOD layout keeps an omitted Effect at zero and loads each level's EffectExt") && ok;
	for (int level : { 1, 2 })
	{
		recipient->life = 200;
		Magic::addEffect(loadedMagic, player, player->getPosition(), recipient->getPosition(), level,
			Magic::calculatePrimaryEffectAmount(loadedMagic, player, level), 0, lkSelf, recipient);
		ok = check(recipient->life == (level == 1 ? 265 : 280),
			"the loaded clear-mind levels restore recipient attack plus the matching extension") && ok;
	}
	magic->level[1].specialKind = mskAddShield;
	ok = check(Magic::calculatePrimaryEffectAmount(magic, player, 1) == 0,
		"a zero shield amount is not converted into attack-based recovery") && ok;
	gameManager.effectManager->effectList = originalEffects;
	gameManager.npcManager->npcList = originalNpcs;
	gameManager.player = originalPlayer;
	gameManager.global.magicEffectCalculationMode = originalMode;
	return ok;
}

bool runFriendlySelfMagicReleaseTest(GameManager& gameManager)
{
	const auto originalPlayer = gameManager.player;
	const auto originalNpcs = gameManager.npcManager->npcList;
	const auto originalEffects = gameManager.effectManager->effectList;
	auto player = std::make_shared<Player>();
	gameManager.player = player;
	player->setPosition({ 10, 10 }, false);
	player->info.lifeMax = player->info.manaMax = player->info.thewMax = 100;
	auto image = std::make_shared<IMPImage>();
	image->directions = 8;
	image->interval = 40;
	image->frame.resize(8);
	player->res.magic.imagePackage = player->res.stand.imagePackage = image;
	auto magic = std::make_shared<Magic>();
	magic->iniName = "friendly-release.ini";
	magic->coldMilliSeconds = 400;
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskAddLife;
	magic->level[1].effect = 25;
	magic->level[1].effectExt = 5;
	magic->level[1].manaCost = 7;
	magic->level[1].thewCost = 5;
	magic->level[1].lifeCost = 3;
	const int slot = gameManager.magicManager.bottomIndex(0);
	const auto originalInfo = gameManager.magicManager.magicList[slot];
	auto& info = gameManager.magicManager.magicList[slot];
	info = {};
	info.magic = magic;
	info.level = 1;
	bool ok = true;
	for (int invalidation = -1; invalidation < 5; ++invalidation)
	{
		player->life = player->mana = player->thew = 80;
		info.remainColdMilliseconds = 0;
		gameManager.effectManager->effectList.clear();
		auto recipient = std::make_shared<NPC>();
		recipient->kind = nkPartner;
		recipient->relation = nrFriendly;
		recipient->lifeMax = 100;
		recipient->life = 30;
		recipient->setPosition({ 12, 12 }, false);
		gameManager.npcManager->npcList = { recipient };
		player->beginMagic(info, recipient->getPosition(), recipient, slot);
		ok = check(player->isMagicing() && player->mana == 80,
			"a friendly cast queues its animation before charging resources") && ok;
		if (invalidation == 0) recipient->life = 0;
		if (invalidation == 1) recipient->nowAction = acHide;
		if (invalidation == 2) gameManager.npcManager->npcList.clear();
		if (invalidation == 3) recipient->relation = nrNeutral;
		if (invalidation == 4)
		{
			gameManager.npcManager->npcList.clear();
			recipient.reset();
		}
		RageSystemTestAccess::advancePlayerMagic(*player);
		if (invalidation < 0)
		{
			ok = check(player->life == 77 && player->mana == 73 && player->thew == 75
				&& recipient->life == 60 && info.remainColdMilliseconds == 400,
				"the caster pays once and starts cooldown while the friendly target receives healing") && ok;
		}
		else
		{
			ok = check(player->life == 80 && player->mana == 80 && player->thew == 80
				&& info.remainColdMilliseconds == 0 && gameManager.effectManager->effectList.empty(),
				"a friendly target invalidated during the animation cancels without cost, cooldown or self healing") && ok;
		}
	}
	for (int specialKind : { mskAddLife, mskClearAbnormalState })
	{
		magic->level[1].specialKind = specialKind;
		info.remainColdMilliseconds = 0;
		auto recipient = std::make_shared<NPC>();
		recipient->kind = nkPartner;
		recipient->life = 30;
		gameManager.npcManager->npcList = { recipient };
		NextAction action;
		action.action = acMagic;
		action.actionParam = 0;
		action.dest = recipient->getPosition();
		action.destGE = recipient;
		ok = check(player->addNextAction(action), "queue a friendly skill through the shared input action path") && ok;
		gameManager.npcManager->npcList.clear();
		RageSystemTestAccess::updatePlayer(*player);
		ok = check(!player->isMagicing() && player->life == 80 && player->mana == 80
			&& info.remainColdMilliseconds == 0 && gameManager.effectManager->effectList.empty(),
			"a queued friendly cast, including immediate cleanse, cancels if its target leaves before animation") && ok;
	}
	gameManager.magicManager.magicList[slot] = originalInfo;
	gameManager.effectManager->effectList = originalEffects;
	gameManager.npcManager->npcList = originalNpcs;
	gameManager.player = originalPlayer;
	return ok;
}

bool runSelfLifeExchangeTest(GameManager& gameManager)
{
	auto player = gameManager.player;
	const PlayerInfo originalInfo = player->info;
	const int originalLife = player->life;
	const int originalMana = player->mana;
	const int originalInvincible = player->invincible;
	const NPCActionType originalAction = player->nowAction;
	const auto originalStateMenu = gameManager.menu->stateMenu;
	if (gameManager.menu->stateMenu == nullptr)
	{
		gameManager.menu->stateMenu = std::make_shared<StateMenu>();
	}

	player->info.lifeMax = 2000;
	player->info.manaMax = 1000;
	player->life = 600;
	player->mana = 0;
	player->invincible = 1;
	player->nowAction = NPCActionType::acStand;

	auto magic = std::make_shared<Magic>();
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskAddLife;
	magic->level[1].effect = -500;
	magic->level[1].manaCost = -60;

	player->mana = 980;
	bool ok = check(player->tryConsumeMagicCost(magic, 1, false)
		&& player->mana == 1000,
		"negative ManaCost restoration clamps at the player's mana maximum");
	player->mana = 0;
	ok = check(player->tryConsumeMagicCost(magic, 1, false)
		&& player->mana == 60,
		"negative ManaCost restores mana for a life-exchange skill") && ok;
	const Point position = player->getPosition();
	const auto firstEffects = Magic::addEffect(
		magic, player, position, position, 1, -500, 0, lkSelf, player);
	ok = check(firstEffects.size() == 1 && player->life == 100,
		"negative self life effect applies its exact value without damage scaling or invincibility") && ok;

	player->life = 400;
	const auto secondEffects = Magic::addEffect(
		magic, player, position, position, 1, -500, 0, lkSelf, player);
	ok = check(secondEffects.size() == 1 && player->life == 0
		&& player->nowAction != NPCActionType::acDeath,
		"life-exchange self effect clamps at zero without starting player death") && ok;

	player->info = originalInfo;
	player->life = originalLife;
	player->mana = originalMana;
	player->invincible = originalInvincible;
	player->nowAction = originalAction;
	gameManager.menu->stateMenu = originalStateMenu;
	return ok;
}

bool runPermanentNpcResourceDuringMorphTest(const std::filesystem::path& root)
{
	if (!check(writeTextFile(root / "ini/npcres/empty-model.ini", "[Stand]\nImage=\n"),
		"prepare a valid empty character resource for morph restoration")) return false;
	bool ok = true;
	for (int change = 0; change < 3; ++change)
	{
		auto actor = std::make_shared<NPC>();
		auto originalImage = std::make_shared<IMPImage>();
		originalImage->directions = 8;
		originalImage->interval = 40;
		originalImage->frame.resize(8);
		actor->res.stand.imagePackage = originalImage;
		Magic morph;
		morph.npcIni = "empty-model.ini";
		actor->applyTemporaryMorph(morph, 1000);
		ok = check(actor->res.stand.imagePackage == nullptr && actor->morphMilliseconds == 1000,
			"morph replaces the current image and retains a restoration deadline") && ok;
		if (change != 0)
		{
			actor->initRes(change == 1 ? "" : "empty-model.ini");
			ok = check(actor->res.stand.imagePackage == nullptr && actor->morphMilliseconds == 1000,
				"a permanent resource change applies without cancelling the morph's other effects") && ok;
		}
		actor->updateMagicRuntimeStateTimers(1000);
		ok = check(actor->morphMilliseconds == 0 && actor->morphMagic == nullptr
			&& actor->res.stand.imagePackage == (change == 0 ? originalImage : nullptr),
			"morph expiry restores its original image only when no permanent resource change superseded it") && ok;
	}
	return ok;
}

bool runPartnerCheatProtectionTest(GameManager& gameManager)
{
	const auto originalNpcs = gameManager.npcManager->npcList;
	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->life = 100;
	partner->res.hurt.imagePackage = std::make_shared<IMPImage>();
	partner->res.hurt.imagePackage->directions = 1;
	partner->res.hurt.imagePackage->frame.resize(1);
	partner->poisoned = partner->frozen = partner->petrified = partner->immobilized = true;
	partner->poisonedLastTime = partner->frozenLastTime = 10000;
	partner->petrifiedLastTime = partner->immobilizedLastTime = 10000;
	partner->disableMoveMilliseconds = partner->disableSkillMilliseconds = 10000;
	auto enemy = std::make_shared<NPC>();
	enemy->kind = nkBattle;
	enemy->relation = nrHostile;
	gameManager.npcManager->npcList = { partner, enemy };
	gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
	auto hasNoAbnormalState = [](const NPC& npc)
	{
		return !npc.frozen && !npc.poisoned && !npc.petrified && !npc.immobilized
			&& npc.frozenLastTime == 0 && npc.poisonedLastTime == 0
			&& npc.petrifiedLastTime == 0 && npc.immobilizedLastTime == 0
			&& npc.disableMoveMilliseconds == 0 && npc.disableSkillMilliseconds == 0;
	};
	bool ok = check(gameManager.isCheatInvincibilityEnabled() && hasNoAbnormalState(*partner),
		"enabling cheat invincibility immediately cures partner statuses and action locks");
	auto effect = std::make_shared<Effect>();
	effect->level = 1;
	effect->damage = 10000;
	effect->magic.disableMoveMilliseconds = effect->magic.disableSkillMilliseconds = 10000;
	partner->addLife(-10000);
	partner->hurtLife(10000, true);
	partner->hurt(effect, 0);
	partner->directHurt(effect);
	partner->applySideEffectDamage(0, 10000);
	partner->beginHurt();
	partner->beginHurt({1, 1});
	for (int status : {mskFreeze, mskPoison, mskPetrify, mskImmobilize})
	{
		effect->magic.level[1].specialKind = status;
		partner->applyPreDamageMagicStatus(*effect, 1);
		partner->applyEffectRuntimeStates(*effect);
	}
	ok = check(partner->life == 100 && !partner->isDying() && !partner->isHurting()
		&& partner->invincible == 0 && hasNoAbnormalState(*partner),
		"partner cheat protection covers lethal hits, direct damage, side effects, hurt actions and statuses without changing Invincible") && ok;

	class PartnerTestNPC : public NPC
	{
	public:
		using NPC::onUpdate;
	};
	auto latePartner = std::make_shared<PartnerTestNPC>();
	latePartner->kind = nkPartner;
	latePartner->life = 80;
	gameManager.npcManager->npcList.push_back(latePartner);
	latePartner->addLife(-10000);
	latePartner->poisoned = latePartner->immobilized = true;
	latePartner->poisonedLastTime = latePartner->immobilizedLastTime = 10000;
	latePartner->disableMoveMilliseconds = latePartner->disableSkillMilliseconds = 10000;
	latePartner->onUpdate();
	ok = check(latePartner->life == 80 && hasNoAbnormalState(*latePartner),
		"new partners gain protection immediately and their next update clears restored statuses") && ok;
	enemy->addLife(-10);
	enemy->applyPreDamageMagicStatus(*effect, 1);
	latePartner->kind = nkNormal;
	latePartner->addLife(-10);
	ok = check(enemy->life == 990 && enemy->immobilized && latePartner->life == 70,
		"enemies and companions that leave the party remain vulnerable") && ok;
	latePartner->kind = nkPartner;
	gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
	partner->addLife(-10);
	partner->applyPreDamageMagicStatus(*effect, 1);
	ok = check(partner->life == 90 && partner->immobilized,
		"turning invincibility off restores partner damage and statuses immediately") && ok;
	gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
	gameManager.setCheatModeEnabled(false);
	partner->addLife(-10);
	ok = check(partner->life == 80 && !partner->isProtectedByCheatInvincibility(),
		"turning cheat mode off also removes partner protection") && ok;
	gameManager.setCheatModeEnabled(true);
	gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
	latePartner->res.death.imagePackage = std::make_shared<IMPImage>();
	latePartner->res.death.imagePackage->directions = 1;
	latePartner->res.death.imagePackage->frame.resize(1);
	latePartner->deathScript = "plot-death.txt";
	latePartner->life = 0;
	latePartner->handleDeath();
	ok = check(latePartner->isDying() && (latePartner->getResult() & erRunDeathScript),
		"explicit plot death still runs for a protected partner") && ok;
	gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);

	auto secondPartner = std::make_shared<NPC>();
	secondPartner->kind = nkPartner;
	secondPartner->life = 25;
	secondPartner->lifeMax = 200;
	secondPartner->mana = secondPartner->thew = 7;
	partner->equipmentAttributes.lifeMax = 50;
	auto hiddenPartner = std::make_shared<NPC>();
	hiddenPartner->kind = nkPartner;
	hiddenPartner->life = 30;
	hiddenPartner->nowAction = acHide;
	gameManager.npcManager->npcList.push_back(secondPartner);
	gameManager.npcManager->npcList.push_back(hiddenPartner);
	gameManager.performCheatAction(GameManager::CheatAction::RestorePlayerResources);
	ok = check(partner->life == 1050 && secondPartner->life == 200
		&& secondPartner->mana == 7 && secondPartner->thew == 7
		&& latePartner->life == 0 && hiddenPartner->life == 30 && enemy->life == 990,
		"resource restoration heals all living partners to their equipped maximum without reviving dead or hidden actors or healing enemies") && ok;
	gameManager.npcManager->npcList = originalNpcs;
	return ok;
}
}

bool runMagicDerivedRuntimeTests()
{
	auto root = makeUniqueTestDirectory("jxqy_magic_derived_runtime_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "magic", errorCode);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(MagicDerivedSaveNamespace);
	File::setPlatformStateParentForTests(root.string());

	Magic copiedParent;
	auto firstFallbackRoot = root / "dependency_fallback_first";
	auto secondFallbackRoot = root / "dependency_fallback_second";
	bool ok = runEffectProjectedDirectionTest();
	ok = runEffectPathWidthTest() && ok;
	ok = runLinkedMagicGraphLoadingTest(root, firstFallbackRoot, secondFallbackRoot) && ok;
	ok = runExplodeMagicLevelLoadingTest(root, copiedParent) && ok;
	ok = runMagicLevelRuntimeTest(root) && ok;
	GameManager gameManager;
	ok = runEffectInitialDirectionTest(gameManager) && ok;
	ok = runExplicitAttackDistanceContract() && ok;
	ok = runSelfMagicLifetimeContract(gameManager) && ok;
	ok = runSelfMagicSelectionContract(gameManager) && ok;
	ok = runPositionCastRangeContract(gameManager) && ok;
	ok = runMagicAdmissionContract(gameManager) && ok;
	ok = runAutomaticSelfMagicAdmissionContract(gameManager) && ok;
	ok = runInvisibleRangeContract(gameManager) && ok;
	ok = runCarryFlyMagicDamageContract(gameManager) && ok;
	ok = runCarryWallCollisionContract(gameManager) && ok;
	ok = runCarriedVictimTranslationContract(gameManager) && ok;
	ok = runResourceConfiguredMinimumMagicDamageTest(gameManager) && ok;
	ok = runResourceConfiguredMagicEffectCalculationTest(gameManager) && ok;
	ok = runFriendlySelfMagicTargetTest(gameManager) && ok;
	ok = runSelfMagicRecoveryAmountsTest(gameManager, root) && ok;
	ok = runFriendlySelfMagicReleaseTest(gameManager) && ok;
	ok = runPermanentNpcResourceDuringMorphTest(root) && ok;
	ok = runSelfLifeExchangeTest(gameManager) && ok;
	ok = runLinkedMagicRuntimeBudgetTest(gameManager) && ok;
	ok = runRageSystemTest(gameManager, root) && ok;
	ok = runInsufficientResourceMessageTest(gameManager) && ok;
	ok = runExplodeMagicLevelDispatchTest(gameManager, copiedParent) && ok;
	ok = runLinkedMagicLevelDispatchTest(gameManager, copiedParent) && ok;
	ok = runEffectReferencePersistenceTest(gameManager) && ok;
	ok = runDerivedExperienceTest(gameManager, copiedParent) && ok;
	ok = runPublishedIniCommentContract(root) && ok;
	ok = runWarningRegionContract(gameManager, root) && ok;
	ok = runDirectDamageShieldTest(gameManager) && ok;

	File::setPlatformStateParentForTests("");
	std::filesystem::remove_all(root, errorCode);
	return ok;
}

bool runReplacementSelectionExperienceTests()
{
	GameManager gameManager;
	auto& manager = gameManager.magicManager;
	auto player = gameManager.player;
	const int toolbar = manager.bottomBegin();
	const auto learnAt = [&](const char* name, int slot, int experience)
	{
		auto* info = manager.addPrimaryMagic(name, false, false);
		if (info == nullptr) return false;
		info->exp = experience;
		const int index = static_cast<int>(info - manager.magicList.data());
		manager.exchange(index, slot);
		return true;
	};
	if (!check(learnAt("cache-source.ini", toolbar, 101)
		&& learnAt("cache-other.ini", toolbar + 1, 202), "prepare primary toolbar controls")) return false;
	Magic firstForm;
	firstForm.name = "SelectionFirst";
	firstForm.replaceMagic = "cache-other.ini;cache-source.ini";
	Magic secondForm = firstForm;
	secondForm.name = "SelectionSecond";
	secondForm.replaceMagic = "cache-source.ini;cache-other.ini";
	const auto killExperience = [&]() { manager.addKillExp(nullptr, 40.0, 0.0f, 0.25f); };
	const auto experience = [&](const char* name) { const auto* info = manager.findMagic(name); return info ? info->exp : -1; };
	manager.recordCurrentUseMagic(toolbar);
	player->applyTemporaryMorph(firstForm, 1000);
	killExperience();
	bool ok = check(experience("cache-other.ini") == 10 && experience("cache-source.ini") == 0,
		"entering a replacement rebinds automatic experience to the same toolbar slot, not the old filename");
	player->applyTemporaryMorph(secondForm, 1000);
	killExperience();
	ok = check(experience("cache-source.ini") == 10 && experience("cache-other.ini") == 0,
		"switching replacement sources retains the toolbar slot with independent learned entries") && ok;
	player->applyTemporaryMorph(firstForm, 1000);
	killExperience();
	ok = check(experience("cache-other.ini") == 20 && experience("cache-source.ini") == 0,
		"returning to a cached arrangement rebinds current experience to that arrangement") && ok;
	manager.recordCurrentUseMagic(toolbar);
	ok = check(manager.save(3), "save an active form with a different primary skill in the selected slot") && ok;
	INIReader saved("save/game/magic3.ini");
	ok = check(saved.Get("Head", "CurrentUseMagicFile", "") == "cache-source.ini",
		"active-form saving records the primary skill occupying the selected slot") && ok;
	killExperience();
	ok = check(experience("cache-other.ini") == 30,
		"saving an active form does not mutate its current experience target") && ok;
	player->updateMagicRuntimeStateTimers(1000);
	killExperience();
	ok = check(experience("cache-source.ini") == 111 && experience("cache-other.ini") == 202,
		"form expiry rebinds automatic experience to the primary skill at the selected slot") && ok;
	ok = check(manager.load(3), "read the saved primary selection and independent caches") && ok;
	killExperience();
	ok = check(experience("cache-source.ini") == 111 && experience("cache-other.ini") == 202,
		"character-file readback restores primary selection, not a same-named skill in another slot") && ok;
	player->applyTemporaryMorph(firstForm, 1000);
	manager.recordCurrentUseMagic(toolbar + 1);
	const auto activeSource = manager.findMagic("cache-source.ini")->magic;
	auto* hiddenPrimary = manager.setMagicHidden("cache-source.ini", true, false, false);
	killExperience();
	ok = check(hiddenPrimary != nullptr && hiddenPrimary->exp == 111
		&& manager.isMagicHidden("cache-source.ini") && manager.findMagic("cache-source.ini")->magic == activeSource
		&& experience("cache-source.ini") == 10,
		"hiding the primary copy leaves the active same-named form selected and credits only its learned entry") && ok;
	auto* revealedPrimary = manager.setMagicHidden("cache-source.ini", false, false, false);
	ok = check(revealedPrimary != nullptr && revealedPrimary->exp == 111 && !manager.isMagicHidden("cache-source.ini")
		&& manager.findMagic("cache-source.ini")->magic == activeSource,
		"equipment reveal remains primary-only while the replacement retains its distinct object") && ok;
	Magic emptyForm = firstForm;
	emptyForm.replaceMagic = u8"无";
	manager.recordCurrentUseMagic(toolbar + 1);
	player->applyTemporaryMorph(emptyForm, 1000);
	player->applyTemporaryMorph(firstForm, 1000);
	const int beforeUnselected = experience("cache-source.ini");
	killExperience();
	ok = check(experience("cache-source.ini") == beforeUnselected,
		"an empty replacement clears selection and cannot revive an old filename on the next form") && ok;
	player->clearMagicRuntimeStates();
	manager.recordCurrentUseMagic(toolbar);
	manager.setMagicHidden("cache-source.ini", true, false, false);
	manager.setMagicHidden("cache-source.ini", false, false, false);
	const int primaryBefore = experience("cache-source.ini");
	killExperience();
	ok = check(experience("cache-source.ini") == primaryBefore,
		"hiding the actually active primary skill still clears current-use experience selection") && ok;
	player->applyTemporaryMorph(firstForm, 1000);
	const auto disabledSelection = manager.magicList[toolbar].magic;
	disabledSelection->disableUse = 1;
	const int disabledBefore = manager.magicList[toolbar].exp;
	player->applyTemporaryMorph(secondForm, 1000);
	manager.recordCurrentUseMagic(toolbar);
	player->applyTemporaryMorph(firstForm, 1000);
	killExperience();
	ok = check(manager.magicList[toolbar].exp == disabledBefore,
		"form rebinding does not select a DisableUse skill for automatic experience") && ok;
	manager.finishMagicUse(disabledSelection, 300, true);
	killExperience();
	ok = check(manager.magicList[toolbar].exp == disabledBefore,
		"late completion also respects the player's disabled current-selection rule") && ok;
	disabledSelection->disableUse = 0;
	player->clearMagicRuntimeStates();
	Magic duplicateForm = firstForm;
	duplicateForm.name = "SelectionDuplicates";
	duplicateForm.replaceMagic = "cache-source.ini;cache-source.ini;cache-other.ini";
	player->applyTemporaryMorph(duplicateForm, 1000);
	manager.recordCurrentUseMagic(toolbar + 1);
	const auto duplicateSource = manager.magicList[toolbar + 1].magic;
	killExperience();
	ok = check(manager.magicList[toolbar].exp == 0 && manager.magicList[toolbar + 1].exp == 10,
		"same-file duplicate entries receive current-use experience by learned object, not first filename match") && ok;
	manager.exchange(toolbar, manager.storeBegin());
	killExperience();
	ok = check(manager.magicList[toolbar + 1].exp == 20 && manager.magicList[manager.storeBegin()].exp == 0,
		"moving an unselected same-file copy out of the toolbar does not clear the selected object") && ok;
	player->applyTemporaryMorph(secondForm, 1000);
	const int otherBefore = experience("cache-other.ini");
	killExperience();
	ok = check(experience("cache-other.ini") == otherBefore + 10,
		"leaving the second duplicate retains its exact toolbar slot when rebinding another form") && ok;
	const int activeBeforeCompletion = experience("cache-source.ini");
	manager.finishMagicUse(duplicateSource, 700, true);
	killExperience();
	ok = check(experience("cache-source.ini") == activeBeforeCompletion,
		"late cast completion records its inactive learned owner rather than the active same-file skill") && ok;
	player->applyTemporaryMorph(duplicateForm, 1000);
	ok = check(manager.magicList[toolbar + 1].exp == 30 && manager.magicList[toolbar + 1].remainColdMilliseconds == 700,
		"the inactive duplicate retains both late completion cooldown and current-use experience") && ok;
	player->clearMagicRuntimeStates();
	// File-backed duplicate primary entries also need an unambiguous save index.
	const std::string duplicateSave = "[Head]\nCount=2\nCurrentUseMagicFile=cache-source.ini\n"
		+ std::string("[") + std::to_string(toolbar + 1) + "]\nIniFile=cache-source.ini\nLevel=1\nExp=41\n["
		+ std::to_string(toolbar + 2) + "]\nIniFile=cache-source.ini\nLevel=1\nExp=82\n";
	for (const std::string& indexValue : std::vector<std::string>{ "", "garbage", "999999", "0", std::to_string(toolbar + 2) })
	{
		const std::string indexField = indexValue.empty() ? "" : "CurrentUseMagicIndex=" + indexValue + "\n";
		std::string content = duplicateSave;
		content.insert(content.find("Count=2"), indexField);
		File::writeFile("save/game/magic3.ini", content.data(), static_cast<int>(content.size()));
		INIReader written("save/game/magic3.ini");
		if (!check(written.Get("Head", "CurrentUseMagicIndex", "") == indexValue
			&& manager.load(3), "optional current-use index accepts legacy, unreadable, out-of-range and explicit-none values")) return false;
		killExperience();
		const bool second = indexValue == std::to_string(toolbar + 2);
		ok = check(manager.magicList[toolbar].exp == (indexValue != "0" && !second ? 51 : 41)
			&& manager.magicList[toolbar + 1].exp == (second ? 92 : 82),
			"optional selection index preserves filename fallback without rejecting an otherwise valid save") && ok;
	}
	manager.recordCurrentUseMagic(toolbar + 1);
	if (!check(manager.save(3), "save the selected second primary duplicate")) return false;
	INIReader duplicateSaved("save/game/magic3.ini");
	ok = check(duplicateSaved.GetInteger("Head", "CurrentUseMagicIndex", -1) == toolbar + 2,
		"new saves write the exact one-based selected entry alongside the legacy filename") && ok;
	if (!check(manager.load(3), "reload the selected primary duplicate")) return false;
	killExperience();
	ok = check(manager.magicList[toolbar].exp == 41 && manager.magicList[toolbar + 1].exp == 102,
		"save and load keep current-use experience on the second same-file entry") && ok;
	std::cout << "Replacement selection experience:\tforms=4\tprimaryHidden=1\toptionalIndexCases=5\tpassed=" << ok << std::endl;
	return ok;
}

bool runReplacementExperienceOwnershipTests()
{
	const auto root = makeUniqueTestDirectory("jxqy_replacement_experience_test");
	File::setPlatformStateParentForTests(root.string());
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(MagicDerivedSaveNamespace);
	if (!check(writeTextFile(root / "ini/magic/cache-source.ini",
		"[Init]\nName=CacheSource\nMoveKind=1\nLevelUpExp=1000\nSpeed=8\n")
		&& writeTextFile(root / "ini/magic/cache-other.ini",
			"[Init]\nName=CacheOther\nMoveKind=1\nLevelUpExp=1000\nSpeed=8\n")
		&& writeTextFile(root / "ini/magic/cache-child.ini",
			"[Init]\nName=CacheChild\nMoveKind=2\nEffect=20\nLifeFrame=100\nKeepMilliseconds=10000\n")
		&& writeTextFile(root / "ini/level/MagicExp.ini",
			"[HitMagicExp]\nLevelFactor=3\n[XiuLianMagicExp]\nFraction=0.5\n[UseMagicExp]\nFraction=0.25\n"),
		"write isolated replacement experience resources")) return false;
	bool ok = runReplacementSelectionExperienceTests();
	for (int transition : { 0, 1, 2 })
	{
		GameManager gameManager;
		auto& manager = gameManager.magicManager;
		auto player = gameManager.player;
		if (!check(manager.addPrimaryMagic("cache-source.ini", false, false) != nullptr,
			"learn an independent same-file primary control")) return false;
		Magic morph;
		morph.name = "ExperienceForm";
		const std::string firstList = "cache-source.ini;cache-other.ini";
		morph.replaceMagic = firstList;
		player->applyTemporaryMorph(morph, 1000);
		auto* original = manager.findMagic("cache-source.ini");
		if (!check(original != nullptr, "enter the originating replacement list")) return false;
		auto effect = std::make_shared<Effect>();
		effect->level = 1;
		effect->user = player;
		effect->initFromMagic(original->magic);
		effect->launcherKind = lkSelf;
		effect->damage = 20;
		if (transition == 1)
		{
			player->updateMagicRuntimeStateTimers(1000);
		}
		else if (transition == 2)
		{
			morph.replaceMagic = "cache-other.ini;cache-source.ini";
			player->applyTemporaryMorph(morph, 1000);
		}
		auto target = std::make_shared<NPC>();
		target->level = 3;
		target->lifeMax = target->life = 1000;
		target->relation = nrHostile;
		target->directHurt(effect);
		const int primaryExperience = manager.findPrimaryMagic("cache-source.ini")->exp;
		const int currentExperience = manager.findMagic("cache-source.ini")->exp;
		morph.replaceMagic = firstList;
		player->applyTemporaryMorph(morph, 1000);
		const int originalExperience = manager.findMagic("cache-source.ini")->exp;
		std::cout << "Replacement hit ownership:\ttransition=" << transition
			<< "\tdamage=" << 1000 - target->life << "\tprimary=" << primaryExperience
			<< "\tcurrent=" << currentExperience << "\torigin=" << originalExperience << std::endl;
		ok = check(target->life < 1000 && originalExperience == 9 && primaryExperience == 0
			&& (transition != 2 || currentExperience == 0),
			"hit experience belongs to the originating replacement even after expiry or list switching") && ok;
	}
	{
		GameManager gameManager;
		auto& manager = gameManager.magicManager;
		const std::string replacementList = "cache-source.ini;cache-other.ini";
		manager.addPrimaryMagic("cache-source.ini", false, false);
		manager.replaceMagicList(replacementList);
		const auto source = manager.findMagic("cache-source.ini")->magic;
		const auto parent = Magic::createRootDispatchContext(source);
		auto child = manager.loadAttackMagic("cache-child.ini");
		manager.stopReplaceMagicList();
		for (const char* relationship : { "FlyMagic", "ParasiticMagic", "JumpEndMagic", "ExplodeMagicFile" })
		{
			auto context = Magic::createDerivedDispatchContext(parent, child, relationship);
			auto effect = std::make_shared<Effect>();
			effect->level = 1;
			effect->initFromMagic(child, context);
			effect->user = gameManager.player;
			effect->launcherKind = lkSelf;
			effect->damage = 20;
			auto target = std::make_shared<NPC>();
			target->level = 3;
			target->lifeMax = target->life = 1000;
			target->directHurt(effect);
			ok = check(target->life == 980 && Magic::getExperienceOwner(context).magic.lock() == source,
				"published inherited child retains its originating learned object") && ok;
		}
		for (const char* relationship : { "SecondMagic", "RandMagic", "ChangeMagic", "CounterMagic" })
		{
			ok = check(!Magic::getExperienceOwner(Magic::createDerivedDispatchContext(parent, child, relationship)).assigned,
				"independent derived casts do not inherit the parent's learned entry") && ok;
		}
		manager.replaceMagicList(replacementList);
		ok = check(manager.findMagic("cache-source.ini")->exp == 36
			&& manager.findPrimaryMagic("cache-source.ini")->exp == 0,
			"four inherited child hits credit the inactive cache exactly once each") && ok;
		auto equipment = std::make_shared<Goods>();
		equipment->kind = gkEquipment;
		equipment->part = "Hand";
		equipment->replaceMagic = "cache-source.ini";
		equipment->useReplaceMagic = "cache-child.ini";
		auto& equipmentInfo = gameManager.goodsManager.goodsList[gameManager.goodsManager.equipIndex(NPC::getEquipmentPartIndex("Hand"))];
		equipmentInfo.goods = equipment;
		equipmentInfo.iniFile = "cache-weapon.ini";
		equipmentInfo.number = 1;
		gameManager.player->calInfo();
		const auto prepared = gameManager.player->resolveMagicReplacement(source);
		manager.stopReplaceMagicList();
		const auto otherPrepared = gameManager.player->resolveMagicReplacement(manager.findPrimaryMagic("cache-source.ini")->magic);
		ok = check(prepared != otherPrepared && prepared != child
			&& prepared->experienceOwner.magic.lock() == source
			&& otherPrepared->experienceOwner.magic.lock() != source
			&& !child->experienceOwner.assigned,
			"equipment replacement snapshots per-cast origins without mutating the shared resource") && ok;
	}
	for (int originKind : { 0, 1, 2 })
	{
		GameManager gameManager;
		auto& manager = gameManager.magicManager;
		gameManager.global.data.characterIndex = 0;
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = 16;
		gameManager.map->data->head.height = 16;
		gameManager.map->data->tile.resize(16);
		for (auto& row : gameManager.map->data->tile) row.resize(16);
		gameManager.map->createDataMap();
		gameManager.player->setPosition({ 4, 4 }, false);
		manager.addPrimaryMagic("cache-source.ini", false, false);
		const std::string replacementList = "cache-source.ini;cache-other.ini";
		if (originKind == 2) manager.replaceMagicList(replacementList);
		auto source = manager.findMagic("cache-source.ini")->magic;
		auto context = Magic::createRootDispatchContext(source);
		if (originKind == 1) manager.setMagicHidden("cache-source.ini", true, false, false);
		const auto child = manager.loadAttackMagic("cache-child.ini");
		const auto childContext = Magic::createDerivedDispatchContext(context, child, "FlyMagic");
		auto effect = std::make_shared<Effect>();
		effect->level = 1;
		effect->initFromMagic(child, childContext);
		effect->fileName = child->iniName;
		effect->user = gameManager.player;
		effect->launcherKind = lkSelf;
		effect->damage = 20;
		gameManager.effectManager->addEffect(effect);
		gameManager.effectManager->addDelayedMagic(child, gameManager.player, { 4, 4 }, { 4, 5 }, 1, lkSelf, nullptr, 0, childContext);
		gameManager.effectManager->addTrailMagic(child, gameManager.player, 1, 20, 0, lkSelf, childContext);
		ok = check(manager.save(0) && gameManager.effectManager->save(), "save originating lists and active/delayed/trail effect files") && ok;
		std::cout << "Ownership recovery stage:\torigin=" << originKind << "\tstage=saved" << std::endl;
		gameManager.effectManager->clearEffect();
		ok = check(manager.load(0), "load new learned objects before effects") && ok;
		std::cout << "Ownership recovery stage:\torigin=" << originKind << "\tstage=magic-loaded" << std::endl;
		ok = check(gameManager.effectManager->load(), "load active/delayed/trail effects") && ok;
		std::cout << "Ownership recovery stage:\torigin=" << originKind << "\tstage=effects-loaded" << std::endl;
		if (originKind == 1) manager.setMagicHidden("cache-source.ini", false, false, false);
		if (originKind == 2) manager.replaceMagicList(replacementList);
		const auto restoredSource = manager.findMagic("cache-source.ini")->magic;
		ok = check(restoredSource != source, "save recovery uses new objects, not surviving pointers") && ok;
		manager.replaceMagicList("cache-other.ini;cache-source.ini");
		MagicDerivedRuntimeTestAccess::updateDelayedMagic(*gameManager.effectManager);
		std::cout << "Ownership recovery stage:\torigin=" << originKind << "\tstage=delayed-released" << std::endl;
		gameManager.player->setPosition({ 4, 5 }, false);
		MagicDerivedRuntimeTestAccess::updateTrailMagic(*gameManager.effectManager);
		std::cout << "Ownership recovery stage:\torigin=" << originKind << "\tstage=trail-released" << std::endl;
		ok = check(gameManager.effectManager->effectList.size() == 3,
			"loaded active, delayed and movement trail produce three actual effects") && ok;
		for (const auto& loaded : gameManager.effectManager->effectList)
		{
			std::cout << "Ownership recovered hit:\torigin=" << originKind << "\tdamage=" << loaded->damage
				<< "\tlevel=" << loaded->level << "\tmove=" << loaded->getMoveKind() << std::endl;
			ok = check(Magic::getExperienceOwner(loaded->magicDispatchContext).magic.lock() == restoredSource,
				"all recovered effects resolve the original list entry after another list switch") && ok;
			auto target = std::make_shared<NPC>();
			target->level = 3;
			target->lifeMax = target->life = 1000;
			target->directHurt(loaded);
			std::cout << "Ownership recovered hit complete:\tlife=" << target->life << std::endl;
			ok = check(target->life < 1000, "recovered effect still deals actual damage") && ok;
		}
		manager.stopReplaceMagicList();
		if (originKind == 2) manager.replaceMagicList(replacementList);
		ok = check(manager.findMagic("cache-source.ini")->exp == 27,
			"three recovered hits credit their original entry, including hidden and replacement saves") && ok;
		std::cout << "Experience owner file recovery:\torigin=" << originKind
			<< "\teffects=" << gameManager.effectManager->effectList.size()
			<< "\texp=" << manager.findMagic("cache-source.ini")->exp << std::endl;
		INIReader ownerIni;
		manager.saveExperienceOwner(ownerIni, "Origin", restoredSource->experienceOwner);
		ownerIni.SetInteger("Origin", "ExperienceOwnerCharacter", 1);
		ok = check(manager.loadExperienceOwner(ownerIni, "Origin").assigned
			&& manager.loadExperienceOwner(ownerIni, "Origin").magic.expired(),
			"another character's same-named entry is not accepted as the owner") && ok;
		ownerIni.SetInteger("Origin", "ExperienceOwnerCharacter", 0);
		ownerIni.SetInteger("Origin", "ExperienceOwnerSlot", -1);
		ok = check(manager.loadExperienceOwner(ownerIni, "Origin").assigned
			&& manager.loadExperienceOwner(ownerIni, "Origin").magic.expired(),
			"invalid optional owner slot is ignored without falling back to another entry") && ok;
		INIReader oldIni;
		ok = check(!manager.loadExperienceOwner(oldIni, "Origin").assigned, "older effects without optional ownership retain legacy handling") && ok;
		if (originKind == 2)
		{
			gameManager.effectManager->clearEffect();
			manager.stopReplaceMagicList();
			auto fullScreen = std::make_shared<Magic>(*child);
			fullScreen->level[1].moveKind = mmkFullScreen;
			auto fullScreenContext = Magic::createDerivedDispatchContext(
				Magic::createRootDispatchContext(restoredSource), fullScreen, "ExplodeMagicFile");
			auto target = std::make_shared<NPC>();
			target->kind = nkBattle;
			target->relation = nrHostile;
			target->level = 3;
			target->lifeMax = target->life = 1000;
			gameManager.npcManager->addNPC(target);
			target->setPosition({ 5, 5 }, false);
			Magic::addEffect(fullScreen, gameManager.player, { 4, 5 }, { 5, 5 }, 1, 20, 0, lkSelf, target, fullScreenContext);
			manager.replaceMagicList(replacementList);
			ok = check(target->life == 980 && manager.findMagic("cache-source.ini")->exp == 36
				&& manager.findPrimaryMagic("cache-source.ini")->exp == 0,
				"full-screen child credits the original entry during its internal damage phase") && ok;
			std::cout << "Full-screen experience ownership:\tdamage=" << 1000 - target->life
				<< "\texp=" << manager.findMagic("cache-source.ini")->exp << std::endl;
			auto block = std::make_shared<Effect>();
			block->level = 1;
			block->doing = ekFlying;
			block->magic.level[1].moveKind = mmkSelf;
			block->magic.level[1].specialKind = mskBlockDamage;
			target->shieldEffects = { block };
			Magic::addEffect(fullScreen, gameManager.player, { 4, 5 }, { 5, 5 }, 1, 20, 0, lkSelf, target, fullScreenContext);
			ok = check(target->life == 980 && manager.findMagic("cache-source.ini")->exp == 36,
				"full-screen immunity prevents damage and magic hit experience") && ok;
			block->vanishing = true;
			Magic::addEffect(fullScreen, gameManager.player, { 4, 5 }, { 5, 5 }, 1, 20, 0, lkSelf, target, fullScreenContext);
			ok = check(target->life == 960 && manager.findMagic("cache-source.ini")->exp == 45,
				"full-screen damage and hit experience resume after immunity expires") && ok;
			target->shieldEffects.clear();
			const auto casterOffset = PointEx{ 5.5f, -7.5f };
			auto hits = [&](Point tileDelta, PointEx extraOffset)
			{
				target->setPosition(gameManager.player->getPosition() + tileDelta, false);
				target->setOffset(casterOffset + extraOffset);
				target->life = 1000;
				Magic::addEffect(fullScreen, gameManager.player, gameManager.player->getPosition(),
					target->getPosition(), 1, 20, 0, lkSelf, target);
				return target->life == 980;
			};
			// No window or video initialization: only change the logical size used by the engine.
			for (Point size : { Point{ 640, 480 }, Point{ 1280, 720 }, Point{ 2560, 1440 } })
			{
				Engine::getInstance()->setWindowSize(size.x, size.y);
				gameManager.camera->position = { size.x, size.y };
				gameManager.camera->offset = { 120.5f, -42.5f };
				for (Point center : { Point{ 32, 32 }, Point{ 32, 33 } })
				{
					gameManager.player->setPosition(center, false);
					gameManager.player->setOffset(casterOffset);
					ok = check(hits({ 10, 0 }, { 0, 0 }) && !hits({ 10, 0 }, { 0.5f, 0 })
						&& hits({ -10, 0 }, { 0, 0 }) && !hits({ -10, 0 }, { -0.5f, 0 }),
						"fixed full-screen horizontal bounds are exactly plus/minus 640 including fractional movement") && ok;
					ok = check(hits({ 0, 30 }, { 0, 0 }) && !hits({ 0, 30 }, { 0, 0.5f })
						&& hits({ 0, -30 }, { 0, 0 }) && !hits({ 0, -30 }, { 0, -0.5f }),
						"fixed full-screen vertical bounds are exactly plus/minus 480") && ok;
					ok = check(hits({ 10, 30 }, { 0, 0 }) && hits({ -10, -30 }, { 0, 0 })
						&& hits({ -10, 30 }, { 0, 0 }) && hits({ 10, -30 }, { 0, 0 }),
						"all four corners are hit beyond the former twenty-tile radius at every resolution and row parity") && ok;
					ok = check(!hits({ 11, 0 }, { 0, 0 }) && !hits({ 0, 32 }, { 0, 0 }),
						"NPCs outside the rectangle are excluded even when within twenty tiles") && ok;
				}
			}
			target->relation = nrFriendly;
			ok = check(!hits({ 0, 0 }, { 0, 0 }), "fixed-area casts retain faction filtering") && ok;
			target->relation = nrHostile;
			target->scriptHidden = true;
			ok = check(!hits({ 0, 0 }, { 0, 0 }), "fixed-area casts exclude hidden NPCs") && ok;
			target->scriptHidden = false;
			target->actionManager->restartActionIgnoringTransitions(acDeath);
			ok = check(!hits({ 0, 0 }, { 0, 0 }), "fixed-area casts exclude dead NPCs") && ok;
			target->actionManager->restartActionIgnoringTransitions(acStand);
			target->setPosition({ 32, 32 }, false);
			target->setOffset(casterOffset);
			for (int horizontalTiles : { 10, 11 })
			{
				gameManager.player->setPosition({ 32 + horizontalTiles, 62 }, false);
				gameManager.player->life = 1000;
				Magic::addEffect(fullScreen, target, target->getPosition(), gameManager.player->getPosition(),
					1, 20, 0, lkEnemy, gameManager.player);
				ok = check((gameManager.player->life < 1000) == (horizontalTiles == 10),
					"enemy full-screen damage applies the same fixed rectangle to the player") && ok;
			}
			ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
				"fixed-area tests never initialize the video subsystem") && ok;
		}
	}
	{
		GameManager gameManager;
		auto& manager = gameManager.magicManager;
		const auto oldSource = manager.addPrimaryMagic("cache-source.ini", false, false)->magic;
		auto effect = std::make_shared<Effect>();
		effect->initFromMagic(oldSource);
		manager.deletePrimaryMagic("cache-source.ini");
		manager.addPrimaryMagic("cache-source.ini", false, false);
		manager.addHitExp(effect, 3);
		ok = check(manager.findPrimaryMagic("cache-source.ini")->exp == 0,
			"deleting and relearning a same-named magic does not steal an old effect's experience") && ok;
	}
	std::error_code errorCode;
	File::setPlatformStateParentForTests("");
	std::filesystem::remove_all(root, errorCode);
	return ok;
}

bool runMagicExperienceTests()
{
	struct ExperienceMultiplierRestore
	{
		int previous = Config::experienceMultiplier;
		~ExperienceMultiplierRestore()
		{
			Config::experienceMultiplier = previous;
		}
	} restoreMultiplier;
	Config::experienceMultiplier = 3;
	auto root = makeUniqueTestDirectory("jxqy_magic_experience_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "magic", errorCode);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(MagicDerivedSaveNamespace);
	if (!prepareMagicFixtures(root))
	{
		std::filesystem::remove_all(root, errorCode);
		return check(false, "write magic experience fixtures");
	}

	Magic parent;
	parent.initFromIni("parent.ini");
	Magic copiedParent;
	copiedParent.copy(parent);
	GameManager gameManager;
	bool ok = runDerivedExperienceTest(gameManager, copiedParent);
	for (const auto& levelDefinition : std::initializer_list<std::string>{
		"", "levelupexp=\n", "levelupexp=0\n", "levelupexp=not-a-number\n" })
	{
		for (int baseThreshold : { 0, 40 })
		{
			const std::string contents = "[init]\nname=ThresholdDefaults\nlevelupexp=" + std::to_string(baseThreshold)
				+ "\n[level1]\nlevelupexp=100\neffect=23\n[level2]\n" + levelDefinition;
			if (!check(writeTextFile(root / "ini/magic/threshold-defaults.ini", contents),
				"write missing, blank, zero and malformed level-threshold fixtures")) return false;
			Magic magic;
			magic.initFromIni("threshold-defaults.ini", false);
			const int expectedThreshold = levelDefinition == "levelupexp=0\n" ? 0 : baseThreshold;
			ok = check(magic.loadSucceeded && magic.level[1].levelupExp == 100
				&& magic.level[2].levelupExp == expectedThreshold && magic.level[2].effect == 23,
				"level thresholds use the Init default while unrelated legacy attributes retain inheritance") && ok;
			gameManager.magicManager.clearMagicList();
			auto* learned = gameManager.magicManager.addPrimaryMagic("threshold-defaults.ini", false, false);
			if (!check(learned != nullptr, "learn the file-backed threshold fixture")) return false;
			gameManager.scriptAPI.addMagicExp("threshold-defaults.ini", 100);
			ok = check(learned->level == (expectedThreshold == 0 ? 2 : MAGIC_MAX_LEVEL),
				"a terminal threshold stops script-driven advancement without changing the existing multi-level rule") && ok;
			Magic copied;
			copied.copy(magic);
			ok = check(copied.level[2].levelupExp == expectedThreshold,
				"copied and cast magic retains the corrected threshold") && ok;
		}
	}
	ok = runTerminalMagicExperienceTest(gameManager, root) && ok;
	gameManager.menu->stateMenu = std::make_shared<StateMenu>();
	LevelInfo currentLevel;
	currentLevel.levelUpExp = 100;
	currentLevel.attack = gameManager.player->attack;
	currentLevel.attack2 = gameManager.player->attack2;
	currentLevel.attack3 = gameManager.player->attack3;
	currentLevel.defend = gameManager.player->defend;
	currentLevel.defend2 = gameManager.player->defend2;
	currentLevel.defend3 = gameManager.player->defend3;
	currentLevel.evade = gameManager.player->evade;
	currentLevel.lifeMax = gameManager.player->lifeMax;
	currentLevel.thewMax = gameManager.player->thewMax;
	currentLevel.manaMax = gameManager.player->manaMax;
	LevelInfo nextLevel = currentLevel;
	nextLevel.levelUpExp = 200;
	gameManager.player->levelList = { currentLevel, nextLevel };
	gameManager.menu->systemNotice = std::make_shared<SystemNotice>();
	gameManager.setCheatModeEnabled(false);
	const int disabledCheatExperience = gameManager.player->exp;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePlayerLevel)
			&& gameManager.player->exp == disabledCheatExperience
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：请先开启作弊模式",
		"disabled cheat actions preserve state and explain how to enable cheats") && ok;
	ok = check(!gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility)
		&& !gameManager.isCheatInvincibilityEnabled()
		&& !gameManager.player->hasUnlimitedCheatResources(),
		"the invincibility shortcut action also requires cheat mode to be enabled") && ok;
	gameManager.setCheatModeEnabled(true);
	ok = check(gameManager.isCheatModeEnabled()
		&& gameManager.menu->systemNotice->currentMessage
			== u8"系统：作弊模式已开启",
		"cheat mode exposes a runtime-only explicit enable interface") && ok;
	gameManager.player->life = 100;
	gameManager.player->frozen = gameManager.player->poisoned = true;
	gameManager.player->petrified = gameManager.player->immobilized = true;
	gameManager.player->frozenLastTime = gameManager.player->poisonedLastTime = 10000;
	gameManager.player->petrifiedLastTime = gameManager.player->immobilizedLastTime = 10000;
	gameManager.player->disableMoveMilliseconds = gameManager.player->disableSkillMilliseconds = 10000;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::ToggleInvincibility)
			&& gameManager.isCheatInvincibilityEnabled(),
		"cheat invincibility can be enabled explicitly") && ok;
	auto playerHasNoAbnormalState = [&]()
	{
		auto player = gameManager.player;
		return !player->frozen && !player->poisoned && !player->petrified && !player->immobilized
			&& player->frozenLastTime == 0 && player->poisonedLastTime == 0
			&& player->petrifiedLastTime == 0 && player->immobilizedLastTime == 0
			&& player->disableMoveMilliseconds == 0 && player->disableSkillMilliseconds == 0;
	};
	ok = check(playerHasNoAbnormalState(), "enabling invincibility immediately cures existing statuses and action locks") && ok;
	auto statusEffect = std::make_shared<Effect>();
	statusEffect->level = 1;
	statusEffect->magic.disableMoveMilliseconds = statusEffect->magic.disableSkillMilliseconds = 10000;
	for (int status : {mskFreeze, mskPoison, mskPetrify, mskImmobilize})
	{
		statusEffect->magic.level[1].specialKind = status;
		gameManager.player->applyEffectRuntimeStates(*statusEffect);
		gameManager.player->applyPreDamageMagicStatus(*statusEffect, 1);
	}
	for (int additional : {maeFrozen, maePoison, maePetrified})
	{
		statusEffect->additionalEffect = additional;
		gameManager.player->applyAdditionalAttackEffect(*statusEffect, 1);
	}
	gameManager.scriptAPI.frozenMillisecond(10000);
	gameManager.scriptAPI.poisonMillisecond(10000);
	gameManager.scriptAPI.petrifyMillisecond(10000);
	ok = check(playerHasNoAbnormalState(), "invincibility rejects hit, equipment, script and action-lock statuses") && ok;
	auto statusTarget = std::make_shared<NPC>();
	statusTarget->applyPreDamageMagicStatus(*statusEffect, 1);
	ok = check(statusTarget->petrified, "cheat immunity does not protect ordinary NPCs") && ok;
	const auto originalStatusMap = gameManager.map->data;
	const Point originalStatusPosition = gameManager.player->getPosition();
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 8;
	gameManager.map->data->tile.assign(8, std::vector<MapTile>(8));
	gameManager.map->createDataMap();
	gameManager.player->setPosition({4, 4}, false);
	statusEffect->additionalEffect = maeNone;
	statusEffect->magic.rangeEffect = 1;
	statusEffect->magic.rangeRadius = 0;
	statusEffect->magic.attackAll = 1;
	statusEffect->magic.level[1].rangeFreezeMilliseconds = 10000;
	statusEffect->magic.level[1].rangePoisonMilliseconds = 10000;
	statusEffect->magic.level[1].rangePetrifyMilliseconds = 10000;
	statusEffect->user = gameManager.player;
	statusEffect->position = gameManager.player->getPosition();
	RageSystemTestAccess::updateRangeEffect(*statusEffect, 1);
	ok = check(playerHasNoAbnormalState(), "invincibility rejects range-applied statuses") && ok;
	gameManager.player->immobilized = true;
	gameManager.player->immobilizedLastTime = 10000;
	gameManager.player->disableSkillMilliseconds = 10000;
	RageSystemTestAccess::updatePlayer(*gameManager.player);
	ok = check(playerHasNoAbnormalState(), "a status restored while invincible is cleared on the next player update") && ok;
	gameManager.player->addLife(-50);
	ok = check(gameManager.player->life == 100,
		"cheat invincibility blocks player damage") && ok;
	gameManager.player->mana = gameManager.player->thew = 40;
	gameManager.player->addMana(-10);
	gameManager.player->addThew(-10);
	auto drainingEffect = std::make_shared<Effect>();
	drainingEffect->damageMana = 10;
	gameManager.player->applyEffectManaDamage(drainingEffect);
	gameManager.player->applySideEffectDamage(2, 10);
	ok = check(gameManager.player->mana == 40 && gameManager.player->thew == 40,
		"cheat invincibility protects mana and stamina from direct and effect drains") && ok;
	auto costlyMagic = std::make_shared<Magic>();
	costlyMagic->level[1].manaCost = 50;
	costlyMagic->level[1].thewCost = 20;
	gameManager.player->mana = gameManager.player->thew = 0;
	ok = check(gameManager.player->tryConsumeMagicCost(costlyMagic, 1, false)
		&& gameManager.player->mana == 0 && gameManager.player->thew == 0
		&& gameManager.player->canPayRunThewCost(),
		"cheat invincibility permits skill and running costs even at zero resources") && ok;
	costlyMagic->goodsName = "missing-cheat-required-item";
	ok = check(!gameManager.player->tryConsumeMagicCost(costlyMagic, 1, false),
		"unlimited resources retain the skill's required-item check") && ok;
	costlyMagic->goodsName.clear();
	gameManager.player->canUseMana = false;
	ok = check(!gameManager.player->tryConsumeMagicCost(costlyMagic, 1, false),
		"unlimited resources retain the player's skill-use restriction") && ok;
	gameManager.player->canUseMana = true;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::ToggleInvincibility)
			&& !gameManager.isCheatInvincibilityEnabled(),
		"cheat invincibility can be disabled for required story defeats") && ok;
	gameManager.player->applyPreDamageMagicStatus(*statusEffect, 1);
	ok = check(gameManager.player->immobilized, "disabling invincibility restores normal negative statuses") && ok;
	gameManager.player->clearAbnormalState();
	int rangeStatusHits = 0;
	statusEffect->magic.level[1].rangeDamage = 10;
	for (int attempt = 0; attempt < 256; ++attempt)
	{
		gameManager.player->clearAbnormalState();
		gameManager.player->disableMoveMilliseconds = gameManager.player->disableSkillMilliseconds = 0;
		gameManager.player->life = 100;
		gameManager.player->nowAction = acStand;
		RageSystemTestAccess::updateRangeEffect(*statusEffect, 1);
		const bool statusHit = gameManager.player->petrified && gameManager.player->poisoned;
		rangeStatusHits += statusHit;
		ok = check(!statusHit || gameManager.player->life < 100,
			"a missed range damage roll cannot apply range negative states") && ok;
	}
	ok = check(rangeStatusHits > 0 && rangeStatusHits < 256,
		"range negative states can hit and be evaded when invincibility is disabled") && ok;
	gameManager.player->clearAbnormalState();
	gameManager.player->disableMoveMilliseconds = gameManager.player->disableSkillMilliseconds = 0;
	gameManager.player->life = 100;
	gameManager.player->setPosition(originalStatusPosition, false);
	gameManager.map->data = originalStatusMap;
	gameManager.map->createDataMap();
	gameManager.player->addLife(-10);
	ok = check(gameManager.player->life < 100,
		"disabling cheat invincibility restores player damage") && ok;
	gameManager.player->mana = 100;
	gameManager.player->thew = 40;
	ok = check(gameManager.player->tryConsumeMagicCost(costlyMagic, 1, false)
		&& gameManager.player->mana == 50 && gameManager.player->thew == 20,
		"disabling cheat invincibility restores normal skill costs") && ok;
	gameManager.player->applyEffectManaDamage(drainingEffect);
	gameManager.player->applySideEffectDamage(2, 10);
	ok = check(gameManager.player->mana == 40 && gameManager.player->thew == 10,
		"disabling cheat invincibility restores effect drains") && ok;
	gameManager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
	gameManager.setCheatModeEnabled(false);
	gameManager.player->thew = 0;
	ok = check(!gameManager.player->hasUnlimitedCheatResources()
		&& !gameManager.player->canPayRunThewCost(),
		"disabling cheat mode also clears unlimited resources") && ok;
	gameManager.setCheatModeEnabled(true);
	for (const auto& [thresholdMode, expectedExperience] :
		std::initializer_list<std::pair<LevelUpThresholdMode, int>>{
			{ LevelUpThresholdMode::GreaterThanOrEqual, 100 },
			{ LevelUpThresholdMode::GreaterThan, 101 } })
	{
		ResourceManifest behavior;
		behavior.levelUpThresholdMode = thresholdMode;
		behavior.levelUpThresholdModeDefined = true;
		gameManager.global.applyResourceManifestFeatures(behavior);
		gameManager.player->level = 1;
		gameManager.player->exp = 0;
		gameManager.player->levelUpExp = 100;
		ok = check(
			gameManager.performCheatAction(
				GameManager::CheatAction::IncreasePlayerLevel)
				&& gameManager.player->level == 2
				&& gameManager.player->exp == expectedExperience
				&& gameManager.player->levelUpExp == 200
				&& gameManager.menu->systemNotice->currentMessage
					== u8"系统：角色已提升至2级",
			"cheat player-level increase honors the configured threshold and triggers normal leveling") && ok;
	}
	const int maximumLevelExperience = gameManager.player->exp;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePlayerLevel)
			&& gameManager.player->level == 2
			&& gameManager.player->exp == maximumLevelExperience,
		"cheat player-level increase does nothing at the configured maximum level") && ok;

	ResourceManifest extendedInventory;
	extendedInventory.uiProfile = "YYCS";
	gameManager.global.applyResourceManifestFeatures(extendedInventory);
	gameManager.magicManager.configureLayout();
	gameManager.menu->practiceMenu = std::make_shared<PracticeMenu>();
	const int practiceIndex = gameManager.magicManager.practiceIndex();
	MagicInfo practiceInfo;
	practiceInfo.iniFile = "cheat-practice.ini";
	practiceInfo.level = 1;
	practiceInfo.exp = 25;
	practiceInfo.magic = std::make_shared<Magic>();
	practiceInfo.magic->name = "Cheat Practice";
	practiceInfo.magic->level[1].levelupExp = 100;
	practiceInfo.magic->level[2].levelupExp = 250;
	for (int level = 3; level < MAGIC_MAX_LEVEL; ++level)
	{
		practiceInfo.magic->level[level].levelupExp = 1000 + level;
	}
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)] = practiceInfo;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].level == 2
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].exp == 100
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：Cheat Practice已提升至2级",
		"cheat practice-magic increase adds required experience through normal leveling") && ok;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].level == 3
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].exp == 250,
		"repeated cheat practice-magic increase accumulates experience for exactly the next threshold") && ok;
	auto& maximumLevelPracticeMagic = gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)];
	maximumLevelPracticeMagic.level = MAGIC_MAX_LEVEL;
	const int maximumLevelMagicExperience = maximumLevelPracticeMagic.exp;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& maximumLevelPracticeMagic.level == MAGIC_MAX_LEVEL
			&& maximumLevelPracticeMagic.exp == maximumLevelMagicExperience,
		"cheat practice-magic increase does nothing at maximum magic level") && ok;
	maximumLevelPracticeMagic.level = 1;
	maximumLevelPracticeMagic.exp = 25;
	maximumLevelPracticeMagic.magic->level[1].levelupExp = 0;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& maximumLevelPracticeMagic.level == 1
			&& maximumLevelPracticeMagic.exp == 25,
		"cheat practice-magic increase does not invent experience for a missing threshold") && ok;

	gameManager.player->info.lifeMax = 321;
	gameManager.player->info.manaMax = 222;
	gameManager.player->info.thewMax = 123;
	gameManager.player->life = 1;
	gameManager.player->mana = 2;
	gameManager.player->thew = 3;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::RestorePlayerResources)
			&& gameManager.player->life == 321
			&& gameManager.player->mana == 222
			&& gameManager.player->thew == 123
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：主角生命、内力和体力及伙伴生命已补满",
		"cheat resource restoration reports the completed operation") && ok;
	ok = runPartnerCheatProtectionTest(gameManager) && ok;
	gameManager.menu->goodsMenu = std::make_shared<GoodsMenu>();
	gameManager.player->money = 456;
	ok = check(
		gameManager.performCheatAction(GameManager::CheatAction::AddMoney)
			&& gameManager.player->money == 100456
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：已增加100000两银子，当前银两：100456",
		"cheat money grant reports the actual resulting balance") && ok;
	ok = runExperienceSaturationTest(gameManager) && ok;
	const std::string zeroHitExperience =
		"[HitMagicExp]\n"
		"LevelFactor=0\n"
		"[XiuLianMagicExp]\n"
		"Fraction=1.0\n"
		"[UseMagicExp]\n"
		"Fraction=1.0\n";
	if (!writeTextFile(root / "ini" / "level" / "MagicExp.ini", zeroHitExperience))
	{
		ok = check(false, "write zero-hit magic experience fixture") && ok;
	}
	else
	{
		gameManager.magicManager.configureLayout();
		const int currentUseIndex = gameManager.magicManager.bottomIndex(0);
		MagicInfo currentUseInfo;
		currentUseInfo.iniFile = "current.ini";
		currentUseInfo.level = 1;
		currentUseInfo.magic = std::make_shared<Magic>();
		currentUseInfo.magic->level[1].levelupExp = 1000;
		gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)] = currentUseInfo;
		gameManager.magicManager.recordCurrentUseMagic(currentUseIndex);

		auto ordinaryAttackEffect = std::make_shared<Effect>();
		ordinaryAttackEffect->magic.iniName = "ordinary_attack.ini";
		gameManager.magicManager.addHitExp(ordinaryAttackEffect, 10);
		gameManager.magicManager.addKillExp(ordinaryAttackEffect, 100);
		ok = check(
			gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 100,
			"zero hit factor awards no hit experience while full kill fraction remains active") && ok;
	}

	const auto resetExperienceSlots = [&gameManager](int levelUpExperience)
	{
		gameManager.magicManager.magicList.assign(
			static_cast<size_t>(gameManager.magicManager.listLength()), MagicInfo());
		const int practiceIndexValue = gameManager.magicManager.practiceIndex();
		const int useIndexValue = gameManager.magicManager.bottomIndex(0);
		MagicInfo practiceInfo;
		practiceInfo.iniFile = "chain-practice.ini";
		practiceInfo.level = 1;
		practiceInfo.magic = std::make_shared<Magic>();
		practiceInfo.magic->name = "ChainPractice";
		practiceInfo.magic->level[1].levelupExp = levelUpExperience;
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(practiceIndexValue)] = practiceInfo;
		MagicInfo useInfo;
		useInfo.iniFile = "chain-current.ini";
		useInfo.level = 1;
		useInfo.magic = std::make_shared<Magic>();
		useInfo.magic->name = "ChainCurrent";
		useInfo.magic->level[1].levelupExp = levelUpExperience;
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(useIndexValue)] = useInfo;
		gameManager.magicManager.recordCurrentUseMagic(useIndexValue);
		return std::pair<int, int>{ practiceIndexValue, useIndexValue };
	};
	const std::string legacyExperienceTable =
		"[Exp]\n"
		"0=3\n"
		"1=3\n"
		"2=4\n";
	const std::string baseEngineExperience =
		"[HitMagicExp]\n"
		"LevelFactor=5\n"
		"[XiuLianMagicExp]\n"
		"Fraction=0.5\n"
		"[UseMagicExp]\n"
		"Fraction=0.25\n";
	const std::filesystem::path baseRoot = root / "chain-base";
	ok = check(
		std::filesystem::create_directories(baseRoot / "ini" / "level", errorCode) &&
			writeTextFile(baseRoot / "ini" / "level" / "MagicExp.ini", baseEngineExperience),
		"write dependency-root engine-format MagicExp fixture") && ok;

	auto chainEffect = std::make_shared<Effect>();
	chainEffect->magic.iniName = "chain-current.ini";
	if (!writeTextFile(root / "ini" / "level" / "MagicExp.ini", legacyExperienceTable))
	{
		ok = check(false, "write legacy-format active MagicExp fixture") && ok;
	}
	else
	{
		File::setResourceFallbackRoots({ baseRoot.string() });
		gameManager.magicManager.configureLayout();
		const auto [chainPracticeIndex, chainUseIndex] = resetExperienceSlots(1000);
		gameManager.magicManager.addHitExp(chainEffect, 3);
		gameManager.magicManager.addKillExp(chainEffect, 200);
		ok = check(
			gameManager.magicManager.magicList[static_cast<std::size_t>(chainPracticeIndex)].exp == 100 &&
				gameManager.magicManager.magicList[static_cast<std::size_t>(chainUseIndex)].exp == 65,
			"legacy active MagicExp table inherits the dependency root's engine rules") && ok;
	}

	const std::string activeEngineExperience =
		"[HitMagicExp]\n"
		"LevelFactor=2\n"
		"[XiuLianMagicExp]\n"
		"Fraction=0.2222\n"
		"[UseMagicExp]\n"
		"Fraction=0.0333\n";
	if (!writeTextFile(root / "ini" / "level" / "MagicExp.ini", activeEngineExperience))
	{
		ok = check(false, "write active engine-format MagicExp fixture") && ok;
	}
	else
	{
		gameManager.magicManager.configureLayout();
		const auto [chainPracticeIndex, chainUseIndex] = resetExperienceSlots(1000);
		gameManager.magicManager.addHitExp(chainEffect, 3);
		gameManager.magicManager.addKillExp(chainEffect, 200);
		ok = check(
			gameManager.magicManager.magicList[static_cast<std::size_t>(chainPracticeIndex)].exp == 44 &&
				gameManager.magicManager.magicList[static_cast<std::size_t>(chainUseIndex)].exp == 12,
			"the active root's complete MagicExp rules shadow the dependency root") && ok;
	}

	if (!writeTextFile(root / "ini" / "level" / "MagicExp.ini", legacyExperienceTable)
		|| !writeTextFile(baseRoot / "ini" / "level" / "MagicExp.ini", legacyExperienceTable))
	{
		ok = check(false, "write legacy MagicExp fixtures for both roots") && ok;
	}
	else
	{
		gameManager.magicManager.configureLayout();
		const auto [chainPracticeIndex, chainUseIndex] = resetExperienceSlots(1000);
		gameManager.magicManager.addHitExp(chainEffect, 5);
		ok = check(
			gameManager.magicManager.magicList[static_cast<std::size_t>(chainPracticeIndex)].exp == 0 &&
				gameManager.magicManager.magicList[static_cast<std::size_t>(chainUseIndex)].exp == 0,
			"unconfigured rules stay inactive when no root provides a complete MagicExp file") && ok;
		gameManager.magicManager.addKillExp(chainEffect, 100);
		ok = check(
			gameManager.magicManager.magicList[static_cast<std::size_t>(chainPracticeIndex)].exp == 100 &&
				gameManager.magicManager.magicList[static_cast<std::size_t>(chainUseIndex)].exp == 100,
			"the unconfigured kill branch keeps awarding the full amount to practice and killing magic") && ok;
		gameManager.magicManager.addKillExp(chainEffect, 63, 0.2222f, 0.0333f);
		ok = check(
			gameManager.magicManager.magicList[static_cast<std::size_t>(chainPracticeIndex)].exp == 113 &&
				gameManager.magicManager.magicList[static_cast<std::size_t>(chainUseIndex)].exp == 102,
			"explicit kill fractions override the pack configuration for fallback awards") && ok;
	}

	File::setResourceFallbackRoots({});
	gameManager.magicManager.configureLayout();
	const auto [guardPracticeIndex, guardUseIndex] = resetExperienceSlots(0);
	gameManager.magicManager.addUseExp(chainEffect, 500);
	ok = check(
		gameManager.magicManager.magicList[static_cast<std::size_t>(guardUseIndex)].exp == 500 &&
			gameManager.magicManager.magicList[static_cast<std::size_t>(guardUseIndex)].level == 1,
		"zero level-up thresholds stop leveling without changing experience accumulation") && ok;
	gameManager.magicManager.addUseExp(chainEffect, 0);
	gameManager.magicManager.addPracticeExp(0);
	ok = check(
		gameManager.magicManager.magicList[static_cast<std::size_t>(guardUseIndex)].exp == 500 &&
			gameManager.magicManager.magicList[static_cast<std::size_t>(guardUseIndex)].level == 1,
		"zero experience awards leave magic state untouched") && ok;
	{
		const auto skillFallback = root / "skill-fallback";
		const std::string skillSettings =
			u8"\uFEFF[Init]\r\n"
			u8"//升级点数启用\r\nPointEnable=1\r\n"
			u8"//技能树启用\r\nTreeEnable=1\r\n"
			u8"//每升一级给的技能点数\r\nLevelUpRatio=1\r\n"
			u8"//点数上限\r\nMaxValue=100";
		ok = check(writeTextFile(skillFallback / "ini/other/Skill.ini", skillSettings),
			"write skill-point dependency fixture") && ok;
		File::setResourceFallbackRoots({ skillFallback.string() });
		gameManager.global.loadUiSettings();
		ok = check(!gameManager.global.skillPointsEnabled,
			"skill points are disabled without an active-package Skill.ini") && ok;
		ok = check(writeTextFile(root / "ini/other/Skill.ini", skillSettings),
			"write actual MG skill-point settings") && ok;
		std::unique_ptr<char[]> authoredBytes;
		int authoredLength = 0;
		const bool authoredRead = File::readActiveResourceFile("ini/other/Skill.ini", authoredBytes, authoredLength);
		ok = check(authoredRead && authoredLength == 159 && skillSettings.size() == 159
			&& std::string(authoredBytes.get(), static_cast<size_t>(authoredLength)) == skillSettings,
			"compiled UTF-8 BOM and Chinese fixture bytes match the published MG settings") && ok;
		INIReader authoredSkillIni(authoredBytes);
		ok = check(authoredSkillIni.ParseError() == 0, "actual MG skill-point INI parses without errors") && ok;
		if (authoredSkillIni.ParseError() != 0)
		{
			std::cerr << "MG Skill.ini read=" << authoredRead << " size=" << authoredLength
				<< " parse=" << authoredSkillIni.ParseError() << " fixture=" << skillSettings.size() << std::endl;
		}
		gameManager.global.loadUiSettings();
		ok = check(gameManager.global.skillPointsEnabled
			&& gameManager.global.skillPointLevelUpRatio == 1
			&& gameManager.global.skillPointMaximum == 100,
			"active MG Skill.ini configures one point per level and a 100-point cap") && ok;
		gameManager.player->levelList.assign(4, LevelInfo{});
		for (int i = 0; i < 4; ++i)
		{
			gameManager.player->levelList[i].levelUpExp = (i + 1) * 10;
			gameManager.player->levelList[i].lifeMax = 100;
		}
		const auto resetPlayerLevel = [&]()
		{
			gameManager.player->setLevel(1);
			gameManager.player->exp = 0;
		};
		gameManager.global.levelUpThresholdMode = LevelUpThresholdMode::GreaterThanOrEqual;
		resetPlayerLevel();
		gameManager.player->skillPoints = 0;
		gameManager.player->addExp(30);
		ok = check(gameManager.player->level == 4 && gameManager.player->skillPoints == 3,
			"one experience award grants skill points for every earned player level") && ok;
		resetPlayerLevel();
		gameManager.player->skillPoints = 99;
		gameManager.player->addExp(10);
		gameManager.player->addExp(20);
		ok = check(gameManager.player->level == 4 && gameManager.player->skillPoints == 100,
			"earned skill points stop at the authored cap") && ok;
		resetPlayerLevel();
		gameManager.player->skillPoints = 7;
		gameManager.player->setLevel(3);
		ok = check(gameManager.player->skillPoints == 7,
			"explicit level replacement does not award natural-level skill points") && ok;
		gameManager.player->addExp(0);
		gameManager.player->addExp(-1);
		ok = check(gameManager.player->skillPoints == 7,
			"zero and negative experience without a level gain do not award skill points") && ok;
		for (const std::string& invalid : {
			"PointEnable=0\nLevelUpRatio=1\nMaxValue=100\n",
			"PointEnable=1\nLevelUpRatio=-1\nMaxValue=100\n",
			"PointEnable=1\nLevelUpRatio=1\nMaxValue=invalid\n",
			"PointEnable=1\nLevelUpRatio=2147483648\nMaxValue=100\n" })
		{
			ok = check(writeTextFile(root / "ini/other/Skill.ini", "[Init]\n" + invalid),
				"write disabled or invalid skill-point settings") && ok;
			gameManager.global.loadUiSettings();
			resetPlayerLevel();
			gameManager.player->skillPoints = 7;
			gameManager.player->addExp(30);
			ok = check(!gameManager.global.skillPointsEnabled && gameManager.player->skillPoints == 7,
				"disabled or invalid skill settings reset previous rules without altering saved points") && ok;
		}
		ok = check(writeTextFile(root / "ini/other/Skill.ini",
			"[Init]\nPointEnable=1\nLevelUpRatio=2147483647\nMaxValue=2147483647\n"),
			"write maximum integer skill-point settings") && ok;
		gameManager.global.loadUiSettings();
		resetPlayerLevel();
		gameManager.player->skillPoints = std::numeric_limits<int>::max() - 1;
		gameManager.player->addExp(10);
		ok = check(gameManager.player->skillPoints == std::numeric_limits<int>::max(),
			"skill-point awards use a wide intermediate before applying the cap") && ok;
		File::setResourceFallbackRoots({});
	}
	ResourceManager& resources = ResourceManager::instance();
	ok = check(writeTextFile(root / "ini/level/MagicExp.ini", baseEngineExperience),
		"write kill-share and fixed hit-experience rules for the player multiplier matrix") && ok;
	for (int gameType : { 0, 1, 2, 3 })
	{
		const std::string profile = "[Game]\nId=PLAYER_MULTIPLIER\nType=" + std::to_string(gameType) +
			"\n[Experience]\nExperienceMultiplier=" + (gameType == 0 ? "99" : "0") + "\n";
		if (!check(writeTextFile(root / "game_profile.ini", profile) && resources.initialize(root.string()) &&
			resources.setActiveResourcePackById("PLAYER_MULTIPLIER"),
			"activate a real resource profile containing an obsolete experience multiplier"))
		{
			ok = false;
			continue;
		}
		gameManager.magicManager.configureLayout();
		for (int multiplier : { 1, 2, 3 })
		{
			Config::experienceMultiplier = multiplier;
			for (int storedExperience : { 100, 0 })
			{
				const auto [selectedPracticeIndex, selectedUseIndex] = resetExperienceSlots(10000);
				gameManager.player->level = 9;
				gameManager.player->exp = 0;
				gameManager.player->levelUpExp = 1000000;
				auto victim = std::make_shared<NPC>();
				victim->kind = nkBattle;
				victim->relation = nrHostile;
				victim->level = 7;
				victim->exp = storedExperience;
				chainEffect->user = gameManager.player;
				chainEffect->launcherKind = lkSelf;
				victim->awardDefeatedNpcExperience(chainEffect);
				const bool fallback = gameType == 0 && storedExperience == 0;
				const int expectedReward = (gameType == 0 && storedExperience != 0 ? 100 : 63) * multiplier;
				const int expectedPractice = fallback ? (multiplier == 1 ? 13 : multiplier == 2 ? 27 : 41)
					: expectedReward / 2;
				const int expectedUse = fallback ? 2 * multiplier : expectedReward / 4;
				const auto& practice = gameManager.magicManager.magicList[static_cast<size_t>(selectedPracticeIndex)];
				const auto& current = gameManager.magicManager.magicList[static_cast<size_t>(selectedUseIndex)];
				ok = check(gameManager.player->exp == expectedReward && practice.exp == expectedPractice &&
					current.exp == expectedUse,
					"global 1/2/3 choices replace obsolete resource multipliers for player and kill-magic rewards") && ok;
				victim->awardDefeatedNpcExperience(chainEffect);
				ok = check(gameManager.player->exp == expectedReward && practice.exp == expectedPractice &&
					current.exp == expectedUse,
					"scaled stored, level-product and zero-exp fallback kills are awarded only once") && ok;
				gameManager.scriptAPI.addExp(7);
				gameManager.magicManager.addHitExp(chainEffect, 3);
				ok = check(gameManager.player->exp == expectedReward + 7 && practice.exp == expectedPractice &&
					current.exp == expectedUse + 15,
					"script rewards and magic-hit experience retain their fixed amounts under every player choice") && ok;
			}
		}
	}
	const auto emptyCollection = root / "empty-collection";
	std::filesystem::create_directories(emptyCollection, errorCode);
	ok = check(!errorCode && resources.initialize(emptyCollection.string()),
		"clear the isolated experience resource profile") && ok;
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setActiveSaveNamespace(MagicDerivedSaveNamespace);
	std::filesystem::remove_all(root, errorCode);
	return ok;
}
