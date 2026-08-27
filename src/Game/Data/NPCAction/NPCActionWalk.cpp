#include "NPCActionWalk.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionWalk::NPCActionWalk(NPC* npc)
    : NPCActionBase(npc)
{
}

// 进入行走状态，处理从中断恢复或新开始行走两种情况
void NPCActionWalk::enter()
{
    // 从中断恢复行走的情况（如受击后恢复）
    if (_npc->resumingMove)
    {
        _npc->resumingMove = false;
        _npc->direction = _npc->savedDirection;
        _npc->stepState = _npc->savedStepState;
        _npc->stepBeginTime += _npc->getTime() - _npc->hurtBeginStepTime;

        if (_npc->savedStepState == ssIn && _npc->savedStepPositions.size() > 0)
        {
            _previousPosition = _npc->savedStepPositions[0];
        }
        else
        {
            _previousPosition = _npc->position;
        }

        if (isFightActionAvailable(acWalk, acAWalk))
        {
            _npc->nowAction = acAWalk;
            _npc->actionLastTime = _npc->getActionTime(acAWalk);
        }
        else
        {
            _npc->nowAction = acWalk;
            _npc->actionLastTime = _npc->getActionTime(acWalk);
        }

        _npc->actionBeginTime = _npc->getTime();
        _npc->cancelMoveResumeState();
        return;
    }

    // 新开始行走的情况
    _npc->clearStep();
    if (_npc->stepList.size() == 0)
    {
        _npc->beginStand();
        return;
    }
    
    _npc->offset = { 0, 0 };
    _npc->stepState = ssOut;
    _npc->stepBeginTime = _npc->getTime();
    _npc->actionBeginTime = _npc->getTime();
    _previousPosition = _npc->position;
    
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        _npc->nowAction = acAWalk;
        _npc->actionLastTime = _npc->getActionTime(acAWalk);
    }
    else
    {
        _npc->nowAction = acWalk;
        _npc->actionLastTime = _npc->getActionTime(acWalk);
    }
    
    calStepLastTime(_npc->getAdjustedWalkSpeed());
    gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    playSound();
}

// 更新行走状态，处理体力恢复、步进状态机和动画循环
void NPCActionWalk::update(UTime frameTime)
{
    Player* player = dynamic_cast<Player*>(_npc);

    // 体力恢复逻辑（仅玩家角色）
    if (player)
    {
        player->recoverWhenStandingOrWalking();
    }

    // 根据战斗状态切换行走动画（普通行走/战斗行走）
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        if (_npc->nowAction != acAWalk)
        {
            _npc->nowAction = acAWalk;
            _npc->actionLastTime = _npc->getActionTime(acAWalk);
        }
    }
    else
    {
        if (_npc->nowAction != acWalk)
        {
            _npc->nowAction = acWalk;
            _npc->actionLastTime = _npc->getActionTime(acWalk);
        }
    }

    // 动画循环播放，到达动作时长后重新播放并播放音效
    if (_npc->getTime() - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        playSound();
        _npc->actionBeginTime += _npc->actionLastTime;
    }
    
    // 步进状态机循环处理
    while (1)
    {
        if (_npc->getTime() - _npc->stepBeginTime < _npc->stepLastTime)
        {
            break;
        }

        if (_npc->stepState == ssIn)
        {
            processStepIn();
        }
        else if (_npc->stepState == ssOut)
        {
            if (_npc->stepList.empty())
            {
                _npc->beginStand();
                return;
            }
            processStepOut();
        }
        else
        {
            break;
        }

        // 如果动作已被切换为非行走类型，则退出更新
        auto currentType = _npc->actionManager->getCurrentActionType();
        if (currentType != acWalk && currentType != acAWalk)
        {
            return;
        }
    }

    updateOffset();
}

