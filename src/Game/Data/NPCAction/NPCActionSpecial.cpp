#include "NPCActionSpecial.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionSpecial::NPCActionSpecial(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionSpecial::enter()
{
    _npc->clearStep();
    _npc->stepList.resize(0);
    _npc->offset = { 0, 0 };
    _npc->actionBeginTime = _npc->getTime();
    _npc->nowAction = acSpecial;
    _npc->actionLastTime = _npc->getActionTime(acSpecial);
}

void NPCActionSpecial::update(UTime frameTime)
{
    if (_npc->getTime() - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        Player* player = dynamic_cast<Player*>(_npc);
        if (player)
        {
            player->resetRecoveryTime(_npc->getTime());
        }
        _npc->actionManager->forceChangeAction(NPCActionType::acStand);
    }
}

void NPCActionSpecial::exit()
{
}

bool NPCActionSpecial::canTransitionTo(NPCActionType type) const
{
    return type == acHurt || type == acDeath || type == acHide;
}

_shared_image NPCActionSpecial::getActionImage(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(_npc->res.special.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

_shared_image NPCActionSpecial::getActionShadow(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(_npc->res.special.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

UTime NPCActionSpecial::getActionTime() const
{
    return IMP::getIMPImageActionTime(_npc->res.special.imagePackage);
}

void NPCActionSpecial::playSound()
{
    _npc->playSound(acSpecial);
}
