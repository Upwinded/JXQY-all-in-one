#include "NPCActionMagicForcedMove.h"
#include "../NPC.h"
#include "../../../Image/IMP.h"

NPCActionMagicForcedMove::NPCActionMagicForcedMove(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionMagicForcedMove::enter()
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

void NPCActionMagicForcedMove::update(UTime frameTime)
{
    _npc->updateMagicForcedMovement(frameTime);
    auto nextAction = resolveForcedMovementActionAfterUpdate(getActionType(), _npc->isBouncing(), _npc->isMagicForcedMoving());
    if (nextAction != getActionType())
    {
        _npc->actionManager->forceChangeAction(nextAction);
    }
}

void NPCActionMagicForcedMove::exit()
{
}

bool NPCActionMagicForcedMove::canTransitionTo(NPCActionType type) const
{
    return isForcedMovementActionTransitionAllowed(type);
}

_shared_image NPCActionMagicForcedMove::getActionImage(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(
        _npc->nowAction == acAStand ? _npc->res.astand.imagePackage : _npc->res.stand.imagePackage,
        _npc->direction,
        _npc->getTime() - _npc->actionBeginTime,
        offsetx,
        offsety,
        true);
}

_shared_image NPCActionMagicForcedMove::getActionShadow(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(
        _npc->nowAction == acAStand ? _npc->res.astand.shadowPackage : _npc->res.stand.shadowPackage,
        _npc->direction,
        _npc->getTime() - _npc->actionBeginTime,
        offsetx,
        offsety,
        true);
}

UTime NPCActionMagicForcedMove::getActionTime() const
{
    return IMP::getIMPImageActionTime(
        _npc->nowAction == acAStand ? _npc->res.astand.imagePackage : _npc->res.stand.imagePackage);
}