// 退出行走状态，清理地图数据
void NPCActionWalk::exit()
{
    // 如果是中断恢复中，不清理地图数据
    if (_npc->resumingMove)
    {
        return;
    }
    
    // 从地图数据中移除当前占用的格子
    std::vector<Point> stepPositions = getStepPositions();
    for (const Point& pos : stepPositions)
    {
        gm->map->deleteStepFromDataMap(pos, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    }
}

// 行走不能转换到另一个行走（允许其他所有状态转换）
bool NPCActionWalk::canTransitionTo(NPCActionType type) const
{
    return type != acWalk && type != acAWalk;
}

// 获取行走动作图像
_shared_image NPCActionWalk::getActionImage(int* offsetx, int* offsety)
{
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        return IMP::loadImageForDirection(_npc->res.awalk.imagePackage, _npc->direction,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
    }
    return IMP::loadImageForDirection(_npc->res.walk.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
}

// 获取行走动作阴影图像
_shared_image NPCActionWalk::getActionShadow(int* offsetx, int* offsety)
{
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        return IMP::loadImageForDirection(_npc->res.awalk.shadowPackage, _npc->direction,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
    }
    return IMP::loadImageForDirection(_npc->res.walk.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
}

// 获取行走动作时长
UTime NPCActionWalk::getActionTime() const
{
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        return IMP::getIMPImageActionTime(_npc->res.awalk.imagePackage);
    }
    return IMP::getIMPImageActionTime(_npc->res.walk.imagePackage);
}

// 播放行走音效
void NPCActionWalk::playSound()
{
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        _npc->playSound(acAWalk);
    }
    else
    {
        _npc->playSound(acWalk);
    }
}

// 重新设定移动目标，保持行走状态但更新路径
void NPCActionWalk::retarget()
{
    if (_npc->stepList.empty())
    {
        _npc->beginStand();
        return;
    }
    _npc->offset = { 0, 0 };
    _npc->stepState = ssOut;
    _npc->stepBeginTime = _npc->getTime();
    _previousPosition = _npc->position;
    _npc->direction = _npc->getDirection(_npc->stepList[0]);
    calStepLastTime(_npc->getAdjustedWalkSpeed());
    gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    if (isFightActionAvailable(acWalk, acAWalk))
    {
        _npc->nowAction = acAWalk;
        _npc->actionLastTime = _npc->getActionTime(acAWalk);
    }
    else
    {
        _npc->nowAction = acWalk;
        _npc->actionLastTime = _npc->getActionTime(acWalk);
    }
    updateOffset();
}

// 到达目标格子后的处理，依次检查玩家逻辑、战斗AI、跟随逻辑，然后提交下一步
void NPCActionWalk::processStepIn()
{
    struct StepInProcessingGuard
    {
        NPC* npc;
        explicit StepInProcessingGuard(NPC* value) : npc(value) { npc->processingStepIn = true; }
        ~StepInProcessingGuard() { npc->processingStepIn = false; }
    } stepInGuard(_npc);

    // 从地图数据中移除上一步位置
    gm->map->deleteStepFromDataMap(_previousPosition, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));

    bool canWalkNextStep = false;
    // 弹出已完成的步进
    if (_npc->stepList.size() > 0)
    {
        _npc->stepList.pop_front();
    }

    Player* player = dynamic_cast<Player*>(_npc);

    // 依次检查：玩家踏入逻辑 -> 战斗AI -> 跟随逻辑
    StepResult result = handlePlayerStepIn(player);
    if (result == StepResult::Terminated)
    {
        return;
    }

    result = handleBattleStepIn(player);
    if (result == StepResult::Terminated)
    {
        return;
    }

    result = handleFollowerStepIn(canWalkNextStep);
    if (result == StepResult::Terminated)
    {
        return;
    }
    {
        auto currentType = _npc->actionManager->getCurrentActionType();
        if (currentType != acWalk && currentType != acAWalk)
        {
            return;
        }
    }

    // 提交下一步移动
    commitNextStep(player, canWalkNextStep);
}

// 玩家踏入时检查陷阱、NPC交互（攻击/对话/物体触发）、下一个动作
StepResult NPCActionWalk::handlePlayerStepIn(Player* player)
{
    if (!player)
    {
        return StepResult::Continue;
    }

    // 检查是否踩到陷阱
    if (gm->map->haveTraps(_npc->position) && !gm->inEvent)
    {
        NPC* npc = _npc;
        int trapIndex = gm->map->getTrapIndex(npc->position);
        player->nextAction = nullptr;
        if (npc->isRunning())
        {
            player->resetRecoveryTime(npc->getTime());
        }
        npc->stepList.resize(0);
        npc->actionBeginTime = npc->stepBeginTime + npc->stepLastTime;
        npc->offset = { 0, 0 };
        npc->beginStand();
        gm->runTrapScript(trapIndex);
        return StepResult::Terminated;
    }

    // 统一由 Player 处理旧兼容路径与严格手柄交互的到达重验。
    if (player->nextAction == nullptr && player->nextDest != ndNone
        && player->handleQueuedInteractionAtCurrentPosition())
    {
        return StepResult::Terminated;
    }

    // 检查是否有下一个移动动作需要处理
    if (tryHandleMoveNextAction(player, false, false))
    {
        return StepResult::Terminated;
    }

    // 如果下一个动作不是移动类型，则停止行走
    if (player->nextAction != nullptr)
    {
        auto action = player->nextAction->action;
        bool isMoveAction = (action == acWalk || action == acAWalk || action == acRun || action == acARun);
        if (!isMoveAction)
        {
            _npc->stepList.resize(0);
            _npc->offset = { 0, 0 };
            if (_npc->isRunning())
            {
                player->resetRecoveryTime(_npc->getTime());
            }
            _npc->beginStand();
            return StepResult::Terminated;
        }
    }

    return StepResult::Continue;
}

