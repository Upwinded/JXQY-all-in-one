#include "../Game/Data/EffectManager.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/Data/Player.h"
#include "../Game/GameManager/GameManager.h"
#include "../Image/IMP.h"
#include "../Resource/ResourceCatalog.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAILED: real release timing: " << message << '\n';
    }
    return condition;
}

bool sameRatio(const ActionReleaseFrameRatio& actual, int numerator, int denominator)
{
    return actual.numerator == numerator && actual.denominator == denominator;
}

struct ExpectedProfile
{
    const char* id;
    const char* source;
    bool swordTwo;
};

bool checkInstalledProfiles(const std::filesystem::path& assetsRoot,
    std::map<std::string, ResourceManifest>& resolved)
{
    const ExpectedProfile expected[] = {
        {"JXQY2", "JXQY2", true},
        {"YYCS", "YYCS", false},
        {"XJXQY", "XJXQY", false},
        {"XJXQY_TEST_MOD", "XJXQY", false},
        {"XJXQY_TEST_FALLBACK", "XJXQY", false},
        {"JIAN_ER_GAI_CHENGHE_1_041", "JXQY2", true},
        {"XINYUE_WUHEN_3_0", "JXQY2", true},
        {"XIAOXIANGXING_1_022", "JXQY2", true},
        {"JIANGHU_YUCHEN_1_03", "YYCS", false},
        {"JIANGHU_YUCHEN_2", "YYCS", false},
        {"YUEMEIER_WAIZHUAN_1_053", "YYCS", false},
        {"YUEMEIER_WAIZHUAN_MG", "YYCS", false},
        {"JIAN_ZHI_YUAN_1_HD", "XJXQY", false},
        {"JIAN_ZHI_YUAN_2_HD", "XJXQY", false},
        {"JIAN_ZHI_YUAN_3_HD", "XJXQY", false},
        {"JIAN_ZHI_YUAN_4_HD", "XJXQY", false},
        {"JIAN_ZHI_YUAN_5_HD", "XJXQY", false},
        {"JIAN_ZHI_YUAN_6_HD", "XJXQY", false},
        {"JIAN_ZHI_YUAN_SHARED", "", false},
        {"JIAN_ZHI_YUAN_5_6_SHARED", "", false}
    };
    const auto catalog = RuntimeResource::loadResourceCatalogSnapshot(assetsRoot);
    if (!check(catalog.succeeded(), "read the installed resource catalog")) return false;
    bool ok = true;
    for (const auto& fixture : expected)
    {
        const auto entry = std::find_if(catalog.snapshot.entries.begin(), catalog.snapshot.entries.end(),
            [&](const auto& candidate) { return candidate.manifest.id == fixture.id; });
        if (!check(entry != catalog.snapshot.entries.end(), std::string(fixture.id) + " is installed"))
        {
            ok = false;
            continue;
        }
        const bool explicitBase = std::string(fixture.id) == fixture.source;
        const auto& raw = entry->manifest;
        ok = check(raw.attackReleaseFrameRatioDefined == explicitBase
            && raw.specialAttackReleaseFrameRatioDefined == explicitBase
            && raw.magicReleaseFrameRatioDefined == explicitBase,
            std::string(fixture.id) + " preserves explicit base values or absent MOD overrides") && ok;
        ok = check(raw.features.find("magictriggeratanimationend") == raw.features.end(),
            std::string(fixture.id) + " has no retired release feature") && ok;
        const auto selection = RuntimeResource::resolveExactResourceSelection(assetsRoot, fixture.id);
        if (!check(selection.succeeded(), std::string(fixture.id) + " resolves content dependencies"))
        {
            ok = false;
            continue;
        }
        const auto& manifest = selection.selection.activeManifest;
        ok = check(sameRatio(manifest.attackReleaseFrameRatio, 2, 3)
            && sameRatio(manifest.specialAttackReleaseFrameRatio, 1, fixture.swordTwo ? 1 : 3)
            && sameRatio(manifest.magicReleaseFrameRatio, fixture.swordTwo ? 2 : 4, fixture.swordTwo ? 3 : 5)
            && manifest.attackReleaseFrameRatioSourceId == fixture.source
            && manifest.specialAttackReleaseFrameRatioSourceId == fixture.source
            && manifest.magicReleaseFrameRatioSourceId == fixture.source,
            std::string(fixture.id) + " resolves each ratio from the expected content source, excluding UI") && ok;
        resolved.emplace(fixture.id, manifest);
        std::cout << "Release profile checked: " << fixture.id << " source="
            << (fixture.source[0] != '\0' ? fixture.source : "default") << '\n';
    }
    return ok;
}

