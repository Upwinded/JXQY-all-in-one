#include "../Game/Data/NPCAction/NPCActionMagic.h"
#include "../Game/Data/NPCAction/NPCActionAttack.h"
#include "../Game/Data/NPCAction/ActionReleaseTiming.h"
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

    using Action = NPCActionType;
    ok = check(NPCActionAttack::selectAttackType(0, true, true) == Action::acAttack
        && NPCActionAttack::selectAttackType(1, true, true) == Action::acAttack1
        && NPCActionAttack::selectAttackType(2, true, true) == Action::acAttack2,
        "three available C# attack branches give practice one of three rolls") && ok;
    ok = check(NPCActionAttack::selectAttackType(1, false, true) == Action::acAttack
        && NPCActionAttack::selectAttackType(2, false, true) == Action::acAttack2,
        "missing Attack1 does not increase practice to one of two rolls") && ok;
    ok = check(NPCActionAttack::selectAttackType(1, true, false) == Action::acAttack1
        && NPCActionAttack::selectAttackType(2, true, false) == Action::acAttack,
        "missing Attack2 cannot activate a practice attack") && ok;
    ok = check(NPCActionAttack::selectAttackType(0, false, false) == Action::acAttack
        && NPCActionAttack::selectAttackType(1, false, false) == Action::acAttack
        && NPCActionAttack::selectAttackType(2, false, false) == Action::acAttack,
        "Nalan Zhen's single attack animation handles all three rolls") && ok;

    ok = check(calculateActionReleaseTime(950, 19, {2, 3}) == 600
        && calculateActionReleaseTime(1120, 16, {4, 5}) == 840,
        "native frame gates divide before multiplying instead of taking a continuous percentage") && ok;
    ok = check(calculateActionReleaseTime(269, 8, {2, 3}) == 135
        && calculateActionReleaseTime(269, 8, {1, 3}) == 68,
        "sped-up actions release only when their visual clock reaches the gate") && ok;
    ok = check(calculateActionReleaseTime(950, 19, {2, 2}) == 900
        && calculateActionReleaseTime(950, 19, {1, 1}) == 950,
        "unreduced frame ratios retain native rounding while one over one waits for action end") && ok;
    ok = check(calculateActionReleaseTime(100, 2, {2, 3}) == 50
        && calculateActionReleaseTime(20, 1, {4, 5}) == 20
        && calculateActionReleaseTime(1, 8, {2, 3}) == 1,
        "short animations with a zero gate release on the first frame advance") && ok;
    ok = check(calculateActionReleaseTime(0, 8, {2, 3}) == 0
        && calculateActionReleaseTime(100, 0, {4, 5}) == 0,
        "zero duration and empty animations release immediately") && ok;
    ok = check(Magic::resolvePlayerActionFileName(
        "烈火情天攻击", "z-杨影枫.ini") == "烈火情天攻击1",
        "legacy player action defaults to form one") && ok;
    ok = check(Magic::resolvePlayerActionFileName(
        "烈火情天攻击.asf", "ini/npcres/z-杨影枫2.ini") == "烈火情天攻击2.asf",
        "legacy player action uses the trailing NPC resource form index") && ok;

    return ok ? 0 : 1;
}
