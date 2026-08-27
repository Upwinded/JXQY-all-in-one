#include "NPCActionManager.h"
#include "NPCActionStand.h"
#include "NPCActionWalk.h"
#include "NPCActionRun.h"
#include "NPCActionJump.h"
#include "NPCActionAttack.h"
#include "NPCActionBounce.h"
#include "NPCActionMagicForcedMove.h"
#include "NPCActionMagic.h"
#include "NPCActionHurt.h"
#include "NPCActionDeath.h"
#include "NPCActionHide.h"
#include "NPCActionSit.h"
#include "NPCActionSpecial.h"
#include "../NPC.h"

NPCActionManager::NPCActionManager(NPC* npc)
    : _npc(npc)
{
    createAction(NPCActionType::acStand);
}

void NPCActionManager::changeAction(NPCActionType type)
{
    if (isInAction(type))
    {
        return;
    }
    if (_currentAction && !_currentAction->canTransitionTo(type))
    {
        return;
    }

    resetAction(type);
}

void NPCActionManager::forceChangeAction(NPCActionType type)
{
    auto currentType = getCurrentActionType();
    if (currentType == NPCActionType::acDeath || currentType == NPCActionType::acHide)
    {
        return;
    }
    if (isInAction(type))
    {
        return;
    }

    resetAction(type);
}

void NPCActionManager::resetActionIgnoringTransitions(NPCActionType type)
{
    if (isInAction(type))
    {
        return;
    }

    resetAction(type);
}

void NPCActionManager::restartActionIgnoringTransitions(NPCActionType type)
{
    resetAction(type);
}

void NPCActionManager::resetAction(NPCActionType type)
{
    auto oldAction = _currentAction;
    if (oldAction)
    {
        oldAction->exit();
    }

    createAction(type);
    _currentActionType = type;
    ++_actionRevision;

    if (_currentAction)
    {
        _currentAction->enter();
    }
}

void NPCActionManager::update(UTime frameTime)
{
    auto currentAction = _currentAction;
    if (currentAction)
    {
        currentAction->update(frameTime);
    }
}

NPCActionType NPCActionManager::getCurrentActionType() const
{
    if (_currentAction)
    {
        return _currentAction->getActionType();
    }
    return NPCActionType::acStand;
}

std::vector<Point> NPCActionManager::getStepPositions() const
{
    if (_currentAction)
    {
        return _currentAction->getStepPositions();
    }
    return {};
}

_shared_image NPCActionManager::getActionImage(int* offsetx, int* offsety)
{
    if (_currentAction)
    {
        return _currentAction->getActionImage(offsetx, offsety);
    }
    return nullptr;
}

_shared_image NPCActionManager::getActionShadow(int* offsetx, int* offsety)
{
    if (_currentAction)
    {
        return _currentAction->getActionShadow(offsetx, offsety);
    }
    return nullptr;
}

UTime NPCActionManager::getActionTime() const
{
    if (_currentAction)
    {
        return _currentAction->getActionTime();
    }
    return 0;
}

void NPCActionManager::playSound()
{
    if (_currentAction)
    {
        _currentAction->playSound();
    }
}

bool NPCActionManager::isInAction(NPCActionType type) const
{
    return getCurrentActionType() == type;
}

bool NPCActionManager::isStanding() const
{
    auto type = getCurrentActionType();
    return type == NPCActionType::acStand
        || type == NPCActionType::acStand1
        || type == NPCActionType::acAStand
        || type == NPCActionType::acBounce
        || type == NPCActionType::acMagicForcedMove;
}

bool NPCActionManager::isWalking() const
{
    auto type = getCurrentActionType();
    return type == NPCActionType::acWalk || type == NPCActionType::acAWalk;
}

bool NPCActionManager::isRunning() const
{
    auto type = getCurrentActionType();
    return type == NPCActionType::acRun || type == NPCActionType::acARun;
}

bool NPCActionManager::isJumping() const
{
    auto type = getCurrentActionType();
    return type == NPCActionType::acJump || type == NPCActionType::acAJump;
}

bool NPCActionManager::isAttacking() const
{
    auto type = getCurrentActionType();
    return type == NPCActionType::acAttack || type == NPCActionType::acAttack1 || type == NPCActionType::acAttack2 || type == NPCActionType::acSpecialAttack;
}

bool NPCActionManager::isMagicing() const
{
    return getCurrentActionType() == NPCActionType::acMagic;
}

bool NPCActionManager::isHurting() const
{
    return getCurrentActionType() == NPCActionType::acHurt;
}

bool NPCActionManager::isDying() const
{
    return getCurrentActionType() == NPCActionType::acDeath;
}

bool NPCActionManager::isHiding() const
{
    return getCurrentActionType() == NPCActionType::acHide;
}

bool NPCActionManager::isSitting() const
{
    auto type = getCurrentActionType();
    return type == NPCActionType::acSit || type == NPCActionType::acSitting;
}

bool NPCActionManager::isDoingSpecialAction() const
{
    return getCurrentActionType() == NPCActionType::acSpecial;
}

bool NPCActionManager::canDoAction(NPCActionType type) const
{
    return _npc->canDoAction(type);
}

void NPCActionManager::createAction(NPCActionType type)
{
    switch (type)
    {
    case NPCActionType::acStand:
    case NPCActionType::acStand1:
    case NPCActionType::acAStand:
        _currentAction = std::make_shared<NPCActionStand>(_npc);
        break;
    case NPCActionType::acWalk:
    case NPCActionType::acAWalk:
        _currentAction = std::make_shared<NPCActionWalk>(_npc);
        break;
    case NPCActionType::acRun:
    case NPCActionType::acARun:
        _currentAction = std::make_shared<NPCActionRun>(_npc);
        break;
    case NPCActionType::acJump:
    case NPCActionType::acAJump:
        _currentAction = std::make_shared<NPCActionJump>(_npc);
        break;
    case NPCActionType::acAttack:
    case NPCActionType::acAttack1:
    case NPCActionType::acAttack2:
    case NPCActionType::acSpecialAttack:
        _currentAction = std::make_shared<NPCActionAttack>(_npc);
        break;
    case NPCActionType::acMagic:
        _currentAction = std::make_shared<NPCActionMagic>(_npc);
        break;
    case NPCActionType::acBounce:
        _currentAction = std::make_shared<NPCActionBounce>(_npc);
        break;
    case NPCActionType::acMagicForcedMove:
        _currentAction = std::make_shared<NPCActionMagicForcedMove>(_npc);
        break;
    case NPCActionType::acHurt:
        _currentAction = std::make_shared<NPCActionHurt>(_npc);
        break;
    case NPCActionType::acDeath:
        _currentAction = std::make_shared<NPCActionDeath>(_npc);
        break;
    case NPCActionType::acHide:
        _currentAction = std::make_shared<NPCActionHide>(_npc);
        break;
    case NPCActionType::acSit:
    case NPCActionType::acSitting:
        _currentAction = std::make_shared<NPCActionSit>(_npc);
        break;
    case NPCActionType::acSpecial:
        _currentAction = std::make_shared<NPCActionSpecial>(_npc);
        break;
    default:
        _currentAction = std::make_shared<NPCActionStand>(_npc);
        break;
    }
}
