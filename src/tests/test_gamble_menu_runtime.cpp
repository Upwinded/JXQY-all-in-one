#include "../Game/Menu/GambleMenu.h"
#include "../Engine/Engine.h"
#include "../Game/Menu/BuySellMenu.h"
#include "../Game/Menu/MapThumbnailMenu.h"
#include "../Game/Menu/UIFocusManager.h"
#include "../Game/Menu/YesNo.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Data/Player.h"
#include "HeadlessPhysicalInputTestHarness.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool hasPrompt(
	const std::vector<ControllerPromptItem>& items,
	GameInput::InputAction action,
	const std::string& description)
{
	for (const ControllerPromptItem& item : items)
	{
		if (item.description == description
			&& (item.action == action
				|| std::find(
					item.alternativeActions.begin(),
					item.alternativeActions.end(),
					action) != item.alternativeActions.end()))
		{
			return true;
		}
	}
	return false;
}

class TestTextButton : public TextButton
{
public:
	void triggerClick()
	{
		onClick();
	}
};

class TestButton : public Button
{
public:
	void triggerClick()
	{
		onClick();
	}
};

class TestGambleMenu : public GambleMenu
{
public:
	bool isLogicRunningForTest() const
	{
		return logicRunning;
	}
};

class TestGameManager : public GameManager
{
public:
	bool isLogicRunningForTest() const
	{
		return logicRunning;
	}
};
}

class GambleMenuTestAccess
{
public:
	static void prepareDicePrimary(
		GambleMenu& menu,
		const std::shared_ptr<TestTextButton>& primaryButton)
	{
		menu.mode = GambleMenu::mmDiceGame;
		menu.settled = false;
		menu.diceResourceLoaded = false;
		menu.diceState.reset(100, 50);
		menu.diceState.addBet();
		menu.visible = true;
		menu.canCallBack = true;
		menu.setRunning(true);
		menu.rollButton = primaryButton;
		menu.addChild(primaryButton);
	}

	static void prepareDiceModal(GambleMenu& menu)
	{
		menu.mode = GambleMenu::mmDiceGame;
		menu.settled = false;
		menu.diceState.reset(100, 50);
		menu.visible = true;
		menu.setRunning(true);
		menu.result = erNone;
	}

	static void prepareGambleResourceUp(
		GambleMenu& menu,
		const std::shared_ptr<TestButton>& upButton)
	{
		menu.mode = GambleMenu::mmGamble;
		menu.playerStake = 80;
		menu.dealerStake = 80;
		menu.currentBet = 0;
		menu.settled = false;
		menu.visible = true;
		menu.setRunning(true);
		menu.resourceLayoutLoaded = true;
		menu.resourceUpButton = upButton;
		upButton->canCallBack = true;
		menu.addChild(upButton);
	}

	static int currentBet(const GambleMenu& menu)
	{
		return menu.currentBet;
	}

	static int playerStake(const GambleMenu& menu)
	{
		return menu.playerStake;
	}

	static void prepareResolvedGambleRound(GambleMenu& menu, UTime beginTime)
	{
		menu.mode = GambleMenu::mmGamble;
		menu.settled = false;
		menu.roundResolved = true;
		menu.roundResolvedBeginTime = beginTime;
		menu.visible = true;
		menu.setRunning(true);
	}

	static bool isGambleRoundResolved(const GambleMenu& menu)
	{
		return menu.roundResolved;
	}

	static void prepareOverlappingFishControls(
		GambleMenu& menu,
		const std::shared_ptr<TestButton>& pullButton,
		const std::shared_ptr<TestButton>& reelButton)
	{
		menu.mode = GambleMenu::mmFishGame;
		menu.visible = true;
		menu.setRunning(true);
		menu.fishResourceLoaded = true;
		menu.fishState.reset();
		menu.fishState.startCast();
		std::vector<int> values = { 2, 18, 100, 100, 100 };
		auto random = [&values](int, int)
		{
			int value = values.front();
			values.erase(values.begin());
			return value;
		};
		menu.fishState.update(FishGameState::CastDurationMilliseconds + 2000
			+ FishGameState::BiteDurationMilliseconds, random);
		menu.fishState.pull(random);
		menu.fishState.pull(random);
		menu.fishState.pull(random);

		auto makeControl = [&menu]()
		{
			auto button = std::make_shared<TestButton>();
			button->canCallBack = true;
			button->rect = { 100, 100, 40, 40 };
			menu.addChild(button);
			return button;
		};
		menu.fishCastButton = makeControl();
		menu.fishStruggleButton = makeControl();
		menu.fishCloseButton = makeControl();
		menu.fishPullButton = pullButton;
		menu.fishReelButton = reelButton;
		pullButton->canCallBack = true;
		pullButton->rect = { 100, 100, 40, 40 };
		reelButton->canCallBack = true;
		reelButton->rect = { 90, 90, 60, 60 };
		reelButton->setPriority(epItem);
		menu.addChild(pullButton);
		menu.addChild(reelButton);
	}

	static int fishCatchCount(const GambleMenu& menu)
	{
		return menu.fishState.catchCount();
	}

	static bool reelHasInputPriority(
		const std::shared_ptr<TestButton>& pullButton,
		const std::shared_ptr<TestButton>& reelButton)
	{
		return reelButton->getPriority() < pullButton->getPriority();
	}

	static bool isDiceRolling(const GambleMenu& menu)
	{
		return menu.diceState.phase() == DiceGambleState::Phase::Rolling;
	}

	static void resolveWinningDiceRound(GambleMenu& menu, int playerMoney)
	{
		menu.mode = GambleMenu::mmDiceGame;
		menu.diceState.reset(playerMoney, 50);
		menu.diceState.addBet();
		menu.diceState.start();
		menu.diceState.open({ 6, 6, 6 }, { 1, 2, 4 });
		menu.diceState.update(4000);
		menu.resolveDiceRound();
	}

	static void poll(GambleMenu& menu)
	{
		menu.onEvent();
	}

	static bool dispatch(GambleMenu& menu, AEvent& event)
	{
		return menu.onHandleEvent(event);
	}

	static bool dispatchAction(GambleMenu& menu, UIAction action)
	{
		return menu.onHandleUIAction(action);
	}

