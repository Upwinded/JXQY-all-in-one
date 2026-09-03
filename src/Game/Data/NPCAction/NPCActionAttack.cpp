#include "NPCActionAttack.h"
#include "../../../Engine/Engine.h"
#include "../Magic.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionAttack::NPCActionAttack(NPC* npc)
    : NPCActionBase(npc)
{
}

// 进入攻击状态，随机选择攻击类型（普通攻击/攻击1/攻击2/特殊攻击），播放攻击音效
void NPCActionAttack::enter()
{
    _npc->clearStep();
    _npc->stepList.resize(0);
    _npc->offset = { 0, 0 };
    _npc->actionBeginTime = _npc->getTime();
    _npc->attackDone = false;
    _npc->fightState.set(true);
    _attackDest = _npc->attackDest;
    _target = _npc->destGE.lock();
    
    Player* player = dynamic_cast<Player*>(_npc);
    _actionMagic = nullptr;
    if (player)
    {
        _attackDest = player->magicDest;
        int practiceIndex = gm->magicManager.practiceIndex();
        if (gm->magicManager.magicListExists(practiceIndex))
        {
            _actionMagic = player->resolveMagicReplacement(gm->magicManager.magicList[practiceIndex].magic);
        }
    }
    
    std::vector<NPCActionType> attackList;
    attackList.push_back(NPCActionType::acAttack);
    
    if (_npc->canDoAction(acAttack1))
    {
        attackList.push_back(NPCActionType::acAttack1);
    }
    
    if (_npc->canDoAction(acAttack2))
    {
        attackList.push_back(NPCActionType::acAttack2);
    }
    
    NPCActionType attackNum = NPCActionType::acAttack;
    if (attackList.size() > 0)
    {
        int idx = attackList.size() > 1 ? _npc->engine->getRand((int)attackList.size() - 1) : 0;
        attackNum = attackList[idx];
    }
    
    if (attackNum == NPCActionType::acAttack)
    {
        _attackType = NPCActionType::acAttack;
        _npc->nowAction = NPCActionType::acAttack;
        _npc->actionLastTime = getActionTime();
    }
    else if (attackNum == NPCActionType::acAttack1)
    {
        _attackType = NPCActionType::acAttack1;
        _npc->nowAction = NPCActionType::acAttack1;
        _npc->actionLastTime = getActionTime();
    }
    else
    {
        auto attackActionImage = _actionMagic != nullptr && _actionMagic->useActionImage != nullptr
            ? _actionMagic->useActionImage
            : (_actionMagic != nullptr ? _actionMagic->getActionImageForNPC(_npc) : nullptr);
        if (player && attackActionImage != nullptr)
        {
            _attackType = NPCActionType::acSpecialAttack;
            _npc->nowAction = NPCActionType::acSpecialAttack;
            _npc->actionLastTime = getActionTime();
        }
        else
        {
            _attackType = NPCActionType::acAttack2;
            _npc->nowAction = NPCActionType::acAttack2;
            _npc->actionLastTime = getActionTime();
        }
    }

    if (player && (_attackType == NPCActionType::acAttack2 || _attackType == NPCActionType::acSpecialAttack))
    {
        player->prepareSpecialAttackMagicForAction(_attackDest, _target.lock());
    }
    else
    {
        _actionMagic = _npc->prepareAttackMagicForAction(_attackDest, _target.lock(), _npc->attackReleaseMode);
    }
    _npc->actionLastTime = getActionTime();
    
    playSound();
}

