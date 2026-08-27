#pragma once

#include "../../../Engine/ImageTypes.h"
#include "../../../Types/Types.h"
#include "NPCActionType.h"
#include <vector>

// 动作步骤执行结果
enum class StepResult
{
    Continue,   // 继续执行
    Terminated  // 已终止
};

class NPC;
class Player;

// NPC动作状态机基类，定义动作的进入、更新、退出等生命周期接口
class NPCActionBase
{
public:
    NPCActionBase(NPC* npc);
    virtual ~NPCActionBase() = default;

    // 进入该动作状态时调用
    virtual void enter() = 0;
    // 每帧更新动作状态
    virtual void update(UTime frameTime) = 0;
    // 退出该动作状态时调用
    virtual void exit() = 0;

    // 获取当前动作类型
    virtual NPCActionType getActionType() const = 0;
    // 判断是否可以从当前动作转换到指定动作
    virtual bool canTransitionTo(NPCActionType type) const;

    // 获取当前动作的图像资源及偏移量
    virtual _shared_image getActionImage(int* offsetx, int* offsety);
    // 获取当前动作的阴影图像及偏移量
    virtual _shared_image getActionShadow(int* offsetx, int* offsety);
    // 获取当前动作的持续时间
    virtual UTime getActionTime() const;
    // 播放当前动作的音效
    virtual void playSound();

    // 获取移动步骤的位置列表
    virtual std::vector<Point> getStepPositions() const { return {}; }

    // 重新选择目标（用于战斗中切换攻击目标等场景）
    virtual void retarget() {}

    // 根据速度计算每步的持续时间
    void calStepLastTime(float speed);

protected:
    // 尝试处理玩家的下一个移动动作（行走/跑步切换）
    bool tryHandleMoveNextAction(Player* player, bool resetCurrentPath, bool setRecoveryWhenSwitchToWalk);
    // 判断战斗动作是否可用（处于战斗状态且动作资源可用）
    bool isFightActionAvailable(NPCActionType normalAction, NPCActionType fightAction) const;
    NPC* _npc = nullptr; // 关联的NPC对象
};
