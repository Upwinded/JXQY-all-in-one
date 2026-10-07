#include "CollisionDetector.h"
#include "NPCManager.h"
#include "../GameManager/GameManager.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <list>
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

		const PointEx movement = getCollisionSpaceDelta(
			effect->collisionSweepStartPosition,
			effect->collisionSweepStartOffset,
			effect->position,
			effect->offset);
		const float movementLengthSquared =
			movement.x * movement.x + movement.y * movement.y;
		if (movementLengthSquared <= 0.0f)
		{
			collisionFraction = 1.0f;
			return Map::getTileDistance(
				effect->position, effect->offset,
				npc->getPosition(), npc->getOffset()) <= combinedRadius;
		}

		const PointEx target = getCollisionSpaceDelta(
			effect->collisionSweepStartPosition,
			effect->collisionSweepStartOffset,
			npc->getPosition(),
			npc->getOffset());
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

	void considerBestCollisionTarget(
		const std::list<std::shared_ptr<NPC>>& candidateList,
		std::shared_ptr<Effect> effect,
		std::shared_ptr<NPC>& bestTarget,
		int& bestPriority,
		float& bestCollisionFraction)
	{
		for (auto& npc : candidateList)
		{
			float collisionFraction = 1.0f;
			if (!checkNPCAtPosition(npc, effect) || effect == nullptr || effect->skipsCharacterCollision() || !canCollide(npc, effect) || !getNPCCollisionFraction(npc, effect, collisionFraction))
			{
				continue;
			}
			int priority = getCollisionPriority(npc, effect);
			if (priority < bestPriority ||
				(priority == bestPriority &&
					collisionFraction < bestCollisionFraction))
			{
				bestTarget = npc;
				bestPriority = priority;
				bestCollisionFraction = collisionFraction;
			}
		}
	}

	std::shared_ptr<NPC> findBestCollisionTarget(
		const std::list<std::shared_ptr<NPC>>& npcList,
		const std::list<std::shared_ptr<NPC>>& stepNPCList,
		std::shared_ptr<Effect> effect,
		float& collisionFraction)
	{
		std::shared_ptr<NPC> bestTarget = nullptr;
		int bestPriority = INT_MAX;
		collisionFraction = 1.0f;
		considerBestCollisionTarget(
			npcList,
			effect,
			bestTarget,
			bestPriority,
			collisionFraction);
		considerBestCollisionTarget(
			stepNPCList,
			effect,
			bestTarget,
			bestPriority,
			collisionFraction);
		return bestTarget;
	}

	bool checkNPCAtPosition(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (!npc) return false;
		if (npc->getJumpState() == jsJumping) return false;
		if (npc == gm->player)
		{
			return npc->isVisibleForRuntime() && npc->nowAction != acHide && npc->nowAction != acDeath;
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
	auto& dataMap = gm->map->dataMap;
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

		if (effect->doing != ekThrowing)
		{
			Point fallbackPosition = effect->position;
			for (const auto& pos : effect->passPath)
			{
				if (!effect || effect->doing == ekThrowing) break;

				if (gm->map->isInMap(pos))
				{
					auto& tile = dataMap.tile[pos.y][pos.x];
					float collisionFraction = 1.0f;
					auto target = findBestCollisionTarget(
						tile.npcList,
						tile.stepNPCList,
						effect,
						collisionFraction);
					if (target != nullptr && processCollision(
						target,
						effect,
						getCollisionPosition(effect, collisionFraction)))
					{
						collided = true;
					}
				}

				if (collided) break;

				if (!effect->canPassThroughWall() && !gm->map->canFly(pos))
				{
					if (!effect->handleBallWallCollision(pos, fallbackPosition))
					{
						effect->beginExplode(pos);
					}
					collided = true;
					break;
				}
				if (gm->map->canFly(pos))
				{
					fallbackPosition = pos;
				}
			}
		}

		if (!collided && effect && effect->doing != ekThrowing)
		{
			Point pos = effect->position;
			if (gm->map->isInMap(pos))
			{
				auto& tile = dataMap.tile[pos.y][pos.x];
				float collisionFraction = 1.0f;
				auto target = findBestCollisionTarget(
					tile.npcList,
					tile.stepNPCList,
					effect,
					collisionFraction);
				if (target != nullptr && processCollisionWithCheck(target, effect))
				{
					collided = true;
				}
			}
		}

		if (!collided && effect && !effect->canPassThroughWall() && !gm->map->canFly(effect->position))
		{
			if (!effect->handleBallWallCollision(effect->position, effect->position))
			{
				effect->beginExplode(effect->position);
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
