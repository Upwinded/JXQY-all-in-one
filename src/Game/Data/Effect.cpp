#include "Effect.h"
#include "../../Engine/Engine.h"
#include "ColorStyle.h"
#include "NPC.h"
#include "NPCManager.h"
#include "Player.h"
#include "ProjectedMovement.h"
#include "MagicDamageChannels.h"
#include "MagicTransport.h"
#include "../GameManager/GameManager.h"
#include "../../File/File.h"
#include "../../File/INIReader.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr UTime MgAttributeEffectFrameMilliseconds = 10;

int clampDrawCoordinate(int base, double offset, int imageOffset, int heightOffset)
{
	if (!std::isfinite(offset))
	{
		offset = 0.0;
	}
	const double coordinate = static_cast<double>(base)
		+ std::round(offset)
		- static_cast<double>(imageOffset)
		- static_cast<double>(heightOffset);
	if (coordinate >= static_cast<double>(std::numeric_limits<int>::max()))
	{
		return std::numeric_limits<int>::max();
	}
	if (coordinate <= static_cast<double>(std::numeric_limits<int>::min()))
	{
		return std::numeric_limits<int>::min();
	}
	return static_cast<int>(coordinate);
}
}

int EffectReferenceSaveContext::registerDetachedCaster(const std::shared_ptr<NPC>& caster)
{
	if (caster == nullptr)
	{
		return -1;
	}
	auto existing = detachedCasterIndices.find(caster.get());
	if (existing != detachedCasterIndices.end())
	{
		return existing->second;
	}
	// Retain one overflow entry so EffectManager rejects the whole save.
	if (detachedCasters.size()
		> static_cast<size_t>(MaximumPersistedEffectCollectionCount))
	{
		return -1;
	}
	int index = static_cast<int>(detachedCasters.size());
	detachedCasterIndices[caster.get()] = index;
	detachedCasters.push_back(caster);
	return index;
}

void EffectReferenceLoadContext::setDetachedCaster(size_t index, const std::shared_ptr<NPC>& caster)
{
	if (index < detachedCasters.size())
	{
		detachedCasters[index] = caster;
	}
}

std::shared_ptr<NPC> EffectReferenceLoadContext::getDetachedCaster(int index) const
{
	if (index < 0 || static_cast<size_t>(index) >= detachedCasters.size())
	{
		return nullptr;
	}
	return detachedCasters[static_cast<size_t>(index)];
}

namespace
{
enum class EffectElementReferenceKind
{
	None = 0,
	Player = 1,
	Partner = 2,
	NPC = 3,
	DetachedCaster = 4,
};

struct EffectElementReference
{
	EffectElementReferenceKind kind = EffectElementReferenceKind::None;
	int index = -1;
};

EffectElementReference makeEffectElementReference(const std::shared_ptr<GameElement>& element)
{
	auto npc = std::dynamic_pointer_cast<NPC>(element);
	if (npc == nullptr || gm == nullptr || gm->npcManager == nullptr)
	{
		return {};
	}
	if (npc == gm->player)
	{
		return { EffectElementReferenceKind::Player, 0 };
	}

	int partnerIndex = 0;
	int npcIndex = 0;
	for (const auto& candidate : gm->npcManager->npcList)
	{
		if (!NPCManager::shouldPersistNPC(candidate))
		{
			continue;
		}
		if (candidate->kind == nkPartner)
		{
			if (candidate == npc)
			{
				return { EffectElementReferenceKind::Partner, partnerIndex };
			}
			partnerIndex++;
		}
		else if (candidate->kind != nkPlayer)
		{
			if (candidate == npc)
			{
				return { EffectElementReferenceKind::NPC, npcIndex };
			}
			npcIndex++;
		}
	}
	return {};
}

std::shared_ptr<GameElement> resolveEffectElementReference(
	const EffectElementReference& reference,
	EffectElementReferenceRole role = EffectElementReferenceRole::ActiveOnly,
	const EffectReferenceLoadContext* referenceContext = nullptr)
{
	if (reference.index < 0)
	{
		return nullptr;
	}
	if (reference.kind == EffectElementReferenceKind::DetachedCaster)
	{
		return role == EffectElementReferenceRole::Caster && referenceContext != nullptr
			? referenceContext->getDetachedCaster(reference.index)
			: nullptr;
	}
	if (gm == nullptr || gm->npcManager == nullptr)
	{
		return nullptr;
	}
	if (reference.kind == EffectElementReferenceKind::Player)
	{
		return gm->player;
	}

	int matchingIndex = 0;
	for (const auto& candidate : gm->npcManager->npcList)
	{
		if (!NPCManager::shouldPersistNPC(candidate))
		{
			continue;
		}
		bool matchesKind = reference.kind == EffectElementReferenceKind::Partner
			? candidate->kind == nkPartner
			: reference.kind == EffectElementReferenceKind::NPC
				&& candidate->kind != nkPartner
				&& candidate->kind != nkPlayer;
		if (!matchesKind)
		{
			continue;
		}
		if (matchingIndex == reference.index)
		{
			return candidate;
		}
		matchingIndex++;
	}
	return nullptr;
}

EffectElementReference readEffectElementReference(
	const INIReader& ini,
	const std::string& section,
	const std::string& prefix)
{
	int kind = static_cast<int>(ini.GetInteger(section, prefix + "ReferenceKind", 0));
	int index = static_cast<int>(ini.GetInteger(section, prefix + "ReferenceIndex", -1));
	if (kind < static_cast<int>(EffectElementReferenceKind::None)
		|| kind > static_cast<int>(EffectElementReferenceKind::DetachedCaster))
	{
		return {};
	}
	return { static_cast<EffectElementReferenceKind>(kind), index };
}

void writeEffectElementReference(
	INIReader& ini,
	const std::string& section,
	const std::string& prefix,
	const std::shared_ptr<GameElement>& element,
	EffectElementReferenceRole role = EffectElementReferenceRole::ActiveOnly,
	EffectReferenceSaveContext* referenceContext = nullptr)
{
	auto reference = makeEffectElementReference(element);
	if (reference.kind == EffectElementReferenceKind::None
		&& role == EffectElementReferenceRole::Caster
		&& referenceContext != nullptr)
	{
		auto caster = std::dynamic_pointer_cast<NPC>(element);
		int index = referenceContext->registerDetachedCaster(caster);
		if (index >= 0)
		{
			reference = { EffectElementReferenceKind::DetachedCaster, index };
		}
	}
	ini.SetInteger(section, prefix + "ReferenceKind", static_cast<int>(reference.kind));
	ini.SetInteger(section, prefix + "ReferenceIndex", reference.index);
}

std::vector<std::weak_ptr<NPC>> readEffectNPCReferenceList(
	const INIReader& ini,
	const std::string& section,
	const std::string& prefix)
{
	int count = static_cast<int>(ini.GetInteger(section, prefix + "Count", 0));
	count = std::clamp(count, 0, MaximumPersistedEffectCollectionCount);
	std::vector<std::weak_ptr<NPC>> references;
	references.reserve(count);
	for (int i = 0; i < count; i++)
	{
		auto reference = readEffectElementReference(ini, section, prefix + std::to_string(i + 1));
		auto npc = std::dynamic_pointer_cast<NPC>(resolveEffectElementReference(reference));
		if (npc != nullptr)
		{
			references.push_back(npc);
		}
	}
	return references;
}

void writeEffectNPCReferenceList(
	INIReader& ini,
	const std::string& section,
	const std::string& prefix,
	const std::vector<std::weak_ptr<NPC>>& elements)
{
	std::vector<std::shared_ptr<NPC>> resolvedElements;
	resolvedElements.reserve(elements.size());
	for (const auto& weakElement : elements)
	{
		auto element = weakElement.lock();
		if (element == nullptr)
		{
			continue;
		}
		auto reference = makeEffectElementReference(element);
		if (reference.kind != EffectElementReferenceKind::None)
		{
			resolvedElements.push_back(element);
		}
	}

	ini.SetInteger(section, prefix + "Count", static_cast<long>(resolvedElements.size()));
	for (size_t i = 0; i < resolvedElements.size(); i++)
	{
		writeEffectElementReference(ini, section, prefix + std::to_string(i + 1), resolvedElements[i]);
	}
}

int signum(int value)
{
	if (value > 0)
	{
		return 1;
	}
	if (value < 0)
	{
		return -1;
	}
	return 0;
}

PointEx getWorldPosition(Point tile, PointEx tileOffset)
{
	auto base = Map::getTilePositionEx(tile, { 0, 0 }, { 0, 0 }, { 0, 0 });
	return { base.x + tileOffset.x, base.y + tileOffset.y };
}

bool areOppositeRelations(int relation, int otherRelation)
{
	if (relation == nrFriendly)
	{
		return otherRelation == nrHostile || otherRelation == nrNone;
	}
	if (relation == nrHostile)
	{
		return otherRelation == nrFriendly || otherRelation == nrNone;
	}
	if (relation == nrNone)
	{
		return otherRelation == nrFriendly || otherRelation == nrHostile;
	}
	return false;
}

bool isMoveKindExcludedFromDiscard(int moveKind)
{
	return moveKind == mmkSelf || moveKind == mmkFullScreen || moveKind == mmkControl || moveKind == mmkTimeStop;
}

bool isMoveKindExcludedFromExchangeUser(int moveKind)
{
	return moveKind == mmkSelf || moveKind == mmkFullScreen || moveKind == mmkTransport || moveKind == mmkControl || moveKind == mmkSummon || moveKind == mmkTimeStop;
}

bool isSelfAnchoredMoveKind(int moveKind)
{
	return moveKind == mmkSelf || moveKind == mmkTimeStop;
}

bool isLiveAnchoredUser(std::shared_ptr<GameElement> element)
{
	return NPCManager::isManagedEffectCaster(element);
}

bool isAttackAllFighterTarget(const std::shared_ptr<NPC>& target)
{
	if (target == nullptr)
	{
		return false;
	}
	if (gm != nullptr && gm->player != nullptr && target.get() == gm->player.get())
	{
		return true;
	}
	return target->isFighterLike();
}

PointEx normalizeDirection(Point direction)
{
	float length = hypot((float)direction.x, (float)direction.y);
	if (length <= 0.001f)
	{
		return { 0.0f, 0.0f };
	}
	return { (float)direction.x / length, (float)direction.y / length };
}

int clampMagicLevel(int level)
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

bool isPlayerCaster(const std::shared_ptr<NPC>& caster)
{
	return caster != nullptr && caster->kind == nkPlayer;
}

int getEffectAmount2(const Magic& magic, int level, const std::shared_ptr<NPC>& caster)
{
	if (caster == nullptr)
	{
		return 0;
	}
	const int effectLevel = clampMagicLevel(level);
	int effect = magic.level[effectLevel].effect2;
	effect = (effect == 0 || !isPlayerCaster(caster)) ? caster->getAttack2() : effect;
	return caster->applyMagicEffectBonus(magic, effect);
}

int getEffectAmount3(const Magic& magic, int level, const std::shared_ptr<NPC>& caster)
{
	if (caster == nullptr)
	{
		return 0;
	}
	const int effectLevel = clampMagicLevel(level);
	int effect = magic.level[effectLevel].effect3;
	effect = (effect == 0 || !isPlayerCaster(caster)) ? caster->getAttack3() : effect;
	return caster->applyMagicEffectBonus(magic, effect);
}

int getPrimaryEffectAmount(const Magic& magic, int level, const std::shared_ptr<NPC>& caster)
{
	if (caster == nullptr)
	{
		return 0;
	}
	const int effectLevel = clampMagicLevel(level);
	int effect = magic.level[effectLevel].effect;
	return (effect == 0 || !isPlayerCaster(caster)) ? caster->getAttack() : effect;
}
}

Effect::Effect()
{
	coverMouse = false;
}

Effect::~Effect()
{
	freeResource();
}

void Effect::eventRun()
{
	run();
}

UTime Effect::getFlyingImageTime()
{
	if (getMoveKind() == mmkThrow)
	{
		return IMP::getIMPImageActionTime(magic.explodeImage);
	}
	else
	{
		return IMP::getIMPImageActionTime(magic.flyImage);
	}
}

UTime Effect::getExplodinUTime()
{
	if (isSelfAnchoredMoveKind(getMoveKind()))
	{
		if (vanishing)
		{
			return IMP::getIMPImageActionTime(magic.explodeImage);
		}
		else if (magic.level[level].specialKind == mskChangeAttributes
			&& gm != nullptr
			&& gm->global.feature.rageSystem)
		{
			return static_cast<UTime>(magic.level[level].lifeFrame) * MgAttributeEffectFrameMilliseconds;
		}
		else if (isLifeFrameSelfAnchoredSpecialKind(magic.level[level].specialKind))
		{
			return (unsigned int)(((float)magic.level[level].lifeFrame) * EFFECT_FRAME_TIME);
		}
		else
		{
			return IMP::getIMPImageActionTime(magic.flyImage);
		}	
	}
	else if (getMoveKind() == mmkPoint)
	{
		return IMP::getIMPImageActionTime(magic.flyImage);
	}
	else if (getMoveKind() == mmkThrow)
	{
		return 0;
	}
	else
	{
		return IMP::getIMPImageActionTime(magic.explodeImage);
	}
}

UTime Effect::getSuperImageTime()
{
	return IMP::getIMPImageActionTime(magic.superImage);
}

void Effect::beginExplode(Point pos)
{
	doing = ekExploding;
	if (magic.level[level].moveKind == mmkSummon)
	{
		auto summoned = summonedNPC.lock();
		if (summoned != nullptr)
		{
			position = summoned->getPosition();
			offset = summoned->getOffset();
		}
		clearSummonedNPC(true);
	}
	if (isSelfAnchoredMoveKind(magic.level[level].moveKind))
	{
		auto userPtr = user.lock();
		if (isLiveAnchoredUser(userPtr))
		{
			position = userPtr->position;
			offset = userPtr->offset;
		}
		else
		{
			position = pos;
		}
	}
	else if (flyingDirection.is_zero() || pos == src)
	{
		offset = srcOffset;
		position = pos;
	}
	else
	{
		offset = getCollideOffset(pos);
		auto l1 = hypot(offset.x, offset.y);
		if (l1 < TILE_HEIGHT)
		{
			auto l2 = hypot(flyingDirection.x * MapXRatio,flyingDirection.y);
			offset.x -= flyingDirection.x * (TILE_HEIGHT - l1) / l2 * MapXRatio;
			offset.y -= flyingDirection.y * (TILE_HEIGHT - l1) / l2;
		}
		position = pos;
		updatePosition();
	}
	if (magic.level[level].moveKind != mmkPoint)
	{
		beginTime = getTime();
		lifeTime = getExplodinUTime();
		playSound(doing);
	}
	triggerExplodeMagic(position);
	if (!vibrationTriggered && magic.vibratingScreen > 0 && gm != nullptr && gm->camera != nullptr)
	{
		gm->camera->vibrate(magic.vibratingScreen);
		vibrationTriggered = true;
	}
	if (lifeTime == 0)
	{
		doing = ekHiding;
		result = erLifeExhaust;
	}
}

void Effect::beginFly()
{
	magicWhenNewPositionLastTile = position;
	magicWhenNewPositionInitialized = true;
	if (getMoveKind() == mmkThrow)
	{
		doing = ekThrowing;
		beginTime = getTime();
		lifeTime = getFlyinUTime();
		playSound(ekFlying);
	}
	else
	{
		doing = ekFlying;
		beginTime = getTime();
		lifeTime = getFlyinUTime();
		playSound(doing);
		beginMeteorMove();
	}
}

