#pragma once

#include "NPCActionBase.h"

class NPCActionMagicForcedMove : public NPCActionBase
{
public:
    NPCActionMagicForcedMove(NPC* npc);
    ~NPCActionMagicForcedMove() override = default;

    void enter() override;
    void update(UTime frameTime) override;
    void exit() override;

    NPCActionType getActionType() const override { return NPCActionType::acMagicForcedMove; }
    bool canTransitionTo(NPCActionType type) const override;

    _shared_image getActionImage(int* offsetx, int* offsety) override;
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    UTime getActionTime() const override;
};
