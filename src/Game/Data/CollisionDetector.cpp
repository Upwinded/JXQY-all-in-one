#include "CollisionDetector.h"
#include "NPCManager.h"
#include "../GameManager/GameManager.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <list>
#include <unordered_set>
#include <vector>

namespace
{
	bool checkNPCAtPosition(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect);

	struct CollisionPosition
	{
		Point position = { 0, 0 };
		PointEx offset = { 0.0f, 0.0f };
	};

	PointEx getCollisionSpaceDelta(
		Point from,
		PointEx fromOffset,
		Point to,
		PointEx toOffset)
	{
		const PointEx tileDelta = Map::getTilePositionEx(to, from);
		return
		{
			(tileDelta.x + toOffset.x - fromOffset.x) / MapXRatio,
			tileDelta.y + toOffset.y - fromOffset.y
		};
	}

	bool getSweepCollisionFraction(
		PointEx relativeStart,
		PointEx relativeMovement,
		double radius,
		float& collisionFraction)
	{
		const double startX = relativeStart.x;
		const double startY = relativeStart.y;
		const double movementX = relativeMovement.x;
		const double movementY = relativeMovement.y;
		const double constant =
			startX * startX + startY * startY - radius * radius;
		if (constant <= 0.0)
		{
			collisionFraction = 0.0f;
			return true;
		}

		const double quadratic =
			movementX * movementX + movementY * movementY;
		if (quadratic <= 0.0)
		{
			return false;
		}
		const double linear =
			2.0 * (startX * movementX + startY * movementY);
		const double discriminant =
			linear * linear - 4.0 * quadratic * constant;
		if (discriminant < 0.0)
		{
			return false;
		}

		const double entryFraction =
			(-linear - std::sqrt(discriminant)) / (2.0 * quadratic);
		if (entryFraction < 0.0 || entryFraction > 1.0)
		{
			return false;
		}
		collisionFraction = static_cast<float>(entryFraction);
		return true;
	}

	CollisionPosition getCollisionPosition(
		const std::shared_ptr<Effect>& effect,
		float collisionFraction)
	{
		if (effect == nullptr || !effect->collisionSweepInitialized)
		{
			return effect == nullptr
				? CollisionPosition{}
				: CollisionPosition{ effect->position, effect->offset };
		}

		PointEx movement = Map::getTilePositionEx(
			effect->position,
			effect->collisionSweepStartPosition);
		movement.x += effect->offset.x - effect->collisionSweepStartOffset.x;
		movement.y += effect->offset.y - effect->collisionSweepStartOffset.y;
		PointEx collisionOffset =
		{
			effect->collisionSweepStartOffset.x + movement.x * collisionFraction,
			effect->collisionSweepStartOffset.y + movement.y * collisionFraction
		};
		CollisionPosition result{ effect->collisionSweepStartPosition, collisionOffset };
		effect->getNewPosition(
			result.position,
			result.offset,
			&result.position,
			&result.offset);
		return result;
	}

	bool isActiveProjectile(const std::shared_ptr<Effect>& effect)
	{
		return effect != nullptr &&
			(effect->doing == ekFlying || effect->doing == ekThrowing);
	}

	bool canParticipateInProjectileCollision(
		const std::shared_ptr<Effect>& effect)
	{
		return effect != nullptr && gm != nullptr &&
			gm->effectManager != nullptr &&
			effect->collisionSweepInitialized &&
			effect->projectileCollisionCreationFrame !=
				gm->effectManager->getProjectileCollisionFrame();
	}

	bool isSpecialProjectile(const std::shared_ptr<Effect>& effect)
	{
		return isActiveProjectile(effect) &&
			canParticipateInProjectileCollision(effect) &&
			(effect->magic.discardOppositeMagic > 0 ||
				effect->magic.exchangeUser > 0);
	}

