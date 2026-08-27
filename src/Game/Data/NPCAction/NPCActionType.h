#pragma once

enum class NPCActionType : int
{
    acStand = 0,
    acStand1 = 1,
    acWalk = 2,
    acRun = 3,
    acJump = 4,
    acAttack = 5,
    acAttack1 = 6,
    acAttack2 = 7,
    acMagic = 8,
    acHurt = 9,
    acSit = 10,
    acDeath = 11,
    acSpecial = 12,
    acSitting = 13,
    acAStand = 20,
    acAWalk = 21,
    acARun = 22,
    acAJump = 23,
    acSpecialAttack = 24,
    acBounce = 25,
    acMagicForcedMove = 26,
    acHide = 0xFF,
};

inline bool isForcedMovementAction(NPCActionType type)
{
    return type == NPCActionType::acBounce || type == NPCActionType::acMagicForcedMove;
}

inline bool isActionAllowedWhileBouncing(NPCActionType type)
{
    switch (type)
    {
    case NPCActionType::acStand:
    case NPCActionType::acStand1:
    case NPCActionType::acAStand:
    case NPCActionType::acBounce:
    case NPCActionType::acMagicForcedMove:
    case NPCActionType::acHurt:
    case NPCActionType::acDeath:
    case NPCActionType::acHide:
        return true;
    default:
        return false;
    }
}

inline bool isForcedMovementActionTransitionAllowed(NPCActionType type)
{
    switch (type)
    {
    case NPCActionType::acHurt:
    case NPCActionType::acDeath:
    case NPCActionType::acHide:
    case NPCActionType::acBounce:
    case NPCActionType::acMagicForcedMove:
        return true;
    default:
        return false;
    }
}

inline bool isBounceActionTransitionAllowed(NPCActionType type)
{
    return isForcedMovementActionTransitionAllowed(type);
}

inline NPCActionType resolveForcedMovementActionAfterUpdate(NPCActionType currentType, bool bouncing, bool magicForcedMoving)
{
    if (currentType == NPCActionType::acBounce && !bouncing)
    {
        return magicForcedMoving ? NPCActionType::acMagicForcedMove : NPCActionType::acStand;
    }
    if (currentType == NPCActionType::acMagicForcedMove && !magicForcedMoving)
    {
        return bouncing ? NPCActionType::acBounce : NPCActionType::acStand;
    }
    return currentType;
}
