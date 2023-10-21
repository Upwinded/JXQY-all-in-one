#include "NPCManager.h"
#include "../GameManager/GameManager.h"

NPCManager::NPCManager()
{
	priority = epGameManager;
	npcList.resize(0);
	needArrangeChild = false;
	canDraw = false;
}

NPCManager::~NPCManager()
{
	freeResource();
    if (player != nullptr)
    {
        player = nullptr;
    }
}

void NPCManager::setPlayer(std::shared_ptr<NPC> nowPlayer)
{
    player = nowPlayer;
}

void NPCManager::standAll()
{
	if (!(gm->player->isJumping() && gm->player->jumpState == jsJumping))
	{
		gm->player->beginStand();
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr)
		{
			npcList[i]->beginStand();
		}
	}
}

int NPCManager::findNPCIndex(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return -1;
	}
	if (npc.get() == gm->player.get())
	{
		return 0;
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] == npc)
		{
			return i + 1;
		}
	}
	return -1;
}

bool NPCManager::findNPC(std::shared_ptr<NPC> npc)
{
	return (findNPCIndex(npc) >= 0);
}

std::shared_ptr<NPC> NPCManager::findPlayerNPC()
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->kind == nkPlayer)
		{
			return npcList[i];
		}
	}
	return nullptr;
}

std::vector<std::shared_ptr<NPC>> NPCManager::findNPC(int launcherKind)
{
	return findNPC(launcherKind, { 0, 0 }, 0);
}

std::vector<std::shared_ptr<NPC>> NPCManager::findNPC(const std::string & npcName)
{
	std::vector<std::shared_ptr<NPC>> result;
	result.resize(0);
	if (npcName == gm->player->npcName)
	{
		result.push_back(gm->player);
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->npcName == npcName)
		{
			result.push_back(npcList[i]);
		}
	}
	return result;
}

std::vector<std::shared_ptr<NPC>> NPCManager::findNPC(int launcherKind, Point pos, int radius)
{
	std::vector<std::shared_ptr<NPC>> result;
	if (launcherKind == lkSelf)
	{
		result.push_back(gm->player);
		return result;
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i]->kind == nkBattle)
		{
			std::shared_ptr<NPC> tempNPC = nullptr;
			if (launcherKind == lkFriend && npcList[i]->relation == nrFriendly)
			{
				tempNPC = npcList[i];
			}
			else if (launcherKind == lkEnemy && npcList[i]->relation == nrHostile)
			{
				tempNPC = npcList[i];
			}
			else if (launcherKind == lkNeutral && npcList[i]->relation == nrNeutral)
			{
				tempNPC = npcList[i];
			}
			if (tempNPC != nullptr)
			{
				if (radius > 0)
				{
					int distance = gm->map->calDistance(tempNPC->position, pos);
					if (distance <= radius)
					{
						result.push_back(tempNPC);
					}
				}
				else
				{
					result.push_back(tempNPC);
				}
			}
		}
	}
	return result;
}
std::vector<std::shared_ptr<NPC>> NPCManager::findNPC(Point pos, int radius)
{
    std::vector<std::shared_ptr<NPC>> result;
    for (size_t i = 0; i < npcList.size(); i++)
    {
        auto tempNPC = npcList[i];
        if (tempNPC != nullptr)
        {
            if (radius > 0)
            {
                int distance = gm->map->calDistance(tempNPC->position, pos);
                if (distance <= radius)
                {
                    result.push_back(tempNPC);
                }
            }
            else
            {
                result.push_back(tempNPC);
            }
        }
    }
    return result;
}

std::shared_ptr<NPC> NPCManager::findNearestNPC(int launcherKind, Point pos, int radius)
{
	auto tempNPCList = findNPC(launcherKind, pos, radius);
	int temp = -1;
	int distance = radius + 1;
	for (int i = 0; i < (int)tempNPCList.size(); i++)
	{
		int tempDistance = Map::calDistance(pos, tempNPCList[i]->position);
		if (tempDistance < distance)
		{
			temp = i;
			distance = tempDistance;
		}
	}
	if (temp < 0)
	{
		return nullptr;
	}
	else
	{
		return tempNPCList[temp];
	}	
}

