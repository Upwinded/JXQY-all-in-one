#include "NPCManager.h"
#include "../../Engine/Engine.h"
#include "ImageResourcePathResolver.h"
#include "NPC.h"
#include "NPCPersistence.h"
#include "Player.h"
#include "TimeStopUpdateGate.h"
#include "../GameManager/GameManager.h"
#include "../../File/File.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

namespace
{
constexpr int DeadNPCInfoFramesToKeep = 2;
constexpr int MaximumDropTableEntryCount = 4096;
constexpr int MaximumDropObjectsPerEntry = 1024;
constexpr size_t MaximumDropObjectsPerNpc = 4096;
constexpr int MaximumNpcListFileBytes = 16 * 1024 * 1024;

int getRuntimeRelation(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return nrFriendly;
	}
	if (gm != nullptr && gm->player != nullptr)
	{
		if (npc == gm->player)
		{
			return nrFriendly;
		}
		auto controlled = gm->player->getControlledCharacter();
		if (controlled != nullptr && controlled == npc && npc->relation == nrHostile)
		{
			return nrFriendly;
		}
	}
	return npc->relation;
}

bool canReachScriptedInteractionWithinSteps(
	Map* map, Point from, const NPC& npc, int maximumSteps)
{
	if (map == nullptr || maximumSteps <= 0 || npc.isFighterLike() ||
		(npc.scriptFile.empty() && npc.scriptFileRight.empty()))
	{
		return false;
	}

	const Point target = npc.getPosition();
	if (Map::calDistance(from, target) > maximumSteps)
	{
		return false;
	}
	if (npc.canTalkAtDistance(Map::calDistance(from, target)))
	{
		return true;
	}

	struct ReachablePosition
	{
		Point position;
		int steps = 0;
	};
	std::deque<ReachablePosition> frontier;
	std::vector<Point> visitedPositions;
	frontier.push_back({ from, 0 });
	visitedPositions.push_back(from);

	while (!frontier.empty())
	{
		const ReachablePosition current = frontier.front();
		frontier.pop_front();
		if (current.steps >= maximumSteps)
		{
			continue;
		}
		for (int direction = 0; direction < 8; ++direction)
		{
			const Point next = map->getSubPoint(current.position, direction);
			if (std::find(
					visitedPositions.begin(), visitedPositions.end(), next) !=
				visitedPositions.end() ||
				!map->canWalkDirectlyTo(current.position, direction))
			{
				continue;
			}
			if (npc.canTalkAtDistance(Map::calDistance(next, target)))
			{
				return true;
			}
			visitedPositions.push_back(next);
			frontier.push_back({ next, current.steps + 1 });
		}
	}
	return false;
}

enum class NPCDropGoodType
{
	Weapon,
	Armor,
	Money,
	Drug
};

std::string getDropObjectIniName(NPCDropGoodType type)
{
	switch (type)
	{
	case NPCDropGoodType::Weapon:
		return "可捡武器.ini";
	case NPCDropGoodType::Armor:
		return "可捡防具.ini";
	case NPCDropGoodType::Money:
		return "可捡钱.ini";
	case NPCDropGoodType::Drug:
		return "可捡药品.ini";
	default:
		return "";
	}
}

int getDefaultDropScriptLevel(std::shared_ptr<NPC> npc, Engine* engine)
{
	if (npc == nullptr)
	{
		return 1;
	}

	int level = npc->level;
	if (npc->expBonus > 0 && engine != nullptr)
	{
		int rand = engine->getRand(99);
		if (rand < 10)
		{
			level += 0;
		}
		else if (rand < 60)
		{
			level += 12;
		}
		else
		{
			level += 24;
		}
	}
	return level;
}

std::string getDropObjectScriptFileName(NPCDropGoodType type, int characterLevel)
{
	switch (type)
	{
	case NPCDropGoodType::Weapon:
	case NPCDropGoodType::Armor:
	case NPCDropGoodType::Money:
	{
		int level = characterLevel / 12 + 1;
		if (level > 7)
		{
			level = 7;
		}

		if (type == NPCDropGoodType::Weapon)
		{
			return std::to_string(level) + "级武器.txt";
		}
		if (type == NPCDropGoodType::Armor)
		{
			return std::to_string(level) + "级防具.txt";
		}
		return std::to_string(level) + "级钱.txt";
	}
	case NPCDropGoodType::Drug:
		if (characterLevel <= 10)
		{
			return "低级药品.txt";
		}
		if (characterLevel <= 30)
		{
			return "中级药品.txt";
		}
		if (characterLevel <= 60)
		{
			return "高级药品.txt";
		}
		return "特级药品.txt";
	default:
		return "";
	}
}

bool canLoadDropObjectIni(const std::string& iniName)
{
	if (iniName.empty())
	{
		return false;
	}
	std::unique_ptr<char[]> data;
	return File::readFile(OBJECT_INI_FOLDER + iniName, data) > 0 && data != nullptr;
}

std::string trimAscii(std::string value)
{
	auto isSpace = [](unsigned char ch)
	{
		return std::isspace(ch) != 0;
	};
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
	{
		value.erase(value.begin());
	}
	while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
	{
		value.pop_back();
	}
	return value;
}

bool parseDropIniReference(const std::string& dropIni, std::string& iniName, Engine* engine)
{
	iniName = trimAscii(dropIni);
	if (iniName.empty())
	{
		return false;
	}

	if (iniName.back() != ']')
	{
		return true;
	}

	size_t chanceBegin = iniName.rfind('[');
	if (chanceBegin == std::string::npos)
	{
		return true;
	}

	std::string chanceText = iniName.substr(chanceBegin + 1, iniName.size() - chanceBegin - 2);
	try
	{
		int chance = std::stoi(chanceText);
		if (engine != nullptr && engine->getRand(99) > chance)
		{
			return false;
		}
		iniName = trimAscii(iniName.substr(0, chanceBegin));
		return !iniName.empty();
	}
	catch (const std::exception&)
	{
		return true;
	}
}

bool rollDropOdds(float odds, Engine* engine)
{
	if (odds <= 0.0f)
	{
		return false;
	}
	if (odds >= 1.0f || engine == nullptr)
	{
		return true;
	}
	constexpr int OddsScale = 10000;
	int threshold = static_cast<int>(std::round(odds * OddsScale));
	if (threshold <= 0)
	{
		return false;
	}
	if (threshold >= OddsScale)
	{
		return true;
	}
	return engine->getRand(OddsScale - 1) < threshold;
}

bool isLiveFriendDeathAttacker(std::shared_ptr<NPC> attacker)
{
	return attacker != nullptr
		&& attacker->isVisibleForRuntime()
		&& !attacker->isDying()
		&& !attacker->isHiding();
}