void Effect::beginDrop()
{
	doing = ekHiding;
	lifeTime = 0;
	result = erLifeExhaust;
	auto tempMagic = std::make_shared<Magic>();
	tempMagic->copy(magic);
	auto userPtr = user.lock();
	auto effects = Magic::addThrowExplodeEffect(tempMagic, userPtr, dest, dest, level, damage, evade, launcherKind);
	for (auto& effect : effects)
	{
		if (effect != nullptr)
		{
			effect->additionalEffect = additionalEffect;
			if (effect->magic.noExplodeWhenLifeFrameEnd <= 0)
			{
				effect->triggerExplodeMagic(effect->position);
			}
		}
	}
	playSound(ekExploding);
}

UTime Effect::getFlyinUTime()
{
	if (getMoveKind() == mmkThrow)
	{
		return getFlyingImageTime();
	}
	else if (magic.level[level].lifeFrame <= 0)
	{
		return getFlyingImageTime();
	}
	else
	{
		return (unsigned int)((float)magic.level[level].lifeFrame * EFFECT_FRAME_TIME);
	}
}

void Effect::updateSound()
{
	if (channel == nullptr)
	{
		return;
	}
	if (!engine->getMusicPlaying(channel))
	{
		channel = nullptr;
		return;
	}
	if (gm == nullptr || gm->camera == nullptr)
	{
		return;
	}

	PointEx soundOffset = gm->camera->offset - offset;
	Point pos = Map::getTilePosition(position, gm->camera->position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	engine->setMusicPosition(channel, x, y);
	if (engine->getSoundVolume() != soundVolume)
	{
		soundVolume = engine->getSoundVolume();
		engine->setMusicVolume(channel, soundVolume);
	}
}

void Effect::initParam()
{
	fileName = magic.iniName;
	const int effectLevel = clampMagicLevel(level);
	speed = magic.level[effectLevel].speed;
	auto caster = std::dynamic_pointer_cast<NPC>(user.lock());
	damage2 = getEffectAmount2(magic, level, caster);
	damage3 = getEffectAmount3(magic, level, caster);
	damageMana = magic.level[effectLevel].effectMana;
	rangeElapsedMilliseconds = magic.rangeTimeInterval;
	flyMagicElapsedMilliseconds = 0;
	leapTimesRemaining = magic.level[effectLevel].leapTimes;
	leapFlying = false;
	summonedNPC.reset();
	vibrationTriggered = false;
	magicWhenNewPositionLastTile = { 0, 0 };
	magicWhenNewPositionInitialized = false;
	explodeMagicTriggered = false;
	moveImitateUserPositionInitialized = false;
	moveImitateUserLastPosition = { 0, 0 };
	moveImitateUserLastOffset = { 0, 0 };
	moveBackActive = false;
	clearCarryUser();
	clearAttachedNPCs();
	finishParasiticEffect();
	if (magic.moveImitateUser > 0)
	{
		auto userPtr = user.lock();
		if (userPtr != nullptr)
		{
			moveImitateUserLastPosition = userPtr->position;
			moveImitateUserLastOffset = userPtr->offset;
			moveImitateUserPositionInitialized = true;
		}
	}
	circleMoveBaseDirectionInitialized = false;
	circleMoveBaseDirection = { 0, 0 };
	leapHitTargets.clear();
	passThroughHitTargets.clear();
}

void Effect::attachCarryUser(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr || magic.carryUser <= 0)
	{
		return;
	}

	clearCarryUser();
	carriedUser = npc;
	carryUserActive = true;
	if (magic.hideUserWhenCarry > 0)
	{
		npc->setHiddenByCarryMagic(std::dynamic_pointer_cast<Effect>(getMySharedPtr()));
	}
	npc->stopMovement();
	npc->setPosition(position, false);
	npc->setOffset(offset);
	npc->direction = direction;
	if (magic.carryUser == 4)
	{
		handleCarryUser4NeighborCollisions();
	}
}

void Effect::clearCarryUser()
{
	auto npc = carriedUser.lock();
	if (npc != nullptr)
	{
		npc->clearHiddenByCarryMagic(this);
	}
	carriedUser.reset();
	carryUserActive = false;
}

bool Effect::beginSummon(Point summonPosition)
{
	auto caster = std::dynamic_pointer_cast<NPC>(user.lock());
	if (caster == nullptr || magic.npcFile.empty() || gm == nullptr || gm->map == nullptr || gm->npcManager == nullptr)
	{
		return false;
	}

	Point summonTile = gm->map->clampToWalkable(summonPosition);
	if (!gm->map->canWalk(summonTile))
	{
		return false;
	}
	if (caster->summonedNpcsCount(magic) >= magic.maxCount)
	{
		caster->removeFirstSummonedNpc(magic);
	}

	auto npc = std::make_shared<NPC>();
	std::unique_ptr<char[]> data;
	int len = File::readFile(NPC_INI_FOLDER + magic.npcFile, data);
	if (data == nullptr || len <= 0)
	{
		return false;
	}

	INIReader ini(data);
	npc->initFromIni(&ini, "Init");
	npc->direction = NPC::getDirection(caster->getPosition(), summonTile);
	if (caster->kind == nkPlayer || caster->relation == nrFriendly)
	{
		npc->relation = nrFriendly;
	}
	else
	{
		npc->kind = nkBattle;
		npc->relation = caster->relation;
	}

	gm->npcManager->addNPC(npc);
	npc->setPosition(summonTile, false);
	npc->transientSummonedNPC = true;
	npc->summonedByMagicEffect = std::dynamic_pointer_cast<Effect>(getMySharedPtr());
	caster->addSummonedNpc(magic, npc);
	summonedNPC = npc;
	position = summonTile;
	offset = npc->getOffset();
	direction = npc->direction;
	return true;
}

void Effect::clearSummonedNPC(bool killNPC)
{
	auto npc = summonedNPC.lock();
	summonedNPC.reset();
	if (npc == nullptr)
	{
		return;
	}

	if (auto owner = npc->summonedByMagicEffect.lock())
	{
		if (owner.get() == this)
		{
			npc->summonedByMagicEffect.reset();
		}
	}
	if (killNPC && !npc->isDying() && !npc->isHiding())
	{
		npc->life = 0;
		npc->handleDeath();
	}
}

void Effect::detachSummonedNPCAfterDeath(std::shared_ptr<NPC> npc)
{
	auto currentNPC = summonedNPC.lock();
	if (npc == nullptr || currentNPC != npc)
	{
		return;
	}

	position = npc->getPosition();
	offset = npc->getOffset();
	summonedNPC.reset();
	if (doing != ekHiding && !vanishing)
	{
		vanishing = true;
		beginExplode(position);
	}
}

bool Effect::isRangeSpeedUpActive() const
{
	return magic.rangeSpeedUp > 0
		&& doing != ekHiding
		&& !vanishing
		&& !(result & erLifeExhaust);
}

bool Effect::canBall() const
{
	return magic.ball > 0 && (doing == ekFlying || doing == ekThrowing) && !vanishing;
}

bool Effect::canSticky() const
{
	return magic.sticky > 0 && (doing == ekFlying || doing == ekThrowing) && !vanishing;
}

bool Effect::isSolidObstacle() const
{
	return magic.solid > 0 && (doing == ekFlying || doing == ekThrowing) && !vanishing && !(result & erLifeExhaust);
}

bool Effect::canBeDiscardedByOppositeMagic() const
{
	return (doing == ekFlying || doing == ekThrowing)
		&& !vanishing
		&& stickyTarget.lock() == nullptr
		&& parasiticTarget.lock() == nullptr
		&& !isMoveKindExcludedFromDiscard(magic.level[level].moveKind);
}

bool Effect::canExchangeUserByOppositeMagic() const
{
	return (doing == ekFlying || doing == ekThrowing)
		&& !vanishing
		&& stickyTarget.lock() == nullptr
		&& parasiticTarget.lock() == nullptr
		&& !isMoveKindExcludedFromExchangeUser(magic.level[level].moveKind);
}

bool Effect::isOppositeEffect(std::shared_ptr<Effect> other) const
{
	if (other == nullptr)
	{
		return false;
	}
	auto selfUser = user.lock();
	auto otherUser = other->user.lock();
	int relation = NPCManager::getRelationOf(std::dynamic_pointer_cast<GameElement>(selfUser), launcherKind);
	int otherRelation = NPCManager::getRelationOf(std::dynamic_pointer_cast<GameElement>(otherUser), other->launcherKind);
	return areOppositeRelations(relation, otherRelation);
}

bool Effect::hasAttachedNPC(std::shared_ptr<NPC> npc) const
{
	if (npc == nullptr)
	{
		return true;
	}
	if (stickyTarget.lock() == npc)
	{
		return true;
	}
	for (const auto& item : attachedNPCs)
	{
		if (item.npc.lock() == npc)
		{
			return true;
		}
	}
	return false;
}

bool Effect::skipsCharacterCollision() const
{
	if (getMoveKind() == mmkTransport || getMoveKind() == mmkControl)
	{
		return true;
	}
	if (isTimeStopper())
	{
		return true;
	}
	if (meteorMoveActive)
	{
		return true;
	}
	if (magic.carryUser == 3)
	{
		return true;
	}
	auto sticky = stickyTarget.lock();
	return sticky != nullptr && sticky->nowAction != acDeath && sticky->nowAction != acHide;
}

void Effect::attachNPCToEffect(std::shared_ptr<NPC> npc, bool preserveOffset, bool destroyOnObstacle)
{
	if (npc == nullptr || hasAttachedNPC(npc))
	{
		return;
	}

	AttachedNPC item;
	item.npc = npc;
	item.preserveOffset = preserveOffset;
	item.initialPosition = npc->getPosition();
	item.initialOffset = npc->getOffset();
	item.destroyOnObstacle = destroyOnObstacle;
	if (preserveOffset)
	{
		item.tileOffset = { npc->getPosition().x - position.x, npc->getPosition().y - position.y };
		auto npcOffset = npc->getOffset();
		item.offsetDelta = { npcOffset.x - offset.x, npcOffset.y - offset.y };
	}
	attachedNPCs.push_back(item);
	npc->stopMovement();
}

void Effect::clearAttachedNPCs()
{
	stickyTarget.reset();
	attachedNPCs.clear();
}

Point Effect::findBallFallbackPosition(Point preferredPosition) const
{
	if (gm == nullptr || gm->map == nullptr)
	{
		return preferredPosition;
	}
	if (gm->map->isInMap(preferredPosition) && gm->map->canFly(preferredPosition))
	{
		return preferredPosition;
	}
	if (gm->map->isInMap(position) && gm->map->canFly(position))
	{
		return position;
	}
	if (gm->map->isInMap(src) && gm->map->canFly(src))
	{
		return src;
	}
	for (int radius = 1; radius <= 2; radius++)
	{
		for (int dy = -radius; dy <= radius; dy++)
		{
			for (int dx = -radius; dx <= radius; dx++)
			{
				Point candidate = { preferredPosition.x + dx, preferredPosition.y + dy };
				if (gm->map->isInMap(candidate) && gm->map->canFly(candidate))
				{
					return candidate;
				}
			}
		}
	}
	return preferredPosition;
}

void Effect::updateAttachedNPCs()
{
	if (attachedNPCs.empty())
	{
		auto sticky = stickyTarget.lock();
		if (sticky == nullptr || sticky->nowAction == acDeath || sticky->nowAction == acHide)
		{
			stickyTarget.reset();
		}
		return;
	}

	if (doing == ekExploding || doing == ekHiding || (result & erLifeExhaust))
	{
		clearAttachedNPCs();
		return;
	}

	bool destroyEffect = false;
	for (auto& item : attachedNPCs)
	{
		auto npc = item.npc.lock();
		if (npc == nullptr || npc->isDying() || npc->isHiding())
		{
			item.npc.reset();
			continue;
		}

		Point targetPosition = { position.x + item.tileOffset.x, position.y + item.tileOffset.y };
		PointEx targetOffset = item.preserveOffset
			? PointEx{ offset.x + item.offsetDelta.x, offset.y + item.offsetDelta.y }
			: offset;
		if (item.destroyOnObstacle && gm != nullptr && gm->map != nullptr
			&& (!gm->map->isInMap(targetPosition) || !gm->map->canFly(targetPosition)))
		{
			Point fallback = findBallFallbackPosition(npc->getPosition());
			npc->setPosition(fallback, false);
			item.npc.reset();
			destroyEffect = true;
			continue;
		}
		npc->setPosition(targetPosition, false);
		npc->setOffset(targetOffset);
		npc->direction = direction;
		if (Map::getTileDistance(item.initialPosition, item.initialOffset, targetPosition, targetOffset) > 0.5f)
		{
			item.hasMoved = true;
		}
	}

	attachedNPCs.erase(
		std::remove_if(attachedNPCs.begin(), attachedNPCs.end(), [](const AttachedNPC& item)
		{
			return item.npc.expired();
		}),
		attachedNPCs.end());
	auto sticky = stickyTarget.lock();
	if (sticky == nullptr || sticky->nowAction == acDeath || sticky->nowAction == acHide)
	{
		stickyTarget.reset();
	}

	if (destroyEffect && doing != ekExploding && doing != ekHiding)
	{
		beginExplode(position);
	}
}

void Effect::addDestroyVisualEffect(Point hitPosition)
{
	if (gm == nullptr || gm->effectManager == nullptr)
	{
		return;
	}

	auto destroyEffect = std::make_shared<Effect>();
	destroyEffect->user = user;
	destroyEffect->level = level;
	destroyEffect->initFromMagic(std::make_shared<Magic>(magic), magicDispatchContext);
	destroyEffect->direction = direction;
	destroyEffect->flyingDirection = flyingDirection;
	destroyEffect->launcherKind = launcherKind;
	destroyEffect->damage = 0;
	destroyEffect->damage2 = 0;
	destroyEffect->damage3 = 0;
	destroyEffect->damageMana = 0;
	destroyEffect->evade = evade;
	destroyEffect->position = hitPosition;
	destroyEffect->src = hitPosition;
	destroyEffect->offset = getCollideOffset(hitPosition);
	destroyEffect->srcOffset = destroyEffect->offset;
	destroyEffect->beginExplode(hitPosition);
	gm->effectManager->addEffect(destroyEffect);
}

void Effect::reflectBallFromPoint(Point hitPosition, PointEx normalPoint)
{
	PointEx effectPoint = getWorldPosition(position, offset);
	float normalX = effectPoint.x - normalPoint.x;
	float normalY = effectPoint.y - normalPoint.y;
	float normalLength = hypot(normalX, normalY);
	float dirX = (float)flyingDirection.x;
	float dirY = (float)flyingDirection.y;
	if (normalLength <= 0.001f || (dirX == 0.0f && dirY == 0.0f))
	{
		flyingDirection.x = -flyingDirection.x;
		flyingDirection.y = -flyingDirection.y;
	}
	else
	{
		normalX /= normalLength;
		normalY /= normalLength;
		float dot = dirX * normalX + dirY * normalY;
		flyingDirection.x = (int)round(dirX - 2.0f * dot * normalX);
		flyingDirection.y = (int)round(dirY - 2.0f * dot * normalY);
		if (flyingDirection.is_zero())
		{
			flyingDirection.x = -(int)round(dirX);
			flyingDirection.y = -(int)round(dirY);
		}
	}
	direction = getDirection(flyingDirection);
	Point nextPosition = Map::getSubPoint(position, direction);
	if (gm != nullptr && gm->map != nullptr && gm->map->isInMap(nextPosition) && gm->map->canFly(nextPosition))
	{
		position = nextPosition;
		offset = { 0, 0 };
	}
	src = position;
	srcOffset = offset;
	calDest();
	passPath.clear();
	addDestroyVisualEffect(hitPosition);
}