	static void prepareGambleController(GambleMenu& menu)
	{
		menu.mode = GambleMenu::mmGamble;
		menu.resourceLayoutLoaded = true;
		menu.playerStake = 20;
		menu.dealerStake = 20;
		menu.currentBet = 0;
		menu.betBig = true;
		menu.settled = false;
		menu.visible = true;
		menu.setRunning(true);

		auto addControl = [&menu](const Rect& rectangle)
		{
			auto button = std::make_shared<TestButton>();
			button->rect = rectangle;
			button->canCallBack = true;
			menu.addChild(button);
			return button;
		};
		// These are the production XJXQY littlegame INI rectangles. Keeping the
		// irregular layout here prevents a fabricated 4+2 grid from masking
		// directional mismatches.
		menu.resourceBigButton = addControl({ 204, 261, 120, 70 });
		menu.resourceSmallButton = addControl({ 325, 262, 120, 70 });
		menu.resourceChipInButton = addControl({ 267, 411, 103, 33 });
		menu.resourceUpButton = addControl({ 229, 444, 10, 10 });
		menu.resourceDownButton = addControl({ 229, 454, 10, 10 });
		menu.resourceLeaveButton = addControl({ 267, 444, 103, 36 });
		menu.configureControllerFocus();
	}

	static bool betsBig(const GambleMenu& menu)
	{
		return menu.betBig;
	}

	static std::string focusedGambleNode(const GambleMenu& menu)
	{
		return menu.controllerFocusManager.getFocusedNodeId();
	}

	static void prepareDiceController(GambleMenu& menu)
	{
		menu.mode = GambleMenu::mmDiceGame;
		menu.settled = false;
		menu.diceState.reset(200, 100);
		menu.visible = true;
		menu.setRunning(true);
		menu.diceNpcTalk.clear();
	}

	static void prepareDiceRevealing(GambleMenu& menu)
	{
		prepareDiceController(menu);
		menu.diceState.addBet();
		menu.diceState.start();
		menu.diceState.open({ 6, 6, 6 }, { 1, 2, 4 });
	}

	static DiceGambleState::Phase dicePhase(const GambleMenu& menu)
	{
		return menu.diceState.phase();
	}

	static bool diceCupOpened(const GambleMenu& menu)
	{
		const DiceGambleState::Phase phase = menu.diceState.phase();
		return phase == DiceGambleState::Phase::Revealing
			|| (phase == DiceGambleState::Phase::WaitingForBet
				&& menu.diceState.outcome().result != DiceGambleState::Result::None);
	}

	static int diceStake(const GambleMenu& menu)
	{
		return menu.diceState.stake();
	}

	static const std::string& diceNpcTalk(const GambleMenu& menu)
	{
		return menu.diceNpcTalk;
	}

	static void prepareFishIdle(GambleMenu& menu)
	{
		menu.mode = GambleMenu::mmFishGame;
		menu.settled = false;
		menu.fishResourceLoaded = false;
		menu.fishState.reset();
		menu.visible = true;
		menu.setRunning(true);
	}

	static void prepareFishStruggling(GambleMenu& menu)
	{
		prepareFishIdle(menu);
		menu.fishState.startCast();
		std::vector<int> updateValues = { 2, 18 };
		auto updateRandom = [&updateValues](int, int)
		{
			int value = updateValues.front();
			updateValues.erase(updateValues.begin());
			return value;
		};
		menu.fishState.update(FishGameState::CastDurationMilliseconds + 2000
			+ FishGameState::BiteDurationMilliseconds, updateRandom);
		std::vector<int> pullValues = { 0, 2 };
		auto pullRandom = [&pullValues](int, int)
		{
			int value = pullValues.front();
			pullValues.erase(pullValues.begin());
			return value;
		};
		menu.fishState.pull(pullRandom);
	}

	static FishGameState::Phase fishPhase(const GambleMenu& menu)
	{
		return menu.fishState.phase();
	}

	static int fishLostLives(const GambleMenu& menu)
	{
		return menu.fishState.lostLives();
	}

	static void prepareFishReadyToReel(GambleMenu& menu)
	{
		prepareFishIdle(menu);
		menu.fishState.startCast();
		std::vector<int> updateValues = { 2, 18 };
		auto updateRandom = [&updateValues](int, int)
		{
			int value = updateValues.front();
			updateValues.erase(updateValues.begin());
			return value;
		};
		menu.fishState.update(FishGameState::CastDurationMilliseconds + 2000
			+ FishGameState::BiteDurationMilliseconds, updateRandom);
		auto safePullRandom = [](int, int)
		{
			return 100;
		};
		menu.fishState.pull(safePullRandom);
		menu.fishState.pull(safePullRandom);
		menu.fishState.pull(safePullRandom);
	}

	static std::vector<ControllerPromptItem> promptItems(const GambleMenu& menu)
	{
		return menu.controllerPromptItems();
	}

	static void reportFishCaught(GambleMenu& menu)
	{
		menu.mode = GambleMenu::mmFishGame;
		menu.fishResourceLoaded = false;
		menu.handleFishEvents(FishGameState::EventCaught);
	}

	static bool fishWindowWon(const GambleMenu& menu)
	{
		return menu.win;
	}
};

