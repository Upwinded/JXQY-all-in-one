#include "NPCActionBounce.h"
#include "../NPC.h"
#include "../../../Image/IMP.h"

NPCActionBounce::NPCActionBounce(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionBounce::enter()
{
    _npc->clearStep();
    _npc->cancelMoveResumeState();
    _npc->stepList.resize(0);
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
}

void NPCActionBounce::update(UTime frameTime)
{
    _npc->updateBounceMovement(frameTime);
    auto nextAction = resolveForcedMovementActionAfterUpdate(getActionType(), _npc->isBouncing(), _npc->isMagicForcedMoving());
    if (nextAction != getActionType())
    {
        _npc->actionManager->forceChangeAction(nextAction);
    }
}

void NPCActionBounce::exit()
{
}

bool NPCActionBounce::canTransitionTo(NPCActionType type) const
{
    return isForcedMovementActionTransitionAllowed(type);
}

_shared_image NPCActionBounce::getActionImage(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(
        _npc->nowAction == acAStand ? _npc->res.astand.imagePackage : _npc->res.stand.imagePackage,
        _npc->direction,
        _npc->getTime() - _npc->actionBeginTime,
        offsetx,
        offsety,
        true);
}

_shared_image NPCActionBounce::getActionShadow(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(
        _npc->nowAction == acAStand ? _npc->res.astand.shadowPackage : _npc->res.stand.shadowPackage,
        _npc->direction,
        _npc->getTime() - _npc->actionBeginTime,
        offsetx,
        offsety,
        true);
}

UTime NPCActionBounce::getActionTime() const
{
    return IMP::getIMPImageActionTime(
        _npc->nowAction == acAStand ? _npc->res.astand.imagePackage : _npc->res.stand.imagePackage);
}