	void finishEffectCollisionFrame(
		const std::shared_ptr<Effect>& effect,
		bool collided)
	{
		if (!collided && effect)
		{
			effect->finishMeteorArrivalWithoutCollision();
		}
		else if (effect)
		{
			effect->consumeMeteorArrivalPending();
		}

		if (effect && effect->doing != ekHiding)
		{
			effect->handleCarryUser4NeighborCollisions();
		}
	}

	int getCollisionPriority(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (npc == nullptr || effect == nullptr)
		{
			return INT_MAX;
		}
		auto caster = std::dynamic_pointer_cast<NPC>(effect->user.lock());
		if (effect->magic.attackAll > 0)
		{
			return npc != caster ? 0 : INT_MAX;
		}
		return NPCManager::getLauncherHitPriority(effect->launcherKind, npc, effect->user.lock());
	}

	bool canCollide(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (effect == nullptr)
		{
			return false;
		}
		if (effect->hasAttachedNPC(npc))
		{
			return false;
		}
		if (effect != nullptr && effect->canPassThrough() && effect->hasPassThroughHitTarget(npc))
		{
			return false;
		}
		if (effect->hasLeapHitTarget(npc))
		{
			return false;
		}
		return getCollisionPriority(npc, effect) != INT_MAX;
	}

	bool getNPCCollisionFraction(
		std::shared_ptr<NPC> npc,
		std::shared_ptr<Effect> effect,
		float& collisionFraction)
	{
		if (!npc || !effect) return false;

		const float combinedRadius = effect->width * 0.5f + npc->radius;
		if (combinedRadius < 0.0f)
		{
			return false;
		}

		if (!effect->collisionSweepInitialized)
		{
			collisionFraction = 1.0f;
			return Map::getTileDistance(
				effect->position, effect->offset,
				npc->getPosition(), npc->getOffset()) <= combinedRadius;
		}

		PointEx movement = getCollisionSpaceDelta(
			effect->collisionSweepStartPosition,
			effect->collisionSweepStartOffset,
			effect->position,
			effect->offset);
		Point targetStart = npc->getPosition();
		PointEx targetStartOffset = npc->getOffset();
		const auto frame = gm->effectManager->getProjectileCollisionFrame();
		// A newly released projectile cannot hit an earlier part of the actor's frame.
		if (effect->projectileCollisionCreationFrame != frame
			&& effect->characterCollisionActivationFrame != frame
			&& npc->getProjectileCollisionStart(frame, targetStart, targetStartOffset))
		{
			movement = movement - getCollisionSpaceDelta(targetStart, targetStartOffset,
				npc->getPosition(), npc->getOffset());
		}
		const PointEx target = getCollisionSpaceDelta(
			effect->collisionSweepStartPosition,
			effect->collisionSweepStartOffset,
			targetStart,
			targetStartOffset);
		constexpr float diagonalTileStepLength =
			(float)TILE_HEIGHT / 1.41421356237f;
		const float combinedRadiusInPixels =
			combinedRadius * diagonalTileStepLength;
		return getSweepCollisionFraction(
			{ -target.x, -target.y },
			movement,
			combinedRadiusInPixels,
			collisionFraction);
	}

	bool processCollision(
		std::shared_ptr<NPC> npc,
		std::shared_ptr<Effect> effect,
		const CollisionPosition& collisionPosition)
	{
		if (!canCollide(npc, effect)) return false;
		if (effect->skipsCharacterCollision()) return false;

		const Point endPosition = effect->position;
		const PointEx endOffset = effect->offset;
		effect->position = collisionPosition.position;
		effect->offset = collisionPosition.offset;
		if (effect->handleCarryUser4AfterHit(npc))
		{
			npc->hurt(effect);
			return true;
		}
		if (effect->canBall())
		{
			effect->handleBallAfterHit(npc, collisionPosition.position);
			npc->hurt(effect);
			return true;
		}
		if (effect->handleStickyAfterHit(npc))
		{
			npc->hurt(effect);
			return true;
		}
		if (effect->canParasitic() && effect->beginParasitic(npc, collisionPosition.position))
		{
			return true;
		}
		if (effect->canLeap())
		{
			npc->hurt(effect);
			effect->handleLeapAfterHit(npc);
			return true;
		}
		if (effect->canPassThrough())
		{
			effect->position = endPosition;
			effect->offset = endOffset;
			npc->hurt(effect);
			effect->handlePassThroughAfterHit(
				npc,
				collisionPosition.position,
				collisionPosition.offset);
			return true;
		}
		effect->beginExplode(
			collisionPosition.position,
			collisionPosition.offset);
		npc->hurt(effect);
		return true;
	}

