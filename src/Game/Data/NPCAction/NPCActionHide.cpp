#include "NPCActionHide.h"
#include "../NPC.h"
#include "../NPCManager.h"
#include "../../GameManager/GameManager.h"

NPCActionHide::NPCActionHide(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionHide::enter()
{
    _npc->clearStep();
    _npc->cancelMoveResumeState();
    _npc->stepList.resize(0);
    _npc->actionBeginTime = _npc->getTime();
    _npc->nowAction = acHide;
    _npc->actionLastTime = 0;
    _scriptCalled = false;
}

void NPCActionHide::update(UTime frameTime)
{
    if (_npc->kind == nkPlayer)
    {
        return;
    }

    if (!_scriptCalled)
    {
        _scriptCalled = true;
        return;
    }
}

void NPCActionHide::exit()
{
}

bool NPCActionHide::canTransitionTo(NPCActionType type) const
{
    return false;
}

_shared_image NPCActionHide::getActionImage(int* offsetx, int* offsety)
{
    return nullptr;
}

_shared_image NPCActionHide::getActionShadow(int* offsetx, int* offsety)
{
    return nullptr;
}

UTime NPCActionHide::getActionTime() const
{
    return 0;
}

void NPCActionHide::playSound()
{
}