void Effect::reflectBallFromWall(Point hitPosition)
{
	bool reflected = false;
	int sx = signum(flyingDirection.x);
	int sy = signum(flyingDirection.y);
	if (gm != nullptr && gm->map != nullptr)
	{
		Point horizontal = { position.x + sx, position.y };
		Point vertical = { position.x, position.y + sy };
		if (sx != 0 && (!gm->map->isInMap(horizontal) || !gm->map->canFly(horizontal) || horizontal == hitPosition))
		{
			flyingDirection.x = -flyingDirection.x;
			reflected = true;
		}
		if (sy != 0 && (!gm->map->isInMap(vertical) || !gm->map->canFly(vertical) || vertical == hitPosition))
		{
			flyingDirection.y = -flyingDirection.y;
			reflected = true;
		}
	}
	if (!reflected)
	{
		flyingDirection.x = -flyingDirection.x;
		flyingDirection.y = -flyingDirection.y;
	}
	if (flyingDirection.is_zero())
	{
		flyingDirection = { -1, 0 };
	}
	direction = getDirection(flyingDirection);
	src = position;
	srcOffset = offset;
	calDest();
	passPath.clear();
	addDestroyVisualEffect(hitPosition);
}

bool Effect::handleBallAfterHit(std::shared_ptr<NPC> hitTarget, Point hitPosition)
{
	if (!canBall() || hitTarget == nullptr)
	{
		return false;
	}
	auto targetPoint = getWorldPosition(hitTarget->getPosition(), hitTarget->getOffset());
	reflectBallFromPoint(hitPosition, targetPoint);
	return true;
}

bool Effect::handleBallWallCollision(Point hitPosition, Point fallbackPosition)
{
	if (!canBall())
	{
		return false;
	}
	position = findBallFallbackPosition(fallbackPosition);
	offset = { 0, 0 };
	reflectBallFromWall(hitPosition);
	return true;
}

bool Effect::handleStickyAfterHit(std::shared_ptr<NPC> hitTarget)
{
	if (!canSticky() || hitTarget == nullptr || stickyTarget.lock() != nullptr)
	{
		return false;
	}
	attachNPCToEffect(hitTarget, false, false);
	stickyTarget = hitTarget;
	if (magic.moveBack > 0)
	{
		beginMoveBack();
	}
	return true;
}

bool Effect::handleCarryUser4AfterHit(std::shared_ptr<NPC> hitTarget)
{
	if (magic.carryUser != 4 || !carryUserActive || hitTarget == nullptr || hasAttachedNPC(hitTarget))
	{
		return false;
	}
	attachNPCToEffect(hitTarget, true, true);
	return true;
}

void Effect::handleCarryUser4NeighborCollisions()
{
	if (magic.carryUser != 4 || !carryUserActive || gm == nullptr || gm->map == nullptr)
	{
		return;
	}
	auto selfEffect = std::dynamic_pointer_cast<Effect>(getMySharedPtr());
	if (selfEffect == nullptr)
	{
		return;
	}

	std::vector<std::shared_ptr<NPC>> seedNPCs;
	auto carried = carriedUser.lock();
	if (carried != nullptr && !carried->isDying() && !carried->isHiding())
	{
		seedNPCs.push_back(carried);
	}
	for (const auto& item : attachedNPCs)
	{
		auto npc = item.npc.lock();
		if (npc != nullptr && !npc->isDying() && !npc->isHiding())
		{
			seedNPCs.push_back(npc);
		}
	}
	if (seedNPCs.empty())
	{
		return;
	}

	std::vector<std::shared_ptr<NPC>> candidates;
	auto collectCandidate = [&](std::shared_ptr<NPC> candidate)
	{
		if (candidate == nullptr || candidate->nowAction == acDeath || candidate->nowAction == acHide || candidate->getJumpState() == jsJumping)
		{
			return;
		}
		if (hasAttachedNPC(candidate) || !NPCManager::canLauncherHitNPC(launcherKind, candidate, user.lock()))
		{
			return;
		}
		if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
		{
			candidates.push_back(candidate);
		}
	};

	for (const auto& npc : seedNPCs)
	{
		Point center = npc->getPosition();
		for (int dy = -1; dy <= 1; dy++)
		{
			for (int dx = -1; dx <= 1; dx++)
			{
				Point tilePosition = { center.x + dx, center.y + dy };
				if (!gm->map->isInMap(tilePosition))
				{
					continue;
				}
				auto& tile = gm->map->dataMap.tile[tilePosition.y][tilePosition.x];
				for (auto& candidate : tile.npcList)
				{
					collectCandidate(candidate);
				}
				for (auto& candidate : tile.stepNPCList)
				{
					collectCandidate(candidate);
				}
			}
		}
	}
	auto isNearAnySeed = [&](std::shared_ptr<NPC> candidate)
	{
		if (candidate == nullptr)
		{
			return false;
		}
		Point candidatePosition = candidate->getPosition();
		for (const auto& seed : seedNPCs)
		{
			if (seed == nullptr)
			{
				continue;
			}
			Point seedPosition = seed->getPosition();
			if (std::abs(candidatePosition.x - seedPosition.x) <= 1 && std::abs(candidatePosition.y - seedPosition.y) <= 1)
			{
				return true;
			}
		}
		return false;
	};
	if (gm->npcManager != nullptr)
	{
		for (auto& candidate : gm->npcManager->npcList)
		{
			if (isNearAnySeed(candidate))
			{
				collectCandidate(candidate);
			}
		}
	}
	if (gm->player != nullptr && isNearAnySeed(gm->player))
	{
		collectCandidate(gm->player);
	}

	for (auto& candidate : candidates)
	{
		if (handleCarryUser4AfterHit(candidate))
		{
			candidate->hurt(selfEffect);
		}
	}
}

bool Effect::handleDiscardOppositeMagic(std::shared_ptr<Effect> other)
{
	if (magic.discardOppositeMagic <= 0 || other == nullptr || other.get() == this || position != other->position)
	{
		return false;
	}
	if (!isOppositeEffect(other) || !other->canBeDiscardedByOppositeMagic())
	{
		return false;
	}
	other->doing = ekHiding;
	other->result = erLifeExhaust;
	doing = ekHiding;
	result = erLifeExhaust;
	return true;
}

bool Effect::handleExchangeUserWithOppositeMagic(std::shared_ptr<Effect> other)
{
	if (magic.exchangeUser <= 0 || other == nullptr || other.get() == this || position != other->position)
	{
		return false;
	}
	if (!isOppositeEffect(other) || !other->canExchangeUserByOppositeMagic())
	{
		return false;
	}

	auto otherDirection = normalizeDirection(other->flyingDirection);
	auto selfDirection = normalizeDirection(flyingDirection);
	float otherSpeed = (float)other->speed;
	float selfSpeed = (float)speed;
	float combinedX = otherDirection.x * otherSpeed + selfDirection.x * selfSpeed;
	float combinedY = otherDirection.y * otherSpeed + selfDirection.y * selfSpeed;
	float combinedSpeed = hypot(combinedX, combinedY);
	if (combinedSpeed > 0.001f)
	{
		other->flyingDirection.x = (int)round(combinedX / combinedSpeed * 1000.0f);
		other->flyingDirection.y = (int)round(combinedY / combinedSpeed * 1000.0f);
		other->magic.level[other->level].speed = std::max(1, (int)round(combinedSpeed));
		other->speed = other->magic.level[other->level].speed;
		other->direction = other->getDirection(other->flyingDirection);
	}
	else
	{
		other->flyingDirection = { 0, 0 };
		other->magic.level[other->level].speed = 0;
		other->speed = 0;
	}
	other->src = other->position;
	other->srcOffset = other->offset;
	other->calDest();
	other->passPath.clear();
	other->user = user;
	other->launcherKind = launcherKind;
	doing = ekHiding;
	result = erLifeExhaust;
	return true;
}

int Effect::getAttachedNPCCount() const
{
	int count = 0;
	auto sticky = stickyTarget.lock();
	for (const auto& item : attachedNPCs)
	{
		auto npc = item.npc.lock();
		if (npc != nullptr && !npc->isDying() && !npc->isHiding())
		{
			count++;
		}
	}
	if (sticky != nullptr && !sticky->isDying() && !sticky->isHiding())
	{
		bool stickyAlreadyCounted = false;
		for (const auto& item : attachedNPCs)
		{
			if (item.npc.lock() == sticky)
			{
				stickyAlreadyCounted = true;
				break;
			}
		}
		if (!stickyAlreadyCounted)
		{
			count++;
		}
	}
	return count;
}

int Effect::getMovedAttachedNPCCount() const
{
	int count = 0;
	for (const auto& item : attachedNPCs)
	{
		auto npc = item.npc.lock();
		if (npc != nullptr && !npc->isDying() && !npc->isHiding() && item.hasMoved)
		{
			count++;
		}
	}
	return count;
}

bool Effect::hasStickyTarget() const
{
	auto sticky = stickyTarget.lock();
	return sticky != nullptr && !sticky->isDying() && !sticky->isHiding();
}

bool Effect::hasAnyAttachedNPC() const
{
	return getAttachedNPCCount() > 0;
}

bool Effect::hasMovedAttachedNPC() const
{
	return getMovedAttachedNPCCount() > 0;
}

void Effect::updateCarryUserPosition()
{
	if (!carryUserActive)
	{
		return;
	}

	auto npc = carriedUser.lock();
	if (npc == nullptr
		|| !NPCManager::isManagedEffectCaster(npc)
		|| npc->isDying()
		|| npc->isHiding()
		|| (result & erLifeExhaust))
	{
		clearCarryUser();
		return;
	}

	if (doing == ekExploding && magic.hideUserWhenCarry <= 0)
	{
		clearCarryUser();
		return;
	}

	npc->setPosition(position, false);
	npc->setOffset(offset);
	npc->direction = direction;
}

void Effect::finishParasiticEffect()
{
	parasiticTarget.reset();
	parasiticElapsedMilliseconds = 0;
	parasiticTotalEffect = 0;
}

void Effect::finishTransportEffect()
{
	if (transportFinished || getMoveKind() != mmkTransport)
	{
		return;
	}

	transportFinished = true;
	auto caster = std::dynamic_pointer_cast<NPC>(user.lock());
	if (caster == nullptr)
	{
		return;
	}
	if (!NPCManager::isManagedEffectCaster(caster))
	{
		caster->clearTransportEffect(this);
		return;
	}

	if (gm != nullptr && gm->map != nullptr)
	{
		auto destination = resolveMagicTransportDestination(dest, [](Point candidate)
		{
			return gm != nullptr && gm->map != nullptr && gm->map->isInMap(candidate) && gm->map->canWalk(candidate);
		});
		if (destination.has_value())
		{
			caster->setOffset({ 0.0f, 0.0f });
			caster->setPosition(*destination, false);
			caster->setOffset({ 0.0f, 0.0f });
		}
	}

	caster->clearTransportEffect(this);
}

void Effect::clearTransportEffectState()
{
	if (getMoveKind() != mmkTransport)
	{
		return;
	}

	if (auto caster = std::dynamic_pointer_cast<NPC>(user.lock()))
	{
		caster->clearTransportEffect(this);
	}
}

void Effect::finishControlEffect()
{
	if (controlFinished || getMoveKind() != mmkControl)
	{
		return;
	}

	controlFinished = true;
	if (auto player = std::dynamic_pointer_cast<Player>(user.lock()))
	{
		player->endControlCharacter(this);
	}
}

void Effect::clearControlEffectState()
{
	if (getMoveKind() != mmkControl)
	{
		return;
	}

	if (auto player = std::dynamic_pointer_cast<Player>(user.lock()))
	{
		player->endControlCharacter(this);
	}
}

void Effect::updateParasiticEffect(UTime frameTime)
{
	const auto& linked = magic.getLinkedLevel(level);
	auto targetNPC = parasiticTarget.lock();
	if (targetNPC == nullptr || targetNPC->isDying() || targetNPC->isHiding())
	{
		finishParasiticEffect();
		doing = ekHiding;
		vanishing = false;
		result = erLifeExhaust;
		return;
	}

	position = targetNPC->getPosition();
	offset = targetNPC->getOffset();
	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		finishParasiticEffect();
		doing = ekHiding;
		vanishing = false;
		result = erLifeExhaust;
		return;
	}

	parasiticElapsedMilliseconds += frameTime;
	if (linked.parasiticInterval > 0 && parasiticElapsedMilliseconds < linked.parasiticInterval)
	{
		return;
	}
	if (linked.parasiticInterval > 0)
	{
		parasiticElapsedMilliseconds -= linked.parasiticInterval;
	}
	else
	{
		parasiticElapsedMilliseconds = 0;
	}

	if (linked.parasiticMagic != nullptr && linked.parasiticMagic->loadSucceeded && userPtr != nullptr)
	{
		Point magicTo = position;
		if (!flyingDirection.is_zero())
		{
			magicTo = Map::getSubPoint(position, direction / 2);
		}
		else
		{
			Point delta = position - userPtr->position;
			magicTo = position + delta;
			if (magicTo == position)
			{
				magicTo = Map::getSubPoint(position, direction / 2);
			}
		}
		auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
		const int childDamage = getPrimaryEffectAmount(*linked.parasiticMagic, level, caster);
		const int childEvade = caster != nullptr ? caster->getEvade() : evade;
		auto childContext = Magic::createDerivedDispatchContext(
			magicDispatchContext,
			linked.parasiticMagic,
			"ParasiticMagic");
		if (childContext != nullptr)
		{
			Magic::addEffect(
				linked.parasiticMagic,
				userPtr,
				position,
				magicTo,
				level,
				childDamage,
				childEvade,
				launcherKind,
				targetNPC,
				childContext);
		}
	}

	auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
	auto tickMagic = std::make_shared<Magic>();
	tickMagic->copy(magic);
	auto tickEffect = std::make_shared<Effect>();
	tickEffect->user = user;
	tickEffect->target = targetNPC;
	tickEffect->level = level;
	tickEffect->initFromMagic(tickMagic, magicDispatchContext);
	tickEffect->launcherKind = launcherKind;
	tickEffect->additionalEffect = additionalEffect;
	tickEffect->position = targetNPC->getPosition();
	tickEffect->offset = targetNPC->getOffset();
	tickEffect->src = position;
	tickEffect->srcOffset = offset;
	tickEffect->dest = targetNPC->getPosition();
	tickEffect->destOffset = targetNPC->getOffset();
	tickEffect->damage = caster != nullptr
		? caster->applyMagicEffectBonus(magic, getPrimaryEffectAmount(magic, level, caster))
		: getPrimaryEffectAmount(magic, level, caster);
	tickEffect->damage2 = getEffectAmount2(magic, level, caster);
	tickEffect->damage3 = getEffectAmount3(magic, level, caster);
	tickEffect->damageMana = magic.level[clampMagicLevel(level)].effectMana;
	tickEffect->evade = evade;
	tickEffect->flyingDirection = flyingDirection;
	tickEffect->direction = direction;
	targetNPC->directHurt(tickEffect);
	recordParasiticDamage(tickEffect->damage);

	if (linked.parasiticMaxEffect > 0 && parasiticTotalEffect >= linked.parasiticMaxEffect)
	{
		finishParasiticEffect();
		doing = ekHiding;
		vanishing = false;
		result = erLifeExhaust;
	}
}

bool Effect::canLeap() const
{
	return leapTimesRemaining > 0;
}

bool Effect::shouldExplodeWhenLifeFrameEnds() const
{
	if (magic.noExplodeWhenLifeFrameEnd > 0)
	{
		return false;
	}
	if (magic.explodeWhenLifeFrameEnd > 0)
	{
		return true;
	}
	const int moveKind = magic.level[level].moveKind;
	return moveKind == mmkPoint || moveKind == mmkRegion;
}

bool Effect::hasLeapHitTarget(std::shared_ptr<NPC> npc) const
{
	if (npc == nullptr)
	{
		return true;
	}
	for (const auto& weakTarget : leapHitTargets)
	{
		if (weakTarget.lock() == npc)
		{
			return true;
		}
	}
	return false;
}

bool Effect::canPassThrough() const
{
	return magic.passThrough > 0;
}

bool Effect::canPassThroughWall() const
{
	return magic.passThroughWall > 0;
}

