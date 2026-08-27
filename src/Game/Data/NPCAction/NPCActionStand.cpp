#include <cmath>
#include "../../../Engine/Engine.h"
#include "NPCActionStand.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"


NPCActionStand::NPCActionStand(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionStand::enter()
{
    _npc->destGE.reset();
    _npc->clearStep();
    _npc->cancelMoveResumeState();
    _npc->stepList.resize(0);
    _npc->offset = { 0, 0 };
    _useAlternate = false;
    _npc->actionBeginTime = _npc->getTime();
    
    if (isFightActionAvailable(acStand, acAStand))
    {
        _npc->nowAction = acAStand;
        _npc->actionLastTime = _npc->getActionTime(acAStand);
    }
    else
    {
        _npc->nowAction = acStand;
        _npc->actionLastTime = _npc->getActionTime(acStand);
    }
    playSound();
}

void NPCActionStand::update(UTime frameTime)
{
    Player* player = dynamic_cast<Player*>(_npc);
    if (player)
    {
        player->recoverWhenStandingOrWalking();
        if (player->logicRunning)
        {
            player->haveAsyncDest = false;
        }
    }

    if (_npc->haveAsyncDest)
    {
        if (_npc->position != _npc->gotoExDest)
        {
            if (gm->map->canWalk(_npc->gotoExDest))
            {
                _npc->beginWalk(_npc->gotoExDest);
                return;
            }
        }
        else
        {
            _npc->haveAsyncDest = false;
        }
    }

    bool npcAIEnabled = _npc->isAIEnabled();
    if (npcAIEnabled && player == nullptr &&
        (_npc->kind == nkBattle || (_npc->kind == nkPartner && gm->global.data.PartnerCombat)))
    {
        if (_npc->tryKeepDistanceWhenFriendDeath())
        {
            return;
        }
        if (_npc->getTime() - _npc->lastBattleScanTime >= NPC_BATTLE_SCAN_INTERVAL)
        {
            _npc->lastBattleScanTime = _npc->getTime();
            auto selfPtr = std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr());
            bool actionHandled = gm->npcManager->scheduleBattleAction(selfPtr);
            if (actionHandled)
            {
                return;
            }
        }
    }

	bool hasCombatWork = _npc->hasActiveCombatWork();
	if (npcAIEnabled && _npc->isFollower() && !hasCombatWork)
	{
		UTime now = _npc->getTime();
		if (_npc->nextFollowCheckTime == 0)
		{
			int jitter = NPC_FOLLOW_INTERVAL_RANDOM_RANGE > 0 ? _npc->engine->getRand(NPC_FOLLOW_INTERVAL_RANDOM_RANGE - 1) : 0;
			_npc->nextFollowCheckTime = now + NPC_FOLLOW_INTERVAL + jitter;
		}
		if (now >= _npc->nextFollowCheckTime)
		{
            std::shared_ptr<NPC> fnpc = nullptr;
            if (_npc->kind == nkPartner)
            {
                fnpc = gm->player;
            }
            else
            {
                auto fnpcList = gm->npcManager->findNPC(_npc->followNPC);
                if (fnpcList.size() > 0)
                {
                    fnpc = fnpcList[0];
                }
            }

            if (fnpc != nullptr)
            {
                if (_npc->isFollowAttack(fnpc) && _npc->kind != nkPartner)
                {
                    if (!_npc->isCombatTargetValid(fnpc))
                    {
                        _npc->clearCombatTargetMemory();
                        _npc->beginStand();
                        _npc->lastTimeTryingToFollow = now;
                        int jitter = NPC_FOLLOW_INTERVAL_RANDOM_RANGE > 0 ? _npc->engine->getRand(NPC_FOLLOW_INTERVAL_RANDOM_RANGE - 1) : 0;
                        _npc->nextFollowCheckTime = now + NPC_FOLLOW_INTERVAL + jitter;
                        return;
                    }

                    bool canSeeTarget = _npc->canSee(fnpc->position);
                    Point chasePosition = fnpc->position;
                    if (canSeeTarget)
                    {
                        _npc->rememberCombatTargetPosition(fnpc);
                    }
                    else
                    {
                        auto memoryPosition = _npc->getCombatTargetMemoryPosition(fnpc);
                        if (!memoryPosition.has_value())
                        {
                            _npc->clearCombatTargetMemory();
                            _npc->beginStand();
                            _npc->lastTimeTryingToFollow = now;
                            int jitter = NPC_FOLLOW_INTERVAL_RANDOM_RANGE > 0 ? _npc->engine->getRand(NPC_FOLLOW_INTERVAL_RANDOM_RANGE - 1) : 0;
                            _npc->nextFollowCheckTime = now + NPC_FOLLOW_INTERVAL + jitter;
                            return;
                        }
                        chasePosition = memoryPosition.value();
                    }

                    auto chaseDistance = gm->map->calDistance(_npc->position, chasePosition);
                    if (chaseDistance > _npc->visionRadius * 2)
                    {
                        _npc->clearCombatTargetMemory();
                        _npc->beginStand();
                    }
                    else if (!canSeeTarget && chaseDistance <= 1)
                    {
                        _npc->clearCombatTargetMemory();
                        _npc->beginStand();
                    }
                    else
                    {
                        if (canSeeTarget && !(_npc->evaluateAndPlan(fnpc) && _npc->executeActionPlan(fnpc)))
                        {
                            _npc->beginFollowAttack(chasePosition);
                        }
                        else if (!canSeeTarget)
                        {
                            _npc->beginFollowAttack(chasePosition);
                        }
                    }
                    _npc->lastTimeTryingToFollow = now;
                    int jitter = NPC_FOLLOW_INTERVAL_RANDOM_RANGE > 0 ? _npc->engine->getRand(NPC_FOLLOW_INTERVAL_RANDOM_RANGE - 1) : 0;
                    _npc->nextFollowCheckTime = now + NPC_FOLLOW_INTERVAL + jitter;
                    return;
                }
                else
                {
                    auto distance = gm->map->calDistance(_npc->position, fnpc->position);
                    int followRadius = _npc->kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
                    int followRunRadius = _npc->kind == nkPartner ? gm->global.getPartnerFollowRunRadius() : NPC_FOLLOW_RADIUS_RUN;
                    if ((distance > followRunRadius) && _npc->canDoAction(acRun))
                    {
                        _npc->beginFollowRun(fnpc->position);
                    }
                    else if (distance > followRadius)
                    {
                        _npc->beginFollowWalk(fnpc->position);
                    }
                    _npc->lastTimeTryingToFollow = now;
                    int jitter = NPC_FOLLOW_INTERVAL_RANDOM_RANGE > 0 ? _npc->engine->getRand(NPC_FOLLOW_INTERVAL_RANDOM_RANGE - 1) : 0;
                    _npc->nextFollowCheckTime = now + NPC_FOLLOW_INTERVAL + jitter;
                    return;
                }
            }
            else
            {
                _npc->followNPC = "";
            }
            _npc->lastTimeTryingToFollow = now;
            int jitter = NPC_FOLLOW_INTERVAL_RANDOM_RANGE > 0 ? _npc->engine->getRand(NPC_FOLLOW_INTERVAL_RANDOM_RANGE - 1) : 0;
            _npc->nextFollowCheckTime = now + NPC_FOLLOW_INTERVAL + jitter;
        }
    }

    if (player == nullptr && _npc->tryMoveToDestinationMapPosition())
    {
        return;
    }

    const UTime currentTime = _npc->getTime();
    if (currentTime >= _npc->actionBeginTime
        && currentTime - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        _npc->actionBeginTime += _npc->actionLastTime;
        
        if (isFightActionAvailable(acStand, acAStand))
        {
            _npc->nowAction = acAStand;
            _npc->actionLastTime = _npc->getActionTime(acAStand);
            playSound();
        }
        else
        {
            if (!_useAlternate && _npc->canDoAction(acStand1) && _npc->engine->getRand(5) == 0)
            {
                _npc->nowAction = acStand1;
                _npc->actionLastTime = _npc->getActionTime(acStand1);
                _useAlternate = true;
            }
            else
            {
                _npc->nowAction = acStand;
                _npc->actionLastTime = _npc->getActionTime(acStand);
                _useAlternate = false;
            }
            playSound();
        }
    }
}

