#include "NPCActionBase.h"
#include "../Map.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

// 构造函数，初始化关联的NPC对象
NPCActionBase::NPCActionBase(NPC* npc)
    : _npc(npc)
{
}

// 判断是否可以从当前动作转换到指定动作，默认允许所有转换
bool NPCActionBase::canTransitionTo(NPCActionType type) const
{
    return true;
}

// 获取当前动作的图像资源及偏移量，默认返回空
_shared_image NPCActionBase::getActionImage(int* offsetx, int* offsety)
{
    return nullptr;
}

// 获取当前动作的阴影图像及偏移量，默认返回空
_shared_image NPCActionBase::getActionShadow(int* offsetx, int* offsety)
{
    return nullptr;
}

// 获取当前动作的持续时间，默认返回0
UTime NPCActionBase::getActionTime() const
{
    return 0;
}

// 播放当前动作的音效，默认空实现
void NPCActionBase::playSound()
{
}

// 判断战斗动作是否可用（处于战斗状态且动作资源可用）
bool NPCActionBase::isFightActionAvailable(NPCActionType normalAction, NPCActionType fightAction) const
{
    bool fightActionAvailable = _npc->canDoAction(fightAction);
    if (_npc->fightState.get() && fightActionAvailable)
    {
        return true;
    }
    if (fightActionAvailable && !_npc->canDoAction(normalAction))
    {
        return true;
    }
    return false;
}

// 尝试处理玩家的下一个移动动作（行走/跑步切换）
// resetCurrentPath: 是否重置当前路径
// setRecoveryWhenSwitchToWalk: 切换到行走时是否设置恢复时间
bool NPCActionBase::tryHandleMoveNextAction(Player* player, bool resetCurrentPath, bool setRecoveryWhenSwitchToWalk)
{
    if (player == nullptr || player->nextAction == nullptr)
    {
        return false;
    }
    auto action = player->nextAction->action;
    bool isWalkAction = (action == acWalk || action == acAWalk);
    bool isRunAction = (action == acRun || action == acARun);
    if (!isWalkAction && !(isRunAction && player->canRun))
    {
        return false;
    }

    Point dest = player->nextAction->dest;
    player->nextDest = player->nextAction->destKind;
    player->nextDestUseRightScript = player->nextAction->useRightScript;
    player->nextDestStrictWorldInteraction = player->nextAction->strictWorldInteraction;
    player->nextDestRequestedRunning = isRunAction;
    player->destGE = player->nextAction->destGE;
    player->nextAction = nullptr;

    if (isWalkAction && setRecoveryWhenSwitchToWalk)
    {
        player->resetRecoveryTime(_npc->getTime());
    }

    if (!resetCurrentPath)
    {
        if (isWalkAction)
        {
            return player->changeWalk(dest);
        }
        else
        {
            return player->changeRun(dest);
        }
    }

    NPC* npc = _npc;
    bool success = false;
    if (isWalkAction)
    {
        success = player->changeWalk(dest);
    }
    else
    {
        success = player->changeRun(dest);
    }
    
    if (!success)
    {
        npc->offset = { 0, 0 };
        npc->stepList.resize(0);
    }
    
    return success;
}

// 根据实际下一格的投影距离计算每步持续时间。
void NPCActionBase::calStepLastTime(float speed)
{
    if (_npc == nullptr)
    {
        return;
    }
    if (speed <= 0.0f)
    {
        speed = 0.1f;
    }
    float stepDistance = 1.0f;
    Point stepPosition = !_npc->stepList.empty() ? _npc->stepList.front() : Map::getSubPoint(_npc->position, _npc->direction);
    stepDistance = Map::getTileDistance(_npc->position, { 0.0f, 0.0f }, stepPosition, { 0.0f, 0.0f });
    if (stepDistance <= 0.0f)
    {
        stepDistance = 1.0f;
    }
    auto tempStepLastTime = (0.5f / speed / (float)Config::getGameSpeed());
    _npc->stepLastTime = static_cast<UTime>(round(stepDistance * tempStepLastTime));
}
