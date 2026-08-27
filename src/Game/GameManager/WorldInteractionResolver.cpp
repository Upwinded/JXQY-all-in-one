#include "WorldInteractionResolver.h"

#include "../Data/Map.h"
#include "../Data/NPC.h"
#include "../Data/NPCManager.h"
#include "../Data/Object.h"
#include "../Data/ObjectManager.h"

#include <algorithm>
#include <cstdlib>

namespace
{
int getFacingDifference(int facingDirection, Point origin, Point target, int distance)
{
	if (distance == 0)
	{
		return 0;
	}

	int normalizedFacingDirection = GameElement::normalizeDir(facingDirection);
	int targetDirection = NPC::getDirection(origin, target);
	int difference = std::abs(targetDirection - normalizedFacingDirection);
	return std::min(difference, 8 - difference);
}

bool candidateComesBefore(const WorldInteractionCandidate& left, const WorldInteractionCandidate& right)
{
	if (left.preferred != right.preferred)
	{
		return left.preferred;
	}
	if (left.executionDistanceReached != right.executionDistanceReached)
	{
		return left.executionDistanceReached;
	}
	if (left.inNearRange != right.inNearRange)
	{
		return left.inNearRange;
	}
	if (left.facingDifference != right.facingDifference)
	{
		return left.facingDifference < right.facingDifference;
	}
	if (left.distance != right.distance)
	{
		return left.distance < right.distance;
	}
	if (left.targetType != right.targetType)
	{
		return left.targetType == WorldInteractionTargetType::Object;
	}
	return left.stableOrder < right.stableOrder;
}
}

bool WorldInteractionResolver::isObjectValidForIntent(
	const std::shared_ptr<Object>& object, WorldInteractionIntent intent)
{
	if (object == nullptr || !object->canSelectForInteraction())
	{
		return false;
	}

	switch (intent)
	{
	case WorldInteractionIntent::Primary:
		return object->hasAnyInteractScript();
	case WorldInteractionIntent::Alternate:
		return !object->scriptFileRight.empty();
	case WorldInteractionIntent::Attack:
	default:
		return false;
	}
}

bool WorldInteractionResolver::isNPCValidForIntent(
	const std::shared_ptr<NPC>& npc, WorldInteractionIntent intent,
	const std::shared_ptr<NPC>& actionActor)
{
	if (npc == nullptr || npc == actionActor || !npc->isVisibleForRuntime()
		|| npc->isDying() || npc->isHiding() || npc->isHiddenByCarryMagic())
	{
		return false;
	}

	switch (intent)
	{
	case WorldInteractionIntent::Primary:
		return !npc->scriptFile.empty() || !npc->scriptFileRight.empty();
	case WorldInteractionIntent::Alternate:
		return !npc->scriptFileRight.empty();
	case WorldInteractionIntent::Attack:
		return NPCManager::canLauncherHitNPC(lkSelf, npc, actionActor);
	default:
		return false;
	}
}

bool WorldInteractionCandidate::isValid() const
{
	if (targetType == WorldInteractionTargetType::Object)
	{
		return object != nullptr;
	}
	if (targetType == WorldInteractionTargetType::NPC)
	{
		return npc != nullptr;
	}
	return false;
}

std::shared_ptr<GameElement> WorldInteractionCandidate::getTarget() const
{
	if (targetType == WorldInteractionTargetType::Object)
	{
		return object;
	}
	if (targetType == WorldInteractionTargetType::NPC)
	{
		return npc;
	}
	return nullptr;
}

std::vector<WorldInteractionCandidate> WorldInteractionResolver::findCandidates(
	WorldInteractionIntent intent,
	const WorldInteractionQuery& query,
	Map* map,
	NPCManager* npcManager,
	ObjectManager* objectManager,
	const std::shared_ptr<NPC>& actionActor)
{
	std::vector<WorldInteractionCandidate> candidates;
	if (map == nullptr || actionActor == nullptr)
	{
		return candidates;
	}

	const int radius = std::max(0, query.radius);
	const int nearRadius = std::clamp(query.nearRadius, 0, radius);
	auto preferredTarget = query.preferredTarget.lock();

	if (intent != WorldInteractionIntent::Attack && objectManager != nullptr)
	{
		for (std::size_t index = 0; index < objectManager->objectList.size(); ++index)
		{
			auto object = objectManager->objectList[index];
			if (!isObjectValidForIntent(object, intent))
			{
				continue;
			}

			int distance = Map::calDistance(query.origin, object->position);
			if (distance > radius || !map->canSee(query.origin, object->position))
			{
				continue;
			}

			WorldInteractionCandidate candidate;
			candidate.intent = intent;
			candidate.targetType = WorldInteractionTargetType::Object;
			candidate.object = object;
			candidate.distance = distance;
			candidate.facingDifference = getFacingDifference(
				query.facingDirection, query.origin, object->position, distance);
			candidate.executionDistanceReached = object->canInteractAtDistance(distance);
			candidate.inNearRange = distance <= nearRadius;
			candidate.preferred = preferredTarget != nullptr && preferredTarget == object;
			candidate.stableOrder = index;
			candidates.push_back(candidate);
		}
	}

	if (npcManager != nullptr)
	{
		for (std::size_t index = 0; index < npcManager->npcList.size(); ++index)
		{
			auto npc = npcManager->npcList[index];
			if (!isNPCValidForIntent(npc, intent, actionActor))
			{
				continue;
			}

			int distance = Map::calDistance(query.origin, npc->getPosition());
			if (distance > radius || !map->canSee(query.origin, npc->getPosition()))
			{
				continue;
			}

			WorldInteractionCandidate candidate;
			candidate.intent = intent;
			candidate.targetType = WorldInteractionTargetType::NPC;
			candidate.npc = npc;
			candidate.distance = distance;
			candidate.facingDifference = getFacingDifference(
				query.facingDirection, query.origin, npc->getPosition(), distance);
			candidate.executionDistanceReached = intent == WorldInteractionIntent::Attack
				? distance <= actionActor->attackRadius
				: npc->canTalkAtDistance(distance);
			candidate.inNearRange = distance <= nearRadius;
			candidate.preferred = preferredTarget != nullptr && preferredTarget == npc;
			candidate.stableOrder = index;
			candidates.push_back(candidate);
		}
	}

	std::stable_sort(candidates.begin(), candidates.end(), candidateComesBefore);
	return candidates;
}

WorldInteractionCandidate WorldInteractionResolver::findBestCandidate(
	WorldInteractionIntent intent,
	const WorldInteractionQuery& query,
	Map* map,
	NPCManager* npcManager,
	ObjectManager* objectManager,
	const std::shared_ptr<NPC>& actionActor)
{
	auto candidates = findCandidates(intent, query, map, npcManager, objectManager, actionActor);
	if (candidates.empty())
	{
		return {};
	}
	return candidates.front();
}