bool Effect::canParasitic() const
{
	return magic.getLinkedLevel(level).parasitic > 0;
}

bool Effect::hasPassThroughHitTarget(std::shared_ptr<NPC> npc) const
{
	if (npc == nullptr)
	{
		return true;
	}
	for (const auto& weakTarget : passThroughHitTargets)
	{
		if (weakTarget.lock() == npc)
		{
			return true;
		}
	}
	return false;
}

void Effect::addPassThroughDestroyEffect(Point hitPosition)
{
	if (magic.passThroughWithDestroyEffect <= 0 || gm == nullptr || gm->effectManager == nullptr)
	{
		return;
	}

	auto destroyEffect = std::make_shared<Effect>();
	destroyEffect->user = user;
	destroyEffect->level = level;
	destroyEffect->initFromMagic(std::make_shared<Magic>(magic), magicDispatchContext);
	destroyEffect->direction = direction;
	destroyEffect->flyingDirection = flyingDirection;
	destroyEffect->launcherKind = launcherKind;
	destroyEffect->damage = 0;
	destroyEffect->damage2 = 0;
	destroyEffect->damage3 = 0;
	destroyEffect->damageMana = 0;
	destroyEffect->evade = evade;
	destroyEffect->position = hitPosition;
	destroyEffect->src = hitPosition;
	destroyEffect->offset = getCollideOffset(hitPosition);
	destroyEffect->srcOffset = destroyEffect->offset;
	destroyEffect->beginExplode(hitPosition);
	gm->effectManager->addEffect(destroyEffect);
}

bool Effect::handlePassThroughAfterHit(std::shared_ptr<NPC> hitTarget, Point hitPosition)
{
	if (hitTarget == nullptr || !canPassThrough())
	{
		return false;
	}
	passThroughHitTargets.push_back(hitTarget);
	addPassThroughDestroyEffect(hitPosition);
	return true;
}

bool Effect::beginParasitic(std::shared_ptr<NPC> hitTarget, Point hitPosition)
{
	if (!canParasitic() || hitTarget == nullptr)
	{
		return false;
	}

	parasiticTarget = hitTarget;
	parasiticElapsedMilliseconds = 0;
	parasiticTotalEffect = 0;
	position = hitTarget->getPosition();
	offset = hitTarget->getOffset();
	doing = ekExploding;
	vanishing = true;
	result = erNone;
	beginTime = getTime();
	lifeTime = getExplodinUTime();
	playSound(ekExploding);

	auto self = std::dynamic_pointer_cast<Effect>(getMySharedPtr());
	if (self != nullptr)
	{
		hitTarget->directHurt(self);
		recordParasiticDamage(damage);
	}
	triggerExplodeMagic(hitPosition);
	return true;
}

void Effect::recordParasiticDamage(int amount)
{
	if (amount > 0)
	{
		parasiticTotalEffect += amount;
	}
}

std::shared_ptr<NPC> Effect::findNextLeapTarget(Point fromPosition) const
{
	if (gm == nullptr || gm->npcManager == nullptr || gm->map == nullptr)
	{
		return nullptr;
	}

	auto caster = std::dynamic_pointer_cast<NPC>(user.lock());
	std::shared_ptr<NPC> nearestTarget = nullptr;
	float nearestDistance = 99999999.0f;
	auto checkCandidate = [&](std::shared_ptr<NPC> candidate)
	{
		if (candidate == nullptr || candidate == caster || hasLeapHitTarget(candidate))
		{
			return;
		}
		if (candidate->nowAction == acDeath || candidate->nowAction == acHide || !candidate->isVisibleForRuntime())
		{
			return;
		}
		if (!isAttackAllFighterTarget(candidate))
		{
			return;
		}
		if (candidate != gm->player && !gm->npcManager->findNPC(candidate))
		{
			return;
		}
		if (magic.attackAll <= 0 && !NPCManager::canLauncherHitNPC(launcherKind, candidate, user.lock()))
		{
			return;
		}
		float distance = Map::getTileDistance(fromPosition, { 0.0f, 0.0f }, candidate->getPosition(), candidate->getOffset());
		if (distance < nearestDistance)
		{
			nearestDistance = distance;
			nearestTarget = candidate;
		}
	};

	checkCandidate(gm->player);
	for (auto& npc : gm->npcManager->npcList)
	{
		checkCandidate(npc);
	}
	return nearestTarget;
}

bool Effect::handleLeapAfterHit(std::shared_ptr<NPC> hitTarget)
{
	if (hitTarget == nullptr || !canLeap())
	{
		return false;
	}

	Point hitPosition = hitTarget->getPosition();
	leapHitTargets.push_back(hitTarget);
	leapTimesRemaining--;

	int reducePercentage = magic.level[level].effectReducePercentage;
	if (reducePercentage > 0)
	{
		MagicDamageChannels reducedChannels = reduceMagicDamageChannels({ damage, damage2, damage3, damageMana }, reducePercentage);
		damage = reducedChannels.damage;
		damage2 = reducedChannels.damage2;
		damage3 = reducedChannels.damage3;
		damageMana = reducedChannels.damageMana;
	}

	auto nextTarget = findNextLeapTarget(hitPosition);
	if (nextTarget == nullptr)
	{
		doing = ekHiding;
		result = erLifeExhaust;
		return true;
	}

	position = hitPosition;
	offset = hitTarget->getOffset();
	src = position;
	srcOffset = offset;
	changeFollowTarget(nextTarget);
	leapFlying = true;
	doing = ekFlying;
	beginTime = getTime();
	lifeTime = (UTime)((float)magic.level[level].leapFrame * EFFECT_FRAME_TIME);
	if (lifeTime == 0)
	{
		lifeTime = getFlyinUTime();
	}
	passPath.clear();
	playSound(ekFlying);
	return true;
}

void Effect::updateFlyMagic(UTime frameTime)
{
	if (level < 1 || level > MAGIC_MAX_LEVEL)
	{
		return;
	}
	const auto& linked = magic.getLinkedLevel(level);
	if (linked.flyMagic == nullptr || !linked.flyMagic->loadSucceeded)
	{
		return;
	}
	if (doing != ekFlying && doing != ekThrowing)
	{
		return;
	}

	flyMagicElapsedMilliseconds += frameTime;
	if (linked.flyInterval > 0)
	{
		if (flyMagicElapsedMilliseconds < linked.flyInterval)
		{
			return;
		}
		flyMagicElapsedMilliseconds -= linked.flyInterval;
	}
	else
	{
		flyMagicElapsedMilliseconds = 0;
	}

	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		return;
	}

	Point flyTo = dest;
	if (flyTo == position)
	{
		flyTo = userPtr->position;
	}
	if (flyTo == position)
	{
		flyTo = Map::getSubPoint(position, direction / 2);
	}

	int childDamage = linked.flyMagic->level[level].effect;
	int childEvade = evade;
	auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
	if (caster != nullptr)
	{
		if (childDamage == 0 || caster->kind != nkPlayer)
		{
			childDamage = caster->getAttack();
		}
		childEvade = caster->getEvade();
	}
	auto childContext = Magic::createDerivedDispatchContext(
		magicDispatchContext,
		linked.flyMagic,
		"FlyMagic");
	if (childContext != nullptr)
	{
		Magic::addEffect(
			linked.flyMagic,
			userPtr,
			position,
			flyTo,
			level,
			childDamage,
			childEvade,
			launcherKind,
			nullptr,
			childContext);
	}
}

void Effect::updateMagicWhenNewPosition()
{
	const auto& linked = magic.getLinkedLevel(level);
	if (linked.magicWhenNewPosition == nullptr || !linked.magicWhenNewPosition->loadSucceeded || gm == nullptr || gm->effectManager == nullptr)
	{
		return;
	}
	if (!magicWhenNewPositionInitialized)
	{
		magicWhenNewPositionLastTile = position;
		magicWhenNewPositionInitialized = true;
		return;
	}
	if (magicWhenNewPositionLastTile == position)
	{
		return;
	}

	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		magicWhenNewPositionLastTile = position;
		return;
	}

	auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
	const int childDamage = getPrimaryEffectAmount(*linked.magicWhenNewPosition, level, caster);
	const int childEvade = caster != nullptr ? caster->getEvade() : evade;
	auto childContext = Magic::createDerivedDispatchContext(
		magicDispatchContext,
		linked.magicWhenNewPosition,
		"MagicWhenNewPos");
	if (childContext == nullptr)
	{
		magicWhenNewPositionLastTile = position;
		return;
	}
	auto childEffect = Magic::createFixedEffect(
		linked.magicWhenNewPosition,
		userPtr,
		magicWhenNewPositionLastTile,
		{ 0.0f, 0.0f },
		level,
		childDamage,
		childEvade,
		launcherKind,
		childContext);
	gm->effectManager->addEffect(childEffect);
	childEffect->beginTime = childEffect->getTime();
	magicWhenNewPositionLastTile = position;
}

void Effect::triggerExplodeMagic(Point explodePosition)
{
	auto explodeMagic = magic.getExplodeMagicForLevel(level);
	if (explodeMagicTriggered || explodeMagic == nullptr || !explodeMagic->loadSucceeded)
	{
		return;
	}
	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		return;
	}

	explodeMagicTriggered = true;
	Point magicTo = explodePosition;
	if (!flyingDirection.is_zero())
	{
		magicTo = Map::getSubPoint(explodePosition, direction / 2);
	}
	else
	{
		Point delta = explodePosition - userPtr->position;
		magicTo = explodePosition + delta;
		if (magicTo == explodePosition)
		{
			magicTo = Map::getSubPoint(explodePosition, direction / 2);
		}
	}

	auto caster = std::dynamic_pointer_cast<NPC>(userPtr);
	const int childDamage = getPrimaryEffectAmount(*explodeMagic, level, caster);
	const int childEvade = caster != nullptr ? caster->getEvade() : evade;
	auto childContext = Magic::createDerivedDispatchContext(
		magicDispatchContext,
		explodeMagic,
		"ExplodeMagicFile");
	if (childContext != nullptr)
	{
		Magic::addEffect(
			explodeMagic,
			userPtr,
			explodePosition,
			magicTo,
			level,
			childDamage,
			childEvade,
			launcherKind,
			nullptr,
			childContext);
	}
}

void Effect::updateRangeEffect(UTime frameTime)
{
	if (magic.rangeEffect <= 0 || gm == nullptr || gm->npcManager == nullptr || gm->map == nullptr)
	{
		return;
	}
	if (level < 1 || level > MAGIC_MAX_LEVEL)
	{
		return;
	}
	auto& levelInfo = magic.level[level];
	bool hasFriendEffect = levelInfo.rangeAddLife > 0
		|| levelInfo.rangeAddMana > 0
		|| levelInfo.rangeAddThew > 0
		|| magic.rangeSpeedUp > 0
		|| (gm->global.feature.rageSystem && levelInfo.rangeAddRage > 0);
	bool hasAttackEffect = levelInfo.rangeFreezeMilliseconds > 0
		|| levelInfo.rangePoisonMilliseconds > 0
		|| levelInfo.rangePetrifyMilliseconds > 0
		|| levelInfo.rangeDamage > 0;
	if (!hasFriendEffect && !hasAttackEffect)
	{
		return;
	}

	rangeElapsedMilliseconds += frameTime;
	if (magic.rangeTimeInterval > 0)
	{
		if (rangeElapsedMilliseconds < magic.rangeTimeInterval)
		{
			return;
		}
		rangeElapsedMilliseconds -= magic.rangeTimeInterval;
	}
	else
	{
		rangeElapsedMilliseconds = 0;
	}

	auto caster = std::dynamic_pointer_cast<NPC>(user.lock());
	if (caster == nullptr)
	{
		return;
	}
	auto selfEffect = std::dynamic_pointer_cast<Effect>(getMySharedPtr());

	auto isLiveVisibleTarget = [&](std::shared_ptr<NPC> target)
	{
		if (target == nullptr || !target->isVisibleForRuntime() || target->nowAction == acDeath || target->nowAction == acHide)
		{
			return false;
		}
		if (magic.rangeRadius > 0 && gm->map->calDistance(position, target->getPosition()) > magic.rangeRadius)
		{
			return false;
		}
		return true;
	};

	auto targetRelation = [&](std::shared_ptr<NPC> target)
	{
		return target == gm->player ? nrFriendly : target->relation;
	};

	auto canReceiveRangeFriendEffect = [&](std::shared_ptr<NPC> target)
	{
		if (!isLiveVisibleTarget(target))
		{
			return false;
		}
		if (magic.rangeRadius == 0)
		{
			return target == caster;
		}
		return targetRelation(target) == targetRelation(caster);
	};

	auto canReceiveRangeAttackEffect = [&](std::shared_ptr<NPC> target)
	{
		if (!isLiveVisibleTarget(target))
		{
			return false;
		}
		if (magic.attackAll > 0)
		{
			return isAttackAllFighterTarget(target);
		}
		if (target == caster)
		{
			return false;
		}
		if (target != gm->player && target->kind != nkBattle && !(target->kind == nkPartner && gm->global.data.PartnerCombat))
		{
			return false;
		}
		return NPCManager::canLauncherHitNPC(launcherKind, target, user.lock());
	};

	auto applyRangeFriendEffect = [&](std::shared_ptr<NPC> target)
	{
		if (!canReceiveRangeFriendEffect(target))
		{
			return;
		}
		if (levelInfo.rangeAddLife > 0)
		{
			target->addLife(levelInfo.rangeAddLife);
		}
		if (levelInfo.rangeAddMana > 0)
		{
			target->addMana(levelInfo.rangeAddMana);
		}
		if (levelInfo.rangeAddThew > 0)
		{
			target->addThew(levelInfo.rangeAddThew);
		}
		if (gm->global.feature.rageSystem && levelInfo.rangeAddRage > 0)
		{
			if (auto player = std::dynamic_pointer_cast<Player>(target))
			{
				player->addRage(-levelInfo.rangeAddRage);
			}
		}
		if (magic.rangeSpeedUp > 0 && selfEffect != nullptr)
		{
			target->applyRangeSpeedUp(selfEffect);
		}
	};

	auto applyRangeAttackEffect = [&](std::shared_ptr<NPC> target)
	{
		if (!canReceiveRangeAttackEffect(target))
		{
			return;
		}
		bool showSpecialKindVisualEffect = magic.noSpecialKindEffect <= 0;
		if (levelInfo.rangeFreezeMilliseconds > 0 && !target->petrified && !target->frozen)
		{
			target->frozen = true;
			target->frozenLastTime = levelInfo.rangeFreezeMilliseconds;
			target->frozenVisualEffect = showSpecialKindVisualEffect;
		}
		if (levelInfo.rangePoisonMilliseconds > 0 && !target->petrified && !target->poisoned)
		{
			target->poisoned = true;
			target->poisonedLastTime = levelInfo.rangePoisonMilliseconds;
			target->poisonedDamageTimer = 0;
			target->poisonedVisualEffect = showSpecialKindVisualEffect;
			if (caster == gm->player || caster->kind == nkPartner)
			{
				target->rememberPoisonSource(caster);
			}
		}
		if (levelInfo.rangePetrifyMilliseconds > 0 && !target->petrified)
		{
			target->petrified = true;
			target->petrifiedLastTime = levelInfo.rangePetrifyMilliseconds;
			target->petrifiedVisualEffect = showSpecialKindVisualEffect;
			target->clearFrozenState();
		}
		if (levelInfo.rangeDamage > 0)
		{
			MagicDamageChannels rangeDamageChannels = makeRangeDamageChannels(levelInfo);
			auto rangeDamageMagic = std::make_shared<Magic>();
			rangeDamageMagic->copy(magic);
			auto rangeDamageEffect = std::make_shared<Effect>();
			rangeDamageEffect->user = user;
			rangeDamageEffect->level = level;
			rangeDamageEffect->initFromMagic(rangeDamageMagic, magicDispatchContext);
			rangeDamageEffect->launcherKind = launcherKind;
			rangeDamageEffect->additionalEffect = additionalEffect;
			rangeDamageEffect->position = target->getPosition();
			rangeDamageEffect->offset = target->getOffset();
			rangeDamageEffect->src = position;
			rangeDamageEffect->srcOffset = offset;
			rangeDamageEffect->dest = target->getPosition();
			rangeDamageEffect->damage = rangeDamageChannels.damage;
			rangeDamageEffect->damage2 = rangeDamageChannels.damage2;
			rangeDamageEffect->damage3 = rangeDamageChannels.damage3;
			rangeDamageEffect->damageMana = rangeDamageChannels.damageMana;
			rangeDamageEffect->evade = evade;
			rangeDamageEffect->flyingDirection = Map::getTilePosition(target->getPosition(), position);
			rangeDamageEffect->direction = rangeDamageEffect->getDirection();
			target->hurt(rangeDamageEffect);
		}
	};

	if (hasFriendEffect)
	{
		applyRangeFriendEffect(gm->player);
		for (auto& npc : gm->npcManager->npcList)
		{
			applyRangeFriendEffect(npc);
		}
	}
	if (hasAttackEffect)
	{
		applyRangeAttackEffect(gm->player);
		for (auto& npc : gm->npcManager->npcList)
		{
			applyRangeAttackEffect(npc);
		}
	}
}