bool readDropTableObjectNames(const std::string& iniName, std::vector<std::string>& objectNames, Engine* engine)
{
	objectNames.clear();
	if (iniName.empty())
	{
		return false;
	}

	std::unique_ptr<char[]> data;
	int len = File::readFile(OBJECT_INI_FOLDER + iniName, data);
	if (len <= 0 || data == nullptr)
	{
		return false;
	}

	INIReader ini(data);
	if (ini.ParseError() != 0)
	{
		return false;
	}

	std::string countText = ini.Get("INIT", "Count", "");
	if (countText.empty())
	{
		return false;
	}
	int count = 0;
	if (!NPCPersistence::readBoundedInteger(
		ini, "INIT", "Count", 1, MaximumDropTableEntryCount, count))
	{
		GameLog::write("NPCManager: invalid drop table count in %s\n", iniName.c_str());
		return true;
	}

	std::set<int> droppedGroups;
	for (int i = 1; i <= count; ++i)
	{
		std::string section = std::to_string(i);
		std::string objectIni = trimAscii(ini.Get(section, "ObjFile", ""));
		if (objectIni.empty())
		{
			continue;
		}

		int group = static_cast<int>(ini.GetInteger(section, "Group", 0));
		if (group > 0 && droppedGroups.find(group) != droppedGroups.end())
		{
			continue;
		}

		float odds = ini.GetReal(section, "Odds", 1.0f);
		if (!rollDropOdds(odds, engine))
		{
			continue;
		}

		if (!canLoadDropObjectIni(objectIni))
		{
			continue;
		}

		int num = 1;
		std::string numText = ini.Get(section, "Num", "");
		if (!numText.empty()
			&& !NPCPersistence::readBoundedInteger(
				ini, section, "Num", 0, MaximumDropObjectsPerEntry, num))
		{
			GameLog::write("NPCManager: invalid drop count in %s section %s\n",
				iniName.c_str(), section.c_str());
			continue;
		}
		num = std::max(num, 1);
		if (objectNames.size() + static_cast<size_t>(num) > MaximumDropObjectsPerNpc)
		{
			GameLog::write("NPCManager: drop object limit reached in %s\n", iniName.c_str());
			return true;
		}
		for (int j = 0; j < num; ++j)
		{
			objectNames.push_back(objectIni);
		}
		if (group > 0)
		{
			droppedGroups.insert(group);
		}
	}

	return true;
}

bool addCustomDefeatedNPCDrop(std::shared_ptr<NPC> npc, ObjectManager* objectManager, Engine* engine)
{
	if (npc == nullptr || objectManager == nullptr || npc->dropIni.empty())
	{
		return false;
	}

	std::string iniName;
	if (!parseDropIniReference(npc->dropIni, iniName, engine))
	{
		return true;
	}

	Point dropPosition = npc->getPosition();
	std::vector<std::string> objectNames;
	if (readDropTableObjectNames(iniName, objectNames, engine))
	{
		for (const std::string& objectName : objectNames)
		{
			objectManager->addObject(objectName, dropPosition.x, dropPosition.y, 0);
		}
		return true;
	}

	if (canLoadDropObjectIni(iniName))
	{
		objectManager->addObject(iniName, dropPosition.x, dropPosition.y, 0);
	}
	return true;
}

bool isAutoStrollIntent(int strollIntent)
{
	return strollIntent == nsiStroll || strollIntent == nsiGo;
}

bool isPlayerCandidate(GameManager* gameManager, const std::shared_ptr<NPC>& candidate)
{
	return candidate != nullptr && ((gameManager != nullptr && candidate == gameManager->player) || candidate->kind == nkPlayer);
}

bool canNpcAutoTarget(GameManager* gameManager, const std::shared_ptr<NPC>& attacker, const std::shared_ptr<NPC>& candidate)
{
	if (attacker == nullptr || candidate == nullptr)
	{
		return false;
	}

	const int attackerRelation = getRuntimeRelation(attacker);
	const int candidateRelation = getRuntimeRelation(candidate);
	return NPCManager::getAutomaticTargetPriority(
		attacker->kind, attackerRelation, attacker->group, attacker->noAutoAttackPlayer,
		candidate->kind, candidateRelation, candidate->group,
		isPlayerCandidate(gameManager, candidate)) != INT_MAX;
}

int getNpcAutoTargetPriority(GameManager* gameManager, const std::shared_ptr<NPC>& attacker, const std::shared_ptr<NPC>& candidate)
{
	if (attacker == nullptr || candidate == nullptr)
	{
		return INT_MAX;
	}
	const int attackerRelation = getRuntimeRelation(attacker);
	const int candidateRelation = getRuntimeRelation(candidate);
	return NPCManager::getAutomaticTargetPriority(
		attacker->kind, attackerRelation, attacker->group, attacker->noAutoAttackPlayer,
		candidate->kind, candidateRelation, candidate->group,
		isPlayerCandidate(gameManager, candidate));
}
}

NPCManager::NPCManager()
{
	name = "NPC Manager";
	setPriority(epGameManager);
	npcList.resize(0);
	needArrangeChild = false;
	canDraw = false;
	autoFreeResourceOnExit = true;
}

NPCManager::~NPCManager()
{
	freeResource();
    if (player != nullptr)
    {
        player = nullptr;
    }
}

bool NPCManager::shouldUpdateChild(PElement child)
{
	const bool hasActiveTimeStopper = gm != nullptr
		&& gm->effectManager != nullptr
		&& gm->effectManager->hasActiveTimeStopper();
	auto caster = hasActiveTimeStopper ? gm->effectManager->getActiveTimeStopperUser() : nullptr;
	return shouldUpdateNpcManagerChildDuringTimeStop(hasActiveTimeStopper,
		child != nullptr && caster != nullptr && child.get() == caster.get());
}

void NPCManager::setPlayer(std::shared_ptr<NPC> nowPlayer)
{
    player = nowPlayer;
}

void NPCManager::clearCombatTargetIfEqual(std::shared_ptr<GameElement> target)
{
	if (target == nullptr)
	{
		return;
	}

	for (auto& npc : npcList)
	{
		if (npc == nullptr)
		{
			continue;
		}
		bool clearedReference = false;
		if (npc->lastCombatTarget.lock() == target)
		{
			npc->lastCombatTarget.reset();
			npc->lastCombatTargetTime = 0;
			npc->lastCombatMagicDirection = { 0.0f, 0.0f };
			npc->hasLastCombatMagicDirection = false;
			clearedReference = true;
		}
		if (npc->currentCombatTarget.lock() == target)
		{
			npc->currentCombatTarget.reset();
			npc->currentCombatTargetTime = 0;
			clearedReference = true;
		}
		if (npc->lastKnownCombatTarget.lock() == target)
		{
			npc->lastKnownCombatTarget.reset();
			npc->lastKnownCombatTargetPosition = { 0, 0 };
			npc->lastKnownCombatTargetTime = 0;
			npc->hasLastKnownCombatTargetPosition = false;
			clearedReference = true;
		}
		if (npc->actionPlan.planTarget.lock() == target)
		{
			npc->actionPlan.reset();
			clearedReference = true;
		}
		if (clearedReference
			&& npc->currentCombatTarget.expired()
			&& npc->lastCombatTarget.expired()
			&& !npc->actionPlan.isActive())
		{
			npc->fightState.set(false);
		}
	}
}

void NPCManager::standAll()
{
	if (!(gm->player->isJumping() && gm->player->getJumpState() == jsJumping))
	{
		gm->player->beginStand();
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->isVisibleByVariable)
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
	if (npc == gm->player)
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
	return npc != nullptr && npc->isVisibleByVariable && (findNPCIndex(npc) >= 0);
}

