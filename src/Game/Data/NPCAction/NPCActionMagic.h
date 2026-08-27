#pragma once

#include "NPCActionBase.h"
#include "../GameElement.h"
#include "../../GameTypes.h"

class Magic;

class NPCActionMagic : public NPCActionBase
{
public:
    NPCActionMagic(NPC* npc);
    ~NPCActionMagic() override = default;

    void enter() override;
    void update(UTime frameTime) override;
    void exit() override;

    NPCActionType getActionType() const override { return NPCActionType::acMagic; }
    bool canTransitionTo(NPCActionType type) const override;

    _shared_image getActionImage(int* offsetx, int* offsety) override;
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    UTime getActionTime() const override;
    void playSound() override;

    static _shared_imp selectMagicActionShadowPackage(_shared_imp actionShadow, _shared_imp useActionImage, _shared_imp actionImage, _shared_imp fallbackShadow)
    {
        (void)useActionImage;
        (void)actionImage;
        if (actionShadow != nullptr)
        {
            return actionShadow;
        }
        return fallbackShadow;
    }

    static UTime resolveMagicTriggerTime(UTime actionTime, bool isPlayer, bool triggerAtAnimationEnd)
    {
        if (!isPlayer || triggerAtAnimationEnd)
        {
            return actionTime;
        }
        if (actionTime < PLAYER_MAGIC_DELAY)
        {
            return actionTime;
        }
        return PLAYER_MAGIC_DELAY;
    }
private:
    _shared_imp getMagicActionImagePackage() const;
    _shared_imp getMagicActionShadowPackage() const;

    Point _magicDest = { 0, 0 };
    int _magicLevel = 1;
    int _magicListIndex = -1;
    std::shared_ptr<Magic> _magicToUse = nullptr;
    std::weak_ptr<GameElement> _target;
    bool _magicDone = false;
};
