#pragma once

#include "NPCActionBase.h"
#include <cstdint>
#include <memory>

class NPC;

class NPCActionManager
{
public:
    NPCActionManager(NPC* npc);
    ~NPCActionManager() = default;

    void changeAction(NPCActionType type);
    void forceChangeAction(NPCActionType type);
    // Reserved for explicit script/runtime state resets that must leave terminal actions.
    void resetActionIgnoringTransitions(NPCActionType type);
    // Scripted one-shot actions must also restart when the requested type is already active.
    void restartActionIgnoringTransitions(NPCActionType type);
    void update(UTime frameTime);

    NPCActionBase* getCurrentAction() const { return _currentAction.get(); }
    uint64_t getActionRevision() const { return _actionRevision; }
    NPCActionType getCurrentActionType() const;
    std::vector<Point> getStepPositions() const;

    _shared_image getActionImage(int* offsetx, int* offsety);
    _shared_image getActionShadow(int* offsetx, int* offsety);
    UTime getActionTime() const;
    void playSound();

    bool isInAction(NPCActionType type) const;
    bool isStanding() const;
    bool isWalking() const;
    bool isRunning() const;
    bool isJumping() const;
    bool isAttacking() const;
    bool isMagicing() const;
    bool isHurting() const;
    bool isDying() const;
    bool isHiding() const;
    bool isSitting() const;
    bool isDoingSpecialAction() const;

    bool canDoAction(NPCActionType type) const;

private:
    uint64_t _actionRevision = 0;
    void createAction(NPCActionType type);
    void resetAction(NPCActionType type);

    NPC* _npc = nullptr;
    std::shared_ptr<NPCActionBase> _currentAction;
    NPCActionType _currentActionType = NPCActionType::acStand;
};