bool NPCManager::isManagedEffectCaster(const std::shared_ptr<GameElement>& actor)
{
	auto npc = std::dynamic_pointer_cast<NPC>(actor);
	if (npc == nullptr || gm == nullptr)
	{
		return false;
	}
	if (gm->player != nullptr && npc == gm->player)
	{
		return true;
	}
	return gm->npcManager != nullptr && gm->npcManager->findNPCIndex(npc) >= 0;
}

bool NPCManager::shouldPersistNPC(const std::shared_ptr<NPC>& npc)
{
	return npc != nullptr && !npc->transientSummonedNPC;
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
	if (gm->player != nullptr && npcName == gm->player->npcName)
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
		if (gm->player != nullptr)
		{
			result.push_back(gm->player);
		}
		return result;
	}
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] == nullptr)
		{
			continue;
		}
		if (npcList[i]->isVisibleByVariable && npcList[i]->kind == nkBattle)
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
					int distance = gm->map->calDistance(tempNPC->getPosition(), pos);
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
        if (tempNPC != nullptr && tempNPC->isVisibleByVariable)
        {
            if (radius > 0)
            {
                int distance = gm->map->calDistance(tempNPC->getPosition(), pos);
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
		int tempDistance = Map::calDistance(pos, tempNPCList[i]->getPosition());
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
		int tempDistance = Map::calDistance(pos, tempNPCList[i]->getPosition());
		if (tempDistance < distance && gm->map->canSee(pos, tempNPCList[i]->getPosition()))
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
		if (npcList[i] == nullptr || !npcList[i]->isVisibleForRuntime() || !npcList[i]->isInteractive()) { continue; }
		int tempDistance = Map::calDistance(pos, npcList[i]->getPosition());
		bool dialogRadiusLarger = (npcList[i]->dialogRadius >= tempDistance);
		if ((gm->map->canSee(pos, npcList[i]->getPosition()) && tempDistance <= radius) || dialogRadiusLarger)
		{
			if (tempDistance < distance || (dialogRadiusLarger && temp < 0))
			{
				temp = i;
				distance = tempDistance;
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
		if (npcList[i] == nullptr || !npcList[i]->isVisibleForRuntime() || !npcList[i]->isInteractive()) { continue; }
		int tempDistance = Map::calDistance(pos, npcList[i]->getPosition());
		bool dialogRadiusLarger = (npcList[i]->dialogRadius >= tempDistance);
		if ((gm->map->canSee(pos, npcList[i]->getPosition()) && tempDistance <= radius) || dialogRadiusLarger)
		{
			ret.push_back(npcList[i]);
		}
	}
	return ret;
}

std::vector<std::shared_ptr<NPC>> NPCManager::findRadiusFastSelectionNPC(Point pos, int radius)
{
	auto result = findRadiusScriptViewNPC(pos, radius);
	for (const auto& npc : npcList)
	{
		if (npc == nullptr || !npc->isVisibleForRuntime() || !npc->isInteractive() ||
			std::find(result.begin(), result.end(), npc) != result.end())
		{
			continue;
		}
		if (canReachScriptedInteractionWithinSteps(
				gm != nullptr ? gm->map.get() : nullptr, pos, *npc, radius))
		{
			result.push_back(npc);
		}
	}
	return result;
}

std::vector<std::shared_ptr<NPC>> NPCManager::findFriendFighters()
{
	std::vector<std::shared_ptr<NPC>> result;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		auto npc = npcList[i];
		if (npc != nullptr && npc->isVisibleByVariable && (npc->kind == nkBattle || npc->kind == nkPartner) && npc->relation == nrFriendly)
		{
			result.push_back(npc);
		}
	}
	return result;
}

void NPCManager::deleteNPCFromOtherPlace(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}
	if (gm->player != nullptr && gm->player->getControlledCharacter() == npc)
	{
		gm->player->endControlCharacter();
	}
	clearCombatTargetIfEqual(npc);
	if (gm->camera->followNPC.lock() == npc)
	{
		gm->camera->followNPC.reset();
	}
	npc->removeFromDataMap();
	removeChild(npc);
	tryCleanActionImageList();
}

int NPCManager::getRelationOf(std::shared_ptr<GameElement> element, int fallbackLauncher)
{
	if (element != nullptr)
	{
		auto npc = std::dynamic_pointer_cast<NPC>(element);
		if (npc != nullptr)
		{
			return getRuntimeRelation(npc);
		}
	}
	if (fallbackLauncher == lkEnemy)
	{
		return nrHostile;
	}
	if (fallbackLauncher == lkNeutral)
	{
		return nrNeutral;
	}
	return nrFriendly;
}

bool NPCManager::canLauncherHitNPC(int launcherKind, std::shared_ptr<NPC> target)
{
	if (target == nullptr || !target->isVisibleForRuntime())
	{
		return false;
	}
	int relation = getRuntimeRelation(target);
	return canLauncherHitRelation(launcherKind, relation);
}

int NPCManager::getLauncherHitPriority(int launcherKind, std::shared_ptr<NPC> target, std::shared_ptr<GameElement> launcher)
{
	if (target == nullptr || !target->isVisibleForRuntime())
	{
		return INT_MAX;
	}
	auto launcherNPC = std::dynamic_pointer_cast<NPC>(launcher);
	if (launcherNPC != nullptr && launcherNPC == target)
	{
		return INT_MAX;
	}

	int targetRelation = getRuntimeRelation(target);
	int targetGroup = target->group;
	int launcherRelation = nrFriendly;
	int launcherGroup = 0;
	if (launcherNPC != nullptr)
	{
		launcherRelation = getRuntimeRelation(launcherNPC);
		launcherGroup = launcherNPC->group;
	}
	else
	{
		launcherRelation = getRelationOf(launcher, launcherKind);
	}
	return getLauncherHitPriority(launcherRelation, launcherGroup, targetRelation, targetGroup);
}

bool NPCManager::canLauncherHitNPC(int launcherKind, std::shared_ptr<NPC> target, std::shared_ptr<GameElement> launcher)
{
	return getLauncherHitPriority(launcherKind, target, launcher) != INT_MAX;
}

void NPCManager::addDefeatedNPCDrop(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr || gm == nullptr || gm->objectManager == nullptr)
	{
		return;
	}
	if (npc->relation != nrHostile)
	{
		return;
	}
	if (gm->global.data.dropDisabled)
	{
		return;
	}
	if (npc->noDropWhenDie > 0)
	{
		return;
	}
	if (!npc->dropIni.empty())
	{
		addCustomDefeatedNPCDrop(npc, gm->objectManager.get(), engine);
		return;
	}

	NPCDropGoodType dropType = NPCDropGoodType::Drug;
	if (npc->expBonus > 0)
	{
		dropType = engine->getRand(1) == 0 ? NPCDropGoodType::Weapon : NPCDropGoodType::Armor;
	}
	else
	{
		int randomType = engine->getRand(3);
		dropType = static_cast<NPCDropGoodType>(randomType);
		int maxRandomValue = (dropType == NPCDropGoodType::Weapon || dropType == NPCDropGoodType::Armor) ? 10 : 2;
		if (engine->getRand(maxRandomValue - 1) != 0)
		{
			return;
		}
	}

	std::string iniName = getDropObjectIniName(dropType);
	if (!canLoadDropObjectIni(iniName))
	{
		return;
	}

	Point dropPosition = npc->getPosition();
	auto droppedObject = gm->objectManager->addObject(iniName, dropPosition.x, dropPosition.y, 0);
	if (droppedObject != nullptr)
	{
		droppedObject->scriptFile = getDropObjectScriptFileName(dropType, getDefaultDropScriptLevel(npc, engine));
	}
}

