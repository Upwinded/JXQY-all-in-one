#include "../Game/Data/EffectManager.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/Data/NPCAction/ActionReleaseTiming.h"
#include "../Game/GameManager/GameManager.h"

#include <iostream>
#include <memory>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

_shared_imp makeAnimation(int offsetBase)
{
    auto image = std::make_shared<IMPImage>();
    image->directions = 8;
    image->interval = 101;
    image->frame.resize(64);
    for (int index = 0; index < 64; ++index)
    {
        // IMP::loadImage exposes the selected frame's offsets without a GPU.
        image->frame[index].xOffset = offsetBase + index;
        image->frame[index].yOffset = -offsetBase - index;
    }
    return image;
}

struct FrameSample
{
    UTime elapsed;
    int frame;
};

struct AttackCase
{
    int speed;
    UTime duration;
    std::vector<FrameSample> samples;
};
}

bool runAttackAnimationRuntimeTests()
{
    GameManager gameManager;
    gameManager.global.data.NPCAI = false;
    gameManager.global.feature.nativeNpcAttackAtAnimationEnd = false;
    gameManager.map->data = std::make_shared<MapData>();
    gameManager.map->data->head.width = gameManager.map->data->head.height = 16;
    gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
    gameManager.map->createDataMap();

    const std::vector<AttackCase> cases = {
        {1, 808, {{0, 0}, {100, 0}, {101, 1}, {403, 3}, {404, 4}, {707, 7}, {807, 7}}},
        {2, 404, {{0, 0}, {50, 0}, {51, 1}, {201, 3}, {202, 4}, {354, 7}, {403, 7}}},
        {3, 269, {{0, 0}, {33, 0}, {34, 1}, {134, 3}, {135, 4}, {236, 7}, {268, 7}}},
        {10, 80, {{0, 0}, {9, 0}, {10, 1}, {39, 3}, {40, 4}, {70, 7}, {79, 7}}},
        {1000, 1, {{0, 0}}},
        {10, 0, {}}
    };
    bool ok = true;
    for (const ActionReleaseFrameRatio ratio : {ActionReleaseFrameRatio{2, 3}, ActionReleaseFrameRatio{1, 1}})
    for (const auto& test : cases)
    {
        gameManager.global.attackReleaseFrameRatio = ratio;
        gameManager.effectManager->freeResource();
        auto actor = std::make_shared<NPC>();
        actor->kind = nkBattle;
        actor->relation = nrHostile;
        actor->setPosition({3, 4}, false);
        actor->attackSpeed = test.speed;
        actor->res.attack.imagePackage = makeAnimation(1000);
        actor->res.attack.shadowPackage = makeAnimation(2000);
        actor->npcMagic = std::make_shared<Magic>();
        actor->npcMagic->loadSucceeded = true;
        actor->npcMagic->level[1].moveKind = mmkPoint;
        actor->npcMagic->level[1].lifeFrame = 100;
        if (test.duration == 0)
        {
            actor->npcMagic->useActionImage = std::make_shared<IMPImage>();
            actor->npcMagic->useActionImage->directions = 8;
        }
        actor->setTime(1000);
        actor->beginAttack({4, 4});
        actor->direction = 5;
        ok = check(actor->isAttacking() && actor->actionLastTime == test.duration,
            "attack admission fixes the full duration, including rounding and the one millisecond floor") && ok;
        actor->attackSpeed = test.speed == 1 ? 10 : 1;
        gameManager.global.attackReleaseFrameRatio = {1, 3};
        const UTime releaseTime = calculateActionReleaseTime(test.duration, test.duration == 0 ? 0 : 8, ratio);

        for (const auto& sample : test.samples)
        {
            actor->setTime(1000 + sample.elapsed);
            actor->actionManager->update(0);
            int imageX = -1;
            int imageY = -1;
            int shadowX = -1;
            int shadowY = -1;
            actor->getActionImage(&imageX, &imageY);
            actor->getActionShadow(&shadowX, &shadowY);
            const int frameIndex = 5 * 8 + sample.frame;
            ok = check(imageX == 1000 + frameIndex && imageY == -1000 - frameIndex
                && shadowX == 2000 + frameIndex && shadowY == -2000 - frameIndex,
                "real attack image and shadow selection follow the admitted duration despite later speed changes") && ok;
            const std::size_t expectedEffects = sample.elapsed >= releaseTime ? 1 : 0;
            ok = check(actor->isAttacking() && gameManager.effectManager->effectList.size() == expectedEffects,
                "release uses the admitted ratio once and preserves the rest of the attack animation") && ok;
        }

        actor->setTime(1000 + test.duration);
        int imageX = -1;
        int shadowX = -1;
        actor->getActionImage(&imageX, nullptr);
        actor->getActionShadow(&shadowX, nullptr);
        if (test.duration == 0)
        {
            ok = check(imageX == -1 && shadowX == 2040,
                "zero-duration action images keep shadow sampling defined without dividing by zero") && ok;
        }
        else
        {
            ok = check(imageX == 1047 && shadowX == 2047,
                "both visual clocks reach their final frame at the full action deadline") && ok;
        }
        actor->actionManager->update(0);
        ok = check(actor->isStanding() && gameManager.effectManager->effectList.size() == 1,
            "each attack finishes with exactly one effect, including zero-duration actions") && ok;
        actor->setTime(actor->getTime() + 1);
        actor->actionManager->update(1);
        ok = check(gameManager.effectManager->effectList.size() == 1,
            "the completed attack does not release again") && ok;
    }
    return ok;
}
