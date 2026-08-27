#include "NPCActionRun.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionRun::NPCActionRun(NPC* npc)
    : NPCActionBase(npc)
{
}

// 进入跑步状态，处理从中断恢复或新开始跑步两种情况
void NPCActionRun::enter()
{
    // 从中断中恢复跑步（如受击后继续移动）
    if (_npc->resumingMove)
    {
        _npc->resumingMove = false;
        _npc->direction = _npc->savedDirection;
        _npc->stepState = _npc->savedStepState;
        _npc->stepBeginTime += _npc->getTime() - _npc->hurtBeginStepTime;

        // 恢复上一步位置：若恢复时处于踏入状态则使用保存的位置，否则用当前位置
        if (_npc->savedStepState == ssIn && _npc->savedStepPositions.size() > 0)
        {
            _previousPosition = _npc->savedStepPositions[0];
        }
        else
        {
            _previousPosition = _npc->position;
        }

        // 根据是否有战斗动作资源选择跑步动画
        if (isFightActionAvailable(acRun, acARun))
        {
            _npc->nowAction = acARun;
            _npc->actionLastTime = _npc->getActionTime(acARun);
        }
        else
        {
            _npc->nowAction = acRun;
            _npc->actionLastTime = _npc->getActionTime(acRun);
        }

        _npc->actionBeginTime = _npc->getTime();
        _npc->cancelMoveResumeState();
        return;
    }

    // 新开始跑步
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
    
    // 根据是否有战斗动作资源选择跑步动画
    if (isFightActionAvailable(acRun, acARun))
    {
        _npc->nowAction = acARun;
        _npc->actionLastTime = _npc->getActionTime(acARun);
    }
    else
    {
        _npc->nowAction = acRun;
        _npc->actionLastTime = _npc->getActionTime(acRun);
    }
    
    calStepLastTime(_npc->getAdjustedRunSpeed());
    gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    playSound();
}

// 更新跑步状态，处理步进状态机和动画循环
void NPCActionRun::update(UTime frameTime)
{
    // 根据战斗状态切换跑步/战斗跑步动画
    if (isFightActionAvailable(acRun, acARun))
    {
        if (_npc->nowAction != acARun)
        {
            _npc->nowAction = acARun;
            _npc->actionLastTime = _npc->getActionTime(acARun);
        }
    }
    else
    {
        if (_npc->nowAction != acRun)
        {
            _npc->nowAction = acRun;
            _npc->actionLastTime = _npc->getActionTime(acRun);
        }
    }

    // 动画循环播放：到达动作时长后重新播放音效并重置计时
    if (_npc->getTime() - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        playSound();
        _npc->actionBeginTime += _npc->actionLastTime;
    }
    
    // 步进状态机：循环处理踏入/踏出直到当前步进未完成或动作被切换
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

        // 如果动作已被切换为非跑步类型，直接返回
        auto currentType = _npc->actionManager->getCurrentActionType();
        if (currentType != acRun && currentType != acARun)
        {
            return;
        }
    }

    updateOffset();
}