std::shared_ptr<NPC> NPCManager::findNearestViewNPC(int launcherKind, Point pos, int radius)
{
	auto tempNPCList = findNPC(launcherKind, pos, radius);
	int temp = -1;
	int distance = radius + 1;
	for (int i = 0; i < (int)tempNPCList.size(); i++)
	{
		int tempDistance = Map::calDistance(pos, tempNPCList[i]->position);
		if (tempDistance < distance && gm->map->canView(pos, tempNPCList[i]->position))
		{
            temp = i;
            distance = tempDistance;
		}
	}
	if (temp < 0)
	{
		return nullptr;
	}
	else
	{
		return tempNPCList[temp];
	}
}

std::shared_ptr<NPC> NPCManager::findNearestScriptViewNPC(Point pos, int radius)
{
    int temp = -1;
    int distance = radius + 1;
    for (size_t i = 0; i < npcList.size(); ++i)
    {
        if (npcList[i] == nullptr) { continue; }
        if (npcList[i]->relation == nrFriendly || npcList[i]->relation == nrNeutral)
        {
            int tempDistance = Map::calDistance(pos, npcList[i]->position);
            bool dialogRadiusLarger = (npcList[i]->dialogRadius >= tempDistance);
            if (npcList[i]->scriptFile != "" && (gm->map->canView(pos, npcList[i]->position) || dialogRadiusLarger))
            {
                if (tempDistance < distance || (dialogRadiusLarger && temp < 0))
                {
                    temp = i;
                    distance = tempDistance;
                }
            }
        }
    }
    if (temp >= 0)
    {
        return npcList[temp];
    }
    else
    {
        return nullptr;
    }
}

std::vector<std::shared_ptr<NPC>> NPCManager::findRadiusScriptViewNPC(Point pos, int radius)
{
	std::vector<std::shared_ptr<NPC>> ret;
	for (size_t i = 0; i < npcList.size(); ++i)
	{
		if (npcList[i] == nullptr) { continue; }
		if (npcList[i]->relation == nrFriendly || npcList[i]->relation == nrNeutral)
		{
			int tempDistance = Map::calDistance(pos, npcList[i]->position);
			bool dialogRadiusLarger = (npcList[i]->dialogRadius >= tempDistance);
			if (npcList[i]->scriptFile != "" && ((gm->map->canView(pos, npcList[i]->position) && tempDistance <= radius) || dialogRadiusLarger))
			{
				ret.push_back(npcList[i]);
			}
		}
	}
	return ret;
}

void NPCManager::deleteNPCFromOtherPlace(std::shared_ptr<NPC> npc)
{
	if (gm->camera->followNPC.get() == npc.get())
	{
		gm->camera->followNPC = nullptr;
	}
	removeChild(npc);
	tryCleanActionImageList();
}

