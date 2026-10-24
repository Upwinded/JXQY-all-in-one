#include "../Game/Data/Global.h"
#include "../Game/Data/DefeatedNpcExperience.h"
#include "../Game/Config/Config.h"
#include "../Resource/ResourceManifest.h"

#include <iostream>
#include <string>

// This profile-only target runs without the engine configuration implementation.
bool Config::useQingyuUi = false;

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
	ok = check(!global.feature.separateTalentSlots,
		"unconfigured resources retain their existing magic capacity") && ok;
	ok = check(!global.feature.magicLevelLimitFromDefinition,
		"unconfigured resources retain their existing magic level rules") && ok;
	ok = check(!global.feature.nativeNpcAttackAtAnimationEnd,
		"unconfigured resources keep their existing NPC attack protocol") && ok;
	ok = check(!global.feature.restorePartnerAttributes,
		"unconfigured resources do not restore character attributes onto NPCs") && ok;
	ok = check(global.feature.useThewWhenNormalRun,
		"unconfigured resources retain their normal running stamina cost") && ok;

	ResourceManifest yycsUi;
	yycsUi.uiProfile = "YYCS";
	global.applyResourceManifestFeatures(yycsUi);
	ok = check(global.feature.topButtonsLayout &&
		global.feature.extendedInventoryLayout &&
		global.feature.menuResourceProfile == mrpYycs,
		"UI.Profile applies a coherent YYCS layout bundle") && ok;
	ok = check(global.feature.lumAsBrightness,
		"UI.Profile does not replace rendering behavior defaults") && ok;
	ok = check(!global.feature.nativeNpcAttackAtAnimationEnd,
		"the YYCS layout does not enable a native combat protocol for dependent MODs") && ok;
	ok = check(global.feature.useThewWhenNormalRun,
		"the UI layout does not change normal running stamina rules") && ok;

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
		"NativeNpcAttackAtAnimationEnd=1\n"
		"RestorePartnerAttributes=1\n"
		"UseThewWhenNormalRun=0\n"
		"RageSystem=1\n"
		"SeparateTalentSlots=1\n"
		"MagicLevelLimitFromDefinition=1\n";
	ResourceManifest explicitOverrides;
	ok = check(explicitOverrides.loadFromBuffer(overrideText.c_str(), (int)overrideText.size()),
		"feature override manifest parses") && ok;
	global.applyResourceManifestFeatures(explicitOverrides);
	ok = check(!global.feature.topButtonsLayout,
		"explicit disabled feature overrides UI.Profile default") && ok;
	ok = check(actionReleaseFrameRatioToString(global.magicReleaseFrameRatio) == "4/5",
		"the obsolete magic timing feature does not change release ratios") && ok;
	ok = check(global.feature.nativeNpcAttackAtAnimationEnd,
		"the native NPC attack protocol is explicitly enabled through the generic feature parser") && ok;
	ok = check(global.feature.restorePartnerAttributes,
		"partner attribute restoration is enabled only by its explicit feature") && ok;
	ok = check(!global.feature.useThewWhenNormalRun,
		"the generic feature parser can disable normal running stamina costs") && ok;
	ok = check(global.feature.rageSystem,
		"explicit manifest feature enables the MG-only rage system") && ok;
	ok = check(global.feature.separateTalentSlots,
		"the generic manifest feature parser enables separate talent slots") && ok;
	ok = check(global.feature.magicLevelLimitFromDefinition,
		"the generic manifest feature parser enables a declared magic level limit") && ok;
	ok = check(global.feature.extendedInventoryLayout,
		"absent feature keeps UI.Profile-derived default") && ok;

	ResourceManifest independentBehavior;
	independentBehavior.uiProfile = "JXQY2";
	independentBehavior.partnerFollowRadius = 2;
	independentBehavior.partnerFollowRadiusDefined = true;
	independentBehavior.features["lumasbrightness"] = false;
	global.applyResourceManifestFeatures(independentBehavior);
	ok = check(!global.feature.restorePartnerAttributes,
		"switching resources resets partner restoration independently of UI layout") && ok;
	ok = check(global.feature.useThewWhenNormalRun,
		"switching resources restores the default running stamina rule") && ok;
	ok = check(!global.feature.separateTalentSlots,
		"switching resources does not leak the talent capacity into another game") && ok;
	ok = check(!global.feature.nativeNpcAttackAtAnimationEnd,
		"switching resource profiles resets the native NPC protocol instead of leaking into another game") && ok;
	ok = check(!global.feature.topButtonsLayout &&
		global.feature.menuResourceProfile == mrpDefault,
		"JXQY2 UI.Profile selects only the JXQY2 layout bundle") && ok;
	ok = check(!global.feature.lumAsBrightness &&
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
			jxqy2Experience, 4, 5, 100, 99) == 100,
		"JXQY2 defaults to stored NPC.Exp as the base reward") && ok;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 7, 0, 0, true) == 63 &&
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 0, 0, true) == 72 &&
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 0, 0, false) == 0,
		"zero-exp hostile battle NPCs use the level formula while other zero-exp NPCs stay unrewarded") && ok;
	bool levelProductFallbackUsed = false;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 0, 0, true,
			&levelProductFallbackUsed) == 72 &&
			levelProductFallbackUsed &&
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 100, 0, true,
			&levelProductFallbackUsed) == 100 &&
			!levelProductFallbackUsed &&
		calculateDefeatedNpcBaseExperience(
			jxqy2Experience, 9, 8, 0, 0, false,
			&levelProductFallbackUsed) == 0 &&
			!levelProductFallbackUsed,
		"the level-product fallback flag marks only zero-exp hostile battle NPCs in stored mode") && ok;
	ok = check(
		FallbackPracticeKillFraction > 0.2221f &&
			FallbackPracticeKillFraction < 0.2223f &&
			FallbackUseKillFraction > 0.0332f &&
			FallbackUseKillFraction < 0.0334f,
		"fallback magic fractions mirror the reference Yueying kill rules") && ok;

	ResourceManifest yycsExperience;
	yycsExperience.type = GAME_YYCS;
	const int yycsBase = calculateDefeatedNpcBaseExperience(
		yycsExperience, 4, 5, 100, 0);
	ok = check(yycsBase == 20,
		"YYCS defaults to level product plus bonus as the base reward") && ok;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			yycsExperience, 4, 12, 100, 10) == 58,
		"YYCS experience bonus is added after the recipient and defeated levels") && ok;
	ok = check(
		calculateDefeatedNpcBaseExperience(
			yycsExperience, 9, 8, 0, 0, true,
			&levelProductFallbackUsed) == 72 &&
			!levelProductFallbackUsed,
		"the level-product fallback flag stays clear for packs whose primary mode is the level formula") && ok;

	ResourceManifest xjxqyExperience;
	xjxqyExperience.type = GAME_XJXQY;
	ok = check(
		xjxqyExperience.resolvedDefeatedNpcExperienceMode() ==
			DefeatedNpcExperienceMode::LevelProductWithBonus,
		"XJXQY defaults to the level-product formula") && ok;

	ResourceManifest customExperience;
	customExperience.type = GAME_CUSTOM;
	ok = check(
		customExperience.resolvedDefeatedNpcExperienceMode() ==
			DefeatedNpcExperienceMode::LevelProductWithBonus,
		"Type 3 defaults to the level-product formula") && ok;
	for (int multiplier : { 1, 2, 3 })
	{
		ok = check(roundAutomaticExperience(scaleAutomaticExperience(100, multiplier)) == 100 * multiplier &&
			roundAutomaticExperience(scaleAutomaticExperience(63, multiplier)) == 63 * multiplier &&
			roundAutomaticExperience(scaleAutomaticExperience(58, multiplier)) == 58 * multiplier,
			"each player multiplier scales stored, fallback and level-product rewards once") && ok;
	}
	ok = check(
		floorAutomaticExperience(scaleAutomaticExperience(20, 3.0), 0.25) == 15 &&
		floorAutomaticExperience(scaleAutomaticExperience(20, 3.0), 0.4) == 24,
		"practice and current-use magic fractions consume the scaled base exactly once") && ok;

	const std::string explicitExperienceText =
		"[Game]\nId=MOD\nType=0\n\n"
		"[Resource]\nTextEncodingConverted=1\n\n"
		"[Experience]\n"
		"DefeatedNpcExperienceMode=LevelProductWithBonus\n";
	ResourceManifest explicitExperience;
	ok = check(explicitExperience.loadFromBuffer(
		explicitExperienceText.c_str(),
		static_cast<int>(explicitExperienceText.size())) &&
		explicitExperience.resolvedDefeatedNpcExperienceMode() ==
			DefeatedNpcExperienceMode::LevelProductWithBonus &&
		explicitExperience.textEncodingConverted,
		"a Type 0 MOD can explicitly opt into the level-product experience formula") && ok;

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
	ActionReleaseFrameRatio parsedRatio{ 7, 9 };
	for (const std::string& invalid : { "", "2", "0/3", "1/0", "-1/3", "3/2",
		"1/2/3", "1.0/2", "1/2x", "2147483648/2147483648", "+1/2" })
	{
		ok = check(!tryParseActionReleaseFrameRatio(invalid, parsedRatio)
			&& parsedRatio.numerator == 7 && parsedRatio.denominator == 9,
			"invalid release ratios are rejected without changing the output: " + invalid) && ok;
	}
	ok = check(tryParseActionReleaseFrameRatio(" 2 / 4 ", parsedRatio)
		&& actionReleaseFrameRatioToString(parsedRatio) == "2/4",
		"release ratios retain their unreduced integer frame calculation") && ok;
	ok = check(tryParseActionReleaseFrameRatio("2147483647/2147483647", parsedRatio)
		&& parsedRatio.numerator == 2147483647 && parsedRatio.denominator == 2147483647,
		"release ratios accept positive int boundaries") && ok;
	const std::string releaseText =
		"[Game]\nId=RELEASE_TEST\nType=0\n"
		"[Combat]\nAttackReleaseFrameRatio=2/4\n"
		"SpecialAttackReleaseFrameRatio=1/1\nMagicReleaseFrameRatio=3/7\n";
	ResourceManifest releaseManifest;
	ok = check(releaseManifest.loadFromBuffer(releaseText.c_str(), static_cast<int>(releaseText.size()))
		&& releaseManifest.attackReleaseFrameRatioDefined
		&& releaseManifest.specialAttackReleaseFrameRatioDefined
		&& releaseManifest.magicReleaseFrameRatioDefined
		&& actionReleaseFrameRatioToString(releaseManifest.attackReleaseFrameRatio) == "2/4"
		&& actionReleaseFrameRatioToString(releaseManifest.specialAttackReleaseFrameRatio) == "1/1"
		&& actionReleaseFrameRatioToString(releaseManifest.magicReleaseFrameRatio) == "3/7"
		&& releaseManifest.attackReleaseFrameRatioSourceId == "RELEASE_TEST"
		&& releaseManifest.specialAttackReleaseFrameRatioSourceId == "RELEASE_TEST"
		&& releaseManifest.magicReleaseFrameRatioSourceId == "RELEASE_TEST",
		"each explicit release ratio retains its independent value and local source") && ok;
	global.applyResourceManifestFeatures(releaseManifest);
	ok = check(actionReleaseFrameRatioToString(global.attackReleaseFrameRatio) == "2/4"
		&& actionReleaseFrameRatioToString(global.specialAttackReleaseFrameRatio) == "1/1"
		&& actionReleaseFrameRatioToString(global.magicReleaseFrameRatio) == "3/7",
		"runtime installs the three effective resource release ratios") && ok;
	global.applyResourceManifestFeatures(defaultManifest);
	ok = check(actionReleaseFrameRatioToString(global.attackReleaseFrameRatio) == "2/3"
		&& actionReleaseFrameRatioToString(global.specialAttackReleaseFrameRatio) == "1/3"
		&& actionReleaseFrameRatioToString(global.magicReleaseFrameRatio) == "4/5",
		"switching resources clears all previously installed release ratios") && ok;
	const std::string missingReleaseText = "[Game]\nId=NO_RELEASE\nType=2\n";
	ok = check(releaseManifest.loadFromBuffer(missingReleaseText.c_str(), static_cast<int>(missingReleaseText.size()))
		&& !releaseManifest.attackReleaseFrameRatioDefined
		&& !releaseManifest.specialAttackReleaseFrameRatioDefined
		&& !releaseManifest.magicReleaseFrameRatioDefined
		&& releaseManifest.attackReleaseFrameRatioSourceId.empty()
		&& releaseManifest.specialAttackReleaseFrameRatioSourceId.empty()
		&& releaseManifest.magicReleaseFrameRatioSourceId.empty()
		&& actionReleaseFrameRatioToString(releaseManifest.attackReleaseFrameRatio) == "2/3"
		&& actionReleaseFrameRatioToString(releaseManifest.specialAttackReleaseFrameRatio) == "1/3"
		&& actionReleaseFrameRatioToString(releaseManifest.magicReleaseFrameRatio) == "4/5",
		"reloading a manifest resets absent declarations and sources to universal defaults") && ok;
	const std::string invalidReleaseText =
		"[Game]\nId=BAD_RELEASE\n[Combat]\nAttackReleaseFrameRatio=0/3\n"
		"SpecialAttackReleaseFrameRatio=3/2\nMagicReleaseFrameRatio=\n";
	ok = check(releaseManifest.loadFromBuffer(invalidReleaseText.c_str(), static_cast<int>(invalidReleaseText.size()))
		&& !releaseManifest.attackReleaseFrameRatioDefined
		&& !releaseManifest.specialAttackReleaseFrameRatioDefined
		&& !releaseManifest.magicReleaseFrameRatioDefined
		&& releaseManifest.invalidActionReleaseFrameRatioKeys.size() == 3,
		"invalid and empty declarations are missing for inheritance and retain diagnostic keys") && ok;
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
		u8"PlayerName=  杨影枫  \n"
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
		explicitBehavior.scriptPlayerName == u8"杨影枫" &&
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
		global.scriptPlayerName == u8"杨影枫" &&
		global.resolveScriptCharacterName("#name") == u8"杨影枫" &&
		global.resolveScriptCharacterName("#Name") == "#Name" &&
		global.resolveScriptCharacterName("#name.lua") == "#name.lua" &&
		global.resolveScriptText(u8"#name：你好，#name！") == u8"杨影枫：你好，杨影枫！" &&
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
	const std::string unnamedText = "[Game]\nId=UNNAMED\n[Script]\nPlayerName=\n";
	ok = check(explicitBehavior.loadFromBuffer(unnamedText.data(), static_cast<int>(unnamedText.size()))
		&& explicitBehavior.scriptPlayerName.empty(), "empty player name is optional and clears a reused manifest") && ok;
	global.applyResourceManifestFeatures(explicitBehavior);
	ok = check(global.resolveScriptText("#name") == "#name"
		&& global.resolveScriptCharacterName("#name") == "#name",
		"switching resources clears the configured name without tightening load rules") && ok;

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
	ok = check(levelUpManifest.levelUpMessageDefined &&
		levelUpManifest.levelUpEffectMode == LevelUpEffectMode::Append &&
		levelUpManifest.getLevelUpEffectCandidates(0) == std::vector<std::string>{ "male.ini" } &&
		levelUpManifest.getLevelUpEffectCandidates(2) == std::vector<std::string>{ "female.ini" },
		"level-up defaults to append and chooses the local definition for the player's sex") && ok;
	const std::string emptyLevelUpText =
		"[Game]\nId=EMPTY_LEVEL_UP\n[LevelUp]\nEffectMode=rEpLaCe\nRandomEffects=\nMaleEffect=\n";
	ok = check(
		levelUpManifest.loadFromBuffer(emptyLevelUpText.data(),
			static_cast<int>(emptyLevelUpText.size())) &&
			!levelUpManifest.levelUpMessageDefined &&
			levelUpManifest.levelUpMessage == "{name}的等级得到提升！" &&
			levelUpManifest.levelUpEffectMode == LevelUpEffectMode::Replace &&
			levelUpManifest.levelUpRandomEffects.empty() &&
			levelUpManifest.levelUpMaleEffect.empty() &&
			levelUpManifest.getLevelUpEffectCandidates(2).empty(),
		"effect mode is case insensitive and reloading clears prior local effects") && ok;

	return ok ? 0 : 1;
}
