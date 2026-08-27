#pragma once

#include "../Data/Global.h"

enum class ScriptNpcActionKind
{
	Unknown,
	Stand,
	Walk,
	Run,
	Jump,
	Attack,
	Magic,
	Sit,
	Hurt,
	Death,
	Special,
	FightStand,
	FightWalk,
	FightRun,
	FightJump,
};

inline ScriptNpcActionKind resolveCommonScriptNpcAction(int action)
{
	switch (action)
	{
	case 0:
	case 1:
		return ScriptNpcActionKind::Stand;
	case 2:
		return ScriptNpcActionKind::Walk;
	case 3:
		return ScriptNpcActionKind::Run;
	case 4:
		return ScriptNpcActionKind::Jump;
	case 5:
	case 6:
	case 7:
		return ScriptNpcActionKind::Attack;
	case 8:
		return ScriptNpcActionKind::Magic;
	case 11:
		return ScriptNpcActionKind::Death;
	default:
		return ScriptNpcActionKind::Unknown;
	}
}

inline ScriptNpcActionKind resolveYycsXjxqyScriptNpcAction(int action)
{
	ScriptNpcActionKind commonAction = resolveCommonScriptNpcAction(action);
	if (commonAction != ScriptNpcActionKind::Unknown)
	{
		return commonAction;
	}

	switch (action)
	{
	case 9:
		return ScriptNpcActionKind::Sit;
	case 10:
		return ScriptNpcActionKind::Hurt;
	case 12:
		return ScriptNpcActionKind::FightStand;
	case 13:
		return ScriptNpcActionKind::FightWalk;
	case 14:
		return ScriptNpcActionKind::FightRun;
	case 15:
		return ScriptNpcActionKind::FightJump;
	default:
		return ScriptNpcActionKind::Unknown;
	}
}

inline ScriptNpcActionKind resolveLegacyScriptNpcAction(int action)
{
	ScriptNpcActionKind commonAction = resolveCommonScriptNpcAction(action);
	if (commonAction != ScriptNpcActionKind::Unknown)
	{
		return commonAction;
	}

	switch (action)
	{
	case 9:
		return ScriptNpcActionKind::Hurt;
	case 10:
		return ScriptNpcActionKind::Sit;
	case 12:
		return ScriptNpcActionKind::Special;
	default:
		return ScriptNpcActionKind::Unknown;
	}
}

inline ScriptNpcActionKind resolveScriptNpcAction(
	ScriptNpcActionProfile profile,
	int action)
{
	// The original XJXQY converter uses -1 for an omitted action field; the
	// corresponding resource comment says the character should stand up.
	if (action == -1)
	{
		return ScriptNpcActionKind::Stand;
	}

	if (profile == ScriptNpcActionProfile::Yycs)
	{
		return resolveYycsXjxqyScriptNpcAction(action);
	}

	if (profile == ScriptNpcActionProfile::Xjxqy)
	{
		// Original XJXQY resources use 13 for legacy pose/stand-up sequences,
		// not for path movement. That resource contract takes precedence over
		// the later reference enum where 13 means FightWalk.
		if (action == 13)
		{
			return ScriptNpcActionKind::Stand;
		}
		return resolveYycsXjxqyScriptNpcAction(action);
	}

	// JXQY2 and custom profiles retain the established C++/legacy numbering.
	return resolveLegacyScriptNpcAction(action);
}

inline int resolveScriptNpcActionFileSlot(
	ScriptNpcActionProfile profile,
	int action)
{
	if (profile == ScriptNpcActionProfile::Legacy)
	{
		return action;
	}

	switch (action)
	{
	case 0: return 0;  // Stand
	case 1: return 1;  // Stand1
	case 2: return 2;  // Walk
	case 3: return 3;  // Run
	case 4: return 4;  // Jump
	case 5: return 5;  // Attack
	case 6: return 6;  // Attack1
	case 7: return 7;  // Attack2
	case 8: return 8;  // Magic
	case 9: return 10; // Sit
	case 10: return 9; // Hurt
	case 11: return 11; // Death
	case 12: return 20; // FightStand
	case 13: return 21; // FightWalk
	case 14: return 22; // FightRun
	case 15: return 23; // FightJump
	default: return -1;
	}
}

inline bool isFightScriptNpcAction(ScriptNpcActionKind action)
{
	return action == ScriptNpcActionKind::FightStand
		|| action == ScriptNpcActionKind::FightWalk
		|| action == ScriptNpcActionKind::FightRun
		|| action == ScriptNpcActionKind::FightJump;
}