// 退出跑步状态，清理地图数据
void NPCActionRun::exit()
{
    // 如果是中断恢复模式（如受击），不清理地图数据
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

// 跑步不能转换到另一个跑步（允许其他所有状态转换）
bool NPCActionRun::canTransitionTo(NPCActionType type) const
{
    return type != acRun && type != acARun;
}

_shared_image NPCActionRun::getActionImage(int* offsetx, int* offsety)
{
    if (isFightActionAvailable(acRun, acARun))
    {
        return IMP::loadImageForDirection(_npc->res.arun.imagePackage, _npc->direction,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
    }
    return IMP::loadImageForDirection(_npc->res.run.imagePackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
}

_shared_image NPCActionRun::getActionShadow(int* offsetx, int* offsety)
{
    if (isFightActionAvailable(acRun, acARun))
    {
        return IMP::loadImageForDirection(_npc->res.arun.shadowPackage, _npc->direction,
            _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
    }
    return IMP::loadImageForDirection(_npc->res.run.shadowPackage, _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety);
}

UTime NPCActionRun::getActionTime() const
{
    if (isFightActionAvailable(acRun, acARun))
    {
        return IMP::getIMPImageActionTime(_npc->res.arun.imagePackage);
    }
    return IMP::getIMPImageActionTime(_npc->res.run.imagePackage);
}

void NPCActionRun::playSound()
{
    if (isFightActionAvailable(acRun, acARun))
    {
        _npc->playSound(acARun);
    }
    else
    {
        _npc->playSound(acRun);
    }
}

// 重新设定移动目标，保持跑步状态但更新路径
void NPCActionRun::retarget()
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
    calStepLastTime(_npc->getAdjustedRunSpeed());
    gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    // 根据战斗状态选择跑步动画
    if (isFightActionAvailable(acRun, acARun))
    {
        _npc->nowAction = acARun;
        _npc->actionLastTime = _npc->getActionTime(acARun);
    }
    else
    {
        _npc->nowAction = acRun;
        _npc->actionLastTime = _npc->getActionTime(acRun);
    }
    updateOffset();
}

// 到达目标格子后的处理，依次检查玩家逻辑、战斗AI、跟随逻辑，然后提交下一步
void NPCActionRun::processStepIn()
{
    struct StepInProcessingGuard
    {
        NPC* npc;
        explicit StepInProcessingGuard(NPC* value) : npc(value) { npc->processingStepIn = true; }
        ~StepInProcessingGuard() { npc->processingStepIn = false; }
    } stepInGuard(_npc);

    // 从地图数据中删除上一步位置的占用
    gm->map->deleteStepFromDataMap(_previousPosition, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));

    bool canWalkNextStep = false;
    // 弹出已完成的步进
    if (_npc->stepList.size() > 0)
    {
        _npc->stepList.pop_front();
    }

    Player* player = dynamic_cast<Player*>(_npc);

    // 依次处理：玩家踏入逻辑 -> 战斗AI -> 跟随逻辑
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

    result = handleFollowerStepIn(player, canWalkNextStep);
    if (result == StepResult::Terminated)
    {
        return;
    }
    {
        auto currentType = _npc->actionManager->getCurrentActionType();
        if (currentType != acRun && currentType != acARun)
        {
            return;
        }
    }

    // 提交下一步移动
    commitNextStep(player, canWalkNextStep);
}

// 玩家踏入时检查陷阱、NPC交互（攻击/对话/物体触发）、下一个动作
StepResult NPCActionRun::handlePlayerStepIn(Player* player)
{
    if (!player)
    {
        return StepResult::Continue;
    }

    // 检查当前位置是否有陷阱
    if (gm->map->haveTraps(_npc->position) && !gm->inEvent)
    {
        NPC* npc = _npc;
        int trapIndex = gm->map->getTrapIndex(npc->position);
        player->nextAction = nullptr;
        player->resetRecoveryTime(npc->getTime());
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

    // 尝试处理下一个移动动作
    if (tryHandleMoveNextAction(player, false, true))
    {
        return StepResult::Terminated;
    }

    // 如果下一个动作不是移动类动作，停止跑步
    if (player->nextAction != nullptr)
    {
        auto action = player->nextAction->action;
        bool isMoveAction = (action == acWalk || action == acAWalk || action == acRun || action == acARun);
        if (!isMoveAction)
        {
            _npc->stepList.resize(0);
            _npc->offset = { 0, 0 };
            player->resetRecoveryTime(_npc->getTime());
            _npc->beginStand();
            return StepResult::Terminated;
        }
    }

    return StepResult::Continue;
}

// 跟随NPC踏入时检查是否需要追击或跟随主人
StepResult NPCActionRun::handleFollowerStepIn(Player* player, bool& canWalkNextStep)
{
    // 玩家不处理跟随逻辑
    if (player)
    {
        return StepResult::Continue;
    }

    // 非跟随NPC不处理
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
    // 确定跟随目标：伙伴跟随玩家，其他跟随指定NPC
    std::shared_ptr<NPC> fnpc = nullptr;
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
        // 追击模式：非伙伴NPC在追击状态下尝试执行战斗动作
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
            // 尝试评估并执行战斗动作
            if (canSeeTarget && _npc->evaluateAndPlan(fnpc) && _npc->executeActionPlan(fnpc))
            {
                return StepResult::Terminated;
            }
            // 战斗动作未执行，重新规划追击路径
            _npc->changeFollowAttack(chasePosition);
            if (_npc->stepList.empty())
            {
                canWalkNextStep = false;
            }
        }
        // 跟随模式：根据与主人的距离选择跑步/走路跟随
        else if (_npc->kind != nkPartner || !_npc->isPartnerBlockingPlayer)
        {
            _npc->offset = { 0, 0 };
            _npc->stepList.resize(0);
            auto distance = gm->map->calDistance(_npc->position, fnpc->position);
            int followRadius = _npc->kind == nkPartner ? gm->global.getPartnerFollowRadius() : NPC_FOLLOW_RADIUS;
            int followRunRadius = _npc->kind == nkPartner ? gm->global.getPartnerFollowRunRadius() : NPC_FOLLOW_RADIUS_RUN;
            bool requestedFollowMove = false;
            // 距离较远时跑步跟随
            if ((distance > followRunRadius) && _npc->canDoAction(acRun))
            {
                _npc->changeFollowRun(fnpc->position);
                requestedFollowMove = true;
            }
            // 距离适中时走路跟随
            else if (distance > followRadius)
            {
                _npc->changeFollowWalk(fnpc->position);
                requestedFollowMove = true;
            }
            // 距离足够近时站立
            else
            {
                _npc->beginStand();
                return StepResult::Terminated;
            }
            // 跟随路径为空则无法继续移动
            if (requestedFollowMove && _npc->stepList.empty())
            {
                canWalkNextStep = false;
            }
        }
    }
    else
    {
        // 跟随目标不存在，清除跟随关系
        _npc->followNPC = "";
    }

    return StepResult::Continue;
}

// 战斗NPC踏入时触发AI调度
StepResult NPCActionRun::handleBattleStepIn(Player* player)
{
	if (_npc->isAIEnabled() && player == nullptr &&
		(_npc->kind == nkBattle || (_npc->kind == nkPartner && gm->global.data.PartnerCombat)))
	{
		auto selfPtr = std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr());
		// 调度战斗AI动作
		bool actionHandled = gm->npcManager->scheduleBattleAction(selfPtr);
		if (actionHandled)
		{
			// 如果AI切换了动作类型（非跑步），终止当前跑步
			auto currentType = _npc->actionManager->getCurrentActionType();
			if (currentType != acRun && currentType != acARun)
			{
				return StepResult::Terminated;
			}
			return StepResult::Continue;
		}
	}
	return StepResult::Continue;
}

