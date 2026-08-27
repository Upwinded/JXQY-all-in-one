#pragma once

#include "NPCActionBase.h"

// NPC跑步动作，管理跑步移动逻辑、步进状态和战斗/跟随决策
class NPCActionRun : public NPCActionBase
{
public:
    // 构造函数，传入NPC指针
    NPCActionRun(NPC* npc);
    // 析构函数
    ~NPCActionRun() override = default;

    // 进入跑步状态
    void enter() override;
    // 更新跑步状态
    void update(UTime frameTime) override;
    // 退出跑步状态
    void exit() override;

    // 获取动作类型为跑步
    NPCActionType getActionType() const override { return NPCActionType::acRun; }
    // 判断是否可以转换到指定动作类型
    bool canTransitionTo(NPCActionType type) const override;

    // 获取跑步动作图像
    _shared_image getActionImage(int* offsetx, int* offsety) override;
    // 获取跑步动作阴影图像
    _shared_image getActionShadow(int* offsetx, int* offsety) override;
    // 获取跑步动作时长
    UTime getActionTime() const override;
    // 播放跑步音效
    void playSound() override;

    // 获取当前占用的格子位置（用于地图数据管理）
    std::vector<Point> getStepPositions() const override;

    // 重新设定移动目标，保持跑步状态但更新路径
    void retarget() override;

private:
    // 处理踏入（到达目标格）
    void processStepIn();
    // 处理踏出（离开当前格）
    void processStepOut();
    // 更新像素偏移实现平滑移动
    void updateOffset();

    // 处理玩家踏入时的逻辑（陷阱、NPC交互等）
    StepResult handlePlayerStepIn(Player* player);
    // 处理跟随NPC踏入时的逻辑（跟随目标、战斗切换）
    StepResult handleFollowerStepIn(Player* player, bool& canWalkNextStep);
    // 处理战斗NPC踏入时的AI决策
    StepResult handleBattleStepIn(Player* player);
    // 提交下一步移动
    void commitNextStep(Player* player, bool canWalkNextStep);

    // 上一步的位置坐标
    Point _previousPosition = { -1, -1 };
};
