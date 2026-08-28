#include "../Game/Data/NPC.h"
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

	ok = check(NPC::shouldApplyTemporaryMagicListReplacement(nkPlayer, "ini\\magic\\morph.ini"),
		"player ReplaceMagic can replace the visible skill list during morph") && ok;
	ok = check(!NPC::shouldApplyTemporaryMagicListReplacement(nkBattle, ""),
		"empty non-player ReplaceMagic falls back to the normal attack list") && ok;
	ok = check(NPC::shouldApplyTemporaryMagicListReplacement(nkBattle, "ini\\magic\\morph.ini"),
		"battle NPC can use temporary ReplaceMagic attack list") && ok;
	ok = check(NPC::shouldApplyTemporaryMagicListReplacement(nkPartner, "ini\\magic\\partner-morph.ini"),
		"partner can use temporary ReplaceMagic attack list") && ok;

	ok = check(mskInvisibleKeepHidden == 4, "SpecialKind=4 follows C# invisible keep-hidden semantics") && ok;
	ok = check(mskInvisibleVisibleWhenAttack == 5, "SpecialKind=5 keeps C# visible-when-attack semantics") && ok;
	ok = check(mskAddDamageReduceShield == 3,
		"SpecialKind=3 follows the reference damage-reduce shield semantics") && ok;
	ok = check(mskImmobilize == 10,
		"SpecialKind=10 follows miu2d immobilize semantics") && ok;
	ok = check(mskAddShield == 11,
		"SpecialKind=11 isolates the JXQY2 full-absorb shield extension") && ok;

	return ok ? 0 : 1;
}