void NPCManager::npcAutoAction()
{
	if (!gm->global.data.NPCAI)
	{
		return;
	}

	std::vector<std::shared_ptr<NPC>> normalList;
	std::vector<std::shared_ptr<NPC>> afraidPlayerAnimalList;

	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->isVisibleByVariable && !npcList[i]->isAIDisabled)
		{
			bool canAutoStroll = isAutoStrollIntent(npcList[i]->strollIntent);
			bool hasPendingDestination = npcList[i]->hasDestinationMapPosition();
			bool hasCombatContext = npcList[i]->fightState.get()
				|| npcList[i]->actionPlan.isActive()
				|| !npcList[i]->currentCombatTarget.expired()
				|| !npcList[i]->lastCombatTarget.expired();
			bool canUseAutoAction = !npcList[i]->isFollower()
				&& !npcList[i]->fightState.get()
				&& !npcList[i]->actionPlan.isActive()
				&& !hasPendingDestination
				&& !hasCombatContext
				&& npcList[i]->isStanding();
			bool canStartAfraidPlayerAnimalMove = canUseAutoAction
				&& npcList[i]->kind == nkAfraidPlayerAnimal
				&& gm->player != nullptr
				&& npcList[i]->visionRadius > 0
				&& gm->map->calDistance(npcList[i]->getPosition(), gm->player->getPosition()) < npcList[i]->visionRadius
				&& npcList[i]->canSee(gm->player->getPosition());
			if (canStartAfraidPlayerAnimalMove)
			{
				afraidPlayerAnimalList.push_back(npcList[i]);
				continue;
			}
			bool canStartAutoStroll = canUseAutoAction
				&& canAutoStroll
				&& (npcList[i]->getTime() > npcList[i]->walkTime + NPC_WALK_INTERVAL);
			bool canStartRandomAIMove = canUseAutoAction
				&& npcList[i]->isRandMoveRandAttack();
			if (canStartAutoStroll || canStartRandomAIMove)
			{
				normalList.push_back(npcList[i]);
			}
		}
	}
	for (size_t i = 0; i < afraidPlayerAnimalList.size(); i++)
	{
		if (gm->inEvent && gm->scriptType == stNPC && gm->scriptNPC == afraidPlayerAnimalList[i])
		{
			continue;
		}
		int distance = gm->map->calDistance(afraidPlayerAnimalList[i]->getPosition(), gm->player->getPosition());
		int retreatDistance = afraidPlayerAnimalList[i]->visionRadius - distance;
		if (retreatDistance <= 0)
		{
			continue;
		}
		if (afraidPlayerAnimalList[i]->usePathFinder())
		{
			afraidPlayerAnimalList[i]->beginRetreatWalk(gm->player->getPosition(), gm->player, retreatDistance);
		}
		else
		{
			afraidPlayerAnimalList[i]->beginRetreatStep(gm->player->getPosition(), gm->player, retreatDistance);
		}
		afraidPlayerAnimalList[i]->walkTime = afraidPlayerAnimalList[i]->getTime() + engine->getRand(NPC_WALK_INTERVAL_RANGE);
	}
	for (size_t i = 0; i < normalList.size(); i++)
	{
		if (gm->inEvent && gm->scriptType == stNPC && gm->scriptNPC == normalList[i])
		{
			continue;
		}
		if (normalList[i]->hasFixedPath() && !normalList[i]->isRandMoveRandAttack())
		{
			normalList[i]->beginFixedPathWalk();
			normalList[i]->walkTime = normalList[i]->getTime() + engine->getRand(NPC_WALK_INTERVAL_RANGE);
			continue;
		}
		bool isFlyer = normalList[i]->kind == nkFlyingAnimal;
		bool moved = false;
		if (normalList[i]->isRandMoveRandAttack())
		{
			moved = normalList[i]->beginRandWalkFromActionPath(false, 2, 10, false);
		}
		else
		{
			moved = normalList[i]->beginRandWalkFromActionPath(isFlyer);
		}
		if (moved)
		{
			normalList[i]->walkTime = normalList[i]->getTime() + engine->getRand(NPC_WALK_INTERVAL_RANGE);
		}
	}
}

void NPCManager::addDeadNPC(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}

	auto attacker = std::dynamic_pointer_cast<NPC>(npc->lastCombatTarget.lock());
	if (attacker == nullptr)
	{
		attacker = std::dynamic_pointer_cast<NPC>(npc->currentCombatTarget.lock());
	}
	if (!isLiveFriendDeathAttacker(attacker))
	{
		return;
	}

	DeadNPCInfo info;
	info.position = npc->getPosition();
	info.kind = npc->kind;
	info.relation = npc->relation;
	info.attacker = attacker;
	info.framesToKeep = DeadNPCInfoFramesToKeep;
	deadNPCInfos.push_back(info);
}

std::shared_ptr<NPC> NPCManager::findFriendDeathAttacker(std::shared_ptr<NPC> finder, int maxTileDistance)
{
	if (finder == nullptr || gm == nullptr || gm->map == nullptr)
	{
		return nullptr;
	}

	for (const auto& info : deadNPCInfos)
	{
		if (info.framesToKeep <= 0)
		{
			continue;
		}
		if (!isFriendDeathRelationMatch(finder->kind, finder->relation, info.kind, info.relation))
		{
			continue;
		}
		if (gm->map->calDistance(finder->getPosition(), info.position) > maxTileDistance)
		{
			continue;
		}

		auto attacker = info.attacker.lock();
		if (isLiveFriendDeathAttacker(attacker))
		{
			return attacker;
		}
	}
	return nullptr;
}

void NPCManager::advanceDeadNPCInfos()
{
	for (auto& info : deadNPCInfos)
	{
		info.framesToKeep--;
	}
	deadNPCInfos.erase(
		std::remove_if(deadNPCInfos.begin(), deadNPCInfos.end(),
			[](const DeadNPCInfo& info)
			{
				return info.framesToKeep <= 0 || !isLiveFriendDeathAttacker(info.attacker.lock());
			}),
		deadNPCInfos.end());
}

