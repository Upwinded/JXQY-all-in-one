#include "../GameplayAutomation/GameplayAutomationSession.h"
#include "../Engine/Engine.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/Data/NPCAction/NPCActionWalk.h"
#include "../Game/Data/CollisionDetector.h"
#include "../Game/Data/Object.h"
#include "../Game/GameManager/GameController.h"
#include "../Game/Menu/ChooseMenu.h"
#include "../Resource/ResourceManager.h"
#include "../File/File.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "TestTemporaryDirectory.h"
#include <iostream>

#if defined(_WIN32) && defined(JXQY_ENABLE_TEST_HOOKS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace GameplayAutomation;

class GameplayAutomationTestAccess
{
public:
	static void run(const std::filesystem::path& directory)
	{
		auto require = [](bool condition, const char* message)
		{
			if (!condition) throw std::runtime_error(message);
		};
		const auto name = "runtime-test-" + std::to_string(GetCurrentProcessId());
		GameplayAutomationSession session(name, directory);
		GameManager manager;
		auto choice = std::make_shared<ChooseMenu>();
		ChooseMenu::SelectionConfiguration configuration;
		configuration.message = u8"选择测试：中文";
		configuration.options = {u8"可见选项", u8"隐藏选项"};
		configuration.visibleOptions = {true, false};
		require(choice->prepareSelection(configuration), "choice fixture loads");
		choice->setRunning(true);
		HeadlessPhysicalInputTest::ScopedRunningOwner owner(choice);
		session.updateContext();
		auto state = session.observe(object());
		require(member(state, "choices").arrayValues.size() == 1, "hidden choice is not observed");
		require(textMember(state, "choiceMessage") == configuration.message, "Chinese choice text survives snapshot");
		auto requireCheatState = [&](bool enabled, bool invincibility)
		{
			const auto observed = session.observe(object());
			require(boolMember(observed, "cheatModeEnabled") == enabled
				&& boolMember(observed, "cheatInvincibilityEnabled") == invincibility,
				"cheat snapshot reads the actual runtime switches");
		};
		requireCheatState(false, false);
		require(manager.global.data.PartnerCombat && boolMember(session.observe(object()), "partnerCombatEnabled"),
			"partner combat defaults to enabled and Observe reports the actual switch");
		manager.global.data.PartnerCombat = false;
		require(!boolMember(session.observe(object()), "partnerCombatEnabled"),
			"Observe preserves an explicitly disabled partner-combat switch");
		manager.global.data.PartnerCombat = true;
		manager.timerStarted = true;
		manager.timerHidden = true;
		manager.timerSeconds = 45;
		manager.timerAccumulated = 125;
		manager.timeScriptSet = true;
		manager.timeScriptSeconds = 0;
		manager.timeScriptFileName = u8"剑门关之战.txt";
		const auto timer = member(session.observe(object()), "timer");
		require(boolMember(timer, "started") && boolMember(timer, "hidden") && boolMember(timer, "callbackSet")
			&& integerMember(timer, "remainingSeconds", 0, 86400) == 45
			&& integerMember(timer, "accumulatedMilliseconds", 0, 999) == 125
			&& integerMember(timer, "triggerSeconds", 0, 86400) == 0
			&& textMember(timer, "script") == manager.timeScriptFileName,
			"Observe reports the actual hidden countdown and callback without changing them");
		require(manager.timerSeconds == 45 && manager.timerAccumulated == 125 && manager.timeScriptSet,
			"observing a countdown does not advance it or consume its callback");
		manager.timerStarted = manager.timerHidden = manager.timeScriptSet = false;
		manager.timerSeconds = manager.timeScriptSeconds = 0;
		manager.timerAccumulated = 0;
		manager.timeScriptFileName.clear();
		manager.player->invincible = 1;
		requireCheatState(false, false);
		manager.player->invincible = 0;
		manager.setCheatModeEnabled(true);
		requireCheatState(true, false);
		manager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
		requireCheatState(true, true);
		manager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
		requireCheatState(true, false);
		manager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
		manager.setCheatModeEnabled(false);
		requireCheatState(false, false);
		auto command = [&](const char* name, Value arguments)
		{
			GameplayAutomationSession::Action action;
			action.id = session.nextAction++;
			action.command = name;
			action.arguments = std::move(arguments);
			action.generation = session.generation;
			action.started = action.lastProgress = SDL_GetTicks();
			auto& stored = session.actions.emplace(action.id, std::move(action)).first->second;
			try { session.execute(stored); }
			catch (const std::exception& error) { session.finish(stored, "failed", error.what()); }
			return session.actionState(stored);
		};
		auto arguments = object();
		auto options = array();
		options.arrayValues.push_back(number(1));
		arguments.objectValues = {{"context", number(session.context)}, {"options", options}};
		require(textMember(command("Choose", arguments), "reason") == "unavailable_option", "hidden choice is rejected");
		require(choice->getSelection() == -1, "rejected choice does not change selection");
		arguments.objectValues["context"] = number(session.context - 1);
		require(textMember(command("Choose", arguments), "reason") == "stale_ui_context", "stale choice context is rejected");

		// A connected controller with auto-dialogue enabled must leave choices alone.
		const std::string path = "\\\\.\\pipe\\jxqy-" + name;
		HANDLE connection = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		require(connection != INVALID_HANDLE_VALUE, "connect to test pipe");
		for (int i = 0; !session.pipe.isConnected() && i < 100; ++i) SDL_Delay(1);
		session.autoDialogue = true;
		session.presentedContext = session.context;
		for (int i = 0; i < 5; ++i) { SDL_Delay(30); session.tick(); }
		require(choice->getSelection() == -1, "auto dialogue stops at a choice");
		CloseHandle(connection);
		for (int i = 0; session.pipe.isConnected() && i < 100; ++i) SDL_Delay(1);
		session.tick();
		require(!session.autoDialogue, "disconnect disables automatic dialogue");

		const auto targetId = session.identify(choice);
		GameplayAutomationSession::worldChanged();
		bool stale = false;
		try { session.resolve(targetId); } catch (const std::exception&) { stale = true; }
		require(stale, "map replacement invalidates retained IDs");
		std::uint64_t destroyedId = 0;
		{
			auto transient = std::make_shared<Element>();
			destroyedId = session.identify(transient);
		}
		stale = false;
		try { session.resolve(destroyedId); } catch (const std::exception&) { stale = true; }
		require(stale, "destroyed weak target is rejected");
		manager.inThread.store(true);
		state = session.observe(object());
		require(boolMember(state, "loading") && !state.objectValues.count("targets") && !state.objectValues.count("ui"),
			"loading snapshots do not read replaced objects");
		require(!state.objectValues.count("cheatModeEnabled") && !state.objectValues.count("cheatInvincibilityEnabled"),
			"loading snapshots do not report unavailable cheat state");
		manager.inThread.store(false);

		// Instant self cures never enter the magic animation; observe actual use.
		auto magic = std::make_shared<Magic>();
		GameplayAutomationSession::Action cast;
		cast.id = session.nextAction++;
		cast.command = "CastSkill";
		cast.magic = magic;
		session.worldAction = cast.id;
		auto& storedCast = session.actions.emplace(cast.id, std::move(cast)).first->second;
		manager.magicManager.finishMagicUse(std::make_shared<Magic>(), 100, false);
		require(!storedCast.observedExecution, "unrelated skill does not complete the action");
		manager.magicManager.finishMagicUse(magic, 100, false);
		require(storedCast.observedExecution, "normal skill completion covers instant skills");
		session.worldAction = 0;

		// Combat must approach before spending mana on a target beyond flight range.
		manager.map->data = std::make_shared<MapData>();
		manager.map->data->head.width = manager.map->data->head.height = 64;
		manager.map->data->tile.assign(64, std::vector<MapTile>(64));
		manager.map->createDataMap();
		auto player = manager.player;
		player->setPosition({10, 10}, false);
		player->canFight = player->canUseMana = true;
		player->mana = player->life = player->thew = 1000;
		player->res.magic.imagePackage = std::make_shared<IMPImage>();
		player->res.magic.imagePackage->directions = 8;
		player->res.magic.imagePackage->interval = 100;
		player->res.magic.imagePackage->frame.resize(8);
		auto& equippedMagic = manager.magicManager.magicList[manager.magicManager.bottomIndex(0)];
		equippedMagic.iniFile = u8"player-magic-寒霜掌.ini";
		equippedMagic.magic = std::make_shared<Magic>();
		equippedMagic.magic->initFromIni(equippedMagic.iniFile);
		equippedMagic.level = 1;
		require(equippedMagic.magic->loadSucceeded, "combat range fixture loads its skill");
		auto distantTarget = std::make_shared<NPC>();
		distantTarget->setPosition({60, 60}, false);
		require(Map::calDistance(player->getPosition(), distantTarget->getPosition())
			> player->estimatePhysicalReach(*equippedMagic.magic, equippedMagic.level),
			"combat range fixture places its target beyond flight range");
		require(session.queueSkill(0, distantTarget, false) && player->nextAction
			&& player->nextAction->destGE.lock() == distantTarget,
			"normal casting keeps the player's out-of-range request");
		player->nextAction.reset();
		require(!session.queueSkill(0, distantTarget, true) && !player->nextAction && player->mana == 1000,
			"combat refuses an out-of-range cast without spending mana");
		distantTarget->setPosition({11, 10}, false);
		require(session.queueSkill(0, distantTarget, true) && player->nextAction
			&& player->nextAction->destGE.lock() == distantTarget,
			"in-range casting retains the actual target in the normal action queue");
		player->nextAction.reset();

		const auto flightMagic = equippedMagic.magic;
		const auto flightFile = equippedMagic.iniFile;
		for (const auto* file : {u8"player-magic-洗髓经.ini", u8"player-magic-江翻海沸.ini", u8"player-magic-风雪狂刀.ini"})
		{
			equippedMagic.iniFile = file;
			equippedMagic.magic = std::make_shared<Magic>();
			equippedMagic.magic->initFromIni(file);
			equippedMagic.level = 10;
			require(equippedMagic.magic->loadSucceeded, "area combat fixture loads its real skill");
			const bool square = equippedMagic.magic->level[10].region == mrSquare;
			const bool cross = equippedMagic.magic->level[10].region == mrCross;
			distantTarget->setPosition(square ? Point{30, 10} : cross ? Point{13, 16} : Point{15, 10}, false);
			manager.map->data->tile[10][13].obstacle = 0x80;
			require(session.queueSkill(0, distantTarget, true) && player->nextAction
				&& player->nextAction->destGE.lock() == distantTarget && player->mana == 1000,
				"area combat retains the real aim without requiring a projectile path or charging before release");
			player->nextAction.reset();
			const auto effects = Magic::addEffect(equippedMagic.magic, player,
				player->getPosition(), distantTarget->getPosition(), 10, 1, 0, lkSelf, distantTarget);
			require(std::any_of(effects.begin(), effects.end(), [&](const auto& effect)
				{ return effect->position == distantTarget->getPosition(); }),
				"accepted area aim is covered by the actual native effects");
			manager.effectManager->clearEffect();
			if (square)
			{
				distantTarget->setPosition({10 + MAGIC_MAX_CAST_DISTANCE + 1, 10}, false);
				std::string reason;
				require(!session.queueSkill(0, distantTarget, true, &reason)
					&& reason == "skill_out_of_range" && !player->nextAction && player->mana == 1000,
					"square combat approaches before the normal dispatcher clamps a distant aim");
			}
			else
			{
				equippedMagic.level = 1;
				require(!session.queueSkill(0, distantTarget, true) && !player->nextAction,
					"a level-one fixed area cannot cover the level-ten distant aim");
				equippedMagic.level = 10;
				distantTarget->setPosition({20, 10}, false);
				require(!session.queueSkill(0, distantTarget, true) && !player->nextAction,
					"area combat refuses to waste mana outside its actual fixed tiles");
				if (cross)
				{
					distantTarget->setPosition({11, 10}, false);
					require(!session.queueSkill(0, distantTarget, true) && !player->nextAction && player->mana == 1000,
						"cross combat rejects an adjacent enemy outside all four diagonal arms");
					require(session.queueSkill(0, distantTarget, false) && player->nextAction,
						"normal cross casting still accepts the player's aimed request");
					player->nextAction.reset();
				}
			}
			manager.map->data->tile[10][13].obstacle = 0;
		}
		equippedMagic.magic = flightMagic;
		equippedMagic.iniFile = flightFile;
		equippedMagic.level = 1;
		for (int moveKind : { mmkPoint, mmkLine, mmkSummon, mmkTransport, mmkControl, mmkWarningRegion })
		{
			equippedMagic.magic = std::make_shared<Magic>();
			equippedMagic.magic->level[1].moveKind = moveKind;
			equippedMagic.magic->level[1].region = mrRegionFile;
			for (int distance : { MAGIC_MAX_CAST_DISTANCE, MAGIC_MAX_CAST_DISTANCE + 1 })
			{
				distantTarget->setPosition({ 10 + distance, 10 }, false);
				std::string reason;
				const bool queued = session.queueSkill(0, distantTarget, true, &reason);
				require(queued == (distance <= MAGIC_MAX_CAST_DISTANCE)
					&& (queued || reason == "skill_out_of_range") && player->mana == 1000,
					"all position casts use the dispatcher's inclusive aim limit before queueing");
				player->nextAction.reset();
			}
		}
		equippedMagic.magic = flightMagic;
		distantTarget->setPosition({11, 10}, false);

		auto startCombat = [&](const std::shared_ptr<NPC>& target, int kills)
			-> GameplayAutomationSession::Action&
		{
			GameplayAutomationSession::Action combat;
			combat.id = session.nextAction++;
			combat.command = "StartCombat";
			combat.generation = session.generation;
			combat.arguments = object();
			combat.arguments.objectValues["kills"] = number(kills);
			combat.target = target;
			combat.started = combat.lastProgress = SDL_GetTicks();
			session.worldAction = combat.id;
			return session.actions.emplace(combat.id, std::move(combat)).first->second;
		};

		// Use the real player queue and movement actions, with a ready headless frame.
		auto* engine = Engine::getInstance();
		const bool previousFrameReady = engine->currentFrameReady.exchange(true);
		manager.global.data.canInput = true;
		player->canRun = true;
		player->res.stand.imagePackage = player->res.magic.imagePackage;
		player->res.walk.imagePackage = player->res.magic.imagePackage;
		player->res.run.imagePackage = player->res.magic.imagePackage;
		player->res.attack.imagePackage = player->res.magic.imagePackage;
		player->res.hurt.imagePackage = player->res.magic.imagePackage;
		player->res.jump.imagePackage = player->res.magic.imagePackage;
		distantTarget->kind = nkBattle;
		distantTarget->relation = nrHostile;
		distantTarget->isVisibleByVariable = true;
		distantTarget->life = 1000;
		manager.npcManager->npcList = {distantTarget};
		distantTarget->setPosition({13, 10}, false);
		state = session.observe(object());
		require(boolMember(member(state, "targets").arrayValues.front(), "visibleFromPlayer"),
			"NPC snapshot reports a visible enemy in the same corridor");
		manager.map->data->tile[10][11].obstacle = 0x80;
		manager.map->data->tile[10][12].obstacle = 0x80;
		state = session.observe(object());
		require(!boolMember(member(state, "targets").arrayValues.front(), "visibleFromPlayer")
			&& boolMember(member(state, "targets").arrayValues.front(), "attackable"),
			"an occluded enemy remains valid but is not a visible travel threat");
		manager.map->data->tile[10][11].obstacle = 0;
		manager.map->data->tile[10][12].obstacle = 0;
		distantTarget->setPosition({11, 10}, false);
		const int previousAttackRadius = distantTarget->attackRadius;
		for (const int attackRadius : {10, 1})
		{
			distantTarget->attackRadius = attackRadius;
			state = session.observe(object());
			const auto& observedTarget = member(state, "targets").arrayValues.front();
			require(integerMember(observedTarget, "attackRadius", 0, 100) == attackRadius,
				"NPC snapshot exposes the current native attack radius");
		}
		distantTarget->attackRadius = previousAttackRadius;
		const auto previousWalkImage = distantTarget->res.walk.imagePackage;
		const auto previousAlternateWalkImage = distantTarget->res.awalk.imagePackage;
		distantTarget->res.walk.imagePackage.reset();
		distantTarget->res.awalk.imagePackage.reset();
		state = session.observe(object());
		require(!boolMember(member(state, "targets").arrayValues.front(), "hasWalkAction"),
			"NPC without either walking animation is observed as stationary");
		for (auto* walkingAction : {&distantTarget->res.walk, &distantTarget->res.awalk})
		{
			walkingAction->imagePackage = player->res.walk.imagePackage;
			state = session.observe(object());
			require(boolMember(member(state, "targets").arrayValues.front(), "hasWalkAction"),
				"NPC snapshot recognizes either normal or alternate walking animation");
			walkingAction->imagePackage.reset();
		}
		distantTarget->res.walk.imagePackage = previousWalkImage;
		distantTarget->res.awalk.imagePackage = previousAlternateWalkImage;
		require(session.worldInputAllowed(), "combat fixture permits normal world input");
		// Legacy combat NPCs can omit Life while retaining normal movement and attacks.
		const auto previousAttackMagic = player->npcMagic;
		const auto previousAttackOptions = player->attackOptions;
		const int previousEvade = player->evade;
		const auto previousDeathImage = distantTarget->res.death.imagePackage;
		player->npcMagic = manager.magicManager.loadAttackMagic(u8"player-magic-长剑.ini");
		player->rebuildAttackOptions();
		player->evade = distantTarget->getEvade() + 1;
		distantTarget->res.death.imagePackage = player->res.attack.imagePackage;
		distantTarget->life = 0;
		state = session.observe(object());
		require(boolMember(member(state, "targets").arrayValues.front(), "attackable")
			&& integerMember(member(state, "targets").arrayValues.front(), "life", 0, 1000) == 0,
			"zero-life hostile NPC remains attackable under the native interaction rules");
		auto& zeroLifeCombat = startCombat(distantTarget, 1);
		session.updateWorldAction();
		require(zeroLifeCombat.status == "running" && zeroLifeCombat.kills == 0 && player->nextAction
			&& player->nextAction->destGE.lock() == distantTarget,
			"combat submits a normal attack instead of waiting for an alive zero-life target to die");
		player->onUpdate();
		require(player->isAttacking() && player->hasPreparedAttackMagic,
			"targeted ordinary attack prepares real long-sword damage for an alive zero-life NPC");
		player->setTime(player->actionBeginTime + player->actionLastTime + 1);
		player->actionManager->update(0);
		require(!manager.effectManager->effectList.empty()
			&& manager.effectManager->effectList.back()->magic.iniName == player->npcMagic->iniName,
			"ordinary attack animation releases its actual weapon effect");
		manager.effectManager->onUpdate();
		require(distantTarget->isDying() && zeroLifeCombat.kills == 0,
			"normal collision kills the zero-life fighter before any automation defeat count");
		state = session.observe(object());
		require(!boolMember(member(state, "targets").arrayValues.front(), "attackable"),
			"actual dying NPC is not reported as attackable");
		distantTarget->setTime(distantTarget->actionBeginTime + distantTarget->actionLastTime + 1);
		distantTarget->actionManager->update(0);
		manager.npcManager->onUpdate();
		require(zeroLifeCombat.status == "succeeded" && zeroLifeCombat.kills == 1,
			"normal death completion records exactly one defeated zero-life fighter");
		manager.npcManager->onUpdate();
		require(zeroLifeCombat.kills == 1, "repeated NPC updates cannot count the same death again");
		player->npcMagic = previousAttackMagic;
		player->attackOptions = previousAttackOptions;
		player->evade = previousEvade;
		distantTarget->res.death.imagePackage = previousDeathImage;
		distantTarget->result = 0;
		distantTarget->actionManager->resetActionIgnoringTransitions(acStand);
		manager.npcManager->npcList = {distantTarget};
		distantTarget->addToDataMap();
		distantTarget->relation = nrFriendly;
		state = session.observe(object());
		require(!boolMember(member(state, "targets").arrayValues.front(), "attackable"),
			"friendly NPC is not reported as a player attack target");
		distantTarget->relation = nrHostile;
		distantTarget->life = 1000;
		player->life = 0;
		require(!distantTarget->isTargetValid(player) && !distantTarget->isCombatTargetValid(player),
			"zero-life player remains invalid even while standing after a story defeat");
		player->life = 1000;
		require(distantTarget->isTargetValid(player) && distantTarget->isCombatTargetValid(player),
			"restored player life permits normal combat targeting again");
		// The explicitly requested first target must not be replaced after its
		// last strong reference is released, even when another enemy is nearby.
		auto destroyedCombatTarget = std::make_shared<NPC>();
		destroyedCombatTarget->npcName = "automation-destroyed-combat-target";
		destroyedCombatTarget->kind = nkBattle;
		destroyedCombatTarget->relation = nrHostile;
		destroyedCombatTarget->life = 100;
		destroyedCombatTarget->setPosition({12, 10}, false);
		manager.npcManager->npcList.push_back(destroyedCombatTarget);
		auto& destroyedCombat = startCombat(destroyedCombatTarget, 1);
		destroyedCombat.arguments.objectValues["targetId"] = number(session.identify(destroyedCombatTarget));
		const std::weak_ptr<NPC> destroyedCombatWeak = destroyedCombatTarget;
		manager.npcManager->deleteNPC(destroyedCombatTarget->npcName);
		destroyedCombatTarget.reset();
		require(destroyedCombatWeak.expired(), "explicit combat target really loses its final strong reference");
		session.updateWorldAction();
		require(destroyedCombat.status == "failed" && destroyedCombat.reason == "target_unavailable"
			&& destroyedCombat.kills == 0 && !player->nextAction && destroyedCombat.target.expired(),
			"destroyed explicit target fails without silently attacking the remaining live enemy");
		for (const bool running : {false, true})
		for (const bool steppingIn : {false, true})
		for (const bool cancelBeforeCast : {false, true})
		{
			player->actionManager->resetActionIgnoringTransitions(acStand);
			player->setPosition({10, 10}, false);
			distantTarget->setPosition({30, 10}, false);
			player->mana = 0;
			auto& combat = startCombat(distantTarget, 1);
			combat.arguments.objectValues["skills"] = array();
			combat.arguments.objectValues["skills"].arrayValues.push_back(number(0));
			if (running)
				require(manager.queueNPCAttackInteraction(distantTarget, true), "normal running attack pursuit queues");
			else
				session.updateWorldAction();
			require(player->nextAction && player->nextAction->destKind == ndAttack,
				"standing combat falls back to a normal attack pursuit");
			player->onUpdate();
			require((running ? player->isRunning() : player->isWalking())
				&& !player->nextAction && player->nextDest == ndAttack
				&& player->nextDestStrictWorldInteraction, "normal attack queue starts strict pursuit");
			const Point arrival = player->stepList.front();
			const UTime arrivalTime = player->stepBeginTime + player->stepLastTime * 2;
			player->setTime(player->stepBeginTime + player->stepLastTime / 2
				+ (steppingIn ? player->stepLastTime : 0));
			player->onUpdate();
			require(player->offset != PointEx{0, 0}, "pursuit fixture is between tile centers");
			const auto revision = player->actionManager->getActionRevision();
			const auto steps = player->stepList;
			for (int i = 0; i < 3; ++i) session.updateWorldAction();
			require(!player->nextAction && player->nextDest == ndAttack
				&& player->actionManager->getActionRevision() == revision && player->stepList == steps,
				"unaffordable skill preserves ongoing pursuit without resetting its movement");
			player->mana = 1000;
			equippedMagic.remainColdMilliseconds = 100;
			session.updateWorldAction();
			require(!player->nextAction && player->actionManager->getActionRevision() == revision,
				"cooling skill leaves pursuit intact");
			equippedMagic.remainColdMilliseconds = 0;
			distantTarget->setPosition({60, 60}, false);
			session.updateWorldAction();
			require(!player->nextAction && player->actionManager->getActionRevision() == revision,
				"out-of-range skill leaves pursuit intact");
			distantTarget->setPosition({30, 10}, false);
			const auto movementOffset = player->offset;
			if (cancelBeforeCast)
			{
				session.cancelWorld("client_cancelled");
				require((running ? player->isRunning() : player->isWalking())
					&& player->offset == movementOffset && player->stepList.size() == 1
					&& player->stepList.front() == arrival && !player->nextAction && player->nextDest == ndNone,
					"cancelling a route preserves its current tile step and discards only the later path");
				auto& replacementCombat = startCombat(distantTarget, 1);
				replacementCombat.arguments.objectValues["skills"] = array();
				replacementCombat.arguments.objectValues["skills"].arrayValues.push_back(number(0));
			}
			session.updateWorldAction();
			require((running ? player->isRunning() : player->isWalking())
				&& player->offset == movementOffset && player->nextDest == ndNone && player->nextAction
				&& player->nextAction->action == acMagic && player->nextAction->destGE.lock() == distantTarget,
				"an available ranged skill replaces pursuit without interrupting the current tile step");
			const auto queuedSkill = player->nextAction;
			session.updateWorldAction();
			require(player->nextAction == queuedSkill, "accepted skill is not queued again before execution");
			player->setTime(arrivalTime - 1);
			player->onUpdate();
			require((running ? player->isRunning() : player->isWalking())
				&& player->nextAction == queuedSkill && player->mana == 1000,
				"queued magic waits through both halves of the tile step without spending mana");
			player->setTime(arrivalTime);
			player->onUpdate();
			require(player->isMagicing() && !player->nextAction && player->destGE.lock() == distantTarget
				&& player->getPosition() == arrival && player->offset == PointEx{0, 0},
				"normal player update enters the targeted magic action exactly at the next tile center");
		}
		for (const auto action : {acAttack, acJump, acMagic})
		{
			player->actionManager->resetActionIgnoringTransitions(action);
			require(player->actionManager->getCurrentActionType() == action,
				"fixture enters the protected action");
			const auto revision = player->actionManager->getActionRevision();
			session.updateWorldAction();
			require(!player->nextAction && player->actionManager->getActionRevision() == revision,
				"combat does not replace attack, jump, or magic actions");
		}
		player->actionManager->resetActionIgnoringTransitions(acStand);
		for (const bool useCheatInvincibility : {false, true})
		{
			player->invincible = useCheatInvincibility ? 0 : 1;
			if (useCheatInvincibility)
			{
				manager.setCheatModeEnabled(true);
				manager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
			}
			const auto revision = player->actionManager->getActionRevision();
			const int direction = player->direction;
			player->beginHurt({11, 10});
			player->NPC::beginHurt();
			require(!player->isHurting() && player->direction == direction
				&& player->actionManager->getActionRevision() == revision && !player->resumingMove,
				"both player invincibility sources reject directed and script hurt without action side effects");
			manager.setCheatModeEnabled(false);
		}
		player->invincible = 0;
		auto invincibleHurtTarget = std::make_shared<NPC>();
		invincibleHurtTarget->res.hurt.imagePackage = player->res.hurt.imagePackage;
		invincibleHurtTarget->res.stand.imagePackage = player->res.stand.imagePackage;
		invincibleHurtTarget->invincible = 1;
		const auto npcHurtRevision = invincibleHurtTarget->actionManager->getActionRevision();
		const int npcHurtDirection = invincibleHurtTarget->direction;
		invincibleHurtTarget->beginHurt({11, 10});
		invincibleHurtTarget->beginHurt();
		require(!invincibleHurtTarget->isHurting() && invincibleHurtTarget->direction == npcHurtDirection
			&& invincibleHurtTarget->actionManager->getActionRevision() == npcHurtRevision,
			"an invincible NPC rejects both hurt entry points without changing direction or action");
		invincibleHurtTarget->invincible = 0;
		invincibleHurtTarget->beginHurt();
		require(invincibleHurtTarget->isHurting(), "disabling NPC invincibility restores native hurt");
		player->beginHurt({11, 10});
		require(player->isHurting(), "normal hurt action starts before queued combat casting");
		const auto hurtRevision = player->actionManager->getActionRevision();
		const int manaBeforeHurtQueue = player->mana;
		session.updateWorldAction();
		const auto hurtQueuedSkill = player->nextAction;
		require(hurtQueuedSkill && hurtQueuedSkill->action == acMagic
			&& hurtQueuedSkill->destGE.lock() == distantTarget && player->isHurting()
			&& player->actionManager->getActionRevision() == hurtRevision && player->mana == manaBeforeHurtQueue,
			"combat prequeues a targeted skill during hurt without changing the hurt action or spending mana");
		session.updateWorldAction();
		require(player->nextAction == hurtQueuedSkill, "hurt recovery retains exactly one queued skill");
		player->setTime(player->actionBeginTime);
		player->onUpdate();
		require(player->isHurting() && player->nextAction == hurtQueuedSkill,
			"native player update leaves the queued skill pending before hurt recovery ends");
		player->setTime(player->actionBeginTime + player->actionLastTime + 1);
		player->onUpdate();
		require(player->isMagicing() && !player->nextAction && player->destGE.lock() == distantTarget
			&& player->mana == manaBeforeHurtQueue,
			"native hurt completion consumes the skill queue in the same player update without charging before release");
		session.updateWorldAction();
		require(!player->nextAction, "combat does not prequeue another skill during the active cast");
		player->beginHurt({11, 10});
		require(player->isHurting() && !player->nextAction && player->mana == manaBeforeHurtQueue,
			"a later native hit can still interrupt the started cast before release without consuming mana");
		session.cancelWorld("test_complete");
		player->actionManager->resetActionIgnoringTransitions(acStand);
		player->nextAction.reset();
		player->setPosition({10, 10}, false);
		distantTarget->setPosition({60, 60}, false);
		player->mana = 0;
		auto& strictEmptyCombat = startCombat(distantTarget, 1);
		strictEmptyCombat.arguments.objectValues["skills"] = array();
		strictEmptyCombat.arguments.objectValues["skills"].arrayValues.push_back(number(0));
		strictEmptyCombat.arguments.objectValues["allowMeleeFallback"] = GameplayAutomation::boolean(false);
		session.updateWorldAction();
		require(strictEmptyCombat.reason == "skill_resources_unavailable" && !player->nextAction
			&& player->nextDest == ndNone, "mandatory ranged combat fails rather than chasing with no mana");
		player->mana = 1000;
		auto& rangedCombat = startCombat(distantTarget, 1);
		rangedCombat.arguments = strictEmptyCombat.arguments;
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->action == acWalk
			&& player->nextAction->destKind == ndNone && player->nextAction->destGE.expired(),
			"mandatory ranged approach queues movement without an attack interaction");
		require(Map::calDistance(player->nextAction->dest, distantTarget->getPosition())
			> player->attackRadius, "ranged approach stops outside ordinary melee reach");
		player->onUpdate();
		require(player->isWalking() && player->nextDest == ndNone, "mandatory ranged approach uses normal walking");
		distantTarget->setPosition({11, 10}, false);
		equippedMagic.remainColdMilliseconds = 100;
		session.updateWorldAction();
		require(!player->nextAction && rangedCombat.status == "running" && player->nextDest == ndNone,
			"mandatory ranged combat waits out cooldown without queuing melee");
		equippedMagic.remainColdMilliseconds = 0;
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->action == acMagic
			&& player->nextAction->destGE.lock() == distantTarget,
			"mandatory ranged approach hands over to a targeted skill as soon as it can hit");
		session.cancelWorld("test_complete");
		player->actionManager->resetActionIgnoringTransitions(acStand);
		player->setPosition({10, 10}, false);
		distantTarget->setPosition({60, 60}, false);
		player->mana = 1000;
		auto& interruptedApproach = startCombat(distantTarget, 1);
		interruptedApproach.arguments = strictEmptyCombat.arguments;
		session.updateWorldAction();
		player->onUpdate();
		require(player->isWalking(), "resource failure fixture starts a ranged approach");
		const auto interruptedArrival = player->stepList.front();
		const auto interruptedArrivalTime = player->stepBeginTime + player->stepLastTime * 2;
		player->setTime(player->stepBeginTime + player->stepLastTime / 2);
		player->onUpdate();
		const auto interruptedOffset = player->offset;
		player->mana = 0;
		session.updateWorldAction();
		require(interruptedApproach.reason == "skill_resources_unavailable" && player->isWalking()
			&& player->stepList.size() == 1 && player->offset == interruptedOffset
			&& !player->nextAction && player->nextDest == ndNone,
			"failed mandatory ranged combat finishes only its current tile step");
		player->setTime(interruptedArrivalTime);
		player->onUpdate();
		require(player->isStanding() && player->stepList.empty()
			&& player->getPosition() == interruptedArrival && player->offset == PointEx{0, 0},
			"failed combat stops at the next tile center without continuing its chase");

		// Recorded Bieli forest cast: the caster's visibility line is clear, but
		// the forward projectile spawn changes its slope and crosses a tree.
		const auto previousMapData = manager.map->data;
		manager.map->data = std::make_shared<MapData>();
		manager.map->data->head.width = 100;
		manager.map->data->head.height = 64;
		manager.map->data->tile.assign(64, std::vector<MapTile>(100));
		manager.map->data->tile[18][72].obstacle = 0x80;
		manager.map->createDataMap();
		player->setPosition({75, 14}, false);
		distantTarget->setPosition({71, 18}, false);
		player->mana = 1000;
		equippedMagic.level = 7;
		require(manager.map->canSee(player->getPosition(), distantTarget->getPosition()),
			"recorded forest positions pass normal character visibility");
		auto releaseProjectile = [&]()
		{
			return Magic::addFlyEffect(equippedMagic.magic, player, player->getPosition(),
				distantTarget->getPosition(), equippedMagic.level, 1, 0, lkSelf).front();
		};
		auto blockedProjectile = releaseProjectile();
		require(blockedProjectile->src == Point{74, 15}, "normal cold-palm release starts one tile ahead");
		const auto blockedPath = blockedProjectile->getPassPath(blockedProjectile->src, {0, 0},
			distantTarget->getPosition(), {0, 0});
		require(std::find(blockedPath.begin(), blockedPath.end(), Point{72, 18}) != blockedPath.end()
			&& !manager.map->canFly({72, 18}), "actual projectile sweep crosses the recorded tree tile");
		std::string blockedSkillReason;
		require(!session.queueSkill(0, distantTarget, true, &blockedSkillReason)
			&& blockedSkillReason == "skill_out_of_range" && !player->nextAction && player->mana == 1000,
			"combat refuses the visible but blocked shot without spending mana");
		require(session.queueSkill(0, distantTarget, false) && player->nextAction,
			"explicit ordinary casting retains its existing admission rules");
		player->nextAction.reset();
		auto& forestCombat = startCombat(distantTarget, 1);
		forestCombat.arguments = strictEmptyCombat.arguments;
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->action == acWalk && player->nextAction->destKind == ndNone,
			"blocked ranged combat first queues normal movement to a clear firing tile");
		const Point firingPosition = player->nextAction->dest;
		player->onUpdate();
		player->setPosition(firingPosition);
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->action == acMagic
			&& player->nextAction->destGE.lock() == distantTarget,
			"the clear firing tile permits the same targeted skill");
		auto clearProjectile = releaseProjectile();
		const auto clearPath = clearProjectile->getPassPath(clearProjectile->src, {0, 0},
			distantTarget->getPosition(), {0, 0});
		require(manager.map->canFly(clearProjectile->src)
			&& std::all_of(clearPath.begin(), clearPath.end(), [&](Point point) { return manager.map->canFly(point); }),
			"the replacement firing tile has an unobstructed native projectile sweep");
		session.cancelWorld("test_complete");
		manager.effectManager->clearEffect();

		// Recorded Jin camp shot: Tianyi's central sector ray immediately enters
		// a low-bit tile that permits sight but blocks normal projectiles.
		const auto previousEquippedMagic = equippedMagic;
		equippedMagic.iniFile = u8"player-magic-天意剑诀.ini";
		equippedMagic.magic = std::make_shared<Magic>();
		equippedMagic.magic->initFromIni(equippedMagic.iniFile);
		equippedMagic.level = 6;
		require(equippedMagic.magic->loadSucceeded, "sector regression loads the actual Tianyi skill");
		manager.map->data = std::make_shared<MapData>();
		manager.map->data->head.width = 100;
		manager.map->data->head.height = 200;
		manager.map->data->tile.assign(200, std::vector<MapTile>(100));
		manager.map->data->tile[135][35].obstacle = 0x02;
		manager.map->createDataMap();
		player->setPosition({36, 136}, false);
		distantTarget->setPosition({31, 134}, false);
		player->mana = 1000;
		require(manager.map->canSee(player->getPosition(), distantTarget->getPosition())
			&& !manager.map->canFly({35, 135}), "recorded Jin camp tile is visible but blocks magic");
		for (const auto moveKind : {mmkSector, mmkRandSector})
		{
			equippedMagic.magic->level[6].moveKind = moveKind;
			auto projectiles = Magic::addSectorEffect(equippedMagic.magic, player, player->getPosition(),
				distantTarget->getPosition(), 6, 1, 0, lkSelf, moveKind == mmkRandSector);
			require(projectiles.size() == 5 && projectiles[2]->src == player->getPosition(),
				"normal and random sector releases start their aimed projectile on the caster's tile");
			const auto sectorPath = projectiles[2]->getPassPath(projectiles[2]->src, {0, 0},
				distantTarget->getPosition(), {0, 0});
			require(std::find(sectorPath.begin(), sectorPath.end(), Point{35, 135}) != sectorPath.end(),
				"the native central sector projectile crosses the recorded blocking tile");
			require(!session.queueSkill(0, distantTarget, true, &blockedSkillReason)
				&& blockedSkillReason == "skill_out_of_range" && !player->nextAction && player->mana == 1000,
				"combat rejects a blocked sector shot without spending mana");
			manager.map->data->tile[135][35].obstacle = 0;
			require(session.queueSkill(0, distantTarget, true) && player->nextAction
				&& player->nextAction->destGE.lock() == distantTarget,
				"a clear sector shot retains the selected target");
			player->nextAction.reset();
			manager.map->data->tile[134][31].obstacle = 0x02;
			require(session.queueSkill(0, distantTarget, true) && player->nextAction
				&& player->nextAction->destGE.lock() == distantTarget,
				"sector admission permits native NPC collision before its own tile obstacle");
			player->nextAction.reset();
			manager.map->data->tile[134][31].obstacle = 0;
			manager.map->data->tile[135][35].obstacle = 0x02;
			manager.effectManager->clearEffect();
		}

		// Xuanci stands on a 0x02 pole in the actual Zhongdu map. The recorded
		// normal Tianyi cast hits him before collision considers that tile's wall.
		const auto previousMapImages = manager.map->mapMpc;
		{
			std::unique_ptr<char[]> mapBytes, npcBytes;
			int mapLength = 0, npcLength = 0;
			require(File::readActiveResourceFile(u8"map/中都.map", mapBytes, mapLength, MapSafety::MaximumFileBytes)
				&& manager.map->load(mapBytes, mapLength)
				&& File::readActiveResourceFile("ini/save/zhongdu.npc", npcBytes, npcLength),
				"Xuanci collision regression loads the actual map and NPC table");
			INIReader npcFixture(npcBytes);
			auto xuanci = std::make_shared<NPC>();
			xuanci->initFromIni(&npcFixture, "npc137");
			xuanci->relation = nrHostile;
			manager.npcManager->npcList = {xuanci};
			manager.map->createDataMap();
			player->actionManager->resetActionIgnoringTransitions(acStand);
			player->setPosition({129, 168}, false);
			player->mana = player->thew = 1000;
			equippedMagic.level = 9;
			require(xuanci->npcName == u8"玄慈" && xuanci->getPosition() == Point{132, 162}
				&& xuanci->life == 5120 && manager.map->data->tile[162][132].obstacle == 0x02
				&& !manager.map->canFly(xuanci->getPosition())
				&& manager.map->findPath(player->getPosition(), xuanci->getPosition()).empty(),
				"real Xuanci position blocks projectile terrain and cannot be reached by walking from the pole");
			require(session.queueSkill(0, xuanci, true) && player->nextAction
				&& player->nextAction->destGE.lock() == xuanci && player->mana == 1000,
				"combat admits the recorded native cast without changing position or spending mana in admission");
			player->nextAction.reset();
			auto releaseXuanciShot = [&]()
			{
				auto projectiles = Magic::addSectorEffect(equippedMagic.magic, player, player->getPosition(),
					xuanci->getPosition(), 9, equippedMagic.magic->level[9].effect, xuanci->getEvade() + 1, lkSelf, true);
				require(projectiles.size() == 7, "level-nine Tianyi creates its seven native projectiles");
				auto aimed = projectiles[3];
				for (const auto& projectile : projectiles)
					if (projectile != aimed) manager.effectManager->deleteEffect(projectile);
				// Exercise the native width sweep and NPC-before-wall collision for
				// the released central ray, with one deterministic movement segment.
				aimed->doing = ekFlying;
				aimed->collisionSweepStartPosition = aimed->src;
				aimed->collisionSweepStartOffset = {0, 0};
				aimed->collisionSweepInitialized = true;
				aimed->position = xuanci->getPosition();
				aimed->offset = {0, 0};
				aimed->passPath = aimed->getPassPath(aimed->src, {0, 0}, aimed->position, {0, 0});
				return aimed;
			};
			auto aimed = releaseXuanciShot();
			CollisionDetector::detectCollision();
			require(xuanci->life < 5120 && aimed->doing != ekFlying,
				"native projectile collision damages Xuanci before the obstacle on his pole");
			const int lifeAfterHit = xuanci->life;
			manager.effectManager->clearEffect();
			aimed = releaseXuanciShot();
			const auto intermediate = std::find_if(aimed->passPath.begin(), aimed->passPath.end(),
				[&](Point point) { return point != aimed->src && point != xuanci->getPosition(); });
			require(intermediate != aimed->passPath.end(), "pole shot has an intermediate projectile tile");
			const auto obstruction = *intermediate;
			const auto previousObstacle = manager.map->data->tile[obstruction.y][obstruction.x].obstacle;
			manager.map->data->tile[obstruction.y][obstruction.x].obstacle = 0x02;
			require(!session.queueSkill(0, xuanci, true) && !player->nextAction,
				"allowing the occupied destination never permits an intermediate projectile obstacle");
			CollisionDetector::detectCollision();
			require(xuanci->life == lifeAfterHit && aimed->doing != ekFlying,
				"native intermediate wall still stops the shot before Xuanci receives damage");
			manager.effectManager->clearEffect();
			manager.map->data->tile[obstruction.y][obstruction.x].obstacle = previousObstacle;
			manager.map->data->tile[168][129].obstacle = 0x02;
			require(!session.queueSkill(0, xuanci, true) && !player->nextAction,
				"occupied destination exception does not bypass a blocked projectile birth tile");
			manager.map->data->tile[168][129].obstacle = 0;
			manager.npcManager->npcList.clear();
			require(!session.queueSkill(0, xuanci, true) && !player->nextAction,
				"unregistered target cannot bypass its tile obstacle");
			manager.npcManager->npcList = {distantTarget};
		}

		// The mine's shortest approach stays behind (13,125), although a side
		// firing position is reachable. Load the original terrain for this case.
		std::unique_ptr<char[]> mineBytes;
		int mineLength = 0;
		require(File::readActiveResourceFile(u8"map/矿山.map", mineBytes, mineLength, MapSafety::MaximumFileBytes)
			&& manager.map->load(mineBytes, mineLength), "mine firing-position regression loads real terrain");
		manager.map->createDataMap();
		player->setPosition({9, 127}, false);
		distantTarget->setPosition({14, 125}, false);
		player->mana = player->thew = 1000;
		equippedMagic.level = 7;
		require(manager.map->data->tile[125][13].obstacle == 0x01
			&& manager.map->canFly(distantTarget->getPosition()),
			"mine blocker is intermediate; the target tile already permits magic");
		auto mineProjectilePath = [&](Point from)
		{
			const auto effects = Magic::addSectorEffect(equippedMagic.magic, player, from,
				distantTarget->getPosition(), 7, 1, 0, lkSelf, true);
			require(effects.size() == 7, "level-seven Tianyi releases seven native projectiles");
			const auto path = effects[3]->getPassPath(from, {0, 0}, distantTarget->getPosition(), {0, 0});
			for (const auto& effect : effects) manager.effectManager->deleteEffect(effect);
			return path;
		};
		const auto mineDirectPath = manager.map->findPath(player->getPosition(), distantTarget->getPosition());
		require(!mineDirectPath.empty(), "mine target has an ordinary approach path");
		for (const auto point : mineDirectPath)
		{
			if (!manager.map->canWalk(point)) continue;
			const auto projectilePath = mineProjectilePath(point);
			require(std::any_of(projectilePath.begin(), projectilePath.end(), [&](Point tile)
				{ return !manager.map->canFly(tile); }),
				"every walkable tile on the shortest mine path has a blocked native shot");
		}
		auto& mineCombat = startCombat(distantTarget, 1);
		mineCombat.arguments = strictEmptyCombat.arguments;
		session.updateWorldAction();
		require(mineCombat.status == "running" && player->nextAction
			&& player->nextAction->action == acWalk && player->nextAction->destKind == ndNone
			&& player->mana == 1000, "blocked shortest approach queues a normal sideward walk without casting");
		const Point mineFiringPosition = player->nextAction->dest;
		require(std::find(mineDirectPath.begin(), mineDirectPath.end(), mineFiringPosition) == mineDirectPath.end(),
			"mine firing position comes from outside the unsuccessful shortest path");
		const auto mineClearPath = mineProjectilePath(mineFiringPosition);
		require(manager.map->canFly(mineFiringPosition)
			&& std::all_of(mineClearPath.begin(), mineClearPath.end(), [&](Point tile) { return manager.map->canFly(tile); }),
			"selected mine side position has a clear native projectile width sweep");
		player->onUpdate();
		require(player->isWalking(), "mine side approach starts the real walking action");
		const auto mineMoveRevision = player->actionManager->getActionRevision();
		const auto mineMoveSteps = player->stepList;
		session.updateWorldAction();
		require(!player->nextAction && player->stepList == mineMoveSteps
			&& player->actionManager->getActionRevision() == mineMoveRevision,
			"ongoing side approach is not replaced by another position search");
		for (int i = 0; player->isWalking() && i < 64; ++i)
		{
			player->setTime(player->getTime() + player->stepLastTime + 1);
			player->actionManager->update(0);
		}
		require(player->isStanding() && player->getPosition() == mineFiringPosition && player->mana == 1000,
			"normal movement reaches the side firing position without teleportation or mana use");
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->action == acMagic
			&& player->nextAction->destGE.lock() == distantTarget,
			"clear side position queues Tianyi against the original target");
		session.cancelWorld("test_complete");
		manager.map->mapMpc = previousMapImages;
		equippedMagic = previousEquippedMagic;
		equippedMagic.level = 1;
		manager.map->data = previousMapData;
		manager.map->createDataMap();
		player->setPosition({2, 32}, false);
		distantTarget->setPosition({60, 32}, false);
		player->mana = 1000;
		for (auto& row : manager.map->data->tile) row[58].obstacle = 0x40;
		require(manager.map->findPath(player->getPosition(), distantTarget->getPosition()).empty()
			&& manager.map->canSee(player->getPosition(), distantTarget->getPosition()),
			"water fixture blocks walking to the target but allows projectiles and sight");
		auto& acrossWaterCombat = startCombat(distantTarget, 1);
		acrossWaterCombat.arguments = strictEmptyCombat.arguments;
		session.updateWorldAction();
		require(acrossWaterCombat.status == "running" && player->nextAction
			&& player->nextAction->action == acWalk && player->nextAction->destKind == ndNone
			&& player->nextAction->dest.x < 58
			&& manager.map->canSee(player->nextAction->dest, distantTarget->getPosition())
			&& Map::calDistance(player->nextAction->dest, distantTarget->getPosition())
				<= player->estimatePhysicalReach(*equippedMagic.magic, equippedMagic.level),
			"mandatory ranged approach reaches a firing tile on this side of an uncrossable barrier");
		session.cancelWorld("test_complete");
		for (auto& row : manager.map->data->tile) row[58].obstacle = 0x80;
		auto& blockedCombat = startCombat(distantTarget, 1);
		blockedCombat.arguments = strictEmptyCombat.arguments;
		session.updateWorldAction();
		require(blockedCombat.reason == "target_unreachable" && player->isStanding()
			&& player->stepList.empty() && !player->nextAction,
			"an opaque disconnected barrier fails without queuing an unreachable approach");
		for (auto& row : manager.map->data->tile) row[58].obstacle = 0;
		player->setPosition({10, 10}, false);
		player->mana = 0;

		// A configured potion that is cooling down can fund the next normal cast.
		// Keep waiting without a melee fallback, then consume the real inventory item.
		player->manaMax = 1000;
		player->calInfo();
		const std::string manaMedicine = u8"goods-yaowu-1-凝神丹.ini";
		require(manager.goodsManager.addItem(manaMedicine, 2), "mana medicine fixture loads");
		int medicineSlot = -1;
		for (int i = 0; i < manager.goodsManager.listLength(); ++i)
			if (manager.goodsManager.goodsList[i].iniFile == manaMedicine) { medicineSlot = i; break; }
		require(medicineSlot >= 0, "mana medicine fixture occupies inventory");
		manager.goodsManager.goodsList[medicineSlot].remainColdMilliseconds = 100;
		distantTarget->setPosition({11, 10}, false);
		auto& waitingCombat = startCombat(distantTarget, 1);
		waitingCombat.arguments = strictEmptyCombat.arguments;
		waitingCombat.arguments.objectValues["manaItem"] = string(manaMedicine);
		waitingCombat.arguments.objectValues["manaPercent"] = number(10);
		session.updateWorldAction();
		require(waitingCombat.status == "running" && !player->nextAction && player->isStanding()
			&& player->mana == 0 && manager.goodsManager.goodsList[medicineSlot].number == 2,
			"mandatory ranged combat waits for its configured mana medicine to cool");
		manager.goodsManager.goodsList[medicineSlot].remainColdMilliseconds = 0;
		session.updateWorldAction();
		require(waitingCombat.status == "running" && player->mana == 160
			&& manager.goodsManager.goodsList[medicineSlot].number == 1,
			"cooled medicine restores mana through normal item use and consumes one item");
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->action == acMagic
			&& player->nextAction->destGE.lock() == distantTarget,
			"recovered mana permits the next targeted skill without a melee fallback");
		session.cancelWorld("test_complete");
		manager.goodsManager.clearItemByFileName(manaMedicine);

		// A normal path stops when an NPC enters its next tile. Only the test
		// controller retains the requested destination and retries through Player.
		auto beginMove = [&](const char* commandName = "MoveTo") -> GameplayAutomationSession::Action&
		{
			session.cancelWorld("next_test_case");
			player->nextAction.reset();
			player->actionManager->resetActionIgnoringTransitions(acStand);
			player->setPosition({10, 10}, false);
			auto moveArguments = object();
			moveArguments.objectValues = {{"generation", number(session.generation)},
				{"x", number(16)}, {"y", number(10)}};
			auto result = command(commandName, moveArguments);
			require(textMember(result, "status") == "running", "normal movement queues");
			return session.actions.at(integer(member(result, "actionId")));
		};
		manager.npcManager->npcList.clear();
		manager.map->createDataMap();
		player->actionManager->resetActionIgnoringTransitions(acStand);
		player->nextAction.reset();
		manager.scriptAPI.disableJump();
		auto disabledJumpArguments = object();
		disabledJumpArguments.objectValues = {{"generation", number(session.generation)},
			{"x", number(16)}, {"y", number(10)}};
		require(textMember(command("JumpTo", disabledJumpArguments), "reason") == "jump_disabled"
			&& !player->nextAction && player->isStanding(),
			"DisableJump rejects automation requests without queuing movement");
		const int disabledJumpThew = player->thew;
		NextAction disabledJump;
		disabledJump.action = acJump;
		disabledJump.dest = {16, 10};
		player->addNextAction(disabledJump);
		player->onUpdate();
		require(player->isStanding() && !player->nextAction && player->thew == disabledJumpThew,
			"the shared native player action gate blocks queued jumps while disabled");
		manager.scriptAPI.enableJump();
		player->life = 0;
		auto& storyDefeatMove = beginMove();
		session.updateWorldAction();
		player->onUpdate();
		require(storyDefeatMove.status == "running" && player->isWalking() && player->life == 0,
			"standing zero-life story survivor moves through the native player action queue");
		player->setPosition({16, 10});
		++session.frame;
		session.updateWorldAction();
		require(storyDefeatMove.status == "succeeded",
			"zero-life story movement completes without changing the player's life");
		for (const auto deathAction : {acDeath, acHide})
		{
			auto& dyingMove = beginMove();
			player->actionManager->resetActionIgnoringTransitions(deathAction);
			session.updateWorldAction();
			require(dyingMove.status == "failed" && dyingMove.reason == "player_dead",
				"actual death or hidden state stops an ongoing world action");
			require(textMember(command("MoveTo", disabledJumpArguments), "reason") == "player_dead",
				"actual death or hidden state rejects a new world action");
		}
		player->life = 1000;
		auto& interruptedMove = beginMove();
		player->onUpdate();
		require(player->isWalking() && player->stepList.size() > 1, "MoveTo starts a normal path");
		auto blocker = std::make_shared<NPC>();
		blocker->kind = nkNormal;
		blocker->isVisibleByVariable = true;
		blocker->setPosition(player->stepList[1], false);
		manager.npcManager->npcList = {blocker};
		manager.map->createDataMap();
		auto* walk = dynamic_cast<NPCActionWalk*>(player->actionManager->getCurrentAction());
		require(walk != nullptr, "walking fixture has the real step state machine");
		walk->processStepOut();
		walk->processStepIn();
		require(player->isStanding() && player->stepList.empty(), "dynamic NPC blocks the next normal step");
		session.updateWorldAction();
		const auto progressBeforeRetry = interruptedMove.lastProgress;
		blocker->setPosition({20, 20}, false);
		interruptedMove.lastMoveAttempt = SDL_GetTicks();
		session.updateWorldAction();
		require(!player->nextAction, "stopped MoveTo waits for its retry interval");
		interruptedMove.lastMoveAttempt = SDL_GetTicks() - 500;
		session.updateWorldAction();
		require(player->nextAction && player->nextAction->dest == Point{16, 10}
			&& interruptedMove.lastProgress == progressBeforeRetry, "retry uses the old goal without faking progress");
		const auto queuedMove = player->nextAction;
		session.updateWorldAction();
		require(player->nextAction == queuedMove, "retry never replaces an already queued action");
		player->onUpdate();
		require(player->isWalking(), "normal movement resumes after the blocker leaves");
		const auto movementRevision = player->actionManager->getActionRevision();
		interruptedMove.lastMoveAttempt = SDL_GetTicks() - 500;
		session.updateWorldAction();
		require(!player->nextAction && player->actionManager->getActionRevision() == movementRevision,
			"retry does not reset an active movement");
		for (const auto protectedAction : {acAttack, acJump, acMagic})
		{
			player->actionManager->resetActionIgnoringTransitions(protectedAction);
			session.updateWorldAction();
			require(!player->nextAction && player->actionManager->getCurrentActionType() == protectedAction,
				"retry does not interrupt an attack, jump, or skill");
		}
		player->actionManager->resetActionIgnoringTransitions(acStand);
		player->beginHurt({11, 10});
		const auto moveHurtRevision = player->actionManager->getActionRevision();
		interruptedMove.lastMoveAttempt = SDL_GetTicks() - 500;
		session.updateWorldAction();
		require(player->isHurting() && player->nextAction && player->nextAction->action == acWalk
			&& player->actionManager->getActionRevision() == moveHurtRevision,
			"interrupted movement prequeues its old goal without interrupting native hurt");
		player->setTime(player->actionBeginTime + player->actionLastTime + 1);
		player->onUpdate();
		require(player->isWalking() && !player->nextAction,
			"native hurt recovery consumes the retained movement goal");
		player->actionManager->resetActionIgnoringTransitions(acStand);
		manager.global.data.canInput = false;
		session.updateWorldAction();
		require(!player->nextAction, "retry respects blocked world input");
		manager.global.data.canInput = true;
		blocker->setPosition({16, 10}, false);
		session.updateWorldAction();
		require(!player->nextAction, "retry does not bypass an occupied destination");
		interruptedMove.lastProgress = SDL_GetTicks() - 10001;
		session.updateWorldAction();
		require(interruptedMove.status == "failed" && interruptedMove.reason == "no_progress"
			&& !player->nextAction, "retry preserves the ten-second no-progress failure");
		blocker->setPosition({20, 20}, false);
		auto& interruptedJump = beginMove("JumpTo");
		player->onUpdate();
		require(player->isJumping(), "normal jump starts before its native hurt interruption");
		player->beginHurt({11, 10});
		require(player->isHurting() && !player->nextAction,
			"native damage can interrupt jump startup before the airborne phase");
		const auto jumpHurtRevision = player->actionManager->getActionRevision();
		const int thewBeforeJumpRetry = player->thew;
		interruptedJump.lastMoveAttempt = SDL_GetTicks() - 500;
		session.updateWorldAction();
		const auto retriedJump = player->nextAction;
		require(retriedJump && retriedJump->action == acJump && retriedJump->dest == Point{16, 10}
			&& player->isHurting() && player->actionManager->getActionRevision() == jumpHurtRevision
			&& player->thew == thewBeforeJumpRetry,
			"jump retry only queues its original goal during hurt without spending thew or changing the action");
		session.updateWorldAction();
		require(player->nextAction == retriedJump, "jump retry keeps exactly one pending native action");
		player->setTime(player->actionBeginTime + player->actionLastTime + 1);
		player->onUpdate();
		require(player->isJumping() && !player->nextAction,
			"native hurt recovery starts the pending jump");
		session.updateWorldAction();
		require(!player->nextAction, "jump retry does not replace an active jump");
		player->setTime(player->actionBeginTime + player->actionLastTime + 1);
		player->onUpdate();
		++session.frame;
		session.updateWorldAction();
		require(player->getPosition() == Point{16, 10} && interruptedJump.status == "succeeded",
			"retried jump reaches its goal through native collision and movement updates");

		// Feed real SDL events through EngineBase before dispatching the resulting
		// ordinary keyboard event. Cancellation must not erase that new skill.
		require(SDL_InitSubSystem(SDL_INIT_EVENTS), "initialize the SDL event queue");
		HANDLE manualConnection = INVALID_HANDLE_VALUE;
		auto reconnect = [&]()
		{
			if (manualConnection != INVALID_HANDLE_VALUE)
			{
				CloseHandle(manualConnection);
				for (int i = 0; session.pipe.isConnected() && i < 100; ++i) SDL_Delay(1);
				require(!session.pipe.isConnected(), "manual test client disconnects");
			}
			manualConnection = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
				0, nullptr, OPEN_EXISTING, 0, nullptr);
			require(manualConnection != INVALID_HANDLE_VALUE, "manual test client reconnects");
			for (int i = 0; !session.pipe.isConnected() && i < 100; ++i) SDL_Delay(1);
			require(session.pipe.isConnected(), "manual test connection becomes active");
		};
		reconnect();
		auto& manualMove = beginMove();
		session.autoDialogue = true;
		SDL_Event physicalEvent{};
		physicalEvent.type = SDL_EVENT_MOUSE_MOTION;
		require(SDL_PushEvent(&physicalEvent), "queue ordinary mouse motion");
		engine->handleEvent();
		require(manualMove.status == "running" && session.autoDialogue, "mouse hover leaves automation running");
		physicalEvent = {};
		physicalEvent.type = SDL_EVENT_KEY_DOWN;
		physicalEvent.key.scancode = SDL_SCANCODE_A;
		physicalEvent.key.down = true;
		require(SDL_PushEvent(&physicalEvent), "queue a real skill key event");
		engine->handleEvent();
		require(manualMove.status == "cancelled" && manualMove.reason == "manual_input"
			&& !session.autoDialogue && !player->nextAction, "key input cancels the old automation first");
		AEvent gameEvent;
		bool deliveredSkillKey = false;
		while (engine->getEvent(gameEvent) > 0)
		{
			if (gameEvent.eventType == ET_KEYDOWN && gameEvent.eventData == KEY_A)
			{
				deliveredSkillKey = manager.controller->onHandleEvent(gameEvent);
			}
		}
		require(deliveredSkillKey && player->nextAction && player->nextAction->action == acMagic,
			"the same physical key still queues its normal player skill");
		const auto manualSkill = player->nextAction;
		session.updateWorldAction();
		require(player->nextAction == manualSkill, "cancelled automation never takes back the manual skill");
		auto dialogueArguments = object();
		dialogueArguments.objectValues["enabled"] = GameplayAutomation::boolean(true);
		require(textMember(command("SetAutoDialogue", dialogueArguments), "reason") == "manual_input"
			&& !session.autoDialogue && !session.worldAction,
			"manual takeover rejects later commands even without a running world action");
		require(player->nextAction == manualSkill, "rejected commands preserve manual input");
		require(!session.observe(object()).objectValues.empty(), "manual takeover still allows observation");
		CloseHandle(manualConnection);
		manualConnection = INVALID_HANDLE_VALUE;
		for (int i = 0; session.pipe.isConnected() && i < 100; ++i) SDL_Delay(1);
		require(session.worldInputAllowed(), "manual disconnect fixture permits a normal system menu");
		session.tick();
		require(session.worldInputAllowed() && player->nextAction == manualSkill,
			"disconnect after manual takeover neither pauses nor erases the player's own action");
		for (const auto eventType : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_WHEEL})
		{
			reconnect();
			auto& mouseMove = beginMove();
			session.autoDialogue = true;
			physicalEvent = {};
			physicalEvent.type = eventType;
			if (eventType == SDL_EVENT_MOUSE_BUTTON_DOWN) physicalEvent.button.button = SDL_BUTTON_RIGHT;
			else physicalEvent.wheel.y = 1;
			require(SDL_PushEvent(&physicalEvent), "queue explicit mouse input");
			engine->handleEvent();
			require(mouseMove.status == "cancelled" && mouseMove.reason == "manual_input"
				&& !session.autoDialogue, "explicit mouse input yields automation control");
		}
		reconnect();
		require(textMember(command("SetAutoDialogue", dialogueArguments), "status") == "succeeded",
			"a new connection can explicitly resume control");
		session.autoDialogue = false;
		GameplayAutomationSession::manualInput();
		require(!session.worldAction
			&& textMember(command("ToggleSit", object()), "reason") == "manual_input",
			"idle manual takeover also blocks the next controller command");
		session.tick();
		require(textMember(command("SetAutoDialogue", dialogueArguments), "reason") == "manual_input",
			"a delayed old disconnect does not clear takeover on the new connection");
		reconnect();
		session.tick();
		auto& disconnectedMove = beginMove();
		session.autoDialogue = true;
		reconnect();
		session.tick();
		require(disconnectedMove.status == "cancelled" && disconnectedMove.reason == "client_disconnected"
			&& !session.autoDialogue && !player->nextAction && session.worldInputAllowed(),
			"quick reconnect stops the old connection's controls without pausing the new connection");

		// ToggleSit invokes the same controller action as V; recovery remains in
		// NPCActionSit and consumes the normal amount of thew over game time.
		player->res.sit.imagePackage = player->res.magic.imagePackage;
		player->info.manaMax = 1000;
		player->mana = 100;
		player->thew = 20;
		auto sitArguments = object();
		sitArguments.objectValues["generation"] = number(session.generation);
		require(textMember(command("ToggleSit", sitArguments), "reason") == "sitting"
			&& player->isSitting() && player->mana == 100 && player->thew == 20,
			"ToggleSit starts sitting without granting immediate resources");
		require(boolMember(member(session.observe(object()), "player"), "sitting"), "snapshot reports sitting");
		player->setTime(player->getTime() + player->actionLastTime + 1);
		player->actionManager->update(0);
		require(player->mana == 105 && player->thew == 15, "normal sitting restores mana and consumes thew");
		require(textMember(command("ToggleSit", sitArguments), "reason") == "standing" && player->isStanding(),
			"a second toggle uses the normal stand-up action");
		player->immobilized = true;
		require(textMember(command("ToggleSit", sitArguments), "reason") == "action_rejected"
			&& !player->isSitting(), "sitting respects the original movement restrictions");
		player->immobilized = false;
		player->mana = 100;
		player->thew = SIT_THEW_COST - 1;
		command("ToggleSit", sitArguments);
		player->setTime(player->getTime() + player->actionLastTime + 1);
		player->actionManager->update(0);
		require(player->isStanding() && player->mana == 100 && player->thew == SIT_THEW_COST - 1,
			"native sitting stops when thew cannot pay its cost");

		manager.setCheatModeEnabled(true);
		manager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
		player->mana = player->thew = 40;
		player->setPosition({10, 10}, false);
		player->beginRun({10, 14});
		require(player->isRunning() && player->thew == 40,
			"invincibility preserves stamina at the native running start");
		player->setTime(player->getTime() + 1000);
		player->actionManager->update(1000);
		require(player->thew == 40, "native running steps retain stamina while invincible");
		player->beginStand();
		player->beginJump({10, 16});
		require(player->isJumping() && player->thew == 40,
			"invincibility preserves stamina at the native jump start");
		player->forceBeginStand();
		player->beginAttack({11, 10}, nullptr);
		require(player->isAttacking() && player->thew == 40,
			"invincibility preserves stamina at the native attack start");
		player->forceBeginStand();
		player->mana = player->thew = 0;
		equippedMagic.remainColdMilliseconds = 0;
		require(session.queueSkill(0, nullptr) && player->nextAction,
			"automation accepts the same zero-resource invincible skill as the native player");
		player->nextAction.reset();
		equippedMagic.remainColdMilliseconds = 1;
		require(!session.queueSkill(0, nullptr) && !player->nextAction,
			"invincible automation still refuses a cooling skill");
		equippedMagic.remainColdMilliseconds = 0;
		player->beginJump({10, 18});
		require(player->isJumping() && player->thew == 0,
			"invincibility permits a zero-stamina native jump");
		player->forceBeginStand();
		player->beginAttack({11, 10}, nullptr);
		require(player->isAttacking() && player->thew == 0,
			"invincibility permits a zero-stamina native attack");
		player->forceBeginStand();
		command("ToggleSit", sitArguments);
		player->setTime(player->getTime() + player->actionLastTime + 1);
		player->actionManager->update(0);
		require(player->isSitting() && player->mana == 5 && player->thew == 0,
			"invincible native sitting restores mana without requiring or spending stamina");
		command("ToggleSit", sitArguments);
		manager.setCheatModeEnabled(false);
		player->beginJump({10, 18});
		require(player->isStanding() && !player->canPayRunThewCost(),
			"turning cheats off restores zero-stamina movement restrictions");
		CloseHandle(manualConnection);
		engine->currentFrameReady.store(previousFrameReady);

		// Death results must complete combat before a nested script changes NPCs.
		manager.inEvent = true;
		manager.global.data.NPCAI = false;
		auto firstTarget = std::make_shared<NPC>();
		auto secondTarget = std::make_shared<NPC>();
		firstTarget->isVisibleByVariable = secondTarget->isVisibleByVariable = true;
		manager.npcManager->npcList = {firstTarget, secondTarget};
		auto reportDeathScript = [&](const std::shared_ptr<NPC>& target)
		{
			target->deathScript = "automation-death-test.txt";
			target->result = erRunDeathScript;
			manager.npcManager->onUpdate();
		};
		auto& singleCombat = startCombat(firstTarget, 1);
		reportDeathScript(secondTarget);
		require(singleCombat.kills == 0, "another NPC death does not count toward the current target");
		reportDeathScript(firstTarget);
		require(singleCombat.kills == 1 && singleCombat.status == "succeeded" && !session.worldAction,
			"death script result completes combat while world input is blocked");
		require(manager.eventList.size() == 2, "combat finishes before queued death scripts run");
		firstTarget->life = 100;
		firstTarget->nowAction = acStand;
		GameplayAutomationSession::worldChanged();
		require(singleCombat.status == "succeeded", "revival and map replacement preserve recorded defeat");
		manager.eventList.clear();

		auto& multipleCombat = startCombat(firstTarget, 2);
		reportDeathScript(firstTarget);
		reportDeathScript(firstTarget);
		require(multipleCombat.kills == 1 && multipleCombat.status == "running",
			"repeated death results count the consumed target only once");
		multipleCombat.target = secondTarget;
		firstTarget->isBodyIniAdded = 1;
		firstTarget->result = erLifeExhaust;
		manager.npcManager->onUpdate();
		require(multipleCombat.kills == 1, "later corpse removal does not count again");
		secondTarget->isBodyIniAdded = 1;
		secondTarget->result = erLifeExhaust;
		manager.npcManager->onUpdate();
		require(multipleCombat.kills == 2 && multipleCombat.status == "succeeded",
			"normal death without a script completes the next target before deletion");
		manager.eventList.clear();

		auto removedTarget = std::make_shared<NPC>();
		removedTarget->npcName = "automation-remove-test";
		manager.npcManager->npcList.push_back(removedTarget);
		auto& removedCombat = startCombat(removedTarget, 1);
		manager.npcManager->deleteNPC(removedTarget->npcName);
		require(removedCombat.kills == 0 && removedCombat.status == "running",
			"script deletion is not a combat defeat");
		session.cancelWorld("test_complete");
		manager.inEvent = false;

		// Exercise the real trap update with armor exceeding its configured damage.
		player->invincible = 0;
		player->shieldEffects.clear();
		player->shieldLife = 0;
		player->lifeMax = player->info.lifeMax = player->life = 1000;
		player->defend = player->info.defend = 2000;
		player->actionManager->resetActionIgnoringTransitions(acStand);
		auto trapTarget = std::make_shared<NPC>();
		trapTarget->kind = nkBattle;
		trapTarget->lifeMax = trapTarget->life = 1000;
		trapTarget->defend = 2000;
		trapTarget->setPosition(player->getPosition(), false);
		manager.npcManager->npcList = {trapTarget};
		player->hurtLife(100);
		trapTarget->hurtLife(100);
		require(player->life == 1000 && trapTarget->life == 1000,
			"ordinary direct-damage callers retain defense subtraction");
		auto spike = std::make_shared<Object>();
		spike->kind = okTrap;
		spike->damage = 100;
		spike->damageInterval = 100;
		spike->setPosition(player->getPosition());
		spike->onUpdate();
		require(player->life == 920 && trapTarget->life == 900,
			"trap damages armored player and NPC through their normal damage paths");
		spike->onUpdate();
		require(player->life == 920 && trapTarget->life == 900,
			"trap does not repeat damage within the same animation cycle");
		manager.setCheatModeEnabled(true);
		manager.performCheatAction(GameManager::CheatAction::ToggleInvincibility);
		trapTarget->invincible = 1;
		spike->lastTrapDamageCycle = OBJECT_TRAP_DAMAGE_CYCLE_UNSET;
		spike->onUpdate();
		require(player->life == 920 && trapTarget->life == 900,
			"ignoring trap defense still honors player cheat protection and NPC invincibility");
		manager.setCheatModeEnabled(false);

		options.arrayValues[0] = number(0);
		arguments.objectValues = {{"context", number(session.context)}, {"options", options}};
		require(textMember(command("Choose", arguments), "status") == "succeeded", "explicit visible choice succeeds");
		require(choice->getSelection() == 0, "normal choice callback selects intended option");
	}
};
#endif

