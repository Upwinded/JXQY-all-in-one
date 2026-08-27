#pragma once

#include "NPCActionBase.h"

class NPCActionStand : public NPCActionBase
{
public:
    NPCActionStand(NPC* npc);
    ~NPCActionStand() override = default;

    void enter() override;
    void update(UTime frameTime) override;
    void exit() override;

    NPCActionType getActionType() const override { return NPCActionType::acStand; }
    bool canTransitionTo(NPCActionType type) const override;

    _shared_image getActionImage(int* offsetx, int* offsety) override;
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    UTime getActionTime() const override;
    void playSound() override;

private:
    bool _useAlternate = false;
};