void Effect::calTime()
{
	lifeTime = (unsigned int)(((float)magic.level[level].lifeFrame) * EFFECT_FRAME_TIME);
	waitTime = (unsigned int)(((float)magic.level[level].waitFrame) * EFFECT_FRAME_TIME);
}

//计算一个较远处的目标，新版轨迹计算已不需要
void Effect::calDest()
{
	dest = src;
	if (flyingDirection.is_zero())
	{
		return;
	}
	int distance = (int)((speed + 5) * MAGIC_FLYING_SPEED_SCALE * Config::getGameSpeed() * ((float)lifeTime + 5000));
	if (flyingDirection.x < 10 || flyingDirection.y < 10)
	{
		dest.x = flyingDirection.x * 100 * distance + src.x;
		dest.y = (int)round(((float)flyingDirection.y) * 100 * distance * MapXRatio) + src.y;
	}
	else
	{
		dest.x = flyingDirection.x * distance * 10 + src.x;
		dest.y = (int)round(((float)flyingDirection.y) * distance * 10 * MapXRatio) + src.y;
	}
}

auto Effect::getPassPath(Point from, PointEx fromOffset, Point to, PointEx toOffset)
{
	std::deque<Point> result, tempPath[3];
	if (flyingDirection.is_zero())
	{
		return result;
	}
	std::vector<float> resultDistance;
	PointEx distanceOffset[3];
	distanceOffset[0] = { 0, 0 };
	tempPath[0] = gm->map->getPassPathEx(from, fromOffset, to, toOffset, flyingDirection);
	float l = hypot(flyingDirection.x, flyingDirection.y);
	int tempX = convert_max((int)round(((float)flyingDirection.x) / l * width * TILE_WIDTH / 2) - 1, 1);
	int tempY = convert_max((int)round((((float)flyingDirection.y) / l * width * TILE_WIDTH / 2) - 1), 1);
	Point tempFrom, tempTo;
	PointEx tempFromOffset, tempToOffset;
	getNewPosition(from, { fromOffset.x + tempY , fromOffset.y - tempX }, &tempFrom, &tempFromOffset);
	getNewPosition(to, { toOffset.x + tempY , toOffset.y - tempX }, &tempTo, &tempToOffset);
	distanceOffset[1] = { (float)tempY, (float)- tempX};
	tempPath[1] = gm->map->getPassPathEx(tempFrom, tempFromOffset, tempTo, tempToOffset, flyingDirection);
	getNewPosition(from, { fromOffset.x - tempY , fromOffset.y + tempX }, &tempFrom, &tempFromOffset);
	getNewPosition(to, { toOffset.x - tempY , toOffset.y + tempX }, &tempTo, &tempToOffset);
	distanceOffset[2] = { (float)-tempY, (float)tempX };
	tempPath[2] = gm->map->getPassPathEx(tempFrom, tempFromOffset, tempTo, tempToOffset, flyingDirection);
	auto maxStep = convert_max(convert_max(tempPath[0].size(), tempPath[1].size()), tempPath[2].size());
	for (size_t i = 0; i < maxStep; i++)
	{
		for (size_t j = 0; j < 3; j++)
		{
			if (tempPath[j].size() > i)
			{
				auto toOffset = getCollideOffset(tempPath[j][i]);
				auto l = Map::getTileDistance(src, srcOffset + distanceOffset[j], tempPath[j][i], toOffset);
				bool found = false;
				for (size_t k = 0; k < result.size(); k++)
				{
					if (result[k] == tempPath[j][i])
					{
						found = true;
						break;
					}
					else
					{
						if (l < resultDistance[k])
						{
							found = true;
							result.insert(result.begin() + k, tempPath[j][i]);
							resultDistance.insert(resultDistance.begin() + k, l);
							break;
						}
					}
				}
				if (!found)
				{
					result.push_back(tempPath[j][i]);
					resultDistance.push_back(l);
				}
			}
		}
	}
	return result;
}

void Effect::changeFollowTarget(std::shared_ptr<GameElement> newTarget)
{
	target = newTarget;
	auto targetNPC = std::dynamic_pointer_cast<NPC>(newTarget);
	updateFollowDirectionToTarget(targetNPC);
}

bool Effect::updateFollowDirectionToTarget(std::shared_ptr<NPC> targetNPC)
{
	if (targetNPC == nullptr)
	{
		return false;
	}

	return updateFollowDirectionToPoint(targetNPC->getPosition(), targetNPC->getOffset(), 0.0f);
}

bool Effect::updateFollowDirectionToPoint(Point targetPosition, PointEx targetOffset, float stopDistance)
{
	PointEx currentWorldPosition = getWorldPosition(position, offset);
	PointEx targetWorldPosition = getWorldPosition(targetPosition, targetOffset);
	float directionX = targetWorldPosition.x - currentWorldPosition.x;
	float directionY = targetWorldPosition.y - currentWorldPosition.y;
	if (stopDistance > 0.0f && isWithinProjectedMovementDistance({ directionX, directionY }, stopDistance))
	{
		src = position;
		srcOffset = offset;
		dest = position;
		destOffset = offset;
		flyingDirection = { 0, 0 };
		return true;
	}

	src = position;
	srcOffset = offset;
	dest = targetPosition;
	destOffset = targetOffset;
	auto targetDirection = Map::getTilePosition(dest, src);
	directionX = (float)targetDirection.x + destOffset.x - srcOffset.x;
	directionY = (float)targetDirection.y + destOffset.y - srcOffset.y;
	if (directionX == 0.0f && directionY == 0.0f)
	{
		return false;
	}
	flyingDirection.x = (int)round(directionX);
	flyingDirection.y = (int)round(directionY * MapXRatio);
	if (flyingDirection.is_zero())
	{
		return false;
	}
	direction = getDirection(flyingDirection);
	return true;
}

bool Effect::updateFollowMouseDirection()
{
	if (magic.followMouse <= 0 || gm == nullptr || gm->map == nullptr || gm->camera == nullptr || engine == nullptr)
	{
		return false;
	}

	int mouseX = -1;
	int mouseY = -1;
	engine->getMousePosition(mouseX, mouseY);
	if (mouseX < 0 || mouseY < 0)
	{
		return false;
	}

	Point mousePoint = gm->getMousePoint(mouseX, mouseY);
	if (!gm->map->isInMap(mousePoint))
	{
		return false;
	}

	return updateFollowDirectionToPoint(mousePoint, { 0.0f, 0.0f }, 25.0f);
}

void Effect::updateMoveImitateUserPosition()
{
	if (magic.moveImitateUser <= 0)
	{
		moveImitateUserPositionInitialized = false;
		return;
	}

	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		return;
	}

	Point userPosition = userPtr->position;
	PointEx userOffset = userPtr->offset;
	if (!moveImitateUserPositionInitialized)
	{
		moveImitateUserLastPosition = userPosition;
		moveImitateUserLastOffset = userOffset;
		moveImitateUserPositionInitialized = true;
		return;
	}

	PointEx delta = Map::getTilePositionEx(userPosition, moveImitateUserLastPosition, { 0, 0 }, { 0, 0 });
	delta = delta + userOffset - moveImitateUserLastOffset;
	if (delta.x != 0.0f || delta.y != 0.0f)
	{
		offset = offset + delta;
		updatePosition();
	}

	moveImitateUserLastPosition = userPosition;
	moveImitateUserLastOffset = userOffset;
}

bool Effect::beginMoveBack()
{
	if (magic.moveBack <= 0)
	{
		moveBackActive = false;
		return false;
	}
	if (user.expired())
	{
		moveBackActive = false;
		return false;
	}

	moveBackActive = true;
	return true;
}

void Effect::updateMoveBackDirection()
{
	if (!moveBackActive)
	{
		return;
	}

	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		moveBackActive = false;
		doing = ekHiding;
		result = erLifeExhaust;
		return;
	}

	PointEx currentPosition = getWorldPosition(position, offset);
	PointEx userPosition = getWorldPosition(userPtr->position, userPtr->offset);
	PointEx delta = { userPosition.x - currentPosition.x, userPosition.y - currentPosition.y };
	if (isWithinProjectedMovementDistance(delta, 20.0f))
	{
		moveBackActive = false;
		doing = ekHiding;
		result = erLifeExhaust;
		return;
	}

	flyingDirection.x = (int)round(delta.x);
	flyingDirection.y = (int)round(delta.y * MapXRatio);
	if (!flyingDirection.is_zero())
	{
		direction = getDirection(flyingDirection);
	}
}

void Effect::updateRandomMoveDirection()
{
	if (magic.randomMoveDegree <= 0)
	{
		return;
	}

	PointEx currentDirection = normalizeDirection(flyingDirection);
	while (currentDirection.x == 0.0f && currentDirection.y == 0.0f)
	{
		int randomX = engine->getRand(199) - 99;
		int randomY = engine->getRand(199) - 99;
		currentDirection = normalizeDirection({ randomX, randomY });
	}

	PointEx perpendicular = engine->getRand(1) == 0
		? PointEx{ currentDirection.y, -currentDirection.x }
		: PointEx{ -currentDirection.y, currentDirection.x };
	int maxRandom = magic.randomMoveDegree - 1;
	float randomAmount = maxRandom > 0 ? (float)engine->getRand(maxRandom) : 0.0f;
	PointEx newDirection =
	{
		currentDirection.x + perpendicular.x * randomAmount,
		currentDirection.y + perpendicular.y * randomAmount
	};
	float length = hypot(newDirection.x, newDirection.y);
	if (length <= 0.001f)
	{
		return;
	}

	flyingDirection.x = (int)round(newDirection.x / length * 1000.0f);
	flyingDirection.y = (int)round(newDirection.y / length * 1000.0f);
	direction = getDirection(flyingDirection);
}

void Effect::beginMeteorMove()
{
	meteorPath.clear();
	meteorMoveActive = false;
	meteorArrivalPending = false;
	if (magic.meteorMove <= 0)
	{
		return;
	}

	int meteorDirection = magic.meteorMoveDir;
	if (meteorDirection > 7)
	{
		meteorDirection = engine->getRand(7);
	}
	meteorDirection = GameElement::normalizeDir(meteorDirection);

	std::deque<MeteorPathNode> path;
	path.push_front({ position, offset });
	Point tile = position;
	for (int i = 0; i <= magic.meteorMove; i++)
	{
		tile = Map::getSubPoint(tile, meteorDirection);
		path.push_front({ tile, { 0.0f, 0.0f } });
	}

	if (path.size() < 2)
	{
		return;
	}

	position = path.front().position;
	offset = path.front().offset;
	path.pop_front();
	meteorPath = path;
	meteorMoveActive = true;
	if (magic.level[level].speed > 0)
	{
		waitTime += (UTime)engine->getRand(999);
	}
}

bool Effect::updateMeteorMove(UTime frameTime)
{
	if (!meteorMoveActive)
	{
		return false;
	}

	passPath.clear();
	if (waitTime > 0)
	{
		if (frameTime >= waitTime)
		{
			waitTime = 0;
		}
		else
		{
			waitTime -= frameTime;
			return true;
		}
	}

	if (meteorPath.empty())
	{
		meteorMoveActive = false;
		meteorArrivalPending = true;
		return true;
	}

	MeteorPathNode next = meteorPath.front();
	PointEx currentWorld = getWorldPosition(position, offset);
	PointEx nextWorld = getWorldPosition(next.position, next.offset);
	PointEx delta = { nextWorld.x - currentWorld.x, nextWorld.y - currentWorld.y };
	float distanceToNext = getProjectedMovementLength(delta);
	if (distanceToNext <= 0.001f)
	{
		position = next.position;
		offset = next.offset;
		meteorPath.pop_front();
		if (meteorPath.empty())
		{
			meteorMoveActive = false;
			meteorArrivalPending = true;
		}
		return true;
	}

	flyingDirection.x = (int)round(delta.x);
	flyingDirection.y = (int)round(delta.y * MapXRatio);
	if (!flyingDirection.is_zero())
	{
		direction = getDirection(flyingDirection);
	}

	float stepDistance = getProjectedMagicFrameDistance((float)speed, (float)frameTime, Config::getGameSpeed());
	if (stepDistance <= 0.0f || stepDistance >= distanceToNext)
	{
		position = next.position;
		offset = next.offset;
		meteorPath.pop_front();
		if (meteorPath.empty())
		{
			meteorMoveActive = false;
			meteorArrivalPending = true;
		}
		return true;
	}

	PointEx nextOffset = advanceProjectedMovement(offset, delta, stepDistance);
	getNewPosition(position, nextOffset, &position, &offset);
	return true;
}

bool Effect::consumeMeteorArrivalPending()
{
	bool pending = meteorArrivalPending;
	meteorArrivalPending = false;
	return pending;
}

void Effect::finishMeteorArrivalWithoutCollision()
{
	if (!consumeMeteorArrivalPending())
	{
		return;
	}

	beginExplode(position);
	result = doing == ekHiding ? erLifeExhaust : erExplode;
}

void Effect::initRoundMove(int index)
{
	if (magic.roundMoveColockwise <= 0 && magic.roundMoveAnticlockwise <= 0)
	{
		roundMoveActive = false;
		return;
	}

	int count = magic.roundMoveCount <= 0 ? 1 : magic.roundMoveCount;
	roundMoveActive = true;
	roundMoveDegree = (magic.roundMoveColockwise > 0 ? -1.0f : 1.0f) * (360.0f * (float)index / (float)count);
	updateRoundMovePosition(0);
}

void Effect::updateRoundMovePosition(UTime frameTime)
{
	if (!roundMoveActive)
	{
		return;
	}
	if (magic.roundRadius <= 0)
	{
		return;
	}

	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		roundMoveActive = false;
		return;
	}

	if (frameTime > 0)
	{
		float directionSign = magic.roundMoveColockwise > 0 ? 1.0f : -1.0f;
		roundMoveDegree += directionSign * (float)magic.roundMoveDegreeSpeed * (float)frameTime / 1000.0f;
	}

	float radians = roundMoveDegree * (float)M_PI / 180.0f;
	PointEx radiusOffset =
	{
		(float)cos(radians) * (float)magic.roundRadius,
		(float)sin(radians) * (float)magic.roundRadius
	};
	PointEx newOffset = userPtr->offset + radiusOffset;
	getNewPosition(userPtr->position, newOffset, &position, &offset);

	float length = hypot(radiusOffset.x, radiusOffset.y);
	if (length <= 0.001f)
	{
		return;
	}
	PointEx tangent = magic.roundMoveColockwise > 0
		? PointEx{ -radiusOffset.y / length, radiusOffset.x / length }
		: PointEx{ radiusOffset.y / length, -radiusOffset.x / length };
	flyingDirection.x = (int)round(tangent.x * 1000.0f);
	flyingDirection.y = (int)round(tangent.y * 1000.0f * MapXRatio);
	direction = getDirection(flyingDirection);
}

