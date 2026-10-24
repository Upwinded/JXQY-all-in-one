#include "../Game/Data/CollisionDetector.h"
#include "../Game/Data/NPCManager.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/GameManager/GameManager.h"

#include <cmath>
#include <iostream>

class ProjectileCollisionRuntimeTestAccess
{
public:
	static void beginFrame(EffectManager& manager) { manager.onPreTreatment(); }
	static void advance(Effect& effect, UTime elapsed)
	{
		effect.frameTime = elapsed;
		effect.setTime(effect.getTime() + elapsed);
		effect.onUpdate();
	}
};

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition) std::cerr << "FAILED: " << message << '\n';
	return condition;
}

void place(const std::shared_ptr<NPC>& npc, PointEx offset, bool continuous = false)
{
	Point position;
	PointEx normalizedOffset;
	npc->getNewPosition({5, 5}, offset, &position, &normalizedOffset);
	npc->setPosition(position, false, continuous);
	npc->setOffset(normalizedOffset, continuous);
}

std::shared_ptr<Effect> projectile(PointEx from, PointEx to)
{
	auto effect = std::make_shared<Effect>();
	effect->doing = ekFlying;
	effect->magic.attackAll = 1;
	effect->level = 1;
	effect->damage = 100;
	effect->lifeTime = 1000;
	effect->flyingDirection = {static_cast<int>(to.x - from.x), static_cast<int>((to.y - from.y) * MapXRatio)};
	effect->getNewPosition({5, 5}, from, &effect->src, &effect->srcOffset);
	effect->getNewPosition({5, 5}, to, &effect->position, &effect->offset);
	effect->collisionSweepStartPosition = effect->src;
	effect->collisionSweepStartOffset = effect->srcOffset;
	effect->collisionSweepInitialized = true;
	effect->passPath = effect->getPassPath(effect->src, effect->srcOffset, effect->position, effect->offset);
	return effect;
}
}