namespace
{
struct PhysicalActionObservation
{
	bool semanticActionPressed = false;
	bool worldAliasPressed = false;
	bool semanticInputBlocked = false;
	bool worldStateQueuedAfterPress = false;
};

PhysicalActionObservation tapGamblePhysicalButton(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	GameInput::InputAction semanticAction,
	GameInput::InputAction worldAlias,
	TestGameManager& gameManager,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	PhysicalActionObservation observation;
	HeadlessPhysicalInputTest::FrameCallbacks pressCallbacks;
	pressCallbacks.afterInputUpdate =
		[&observation, semanticAction, worldAlias](
			const GameInput::PhysicalInputManager& inputManager)
	{
		observation.semanticActionPressed =
			inputManager.wasActionPressed(semanticAction);
		observation.worldAliasPressed =
			inputManager.wasActionPressed(worldAlias);
	};
	pressCallbacks.afterDispatch =
		[&gameManager, &observation](bool semanticInputBlocked)
	{
		observation.semanticInputBlocked = semanticInputBlocked;
		observation.worldStateQueuedAfterPress =
			gameManager.player->nextAction != nullptr
			|| gameManager.player->nextDest != ndNone
			|| !gameManager.player->destGE.expired();
	};
	frameDriver.tapButton(gamepad, button, pressCallbacks);
	return observation;
}

PhysicalActionObservation tapGamblePhysicalRightTrigger(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	TestGameManager& gameManager,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	PhysicalActionObservation observation;
	HeadlessPhysicalInputTest::FrameCallbacks pressCallbacks;
	pressCallbacks.afterInputUpdate =
		[&observation](
			const GameInput::PhysicalInputManager& inputManager)
	{
		observation.semanticActionPressed = inputManager.wasActionPressed(
			GameInput::InputAction::NextPage);
		observation.worldAliasPressed = inputManager.wasActionPressed(
			GameInput::InputAction::CastSkill1);
	};
	pressCallbacks.afterDispatch =
		[&gameManager, &observation](bool semanticInputBlocked)
	{
		observation.semanticInputBlocked = semanticInputBlocked;
		observation.worldStateQueuedAfterPress =
			gameManager.player->nextAction != nullptr
			|| gameManager.player->nextDest != ndNone
			|| !gameManager.player->destGE.expired();
	};
	frameDriver.pulseAxis(
		gamepad,
		SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
		SDL_JOYSTICK_AXIS_MAX,
		SDL_JOYSTICK_AXIS_MIN,
		pressCallbacks);
	return observation;
}

PhysicalActionObservation tapGamblePhysicalRightShoulder(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	TestGameManager& gameManager,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	PhysicalActionObservation observation;
	HeadlessPhysicalInputTest::FrameCallbacks pressCallbacks;
	pressCallbacks.afterDispatch =
		[&gameManager, &observation](bool semanticInputBlocked)
	{
		observation.semanticInputBlocked =
			observation.semanticInputBlocked || semanticInputBlocked;
		observation.worldStateQueuedAfterPress =
			observation.worldStateQueuedAfterPress
			|| gameManager.player->nextAction != nullptr
			|| gameManager.player->nextDest != ndNone
			|| !gameManager.player->destGE.expired();
	};
	HeadlessPhysicalInputTest::FrameCallbacks releaseCallbacks;
	releaseCallbacks.afterInputUpdate =
		[&observation](
			const GameInput::PhysicalInputManager& inputManager)
	{
		observation.semanticActionPressed = inputManager.wasActionPressed(
			GameInput::InputAction::NextPanel);
		observation.worldAliasPressed = inputManager.wasActionPressed(
			GameInput::InputAction::InteractAlternate);
	};
	releaseCallbacks.afterDispatch = pressCallbacks.afterDispatch;
	frameDriver.tapButton(
		gamepad,
		SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
		pressCallbacks,
		releaseCallbacks);
	return observation;
}

bool observedPhysicalAlias(
	const PhysicalActionObservation& observation)
{
	return observation.semanticActionPressed
		&& observation.worldAliasPressed
		&& observation.semanticInputBlocked
		&& !observation.worldStateQueuedAfterPress;
}

bool runGambleMenuPhysicalLinkTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"gamble physical-link test started with SDL video initialized");
	{
		VirtualGamepadTest::SDLSession sdlSession;
		VirtualGamepadTest::VirtualGamepad gamepad(
			"JXQY Headless Gamble Physical-Link Pad");
		gamepad.setAxis(
			SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
		gamepad.setAxis(
			SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);

		TestGameManager gameManager;
		gameManager.global.data.canInput = true;
		gameManager.inEvent = false;
		gameManager.setGameplayPaused(false);
		gameManager.player->setPosition({ 20, 20 });
		gameManager.player->direction = 6;
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = 48;
		gameManager.map->data->head.height = 48;
		gameManager.map->data->tile.assign(
			48, std::vector<MapTile>(48));
		gameManager.map->dataMap.tile.assign(
			48, std::vector<DataTile>(48));
		auto rightInteractionTarget = std::make_shared<Object>();
		rightInteractionTarget->objName =
			"gamble physical RB right-interaction sentinel";
		rightInteractionTarget->scriptFile = "gamble_primary.lua";
		rightInteractionTarget->scriptFileRight = "gamble_alternate.lua";
		rightInteractionTarget->setPosition({ 21, 20 });
		gameManager.objectManager->objectList.push_back(rightInteractionTarget);
		const auto rightInteractionCandidates =
			gameManager.findWorldInteractionCandidates(
				WorldInteractionIntent::Alternate, 10, 2);
		ok = check(!rightInteractionCandidates.empty()
			&& rightInteractionCandidates.front().getTarget()
				== rightInteractionTarget,
			"gamble RB leak sentinel was not a valid right-side"
			" interaction target") && ok;
		ok = check(gameManager.magicManager.bottomCount() >= 3,
			"gamble B leak sentinel did not expose the CastSkill3 slot") && ok;

		auto menu = std::make_shared<TestGambleMenu>();
		gameManager.menu->gambleMenu = menu;
		gameManager.menu->addChild(menu);
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(menu);

		auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
			Engine::getInstance()->inputActions());
		HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
			inputManager);
		ok = check(inputScope.isInitialized(),
			"gamble physical-link input manager did not initialize") && ok;
		if (inputScope.isInitialized())
		{
			std::uint64_t nowMilliseconds = SDL_GetTicks();
			HeadlessPhysicalInputTest::FrameDriver frameDriver(
				inputManager,
				nowMilliseconds,
				[]()
				{
					return dispatchPhysicalUIActions(Engine::getInstance());
				},
				[&gameManager]()
				{
					gameManager.controller->processPhysicalInputFrame();
				});
			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
			VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
			gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
			VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
			ok = check(inputManager.activeGamepadID() == gamepad.id(),
				"virtual gamepad did not claim the gamble physical-link channel") && ok;
			inputManager.releaseForContextTransition();
			frameDriver.runFrame();

			GambleMenuTestAccess::prepareGambleController(*menu);
			gameManager.player->nextAction = nullptr;
			const int navigationBetBefore =
				GambleMenuTestAccess::currentBet(*menu);
			const int navigationStakeBefore =
				GambleMenuTestAccess::playerStake(*menu);
			const bool navigationBigBefore =
				GambleMenuTestAccess::betsBig(*menu);
			ok = check(GambleMenuTestAccess::focusedGambleNode(*menu)
				== "gamble-increase",
				"gamble physical navigation did not start at the default node") && ok;
			struct GambleNavigationCase
			{
				SDL_GamepadButton button;
				GameInput::InputAction action;
				const char* expectedFocus;
			};
			const std::array<GambleNavigationCase, 9> navigationCases =
			{
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_DOWN,
					GameInput::InputAction::NavigateDown,
					"gamble-decrease" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_UP,
					GameInput::InputAction::NavigateUp,
					"gamble-increase" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_UP,
					GameInput::InputAction::NavigateUp,
					"gamble-big" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
					GameInput::InputAction::NavigateRight,
					"gamble-small" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_LEFT,
					GameInput::InputAction::NavigateLeft,
					"gamble-big" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_DOWN,
					GameInput::InputAction::NavigateDown,
					"gamble-primary" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_DOWN,
					GameInput::InputAction::NavigateDown,
					"gamble-exit" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_UP,
					GameInput::InputAction::NavigateUp,
					"gamble-primary" },
				GambleNavigationCase{
					SDL_GAMEPAD_BUTTON_DPAD_LEFT,
					GameInput::InputAction::NavigateLeft,
					"gamble-increase" },
			};
			for (const GambleNavigationCase& navigationCase : navigationCases)
			{
				bool actionPressed = false;
				bool sourceMatched = false;
				bool semanticInputBlocked = false;
				bool actionConsumed = false;
				bool worldStateQueued = false;
				bool actionRepeatedOnRelease = false;
				std::string focusAfterPress;
				std::string focusAfterRelease;
				HeadlessPhysicalInputTest::FrameCallbacks pressCallbacks;
				pressCallbacks.afterInputUpdate =
					[&actionPressed, &sourceMatched, &gamepad, &navigationCase](
						const GameInput::PhysicalInputManager& currentInputManager)
				{
					actionPressed = currentInputManager.wasActionPressed(
						navigationCase.action);
					sourceMatched = currentInputManager.action(
						navigationCase.action).sourceDeviceID == gamepad.id();
				};
				pressCallbacks.afterDispatch =
					[&](bool blocked)
				{
					semanticInputBlocked = blocked;
					actionConsumed = !inputManager.wasActionPressed(
						navigationCase.action);
					focusAfterPress =
						GambleMenuTestAccess::focusedGambleNode(*menu);
					worldStateQueued = gameManager.player->nextAction != nullptr
						|| gameManager.player->nextDest != ndNone
						|| !gameManager.player->destGE.expired();
				};
				HeadlessPhysicalInputTest::FrameCallbacks releaseCallbacks;
				releaseCallbacks.afterInputUpdate =
					[&actionRepeatedOnRelease, &navigationCase](
						const GameInput::PhysicalInputManager& currentInputManager)
				{
					actionRepeatedOnRelease =
						currentInputManager.wasActionPressed(
							navigationCase.action);
				};
				releaseCallbacks.afterDispatch =
					[&](bool)
				{
					focusAfterRelease =
						GambleMenuTestAccess::focusedGambleNode(*menu);
					worldStateQueued = worldStateQueued
						|| gameManager.player->nextAction != nullptr
						|| gameManager.player->nextDest != ndNone
						|| !gameManager.player->destGE.expired();
				};
				frameDriver.tapButton(
					gamepad,
					navigationCase.button,
					pressCallbacks,
					releaseCallbacks);
				ok = check(actionPressed
					&& sourceMatched
					&& semanticInputBlocked
					&& actionConsumed
					&& !actionRepeatedOnRelease
					&& !worldStateQueued
					&& focusAfterPress == navigationCase.expectedFocus
					&& focusAfterRelease == navigationCase.expectedFocus,
					std::string("gamble physical navigation did not reach and hold ")
						+ navigationCase.expectedFocus) && ok;
			}
			ok = check(GambleMenuTestAccess::currentBet(*menu)
					== navigationBetBefore
				&& GambleMenuTestAccess::playerStake(*menu)
					== navigationStakeBefore
				&& GambleMenuTestAccess::betsBig(*menu) == navigationBigBefore
				&& menu->visible
				&& menu->isLogicRunningForTest(),
				"gamble physical focus navigation changed business state") && ok;

			const PhysicalActionObservation gambleConfirm =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(gambleConfirm)
				&& GambleMenuTestAccess::currentBet(*menu) == 1
				&& GambleMenuTestAccess::playerStake(*menu) == 19
				&& gameManager.player->nextAction == nullptr,
				"gamble physical A did not confirm the focused wager or block InteractPrimary") && ok;

			const PhysicalActionObservation gamblePage =
				tapGamblePhysicalRightTrigger(
					gamepad,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(gamblePage)
				&& GambleMenuTestAccess::currentBet(*menu) == 1
				&& GambleMenuTestAccess::playerStake(*menu) == 19
				&& gameManager.player->nextAction == nullptr,
				"gamble physical RT was not contained as an inactive modal action") && ok;

			const PhysicalActionObservation gambleAlternate =
				tapGamblePhysicalRightShoulder(
					gamepad,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(gambleAlternate)
				&& GambleMenuTestAccess::currentBet(*menu) == 1
				&& GambleMenuTestAccess::playerStake(*menu) == 19
				&& gameManager.player->nextAction == nullptr,
				"gamble physical RB did not contain NextPanel and the"
				" InteractAlternate release alias") && ok;

			const PhysicalActionObservation gambleCancel =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_EAST,
					GameInput::InputAction::Cancel,
					GameInput::InputAction::CastSkill3,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(gambleCancel)
				&& !menu->isLogicRunningForTest()
				&& gameManager.player->nextAction == nullptr,
				"gamble physical B did not exit or leaked CastSkill3") && ok;

			GambleMenuTestAccess::prepareDiceController(*menu);
			gameManager.player->nextAction = nullptr;
			const PhysicalActionObservation diceBet =
				tapGamblePhysicalRightTrigger(
					gamepad,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(diceBet)
				&& GambleMenuTestAccess::diceStake(*menu)
					== DiceGambleState::BetStep
				&& gameManager.player->nextAction == nullptr,
				"dice physical RT did not add the waiting-phase bet or block CastSkill1") && ok;

			const PhysicalActionObservation diceStart =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(diceStart)
				&& GambleMenuTestAccess::dicePhase(*menu)
					== DiceGambleState::Phase::Rolling
				&& gameManager.player->nextAction == nullptr,
				"dice physical A did not start the roll or leaked InteractPrimary") && ok;

			const int rollingStake = GambleMenuTestAccess::diceStake(*menu);
			const PhysicalActionObservation diceRollingPage =
				tapGamblePhysicalRightTrigger(
					gamepad,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(diceRollingPage)
				&& GambleMenuTestAccess::diceStake(*menu) == rollingStake
				&& GambleMenuTestAccess::dicePhase(*menu)
					== DiceGambleState::Phase::Rolling
				&& gameManager.player->nextAction == nullptr,
				"dice physical RT changed the wager outside the waiting phase") && ok;

			const PhysicalActionObservation diceOpen =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(diceOpen)
				&& gameManager.player->nextAction == nullptr,
				"dice rolling-phase physical A was not observed or leaked InteractPrimary") && ok;
			ok = check(GambleMenuTestAccess::diceCupOpened(*menu),
				"dice rolling-phase physical A did not open or resolve the cup") && ok;

			const PhysicalActionObservation diceRevealExit =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_EAST,
					GameInput::InputAction::Cancel,
					GameInput::InputAction::CastSkill3,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(diceRevealExit)
				&& !menu->isLogicRunningForTest()
				&& gameManager.player->nextAction == nullptr,
				"dice reveal-phase physical B did not exit or leak CastSkill3") && ok;

			GambleMenuTestAccess::prepareFishIdle(*menu);
			gameManager.player->nextAction = nullptr;
			const PhysicalActionObservation fishCast =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(fishCast)
				&& GambleMenuTestAccess::fishPhase(*menu)
					== FishGameState::Phase::Casting
				&& gameManager.player->nextAction == nullptr,
				"fish physical A did not cast or leaked InteractPrimary") && ok;

			const PhysicalActionObservation fishEarlyReel =
				tapGamblePhysicalRightTrigger(
					gamepad,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(fishEarlyReel)
				&& GambleMenuTestAccess::fishPhase(*menu)
					== FishGameState::Phase::Casting
				&& GambleMenuTestAccess::fishCatchCount(*menu) == 0
				&& gameManager.player->nextAction == nullptr,
				"fish physical RT reeled before canReel or leaked CastSkill1") && ok;

			const PhysicalActionObservation fishCancel =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_EAST,
					GameInput::InputAction::Cancel,
					GameInput::InputAction::CastSkill3,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(fishCancel)
				&& !menu->isLogicRunningForTest()
				&& gameManager.player->nextAction == nullptr,
				"fish physical B did not exit or leaked CastSkill3") && ok;

			GambleMenuTestAccess::prepareFishReadyToReel(*menu);
			const PhysicalActionObservation fishReel =
				tapGamblePhysicalRightTrigger(
					gamepad,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(fishReel)
				&& GambleMenuTestAccess::fishCatchCount(*menu) == 1
				&& gameManager.player->nextAction == nullptr,
				"fish physical RT did not reel after canReel or leaked CastSkill1") && ok;

			GambleMenuTestAccess::prepareFishStruggling(*menu);
			const int lostLivesBeforeConfirm =
				GambleMenuTestAccess::fishLostLives(*menu);
			const PhysicalActionObservation fishStruggleConfirm =
				tapGamblePhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = check(observedPhysicalAlias(fishStruggleConfirm)
				&& GambleMenuTestAccess::fishPhase(*menu)
					== FishGameState::Phase::Struggling
				&& GambleMenuTestAccess::fishLostLives(*menu)
					== lostLivesBeforeConfirm
				&& gameManager.player->nextAction == nullptr,
				"fish struggling-phase physical A was not consumed as a no-op") && ok;
		}
	}
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"gamble physical-link test initialized SDL video") && ok;
	return ok;
}
}

