#pragma once

#include "NPCActionBase.h"

// NPC行走动作，管理行走移动逻辑、步进状态和体力恢复
class NPCActionWalk : public NPCActionBase
{
public:
    // 构造函数，传入NPC指针
    NPCActionWalk(NPC* npc);
    // 析构函数
    ~NPCActionWalk() override = default;

    // 进入行走状态
    void enter() override;
    // 更新行走状态
    void update(UTime frameTime) override;
    // 退出行走状态
    void exit() override;

    // 获取动作类型为行走
    NPCActionType getActionType() const override { return NPCActionType::acWalk; }
    // 判断是否可以转换到指定动作类型
    bool canTransitionTo(NPCActionType type) const override;

    // 获取行走动作图像
    _shared_image getActionImage(int* offsetx, int* offsety) override;
    // 获取行走动作阴影图像
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    // 获取行走动作时长
    UTime getActionTime() const override;
    // 播放行走音效
    void playSound() override;

    // 获取当前占用的格子位置（用于地图数据管理）
    std::vector<Point> getStepPositions() const override;

    // 重新设定移动目标，保持行走状态但更新路径
    void retarget() override;

    // 到达目标格子后的处理
    void processStepIn();
    // 离开当前格子，更新位置和地图数据
    void processStepOut();
    // 计算当前步进的像素偏移量实现平滑移动
    void updateOffset();

private:
    // 处理玩家踏入时的逻辑（陷阱、NPC交互等）
    StepResult handlePlayerStepIn(Player* player);
    // 处理跟随NPC踏入时的逻辑（跟随目标、战斗切换）
    StepResult handleFollowerStepIn(bool& canWalkNextStep);
    // 处理战斗NPC踏入时的AI决策
    StepResult handleBattleStepIn(Player* player);
    // 提交下一步移动，检查可通行性
    void commitNextStep(Player* player, bool canWalkNextStep);

private:
    // 上一步的位置坐标
    Point _previousPosition = { -1, -1 };
};
