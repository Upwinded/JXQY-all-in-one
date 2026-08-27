#pragma once

#include "NPCActionBase.h"

class NPCActionJump : public NPCActionBase
{
public:
    NPCActionJump(NPC* npc);
    ~NPCActionJump() override = default;

    void enter() override;
    void update(UTime frameTime) override;
    void exit() override;

    NPCActionType getActionType() const override { return NPCActionType::acJump; }
    bool canTransitionTo(NPCActionType type) const override;

    _shared_image getActionImage(int* offsetx, int* offsety) override;
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    UTime getActionTime() const override;
    void playSound() override;

    std::vector<Point> getStepPositions() const override;

private:
    void updateJumpDownPhase();
    void updateJumpMiddlePhase(UTime frameTime);
    void updateJumpUpPhase();
    void handleJumpCompletion(bool isPlayer);
    UTime getElapsedTime() const;
    bool canJumpToDest() const;
    void clearJumpDest();
    
    float _jumpSpeed = 10.0f;
    bool _stepAdded = false;
    NPCActionType _lockedJumpAction = NPCActionType::acJump;
};
