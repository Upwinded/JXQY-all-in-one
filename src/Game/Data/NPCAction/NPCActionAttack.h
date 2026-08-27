#pragma once

#include "NPCActionBase.h"
#include "../GameElement.h"

class Magic;

// NPC攻击动作，管理攻击动画和伤害释放时机
class NPCActionAttack : public NPCActionBase
{
public:
    NPCActionAttack(NPC* npc);
    ~NPCActionAttack() override = default;

    // 进入攻击状态
    void enter() override;
    // 更新攻击状态
    void update(UTime frameTime) override;
    // 退出攻击状态
    void exit() override;

    // 获取当前攻击类型
    NPCActionType getActionType() const override { return _attackType; }
    // 判断是否可以转换到指定动作状态
    bool canTransitionTo(NPCActionType type) const override;

    // 获取当前帧的攻击动画图像
    _shared_image getActionImage(int* offsetx, int* offsety) override;
    // 获取当前帧的攻击动画阴影
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    // 获取当前攻击类型的动画总时长
    UTime getActionTime() const override;
    // 播放攻击音效
    void playSound() override;

    static _shared_imp selectSpecialAttackShadowPackage(_shared_imp actionShadow, _shared_imp useActionImage, _shared_imp actionImage, _shared_imp fallbackShadow)
    {
        (void)useActionImage;
        (void)actionImage;
        if (actionShadow != nullptr)
        {
            return actionShadow;
        }
        return fallbackShadow;
    }
private:
    // 根据攻击类型获取对应的图像资源
    _shared_imp getAttackImagePackage() const;
    // 根据攻击类型获取对应的阴影资源
    _shared_imp getAttackShadowPackage() const;

    NPCActionType _attackType = NPCActionType::acAttack;   // 攻击类型
    std::shared_ptr<Magic> _actionMagic = nullptr;
    Point _attackDest = { 0, 0 };                          // 攻击目标位置
    std::weak_ptr<GameElement> _target;                    // 攻击目标
    bool _attackDone = false;                              // 是否已释放伤害
};