struct AnimationFixture
{
    const char* path;
    int frameCount;
    int interval;
    UTime duration;
    UTime release;
};

_shared_imp loadAnimation(const std::filesystem::path& assetsRoot, const AnimationFixture& fixture)
{
    const auto path = assetsRoot / std::filesystem::u8path(fixture.path);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!check(input && length > 0 && length <= INT_MAX, path.u8string() + " is readable")) return nullptr;
    const int size = static_cast<int>(length);
    auto bytes = std::make_unique<char[]>(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(bytes.get(), size);
    auto image = input ? IMP::createIMPImageFromMem(bytes, size, false) : nullptr;
    if (!check(image != nullptr,
        path.u8string() + " loads through the runtime IMP decoder")) return nullptr;
    if (!check(image->frame.size() == static_cast<std::size_t>(fixture.frameCount)
        && image->directions == 8 && image->interval == fixture.interval
        && IMP::getIMPImageActionTime(image) == fixture.duration,
        path.u8string() + " retains the verified frame count, directions and interval")) return nullptr;
    return image;
}

std::shared_ptr<Magic> makeReleaseProbe(const std::string& name)
{
    // Real animations drive real actions; one projectile makes duplicate releases observable.
    auto magic = std::make_shared<Magic>();
    magic->iniName = name;
    magic->loadSucceeded = true;
    magic->level[1].moveKind = mmkPoint;
    magic->level[1].lifeFrame = 100;
    magic->level[1].manaCost = 7;
    return magic;
}

enum class ActionCase
{
    Attack,
    Magic,
    SpecialAttack,
    UnlinkedAttack2
};

enum class InterruptionMoment
{
    None,
    BeforeRelease,
    AfterRelease
};

