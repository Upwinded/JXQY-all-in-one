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
}

void NPCManager::standAll()
{
	if (!(gm->player.isJumping() && gm->player.jumpState == jsJumping))
	{
		gm->player.beginStand();
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL)
		{
			npcList[i]->beginStand();
		}
	}
}

int NPCManager::findNPCIndex(NPC * npc)
{
	if (npc == NULL)
	{
		return -1;
	}
	if (npc == &gm->player)
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

bool NPCManager::findNPC(NPC * npc)
{
	return (findNPCIndex(npc) >= 0);
}

std::vector<NPC*> NPCManager::findNPC(int launcherKind)
{
	return findNPC(launcherKind, { 0, 0 }, 0);
}

std::vector<NPC *> NPCManager::findNPC(const std::string & npcName)
{
	std::vector<NPC *> result;
	result.resize(0);
	if (npcName == gm->player.npcName)
	{
		result.push_back(&gm->player);
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL && npcList[i]->npcName == npcName)
		{
			result.push_back(npcList[i]);
		}
	}
	return result;
}

std::vector<NPC*> NPCManager::findNPC(int launcherKind, Point pos, int radius)
{
	std::vector<NPC*> result = {};
	if (launcherKind == lkSelf)
	{
		result.push_back(&gm->player);
		return result;
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i]->kind == nkBattle)
		{
			NPC * tempNPC = NULL;
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
			if (tempNPC != NULL)
			{
				if (radius > 0)
				{
					int distance = gm->map.calDistance(tempNPC->position, pos);
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

NPC * NPCManager::findNearestNPC(int launcherKind, Point pos, int radius)
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

NPC * NPCManager::findNearestViewNPC(int launcherKind, Point pos, int radius)
{
	auto tempNPCList = findNPC(launcherKind, pos, radius);
	int temp = -1;
	int distance = radius + 1;
	for (int i = 0; i < (int)tempNPCList.size(); i++)
	{
		int tempDistance = Map::calDistance(pos, tempNPCList[i]->position);
		if (tempDistance < distance && gm->map.canView(pos, tempNPCList[i]->position))
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

void NPCManager::npcAutoAction()
{
	if (!gm->global.data.NPCAI)
	{
		return;
	}
	//根据人物的属性建立7个列表
	//常规列表，放置需要更新走动的常规NPC
	std::vector<NPC *> normalList;
	//以下3个列表放置所有战斗类NPC，根据关系分类
	std::vector<NPC *> friendList;
	std::vector<NPC *> enemyList;
	std::vector<NPC *> neutralList;
	//以下3个列表放置所有需要更新动作的NPC，根据关系分类
	std::vector<NPC *> friendUpdateList;
	std::vector<NPC *> enemyUpdateList;
	std::vector<NPC *> neutralUpdateList;

	//友军列表添加玩家
	if (gm->player.nowAction != acDeath && gm->player.nowAction != acHide)
	{
		friendList.push_back(&gm->player);
	}

	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL && !npcList[i]->isFollower())
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
			if (normalList[i]->canView({ stepX, stepY }) && gm->map.canWalk({ stepX, stepY }))
			{
				normalList[i]->beginWalk({ stepX, stepY });
				normalList[i]->walkTime = normalList[i]->getTime() + std::abs(rand()) % NPC_WALK_INTERVAL_RANGE;
			}
			normalList[i]->visionRadius = tvr;
		}
	}
	for (size_t i = 0; i < friendUpdateList.size(); i++)
	{
		NPC * e = NULL;
		int distance = -1;
		for (size_t j = 0; j < enemyList.size(); j++)
		{
			if (friendUpdateList[i]->canView(enemyList[j]->position))
			{
				int temp = gm->map.calDistance(friendUpdateList[i]->position, enemyList[j]->position);
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
				int temp = gm->map.calDistance(friendUpdateList[i]->position, neutralList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = neutralList[j];
				}
			}
		}
		if (e != NULL)
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
		NPC * e = NULL;
		int distance = -1;
		for (size_t j = 0; j < friendList.size(); j++)
		{
			if (enemyUpdateList[i]->canView(friendList[j]->position))
			{
				int temp = gm->map.calDistance(enemyUpdateList[i]->position, friendList[j]->position);
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
				int temp = gm->map.calDistance(enemyUpdateList[i]->position, neutralList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = neutralList[j];
				}
			}
		}
		if (e != NULL)
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
		NPC * e = NULL;
		int distance = -1;
		for (size_t j = 0; j < enemyList.size(); j++)
		{
			if (neutralUpdateList[i]->canView(enemyList[j]->position))
			{
				int temp = gm->map.calDistance(neutralUpdateList[i]->position, enemyList[j]->position);
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
				int temp = gm->map.calDistance(neutralUpdateList[i]->position, friendList[j]->position);
				if (distance == -1 || temp < distance)
				{
					distance = temp;
					e = friendList[j];
				}
			}
		}
		if (e != NULL)
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
		if (npcList[i] != NULL && npcList[i]->selected)
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
	if (npcList[i] != NULL)
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
	if (npcList[i] != NULL)
	{
		npcList[i]->draw(cenTile, cenScreen, offset);
	}
}

void NPCManager::draw(Point tile, Point cenTile, Point cenScreen, PointEx offset)
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL && npcList[i]->position.x == tile.x && npcList[i]->position.y == tile.y)
		{
			
			Point pos = Map::getTilePosition(tile, cenTile, cenScreen, offset);
			int offsetX, offsetY;
			_image image = npcList[i]->getActionShadow(&offsetX, &offsetY);
			engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
			if (!npcList[i]->selected)
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
		if (npcList[i] != NULL && npcList[i]->kind == nkPartner)
		{
			npcList[i]->direction = dir;
			//npcList[i]->position = { x, y };
			dir += 4;
			if (dir > 7)
			{
				dir -= 8;
			}
			npcList[i]->position = gm->map.getSubPoint({ x, y }, dir);

		}
	}
}

void NPCManager::clearNPC()
{
	gm->partnerManager.loadPartner();
	freeResource();
	gm->partnerManager.addPartner();
	gm->map.createDataMap();
	clearActionImageList();
	gm->player.reloadAction();
	for (size_t i = 0; i < npcList.size(); i++)
	{
		npcList[i]->reloadAction();
	}
}

void NPCManager::clearSelected()
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL && npcList[i]->selected)
		{
			npcList[i]->selected = false;
		}
	}
}