	bool processCollisionWithCheck(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (!canCollide(npc, effect)) return false;
		if (effect->skipsCharacterCollision()) return false;

		float collisionFraction = 1.0f;
		if (!getNPCCollisionFraction(npc, effect, collisionFraction))
		{
			return false;
		}
		return processCollision(
			npc,
			effect,
			getCollisionPosition(effect, collisionFraction));
	}

	PointEx getCharacterCollisionReach()
	{
		PointEx reach = { 0, 0 };
		const auto frame = gm->effectManager->getProjectileCollisionFrame();
		auto include = [&](const std::shared_ptr<NPC>& npc)
		{
			if (!npc || !npc->isPresentForRuntime()) return;
			Point start = npc->getPosition();
			PointEx startOffset = npc->getOffset();
			npc->getProjectileCollisionStart(frame, start, startOffset);
			const auto movement = getCollisionSpaceDelta(start, startOffset,
				npc->getPosition(), npc->getOffset());
			const float radius = std::max(0.0f, npc->radius) * TILE_HEIGHT / std::sqrt(2.0f);
			reach.x = std::max(reach.x, (std::abs(movement.x) + radius) * MapXRatio + std::abs(npc->getOffset().x));
			reach.y = std::max(reach.y, std::abs(movement.y) + radius + std::abs(npc->getOffset().y));
		};
		include(gm->player);
		for (const auto& npc : gm->npcManager->npcList) include(npc);
		return reach;
	}

	std::shared_ptr<NPC> findBestCollisionTarget(
		const std::shared_ptr<Effect>& effect,
		PointEx characterReach,
		float& collisionFraction)
	{
		std::shared_ptr<NPC> bestTarget;
		int bestPriority = INT_MAX;
		collisionFraction = 1.0f;
		if (effect->skipsCharacterCollision()) return bestTarget;
		const auto& tiles = gm->map->dataMap.tile;
		if (tiles.empty() || tiles.front().empty()) return bestTarget;
		const Point startTile = effect->collisionSweepInitialized
			? effect->collisionSweepStartPosition : effect->position;
		const PointEx startOffset = effect->collisionSweepInitialized
			? effect->collisionSweepStartOffset : effect->offset;
		const auto start = Map::getTilePositionEx(startTile, { 0, 0 }) + startOffset;
		const auto end = Map::getTilePositionEx(effect->position, { 0, 0 }) + effect->offset;
		const float radius = std::max(0.0f, effect->width * 0.5f) * TILE_HEIGHT / std::sqrt(2.0f);
		characterReach.x += radius * MapXRatio;
		characterReach.y += radius;
		auto index = [](double value, size_t size)
		{
			return static_cast<int>(std::clamp(value, 0.0, static_cast<double>(size - 1)));
		};
		// The extra column covers the half-tile horizontal shift of odd rows.
		const int left = index(std::floor((std::min(start.x, end.x) - characterReach.x) / TILE_WIDTH) - 1, tiles.front().size());
		const int right = index(std::ceil((std::max(start.x, end.x) + characterReach.x) / TILE_WIDTH) + 1, tiles.front().size());
		const int top = index(std::floor((std::min(start.y, end.y) - characterReach.y) / (TILE_HEIGHT / 2)) - 1, tiles.size());
		const int bottom = index(std::ceil((std::max(start.y, end.y) + characterReach.y) / (TILE_HEIGHT / 2)) + 1, tiles.size());
		std::unordered_set<NPC*> visited;
		auto consider = [&](const std::list<std::shared_ptr<NPC>>& candidates)
		{
			for (const auto& npc : candidates)
			{
				if (!npc || !visited.insert(npc.get()).second || !checkNPCAtPosition(npc, effect) || !canCollide(npc, effect)) continue;
				float fraction = 1.0f;
				if (!getNPCCollisionFraction(npc, effect, fraction)) continue;
				const int priority = getCollisionPriority(npc, effect);
				if (!bestTarget || fraction < collisionFraction || (fraction == collisionFraction && priority < bestPriority))
				{
					bestTarget = npc;
					bestPriority = priority;
					collisionFraction = fraction;
				}
			}
		};
		for (int y = top; y <= bottom; ++y)
		{
			for (int x = left; x <= right; ++x)
			{
				consider(tiles[y][x].npcList);
				consider(tiles[y][x].stepNPCList);
			}
		}
		return bestTarget;
	}

