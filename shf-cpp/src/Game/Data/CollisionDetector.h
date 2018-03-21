#pragma once
#include "NPC.h"
#include "Effect.h"

namespace CollisionDetector
{
	void detectCollision();
	bool detectCollision(NPC * npc, Effect * effect);
	bool detectCollisionPass(NPC * npc, Effect * effect, int idx);
};

