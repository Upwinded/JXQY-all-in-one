#include "CollisionDetector.h"
#include "../GameManager/GameManager.h"

void CollisionDetector::detectCollision()
{
	std::vector<NPC *> collisionList = {};
	if (gm->player.nowAction != acHide && gm->player.nowAction != acDeath)
	{
		collisionList.push_back(&gm->player);
	}
	for (size_t i = 0; i < gm->npcManager.npcList.size(); i++)
	{
		if (gm->npcManager.npcList[i] != NULL && gm->npcManager.npcList[i]->kind == nkBattle)
		{
			collisionList.push_back(gm->npcManager.npcList[i]);
		}
	}
	for (size_t i = 0; i < gm->effectManager.effectList.size(); i++)
	{
		if (gm->effectManager.effectList[i] != NULL && gm->effectManager.effectList[i]->doing == ekFlying ||
			gm->effectManager.effectList[i] != NULL && gm->effectManager.effectList[i]->doing == ekThrowing)
		{
			bool collided = false;
			for (size_t j = 0; j < gm->effectManager.effectList[i]->passPath.size(); j++)
			{
				if (gm->effectManager.effectList[i] != NULL && gm->effectManager.effectList[i]->doing != ekThrowing)
				{
					for (int k = 0; k < (int)collisionList.size(); k++)
					{
						if (gm->npcManager.findNPC(collisionList[k]) && detectCollisionPass(collisionList[k], gm->effectManager.effectList[i], j))
						{
							collided = true;
							break;
						}
					}
				}
				
				if (!collided)
				{
					if (!gm->map.canFly(gm->effectManager.effectList[i]->passPath[j]))
					{
						gm->effectManager.effectList[i]->position = gm->effectManager.effectList[i]->passPath[j];
						gm->effectManager.effectList[i]->beginExplode();
						collided = true;
					}
				}
			}

			if (!collided)
			{
				if (gm->effectManager.effectList[i] != NULL && gm->effectManager.effectList[i]->doing != ekThrowing)
				{
					for (int j = 0; j < (int)collisionList.size(); j++)
					{
						if (gm->npcManager.findNPC(collisionList[j]) && detectCollision(collisionList[j], gm->effectManager.effectList[i]))
						{
							collided = true;
							break;
						}
					}
				}

				if (!collided)
				{
					if (!gm->map.canFly(gm->effectManager.effectList[i]->position))
					{
						gm->effectManager.effectList[i]->beginExplode();
					}
				}
			}					
		}
	}
}

bool CollisionDetector::detectCollision(NPC * npc, Effect * effect)
{
	if (npc == &gm->player)
	{
		if (effect->launcherKind == lkEnemy)
		{
			if (effect->checkCollide(npc))
			{
				effect->beginExplode();
				npc->hurt(effect);
				return true;
			}
		}
	}
	else
	{
		switch (npc->relation)
		{
		case nrFriendly:
		{
			if (effect->launcherKind == lkEnemy)
			{
				if (effect->checkCollide(npc))
				{
					effect->beginExplode();			
					npc->hurt(effect);
					
					return true;
				}
			}
			break;
		}
		case nrHostile:
		{
			if (effect->launcherKind == lkSelf || effect->launcherKind == lkFriend)
			{
				if (effect->checkCollide(npc))
				{
					effect->beginExplode();

					npc->hurt(effect);
					return true;
				}
			}
			break;
		}
		case nrNeutral:
		{
			break;
		}
		default:
			break;
		}
	}
	
	return false;
}

bool CollisionDetector::detectCollisionPass(NPC * npc, Effect * effect, int idx)
{
	if (npc == &gm->player)
	{
		if (effect->launcherKind == lkEnemy)
		{
			if (npc->position.x == effect->passPath[idx].x && npc->position.y == effect->passPath[idx].y)
			{
				effect->position = effect->passPath[idx];
				effect->offset = { 0, 0 };
				effect->beginExplode();
				npc->hurt(effect);
				return true;
			}
		}
	}
	else
	{
		switch (npc->relation)
		{
		case nrFriendly:
		{
			if (effect->launcherKind == lkEnemy)
			{
				if (npc->position.x == effect->passPath[idx].x && npc->position.y == effect->passPath[idx].y)
				{
					effect->position = effect->passPath[idx];
					effect->offset = { 0, 0 };
					effect->beginExplode();
					npc->hurt(effect);

					return true;
				}
			}
			break;
		}
		case nrHostile:
		{
			if (effect->launcherKind == lkSelf || effect->launcherKind == lkFriend)
			{
				if (npc->position.x == effect->passPath[idx].x && npc->position.y == effect->passPath[idx].y)
				{
					effect->position = effect->passPath[idx];
					effect->offset = { 0, 0 };
					effect->beginExplode();
					npc->hurt(effect);
					return true;
				}
			}
			break;
		}
		case nrNeutral:
		{
			break;
		}
		default:
			break;
		}
	}

	return false;
}
