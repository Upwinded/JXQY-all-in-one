#pragma once

#include "../../Types/ElementTypes.h"

#include <cstddef>
#include <memory>
#include <vector>

class GameElement;
class Map;
class NPC;
class NPCManager;
class Object;
class ObjectManager;

enum class WorldInteractionIntent
{
	Primary,
	Alternate,
	Attack,
};

enum class WorldInteractionTargetType
{
	None,
	Object,
	NPC,
};

enum class WorldInteractionScriptSide
{
	Primary,
	Alternate,
};

struct WorldInteractionQuery
{
	Point origin = { 0, 0 };
	int facingDirection = 0;
	int radius = 13;
	int nearRadius = 2;
	std::weak_ptr<GameElement> preferredTarget;
};

struct WorldInteractionCandidate
{
	WorldInteractionIntent intent = WorldInteractionIntent::Primary;
	WorldInteractionTargetType targetType = WorldInteractionTargetType::None;
	std::shared_ptr<Object> object;
	std::shared_ptr<NPC> npc;
	int distance = 0;
	int facingDifference = 0;
	bool executionDistanceReached = false;
	bool inNearRange = false;
	bool preferred = false;
	std::size_t stableOrder = 0;

	bool isValid() const;
	std::shared_ptr<GameElement> getTarget() const;
};

class WorldInteractionResolver
{
public:
	static bool isObjectValidForIntent(
		const std::shared_ptr<Object>& object,
		WorldInteractionIntent intent);
	static bool isNPCValidForIntent(
		const std::shared_ptr<NPC>& npc,
		WorldInteractionIntent intent,
		const std::shared_ptr<NPC>& actionActor);

	static std::vector<WorldInteractionCandidate> findCandidates(
		WorldInteractionIntent intent,
		const WorldInteractionQuery& query,
		Map* map,
		NPCManager* npcManager,
		ObjectManager* objectManager,
		const std::shared_ptr<NPC>& actionActor);

	static WorldInteractionCandidate findBestCandidate(
		WorldInteractionIntent intent,
		const WorldInteractionQuery& query,
		Map* map,
		NPCManager* npcManager,
		ObjectManager* objectManager,
		const std::shared_ptr<NPC>& actionActor);
};
