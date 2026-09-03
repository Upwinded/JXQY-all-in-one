#include "../Game/Data/NPCAction/NPCActionMagic.h"
#include "../Game/Data/Magic.h"

#include <iostream>

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
}

int main()
{
    bool ok = true;

    ok = check(NPCActionMagic::resolveMagicTriggerTime(960, false, false) == 960,
        "NPC magic always waits for the full action time") && ok;
    ok = check(NPCActionMagic::resolveMagicTriggerTime(960, true, true) == 960,
        "player magic waits for the full action time when animation-end triggering is enabled") && ok;
    ok = check(NPCActionMagic::resolveMagicTriggerTime(960, true, false) == PLAYER_MAGIC_DELAY,
        "legacy player magic can trigger at the configured delay before a long action ends") && ok;
    ok = check(NPCActionMagic::resolveMagicTriggerTime(120, true, false) == 120,
        "legacy player magic still triggers before a short action exits") && ok;
    ok = check(NPCActionMagic::resolveMagicTriggerTime(0, true, false) == 0,
        "zero-duration player magic triggers immediately instead of being skipped") && ok;
    ok = check(Magic::resolvePlayerActionFileName(
        "烈火情天攻击", "z-杨影枫.ini") == "烈火情天攻击1",
        "legacy player action defaults to form one") && ok;
    ok = check(Magic::resolvePlayerActionFileName(
        "烈火情天攻击.asf", "ini/npcres/z-杨影枫2.ini") == "烈火情天攻击2.asf",
        "legacy player action uses the trailing NPC resource form index") && ok;

    return ok ? 0 : 1;
}