// 跟随NPC踏入时检查是否需要追击或跟随主人
StepResult NPCActionWalk::handleFollowerStepIn(bool& canWalkNextStep)
{
    if (!_npc->isFollower())
    {
        return StepResult::Continue;
    }
	// 伙伴战斗路径优先，避免在同一次踏入中被主人跟随路径覆盖而往返摇摆。
	if (NPC::shouldPrioritizeCombatMovement(
		_npc->kind, gm->global.data.PartnerCombat, _npc->hasActiveCombatWork()))
	{
		return StepResult::Continue;
	}

    canWalkNextStep = true;
    std::shared_ptr<NPC> fnpc = nullptr;
    // 伙伴跟随玩家，其他跟随指定NPC
    if (_npc->kind == nkPartner)
    {
        fnpc = gm->player;
    }
    else
    {
        auto fnpcList = gm->npcManager->findNPC(_npc->followNPC);
        if (fnpcList.size() > 0)
        {
            fnpc = fnpcList[0];
        }
    }

    if (fnpc != nullptr)
    {
        // 追击模式：尝试执行战斗动作
        if (_npc->isFollowAttack(fnpc) && _npc->kind != nkPartner)
        {
            if (!_npc->isCombatTargetValid(fnpc))
            {
                _npc->stepList.resize(0);
                _npc->clearCombatTargetMemory();
                _npc->beginStand();
                return StepResult::Terminated;
            }
            bool canSeeTarget = _npc->canSee(fnpc->position);
            Point chasePosition = fnpc->position;
            if (canSeeTarget)
            {
                _npc->rememberCombatTargetPosition(fnpc);
            }
            else
            {
                auto memoryPosition = _npc->getCombatTargetMemoryPosition(fnpc);
                if (!memoryPosition.has_value())
                {
                    _npc->stepList.resize(0);
                    _npc->clearCombatTargetMemory();
                    _npc->beginStand();
                    return StepResult::Terminated;
                }
                chasePosition = memoryPosition.value();
            }

            auto chaseDistance = gm->map->calDistance(_npc->position, chasePosition);
            if (chaseDistance > _npc->visionRadius * 2)
            {
                _npc->stepList.resize(0);
                _npc->clearCombatTargetMemory();
                _npc->beginStand();
                return StepResult::Terminated;
            }
            if (!canSeeTarget && chaseDistance <= 1)
            {
                _npc->stepList.resize(0);
                _npc->clearCombatTargetMemory();
                _npc->beginStand();
                return StepResult::Terminated;
            }
            _npc->offset = { 0, 0 };
            if (canSeeTarget && _npc->evaluateAndPlan(fnpc) && _npc->executeActionPlan(fnpc))
            {
                return StepResult::Terminated;
            }
            // 无法执行战斗动作，重新规划追击路径
            _npc->changeFollowAttack(chasePosition);
            if (_npc->stepList.empty())
            {
                canWalkNextStep = false;
            }
        }
        // 跟随模式：根据距离决定行走或奔跑跟随
        else if (_npc->kind != nkPartner || !_npc->isPartnerBlockingPlayer)
        {
            _npc->offset = { 0, 0 };
            _npc->stepList.resize(0);
            auto distance = gm->map->calDistance(_npc->position, fnpc->position);
            int followRadius = _npc->kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
            int followRunRadius = _npc->kind == nkPartner ? gm->global.getPartnerFollowRunRadius() : NPC_FOLLOW_RADIUS_RUN;
            bool requestedFollowMove = false;
            if ((distance > followRunRadius) && _npc->canDoAction(acRun))
            {
                _npc->changeFollowRun(fnpc->position);
                requestedFollowMove = true;
            }
            else if (distance > followRadius)
            {
                _npc->changeFollowWalk(fnpc->position);
                requestedFollowMove = true;
            }
            else
            {
                // 距离足够近，停止行走
                _npc->beginStand();
                return StepResult::Terminated;
            }
            if (requestedFollowMove && _npc->stepList.empty())
            {
                canWalkNextStep = false;
            }
        }
    }
    else
    {
        // 跟随目标不存在，清除跟随信息
        _npc->followNPC = "";
    }

    return StepResult::Continue;
}

