#include "../Game/Data/NPCAction/NPCActionAttack.h"
#include "../Game/Data/NPCAction/NPCActionMagic.h"
#include "../Image/IMP.h"

#include <iostream>
#include <memory>

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

	auto fallbackShadow = std::make_shared<IMPImage>();
	auto actionShadow = std::make_shared<IMPImage>();
	auto useActionImage = std::make_shared<IMPImage>();
	auto actionImage = std::make_shared<IMPImage>();

	ok = check(NPCActionMagic::selectMagicActionShadowPackage(nullptr, useActionImage, actionImage, fallbackShadow) == fallbackShadow,
		"magic action shadow ignores UseActionFile and action image when ActionShadowFile is absent") && ok;
	ok = check(NPCActionAttack::selectSpecialAttackShadowPackage(nullptr, useActionImage, actionImage, fallbackShadow) == fallbackShadow,
		"special attack shadow ignores UseActionFile and action image when ActionShadowFile is absent") && ok;

	ok = check(NPCActionMagic::selectMagicActionShadowPackage(actionShadow, useActionImage, actionImage, fallbackShadow) == actionShadow,
		"magic action shadow prefers ActionShadowFile") && ok;
	ok = check(NPCActionAttack::selectSpecialAttackShadowPackage(actionShadow, useActionImage, actionImage, fallbackShadow) == actionShadow,
		"special attack shadow prefers ActionShadowFile") && ok;

	return ok ? 0 : 1;
}