void Effect::updateSummonEffect()
{
	if (magic.level[level].moveKind != mmkSummon || vanishing || doing == ekHiding)
	{
		return;
	}

	auto npc = summonedNPC.lock();
	if (npc == nullptr || npc->isDying() || npc->isHiding())
	{
		summonedNPC.reset();
		vanishing = true;
		beginExplode(position);
		return;
	}

	position = npc->getPosition();
	offset = npc->getOffset();
	if (getTime() - beginTime >= lifeTime)
	{
		vanishing = true;
		beginExplode(position);
	}
}

Point Effect::getCircleMoveDirection(Point baseDirection)
{
	if (magic.circleMoveColockwise <= 0 && magic.circleMoveAnticlockwise <= 0)
	{
		circleMoveBaseDirectionInitialized = false;
		return baseDirection;
	}

	auto userPtr = user.lock();
	if (userPtr == nullptr)
	{
		return baseDirection;
	}

	PointEx relative = Map::getTilePositionEx(position, userPtr->position, { 0, 0 }, { 0, 0 });
	relative = relative + offset - userPtr->offset;
	relative.y *= MapXRatio;
	float relativeLength = hypot(relative.x, relative.y);
	if (relativeLength == 0.0f)
	{
		return baseDirection;
	}

	PointEx realDirection = { 0.0f, 0.0f };
	float baseLength = hypot((float)baseDirection.x, (float)baseDirection.y);
	if (baseLength > 0.0f)
	{
		realDirection.x += (float)baseDirection.x / baseLength;
		realDirection.y += (float)baseDirection.y / baseLength;
	}

	PointEx tangent = magic.circleMoveColockwise > 0
		? PointEx{ -relative.y / relativeLength, relative.x / relativeLength }
		: PointEx{ relative.y / relativeLength, -relative.x / relativeLength };
	realDirection = realDirection + tangent;
	if (realDirection.x == 0.0f && realDirection.y == 0.0f)
	{
		return baseDirection;
	}
	return { (int)round(realDirection.x * 1000.0f), (int)round(realDirection.y * 1000.0f) };
}

unsigned char Effect::getLum()
{
	if (noLum)
	{
		return 0;
	}
	if (doing == ekFlying || doing == ekThrowing)
	{
		return magic.level[level].flyingLum;
	}
	else if (doing == ekExploding)
	{
		if (vanishing)
		{
			return magic.level[level].vanishLum;
		}
		else if (isSelfAnchoredMoveKind(magic.level[level].moveKind))
		{
			return magic.level[level].flyingLum;
		}
		else
		{
			return magic.level[level].vanishLum;
		}
	}
	return 0;
}

int Effect::getMoveKind() const
{
	return magic.level[level].moveKind;
}

bool Effect::isTimeStopper() const
{
	return magic.level[level].moveKind == mmkTimeStop;
}

void Effect::initFromMagic(
	std::shared_ptr<Magic> m,
	std::shared_ptr<MagicDispatchContext> dispatchContext)
{
	if (m == nullptr)
	{
		magic.reset();
		additionalEffect = maeNone;
		magicDispatchContext = nullptr;
	}
	else
	{
		magic.copy(*m.get());
		additionalEffect = magic.additionalEffect;
		magicDispatchContext = dispatchContext != nullptr
			? dispatchContext
			: Magic::createRootDispatchContext(m);
	}
	initParam();
	calTime();
}

void Effect::initFromIni(INIReader * ini, const std::string & section)
{
	initFromIni(ini, section, nullptr);
}

void Effect::initFromIni(
	INIReader* ini,
	const std::string& section,
	const EffectReferenceLoadContext* referenceContext)
{
	freeResource();
	if (ini == nullptr)
	{
		return;
	}
	getFrameTime();
	user.reset();
	target.reset();
	auto userReference = readEffectElementReference(*ini, section, "User");
	auto targetReference = readEffectElementReference(*ini, section, "Target");
	user = resolveEffectElementReference(
		userReference,
		EffectElementReferenceRole::Caster,
		referenceContext);
	target = resolveEffectElementReference(targetReference);
	fileName = ini->Get(section, "FileName", "");
	doing = ini->GetInteger(section, "Doing", ekExploding);
	position.x = ini->GetInteger(section, "MapX", 0);
	position.y = ini->GetInteger(section, "MapY", 0);
	flyingDirection.x = ini->GetInteger(section, "DirectionX", 0);
	flyingDirection.y = ini->GetInteger(section, "DirectionY", 0);
	offset.x = ini->GetReal(section, "OffsetX", 0.0);
	offset.y = ini->GetReal(section, "OffsetY", 0.0);
	src.x = ini->GetInteger(section, "SrcX", 0);
	src.y = ini->GetInteger(section, "SrcY", 0);
	srcOffset.x = ini->GetReal(section, "SrcOffsetX", 0);
	srcOffset.y = ini->GetReal(section, "SrcOffsetY", 0);
	dest.x = ini->GetInteger(section, "DestX", 0);
	dest.y = ini->GetInteger(section, "DestY", 0);
	destOffset.x = ini->GetReal(section, "DestOffsetX", 0);
	destOffset.y = ini->GetReal(section, "DestOffsetY", 0);
	beginTime = ini->GetInteger(section, "BeginTime", 0);
	beginTime = getTime() - beginTime;
	lifeTime = ini->GetInteger(section, "LifeTime", 0);
	waitTime = ini->GetInteger(section, "WaitTime", 0);
	damage = ini->GetInteger(section, "Damage", 0);
	std::string savedDamage2Text = ini->Get(section, "Damage2", "");
	std::string savedDamage3Text = ini->Get(section, "Damage3", "");
	std::string savedDamageManaText = ini->Get(section, "DamageMana", "");
	int savedDamage2 = ini->GetInteger(section, "Damage2", 0);
	int savedDamage3 = ini->GetInteger(section, "Damage3", 0);
	int savedDamageMana = ini->GetInteger(section, "DamageMana", 0);
	std::string savedExperienceOwnerMagicFile = ini->Get(section, "ExperienceOwnerMagicFile", "");
	std::string savedEffectSpeedText = ini->Get(section, "EffectSpeed", "");
	std::string savedMagicLevelSpeedText = ini->Get(section, "MagicLevelSpeed", "");
	std::string savedAdditionalEffectText = ini->Get(section, "AdditionalEffect", "");
	std::string savedRangeElapsedText = ini->Get(section, "RangeElapsedMilliseconds", "");
	std::string savedFlyMagicElapsedText = ini->Get(section, "FlyMagicElapsedMilliseconds", "");
	std::string savedLeapTimesRemainingText = ini->Get(section, "LeapTimesRemaining", "");
	std::string savedLeapFlyingText = ini->Get(section, "LeapFlying", "");
	std::string savedMagicWhenNewPositionInitializedText = ini->Get(section, "MagicWhenNewPositionInitialized", "");
	std::string savedExplodeMagicTriggeredText = ini->Get(section, "ExplodeMagicTriggered", "");
	std::string savedMoveBackActiveText = ini->Get(section, "MoveBackActive", "");
	std::string savedMeteorMoveActiveText = ini->Get(section, "MeteorMoveActive", "");
	std::string savedMeteorArrivalPendingText = ini->Get(section, "MeteorArrivalPending", "");
	std::string savedCircleMoveBaseDirectionInitializedText = ini->Get(section, "CircleMoveBaseDirectionInitialized", "");
	std::string savedRoundMoveActiveText = ini->Get(section, "RoundMoveActive", "");
	std::string savedVibrationTriggeredText = ini->Get(section, "VibrationTriggered", "");
	std::string savedVanishingText = ini->Get(section, "Vanishing", "");
	auto savedLeapHitTargets = readEffectNPCReferenceList(*ini, section, "LeapHitTarget");
	auto savedPassThroughHitTargets = readEffectNPCReferenceList(*ini, section, "PassThroughHitTarget");
	auto savedParasiticTarget = std::dynamic_pointer_cast<NPC>(resolveEffectElementReference(
		readEffectElementReference(*ini, section, "ParasiticTarget")));
	auto savedCarriedUser = std::dynamic_pointer_cast<NPC>(resolveEffectElementReference(
		readEffectElementReference(*ini, section, "CarriedUser")));
	auto savedStickyTarget = std::dynamic_pointer_cast<NPC>(resolveEffectElementReference(
		readEffectElementReference(*ini, section, "StickyTarget")));
	std::deque<MeteorPathNode> savedMeteorPath;
	int savedMeteorPathCount = std::clamp(
		static_cast<int>(ini->GetInteger(section, "MeteorPathCount", 0)),
		0,
		MaximumPersistedEffectCollectionCount);
	for (int i = 0; i < savedMeteorPathCount; i++)
	{
		std::string prefix = "MeteorPath" + std::to_string(i + 1);
		MeteorPathNode node;
		node.position.x = static_cast<int>(ini->GetInteger(section, prefix + "MapX", 0));
		node.position.y = static_cast<int>(ini->GetInteger(section, prefix + "MapY", 0));
		node.offset.x = ini->GetReal(section, prefix + "OffsetX", 0.0f);
		node.offset.y = ini->GetReal(section, prefix + "OffsetY", 0.0f);
		savedMeteorPath.push_back(node);
	}
	evade = ini->GetInteger(section, "Evade", 0);
	launcherKind = ini->GetInteger(section, "Launcher", lkSelf);
	direction = ini->GetInteger(section, "Direction", 0);
	level = ini->GetInteger(section, "Level", 0);

	magic.initFromIni(fileName);
	magicDispatchContext = magic.loadSucceeded
		? Magic::createRootDispatchContext(std::make_shared<Magic>(magic))
		: nullptr;
	if (!savedExperienceOwnerMagicFile.empty())
	{
		magic.experienceOwnerMagicFile = savedExperienceOwnerMagicFile;
	}
	initParam();
	if (!savedEffectSpeedText.empty())
	{
		speed = static_cast<int>(ini->GetInteger(section, "EffectSpeed", speed));
	}
	if (!savedMagicLevelSpeedText.empty())
	{
		magic.level[clampMagicLevel(level)].speed = static_cast<int>(ini->GetInteger(
			section,
			"MagicLevelSpeed",
			magic.level[clampMagicLevel(level)].speed));
	}
	additionalEffect = savedAdditionalEffectText.empty()
		? magic.additionalEffect
		: static_cast<int>(ini->GetInteger(section, "AdditionalEffect", magic.additionalEffect));
	if (!savedRangeElapsedText.empty())
	{
		rangeElapsedMilliseconds = ini->GetTime(section, "RangeElapsedMilliseconds", rangeElapsedMilliseconds);
	}
	if (!savedFlyMagicElapsedText.empty())
	{
		flyMagicElapsedMilliseconds = ini->GetTime(section, "FlyMagicElapsedMilliseconds", flyMagicElapsedMilliseconds);
	}
	if (!savedLeapTimesRemainingText.empty())
	{
		leapTimesRemaining = std::clamp(
			static_cast<int>(ini->GetInteger(section, "LeapTimesRemaining", leapTimesRemaining)),
			0,
			std::max(0, magic.level[clampMagicLevel(level)].leapTimes));
	}
	if (!savedLeapFlyingText.empty())
	{
		leapFlying = ini->GetBoolean(section, "LeapFlying", leapFlying);
	}
	leapHitTargets = std::move(savedLeapHitTargets);
	passThroughHitTargets = std::move(savedPassThroughHitTargets);
	if (!savedMagicWhenNewPositionInitializedText.empty())
	{
		magicWhenNewPositionInitialized = ini->GetBoolean(
			section,
			"MagicWhenNewPositionInitialized",
			magicWhenNewPositionInitialized);
		magicWhenNewPositionLastTile.x = static_cast<int>(ini->GetInteger(
			section,
			"MagicWhenNewPositionLastTileX",
			magicWhenNewPositionLastTile.x));
		magicWhenNewPositionLastTile.y = static_cast<int>(ini->GetInteger(
			section,
			"MagicWhenNewPositionLastTileY",
			magicWhenNewPositionLastTile.y));
	}
	if (!savedExplodeMagicTriggeredText.empty())
	{
		explodeMagicTriggered = ini->GetBoolean(section, "ExplodeMagicTriggered", explodeMagicTriggered);
	}
	if (!savedMoveBackActiveText.empty())
	{
		moveBackActive = ini->GetBoolean(section, "MoveBackActive", moveBackActive);
	}
	meteorMoveActive = !savedMeteorMoveActiveText.empty()
		&& ini->GetBoolean(section, "MeteorMoveActive", false);
	meteorArrivalPending = !savedMeteorArrivalPendingText.empty()
		&& ini->GetBoolean(section, "MeteorArrivalPending", false);
	meteorPath = std::move(savedMeteorPath);
	if (meteorMoveActive && meteorPath.empty())
	{
		meteorMoveActive = false;
		meteorArrivalPending = true;
	}
	if (!savedCircleMoveBaseDirectionInitializedText.empty())
	{
		circleMoveBaseDirectionInitialized = ini->GetBoolean(
			section,
			"CircleMoveBaseDirectionInitialized",
			circleMoveBaseDirectionInitialized);
		circleMoveBaseDirection.x = static_cast<int>(ini->GetInteger(
			section,
			"CircleMoveBaseDirectionX",
			circleMoveBaseDirection.x));
		circleMoveBaseDirection.y = static_cast<int>(ini->GetInteger(
			section,
			"CircleMoveBaseDirectionY",
			circleMoveBaseDirection.y));
	}
	if (!savedRoundMoveActiveText.empty())
	{
		roundMoveActive = ini->GetBoolean(section, "RoundMoveActive", roundMoveActive);
		roundMoveDegree = ini->GetReal(section, "RoundMoveDegree", roundMoveDegree);
	}
	if (!savedVibrationTriggeredText.empty())
	{
		vibrationTriggered = ini->GetBoolean(section, "VibrationTriggered", vibrationTriggered);
	}
	if (savedParasiticTarget != nullptr
		&& magic.getLinkedLevel(level).parasitic > 0
		&& doing == ekExploding
		&& !savedVanishingText.empty()
		&& ini->GetBoolean(section, "Vanishing", false))
	{
		parasiticTarget = savedParasiticTarget;
		parasiticElapsedMilliseconds = ini->GetTime(section, "ParasiticElapsedMilliseconds", 0);
		parasiticTotalEffect = std::max(
			0,
			static_cast<int>(ini->GetInteger(section, "ParasiticTotalEffect", 0)));
	}
	vanishing = savedVanishingText.empty()
		? false
		: ini->GetBoolean(section, "Vanishing", false);
	carryUserActive = ini->GetBoolean(section, "CarryUserActive", false)
		&& magic.carryUser > 0
		&& savedCarriedUser != nullptr;
	carriedUser = carryUserActive ? savedCarriedUser : nullptr;
	if (magic.sticky > 0)
	{
		stickyTarget = savedStickyTarget;
	}
	if (magic.carryUser == 4 || magic.sticky > 0)
	{
		int attachedCount = std::clamp(
			static_cast<int>(ini->GetInteger(section, "AttachedNPCCount", 0)),
			0,
			MaximumPersistedEffectCollectionCount);
		for (int i = 0; i < attachedCount; i++)
		{
			std::string prefix = "AttachedNPC" + std::to_string(i + 1);
			auto npc = std::dynamic_pointer_cast<NPC>(resolveEffectElementReference(
				readEffectElementReference(*ini, section, prefix)));
			if (npc == nullptr)
			{
				continue;
			}
			Point npcPosition = npc->getPosition();
			PointEx npcOffset = npc->getOffset();

			AttachedNPC item;
			item.npc = npc;
			item.tileOffset.x = static_cast<int>(ini->GetInteger(section, prefix + "TileOffsetX", 0));
			item.tileOffset.y = static_cast<int>(ini->GetInteger(section, prefix + "TileOffsetY", 0));
			item.offsetDelta.x = ini->GetReal(section, prefix + "OffsetDeltaX", 0.0f);
			item.offsetDelta.y = ini->GetReal(section, prefix + "OffsetDeltaY", 0.0f);
			item.initialPosition.x = static_cast<int>(ini->GetInteger(section, prefix + "InitialMapX", npcPosition.x));
			item.initialPosition.y = static_cast<int>(ini->GetInteger(section, prefix + "InitialMapY", npcPosition.y));
			item.initialOffset.x = ini->GetReal(section, prefix + "InitialOffsetX", npcOffset.x);
			item.initialOffset.y = ini->GetReal(section, prefix + "InitialOffsetY", npcOffset.y);
			item.hasMoved = ini->GetBoolean(section, prefix + "HasMoved", false);
			item.preserveOffset = ini->GetBoolean(section, prefix + "PreserveOffset", false);
			item.destroyOnObstacle = ini->GetBoolean(section, prefix + "DestroyOnObstacle", false);
			attachedNPCs.push_back(item);
		}
	}
	selfShieldBoundAfterLoad = ini->GetBoolean(section, "SelfShieldBound", false);
	selfShieldLifeAfterLoad = std::max(
		0,
		static_cast<int>(ini->GetInteger(section, "SelfShieldLife", 0)));
	selfShieldRemainingMillisecondsAfterLoad = ini->GetTime(
		section,
		"SelfShieldRemainingMilliseconds",
		0);
	if (!savedDamage2Text.empty())
	{
		damage2 = savedDamage2;
	}
	if (!savedDamage3Text.empty())
	{
		damage3 = savedDamage3;
	}
	if (!savedDamageManaText.empty())
	{
		damageMana = savedDamageMana;
	}
}