bool NPCManager::beginFallbackApproach(std::shared_ptr<NPC> npc, std::shared_ptr<NPC> target)
{
	if (npc == nullptr || target == nullptr)
	{
		return false;
	}
	auto stopApproach = [&](bool clearMemory) {
		if (clearMemory)
		{
			npc->clearCombatTargetMemory();
		}
		else
		{
			npc->actionPlan.reset();
		}
		npc->stopMovement();
		return true;
	};
	auto finishMove = [&](RadiusMoveResult result, Point chasePosition, bool clearMemoryOnFail) {
		if (result == rmrAlreadyInRadius)
		{
			npc->beginStand();
			return true;
		}
		if (result == rmrMoved)
		{
			if (!npc->isCurrentPathWithinCombatChaseLimit(chasePosition))
			{
				return stopApproach(clearMemoryOnFail);
			}
			return true;
		}
		return stopApproach(clearMemoryOnFail);
	};
	Point targetPosition = target->getPosition();
	bool targetVisible = npc->canSee(targetPosition);
	if (!targetVisible)
	{
		auto memoryPosition = npc->getCombatTargetMemoryPosition(target);
		if (!memoryPosition.has_value())
		{
			return stopApproach(true);
		}

		Point rememberedPosition = memoryPosition.value();
		int memoryDistance = gm->map->calDistance(npc->getPosition(), rememberedPosition);
		if (memoryDistance > npc->visionRadius * 2)
		{
			return stopApproach(true);
		}
		if (memoryDistance <= 1)
		{
			npc->clearCombatTargetMemory();
			npc->beginStand();
			return true;
		}
		if (npc->usePathFinder())
		{
			RadiusMoveResult result = npc->beginRadiusWalk(rememberedPosition, 1);
			return finishMove(result, rememberedPosition, false);
		}
		RadiusMoveResult result = npc->beginRadiusStep(rememberedPosition, 1);
		return finishMove(result, rememberedPosition, false);
	}

	npc->rememberCombatTargetPosition(target);
	int currentDistance = gm->map->calDistance(npc->getPosition(), targetPosition);
	if (currentDistance > npc->visionRadius * 2)
	{
		return stopApproach(true);
	}
	int approachDistance = npc->getMaxAttackOptionDistance();
	int chaseLimit = npc->visionRadius * 2;
	if (approachDistance > chaseLimit)
	{
		approachDistance = chaseLimit;
	}
	if (approachDistance < 1)
	{
		approachDistance = 1;
	}
	if (gm->map->calDistance(npc->getPosition(), targetPosition) <= approachDistance)
	{
		if (!npc->canAnyAttackOptionHitTarget(targetPosition))
		{
			if (!npc->trySelfBuff())
			{
				Point approachPosition;
				int approachRadius;
				bool hasValidApproach = npc->getFallbackApproachInfo(targetPosition, approachPosition, approachRadius);
				if (hasValidApproach && !npc->isWithinCombatChaseLimit(targetPosition, approachPosition))
				{
					hasValidApproach = false;
				}
				if (hasValidApproach && approachPosition != targetPosition && approachPosition != npc->getPosition())
				{
					if (npc->usePathFinder())
					{
						return finishMove(npc->beginRadiusWalk(approachPosition, 0), targetPosition, false);
					}
					return finishMove(npc->beginRadiusStep(approachPosition, 0), targetPosition, false);
				}
				if (hasValidApproach && approachPosition == npc->getPosition())
				{
					npc->beginStand();
					return npc->isStanding();
				}
				int fallbackRadius = hasValidApproach ? (approachRadius > 0 ? approachRadius : 1) : 1;
				if (npc->usePathFinder())
				{
					return finishMove(npc->beginRadiusWalk(targetPosition, fallbackRadius), targetPosition, false);
				}
				return finishMove(npc->beginRadiusStep(targetPosition, fallbackRadius), targetPosition, false);
			}
			return true;
		}
		auto readyOption = npc->findReadyAttackOption(targetPosition);
		if (readyOption.has_value())
		{
			npc->prepareImmediateAttackPlan(target, readyOption.value(), targetPosition);
		}
		else
		{
			npc->actionPlan.reset();
		}
		if (!npc->canStartIdleAttack())
		{
			npc->beginStand();
			return true;
		}
		npc->beginAttack(targetPosition, target);
		if (npc->isAttacking())
		{
			return true;
		}
		npc->actionPlan.reset();
		if (npc->usePathFinder())
		{
			return finishMove(npc->beginRadiusWalk(targetPosition, approachDistance), targetPosition, false);
		}
		return finishMove(npc->beginRadiusStep(targetPosition, approachDistance), targetPosition, false);
	}

	if (npc->usePathFinder())
	{
		return finishMove(npc->beginRadiusWalk(targetPosition, approachDistance), targetPosition, false);
	}
	return finishMove(npc->beginRadiusStep(targetPosition, approachDistance), targetPosition, false);
}

bool NPCManager::scheduleBattleAction(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr || npc->isAIDisabled || (npc->kind != nkBattle && !(npc->kind == nkPartner && gm->global.data.PartnerCombat)))
	{
		return false;
	}
	// Owner following outranks target acquisition once a partner falls too far
	// behind. Clearing combat state here also lets walk/run retarget to the player
	// during the same movement step instead of reacquiring an enemy first.
	if (npc->abandonPartnerCombatForPlayerFollow())
	{
		return false;
	}
	auto hasCombatContext = [&]() {
		return npc->fightState.get()
			|| npc->actionPlan.isActive()
			|| !npc->currentCombatTarget.expired()
			|| !npc->lastCombatTarget.expired();
	};
	auto stopCombat = [&]() {
		npc->clearCombatTargetMemory();
		npc->stopMovement();
		return true;
	};

	int bestDist = -1;
	int bestPriority = INT_MAX;
	std::shared_ptr<NPC> bestTarget = nullptr;

	auto checkCandidate = [&](std::shared_ptr<NPC> candidate)
	{
		if (candidate == nullptr || !candidate->isVisibleForRuntime() || candidate->nowAction == acDeath || candidate->nowAction == acHide)
		{
			return;
		}
		if (candidate.get() == npc.get())
		{
			return;
		}
		if (!canNpcAutoTarget(gm, npc, candidate))
		{
			return;
		}
		int tempDist = gm->map->calDistance(npc->getPosition(), candidate->getPosition());
		if (tempDist > npc->visionRadius)
		{
			return;
		}
		if (npc->canSee(candidate->getPosition()))
		{
			int priority = getNpcAutoTargetPriority(gm, npc, candidate);
			if (bestDist == -1 || priority < bestPriority || (priority == bestPriority && tempDist < bestDist))
			{
				bestDist = tempDist;
				bestPriority = priority;
				bestTarget = candidate;
			}
		}
	};

	for (size_t i = 0; i < npcList.size(); i++)
	{
		checkCandidate(npcList[i]);
	}
	checkCandidate(gm->player);

	std::shared_ptr<NPC> target = nullptr;
	const int targetSwitchDistanceBias = 2;

	auto tryKeepTarget = [&](std::shared_ptr<GameElement> candidateElement) {
		auto candidate = std::dynamic_pointer_cast<NPC>(candidateElement);
		if (candidate == nullptr || !candidate->isVisibleForRuntime() || candidate->nowAction == acDeath || candidate->nowAction == acHide)
		{
			return false;
		}
		if (candidate.get() == npc.get())
		{
			return false;
		}
		if (!canNpcAutoTarget(gm, npc, candidate))
		{
			return false;
		}
		int candidateDistance = gm->map->calDistance(npc->getPosition(), candidate->getPosition());
		bool candidateVisible = candidateDistance <= npc->visionRadius && npc->canSee(candidate->getPosition());
		if (candidateVisible)
		{
			int candidatePriority = getNpcAutoTargetPriority(gm, npc, candidate);
			if (bestTarget == nullptr
				|| candidatePriority < bestPriority
				|| (candidatePriority == bestPriority && candidateDistance <= bestDist + targetSwitchDistanceBias))
			{
				target = candidate;
				return true;
			}
			return false;
		}
		auto memoryPosition = npc->getCombatTargetMemoryPosition(candidate);
		if (npc->fightState.get() && bestTarget == nullptr && memoryPosition.has_value())
		{
			target = candidate;
			return true;
		}
		return false;
	};

	auto planTarget = npc->actionPlan.planTarget.lock();
	if (planTarget != nullptr)
	{
		tryKeepTarget(planTarget);
	}
	if (target == nullptr)
	{
		tryKeepTarget(npc->currentCombatTarget.lock());
	}
	bool canAcquireVisibleTarget = !npc->isRandMoveRandAttack()
		|| (npc->isStanding() && engine->getRand(99) > 70);
	canAcquireVisibleTarget = canAcquireVisibleTarget && npc->stopFindingTarget == 0;
	if (target == nullptr && bestTarget != nullptr && canAcquireVisibleTarget)
	{
		target = bestTarget;
	}

	if (target == nullptr && npc->fightState.get())
	{
		auto damageSource = npc->lastCombatTarget.lock();
		if (damageSource != nullptr)
		{
			auto sourceNPC = std::dynamic_pointer_cast<NPC>(damageSource);
			if (sourceNPC != nullptr
				&& sourceNPC->isVisibleForRuntime()
				&& sourceNPC->nowAction != acDeath && sourceNPC->nowAction != acHide
				&& canNpcAutoTarget(gm, npc, sourceNPC))
			{
				auto memoryPosition = npc->getCombatTargetMemoryPosition(sourceNPC);
				if (memoryPosition.has_value())
				{
					target = sourceNPC;
				}
			}
		}
	}

	if (npc->tryKeepDistanceWhenFriendDeath())
	{
		return true;
	}

	if (target == nullptr && npc->tryUseMagicWhenLifeLow())
	{
		return true;
	}

	if (target == nullptr)
	{
		if (hasCombatContext())
		{
			return stopCombat();
		}
		return false;
	}

	npc->fightState.set(true);

	bool targetVisible = npc->canSee(target->getPosition());
	if (targetVisible)
	{
		npc->rememberCombatTargetPosition(target);
	}
	else if (npc->canChaseCombatTargetFromMemory(target))
	{
		return beginFallbackApproach(npc, target);
	}
	else
	{
		return stopCombat();
	}

	if (npc->tryKeepDistanceWhenFriendDeath())
	{
		return true;
	}
	if (npc->tryKeepDistanceWhenLifeLow(target))
	{
		return true;
	}
	if (npc->tryUseMagicWhenLifeLow())
	{
		return true;
	}

	if (npc->evaluateAndPlan(target))
	{
		if (npc->executeActionPlan(target))
		{
			return true;
		}
		return beginFallbackApproach(npc, target);
	}

	return beginFallbackApproach(npc, target);
}