bool runProjectileCollisionRuntimeTests()
{
	GameManager manager;
	manager.map->data = std::make_shared<MapData>();
	manager.map->data->head.width = manager.map->data->head.height = 24;
	manager.map->data->tile.assign(24, std::vector<MapTile>(24));
	auto target = std::make_shared<NPC>();
	target->kind = nkBattle;
	target->relation = nrHostile;
	target->lifeMax = target->life = 1000;
	target->testMagicHitRoll = 0;
	manager.npcManager->npcList.push_back(target);
	manager.map->createDataMap();
	bool ok = true;
	auto begin = [&](PointEx start)
	{
		manager.effectManager->freeResource();
		target->life = 1000;
		place(target, start);
		ProjectileCollisionRuntimeTestAccess::beginFrame(*manager.effectManager);
	};
	auto collide = [&](const std::shared_ptr<Effect>& effect, bool expected, const char* message)
	{
		manager.effectManager->effectList.push_back(effect);
		CollisionDetector::detectCollision();
		return check((effect->doing == ekExploding) == expected
			&& target->life == (expected ? 900 : 1000), message);
	};

	// The target is stationary; no reserved walking tile can mask a broad-phase miss.
	begin({32, 16});
	auto edge = projectile({15, 7}, {17, 7});
	ok = check(edge->passPath.empty(), "short centre and edge paths stay in their respective tiles") && ok;
	ok = collide(edge, true, "a nearby stationary NPC in the adjacent tile takes damage") && ok;

	begin({20, 18});
	place(target, {20, 12}, true);
	ok = collide(projectile({0, 0}, {64, 0}), false,
		"a moving target that never meets the projectile is not hit at its final position") && ok;
	begin({20, 10});
	place(target, {20, 16}, true);
	ok = collide(projectile({0, 0}, {64, 0}), true,
		"relative movement detects contact before the target leaves the projectile path") && ok;
	begin({0, -40});
	place(target, {0, 40}, true);
	ok = collide(projectile({0, 0}, {0, 0}), true,
		"a moving character crossing a stationary effect remains a collision candidate") && ok;

	begin({0, -40});
	place(target, {0, 40});
	ok = collide(projectile({0, 0}, {0, 0}), false,
		"explicit relocation does not sweep through intervening projectiles") && ok;
	begin({0, -10});
	target->setPosition(target->getPosition(), false);
	target->setOffset({0, 10}, true);
	Point start;
	PointEx startOffset;
	ok = check(!target->getProjectileCollisionStart(manager.effectManager->getProjectileCollisionFrame(), start, startOffset),
		"same-tile relocation invalidates the old collision origin") && ok;

	begin({0, -40});
	place(target, {0, 40}, true);
	auto newborn = projectile({0, 0}, {0, 0});
	manager.effectManager->addEffect(newborn);
	CollisionDetector::detectCollision();
	ok = check(newborn->doing == ekFlying && target->life == 1000,
		"newly released effects do not retroactively collide with earlier character movement") && ok;

	begin({0, 0});
	auto delayed = projectile({0, 0}, {0, 0});
	delayed->doing = ekHiding;
	delayed->magic.level[1].moveKind = mmkPoint;
	delayed->waitTime = 100;
	manager.effectManager->addEffect(delayed);
	delayed->setTime(1000);
	delayed->beginTime = 1000;
	const auto creationFrame = delayed->projectileCollisionCreationFrame;
	ProjectileCollisionRuntimeTestAccess::beginFrame(*manager.effectManager);
	place(target, {0, 40}, true);
	ProjectileCollisionRuntimeTestAccess::advance(*delayed, 100);
	CollisionDetector::detectCollision();
	ok = check(delayed->doing == ekFlying && target->life == 1000
		&& delayed->projectileCollisionCreationFrame == creationFrame
		&& delayed->characterCollisionActivationFrame == manager.effectManager->getProjectileCollisionFrame(),
		"a delayed effect activated at frame end cannot hit a character that already left, or change its creation frame") && ok;
	ProjectileCollisionRuntimeTestAccess::beginFrame(*manager.effectManager);
	place(target, {0, -40}, true);
	ProjectileCollisionRuntimeTestAccess::advance(*delayed, 1);
	CollisionDetector::detectCollision();
	ok = check(delayed->doing == ekExploding && target->life == 900,
		"an already active delayed effect detects character crossings on the following frame") && ok;

	begin({20, 10});
	place(target, {20, 16}, true);
	auto stale = projectile({0, 0}, {64, 0});
	manager.effectManager->effectList.push_back(stale);
	ProjectileCollisionRuntimeTestAccess::beginFrame(*manager.effectManager);
	CollisionDetector::detectCollision();
	ok = check(!stale->collisionSweepInitialized && stale->passPath.empty()
		&& stale->doing == ekFlying && target->life == 1000,
		"a frozen effect cannot replay its previous sweep when time stop ends") && ok;

	// A wide projectile can touch a side wall before its centre leaves its tile.
	begin({52, 0});
	manager.map->data->tile[4][6].obstacle = 0x02;
	auto sideWall = projectile({18, 0}, {24, 0});
	manager.effectManager->effectList.push_back(sideWall);
	CollisionDetector::detectCollision();
	ok = check(sideWall->doing == ekExploding && target->life == 1000,
		"an earlier side-wall contact wins over later character contact in the start tile") && ok;
	manager.map->data->tile[4][6].obstacle = 0;

	begin({192, 0});
	manager.map->data->tile[5][6].obstacle = 0x02;
	auto blocked = projectile({0, 0}, {256, 0});
	manager.effectManager->effectList.push_back(blocked);
	CollisionDetector::detectCollision();
	ok = check(blocked->doing == ekExploding && target->life == 1000,
		"expanded candidates do not allow a projectile to damage a target behind a wall") && ok;
	begin({192, 0});
	auto throughWall = projectile({0, 0}, {256, 0});
	throughWall->magic.passThroughWall = 1;
	ok = collide(throughWall, true, "explicit wall-passing magic still reaches a target behind the wall") && ok;
	manager.map->data->tile[5][6].obstacle = 0;

	begin({32, 16});
	auto throughTarget = projectile({15, 7}, {17, 7});
	throughTarget->magic.passThrough = 1;
	manager.effectManager->effectList.push_back(throughTarget);
	CollisionDetector::detectCollision();
	CollisionDetector::detectCollision();
	ok = check(throughTarget->doing == ekFlying && target->life == 900,
		"expanded candidates preserve once-per-target pass-through damage") && ok;

	manager.global.data.NPCAI = false;
	auto mover = std::make_shared<NPC>();
	mover->kind = nkBattle;
	mover->life = 100;
	auto movementImage = std::make_shared<IMPImage>();
	movementImage->directions = 8;
	movementImage->interval = 100;
	movementImage->frame.resize(24);
	mover->res.stand.imagePackage = movementImage;
	mover->res.walk.imagePackage = movementImage;
	mover->res.run.imagePackage = movementImage;
	mover->res.jump.imagePackage = movementImage;
	manager.npcManager->npcList.push_back(mover);
	const Point moveStart = {10, 10};
	const Point moveDestination = {11, 10};
	for (const bool running : {false, true})
	{
		mover->actionManager->restartActionIgnoringTransitions(acStand);
		mover->setPosition(moveStart, false);
		mover->setTime(1000);
		if (running) mover->beginRun(moveDestination);
		else mover->beginWalk(moveDestination);
		const bool started = running ? mover->isRunning() : mover->isWalking();
		ok = check(started && mover->stepLastTime > 0,
			"collision-origin fixture starts real NPC walking and running actions") && ok;
		if (!started || mover->stepLastTime == 0) continue;
		ProjectileCollisionRuntimeTestAccess::beginFrame(*manager.effectManager);
		const UTime elapsed = mover->stepLastTime + 1;
		mover->setTime(1000 + elapsed);
		mover->actionManager->update(elapsed);
		ok = check(mover->getPosition() != moveStart
			&& mover->getProjectileCollisionStart(manager.effectManager->getProjectileCollisionFrame(), start, startOffset)
			&& start == moveStart && startOffset.x == 0 && startOffset.y == 0,
			"real walk/run step-out preserves the frame origin across its tile change") && ok;
	}

	mover->actionManager->restartActionIgnoringTransitions(acStand);
	mover->setPosition(moveStart, false);
	mover->setTime(1000);
	mover->beginJump(moveDestination);
	const UTime jumpDuration = mover->actionLastTime;
	ok = check(mover->isJumping() && jumpDuration > 0,
		"collision-origin fixture starts a real NPC jump") && ok;
	if (mover->isJumping() && jumpDuration > 0)
	{
		mover->setTime(1000 + jumpDuration / 2);
		mover->actionManager->update(jumpDuration / 2);
		ProjectileCollisionRuntimeTestAccess::beginFrame(*manager.effectManager);
		ok = check(mover->getJumpState() == jsJumping
			&& !mover->getProjectileCollisionStart(manager.effectManager->getProjectileCollisionFrame(), start, startOffset),
			"an airborne NPC does not contribute a ground collision origin") && ok;
		mover->setTime(1000 + jumpDuration);
		mover->actionManager->update(jumpDuration - jumpDuration / 2);
		ok = check(mover->getJumpState() == jsDown && mover->getPosition() == moveDestination
			&& !mover->getProjectileCollisionStart(manager.effectManager->getProjectileCollisionFrame(), start, startOffset),
			"real jump landing does not sweep the airborne path through ground effects") && ok;
	}
	return ok;
}
