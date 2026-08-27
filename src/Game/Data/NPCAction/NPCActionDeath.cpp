#include "NPCActionDeath.h"
#include "NPCDeathState.h"
#include "../NPC.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionDeath::NPCActionDeath(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionDeath::enter()
{
    _npc->clearStep();
    _npc->cancelMoveResumeState();
    _npc->stepList.resize(0);
    _npc->actionBeginTime = _npc->getTime();
    _npc->nowAction = acDeath;

    _npc->useSpecialDeath = false;
    _npc->specialDeathAction = "";
    _npc->noAddBody = false;

    NPCSpecialDeathKind specialDeathKind = selectNpcSpecialDeathKind(
        _npc->frozen,
        _npc->frozenVisualEffect,
        gm->global.feature.freezeVisualEffect,
        _npc->poisoned,
        _npc->poisonedVisualEffect,
        gm->global.feature.poisonVisualEffect,
        _npc->petrified,
        _npc->petrifiedVisualEffect,
        gm->global.feature.petrifyVisualEffect);
    switch (specialDeathKind)
    {
    case NPCSpecialDeathKind::Frozen:
        _npc->specialDeathAction = "asf\\interlude\\die-冰.asf";
        break;
    case NPCSpecialDeathKind::Poisoned:
        _npc->specialDeathAction = "asf\\interlude\\die-毒.asf";
        break;
    case NPCSpecialDeathKind::Petrified:
        _npc->specialDeathAction = "asf\\interlude\\die-石.asf";
        break;
    default:
        break;
    }

    _npc->clearAbnormalState();

    if (shouldNpcSpecialDeathSuppressBody(specialDeathKind))
    {
        _npc->noAddBody = true;
        _npc->loadSpecialAction(_npc->specialDeathAction);
        if (_npc->res.special.imagePackage != nullptr)
        {
            _npc->useSpecialDeath = true;
        }
    }

    _npc->actionLastTime = _npc->getActionTime(acDeath);
    playSound();
}

void NPCActionDeath::update(UTime frameTime)
{
    if (_npc->getTime() - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        if (_npc->nowAction != acHide)
        {
            if (_npc->kind == nkPlayer && !_npc->noAddBody)
            {
                _npc->addBody();
            }
            _npc->result |= erLifeExhaust;
            _npc->actionManager->changeAction(acHide);
        }
    }
}

void NPCActionDeath::exit()
{

}

bool NPCActionDeath::canTransitionTo(NPCActionType type) const
{
    if (type == acHide)
    {
        return true;
    }
    return false;
}

_shared_image NPCActionDeath::getActionImage(int* offsetx, int* offsety)
{
    if (_npc->useSpecialDeath && _npc->res.special.imagePackage != nullptr)
    {
        return IMP::loadImageForDirection(_npc->res.special.imagePackage, 0,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
    }
    return IMP::loadImageForDirection(_npc->res.death.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

_shared_image NPCActionDeath::getActionShadow(int* offsetx, int* offsety)
{
    if (_npc->useSpecialDeath && _npc->res.special.shadowPackage != nullptr)
    {
        return IMP::loadImageForDirection(_npc->res.special.shadowPackage, 0,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
    }
    return IMP::loadImageForDirection(_npc->res.death.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

UTime NPCActionDeath::getActionTime() const
{
    if (_npc->useSpecialDeath && _npc->res.special.imagePackage != nullptr)
    {
        return IMP::getIMPImageActionTime(_npc->res.special.imagePackage);
    }
    return IMP::getIMPImageActionTime(_npc->res.death.imagePackage);
}

void NPCActionDeath::playSound()
{
    _npc->playSound(acDeath);
}
