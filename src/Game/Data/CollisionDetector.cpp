#include "CollisionDetector.h"
#include "../GameManager/GameManager.h"


void CollisionDetector::detectCollision()
{
	std::vector<std::shared_ptr<NPC>> collisionList;
	if (gm->player->nowAction != acHide && gm->player->nowAction != acDeath)
	{
		collisionList.push_back(gm->player);
	}
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i].get() != nullptr && gm->npcManager->npcList[i]->kind == nkBattle)
		{
			collisionList.push_back(gm->npcManager->npcList[i]);
		}
	}
	for (size_t i = 0; i < gm->effectManager->effectList.size(); i++)
	{
		if (gm->effectManager->effectList[i].get() != nullptr && gm->effectManager->effectList[i]->doing == ekFlying ||
			gm->effectManager->effectList[i].get() != nullptr && gm->effectManager->effectList[i]->doing == ekThrowing)
		{
			bool collided = false;
			for (size_t j = 0; j < gm->effectManager->effectList[i]->passPath.size(); j++)
			{
				if (gm->effectManager->effectList[i].get() != nullptr && gm->effectManager->effectList[i]->doing != ekThrowing)
				{
					for (int k = 0; k < (int)collisionList.size(); k++)
					{
						if (gm->npcManager->findNPC(collisionList[k]) && detectCollisionPass(collisionList[k], gm->effectManager->effectList[i], gm->effectManager->effectList[i]->passPath[j]))
						{
							collided = true;
							break;
						}
					}
				}
				
				if (!collided)
				{
					if (!gm->map->canFly(gm->effectManager->effectList[i]->passPath[j]))
					{
						gm->effectManager->effectList[i]->beginExplode(gm->effectManager->effectList[i]->passPath[j]);
						collided = true;
						break;
					}
				}
			}

			if (!collided)
			{
				if (gm->effectManager->effectList[i].get() != nullptr && gm->effectManager->effectList[i]->doing != ekThrowing)
				{
					for (int j = 0; j < (int)collisionList.size(); j++)
					{
						if (gm->npcManager->findNPC(collisionList[j]) && detectCollision(collisionList[j], gm->effectManager->effectList[i]))
						{
							collided = true;
							break;
						}
					}
				}

				if (!collided)
				{
					if (!gm->map->canFly(gm->effectManager->effectList[i]->position))
					{
						gm->effectManager->effectList[i]->beginExplode(gm->effectManager->effectList[i]->position);
					}
				}
			}					
		}
	}
}

bool CollisionDetector::detectCollision(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect)
{
	if (npc == gm->player)
	{
		if (effect->launcherKind == lkEnemy)
		{
			if (effect->checkCollide(npc))
			{
				effect->beginExplode(npc->position);
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
					effect->beginExplode(npc->position);			
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
					effect->beginExplode(npc->position);

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

bool CollisionDetector::detectCollisionPass(std::shared_ptr<NPC> npc, std::shared_ptr<Effect> effect, Point pos)
{
	if (npc == gm->player)
	{
		if (effect->launcherKind == lkEnemy)
		{
			if (npc->position == pos)
			{
				effect->beginExplode(npc->position);
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
				if (npc->position == pos)
				{
					effect->beginExplode(npc->position);
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
				if (npc->position == pos)
				{
					effect->beginExplode(npc->position);
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