bool checkAction(const ResourceManifest& manifest, const _shared_imp& image,
    const AnimationFixture& fixture, bool playerActor, ActionCase actionCase,
    UTime releaseTime, const _shared_imp& practiceImage = nullptr,
    InterruptionMoment interruption = InterruptionMoment::None, NPCActionType interruptTo = acStand)
{
    GameManager game;
    game.global.applyResourceManifestFeatures(manifest);
    game.global.data.NPCAI = false;
    game.map->data = std::make_shared<MapData>();
    game.map->data->head.width = game.map->data->head.height = 16;
    game.map->data->tile.assign(16, std::vector<MapTile>(16));
    game.map->createDataMap();
    std::shared_ptr<NPC> actor = playerActor ? std::static_pointer_cast<NPC>(game.player)
        : std::make_shared<NPC>();
    actor->kind = playerActor ? nkPlayer : nkBattle;
    actor->relation = playerActor ? nrFriendly : nrHostile;
    actor->setPosition({3, 4}, false);
    actor->attackSpeed = 1;
    actor->attackLevel = 1;
    actor->life = actor->lifeMax = actor->mana = actor->manaMax = actor->thew = actor->thewMax = 1000;
    actor->res.attack.imagePackage = image;
    actor->res.magic.imagePackage = image;
    actor->res.stand.imagePackage = image;
    actor->npcMagic = makeReleaseProbe("release-timing-primary.ini");
    game.player->canFight = game.player->canUseMana = true;
    game.player->info.lifeMax = game.player->info.manaMax = game.player->info.thewMax = 1000;
    if (!playerActor) game.npcManager->npcList.push_back(actor);

    const bool practiceBranch = actionCase == ActionCase::SpecialAttack || actionCase == ActionCase::UnlinkedAttack2;
    if (practiceBranch)
    {
        actor->res.attack2.imagePackage = image;
        auto practice = makeReleaseProbe("release-timing-practice.ini");
        practice->actionImage = practiceImage;
        if (actionCase == ActionCase::SpecialAttack)
        {
            practice->linkedLevel[1].specialMagic = makeReleaseProbe("release-timing-linked.ini");
        }
        const int index = game.magicManager.practiceIndex();
        game.magicManager.magicList.resize(static_cast<std::size_t>(game.magicManager.listLength()));
        auto& info = game.magicManager.magicList[index];
        info.iniFile = practice->iniName;
        info.magic = practice;
        info.level = 1;
    }

    const auto desiredAction = actionCase == ActionCase::Magic ? acMagic
        : actionCase == ActionCase::SpecialAttack ? acSpecialAttack
        : actionCase == ActionCase::UnlinkedAttack2 ? acAttack2 : acAttack;
    for (int attempt = 0; attempt < (practiceBranch ? 64 : 1); ++attempt)
    {
        actor->actionManager->restartActionIgnoringTransitions(acStand);
        actor->setTime(1000);
        actor->thew = 1000;
        if (actionCase == ActionCase::Magic)
        {
            if (playerActor)
            {
                MagicInfo spell;
                spell.iniFile = actor->npcMagic->iniName;
                spell.magic = actor->npcMagic;
                spell.level = 1;
                game.player->beginMagic(spell, {4, 4});
            }
            else
            {
                actor->setPreparedMagicAction(actor->npcMagic, {4, 4}, 1, nullptr);
                actor->beginMagic({4, 4});
            }
        }
        else
        {
            actor->beginAttack({4, 4});
        }
        if (actor->nowAction == desiredAction) break;
    }
    const std::string label = manifest.id + (playerActor ? " Player " : " NPC ")
        + fixture.path + " action=" + std::to_string(static_cast<int>(desiredAction));
    if (!check(actor->nowAction == desiredAction && actor->actionLastTime == fixture.duration,
        label + " admits the actual animation and selected branch")) return false;
    bool ok = check(game.effectManager->effectList.empty(), label + " begins without a projectile");
    const int originalMana = actor->mana;
    // A running action keeps its admitted clocks when configuration or speed changes.
    game.global.attackReleaseFrameRatio = {1, 1};
    game.global.specialAttackReleaseFrameRatio = {1, 1};
    game.global.magicReleaseFrameRatio = {1, 1};
    actor->attackSpeed = 3;
    const auto updateAt = [&](UTime elapsed)
    {
        actor->setTime(1000 + elapsed);
        actor->actionManager->update(0);
    };
    const auto interruptAndCheck = [&]()
    {
        const auto releasedEffects = game.effectManager->effectList;
        const int remainingMana = actor->mana;
        actor->actionManager->forceChangeAction(interruptTo);
        bool passed = check(actor->nowAction == interruptTo,
            label + " accepts the requested interruption");
        updateAt(fixture.duration + 1);
        updateAt(fixture.duration * 2 + 1);
        passed = check(actor->nowAction != desiredAction
            && game.effectManager->effectList == releasedEffects && actor->mana == remainingMana,
            label + " cancellation preserves released effects without a late release or repeated cost") && passed;
        std::cout << "Release interruption checked: " << label << " target=" << static_cast<int>(interruptTo)
            << " after-release=" << (interruption == InterruptionMoment::AfterRelease) << '\n';
        return passed;
    };
    updateAt(releaseTime - 1);
    ok = check(actor->nowAction == desiredAction && game.effectManager->effectList.empty()
        && actor->mana == originalMana, label + " does not release or charge before the frame gate") && ok;
    if (interruption == InterruptionMoment::BeforeRelease) return interruptAndCheck() && ok;
    updateAt(releaseTime);
    const std::size_t expectedEffects = actionCase == ActionCase::SpecialAttack ? 2 : 1;
    const int expectedMana = originalMana - (playerActor && actionCase == ActionCase::Magic ? 7 : 0);
    ok = check(game.effectManager->effectList.size() == expectedEffects && actor->mana == expectedMana,
        label + " releases each primary or linked projectile once and charges once") && ok;
    const auto countMagic = [&](const std::string& name)
    {
        return std::count_if(game.effectManager->effectList.begin(), game.effectManager->effectList.end(),
            [&](const auto& effect) { return effect && effect->magic.iniName == name && effect->user.lock() == actor; });
    };
    ok = check(countMagic("release-timing-primary.ini") == 1
        && countMagic("release-timing-linked.ini") == (actionCase == ActionCase::SpecialAttack ? 1 : 0),
        label + " retains the primary and linked magic identities and their caster") && ok;
    ok = check(releaseTime < fixture.duration ? actor->nowAction == desiredAction : actor->isStanding(),
        label + " preserves recovery frames, or finishes an explicit 1/1 action") && ok;
    if (interruption == InterruptionMoment::AfterRelease) return interruptAndCheck() && ok;
    updateAt(releaseTime);
    if (releaseTime < fixture.duration) updateAt(fixture.duration - 1);
    ok = check(game.effectManager->effectList.size() == expectedEffects && actor->mana == expectedMana
        && (releaseTime == fixture.duration || actor->nowAction == desiredAction),
        label + " keeps recovery active without duplicate release or cost") && ok;
    updateAt(fixture.duration);
    ok = check(actor->isStanding() && game.effectManager->effectList.size() == expectedEffects,
        label + " returns to stand only at full animation duration") && ok;
    updateAt(fixture.duration + 1);
    ok = check(game.effectManager->effectList.size() == expectedEffects && actor->mana == expectedMana,
        label + " remains single-release after completion") && ok;
    std::cout << "Release animation checked: " << label << " release-ms=" << releaseTime
        << " duration-ms=" << fixture.duration << '\n';
    return ok;
}
}

