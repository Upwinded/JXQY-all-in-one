#pragma once

#include "NPCActionBase.h"

class NPCActionBounce : public NPCActionBase
{
public:
    NPCActionBounce(NPC* npc);
    ~NPCActionBounce() override = default;

    void enter() override;
    void update(UTime frameTime) override;
    void exit() override;

    NPCActionType getActionType() const override { return NPCActionType::acBounce; }
    bool canTransitionTo(NPCActionType type) const override;

    _shared_image getActionImage(int* offsetx, int* offsety) override;
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    UTime getActionTime() const override;
};
