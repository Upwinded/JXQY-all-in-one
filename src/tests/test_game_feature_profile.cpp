#include "../Game/Data/Global.h"
#include "../Game/Data/DefeatedNpcExperience.h"
#include "../Resource/ResourceManifest.h"

#include <iostream>
#include <string>

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		return false;
	}
	return true;
}
}

int main()
{
	bool ok = true;
	const ResourceManifest defaultManifest =
		ResourceManifest::createDefault(".");
	ok = check(
		defaultManifest.levelUpRandomEffects.empty() &&
		defaultManifest.levelUpMaleEffect.empty() &&
		defaultManifest.levelUpFemaleEffect.empty() &&
		defaultManifest.resolvedMinimumMagicDamage() == 10 &&
		defaultManifest.resolvedMagicEffectCalculationMode() ==
			MagicEffectCalculationMode::ReplaceAttack,
		"a new resource manifest does not assume game-specific level-up files") && ok;
	Global global;
	global.applyResourceManifestFeatures(defaultManifest);
	ok = check(!global.feature.topButtonsLayout && global.feature.lumAsBrightness,
		"resource behavior starts from stable universal defaults") && ok;
	ok = check(!global.feature.rageSystem,
		"unconfigured resources do not enable the MG-only rage system") && ok;

	ResourceManifest yycsUi;
	yycsUi.uiProfile = "YYCS";
	global.applyResourceManifestFeatures(yycsUi);
	ok = check(global.feature.topButtonsLayout &&
		global.feature.extendedInventoryLayout &&
		global.feature.menuResourceProfile == mrpYycs,
		"UI.Profile applies a coherent YYCS layout bundle") && ok;
	ok = check(!global.feature.magicTriggerAtAnimationEnd && global.feature.lumAsBrightness,
		"UI.Profile does not replace JXQY2 gameplay and rendering behavior defaults") && ok;

	const std::string overrideText =
		"[Game]\n"
		"Id=MOD\n"
		"Type=0\n"
		"\n"
		"[UI]\n"
		"Profile=YYCS\n"
		"\n"
		"[Features]\n"
		"TopButtonsLayout=0\n"
		"MagicTriggerAtAnimationEnd=1\n"
		"RageSystem=1\n";
	ResourceManifest explicitOverrides;
	ok = check(explicitOverrides.loadFromBuffer(overrideText.c_str(), (int)overrideText.size()),
		"feature override manifest parses") && ok;
	global.applyResourceManifestFeatures(explicitOverrides);
	ok = check(!global.feature.topButtonsLayout,
		"explicit disabled feature overrides UI.Profile default") && ok;
	ok = check(global.feature.magicTriggerAtAnimationEnd,
		"explicit enabled feature overrides the universal default") && ok;
	ok = check(global.feature.rageSystem,
		"explicit manifest feature enables the MG-only rage system") && ok;
	ok = check(global.feature.extendedInventoryLayout,
		"absent feature keeps UI.Profile-derived default") && ok;

	ResourceManifest independentBehavior;
	independentBehavior.uiProfile = "JXQY2";
	independentBehavior.partnerFollowRadius = 2;
	independentBehavior.partnerFollowRadiusDefined = true;
	independentBehavior.features["magictriggeratanimationend"] = true;
	independentBehavior.features["lumasbrightness"] = false;
	global.applyResourceManifestFeatures(independentBehavior);
	ok = check(!global.feature.topButtonsLayout &&
		global.feature.menuResourceProfile == mrpDefault,
		"JXQY2 UI.Profile selects only the JXQY2 layout bundle") && ok;
	ok = check(global.feature.magicTriggerAtAnimationEnd &&
		!global.feature.lumAsBrightness &&
		global.partnerFollowRadius == 2,
		"explicit gameplay behavior remains independent from UI.Profile") && ok;

	ResourceManifest dependentLayoutOverrides;
	dependentLayoutOverrides.features["stateequipintegratedlayout"] = true;
	dependentLayoutOverrides.features["topbuttonslayout"] = false;
	global.applyResourceManifestFeatures(dependentLayoutOverrides);
	ok = check(global.feature.stateEquipIntegratedLayout && global.feature.topButtonsLayout,
		"integrated equip layout normalizes its required top-button layout") && ok;

	dependentLayoutOverrides.features["stateequipintegratedlayout"] = false;
	dependentLayoutOverrides.features["hiderightmenuswithintegratedequip"] = true;
	dependentLayoutOverrides.features["magicbuttonopensintegratedequip"] = true;
	global.applyResourceManifestFeatures(dependentLayoutOverrides);
	ok = check(!global.feature.hideRightMenusWithIntegratedEquip &&
		!global.feature.magicButtonOpensIntegratedEquip,
		"integrated-layout-only dependent features are disabled without their parent") && ok;

	ResourceManifest jxqy2Experience;
	jxqy2Experience.type = GAME_JXQY2;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 4, 5, 100, 99) == 100 &&
		roundAutomaticExperience(scaleAutomaticExperience(
			100,
			jxqy2Experience.resolvedExperienceMultiplier())) == 300,
		"JXQY2 defaults to stored NPC.Exp with multiplier 3.0") && ok;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 7, 0, 0, true) == 63 &&
		roundAutomaticExperience(scaleAutomaticExperience(
			63,
			jxqy2Experience.resolvedExperienceMultiplier())) == 189 &&
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 0, 0, true) == 72 &&
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 0, 0, false) == 0,
		"zero-exp hostile battle NPCs use the level formula while other zero-exp NPCs stay unrewarded") && ok;

	ResourceManifest yycsExperience;
	yycsExperience.type = GAME_YYCS;
	const int yycsBase = calculateDefeatedNpcBaseExperience(
		yycsExperience, 4, 5, 100, 0);
	ok = check(yycsBase == 20 &&
		roundAutomaticExperience(scaleAutomaticExperience(
			yycsBase,
			yycsExperience.resolvedExperienceMultiplier())) == 60,
		"YYCS defaults to level product plus bonus with multiplier 3.0") && ok;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			yycsExperience, 4, 12, 100, 10) == 58 &&
		roundAutomaticExperience(scaleAutomaticExperience(
			58,
			yycsExperience.resolvedExperienceMultiplier())) == 174,
		"YYCS experience bonus is added after the recipient and defeated levels") && ok;

	ResourceManifest xjxqyExperience;
	xjxqyExperience.type = GAME_XJXQY;
	ok = check(
		xjxqyExperience.resolvedDefeatedNpcExperienceMode() ==
			DefeatedNpcExperienceMode::LevelProductWithBonus &&
		xjxqyExperience.resolvedExperienceMultiplier() == 3.0,
		"XJXQY defaults to the new formula with multiplier 3.0") && ok;

	ResourceManifest customExperience;
	customExperience.type = GAME_CUSTOM;
	ok = check(
		customExperience.resolvedDefeatedNpcExperienceMode() ==
			DefeatedNpcExperienceMode::LevelProductWithBonus &&
		customExperience.resolvedExperienceMultiplier() == 1.0,
		"Type 3 defaults to the new formula with multiplier 1.0") && ok;
	ok = check(
		floorAutomaticExperience(scaleAutomaticExperience(20, 3.0), 0.25) == 15 &&
		floorAutomaticExperience(scaleAutomaticExperience(20, 3.0), 0.4) == 24,
		"practice and current-use magic fractions consume the scaled base exactly once") && ok;

	const std::string explicitExperienceText =
		"[Game]\nId=MOD\nType=0\n\n"
		"[Resource]\nTextEncodingConverted=1\n\n"
		"[Experience]\n"
		"DefeatedNpcExperienceMode=LevelProductWithBonus\n"
		"ExperienceMultiplier=1.5\n";
	ResourceManifest explicitExperience;
	ok = check(explicitExperience.loadFromBuffer(
		explicitExperienceText.c_str(),
		static_cast<int>(explicitExperienceText.size())) &&
		explicitExperience.resolvedDefeatedNpcExperienceMode() ==
			DefeatedNpcExperienceMode::LevelProductWithBonus &&
		explicitExperience.resolvedExperienceMultiplier() == 1.5 &&
		explicitExperience.textEncodingConverted,
		"a Type 0 MOD can explicitly opt into independently configurable new experience rules") && ok;

	const std::string combatText =
		"[Game]\nId=COMBAT_TEST\nType=0\n\n"
		"[Combat]\nMinimumMagicDamage=5\n"
		"MagicEffectCalculationMode=AddToAttack\n";
	ResourceManifest combatManifest;
	ok = check(
		combatManifest.loadFromBuffer(
			combatText.c_str(), static_cast<int>(combatText.size())) &&
		combatManifest.minimumMagicDamageDefined &&
		combatManifest.resolvedMinimumMagicDamage() == 5 &&
		combatManifest.magicEffectCalculationModeDefined &&
		combatManifest.resolvedMagicEffectCalculationMode() ==
			MagicEffectCalculationMode::AddToAttack,
		"minimum magic damage parses from resource-wide combat configuration") && ok;
	combatManifest.type = GAME_YYCS;
	ok = check(
		combatManifest.resolvedMinimumMagicDamage() == 5 &&
		combatManifest.resolvedMagicEffectCalculationMode() ==
			MagicEffectCalculationMode::AddToAttack,
		"combat configuration does not change with Game.Type") && ok;

	const std::string invalidCombatText =
		"[Game]\nId=INVALID_COMBAT_TEST\nType=2\n\n"
		"[Combat]\nMinimumMagicDamage=-1\n"
		"MagicEffectCalculationMode=Unknown\n";
	ResourceManifest invalidCombatManifest;
	ok = check(
		invalidCombatManifest.loadFromBuffer(
			invalidCombatText.c_str(),
			static_cast<int>(invalidCombatText.size())) &&
		!invalidCombatManifest.minimumMagicDamageDefined &&
		invalidCombatManifest.resolvedMinimumMagicDamage() == 10 &&
		!invalidCombatManifest.magicEffectCalculationModeDefined &&
		invalidCombatManifest.resolvedMagicEffectCalculationMode() ==
			MagicEffectCalculationMode::ReplaceAttack,
		"invalid minimum magic damage uses the universal compatibility default") && ok;

	const std::string explicitBehaviorText =
		"[Game]\nId=BEHAVIOR_TEST\nType=2\n\n"
		"[Experience]\nLevelUpThresholdMode=GreaterThanOrEqual\n\n"
		"[Gameplay]\n"
		"PartnerFollowRadius=4\n"
		"PartnerFollowRunRadius=7\n\n"
		"[Script]\n"
		"NpcActionProfile=Legacy\n"
		"NpcRuntimeProfile=Legacy\n"
		"SpecialActionMode=Replace\n"
		"AddLifeMode=PlayerRules\n\n"
		"[Features]\nRainSceneTint=0\n";
	ResourceManifest explicitBehavior;
	ok = check(
		explicitBehavior.loadFromBuffer(
			explicitBehaviorText.c_str(),
			static_cast<int>(explicitBehaviorText.size())) &&
		explicitBehavior.resolvedLevelUpThresholdMode() ==
			LevelUpThresholdMode::GreaterThanOrEqual &&
		explicitBehavior.resolvedPartnerFollowRadius() == 4 &&
		explicitBehavior.resolvedPartnerFollowRunRadius() == 7 &&
		explicitBehavior.resolvedNpcActionProfile() ==
			ScriptNpcActionProfile::Legacy &&
		explicitBehavior.resolvedNpcRuntimeProfile() ==
			ScriptNpcRuntimeProfile::Legacy &&
		explicitBehavior.resolvedSpecialActionMode() ==
			ScriptSpecialActionMode::Replace &&
		explicitBehavior.resolvedAddLifeMode() ==
			ScriptAddLifeMode::PlayerRules,
		"resource configuration overrides every gameplay and script compatibility default") && ok;
	global.applyResourceManifestFeatures(explicitBehavior);
	ok = check(
		global.partnerFollowRadius == 4 &&
		global.partnerFollowRunRadius == 7 &&
		global.levelUpThresholdMode ==
			LevelUpThresholdMode::GreaterThanOrEqual &&
		global.npcActionProfile == ScriptNpcActionProfile::Legacy &&
		global.npcRuntimeProfile == ScriptNpcRuntimeProfile::Legacy &&
		global.specialActionMode == ScriptSpecialActionMode::Replace &&
		global.addLifeMode == ScriptAddLifeMode::PlayerRules &&
		!global.feature.rainSceneTint,
		"explicit resource behavior is independent from Game.Type") && ok;

	const std::string levelUpText =
		"[Game]\nId=LEVEL_UP_TEST\nType=2\n\n"
		"[LevelUp]\n"
		"Message={name}升到{level}级\n"
		"RandomEffects= first.ini,second.ini ,, third.ini \n"
		"MaleEffect=male.ini\n"
		"FemaleEffect=female.ini\n";
	ResourceManifest levelUpManifest;
	ok = check(
		levelUpManifest.loadFromBuffer(
			levelUpText.c_str(),
			static_cast<int>(levelUpText.size())) &&
		levelUpManifest.levelUpMessage == "{name}升到{level}级" &&
		levelUpManifest.levelUpRandomEffects ==
			std::vector<std::string>{
				"first.ini", "second.ini", "third.ini" } &&
		levelUpManifest.levelUpMaleEffect == "male.ini" &&
		levelUpManifest.levelUpFemaleEffect == "female.ini",
		"level-up message and effect Magic files parse from the resource manifest") && ok;

	return ok ? 0 : 1;
}
