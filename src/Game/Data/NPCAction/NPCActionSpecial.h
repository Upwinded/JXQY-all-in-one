#pragma once

#include "NPCActionBase.h"

class NPCActionSpecial : public NPCActionBase
{
public:
    NPCActionSpecial(NPC* npc);
    ~NPCActionSpecial() override = default;

    void enter() override;
    void update(UTime frameTime) override;
    void exit() override;

    NPCActionType getActionType() const override { return NPCActionType::acSpecial; }
    bool canTransitionTo(NPCActionType type) const override;

    _shared_image getActionImage(int* offsetx, int* offsety) override;
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    UTime getActionTime() const override;
    void playSound() override;
};