bool NPCManager::drawNPCSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset)
{
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->isVisibleByVariable && npcList[i]->selecting)
		{
			npcList[i]->drawNPCAlpha(cenTile, cenScreen, offset);
			return true;
		}
	}
	return false;
}

void NPCManager::drawNPC(std::shared_ptr<NPC> npc, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle)
{	
	if (npc != nullptr && npc->isVisibleByVariable)
	{
		npc->draw(cenTile, cenScreen, offset, colorStyle);
	}
}

void NPCManager::sortChildrenByY()
{
	if (children.size() <= 1)
	{
		return;
	}

	std::stable_sort(children.begin(), children.end(),
		[](const PElement& a, const PElement& b)
		{
			auto npcA = std::dynamic_pointer_cast<NPC>(a);
			auto npcB = std::dynamic_pointer_cast<NPC>(b);
			if (npcA == nullptr || npcB == nullptr)
			{
				return a.get() < b.get();
			}

			if (npcA->getPosition().y != npcB->getPosition().y)
			{
				return npcA->getPosition().y > npcB->getPosition().y;
			}
			if (npcA->getOffset().y != npcB->getOffset().y)
			{
				return npcA->getOffset().y > npcB->getOffset().y;
			}
			if (npcA->getPosition().x != npcB->getPosition().x)
			{
				return npcA->getPosition().x < npcB->getPosition().x;
			}
			return a.get() < b.get();
		});
}

//void NPCManager::draw(Point tile, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle)
//{
//	for (size_t i = 0; i < npcList.size(); i++)
//	{
//		if (npcList[i] != nullptr && npcList[i]->position == tile)
//		{
//			
//			Point pos = Map::getTilePosition(tile, cenTile, cenScreen, offset);
//			int offsetX, offsetY;
//			_shared_image image = npcList[i]->getActionShadow(&offsetX, &offsetY);
//			engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
//			if (!npcList[i]->selecting)
//			{		
//				image = npcList[i]->getActionImage(&offsetX, &offsetY);
//				engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
//			}
//			else
//			{
//				if (npcList[i]->relation == nrHostile)
//				{
//					image = npcList[i]->getActionImage(&offsetX, &offsetY);
//					engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 100, 100, 100);
//				}
//				else if (npcList[i]->scriptFile != "")
//				{
//					image = npcList[i]->getActionImage(&offsetX, &offsetY);
//					engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 200, 100, 100);
//				}
//				else
//				{
//					image = npcList[i]->getActionImage(&offsetX, &offsetY);
//					engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
//				}
//			}	
//		}
//	}
//}

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
			npcList[i]->setPosition(gm->map->getSubPoint({ x, y }, dir));

		}
	}
}

void NPCManager::clearNPC(bool rebuildDataMap)
{
	gm->partnerManager.extractPartnerListFromNPCManager();
	freeResource();
	gm->partnerManager.transferPartnerListToNPCManager();
	clearActionImageList();
	gm->player->reloadAction();
	for (size_t i = 0; i < npcList.size(); i++)
	{
		npcList[i]->reloadAction();
	}
	if (rebuildDataMap)
	{
		gm->map->createDataMap();
	}
}

void NPCManager::clearAllNPC(bool rebuildDataMap)
{
	freeResource();
	gm->player->reloadAction();
	if (rebuildDataMap)
	{
		gm->map->createDataMap();
	}
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
			npc->removeFromDataMap();
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
}

void NPCManager::deleteNPC(std::string nName)
{
	std::vector<std::shared_ptr<NPC>> newList;
	newList.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] == nullptr
			|| (npcList[i]->npcName == nName && npcList[i]->kind != nkPartner))
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
}

void NPCManager::addNPC(std::string iniName, int x, int y, int dir)
{
	if (npcList.size() >= static_cast<size_t>(NPCPersistence::MaximumRuntimeNpcCount))
	{
		GameLog::write("NPCManager: runtime NPC limit reached while adding %s\n", iniName.c_str());
		return;
	}
	std::string iniN = NPC_INI_FOLDER + iniName;
	std::unique_ptr<char[]> s;
	int len = File::readFile(iniN, s);
	if (s == nullptr || len <= 0)
	{
		GameLog::write("NPCManager: NPC resource missing %s\n", iniN.c_str());
		return;
	}
	INIReader ini(s);
	if (ini.ParseError() != 0)
	{
		GameLog::write("NPCManager: invalid NPC resource %s\n", iniN.c_str());
		return;
	}

	auto npc = std::make_shared<NPC>();
	npc->initFromIni(&ini, "Init");
	npc->setPosition({ x, y });
	npc->direction = dir;
	addNPC(npc);
}