void Effect::saveToIni(INIReader * ini, const std::string & section)
{
	saveToIni(ini, section, nullptr);
}

void Effect::saveToIni(
	INIReader* ini,
	const std::string& section,
	EffectReferenceSaveContext* referenceContext)
{
	if (ini == nullptr)
	{
		return;
	}
	ini->Set(section, "FileName", fileName);
	writeEffectElementReference(
		*ini,
		section,
		"User",
		user.lock(),
		EffectElementReferenceRole::Caster,
		referenceContext);
	writeEffectElementReference(*ini, section, "Target", target.lock());
	ini->SetInteger(section, "Doing", doing);
	ini->SetInteger(section, "MapX", position.x);
	ini->SetInteger(section, "MapY", position.y);
	ini->SetInteger(section, "DirectionX", flyingDirection.x);
	ini->SetInteger(section, "DirectionY", flyingDirection.y);
	ini->SetInteger(section, "SrcX", src.x);
	ini->SetInteger(section, "SrcY", src.y);
	ini->SetReal(section, "SrcOffsetX", srcOffset.x);
	ini->SetReal(section, "SrcOffsetY", srcOffset.y);
	ini->SetInteger(section, "DestX", dest.x);
	ini->SetInteger(section, "DestY", dest.y);
	ini->SetReal(section, "DestOffsetX", destOffset.x);
	ini->SetReal(section, "DestOffsetY", destOffset.y);
	ini->SetReal(section, "OffsetX", offset.x);
	ini->SetReal(section, "OffsetY", offset.y);
	
	auto tempBeginTime = getTime() - beginTime;
	ini->SetTime(section, "BeginTime", tempBeginTime);
	ini->SetTime(section, "LifeTime", lifeTime);
	ini->SetTime(section, "WaitTime", waitTime);
	ini->SetInteger(section, "Damage", damage);
	ini->SetInteger(section, "Damage2", damage2);
	ini->SetInteger(section, "Damage3", damage3);
	ini->SetInteger(section, "DamageMana", damageMana);
	ini->Set(section, "ExperienceOwnerMagicFile", magic.experienceOwnerMagicFile);
	ini->SetInteger(section, "EffectSpeed", speed);
	ini->SetInteger(section, "MagicLevelSpeed", magic.level[clampMagicLevel(level)].speed);
	ini->SetInteger(section, "AdditionalEffect", additionalEffect);
	ini->SetTime(section, "RangeElapsedMilliseconds", rangeElapsedMilliseconds);
	ini->SetTime(section, "FlyMagicElapsedMilliseconds", flyMagicElapsedMilliseconds);
	ini->SetInteger(section, "LeapTimesRemaining", leapTimesRemaining);
	ini->SetBoolean(section, "LeapFlying", leapFlying);
	writeEffectNPCReferenceList(*ini, section, "LeapHitTarget", leapHitTargets);
	writeEffectNPCReferenceList(*ini, section, "PassThroughHitTarget", passThroughHitTargets);
	ini->SetBoolean(section, "MagicWhenNewPositionInitialized", magicWhenNewPositionInitialized);
	ini->SetInteger(section, "MagicWhenNewPositionLastTileX", magicWhenNewPositionLastTile.x);
	ini->SetInteger(section, "MagicWhenNewPositionLastTileY", magicWhenNewPositionLastTile.y);
	ini->SetBoolean(section, "ExplodeMagicTriggered", explodeMagicTriggered);
	ini->SetBoolean(section, "MoveBackActive", moveBackActive);
	ini->SetBoolean(section, "MeteorMoveActive", meteorMoveActive);
	ini->SetBoolean(section, "MeteorArrivalPending", meteorArrivalPending);
	ini->SetInteger(section, "MeteorPathCount", static_cast<long>(meteorPath.size()));
	for (size_t i = 0; i < meteorPath.size(); i++)
	{
		std::string prefix = "MeteorPath" + std::to_string(i + 1);
		ini->SetInteger(section, prefix + "MapX", meteorPath[i].position.x);
		ini->SetInteger(section, prefix + "MapY", meteorPath[i].position.y);
		ini->SetReal(section, prefix + "OffsetX", meteorPath[i].offset.x);
		ini->SetReal(section, prefix + "OffsetY", meteorPath[i].offset.y);
	}
	ini->SetBoolean(section, "CircleMoveBaseDirectionInitialized", circleMoveBaseDirectionInitialized);
	ini->SetInteger(section, "CircleMoveBaseDirectionX", circleMoveBaseDirection.x);
	ini->SetInteger(section, "CircleMoveBaseDirectionY", circleMoveBaseDirection.y);
	ini->SetBoolean(section, "RoundMoveActive", roundMoveActive);
	ini->SetReal(section, "RoundMoveDegree", roundMoveDegree);
	ini->SetBoolean(section, "VibrationTriggered", vibrationTriggered);
	writeEffectElementReference(*ini, section, "ParasiticTarget", parasiticTarget.lock());
	ini->SetTime(section, "ParasiticElapsedMilliseconds", parasiticElapsedMilliseconds);
	ini->SetInteger(section, "ParasiticTotalEffect", parasiticTotalEffect);
	ini->SetBoolean(section, "Vanishing", vanishing);
	ini->SetBoolean(section, "CarryUserActive", carryUserActive);
	writeEffectElementReference(*ini, section, "CarriedUser", carriedUser.lock());
	writeEffectElementReference(*ini, section, "StickyTarget", stickyTarget.lock());
	int attachedIndex = 0;
	for (const auto& item : attachedNPCs)
	{
		auto npc = item.npc.lock();
		if (npc == nullptr || makeEffectElementReference(npc).kind == EffectElementReferenceKind::None)
		{
			continue;
		}
		std::string prefix = "AttachedNPC" + std::to_string(++attachedIndex);
		writeEffectElementReference(*ini, section, prefix, npc);
		ini->SetInteger(section, prefix + "TileOffsetX", item.tileOffset.x);
		ini->SetInteger(section, prefix + "TileOffsetY", item.tileOffset.y);
		ini->SetReal(section, prefix + "OffsetDeltaX", item.offsetDelta.x);
		ini->SetReal(section, prefix + "OffsetDeltaY", item.offsetDelta.y);
		ini->SetInteger(section, prefix + "InitialMapX", item.initialPosition.x);
		ini->SetInteger(section, prefix + "InitialMapY", item.initialPosition.y);
		ini->SetReal(section, prefix + "InitialOffsetX", item.initialOffset.x);
		ini->SetReal(section, prefix + "InitialOffsetY", item.initialOffset.y);
		ini->SetBoolean(section, prefix + "HasMoved", item.hasMoved);
		ini->SetBoolean(section, prefix + "PreserveOffset", item.preserveOffset);
		ini->SetBoolean(section, prefix + "DestroyOnObstacle", item.destroyOnObstacle);
	}
	ini->SetInteger(section, "AttachedNPCCount", attachedIndex);
	bool selfShieldBound = false;
	int selfShieldLife = 0;
	UTime selfShieldRemainingMilliseconds = 0;
	auto shieldOwner = std::dynamic_pointer_cast<NPC>(user.lock());
	int effectLevel = clampMagicLevel(level);
	if (shieldOwner != nullptr
		&& magic.level[effectLevel].moveKind == mmkSelf
		&& magic.level[effectLevel].specialKind == mskAddShield)
	{
		auto shield = shieldOwner->shieldEffect.lock();
		if (shield != nullptr && shield.get() == this && shieldOwner->shieldLife > 0)
		{
			selfShieldBound = true;
			selfShieldLife = shieldOwner->shieldLife;
			selfShieldRemainingMilliseconds = shieldOwner->shieldLastTime;
		}
	}
	ini->SetBoolean(section, "SelfShieldBound", selfShieldBound);
	ini->SetInteger(section, "SelfShieldLife", selfShieldLife);
	ini->SetTime(section, "SelfShieldRemainingMilliseconds", selfShieldRemainingMilliseconds);
	ini->SetInteger(section, "Evade", evade);
	ini->SetInteger(section, "Launcher", launcherKind);
	ini->SetInteger(section, "Direction", direction);
	ini->SetInteger(section, "Level", level);
}

void Effect::restoreRuntimeBindingsAfterLoad()
{
	if (carryUserActive && magic.carryUser > 0)
	{
		auto npc = carriedUser.lock();
		if (npc == nullptr || npc->isDying() || npc->isHiding())
		{
			carriedUser.reset();
			carryUserActive = false;
		}
		else
		{
			npc->stopMovement();
			npc->setPosition(position, false);
			npc->setOffset(offset);
			npc->direction = direction;
			if (magic.hideUserWhenCarry > 0)
			{
				auto self = std::dynamic_pointer_cast<Effect>(getMySharedPtr());
				if (self != nullptr)
				{
					npc->setHiddenByCarryMagic(self);
				}
			}
		}
	}

	for (auto& item : attachedNPCs)
	{
		if (auto npc = item.npc.lock())
		{
			npc->stopMovement();
		}
	}

	auto shieldOwner = std::dynamic_pointer_cast<NPC>(user.lock());
	auto self = std::dynamic_pointer_cast<Effect>(getMySharedPtr());
	int effectLevel = clampMagicLevel(level);
	bool activeSelfEffect = shieldOwner != nullptr
		&& self != nullptr
		&& NPCManager::isManagedEffectCaster(shieldOwner)
		&& magic.level[effectLevel].moveKind == mmkSelf
		&& !vanishing
		&& (doing == ekFlying
			|| doing == ekExploding
			|| (doing == ekHiding && waitTime > 0));
	if (activeSelfEffect)
	{
		int specialKind = magic.level[effectLevel].specialKind;
		if (specialKind == mskAddDamageReduceShield || specialKind == mskBlockDamage)
		{
			shieldOwner->shieldEffects.erase(
				std::remove_if(
					shieldOwner->shieldEffects.begin(),
					shieldOwner->shieldEffects.end(),
					[&](const std::weak_ptr<Effect>& weakEffect)
					{
						auto effect = weakEffect.lock();
						return effect == nullptr || effect == self;
					}),
				shieldOwner->shieldEffects.end());
			shieldOwner->shieldEffects.push_back(self);
		}
		else if (specialKind == mskAddShield
			&& selfShieldBoundAfterLoad
			&& selfShieldLifeAfterLoad > 0
			&& selfShieldRemainingMillisecondsAfterLoad > 0)
		{
			shieldOwner->shieldEffect = self;
			shieldOwner->shieldLife = selfShieldLifeAfterLoad;
			shieldOwner->shieldBeginTime = shieldOwner->getTime();
			shieldOwner->shieldLastTime = selfShieldRemainingMillisecondsAfterLoad;
		}
		else if (specialKind == mskChangeAttributes
			&& gm != nullptr
			&& gm->global.feature.rageSystem)
		{
			auto player = std::dynamic_pointer_cast<Player>(shieldOwner);
			if (player != nullptr)
			{
				player->attributeChangeEffect = self;
			}
		}
	}
}

void Effect::releaseRuntimeBindings()
{
	auto shieldOwner = std::dynamic_pointer_cast<NPC>(user.lock());
	if (shieldOwner != nullptr)
	{
		if (auto player = std::dynamic_pointer_cast<Player>(shieldOwner))
		{
			auto attributeEffect = player->attributeChangeEffect.lock();
			if (attributeEffect != nullptr && attributeEffect.get() == this)
			{
				player->attributeChangeEffect.reset();
			}
		}
		auto shield = shieldOwner->shieldEffect.lock();
		if (shield != nullptr && shield.get() == this)
		{
			shieldOwner->shieldEffect.reset();
			shieldOwner->shieldLife = 0;
			shieldOwner->shieldBeginTime = 0;
			shieldOwner->shieldLastTime = 0;
		}
		shieldOwner->shieldEffects.erase(
			std::remove_if(
				shieldOwner->shieldEffects.begin(),
				shieldOwner->shieldEffects.end(),
				[&](const std::weak_ptr<Effect>& weakEffect)
				{
					auto effect = weakEffect.lock();
					return effect == nullptr || effect.get() == this;
				}),
			shieldOwner->shieldEffects.end());
	}
	clearCarryUser();
	clearSummonedNPC(true);
	clearAttachedNPCs();
	finishParasiticEffect();
	clearTransportEffectState();
	clearControlEffectState();
	user.reset();
}

std::shared_ptr<GameElement> Effect::loadElementReference(
	const INIReader& ini,
	const std::string& section,
	const std::string& prefix,
	EffectElementReferenceRole role,
	const EffectReferenceLoadContext* referenceContext)
{
	return resolveEffectElementReference(
		readEffectElementReference(ini, section, prefix),
		role,
		referenceContext);
}

void Effect::saveElementReference(
	INIReader& ini,
	const std::string& section,
	const std::string& prefix,
	const std::shared_ptr<GameElement>& element,
	EffectElementReferenceRole role,
	EffectReferenceSaveContext* referenceContext)
{
	writeEffectElementReference(ini, section, prefix, element, role, referenceContext);
}