	float getWallCollisionFraction(const std::shared_ptr<Effect>& effect, Point wall)
	{
		if (!effect->collisionSweepInitialized) return 1.0f;
		const auto tile = Map::getTilePositionEx(wall, effect->collisionSweepStartPosition);
		const auto displacement = Map::getTilePositionEx(effect->position, effect->collisionSweepStartPosition)
			+ effect->offset - effect->collisionSweepStartOffset;
		float firstContact = 1.0f;
		for (const auto offset : effect->getPathOffsets())
		{
			const auto start = effect->collisionSweepStartOffset + offset - tile;
			float entry = 0.0f;
			float exit = 1.0f;
			// A tile is a diamond: clipping its two diagonal coordinates gives
			// the entry time of the same centre/edge rays used by getPassPath.
			for (const float sign : { -1.0f, 1.0f })
			{
				const float position = start.x / (TILE_WIDTH / 2) + sign * start.y / (TILE_HEIGHT / 2);
				const float movement = displacement.x / (TILE_WIDTH / 2) + sign * displacement.y / (TILE_HEIGHT / 2);
				if (movement == 0.0f)
				{
					if (std::abs(position) > 1.0f) exit = -1.0f;
					continue;
				}
				float begin = (-1.0f - position) / movement;
				float end = (1.0f - position) / movement;
				if (begin > end) std::swap(begin, end);
				entry = std::max(entry, begin);
				exit = std::min(exit, end);
			}
			if (entry <= exit) firstContact = std::min(firstContact, entry);
		}
		return firstContact;
	}

	bool checkNPCAtPosition(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (!npc || !npc->isPresentForRuntime() || npc->isDying() || npc->isHiding()) return false;
		if (npc->getJumpState() == jsJumping) return false;
		if (npc == gm->player)
		{
			return true;
		}
		if (!npc->isFighterLike() || !gm->npcManager->findNPC(npc))
		{
			return false;
		}
		if (npc->kind != nkPartner || gm->global.data.PartnerCombat)
		{
			return true;
		}
		return effect != nullptr && effect->magic.attackAll > 0;
	}

	bool canAffectOppositeProjectile(
		const std::shared_ptr<Effect>& effect,
		const std::shared_ptr<Effect>& other)
	{
		if (!isActiveProjectile(effect) || !isActiveProjectile(other) ||
			effect == other ||
			!canParticipateInProjectileCollision(effect) ||
			!canParticipateInProjectileCollision(other))
		{
			return false;
		}
		const bool canDiscard = effect->magic.discardOppositeMagic > 0 &&
			other->canBeDiscardedByOppositeMagic();
		const bool canExchange = effect->magic.exchangeUser > 0 &&
			other->canExchangeUserByOppositeMagic();
		return (canDiscard || canExchange) && effect->isOppositeEffect(other);
	}