void NPCManager::addNPC(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}
	if (npcList.size() >= static_cast<size_t>(NPCPersistence::MaximumRuntimeNpcCount))
	{
		GameLog::write("NPCManager: runtime NPC limit reached\n");
		return;
	}
	addChild(npc);
	npc->beginStand();
	npcList.push_back(npc);
	npc->updateVisibleByVariable();
	if (npc->isVisibleByVariable && !npc->isHiding())
	{
		gm->map->addNPCToDataMap(npc->getPosition(), npc);
	}
}

void NPCManager::freeResource()
{
	releaseManagedNPCs(true);
}

void NPCManager::releaseManagedNPCs(bool clearActionImages)
{
	std::map<std::string, _shared_imp> retainedActionImages;
	retainedActionImages.swap(actionImageList);
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
	deadNPCInfos.clear();
	if (!clearActionImages)
	{
		actionImageList.swap(retainedActionImages);
	}
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

	const std::vector<std::string> fallbackFolders =
	{
		ASF_FOLDER "interlude\\",
		MPC_FOLDER "interlude\\"
	};
	_shared_imp actImg = loadFirstImageResourceCandidate(buildImageResourceCandidatesForCategory(
		imageName,
		"character",
		NPC_RES_FOLDER_ASF,
		NPC_RES_FOLDER,
		fallbackFolders));
	// Converted character packages are already trimmed by the migrator. Original
	// ASF/MPC packages still carry decoded pixels here, so apply the same lossless
	// crop at runtime. Offset compensation keeps feet, shadows and effect anchors
	// fixed while head UI uses the visible frame top instead of transparent padding.
	IMP::cropTransparentEdges(actImg);
	actionImageList[imageName] = actImg;
	return actImg;
}

_shared_imp NPCManager::loadActionImageDirect(const std::string & fullPath)
{
	if (fullPath.empty())
	{
		return nullptr;
	}
	auto img = actionImageList.find(fullPath);
	if (img != actionImageList.end())
	{
		return img->second;
	}

	_shared_imp actImg = IMP::createIMPImage(fullPath);
	IMP::cropTransparentEdges(actImg);
	actionImageList[fullPath] = actImg;
	return actImg;
}

bool NPCManager::PreparedLoad::isValid() const noexcept
{
	return parsedIni != nullptr;
}

std::size_t NPCManager::PreparedLoad::npcCount() const noexcept
{
	return preparedNpcCount;
}

const std::string& NPCManager::PreparedLoad::sourcePath() const noexcept
{
	return resolvedSourcePath;
}

bool NPCManager::prepareParsedLoad(
	const std::unique_ptr<char[]>& data,
	const std::string& sourcePath,
	bool exactResource,
	bool allowIncompleteSectionList,
	PreparedLoad& preparedLoad) const
{
	preparedLoad = PreparedLoad{};
	if (data == nullptr)
	{
		return false;
	}

	auto parsedIni = std::make_shared<INIReader>(data);
	if (parsedIni->ParseError() != 0)
	{
		if (allowIncompleteSectionList)
		{
			GameLog::write(
				"NPCManager: compatible NPC list %s has invalid or empty INI content; loading an empty list\n",
				sourcePath.c_str());
			PreparedLoad candidate;
			candidate.parsedIni = std::move(parsedIni);
			candidate.preparedNpcCount = 0;
			candidate.resolvedSourcePath = sourcePath;
			preparedLoad = std::move(candidate);
			return true;
		}
		GameLog::write(
			exactResource
				? "NPCManager: invalid exact NPC list %s\n"
				: "NPCManager: invalid NPC list %s\n",
			sourcePath.c_str());
		return false;
	}

	int declaredCount = 0;
	const bool declaredCountIsValid =
		NPCPersistence::readCount(
			*parsedIni,
			NPCPersistence::MaximumNpcCount,
			declaredCount);
	if (!declaredCountIsValid &&
		!allowIncompleteSectionList)
	{
		GameLog::write(
			exactResource
				? "NPCManager: invalid exact NPC count in %s\n"
				: "NPCManager: invalid NPC count in %s\n",
			sourcePath.c_str());
		return false;
	}

	const int sectionLimit = declaredCountIsValid
		? declaredCount
		: NPCPersistence::MaximumNpcCount;
	int count = 0;
	for (int index = 0; index < sectionLimit; ++index)
	{
		const std::string section =
			convert::formatString("NPC%03d", index);
		if (!parsedIni->HasSection(section))
		{
			if (allowIncompleteSectionList)
			{
				break;
			}
			if (exactResource)
			{
				GameLog::write(
					"NPCManager: missing section %s in exact NPC list %s\n",
					section.c_str(),
					sourcePath.c_str());
			}
			else
			{
				GameLog::write(
					"NPCManager: missing section %s in %s\n",
					section.c_str(),
					sourcePath.c_str());
			}
			return false;
		}
		count = index + 1;
	}
	if (allowIncompleteSectionList &&
		(!declaredCountIsValid || count != declaredCount))
	{
		const std::string declaredCountDescription =
			declaredCountIsValid
				? convert::formatString("%d NPCs", declaredCount)
				: "an invalid count";
		GameLog::write(
			"NPCManager: compatible NPC list %s declares %s; loading %d contiguous sections\n",
			sourcePath.c_str(),
			declaredCountDescription.c_str(),
			count);
	}

	PreparedLoad candidate;
	candidate.parsedIni = std::move(parsedIni);
	candidate.preparedNpcCount = static_cast<size_t>(count);
	candidate.resolvedSourcePath = sourcePath;
	preparedLoad = std::move(candidate);
	return true;
}

bool NPCManager::prepareLoad(
	const std::string& fileName,
	PreparedLoad& preparedLoad,
	bool allowIncompleteSectionList) const
{
	preparedLoad = PreparedLoad{};
	std::unique_ptr<char[]> data;
	int length = 0;
	std::string loadedPath;
	if (!SaveFileManager::ReadNpcObjFile(
			fileName,
			data,
			length,
			&loadedPath,
			MaximumNpcListFileBytes))
	{
		GameLog::write(
			"NPCManager: NPC list missing %s\n",
			fileName.c_str());
		return false;
	}
	return prepareParsedLoad(
		data,
		loadedPath,
		false,
		allowIncompleteSectionList,
		preparedLoad);
}

bool NPCManager::prepareExactResourceBytes(
	const std::string& virtualPath,
	const std::vector<std::uint8_t>& bytes,
	PreparedLoad& preparedLoad) const
{
	preparedLoad = PreparedLoad{};
	if (virtualPath.empty() ||
		bytes.empty() ||
		bytes.size() > static_cast<std::size_t>(
			MaximumNpcListFileBytes) ||
		std::find(bytes.begin(), bytes.end(), std::uint8_t{ 0 }) !=
			bytes.end())
	{
		return false;
	}

	auto data = std::make_unique<char[]>(bytes.size() + 1);
	std::memcpy(data.get(), bytes.data(), bytes.size());
	data[bytes.size()] = '\0';
	return prepareParsedLoad(
		data,
		virtualPath,
		true,
		false,
		preparedLoad);
}

