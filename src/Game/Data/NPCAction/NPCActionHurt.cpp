#include "NPCActionHurt.h"
#include "../NPC.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionHurt::NPCActionHurt(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionHurt::enter()
{
    _npc->destGE.reset();
    _npc->actionBeginTime = _npc->getTime();
    _npc->nowAction = acHurt;
    _npc->actionLastTime = _npc->getActionTime(acHurt);
    _npc->fightState.set(true);

    wasMoving = false;
    previousMoveType = NPCActionType::acStand;

    if (_npc->resumingMove)
    {
        wasMoving = true;
        previousMoveType = _npc->previousMoveAction;
    }
    else
    {
        _npc->clearStep();
        _npc->stepList.resize(0);
        _npc->offset = { 0, 0 };
    }

    playSound();
}

void NPCActionHurt::update(UTime frameTime)
{
    if (_npc->getTime() - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        _npc->lastBattleScanTime = 0;
        if (wasMoving && _npc->stepList.size() > 0)
        {
            _npc->actionManager->forceChangeAction(previousMoveType);
        }
        else
        {
            if (wasMoving)
            {
                _npc->clearStep();
                _npc->cancelMoveResumeState();
                _npc->stepList.resize(0);
                _npc->offset = { 0, 0 };
            }
            _npc->actionManager->forceChangeAction(NPCActionType::acStand);
        }
    }
}

void NPCActionHurt::exit()
{
 
}

bool NPCActionHurt::canTransitionTo(NPCActionType type) const
{
    return type == acDeath || type == acHide;
}

_shared_image NPCActionHurt::getActionImage(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(_npc->res.hurt.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

_shared_image NPCActionHurt::getActionShadow(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(_npc->res.hurt.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

UTime NPCActionHurt::getActionTime() const
{
    return IMP::getIMPImageActionTime(_npc->res.hurt.imagePackage);
}

void NPCActionHurt::playSound()
{
    _npc->playSound(acHurt);
}
