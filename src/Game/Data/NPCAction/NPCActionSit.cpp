#include "NPCActionSit.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"
#include <cmath>

NPCActionSit::NPCActionSit(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionSit::enter()
{
    _npc->clearStep();
    _npc->stepList.resize(0);
    _npc->offset = { 0, 0 };
    _npc->actionBeginTime = _npc->getTime();
    _npc->nowAction = acSitting;
    _npc->sitState = ssSitting;
    _npc->actionLastTime = _npc->getActionTime(acSit);
    _isSitting = false;
    playSound();
}

void NPCActionSit::update(UTime frameTime)
{
    if (_npc->getTime() - _npc->actionBeginTime > _npc->actionLastTime && !_isSitting)
    {
        _npc->nowAction = acSit;
        _npc->sitState = ssSat;
        _npc->actionBeginTime += _npc->actionLastTime;
        _isSitting = true;

        Player* player = dynamic_cast<Player*>(_npc);
        if (player)
        {
            if (player->mana == player->info.manaMax)
            {
                _npc->beginStand();
                return;
            }
            else if (player->thew < SIT_THEW_COST)
            {
                gm->showMessage("体力不足!");
                _npc->beginStand();
                return;
            }
            else
            {
                player->thew -= SIT_THEW_COST;
                player->addMana(convert_max((int)round(SIT_MANA_ADD_RATE * player->info.manaMax), SIT_THEW_COST));
                if (player->mana == player->info.manaMax)
                {
                    _npc->beginStand();
                    return;
                }
            }
        }
    }

    Player* player = dynamic_cast<Player*>(_npc);
    if (_isSitting && player)
    {
        if (_npc->getTime() - _npc->actionBeginTime > MANA_RECOVERY_INTERVAL)
        {
            _npc->actionBeginTime += MANA_RECOVERY_INTERVAL;
            if (player->mana == player->info.manaMax)
            {
                _npc->beginStand();
                return;
            }
            else if (player->thew < SIT_THEW_COST)
            {
                gm->showMessage("体力不足!");
                _npc->beginStand();
                return;
            }
            else
            {
                player->thew -= SIT_THEW_COST;
                player->addMana(convert_max((int)round(SIT_MANA_ADD_RATE * player->info.manaMax), SIT_THEW_COST));
                if (player->mana == player->info.manaMax)
                {
                    _npc->beginStand();
                    return;
                }
            }
        }
        if (player->nextAction != nullptr)
        {
            _npc->beginStand();
            return;
        }
    }
}

void NPCActionSit::exit()
{
    _npc->sitState = ssSitting;
}

bool NPCActionSit::canTransitionTo(NPCActionType type) const
{
    return type != acSit && type != acSitting;
}

_shared_image NPCActionSit::getActionImage(int* offsetx, int* offsety)
{
    if (_isSitting)
    {
        return IMP::loadImageForLastFrame(_npc->res.sit.imagePackage, _npc->direction, offsetx, offsety);
    }
    return IMP::loadImageForDirection(_npc->res.sit.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

_shared_image NPCActionSit::getActionShadow(int* offsetx, int* offsety)
{
    if (_isSitting)
    {
        return IMP::loadImageForLastFrame(_npc->res.sit.shadowPackage, _npc->direction, offsetx, offsety);
    }
    return IMP::loadImageForDirection(_npc->res.sit.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

UTime NPCActionSit::getActionTime() const
{
    return IMP::getIMPImageActionTime(_npc->res.sit.imagePackage);
}

void NPCActionSit::playSound()
{
    _npc->playSound(acSit);
}
