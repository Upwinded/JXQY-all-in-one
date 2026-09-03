#include "NPCActionMagic.h"
#include "../Magic.h"
#include "../NPC.h"
#include "../Player.h"
#include "../../GameManager/GameManager.h"
#include "../../../Image/IMP.h"

NPCActionMagic::NPCActionMagic(NPC* npc)
    : NPCActionBase(npc)
{
}

void NPCActionMagic::enter()
{
    _npc->clearStep();
    _npc->stepList.resize(0);
    _npc->offset = { 0, 0 };
    _npc->actionBeginTime = _npc->getTime();
    _npc->attackDone = false;
    _magicDone = false;
    _target = _npc->destGE.lock();
    _magicToUse = _npc->preparedMagicAction;
    _magicDest = _npc->preparedMagicActionDest;
    _magicLevel = _npc->preparedMagicActionLevel;
    _magicListIndex = _npc->preparedMagicActionListIndex;
    if (auto preparedTarget = _npc->preparedMagicActionTarget.lock())
    {
        _target = preparedTarget;
    }

    Player* player = dynamic_cast<Player*>(_npc);
    if (_magicToUse == nullptr && player && player->magicIndex >= 0 && player->magicIndex < gm->magicManager.bottomCount())
    {
        int listIndex = gm->magicManager.bottomIndex(player->magicIndex);
        if (gm->magicManager.magicListExists(listIndex))
        {
            auto& magicInfo = gm->magicManager.magicList[listIndex];
            _magicToUse = player->resolveMagicReplacement(magicInfo.magic);
            _magicDest = player->magicDest;
            _magicLevel = magicInfo.level;
            _magicListIndex = listIndex;
        }
    }
    _npc->nowAction = acMagic;
    _npc->actionLastTime = getActionTime();
    _npc->fightState.set(true);

    playSound();
}

void NPCActionMagic::update(UTime frameTime)
{
    Player* player = dynamic_cast<Player*>(_npc);
    UTime elapsedTime = _npc->getTime() - _npc->actionBeginTime;
    UTime magicTriggerTime = resolveMagicTriggerTime(
        _npc->actionLastTime,
        player != nullptr,
        gm->global.feature.magicTriggerAtAnimationEnd);

    if (!_magicDone && elapsedTime >= magicTriggerTime)
    {
        _magicDone = true;
        _npc->attackDone = true;
        if (_magicToUse != nullptr)
        {
            bool canUse = true;
            int level = _magicLevel;
            if (level < 1 || level > MAGIC_MAX_LEVEL)
            {
                canUse = false;
            }

            if (canUse && player != nullptr)
            {
                canUse = player->tryConsumeMagicCost(_magicToUse, level, true);
            }

            if (canUse)
            {
                if (player != nullptr)
                {
                    gm->magicManager.recordCurrentUseMagic(_magicListIndex);
                }
                _npc->useMagic(_magicToUse, _magicDest, level, _target.lock());
                if (player != nullptr && _magicListIndex >= 0 && _magicListIndex < gm->magicManager.listLength())
                {
                    gm->magicManager.magicList[_magicListIndex].remainColdMilliseconds = _magicToUse->coldMilliSeconds;
                }
            }
        }
    }

    if (elapsedTime >= _npc->actionLastTime)
    {
        _npc->fightState.set(true);
        _npc->lastBattleScanTime = _npc->getTime();
        _npc->revealMagicInvisibilityOnAction();
        _npc->actionManager->forceChangeAction(NPCActionType::acStand);
    }
}

void NPCActionMagic::exit()
{
    _npc->attackDone = false;
    _npc->clearPreparedMagicAction();
}

bool NPCActionMagic::canTransitionTo(NPCActionType type) const
{
    if (type == acDeath || type == acHide)
    {
        return true;
    }
    if (type == acHurt)
    {
        return _magicToUse == nullptr || _magicToUse->noInterruption <= 0;
    }
    return false;
}

_shared_imp NPCActionMagic::getMagicActionImagePackage() const
{
    if (_magicToUse != nullptr)
    {
        if (_magicToUse->useActionImage != nullptr)
        {
            return _magicToUse->useActionImage;
        }
        auto actionImage = _magicToUse->getActionImageForNPC(_npc);
        if (actionImage != nullptr)
        {
            return actionImage;
        }
    }
    return _npc->res.magic.imagePackage;
}

_shared_imp NPCActionMagic::getMagicActionShadowPackage() const
{
    return selectMagicActionShadowPackage(
        _magicToUse != nullptr ? _magicToUse->actionShadow : nullptr,
        _magicToUse != nullptr ? _magicToUse->useActionImage : nullptr,
        _magicToUse != nullptr ? _magicToUse->actionImage : nullptr,
        _npc->res.magic.shadowPackage);
}

_shared_image NPCActionMagic::getActionImage(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(getMagicActionImagePackage(), _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

_shared_image NPCActionMagic::getActionShadow(int* offsetx, int* offsety)
{
    return IMP::loadImageForDirection(getMagicActionShadowPackage(), _npc->direction,
        _npc->getTime() - _npc->actionBeginTime, offsetx, offsety, true);
}

UTime NPCActionMagic::getActionTime() const
{
    return IMP::getIMPImageActionTime(getMagicActionImagePackage());
}

void NPCActionMagic::playSound()
{
    _npc->playSound(acMagic);
}