void Effect::playSound(int act)
{
	PointEx soundOffset = gm->camera->offset - offset;
	Point pos = Map::getTilePosition(position, gm->camera->position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	float soundFactor = 1.0f;
	switch (act)
	{
	case ekFlying:
		// 多重飞行技能释放时，声音叠加过大，此处降低音量处理
		if (magic.level[level].moveKind == mmkHeartCircle || magic.level[level].moveKind == mmkCircle 
			|| magic.level[level].moveKind == mmkHelixCircle)
		{
			soundFactor = 0.05f;
		}
		else if (magic.level[level].moveKind == mmkSector || magic.level[level].moveKind == mmkRandSector ||
			(magic.level[level].moveKind == mmkRegion && magic.level[level].region == mrWave))
		{
			if (level < 4)
			{
				soundFactor = 0.35f;
			}
			else if (level < 7)
			{
				soundFactor = 0.2f;
			}
			else
			{
				soundFactor = 0.1f;
			}
		}
		else if (magic.level[level].moveKind == mmkRegion)
		{
			if (level < 4)
			{
				soundFactor = 0.15f;
			}
			else if (level < 7)
			{
				soundFactor = 0.1f;
			}
			else
			{
				soundFactor = 0.05f;
			}
		}
		channel = playSoundFile(magic.flyingSound, x, y, engine->getSoundVolume() * soundFactor);
		break;
	case ekExploding:
		if (magic.level[level].moveKind == mmkFullScreen)
		{
			soundFactor = 0.2f;
		}
		channel = playSoundFile(magic.vanishSound, x, y, engine->getSoundVolume() * soundFactor);
		break;
	case ekSuperMode:
		channel = playSoundFile(magic.superModeSound, x, y, engine->getSoundVolume());
		break;
	case ekHiding:
		break;
	default:
		break;
	}
}

int Effect::getDirection(Point fDir)
{
	fDir.x = - fDir.x;
	float angle = atan2((float)fDir.x, (float)fDir.y);

	if (angle < 0)
	{
		angle += 2 * M_PI;
	}

	if (angle > 2 * M_PI)
	{
		angle -= 2 * M_PI;
	}
	return (int)(angle / (M_PI / 8));	
}

int Effect::getDirection()
{
	return direction = getDirection(flyingDirection);
}

int Effect::calculateThrowHeightOffset(
	double traveledDistance,
	double totalDistance,
	double moveSpeed)
{
	if (!std::isfinite(traveledDistance)
		|| !std::isfinite(totalDistance)
		|| !std::isfinite(moveSpeed)
		|| traveledDistance <= 0.0
		|| totalDistance <= std::numeric_limits<double>::epsilon()
		|| moveSpeed <= std::numeric_limits<double>::epsilon()
		|| traveledDistance >= totalDistance)
	{
		return 0;
	}

	const double progress = traveledDistance / totalDistance;
	const double arcFactor = 4.0 * progress * (1.0 - progress);
	const double height = MAGIC_THROW_HEIGHT
		* static_cast<double>(TILE_HEIGHT)
		* totalDistance
		* arcFactor
		/ moveSpeed;
	if (!std::isfinite(height)
		|| height >= static_cast<double>(std::numeric_limits<int>::max()))
	{
		return std::numeric_limits<int>::max();
	}
	if (height <= 0.0)
	{
		return 0;
	}
	return static_cast<int>(height);
}

void Effect::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = loadImage(&offsetX, &offsetY);
	int offsetH = 0;
	if (getMoveKind() == mmkThrow && doing == ekThrowing)
	{
		auto l1 = Map::getTileDistance(src, srcOffset, position, offset);
		auto l2 = Map::getTileDistance(src, srcOffset, dest, destOffset);
		//抛物线Y坐标偏移
		offsetH = calculateThrowHeightOffset(l1, l2, speed);
	}
	const int drawX = clampDrawCoordinate(pos.x, offset.x, offsetX, 0);
	const int drawY = clampDrawCoordinate(pos.y, offset.y, offsetY, offsetH);
	if (ColorStyle::isNormal(colorStyle))
	{
		if (magic.level[level].alphaBlend > 0)
		{
			engine->drawImageWithBlendAlpha(image, drawX,
				drawY, 255, SDL_BLENDMODE_BLEND);
		}
		else
		{
			engine->drawImage(image, drawX, drawY);
		}
	}
	else
	{
		if (magic.level[level].alphaBlend > 0)
		{
			engine->setImageAlpha(image, 255);
		}
		ColorStyle::drawImage(engine, image, drawX, drawY, colorStyle);
	}
}

_shared_image Effect::loadImage(int * x, int * y)
{
	if (magic.level[level].moveKind == mmkSummon && !vanishing)
	{
		if (getTime() - beginTime < getFlyingImageTime())
		{
			return IMP::loadImageForDirection(magic.flyImage, direction, getTime() - beginTime, x, y);
		}
		return nullptr;
	}

	if (doing == ekExploding)
	{
		if (vanishing)
		{
			return IMP::loadImageForDirection(magic.explodeImage, direction, getTime() - beginTime, x, y);
		}
		else if (!isSelfAnchoredMoveKind(magic.level[level].moveKind) && magic.level[level].moveKind != mmkPoint)
		{
			return IMP::loadImageForDirection(magic.explodeImage, direction, getTime() - beginTime, x, y);
		}
		else
		{
			return IMP::loadImageForDirection(magic.flyImage, direction, getTime() - beginTime, x, y);
		}
	}
	else if (doing == ekFlying)
	{
		if (leapFlying && magic.leapImage != nullptr)
		{
			return IMP::loadImageForDirection(magic.leapImage, direction, getTime() - beginTime, x, y);
		}
		if (getMoveKind() == mmkThrow)
		{
			return IMP::loadImageForDirection(magic.explodeImage, direction, getTime() - beginTime, x, y);
		}
		else
		{
			return IMP::loadImageForDirection(magic.flyImage, direction, getTime() - beginTime, x, y);
		}		
	}
	else if (doing == ekSuperMode)
	{
		return IMP::loadImageForDirection(magic.superImage, direction, getTime() - beginTime, x, y);
	}
	else if (doing == ekThrowing)
	{
		return IMP::loadImageForDirection(magic.flyImage, direction, getTime() - beginTime, x, y);
	}
	else
	{
		return nullptr;
	}
}

void Effect::freeResource()
{
	releaseRuntimeBindings();
	magic.freeResource();
}

void Effect::onUpdate()
{
	auto ft = getFrameTime();
	if (magic.level[level].moveKind == mmkSummon && !vanishing && doing != ekHiding)
	{
		updateSummonEffect();
		updateSound();
		return;
	}
	if (parasiticTarget.lock() != nullptr)
	{
		updateParasiticEffect(ft);
		updateSound();
		return;
	}
	if (doing == ekSuperMode)
	{
		if (getTime() - beginTime >= lifeTime)
		{
			if (logicRunning)
			{
				logicRunning = false;
			}
		}
	}
	else if (doing == ekThrowing)
	{
		Point from = position;
		PointEx fromOffset = offset;
		updateEffectPosition(ft, (float)speed);
		Point to = position;
		PointEx toOffset = offset;
		//passPath = gm->map->getPassPath(from, to, flyingDirection, dest);
		passPath = getPassPath(from, fromOffset, to, toOffset);
		updateMagicWhenNewPosition();
		if ((position == dest) || Map::getTileDistance(src, srcOffset, position, offset) >= Map::getTileDistance(src, srcOffset, dest, destOffset))
		{
			beginDrop();
		}
		else if (getTime() - beginTime > lifeTime)
		{
			if (logicRunning)
			{
				logicRunning = false;
			}
			doing = ekHiding;
			result = erLifeExhaust;
		}
	}
	else if (doing == ekFlying)
	{		
		if (lifeTime > 0)
		{	
			bool useTraceSpeed = false;
			bool followByTraceEnemy = false;
			Point from = position;
			PointEx fromOffset = offset;
			bool meteorMoveHandled = updateMeteorMove(ft);
			bool roundMoveHandled = false;
			if (!meteorMoveHandled)
			{
				updateMoveImitateUserPosition();
				bool followDirectionUpdated = false;
				UTime elapsedMilliseconds = getTime() - beginTime;
				bool followByMoveKind = magic.level[level].moveKind == mmkFollow && elapsedMilliseconds > MAGIC_FOLLOW_DELAY;
				followByTraceEnemy = magic.traceEnemy > 0 && elapsedMilliseconds >= magic.traceEnemyDelayMilliseconds;
				if (followByMoveKind || followByTraceEnemy)
				{
					auto targetPtr = target.lock();
					auto targetNPC = std::dynamic_pointer_cast<NPC>(targetPtr);
					auto caster = std::dynamic_pointer_cast<NPC>(user.lock());
					auto canFollowTarget = [&](std::shared_ptr<NPC> candidate)
					{
						if (candidate == nullptr || candidate->nowAction == acDeath || candidate->nowAction == acHide || !candidate->isVisibleForRuntime())
						{
							return false;
						}
						if (candidate != gm->player && !gm->npcManager->findNPC(candidate))
						{
							return false;
						}
						if (magic.attackAll > 0)
						{
							return candidate != caster && isAttackAllFighterTarget(candidate);
						}
						return NPCManager::canLauncherHitNPC(launcherKind, candidate, user.lock());
					};
					if (canFollowTarget(targetNPC))
					{
						followDirectionUpdated = updateFollowDirectionToTarget(targetNPC);
						useTraceSpeed = followDirectionUpdated;
					}
					else
					{
						std::shared_ptr<NPC> nearestEnemy = nullptr;
						int nearestDistance = followByTraceEnemy ? std::numeric_limits<int>::max() : MAGIC_FOLLOW_RADIUS + 1;
						auto checkCandidate = [&](std::shared_ptr<NPC> candidate)
						{
							if (!canFollowTarget(candidate))
							{
								return;
							}
							if (caster && candidate.get() == caster.get())
							{
								return;
							}
							int dist = gm->map->calDistance(position, candidate->getPosition());
							bool canTraceCandidate = followByTraceEnemy || (dist <= MAGIC_FOLLOW_RADIUS && gm->map->canSee(position, candidate->getPosition()));
							if (canTraceCandidate && dist < nearestDistance)
							{
								nearestDistance = dist;
								nearestEnemy = candidate;
							}
						};
						for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
						{
							checkCandidate(gm->npcManager->npcList[i]);
						}
						checkCandidate(gm->player);
						if (nearestEnemy != nullptr)
						{
							target = nearestEnemy;
							followDirectionUpdated = updateFollowDirectionToTarget(nearestEnemy);
							useTraceSpeed = followDirectionUpdated;
						}
						else
						{
							target.reset();
						}
					}
				}

				if (!updateFollowMouseDirection())
				{
					updateRandomMoveDirection();
				}
				if (magic.circleMoveColockwise > 0 || magic.circleMoveAnticlockwise > 0)
				{
					if (!circleMoveBaseDirectionInitialized || followDirectionUpdated)
					{
						circleMoveBaseDirection = flyingDirection;
						circleMoveBaseDirectionInitialized = true;
					}
					Point realDirection = getCircleMoveDirection(circleMoveBaseDirection);
					if (!realDirection.is_zero())
					{
						flyingDirection = realDirection;
						direction = getDirection(flyingDirection);
					}
				}
				else
				{
					circleMoveBaseDirectionInitialized = false;
				}

				updateMoveBackDirection();
				if (result & erLifeExhaust)
				{
					return;
				}

				roundMoveHandled = roundMoveActive;
				updateRoundMovePosition(ft);
			}
			float flySpeed = (followByTraceEnemy && useTraceSpeed && magic.traceSpeed > 0)
				? (float)magic.traceSpeed
				: (float)speed;
			if (!meteorMoveHandled && !roundMoveHandled)
			{
				updateEffectPosition(ft, flySpeed);
			}
			Point to = position;
			PointEx toOffset = offset;
			//passPath = gm->map->getPassPath(from, to, flyingDirection, dest);
			passPath = getPassPath(from, fromOffset, to, toOffset);
			updateMagicWhenNewPosition();
			if (getTime() - beginTime >= lifeTime)
			{
				if (logicRunning)
				{
					logicRunning = false;
				}
				else if ((moveBackActive || magic.moveBack > 0) && beginMoveBack())
				{
					result = erNone;
				}
				else if (shouldExplodeWhenLifeFrameEnds())
				{
					beginExplode(position);
					result = erExplode;
					if (lifeTime == 0)
					{
						doing = ekHiding;
						result = erLifeExhaust;
					}
				}
				else
				{
					doing = ekHiding;
					result = erLifeExhaust;
				}			
			}
		}
		else
		{
			if (logicRunning)
			{
				logicRunning = false;
			}
			else
			{
				beginExplode(position);
				result = erExplode;
				if (lifeTime == 0)
				{
					doing = ekHiding;
					result = erLifeExhaust;
				}
			}		
		}	
	}
	else if (doing == ekExploding)
	{
		if (getTime() - beginTime >= lifeTime)
		{
			if (isSelfAnchoredMoveKind(magic.level[level].moveKind) && !vanishing)
			{
				vanishing = true;
				beginTime = getTime();
				lifeTime = getExplodinUTime();
				playSound(ekExploding);
				if (lifeTime == 0)
				{
					doing = ekHiding;
					result = erLifeExhaust;
				}
			}
			else
			{
				doing = ekHiding;
				result = erLifeExhaust;
			}
		}
		if (isSelfAnchoredMoveKind(magic.level[level].moveKind))
		{
			auto userPtr = user.lock();
			if (isLiveAnchoredUser(userPtr))
			{
				position = userPtr->position;
				offset = userPtr->offset;
			}
		}
	}
	else if (doing == ekHiding)
	{
		if (getTime() - beginTime >= waitTime)
		{
			if (getMoveKind() == mmkThrow)
			{
				doing = ekThrowing;
				beginTime += waitTime;
				waitTime = 0;
				magicWhenNewPositionLastTile = position;
				magicWhenNewPositionInitialized = true;
				playSound(ekFlying);
			}
			else
			{
				doing = ekFlying;
				beginTime += waitTime;
				waitTime = 0;
				magicWhenNewPositionLastTile = position;
				magicWhenNewPositionInitialized = true;
				playSound(doing);
				beginMeteorMove();
			}
			if (lifeTime > 0)
			{
				Point from = position;
				PointEx fromOffset = offset;
				updateEffectPosition(getTime() - beginTime, (float)speed);
				Point to = position;
				PointEx toOffset = offset;
				//passPath = gm->map->getPassPath(from, to, flyingDirection, dest);
				passPath = getPassPath(from, fromOffset, to, toOffset);
				updateMagicWhenNewPosition();
				if (getTime() - beginTime >= lifeTime)
				{
					if (shouldExplodeWhenLifeFrameEnds())
					{
						beginExplode(position);
						result = erExplode;
						if (lifeTime == 0)
						{
							doing = ekHiding;
							result = erLifeExhaust;
						}
					}
					else
					{
						doing = ekHiding;
						result = erLifeExhaust;
					}
				}
			}
			else
			{
				beginExplode(position);
				if (isSelfAnchoredMoveKind(magic.level[level].moveKind))
				{
					auto userPtr = user.lock();
					if (isLiveAnchoredUser(userPtr))
					{
						position = userPtr->position;
						offset = userPtr->offset;
					}
				}
			}
		}
	}
	if ((doing == ekFlying || doing == ekThrowing) && !vanishing)
	{
		updateFlyMagic(ft);
	}
	updateCarryUserPosition();
	if ((doing == ekFlying || doing == ekThrowing) && !vanishing)
	{
		handleCarryUser4NeighborCollisions();
	}
	updateAttachedNPCs();
	if (doing != ekHiding && !vanishing)
	{
		updateRangeEffect(ft);
	}
	if (result & erLifeExhaust)
	{
		finishTransportEffect();
		finishControlEffect();
	}
	updateSound();
}

PointEx Effect::getCollideOffset(Point pos)
{
	PointEx result;
	auto p0 = Map::getTilePosition(src, pos, { 0, 0 }, srcOffset);
	if (flyingDirection.x == 0)
	{
		result.x = (int)round(p0.x);
		result.y = 0;
	}
	else if (flyingDirection.y == 0)
	{
		result.y = (int)round(p0.y);
		result.x = 0;
	}
	else
	{
		float k0 = ((float)flyingDirection.y) / ((float)flyingDirection.x * MapXRatio);
		Point p1;
		p1.x = (int)round(((k0 * p0.x) - p0.y) / (k0 + 1 / k0));
		p1.y = (int)round(-p1.x / k0);
		//p1.x = (int)round(p1.x / MapXRatio);
		result.x = p1.x;
		result.y = p1.y;
	}

	return result;
}