	bool getEffectCollisionFraction(
		const std::shared_ptr<Effect>& effect,
		const std::shared_ptr<Effect>& other,
		float& collisionFraction)
	{
		const float combinedRadius =
			(effect->width + other->width) * 0.5f;
		if (combinedRadius < 0.0f)
		{
			return false;
		}

		const PointEx relativeStart = getCollisionSpaceDelta(
			other->collisionSweepStartPosition,
			other->collisionSweepStartOffset,
			effect->collisionSweepStartPosition,
			effect->collisionSweepStartOffset);
		const PointEx effectMovement = getCollisionSpaceDelta(
			effect->collisionSweepStartPosition,
			effect->collisionSweepStartOffset,
			effect->position,
			effect->offset);
		const PointEx otherMovement = getCollisionSpaceDelta(
			other->collisionSweepStartPosition,
			other->collisionSweepStartOffset,
			other->position,
			other->offset);
		const PointEx relativeMovement =
			effectMovement - otherMovement;
		constexpr double diagonalTileStepLength =
			(double)TILE_HEIGHT / 1.41421356237;
		const double radius =
			(double)combinedRadius * diagonalTileStepLength;
		return getSweepCollisionFraction(
			relativeStart,
			relativeMovement,
			radius,
			collisionFraction);
	}

	struct EffectCollisionEvent
	{
		float fraction = 0.0f;
		size_t effectIndex = 0;
		size_t otherIndex = 0;
	};

	void processEffectCollisions(
		const std::vector<std::shared_ptr<Effect>>& effectList,
		const std::vector<bool>& collisionDetectionEligible,
		std::vector<bool>& collisionStates)
	{
		std::vector<size_t> specialEffectIndices;
		for (size_t effectIndex = 0; effectIndex < effectList.size(); ++effectIndex)
		{
			const auto& effect = effectList[effectIndex];
			if (collisionDetectionEligible[effectIndex] &&
				!collisionStates[effectIndex] && isSpecialProjectile(effect))
			{
				specialEffectIndices.push_back(effectIndex);
			}
		}
		if (specialEffectIndices.empty())
		{
			return;
		}

		std::vector<EffectCollisionEvent> collisionEvents;
		for (const size_t effectIndex : specialEffectIndices)
		{
			const auto& effect = effectList[effectIndex];
			for (size_t otherIndex = 0; otherIndex < effectList.size(); ++otherIndex)
			{
				if (collisionStates[otherIndex])
				{
					continue;
				}
				const auto& other = effectList[otherIndex];
				if (!canAffectOppositeProjectile(effect, other))
				{
					continue;
				}
				float collisionFraction = 0.0f;
				if (getEffectCollisionFraction(
					effect, other, collisionFraction))
				{
					collisionEvents.push_back(
						{ collisionFraction, effectIndex, otherIndex });
				}
			}
		}

		std::sort(
			collisionEvents.begin(),
			collisionEvents.end(),
			[](const EffectCollisionEvent& left,
				const EffectCollisionEvent& right)
			{
				if (left.fraction != right.fraction)
				{
					return left.fraction < right.fraction;
				}
				if (left.effectIndex != right.effectIndex)
				{
					return left.effectIndex < right.effectIndex;
				}
				return left.otherIndex < right.otherIndex;
			});

		for (const auto& event : collisionEvents)
		{
			if (collisionStates[event.effectIndex] ||
				collisionStates[event.otherIndex])
			{
				continue;
			}
			auto effect = effectList[event.effectIndex];
			auto other = effectList[event.otherIndex];
			if (!canAffectOppositeProjectile(effect, other))
			{
				continue;
			}
			const CollisionPosition effectCollisionPosition =
				getCollisionPosition(effect, event.fraction);
			const CollisionPosition otherCollisionPosition =
				getCollisionPosition(other, event.fraction);
			effect->position = effectCollisionPosition.position;
			effect->offset = effectCollisionPosition.offset;
			other->position = otherCollisionPosition.position;
			other->offset = otherCollisionPosition.offset;
			if (effect->handleDiscardOppositeMagic(other, true) ||
				effect->handleExchangeUserWithOppositeMagic(other, true))
			{
				collisionStates[event.effectIndex] = true;
				collisionStates[event.otherIndex] = true;
			}
		}
	}
}