bool runGameplayAutomationRuntimeTests()
{
#if defined(_WIN32) && defined(JXQY_ENABLE_TEST_HOOKS)
	const auto root = makeUniqueTestDirectory("jxqy_gameplay_interface_test");
	try
	{
		SDL_Init(0);
		const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
		if (!ResourceManager::instance().initialize(assets.u8string())
			|| !ResourceManager::instance().setActiveResourcePackById("JXQY2"))
			throw std::runtime_error("JXQY2 resources unavailable");
		GameplayAutomationTestAccess::run(root);
		const auto unavailableRoot = root / "unavailable";
		std::filesystem::create_directories(unavailableRoot);
		std::ofstream(unavailableRoot / "automation") << "output is intentionally unavailable";
		{
			GameplayAutomationSession session("output-failure-" + std::to_string(GetCurrentProcessId()), unavailableRoot);
			if (GameplayAutomationSession::traceWriter() != nullptr) throw std::runtime_error("invalid output unexpectedly opened");
			GameplayAutomationSession::onFrame();
		}
		// This test owns only the unique directory immediately under the OS temp directory.
		if (root.parent_path() == std::filesystem::temp_directory_path()) std::filesystem::remove_all(root);
		return true;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Gameplay automation regression: " << error.what() << '\n';
		return false;
	}
#else
	return true;
#endif
}