bool NPCManager::commitPreparedLoad(
	const PreparedLoad& preparedLoad,
	bool clearCurrent,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint,
	bool randomOne)
{
	if (!preparedLoad.isValid() ||
		gm == nullptr ||
		gm->player == nullptr ||
		gm->map == nullptr ||
		engine == nullptr)
	{
		return false;
	}
	if (!engine->isMainThread())
	{
		GameLog::write(
			"NPCManager: prepared NPC commit must run on the SDL main thread\n");
		return false;
	}

	size_t retainedCount = npcList.size();
	if (clearCurrent)
	{
		retainedCount = static_cast<size_t>(std::count_if(
			npcList.begin(), npcList.end(),
			[](const std::shared_ptr<NPC>& npc)
			{
				return npc != nullptr && npc->kind == nkPartner;
			}));
	}
	const std::size_t loadCount = randomOne && preparedLoad.npcCount() > 0
		? 1
		: preparedLoad.npcCount();
	if (retainedCount + loadCount
		> static_cast<size_t>(
			NPCPersistence::MaximumRuntimeNpcCount))
	{
		GameLog::write(
			"NPCManager: NPC list exceeds runtime capacity in %s\n",
			preparedLoad.sourcePath().c_str());
		return false;
	}

	std::vector<std::shared_ptr<NPC>> loadedNpcList;
	loadedNpcList.reserve(loadCount);
	const std::size_t randomIndex = randomOne && preparedLoad.npcCount() > 0
		? static_cast<std::size_t>(engine->getRand(
			static_cast<int>(preparedLoad.npcCount() - 1)))
		: 0;
	for (size_t index = 0;
		index < loadCount;
		++index)
	{
		if (preparationCheckpoint &&
			!preparationCheckpoint())
		{
			return false;
		}
		const std::string section =
			convert::formatString(
				"NPC%03d",
				static_cast<int>(randomOne ? randomIndex : index));
		auto npc = std::make_shared<NPC>();
		npc->initFromIni(
			preparedLoad.parsedIni.get(),
			section);
		loadedNpcList.push_back(std::move(npc));
	}
	if (preparationCheckpoint &&
		!preparationCheckpoint())
	{
		return false;
	}

	bool mutationMarked = false;
	const auto markMutation =
		[&]()
		{
			if (!mutationMarked)
			{
				mutationMarked = true;
				if (beforeMutation)
				{
					beforeMutation();
				}
			}
		};
	if (clearCurrent)
	{
		markMutation();
		gm->partnerManager.extractPartnerListFromNPCManager();
		releaseManagedNPCs(false);
	}
	for (const auto& npc : loadedNpcList)
	{
		markMutation();
		addNPC(npc);
	}
	if (clearCurrent)
	{
		gm->partnerManager.transferPartnerListToNPCManager();
		tryCleanActionImageList();
	}
	return true;
}

bool NPCManager::validate(const std::string& fileName)
{
	std::unique_ptr<char[]> data;
	int length = 0;
	std::string loadedPath;
	if (!SaveFileManager::ReadNpcObjFile(
			fileName,
			data,
			length,
			&loadedPath,
			MaximumNpcListFileBytes))
	{
		GameLog::write(
			"NPCManager: NPC preflight read failed %s\n",
			fileName.c_str());
		return false;
	}

	INIReader ini(data);
	if (ini.ParseError() != 0)
	{
		GameLog::write(
			"NPCManager: NPC preflight parse failed %s\n",
			loadedPath.c_str());
		return false;
	}
	int count = 0;
	if (!NPCPersistence::readCount(
			ini,
			NPCPersistence::MaximumNpcCount,
			count))
	{
		return false;
	}
	for (int index = 0; index < count; ++index)
	{
		if (!ini.HasSection(
				convert::formatString("NPC%03d", index)))
		{
			return false;
		}
	}
	return true;
}

bool NPCManager::load(
	const std::string& fileName,
	bool clearCurrent,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint,
	bool allowIncompleteSectionList)
{
	PreparedLoad preparedLoad;
	return prepareLoad(
			fileName,
			preparedLoad,
			allowIncompleteSectionList) &&
		commitPreparedLoad(
			preparedLoad,
			clearCurrent,
			beforeMutation,
			preparationCheckpoint);
}

bool NPCManager::loadExactResourceBytes(
	const std::string& virtualPath,
	const std::vector<std::uint8_t>& bytes,
	bool clearCurrent,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	if (virtualPath.empty() ||
		bytes.empty() ||
		bytes.size() > static_cast<std::size_t>(
			MaximumNpcListFileBytes) ||
		std::find(bytes.begin(), bytes.end(), std::uint8_t{ 0 }) !=
			bytes.end() ||
		gm == nullptr ||
		gm->player == nullptr ||
		gm->map == nullptr)
	{
		return false;
	}

	PreparedLoad preparedLoad;
	return prepareExactResourceBytes(
			virtualPath,
			bytes,
			preparedLoad) &&
		commitPreparedLoad(
			preparedLoad,
			clearCurrent,
			beforeMutation,
			preparationCheckpoint);
}

bool NPCManager::save(const std::string & fileName)
{
	if (fileName.empty())
	{
		return true;
	}
	
	INIReader ini;

	std::string section = "Head";
	ini.Set(section, "Map", GameManager::getInstance()->global.data.mapName);

	int npcCount = 0;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (shouldPersistNPC(npcList[i])
			&& npcList[i]->kind != nkPartner
			&& npcList[i]->kind != nkPlayer)
		{
			auto section = convert::formatString("NPC%03d", npcCount++);
			npcList[i]->saveToIni(&ini, section);
		}
	}
	ini.SetInteger("Head", "Count", npcCount);

	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + fileName);

	SaveFileManager::AppendFile(fileName);
	return saved;
}

void NPCManager::onUpdate()
{
	std::vector<int> idx;
	idx.resize(0);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->isVisibleByVariable)
		{
			unsigned int ret = npcList[i]->getResult();
			if (ret & erRunDeathScript)
			{
				EventInfo eventInfo;
				eventInfo.npc = npcList[i];
				eventInfo.scriptName = npcList[i]->deathScript;
				//死亡脚本运行后清除
				npcList[i]->deathScript = "";
				eventInfo.scriptMapName = gm->mapFolderName;
				gm->eventList.push_back(eventInfo);
			}
			if ((ret & erRunDeathScript) && (ret & erLifeExhaust))
			{
				npcList[i]->result |= erLifeExhaust;
			}
			else if (ret & erLifeExhaust)
			{
				bool shouldRevive = npcList[i]->reviveMilliseconds > 0;
				if (npcList[i]->isBodyIniAdded == 0)
				{
					if (!npcList[i]->transientSummonedNPC && !npcList[i]->noAddBody)
					{
						npcList[i]->addBody(shouldRevive ? npcList[i]->leftMillisecondsToRevive : 0);
					}
					addDefeatedNPCDrop(npcList[i]);
					npcList[i]->isBodyIniAdded = 1;
				}
				if (shouldRevive)
				{
					npcList[i]->removeFromDataMap();
				}
				else
				{
					idx.push_back(i);
				}
			}
		}
	}

	if (idx.size() > 0)
	{
		deleteNPC(idx);
	}
	npcAutoAction();
	advanceDeadNPCInfos();
	sortChildrenByY();
}

void NPCManager::onEvent()
{
	clickIndex = -1;
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] != nullptr && npcList[i]->isVisibleByVariable && npcList[i]->selecting)
		{
			clickIndex = i;
			break;
		}
	}
}