void NPCManager::npcAutoAction()
{
	if (!gm->global.data.NPCAI)
	{
		return;
	}
	//根据人物的属性建立7个列表
	//常规列表，放置需要更新走动的常规NPC
	std::vector<std::shared_ptr<NPC>> normalList;
	//以下3个列表放置所有战斗类NPC，根据关系分类
	std::vector<std::shared_ptr<NPC>> friendList;
	std::vector<std::shared_ptr<NPC>> enemyList;
	std::vector<std::shared_ptr<NPC>> neutralList;
	//以下3个列表放置所有需要更新动作的NPC，根据关系分类
	std::vector<std::shared_ptr<NPC>> friendUpdateList;
	std::vector<std::shared_ptr<NPC>> enemyUpdateList;
	std::vector<std::shared_ptr<NPC>> neutralUpdateList;

	//友军列表添加玩家
	if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
	{
		if (gm->player->kind == nkPlayer || gm->player->kind == nkBattle || gm->player->kind == nkPartner)
		{
			if (gm->player->relation == nrFriendly)
			{
				friendList.push_back(gm->player);
			}
			else if (gm->player->relation == nrNeutral)
			{
				//neutralList.push_back(gm->player);
			}
			else if (gm->player->relation == nrHostile)
			{
				enemyList.push_back(gm->player);
			}
		}
	}

	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && !npcList[i]->isFollower())
		{
			if (npcList[i]->kind == nkBattle)
			{
				if (npcList[i]->nowAction != acDeath && npcList[i]->nowAction != acHide)
				{
					if (npcList[i]->relation == nrFriendly)
					{
						friendList.push_back(npcList[i]);
						if (npcList[i]->isStanding())
						{
							friendUpdateList.push_back(npcList[i]);
						}
					}
					else if (npcList[i]->relation == nrHostile)
					{
						enemyList.push_back(npcList[i]);
						if (npcList[i]->isStanding())
						{
							enemyUpdateList.push_back(npcList[i]);
						}
					}
					else if (npcList[i]->relation == nrNeutral)
					{
						//neutralList.push_back(npcList[i]);
						//if (npcList[i]->isStanding())
						//{
							//neutralUpdateList.push_back(npcList[i]);
						//}
					}
				}	
			}
			else if (npcList[i]->action != naNone && npcList[i]->isStanding() && (npcList[i]->getTime() > npcList[i]->walkTime + NPC_WALK_INTERVAL))
			{
				normalList.push_back(npcList[i]);
			}
		}
	}
	for (size_t i = 0; i < normalList.size(); i++)
	{
		if (gm->inEvent && gm->scriptType == stNPC && gm->scriptNPC == normalList[i])
		{
			continue;
		}
		int stepX = std::abs(rand()) % (NPC_WALK_STEP * 2 + 1) - NPC_WALK_STEP;
		int stepY = std::abs(rand()) % (NPC_WALK_STEP * 2 + 1) - NPC_WALK_STEP;
		if (stepX != 0 || stepY != 0)
		{
			stepX += normalList[i]->position.x;
			stepY += normalList[i]->position.y;
			int tvr = normalList[i]->visionRadius;
			normalList[i]->visionRadius = NPC_WALK_STEP * 2;
			if (normalList[i]->canView({ stepX, stepY }) && gm->map->canWalk({ stepX, stepY }))
			{
				normalList[i]->beginWalk({ stepX, stepY });
				normalList[i]->walkTime = normalList[i]->getTime() + std::abs(rand()) % NPC_WALK_INTERVAL_RANGE;
			}
			normalList[i]->visionRadius = tvr;
		}
	}
	for (size_t i = 0; i < friendUpdateList.size(); i++)
	{
		std::shared_ptr<NPC> e = nullptr;
		int distance = -1;
		for (size_t j = 0; j < enemyList.size(); j++)
		{
			if (friendUpdateList[i]->canView(enemyList[j]->position))
			{
				int temp = gm->map->calDistance(friendUpdateList[i]->position, enemyList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = enemyList[j];
				}
			}
		}
		for (size_t j = 0; j < neutralList.size(); j++)
		{
			if (friendUpdateList[i]->canView(neutralList[j]->position))
			{
				int temp = gm->map->calDistance(friendUpdateList[i]->position, neutralList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = neutralList[j];
				}
			}
		}
		if (e != nullptr)
		{
			if (distance <= friendUpdateList[i]->attackRadius)
			{
				friendUpdateList[i]->destGE = e;
				friendUpdateList[i]->beginAttack(e->position);
			}
			else
			{
				if (friendUpdateList[i]->pathFinder)
				{
					friendUpdateList[i]->beginRadiusWalk(e->position, friendUpdateList[i]->attackRadius);
				}
				else
				{
					friendUpdateList[i]->beginRadiusStep(e->position, friendUpdateList[i]->attackRadius);
				}
			}
		}
	}
	for (size_t i = 0; i < enemyUpdateList.size(); i++)
	{
		std::shared_ptr<NPC> e = nullptr;
		int distance = -1;
		for (size_t j = 0; j < friendList.size(); j++)
		{
			if (enemyUpdateList[i]->canView(friendList[j]->position))
			{
				int temp = gm->map->calDistance(enemyUpdateList[i]->position, friendList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = friendList[j];
				}
			}
		}
		for (size_t j = 0; j < neutralList.size(); j++)
		{
			if (enemyUpdateList[i]->canView(neutralList[j]->position))
			{
				int temp = gm->map->calDistance(enemyUpdateList[i]->position, neutralList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = neutralList[j];
				}
			}
		}
		if (e != nullptr)
		{
			if (distance <= enemyUpdateList[i]->attackRadius)
			{
				enemyUpdateList[i]->destGE = e;
				enemyUpdateList[i]->beginAttack(e->position);
			}
			else
			{
				if (enemyUpdateList[i]->pathFinder)
				{
					enemyUpdateList[i]->beginRadiusWalk(e->position, enemyUpdateList[i]->attackRadius);
				}
				else
				{
					enemyUpdateList[i]->beginRadiusStep(e->position, enemyUpdateList[i]->attackRadius);
				}
			}
		}
	}
	for (size_t i = 0; i < neutralUpdateList.size(); i++)
	{
		std::shared_ptr<NPC> e = nullptr;
		int distance = -1;
		for (size_t j = 0; j < enemyList.size(); j++)
		{
			if (neutralUpdateList[i]->canView(enemyList[j]->position))
			{
				int temp = gm->map->calDistance(neutralUpdateList[i]->position, enemyList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = enemyList[j];
				}
			}
		}
		for (size_t j = 0; j < friendList.size(); j++)
		{
			if (neutralUpdateList[i]->canView(friendList[j]->position))
			{
				int temp = gm->map->calDistance(neutralUpdateList[i]->position, friendList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = friendList[j];
				}
			}
		}
		if (e != nullptr)
		{
			if (distance <= neutralUpdateList[i]->attackRadius)
			{
				neutralUpdateList[i]->destGE = e;
				neutralUpdateList[i]->beginAttack(e->position);
			}
			else
			{
				if (neutralUpdateList[i]->pathFinder)
				{
					neutralUpdateList[i]->beginRadiusWalk(e->position, neutralUpdateList[i]->attackRadius);
				}
				else
				{
					neutralUpdateList[i]->beginRadiusStep(e->position, neutralUpdateList[i]->attackRadius);
				}
			}
		}
	}
}