bool runGambleMenuRuntimeTests()
{
	bool ok = true;

	{
		auto sceneRoot = std::make_shared<Element>();
		auto gamble = std::make_shared<TestGambleMenu>();
		auto buySell = std::make_shared<BuySellMenu>();
		auto mapThumbnail = std::make_shared<MapThumbnailMenu>();
		sceneRoot->addChild(gamble);
		sceneRoot->addChild(buySell);
		sceneRoot->addChild(mapThumbnail);

		HeadlessPhysicalInputTest::ScopedRunningOwner sceneOwner(sceneRoot);
		ok = check(!ControllerPromptPresenter::canPresentForOwner(
				gamble.get(), ControllerPromptOwnerPolicy::CurrentRunOwner)
			&& !ControllerPromptPresenter::canPresentForOwner(
				buySell.get(), ControllerPromptOwnerPolicy::CurrentRunOwner)
			&& ControllerPromptPresenter::canPresentForOwner(
				mapThumbnail.get(),
				ControllerPromptOwnerPolicy::ActiveNonModalOwner),
			"scene lifecycle did not reserve modal prompts for run owners"
			" while allowing the active non-modal prompt policy") && ok;

		{
			HeadlessPhysicalInputTest::ScopedRunningOwner gambleOwner(gamble);
			ok = check(ControllerPromptPresenter::canPresentForOwner(
					gamble.get(),
					ControllerPromptOwnerPolicy::CurrentRunOwner),
				"gamble prompt was not owned by the active GambleMenu run")
				&& ok;

			auto yesNo = std::make_shared<YesNo>(
				"Controller prompt owner lifecycle");
			gamble->addChild(yesNo);
			{
				HeadlessPhysicalInputTest::ScopedRunningOwner yesNoOwner(yesNo);
				ok = check(Element::isCurrentRunOwner(yesNo.get())
					&& ControllerPromptPresenter::canPresentForOwner(
						yesNo.get(),
						ControllerPromptOwnerPolicy::CurrentRunOwner)
					&& !ControllerPromptPresenter::canPresentForOwner(
						gamble.get(),
						ControllerPromptOwnerPolicy::CurrentRunOwner)
					&& !ControllerPromptPresenter::canPresentForOwner(
						buySell.get(),
						ControllerPromptOwnerPolicy::CurrentRunOwner)
					&& !ControllerPromptPresenter::canPresentForOwner(
						mapThumbnail.get(),
						ControllerPromptOwnerPolicy::ActiveNonModalOwner),
					"nested YesNo run did not suppress Gamble, BuySell,"
					" and map-thumbnail parent/sibling prompts") && ok;
			}
			gamble->removeChild(yesNo);
			ok = check(ControllerPromptPresenter::canPresentForOwner(
					gamble.get(),
					ControllerPromptOwnerPolicy::CurrentRunOwner),
				"gamble prompt ownership did not resume after nested YesNo")
				&& ok;
		}

		{
			HeadlessPhysicalInputTest::ScopedRunningOwner buySellOwner(buySell);
			ok = check(ControllerPromptPresenter::canPresentForOwner(
					buySell.get(),
					ControllerPromptOwnerPolicy::CurrentRunOwner)
				&& !ControllerPromptPresenter::canPresentForOwner(
					gamble.get(),
					ControllerPromptOwnerPolicy::CurrentRunOwner),
				"BuySell run did not exclusively own its specialized prompt")
				&& ok;

			auto yesNo = std::make_shared<YesNo>(
				"BuySell prompt owner lifecycle");
			buySell->addChild(yesNo);
			{
				HeadlessPhysicalInputTest::ScopedRunningOwner yesNoOwner(yesNo);
				ok = check(ControllerPromptPresenter::canPresentForOwner(
						yesNo.get(),
						ControllerPromptOwnerPolicy::CurrentRunOwner)
					&& !ControllerPromptPresenter::canPresentForOwner(
						buySell.get(),
						ControllerPromptOwnerPolicy::CurrentRunOwner)
					&& !ControllerPromptPresenter::canPresentForOwner(
						mapThumbnail.get(),
						ControllerPromptOwnerPolicy::ActiveNonModalOwner),
					"nested YesNo run did not suppress BuySell and"
					" map-thumbnail prompts") && ok;
			}
			buySell->removeChild(yesNo);
			ok = check(ControllerPromptPresenter::canPresentForOwner(
					buySell.get(),
					ControllerPromptOwnerPolicy::CurrentRunOwner),
				"BuySell prompt ownership did not resume after nested YesNo")
				&& ok;
		}
	}

	{
		const std::vector<ControllerPromptItem> items =
		{
			{ GameInput::InputAction::Confirm, "确认" },
			{ GameInput::InputAction::Cancel, "返回" },
			{ GameInput::InputAction::Secondary, "次要" },
			{ GameInput::InputAction::ShowDetails, "详情" },
			{ GameInput::InputAction::NextPage, "翻页" },
			{ GameInput::InputAction::NextPanel, "面板" },
		};
		const ControllerPromptLabelTheme xbox =
		{
			SDL_GAMEPAD_BUTTON_LABEL_A,
			SDL_GAMEPAD_BUTTON_LABEL_B,
			SDL_GAMEPAD_BUTTON_LABEL_X,
			SDL_GAMEPAD_BUTTON_LABEL_Y,
		};
		const ControllerPromptLabelTheme playStation =
		{
			SDL_GAMEPAD_BUTTON_LABEL_CROSS,
			SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
			SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
			SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE,
		};
		const ControllerPromptLabelTheme nintendo =
		{
			SDL_GAMEPAD_BUTTON_LABEL_B,
			SDL_GAMEPAD_BUTTON_LABEL_A,
			SDL_GAMEPAD_BUTTON_LABEL_Y,
			SDL_GAMEPAD_BUTTON_LABEL_X,
		};
		const std::array<const char*, GameInput::InputActionCount> XboxLabels =
		{
			"X", "RT", "Y", "B", "RB+A", "RB+B", "RB+LB", "RB+X",
			"RB+Y", "LB+左摇杆", "A", "RB", "LT", "L3", "R3", "Start",
			"RB+十字键上", "RB+十字键下", "RB+十字键左", "RB+十字键右",
			"十字键", "十字键", "十字键", "十字键", "A", "B", "X", "Y",
			"LB", "RB", "LT", "RT", "Back+Start", "左摇杆",
			"右摇杆", "右摇杆", "右摇杆", "右摇杆",
		};
		const std::array<const char*, GameInput::InputActionCount>
			PlayStationLabels =
		{
			"□", "R2", "△", "○", "R1+×", "R1+○", "R1+L1", "R1+□",
			"R1+△", "L1+左摇杆", "×", "R1", "L2", "L3", "R3", "Options",
			"R1+十字键上", "R1+十字键下", "R1+十字键左", "R1+十字键右",
			"方向键", "方向键", "方向键", "方向键", "×", "○", "□", "△",
			"L1", "R1", "L2", "R2", "Create+Options", "左摇杆",
			"右摇杆", "右摇杆", "右摇杆", "右摇杆",
		};
		const std::array<const char*, GameInput::InputActionCount> NintendoLabels =
		{
			"Y", "ZR", "X", "A", "R+B", "R+A", "R+L", "R+Y",
			"R+X", "L+左摇杆", "B", "R", "ZL", "LS", "RS", "+",
			"R+十字键上", "R+十字键下", "R+十字键左", "R+十字键右",
			"十字键", "十字键", "十字键", "十字键", "B", "A", "Y", "X",
			"L", "R", "ZL", "ZR", "Minus+Plus", "左摇杆",
			"右摇杆", "右摇杆", "右摇杆", "右摇杆",
		};
		for (std::size_t index = 0; index < GameInput::InputActionCount; index++)
		{
			const GameInput::InputAction action =
				static_cast<GameInput::InputAction>(index);
			ok = check(ControllerPromptPresenter::controlLabel(action, xbox)
				== XboxLabels[index],
				"Xbox controller prompt catalog diverged at action index "
					+ std::to_string(index)) && ok;
			ok = check(ControllerPromptPresenter::controlLabel(
				action, playStation) == PlayStationLabels[index],
				"PlayStation controller prompt catalog diverged at action index "
					+ std::to_string(index)) && ok;
			ok = check(ControllerPromptPresenter::controlLabel(action, nintendo)
				== NintendoLabels[index],
				"Nintendo controller prompt catalog diverged at action index "
					+ std::to_string(index)) && ok;
		}
		ok = check(ControllerPromptPresenter::controlLabel(
			GameInput::InputAction::Count, xbox).empty(),
			"invalid input action unexpectedly produced a controller prompt") && ok;
		ok = check(ControllerPromptPresenter::format(items, xbox, " | ")
			== "[A] 确认 | [B] 返回 | [X] 次要 | [Y] 详情 | [RT] 翻页 | [RB] 面板",
			"controller prompts format Xbox labels from spatial face buttons") && ok;
		ok = check(ControllerPromptPresenter::format(items, playStation, " | ")
			== "[×] 确认 | [○] 返回 | [□] 次要 | [△] 详情 | [R2] 翻页 | [R1] 面板",
			"controller prompts format PlayStation face, trigger, and shoulder labels") && ok;
		ok = check(ControllerPromptPresenter::format(items, nintendo, " | ")
			== "[B] 确认 | [A] 返回 | [Y] 次要 | [X] 详情 | [ZR] 翻页 | [R] 面板",
			"controller prompts format Nintendo face, trigger, and shoulder labels") && ok;
		const std::vector<ControllerPromptItem> stickItems =
		{
			{ GameInput::InputAction::Move, "移动" },
			{ GameInput::InputAction::ScrollDown, "滚动" },
		};
		ok = check(ControllerPromptPresenter::format(stickItems, xbox, " | ")
			== "[左摇杆] 移动 | [右摇杆] 滚动",
			"controller prompts expose left- and right-stick labels") && ok;
		const std::vector<ControllerPromptItem> alternativeItems =
		{
			{ GameInput::InputAction::NavigateUp, "滚动",
				{ GameInput::InputAction::ScrollUp } },
			{ GameInput::InputAction::NavigateUp, "选择",
				{ GameInput::InputAction::NavigateDown } },
			{ GameInput::InputAction::Confirm, "相同说明" },
			{ GameInput::InputAction::Cancel, "相同说明" },
		};
		ok = check(ControllerPromptPresenter::format(
			alternativeItems, xbox, " | ")
			== "[十字键/右摇杆] 滚动 | [十字键] 选择 | [A] 相同说明 | [B] 相同说明",
			"controller prompts combine only explicit alternatives and deduplicate labels") && ok;

		const int layoutWidths[] = { 960, 320, 280, 240, 200 };
		for (int width : layoutWidths)
		{
			ControllerPromptDrawOptions options;
			options.width = width;
			options.height = 52;
			options.fontSize = 14;
			options.horizontalPadding = 8;
			options.verticalPadding = 4;
			options.itemGap = 12;
			const ControllerPromptLayout promptLayout =
				ControllerPromptPresenter::layout(items, xbox, options);
			bool withinBounds = !promptLayout.lines.empty()
				&& promptLayout.contentHeight
					<= options.height - options.verticalPadding * 2;
			for (const ControllerPromptLayoutLine& line : promptLayout.lines)
			{
				withinBounds = withinBounds
					&& line.width <= promptLayout.contentWidth;
			}
			ok = check(withinBounds,
				"controller prompt layout escaped its configured rectangle") && ok;
		}

		std::vector<ControllerPromptItem> denseItems = items;
		denseItems.push_back(
			{ GameInput::InputAction::NavigateDown, "选择下一个很长的菜单项目" });
		denseItems.push_back(
			{ GameInput::InputAction::ScrollDown, "连续滚动详细说明" });
		ControllerPromptDrawOptions narrowOptions;
		narrowOptions.width = 120;
		narrowOptions.height = 30;
		narrowOptions.fontSize = 14;
		narrowOptions.horizontalPadding = 8;
		narrowOptions.verticalPadding = 4;
		narrowOptions.itemGap = 12;
		const ControllerPromptLayout narrowLayout =
			ControllerPromptPresenter::layout(
				denseItems, xbox, narrowOptions);
		bool narrowWithinBounds = narrowLayout.truncated
			&& !narrowLayout.lines.empty()
			&& !narrowLayout.lines.front().items.empty()
			&& narrowLayout.lines.front().items.front().find("[A]")
				!= std::string::npos
			&& narrowLayout.contentHeight
				<= narrowOptions.height - narrowOptions.verticalPadding * 2;
		for (const ControllerPromptLayoutLine& line : narrowLayout.lines)
		{
			narrowWithinBounds = narrowWithinBounds
				&& line.width <= narrowLayout.contentWidth;
		}
		ok = check(narrowWithinBounds,
			"narrow controller prompt layout did not truncate safely") && ok;
	}

	{
		using GameInput::InputAction;
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareGambleController(menu);
		const auto prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(hasPrompt(prompts, InputAction::NavigateUp, "选择")
			&& hasPrompt(prompts, InputAction::Confirm, "执行")
			&& hasPrompt(prompts, InputAction::Cancel, "离开"),
			"ordinary gamble exposes semantic select, execute, and exit prompts") && ok;
	}

	{
		using GameInput::InputAction;
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareDiceController(menu);
		auto prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(hasPrompt(prompts, InputAction::Confirm, "开始")
			&& hasPrompt(prompts, InputAction::NextPage, "下注50")
			&& hasPrompt(prompts, InputAction::Cancel, "离开"),
			"dice waiting phase exposes start, bet, and exit prompts") && ok;
		GambleMenuTestAccess::dispatchAction(menu, UIAction::PageNext);
		GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm);
		prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(hasPrompt(prompts, InputAction::Confirm, "开盅")
			&& !hasPrompt(prompts, InputAction::NextPage, "下注50"),
			"dice rolling phase replaces bet with the open-cup prompt") && ok;
		GambleMenuTestAccess::prepareDiceRevealing(menu);
		prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(!hasPrompt(prompts, InputAction::Confirm, "开盅")
			&& !hasPrompt(prompts, InputAction::NextPage, "下注50")
			&& hasPrompt(prompts, InputAction::Cancel, "离开"),
			"dice reveal phase does not advertise inactive primary actions") && ok;
	}

	{
		using GameInput::InputAction;
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareFishIdle(menu);
		auto prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(hasPrompt(prompts, InputAction::Confirm, "抛竿")
			&& hasPrompt(prompts, InputAction::Cancel, "离开"),
			"fish idle phase exposes cast and exit prompts") && ok;
		GambleMenuTestAccess::prepareFishReadyToReel(menu);
		prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(hasPrompt(prompts, InputAction::Confirm, "拉线")
			&& hasPrompt(prompts, InputAction::NextPage, "提竿"),
			"fish ready phase exposes both pull and reel prompts") && ok;
		GambleMenuTestAccess::prepareFishStruggling(menu);
		prompts = GambleMenuTestAccess::promptItems(menu);
		ok = check(!hasPrompt(prompts, InputAction::Confirm, "拉线")
			&& !hasPrompt(prompts, InputAction::Confirm, "挣扎"),
			"fish struggle phase does not advertise A as an active action") && ok;
	}

	{
		TestGambleMenu menu;
		auto upButton = std::make_shared<TestButton>();
		GambleMenuTestAccess::prepareGambleResourceUp(menu, upButton);
		upButton->triggerClick();
		ok = check(GambleMenuTestAccess::currentBet(menu) == 1
			&& GambleMenuTestAccess::playerStake(menu) == 79,
			"resource up button immediately transfers one silver into the wager") && ok;
		GambleMenuTestAccess::poll(menu);
		ok = check(GambleMenuTestAccess::currentBet(menu) == 1,
			"resource up callback is not repeated by frame polling") && ok;
	}

	{
		TestGambleMenu menu;
		auto primaryButton = std::make_shared<TestTextButton>();
		GambleMenuTestAccess::prepareDicePrimary(menu, primaryButton);

		primaryButton->triggerClick();
		ok = check(GambleMenuTestAccess::isDiceRolling(menu),
			"fallback TextButton callback starts the MG dice roll after betting") && ok;
		ok = check(menu.isLogicRunningForTest(),
			"starting the MG dice roll keeps the modal open") && ok;

		GambleMenuTestAccess::poll(menu);
		ok = check(menu.isLogicRunningForTest(),
			"cleared TextButton result is not executed a second time by polling") && ok;
	}

	{
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareResolvedGambleRound(menu, 1000);
		menu.setTime(2499);
		GambleMenuTestAccess::poll(menu);
		ok = check(GambleMenuTestAccess::isGambleRoundResolved(menu),
			"gamble result remains open before the 1500 ms display interval") && ok;
		menu.setTime(2500);
		GambleMenuTestAccess::poll(menu);
		ok = check(!GambleMenuTestAccess::isGambleRoundResolved(menu),
			"gamble result automatically returns to the covered table after 1500 ms") && ok;
	}

	{
		TestGambleMenu menu;
		auto pullButton = std::make_shared<TestButton>();
		auto reelButton = std::make_shared<TestButton>();
		GambleMenuTestAccess::prepareOverlappingFishControls(menu, pullButton, reelButton);
		ok = check(GambleMenuTestAccess::reelHasInputPriority(pullButton, reelButton),
			"ready reel control sorts before the overlapping pull control") && ok;
		reelButton->triggerClick();
		ok = check(GambleMenuTestAccess::fishCatchCount(menu) == 1,
			"ready reel callback completes the fish round") && ok;
	}

	{
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareGambleController(menu);
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm)
			&& GambleMenuTestAccess::currentBet(menu) == 1,
			"normal gamble controller focus confirms the shared increase-bet action") && ok;
		GambleMenuTestAccess::dispatchAction(menu, UIAction::NavigateUp);
		GambleMenuTestAccess::dispatchAction(menu, UIAction::NavigateRight);
		GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm);
		ok = check(!GambleMenuTestAccess::betsBig(menu),
			"normal gamble production geometry reaches the shared small-bet action") && ok;
	}

	{
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareDiceController(menu);
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::PageNext)
			&& GambleMenuTestAccess::diceStake(menu) == DiceGambleState::BetStep,
			"dice RT action adds one bet only while waiting") && ok;
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm)
			&& GambleMenuTestAccess::dicePhase(menu) == DiceGambleState::Phase::Rolling,
			"dice A action starts the shared roll path after betting") && ok;
	}

	{
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareDiceRevealing(menu);
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm)
			&& GambleMenuTestAccess::dicePhase(menu) == DiceGambleState::Phase::Revealing,
			"dice A is consumed during reveal without resolving the round again") && ok;
	}

	{
		TestGambleMenu menu;
		GambleMenuTestAccess::prepareFishIdle(menu);
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm)
			&& GambleMenuTestAccess::fishPhase(menu) == FishGameState::Phase::Casting,
			"fish A casts only from idle") && ok;
		GambleMenuTestAccess::prepareFishStruggling(menu);
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::Confirm)
			&& GambleMenuTestAccess::fishPhase(menu) == FishGameState::Phase::Struggling
			&& GambleMenuTestAccess::fishLostLives(menu) == 0,
			"fish A is consumed while struggling without calling makeMistake") && ok;
	}

	{
		TestGambleMenu menu;
		auto pullButton = std::make_shared<TestButton>();
		auto reelButton = std::make_shared<TestButton>();
		GambleMenuTestAccess::prepareOverlappingFishControls(menu, pullButton, reelButton);
		ok = check(GambleMenuTestAccess::dispatchAction(menu, UIAction::PageNext)
			&& GambleMenuTestAccess::fishCatchCount(menu) == 1,
			"fish RT reels only after the state reports canReel") && ok;
	}

	{
		TestGameManager gameManager;
		gameManager.player = std::make_shared<Player>();
		gameManager.player->money = 100;
		TestGambleMenu menu;
		GambleMenuTestAccess::resolveWinningDiceRound(menu, gameManager.player->money);
		ok = check(gameManager.player->money == 150,
			"resolved MG dice win applies the 50 silver economy delta once") && ok;
		GambleMenuTestAccess::poll(menu);
		ok = check(gameManager.player->money == 150,
			"polling after resolution does not apply the economy delta again") && ok;
	}

	{
		TestGameManager gameManager;
		gameManager.result = erNone;
		gameManager.setRunning(true);

		TestGambleMenu menu;
		GambleMenuTestAccess::prepareDiceModal(menu);
		AEvent quitEvent(ET_QUIT, 0, 0, 0);

		ok = check(GambleMenuTestAccess::dispatch(menu, quitEvent),
			"nested GambleMenu consumes ET_QUIT") && ok;
		ok = check((menu.result & erExit) != 0,
			"nested GambleMenu exits with erExit") && ok;
		ok = check((gameManager.result & erExit) != 0,
			"nested GambleMenu propagates erExit to GameManager") && ok;
		ok = check(!gameManager.isLogicRunningForTest(),
			"nested GambleMenu stops the outer GameManager loop") && ok;
	}

	{
		TestGameManager gameManager;
		gameManager.player = std::make_shared<Player>();
		gameManager.player->money = 4321;
		TestGambleMenu menu;
		GambleMenuTestAccess::reportFishCaught(menu);
		ok = check(GambleMenuTestAccess::fishWindowWon(menu),
			"Fish caught event records the modal result") && ok;
		ok = check(gameManager.player->money == 4321,
			"MG Fish result does not create a money reward") && ok;
	}

	try
	{
		ok = runGambleMenuPhysicalLinkTests() && ok;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAILED: gamble physical-link fixture: "
			<< exception.what() << '\n';
		ok = false;
	}

	return ok;
}
