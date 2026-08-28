#include "../Game/Data/Object.h"
#include "../Game/Data/ObjectManager.h"
#include "../Game/Data/MobileTouchInteraction.h"
#include "../Game/Data/NPC.h"
#include "../Game/GameManager/GameController.h"
#include "../Image/ImagePackagePathCandidates.h"
#include "../Types/WeatherTypes.h"
#include "../Game/Data/WaterEffect.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << "\n";
        return false;
    }
    return true;
}

std::string normalizePath(std::string path)
{
    for (char& ch : path)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    return path;
}

bool checkCandidates(
    const std::string& imageName,
    const std::vector<std::string>& expected,
    const char* message)
{
    std::vector<std::string> actual = buildObjectImageResourceCandidates(imageName);
    if (actual.size() != expected.size())
    {
        std::cerr << "FAILED: " << message << " size " << actual.size() << " expected " << expected.size() << "\n";
        return false;
    }
    for (size_t i = 0; i < actual.size(); i++)
    {
        if (normalizePath(actual[i]) != normalizePath(expected[i]))
        {
            std::cerr << "FAILED: " << message << " candidate " << i << " = "
                << actual[i] << " expected " << expected[i] << "\n";
            return false;
        }
    }
    return true;
}
}

int main()
{
    bool ok = true;

    ok = check(isObjectObstacleKind(okOrnament), "ornament object blocks walking") && ok;
    ok = check(isObjectObstacleKind(okBox), "box object blocks walking") && ok;
    ok = check(isObjectObstacleKind(okDoor), "door object blocks walking") && ok;
    ok = check(!isObjectObstacleKind(okBody), "body object does not block walking") && ok;
    ok = check(!isObjectObstacleKind(okTrap), "trap object does not block walking") && ok;
    ok = check(!isObjectObstacleKind(okPickup), "pickup object does not block walking") && ok;

    ok = check(isObjectAutoPlayKind(okOrnament), "dynamic object auto-plays") && ok;
    ok = check(isObjectAutoPlayKind(okTrap), "trap object auto-plays") && ok;
    ok = check(isObjectAutoPlayKind(okPickup), "pickup object auto-plays") && ok;
    ok = check(isObjectAutoPlayKind(okPickupLegacy), "legacy pickup object auto-plays") && ok;
    ok = check(!isObjectAutoPlayKind(okBox), "box object does not auto-play") && ok;
    ok = check(!isObjectAutoPlayKind(okBody), "body object does not auto-play") && ok;
    ok = check(!isObjectAutoPlayKind(okDoor), "door object does not auto-play") && ok;
    ok = check(shouldStartObjectResourceAnimation(okPickupLegacy, true, false),
        "legacy pickup starts a loaded object animation on its initial resource load") && ok;
    ok = check(!shouldStartObjectResourceAnimation(okPickupLegacy, true, true),
        "persisted pickup state does not replay a completed object animation") && ok;
    ok = check(!shouldStartObjectResourceAnimation(okBox, true, false),
        "box open/close state is not replaced by the pickup one-shot animation") && ok;
	ok = check(isObjectResourceAnimationFinished(okPickup, oaPlaying, 200, 200),
        "pickup one-shot animation finishes at its exact duration") && ok;
    ok = check(!isObjectResourceAnimationFinished(okPickup, oaPlaying, 199, 200),
        "pickup one-shot animation remains active before its duration") && ok;
	ok = check(!isObjectResourceAnimationFinished(okPickup, oaStay, 200, 200),
		"static pickup state does not repeatedly finish the animation") && ok;
	ok = check(combineObjectActionElapsed(5000, 100, 20) == 5080,
		"persisted object action time continues from the current process clock") && ok;
	ok = check(combineObjectActionElapsed(std::numeric_limits<UTime>::max() - 2, 10, 0)
		== std::numeric_limits<UTime>::max(),
		"object action time saturates instead of wrapping") && ok;

    ok = check(canObjectTrapDamageNpcKind(nkBattle), "trap damages fighter NPCs") && ok;
    ok = check(canObjectTrapDamageNpcKind(nkPartner), "trap damages partner NPCs") && ok;
    ok = check(!canObjectTrapDamageNpcKind(nkPlayer), "trap handles player separately") && ok;
    ok = check(!canObjectTrapDamageNpcKind(nkNormal), "trap ignores normal NPCs") && ok;
    ok = check(!canObjectTrapDamageNpcKind(nkEvent), "trap ignores event NPCs") && ok;
    ok = check(!canObjectTrapDamageNpcKind(nkFlyingAnimal), "trap ignores flying animals") && ok;

    ok = check(getObjectTrapDamageCycle(0, 100) == 0, "trap damage starts at cycle zero") && ok;
    ok = check(isObjectTrapDamageCycleDue(0, 100, OBJECT_TRAP_DAMAGE_CYCLE_UNSET), "trap damage is due on first active frame") && ok;
    ok = check(!isObjectTrapDamageCycleDue(50, 100, 0), "trap damage does not repeat in same cycle") && ok;
    ok = check(isObjectTrapDamageCycleDue(100, 100, 0), "trap damage is due on next cycle") && ok;
    ok = check(getObjectTrapDamageCycle(100, 0) == OBJECT_TRAP_DAMAGE_CYCLE_UNSET, "trap damage ignores zero interval") && ok;

    ok = check(shouldUseObjectRightScriptForPrimaryInteraction("", "right.txt"), "right-only object uses right script for primary/mobile interaction") && ok;
    ok = check(!shouldUseObjectRightScriptForPrimaryInteraction("left.txt", "right.txt"), "left script has priority over right fallback") && ok;
    ok = check(!shouldUseObjectRightScriptForPrimaryInteraction("", ""), "empty scripts do not use right fallback") && ok;
    ok = check(shouldDeferMobileRightScriptChoice("left.txt", "right.txt"), "mobile touch defers only when both primary and right scripts exist") && ok;
    ok = check(!shouldDeferMobileRightScriptChoice("", "right.txt"), "right-only mobile touch keeps immediate right fallback") && ok;
    ok = check(!shouldDeferMobileRightScriptChoice("left.txt", ""), "primary-only mobile touch keeps immediate primary interaction") && ok;
    ok = check(!shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS - 1, 0, 0), "short mobile touch keeps primary interaction") && ok;
    ok = check(shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS, 0, 0), "long mobile touch selects right script") && ok;
    ok = check(!shouldUseMobileRightScript(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS, MOBILE_RIGHT_SCRIPT_MOVE_TOLERANCE_PIXELS + 1, 0),
        "mobile right-script long press rejects moved touch") && ok;
    ok = check(canSelectObjectForInteraction(0, true), "object with a script remains selectable") && ok;
    ok = check(!canSelectObjectForInteraction(1, true), "touch-only object is not selected for ordinary interaction") && ok;
    ok = check(!canSelectObjectForInteraction(0, false), "object without scripts is not selected for ordinary interaction") && ok;
    ok = check(canTriggerObjectTouchScript(1, true), "touch-only object with a primary script auto-triggers") && ok;
    ok = check(!canTriggerObjectTouchScript(0, true), "ordinary scripted object does not auto-trigger by touch-only path") && ok;
    ok = check(!canTriggerObjectTouchScript(1, false), "touch-only object without a primary script does not auto-trigger") && ok;
    ok = check(Object::isInteractDistanceReached(1, 0), "object interaction reaches adjacent target") && ok;
    ok = check(!Object::isInteractDistanceReached(2, 0), "object interaction beyond adjacent target needs direct flag") && ok;
	ok = check(Object::isInteractDistanceReached(20, 1), "CanInteractDirectly object interaction bypasses distance") && ok;
	ok = check(GameController::KeyboardAutoInteractionTileDistance == 13,
		"desktop Q/E auto interaction keeps the three-reference 13-tile search radius") && ok;
	ok = check(WeatherSafety::normalizeRainDropCount(-1) == 0
		&& WeatherSafety::normalizeRainDropCount(1000000) == WeatherSafety::MaximumRainDropCount,
		"custom rain drop count remains within the fixed drop budget") && ok;
	ok = check(WeatherSafety::normalizeRainSpeed(0) == 1
		&& WeatherSafety::normalizeRainSpeed(1000000) == WeatherSafety::MaximumRainSpeed,
		"custom rain speed remains positive and bounded") && ok;
	ok = check(WeatherSafety::normalizeBoltProbability(0) == 1
		&& WeatherSafety::shouldTriggerBolt(50, 0)
		&& !WeatherSafety::shouldTriggerBolt(50, 1),
		"custom BoltProb is a bounded random denominator") && ok;
	ok = check(
		WeatherParticleMotion::selectDepthLayer(0) == WeatherDepthLayer::Far
			&& WeatherParticleMotion::selectDepthLayer(44) == WeatherDepthLayer::Far
			&& WeatherParticleMotion::selectDepthLayer(45) == WeatherDepthLayer::Middle
			&& WeatherParticleMotion::selectDepthLayer(84) == WeatherDepthLayer::Middle
			&& WeatherParticleMotion::selectDepthLayer(85) == WeatherDepthLayer::Near
			&& WeatherParticleMotion::selectDepthLayer(99) == WeatherDepthLayer::Near,
		"weather depth selection reserves 45/40/15 percent for far/middle/near particles") && ok;
	const WeatherLayerStyle farRainStyle =
		WeatherParticleMotion::getRainLayerStyle(WeatherDepthLayer::Far);
	const WeatherLayerStyle middleRainStyle =
		WeatherParticleMotion::getRainLayerStyle(WeatherDepthLayer::Middle);
	const WeatherLayerStyle nearRainStyle =
		WeatherParticleMotion::getRainLayerStyle(WeatherDepthLayer::Near);
	ok = check(
		farRainStyle.minimumVisualLength == 2
			&& farRainStyle.maximumVisualLength == 5
			&& middleRainStyle.minimumVisualLength == 8
			&& middleRainStyle.maximumVisualLength == 18
			&& nearRainStyle.minimumVisualLength == 20
			&& nearRainStyle.maximumVisualLength == 40
			&& farRainStyle.cameraParallax < middleRainStyle.cameraParallax
			&& middleRainStyle.cameraParallax < nearRainStyle.cameraParallax,
		"rain layers use short distant marks and stronger near-camera parallax") && ok;
	const WeatherLayerStyle farSnowStyle =
		WeatherParticleMotion::getSnowLayerStyle(WeatherDepthLayer::Far);
	const WeatherLayerStyle middleSnowStyle =
		WeatherParticleMotion::getSnowLayerStyle(WeatherDepthLayer::Middle);
	const WeatherLayerStyle nearSnowStyle =
		WeatherParticleMotion::getSnowLayerStyle(WeatherDepthLayer::Near);
	ok = check(
		farSnowStyle.minimumVisualLength == 2
			&& farSnowStyle.maximumVisualLength == 2
			&& middleSnowStyle.minimumVisualLength == 3
			&& middleSnowStyle.maximumVisualLength == 3
			&& nearSnowStyle.minimumVisualLength == 5
			&& nearSnowStyle.maximumVisualLength == 5
			&& farSnowStyle.alphaScale < middleSnowStyle.alphaScale
			&& middleSnowStyle.alphaScale < nearSnowStyle.alphaScale,
		"snow layers preserve size and opacity depth cues") && ok;
	ok = check(
		std::abs(WeatherParticleMotion::advanceParticleAxis(
			100.0f,
			0.2f,
			10.0f,
			4.0f,
			0.5f) - 100.0f) < 0.0001f,
		"weather motion combines elapsed-time velocity with opposite camera parallax") && ok;
	const float swayPhase = 0.4f;
	const float firstSwayAdvance = 0.25f;
	const float secondSwayAdvance = 0.17f;
	const float swayAmplitude = 8.0f;
	const float combinedSway = WeatherParticleMotion::calculateSnowSwayDelta(
		swayPhase,
		firstSwayAdvance + secondSwayAdvance,
		swayAmplitude);
	const float splitSway = WeatherParticleMotion::calculateSnowSwayDelta(
		swayPhase,
		firstSwayAdvance,
		swayAmplitude) + WeatherParticleMotion::calculateSnowSwayDelta(
		swayPhase + firstSwayAdvance,
		secondSwayAdvance,
		swayAmplitude);
	ok = check(
		std::abs(combinedSway - splitSway) < 0.0001f,
		"snow sway displacement remains stable when the same elapsed time is split across frames") && ok;
	ok = check(
		std::abs(WeatherParticleMotion::calculateRainStreakAngle(0.0f, 1.0f)) < 0.0001f
			&& WeatherParticleMotion::calculateRainStreakAngle(0.2f, 1.0f) < 0.0f
			&& WeatherParticleMotion::calculateRainStreakAngle(-0.2f, 1.0f) > 0.0f,
		"rain streak angle follows the relative horizontal fall direction") && ok;
	ok = check(WaterEffectSafety::isValidGridSize(1)
		&& WaterEffectSafety::isValidGridSize(WaterEffectSafety::MaximumGridSize)
		&& !WaterEffectSafety::isValidGridSize(0)
		&& !WaterEffectSafety::isValidGridSize(WaterEffectSafety::MaximumGridSize + 1),
		"water grid size rejects zero and excessive allocations") && ok;
	ok = check(WaterEffectSafety::isClickRippleActive(1000, 1000, 1800)
		&& WaterEffectSafety::isClickRippleActive(999, 1000, 1800)
		&& WaterEffectSafety::isClickRippleActive(2799, 1000, 1800)
		&& !WaterEffectSafety::isClickRippleActive(2800, 1000, 1800)
		&& !WaterEffectSafety::isClickRippleActive(1000, 1000, 0),
		"water click ripples use one bounded lifetime before per-vertex work") && ok;

    ok = checkCandidates("box.asf", { "asf/object/box.asf", "mpc/object/box.asf", "mpc/object/box.mpc" },
        "object image simple name searches object ASF/MPC folders") && ok;
    ok = checkCandidates("other/box.asf", { "asf/object/other/box.asf", "mpc/object/other/box.asf", "mpc/object/other/box.mpc" },
        "object image subdirectory remains relative to object folders") && ok;
    ok = checkCandidates("object/box.asf", { "asf/object/box.asf", "mpc/object/box.asf", "mpc/object/box.mpc" },
        "object image category-relative path searches object ASF/MPC folders") && ok;
    ok = checkCandidates("asf/object/box.asf", { "asf/object/box.asf", "mpc/object/box.asf", "mpc/object/box.mpc" },
        "object image explicit ASF path keeps MPC fallback visible") && ok;
    ok = checkCandidates("mpc/object/box.mpc", { "mpc/object/box.mpc", "asf/object/box.mpc", "asf/object/box.asf" },
        "object image explicit MPC path keeps ASF fallback visible") && ok;
    ok = checkCandidates("asf/equip/box.asf", { "asf/equip/box.asf", "mpc/equip/box.asf", "mpc/equip/box.mpc" },
        "object image explicit non-object ASF path uses generic IMP alternate") && ok;
    ok = checkCandidates("/./box.asf", { "asf/object/box.asf", "mpc/object/box.asf", "mpc/object/box.mpc" },
        "object image path normalization strips leading markers") && ok;

    ok = check(buildImageResourceCandidatesForCategory("goods/potion.asf", "goods", GOODS_RES_FOLDER_ASF, GOODS_RES_FOLDER) ==
        std::vector<std::string>({ "asf/goods/potion.asf", "mpc/goods/potion.asf", "mpc/goods/potion.mpc" }),
        "goods category strips category prefix") && ok;
    ok = check(buildImageResourceCandidatesForCategory("mpc/effect/hit.mpc", "effect", EFFECT_RES_FOLDER_ASF, EFFECT_RES_FOLDER) ==
        std::vector<std::string>({ "mpc/effect/hit.mpc", "asf/effect/hit.mpc", "asf/effect/hit.asf" }),
        "effect explicit MPC path keeps ASF fallback visible") && ok;
    ok = check(buildImageResourceCandidatesForCategory("role.asf", "character", NPC_RES_FOLDER_ASF, NPC_RES_FOLDER, { ASF_FOLDER "interlude\\", MPC_FOLDER "interlude\\" }) ==
        std::vector<std::string>({
            "asf/character/role.asf", "mpc/character/role.asf", "mpc/character/role.mpc",
            "asf/interlude/role.asf", "mpc/interlude/role.asf", "mpc/interlude/role.mpc" }),
        "character image candidates include interlude fallback folders") && ok;
    ok = check(buildImageResourceCandidatesForCategory("role", "character", NPC_RES_FOLDER_ASF, NPC_RES_FOLDER, { ASF_FOLDER "interlude\\", MPC_FOLDER "interlude\\" }) ==
        std::vector<std::string>({
            "asf/character/role", "asf/character/role.asf",
            "mpc/character/role", "mpc/character/role.mpc",
            "asf/interlude/role", "asf/interlude/role.asf",
            "mpc/interlude/role", "mpc/interlude/role.mpc" }),
        "extensionless character image candidates include default ASF/MPC package extensions") && ok;
    ok = check(buildImageResourceCandidatesForCategory("icon.png", "magic", MAGIC_RES_FOLDER_ASF, MAGIC_RES_FOLDER) ==
        std::vector<std::string>({
            "asf/magic/icon.png", "asf/magic/icon.asf",
            "mpc/magic/icon.png", "mpc/magic/icon.mpc" }),
        "non-package image extensions keep original path and try package fallback") && ok;

	using ImagePackagePathCandidates::build;
	ok = check(build("asf/ui/dialog/panel.asf") == std::vector<std::string>({
		"asf/ui/dialog/panel.asf", "mpc/ui/dialog/panel.asf", "mpc/ui/dialog/panel.mpc" }),
		"generic IMP candidates switch both package folder and extension") && ok;
	ok = check(build("mpc/ui/dialog/panel.mpc") == std::vector<std::string>({
		"mpc/ui/dialog/panel.mpc", "asf/ui/dialog/panel.mpc", "asf/ui/dialog/panel.asf" }),
		"generic IMP candidates preserve MPC preference before ASF fallback") && ok;
	ok = check(build("asf/ui/icon.png") == std::vector<std::string>({
		"asf/ui/icon.png", "asf/ui/icon.asf", "mpc/ui/icon.png", "mpc/ui/icon.mpc" }),
		"generic IMP candidates replace non-package extensions in each package folder") && ok;
	ok = check(build("asf/map/map001/tile.asf") == std::vector<std::string>({
		"asf/map/map001/tile.asf" }),
		"map package paths retain the existing no-cross-format boundary") && ok;
	ok = check(build("/./asf/ui/dialog/panel.asf").front() == "asf/ui/dialog/panel.asf",
		"generic IMP candidates normalize leading markers and separators") && ok;

    return ok ? 0 : 1;
}