// 提交下一步移动，检查可通行性和体力消耗
void NPCActionRun::commitNextStep(Player* player, bool canWalkNextStep)
{
    if (_npc->stepList.size() > 0 && _npc->canEnterMoveStep(_npc->stepList[0]))
    {
        // 玩家体力不足时无法跑步
        if (player && !player->ignoresRunThewCost() && player->thew < RUN_THEW_COST && !gm->inEvent)
        {
            player->resetRecoveryTime(_npc->getTime());
            _npc->stepList.resize(0);
            _npc->beginStand();
            gm->showMessage("体力不足!");
            return;
        }
        canWalkNextStep = true;
        _npc->direction = _npc->getDirection(_npc->stepList[0]);
        
        // 偶数方向（上下左右）需要检查两侧邻格的可通行性（防止穿墙角）
        if (_npc->direction % 2 == 0)
        {
            int dir1 = _npc->direction + 1;
            int dir2 = _npc->direction - 1;
            _npc->limitDir(&dir1);
            _npc->limitDir(&dir2);
            
            // 两侧邻格均可通行时才允许斜向移动
            if (_npc->kind == nkFlyingAnimal ||
                (gm->map->canPass(gm->map->getSubPoint(_npc->position, dir1)) &&
                    gm->map->canPass(gm->map->getSubPoint(_npc->position, dir2))))
            {
                _npc->stepState = ssOut;
                // 扣除玩家体力
                if (player && !gm->inEvent && !player->ignoresRunThewCost())
                {
                    player->thew -= RUN_THEW_COST;
                }
                gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
                _npc->direction = _npc->getDirection(_npc->stepList[0]);
                _npc->stepBeginTime += _npc->stepLastTime;
                calStepLastTime(_npc->getAdjustedRunSpeed());
            }
            else
            {
                // 穿墙角检查失败，无法继续移动
                canWalkNextStep = false;
            }
        }
        else
        {
            // 奇数方向（斜向），直接提交步进
            _npc->stepState = ssOut;
            if (player && !gm->inEvent && !player->ignoresRunThewCost())
            {
                player->thew -= RUN_THEW_COST;
            }
            _npc->stepBeginTime += _npc->stepLastTime;
            gm->map->addStepToDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
            _npc->direction = _npc->getDirection(_npc->stepList[0]);
            calStepLastTime(_npc->getAdjustedRunSpeed());
        }
    }
    
    // 无法继续移动时，尝试处理下一个移动动作或转为站立
    if (!canWalkNextStep)
    {
        if (tryHandleMoveNextAction(player, true, true))
        {
            return;
        }
        if (player)
        {
            player->resetRecoveryTime(_npc->getTime());
        }
        _npc->stepList.resize(0);
        _npc->beginStand();
    }
}

// 离开当前格子，更新位置和地图数据
void NPCActionRun::processStepOut()
{
    // 记录离开前的位置
    _previousPosition = _npc->position;
    
    // 从地图数据中删除目标格的占用标记
    gm->map->deleteStepFromDataMap(_npc->stepList[0], std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
    
    // 更新NPC到新位置，切换为踏入状态
    _npc->setPosition(_npc->stepList[0], false);
    _npc->stepState = ssIn;
    _npc->stepBeginTime += _npc->stepLastTime;

    // 在旧位置添加占用标记（踏入期间仍占用旧位置）
    gm->map->addStepToDataMap(_previousPosition, std::dynamic_pointer_cast<NPC>(_npc->getMySharedPtr()));
}

// 获取当前占用的格子位置（用于地图数据管理）
std::vector<Point> NPCActionRun::getStepPositions() const
{
    std::vector<Point> result;
    
    if (_npc->stepState == ssOut)
    {
        // 踏出状态：占用目标格
        if (!_npc->stepList.empty())
        {
            result.push_back(_npc->stepList[0]);
        }
    }
    else
    {
        // 踏入状态：占用上一步位置
        if (_previousPosition.x >= 0 && _previousPosition.y >= 0)
        {
            result.push_back(_previousPosition);
        }
    }
    
    return result;
}

// 计算当前步进的像素偏移量实现平滑移动
void NPCActionRun::updateOffset()
{
    _npc->calOffset(_npc->getTime() - _npc->stepBeginTime, _npc->stepLastTime);
}
