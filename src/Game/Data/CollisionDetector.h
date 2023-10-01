#pragma once
#include "NPC.h"
#include "Effect.h"

namespace CollisionDetector
{
	void detectCollision();
	bool detectCollision(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect);
	bool detectCollisionPass(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect, Point pos);
};

