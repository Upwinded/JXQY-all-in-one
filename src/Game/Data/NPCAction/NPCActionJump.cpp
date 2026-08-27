#include "NPCActionJump.h"
#include "../NPC.h"
#include "../ProjectedMovement.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"
#include <cmath>

NPCActionJump::NPCActionJump(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionJump::enter()
{
    _npc->destGE.reset();
    _npc->offset = { 0, 0 };
    _npc->actionBeginTime = _npc->getTime();
    _stepAdded = false;
    
    _lockedJumpAction = isFightActionAvailable(acJump, acAJump)
        ? acAJump
        : acJump;
    if (_lockedJumpAction == acAJump)
    {
        _npc->nowAction = acAJump;
        _npc->actionLastTime = _npc->getActionTime(acAJump);
    }
    else
    {
        _npc->nowAction = acJump;
        _npc->actionLastTime = _npc->getActionTime(acJump);
    }
    
    if (_npc->stepList.size() > 0)
    {
        Point step = _npc->stepList[0];
        _npc->flyingDirection = Map::getTilePosition(step, _npc->position, { 0, 0 }, { 0, 0 });
        if (canJumpToDest())
        {
            gm->map->addStepToDataMap(step, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
            _stepAdded = true;
        }
        else
        {
            clearJumpDest();
        }
    }
    else
    {
        _npc->flyingDirection = { 0, 0 };
    }
    
    PointEx jumpVector = { (float)_npc->flyingDirection.x, (float)_npc->flyingDirection.y };
    _jumpSpeed = getProjectedMovementSpeedForDuration(jumpVector, (float)(_npc->actionLastTime / 3), Config::getGameSpeed());
    _npc->jumpSpeed = _jumpSpeed;
    _npc->jumpState = jsUp;

    playSound();
}

UTime NPCActionJump::getElapsedTime() const
{
    return _npc->getTime() - _npc->actionBeginTime;
}

bool NPCActionJump::canJumpToDest() const
{
    if (_npc->stepList.empty())
    {
        return false;
    }
    Point dest = _npc->stepList[0];
    return gm->map->canJump(dest);
}

void NPCActionJump::clearJumpDest()
{
    _npc->stepList.clear();
    _npc->flyingDirection = { 0, 0 };
}

void NPCActionJump::updateJumpDownPhase()
{
    if (_npc->jumpState != jsDown)
    {
        _npc->jumpState = jsDown;
        
        std::vector<Point> stepPositions = getStepPositions();
        for (const Point& pos : stepPositions)
        {
            gm->map->deleteStepFromDataMap(pos, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
        }
        
        if (_stepAdded && _npc->stepList.size() > 0)
        {
            _npc->setPosition(_npc->stepList[0], false);
        }
        _npc->offset = { 0.0, 0.0 };
        _npc->stepList.resize(0);
        _stepAdded = false;
    }
}

void NPCActionJump::updateJumpMiddlePhase(UTime frameTime)
{
    gm->map->deleteNPCFromDataMap(_npc->position, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    if (_npc->jumpState != jsJumping)
    {
        _npc->updateJumpingPosition(getElapsedTime() - _npc->actionLastTime / 3, _jumpSpeed);
        _npc->jumpState = jsJumping;
    }
    else
    {
        _npc->updateJumpingPosition(frameTime, _jumpSpeed);
    }
    gm->map->addNPCToDataMap(_npc->position, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
}

void NPCActionJump::updateJumpUpPhase()
{
    _npc->jumpState = jsUp;
}

void NPCActionJump::handleJumpCompletion(bool isPlayer)
{
    updateJumpDownPhase();
    
    if (isPlayer)
    {
        NPC* npc = _npc;
        int trapIndex = -1;
        Player* player = dynamic_cast<Player*>(npc);
        if (player)
        {
            player->resetRecoveryTime(npc->getTime());
        }
        
        if (gm->map->haveTraps(npc->position))
        {
            trapIndex = gm->map->getTrapIndex(npc->position);
            if (player)
            {
                player->nextAction = nullptr;
            }
        }
        npc->actionManager->forceChangeAction(NPCActionType::acStand);
        if (trapIndex != -1)
        {
            gm->runTrapScript(trapIndex);
        }
    }
    else
    {
        _npc->actionManager->forceChangeAction(NPCActionType::acStand);
    }
}

void NPCActionJump::update(UTime frameTime)
{
    const bool isPlayer = (_npc->kind == nkPlayer);
    const UTime elapsed = getElapsedTime();
    const UTime actionTime = _npc->actionLastTime;
    
    if (elapsed > actionTime)
    {
        handleJumpCompletion(isPlayer);
    }
    else if (elapsed > 2 * actionTime / 3)
    {
        updateJumpDownPhase();
    }
    else if (elapsed > actionTime / 3)
    {
        updateJumpMiddlePhase(frameTime);
    }
    else
    {
        updateJumpUpPhase();
    }
}

void NPCActionJump::exit()
{
    _npc->offset = { 0, 0 };
    
    std::vector<Point> stepPositions = getStepPositions();
    for (const Point& pos : stepPositions)
    {
        gm->map->deleteStepFromDataMap(pos, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    }
    _stepAdded = false;
}

bool NPCActionJump::canTransitionTo(NPCActionType type) const
{
    if (type == acHurt || type == acDeath || type == acHide)
    {
        return _npc->jumpState != jsJumping;
    }
    return false;
}

_shared_image NPCActionJump::getActionImage(int* offsetx, int* offsety)
{
    if (_lockedJumpAction == acAJump)
    {
        return IMP::loadImageForDirection(_npc->res.ajump.imagePackage, _npc->direction,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
    }
    return IMP::loadImageForDirection(_npc->res.jump.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

_shared_image NPCActionJump::getActionShadow(int* offsetx, int* offsety)
{
    if (_lockedJumpAction == acAJump)
    {
        return IMP::loadImageForDirection(_npc->res.ajump.shadowPackage, _npc->direction,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
    }
    return IMP::loadImageForDirection(_npc->res.jump.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

UTime NPCActionJump::getActionTime() const
{
    if (_lockedJumpAction == acAJump)
    {
        return IMP::getIMPImageActionTime(_npc->res.ajump.imagePackage);
    }
    return IMP::getIMPImageActionTime(_npc->res.jump.imagePackage);
}

void NPCActionJump::playSound()
{
    if (_lockedJumpAction == acAJump)
    {
        _npc->playSound(acAJump);
    }
    else
    {
        _npc->playSound(acJump);
    }
}

std::vector<Point> NPCActionJump::getStepPositions() const
{
    std::vector<Point> result;
    if (!_stepAdded || _npc->stepList.empty())
    {
        return result;
    }
    result.push_back(_npc->stepList[0]);
    return result;
}
