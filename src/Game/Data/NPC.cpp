#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include "../../Engine/Engine.h"
#include <climits>
#include <cstdint>
#include <exception>
#include <limits>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "NPC.h"
#include "NPCPersistence.h"
#include "BuySellInventory.h"
#include "ColorStyle.h"
#include "DefeatedNpcExperience.h"
#include "Goods.h"
#include "MagicHitRate.h"
#include "MagicRegionShape.h"
#include "MobileTouchInteraction.h"
#include "NPCManager.h"
#include "NPCAction/NPCBounceMotion.h"
#include "Player.h"
#include "../GameManager/GameManager.h"
#include "../Config/Config.h"
#include "../../Image/IMP.h"
#include "../../File/INIReader.h"
#include "../../Resource/ResourceManager.h"

namespace
{
constexpr UTime MaximumPersistedDeathActionMilliseconds = 600000;
constexpr UTime CriticalDamageTipDurationMilliseconds = 600;
constexpr int CriticalDamageTipFontSize = 20;
constexpr unsigned int CriticalDamageTipColor = 0xFFFF8C00;

int addRepeatedSaturated(int current, int value, int count)
{
	if (count <= 0)
	{
		return current;
	}
	const int64_t total = static_cast<int64_t>(current) +
		static_cast<int64_t>(value) * count;
	if (total > INT_MAX)
	{
		return INT_MAX;
	}
	if (total < INT_MIN)
	{
		return INT_MIN;
	}
	return static_cast<int>(total);
}

bool usesNpcSpecialActionOverlay(ScriptSpecialActionMode mode)
{
	return mode == ScriptSpecialActionMode::Overlay;
}

bool isOneShotActionWhileScriptSpecialActionIsActive(NPCActionType action)
{
	switch (action)
	{
	case NPCActionType::acJump:
	case NPCActionType::acAJump:
	case NPCActionType::acAttack:
	case NPCActionType::acAttack1:
	case NPCActionType::acAttack2:
	case NPCActionType::acSpecialAttack:
	case NPCActionType::acMagic:
	case NPCActionType::acHurt:
	case NPCActionType::acSit:
	case NPCActionType::acSitting:
	case NPCActionType::acDeath:
	case NPCActionType::acSpecial:
		return true;
	default:
		return false;
	}
}

bool hasDirectoryPart(const std::string& fileName)
{
	return fileName.find_first_of("\\/") != std::string::npos;
}

bool isRegionShapeWithRange(int region)
{
	return region == mrSquare || region == mrCross || region == mrWave || region == mrTriangle || region == mrVType;
}

bool isExactPositionRegionShape(int region)
{
	return region == mrCross || region == mrVType;
}

int getChangeMagicHitIconDirection(float radians)
{
	float tangentAngle = (float)std::atan2(std::sin(radians), std::cos(radians));
	return NPC::getDirection(tangentAngle);
}

float getChangeMagicHitGap(size_t iconCount)
{
	return (float)(360 / (int)iconCount);
}

bool isVTypeRegionHit(Point casterPosition, Point targetPosition, int level)
{
	int direction = NPC::getDirection(casterPosition, targetPosition);
	auto tiles = getVTypeMagicRegionTiles(casterPosition, direction, level);
	for (const auto& tile : tiles)
	{
		if (tile.position == targetPosition)
		{
			return true;
		}
	}
	return false;
}

UTime readPositiveTime(INIReader * ini, const std::string & section, const std::string & name, UTime defaultValue)
{
	if (ini == nullptr)
	{
		return defaultValue;
	}
	long value = ini->GetInteger(section, name, (long)defaultValue);
	if (value <= 0)
	{
		return defaultValue;
	}
	return (UTime)value;
}

std::string toLowerIniKey(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
	{
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

bool hasIniKey(INIReader* ini, const std::string& section, const std::string& name)
{
	if (ini == nullptr)
	{
		return false;
	}
	std::vector<std::string> keys = ini->GetSectionKeys(section);
	return std::find(keys.begin(), keys.end(), toLowerIniKey(name)) != keys.end();
}

std::string normalizeEquipmentPartName(std::string part)
{
	std::transform(part.begin(), part.end(), part.begin(),
		[](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return part;
}

int clampMagicLevelForNpc(int level)
{
	if (level < 1)
	{
		return 1;
	}
	if (level > MAGIC_MAX_LEVEL)
	{
		return MAGIC_MAX_LEVEL;
	}
	return level;
}

std::string loadBuyIniStringFromFile(const std::string& buyIniFile)
{
	if (buyIniFile.empty())
	{
		return "";
	}

	std::unique_ptr<char[]> data;
	int len = File::readFile(std::string(BUYSELL_FOLDER) + buyIniFile, data);
	if (len <= 0 || data == nullptr)
	{
		return "";
	}

	BuySellInventoryData inventory;
	if (!BuySellInventory::parseText(std::string(data.get(), len), inventory))
	{
		return "";
	}
	return BuySellInventory::encodeString(BuySellInventory::serializeText(inventory));
}

void addBuyIniStringGoodsToPlayer(NPC* npc)
{
	if (npc == nullptr || npc->buyIniString.empty() || gm == nullptr)
	{
		return;
	}

	std::string decoded;
	BuySellInventoryData inventory;
	if (!BuySellInventory::decodeString(npc->buyIniString, decoded) ||
		!BuySellInventory::parseText(decoded, inventory))
	{
		return;
	}

	for (const auto& item : inventory.items)
	{
		if (item.iniFile.empty() || item.number <= 0)
		{
			continue;
		}
		gm->goodsManager.addItem(item.iniFile, item.number);
	}
}

int getPrimaryDamageForMagic(std::shared_ptr<Magic> magic, int level, std::shared_ptr<NPC> caster)
{
	return Magic::calculatePrimaryEffectAmount(magic, caster, level);
}

int getBounceCollisionTargetPriority(std::shared_ptr<NPC> candidate, std::shared_ptr<NPC> self, int launcherKind, std::shared_ptr<GameElement> launcher)
{
	if (candidate == nullptr || candidate == self)
	{
		return INT_MAX;
	}
	if (candidate->isDying() || candidate->isHiding() || candidate->getJumpState() == jsJumping)
	{
		return INT_MAX;
	}
	if (candidate != gm->player
		&& candidate->kind != nkBattle
		&& !(candidate->kind == nkPartner && gm->global.data.PartnerCombat))
	{
		return INT_MAX;
	}
	return NPCManager::getLauncherHitPriority(launcherKind, candidate, launcher);
}

template <typename NPCList>
void findBounceCollisionTargetInList(
	const NPCList& npcList,
	std::shared_ptr<NPC> self,
	int launcherKind,
	std::shared_ptr<GameElement> launcher,
	std::shared_ptr<NPC>& bestTarget,
	int& bestPriority)
{
	for (const auto& candidate : npcList)
	{
		int priority = getBounceCollisionTargetPriority(candidate, self, launcherKind, launcher);
		if (priority < bestPriority)
		{
			bestPriority = priority;
			bestTarget = candidate;
		}
	}
}

std::shared_ptr<NPC> findBounceCollisionTarget(Point position, std::shared_ptr<NPC> self, int launcherKind, std::shared_ptr<GameElement> launcher)
{
	if (gm == nullptr || gm->map == nullptr || !gm->map->isInMap(position))
	{
		return nullptr;
	}
	if (position.y < 0 || position.y >= (int)gm->map->dataMap.tile.size()
		|| position.x < 0 || position.x >= (int)gm->map->dataMap.tile[position.y].size())
	{
		return nullptr;
	}

	auto& tile = gm->map->dataMap.tile[position.y][position.x];
	std::shared_ptr<NPC> bestTarget = nullptr;
	int bestPriority = INT_MAX;
	findBounceCollisionTargetInList(tile.npcList, self, launcherKind, launcher, bestTarget, bestPriority);
	findBounceCollisionTargetInList(tile.stepNPCList, self, launcherKind, launcher, bestTarget, bestPriority);
	if (gm->player != nullptr)
	{
		bool playerAtPosition = gm->player->getPosition() == position;
		if (!playerAtPosition)
		{
			const auto stepPositions = gm->player->getStepPositions();
			playerAtPosition = std::find(stepPositions.begin(), stepPositions.end(), position) != stepPositions.end();
		}
		if (playerAtPosition)
		{
			int priority = getBounceCollisionTargetPriority(gm->player, self, launcherKind, launcher);
			if (priority < bestPriority)
			{
				bestPriority = priority;
				bestTarget = gm->player;
			}
		}
	}
	return bestTarget;
}

template <typename NPCList>
bool hasCharacterObstacleInList(const NPCList& npcList, const NPC* self)
{
	for (const auto& candidate : npcList)
	{
		if (candidate != nullptr && candidate.get() != self && candidate->isObstacleForCharacter())
		{
			return true;
		}
	}
	return false;
}

bool isPlayerObstacleAt(Point position, const NPC* self)
{
	if (gm == nullptr || gm->player == nullptr || gm->player.get() == self || !gm->player->isObstacleForCharacter())
	{
		return false;
	}
	if (gm->player->getPosition() == position)
	{
		return true;
	}
	const auto stepPositions = gm->player->getStepPositions();
	return std::find(stepPositions.begin(), stepPositions.end(), position) != stepPositions.end();
}

bool hasCharacterObstacleAt(Point position, const NPC* self)
{
	if (gm == nullptr || gm->map == nullptr || !gm->map->isInMap(position))
	{
		return false;
	}
	if (position.y < 0 || position.y >= (int)gm->map->dataMap.tile.size()
		|| position.x < 0 || position.x >= (int)gm->map->dataMap.tile[position.y].size())
	{
		return false;
	}

	auto& tile = gm->map->dataMap.tile[position.y][position.x];
	return hasCharacterObstacleInList(tile.npcList, self)
		|| hasCharacterObstacleInList(tile.stepNPCList, self)
		|| isPlayerObstacleAt(position, self);
}

bool isMagicForcedMoveBlockedAt(Point position, const NPC* self, bool blockCharactersOnPath)
{
	if (gm == nullptr || gm->map == nullptr || !gm->map->isInMap(position))
	{
		return true;
	}
	if (!gm->map->canJump(position))
	{
		return true;
	}
	return blockCharactersOnPath && hasCharacterObstacleAt(position, self);
}

bool isBounceMovementBlockedAt(Point position, const NPC* self)
{
	if (gm == nullptr || gm->map == nullptr || !gm->map->isInMap(position))
	{
		return true;
	}
	if (!gm->map->canWalk(position))
	{
		return true;
	}
	return hasCharacterObstacleAt(position, self);
}

bool findMagicForcedMovePathBlock(
	const NPC* self,
	Point from,
	PointEx fromOffset,
	Point to,
	PointEx toOffset,
	PointEx directionVector,
	bool blockCharactersOnPath,
	Point& lastOpenPosition,
	Point& blockedPosition)
{
	lastOpenPosition = from;
	blockedPosition = from;
	if (gm == nullptr || gm->map == nullptr)
	{
		return false;
	}
	if (from == to)
	{
		if (isMagicForcedMoveBlockedAt(to, self, blockCharactersOnPath))
		{
			blockedPosition = to;
			return true;
		}
		lastOpenPosition = to;
		return false;
	}

	Point pathDirection =
	{
		static_cast<int>(std::round(directionVector.x * 1000.0f)),
		static_cast<int>(std::round(directionVector.y * 1000.0f))
	};
	auto passPath = gm->map->getPassPathEx(from, fromOffset, to, toOffset, pathDirection);
	if (passPath.empty())
	{
		passPath.push_back(to);
	}

	bool sawDestination = false;
	for (const auto& passPosition : passPath)
	{
		if (passPosition == from)
		{
			continue;
		}
		if (passPosition == to)
		{
			sawDestination = true;
		}
		if (isMagicForcedMoveBlockedAt(passPosition, self, blockCharactersOnPath))
		{
			blockedPosition = passPosition;
			return true;
		}
		lastOpenPosition = passPosition;
	}

	if (!sawDestination)
	{
		if (isMagicForcedMoveBlockedAt(to, self, blockCharactersOnPath))
		{
			blockedPosition = to;
			return true;
		}
		lastOpenPosition = to;
	}
	return false;
}

int getLauncherKindForNPC(const NPC& npc)
{
	if (npc.kind == nkPlayer)
	{
		return lkSelf;
	}
	if (gm != nullptr && gm->player != nullptr)
	{
		auto controlled = gm->player->getControlledCharacter();
		if (controlled != nullptr && controlled.get() == &npc)
		{
			return lkSelf;
		}
	}
	if (npc.relation == nrFriendly)
	{
		return lkFriend;
	}
	if (npc.relation == nrHostile)
	{
		return lkEnemy;
	}
	if (npc.relation == nrNeutral)
	{
		return lkNeutral;
	}
	return lkNeutral;
}

std::string getSummonMagicKey(const Magic& magic)
{
	if (!magic.iniName.empty())
	{
		return magic.iniName;
	}
	return magic.name;
}

PointEx getEffectHitDirectionVector(NPC& target, const Effect& effect)
{
	PointEx directionVector = getEffectProjectedMovementVector(effect.flyingDirection);
	if (directionVector.x == 0.0f && directionVector.y == 0.0f)
	{
		auto tileDelta = Map::getTilePosition(target.getPosition(), effect.position, { 0, 0 }, { 0, 0 });
		directionVector = { (float)tileDelta.x, (float)tileDelta.y };
	}
	if (directionVector.x == 0.0f && directionVector.y == 0.0f)
	{
		if (auto userPtr = effect.user.lock())
		{
			auto tileDelta = Map::getTilePosition(target.getPosition(), userPtr->position, { 0, 0 }, { 0, 0 });
			directionVector = { (float)tileDelta.x, (float)tileDelta.y };
		}
	}
	return directionVector;
}

Point findDistanceTileInDirection(Point start, PointEx directionVector, int distance)
{
	if (distance <= 0 || gm == nullptr || gm->map == nullptr)
	{
		return start;
	}
	float directionLength = hypot(directionVector.x, directionVector.y);
	if (directionLength <= 0.0f)
	{
		return start;
	}

	int direction = NPC::getDirection(atan2(directionVector.x, -directionVector.y));
	Point destination = start;
	for (int i = 0; i < distance; ++i)
	{
		Point next = Map::getSubPoint(destination, direction);
		if (!gm->map->isInMap(next))
		{
			break;
		}
		destination = next;
	}
	return destination;
}

void applyDirectMagicMoveHurt(NPC* target, int hurt)
{
	if (target == nullptr || hurt <= 0 || target->isDying() || target->isHiding())
	{
		return;
	}
	if (target->hasActiveSelfMagic(mskBlockDamage))
	{
		return;
	}
	target->addLife(-hurt);
	if (target->life <= 0)
	{
		target->life = 0;
		target->handleDeath();
	}
	else
	{
		target->beginHurt();
	}
}

void applyDirectMagicMoveHurt(std::shared_ptr<NPC> target, int hurt)
{
	applyDirectMagicMoveHurt(target.get(), hurt);
}

std::string makeShadowFileName(const std::string& imageFileName)
{
	std::string shadowFileName = imageFileName;
	size_t directoryPosition = shadowFileName.find_last_of("\\/");
	size_t extensionPosition = shadowFileName.find_last_of('.');
	if (extensionPosition != std::string::npos &&
		(directoryPosition == std::string::npos || extensionPosition > directoryPosition))
	{
		shadowFileName.erase(extensionPosition);
	}
	shadowFileName += ".shd";
	return shadowFileName;
}

std::string trimAsciiCopy(std::string value)
{
	auto isSpace = [](unsigned char character)
	{
		return std::isspace(character) != 0;
	};
	auto first = std::find_if_not(value.begin(), value.end(), isSpace);
	auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
	if (first >= last)
	{
		return "";
	}
	return std::string(first, last);
}

std::string toLowerAsciiCopy(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

std::string canonicalNpcResourceActionSection(const std::string& section)
{
	std::string normalized = toLowerAsciiCopy(trimAsciiCopy(section));
	if (normalized == "fightstand")
	{
		return "astand";
	}
	if (normalized == "fightwalk")
	{
		return "awalk";
	}
	if (normalized == "fightrun")
	{
		return "arun";
	}
	if (normalized == "fightjump")
	{
		return "ajump";
	}
	return normalized;
}

bool isNpcResourceActionSection(const std::string& section)
{
	static const std::unordered_set<std::string> actionSections = {
		"stand",
		"stand1",
		"walk",
		"run",
		"jump",
		"attack",
		"attack1",
		"attack2",
		"magic",
		"hurt",
		"death",
		"sit",
		"astand",
		"awalk",
		"arun",
		"ajump",
	};
	return actionSections.find(canonicalNpcResourceActionSection(section)) != actionSections.end();
}

bool isNpcEntitySection(const std::string& section)
{
	std::string normalized = toLowerAsciiCopy(trimAsciiCopy(section));
	if (normalized == "head" || normalized == "init")
	{
		return true;
	}
	if (normalized.size() <= 3 || normalized.rfind("npc", 0) != 0)
	{
		return false;
	}
	for (size_t i = 3; i < normalized.size(); i++)
	{
		if (!std::isdigit(static_cast<unsigned char>(normalized[i])))
		{
			return false;
		}
	}
	return true;
}

bool isNpcResourceIni(INIReader& ini)
{
	bool hasActionSection = false;
	bool hasActionResourceKey = false;
	for (const auto& section : ini.GetSectionNames())
	{
		if (isNpcEntitySection(section))
		{
			return false;
		}
		if (!isNpcResourceActionSection(section))
		{
			continue;
		}
		hasActionSection = true;
		for (const auto& key : ini.GetSectionKeys(section))
		{
			std::string normalizedKey = toLowerAsciiCopy(trimAsciiCopy(key));
			if (normalizedKey == "image" || normalizedKey == "shade" || normalizedKey == "sound")
			{
				hasActionResourceKey = true;
				break;
			}
		}
	}
	return hasActionSection && hasActionResourceKey;
}

UTime readSecondsFieldAsMilliseconds(INIReader* ini, const std::string& section, const std::string& secondsName)
{
	if (ini == nullptr)
	{
		return 0;
	}
	std::string valueText = trimAsciiCopy(ini->Get(section, secondsName, ""));
	if (valueText.empty())
	{
		return 0;
	}
	double seconds = 0.0;
	try
	{
		seconds = std::stod(valueText);
	}
	catch (const std::exception&)
	{
		return 0;
	}
	if (!std::isfinite(seconds) || seconds <= 0.0)
	{
		return 0;
	}
	const double milliseconds = std::round(seconds * 1000.0);
	if (!std::isfinite(milliseconds)
		|| milliseconds >= static_cast<double>(
			std::numeric_limits<UTime>::max()))
	{
		return std::numeric_limits<UTime>::max();
	}
	return milliseconds > 0.0
		? static_cast<UTime>(milliseconds)
		: 0;
}

float millisecondsToSeconds(UTime milliseconds)
{
	return static_cast<float>(milliseconds) / 1000.0f;
}

bool readBooleanAlias(INIReader* ini, const std::string& section,
	const std::string& primaryName, const std::string& fallbackName, bool defaultValue)
{
	if (ini == nullptr)
	{
		return defaultValue;
	}
	if (!trimAsciiCopy(ini->Get(section, primaryName, "")).empty())
	{
		return ini->GetBoolean(section, primaryName, defaultValue);
	}
	return ini->GetBoolean(section, fallbackName, defaultValue);
}

std::string getCharacterNameForStateSource(std::shared_ptr<GameElement> source)
{
	if (source == nullptr)
	{
		return "";
	}
	auto sourceNPC = std::dynamic_pointer_cast<NPC>(source);
	if (sourceNPC != nullptr && !sourceNPC->npcName.empty())
	{
		return sourceNPC->npcName;
	}
	return source->name;
}

bool sameCharacterName(const std::string& left, const std::string& right)
{
	return toLowerAsciiCopy(trimAsciiCopy(left)) == toLowerAsciiCopy(trimAsciiCopy(right));
}

bool hasFileExtension(const std::string& fileName)
{
	size_t directoryPosition = fileName.find_last_of("\\/");
	size_t extensionPosition = fileName.find_last_of('.');
	return extensionPosition != std::string::npos &&
		(directoryPosition == std::string::npos || extensionPosition > directoryPosition);
}

std::string normalizeSignalImageFileName(std::string fileName)
{
	fileName = trimAsciiCopy(fileName);
	std::replace(fileName.begin(), fileName.end(), '/', '\\');
	return fileName;
}

void appendUniqueSignalImageCandidate(std::vector<std::string>& candidates, const std::string& fileName)
{
	if (fileName.empty())
	{
		return;
	}
	for (const std::string& candidate : candidates)
	{
		if (candidate == fileName)
		{
			return;
		}
	}
	candidates.push_back(fileName);
}

void appendSignalImageCandidate(std::vector<std::string>& candidates, const std::string& fileName)
{
	std::string normalizedFileName = normalizeSignalImageFileName(fileName);
	if (normalizedFileName.empty())
	{
		return;
	}
	if (!hasDirectoryPart(normalizedFileName))
	{
		normalizedFileName = "asf\\signal\\" + normalizedFileName;
	}
	appendUniqueSignalImageCandidate(candidates, normalizedFileName);
	if (!hasFileExtension(normalizedFileName))
	{
		appendUniqueSignalImageCandidate(candidates, normalizedFileName + ".png");
		appendUniqueSignalImageCandidate(candidates, normalizedFileName + ".asf");
		appendUniqueSignalImageCandidate(candidates, normalizedFileName + ".mpc");
	}
}

std::string readSignalIconFileName(int signalIndex)
{
	INIReader ini("ini\\ui\\tips\\SignalFile.ini");
	if (ini.ParseError() != 0)
	{
		return "";
	}
	return ini.Get("SignalIcon", std::to_string(signalIndex), "");
}

_shared_imp loadSignalImagePackage(int signalIndex)
{
	if (signalIndex <= 0)
	{
		return nullptr;
	}

	std::vector<std::string> candidates;
	appendSignalImageCandidate(candidates, readSignalIconFileName(signalIndex));
	appendSignalImageCandidate(candidates, std::to_string(signalIndex));

	for (const std::string& candidate : candidates)
	{
		_shared_imp imagePackage = IMP::createIMPImage(candidate);
		if (imagePackage != nullptr)
		{
			return imagePackage;
		}
	}
	return nullptr;
}

int getHexValue(char value)
{
	unsigned char c = static_cast<unsigned char>(value);
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	c = static_cast<unsigned char>(std::tolower(c));
	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	return -1;
}

bool parseLittleEndianHexInt(const std::string& value, size_t offset, int& result)
{
	if (offset + 8 > value.size())
	{
		return false;
	}

	unsigned int parsed = 0;
	for (size_t byteIndex = 0; byteIndex < 4; ++byteIndex)
	{
		int high = getHexValue(value[offset + byteIndex * 2]);
		int low = getHexValue(value[offset + byteIndex * 2 + 1]);
		if (high < 0 || low < 0)
		{
			return false;
		}
		parsed |= static_cast<unsigned int>((high << 4) | low) << (byteIndex * 8);
	}

	result = static_cast<int>(parsed);
	return true;
}
}

NPC::NPC()
{
	name = "npc";
	setPriority(epNPC);
	rect.w = TILE_WIDTH - 20;
	rect.h = (int)((float)TILE_HEIGHT * 2.5);
	actionManager = std::make_unique<NPCActionManager>(this);
}

NPC::~NPC()
{
	freeResource();
	if (type)
	{
		return;
	}
}

// ===================== 脚本调用接口 =====================

unsigned int NPC::eventRun()
{
	coverMouse = false;
	unsigned int ret = run();
	coverMouse = true;
	return ret;
}

void NPC::jumpTo(Point dest)
{
	destGE.reset();
	beginJump(dest);
	eventRun();
}

void NPC::runTo(Point dest)
{
	destGE.reset();
	if (position == dest)
	{
		return;
	}
	int dir = getDirection(position, dest);
	while (position != dest)
	{
		beginRun(dest);
		if (stepList.size() == 0)
		{
			break;
		}
		eventRun();
	}
	direction = dir;
}

void NPC::goTo(Point dest)
{
	destGE.reset();
	haveAsyncDest = false;
	if (position == dest)
	{
		return;
	}
	int dir = getDirection(position, dest);
	while (position != dest)
	{
		beginWalk(dest);
		if (stepList.size() == 0)
		{
			//beginStand();
			break;
		}
		eventRun();
	}
	direction = dir;
}

void NPC::goToEx(Point dest)
{
	destGE.reset();
	haveAsyncDest = true;
	gotoExDest = dest;
	beginWalk(dest);
}

void NPC::goToDir(int dir, int distance)
{
	Point pos = position;
	for (int i = 0; i < distance; i++)
	{
		pos = gm->map->getSubPoint(pos, dir);
	}
	goTo(pos);
}

void NPC::setFixedPos(const std::string& value)
{
	fixedPos = value;
	fixedPathTilePositions.clear();
	currentFixedPosIndex = 0;

	if (fixedPos.size() < 16)
	{
		return;
	}

	for (size_t offset = 0; offset + 15 < fixedPos.size(); offset += 16)
	{
		int x = 0;
		int y = 0;
		if (!parseLittleEndianHexInt(fixedPos, offset, x) ||
			!parseLittleEndianHexInt(fixedPos, offset + 8, y))
		{
			break;
		}
		if (x == 0 && y == 0)
		{
			break;
		}
		fixedPathTilePositions.push_back({ x, y });
	}

	if (fixedPathTilePositions.size() < 2)
	{
		fixedPathTilePositions.clear();
	}
}

bool NPC::hasFixedPath() const
{
	return fixedPathTilePositions.size() >= 2;
}

bool NPC::beginFixedPathWalk()
{
	if (!hasFixedPath())
	{
		return false;
	}
	if (currentFixedPosIndex >= fixedPathTilePositions.size())
	{
		currentFixedPosIndex = 0;
	}

	size_t checkedCount = 0;
	while (checkedCount < fixedPathTilePositions.size() &&
		position == fixedPathTilePositions[currentFixedPosIndex])
	{
		currentFixedPosIndex = (currentFixedPosIndex + 1) % fixedPathTilePositions.size();
		++checkedCount;
	}
	if (checkedCount >= fixedPathTilePositions.size())
	{
		return false;
	}

	beginWalk(fixedPathTilePositions[currentFixedPosIndex]);
	return isWalking();
}

void NPC::setDestinationMapPosition(Point destination)
{
	destinationMapPosition = destination;
	haveAsyncDest = false;
}

void NPC::clearDestinationMapPosition()
{
	destinationMapPosition = { 0, 0 };
}

bool NPC::hasDestinationMapPosition() const
{
	return destinationMapPosition.x != 0 || destinationMapPosition.y != 0;
}

bool NPC::isAIEnabled() const
{
	return gm != nullptr && gm->global.data.NPCAI && !isAIDisabled;
}

void NPC::setAIDisabled(bool disabled)
{
	if (isAIDisabled == disabled)
	{
		return;
	}
	isAIDisabled = disabled;
	if (disabled)
	{
		clearCombatTargetMemory();
		nextFollowCheckTime = 0;
		lastBattleScanTime = 0;
	}
}

void NPC::clearActionPathTilePositions()
{
	actionPathTilePositions.clear();
}

bool NPC::ensureActionPathTilePositions(bool isFlyer, int count, int maxOffset)
{
	if (!actionPathTilePositions.empty())
	{
		return actionPathTilePositions.size() >= 2;
	}
	if (gm == nullptr || gm->map == nullptr || engine == nullptr || count <= 0)
	{
		return false;
	}

	actionPathTilePositions.push_back(position);
	int effectiveMaxOffset = maxOffset >= 0 ? maxOffset : (isFlyer ? 15 : 10);
	int maxTry = count * 3;
	while ((int)actionPathTilePositions.size() < count && maxTry-- >= 0)
	{
		int offsetX = effectiveMaxOffset > 0 ? engine->getRand(effectiveMaxOffset * 2) - effectiveMaxOffset : 0;
		int offsetY = effectiveMaxOffset > 0 ? engine->getRand(effectiveMaxOffset * 2) - effectiveMaxOffset : 0;
		Point candidate = { position.x + offsetX, position.y + offsetY };
		if (!gm->map->isInMap(candidate) || candidate == position)
		{
			continue;
		}
		if (!isFlyer)
		{
			auto linePath = gm->map->getLinePath(position, candidate, effectiveMaxOffset * 2);
			if (linePath.empty())
			{
				continue;
			}
			bool blocked = false;
			for (const auto& step : linePath)
			{
				if (!gm->map->canWalk(step))
				{
					blocked = true;
					break;
				}
			}
			if (blocked)
			{
				continue;
			}
		}
		actionPathTilePositions.push_back(candidate);
	}
	return actionPathTilePositions.size() >= 2;
}

bool NPC::beginRandWalkFromActionPath(bool isFlyer, int count, int maxOffset, bool cachePath)
{
	if (gm == nullptr || gm->map == nullptr || engine == nullptr)
	{
		return false;
	}

	std::vector<Point> localPath;
	if (cachePath)
	{
		if (!ensureActionPathTilePositions(isFlyer, count, maxOffset))
		{
			return false;
		}
		localPath = actionPathTilePositions;
	}
	else
	{
		std::vector<Point> savedPath = actionPathTilePositions;
		actionPathTilePositions.clear();
		bool hasPath = ensureActionPathTilePositions(isFlyer, count, maxOffset);
		localPath = actionPathTilePositions;
		actionPathTilePositions = savedPath;
		if (!hasPath)
		{
			return false;
		}
	}

	int pathCount = static_cast<int>(localPath.size());
	if (pathCount < 2)
	{
		return false;
	}
	int startIndex = pathCount > 1 ? engine->getRand(pathCount - 1) : 0;
	for (int attempt = 0; attempt < pathCount; attempt++)
	{
		int index = (startIndex + attempt) % pathCount;
		Point destination = localPath[index];
		if (destination == position)
		{
			continue;
		}
		auto tempList = findPathByType(destination, isFlyer ? nptPathStraightLine : nptEnd);
		if (!tempList.empty() && canEnterMoveStep(tempList[0]))
		{
			stepList = tempList;
			direction = getDirection(stepList[0]);
			actionManager->changeAction(acWalk);
			return isWalking();
		}
	}
	return false;
}

int NPC::resolvePathType() const
{
	return resolvePathTypeForState(kind, pathFinder, hasFixedPath(), isEnemy());
}

int NPC::resolveDestinationPathType() const
{
	return hasDestinationMapPosition() ? nptPerfectMaxPlayerTry : nptEnd;
}

std::deque<Point> NPC::findPathByType(Point dest, int pathType, bool temporaryDisableRestrict) const
{
	if (gm == nullptr || gm->map == nullptr)
	{
		return {};
	}
	if (pathType == nptEnd)
	{
		pathType = resolvePathType();
	}

	switch (pathType)
	{
	case nptPathOneStep:
		return gm->map->traceTowardTarget(position, dest, 10, getMoveDirectionCount());
	case nptPathStraightLine:
		return gm->map->getLinePath(position, dest, 100);
	case nptSimpleMaxNpcTry:
		return gm->map->findSimplePath(position, dest, getMoveDirectionCount(), getPathSearchMaxTryForPathType(pathType, temporaryDisableRestrict));
	case nptPerfectMaxNpcTry:
	case nptPerfectMaxPlayerTry:
	default:
		return gm->map->findPath(position, dest, getMoveDirectionCount(), getPathSearchMaxTryForPathType(pathType, temporaryDisableRestrict));
	}
}

bool NPC::canEnterMoveStep(Point step) const
{
	if (gm == nullptr || gm->map == nullptr)
	{
		return false;
	}
	if (kind == nkFlyingAnimal)
	{
		return gm->map->isInMap(step);
	}
	return gm->map->canWalk(step);
}

bool NPC::isDestinationMapPositionBlockedByCharacter() const
{
	if (!hasDestinationMapPosition() || gm == nullptr || gm->map == nullptr || !gm->map->isInMap(destinationMapPosition))
	{
		return false;
	}
	const auto& tile = gm->map->dataMap.tile[destinationMapPosition.y][destinationMapPosition.x];
	for (const auto& npc : tile.npcList)
	{
		if (npc != nullptr && npc.get() != this && npc->isObstacleForCharacter())
		{
			return true;
		}
	}
	for (const auto& npc : tile.stepNPCList)
	{
		if (npc != nullptr && npc.get() != this && npc->isObstacleForCharacter())
		{
			return true;
		}
	}
	return false;
}

bool NPC::tryMoveToDestinationMapPosition()
{
	if (!hasDestinationMapPosition())
	{
		return false;
	}
	if (position == destinationMapPosition)
	{
		clearDestinationMapPosition();
		return false;
	}
	if (gm == nullptr || gm->map == nullptr || !gm->map->isInMap(destinationMapPosition))
	{
		clearDestinationMapPosition();
		return false;
	}
	if (lastPathFindFailTime > 0 && getTime() - lastPathFindFailTime < NPC_PATH_FIND_FAIL_COOLDOWN)
	{
		return false;
	}

	auto tempList = findPathByType(destinationMapPosition, resolveDestinationPathType(), true);
	if (!tempList.empty())
	{
		if (!canEnterMoveStep(tempList[0]))
		{
			lastPathFindFailTime = getTime();
			clearDestinationMapPosition();
			beginStand();
			return false;
		}
		stepList = tempList;
		direction = getDirection(stepList[0]);
		actionManager->changeAction(acWalk);
	}
	else
	{
		lastPathFindFailTime = getTime();
	}
	if (!isWalking())
	{
		clearDestinationMapPosition();
		return false;
	}
	if (stepList.size() == 1 && isDestinationMapPositionBlockedByCharacter())
	{
		clearDestinationMapPosition();
		beginStand();
		return false;
	}
	return true;
}

void NPC::attackTo(Point dest, std::shared_ptr<GameElement> target)
{
	beginAttack(dest, target);
	eventRun();
}

bool NPC::startScriptSpecialAction(const std::string& fileName)
{
	const NPCActionRes previousSpecialAction = res.special;
	loadSpecialAction(fileName);
	if (!canDoAction(&res.special))
	{
		res.special = previousSpecialAction;
		return false;
	}

	const bool useOverlay = gm != nullptr
		&& usesNpcSpecialActionOverlay(gm->global.specialActionMode);
	if (useOverlay)
	{
		scriptSpecialActionOverlayResource = res.special;
		res.special = previousSpecialAction;
		scriptSpecialActionOverlayActive = true;
		scriptSpecialActionOverlaySupersededByAction = false;
		scriptSpecialActionOverlayElapsed = 0;
		scriptSpecialActionOverlayDuration = IMP::getIMPImageActionTime(
			scriptSpecialActionOverlayResource.imagePackage);
		scriptSpecialActionOverlayDirection = direction;
		scriptSpecialActionUnderlyingActionRevision = actionManager->getActionRevision();
		return true;
	}

	scriptSpecialActionOverlayActive = false;
	scriptSpecialActionOverlaySupersededByAction = false;
	actionManager->restartActionIgnoringTransitions(acSpecial);
	return actionManager->isDoingSpecialAction();
}

void NPC::finishScriptSpecialActionOverlay()
{
	scriptSpecialActionOverlayElapsed = scriptSpecialActionOverlayDuration;
	scriptSpecialActionOverlayActive = false;
	scriptSpecialActionOverlaySupersededByAction = false;
	direction = scriptSpecialActionOverlayDirection;
	if (auto player = dynamic_cast<Player*>(this))
	{
		player->resetRecoveryTime(getTime());
	}
}

void NPC::updateScriptSpecialActionOverlay(UTime frameTime)
{
	if (!scriptSpecialActionOverlayActive)
	{
		return;
	}

	const UTime remainingTime = scriptSpecialActionOverlayDuration > scriptSpecialActionOverlayElapsed
		? scriptSpecialActionOverlayDuration - scriptSpecialActionOverlayElapsed
		: 0;
	if (frameTime >= remainingTime)
	{
		finishScriptSpecialActionOverlay();
		return;
	}

	scriptSpecialActionOverlayElapsed += frameTime;
}

void NPC::updateEventRunState()
{
	if (!logicRunning)
	{
		return;
	}

	if ((eventRunUntilScriptSpecialActionEnds && !scriptSpecialActionOverlayActive)
		|| (!eventRunUntilScriptSpecialActionEnds && (isStanding() || isDying() || isHiding())))
	{
		logicRunning = false;
	}
}

void NPC::pauseScriptSpecialActionUnderlyingAction(UTime frameTime)
{
	actionBeginTime += frameTime;
	stepBeginTime += frameTime;
	if (resumingMove)
	{
		hurtBeginStepTime += frameTime;
	}
}

bool NPC::updateScriptSpecialActionOverlayForFrame(UTime frameTime)
{
	if (!scriptSpecialActionOverlayActive)
	{
		return false;
	}

	const uint64_t currentActionRevision = actionManager->getActionRevision();
	if (currentActionRevision != scriptSpecialActionUnderlyingActionRevision)
	{
		scriptSpecialActionUnderlyingActionRevision = currentActionRevision;
		scriptSpecialActionOverlaySupersededByAction = true;
		if (!isOneShotActionWhileScriptSpecialActionIsActive(nowAction))
		{
			finishScriptSpecialActionOverlay();
			updateEventRunState();
			return true;
		}
	}

	if (scriptSpecialActionOverlaySupersededByAction)
	{
		const UTime actionElapsed = getTime() >= actionBeginTime
			? getTime() - actionBeginTime
			: 0;
		if (actionLastTime == 0 || actionElapsed >= actionLastTime)
		{
			finishScriptSpecialActionOverlay();
		}
		updateEventRunState();
		return true;
	}

	pauseScriptSpecialActionUnderlyingAction(frameTime);
	updateScriptSpecialActionOverlay(frameTime);
	updateEventRunState();
	return true;
}

void NPC::doSpecialAction(const std::string & fileName)
{
	if (startScriptSpecialAction(fileName))
	{
		const bool previousWaitMode = eventRunUntilScriptSpecialActionEnds;
		eventRunUntilScriptSpecialActionEnds = gm != nullptr
			&& usesNpcSpecialActionOverlay(gm->global.specialActionMode);
		eventRun();
		eventRunUntilScriptSpecialActionEnds = previousWaitMode;
	}
}

void NPC::setLevel(int lvl)
{
	if (!npcLevelList.empty())
	{
		if (lvl < 1)
		{
			lvl = 1;
		}
		else if (lvl > (int)npcLevelList.size())
		{
			lvl = (int)npcLevelList.size();
		}

		const NPCLevelInfo& levelInfo = npcLevelList[lvl - 1];
		level = lvl;
		int nextLifeMax = levelInfo.lifeMax > 0 ? levelInfo.lifeMax : levelInfo.life;
		if (nextLifeMax > 0)
		{
			lifeMax = nextLifeMax;
			life = getLifeMax();
		}
		if (levelInfo.thewMax > 0)
		{
			thewMax = levelInfo.thewMax;
			thew = getThewMax();
		}
		if (levelInfo.manaMax > 0)
		{
			manaMax = levelInfo.manaMax;
			mana = getManaMax();
		}
		attack = levelInfo.attack;
		attack2 = levelInfo.attack2;
		attack3 = levelInfo.attack3;
		defend = levelInfo.defend;
		defend2 = levelInfo.defend2;
		defend3 = levelInfo.defend3;
		evade = levelInfo.evade;
		levelUpExp = levelInfo.levelUpExp;
		displayLifePercent = (getLifeMax() > 0) ? (float)life / (float)getLifeMax() : 0.0f;
		if (displayLifePercent > 1.0f) displayLifePercent = 1.0f;
		if (displayLifePercent < 0.0f) displayLifePercent = 0.0f;
		return;
	}

	level = lvl;
	life = getLifeMax();
	thew = getThewMax();
	mana = getManaMax();
}

void NPC::setPropToLevel(int lvl)
{
	if (npcLevelList.empty() || lvl < 1 || lvl > (int)npcLevelList.size())
	{
		return;
	}

	const NPCLevelInfo& levelInfo = npcLevelList[lvl - 1];
	bool isFullLife = life == getLifeMax();
	bool isFullThew = thew == getThewMax();
	bool isFullMana = mana == getManaMax();

	int nextLifeMax = levelInfo.life > 0 ? levelInfo.life : levelInfo.lifeMax;
	if (nextLifeMax > 0)
	{
		lifeMax = nextLifeMax;
	}
	if (levelInfo.thewMax > 0)
	{
		thewMax = levelInfo.thewMax;
	}
	if (levelInfo.manaMax > 0)
	{
		manaMax = levelInfo.manaMax;
	}
	if (isFullLife || life > getLifeMax())
	{
		life = getLifeMax();
	}
	if (isFullThew || thew > getThewMax())
	{
		thew = getThewMax();
	}
	if (isFullMana || mana > getManaMax())
	{
		mana = getManaMax();
	}

	attack = levelInfo.attack;
	attack2 = levelInfo.attack2;
	attack3 = levelInfo.attack3;
	defend = levelInfo.defend;
	defend2 = levelInfo.defend2;
	defend3 = levelInfo.defend3;
	evade = levelInfo.evade;
	levelUpExp = levelInfo.levelUpExp;
	displayLifePercent = (getLifeMax() > 0) ? (float)life / (float)getLifeMax() : 0.0f;
	if (displayLifePercent > 1.0f) displayLifePercent = 1.0f;
	if (displayLifePercent < 0.0f) displayLifePercent = 0.0f;
}

void NPC::loadLevel(const std::string& fileName)
{
	npcLevelList.clear();
	if (fileName.empty())
	{
		return;
	}

	std::unique_ptr<char[]> data;
	int len = File::readFile(LEVEL_FOLDER + fileName, data);
	if (len <= 0 || data == nullptr)
	{
		return;
	}

	INIReader ini(data);
	if (ini.ParseError() != 0)
	{
		return;
	}

	int levelCount = 0;
	if (!NPCPersistence::readLevelCount(ini, levelCount))
	{
		GameLog::write("NPC: invalid level count in %s\n", fileName.c_str());
		return;
	}

	npcLevelList.resize(levelCount);
	for (size_t i = 0; i < npcLevelList.size(); ++i)
	{
		std::string section = convert::formatString("Level%d", (int)i + 1);
		NPCLevelInfo& levelInfo = npcLevelList[i];
		levelInfo.exp = ini.GetInteger(section, "Exp", 0);
		levelInfo.levelUpExp = ini.GetInteger(section, "LevelUpExp", 0);
		levelInfo.life = ini.GetInteger(section, "Life", 0);
		levelInfo.lifeMax = ini.GetInteger(section, "LifeMax", 0);
		levelInfo.thewMax = ini.GetInteger(section, "ThewMax", 0);
		levelInfo.manaMax = ini.GetInteger(section, "ManaMax", 0);
		levelInfo.attack = ini.GetInteger(section, "Attack", 0);
		levelInfo.attack2 = ini.GetInteger(section, "Attack2", 0);
		levelInfo.attack3 = ini.GetInteger(section, "Attack3", 0);
		levelInfo.defend = ini.GetInteger(section, "Defend", ini.GetInteger(section, "Defence", 0));
		levelInfo.defend2 = ini.GetInteger(section, "Defend2", 0);
		levelInfo.defend3 = ini.GetInteger(section, "Defend3", 0);
		levelInfo.evade = ini.GetInteger(section, "Evade", 0);
	}
}

// ===================== 绘制与方向 =====================

int NPC::getEquipmentPartIndex(const std::string& part)
{
	std::string normalizedPart = normalizeEquipmentPartName(part);
	if (normalizedPart == "head")
	{
		return 0;
	}
	if (normalizedPart == "neck")
	{
		return 1;
	}
	if (normalizedPart == "body")
	{
		return 2;
	}
	if (normalizedPart == "back")
	{
		return 3;
	}
	if (normalizedPart == "hand")
	{
		return 4;
	}
	if (normalizedPart == "wrist")
	{
		return 5;
	}
	if (normalizedPart == "foot")
	{
		return 6;
	}
	return -1;
}

const char* NPC::getEquipmentPartName(int partIndex)
{
	switch (partIndex)
	{
	case 0:
		return "head";
	case 1:
		return "neck";
	case 2:
		return "body";
	case 3:
		return "back";
	case 4:
		return "hand";
	case 5:
		return "wrist";
	case 6:
		return "foot";
	default:
		return "";
	}
}

std::string NPC::getEquipmentFileByPartIndex(int partIndex) const
{
	switch (partIndex)
	{
	case 0:
		return headEquip;
	case 1:
		return neckEquip;
	case 2:
		return bodyEquip;
	case 3:
		return backEquip;
	case 4:
		return handEquip;
	case 5:
		return wristEquip;
	case 6:
		return footEquip;
	default:
		return "";
	}
}

bool NPC::setEquipmentFileByPartIndex(int partIndex, const std::string& fileName)
{
	switch (partIndex)
	{
	case 0:
		headEquip = fileName;
		break;
	case 1:
		neckEquip = fileName;
		break;
	case 2:
		bodyEquip = fileName;
		break;
	case 3:
		backEquip = fileName;
		break;
	case 4:
		handEquip = fileName;
		break;
	case 5:
		wristEquip = fileName;
		break;
	case 6:
		footEquip = fileName;
		break;
	default:
		return false;
	}
	updateEquipmentAttributes(true);
	return true;
}

void NPC::adjustCurrentAttributesAfterMaximumChange(
	int previousLifeMax,
	int previousThewMax,
	int previousManaMax)
{
	auto adjustCurrentValue = [](int currentValue, int previousMaximum, int currentMaximum)
	{
		const int64_t adjustedValue = static_cast<int64_t>(currentValue)
			+ static_cast<int64_t>(currentMaximum)
			- static_cast<int64_t>(previousMaximum);
		return static_cast<int>(std::clamp<int64_t>(
			adjustedValue,
			0,
			std::max<int64_t>(0, currentMaximum)));
	};

	life = adjustCurrentValue(life, previousLifeMax, getLifeMax());
	thew = adjustCurrentValue(thew, previousThewMax, getThewMax());
	mana = adjustCurrentValue(mana, previousManaMax, getManaMax());
}

void NPC::resetEquipmentMagicEffectBonuses()
{
	equipmentAddMagicEffectPercent = 0;
	equipmentAddMagicEffectAmount = 0;
	equipmentAddMagicEffectByName.clear();
	equipmentAddMagicEffectByType.clear();
	equipmentChangeMoveSpeedPercent = 0;
	equipmentExtraLifeRestorePercent = 0.0f;
	equipmentFlyIniReplacements.clear();
	equipmentFlyIni2Replacements.clear();
	equipmentMagicToUseWhenAttacked.clear();
}

void NPC::addEquipmentMagicEffectBonus(const Goods& goods, int count)
{
	if (goods.addMagicEffectName != "")
	{
		equipmentAddMagicEffectByName[goods.addMagicEffectName].add(
			goods.addMagicEffectPercent,
			goods.addMagicEffectAmount,
			count);
	}
	else if (goods.addMagicEffectType != "")
	{
		equipmentAddMagicEffectByType[goods.addMagicEffectType].add(
			goods.addMagicEffectPercent,
			goods.addMagicEffectAmount,
			count);
	}
	else
	{
		equipmentAddMagicEffectPercent = addRepeatedSaturated(
			equipmentAddMagicEffectPercent,
			goods.addMagicEffectPercent,
			count);
		equipmentAddMagicEffectAmount = addRepeatedSaturated(
			equipmentAddMagicEffectAmount,
			goods.addMagicEffectAmount,
			count);
	}
}

void NPC::addEquipmentRuntimeEffect(
	const Goods& goods,
	const std::string& sourceFile,
	int count)
{
	if (count <= 0)
	{
		return;
	}
	addEquipmentMagicEffectBonus(goods, count);
	equipmentChangeMoveSpeedPercent = addRepeatedSaturated(
		equipmentChangeMoveSpeedPercent,
		goods.changeMoveSpeedPercent,
		count);
	if (goods.specialEffect == 1)
	{
		equipmentExtraLifeRestorePercent = static_cast<float>(goods.specialEffectValue) / 100.0f;
	}
	if (!goods.flyIni.empty())
	{
		equipmentFlyIniReplacements.push_back(goods.flyIni);
	}
	if (!goods.flyIni2.empty())
	{
		equipmentFlyIni2Replacements.push_back(goods.flyIni2);
	}
	if (gm != nullptr && !goods.magicToUseWhenBeAttacked.empty())
	{
		auto magic = gm->magicManager.loadAttackMagic(goods.magicToUseWhenBeAttacked);
		if (magic != nullptr && magic->loadSucceeded)
		{
			NPCMagicToUseWhenAttacked item;
			item.sourceFile = sourceFile;
			item.magic = magic;
			item.direction = goods.magicDirectionWhenBeAttacked;
			equipmentMagicToUseWhenAttacked.push_back(item);
		}
	}
}

void NPC::addMagicPassiveRuntimeEffect(const Magic& magic, const std::string& sourceFile)
{
	if (!magic.flyIni.empty())
	{
		equipmentFlyIniReplacements.push_back(magic.flyIni);
	}
	if (!magic.flyIni2.empty())
	{
		equipmentFlyIni2Replacements.push_back(magic.flyIni2);
	}
	if (gm != nullptr && !magic.magicToUseWhenBeAttackedFile.empty())
	{
		auto triggerMagic = gm->magicManager.loadAttackMagic(magic.magicToUseWhenBeAttackedFile);
		if (triggerMagic != nullptr && triggerMagic->loadSucceeded)
		{
			NPCMagicToUseWhenAttacked item;
			item.sourceFile = sourceFile;
			item.magic = triggerMagic;
			item.direction = magic.magicDirectionWhenBeAttacked;
			equipmentMagicToUseWhenAttacked.push_back(item);
		}
	}
}

int NPC::applyTemporaryAttackModifiers(int value) const
{
	if (weakMagic != nullptr)
	{
		value = value * (100 - weakMagic->weakAttackPercent) / 100;
	}
	if (morphMagic != nullptr)
	{
		value = value * (100 + morphMagic->attackAddPercent) / 100;
	}
	return value;
}

int NPC::applyTemporaryDefendModifiers(int value) const
{
	if (weakMagic != nullptr)
	{
		value = value * (100 - weakMagic->weakDefendPercent) / 100;
	}
	if (morphMagic != nullptr)
	{
		value = value * (100 + morphMagic->defendAddPercent) / 100;
	}
	return value;
}

int NPC::applyTemporaryEvadeModifiers(int value) const
{
	if (morphMagic != nullptr)
	{
		value = value * (100 + morphMagic->evadeAddPercent) / 100;
	}
	return value;
}

int NPC::getTemporarySpeedAddPercent() const
{
	return morphMagic != nullptr ? morphMagic->speedAddPercent : 0;
}

float NPC::getMoveSpeedFold() const
{
	int64_t percent = static_cast<int64_t>(equipmentChangeMoveSpeedPercent)
		+ addMoveSpeedPercent
		+ getRangeSpeedUpPercent()
		+ getTemporarySpeedAddPercent();
	if (percent < -90)
	{
		percent = -90;
	}
	return 1.0f + static_cast<float>(percent) / 100.0f;
}

float NPC::getAdjustedWalkSpeed() const
{
	return std::max(0.1f, static_cast<float>(walkSpeed) * getMoveSpeedFold());
}

float NPC::getAdjustedRunSpeed() const
{
	return std::max(0.1f, static_cast<float>(runSpeed) * getMoveSpeedFold());
}

bool NPC::hasActiveRangeSpeedUp() const
{
	return getRangeSpeedUpPercent() > 0;
}

void NPC::applyRangeSpeedUp(std::shared_ptr<Effect> effect)
{
	if (effect == nullptr || !effect->isRangeSpeedUpActive())
	{
		return;
	}
	if (hasActiveRangeSpeedUp())
	{
		return;
	}
	rangeSpeedUpEffect = effect;
}

void NPC::clearRangeSpeedUp()
{
	rangeSpeedUpEffect.reset();
}

int NPC::getRangeSpeedUpPercent() const
{
	auto effect = rangeSpeedUpEffect.lock();
	if (effect == nullptr || !effect->isRangeSpeedUpActive())
	{
		return 0;
	}
	return effect->magic.rangeSpeedUp;
}

void NPC::updateEquipmentLifeRestore(UTime frameTime)
{
	if (equipmentExtraLifeRestorePercent <= 0.0f || frameTime == 0)
	{
		equipmentLifeRestoreElapsedMilliseconds = 0;
		return;
	}
	if ((!actionManager->isStanding() && !actionManager->isWalking()) || frozen || poisoned || petrified || immobilized)
	{
		equipmentLifeRestoreElapsedMilliseconds = 0;
		return;
	}
	equipmentLifeRestoreElapsedMilliseconds += frameTime;
	while (equipmentLifeRestoreElapsedMilliseconds >= 1000)
	{
		equipmentLifeRestoreElapsedMilliseconds -= 1000;
		int maxLife = getLifeMax();
		if (maxLife <= 0)
		{
			continue;
		}
		int addAmount = static_cast<int>(equipmentExtraLifeRestorePercent * static_cast<float>(maxLife));
		if (addAmount > 0)
		{
			life = std::min(life + addAmount, maxLife);
		}
	}
}

int NPC::applyMagicEffectBonus(const Magic& magic, int effect) const
{
	int64_t percent = equipmentAddMagicEffectPercent;
	int64_t amount = equipmentAddMagicEffectAmount;
	auto nameIter = equipmentAddMagicEffectByName.find(magic.name);
	if (nameIter != equipmentAddMagicEffectByName.end())
	{
		percent += nameIter->second.percent;
		amount += nameIter->second.amount;
	}
	else if (!magic.type.empty())
	{
		auto typeIter = equipmentAddMagicEffectByType.find(magic.type);
		if (typeIter != equipmentAddMagicEffectByType.end())
		{
			percent += typeIter->second.percent;
			amount += typeIter->second.amount;
		}
	}
	if (percent > 0)
	{
		effect = static_cast<int>(std::clamp<int64_t>(
			static_cast<int64_t>(effect)
				+ static_cast<int64_t>(effect) * percent / 100,
			INT_MIN,
			INT_MAX));
	}
	return static_cast<int>(std::clamp<int64_t>(
		static_cast<int64_t>(effect) + amount,
		INT_MIN,
		INT_MAX));
}

void NPC::updateEquipmentAttributes(bool adjustCurrentValues)
{
	const int previousLifeMax = getLifeMax();
	const int previousThewMax = getThewMax();
	const int previousManaMax = getManaMax();
	equipmentAttributes.reset();
	resetEquipmentMagicEffectBonuses();
	attackAdditionalEffect = maeNone;
	if (canEquip <= 0)
	{
		return;
	}

	for (int i = 0; i < GOODS_BODY_COUNT; i++)
	{
		std::string fileName = getEquipmentFileByPartIndex(i);
		if (fileName.empty())
		{
			continue;
		}

		Goods goods;
		goods.initFromIni(fileName);
		if (goods.kind != gkEquipment)
		{
			continue;
		}

		equipmentAttributes.lifeMax += goods.lifeMax;
		equipmentAttributes.thewMax += goods.thewMax;
		equipmentAttributes.manaMax += goods.manaMax;
		equipmentAttributes.attack += goods.attack;
		equipmentAttributes.attack2 += goods.attack2;
		equipmentAttributes.attack3 += goods.attack3;
		equipmentAttributes.defend += goods.defend;
		equipmentAttributes.defend2 += goods.defend2;
		equipmentAttributes.defend3 += goods.defend3;
		equipmentAttributes.evade += goods.evade;
		addEquipmentRuntimeEffect(goods, fileName);
		if (toLowerAsciiCopy(goods.part) == "hand")
		{
			if (goods.effectType == 1)
			{
				attackAdditionalEffect = maeFrozen;
			}
			else if (goods.effectType == 2)
			{
				attackAdditionalEffect = maePoison;
			}
			else if (goods.effectType == 3)
			{
				attackAdditionalEffect = maePetrified;
			}
		}
	}

	rebuildAttackOptions();

	if (adjustCurrentValues)
	{
		adjustCurrentAttributesAfterMaximumChange(
			previousLifeMax,
			previousThewMax,
			previousManaMax);
	}
	else if (life > getLifeMax())
	{
		life = getLifeMax();
	}
	if (!adjustCurrentValues && thew > getThewMax())
	{
		thew = getThewMax();
	}
	if (!adjustCurrentValues && mana > getManaMax())
	{
		mana = getManaMax();
	}
}

_shared_image NPC::getActionImage(int * offsetx, int * offsety)
{
	if (scriptSpecialActionOverlayActive && !scriptSpecialActionOverlaySupersededByAction)
	{
		return IMP::loadImageForDirection(scriptSpecialActionOverlayResource.imagePackage,
			direction,
			scriptSpecialActionOverlayElapsed,
			offsetx,
			offsety,
			true);
	}
	return actionManager->getActionImage(offsetx, offsety);
}

_shared_image NPC::getActionShadow(int * offsetx, int * offsety)
{
	if (scriptSpecialActionOverlayActive && !scriptSpecialActionOverlaySupersededByAction)
	{
		return IMP::loadImageForDirection(scriptSpecialActionOverlayResource.shadowPackage,
			direction,
			scriptSpecialActionOverlayElapsed,
			offsetx,
			offsety,
			true);
	}
	return actionManager->getActionShadow(offsetx, offsety);
}

void NPC::calOffset(UTime nowTime, UTime totalTime)
{
	float percent = ((float)nowTime) / ((float)totalTime);
	if (percent < 0)
	{
		percent = 0;
	}
	else if (percent > 1)
	{
		percent = 1;
	}
	if (stepState == ssIn)
	{
		percent = 1 - percent;
		switch (direction)
		{
		case 0:
			offset.x = 0;
			offset.y = -percent * (TILE_HEIGHT / 2);
			break;
		case 1:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		case 2:
			offset.x = percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 3:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		case 4:
			offset.x = 0;
			offset.y = percent * (TILE_HEIGHT / 2);
			break;
		case 5:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		case 6:
			offset.x = -percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 7:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		}
	}
	else if (stepState == ssOut)
	{
		switch (direction)
		{
		case 0:
			offset.x = 0;
			offset.y = percent * (TILE_HEIGHT / 2);
			break;
		case 1:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		case 2:
			offset.x = -percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 3:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		case 4:
			offset.x = 0;
			offset.y = -percent * (TILE_HEIGHT / 2);
			break;
		case 5:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		case 6:
			offset.x = percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 7:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		}
	}
	else
	{
		offset.x = 0;
		offset.y = 0;
	}
}

int NPC::getInvertDirection(int dir)
{
	int result = dir + 4;
	if (result > 7)
	{
		result -= 8;
	}
	return result;
}

int NPC::getDirection(Point dest)
{
	return getDirection(position, dest);
}

int NPC::getDirection(float angle)
{
	if (angle < 0)
	{
		angle += 2 * M_PI;
	}
	angle += M_PI / 8;
	if (angle > 2 * M_PI)
	{
		angle -= 2 * M_PI;
	}
	return (int)(angle / (M_PI / 4));
}

int NPC::getDirection(Point from, Point to)
{
	Point pos = Map::getTilePosition(to, from, { 0, 0 }, { 0, 0 });
	PointEx dir;
	dir.x = ((float)pos.x) / TILE_HEIGHT / MapXRatio;
	dir.y = ((float)pos.y) / TILE_HEIGHT;
	dir.x = -dir.x;
	float angle = atan2(dir.x, dir.y);
	return getDirection(angle);
}

std::string NPC::getDisplayName() const
{
	return showName.empty() ? npcName : showName;
}

// ===================== 资源加载/保存 =====================

void NPC::saveToIni(INIReader * ini, const std::string & section)
{
	if (ini == nullptr)
	{
		return;
	}

	ini->Set(section, "Name", npcName);
	ini->Set(section, "ShowName", showName);
	ini->SetInteger(section, "Kind", kind);
	ini->Set(section, "NPCIni", npcIni);
	ini->SetInteger(section, "Sex", sex);
	ini->SetInteger(section, "Dir", direction);
	ini->SetInteger(section, "MapX", position.x);
	ini->SetInteger(section, "MapY", position.y);
	ini->SetInteger(section, "Action", strollIntent);
	ini->SetInteger(section, "WalkSpeed", walkSpeed);
	ini->SetInteger(section, "StandSpeed", standSpeed);
	if (hasAttackSpeedField)
	{
		ini->SetInteger(section, "AttackSpeed", attackSpeed);
	}
	else
	{
		ini->Remove(section, "AttackSpeed");
		ini->Remove(section, "attackspeed");
	}
	ini->SetInteger(section, "Idle", idle);
	ini->SetInteger(section, "AI_TYPE", aiType);
	ini->SetInteger(section, "Group", group);
	ini->SetInteger(section, "NoAutoAttackPlayer", noAutoAttackPlayer);
	ini->SetInteger(section, "StopFindingTarget", stopFindingTarget);
	ini->SetInteger(section, "PathFinder", pathFinder);
	ini->SetBoolean(section, "IsAIDisabled", isAIDisabled);
	ini->SetInteger(section, "DialogRadius", dialogRadius);
	ini->Set(section, "ScriptFile", scriptFile);
	ini->Set(section, "ScriptFileRight", scriptFileRight);
	ini->Set(section, "TimerScriptFile", timerScriptFile);
	ini->SetTime(section, "TimerScriptInterval", timerScriptInterval);
	ini->SetInteger(section, "CanInteractDirectly", canInteractDirectly);
	ini->Set(section, "BuyIniFile", buyIniFile);
	ini->Set(section, "BuyIniString", buyIniString);
	ini->Set(section, "DropIni", dropIni);
	ini->SetInteger(section, "NoDropWhenDie", noDropWhenDie);
	ini->SetInteger(section, "KeepAttackX", keepAttackPosition.x);
	ini->SetInteger(section, "KeepAttackY", keepAttackPosition.y);
	ini->SetTime(section, "ReviveMilliseconds", reviveMilliseconds);
	ini->SetTime(section, "LeftMillisecondsToRevive", leftMillisecondsToRevive);
	ini->SetTime(section, "LifeMilliseconds", lifeMilliseconds);
	ini->SetInteger(section, "IsBodyIniAdded", isBodyIniAdded);
	ini->SetInteger(section, "IsNodAddBody", noAddBody ? 1 : 0);
	bool deathInvoked = isDying()
		|| (result & erLifeExhaust)
		|| (isHiding() && (life <= 0 || leftMillisecondsToRevive > 0));
	bool deathCompleted = deathInvoked && !isDying();
	UTime deathActionRemainingMilliseconds = 0;
	if (isDying())
	{
		UTime elapsedMilliseconds = getTime() >= actionBeginTime ? getTime() - actionBeginTime : 0;
		deathActionRemainingMilliseconds = actionLastTime > elapsedMilliseconds
			? actionLastTime - elapsedMilliseconds
			: 0;
	}
	ini->SetInteger(section, "DeathPersistenceVersion", 1);
	ini->SetBoolean(section, "IsDeathInvoked", deathInvoked);
	ini->SetBoolean(section, "IsDeath", deathCompleted);
	ini->SetTime(section, "DeathActionRemainingMilliseconds", deathActionRemainingMilliseconds);
	ini->SetBoolean(section, "PendingDeathScript", (result & erRunDeathScript) != 0);
	ini->SetBoolean(section, "UseSpecialDeath", useSpecialDeath);
	ini->Set(section, "SpecialDeathAction", specialDeathAction);
	ini->Set(section, "VisibleVariableName", visibleVariableName);
	ini->SetInteger(section, "VisibleVariableValue", visibleVariableValue);
	ini->SetInteger(section, "State", state);
	ini->SetInteger(section, "Relation", changeToOppositeMilliseconds > 0 ? originalRelationBeforeOppositeChange : relation);
	ini->SetInteger(section, "KindValue", kindValue);
	ini->SetInteger(section, "KindValueMax", kindValueMax);
	ini->Set(section, "TalkContent", talkContent);
	ini->Set(section, "BagGoods", bagGoods);
	ini->SetInteger(section, "Steal", steal);
	ini->SetInteger(section, "Eloquence", eloquence);
	ini->SetInteger(section, "Leechcraft", leechcraft);
	if (hasAutoRunScriptField || autoRunScript != 0)
	{
		ini->SetInteger(section, "AutoRunScript", autoRunScript);
	}
	else
	{
		ini->Remove(section, "AutoRunScript");
	}
	if (hasArmField || arm != 0)
	{
		ini->SetInteger(section, "Arm", arm);
	}
	else
	{
		ini->Remove(section, "Arm");
	}
	if (hasEvadeNField || evadeN != 0)
	{
		ini->SetInteger(section, "EvadeN", evadeN);
	}
	else
	{
		ini->Remove(section, "EvadeN");
	}
	if (hasGenguField || gengu != 0)
	{
		ini->SetInteger(section, "Gengu", gengu);
	}
	else
	{
		ini->Remove(section, "Gengu");
	}
	if (hasNeixiField || neixi != 0)
	{
		ini->SetInteger(section, "Neixi", neixi);
	}
	else
	{
		ini->Remove(section, "Neixi");
	}
	if (hasPhysiqueField || physique != 0)
	{
		ini->SetInteger(section, "Physique", physique);
	}
	else
	{
		ini->Remove(section, "Physique");
	}
	ini->SetInteger(section, "IsSignalShow", isSignalShow ? 1 : 0);
	ini->SetInteger(section, "SignalIndex", signalIndex);
	ini->Set(section, "SignalType", signalType);
	ini->SetInteger(section, "Life", life);
	ini->SetInteger(section, "LifeMax", lifeMax);
	ini->SetInteger(section, "Thew", thew);
	ini->SetInteger(section, "ThewMax", thewMax);
	ini->SetInteger(section, "Mana", mana);
	ini->SetInteger(section, "ManaMax", manaMax);
	ini->SetInteger(section, "Attack", attack);
	ini->SetInteger(section, "Attack2", attack2);
	ini->SetInteger(section, "Attack3", attack3);
	ini->SetInteger(section, "Defend", defend);
	ini->SetInteger(section, "Defend2", defend2);
	ini->SetInteger(section, "Defend3", defend3);
	ini->SetInteger(section, "Evade", evade);
	ini->SetInteger(section, "Duck", duck);
	if (hasDodgeBeginFrameField || dodgeBeginFrame != 0)
	{
		ini->SetInteger(section, "Dodge_BeginFrame", dodgeBeginFrame);
	}
	else
	{
		ini->Remove(section, "Dodge_BeginFrame");
	}
	if (hasDodgeEndFrameField || dodgeEndFrame != 0)
	{
		ini->SetInteger(section, "Dodge_EndFrame", dodgeEndFrame);
	}
	else
	{
		ini->Remove(section, "Dodge_EndFrame");
	}
	ini->SetInteger(section, "Exp", exp);
	const bool savesExperienceBonus =
		ResourceManager::instance().getActiveManifest().
			resolvedDefeatedNpcExperienceMode() ==
		DefeatedNpcExperienceMode::LevelProductWithBonus;
	if (savesExperienceBonus && hasExpBonusField)
	{
		ini->SetInteger(section, "ExpBonus", expBonus);
	}
	else
	{
		ini->Remove(section, "ExpBonus");
	}

	ini->SetInteger(section, "LevelUpExp", levelUpExp);
	ini->SetInteger(section, "CanLevelUp", canLevelUp);
	ini->SetInteger(section, "Level", level);
	ini->Set(section, "LevelIni", npcLevelIni);
	std::string poisonSourceName = poisoned ? poisonedByCharacterName : "";
	if (poisoned && poisonSourceName.empty())
	{
		poisonSourceName = getCharacterNameForStateSource(poisonedBy.lock());
	}
	ini->SetReal(section, "PoisonSeconds", millisecondsToSeconds(poisoned ? poisonedLastTime : 0));
	ini->Set(section, "PoisonByCharacterName", poisonSourceName);
	ini->SetReal(section, "PetrifiedSeconds", millisecondsToSeconds(petrified ? petrifiedLastTime : 0));
	ini->SetReal(section, "FrozenSeconds", millisecondsToSeconds(frozen ? frozenLastTime : 0));
	ini->SetReal(section, "ImmobilizedSeconds", millisecondsToSeconds(immobilized ? immobilizedLastTime : 0));
	ini->SetBoolean(section, "IsPoisionVisualEffect", poisonedVisualEffect);
	ini->SetBoolean(section, "IsPetrifiedVisualEffect", petrifiedVisualEffect);
	ini->SetBoolean(section, "IsFronzenVisualEffect", frozenVisualEffect);
	ini->SetBoolean(section, "IsImmobilizedVisualEffect", immobilizedVisualEffect);
	ini->SetInteger(section, "Invincible", invincible);
	ini->Set(section, "FixedPos", fixedPos);
	ini->SetInteger(section, "CurrentFixedPosIndex", (int)currentFixedPosIndex);
	ini->SetInteger(section, "DestinationMapPosX", destinationMapPosition.x);
	ini->SetInteger(section, "DestinationMapPosY", destinationMapPosition.y);
	ini->SetInteger(section, "AttackLevel", attackLevel);
	ini->SetInteger(section, "MagicLevel", magicLevel);
	ini->SetInteger(section, "CanEquip", canEquip);
	ini->Set(section, "HeadEquip", headEquip);
	ini->Set(section, "NeckEquip", neckEquip);
	ini->Set(section, "BodyEquip", bodyEquip);
	ini->Set(section, "BackEquip", backEquip);
	ini->Set(section, "HandEquip", handEquip);
	ini->Set(section, "WristEquip", wristEquip);
	ini->Set(section, "FootEquip", footEquip);
	ini->Set(section, "BackgroundTextureEquip", backgroundTextureEquip);
	ini->SetInteger(section, "Lum", lum);
	ini->SetInteger(section, "VisionRadius", visionRadius);
	ini->SetInteger(section, "AttackRadius", attackRadius);
	ini->Set(section, "BodyIni", bodyIni);
	ini->Set(section, "FlyIni", flyIni);
	ini->Set(section, "FlyIni2", flyIni2);
	ini->Set(section, "FlyInis", flyInis);
	ini->Set(section, "MagicIni", magicIni);
	ini->Set(section, "MagicToUseWhenLifeLow", magicToUseWhenLifeLowFile);
	ini->SetInteger(section, "LifeLowPercent", lifeLowPercent);
	ini->SetInteger(section, "KeepRadiusWhenLifeLow", keepRadiusWhenLifeLow);
	ini->SetInteger(section, "KeepRadiusWhenFriendDeath", keepRadiusWhenFriendDeath);
	ini->Set(section, "MagicToUseWhenBeAttacked", magicToUseWhenBeAttackedFile);
	ini->SetInteger(section, "MagicDirectionWhenBeAttacked", magicDirectionWhenBeAttacked);
	ini->Set(section, "MagicToUseWhenDeath", magicToUseWhenDeathFile);
	ini->SetInteger(section, "MagicDirectionWhenDeath", magicDirectionWhenDeath);
	ini->SetInteger(section, "AddMoveSpeedPercent", addMoveSpeedPercent);
	ini->SetTime(section, "HurtPlayerInterval", hurtPlayerInterval);
	ini->SetInteger(section, "HurtPlayerLife", hurtPlayerLife);
	ini->SetInteger(section, "HurtPlayerRadius", hurtPlayerRadius);
	ini->Set(section, "DeathScript", deathScript);
}

void NPC::loadActionRes(NPCActionRes * npcAction)
{
	if (npcAction == nullptr)
	{
		return;
	}
	if (gm == nullptr || gm->npcManager == nullptr)
	{
		npcAction->imagePackage = nullptr;
		npcAction->shadowPackage = nullptr;
		return;
	}
	npcAction->imagePackage = gm->npcManager->loadActionImage(npcAction->imageFile);
	npcAction->shadowPackage = gm->npcManager->loadActionImage(npcAction->shadowFile);
}

void NPC::initActionFromIni(NPCActionRes * npcAction, INIReader * iniReader, const std::string & section)
{
	if (npcAction == nullptr || iniReader == nullptr)
	{
		return;
	}
	npcAction->imageFile = iniReader->Get(section, "Image", "");
	npcAction->shadowFile = iniReader->Get(section, "Shade", "");
	npcAction->soundFile = iniReader->Get(section, "Sound", "");
	loadActionRes(npcAction);
}

void NPC::loadSpecialAction(const std::string & fileName)
{
	res.special.imageFile = fileName;
	res.special.shadowFile = makeShadowFileName(fileName);
	res.special.soundFile = "";
	if (gm && gm->npcManager)
	{
		if (hasDirectoryPart(fileName))
		{
			res.special.imagePackage = gm->npcManager->loadActionImageDirect(res.special.imageFile);
			res.special.shadowPackage = gm->npcManager->loadActionImageDirect(res.special.shadowFile);
		}
		else
		{
			res.special.imagePackage = gm->npcManager->loadActionImage(res.special.imageFile);
			res.special.shadowPackage = gm->npcManager->loadActionImage(res.special.shadowFile);
		}
	}
}

void NPC::initRes(const std::string & fileName)
{
	loadNpcResFromIni(fileName, res);
}

bool NPC::loadNpcResFromIni(const std::string& fileName, NPCRes& targetRes)
{
	freeNPCRes(targetRes);
	if (fileName.empty())
	{
		return false;
	}
	std::unique_ptr<INIReader> ini;
	auto readNpcResIni = [&](const std::string& iniName, bool requireResourceShape)
	{
		std::unique_ptr<char[]> data;
		int len = File::readFile(iniName, data);
		if (len <= 0 || data == nullptr)
		{
			return false;
		}
		auto candidate = std::make_unique<INIReader>(data);
		if (requireResourceShape && !isNpcResourceIni(*candidate))
		{
			return false;
		}
		ini = std::move(candidate);
		return true;
	};
	if (!readNpcResIni(NPC_RES_INI_FOLDER + fileName, false))
	{
		if (hasDirectoryPart(fileName) || !readNpcResIni(NPC_INI_FOLDER + fileName, true))
		{
			return false;
		}
	}
	if (ini == nullptr)
	{
		return false;
	}

#define initNPCAction(act) initActionFromIni(&targetRes.act, ini.get(), #act)

	auto initActionIfPresent = [&](NPCActionRes* actionRes, const std::string& section)
	{
		if (hasIniKey(ini.get(), section, "Image")
			|| hasIniKey(ini.get(), section, "Shade")
			|| hasIniKey(ini.get(), section, "Sound"))
		{
			initActionFromIni(actionRes, ini.get(), section);
		}
	};

	initNPCAction(stand);
	initNPCAction(stand1);
	initNPCAction(walk);
	initNPCAction(run);
	initNPCAction(jump);
	initNPCAction(attack);
	initNPCAction(attack1);
	initNPCAction(attack2);
	initNPCAction(magic);
	initNPCAction(hurt);
	initNPCAction(death);
	initNPCAction(sit);

	initNPCAction(astand);
	initNPCAction(awalk);
	initNPCAction(arun);
	initNPCAction(ajump);
	initActionIfPresent(&targetRes.astand, "FightStand");
	initActionIfPresent(&targetRes.awalk, "FightWalk");
	initActionIfPresent(&targetRes.arun, "FightRun");
	initActionIfPresent(&targetRes.ajump, "FightJump");

#undef initNPCAction
	return true;
}

void NPC::loadActionFile(const std::string & fileName, int act)
{
	NPCActionRes* actionRes = nullptr;
	switch (static_cast<NPCActionType>(act))
	{
	case NPCActionType::acStand: actionRes = &res.stand; break;
	case NPCActionType::acStand1: actionRes = &res.stand1; break;
	case NPCActionType::acWalk: actionRes = &res.walk; break;
	case NPCActionType::acRun: actionRes = &res.run; break;
	case NPCActionType::acJump: actionRes = &res.jump; break;
	case NPCActionType::acAttack: actionRes = &res.attack; break;
	case NPCActionType::acAttack1: actionRes = &res.attack1; break;
	case NPCActionType::acAttack2: actionRes = &res.attack2; break;
	case NPCActionType::acMagic: actionRes = &res.magic; break;
	case NPCActionType::acHurt: actionRes = &res.hurt; break;
	case NPCActionType::acSit:
	case NPCActionType::acSitting: actionRes = &res.sit; break;
	case NPCActionType::acDeath: actionRes = &res.death; break;
	case NPCActionType::acSpecial: actionRes = &res.special; break;
	case NPCActionType::acSpecialAttack: actionRes = &res.specialAttack; break;
	case NPCActionType::acAStand: actionRes = &res.astand; break;
	case NPCActionType::acAWalk: actionRes = &res.awalk; break;
	case NPCActionType::acARun: actionRes = &res.arun; break;
	case NPCActionType::acAJump: actionRes = &res.ajump; break;
	default: break;
	}
	if (actionRes != nullptr)
	{
		actionRes->imageFile = fileName;
		actionRes->shadowFile = convert::extractFileName(fileName) + ".shd";
		actionRes->soundFile = "";
		loadActionRes(actionRes);
		if (nowAction == NPCActionType::acStand || nowAction == NPCActionType::acStand1)
		{
			beginStand();
		}
	}
}

// ===================== 属性操作 =====================

void NPC::addLife(int value)
{
	if (value < 0 && invincible > 0)
	{
		return;
	}
	life += value;
}

void NPC::addLifeWithoutDeath(int value)
{
	life = std::max(0, life + value);
}

void NPC::addThew(int value)
{
	thew += value;
}

void NPC::addMana(int value)
{
	mana += value;
}

void NPC::hurtLife(int damage)
{
	if (hasActiveSelfMagic(mskBlockDamage) || invincible > 0)
	{
		return;
	}

	damage -= defend;
	for (auto it = shieldEffects.begin(); it != shieldEffects.end(); )
	{
		if (auto shield = it->lock())
		{
			if (shield->vanishing || (shield->doing != ekFlying && shield->doing != ekExploding && shield->doing != ekHiding))
			{
				it = shieldEffects.erase(it);
				continue;
			}
			int shieldLevel = shield->level;
			if (shieldLevel < 1) shieldLevel = 1;
			if (shieldLevel > MAGIC_MAX_LEVEL) shieldLevel = MAGIC_MAX_LEVEL;
			damage -= shield->magic.level[shieldLevel].effect;
		}
		else
		{
			it = shieldEffects.erase(it);
			continue;
		}
		++it;
	}
	if (damage < 0)
	{
		damage = 0;
	}
	if (damage > 0 && shieldLife > 0)
	{
		if (damage > shieldLife)
		{
			damage -= shieldLife;
			shieldLife = 0;
			if (auto shield = shieldEffect.lock())
			{
				shield->vanishing = true;
				shield->beginExplode(shield->position);
			}
			shieldEffect.reset();
		}
		else
		{
			shieldLife -= damage;
			return;
		}
	}

	if (damage <= 0)
	{
		return;
	}

	addLife(-damage);
	if (life <= 0)
	{
		life = 0;
		handleDeath();
		return;
	}

	beginHurt();
}

int NPC::calculateEffectDamage(std::shared_ptr<Effect> effect)
{
	if (effect == nullptr)
	{
		return 0;
	}

	int damage = effect->damage - getDefend();
	int damage2 = effect->damage2 - getDefend2();
	int damage3 = effect->damage3 - getDefend3();

	for (auto it = shieldEffects.begin(); it != shieldEffects.end(); )
	{
		if (auto shield = it->lock())
		{
			if (shield->vanishing || (shield->doing != ekFlying && shield->doing != ekExploding && shield->doing != ekHiding))
			{
				it = shieldEffects.erase(it);
				continue;
			}
			int shieldLevel = shield->level;
			if (shieldLevel < 1) shieldLevel = 1;
			if (shieldLevel > MAGIC_MAX_LEVEL) shieldLevel = MAGIC_MAX_LEVEL;
			damage -= shield->magic.level[shieldLevel].effect;
			damage2 -= shield->magic.level[shieldLevel].effect2;
			damage3 -= shield->magic.level[shieldLevel].effect3;
		}
		else
		{
			it = shieldEffects.erase(it);
			continue;
		}
		++it;
	}

	if (damage3 > 0)
	{
		damage += damage3;
	}
	if (damage2 > 0)
	{
		damage += damage2;
	}

	const int minimumMagicDamage = gm != nullptr
		? gm->global.minimumMagicDamage
		: ResourceManifest().resolvedMinimumMagicDamage();
	return std::max(damage, minimumMagicDamage);
}

int NPC::applyCriticalDamageFromEffect(const std::shared_ptr<Effect>& effect, int damage, bool* wasCritical)
{
	if (wasCritical != nullptr)
	{
		*wasCritical = false;
	}
	if (effect == nullptr || gm == nullptr || !gm->global.feature.rageSystem || engine == nullptr)
	{
		return damage;
	}
	if (auto playerCaster = std::dynamic_pointer_cast<Player>(effect->user.lock()))
	{
		return playerCaster->applyCriticalDamage(damage, engine->getRand(100), wasCritical);
	}
	return damage;
}

void NPC::applyEffectManaDamage(std::shared_ptr<Effect> effect)
{
	if (effect == nullptr || effect->damageMana <= 0)
	{
		return;
	}
	mana -= effect->damageMana;
	if (mana < 0)
	{
		mana = 0;
	}
}

void NPC::applyEffectRestore(std::shared_ptr<Effect> effect, int damageAmount)
{
	if (effect == nullptr || damageAmount <= 0 || effect->magic.restoreProbability <= 0 || effect->magic.restorePercent == 0)
	{
		return;
	}
	auto caster = std::dynamic_pointer_cast<NPC>(effect->user.lock());
	if (caster == nullptr || !NPCManager::isManagedEffectCaster(caster))
	{
		return;
	}
	if (engine->getRand(99) >= effect->magic.restoreProbability)
	{
		return;
	}

	int restoreAmount = damageAmount * effect->magic.restorePercent / 100;
	switch (effect->magic.restoreType)
	{
	case 0:
		caster->addLife(restoreAmount);
		if (caster->getLifeMax() > 0 && caster->life > caster->getLifeMax())
		{
			caster->life = caster->getLifeMax();
		}
		if (caster->life <= 0)
		{
			caster->life = 0;
			caster->handleDeath();
		}
		break;
	case 1:
		caster->addMana(restoreAmount);
		if (caster->getManaMax() > 0 && caster->mana > caster->getManaMax())
		{
			caster->mana = caster->getManaMax();
		}
		if (caster->mana < 0)
		{
			caster->mana = 0;
		}
		break;
	case 2:
		caster->addThew(restoreAmount);
		if (caster->getThewMax() > 0 && caster->thew > caster->getThewMax())
		{
			caster->thew = caster->getThewMax();
		}
		if (caster->thew < 0)
		{
			caster->thew = 0;
		}
		break;
	default:
		break;
	}
}

void NPC::applySideEffectDamage(int effectType, int amount)
{
	if (amount <= 0)
	{
		return;
	}

	switch (effectType)
	{
	case 0:
		if (hasActiveSelfMagic(mskBlockDamage))
		{
			return;
		}
		addLife(-amount);
		if (life <= 0)
		{
			life = 0;
			handleDeath();
		}
		break;
	case 1:
		addMana(-amount);
		if (mana < 0)
		{
			mana = 0;
		}
		break;
	case 2:
		addThew(-amount);
		if (thew < 0)
		{
			thew = 0;
		}
		break;
	default:
		break;
	}
}

// ===================== 动作状态查询 =====================

void NPC::recordMagicHitForChange(const Magic& magic, int level)
{
	const auto& linked = magic.getLinkedLevel(level);
	if (linked.hitCountToChangeMagic <= 0 || linked.changeMagic == nullptr || magic.hitCountFlyingImage == nullptr || magic.iniName.empty())
	{
		return;
	}

	int& hitCount = changeMagicHitCounts[magic.iniName];
	if (hitCount < linked.hitCountToChangeMagic)
	{
		hitCount++;
	}

	auto& visual = changeMagicHitVisuals[magic.iniName];
	if (visual.icons.empty())
	{
		visual.beginTime = getTime();
	}
	visual.flyingImage = magic.hitCountFlyingImage;
	visual.vanishImage = magic.hitCountVanishImage;
	visual.threshold = linked.hitCountToChangeMagic;
	visual.radius = linked.hitCountFlyRadius;
	visual.angleSpeed = linked.hitCountFlyAngleSpeed;
	if ((int)visual.icons.size() < linked.hitCountToChangeMagic)
	{
		ChangeMagicHitVisualIcon icon;
		if (visual.beginTime > 0)
		{
			UTime now = getTime();
			icon.elapsedMilliseconds = now >= visual.beginTime ? now - visual.beginTime : 0;
		}
		if (!visual.icons.empty())
		{
			icon.direction = visual.icons.front().direction;
		}
		visual.icons.push_back(icon);
	}
}

bool NPC::shouldUseChangeMagic(const Magic& magic, int level) const
{
	const auto& linked = magic.getLinkedLevel(level);
	if (linked.hitCountToChangeMagic <= 0 || linked.changeMagic == nullptr || magic.iniName.empty())
	{
		return false;
	}

	auto it = changeMagicHitCounts.find(magic.iniName);
	return it != changeMagicHitCounts.end() && it->second >= linked.hitCountToChangeMagic;
}

void NPC::consumeChangeMagicHitCount(const Magic& magic)
{
	if (!magic.iniName.empty())
	{
		auto visualIt = changeMagicHitVisuals.find(magic.iniName);
		if (visualIt != changeMagicHitVisuals.end())
		{
			auto visual = visualIt->second;
			UTime now = getTime();
			UTime elapsed = now >= visual.beginTime ? now - visual.beginTime : 0;
			if (visual.vanishImage != nullptr && !visual.icons.empty())
			{
				const float elapsedSeconds = (float)elapsed / 1000.0f;
				const float gap = getChangeMagicHitGap(visual.icons.size());
				for (size_t i = 0; i < visual.icons.size(); i++)
				{
					float angleDegrees = gap * (float)i + elapsedSeconds * (float)visual.angleSpeed;
					float radians = angleDegrees * (float)M_PI / 180.0f;
					visual.icons[i].elapsedMilliseconds = elapsed;
					visual.icons[i].vanishBeginTime = now;
					visual.icons[i].direction = getChangeMagicHitIconDirection(radians);
				}
				visual.beginTime = now;
				changeMagicHitVanishVisuals[magic.iniName] = visual;
			}
			changeMagicHitVisuals.erase(visualIt);
		}
		changeMagicHitCounts.erase(magic.iniName);
	}
}

void NPC::clearChangeMagicHitCounts()
{
	changeMagicHitCounts.clear();
	changeMagicHitVisuals.clear();
	changeMagicHitVanishVisuals.clear();
}

bool NPC::isSitting()
{
	if (nowAction == acSit || nowAction == acSitting)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isStanding()
{
	if (nowAction == acStand
		|| nowAction == acStand1
		|| nowAction == acAStand
		|| actionManager->getCurrentActionType() == acBounce
		|| actionManager->getCurrentActionType() == acMagicForcedMove)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isAttacking()
{
	if (nowAction == acAttack || nowAction == acAttack1 || nowAction == acAttack2 || nowAction == acSpecialAttack)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isMagicing()
{
	if (nowAction == acMagic)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isRunning()
{
	if (nowAction == acRun || nowAction == acARun)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isWalking()
{
	if (nowAction == acWalk || nowAction == acAWalk)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isJumping()
{
	if (nowAction == acJump || nowAction == acAJump)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isDying()
{
	if (nowAction == acDeath)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isHiding()
{
	if (nowAction == acHide)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isHurting()
{
	if (nowAction == acHurt)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isBouncing() const
{
	return bounceVelocity > 0.0f;
}

bool NPC::isDoingSpecialAction()
{
	if (scriptSpecialActionOverlayActive || nowAction == acSpecial)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::canDoAction(NPCActionRes * act)
{
	if (act == nullptr)
	{
		return false;
	}
	if (act->imagePackage == nullptr)
	{
		return false;
	}
	if (IMP::getIMPImageActionTime(act->imagePackage) > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::canDoAction(NPCActionType act)
{
	if (isBouncing() || isMagicForcedMoving())
	{
		if (!isActionAllowedWhileBouncing(act))
		{
			return false;
		}
	}

	if (disableMoveMilliseconds > 0)
	{
		switch (act)
		{
		case NPCActionType::acWalk:
		case NPCActionType::acRun:
		case NPCActionType::acJump:
		case NPCActionType::acAWalk:
		case NPCActionType::acARun:
		case NPCActionType::acAJump:
			return false;
		default:
			break;
		}
	}

	if (disableSkillMilliseconds > 0)
	{
		switch (act)
		{
		case NPCActionType::acAttack:
		case NPCActionType::acAttack1:
		case NPCActionType::acAttack2:
		case NPCActionType::acMagic:
		case NPCActionType::acSpecialAttack:
			return false;
		default:
			break;
		}
	}

	switch (act)
	{
	case NPCActionType::acStand:
		return canDoAction(&res.stand);
		break;
	case NPCActionType::acStand1:
		return canDoAction(&res.stand1);
		break;
	case NPCActionType::acWalk:
		return canDoAction(&res.walk);
		break;
	case NPCActionType::acRun:
		return canDoAction(&res.run);
		break;
	case NPCActionType::acJump:
		return canDoAction(&res.jump);
		break;
	case NPCActionType::acAttack:
		return canDoAction(&res.attack);
		break;
	case NPCActionType::acAttack1:
		return canDoAction(&res.attack1);
		break;
	case NPCActionType::acAttack2:
		return canDoAction(&res.attack2);
		break;
	case NPCActionType::acSpecialAttack:
		return canDoAction(&res.specialAttack);
		break;
	case NPCActionType::acMagic:
		return canDoAction(&res.magic);
		break;
	case NPCActionType::acHurt:
		return canDoAction(&res.hurt);
		break;
	case NPCActionType::acDeath:
		return canDoAction(&res.death);
		break;
	case NPCActionType::acSit:
		return canDoAction(&res.sit);
		break;
	case NPCActionType::acSitting:
		return canDoAction(&res.sit);
		break;
	case NPCActionType::acSpecial:
		return canDoAction(&res.special);
		break;
	case NPCActionType::acAStand:
		return canDoAction(&res.astand);
		break;
	case NPCActionType::acAWalk:
		return canDoAction(&res.awalk);
		break;
	case NPCActionType::acARun:
		return canDoAction(&res.arun);
		break;
	case NPCActionType::acAJump:
		return canDoAction(&res.ajump);
		break;
	case NPCActionType::acBounce:
	case NPCActionType::acMagicForcedMove:
		return true;
		break;
	default:
		break;
	}
	return false;
}

UTime NPC::getActionTime(int act)
{
	if (canDoAction(static_cast<NPCActionType>(act)))
	{
		auto scaleAttackTime = [this](UTime actionTime) -> UTime
		{
			if (attackSpeed <= 1 || actionTime == 0)
			{
				return actionTime;
			}
			UTime scaledTime = actionTime / static_cast<UTime>(attackSpeed);
			return scaledTime > 0 ? scaledTime : 1;
		};

		switch (static_cast<NPCActionType>(act))
		{
		case NPCActionType::acStand:
			return IMP::getIMPImageActionTime(res.stand.imagePackage);
			break;
		case NPCActionType::acStand1:
			return IMP::getIMPImageActionTime(res.stand1.imagePackage);
			break;
		case NPCActionType::acWalk:
			return IMP::getIMPImageActionTime(res.walk.imagePackage);
			break;
		case NPCActionType::acRun:
			return IMP::getIMPImageActionTime(res.run.imagePackage);
			break;
		case NPCActionType::acJump:
			return IMP::getIMPImageActionTime(res.jump.imagePackage);
			break;
		case NPCActionType::acAttack:
			return scaleAttackTime(IMP::getIMPImageActionTime(res.attack.imagePackage));
			break;
		case NPCActionType::acAttack1:
			return scaleAttackTime(IMP::getIMPImageActionTime(res.attack1.imagePackage));
			break;
		case NPCActionType::acAttack2:
			return scaleAttackTime(IMP::getIMPImageActionTime(res.attack2.imagePackage));
			break;
		case NPCActionType::acSpecialAttack:
			return scaleAttackTime(IMP::getIMPImageActionTime(res.specialAttack.imagePackage));
			break;
		case NPCActionType::acMagic:
			return IMP::getIMPImageActionTime(res.magic.imagePackage);
			break;
		case NPCActionType::acHurt:
			return IMP::getIMPImageActionTime(res.hurt.imagePackage);
			break;
		case NPCActionType::acDeath:
			return IMP::getIMPImageActionTime(res.death.imagePackage);
			break;
		case NPCActionType::acSit:
			return IMP::getIMPImageActionTime(res.sit.imagePackage);
			break;
		case NPCActionType::acSpecial:
			return IMP::getIMPImageActionTime(res.special.imagePackage);
			break;
		case NPCActionType::acAStand:
			return IMP::getIMPImageActionTime(res.astand.imagePackage);
			break;
		case NPCActionType::acAWalk:
			return IMP::getIMPImageActionTime(res.awalk.imagePackage);
			break;
		case NPCActionType::acARun:
			return IMP::getIMPImageActionTime(res.arun.imagePackage);
			break;
		case NPCActionType::acAJump:
			return IMP::getIMPImageActionTime(res.ajump.imagePackage);
			break;
		case NPCActionType::acBounce:
		case NPCActionType::acMagicForcedMove:
			return IMP::getIMPImageActionTime(res.stand.imagePackage);
			break;
		default:
			break;
		}
	}
	return 0;

}

bool NPC::canSee(Point dest) const
{
	if (blindMilliseconds > 0 && kind != nkPlayer)
	{
		return false;
	}

	if (gm->map->calDistance(position, dest) > visionRadius)
	{
		return false;
	}

	Point subPoint = gm->map->getSubPoint(position, getDirection(position, dest));
	if (gm->map->canSee(position, dest) || (gm->map->canSeeTile(subPoint) && gm->map->canSee(subPoint, dest)))
	{
		return true;
	}
	/*
	subPoint = gm->map->getSubPoint(position, getDirection(position, dest) + 1);
	if (gm->map->canSee(position, dest) || (gm->map->canSeeTile(subPoint) && gm->map->canSee(subPoint, dest)))
	{
		return true;
	}
	subPoint = gm->map->getSubPoint(position, getDirection(position, dest) - 1);
	if (gm->map->canSee(position, dest) || (gm->map->canSeeTile(subPoint) && gm->map->canSee(subPoint, dest)))
	{
		return true;
	}
	*/
	return false;
}

void NPC::rememberCombatTargetPosition(std::shared_ptr<GameElement> target)
{
	if (target == nullptr)
	{
		return;
	}

	fightState.set(true);
	currentCombatTarget = target;
	currentCombatTargetTime = getTime();
	if (canSee(target->position))
	{
		lastKnownCombatTarget = target;
		lastKnownCombatTargetPosition = target->position;
		lastKnownCombatTargetTime = getTime();
		hasLastKnownCombatTargetPosition = true;
	}
}

void NPC::rememberCombatTargetInvestigationPosition(std::shared_ptr<GameElement> target, Point investigationPosition)
{
	if (target == nullptr || !isCombatTargetValid(target))
	{
		return;
	}

	fightState.set(true);
	currentCombatTarget = target;
	currentCombatTargetTime = getTime();
	if (canSee(target->position))
	{
		rememberCombatTargetPosition(target);
		return;
	}
	lastKnownCombatTarget = target;
	lastKnownCombatTargetPosition = investigationPosition;
	lastKnownCombatTargetTime = getTime();
	hasLastKnownCombatTargetPosition = true;
}

void NPC::rememberDamageSource(std::shared_ptr<Effect> effect)
{
	if (effect == nullptr || isNotFightBackWhenBeHit())
	{
		return;
	}
	if (blindMilliseconds > 0 && kind != nkPlayer)
	{
		return;
	}
	if (auto userPtr = effect->user.lock(); NPCManager::isManagedEffectCaster(userPtr))
	{
		lastCombatTarget = userPtr;
		lastCombatTargetTime = getTime();
		lastCombatMagicDirection = { static_cast<float>(effect->flyingDirection.x), static_cast<float>(effect->flyingDirection.y) };
		hasLastCombatMagicDirection = effect->flyingDirection.x != 0 || effect->flyingDirection.y != 0;
		if (canSee(userPtr->position))
		{
			rememberCombatTargetPosition(userPtr);
		}
		else
		{
			rememberCombatTargetInvestigationPosition(userPtr, effect->src);
		}
	}
}

void NPC::clearCombatTargetMemory()
{
	fightState.set(false);
	lastCombatTarget.reset();
	lastCombatTargetTime = 0;
	lastCombatMagicDirection = { 0.0f, 0.0f };
	hasLastCombatMagicDirection = false;
	currentCombatTarget.reset();
	currentCombatTargetTime = 0;
	lastKnownCombatTarget.reset();
	lastKnownCombatTargetPosition = { 0, 0 };
	lastKnownCombatTargetTime = 0;
	hasLastKnownCombatTargetPosition = false;
	actionPlan.reset();
}

bool NPC::hasActiveCombatWork()
{
	bool hasActivePlanTarget = false;
	if (actionPlan.isActive())
	{
		auto planTarget = actionPlan.planTarget.lock();
		if (actionPlan.isExpired(getTime()) || !isCombatTargetValid(planTarget))
		{
			actionPlan.reset();
		}
		else
		{
			hasActivePlanTarget = canSee(planTarget->position)
				|| getCombatTargetMemoryPosition(planTarget).has_value();
			if (!hasActivePlanTarget)
			{
				actionPlan.reset();
			}
		}
	}

	return hasActivePlanTarget
		|| getCombatTargetMemoryPosition(currentCombatTarget.lock()).has_value()
		|| getCombatTargetMemoryPosition(lastCombatTarget.lock()).has_value();
}

bool NPC::abandonPartnerCombatForPlayerFollow()
{
	if (!isAIEnabled() || gm == nullptr || gm->map == nullptr || gm->player == nullptr
		|| kind != nkPartner || !gm->global.data.PartnerCombat || isPartnerBlockingPlayer)
	{
		partnerOwnerFollowPriorityActive = false;
		partnerOwnerFollowBestDistance = INT_MAX;
		partnerOwnerFollowLastProgressTime = 0;
		partnerOwnerFollowRetryTime = 0;
		return false;
	}

	const UTime currentTime = getTime();
	const int playerDistance = gm->map->calDistance(position, gm->player->getPosition());
	const int partnerFollowRadius = gm->global.getPartnerFollowRadius();
	const bool shouldKeepOwnerFollowPriority = shouldKeepPartnerOwnerFollowPriority(
		kind, true, true,
		partnerOwnerFollowPriorityActive, playerDistance,
		partnerFollowRadius);
	if (!shouldKeepOwnerFollowPriority)
	{
		partnerOwnerFollowPriorityActive = false;
		partnerOwnerFollowBestDistance = INT_MAX;
		partnerOwnerFollowLastProgressTime = 0;
		if (playerDistance <= partnerFollowRadius)
		{
			partnerOwnerFollowRetryTime = 0;
		}
		return false;
	}

	if (!partnerOwnerFollowPriorityActive)
	{
		if (!canRetryPartnerOwnerFollow(currentTime, partnerOwnerFollowRetryTime))
		{
			return false;
		}
		partnerOwnerFollowPriorityActive = true;
		partnerOwnerFollowBestDistance = playerDistance;
		partnerOwnerFollowLastProgressTime = currentTime;
		partnerOwnerFollowRetryTime = 0;
	}
	else if (playerDistance < partnerOwnerFollowBestDistance)
	{
		partnerOwnerFollowBestDistance = playerDistance;
		partnerOwnerFollowLastProgressTime = currentTime;
	}
	else if (hasPartnerOwnerFollowStalled(currentTime, partnerOwnerFollowLastProgressTime))
	{
		partnerOwnerFollowPriorityActive = false;
		partnerOwnerFollowBestDistance = INT_MAX;
		partnerOwnerFollowLastProgressTime = 0;
		partnerOwnerFollowRetryTime = currentTime + PartnerOwnerFollowRetryCooldownMilliseconds;
		return false;
	}

	clearCombatTargetMemory();
	// Standing followers consult this timer before starting a new follow path.
	// Make the owner-follow decision eligible immediately after disengaging.
	nextFollowCheckTime = currentTime;
	return true;
}

bool NPC::isWithinCombatChaseLimit(Point targetPosition, Point npcPosition) const
{
	return gm->map->calDistance(npcPosition, targetPosition) <= visionRadius * 2;
}

bool NPC::isCurrentPathWithinCombatChaseLimit(Point targetPosition) const
{
	if (!isWithinCombatChaseLimit(targetPosition, position))
	{
		return false;
	}
	for (const auto& step : stepList)
	{
		if (!isWithinCombatChaseLimit(targetPosition, step))
		{
			return false;
		}
	}
	return true;
}

bool NPC::isRememberedCombatTarget(std::shared_ptr<GameElement> target) const
{
	if (target == nullptr)
	{
		return false;
	}
	auto currentTarget = currentCombatTarget.lock();
	if (currentTarget != nullptr && currentTarget == target)
	{
		return true;
	}
	auto lastTarget = lastCombatTarget.lock();
	if (lastTarget != nullptr && lastTarget == target)
	{
		return true;
	}
	return false;
}

bool NPC::canChaseCombatTargetFromMemory(std::shared_ptr<GameElement> target) const
{
	if (!hasLastKnownCombatTargetPosition || lastKnownCombatTargetTime == 0)
	{
		return false;
	}
	auto memoryTarget = lastKnownCombatTarget.lock();
	if (memoryTarget == nullptr || memoryTarget != target)
	{
		return false;
	}
	if (!isCombatTargetValid(target) || !isRememberedCombatTarget(target))
	{
		return false;
	}
	if (getTime() - lastKnownCombatTargetTime > NPC::MemoryPositionChaseTimeout)
	{
		return false;
	}
	int memoryDistance = gm->map->calDistance(position, lastKnownCombatTargetPosition);
	if (memoryDistance > visionRadius * 2)
	{
		return false;
	}
	return true;
}

std::optional<Point> NPC::getCombatTargetMemoryPosition(std::shared_ptr<GameElement> target) const
{
	if (!canChaseCombatTargetFromMemory(target))
	{
		return std::nullopt;
	}
	return lastKnownCombatTargetPosition;
}

bool NPC::canReleaseCombatAttack(std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode) const
{
	if (releaseMode == armGroundTarget)
	{
		return true;
	}
	if (!isCombatTargetValid(target))
	{
		return false;
	}
	if (kind == nkPlayer)
	{
		return true;
	}
	return canSee(target->position);
}

void NPC::updateIdleFrame()
{
	if (idle <= 0)
	{
		idledFrame = 0;
		return;
	}
	if (idledFrame < idle)
	{
		idledFrame++;
	}
}

bool NPC::canStartIdleAttack()
{
	if (idle <= 0)
	{
		idledFrame = 0;
		return true;
	}
	if (idledFrame >= idle)
	{
		idledFrame = 0;
		return true;
	}
	return false;
}

void NPC::applyBounceFromEffect(const Effect& effect)
{
	if (effect.magic.bounce <= 0)
	{
		return;
	}

	PointEx directionVector = getEffectHitDirectionVector(*this, effect);
	float directionLength = getProjectedMovementLength(directionVector);
	if (directionLength <= 0.0f)
	{
		return;
	}

	NPCBounceMotion motion = composeBounceMotion(bounceDirection, bounceVelocity, directionVector, (float)effect.magic.bounce);
	if (motion.velocity <= 0.0f)
	{
		clearBounceState();
		actionPlan.reset();
		if (gm != nullptr && gm->player != nullptr && gm->player.get() == this)
		{
			gm->player->nextAction = nullptr;
		}
		if (actionManager != nullptr && actionManager->getCurrentActionType() == NPCActionType::acBounce)
		{
			actionManager->forceChangeAction(NPCActionType::acStand);
		}
		return;
	}
	bounceDirection = motion.direction;
	bounceVelocity = motion.velocity;
	bounceCollisionHurt = effect.magic.bounceHurt > 0 ? effect.magic.bounceHurt : 0;
	bounceCollisionLauncherKind = effect.launcherKind;
	bounceCollisionLauncher = effect.user.lock();
	lastBounceBlockedByCharacter = false;
	lastBounceBlockedCharacterPosition = { 0, 0 };
	lastBounceEndPosition = position;
	actionPlan.reset();
	if (gm != nullptr && gm->player != nullptr && gm->player.get() == this)
	{
		gm->player->nextAction = nullptr;
	}
	stopMovementPreservingOffset();
	clearMagicForcedMoveState();
	actionManager->forceChangeAction(NPCActionType::acBounce);
}

void NPC::applyBounceFlyFromEffect(const Effect& effect)
{
	const auto& linked = effect.magic.getLinkedLevel(effect.level);
	if (linked.bounceFly <= 0)
	{
		return;
	}

	PointEx directionVector = getEffectHitDirectionVector(*this, effect);
	if (getProjectedMovementLength(directionVector) <= 0.0f)
	{
		return;
	}

	Point destination = findDistanceTileInDirection(position, directionVector, linked.bounceFly);
	auto endMagicContext = linked.bounceFlyEndMagic != nullptr
		? Magic::createDerivedDispatchContext(
			effect.magicDispatchContext,
			linked.bounceFlyEndMagic,
			"BounceFlyEndMagic")
		: nullptr;
	beginMagicForcedMove(
		destination,
		(float)linked.bounceFlySpeed,
		effect.user.lock(),
		endMagicContext != nullptr ? linked.bounceFlyEndMagic : nullptr,
		effect.level,
		effect.launcherKind,
		linked.magicDirectionWhenBounceFlyEnd,
		linked.bounceFlyEndHurt,
		linked.bounceFlyTouchHurt,
		linked.bounceFly,
		true,
		directionVector,
		endMagicContext);
}

void NPC::updateBounceMovement(UTime frameTime)
{
	if (bounceVelocity <= 0.0f || frameTime == 0)
	{
		return;
	}
	if (isDying() || isHiding())
	{
		clearBounceState();
		return;
	}

	float distance = getBounceFrameDistance(bounceVelocity, (float)frameTime, Config::getGameSpeed());
	PointEx nextOffset = { offset.x + bounceDirection.x * distance, offset.y + bounceDirection.y * distance };
	Point nextPosition = position;
	PointEx normalizedOffset = nextOffset;
	getNewPosition(position, nextOffset, &nextPosition, &normalizedOffset);
	auto recordBounceBlock = [&](Point blockedPosition)
	{
		lastBounceBlockedByCharacter = hasCharacterObstacleAt(blockedPosition, this);
		lastBounceBlockedCharacterPosition = lastBounceBlockedByCharacter ? blockedPosition : Point{ 0, 0 };
		lastBounceEndPosition = position;
	};

	if (gm != nullptr && gm->map != nullptr && nextPosition != position)
	{
		Point pathDirection =
		{
			static_cast<int>(std::round(bounceDirection.x * 1000.0f)),
			static_cast<int>(std::round(bounceDirection.y * 1000.0f))
		};
		auto passPath = gm->map->getPassPathEx(position, offset, nextPosition, normalizedOffset, pathDirection);
		for (const auto& passPosition : passPath)
		{
			if (passPosition == position)
			{
				continue;
			}
			if (isBounceMovementBlockedAt(passPosition, this))
			{
				recordBounceBlock(passPosition);
				applyBounceCollisionHurt(passPosition);
				clearBounceState();
				offset = { 0, 0 };
				return;
			}
		}
	}

	if (nextPosition != position)
	{
		if (isBounceMovementBlockedAt(nextPosition, this))
		{
			recordBounceBlock(nextPosition);
			applyBounceCollisionHurt(nextPosition);
			clearBounceState();
			offset = { 0, 0 };
			return;
		}
		setPosition(nextPosition, false);
	}
	offset = normalizedOffset;

	float friction = getBounceFrameFriction((float)frameTime);
	bounceVelocity -= friction;
	if (bounceVelocity <= 0.0f)
	{
		clearBounceState();
		offset = { 0, 0 };
		lastBounceEndPosition = position;
	}
}

void NPC::clearBounceState()
{
	bounceDirection = { 0.0f, 0.0f };
	bounceVelocity = 0.0f;
	bounceCollisionHurt = 0;
	bounceCollisionLauncherKind = lkNeutral;
	bounceCollisionLauncher.reset();
}

void NPC::applyBounceCollisionHurt(Point blockedPosition)
{
	if (bounceCollisionHurt <= 0)
	{
		return;
	}

	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	auto target = findBounceCollisionTarget(blockedPosition, self, bounceCollisionLauncherKind, bounceCollisionLauncher.lock());
	if (target == nullptr || target->hasActiveSelfMagic(mskBlockDamage))
	{
		return;
	}

	target->addLife(-bounceCollisionHurt);
	if (target->life <= 0)
	{
		target->life = 0;
		target->handleDeath();
		return;
	}

	target->beginHurt();
}

bool NPC::isMagicForcedMoving() const
{
	return magicForcedMove.active;
}

void NPC::beginMagicForcedMove(Point destination,
	float moveSpeed,
	std::weak_ptr<GameElement> caster,
	std::shared_ptr<Magic> endMagic,
	int magicLevel,
	int launcher,
	int endDirectionMode,
	int endHurt,
	int touchHurt,
	int touchDistance,
	bool blockCharactersOnPath,
	PointEx touchDirection,
	std::shared_ptr<MagicDispatchContext> dispatchContext)
{
	if (gm == nullptr || gm->map == nullptr || isDying() || isHiding())
	{
		return;
	}
	if (!gm->map->isInMap(destination))
	{
		return;
	}

	stopMovementPreservingOffset();
	clearBounceState();
	lastMagicForcedMoveBlockedByCharacter = false;
	lastMagicForcedMoveBlockedCharacterPosition = { 0, 0 };
	lastMagicForcedMoveEndPosition = position;
	lastMagicForcedMoveTouchTargetCount = 0;
	lastMagicForcedMoveTouchHurtCount = 0;

	magicForcedMove = {};
	magicForcedMove.active = true;
	magicForcedMove.destination = destination;
	magicForcedMove.speed = moveSpeed > 0.0f ? moveSpeed : 32.0f;
	magicForcedMove.caster = caster;
	magicForcedMove.endMagic = endMagic;
	magicForcedMove.magicDispatchContext = dispatchContext;
	magicForcedMove.level = clampMagicLevelForNpc(magicLevel);
	magicForcedMove.launcherKind = launcher;
	magicForcedMove.endDirectionMode = endDirectionMode;
	magicForcedMove.endHurt = endHurt > 0 ? endHurt : 0;
	magicForcedMove.touchHurt = touchHurt > 0 ? touchHurt : 0;
	magicForcedMove.touchDistance = touchDistance > 0 ? touchDistance : 0;
	if (getProjectedMovementLength(touchDirection) > 0.0f)
	{
		magicForcedMove.hasTouchDirection = true;
		magicForcedMove.touchDirection = touchDirection;
	}
	magicForcedMove.blockCharactersOnPath = blockCharactersOnPath;
	magicForcedMove.startPosition = position;
	magicForcedMove.startOffset = offset;
	magicForcedMove.lineOffset = Map::getTilePositionEx(destination, position, { 0, 0 }, { 0.0f, 0.0f });
	magicForcedMove.lineOffset.x -= offset.x;
	magicForcedMove.lineOffset.y -= offset.y;
	magicForcedMove.totalProjectedDistance = getProjectedMovementLength(magicForcedMove.lineOffset);
	magicForcedMove.movedProjectedDistance = 0.0f;
	magicForcedMove.drawOffset = { 0.0f, 0.0f };

	if (position != destination)
	{
		direction = getDirection(destination);
	}
	actionPlan.reset();
	if (gm->player != nullptr && gm->player.get() == this)
	{
		gm->player->nextAction = nullptr;
		gm->player->nextDest = ndNone;
		gm->player->nextDestUseRightScript = false;
		gm->player->nextDestStrictWorldInteraction = false;
		gm->player->nextDestRequestedRunning = false;
		gm->player->destGE.reset();
	}

	if (position == destination && hypot(offset.x, offset.y) <= 0.01f)
	{
		finishMagicForcedMove();
		return;
	}
	actionManager->forceChangeAction(NPCActionType::acMagicForcedMove);
}

void NPC::updateMagicForcedMovement(UTime frameTime)
{
	if (!magicForcedMove.active || frameTime == 0)
	{
		return;
	}
	if (isDying() || isHiding())
	{
		clearMagicForcedMoveState();
		return;
	}

	PointEx destinationOffset = Map::getTilePositionEx(magicForcedMove.destination, position, { 0, 0 }, { 0.0f, 0.0f });
	destinationOffset.x -= offset.x;
	destinationOffset.y -= offset.y;
	float distanceToDestination = getProjectedMovementLength(destinationOffset);
	if (distanceToDestination <= 0.01f)
	{
		magicForcedMove.movedProjectedDistance = magicForcedMove.totalProjectedDistance;
		magicForcedMove.drawOffset = { 0.0f, 0.0f };
		if (magicForcedMove.destination != position
			&& isMagicForcedMoveBlockedAt(magicForcedMove.destination, this, magicForcedMove.blockCharactersOnPath))
		{
			if (magicForcedMove.blockCharactersOnPath && hasCharacterObstacleAt(magicForcedMove.destination, this))
			{
				magicForcedMove.hasBlockedCharacterPosition = true;
				magicForcedMove.blockedCharacterPosition = magicForcedMove.destination;
			}
			offset = { 0.0f, 0.0f };
			finishMagicForcedMove();
			return;
		}
		setPosition(magicForcedMove.destination, false);
		offset = { 0.0f, 0.0f };
		finishMagicForcedMove();
		return;
	}

	float frameDistance = getProjectedFrameDistance(magicForcedMove.speed, (float)frameTime, Config::getGameSpeed());
	if (frameDistance <= 0.0f || frameDistance >= distanceToDestination)
	{
		magicForcedMove.movedProjectedDistance = magicForcedMove.totalProjectedDistance;
		magicForcedMove.drawOffset = { 0.0f, 0.0f };
		Point lastOpenPosition = position;
		Point blockedPosition = position;
		if (findMagicForcedMovePathBlock(
			this,
			position,
			offset,
			magicForcedMove.destination,
			{ 0.0f, 0.0f },
			destinationOffset,
			magicForcedMove.blockCharactersOnPath,
			lastOpenPosition,
			blockedPosition))
		{
			if (magicForcedMove.blockCharactersOnPath && hasCharacterObstacleAt(blockedPosition, this))
			{
				magicForcedMove.hasBlockedCharacterPosition = true;
				magicForcedMove.blockedCharacterPosition = blockedPosition;
			}
			if (lastOpenPosition != position)
			{
				setPosition(lastOpenPosition, false);
			}
			offset = { 0.0f, 0.0f };
			finishMagicForcedMove();
			return;
		}
		setPosition(magicForcedMove.destination, false);
		offset = { 0.0f, 0.0f };
		finishMagicForcedMove();
		return;
	}

	float actualFrameDistance = std::min(frameDistance, distanceToDestination);
	PointEx nextOffset = advanceProjectedMovement(offset, destinationOffset, frameDistance);
	Point nextPosition = position;
	PointEx normalizedOffset = nextOffset;
	getNewPosition(position, nextOffset, &nextPosition, &normalizedOffset);

	if (nextPosition != position)
	{
		Point lastOpenPosition = position;
		Point blockedPosition = position;
		if (findMagicForcedMovePathBlock(
			this,
			position,
			offset,
			nextPosition,
			normalizedOffset,
			destinationOffset,
			magicForcedMove.blockCharactersOnPath,
			lastOpenPosition,
			blockedPosition))
		{
			if (magicForcedMove.blockCharactersOnPath && hasCharacterObstacleAt(blockedPosition, this))
			{
				magicForcedMove.hasBlockedCharacterPosition = true;
				magicForcedMove.blockedCharacterPosition = blockedPosition;
			}
			if (lastOpenPosition != position)
			{
				setPosition(lastOpenPosition, false);
			}
			offset = { 0.0f, 0.0f };
			finishMagicForcedMove();
			return;
		}
		setPosition(nextPosition, false);
	}
	offset = normalizedOffset;
	if (magicForcedMove.totalProjectedDistance > 0.0f)
	{
		magicForcedMove.movedProjectedDistance = std::min(
			magicForcedMove.totalProjectedDistance,
			magicForcedMove.movedProjectedDistance + actualFrameDistance);
		float progress = magicForcedMove.movedProjectedDistance / magicForcedMove.totalProjectedDistance;
		magicForcedMove.drawOffset = getBezierForcedMoveDrawOffset(magicForcedMove.lineOffset, progress);
	}
}

void NPC::clearMagicForcedMoveState()
{
	magicForcedMove = {};
}

void NPC::finishMagicForcedMove()
{
	if (!magicForcedMove.active)
	{
		return;
	}

	NPCMagicForcedMoveState finishedMove = magicForcedMove;
	clearMagicForcedMoveState();
	lastMagicForcedMoveBlockedByCharacter = finishedMove.hasBlockedCharacterPosition;
	lastMagicForcedMoveBlockedCharacterPosition = finishedMove.hasBlockedCharacterPosition
		? finishedMove.blockedCharacterPosition
		: Point{ 0, 0 };
	lastMagicForcedMoveEndPosition = position;

	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	auto caster = finishedMove.caster.lock();
	auto casterNpc = std::dynamic_pointer_cast<NPC>(caster);

	if (finishedMove.endMagic != nullptr && finishedMove.endMagic->loadSucceeded && caster != nullptr && gm != nullptr)
	{
		Point magicDestination = caster->position;
		if (finishedMove.endDirectionMode == 1)
		{
			magicDestination = Map::getSubPoint(position, direction);
		}
		else if (finishedMove.endDirectionMode == 2)
		{
			magicDestination = Map::getSubPoint(position, GameElement::normalizeDir(caster->direction));
		}

		int damage = getPrimaryDamageForMagic(finishedMove.endMagic, finishedMove.level, casterNpc);
		int evadeValue = casterNpc != nullptr ? casterNpc->getEvade() : 0;
		Magic::addEffect(finishedMove.endMagic,
			caster,
			position,
			magicDestination,
			finishedMove.level,
			damage,
			evadeValue,
			finishedMove.launcherKind,
			nullptr,
			finishedMove.magicDispatchContext);
	}

	applyDirectMagicMoveHurt(this, finishedMove.endHurt);

	if (finishedMove.touchHurt > 0 && finishedMove.touchDistance > 0 && gm != nullptr && gm->map != nullptr)
	{
		std::vector<std::shared_ptr<NPC>> touchedTargets;
		auto collectTouchTarget = [&](Point candidatePosition)
		{
			auto target = findBounceCollisionTarget(candidatePosition, self, finishedMove.launcherKind, caster);
			if (target == nullptr)
			{
				return;
			}
			if (std::find(touchedTargets.begin(), touchedTargets.end(), target) == touchedTargets.end())
			{
				touchedTargets.push_back(target);
			}
		};

		collectTouchTarget(position);
		if (finishedMove.hasBlockedCharacterPosition)
		{
			collectTouchTarget(finishedMove.blockedCharacterPosition);
		}
		for (int directionIndex = 0; directionIndex < 8; ++directionIndex)
		{
			collectTouchTarget(Map::getSubPoint(position, directionIndex));
		}
		lastMagicForcedMoveTouchTargetCount = static_cast<int>(touchedTargets.size());

		PointEx moveDirection = finishedMove.touchDirection;
		if (!finishedMove.hasTouchDirection || getProjectedMovementLength(moveDirection) <= 0.0f)
		{
			Point nextDirectionTile = Map::getSubPoint(position, direction);
			moveDirection = Map::getTilePositionEx(nextDirectionTile, position, { 0, 0 }, { 0.0f, 0.0f });
		}
		for (auto& target : touchedTargets)
		{
			lastMagicForcedMoveTouchHurtCount++;
			Point targetDestination = findDistanceTileInDirection(target->getPosition(), moveDirection, finishedMove.touchDistance);
			target->beginMagicForcedMove(targetDestination,
				finishedMove.speed,
				finishedMove.caster,
				nullptr,
				finishedMove.level,
				finishedMove.launcherKind,
				0,
				0,
				0,
				0,
				finishedMove.blockCharactersOnPath,
				moveDirection);
			applyDirectMagicMoveHurt(this, finishedMove.touchHurt);
			applyDirectMagicMoveHurt(target, finishedMove.touchHurt);
		}
	}

	if (!isBouncing() && !isMagicForcedMoving()
		&& (actionManager->getCurrentActionType() == acBounce || actionManager->getCurrentActionType() == acMagicForcedMove))
	{
		actionManager->forceChangeAction(acStand);
	}
}

void NPC::applyEffectActionLocks(const Effect& effect)
{
	if (effect.magic.disableMoveMilliseconds > 0)
	{
		disableMoveMilliseconds = effect.magic.disableMoveMilliseconds;
	}
	if (effect.magic.disableSkillMilliseconds > 0)
	{
		disableSkillMilliseconds = effect.magic.disableSkillMilliseconds;
	}
}

void NPC::applyEffectRuntimeStates(const Effect& effect)
{
	applyEffectActionLocks(effect);
	if (effect.magic.changeToFriendMilliseconds > 0 && kind != nkPlayer && effect.magic.maxLevel >= level)
	{
		applyTemporaryOppositeRelation(effect.magic.changeToFriendMilliseconds);
	}
	if (effect.magic.blindMilliseconds > 0 && kind != nkPlayer)
	{
		blindMilliseconds = effect.magic.blindMilliseconds;
		clearCombatTargetMemory();
	}
	if (effect.magic.weakMilliseconds > 0)
	{
		weakMilliseconds = effect.magic.weakMilliseconds;
		weakMagic = std::make_shared<Magic>(effect.magic);
	}
	if (effect.magic.morphMilliseconds > 0)
	{
		applyTemporaryMorph(effect.magic, effect.magic.morphMilliseconds);
	}
}

void NPC::updateActionLockTimers(UTime frameTime)
{
	if (disableMoveMilliseconds > 0)
	{
		disableMoveMilliseconds = disableMoveMilliseconds > frameTime ? disableMoveMilliseconds - frameTime : 0;
	}
	if (disableSkillMilliseconds > 0)
	{
		disableSkillMilliseconds = disableSkillMilliseconds > frameTime ? disableSkillMilliseconds - frameTime : 0;
	}
	updateMagicRuntimeStateTimers(frameTime);
}

void NPC::updateMagicRuntimeStateTimers(UTime frameTime)
{
	if (!temporaryFlyIniReplacements.empty())
	{
		auto effect = temporaryFlyIniChangeEffect.lock();
		if (effect == nullptr || effect->vanishing || (effect->result & erLifeExhaust))
		{
			clearTemporaryFlyIniChange();
		}
	}
	if (blindMilliseconds > 0)
	{
		blindMilliseconds = blindMilliseconds > frameTime ? blindMilliseconds - frameTime : 0;
	}
	if (invisibleMilliseconds > 0)
	{
		invisibleMilliseconds = invisibleMilliseconds > frameTime ? invisibleMilliseconds - frameTime : 0;
		if (invisibleMilliseconds == 0)
		{
			isVisibleWhenAttack = false;
		}
	}
	if (changeToOppositeMilliseconds > 0)
	{
		changeToOppositeMilliseconds = changeToOppositeMilliseconds > frameTime ? changeToOppositeMilliseconds - frameTime : 0;
		if (changeToOppositeMilliseconds == 0)
		{
			relation = originalRelationBeforeOppositeChange;
			clearCombatTargetMemory();
		}
	}
	if (weakMilliseconds > 0)
	{
		weakMilliseconds = weakMilliseconds > frameTime ? weakMilliseconds - frameTime : 0;
		if (weakMilliseconds == 0)
		{
			weakMagic = nullptr;
		}
	}
	else
	{
		weakMagic = nullptr;
	}
	if (morphMilliseconds > 0)
	{
		morphMilliseconds = morphMilliseconds > frameTime ? morphMilliseconds - frameTime : 0;
		if (morphMilliseconds == 0)
		{
			morphMagic = nullptr;
			clearTemporaryNpcRes();
			clearTemporaryMagicListReplacement();
		}
	}
	else
	{
		morphMagic = nullptr;
		clearTemporaryNpcRes();
		clearTemporaryMagicListReplacement();
	}
}

void NPC::clearMagicRuntimeStates()
{
	if (changeToOppositeMilliseconds > 0)
	{
		relation = originalRelationBeforeOppositeChange;
	}
	blindMilliseconds = 0;
	weakMilliseconds = 0;
	morphMilliseconds = 0;
	invisibleMilliseconds = 0;
	isVisibleWhenAttack = false;
	changeToOppositeMilliseconds = 0;
	originalRelationBeforeOppositeChange = relation;
	weakMagic = nullptr;
	morphMagic = nullptr;
	clearTemporaryNpcRes(false);
	clearTemporaryFlyIniChange(false);
	clearTransportEffect();
	clearTemporaryMagicListReplacement(false);
}

void NPC::applyMagicInvisibility(UTime milliseconds, bool visibleWhenAttack)
{
	if (milliseconds == 0)
	{
		return;
	}
	invisibleMilliseconds = milliseconds;
	isVisibleWhenAttack = visibleWhenAttack;
	selecting = false;
}

void NPC::revealMagicInvisibilityOnAction()
{
	if (invisibleMilliseconds > 0 && isVisibleWhenAttack)
	{
		invisibleMilliseconds = 0;
		isVisibleWhenAttack = false;
	}
}

void NPC::applyTemporaryMorph(const Magic& magic, UTime milliseconds)
{
	if (milliseconds == 0)
	{
		return;
	}
	morphMilliseconds = milliseconds;
	morphMagic = std::make_shared<Magic>(magic);
	applyTemporaryNpcRes(magic);
	applyTemporaryMagicListReplacement(magic);
	stopMovement();
}

void NPC::applyTemporaryNpcRes(const Magic& magic)
{
	if (magic.npcIni.empty())
	{
		clearTemporaryNpcRes();
		return;
	}
	if (!originalResBeforeMorph.has_value())
	{
		originalResBeforeMorph = res;
	}
	if (!loadNpcResFromIni(magic.npcIni, res))
	{
		clearTemporaryNpcRes();
		return;
	}
	temporaryNpcResFile = magic.npcIni;
	beginStand();
}

void NPC::clearTemporaryNpcRes(bool refreshAction)
{
	if (!originalResBeforeMorph.has_value())
	{
		return;
	}
	freeNPCRes();
	res = *originalResBeforeMorph;
	originalResBeforeMorph.reset();
	temporaryNpcResFile.clear();
	if (refreshAction)
	{
		beginStand();
	}
}

void NPC::applyTemporaryFlyIniChange(std::shared_ptr<Effect> effect)
{
	clearTemporaryFlyIniChange();
	if (effect == nullptr)
	{
		return;
	}

	const Magic& magic = effect->magic;
	if (!magic.specialKind9ReplaceFlyIni.empty())
	{
		temporaryFlyIniReplacements.push_back(magic.specialKind9ReplaceFlyIni);
	}
	if (!magic.specialKind9ReplaceFlyIni2.empty())
	{
		temporaryFlyIniReplacements.push_back(magic.specialKind9ReplaceFlyIni2);
	}
	if (temporaryFlyIniReplacements.empty())
	{
		return;
	}

	temporaryFlyIniChangeEffect = effect;
	rebuildAttackOptions();
}

void NPC::clearTemporaryFlyIniChange(bool rebuildOptions)
{
	bool hadReplacement = !temporaryFlyIniReplacements.empty();
	temporaryFlyIniChangeEffect.reset();
	temporaryFlyIniReplacements.clear();
	if (hadReplacement && rebuildOptions)
	{
		rebuildAttackOptions();
	}
}

void NPC::applyTemporaryMagicListReplacement(const Magic& magic)
{
	if (!shouldApplyTemporaryMagicListReplacement(kind, magic.replaceMagic))
	{
		clearTemporaryMagicListReplacement();
		return;
	}
	if (temporaryMagicListReplacement == magic.replaceMagic)
	{
		return;
	}
	temporaryMagicListReplacement = magic.replaceMagic;
	if (kind == nkPlayer)
	{
		if (gm != nullptr)
		{
			gm->magicManager.replaceMagicList(magic.replaceMagic);
		}
	}
	else
	{
		rebuildAttackOptions();
	}
}

void NPC::clearTemporaryMagicListReplacement(bool rebuildOptions)
{
	bool hadReplacement = !temporaryMagicListReplacement.empty();
	temporaryMagicListReplacement.clear();
	if (!hadReplacement)
	{
		return;
	}
	if (kind == nkPlayer)
	{
		if (gm != nullptr)
		{
			gm->magicManager.stopReplaceMagicList();
		}
	}
	else if (rebuildOptions)
	{
		rebuildAttackOptions();
	}
}

void NPC::applyTemporaryOppositeRelation(UTime milliseconds)
{
	if (milliseconds == 0 || kind == nkPlayer)
	{
		return;
	}
	if (changeToOppositeMilliseconds > 0)
	{
		relation = originalRelationBeforeOppositeChange;
		changeToOppositeMilliseconds = 0;
		clearCombatTargetMemory();
		return;
	}

	originalRelationBeforeOppositeChange = relation;
	relation = (relation == nrHostile) ? nrFriendly : nrHostile;
	changeToOppositeMilliseconds = milliseconds;
	clearCombatTargetMemory();
}

void NPC::clearTemporaryOppositeRelation()
{
	if (changeToOppositeMilliseconds > 0)
	{
		relation = originalRelationBeforeOppositeChange;
	}
	changeToOppositeMilliseconds = 0;
	originalRelationBeforeOppositeChange = relation;
}

void NPC::triggerMagicWhenKillEnemy(const Effect& effect)
{
	const auto& linked = effect.magic.getLinkedLevel(effect.level);
	auto killMagic = linked.magicToUseWhenKillEnemy;
	if (killMagic == nullptr || !killMagic->loadSucceeded)
	{
		return;
	}
	auto userPtr = effect.user.lock();
	if (userPtr == nullptr)
	{
		return;
	}

	Point destination = userPtr->position;
	if (linked.magicDirectionWhenKillEnemy == 1)
	{
		destination = Map::getSubPoint(position, direction);
	}
	else if (linked.magicDirectionWhenKillEnemy == 2)
	{
		auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
		int casterDirection = caster != nullptr ? caster->direction : effect.direction / 2;
		destination = Map::getSubPoint(position, casterDirection);
	}

	auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
	const int effectLevel = clampMagicLevelForNpc(effect.level);
	const int damage = getPrimaryDamageForMagic(killMagic, effectLevel, caster);
	const int evade = caster != nullptr ? caster->getEvade() : effect.evade;
	auto childContext = Magic::createDerivedDispatchContext(
		effect.magicDispatchContext,
		killMagic,
		"MagicToUseWhenKillEnemy");
	if (childContext != nullptr)
	{
		Magic::addEffect(
			killMagic,
			userPtr,
			position,
			destination,
			effectLevel,
			damage,
			evade,
			effect.launcherKind,
			nullptr,
			childContext);
	}
}

void NPC::releaseMagicWhenBeAttacked(std::shared_ptr<Magic> magic, int magicDirection, const Effect& effect)
{
	if (magic == nullptr || !magic->loadSucceeded)
	{
		return;
	}

	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	if (self == nullptr)
	{
		return;
	}

	auto attacker = effect.user.lock();
	Point destination = attacker != nullptr ? attacker->position : position;
	std::shared_ptr<GameElement> target = nullptr;
	switch (magicDirection)
	{
	case 0:
		target = attacker;
		break;
	case 1:
		if (!effect.flyingDirection.is_zero())
		{
			int moveDirection = getDirection(atan2(effect.flyingDirection.x, -effect.flyingDirection.y));
			destination = Map::getSubPoint(position, (moveDirection + 4) % 8);
		}
		else
		{
			destination = Map::getSubPoint(position, direction);
		}
		break;
	case 2:
		destination = Map::getSubPoint(position, direction);
		break;
	default:
		break;
	}

	const int effectLevel = getClampedAttackLevel();
	const int damage = getPrimaryDamageForMagic(magic, effectLevel, self);
	auto childContext = Magic::createDerivedDispatchContext(
		effect.magicDispatchContext,
		magic,
		"MagicToUseWhenBeAttacked");
	if (childContext != nullptr)
	{
		Magic::addEffect(
			magic,
			self,
			position,
			destination,
			effectLevel,
			damage,
			getEvade(),
			getLauncherKindForNPC(*this),
			target,
			childContext);
	}
}

void NPC::triggerMagicWhenBeAttacked(const Effect& effect)
{
	releaseMagicWhenBeAttacked(magicToUseWhenBeAttacked, magicDirectionWhenBeAttacked, effect);
	for (const auto& item : equipmentMagicToUseWhenAttacked)
	{
		releaseMagicWhenBeAttacked(item.magic, item.direction, effect);
	}
}

void NPC::triggerMagicWhenDeath()
{
	if (magicToUseWhenDeath == nullptr || !magicToUseWhenDeath->loadSucceeded)
	{
		return;
	}

	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	if (self == nullptr)
	{
		return;
	}

	auto attacker = lastCombatTarget.lock();
	if (attacker == nullptr)
	{
		attacker = currentCombatTarget.lock();
	}

	Point destination = attacker != nullptr ? attacker->position : Map::getSubPoint(position, direction);
	std::shared_ptr<GameElement> target = nullptr;
	switch (magicDirectionWhenDeath)
	{
	case 0:
		target = attacker;
		break;
	case 1:
		if (hasLastCombatMagicDirection)
		{
			int moveDirection = getDirection(atan2(lastCombatMagicDirection.x, -lastCombatMagicDirection.y));
			destination = Map::getSubPoint(position, (moveDirection + 4) % 8);
		}
		else
		{
			destination = attacker != nullptr ? attacker->position : Map::getSubPoint(position, direction);
		}
		break;
	case 2:
		destination = Map::getSubPoint(position, direction);
		break;
	default:
		break;
	}

	const int effectLevel = getClampedAttackLevel();
	const int damage = getPrimaryDamageForMagic(magicToUseWhenDeath, effectLevel, self);
	Magic::addEffect(magicToUseWhenDeath, self, position, destination, effectLevel, damage, getEvade(), getLauncherKindForNPC(*this), target);
}

void NPC::stopMovement()
{
	if (isWalking() || isRunning())
	{
		beginStand();
	}
}

void NPC::stopMovementPreservingOffset()
{
	if (isWalking() || isRunning())
	{
		PointEx movementOffset = offset;
		beginStand();
		offset = movementOffset;
	}
}

bool NPC::mouseInRect(int x, int y)
{
	if (!isVisibleForRuntime() || isHiddenByCarryMagic())
	{
		return false;
	}

	_shared_image image = getActionImage(nullptr, nullptr);
	if (engine->pointInImage(image, x - rect.x, y - rect.y))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void NPC::hurt(std::shared_ptr<Effect> e)
{
	if (isDying() || isHiding() || e == nullptr)
	{
		return;
	}
	bool addexp = false;
	if (e->launcherKind == lkSelf)
	{
		addexp = true;
	}
	if (auto caster = std::dynamic_pointer_cast<NPC>(e->user.lock()))
	{
		caster->recordMagicHitForChange(e->magic, e->level);
	}
	if (hasActiveSelfMagic(mskBlockDamage))
	{
		return;
	}
	int damage = calculateEffectDamage(e);
	applyEffectRuntimeStates(*e);
	int effectLevel = clampMagicLevelForNpc(e->level);
	bool handledSpecialEffect = applyPreDamageMagicStatus(*e, effectLevel);
	const int targetEvade = getEvade();

	if (isMagicDamageHitAgainstNPC(
		e->evade, targetEvade, engine->getRand(100)))
	{
		if (addexp)
		{
			gm->magicManager.addHitExp(e, level);
		}
		bool wasCritical = false;
		damage = applyCriticalDamageFromEffect(e, damage, &wasCritical);
		applyEffectManaDamage(e);
		if (invincible > 0)
		{
			damage = 0;
		}
		if (damage > 0 && shieldLife > 0)
		{
			if (damage > shieldLife)
			{
				damage -= shieldLife;
				shieldLife = 0;
				if (auto shield = shieldEffect.lock())
				{
					shield->vanishing = true;
					shield->beginExplode(shield->position);
				}
				shieldEffect.reset();
			}
			else
			{
				shieldLife -= damage;
				damage = 0;
				return;
			}
		}
		if (wasCritical && damage > 0)
		{
			showCriticalDamageTip(damage);
		}

		int restoreDamage = damage > life ? life : damage;
		applyEffectRestore(e, restoreDamage);
		triggerMagicWhenBeAttacked(*e);
		if (damage >= life)
		{
			life = 0;
			rememberDamageSource(e);
			if (addexp)
			{
				awardDefeatedNpcExperience(e);
			}
			handleDeath();
			triggerMagicWhenKillEnemy(*e);
		}
		else
		{
			addLife(-damage);
			applyBounceFromEffect(*e);
			applyBounceFlyFromEffect(*e);
			if (!isNotFightBackWhenBeHit() && !(blindMilliseconds > 0 && kind != nkPlayer))
			{
				fightState.set(true);
				rememberDamageSource(e);
			}
			int d;
			if (e->flyingDirection.x != 0 || e->flyingDirection.y != 0)
			{
				d = getDirection(atan2(e->flyingDirection.x, -e->flyingDirection.y));
			}
			else
			{
				auto userPtr = e->user.lock();
				if (userPtr)
				{
					d = getDirection(position, userPtr->position);
				}
				else
				{
					d = direction;
				}
			}
			Point fd = gm->map->getSubPoint(position, d);
			if (shouldBeginHurtActionAfterMagicDamage(
				engine->getRand(3), handledSpecialEffect, immobilized, petrified))
			{
				beginHurt(fd);
			}
		}
	}
}

void NPC::directHurt(std::shared_ptr<Effect> e)
{
	if (isDying() || isHiding() || e == nullptr)
	{
		return;
	}
	bool addexp = false;
	if (e->launcherKind == lkSelf)
	{
		addexp = true;
	}
	int damage = calculateEffectDamage(e);
	bool wasCritical = false;
	damage = applyCriticalDamageFromEffect(e, damage, &wasCritical);
	int effectLevel = clampMagicLevelForNpc(e->level);
	applyEffectRuntimeStates(*e);
	bool handledSpecialEffect = applyPreDamageMagicStatus(*e, effectLevel);
	if (addexp)
	{
		gm->magicManager.addHitExp(e, level);
	}
	applyEffectManaDamage(e);
	if (invincible > 0)
	{
		damage = 0;
	}
	if (wasCritical && damage > 0)
	{
		showCriticalDamageTip(damage);
	}
	int restoreDamage = damage > life ? life : damage;
	applyEffectRestore(e, restoreDamage);
	triggerMagicWhenBeAttacked(*e);
	if (damage >= life)
	{
		life = 0;
		rememberDamageSource(e);
		if (addexp)
		{
			awardDefeatedNpcExperience(e);
		}
		handleDeath();
		triggerMagicWhenKillEnemy(*e);
	}
	else
	{
		addLife(-damage);
		if (!isNotFightBackWhenBeHit() && !(blindMilliseconds > 0 && kind != nkPlayer))
		{
			fightState.set(true);
			rememberDamageSource(e);
		}
		if (shouldBeginHurtActionAfterMagicDamage(
			engine->getRand(3), handledSpecialEffect, immobilized, petrified))
		{
			beginHurt();
		}
	}	
}

void NPC::beginJump(Point dest)
{
	if (!canDoAction(acJump) || immobilized || petrified)
	{
		return;
	}
	if (!canActToward(dest, getJumpDirectionCount()))
	{
		return;
	}
	Point step = gm->map->getJumpPath(position, dest);
	stepList.resize(1);
	stepList[0] = step;
	direction = getDirection(stepList[0]);
	
	actionManager->changeAction(acJump);
}

void NPC::playSound(NPCActionType act)
{
	PointEx soundOffset;
	soundOffset.x = gm->camera->offset.x - offset.x;
	soundOffset.y = gm->camera->offset.y - offset.y;
	Point pos = Map::getTilePosition(position, gm->camera->position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	switch (act)
	{
	case NPCActionType::acStand:
		playSoundFile(res.stand.soundFile, x, y);
		break;
	case NPCActionType::acStand1:
		playSoundFile(res.stand1.soundFile, x, y);
		break;
	case NPCActionType::acWalk:
		playSoundFile(res.walk.soundFile, x, y);
		break;
	case NPCActionType::acRun:
		playSoundFile(res.run.soundFile, x, y);
		break;
	case NPCActionType::acJump:
		playSoundFile(res.jump.soundFile, x, y);
		break;
	case NPCActionType::acAttack:
		playSoundFile(res.attack.soundFile, x, y);
		break;
	case NPCActionType::acAttack1:
		playSoundFile(res.attack1.soundFile, x, y);
		break;
	case NPCActionType::acAttack2:
		playSoundFile(res.attack2.soundFile, x, y);
		break;
	case NPCActionType::acMagic:
		playSoundFile(res.magic.soundFile, x, y);
		break;
	case NPCActionType::acHurt:
		playSoundFile(res.hurt.soundFile, x, y);
		break;
	case NPCActionType::acDeath:
		playSoundFile(res.death.soundFile, x, y);
		break;
	case NPCActionType::acSit:
		playSoundFile(res.sit.soundFile, x, y);
		break;
	case NPCActionType::acSpecial:
		playSoundFile(res.special.soundFile, x, y);
		break;
	case NPCActionType::acSitting:
		break;
	case NPCActionType::acAStand:
		playSoundFile(res.astand.soundFile, x, y);
		break;
	case NPCActionType::acAWalk:
		playSoundFile(res.awalk.soundFile, x, y);
		break;
	case NPCActionType::acARun:
		playSoundFile(res.arun.soundFile, x, y);
		break;
	case NPCActionType::acAJump:
		playSoundFile(res.ajump.soundFile, x, y);
		break;
	default:
		break;
	}
}

void NPC::reloadAction()
{

#define reloadAction(act) loadActionRes(&res.act)

	reloadAction(stand);
	reloadAction(stand1);
	reloadAction(walk);
	reloadAction(run);
	reloadAction(jump);
	reloadAction(attack);
	reloadAction(attack1);
	reloadAction(attack2);
	reloadAction(magic);
	reloadAction(hurt);
	reloadAction(death);
	reloadAction(sit);

	reloadAction(astand);
	reloadAction(awalk);
	reloadAction(arun);
	reloadAction(ajump);
}

std::shared_ptr<Magic> NPC::selectAttackMagicForAction(Point dest, std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode)
{
	std::shared_ptr<Magic> selectedMagic = nullptr;
	int clampedLevel = getClampedAttackLevel();
	bool canReleaseTargetAttack = canReleaseCombatAttack(target, releaseMode);

	if (releaseMode == armGroundTarget && actionPlan.isActive())
	{
		actionPlan.reset();
	}

	if (actionPlan.isActive() && actionPlan.hasSelectedOption && actionPlan.selectedOption.magic != nullptr)
	{
		bool canReleasePlannedAttack = canReleaseTargetAttack;
		if (canReleasePlannedAttack && kind != nkPlayer)
		{
			canReleasePlannedAttack = canMagicHitTarget(actionPlan.selectedOption, position, dest, clampedLevel);
		}
		if ((releaseMode == armLockedRelease || releaseMode == armCombatTarget) && canReleasePlannedAttack)
		{
			selectedMagic = actionPlan.selectedOption.magic;
			lastUsedAttackOption = actionPlan.selectedOption;
			hasLastUsedAttackOption = true;
			actionPlan.reset();
		}
		else if (releaseMode == armLockedRelease)
		{
			actionPlan.reset();
			return nullptr;
		}
		else if (releaseMode != armGroundTarget)
		{
			actionPlan.reset();
			return nullptr;
		}
	}

	if (selectedMagic == nullptr && !attackOptions.empty())
	{
		bool allowSelection = (releaseMode == armGroundTarget) || canReleaseTargetAttack;
		if (allowSelection)
		{
			std::vector<const NPCAttackOption*> hitOptions;
			std::vector<const NPCAttackOption*> nearOptions;
			int bestNearCost = INT_MAX;
			for (const auto& option : attackOptions)
			{
				if (option.magic == nullptr || !option.isTargetAttack || !canUseMagicByState(option.magic, false))
				{
					continue;
				}
				if (canMagicHitTarget(option, position, dest, clampedLevel))
				{
					hitOptions.push_back(&option);
				}
				else if (releaseMode == armGroundTarget && target == nullptr)
				{
					int cost = abs(gm->map->calDistance(position, dest) - calcEffectiveUseDistance(option));
					if (cost < bestNearCost)
					{
						bestNearCost = cost;
						nearOptions.clear();
						nearOptions.push_back(&option);
					}
					else if (cost == bestNearCost)
					{
						nearOptions.push_back(&option);
					}
				}
			}
			if (!hitOptions.empty())
			{
				int idx = hitOptions.size() > 1 ? engine->getRand((int)hitOptions.size() - 1) : 0;
				selectedMagic = hitOptions[idx]->magic;
				lastUsedAttackOption = *hitOptions[idx];
				hasLastUsedAttackOption = true;
			}
			else if (!nearOptions.empty())
			{
				int idx = nearOptions.size() > 1 ? engine->getRand((int)nearOptions.size() - 1) : 0;
				selectedMagic = nearOptions[idx]->magic;
				lastUsedAttackOption = *nearOptions[idx];
				hasLastUsedAttackOption = true;
			}
		}
	}

	if (selectedMagic == nullptr && attackOptions.empty())
	{
		bool allowFallback = (releaseMode == armGroundTarget) || canReleaseTargetAttack;
		if (allowFallback)
		{
			bool magic1Valid = npcMagic != nullptr && npcMagic->loadSucceeded;
			bool magic2Valid = npcMagic2 != nullptr && npcMagic2->loadSucceeded;
			if (magic1Valid && !canUseMagicByState(npcMagic, false))
			{
				magic1Valid = false;
			}
			if (magic2Valid && !canUseMagicByState(npcMagic2, false))
			{
				magic2Valid = false;
			}
			if (magic1Valid && !magic2Valid)
			{
				selectedMagic = npcMagic;
			}
			else if (!magic1Valid && magic2Valid)
			{
				selectedMagic = npcMagic2;
			}
			else if (magic1Valid && magic2Valid)
			{
				int idx = engine->getRand(1);
				selectedMagic = idx ? npcMagic : npcMagic2;
			}
			if (selectedMagic != nullptr)
			{
				lastUsedAttackOption = NPCAttackOption();
				lastUsedAttackOption.magic = selectedMagic;
				lastUsedAttackOption.moveKind = selectedMagic->level[clampedLevel].moveKind;
				lastUsedAttackOption.region = selectedMagic->level[clampedLevel].region;
				lastUsedAttackOption.shapeRange = getAttackOptionShapeRange(lastUsedAttackOption.moveKind, lastUsedAttackOption.region, clampedLevel);
				lastUsedAttackOption.configuredUseDistance = lastUsedAttackOption.moveKind == mmkPoint ? 0 : selectedMagic->level[clampedLevel].attackRadius;
				lastUsedAttackOption.hasExplicitUseDistance = false;
				lastUsedAttackOption.sourceIndex = -1;
				lastUsedAttackOption.isTargetAttack = true;
				lastUsedAttackOption.useAdditionalEffect = true;
				hasLastUsedAttackOption = true;
			}
		}
	}

	if (selectedMagic == nullptr && target != nullptr && !attackOptions.empty())
	{
		actionPlan.reset();
		return nullptr;
	}

	if (selectedMagic != nullptr)
	{
		selectedMagic = resolveMagicReplacement(selectedMagic);
		if (!canUseMagicByState(selectedMagic, false))
		{
			selectedMagic = nullptr;
		}
	}
	return selectedMagic;
}

bool NPC::releaseAttackMagic(std::shared_ptr<Magic> selectedMagic, Point dest, std::shared_ptr<GameElement> target, bool applyAdditionalEffect)
{
	if (selectedMagic == nullptr)
	{
		return false;
	}
	if (!canUseMagicByState(selectedMagic, kind == nkPlayer))
	{
		return false;
	}

	int launcher = getLauncherKindForNPC(*this);

	int clampedLevel = getClampedAttackLevel();
	if (target != nullptr && canSee(target->position))
	{
		lastCombatTarget = target;
		lastCombatTargetTime = getTime();
		rememberCombatTargetPosition(target);
	}
	auto effects = Magic::addEffect(selectedMagic, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, clampedLevel, getAttack(), getEvade(), launcher, target);
	if (applyAdditionalEffect)
	{
		for (auto& effect : effects)
		{
			if (effect != nullptr)
			{
				effect->additionalEffect = getAttackAdditionalEffect();
			}
		}
	}
	return true;
}

std::shared_ptr<Magic> NPC::prepareAttackMagicForAction(Point dest, std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode)
{
	clearPreparedAttackMagic();
	auto selectedMagic = selectAttackMagicForAction(dest, target, releaseMode);
	bool applyAdditionalEffect = hasLastUsedAttackOption && lastUsedAttackOption.useAdditionalEffect;
	setPreparedAttackMagic(selectedMagic, applyAdditionalEffect);
	return selectedMagic;
}

bool NPC::releasePreparedAttackMagic(Point dest, std::shared_ptr<GameElement> target)
{
	if (!hasPreparedAttackMagic)
	{
		return false;
	}
	auto selectedMagic = preparedAttackMagic;
	bool applyAdditionalEffect = preparedAttackUsesAdditionalEffect;
	clearPreparedAttackMagic();
	return releaseAttackMagic(selectedMagic, dest, target, applyAdditionalEffect);
}

void NPC::setPreparedAttackMagic(std::shared_ptr<Magic> magic, bool useAdditionalEffect)
{
	preparedAttackMagic = magic;
	hasPreparedAttackMagic = magic != nullptr;
	preparedAttackUsesAdditionalEffect = hasPreparedAttackMagic && useAdditionalEffect;
}

void NPC::clearPreparedAttackMagic()
{
	preparedAttackMagic = nullptr;
	hasPreparedAttackMagic = false;
	preparedAttackUsesAdditionalEffect = false;
}

void NPC::setPreparedMagicAction(std::shared_ptr<Magic> magic, Point dest, int level, std::shared_ptr<GameElement> target, int listIndex)
{
	preparedMagicAction = magic;
	preparedMagicActionDest = dest;
	preparedMagicActionLevel = level < 1 ? 1 : (level > MAGIC_MAX_LEVEL ? MAGIC_MAX_LEVEL : level);
	preparedMagicActionListIndex = listIndex;
	preparedMagicActionTarget = target;
}

void NPC::clearPreparedMagicAction()
{
	preparedMagicAction = nullptr;
	preparedMagicActionDest = { 0, 0 };
	preparedMagicActionLevel = 1;
	preparedMagicActionListIndex = -1;
	preparedMagicActionTarget.reset();
}

bool NPC::canUseMagicByState(std::shared_ptr<Magic> magic, bool showMessage)
{
	if (magic == nullptr)
	{
		return false;
	}
	if (magic->disableUse > 0)
	{
		if (showMessage && kind == nkPlayer && gm != nullptr)
		{
			gm->showMessage("该武功不能使用");
		}
		return false;
	}
	if (magic->lifeFullToUse > 0 && life < getLifeMax())
	{
		if (showMessage && kind == nkPlayer && gm != nullptr)
		{
			gm->showMessage("满血才能使用");
		}
		return false;
	}
	return true;
}

int NPC::summonedNpcsCount(const Magic& magic)
{
	std::string key = getSummonMagicKey(magic);
	if (key.empty())
	{
		return 0;
	}
	auto iter = summonedNPCsByMagic.find(key);
	if (iter == summonedNPCsByMagic.end())
	{
		return 0;
	}

	auto& summonedList = iter->second;
	for (auto npcIter = summonedList.begin(); npcIter != summonedList.end();)
	{
		auto npc = npcIter->lock();
		if (npc == nullptr || npc->isDying() || npc->isHiding())
		{
			npcIter = summonedList.erase(npcIter);
		}
		else
		{
			++npcIter;
		}
	}
	return (int)summonedList.size();
}

void NPC::addSummonedNpc(const Magic& magic, std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}
	std::string key = getSummonMagicKey(magic);
	if (key.empty())
	{
		return;
	}
	summonedNpcsCount(magic);
	summonedNPCsByMagic[key].push_back(npc);
}

void NPC::removeFirstSummonedNpc(const Magic& magic)
{
	std::string key = getSummonMagicKey(magic);
	if (key.empty())
	{
		return;
	}
	summonedNpcsCount(magic);
	auto iter = summonedNPCsByMagic.find(key);
	if (iter == summonedNPCsByMagic.end() || iter->second.empty())
	{
		return;
	}

	auto npc = iter->second.front().lock();
	iter->second.pop_front();
	if (npc == nullptr || npc->isDying() || npc->isHiding())
	{
		return;
	}
	npc->life = 0;
	npc->handleDeath();
}

bool NPC::doAttack(Point dest, std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode)
{
	auto selectedMagic = selectAttackMagicForAction(dest, target, releaseMode);
	bool applyAdditionalEffect = hasLastUsedAttackOption && lastUsedAttackOption.useAdditionalEffect;
	return releaseAttackMagic(selectedMagic, dest, target, applyAdditionalEffect);
}

std::shared_ptr<Magic> NPC::resolveMagicReplacement(std::shared_ptr<Magic> magic)
{
	return magic;
}

void NPC::useMagic(std::shared_ptr<Magic> m, Point dest, int level, std::shared_ptr<GameElement> target)
{
	if (m == nullptr)
	{
		return;
	}
	m = resolveMagicReplacement(m);
	if (m == nullptr)
	{
		return;
	}
	if (!canUseMagicByState(m, kind == nkPlayer))
	{
		return;
	}
	if (level < 1)
	{
		level = 1;
	}
	else if (level > MAGIC_MAX_LEVEL)
	{
		level = MAGIC_MAX_LEVEL;
	}
	int launcher = getLauncherKindForNPC(*this);
	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	int damage = getPrimaryDamageForMagic(m, level, self);
	Magic::addEffect(m, self, position, dest, level, damage, getEvade(), launcher, target);
}

void NPC::addBody(UTime millisecondsToRemove)
{
	if (noAddBody)
	{
		return;
	}
	if (bodyIni != "")
	{
		auto body = gm->objectManager->addObject(bodyIni, position.x, position.y, direction, offset);
		if (body != nullptr && millisecondsToRemove > 0)
		{
			body->millisecondsToRemove = millisecondsToRemove;
		}
	}
}

std::vector<Point> NPC::getStepPositions() const
{
	return actionManager->getStepPositions();
}

void NPC::setPosition(Point newPos, bool forceToStand)
{
	if (forceToStand)
	{
		beginStand();
		clearActionPathTilePositions();
	}
		
	if (position == newPos)
	{
		return;
	}
	
	gm->map->deleteNPCFromDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));

	position = newPos;
	if (isVisibleByVariable && !scriptHidden)
	{
		gm->map->addNPCToDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
	}
}

void NPC::setOffset(PointEx newOffset)
{
	offset = newOffset;
}

PointEx NPC::getDrawOffset() const
{
	if (!magicForcedMove.active)
	{
		return offset;
	}
	return offset + magicForcedMove.drawOffset;
}

void NPC::removeFromDataMap()
{
	if (gm == nullptr || gm->map == nullptr)
	{
		return;
	}
	
	std::shared_ptr<NPC> self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	if (self == nullptr)
	{
		return;
	}
	
	gm->map->deleteNPCFromDataMap(position, self);
	
	std::vector<Point> stepPositions = getStepPositions();
	for (const Point& stepPos : stepPositions)
	{
		gm->map->deleteStepFromDataMap(stepPos, self);
	}
	if (resumingMove)
	{
		for (const Point& stepPos : savedStepPositions)
		{
			gm->map->deleteStepFromDataMap(stepPos, self);
		}
	}
}

void NPC::addToDataMap()
{
	if (gm == nullptr || gm->map == nullptr)
	{
		return;
	}

	std::shared_ptr<NPC> self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	if (self == nullptr)
	{
		return;
	}

	gm->map->deleteNPCFromDataMap(position, self);
	gm->map->addNPCToDataMap(position, self);

	std::vector<Point> stepPositions = getStepPositions();
	for (const Point& stepPos : stepPositions)
	{
		gm->map->deleteStepFromDataMap(stepPos, self);
		gm->map->addStepToDataMap(stepPos, self);
	}
}

void NPC::updateVisibleByVariable()
{
	bool nextVisibleByVariable = true;
	if (!visibleVariableName.empty() && gm != nullptr)
	{
		int value = gm->varList.getInteger(visibleVariableName);
		if (visibleVariableName[0] != '$')
		{
			value = std::max(value, gm->varList.getInteger("$" + visibleVariableName));
		}
		nextVisibleByVariable = value >= visibleVariableValue;
	}

	if (nextVisibleByVariable == isVisibleByVariable)
	{
		return;
	}

	isVisibleByVariable = nextVisibleByVariable;
	if (isVisibleByVariable)
	{
		if (scriptHidden || isHiding())
		{
			return;
		}
		addToDataMap();
	}
	else
	{
		selecting = false;
		removeFromDataMap();
		clearCombatTargetMemory();
	}
}

void NPC::setScriptHidden(bool hidden)
{
	if (scriptHidden == hidden)
	{
		return;
	}

	scriptHidden = hidden;
	if (scriptHidden)
	{
		selecting = false;
		removeFromDataMap();
		clearCombatTargetMemory();
		return;
	}

	if (isVisibleByVariable && !isHiding())
	{
		addToDataMap();
	}
}

bool NPC::isHiddenByCarryMagic() const
{
	auto effect = hiddenByCarryEffect.lock();
	return effect != nullptr
		&& effect->magic.hideUserWhenCarry > 0
		&& effect->carryUserActive
		&& effect->doing != ekHiding
		&& !effect->vanishing
		&& !(effect->result & erLifeExhaust);
}

bool NPC::isTransporting() const
{
	auto effect = transportEffect.lock();
	return effect != nullptr && effect->getMoveKind() == mmkTransport;
}

void NPC::applyTransportEffect(std::shared_ptr<Effect> effect)
{
	if (effect == nullptr)
	{
		return;
	}

	transportEffect = effect;
	selecting = false;
	stopMovement();
}

void NPC::clearTransportEffect(const Effect* effect)
{
	if (effect == nullptr)
	{
		transportEffect.reset();
		return;
	}

	auto currentEffect = transportEffect.lock();
	if (currentEffect == nullptr || currentEffect.get() == effect)
	{
		transportEffect.reset();
	}
}

void NPC::setHiddenByCarryMagic(std::shared_ptr<Effect> effect)
{
	hiddenByCarryEffect = effect;
}

void NPC::clearHiddenByCarryMagic(const Effect* effect)
{
	if (effect == nullptr)
	{
		hiddenByCarryEffect.reset();
		return;
	}

	auto currentEffect = hiddenByCarryEffect.lock();
	if (currentEffect == nullptr || currentEffect.get() == effect)
	{
		hiddenByCarryEffect.reset();
	}
}

void NPC::clearStep()
{
	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	if (resumingMove)
	{
		for (const Point& pos : savedStepPositions)
		{
			gm->map->deleteStepFromDataMap(pos, self);
		}
	}
	if (stepList.size() > 0)
	{
		if ((isWalking() || isRunning()) && stepState == ssOut)
		{
			gm->map->deleteStepFromDataMap(stepList[0], self);
		}
		else if ((isWalking() || isRunning()) && stepState == ssIn)
		{
			std::vector<Point> stepPositions = getStepPositions();
			for (const Point& pos : stepPositions)
			{
				gm->map->deleteStepFromDataMap(pos, self);
			}
		}
		else if (isJumping() && jumpState != jsDown)
		{
			gm->map->deleteStepFromDataMap(stepList[0], self);
		}
	}
}

void NPC::cancelMoveResumeState()
{
	resumingMove = false;
	previousMoveAction = NPCActionType::acStand;
	savedDirection = 0;
	hurtBeginStepTime = 0;
	savedStepState = ssOut;
	savedStepPositions.clear();
}

void NPC::beginStand()
{
	actionManager->changeAction(acStand);
}

void NPC::beginWalk(Point dest)
{
	if ((!canDoAction(acWalk) && !canDoAction(acAWalk)) || immobilized || petrified)
	{
		return;
	}

	if (lastPathFindFailTime > 0 && getTime() - lastPathFindFailTime < NPC_PATH_FIND_FAIL_COOLDOWN)
	{
		return;
	}

	auto tempList = findPathByType(dest);
	if (tempList.size() > 0)
	{	
		if (!canEnterMoveStep(tempList[0]))
		{
			lastPathFindFailTime = getTime();
			beginStand();
			return;
		}
		stepList = tempList;
		direction = getDirection(stepList[0]);
		
		actionManager->changeAction(acWalk);
	}
	else
	{
		lastPathFindFailTime = getTime();
	}
}

void NPC::beginHurt(Point dest)
{
	if (immobilized || petrified)
	{
		GameLog::write("immobilized or petrified, not hurt %s, %d, %d", name.c_str(), immobilized, petrified);
		return;
	}
	beginHurt();
	if (!isHurting())
	{
		return;
	}
	direction = getDirection(dest);
}

void NPC::beginHurt()
{
	if (!canDoAction(acHurt) || immobilized || petrified)
	{
		return;
	}
	destGE.reset();

	if (isWalking() || isRunning())
	{
		resumingMove = true;
		previousMoveAction = nowAction;
		savedDirection = direction;
		hurtBeginStepTime = getTime();
		savedStepState = stepState;
		savedStepPositions = getStepPositions();
	}

	bool canTransition = actionManager->getCurrentAction() != nullptr
		&& actionManager->getCurrentAction()->canTransitionTo(acHurt);
	if (canTransition && actionPlan.isActive())
	{
		actionPlan.reset();
	}

	actionManager->changeAction(acHurt);
}

void NPC::handleDeath()
{
	if (deathScript != "")
	{
		beginDieScript();
	}
	else
	{
		beginDie();
	}
}

void NPC::beginDieScript()
{
	beginDie();
	if (deathScript != "")
	{
		result |= erRunDeathScript;
	}
}

void NPC::beginDie()
{
	if (isDying() || isHiding())
	{
		return;
	}
	if (!canDoAction(acDeath))
	{
		return;
	}

	if (auto summonEffect = summonedByMagicEffect.lock())
	{
		summonedByMagicEffect.reset();
		auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
		summonEffect->detachSummonedNPCAfterDeath(self);
	}

	if (shieldLife > 0)
	{
		shieldLife = 0;
		if (auto shield = shieldEffect.lock())
		{
			if (!shield->vanishing)
			{
				shield->vanishing = true;
				shield->beginExplode(shield->position);
			}
		}
		shieldEffect.reset();
	}
	for (auto it = shieldEffects.begin(); it != shieldEffects.end(); ++it)
	{
		if (auto shield = it->lock())
		{
			if (!shield->vanishing)
			{
				shield->vanishing = true;
				shield->beginExplode(shield->position);
			}
		}
	}
	shieldEffects.clear();

	if (gm != nullptr && gm->npcManager != nullptr)
	{
		auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
		gm->npcManager->addDeadNPC(self);
	}
	addBuyIniStringGoodsToPlayer(this);
	triggerMagicWhenDeath();
	if (gm != nullptr && gm->player != nullptr)
	{
		auto controlled = gm->player->getControlledCharacter();
		if (controlled != nullptr && controlled.get() == this)
		{
			gm->player->endControlCharacter();
		}
	}
	clearCombatTargetMemory();
	isBodyIniAdded = 0;
	if (reviveMilliseconds > 0)
	{
		leftMillisecondsToRevive = reviveMilliseconds;
	}
	actionManager->forceChangeAction(acDeath);
}

bool NPC::updateReviveCountdown(UTime frameTime)
{
	if (leftMillisecondsToRevive == 0)
	{
		return false;
	}

	if (frameTime >= leftMillisecondsToRevive)
	{
		reviveFromDeath();
		return true;
	}

	leftMillisecondsToRevive -= frameTime;
	return isHiding();
}

void NPC::reviveFromDeath()
{
	leftMillisecondsToRevive = 0;
	isBodyIniAdded = 0;
	result &= ~erLifeExhaust;

	int maxLife = getLifeMax();
	life = maxLife;
	displayLifePercent = maxLife > 0 ? 1.0f : 0.0f;
	useSpecialDeath = false;
	specialDeathAction = "";
	noAddBody = false;
	selecting = false;
	keepDistanceCharacterWhenFriendDeath.reset();
	clearCombatTargetMemory();
	actionPlan.reset();

	actionManager->resetActionIgnoringTransitions(acStand);
	if (isVisibleByVariable && !scriptHidden)
	{
		addToDataMap();
	}
}

bool NPC::hasActiveSelfMagic(int specialKind) const
{
	for (const auto& weakEffect : shieldEffects)
	{
		if (auto effect = weakEffect.lock())
		{
			if (effect->vanishing || (effect->doing != ekFlying && effect->doing != ekExploding && effect->doing != ekHiding))
			{
				continue;
			}
			int effectLevel = effect->level;
			if (effectLevel < 1) effectLevel = 1;
			if (effectLevel > MAGIC_MAX_LEVEL) effectLevel = MAGIC_MAX_LEVEL;
			const auto& levelInfo = effect->magic.level[effectLevel];
			if (levelInfo.moveKind == mmkSelf && levelInfo.specialKind == specialKind)
			{
				return true;
			}
		}
	}
	return false;
}

void NPC::clearFrozenState()
{
	frozen = false;
	frozenLastTime = 0;
	frozenVisualEffect = true;
}

void NPC::clearPoisonedState()
{
	poisoned = false;
	poisonedLastTime = 0;
	poisonedDamageTimer = 0;
	poisonedVisualEffect = true;
	poisonedBy.reset();
	poisonedByCharacterName.clear();
}

void NPC::clearPetrifiedState()
{
	petrified = false;
	petrifiedLastTime = 0;
	petrifiedVisualEffect = true;
}

void NPC::clearImmobilizedState()
{
	immobilized = false;
	immobilizedLastTime = 0;
	immobilizedVisualEffect = true;
}

void NPC::clearAbnormalState()
{
	clearFrozenState();
	clearPoisonedState();
	clearPetrifiedState();
	clearImmobilizedState();
}

void NPC::rememberPoisonSource(std::shared_ptr<GameElement> source)
{
	poisonedBy = source;
	poisonedByCharacterName = getCharacterNameForStateSource(source);
}

void NPC::awardDefeatedNpcExperience(std::shared_ptr<Effect> effect)
{
	if (gm == nullptr || gm->player == nullptr)
	{
		return;
	}

	const ResourceManifest& manifest =
		ResourceManager::instance().getActiveManifest();
	const int baseExperience = calculateDefeatedNpcBaseExperience(
		manifest,
		gm->player->level,
		level,
		exp,
		expBonus);
	const double scaledExperience = scaleAutomaticExperience(
		baseExperience,
		manifest.resolvedExperienceMultiplier());
	gm->player->addExp(roundAutomaticExperience(scaledExperience));
	if (effect != nullptr)
	{
		gm->magicManager.addKillExp(effect, scaledExperience);
	}
}

void NPC::rewardPoisonKillExperience()
{
	if (gm == nullptr || gm->player == nullptr)
	{
		return;
	}

	if (auto poisoner = poisonedBy.lock())
	{
		if (std::dynamic_pointer_cast<Player>(poisoner) != nullptr)
		{
			awardDefeatedNpcExperience(nullptr);
		}
		return;
	}

	if (poisonedByCharacterName.empty())
	{
		return;
	}
	if (sameCharacterName(poisonedByCharacterName, gm->player->npcName)
		|| sameCharacterName(poisonedByCharacterName, gm->player->name)
		|| sameCharacterName(poisonedByCharacterName, "player"))
	{
		awardDefeatedNpcExperience(nullptr);
	}
}

bool NPC::applyPreDamageMagicStatus(const Effect& effect, int effectLevel)
{
	effectLevel = clampMagicLevelForNpc(effectLevel);
	const MagicLevel& levelInfo = effect.magic.level[effectLevel];
	bool handledSpecialEffect = false;
	bool showSpecialKindVisualEffect = effect.magic.noSpecialKindEffect <= 0;
	if (!petrified && levelInfo.specialKind == mskFreeze && !frozen)
	{
		UTime newFrozenTime = effect.magic.getSpecialKindDurationMilliseconds(effectLevel);
		frozenVisualEffect = showSpecialKindVisualEffect;
		frozenLastTime = newFrozenTime;
		frozen = true;
		handledSpecialEffect = true;
	}
	else if (!petrified && levelInfo.specialKind == mskImmobilize && !immobilized)
	{
		immobilized = true;
		immobilizedLastTime = effect.magic.getSpecialKindDurationMilliseconds(effectLevel);
		immobilizedVisualEffect = showSpecialKindVisualEffect;
		handledSpecialEffect = true;
	}
	else if (!petrified && levelInfo.specialKind == mskPoison && !poisoned)
	{
		UTime newPoisonedTime = effect.magic.getSpecialKindDurationMilliseconds(effectLevel);
		poisonedLastTime = newPoisonedTime;
		poisoned = true;
		poisonedVisualEffect = showSpecialKindVisualEffect;
		handledSpecialEffect = true;
		if (auto userPtr = effect.user.lock(); userPtr != nullptr)
		{
			bool shouldRememberPoisonSource = kind == nkPlayer
				? effect.launcherKind == lkEnemy
				: (effect.launcherKind == lkSelf || effect.launcherKind == lkFriend);
			if (shouldRememberPoisonSource)
			{
				rememberPoisonSource(userPtr);
			}
		}
	}
	else if (levelInfo.specialKind == mskPetrify && !petrified)
	{
		UTime newPetrifiedTime = effect.magic.getSpecialKindDurationMilliseconds(effectLevel);
		petrified = true;
		petrifiedLastTime = newPetrifiedTime;
		petrifiedVisualEffect = showSpecialKindVisualEffect;
		clearFrozenState();
		clearImmobilizedState();
		handledSpecialEffect = true;
	}
	applyAdditionalAttackEffect(effect, effectLevel);
	return handledSpecialEffect;
}

void NPC::applyAdditionalAttackEffect(const Effect& effect, int effectLevel)
{
	if (effect.additionalEffect == maeNone)
	{
		return;
	}
	auto userPtr = effect.user.lock();
	auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
	int seconds = caster != nullptr ? caster->level / 10 + 1 : effectLevel + 1;
	if (seconds < 1)
	{
		seconds = 1;
	}
	UTime duration = static_cast<UTime>(seconds * 1000);
	bool showSpecialKindVisualEffect = effect.magic.noSpecialKindEffect <= 0;
	switch (effect.additionalEffect)
	{
	case maeFrozen:
		if (!petrified && !frozen)
		{
			frozenLastTime = duration;
			frozen = true;
			frozenVisualEffect = showSpecialKindVisualEffect;
		}
		break;
	case maePoison:
		if (!petrified && !poisoned)
		{
			poisonedLastTime = duration;
			poisoned = true;
			poisonedVisualEffect = showSpecialKindVisualEffect;
			if (userPtr != nullptr && (effect.launcherKind == lkSelf || effect.launcherKind == lkFriend))
			{
				rememberPoisonSource(userPtr);
			}
		}
		break;
	case maePetrified:
		if (!petrified)
		{
			petrified = true;
			petrifiedLastTime = duration;
			petrifiedVisualEffect = showSpecialKindVisualEffect;
			clearFrozenState();
			clearImmobilizedState();
		}
		break;
	default:
		break;
	}
}

void NPC::beginAttack(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canDoAction(acAttack) || immobilized || petrified)
	{
		return;
	}
	if (!canActToward(dest, getAttackDirectionCount()))
	{
		return;
	}
	destGE = target;
	attackDest = dest;
	attackReleaseMode = (target == nullptr) ? armGroundTarget : armLockedRelease;
	direction = getDirection(dest);
	
	actionManager->changeAction(acAttack);
}

void NPC::beginSpecial()
{
	if (!canDoAction(acSpecial) || immobilized || petrified)
	{
		return;
	 }
	
	actionManager->changeAction(acSpecial);
}

void NPC::beginSit()
{
	if (!canDoAction(acSit) || immobilized || petrified)
	{
		return;
	}
	
	actionManager->changeAction(acSit);
}

void NPC::beginMagic(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canDoAction(acMagic) || immobilized || petrified)
	{
		return;
	}
	if (!canActToward(dest, getMagicActionDirectionCount(preparedMagicAction)))
	{
		clearPreparedMagicAction();
		return;
	}
	destGE = target;
	direction = getDirection(dest);
	
	auto previousAction = actionManager->getCurrentAction();
	actionManager->changeAction(acMagic);
	if (actionManager->getCurrentAction() == previousAction)
	{
		clearPreparedMagicAction();
	}
}

void NPC::beginRun(Point dest)
{
	if ((!canDoAction(acRun) && !canDoAction(acARun)) || immobilized || petrified)
	{
		return;
	}

	if (lastPathFindFailTime > 0 && getTime() - lastPathFindFailTime < NPC_PATH_FIND_FAIL_COOLDOWN)
	{
		return;
	}

	auto tempList = findPathByType(dest);
	if (tempList.size() > 0)
	{
		if (!canEnterMoveStep(tempList[0]))
		{
			lastPathFindFailTime = getTime();
			beginStand();
			return;
		}
		stepList = tempList;
		direction = getDirection(stepList[0]);
		
		actionManager->changeAction(acRun);
	}
	else
	{
		lastPathFindFailTime = getTime();
	}
}

RadiusMoveResult NPC::beginRadiusStep(Point dest, int radius, bool findNearDir)
{
	if ((!canDoAction(acWalk) && !canDoAction(acAWalk)) || immobilized || petrified)
	{
		return rmrFailed;
	}
	if (gm->map->calDistance(position, dest) <= radius)
	{
		stopMovement();
		return rmrAlreadyInRadius;
	}
	if (lastPathFindFailTime > 0 && getTime() - lastPathFindFailTime < NPC_PATH_FIND_FAIL_COOLDOWN)
	{
		return rmrFailed;
	}
	if (findNearDir)
	{
		int stepCount = gm->map->calDistance(position, dest) - radius;
		stepCount = stepCount > NPC_STEP_MAX_COUNT ? NPC_STEP_MAX_COUNT : stepCount;
		auto tempList = gm->map->traceTowardTarget(position, dest, stepCount, getMoveDirectionCount());
		if (tempList.size() > 0)
		{
			stepList = tempList;
			direction = getDirection(stepList[0]);
			actionManager->changeAction(acWalk);
			return rmrMoved;
		}
	}
	else
	{
		direction = getDirection(dest);
		if (canMoveInDirection(direction, getMoveDirectionCount()) && gm->map->canWalkDirectlyTo(position, direction))
		{
			stepList.clear();
			stepList.push_back(gm->map->getSubPoint(position, direction));
			actionManager->changeAction(acWalk);
			return rmrMoved;
		}
		else
		{
			beginStand();
		}
	}

	lastPathFindFailTime = getTime();
	return rmrFailed;
}

bool NPC::changeRadiusStep(Point dest, int radius, bool findNearDir)
{
	if ((!canDoAction(acWalk) && !canDoAction(acAWalk)) || immobilized || petrified)
	{
		return false;
	}
	if (findNearDir)
	{
		auto tempList = gm->map->traceTowardTarget(position, dest, radius, getMoveDirectionCount());
		if (tempList.size() > 0)
		{
			stepList = tempList;
			direction = getDirection(stepList[0]);
			actionManager->changeAction(acWalk);
			return true;
		}
	}
	else
	{
		direction = getDirection(dest);
		if (canMoveInDirection(direction, getMoveDirectionCount()) && gm->map->canWalkDirectlyTo(position, direction))
		{
			stepList.clear();
			stepList.push_back(gm->map->getSubPoint(position, direction));
			actionManager->changeAction(acWalk);
			return true;
		}
		else
		{
			beginStand();
		}
	}
	return false;
}

RadiusMoveResult NPC::beginRadiusMove(Point dest, int radius, bool isRun)
{
	auto action = isRun ? acRun : acWalk;
	if (!canDoAction(action))
	{
		return rmrFailed;
	}

	bool retargetCurrentMove = actionManager->isInAction(action);
	if (retargetCurrentMove && stepState == ssIn && !processingStepIn)
	{
		beginStand();
		retargetCurrentMove = false;
	}
	bool retargetActiveStep = retargetCurrentMove && stepState == ssOut && !processingStepIn;
	if ((isWalking() || isRunning()) && !retargetCurrentMove)
	{
		beginStand();
	}

	if (gm->map->calDistance(position, dest) <= radius)
	{
		stopMovement();
		return rmrAlreadyInRadius;
	}

	if (lastPathFindFailTime > 0 && getTime() - lastPathFindFailTime < NPC_PATH_FIND_FAIL_COOLDOWN)
	{
		return rmrFailed;
	}

	auto tempList = gm->map->getRadiusPath(position, dest, radius, getMoveDirectionCount());
	if (tempList.size() > 0)
	{
		auto currentAction = actionManager->getCurrentAction();
		auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
		std::vector<Point> oldStepPositions;
		if (retargetActiveStep && currentAction != nullptr)
		{
			oldStepPositions = currentAction->getStepPositions();
		}
		if (retargetActiveStep && self != nullptr)
		{
			for (const Point& pos : oldStepPositions)
			{
				gm->map->deleteStepFromDataMap(pos, self);
			}
		}
		stepList = tempList;
		direction = getDirection(stepList[0]);
		if (retargetCurrentMove)
		{
			if (retargetActiveStep && currentAction)
			{
				currentAction->retarget();
			}
		}
		else
		{
			actionManager->changeAction(action);
		}
		return rmrMoved;
	}
	else
	{
		lastPathFindFailTime = getTime();
	}

	return rmrFailed;
}

RadiusMoveResult NPC::beginRadiusWalk(Point dest, int radius)
{
	return beginRadiusMove(dest, radius, false);
}

RadiusMoveResult NPC::beginRadiusRun(Point dest, int radius)
{
	return beginRadiusMove(dest, radius, true);
}

RadiusMoveResult NPC::changeRadiusMove(Point dest, int radius, bool isRun, bool dontStandWhenFailed)
{
	if (isWalking() || isRunning())
	{ 
		if (isRun == isRunning() && stepState == ssIn && !processingStepIn)
		{
			beginStand();
			return isRun ? beginRadiusRun(dest, radius) : beginRadiusWalk(dest, radius);
		}
		if (isRun != isRunning())
		{
			beginStand();
			RadiusMoveResult started = rmrFailed;
			if (!isRun)
			{
				started = beginRadiusWalk(dest, radius);
			}
			else
			{
				started = beginRadiusRun(dest, radius);
			}
			if (started == rmrAlreadyInRadius)
			{
				return rmrAlreadyInRadius;
			}
			if (started != rmrMoved && dontStandWhenFailed && !usePathFinder())
			{
				return changeRadiusStep(dest, radius, false) ? rmrMoved : rmrFailed;
			}
			return started;
		}
		else
		{
			if (gm->map->calDistance(position, dest) <= radius)
			{
				stopMovement();
				return rmrAlreadyInRadius;
			}
			if (lastPathFindFailTime > 0 && getTime() - lastPathFindFailTime < NPC_PATH_FIND_FAIL_COOLDOWN)
			{
				return rmrFailed;
			}
			auto tempList = gm->map->getRadiusPath(position, dest, radius, getMoveDirectionCount());
			if (tempList.size() > 0)
			{
				auto currentAction = actionManager->getCurrentAction();
				auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
				bool retargetActiveStep = stepState == ssOut && !processingStepIn;
				std::vector<Point> oldStepPositions;
				if (retargetActiveStep && currentAction != nullptr)
				{
					oldStepPositions = currentAction->getStepPositions();
				}
				if (retargetActiveStep && self != nullptr)
				{
					for (const Point& pos : oldStepPositions)
					{
						gm->map->deleteStepFromDataMap(pos, self);
					}
				}
				stepList = tempList;
				direction = getDirection(stepList[0]);
				if (retargetActiveStep && currentAction)
				{
					currentAction->retarget();
				}
				return rmrMoved;
			}
			lastPathFindFailTime = getTime();
			if (!dontStandWhenFailed)
			{
				beginStand();
			}
			else if (!usePathFinder())
			{
				return changeRadiusStep(dest, radius, false) ? rmrMoved : rmrFailed;
			}
			return rmrFailed;
		}
	}
	else
	{
		RadiusMoveResult started = rmrFailed;
		if (!isRun)
		{
			started = beginRadiusWalk(dest, radius);
		}
		else
		{
			started = beginRadiusRun(dest, radius);
		}
		if (started == rmrAlreadyInRadius)
		{
			return rmrAlreadyInRadius;
		}
		if (started != rmrMoved)
		{
			if (dontStandWhenFailed && !usePathFinder())
			{
				return changeRadiusStep(dest, radius, false) ? rmrMoved : rmrFailed;
			}
			else if (!dontStandWhenFailed)
			{
				beginStand();
			}
		}
		return started;
	}
}

bool NPC::isFollower()
{
	if (kind == nkPartner)
	{
		return !isPartnerBlockingPlayer;
	}
	else if (followNPC != "")
	{
		auto fnpc = gm->npcManager->findNPC(followNPC);

		if (fnpc.size() > 0)
		{
			return true;
		}
		else
		{
			followNPC = "";
		}
	}

	return false;
}

bool NPC::isFollowAttack(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return false;
	}
	if ((kind == nkBattle) && (npc->kind == nkBattle || npc->kind == nkPlayer))
	{
		if ((npc->kind == nkBattle && relation != npc->relation) || (npc->kind == nkPlayer && relation != nrFriendly))
		{
			return true;
		}
	}
	return false;
}

void NPC::beginFollowWalk(Point dest)
{
	int followRadius = kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
	RadiusMoveResult result = beginRadiusWalk(dest, followRadius);
	bool handled = result == rmrMoved || result == rmrAlreadyInRadius;
	if (!handled && !usePathFinder())
	{
		result = beginRadiusStep(dest, followRadius, false);
		handled = result == rmrMoved || result == rmrAlreadyInRadius;
	}
	if (!handled)
	{
		lastTimeTryingToFollow = getTime();
	}
}

void NPC::beginFollowRun(Point dest)
{
	int followRadius = kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
	RadiusMoveResult result = beginRadiusRun(dest, followRadius);
	bool handled = result == rmrMoved || result == rmrAlreadyInRadius;
	if (!handled && !usePathFinder())
	{
		result = beginRadiusStep(dest, followRadius, false);
		handled = result == rmrMoved || result == rmrAlreadyInRadius;
	}
	if (!handled)
	{
		lastTimeTryingToFollow = getTime();
	}
}

void NPC::beginFollowAttack(Point dest)
{
	RadiusMoveResult result = beginRadiusWalk(dest, NPC_FOLLOW_RADIUS);
	bool handled = result == rmrMoved || result == rmrAlreadyInRadius;
	if (!handled && !usePathFinder())
	{
		result = beginRadiusStep(dest, NPC_FOLLOW_RADIUS, false);
		handled = result == rmrMoved || result == rmrAlreadyInRadius;
	}
	if (result == rmrMoved && !isCurrentPathWithinCombatChaseLimit(dest))
	{
		actionPlan.reset();
		stopMovement();
		handled = true;
	}
	if (!handled)
	{
		lastTimeTryingToFollow = getTime();
	}
}

void NPC::changeFollowWalk(Point dest)
{
	int followRadius = kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
	RadiusMoveResult result = changeRadiusMove(dest, followRadius, false, true);
	if (result != rmrMoved && result != rmrAlreadyInRadius)
	{
		lastTimeTryingToFollow = getTime();
	}
}

void NPC::changeFollowRun(Point dest)
{
	int followRadius = kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
	RadiusMoveResult result = changeRadiusMove(dest, followRadius, true, true);
	if (result != rmrMoved && result != rmrAlreadyInRadius)
	{
		lastTimeTryingToFollow = getTime();
	}
}

void NPC::changeFollowAttack(Point dest)
{
	RadiusMoveResult result = changeRadiusMove(dest, NPC_FOLLOW_RADIUS, false, true);
	if (result == rmrAlreadyInRadius)
	{
		stepList.clear();
	}
	else if (result == rmrMoved)
	{
		if (!isCurrentPathWithinCombatChaseLimit(dest))
		{
			actionPlan.reset();
			stopMovement();
			stepList.clear();
			result = rmrAlreadyInRadius;
		}
	}
	if (result != rmrMoved && result != rmrAlreadyInRadius)
	{
		stepList.clear();
		lastTimeTryingToFollow = getTime();
	}
}

bool NPC::usePathFinder() const
{
	return usePathFinderForPathType(resolvePathType());
}

bool NPC::tryAttackOrStand(Point from, std::shared_ptr<GameElement> target)
{
	if (target && canAnyAttackOptionHitTarget(from))
	{
		auto readyOption = findReadyAttackOption(from);
		if (readyOption.has_value())
		{
			prepareImmediateAttackPlan(target, readyOption.value(), from);
		}
		beginAttack(from, target);
		if (!isAttacking())
		{
			actionPlan.reset();
		}
	}
	else
	{
		beginStand();
	}
	return target ? isAttacking() : isStanding();
}

bool NPC::executeRetreatMovement(Point retreatDest)
{
	int stepCount = gm->map->calDistance(position, retreatDest);
	stepCount = stepCount > NPC_STEP_MAX_COUNT ? NPC_STEP_MAX_COUNT : stepCount;
	auto tempList = gm->map->traceTowardTarget(position, retreatDest, stepCount, getMoveDirectionCount());
	if (tempList.size() > 0)
	{
		stepList = tempList;
		direction = getDirection(stepList[0]);
		actionManager->changeAction(acWalk);
		return true;
	}
	return false;
}

std::optional<Point> NPC::findRetreatDestination(Point from, int retreatDistance) const
{
	int effectiveRetreatDistance = retreatDistance > 0 ? retreatDistance : attackRadius;
	int curDistance = gm->map->calDistance(position, from);
	int stepsNeeded = effectiveRetreatDistance - curDistance;
	if (stepsNeeded <= 0)
	{
		return std::nullopt;
	}

	int awayDir = NPC::getDirection(position, from) + 4;
	if (awayDir > 7) awayDir -= 8;

	int candidateDirections[5];
	candidateDirections[0] = awayDir;
	candidateDirections[1] = (awayDir + 1) % 8;
	candidateDirections[2] = (awayDir + 7) % 8;
	candidateDirections[3] = (awayDir + 2) % 8;
	candidateDirections[4] = (awayDir + 6) % 8;

	Point retreatDest = position;
	int bestDistanceFromTarget = curDistance;

	for (int dirIndex = 0; dirIndex < 5; dirIndex++)
	{
		if (!canMoveInDirection(candidateDirections[dirIndex], getMoveDirectionCount()))
		{
			continue;
		}
		std::vector<Point> reachablePositions;
		Point currentPos = position;
		for (int step = 0; step < stepsNeeded; step++)
		{
			Point nextPos = gm->map->getSubPoint(currentPos, candidateDirections[dirIndex]);
			if (gm->map->canWalk(nextPos))
			{
				currentPos = nextPos;
				reachablePositions.push_back(currentPos);
			}
			else
			{
				break;
			}
		}

		for (int i = (int)reachablePositions.size() - 1; i >= 0; i--)
		{
			if (gm->map->canSee(reachablePositions[i], from))
			{
				int distanceFromTarget = gm->map->calDistance(reachablePositions[i], from);
				if (!isWithinCombatChaseLimit(from, reachablePositions[i]))
				{
					continue;
				}
				if (distanceFromTarget > bestDistanceFromTarget)
				{
					bestDistanceFromTarget = distanceFromTarget;
					retreatDest = reachablePositions[i];
				}
				break;
			}
		}
	}

	if (retreatDest == position)
	{
		return std::nullopt;
	}
	return retreatDest;
}

bool NPC::beginRetreatStep(Point from, std::shared_ptr<GameElement> target, int retreatDistance)
{
	if ((!canDoAction(acWalk) && !canDoAction(acAWalk)) || immobilized || petrified)
	{
		return false;
	}

	auto retreatDest = findRetreatDestination(from, retreatDistance);
	if (!retreatDest.has_value())
	{
		return tryAttackOrStand(from, target);
	}

	if (executeRetreatMovement(retreatDest.value()))
	{
		return true;
	}

	return tryAttackOrStand(from, target);
}

bool NPC::beginRetreatWalk(Point from, std::shared_ptr<GameElement> target, int retreatDistance)
{
	auto retreatDest = findRetreatDestination(from, retreatDistance);
	if (!retreatDest.has_value())
	{
		return tryAttackOrStand(from, target);
	}

	if (beginRadiusMove(retreatDest.value(), 0, false) == rmrMoved)
	{
		return true;
	}

	return tryAttackOrStand(from, target);
}

void NPC::drawChangeMagicHitVisuals(Point screenPosition, PointEx drawOffset)
{
	if (engine == nullptr)
	{
		return;
	}

	UTime now = getTime();
	int baseX = screenPosition.x + (int)round(drawOffset.x);
	int baseY = screenPosition.y + (int)round(drawOffset.y);

	for (auto& kv : changeMagicHitVisuals)
	{
		auto& visual = kv.second;
		if (visual.flyingImage == nullptr || visual.icons.empty())
		{
			continue;
		}
		UTime elapsed = now >= visual.beginTime ? now - visual.beginTime : 0;
		float elapsedSeconds = (float)elapsed / 1000.0f;
		float gap = getChangeMagicHitGap(visual.icons.size());
		for (size_t i = 0; i < visual.icons.size(); i++)
		{
			float angleDegrees = gap * (float)i + elapsedSeconds * (float)visual.angleSpeed;
			float radians = angleDegrees * (float)M_PI / 180.0f;
			visual.icons[i].direction = getChangeMagicHitIconDirection(radians);

			int offsetX = 0;
			int offsetY = 0;
			_shared_image image = IMP::loadImageForDirection(visual.flyingImage, visual.icons[i].direction, elapsed, &offsetX, &offsetY);
			if (image != nullptr)
			{
				int x = baseX + (int)round(std::cos(radians) * visual.radius) - offsetX;
				int y = baseY + (int)round(std::sin(radians) * visual.radius) - offsetY;
				engine->drawImage(image, x, y);
			}
		}
	}

	for (auto it = changeMagicHitVanishVisuals.begin(); it != changeMagicHitVanishVisuals.end();)
	{
		auto& visual = it->second;
		if (visual.vanishImage == nullptr || visual.icons.empty())
		{
			it = changeMagicHitVanishVisuals.erase(it);
			continue;
		}
		UTime animationTime = 0;
		UTime actionTime = IMP::getIMPImageActionTime(visual.vanishImage);
		bool removeVisual = false;
		float gap = getChangeMagicHitGap(visual.icons.size());
		for (size_t i = 0; i < visual.icons.size(); i++)
		{
			auto& icon = visual.icons[i];
			animationTime = now >= icon.vanishBeginTime ? now - icon.vanishBeginTime : 0;
			float elapsedSeconds = (float)icon.elapsedMilliseconds / 1000.0f;
			float angleDegrees = gap * (float)i + elapsedSeconds * (float)visual.angleSpeed;
			float radians = angleDegrees * (float)M_PI / 180.0f;

			int offsetX = 0;
			int offsetY = 0;
			_shared_image image = IMP::loadImageForDirection(visual.vanishImage, icon.direction, animationTime, &offsetX, &offsetY, true);
			if (image != nullptr)
			{
				int x = baseX + (int)round(std::cos(radians) * visual.radius) - offsetX;
				int y = baseY + (int)round(std::sin(radians) * visual.radius) - offsetY;
				engine->drawImage(image, x, y);
			}
			if (actionTime == 0 || animationTime >= actionTime)
			{
				removeVisual = true;
			}
		}
		if (removeVisual)
		{
			it = changeMagicHitVanishVisuals.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void NPC::showCriticalDamageTip(int damage)
{
	if (damage <= 0 || !isEnemy() || engine == nullptr)
	{
		return;
	}
	criticalDamageTipImage = engine->createText(
		u8"暴击 " + std::to_string(damage),
		CriticalDamageTipFontSize,
		CriticalDamageTipColor);
	criticalDamageTipBeginTime = getTime();
}

void NPC::drawCriticalDamageTip(Point screenPosition, PointEx drawOffset, int actionOffsetY)
{
	if (criticalDamageTipImage == nullptr || engine == nullptr)
	{
		return;
	}
	UTime now = getTime();
	UTime elapsed = now >= criticalDamageTipBeginTime ? now - criticalDamageTipBeginTime : 0;
	if (elapsed >= CriticalDamageTipDurationMilliseconds)
	{
		criticalDamageTipImage = nullptr;
		criticalDamageTipBeginTime = 0;
		return;
	}

	int width = 0;
	int height = 0;
	engine->getImageSize(criticalDamageTipImage, width, height);
	float progress = static_cast<float>(elapsed) / static_cast<float>(CriticalDamageTipDurationMilliseconds);
	int rise = static_cast<int>(std::round(progress * 20.0f));
	unsigned char alpha = static_cast<unsigned char>(std::round((1.0f - progress) * 255.0f));
	int x = screenPosition.x + static_cast<int>(std::round(drawOffset.x)) - width / 2;
	int y = screenPosition.y + static_cast<int>(std::round(drawOffset.y)) - actionOffsetY - 12 - height - rise;
	engine->setImageAlpha(criticalDamageTipImage, alpha);
	engine->drawImage(criticalDamageTipImage, x, y);
	engine->setImageAlpha(criticalDamageTipImage, 255);
}

void NPC::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	if (!isVisibleForRuntime() || isHiddenByCarryMagic())
	{
		return;
	}

	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	PointEx drawOffset = getDrawOffset();
	int offsetX = 0;
	int offsetY = 0;
	_shared_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY);
	if (selecting && relation == nrHostile)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 200, 0, 0, 220);
	}
	else if (selecting && (scriptFile != "" || scriptFileRight != ""))
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 200, 200, 0, 180);
	}
	else if (frozen && frozenVisualEffect && gm->global.feature.freezeVisualEffect)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithColor(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 80, 80, 255);
	}
	else if (poisoned && poisonedVisualEffect && gm->global.feature.poisonVisualEffect)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithColor(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 80, 255, 80);
	}
	else if (petrified && petrifiedVisualEffect && gm->global.feature.petrifyVisualEffect)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithColor(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 160, 160, 160);
	}
	else
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		ColorStyle::drawImage(engine, image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, colorStyle);
	}

	drawChangeMagicHitVisuals(pos, drawOffset);
	drawSignalTip(pos, drawOffset, offsetY);

	if (shouldDrawLifeBarKindRelation(
		kind,
		relation,
		gm->global.data.PartnerCombat))
	{
		int maxLife = getLifeMax();
		if (maxLife > 0 && nowAction != acDeath && nowAction != acHide)
		{
			float lifePercent = (float)life / (float)maxLife;
			if (lifePercent > 1.0f) lifePercent = 1.0f;
			if (lifePercent < 0.0f) lifePercent = 0.0f;

			const int barWidth = 40;
			const int barHeight = 4;
			const int barOffsetY = 8;

			int barX = pos.x + (int)round(drawOffset.x) - barWidth / 2;
			int barY = pos.y + (int)round(drawOffset.y) - offsetY - barOffsetY;

			uint8_t lifeR, lifeG, lifeB;
			if (relation == nrHostile)
			{
				lifeR = 180; lifeG = 20; lifeB = 20;
			}
			else if (relation == nrFriendly)
			{
				lifeR = 20; lifeG = 180; lifeB = 30;
			}
			else
			{
				lifeR = 40; lifeG = 40; lifeB = 220;
			}

			engine->fillRect(barX, barY, barWidth, barHeight, 0, 0, 0, 160);

			if (displayLifePercent > lifePercent)
			{
				int lagWidth = (int)(barWidth * displayLifePercent);
				int currentWidth = (int)(barWidth * lifePercent);
				int lagPartWidth = lagWidth - currentWidth;
				if (lagPartWidth > 0)
				{
					uint8_t lagR, lagG, lagB;
					if (relation == nrHostile)
					{
						lagR = 255; lagG = 120; lagB = 140;
					}
					else if (relation == nrFriendly)
					{
						lagR = 140; lagG = 255; lagB = 120;
					}
					else
					{
						lagR = 140; lagG = 140; lagB = 255;
					}
					engine->fillRect(barX + currentWidth, barY, lagPartWidth, barHeight, lagR, lagG, lagB, 180);
				}
			}

			int lifeWidth = (int)(barWidth * lifePercent);
			if (lifeWidth > 0)
			{
				engine->fillRect(barX, barY, lifeWidth, barHeight, lifeR, lifeG, lifeB, 220);
			}
		}
	}
	drawCriticalDamageTip(pos, drawOffset, offsetY);
}

void NPC::drawNPCAlpha(Point cenTile, Point cenScreen, PointEx coffset)
{
	if (!isVisibleForRuntime() || isHiddenByCarryMagic())
	{
		return;
	}

	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	PointEx drawOffset = getDrawOffset();
	int offsetX, offsetY;
	if (relation == nrHostile)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 200, 0, 0, 220);
		engine->setImageAlpha(image, 255);
	}
	else if (scriptFile != "" || scriptFileRight != "")
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, 200, 200, 0, 180);
		engine->setImageAlpha(image, 255);
	}
	else
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImage(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY);
		engine->setImageAlpha(image, 255);
	}
}

void NPC::resetSignalImage()
{
	signalImagePackage = nullptr;
	loadedSignalIndex = 0;
	signalImageLoadAttempted = false;
}

_shared_image NPC::getSignalImage()
{
	if (signalIndex <= 0)
	{
		return nullptr;
	}
	if (loadedSignalIndex != signalIndex)
	{
		resetSignalImage();
	}
	if (!signalImageLoadAttempted)
	{
		loadedSignalIndex = signalIndex;
		signalImageLoadAttempted = true;
		signalImagePackage = loadSignalImagePackage(signalIndex);
	}
	if (signalImagePackage == nullptr)
	{
		return nullptr;
	}
	return IMP::loadImageForTime(signalImagePackage, getTime());
}

void NPC::drawSignalTip(Point screenPosition, PointEx drawOffset, int actionOffsetY)
{
	if (!isSignalShow || signalIndex <= 0 || nowAction == acDeath || nowAction == acHide || engine == nullptr)
	{
		return;
	}

	std::string lowerSignalType = toLowerAsciiCopy(signalType);
	UTime nowTime = getTime();
	if (lowerSignalType == "t1" && ((nowTime / 400) % 2) != 0)
	{
		return;
	}

	_shared_image image = getSignalImage();
	if (image == nullptr)
	{
		return;
	}

	int width = 0;
	int height = 0;
	if (!engine->getImageSize(image, width, height) || width <= 0 || height <= 0)
	{
		return;
	}

	int x = screenPosition.x + (int)round(drawOffset.x) - width / 2;
	int y = screenPosition.y + (int)round(drawOffset.y) - actionOffsetY - height - 8;
	if (lowerSignalType == "t0")
	{
		y += (int)round(std::sin((double)nowTime / 120.0) * 3.0);
	}
	engine->drawImage(image, x, y);
}

void NPC::limitDir(int * d)
{
	if (d == nullptr)
	{
		return;
	}
	while (*d < 0)
	{
		*d += 8;
	}
	while (*d > 7)
	{
		*d -= 8;
	}
}

void NPC::limitDir()
{
	limitDir(&direction);
}

void NPC::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	transientSummonedNPC = false;
	
	if (ini == nullptr)
	{
		return;
	}

	//std::string section = "Init";
	getFrameTime();
	destGE.reset();
	npcName = ini->Get(section, "Name", "");
	showName = ini->Get(section, "ShowName", "");
	kind = ini->GetInteger(section, "Kind", nkNormal);
	npcIni = ini->Get(section, "NPCIni", "");
	sex = ini->GetInteger(section, "Sex", 0);
	direction = ini->GetInteger(section, "Dir", 0);
	position.x = ini->GetInteger(section, "MapX", 0);
	position.y = ini->GetInteger(section, "MapY", 0);
	//int dir = 0; //direction
	//int mapX = 0; //position.x
	//int mapY = 0; //position.y
	strollIntent = ini->GetInteger(section, "Action", nsiNone);
	walkSpeed = ini->GetInteger(section, "WalkSpeed", 1);
	if (walkSpeed == 0)
	{
		walkSpeed = 1;
	}
	standSpeed = ini->GetInteger(section, "StandSpeed", 0);
	std::string attackSpeedText = ini->Get(section, "AttackSpeed", ini->Get(section, "attackspeed", ""));
	hasAttackSpeedField = !attackSpeedText.empty();
	attackSpeed = ini->GetInteger(section, "AttackSpeed", ini->GetInteger(section, "attackspeed", 1));
	if (attackSpeed <= 0)
	{
		attackSpeed = 1;
	}
	idle = ini->GetInteger(section, "Idle", ini->GetInteger(section, "idle", 0));
	if (idle < 0)
	{
		idle = 0;
	}
	idledFrame = 0;
	aiType = ini->GetInteger(section, "AI_TYPE", ini->GetInteger(section, "AIType", 0));
	group = ini->GetInteger(section, "Group", 0);
	noAutoAttackPlayer = ini->GetInteger(section, "NoAutoAttackPlayer", 0);
	stopFindingTarget = ini->GetInteger(section, "StopFindingTarget", 0);
	pathFinder = ini->GetInteger(section, "PathFinder", pfSingle);
	isAIDisabled = readBooleanAlias(ini, section, "IsAIDisabled", "AIDisabled", false);
	dialogRadius = ini->GetInteger(section, "DialogRadius", 1);
	scriptFile = ini->Get(section, "ScriptFile", "");
	scriptFileRight = ini->Get(section, "ScriptFileRight", "");
	timerScriptFile = ini->Get(section, "TimerScriptFile", "");
	timerScriptInterval = readPositiveTime(ini, section, "TimerScriptInterval", DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL);
	timerScriptElapsed = 0;
	canInteractDirectly = ini->GetInteger(section, "CanInteractDirectly", 0);
	buyIniFile = ini->Get(section, "BuyIniFile", "");
	buyIniString = ini->Get(section, "BuyIniString", "");
	if (buyIniString.empty())
	{
		buyIniString = loadBuyIniStringFromFile(buyIniFile);
	}
	dropIni = ini->Get(section, "DropIni", "");
	noDropWhenDie = ini->GetInteger(section, "NoDropWhenDie", 0);
	keepAttackPosition.x = ini->GetInteger(section, "KeepAttackX", 0);
	keepAttackPosition.y = ini->GetInteger(section, "KeepAttackY", 0);
	reviveMilliseconds = readPositiveTime(ini, section, "ReviveMilliseconds", 0);
	leftMillisecondsToRevive = readPositiveTime(ini, section, "LeftMillisecondsToRevive", 0);
	lifeMilliseconds = readPositiveTime(ini, section, "LifeMilliseconds", 0);
	isBodyIniAdded = ini->GetInteger(section, "IsBodyIniAdded", 0);
	noAddBody = ini->GetInteger(section, "NoAddBody", ini->GetInteger(section, "IsNodAddBody", 0)) != 0;
	bool savedDeathInvoked = ini->GetBoolean(section, "IsDeathInvoked", false);
	bool savedDeathCompleted = ini->GetBoolean(section, "IsDeath", false);
	UTime savedDeathActionRemainingMilliseconds = std::min<UTime>(
		readPositiveTime(
			ini,
			section,
			"DeathActionRemainingMilliseconds",
			0),
		MaximumPersistedDeathActionMilliseconds);
	bool savedPendingDeathScript = ini->GetBoolean(section, "PendingDeathScript", false);
	bool savedUseSpecialDeath = ini->GetBoolean(section, "UseSpecialDeath", false);
	std::string savedSpecialDeathAction = ini->Get(section, "SpecialDeathAction", "");
	bool savedNoAddBody = noAddBody;
	visibleVariableName = ini->Get(section, "VisibleVariableName", "");
	visibleVariableValue = ini->GetInteger(section, "VisibleVariableValue", 0);
	isVisibleByVariable = true;
	updateVisibleByVariable();
	state = ini->GetInteger(section, "State", state);
	relation = ini->GetInteger(section, "Relation", nrFriendly);
	detachedEffectCaster = false;
	originalRelationBeforeOppositeChange = relation;
	changeToOppositeMilliseconds = 0;
	kindValue = ini->GetInteger(section, "KindValue", 0);
	kindValueMax = ini->GetInteger(section, "KindValueMax", 0);
	talkContent = ini->Get(section, "TalkContent", "");
	bagGoods = ini->Get(section, "BagGoods", "");
	steal = ini->GetInteger(section, "Steal", 0);
	eloquence = ini->GetInteger(section, "Eloquence", 0);
	leechcraft = ini->GetInteger(section, "Leechcraft", 0);
	autoRunScript = ini->GetInteger(section, "AutoRunScript", 0);
	hasAutoRunScriptField = hasIniKey(ini, section, "AutoRunScript");
	arm = ini->GetInteger(section, "Arm", 0);
	hasArmField = hasIniKey(ini, section, "Arm");
	evadeN = ini->GetInteger(section, "EvadeN", 0);
	hasEvadeNField = hasIniKey(ini, section, "EvadeN");
	gengu = ini->GetInteger(section, "Gengu", 0);
	hasGenguField = hasIniKey(ini, section, "Gengu");
	neixi = ini->GetInteger(section, "Neixi", 0);
	hasNeixiField = hasIniKey(ini, section, "Neixi");
	physique = ini->GetInteger(section, "Physique", 0);
	hasPhysiqueField = hasIniKey(ini, section, "Physique");
	isSignalShow = ini->GetInteger(section, "IsSignalShow", 0) != 0;
	signalIndex = ini->GetInteger(section, "SignalIndex", 0);
	signalType = ini->Get(section, "SignalType", "");
	resetSignalImage();
	const ResourceManifest& activeManifest =
		ResourceManager::instance().getActiveManifest();
	const bool usesExperienceBonus =
		activeManifest.resolvedDefeatedNpcExperienceMode() ==
		DefeatedNpcExperienceMode::LevelProductWithBonus;
	std::string expBonusText = usesExperienceBonus
		? ini->Get(section, "ExpBonus", "")
		: "";
	life = ini->GetInteger(section, "Life", 0);
	lifeMax = ini->GetInteger(section, "LifeMax", 0);
	thew = ini->GetInteger(section, "Thew", 0);
	thewMax = ini->GetInteger(section, "ThewMax", 0);
	mana = ini->GetInteger(section, "Mana", 0);
	manaMax = ini->GetInteger(section, "ManaMax", 0);
	attack = ini->GetInteger(section, "Attack", 0);
	attack2 = ini->GetInteger(section, "Attack2", 0);
	attack3 = ini->GetInteger(section, "Attack3", 0);
	defend = ini->GetInteger(section, "Defend", ini->GetInteger(section, "Defence", 0));
	defend2 = ini->GetInteger(section, "Defend2", 0);
	defend3 = ini->GetInteger(section, "Defend3", 0);
	evade = ini->GetInteger(section, "Evade", 0);
	duck = ini->GetInteger(section, "Duck", 0);
	dodgeBeginFrame = ini->GetInteger(section, "Dodge_BeginFrame", 0);
	hasDodgeBeginFrameField = hasIniKey(ini, section, "Dodge_BeginFrame");
	dodgeEndFrame = ini->GetInteger(section, "Dodge_EndFrame", 0);
	hasDodgeEndFrameField = hasIniKey(ini, section, "Dodge_EndFrame");
	exp = ini->GetInteger(section, "Exp", 0);
	hasExpBonusField = !expBonusText.empty();
	expBonus = usesExperienceBonus
		? ini->GetInteger(section, "ExpBonus", 0)
		: 0;

	levelUpExp = ini->GetInteger(section, "LevelUpExp", 0);
	canLevelUp = ini->GetInteger(section, "CanLevelUp", 0);
	level = ini->GetInteger(section, "Level", 0);
	npcLevelIni = ini->Get(section, "LevelIni", "");
	poisonedLastTime = readSecondsFieldAsMilliseconds(ini, section, "PoisonSeconds");
	poisoned = poisonedLastTime > 0;
	poisonedDamageTimer = 0;
	poisonedVisualEffect = readBooleanAlias(ini, section, "IsPoisionVisualEffect", "IsPoisonVisualEffect", true);
	poisonedByCharacterName = ini->Get(section, "PoisonByCharacterName", "");
	petrifiedLastTime = readSecondsFieldAsMilliseconds(ini, section, "PetrifiedSeconds");
	petrified = petrifiedLastTime > 0;
	petrifiedVisualEffect = ini->GetBoolean(section, "IsPetrifiedVisualEffect", true);
	frozenLastTime = readSecondsFieldAsMilliseconds(ini, section, "FrozenSeconds");
	frozen = frozenLastTime > 0;
	frozenVisualEffect = readBooleanAlias(ini, section, "IsFronzenVisualEffect", "IsFrozenVisualEffect", true);
	immobilizedLastTime = readSecondsFieldAsMilliseconds(ini, section, "ImmobilizedSeconds");
	immobilized = immobilizedLastTime > 0;
	immobilizedVisualEffect = ini->GetBoolean(section, "IsImmobilizedVisualEffect", true);
	invincible = ini->GetInteger(section, "Invincible", 0);
	loadLevel(npcLevelIni.empty() ? "level-npc.ini" : npcLevelIni);
	setFixedPos(ini->Get(section, "FixedPos", ""));
	currentFixedPosIndex = (size_t)ini->GetInteger(section, "CurrentFixedPosIndex", ini->GetInteger(section, "CurrPos", 0));
	if (currentFixedPosIndex >= fixedPathTilePositions.size())
	{
		currentFixedPosIndex = 0;
	}
	destinationMapPosition.x = ini->GetInteger(section, "DestinationMapPosX", 0);
	destinationMapPosition.y = ini->GetInteger(section, "DestinationMapPosY", 0);
	attackLevel = ini->GetInteger(section, "AttackLevel", 0);
	magicLevel = ini->GetInteger(section, "MagicLevel", 0);
	canEquip = ini->GetInteger(section, "CanEquip", 0);
	headEquip = ini->Get(section, "HeadEquip", "");
	neckEquip = ini->Get(section, "NeckEquip", "");
	bodyEquip = ini->Get(section, "BodyEquip", "");
	backEquip = ini->Get(section, "BackEquip", "");
	handEquip = ini->Get(section, "HandEquip", "");
	wristEquip = ini->Get(section, "WristEquip", "");
	footEquip = ini->Get(section, "FootEquip", "");
	backgroundTextureEquip = ini->Get(section, "BackgroundTextureEquip", "");
	updateEquipmentAttributes();
	if (level < 0 && gm != nullptr && gm->player != nullptr)
	{
		setPropToLevel(gm->player->level + level);
	}
	displayLifePercent = (getLifeMax() > 0) ? (float)life / (float)getLifeMax() : 0.0f;
	if (displayLifePercent > 1.0f) displayLifePercent = 1.0f;
	if (displayLifePercent < 0.0f) displayLifePercent = 0.0f;
	lum = ini->GetInteger(section, "Lum", nlNone);
	visionRadius = ini->GetInteger(section, "VisionRadius", 0);
	attackRadius = ini->GetInteger(section, "AttackRadius", 0);
	bodyIni = ini->Get(section, "BodyIni", "");
	flyIni = ini->Get(section, "FlyIni", "");
	flyIni2 = ini->Get(section, "FlyIni2", "");
	flyInis = ini->Get(section, "FlyInis", "");
	magicIni = ini->Get(section, "MagicIni", "");
	magicToUseWhenLifeLowFile = ini->Get(section, "MagicToUseWhenLifeLow", "");
	lifeLowPercent = ini->GetInteger(section, "LifeLowPercent", 20);
	if (lifeLowPercent <= 0)
	{
		lifeLowPercent = 20;
	}
	keepRadiusWhenLifeLow = ini->GetInteger(section, "KeepRadiusWhenLifeLow", 0);
	keepRadiusWhenFriendDeath = ini->GetInteger(section, "KeepRadiusWhenFriendDeath", 0);
	magicToUseWhenBeAttackedFile = ini->Get(section, "MagicToUseWhenBeAttacked", "");
	magicDirectionWhenBeAttacked = ini->GetInteger(section, "MagicDirectionWhenBeAttacked", 0);
	magicToUseWhenDeathFile = ini->Get(section, "MagicToUseWhenDeath", "");
	magicDirectionWhenDeath = ini->GetInteger(section, "MagicDirectionWhenDeath", 0);
	addMoveSpeedPercent = ini->GetInteger(section, "AddMoveSpeedPercent", 0);
	hurtPlayerInterval = readPositiveTime(ini, section, "HurtPlayerInterval", 0);
	hurtPlayerIntervalTimer = 0;
	hurtPlayerLife = ini->GetInteger(section, "HurtPlayerLife", 0);
	hurtPlayerRadius = ini->GetInteger(section, "HurtPlayerRadius", 1);
	if (hurtPlayerRadius < 0)
	{
		hurtPlayerRadius = 0;
	}
	deathScript = ini->Get(section, "DeathScript", "");
	npcMagic = gm->magicManager.loadAttackMagic(flyIni);
	npcMagic2 = gm->magicManager.loadAttackMagic(flyIni2);
	magicToUseWhenLifeLow = gm->magicManager.loadAttackMagic(magicToUseWhenLifeLowFile);
	magicToUseWhenBeAttacked = gm->magicManager.loadAttackMagic(magicToUseWhenBeAttackedFile);
	magicToUseWhenDeath = gm->magicManager.loadAttackMagic(magicToUseWhenDeathFile);
	rebuildAttackOptions();

	initRes(npcIni);
	walkTime = getTime() + engine->getRand(NPC_WALK_INTERVAL_RANGE * 2);
	if (savedDeathCompleted)
	{
		savedDeathInvoked = true;
	}
	// Legacy trilogy resources commonly leave Life/LifeMax empty for story,
	// ambient and non-combat NPCs. Zero life is therefore not a reliable death
	// marker; only the explicit persistence fields may restore a death state.
	if (savedDeathInvoked && !savedDeathCompleted)
	{
		actionManager->resetActionIgnoringTransitions(acDeath);
		noAddBody = savedNoAddBody;
		specialDeathAction = savedSpecialDeathAction;
		useSpecialDeath = false;
		if (savedUseSpecialDeath && !specialDeathAction.empty())
		{
			loadSpecialAction(specialDeathAction);
			useSpecialDeath = res.special.imagePackage != nullptr;
		}
		actionBeginTime = getTime();
		actionLastTime = savedDeathActionRemainingMilliseconds;
		if (savedPendingDeathScript)
		{
			result |= erRunDeathScript;
		}
	}
	else if (savedDeathInvoked)
	{
		actionManager->resetActionIgnoringTransitions(acHide);
		if (isBodyIniAdded == 0 || leftMillisecondsToRevive == 0)
		{
			result |= erLifeExhaust;
		}
		if (savedPendingDeathScript)
		{
			result |= erRunDeathScript;
		}
	}
	else
	{
		beginStand();
		if (leftMillisecondsToRevive > 0)
		{
			actionManager->resetActionIgnoringTransitions(acHide);
		}
	}

	if (gm != nullptr && gm->map != nullptr && gm->map->data != nullptr)
	{
		if (!gm->map->isInMap(position))
		{
			position = gm->map->clampToWalkable(position);
		}
	}

	if (name.compare("player") != 0)
	{
		name = "npc-" + npcName;
	}
}

void NPC::freeResource()
{
	clearTemporaryNpcRes(false);
	freeNPCAction(&scriptSpecialActionOverlayResource);
	scriptSpecialActionOverlayActive = false;
	scriptSpecialActionOverlaySupersededByAction = false;
	scriptSpecialActionOverlayElapsed = 0;
	scriptSpecialActionOverlayDuration = 0;
	freeNPCRes();
	originalResBeforeMorph.reset();
	temporaryNpcResFile.clear();
	resetSignalImage();
	equipmentAttributes.reset();
	resetEquipmentMagicEffectBonuses();
	clearHiddenByCarryMagic();
	equipmentLifeRestoreElapsedMilliseconds = 0;
	npcMagic = nullptr;
	npcMagic2 = nullptr;
	magicToUseWhenLifeLow = nullptr;
	magicToUseWhenBeAttacked = nullptr;
	magicToUseWhenDeath = nullptr;
	attackOptions.clear();
	attackAdditionalEffect = maeNone;
	clearBounceState();
	lastBounceBlockedByCharacter = false;
	lastBounceBlockedCharacterPosition = { 0, 0 };
	lastBounceEndPosition = { 0, 0 };
	clearMagicForcedMoveState();
	clearChangeMagicHitCounts();
	clearRangeSpeedUp();
	disableMoveMilliseconds = 0;
	disableSkillMilliseconds = 0;
	clearMagicRuntimeStates();
	actionPlan.reset();
	keepDistanceCharacterWhenFriendDeath.reset();
	hasLastUsedAttackOption = false;
	nextSelfBuffTime = 0;
	if (gm != nullptr)
	{
		gm->magicManager.tryCleanAttackMagic();
	}
	stepList.resize(0);
	cancelMoveResumeState();

	shieldEffects.clear();
	shieldEffect.reset();
	shieldLife = 0;
	shieldLastTime = 0;
	shieldBeginTime = 0;

	clearFrozenState();
	clearPoisonedState();
	clearCombatTargetMemory();
	partnerOwnerFollowPriorityActive = false;
	partnerOwnerFollowBestDistance = INT_MAX;
	partnerOwnerFollowLastProgressTime = 0;
	partnerOwnerFollowRetryTime = 0;
	clearPetrifiedState();
	clearImmobilizedState();
	invincible = 0;
}

void NPC::freeNPCRes()
{
	freeNPCRes(res);
}

void NPC::freeNPCRes(NPCRes& npcRes)
{
	freeNPCAction(&npcRes.stand);
	freeNPCAction(&npcRes.stand1);
	freeNPCAction(&npcRes.walk);
	freeNPCAction(&npcRes.run);
	freeNPCAction(&npcRes.jump);
	freeNPCAction(&npcRes.attack);
	freeNPCAction(&npcRes.attack1);
	freeNPCAction(&npcRes.attack2);
	freeNPCAction(&npcRes.magic);
	freeNPCAction(&npcRes.hurt);
	freeNPCAction(&npcRes.death);
	freeNPCAction(&npcRes.sit);
	freeNPCAction(&npcRes.special);
	freeNPCAction(&npcRes.astand);
	freeNPCAction(&npcRes.awalk);
	freeNPCAction(&npcRes.arun);
	freeNPCAction(&npcRes.ajump);
}

void NPC::freeNPCAction(NPCActionRes * act)
{
	if (act == nullptr)
	{
		return;
	}
	freeActionImage(act);
	act->imageFile = "";
	act->shadowFile = "";
	act->soundFile = "";
}

void NPC::freeActionImage(NPCActionRes * act)
{
	if (act == nullptr)
	{
		return;
	}
	act->imagePackage = nullptr;
	act->shadowPackage = nullptr;
}

void NPC::onUpdate()
{
	updateVisibleByVariable();
	auto ft = getFrameTime();
	if (!isVisibleByVariable)
	{
		if (scriptSpecialActionOverlayActive)
		{
			pauseScriptSpecialActionUnderlyingAction(ft);
		}
		else if (isDoingSpecialAction())
		{
			updateAction(ft);
			updateEventRunState();
		}
		return;
	}
	if (updateReviveCountdown(ft))
	{
		return;
	}

	if (lifeMilliseconds > 0)
	{
		if (ft >= lifeMilliseconds)
		{
			lifeMilliseconds = 0;
			handleDeath();
			return;
		}
		lifeMilliseconds -= ft;
	}

	if (shieldLife > 0)
	{
		if (getTime() - shieldBeginTime >= shieldLastTime)
		{
			shieldLife = 0;
			if (auto shield = shieldEffect.lock())
			{
				if (!shield->vanishing)
				{
					shield->vanishing = true;
					shield->beginExplode(shield->position);
				}
			}
			shieldEffect.reset();
		}
		else
		{
			shieldBeginTime += ft;
			if (shieldLastTime > ft) { shieldLastTime -= ft; } else { shieldLastTime = 0; }
		}
	}
	else if (auto shield = shieldEffect.lock())
	{
		shieldLife = 0;
		if (!shield->vanishing)
		{
			shield->vanishing = true;
			shield->beginExplode(shield->position);
		}
		shieldEffect.reset();
	}

	if (updateScriptSpecialActionOverlayForFrame(ft))
	{
		return;
	}

	updateEventRunState();

	if (hasDestinationMapPosition()
		&& isStanding()
		&& !isFollower()
		&& !fightState.get()
		&& !actionPlan.isActive()
		&& currentCombatTarget.expired()
		&& lastCombatTarget.expired())
	{
		tryMoveToDestinationMapPosition();
	}

	if (poisoned && nowAction != acDeath)
	{
		if (poisonedLastTime >= ft)
		{
			poisonedLastTime -= ft;
			poisonedDamageTimer += ft;
			if (poisonedDamageTimer >= 250)
			{
				poisonedDamageTimer -= 250;
				addLife(-10);
				if (life <= 0)
				{
					rewardPoisonKillExperience();
					handleDeath();
					clearPoisonedState();
					return;
				}
			}
		}
		else
		{
			clearPoisonedState();
		}
	}

	bool stateActionDone = false;
	bool movementLockedByMagic = disableMoveMilliseconds > 0 && nowAction != acDeath && (isWalking() || isRunning() || isJumping());
	updateActionLockTimers(ft);

	if (petrified && nowAction != acDeath)
	{
		clearFrozenState();
		clearImmobilizedState();
		if (petrifiedLastTime >= ft)
		{
			petrifiedLastTime -= ft;
			setTime(getTime() - ft);
			stateActionDone = true;
		}
		else
		{
			auto lastT = ft - petrifiedLastTime;
			setTime(getTime() - petrifiedLastTime);
			clearPetrifiedState();
			updateAction(lastT);
			stateActionDone = true;
		}
	}
	else if (immobilized && nowAction != acDeath)
	{
		if (immobilizedLastTime >= ft)
		{
			immobilizedLastTime -= ft;
			setTime(getTime() - ft);
			stateActionDone = true;
		}
		else
		{
			auto lastT = ft - immobilizedLastTime;
			setTime(getTime() - immobilizedLastTime);
			clearImmobilizedState();
			updateAction(lastT);
			stateActionDone = true;
		}
	}
	else if (movementLockedByMagic)
	{
		setTime(getTime() - ft);
		stateActionDone = true;
	}

	if (!stateActionDone)
	{
		if (frozen && nowAction != acDeath)
		{
			if (frozenLastTime >= ft)
			{
				frozenLastTime -= ft;
				setTime(getTime() - ft / 2);
				ft /= 2;
			}
			else
			{
				auto remainingFreezeTime = frozenLastTime;
				setTime(getTime() - remainingFreezeTime / 2);
				clearFrozenState();
				ft = ft - remainingFreezeTime / 2;
			}
		}
		else
		{
			clearFrozenState();
		}
		updateAction(ft);
	}

	if (isBouncing() && actionManager->getCurrentActionType() != acBounce)
	{
		updateBounceMovement(ft);
	}
	if (isMagicForcedMoving() && actionManager->getCurrentActionType() != acMagicForcedMove)
	{
		updateMagicForcedMovement(ft);
	}
	updateEquipmentLifeRestore(ft);

	if (hurtPlayerInterval > 0 && hurtPlayerLife != 0 && nowAction != acDeath && nowAction != acHide && gm != nullptr && gm->map != nullptr && gm->player != nullptr)
	{
		hurtPlayerIntervalTimer += ft;
		if (hurtPlayerIntervalTimer >= hurtPlayerInterval)
		{
			hurtPlayerIntervalTimer = 0;
			if (gm->map->calDistance(position, gm->player->getPosition()) <= hurtPlayerRadius)
			{
				gm->player->hurtLife(hurtPlayerLife);
			}
		}
	}

	if ((keepAttackPosition.x > 0 || keepAttackPosition.y > 0) && nowAction != acDeath && nowAction != acHide && isStanding())
	{
		beginAttack(keepAttackPosition, nullptr);
	}

	if (nowAction != acDeath && nowAction != acHide)
	{
		updateIdleFrame();
	}

	if (nowAction != acDeath && nowAction != acHide && timerScriptFile != "" && timerScriptInterval > 0 && gm != nullptr)
	{
		timerScriptElapsed += ft;
		if (timerScriptElapsed >= timerScriptInterval)
		{
			timerScriptElapsed -= timerScriptInterval;
			auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
			gm->runNPCScript(self, timerScriptFile, false);
			if (gm->npcManager == nullptr || !gm->npcManager->findNPC(self))
			{
				return;
			}
		}
	}
	
	if (kind != nkBattle && scriptFile.empty() && scriptFileRight.empty())
	{
		//设置无用人物检测范围到屏幕外
		rect.x = -rect.w - 100;
		rect.y = -rect.h - 100;
	}
	else if (kind == nkBattle && relation == nrFriendly && scriptFile.empty() && scriptFileRight.empty())
	{
		//设置无用人物检测范围到屏幕外
		rect.x = -rect.w - 100;
		rect.y = -rect.h - 100;
	}
	else
	{
		int w, h;
		engine->getWindowSize(w, h);
		Point cenScreen;
		cenScreen.x = (int)w / 2;
		cenScreen.y = (int)h / 2;
		int xscal, yscal;
		xscal = cenScreen.x / TILE_WIDTH + 3;
		yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
		int tileHeightScal = 10;
		Point cenTile = gm->camera->position;

		if (position.x >= cenTile.x - xscal && position.x < cenTile.x + xscal && position.y >= cenTile.y - yscal && position.y < cenTile.y + yscal + tileHeightScal)
		{
			PointEx posoffset;
			posoffset.x = (gm->camera->offset.x - offset.x);
			posoffset.y = (gm->camera->offset.y - offset.y);
			Point pos = Map::getTilePosition(position, cenTile, cenScreen, posoffset);

			int ox = 0, oy = 0, iw = 0, ih = 0;
			engine->getImageSize(getActionImage(&ox, &oy), iw, ih);
			rect.w = iw;
			rect.h = ih;
			rect.x = pos.x - ox;
			rect.y = pos.y - oy;
		}
		else
		{
			rect.x = -rect.w - 100;
			rect.y = -rect.h - 100;
		}
	}

	updateEventRunState();

	int maxLife = getLifeMax();
	if (maxLife > 0)
	{
		float actualLifePercent = (float)life / (float)maxLife;
		if (actualLifePercent > 1.0f) actualLifePercent = 1.0f;
		if (actualLifePercent < 0.0f) actualLifePercent = 0.0f;

		if (displayLifePercent > actualLifePercent)
		{
			// 血条渐变速度：每秒减少的百分比，值越大渐变越快
			float lagSpeed = 0.6f * (ft / 1000.0f);
			displayLifePercent -= lagSpeed;
			if (displayLifePercent < actualLifePercent)
			{
				displayLifePercent = actualLifePercent;
			}
		}
		else
		{
			displayLifePercent = actualLifePercent;
		}
	}
	else
	{
		displayLifePercent = 0.0f;
	}
}

void NPC::updateAction(UTime frameTime)
{
	bool wasFighting = fightState.get();
	fightState.update(frameTime);
	if (wasFighting && !fightState.get())
	{
		clearCombatTargetMemory();
	}
	
	actionManager->update(frameTime);
}

void NPC::onEvent()
{
	if (touchingID != TOUCH_UNTOUCHEDID)
	{
		selecting = true;
	}
	else
	{
		selecting = false;
	}
}

void NPC::onMouseLeftDown(int x, int y)
{
	// Finger interaction is owned by the hit NPC. A physical mouse continues
	// through GameController's click-index path so one press cannot queue twice.
	if (touchingDownID == TOUCH_MOUSEID)
	{
		return;
	}
	if (gm == nullptr || gm->blocksWorldPointerInput())
	{
		return;
	}
	if (shouldDeferMobileRightScriptChoice(scriptFile, scriptFileRight))
	{
		return;
	}

    auto player = gm->player;
    if (player->nowAction != acDeath && player->nowAction != acHide)
    {
        NextAction act;
        if (player->canRun && (player->thew > (int)round((float)player->info.thewMax * MIN_THEW_RATE_TO_RUN)  || player->thew > MIN_THEW_LIMIT_TO_RUN))
        {
            act.action = acRun;
        }
        else
        {
            act.action = acWalk;
        }
        act.destGE = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
        bool useRightScript = scriptFile == "" && scriptFileRight != "";
        if (useRightScript)
        {
            act.destKind = ndTalk;
            act.useRightScript = true;
        }
        else if (isEnemy() || isNoneFighter())
        {
            act.destKind = ndAttack;
        }
        else
        {
            act.destKind = ndTalk;
        }
        act.dest = position;
        if (gm->controller != nullptr)
        {
            gm->controller->cancelControllerWorldInteraction();
        }
        player->addNextAction(act);
    }
}

void NPC::onMouseLeftUp(int x, int y)
{
	if (touchingDownID == TOUCH_MOUSEID)
	{
		return;
	}
	if (gm == nullptr || gm->blocksWorldPointerInput())
	{
		return;
	}
	if (!shouldDeferMobileRightScriptChoice(scriptFile, scriptFileRight))
	{
		return;
	}
	auto self = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
	if (self == nullptr || gm == nullptr)
	{
		return;
	}

	Point delta = getTouchingDownMoveDelta(x, y);
	bool useRightScript = shouldUseMobileRightScript(getTouchingDownElapsedTime(), delta.x, delta.y);
	gm->queueNPCInteraction(self, useRightScript);
}

void NPC::onMouseMoveIn(int x, int y)
{

}

void NPC::onMouseMoveOut()
{
	selecting = false;
}

int NPC::getAttackOptionShapeRange(int moveKind, int region, int level) const
{
	if (level < 1)
	{
		level = 1;
	}
	else if (level > MAGIC_MAX_LEVEL)
	{
		level = MAGIC_MAX_LEVEL;
	}

	if (moveKind == mmkRegion)
	{
		if (isRegionShapeWithRange(region))
		{
			return getMagicRegionShapeRange(level);
		}
	}
	else if (moveKind == mmkLine || moveKind == mmkMoveLine || moveKind == mmkVMove || moveKind == mmkSector || moveKind == mmkRandSector)
	{
		return level;
	}
	else if (moveKind == mmkCircle || moveKind == mmkHeartCircle || moveKind == mmkHelixCircle)
	{
		return 3 + ((level - 1) / 3) * 2;
	}
	else if (moveKind == mmkFullScreen)
	{
		return 20;
	}
	return 0;
}

int NPC::estimatePhysicalReach(const Magic& magic, int level) const
{
	if (level < 1 || level > MAGIC_MAX_LEVEL)
	{
		return 0;
	}
	const auto& levelInfo = magic.level[level];

	if (levelInfo.moveKind == mmkFly || levelInfo.moveKind == mmkFlyContinuous
		|| levelInfo.moveKind == mmkFollow || levelInfo.moveKind == mmkThrow
		|| levelInfo.moveKind == mmkSector || levelInfo.moveKind == mmkRandSector
		|| levelInfo.moveKind == mmkMoveLine || levelInfo.moveKind == mmkVMove)
	{
		UTime flyTime = 0;
		if (levelInfo.moveKind == mmkThrow)
		{
			if (magic.explodeImage != nullptr)
			{
				flyTime = IMP::getIMPImageActionTime(magic.explodeImage);
			}
		}
		else if (levelInfo.lifeFrame > 0)
		{
			flyTime = (UTime)((float)levelInfo.lifeFrame * EFFECT_FRAME_TIME);
		}
		else if (magic.flyImage != nullptr)
		{
			flyTime = IMP::getIMPImageActionTime(magic.flyImage);
		}

		if (levelInfo.speed <= 0 || flyTime <= 0)
		{
			return 0;
		}

		float rawReach = (float)levelInfo.speed * MAGIC_FLYING_SPEED_SCALE * Config::getGameSpeed() * (float)flyTime / 2.0f;
		int reach = (int)std::floor(rawReach) - ATTACK_REACH_SAFETY_TILES;
		return reach > 0 ? reach : 1;
	}

	if (levelInfo.moveKind == mmkLine)
	{
		return 0;
	}

	if (levelInfo.moveKind == mmkCircle || levelInfo.moveKind == mmkHeartCircle || levelInfo.moveKind == mmkHelixCircle)
	{
		UTime flyTime = 0;
		if (levelInfo.lifeFrame > 0)
		{
			flyTime = (UTime)((float)levelInfo.lifeFrame * EFFECT_FRAME_TIME);
		}
		else if (magic.flyImage != nullptr)
		{
			flyTime = IMP::getIMPImageActionTime(magic.flyImage);
		}
		if (levelInfo.speed > 0 && flyTime > 0)
		{
			float rawReach = (float)levelInfo.speed * MAGIC_FLYING_SPEED_SCALE * Config::getGameSpeed() * (float)flyTime / 2.0f;
			int reach = (int)std::floor(rawReach) - ATTACK_REACH_SAFETY_TILES;
			return reach > 0 ? reach : 1;
		}
		int range = getAttackOptionShapeRange(levelInfo.moveKind, levelInfo.region, level);
		range -= ATTACK_REACH_SAFETY_TILES;
		return range > 0 ? range : 1;
	}

	if (levelInfo.moveKind == mmkFullScreen)
	{
		return 20;
	}

	return 0;
}

int NPC::calcEffectiveUseDistance(const NPCAttackOption& option) const
{
	if (option.magic == nullptr)
	{
		return attackRadius > 0 ? attackRadius : 1;
	}

	if (option.moveKind == mmkPoint)
	{
		int pointDistance = option.hasExplicitUseDistance ? option.configuredUseDistance : attackRadius;
		if (pointDistance <= 0)
		{
			pointDistance = attackRadius;
		}
		if (pointDistance <= 0)
		{
			pointDistance = 1;
		}
		if (attackRadius > 0 && pointDistance > attackRadius)
		{
			pointDistance = attackRadius;
		}
		return pointDistance;
	}

	int dist = 0;
	if (option.configuredUseDistance > 0)
	{
		dist = option.configuredUseDistance;
	}

	int clampedLevel = getClampedAttackLevel();
	int physReach = estimatePhysicalReach(*option.magic, clampedLevel);
	if (physReach > 0)
	{
		if (dist <= 0)
		{
			dist = physReach;
		}
		else if (physReach < dist)
		{
			dist = physReach;
		}
	}

	if (option.shapeRange > 0 && option.moveKind == mmkRegion
		&& (option.region == mrCross || option.region == mrWave || option.region == mrTriangle || option.region == mrVType))
	{
		if (dist <= 0 || option.shapeRange < dist)
		{
			dist = option.shapeRange;
		}
	}
	if (dist <= 0 && option.moveKind == mmkRegion && option.region == mrSquare && option.shapeRange > 0)
	{
		dist = option.shapeRange;
	}
	if (dist <= 0)
	{
		dist = attackRadius;
	}
	if (dist <= 0)
	{
		dist = 1;
	}
	if (option.moveKind == mmkSelf)
	{
		if (dist < visionRadius)
		{
			dist = visionRadius;
		}
	}
	else if (attackRadius > 0 && dist > attackRadius)
	{
		dist = attackRadius;
	}

	return dist;
}

int NPC::getMaxAttackOptionDistance() const
{
	if (attackOptions.empty())
	{
		return attackRadius;
	}
	int maxDistance = 0;
	for (const auto& option : attackOptions)
	{
		if (option.magic != nullptr && option.isTargetAttack)
		{
			int effectiveDistance = calcEffectiveUseDistance(option);
			if (effectiveDistance > maxDistance)
			{
				maxDistance = effectiveDistance;
			}
		}
	}
	return maxDistance > 0 ? maxDistance : attackRadius;
}

std::vector<AttackCandidateInfo> NPC::buildAttackCandidates(Point targetPosition) const
{
	std::vector<AttackCandidateInfo> candidates;
	if (attackOptions.empty())
	{
		return candidates;
	}
	int clampedLevel = getClampedAttackLevel();
	bool canSeeTarget = canSee(targetPosition);
	int currentDistance = gm->map->calDistance(position, targetPosition);

	for (const auto& option : attackOptions)
	{
		if (option.magic == nullptr || !option.isTargetAttack)
		{
			continue;
		}

		AttackCandidateInfo info;
		info.option = option;
		info.effectiveDistance = calcEffectiveUseDistance(option);
		info.requiresExactPosition = option.moveKind == mmkRegion && isExactPositionRegionShape(option.region);

		auto attackPosition = findBestAttackPosition(option, position, targetPosition, clampedLevel);
		if (!attackPosition.has_value())
		{
			info.positionValid = false;
			info.desiredPosition = targetPosition;
			info.canHitNow = false;
			info.moveCost = INT_MAX;
			candidates.push_back(info);
			continue;
		}

		info.positionValid = true;
		info.desiredPosition = attackPosition.value();
		bool optionNeedsLineOfSight = skillRequiresLineOfSight(option);
		info.canHitNow = (!optionNeedsLineOfSight || canSeeTarget) && canMagicHitTarget(option, position, targetPosition, clampedLevel);
		info.desiredPositionCanHit = !info.requiresExactPosition || canMagicHitTarget(option, info.desiredPosition, targetPosition, clampedLevel);
		info.approachOnly = info.requiresExactPosition && !info.desiredPositionCanHit;

		if (info.canHitNow)
		{
			info.moveCost = 0;
		}
		else if (info.requiresExactPosition)
		{
			info.moveCost = gm->map->calDistance(position, info.desiredPosition);
			if (info.approachOnly)
			{
				info.moveCost += 10000;
			}
		}
		else
		{
			info.moveCost = abs(currentDistance - info.effectiveDistance);
		}

		candidates.push_back(info);
	}

	return candidates;
}

bool NPC::getFallbackApproachInfo(Point targetPosition, Point& outPosition, int& outDistance) const
{
	outPosition = targetPosition;
	outDistance = attackRadius > 0 ? attackRadius : 1;

	auto candidates = buildAttackCandidates(targetPosition);
	if (candidates.empty())
	{
		return false;
	}
	int bestMoveCost = INT_MAX;
	for (const auto& info : candidates)
	{
		if (!info.positionValid)
		{
			continue;
		}
		if (info.approachOnly && info.desiredPosition == position)
		{
			continue;
		}
		if (info.moveCost < bestMoveCost)
		{
			bestMoveCost = info.moveCost;
			outPosition = info.desiredPosition;
			outDistance = info.approachOnly ? 0 : info.effectiveDistance;
		}
	}
	if (bestMoveCost == INT_MAX)
	{
		return false;
	}
	return true;
}

void NPC::parseMagicList(const std::string& listString, int sourceIndexStart)
{
	if (listString.empty())
	{
		return;
	}
	std::string normalized = listString;
	convert::replaceAllString(normalized, "\xEF\xBC\x9A", ":");
	convert::replaceAllString(normalized, "\xEF\xBC\x9B", ";");
	auto items = convert::splitString(normalized, ";");
	auto trimString = [](const std::string& value) {
		size_t first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
		{
			return std::string();
		}
		size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	};
	int sourceIndex = sourceIndexStart;
	for (size_t i = 0; i < items.size(); i++)
	{
		std::string item = trimString(items[i]);
		if (item.empty())
		{
			continue;
		}
		size_t colonPos = item.find(':');
		std::string magicName;
		int distance = 0;
		if (colonPos != std::string::npos)
		{
			magicName = trimString(item.substr(0, colonPos));
			std::string distanceString = trimString(item.substr(colonPos + 1));
			if (!distanceString.empty())
			{
				try { distance = std::stoi(distanceString); } catch (...) { distance = 0; }
			}
		}
		else
		{
			magicName = trimString(item);
		}
		if (!magicName.empty())
		{
			addAttackOption(magicName, distance, sourceIndex);
			sourceIndex++;
		}
	}
}

void NPC::addAttackOption(const std::string& magicFileName, int distance, int sourceIndex, bool useAdditionalEffect)
{
	auto magic = gm->magicManager.loadAttackMagic(magicFileName);
	if (magic != nullptr && !magic->loadSucceeded)
	{
		magic = nullptr;
	}
	if (magic != nullptr)
	{
		int clampedLevel = getClampedAttackLevel();
		auto& levelInfo = magic->level[clampedLevel];

		NPCAttackOption option;
		option.magic = magic;
		option.moveKind = levelInfo.moveKind;
		option.region = levelInfo.region;
		option.shapeRange = getAttackOptionShapeRange(option.moveKind, option.region, clampedLevel);
		option.sourceIndex = sourceIndex;
		option.isTargetAttack = (option.moveKind != mmkSelf);
		option.useAdditionalEffect = useAdditionalEffect;

		option.configuredUseDistance = distance;
		option.hasExplicitUseDistance = distance > 0;
		if (option.configuredUseDistance <= 0)
		{
			option.configuredUseDistance = option.moveKind == mmkPoint ? 0 : levelInfo.attackRadius;
		}

		attackOptions.push_back(option);
	}
}

void NPC::rebuildAttackOptions()
{
	attackOptions.clear();
	actionPlan.reset();
	hasLastUsedAttackOption = false;
	int sourceIndex = 0;

	if (!temporaryMagicListReplacement.empty())
	{
		parseMagicList(temporaryMagicListReplacement, sourceIndex);
		std::sort(attackOptions.begin(), attackOptions.end());
		return;
	}
	else if (!equipmentFlyIniReplacements.empty() || !temporaryFlyIniReplacements.empty())
	{
		for (const auto& replacement : equipmentFlyIniReplacements)
		{
			addAttackOption(replacement, attackRadius, sourceIndex, true);
			sourceIndex++;
		}
		for (const auto& replacement : temporaryFlyIniReplacements)
		{
			addAttackOption(replacement, attackRadius, sourceIndex, true);
			sourceIndex++;
		}
	}
	else if (!flyIni.empty())
	{
		addAttackOption(flyIni, 0, sourceIndex, true);
		sourceIndex++;
	}
	if (!equipmentFlyIni2Replacements.empty())
	{
		for (const auto& replacement : equipmentFlyIni2Replacements)
		{
			addAttackOption(replacement, attackRadius, sourceIndex, true);
			sourceIndex++;
		}
	}
	else if (!flyIni2.empty())
	{
		addAttackOption(flyIni2, 0, sourceIndex, true);
		sourceIndex++;
	}
	if (!flyInis.empty())
	{
		parseMagicList(flyInis, sourceIndex);
	}
	std::sort(attackOptions.begin(), attackOptions.end());
}

bool NPC::isCrossHit(Point casterPosition, Point targetPosition, int crossRange) const
{
	if (crossRange <= 0)
	{
		return false;
	}

	int crossDirections[4] = { 1, 3, 5, 7 };
	for (int d = 0; d < 4; d++)
	{
		Point currentPos = casterPosition;
		for (int step = 1; step <= crossRange; step++)
		{
			currentPos = gm->map->getSubPoint(currentPos, crossDirections[d]);
			if (currentPos == targetPosition)
			{
				return true;
			}
		}
	}
	return false;
}







bool NPC::canMagicHitTarget(const NPCAttackOption& option, Point casterPosition, Point targetPosition, int level) const
{
	if (option.magic == nullptr)
	{
		return false;
	}

	int effectiveDistance = calcEffectiveUseDistance(option);
	if (effectiveDistance <= 0)
	{
		return false;
	}

	int distance = gm->map->calDistance(casterPosition, targetPosition);
	if (distance < 1
		&& option.moveKind != mmkSelf && option.moveKind != mmkPoint && option.moveKind != mmkFullScreen
		&& option.moveKind != mmkCircle && option.moveKind != mmkHeartCircle && option.moveKind != mmkHelixCircle
		&& !(option.moveKind == mmkRegion && option.region == mrSquare))
	{
		return false;
	}

	if (distance > effectiveDistance)
	{
		return false;
	}

	if (option.moveKind == mmkSelf)
	{
		return casterPosition == targetPosition;
	}

	if (option.moveKind == mmkPoint)
	{
		return true;
	}

	if (option.moveKind == mmkRegion)
	{
		int shapeRange = option.shapeRange > 0 ? option.shapeRange : getAttackOptionShapeRange(option.moveKind, option.region, level);
		if (option.region == mrCross)
		{
			if (distance > shapeRange)
			{
				return false;
			}
			return isCrossHit(casterPosition, targetPosition, shapeRange);
		}
		if (option.region == mrVType)
		{
			if (distance > shapeRange)
			{
				return false;
			}
			return isVTypeRegionHit(casterPosition, targetPosition, level);
		}
	}

	// Non-cross attack shapes are only approximated here. Their actual effect code either aims at
	// targetPosition or has enough area/flight tolerance that distance is the important AI signal.
	return true;
}

bool NPC::isTargetValid(std::shared_ptr<GameElement> target) const
{
	if (target == nullptr)
	{
		return true;
	}
	auto targetNPC = std::dynamic_pointer_cast<NPC>(target);
	if (targetNPC == nullptr)
	{
		return true;
	}
	return targetNPC->life > 0 && targetNPC->nowAction != acHide && targetNPC->nowAction != acDeath;
}

bool NPC::isCombatTargetValid(std::shared_ptr<GameElement> target) const
{
	if (target == nullptr)
	{
		return false;
	}
	auto targetNPC = std::dynamic_pointer_cast<NPC>(target);
	if (targetNPC == nullptr)
	{
		return true;
	}
	return targetNPC->life > 0 && targetNPC->nowAction != acHide && targetNPC->nowAction != acDeath;
}

bool NPC::canAnyAttackOptionHitTarget(Point targetPosition) const
{
	if (attackOptions.empty())
	{
		return gm->map->calDistance(position, targetPosition) <= attackRadius;
	}
	return findReadyAttackOption(targetPosition).has_value();
}

std::optional<NPCAttackOption> NPC::findReadyAttackOption(Point targetPosition) const
{
	if (attackOptions.empty())
	{
		return std::nullopt;
	}
	auto candidates = buildAttackCandidates(targetPosition);
	std::vector<const AttackCandidateInfo*> hitCandidates;
	SkillScore bestScore;
	bool hasBest = false;
	for (const auto& info : candidates)
	{
		if (!info.positionValid || !info.canHitNow)
		{
			continue;
		}
		SkillScore score;
		score.canHitNow = info.canHitNow;
		score.moveCost = info.moveCost;
		score.isInertia = hasLastUsedAttackOption && lastUsedAttackOption.magic == info.option.magic && lastUsedAttackOption.sourceIndex == info.option.sourceIndex;
		if (!hasBest || score.isBetterThan(bestScore))
		{
			bestScore = score;
			hitCandidates.clear();
			hitCandidates.push_back(&info);
			hasBest = true;
		}
		else if (!bestScore.isBetterThan(score) && !score.isBetterThan(bestScore))
		{
			hitCandidates.push_back(&info);
		}
	}
	if (hitCandidates.empty())
	{
		return std::nullopt;
	}
	int selectedIndex = hitCandidates.size() > 1 ? engine->getRand((int)hitCandidates.size() - 1) : 0;
	return hitCandidates[selectedIndex]->option;
}

void NPC::prepareImmediateAttackPlan(std::shared_ptr<GameElement> target, const NPCAttackOption& option, Point targetPosition)
{
	UTime currentTime = getTime();
	actionPlan.selectedOption = option;
	actionPlan.hasSelectedOption = true;
	actionPlan.state = npsInAttackRange;
	actionPlan.planTarget = target;
	actionPlan.planStartTime = currentTime;
	actionPlan.desiredAttackPosition = position;
	actionPlan.requiresExactPosition = option.moveKind == mmkRegion && isExactPositionRegionShape(option.region);
	actionPlan.approachOnly = false;
	actionPlan.planOriginTargetPosition = targetPosition;
	actionPlan.lastTargetPosition = targetPosition;
	actionPlan.lastNpcPosition = position;
	actionPlan.lastReplanTime = currentTime;
	fightState.set(true);
	currentCombatTarget = target;
	currentCombatTargetTime = currentTime;
}

bool NPC::isLifeLowForAI() const
{
	int maxLife = lifeMax + equipmentAttributes.lifeMax;
	if (maxLife <= 0)
	{
		return false;
	}
	return life * 100 <= maxLife * lifeLowPercent;
}

bool NPC::tryKeepDistanceWhenLifeLow(std::shared_ptr<GameElement> target)
{
	if (target == nullptr || gm == nullptr || gm->map == nullptr || keepRadiusWhenLifeLow <= 0 || !isLifeLowForAI())
	{
		return false;
	}

	int distance = gm->map->calDistance(position, target->position);
	if (distance >= keepRadiusWhenLifeLow)
	{
		return false;
	}

	int retreatDistance = keepRadiusWhenLifeLow - distance;
	if (usePathFinder())
	{
		return beginRetreatWalk(target->position, target, retreatDistance);
	}
	return beginRetreatStep(target->position, target, retreatDistance);
}

bool NPC::tryKeepDistanceWhenFriendDeath()
{
	if (gm == nullptr || gm->map == nullptr || gm->npcManager == nullptr)
	{
		return false;
	}
	if (keepRadiusWhenFriendDeath <= 0 || kind == nkPartner)
	{
		return false;
	}

	auto target = keepDistanceCharacterWhenFriendDeath.lock();
	auto targetNPC = std::dynamic_pointer_cast<NPC>(target);
	if (targetNPC == nullptr || targetNPC->isDying() || targetNPC->isHiding() || !targetNPC->isVisibleForRuntime())
	{
		target = gm->npcManager->findFriendDeathAttacker(std::dynamic_pointer_cast<NPC>(getMySharedPtr()), visionRadius);
		keepDistanceCharacterWhenFriendDeath = target;
	}
	if (target == nullptr)
	{
		return false;
	}

	int distance = gm->map->calDistance(position, target->position);
	if (distance >= keepRadiusWhenFriendDeath)
	{
		return false;
	}

	int retreatDistance = keepRadiusWhenFriendDeath - distance;
	if (usePathFinder())
	{
		return beginRetreatWalk(target->position, target, retreatDistance);
	}
	return beginRetreatStep(target->position, target, retreatDistance);
}

bool NPC::tryUseMagicWhenLifeLow()
{
	if (magicToUseWhenLifeLow == nullptr || !magicToUseWhenLifeLow->loadSucceeded || !isLifeLowForAI())
	{
		return false;
	}
	if (!canDoAction(acMagic))
	{
		return false;
	}
	if (!canUseMagicByState(magicToUseWhenLifeLow, false))
	{
		return false;
	}

	Point destination = Map::getSubPoint(position, direction);
	setPreparedMagicAction(magicToUseWhenLifeLow, destination, getClampedAttackLevel(), nullptr);
	beginMagic(destination, nullptr);
	return isMagicing();
}

bool NPC::shouldUseSelfBuff(const NPCAttackOption& option) const
{
	if (option.magic == nullptr)
	{
		return false;
	}
	int clampedLevel = getClampedAttackLevel();
	if (clampedLevel < 1 || clampedLevel > MAGIC_MAX_LEVEL)
	{
		return false;
	}
	auto& levelInfo = option.magic->level[clampedLevel];
	switch (levelInfo.specialKind)
	{
	case mskAddLife:
	{
		int maxLife = lifeMax + equipmentAttributes.lifeMax;
		return maxLife > 0 && life * 100 / maxLife <= 60;
	}
	case mskAddThew:
	{
		int maxThew = thewMax + equipmentAttributes.thewMax;
		return maxThew > 0 && thew * 100 / maxThew <= 50;
	}
	case mskAddShield:
		return shieldLife <= 0;
	case mskAddDamageReduceShield:
	{
		for (const auto& weakEffect : shieldEffects)
		{
			if (auto effect = weakEffect.lock())
			{
				if (!effect->vanishing && (effect->doing == ekFlying || effect->doing == ekExploding || effect->doing == ekHiding))
				{
					return false;
				}
			}
		}
		return true;
	}
	case mskBlockDamage:
		return !hasActiveSelfMagic(mskBlockDamage);
	case mskClearAbnormalState:
		return frozen || poisoned || petrified || immobilized;
	default:
		return false;
	}
}

bool NPC::trySelfBuff()
{
	UTime currentTime = getTime();
	if (currentTime < nextSelfBuffTime)
	{
		return false;
	}
	if (attackOptions.empty())
	{
		return false;
	}
	for (const auto& option : attackOptions)
	{
		if (option.isTargetAttack)
		{
			continue;
		}
		if (!shouldUseSelfBuff(option))
		{
			continue;
		}
		if (!canMagicHitTarget(option, position, position, getClampedAttackLevel()))
		{
			continue;
		}
		nextSelfBuffTime = currentTime + SelfBuffMinInterval + engine->getRand(SelfBuffMaxInterval - SelfBuffMinInterval);
		int clampedLevel = getClampedAttackLevel();
		useMagic(option.magic, position, clampedLevel, std::dynamic_pointer_cast<GameElement>(getMySharedPtr()));
		return true;
	}
	return false;
}

bool NPC::isTooCloseForAttackOption(const NPCAttackOption& option, Point casterPosition, Point targetPosition) const
{
	if (option.magic == nullptr)
	{
		return false;
	}
	int effectiveDistance = calcEffectiveUseDistance(option);
	if (effectiveDistance <= 0)
	{
		return false;
	}
	if (option.moveKind == mmkSelf || option.moveKind == mmkFullScreen)
	{
		return false;
	}
	if (option.moveKind == mmkCircle || option.moveKind == mmkHeartCircle || option.moveKind == mmkHelixCircle)
	{
		return false;
	}
	if (option.moveKind == mmkRegion && option.region == mrSquare)
	{
		return false;
	}

	int distance = gm->map->calDistance(casterPosition, targetPosition);
	if (distance < 1)
	{
		return true;
	}

	int minEngage = getMinEngageDistance();
	if (minEngage > 0 && distance < minEngage)
	{
		return true;
	}

	if (option.moveKind == mmkRegion && isExactPositionRegionShape(option.region))
	{
		int clampedLevel = getClampedAttackLevel();
		if (!canMagicHitTarget(option, casterPosition, targetPosition, clampedLevel) && distance <= 1)
		{
			return true;
		}
	}

	return false;
}

int NPC::getMinEngageDistance() const
{
	if (attackRadius < 5)
	{
		return 0;
	}
	int minDist = attackRadius / 2;
	return minDist >= 2 ? minDist : 2;
}

bool NPC::skillRequiresLineOfSight(const NPCAttackOption& option) const
{
	if (option.moveKind == mmkFullScreen || option.moveKind == mmkFollow)
	{
		return false;
	}
	if (option.moveKind == mmkCircle || option.moveKind == mmkHeartCircle || option.moveKind == mmkHelixCircle)
	{
		return false;
	}
	if (option.moveKind == mmkRegion && option.region == mrSquare)
	{
		return false;
	}
	return true;
}

std::optional<Point> NPC::findBestAttackPosition(const NPCAttackOption& option, Point casterPosition, Point targetPosition, int level) const
{
	bool requiresExactPosition = option.moveKind == mmkRegion && isExactPositionRegionShape(option.region);
	if (!requiresExactPosition)
	{
		if (canMagicHitTarget(option, casterPosition, targetPosition, level))
		{
			return casterPosition;
		}
		return targetPosition;
	}

	if (canMagicHitTarget(option, casterPosition, targetPosition, level))
	{
		return casterPosition;
	}
	int shapeRange = option.shapeRange > 0 ? option.shapeRange : getAttackOptionShapeRange(option.moveKind, option.region, level);
	if (shapeRange < 1)
	{
		return std::nullopt;
	}

	auto encodePoint = [](Point point) -> uint64_t {
		return ((uint64_t)(uint32_t)point.x << 32) | (uint32_t)point.y;
	};

	struct PositionCandidate
	{
		Point position = { 0, 0 };
		int moveCost = INT_MAX;
	};

	std::vector<PositionCandidate> exactCandidates;
	std::vector<PositionCandidate> fallbackCandidates;
	std::unordered_set<uint64_t> exactVisited;
	std::unordered_set<uint64_t> fallbackVisited;
	int effectiveDistance = calcEffectiveUseDistance(option);
	int maxCrossStep = shapeRange;
	if (effectiveDistance > 0 && effectiveDistance < maxCrossStep)
	{
		maxCrossStep = effectiveDistance;
	}

	auto addPositionCandidate = [&](std::vector<PositionCandidate>& list, std::unordered_set<uint64_t>& visited, Point candidatePosition, bool requireHit) {
		uint64_t encoded = encodePoint(candidatePosition);
		if (visited.count(encoded))
		{
			return;
		}
		visited.insert(encoded);
		if (!gm->map->canWalk(candidatePosition))
		{
			return;
		}
		if (!isWithinCombatChaseLimit(targetPosition, candidatePosition))
		{
			return;
		}
		if (requireHit && !canMagicHitTarget(option, candidatePosition, targetPosition, level))
		{
			return;
		}
		list.push_back({ candidatePosition, gm->map->calDistance(casterPosition, candidatePosition) });
	};

	auto addFallbackAround = [&](Point anchorPosition) {
		addPositionCandidate(fallbackCandidates, fallbackVisited, anchorPosition, false);
		for (int dir = 0; dir < 8; dir++)
		{
			addPositionCandidate(fallbackCandidates, fallbackVisited, gm->map->getSubPoint(anchorPosition, dir), false);
		}
	};

	if (option.region == mrCross)
	{
		for (int dir = 1; dir <= 7; dir += 2)
		{
			Point current = targetPosition;
			for (int step = 1; step <= maxCrossStep; step++)
			{
				current = gm->map->getSubPoint(current, dir);
				addPositionCandidate(exactCandidates, exactVisited, current, true);
				addFallbackAround(current);
			}
		}
	}
	else if (option.region == mrVType)
	{
		std::vector<Point> frontier = { targetPosition };
		std::unordered_set<uint64_t> visitedAroundTarget;
		visitedAroundTarget.insert(encodePoint(targetPosition));
		for (int step = 1; step <= maxCrossStep; step++)
		{
			std::vector<Point> nextFrontier;
			for (Point anchor : frontier)
			{
				for (int dir = 0; dir < 8; dir++)
				{
					Point current = gm->map->getSubPoint(anchor, dir);
					uint64_t encoded = encodePoint(current);
					if (visitedAroundTarget.count(encoded))
					{
						continue;
					}
					visitedAroundTarget.insert(encoded);
					nextFrontier.push_back(current);
					addPositionCandidate(exactCandidates, exactVisited, current, true);
					addFallbackAround(current);
				}
			}
			frontier = nextFrontier;
		}
	}

	auto selectBestPosition = [&](std::vector<PositionCandidate>& list, bool avoidCurrentPosition, bool allowUncheckedApproach) -> std::optional<Point> {
		std::sort(list.begin(), list.end(), [](const PositionCandidate& lhs, const PositionCandidate& rhs) {
			return lhs.moveCost < rhs.moveCost;
		});
		int pathCheckCount = 0;
		const int maxPathCheckCount = 6;
		std::optional<Point> uncheckedApproachPosition;
		for (const auto& candidate : list)
		{
			if (avoidCurrentPosition && candidate.position == casterPosition)
			{
				continue;
			}
			if (!usePathFinder() || candidate.position == casterPosition)
			{
				return candidate.position;
			}
			if (pathCheckCount >= maxPathCheckCount)
			{
				if (allowUncheckedApproach && !uncheckedApproachPosition.has_value()
					&& !canMagicHitTarget(option, candidate.position, targetPosition, level))
				{
					uncheckedApproachPosition = candidate.position;
				}
				continue;
			}
			pathCheckCount++;
			if (!gm->map->getRadiusPath(casterPosition, candidate.position, 0).empty())
			{
				return candidate.position;
			}
		}
		return uncheckedApproachPosition;
	};

	auto exactPosition = selectBestPosition(exactCandidates, false, false);
	if (exactPosition.has_value())
	{
		return exactPosition.value();
	}

	auto fallbackPosition = selectBestPosition(fallbackCandidates, true, true);
	if (fallbackPosition.has_value())
	{
		return fallbackPosition.value();
	}

	return std::nullopt;
}

bool NPC::evaluateAndPlan(std::shared_ptr<GameElement> target)
{
	if (target == nullptr || !isCombatTargetValid(target))
	{
		actionPlan.reset();
		return false;
	}

	UTime currentTime = getTime();
	Point targetPosition = target->position;
	bool targetVisible = canSee(targetPosition);
	if (!targetVisible)
	{
		actionPlan.reset();
		return false;
	}
	rememberCombatTargetPosition(target);

	int currentDistance = gm->map->calDistance(position, targetPosition);
	if (currentDistance > visionRadius * 2)
	{
		actionPlan.reset();
		stopMovement();
		return false;
	}
	if (actionPlan.isActive() && !actionPlan.isExpired(currentTime))
	{
		auto planTarget = actionPlan.planTarget.lock();
		if (planTarget != nullptr && planTarget == target)
		{
			int cumulativeTargetDisplacement = gm->map->calDistance(actionPlan.planOriginTargetPosition, targetPosition);
			bool positionStillValid = true;
			if (actionPlan.requiresExactPosition)
			{
				bool desiredPositionCanHit = canMagicHitTarget(actionPlan.selectedOption, actionPlan.desiredAttackPosition, targetPosition, getClampedAttackLevel());
				if (actionPlan.approachOnly)
				{
					positionStillValid = !desiredPositionCanHit && cumulativeTargetDisplacement == 0 && position != actionPlan.desiredAttackPosition;
				}
				else
				{
					positionStillValid = desiredPositionCanHit;
				}
				int npcDisplacement = gm->map->calDistance(position, actionPlan.desiredAttackPosition);
				int previousDisplacement = gm->map->calDistance(actionPlan.lastNpcPosition, actionPlan.desiredAttackPosition);
				if (npcDisplacement > NPCActionPlan::ReplanMoveThreshold && npcDisplacement > previousDisplacement)
				{
					positionStillValid = false;
				}
			}
			if (cumulativeTargetDisplacement <= NPCActionPlan::ReplanMoveThreshold && positionStillValid)
			{
				if (!isWithinCombatChaseLimit(targetPosition, actionPlan.desiredAttackPosition))
				{
					actionPlan.reset();
					stopMovement();
					return false;
				}
				if (!actionPlan.requiresExactPosition && cumulativeTargetDisplacement > 0)
				{
					actionPlan.desiredAttackPosition = targetPosition;
				}
				actionPlan.lastTargetPosition = targetPosition;
				actionPlan.lastNpcPosition = position;
				return true;
			}
		}
	}

	actionPlan.reset();

	if (attackOptions.empty())
	{
		rebuildAttackOptions();
	}

	auto candidates = buildAttackCandidates(targetPosition);

	std::vector<const AttackCandidateInfo*> bestCandidates;
	SkillScore bestScore;
	bool hasBest = false;

	for (const auto& info : candidates)
	{
		if (!info.positionValid)
		{
			continue;
		}
		if (!isWithinCombatChaseLimit(targetPosition, info.desiredPosition))
		{
			continue;
		}

		SkillScore score;
		score.canHitNow = info.canHitNow;
		score.moveCost = info.moveCost;
		score.isInertia = hasLastUsedAttackOption && lastUsedAttackOption.magic == info.option.magic && lastUsedAttackOption.sourceIndex == info.option.sourceIndex;

		if (!hasBest || score.isBetterThan(bestScore))
		{
			bestScore = score;
			bestCandidates.clear();
			bestCandidates.push_back(&info);
			hasBest = true;
		}
		else if (!bestScore.isBetterThan(score) && !score.isBetterThan(bestScore))
		{
			bestCandidates.push_back(&info);
		}
	}

	if (bestCandidates.empty())
	{
		return false;
	}
	if (!bestScore.canHitNow && bestScore.moveCost == INT_MAX)
	{
		return false;
	}

	int selectedIndex = bestCandidates.size() > 1 ? engine->getRand((int)bestCandidates.size() - 1) : 0;
	const auto& selected = *bestCandidates[selectedIndex];

	actionPlan.selectedOption = selected.option;
	actionPlan.hasSelectedOption = true;
	actionPlan.planTarget = target;
	fightState.set(true);
	currentCombatTarget = target;
	currentCombatTargetTime = currentTime;
	actionPlan.planStartTime = currentTime;
	actionPlan.desiredAttackPosition = selected.desiredPosition;
	actionPlan.requiresExactPosition = selected.requiresExactPosition;
	actionPlan.approachOnly = selected.approachOnly;
	actionPlan.planOriginTargetPosition = targetPosition;
	actionPlan.lastTargetPosition = targetPosition;
	actionPlan.lastNpcPosition = position;
	actionPlan.lastReplanTime = currentTime;
	actionPlan.state = selected.canHitNow ? npsInAttackRange : npsApproaching;

	return true;
}

bool NPC::executeActionPlan(std::shared_ptr<GameElement> target)
{
	if (!actionPlan.isActive() || !actionPlan.hasSelectedOption || target == nullptr || !isCombatTargetValid(target))
	{
		if (actionPlan.isActive() && (!actionPlan.hasSelectedOption || target == nullptr || !isCombatTargetValid(target)))
		{
			actionPlan.reset();
		}
		return false;
	}

	Point targetPosition = target->position;
	bool bCanSee = canSee(targetPosition);
	if (!bCanSee)
	{
		actionPlan.reset();
		return false;
	}
	rememberCombatTargetPosition(target);

	int currentDistance = gm->map->calDistance(position, targetPosition);
	if (currentDistance > visionRadius * 2)
	{
		actionPlan.reset();
		stopMovement();
		return false;
	}

	int clampedLevel = getClampedAttackLevel();
	bool canHitNow = canMagicHitTarget(actionPlan.selectedOption, position, targetPosition, clampedLevel);
	bool tooClose = isTooCloseForAttackOption(actionPlan.selectedOption, position, targetPosition);

	if (tooClose)
	{
		actionPlan.state = npsApproaching;
		int effectiveDistance = calcEffectiveUseDistance(actionPlan.selectedOption);
		bool handled = false;
		if (usePathFinder())
		{
			handled = beginRetreatWalk(targetPosition, target, effectiveDistance);
		}
		else
		{
			handled = beginRetreatStep(targetPosition, target, effectiveDistance);
		}
		if (!handled)
		{
			if (canHitNow)
			{
				if (!canStartIdleAttack())
				{
					beginStand();
					return true;
				}
				beginAttack(targetPosition, target);
				if (isAttacking())
				{
					return true;
				}
			}
			actionPlan.reset();
			return false;
		}
		if (!isCurrentPathWithinCombatChaseLimit(targetPosition))
		{
			actionPlan.reset();
			stopMovement();
			return false;
		}
		return true;
	}

	if (canHitNow)
	{
		actionPlan.state = npsInAttackRange;
		if (!canStartIdleAttack())
		{
			beginStand();
			return true;
		}
		beginAttack(targetPosition, target);
		if (!isAttacking())
		{
			actionPlan.reset();
			return false;
		}
		return true;
	}

	if (actionPlan.approachOnly && position == actionPlan.desiredAttackPosition)
	{
		actionPlan.reset();
		return false;
	}

	actionPlan.state = npsApproaching;
	Point moveTarget = actionPlan.desiredAttackPosition;
	int effectiveDistance = calcEffectiveUseDistance(actionPlan.selectedOption);
	int moveRadius = actionPlan.requiresExactPosition ? 0 : effectiveDistance;
	if (!isWithinCombatChaseLimit(targetPosition, moveTarget))
	{
		actionPlan.reset();
		stopMovement();
		return false;
	}

	RadiusMoveResult moveResult = rmrFailed;
	if (usePathFinder())
	{
		moveResult = beginRadiusWalk(moveTarget, moveRadius);
	}
	else
	{
		moveResult = beginRadiusStep(moveTarget, moveRadius);
	}

	if (moveResult == rmrMoved)
	{
		if (!isCurrentPathWithinCombatChaseLimit(targetPosition))
		{
			actionPlan.reset();
			stopMovement();
			return false;
		}
		return true;
	}

	if (moveResult == rmrAlreadyInRadius)
	{
		if (actionPlan.approachOnly)
		{
			actionPlan.reset();
			return false;
		}
	}

	actionPlan.reset();
	return false;
}
