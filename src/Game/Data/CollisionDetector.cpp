#include "CollisionDetector.h"
#include "NPCManager.h"
#include "../GameManager/GameManager.h"
#include <climits>
#include <list>

namespace
{
	bool checkNPCAtPosition(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect);

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

	bool isPreciseHit(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (!npc || !effect) return false;

		float dist = Map::getTileDistance(
			effect->position, effect->offset,
			npc->getPosition(), npc->getOffset()
		);

		float effectRadius = effect->width * 0.5f;
		float npcRadius = npc->radius;
		return dist <= effectRadius + npcRadius;
	}

	bool processCollision(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect, Point collidePos)
	{
		if (!canCollide(npc, effect)) return false;
		if (effect->skipsCharacterCollision()) return false;

		if (isPreciseHit(npc, effect))
		{
			if (effect->handleCarryUser4AfterHit(npc))
			{
				npc->hurt(effect);
				return true;
			}
			if (effect->canBall())
			{
				effect->handleBallAfterHit(npc, collidePos);
				npc->hurt(effect);
				return true;
			}
			if (effect->handleStickyAfterHit(npc))
			{
				npc->hurt(effect);
				return true;
			}
			if (effect->canParasitic() && effect->beginParasitic(npc, collidePos))
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
				npc->hurt(effect);
				effect->handlePassThroughAfterHit(npc, collidePos);
				return true;
			}
			effect->beginExplode(collidePos);
			npc->hurt(effect);
			return true;
		}
		return false;
	}

	bool processCollisionWithCheck(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
	{
		if (!canCollide(npc, effect)) return false;
		if (effect->skipsCharacterCollision()) return false;

		if (isPreciseHit(npc, effect))
		{
			if (effect->handleCarryUser4AfterHit(npc))
			{
				npc->hurt(effect);
				return true;
			}
			if (effect->canBall())
			{
				effect->handleBallAfterHit(npc, effect->position);
				npc->hurt(effect);
				return true;
			}
			if (effect->handleStickyAfterHit(npc))
			{
				npc->hurt(effect);
				return true;
			}
			if (effect->canParasitic() && effect->beginParasitic(npc, effect->position))
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
				npc->hurt(effect);
				effect->handlePassThroughAfterHit(npc, effect->position);
				return true;
			}
			effect->beginExplode(effect->position);
			npc->hurt(effect);
			return true;
		}
		return false;
	}

	void considerBestCollisionTarget(
		const std::list<std::shared_ptr<NPC>>& candidateList,
		std::shared_ptr<Effect> effect,
		std::shared_ptr<NPC>& bestTarget,
		int& bestPriority)
	{
		for (auto& npc : candidateList)
		{
			if (!checkNPCAtPosition(npc, effect) || effect == nullptr || effect->skipsCharacterCollision() || !canCollide(npc, effect) || !isPreciseHit(npc, effect))
			{
				continue;
			}
			int priority = getCollisionPriority(npc, effect);
			if (priority < bestPriority)
			{
				bestTarget = npc;
				bestPriority = priority;
			}
		}
	}

	std::shared_ptr<NPC> findBestCollisionTarget(const std::list<std::shared_ptr<NPC>>& npcList, const std::list<std::shared_ptr<NPC>>& stepNPCList, std::shared_ptr<Effect> effect)
	{
		std::shared_ptr<NPC> bestTarget = nullptr;
		int bestPriority = INT_MAX;
		considerBestCollisionTarget(npcList, effect, bestTarget, bestPriority);
		considerBestCollisionTarget(stepNPCList, effect, bestTarget, bestPriority);
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

	bool processEffectCollision(std::shared_ptr<Effect> effect, const std::vector<std::shared_ptr<Effect>>& effectList)
	{
		if (effect == nullptr || (effect->doing != ekFlying && effect->doing != ekThrowing))
		{
			return false;
		}
		for (auto& other : effectList)
		{
			if (other == nullptr || other == effect || (other->doing != ekFlying && other->doing != ekThrowing))
			{
				continue;
			}
			if (effect->handleDiscardOppositeMagic(other))
			{
				return true;
			}
			if (effect->handleExchangeUserWithOppositeMagic(other))
			{
				return true;
			}
		}
		return false;
	}
}

void CollisionDetector::detectCollision()
{
	auto effectList = gm->effectManager->effectList;
	auto& dataMap = gm->map->dataMap;

	for (auto& effect : effectList)
	{
		if (!effect) continue;
		if (effect->doing != ekFlying && effect->doing != ekThrowing) continue;
		if (effect->isEnteringWithMeteor()) continue;

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
					auto target = findBestCollisionTarget(tile.npcList, tile.stepNPCList, effect);
					if (target != nullptr && processCollision(target, effect, pos))
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
				auto target = findBestCollisionTarget(tile.npcList, tile.stepNPCList, effect);
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

		if (!collided && effect)
		{
			collided = processEffectCollision(effect, effectList);
		}

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
}

bool CollisionDetector::detectCollision(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
{
	if (!checkNPCAtPosition(npc, effect)) return false;
	return processCollisionWithCheck(npc, effect);
}

bool CollisionDetector::detectCollisionPass(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect, Point pos)
{
	if (!checkNPCAtPosition(npc, effect)) return false;
	return processCollision(npc, effect, pos);
}