// 战斗NPC踏入时触发AI调度
StepResult NPCActionWalk::handleBattleStepIn(Player* player)
{
	if (_npc->isAIEnabled() && player == nullptr &&
		(_npc->kind == nkBattle || (_npc->kind == nkPartner && gm->global.data.PartnerCombat)))
	{
		auto selfPtr = std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr());
		bool actionHandled = gm->npcManager->scheduleBattleAction(selfPtr);
		if (actionHandled)
		{
			auto currentType = _npc->actionManager->getCurrentActionType();
			if (currentType != acWalk && currentType != acAWalk)
			{
				return StepResult::Terminated;
			}
			return StepResult::Continue;
		}
	}
	return StepResult::Continue;
}

// 提交下一步移动，检查可通行性
void NPCActionWalk::commitNextStep(Player* player, bool canWalkNextStep)
{
    if (_npc->stepList.size() > 0 && _npc->canEnterMoveStep(_npc->stepList[0]))
    {
        canWalkNextStep = true;
        _npc->direction = _npc->getDirection(_npc->stepList[0]);
        
        // 正方向（偶数方向）需要检查两侧是否可通行，避免穿墙
        if (_npc->direction % 2 == 0)
        {
            int dir1 = _npc->direction + 1;
            int dir2 = _npc->direction - 1;
            _npc->limitDir(&dir1);
            _npc->limitDir(&dir2);
            
            if (_npc->kind == nkFlyingAnimal ||
                (gm->map->canPass(gm->map->getSubPoint(_npc->position, dir1)) &&
                    gm->map->canPass(gm->map->getSubPoint(_npc->position, dir2))))
            {
                _npc->stepState = ssOut;
                gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
                _npc->direction = _npc->getDirection(_npc->stepList[0]);
                _npc->stepBeginTime += _npc->stepLastTime;
                calStepLastTime(_npc->getAdjustedWalkSpeed());
            }
            else
            {
                canWalkNextStep = false;
            }
        }
        // 斜方向（奇数方向）直接可以移动
        else
        {
            _npc->stepState = ssOut;
            _npc->stepBeginTime += _npc->stepLastTime;
            gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
            _npc->direction = _npc->getDirection(_npc->stepList[0]);
            calStepLastTime(_npc->getAdjustedWalkSpeed());
        }
    }
    
    // 无法继续行走，转为站立状态
    if (!canWalkNextStep)
    {
        if (tryHandleMoveNextAction(player, true, false))
        {
            return;
        }
        if (player && _npc->isRunning())
        {
            player->resetRecoveryTime(_npc->getTime());
        }
        _npc->stepList.resize(0);
        _npc->beginStand();
    }
}

// 离开当前格子，更新位置和地图数据
void NPCActionWalk::processStepOut()
{
    _previousPosition = _npc->position;
    
    // 从地图数据中移除目标格子的步进占位
    gm->map->deleteStepFromDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    
    // 更新NPC位置到目标格子
    _npc->setPosition(_npc->stepList[0], false);
    _npc->stepState = ssIn;
    _npc->stepBeginTime += _npc->stepLastTime;

    // 在地图数据中添加原位置的占位（步进期间同时占用两个格子）
    gm->map->addStepToDataMap(_previousPosition, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
}

// 获取当前占用的格子位置（用于地图数据管理）
std::vector<Point> NPCActionWalk::getStepPositions() const
{
    std::vector<Point> result;
    
    if (_npc->stepState == ssOut)
    {
        // 步出状态：占用目标格子
        if (!_npc->stepList.empty())
        {
            result.push_back(_npc->stepList[0]);
        }
    }
    else
    {
        // 步入状态：占用上一步位置
        if (_previousPosition.x >= 0 && _previousPosition.y >= 0)
        {
            result.push_back(_previousPosition);
        }
    }
    
    return result;
}

// 计算当前步进的像素偏移量实现平滑移动
void NPCActionWalk::updateOffset()
{
    _npc->calOffset(_npc->getTime() - _npc->stepBeginTime, _npc->stepLastTime);
}