// 更新攻击状态，当攻击动画播放到释放点时触发伤害计算，然后回到站立
void NPCActionAttack::update(UTime frameTime)
{
	// Do not release an attack after the owner has already crossed the partner
	// leash. Exiting the action clears prepared attack magic before following.
	if (_npc->abandonPartnerCombatForPlayerFollow())
	{
		_npc->actionManager->forceChangeAction(NPCActionType::acStand);
		return;
	}

    if (_npc->getTime() - _npc->actionBeginTime >= _npc->actionLastTime)
    {
        bool released = false;
        if (!_attackDone)
        {
            _attackDone = true;
            Player* player = dynamic_cast<Player*>(_npc);
            auto targetPtr = _target.lock();
            if (player && (_attackType == NPCActionType::acAttack2 || _attackType == NPCActionType::acSpecialAttack))
            {
                released = player->releasePreparedSpecialAttackMagic(_attackDest, targetPtr);
                if (!released)
                {
                    released = player->doSpecialAttack(_attackDest, targetPtr);
                }
            }
            else
            {
                released = _npc->releasePreparedAttackMagic(_attackDest, targetPtr);
                if (!released)
                {
                    released = _npc->doAttack(_attackDest, targetPtr, _npc->attackReleaseMode);
                }
            }
        }
        if (released)
        {
            _npc->fightState.set(true);
            _npc->lastBattleScanTime = _npc->getTime();
        }
        _npc->revealMagicInvisibilityOnAction();
        _npc->actionManager->forceChangeAction(NPCActionType::acStand);
    }
}

// 退出攻击状态，重置攻击完成标记
void NPCActionAttack::exit()
{
    _npc->attackDone = false;
    _npc->clearPreparedAttackMagic();
}

// 攻击状态只能转换到站立、受伤、死亡、隐藏
bool NPCActionAttack::canTransitionTo(NPCActionType type) const
{
    return type == NPCActionType::acHurt || type == NPCActionType::acDeath || type == NPCActionType::acHide;
}

// 根据攻击类型获取对应的图像资源
_shared_imp NPCActionAttack::getAttackImagePackage() const
{
    if (_actionMagic != nullptr && _actionMagic->useActionImage != nullptr)
    {
        return _actionMagic->useActionImage;
    }
    if (_attackType == NPCActionType::acSpecialAttack && _actionMagic != nullptr)
    {
        auto actionImage = _actionMagic->getActionImageForNPC(_npc);
        if (actionImage != nullptr)
        {
            return actionImage;
        }
    }

    switch (_attackType)
    {
    case NPCActionType::acAttack:
        return _npc->res.attack.imagePackage;
    case NPCActionType::acAttack1:
        return _npc->res.attack1.imagePackage;
    case NPCActionType::acAttack2:
        return _npc->res.attack2.imagePackage;
    case NPCActionType::acSpecialAttack:
        return _npc->res.specialAttack.imagePackage;
    default:
        return _npc->res.attack.imagePackage;
    }
}

// 根据攻击类型获取对应的阴影资源
_shared_imp NPCActionAttack::getAttackShadowPackage() const
{
    switch (_attackType)
    {
    case NPCActionType::acAttack:
        return _npc->res.attack.shadowPackage;
    case NPCActionType::acAttack1:
        return _npc->res.attack1.shadowPackage;
    case NPCActionType::acAttack2:
        return _npc->res.attack2.shadowPackage;
    case NPCActionType::acSpecialAttack:
        return selectSpecialAttackShadowPackage(
            _actionMagic != nullptr ? _actionMagic->actionShadow : nullptr,
            _actionMagic != nullptr ? _actionMagic->useActionImage : nullptr,
            _actionMagic != nullptr ? _actionMagic->actionImage : nullptr,
            _npc->res.specialAttack.shadowPackage);
    default:
        return _npc->res.attack.shadowPackage;
    }
}

// 获取当前帧的攻击动画图像
_shared_image NPCActionAttack::getActionImage(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(getAttackImagePackage(), _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

// 获取当前帧的攻击动画阴影
_shared_image NPCActionAttack::getActionShadow(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(getAttackShadowPackage(), _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

// 获取当前攻击类型的动画总时长
UTime NPCActionAttack::getActionTime() const
{
	auto scaleAttackTime = [this](UTime actionTime) -> UTime
	{
		if (_npc->attackSpeed <= 1 || actionTime == 0)
		{
			return actionTime;
		}
		UTime scaledTime = actionTime / static_cast<UTime>(_npc->attackSpeed);
		return scaledTime > 0 ? scaledTime : 1;
	};

	UTime actionTime = IMP::getIMPImageActionTime(getAttackImagePackage());
	return scaleAttackTime(actionTime);
}

// 播放攻击音效
void NPCActionAttack::playSound()
{
    _npc->playSound(_attackType);
}