bool runReleaseTimingRealResourceTests()
{
    const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
    if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini"))
    {
        std::cout << "SKIP: optional trilogy release-timing resources are absent\n";
        return true;
    }
    std::map<std::string, ResourceManifest> manifests;
    if (!checkInstalledProfiles(assetsRoot, manifests)) return false;

    struct TrilogyFixture
    {
        const char* id;
        AnimationFixture attack;
        AnimationFixture magic;
    };
    const TrilogyFixture trilogy[] = {
        {"JXQY2", {u8"jxqy2/mpc/character/z01-南宫飞云-at1.mpc", 136, 20, 340, 200},
            {u8"jxqy2/mpc/character/z01-南宫飞云-sat.mpc", 128, 30, 480, 300}},
        {"YYCS", {u8"yycs/asf/character/杨影枫攻击1a.asf", 104, 35, 455, 280},
            {u8"yycs/asf/character/杨影枫攻击发功a.asf", 160, 90, 1800, 1440}},
        {"XJXQY", {"xjxqy/asf/character/npc001_at1.asf", 224, 30, 840, 540},
            {"xjxqy/asf/character/npc001_mag.asf", 128, 70, 1120, 840}}
    };
    bool ok = true;
    for (const auto& fixture : trilogy)
    {
        auto attack = loadAnimation(assetsRoot, fixture.attack);
        auto magic = loadAnimation(assetsRoot, fixture.magic);
        if (attack == nullptr || magic == nullptr) return false;
        for (bool playerActor : {false, true})
        {
            ok = checkAction(manifests.at(fixture.id), attack, fixture.attack, playerActor,
                ActionCase::Attack, fixture.attack.release) && ok;
            ok = checkAction(manifests.at(fixture.id), magic, fixture.magic, playerActor,
                ActionCase::Magic, fixture.magic.release) && ok;
        }
    }

    const AnimationFixture bat{"yycs/asf/character/npc050_at.asf", 152, 50, 950, 600};
    auto batImage = loadAnimation(assetsRoot, bat);
    if (batImage == nullptr) return false;
    ok = checkAction(manifests.at("YYCS"), batImage, bat, false, ActionCase::Attack, bat.release) && ok;
    // These resource-only packs have no configured content source, even though their UI resolves to YYCS.
    ok = checkAction(manifests.at("JIAN_ZHI_YUAN_SHARED"), batImage, bat, false,
        ActionCase::Attack, bat.release) && ok;
    for (const auto moment : {InterruptionMoment::BeforeRelease, InterruptionMoment::AfterRelease})
    {
        for (const auto nextAction : {acHurt, acDeath, acHide, acStand})
        {
            ok = checkAction(manifests.at("YYCS"), batImage, bat, false,
                ActionCase::Attack, bat.release, nullptr, moment, nextAction) && ok;
            // The same actual 19-frame asset releases acMagic at frame 12 under 4/5.
            ok = checkAction(manifests.at("YYCS"), batImage, bat, true,
                ActionCase::Magic, 600, nullptr, moment, nextAction) && ok;
        }
    }

    const AnimationFixture special{u8"yycs/asf/character/烈火情天攻击1.asf", 88, 90, 990, 270};
    const AnimationFixture attack2{u8"yycs/asf/character/杨影枫攻击3a.asf", 96, 45, 540, 360};
    auto specialImage = loadAnimation(assetsRoot, special);
    auto attack2Image = loadAnimation(assetsRoot, attack2);
    if (specialImage == nullptr || attack2Image == nullptr) return false;
    ok = checkAction(manifests.at("YYCS"), specialImage, special, true,
        ActionCase::SpecialAttack, special.release, specialImage) && ok;
    ok = checkAction(manifests.at("YYCS"), attack2Image, attack2, true,
        ActionCase::UnlinkedAttack2, attack2.release, specialImage) && ok;

    ResourceManifest custom;
    const std::string definition = "[Game]\nId=RELEASE_AT_END\n[Combat]\n"
        "AttackReleaseFrameRatio=1/1\nSpecialAttackReleaseFrameRatio=1/1\nMagicReleaseFrameRatio=1/1\n";
    if (!check(custom.loadFromBuffer(definition.data(), static_cast<int>(definition.size())),
        "read an explicit 1/1 resource profile")) return false;
    for (bool playerActor : {false, true})
    {
        ok = checkAction(custom, batImage, bat, playerActor, ActionCase::Attack, bat.duration) && ok;
        ok = checkAction(custom, batImage, bat, playerActor, ActionCase::Magic, bat.duration) && ok;
    }
    ok = checkAction(custom, specialImage, special, true,
        ActionCase::SpecialAttack, special.duration, specialImage) && ok;
    return ok;
}