bool NPCManager::drawNPCSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset)
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->selecting)
		{
			npcList[i]->drawNPCAlpha(cenTile, cenScreen, offset);
			return true;
		}
	}
	return false;
}

void NPCManager::drawNPCAlpha(int index, Point cenTile, Point cenScreen, PointEx offset)
{
	if (index < 0 || index >= (int)npcList.size())
	{
		return;
	}
	int i = index;
	if (npcList[i] != nullptr)
	{
		npcList[i]->drawNPCAlpha(cenTile, cenScreen, offset);
	}	
}

void NPCManager::drawNPC(int index, Point cenTile, Point cenScreen, PointEx offset)
{	
	if (index < 0 || index >= (int)npcList.size())
	{
		return;
	}
	int i = index;
	if (npcList[i] != nullptr)
	{
		npcList[i]->draw(cenTile, cenScreen, offset);
	}
}

void NPCManager::draw(Point tile, Point cenTile, Point cenScreen, PointEx offset)
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->position == tile)
		{
			
			Point pos = Map::getTilePosition(tile, cenTile, cenScreen, offset);
			int offsetX, offsetY;
			_shared_image image = npcList[i]->getActionShadow(&offsetX, &offsetY);
			engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
			if (!npcList[i]->selecting)
			{		
				image = npcList[i]->getActionImage(&offsetX, &offsetY);
				engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
			}
			else
			{
				if (npcList[i]->relation == nrHostile)
				{
					image = npcList[i]->getActionImage(&offsetX, &offsetY);
					engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 100, 100, 100);
				}
				else if (npcList[i]->scriptFile != "")
				{
					image = npcList[i]->getActionImage(&offsetX, &offsetY);
					engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 200, 100, 100);
				}
				else
				{
					image = npcList[i]->getActionImage(&offsetX, &offsetY);
					engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
				}
			}	
		}
	}
}