void NPCActionStand::exit()
{
}

bool NPCActionStand::canTransitionTo(NPCActionType type) const
{
    return type != acStand && type != acStand1 && type != acAStand;
}

_shared_image NPCActionStand::getActionImage(int* offsetx, int* offsety)
{
    const UTime currentTime = _npc->getTime();
    const UTime elapsedTime = currentTime >= _npc->actionBeginTime
        ? currentTime - _npc->actionBeginTime
        : 0;
    if (isFightActionAvailable(acStand, acAStand))
    {
        return IMP::loadImageForDirection(_npc->res.astand.imagePackage, _npc->direction,
            elapsedTime, offsetx, offsety);
    }
    
    if (_useAlternate && _npc->canDoAction(acStand1))
    {
        return IMP::loadImageForDirection(_npc->res.stand1.imagePackage, _npc->direction,
            elapsedTime, offsetx, offsety);
    }
    
    return IMP::loadImageForDirection(_npc->res.stand.imagePackage, _npc->direction,
        elapsedTime, offsetx, offsety);
}

_shared_image NPCActionStand::getActionShadow(int* offsetx, int* offsety)
{
    const UTime currentTime = _npc->getTime();
    const UTime elapsedTime = currentTime >= _npc->actionBeginTime
        ? currentTime - _npc->actionBeginTime
        : 0;
    if (isFightActionAvailable(acStand, acAStand))
    {
        return IMP::loadImageForDirection(_npc->res.astand.shadowPackage, _npc->direction,
            elapsedTime, offsetx, offsety);
    }
    
    if (_useAlternate && _npc->canDoAction(acStand1))
    {
        return IMP::loadImageForDirection(_npc->res.stand1.shadowPackage, _npc->direction,
            elapsedTime, offsetx, offsety);
    }
    
    return IMP::loadImageForDirection(_npc->res.stand.shadowPackage, _npc->direction,
        elapsedTime, offsetx, offsety);
}

UTime NPCActionStand::getActionTime() const
{
    if (isFightActionAvailable(acStand, acAStand))
    {
        return IMP::getIMPImageActionTime(_npc->res.astand.imagePackage);
    }
    
    if (_useAlternate && _npc->canDoAction(acStand1))
    {
        return IMP::getIMPImageActionTime(_npc->res.stand1.imagePackage);
    }
    
    return IMP::getIMPImageActionTime(_npc->res.stand.imagePackage);
}

void NPCActionStand::playSound()
{
    if (isFightActionAvailable(acStand, acAStand))
    {
        _npc->playSound(acAStand);
    }
    else if (_useAlternate)
    {
        _npc->playSound(acStand1);
    }
    else
    {
        _npc->playSound(acStand);
    }
}