void NPCManager::deleteNPC(std::vector<int> idx)
{
	if (idx.size() == 0)
	{
		return;
	}
	std::vector<NPC *> newList = {};
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
			if (npcList[i] != NULL)
			{
				removeChild(npcList[i]);
				delete npcList[i];
				npcList[i] = NULL;
			}		
		}
		else
		{
			newList.resize(newList.size() + 1);
			newList[newList.size() - 1] = npcList[i];
		}
	}
	npcList.resize(newList.size());
	for (size_t i = 0; i < newList.size(); i++)
	{
		npcList[i] = newList[i];
	}
	newList.resize(0);
	gm->map.createDataMap();
}

void NPCManager::deleteNPC(int idx)
{
	std::vector<NPC *> newList = {};
	newList.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (i == idx)
		{
			if (npcList[i] != NULL)
			{
				removeChild(npcList[i]);
				delete npcList[i];
				npcList[i] = NULL;
			}		
		}
		else
		{
			newList.resize(newList.size() + 1);
			newList[newList.size() - 1] = npcList[i];
		}
	}
	npcList.resize(newList.size());
	for (size_t i = 0; i < newList.size(); i++)
	{
		npcList[i] = newList[i];
	}
	newList.resize(0);
	gm->map.createDataMap();
}

void NPCManager::deleteNPC(std::string nName)
{
	std::vector<NPC *> newList = {};
	newList.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] == NULL || npcList[i]->npcName == nName)
		{
			if (npcList[i] != NULL)
			{
				removeChild(npcList[i]);
				delete npcList[i];
				npcList[i] = NULL;
			}		
		}
		else
		{
			newList.push_back(npcList[i]);
		}
	}
	npcList = newList;
	gm->map.createDataMap();
}

