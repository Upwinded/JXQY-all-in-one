#include "../Game/Data/NPCAction/NPCActionBounce.h"
#include "../Game/Data/NPCAction/NPCBounceMotion.h"
#include "../Game/Data/NPCAction/NPCActionMagicForcedMove.h"
#include "../Game/Data/NPCAction/NPCActionType.h"

#include <cmath>
#include <iostream>
#include <type_traits>

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

bool checkNear(float actual, float expected, float tolerance, const char* message)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        std::cerr << "FAILED: " << message << " actual=" << actual << " expected=" << expected << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    bool ok = true;

    ok = check((std::is_base_of<NPCActionBase, NPCActionBounce>::value), "bounce has a dedicated action class") && ok;
    ok = check((std::is_base_of<NPCActionBase, NPCActionMagicForcedMove>::value), "magic forced move has a dedicated action class") && ok;

    ok = check(isForcedMovementAction(NPCActionType::acBounce), "bounce is a forced movement action") && ok;
    ok = check(isForcedMovementAction(NPCActionType::acMagicForcedMove), "magic forced move is a forced movement action") && ok;
    ok = check(!isForcedMovementAction(NPCActionType::acStand), "stand is not a forced movement action") && ok;

    ok = check(isActionAllowedWhileBouncing(NPCActionType::acStand), "bounce allows stand recovery") && ok;
    ok = check(isActionAllowedWhileBouncing(NPCActionType::acAStand), "bounce allows fight stand recovery") && ok;
    ok = check(isActionAllowedWhileBouncing(NPCActionType::acBounce), "bounce allows bounce refresh") && ok;
    ok = check(isActionAllowedWhileBouncing(NPCActionType::acMagicForcedMove), "bounce allows forced move replacement") && ok;
    ok = check(isActionAllowedWhileBouncing(NPCActionType::acHurt), "bounce allows hurt interrupt") && ok;
    ok = check(isActionAllowedWhileBouncing(NPCActionType::acDeath), "bounce allows death interrupt") && ok;
    ok = check(isActionAllowedWhileBouncing(NPCActionType::acHide), "bounce allows hide interrupt") && ok;

    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acWalk), "bounce blocks walk") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acRun), "bounce blocks run") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acJump), "bounce blocks jump") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acAJump), "bounce blocks fight jump") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acAttack), "bounce blocks attack") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acMagic), "bounce blocks magic") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acSpecial), "bounce blocks special action") && ok;
    ok = check(!isActionAllowedWhileBouncing(NPCActionType::acSpecialAttack), "bounce blocks special attack") && ok;

    ok = check(isBounceActionTransitionAllowed(NPCActionType::acBounce), "bounce action allows bounce refresh") && ok;
    ok = check(isBounceActionTransitionAllowed(NPCActionType::acMagicForcedMove), "bounce action allows forced move transition") && ok;
    ok = check(isBounceActionTransitionAllowed(NPCActionType::acHurt), "bounce action allows hurt transition") && ok;
    ok = check(isBounceActionTransitionAllowed(NPCActionType::acDeath), "bounce action allows death transition") && ok;
    ok = check(isBounceActionTransitionAllowed(NPCActionType::acHide), "bounce action allows hide transition") && ok;

    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acStand), "bounce action does not normal-transition to stand") && ok;
    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acWalk), "bounce action blocks walk transition") && ok;
    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acRun), "bounce action blocks run transition") && ok;
    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acJump), "bounce action blocks jump transition") && ok;
    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acAttack), "bounce action blocks attack transition") && ok;
    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acMagic), "bounce action blocks magic transition") && ok;
    ok = check(!isBounceActionTransitionAllowed(NPCActionType::acSpecial), "bounce action blocks special transition") && ok;

    ok = check(isForcedMovementActionTransitionAllowed(NPCActionType::acBounce), "forced movement action allows bounce refresh") && ok;
    ok = check(isForcedMovementActionTransitionAllowed(NPCActionType::acMagicForcedMove), "forced movement action allows forced move refresh") && ok;
    ok = check(isForcedMovementActionTransitionAllowed(NPCActionType::acHurt), "forced movement action allows hurt transition") && ok;
    ok = check(!isForcedMovementActionTransitionAllowed(NPCActionType::acAttack), "forced movement action blocks attack transition") && ok;

    ok = check(resolveForcedMovementActionAfterUpdate(NPCActionType::acBounce, true, false) == NPCActionType::acBounce,
        "active bounce keeps bounce action") && ok;
    ok = check(resolveForcedMovementActionAfterUpdate(NPCActionType::acBounce, false, true) == NPCActionType::acMagicForcedMove,
        "finished bounce hands off to active magic forced move") && ok;
    ok = check(resolveForcedMovementActionAfterUpdate(NPCActionType::acBounce, false, false) == NPCActionType::acStand,
        "finished bounce recovers to stand") && ok;
    ok = check(resolveForcedMovementActionAfterUpdate(NPCActionType::acMagicForcedMove, false, true) == NPCActionType::acMagicForcedMove,
        "active magic forced move keeps forced move action") && ok;
    ok = check(resolveForcedMovementActionAfterUpdate(NPCActionType::acMagicForcedMove, true, false) == NPCActionType::acBounce,
        "finished magic forced move hands off to active bounce") && ok;
    ok = check(resolveForcedMovementActionAfterUpdate(NPCActionType::acMagicForcedMove, false, false) == NPCActionType::acStand,
        "finished magic forced move recovers to stand") && ok;

    auto firstBounce = composeBounceMotion({ 0.0f, 0.0f }, 0.0f, { 3.0f, 4.0f }, 10.0f);
    ok = checkNear(firstBounce.velocity, 10.0f, 0.001f, "first bounce keeps incoming velocity") && ok;
    ok = checkNear(getBounceVectorLength(firstBounce.direction), 1.0f, 0.001f,
        "first bounce normalizes projected direction") && ok;

    PointEx horizontalStep = normalizeBounceVector({ 64.0f, 0.0f });
    PointEx verticalStep = normalizeBounceVector({ 0.0f, 32.0f });
    ok = checkNear(getBounceVectorLength({ 64.0f, 0.0f }), 32.0f, 0.001f,
        "projected movement treats horizontal tile length as logical distance") && ok;
    ok = checkNear(getBounceVectorLength({ 0.0f, 32.0f }), 32.0f, 0.001f,
        "projected movement treats vertical tile length as logical distance") && ok;
    ok = checkNear(getProjectedTileUnitDistance({ 32.0f, 16.0f }), 1.0f, 0.001f,
        "projected tile units treat diagonal neighbor as unit distance") && ok;
    ok = checkNear(getProjectedTileUnitDistance({ 64.0f, 0.0f }), 1.4142135f, 0.001f,
        "projected tile units treat horizontal neighbor as sqrt two") && ok;
    ok = checkNear(getProjectedTileUnitDistance({ 0.0f, 32.0f }), 1.4142135f, 0.001f,
        "projected tile units treat vertical neighbor as sqrt two") && ok;
    ok = checkNear(getProjectedMovementSpeedForDuration({ 32.0f, 16.0f }, 20.0f, 1.0f), 1.1313709f, 0.001f,
        "projected speed derives diagonal jump speed from projected length and duration") && ok;
    ok = checkNear(getProjectedMovementSpeedForDuration({ 64.0f, 0.0f }, 20.0f, 1.0f), 1.6f, 0.001f,
        "projected speed derives horizontal jump speed from projected length and duration") && ok;
    ok = checkNear(getProjectedFrameDistance(10.0f, 16.0f, 0.004f), 0.64f, 0.001f,
        "projected frame distance keeps speed and game speed conversion explicit") && ok;
    ok = checkNear(getProjectedMagicFrameDistance(1.0f, 16.0f, 0.004f), 2.048f, 0.001f,
        "projected magic frame distance keeps magic flying scale explicit") && ok;
    ok = checkNear(getProjectedMovementSpeedForDuration({ 32.0f, 16.0f }, 0.0f, 1.0f), 0.0f, 0.001f,
        "projected speed with zero duration does not move") && ok;
    ok = checkNear(getProjectedMovementSpeedForDuration({ 32.0f, 16.0f }, 20.0f, 0.0f), 0.0f, 0.001f,
        "projected speed with zero game speed does not move") && ok;
    ok = checkNear(getProjectedFrameDistance(-1.0f, 16.0f, 0.004f), 0.0f, 0.001f,
        "negative projected speed does not advance") && ok;
    ok = checkNear(getProjectedFrameDistance(10.0f, 0.0f, 0.004f), 0.0f, 0.001f,
        "zero frame time does not advance projected movement") && ok;
    ok = checkNear(getProjectedFrameDistance(10.0f, 16.0f, 0.0f), 0.0f, 0.001f,
        "zero game speed does not advance projected movement") && ok;
    ok = check(isWithinProjectedMovementDistance({ 32.0f, 16.0f }, 22.7f),
        "projected distance check accepts one diagonal tile") && ok;
    ok = check(!isWithinProjectedMovementDistance({ 64.0f, 0.0f }, 22.7f),
        "projected distance check rejects horizontal screen tile at diagonal threshold") && ok;
    ok = check(!isWithinProjectedMovementDistance({ 32.0f, 16.0f }, -1.0f),
        "projected distance check rejects negative threshold") && ok;
    ok = checkNear(getBounceFrameDistance(10.0f, 16.0f, 0.004f), 0.64f, 0.001f,
        "bounce frame distance uses projected frame distance without magic scale") && ok;
    ok = checkNear(getBounceFrameFriction(20.0f), 4.0f, 0.001f,
        "bounce friction keeps C# per-frame value at effect frame time") && ok;
    ok = checkNear(horizontalStep.x * 32.0f, 64.0f, 0.001f,
        "horizontal projected unit advances full screen x distance") && ok;
    ok = checkNear(verticalStep.y * 32.0f, 32.0f, 0.001f,
        "vertical projected unit advances full screen y distance") && ok;

    PointEx horizontalOffset = advanceProjectedMovement({ 0.0f, 0.0f }, { 64.0f, 0.0f }, 16.0f);
    PointEx verticalOffset = advanceProjectedMovement({ 0.0f, 0.0f }, { 0.0f, 32.0f }, 16.0f);
    ok = checkNear(horizontalOffset.x, 32.0f, 0.001f,
        "half projected horizontal move covers half horizontal screen distance") && ok;
    ok = checkNear(verticalOffset.y, 16.0f, 0.001f,
        "half projected vertical move covers half vertical screen distance") && ok;
    PointEx zeroStepOffset = advanceProjectedMovement({ 1.0f, 2.0f }, { 64.0f, 0.0f }, 0.0f);
    PointEx overshootOffset = advanceProjectedMovement({ 1.0f, 2.0f }, { 64.0f, 0.0f }, 1000.0f);
    ok = checkNear(zeroStepOffset.x, 1.0f, 0.001f,
        "zero projected step keeps current x offset") && ok;
    ok = checkNear(zeroStepOffset.y, 2.0f, 0.001f,
        "zero projected step keeps current y offset") && ok;
    ok = checkNear(overshootOffset.x, 65.0f, 0.001f,
        "oversized projected step clamps to destination x offset") && ok;
    ok = checkNear(overshootOffset.y, 2.0f, 0.001f,
        "oversized projected step clamps to destination y offset") && ok;
    ok = check(getMovementProgressPermille(16.0f, 64.0f) == 250,
        "forced move progress exposes projected progress in permille") && ok;
    ok = check(getMovementProgressPermille(-10.0f, 64.0f) == 0,
        "forced move progress clamps negative progress") && ok;
    ok = check(getMovementProgressPermille(80.0f, 64.0f) == 1000,
        "forced move progress clamps overshoot progress") && ok;
    ok = check(getMovementProgressPermille(10.0f, 0.0f) == 1000,
        "forced move progress treats zero total distance as complete") && ok;

    PointEx horizontalBezierHalf = getBezierForcedMoveDrawOffset({ 64.0f, 0.0f }, 0.5f);
    PointEx horizontalBezierEnd = getBezierForcedMoveDrawOffset({ 64.0f, 0.0f }, 1.0f);
    PointEx verticalBezierHalf = getBezierForcedMoveDrawOffset({ 0.0f, 32.0f }, 0.5f);
    PointEx zeroBezierHalf = getBezierForcedMoveDrawOffset({ 0.0f, 0.0f }, 0.5f);
    ok = checkNear(horizontalBezierHalf.x, 0.0f, 0.001f,
        "forced move bezier keeps horizontal midpoint x on the real path") && ok;
    ok = checkNear(horizontalBezierHalf.y, -50.0f, 0.001f,
        "forced move bezier lifts horizontal midpoint like C# BezierMoveTo") && ok;
    ok = checkNear(horizontalBezierEnd.y, 0.0f, 0.001f,
        "forced move bezier visual offset returns to zero at endpoint") && ok;
    ok = checkNear(verticalBezierHalf.x, 10.0f, 0.001f,
        "forced move bezier gives vertical midpoint a side arc") && ok;
    ok = checkNear(verticalBezierHalf.y, 0.0f, 0.001f,
        "forced move bezier vertical side arc does not add y displacement") && ok;
    ok = checkNear(zeroBezierHalf.x, 0.0f, 0.001f,
        "forced move bezier with zero line has no visual x offset") && ok;
    ok = checkNear(zeroBezierHalf.y, 0.0f, 0.001f,
        "forced move bezier with zero line has no visual y offset") && ok;

    PointEx horizontalEffectVector = getEffectProjectedMovementVector({ 1, 0 });
    PointEx verticalEffectVector = getEffectProjectedMovementVector({ 0, 1 });
    PointEx diagonalEffectVector = getEffectProjectedMovementVector({ 32, 32 });
    ok = checkNear(getProjectedMovementLength(horizontalEffectVector), 32.0f, 0.001f,
        "effect horizontal direction has projected tile-height length") && ok;
    ok = checkNear(getProjectedMovementLength(verticalEffectVector), 32.0f, 0.001f,
        "effect vertical direction has projected tile-height length") && ok;
    ok = checkNear(getProjectedMovementLength(diagonalEffectVector), 1448.1547f, 0.01f,
        "effect diagonal direction uses projected length") && ok;

    PointEx horizontalEffectOffset = advanceProjectedMovement({ 0.0f, 0.0f }, horizontalEffectVector, 32.0f);
    PointEx verticalEffectOffset = advanceProjectedMovement({ 0.0f, 0.0f }, verticalEffectVector, 32.0f);
    PointEx diagonalEffectOffset = advanceProjectedMovement({ 0.0f, 0.0f }, diagonalEffectVector, 32.0f);
    ok = checkNear(horizontalEffectOffset.x, 64.0f, 0.001f,
        "effect horizontal projected step advances one horizontal screen tile") && ok;
    ok = checkNear(verticalEffectOffset.y, 32.0f, 0.001f,
        "effect vertical projected step advances one vertical screen tile") && ok;
    ok = checkNear(getProjectedMovementLength(diagonalEffectOffset), 32.0f, 0.001f,
        "effect diagonal projected step advances the requested projected distance") && ok;

    PointEx diagonalJumpOffset = advanceProjectedMovement(
        { 0.0f, 0.0f },
        { 32.0f, 16.0f },
        getProjectedMovementSpeedForDuration({ 32.0f, 16.0f }, 20.0f, 1.0f) * 20.0f);
    PointEx horizontalJumpOffset = advanceProjectedMovement(
        { 0.0f, 0.0f },
        { 64.0f, 0.0f },
        getProjectedMovementSpeedForDuration({ 64.0f, 0.0f }, 20.0f, 1.0f) * 20.0f);
    ok = checkNear(diagonalJumpOffset.x, 32.0f, 0.001f,
        "projected jump speed reaches diagonal destination x at duration") && ok;
    ok = checkNear(diagonalJumpOffset.y, 16.0f, 0.001f,
        "projected jump speed reaches diagonal destination y at duration") && ok;
    ok = checkNear(horizontalJumpOffset.x, 64.0f, 0.001f,
        "projected jump speed reaches horizontal destination at duration") && ok;

    auto combinedBounce = composeBounceMotion(horizontalStep, 5.0f, { 0.0f, 32.0f }, 12.0f);
    ok = checkNear(combinedBounce.velocity, 13.0f, 0.001f, "repeated bounce combines velocity vectors") && ok;
    ok = checkNear(getBounceVectorLength(combinedBounce.direction), 1.0f, 0.001f,
        "repeated bounce keeps projected direction normalized") && ok;
    ok = checkNear(combinedBounce.direction.x * combinedBounce.velocity / MapXRatio, 5.0f, 0.001f,
        "repeated bounce keeps logical x velocity component") && ok;
    ok = checkNear(combinedBounce.direction.y * combinedBounce.velocity, 12.0f, 0.001f,
        "repeated bounce keeps logical y velocity component") && ok;

    auto cancelledBounce = composeBounceMotion(horizontalStep, 8.0f, { -64.0f, 0.0f }, 8.0f);
    ok = checkNear(cancelledBounce.velocity, 0.0f, 0.001f, "opposite equal bounce cancels velocity") && ok;
    ok = checkNear(cancelledBounce.direction.x, 0.0f, 0.001f, "cancelled bounce clears direction x") && ok;
    ok = checkNear(cancelledBounce.direction.y, 0.0f, 0.001f, "cancelled bounce clears direction y") && ok;

    return ok ? 0 : 1;
}