void CollisionDetector::detectCollision()
{
	auto effectList = gm->effectManager->effectList;
	const PointEx characterReach = getCharacterCollisionReach();
	const bool hasSpecialProjectile = std::any_of(
		effectList.begin(),
		effectList.end(),
		[](const std::shared_ptr<Effect>& effect)
		{
			return isSpecialProjectile(effect) &&
				!effect->isEnteringWithMeteor();
		});
	std::vector<bool> collisionDetectionEligible;
	std::vector<bool> collisionStates;
	if (hasSpecialProjectile)
	{
		collisionDetectionEligible.assign(effectList.size(), false);
		collisionStates.assign(effectList.size(), false);
	}

	for (size_t effectIndex = 0; effectIndex < effectList.size(); ++effectIndex)
	{
		auto& effect = effectList[effectIndex];
		if (!effect) continue;
		if (effect->doing != ekFlying && effect->doing != ekThrowing) continue;
		if (effect->isEnteringWithMeteor()) continue;
		if (hasSpecialProjectile)
		{
			collisionDetectionEligible[effectIndex] = true;
		}

		bool collided = false;

		float characterFraction = 1.0f;
		auto target = effect->doing == ekThrowing ? nullptr
			: findBestCollisionTarget(effect, characterReach, characterFraction);
		bool wallFound = false;
		float wallFraction = 1.0f;
		Point wallPosition = effect->position;
		Point wallFallback = effect->position;
		if (!effect->canPassThroughWall())
		{
			Point fallback = effect->position;
			auto considerWall = [&](Point position)
			{
				if (gm->map->canFly(position))
				{
					fallback = position;
					return;
				}
				const float fraction = getWallCollisionFraction(effect, position);
				if (!wallFound || fraction < wallFraction)
				{
					wallFound = true;
					wallFraction = fraction;
					wallPosition = position;
					wallFallback = fallback;
				}
			};
			if (effect->doing != ekThrowing && effect->collisionSweepInitialized)
			{
				for (const auto position : effect->passPath) considerWall(position);
			}
			considerWall(effect->position);
		}
		if (target && (!wallFound || characterFraction <= wallFraction))
		{
			collided = processCollision(target, effect, getCollisionPosition(effect, characterFraction));
		}
		if (!collided && wallFound)
		{
			if (!effect->handleBallWallCollision(wallPosition, wallFallback))
			{
				effect->beginExplode(wallPosition);
			}
			collided = true;
		}

		if (hasSpecialProjectile)
		{
			collisionStates[effectIndex] = collided;
		}
		else
		{
			finishEffectCollisionFrame(effect, collided);
		}
	}

	if (!hasSpecialProjectile)
	{
		return;
	}

	processEffectCollisions(
		effectList,
		collisionDetectionEligible,
		collisionStates);

	for (size_t effectIndex = 0; effectIndex < effectList.size(); ++effectIndex)
	{
		auto& effect = effectList[effectIndex];
		if (!collisionDetectionEligible[effectIndex])
		{
			continue;
		}
		finishEffectCollisionFrame(effect, collisionStates[effectIndex]);
	}
}

bool CollisionDetector::detectCollision(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
{
	if (!checkNPCAtPosition(npc, effect)) return false;
	return processCollisionWithCheck(npc, effect);
}

bool CollisionDetector::detectCollisionPass(
	std::shared_ptr<NPC> npc,
	std::shared_ptr<Effect> effect,
	Point)
{
	if (!checkNPCAtPosition(npc, effect)) return false;
	return processCollisionWithCheck(npc, effect);
}