void NPCManager::addNPC(std::string iniName, int x, int y, int dir)
{
	npcList.resize(npcList.size() + 1);
	npcList[npcList.size() - 1] = new NPC;
	npcList[npcList.size() - 1]->npcIndex = npcList.size();
	std::string iniN = NPC_INI_FOLDER + iniName;
	char * s = NULL;
	int len = PakFile::readFile(iniN, &s);
	if (s != NULL && len > 0)
	{
		INIReader * ini = new INIReader(s);
		delete[] s;
		npcList[npcList.size() - 1]->initFromIni(ini, "Init");
		delete ini;
	}

	npcList[npcList.size() - 1]->position.x = x;
	npcList[npcList.size() - 1]->position.y = y;
	npcList[npcList.size() - 1]->direction = dir;
	addChild(npcList[npcList.size() - 1]);
	gm->map.addNPCToDataMap(npcList[npcList.size() - 1]->position, npcList.size());
}

void NPCManager::addNPC(NPC * npc)
{
	if (npc == NULL)
	{
		return;
	}
	addChild(npc);
	npc->beginStand();
	npcList.push_back(npc);
	npcList[npcList.size() - 1]->npcIndex = npcList.size();
	gm->map.addNPCToDataMap(npcList[npcList.size() - 1]->position, npcList.size());
}

void NPCManager::freeResource()
{
	removeAllChild();
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL)
		{	
			delete npcList[i];
			npcList[i] = NULL;
		}
	}
	npcList.resize(0);
	clearActionImageList();
}

void NPCManager::clearActionImageList()
{
	for (size_t i = 0; i < actionImageList.size(); i++)
	{
		if (actionImageList[i].image != nullptr)
		{
			IMP::clearIMPImage(actionImageList[i].image);
			delete actionImageList[i].image;
			actionImageList[i].image = nullptr;
		}
	}
	actionImageList.resize(0);
}

IMPImage * NPCManager::loadActionImage(const std::string & imageName)
{
	if (imageName == "")
	{
		return nullptr;
	}
	for (size_t i = 0; i < actionImageList.size(); i++)
	{
		if (actionImageList[i].name == imageName)
		{
			return actionImageList[i].image;
		}
	}
	ActionImage actImg;
	actImg.name = imageName;
	std::string tempName = NPC_RES_FOLDER + imageName;
	actImg.image = IMP::createIMPImage(tempName);
	actionImageList.push_back(actImg);
	return actImg.image;
}

void NPCManager::load(const std::string & fileName)
{
	gm->partnerManager.loadPartner();
	freeResource();

	std::string iniName = DEFAULT_FOLDER + fileName;
	INIReader * ini = new INIReader(iniName);

	std::string section = "Head";
	int count = ini->GetInteger(section, "Count", 0);
	if (count <= 0)
	{
		delete ini;
		ini = NULL;
		gm->partnerManager.addPartner();
		gm->player.reloadAction();
		for (size_t i = 0; i < npcList.size(); i++)
		{
			npcList[i]->reloadAction();
		}
		return;
	}
	npcList.resize(count);
	for (int i = 0; i < count; i++)
	{
		section = convert::formatString("NPC%03d", i);
		npcList[i] = new NPC;
		npcList[i]->initFromIni(ini, section);
		addChild(npcList[i]);
	}
	delete ini;
	ini = NULL;
	gm->partnerManager.addPartner();
	gm->player.reloadAction();
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
	
	INIReader * ini = new INIReader();

	std::string section = "Head";
	ini->Set(section, "Map", GameManager::getInstance()->global.data.mapName);
	ini->SetInteger(section, "Count", npcList.size());
	int npcCount = 0;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL && npcList[i]->kind != nkPartner && npcList[i]->kind != nkPlayer)
		{
			section = convert::formatString("NPC%03d", npcCount++);
			npcList[i]->saveToIni(ini, section);
		}
		
	}
	std::string iniName = DEFAULT_FOLDER + fileName;
	ini->saveToFile(iniName);
	delete ini;
	ini = NULL;
}

void NPCManager::onUpdate()
{
	std::vector<int> idx;
	std::vector<NPC *> deathScriptList;
	idx.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != NULL)
		{
			unsigned int ret = npcList[i]->getResult();
			if (ret & erRunDeathScript)
			{
				deathScriptList.push_back(npcList[i]);
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

	for (size_t i = 0; i < deathScriptList.size(); i++)
	{
		if (findNPC(deathScriptList[i]))
		{
			gm->runNPCDeathScript(deathScriptList[i]);		
		}	
	}

	npcAutoAction();
}

void NPCManager::onEvent()
{
	clickIndex = -1;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i]->selected)
		{
			clickIndex = i;
			break;
		}
	}
}