void NPCManager::setPartnerPos(int x, int y, int dir)
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->kind == nkPartner)
		{
			npcList[i]->direction = dir;
			//npcList[i]->position = { x, y };
			dir += 4;
			if (dir > 7)
			{
				dir -= 8;
			}
			npcList[i]->position = gm->map->getSubPoint({ x, y }, dir);

		}
	}
}

void NPCManager::clearNPC()
{
	gm->partnerManager.loadPartner();
	freeResource();
	gm->partnerManager.addPartner();
	gm->map->createDataMap();
	clearActionImageList();
	gm->player->reloadAction();
	for (size_t i = 0; i < npcList.size(); i++)
	{
		npcList[i]->reloadAction();
	}
}

void NPCManager::clearAllNPC()
{
	freeResource();
	gm->map->createDataMap();
	gm->player->reloadAction();
}

void NPCManager::clearSelected()
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->selecting)
		{
			npcList[i]->selecting = false;
		}
	}
}

void NPCManager::removeNPCOnlyFromList(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr) { return; }
	for (size_t i = 0; i < npcList.size(); ++i) {
		if (npcList[i] == npc)
		{
			npcList.erase(npcList.begin() + i);
			removeChild(npc);
			break;
		}
	}
}

void NPCManager::deleteNPC(std::vector<int> idx)
{
	if (idx.size() == 0)
	{
		return;
	}
	std::vector<std::shared_ptr<NPC>> newList;
	newList.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		bool found = false;
		for (size_t j = 0; j < idx.size(); j++)
		{
			if (i == idx[j])
			{
				found = true;
				break;
			}
		}
		if (found)
		{
			if (npcList[i] != nullptr)
			{
				deleteNPCFromOtherPlace(npcList[i]);
				npcList[i] = nullptr;
			}
		}
		else
		{
			newList.push_back(npcList[i]);
		}
	}
	npcList = newList;
	gm->map->createDataMap();
	tryCleanActionImageList();
}

void NPCManager::deleteNPC(int idx)
{
	if (idx >= 0 && idx < (int)npcList.size())
	{
		auto npc = npcList[idx];
		npcList.erase(npcList.begin() + idx);
		if (npc != nullptr)
		{
			deleteNPCFromOtherPlace(npc);
			npc = nullptr;
		}
	}
	gm->map->createDataMap();
}

void NPCManager::deleteNPC(std::string nName)
{
	std::vector<std::shared_ptr<NPC>> newList;
	newList.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] == nullptr || npcList[i]->npcName == nName)
		{
			if (npcList[i] != nullptr)
			{
				deleteNPCFromOtherPlace(npcList[i]);
				npcList[i] = nullptr;
			}		
		}
		else
		{
			newList.push_back(npcList[i]);
		}
	}
	npcList = newList;
	gm->map->createDataMap();
}

void NPCManager::addNPC(std::string iniName, int x, int y, int dir)
{
	auto npc = std::make_shared<NPC>();
	npcList.push_back(npc);
	npc->npcIndex = npcList.size();
	std::string iniN = NPC_INI_FOLDER + iniName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(iniN, s);
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		npc->initFromIni(&ini, "Init");
	}
	npc->position.x = x;
	npc->position.y = y;
	npc->direction = dir;
	addChild(npc);
	gm->map->addNPCToDataMap(npc->position, npcList.size());
}

void NPCManager::addNPC(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}
	addChild(npc);
	npc->beginStand();
	npcList.push_back(npc);
	npc->npcIndex = npcList.size();
	gm->map->addNPCToDataMap(npc->position, npcList.size());
}

void NPCManager::freeResource()
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr)
		{
			deleteNPCFromOtherPlace(npcList[i]);
			npcList[i] = nullptr;
		}
	}
	removeAllChild();
	npcList.resize(0);
	clearActionImageList();
}

void NPCManager::clearActionImageList()
{
	for (auto iter = actionImageList.begin(); iter != actionImageList.end(); iter++)
	{
		iter->second = nullptr;
	}
	actionImageList.clear();
}

void NPCManager::tryCleanActionImageList()
{
	auto iter = actionImageList.begin();
	while (iter != actionImageList.end())
	{
		if (iter->second.use_count() <= 1)
		{
			iter->second = nullptr;
			iter = actionImageList.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}

_shared_imp NPCManager::loadActionImage(const std::string & imageName)
{
	if (imageName.empty())
	{
		return nullptr;
	}
	auto img = actionImageList.find(imageName);
	if (img != actionImageList.end())
	{
		return img->second;
	}

	std::string tempName = NPC_RES_FOLDER + imageName;
	_shared_imp actImg = IMP::createIMPImage(tempName);
	actionImageList[imageName] = actImg;
	return actImg;
}

void NPCManager::load(const std::string & fileName)
{
	gm->partnerManager.loadPartner();
	freeResource();

	std::string iniName = SAVE_CURRENT_FOLDER + fileName;
	INIReader ini(iniName);

	std::string section = "Head";
	int count = ini.GetInteger(section, "Count", 0);
	if (count <= 0)
	{
		gm->partnerManager.addPartner();
		gm->player->reloadAction();
		for (size_t i = 0; i < npcList.size(); i++)
		{
			npcList[i]->reloadAction();
		}
		return;
	}
	for (int i = 0; i < count; i++)
	{
		section = convert::formatString("NPC%03d", i);
		auto npc = std::make_shared<NPC>();
		npc->initFromIni(&ini, section);
		addChild(npc);
		npcList.push_back(npc);
	}

	gm->partnerManager.addPartner();
	gm->player->reloadAction();
	for (size_t i = 0; i < npcList.size(); i++)
	{
		npcList[i]->reloadAction();
	}
}

void NPCManager::save(const std::string & fileName)
{
	if (fileName == "")
	{
		return;
	}
	
	INIReader ini;

	std::string section = "Head";
	ini.Set(section, "Map", GameManager::getInstance()->global.data.mapName);
	ini.SetInteger(section, "Count", npcList.size());
	int npcCount = 0;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->kind != nkPartner && npcList[i]->kind != nkPlayer)
		{
			section = convert::formatString("NPC%03d", npcCount++);
			npcList[i]->saveToIni(&ini, section);
		}
		
	}
	ini.saveToFile(SaveFileManager::CurrentPath() + fileName);

	SaveFileManager::AppendFile(fileName);
}

void NPCManager::onUpdate()
{
	std::vector<int> idx;
	idx.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr)
		{
			unsigned int ret = npcList[i]->getResult();
			if (ret & erRunDeathScript)
			{
				EventInfo eventInfo;
				eventInfo.npc = npcList[i];
				eventInfo.scriptName = npcList[i]->deathScript;
				//死亡脚本运行后清除
				npcList[i]->deathScript = "";
				eventInfo.scriptMapName = gm->mapName;
				gm->eventList.push_back(eventInfo);
			}
			else if (ret & erLifeExhaust)
			{
				idx.push_back(i);
				npcList[i]->addBody();
			}
		}
	}


	if (idx.size() > 0)
	{
		deleteNPC(idx);
	}
	GameLog::write("NPC Action count:%d", actionImageList.size());
	npcAutoAction();
}

void NPCManager::onEvent()
{
	clickIndex = -1;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i]->selecting)
		{
			clickIndex = i;
			break;
		}
	}
}
