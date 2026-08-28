#include "VirtualGamepadTestHarness.h"
#include "Input/ControllerBindingCatalog.h"
#include "Input/TouchControlsVisibilityPolicy.h"
#include "Input/GamepadConnectionObserver.h"
#include "Input/TouchControlsRecoveryGesture.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <iostream>

namespace
{
using GameInput::InputAction;
using GameInput::KeyboardInputDeviceID;
using GameInput::PhysicalInputManager;
using VirtualGamepadTest::VirtualGamepad;
using VirtualGamepadTest::connectedGamepadCount;
using VirtualGamepadTest::require;
using VirtualGamepadTest::runFrame;

Sint16 axisValue(float normalizedValue)
{
	return static_cast<Sint16>(std::round(normalizedValue * 32767.0f));
}

Sint16 triggerAxisValue(float normalizedValue)
{
	return static_cast<Sint16>(std::round(normalizedValue * 65535.0f - 32768.0f));
}

bool containsAction(
	std::initializer_list<InputAction> actions, InputAction action)
{
	for (InputAction candidate : actions)
	{
		if (candidate == action)
		{
			return true;
		}
	}
	return false;
}

void testControllerBindingCatalogContract()
{
	using GameInput::ControllerActionBinding;
	using GameInput::ControllerBindingTrigger;
	using GameInput::ControllerControl;
	using GameInput::ControllerDirection;
	struct ExpectedBinding
	{
		InputAction action;
		ControllerControl primary;
		ControllerControl modifier;
		ControllerDirection direction;
		ControllerBindingTrigger trigger;
		bool showDirectionInPrompt;
	};
	const std::array<ExpectedBinding, GameInput::InputActionCount>
		ExpectedBindings =
	{
		ExpectedBinding{ InputAction::AttackPrimary, ControllerControl::West,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::CastSkill1,
			ControllerControl::RightTrigger,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::TriggerThreshold, false },
		ExpectedBinding{ InputAction::CastSkill2, ControllerControl::North,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::CastSkill3, ControllerControl::East,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::CastSkill4, ControllerControl::South,
			ControllerControl::RightShoulder, ControllerDirection::None,
			ControllerBindingTrigger::ButtonChordHeld, false },
		ExpectedBinding{ InputAction::CastSkill5, ControllerControl::East,
			ControllerControl::RightShoulder, ControllerDirection::None,
			ControllerBindingTrigger::ButtonChordHeld, false },
		ExpectedBinding{ InputAction::UseQuickItem1,
			ControllerControl::LeftShoulder,
			ControllerControl::RightShoulder, ControllerDirection::None,
			ControllerBindingTrigger::ButtonChordHeld, false },
		ExpectedBinding{ InputAction::UseQuickItem2, ControllerControl::West,
			ControllerControl::RightShoulder, ControllerDirection::None,
			ControllerBindingTrigger::ButtonChordHeld, false },
		ExpectedBinding{ InputAction::UseQuickItem3, ControllerControl::North,
			ControllerControl::RightShoulder, ControllerDirection::None,
			ControllerBindingTrigger::ButtonChordHeld, false },
		ExpectedBinding{ InputAction::Jump, ControllerControl::LeftStick,
			ControllerControl::LeftShoulder, ControllerDirection::None,
			ControllerBindingTrigger::DirectionalChord, false },
		ExpectedBinding{ InputAction::InteractPrimary,
			ControllerControl::South,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::InteractAlternate,
			ControllerControl::RightShoulder,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonRelease, false },
		ExpectedBinding{ InputAction::CycleInteractionTarget,
			ControllerControl::LeftTrigger,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::TriggerThreshold, false },
		ExpectedBinding{ InputAction::ToggleMiniMap,
			ControllerControl::LeftStickButton,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::ToggleSit,
			ControllerControl::RightStickButton,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::OpenSystemMenu, ControllerControl::Start,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonRelease, false },
		ExpectedBinding{ InputAction::OpenSettings, ControllerControl::DPad,
			ControllerControl::RightShoulder, ControllerDirection::Up,
			ControllerBindingTrigger::ButtonChordHeld, true },
		ExpectedBinding{ InputAction::OpenMemo, ControllerControl::DPad,
			ControllerControl::RightShoulder, ControllerDirection::Down,
			ControllerBindingTrigger::ButtonChordHeld, true },
		ExpectedBinding{ InputAction::OpenEquip, ControllerControl::DPad,
			ControllerControl::RightShoulder, ControllerDirection::Left,
			ControllerBindingTrigger::ButtonChordHeld, true },
		ExpectedBinding{ InputAction::OpenGoods, ControllerControl::DPad,
			ControllerControl::RightShoulder, ControllerDirection::Right,
			ControllerBindingTrigger::ButtonChordHeld, true },
		ExpectedBinding{ InputAction::NavigateUp, ControllerControl::DPad,
			ControllerControl::None, ControllerDirection::Up,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::NavigateDown, ControllerControl::DPad,
			ControllerControl::None, ControllerDirection::Down,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::NavigateLeft, ControllerControl::DPad,
			ControllerControl::None, ControllerDirection::Left,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::NavigateRight, ControllerControl::DPad,
			ControllerControl::None, ControllerDirection::Right,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::Confirm, ControllerControl::South,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::Cancel, ControllerControl::East,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::Secondary, ControllerControl::West,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::ShowDetails, ControllerControl::North,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonHeld, false },
		ExpectedBinding{ InputAction::PreviousPanel,
			ControllerControl::LeftShoulder,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonRelease, false },
		ExpectedBinding{ InputAction::NextPanel,
			ControllerControl::RightShoulder,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::ButtonRelease, false },
		ExpectedBinding{ InputAction::PreviousPage,
			ControllerControl::LeftTrigger,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::TriggerThreshold, false },
		ExpectedBinding{ InputAction::NextPage,
			ControllerControl::RightTrigger,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::TriggerThreshold, false },
		ExpectedBinding{ InputAction::ToggleTouchControls,
			ControllerControl::Start, ControllerControl::Back,
			ControllerDirection::None,
			ControllerBindingTrigger::LongPressChord, false },
		ExpectedBinding{ InputAction::Move, ControllerControl::LeftStick,
			ControllerControl::None, ControllerDirection::None,
			ControllerBindingTrigger::StickContinuous, false },
		ExpectedBinding{ InputAction::ScrollUp, ControllerControl::RightStick,
			ControllerControl::None, ControllerDirection::Up,
			ControllerBindingTrigger::StickDirectional, false },
		ExpectedBinding{ InputAction::ScrollDown, ControllerControl::RightStick,
			ControllerControl::None, ControllerDirection::Down,
			ControllerBindingTrigger::StickDirectional, false },
		ExpectedBinding{ InputAction::ScrollLeft, ControllerControl::RightStick,
			ControllerControl::None, ControllerDirection::Left,
			ControllerBindingTrigger::StickDirectional, false },
		ExpectedBinding{ InputAction::ScrollRight, ControllerControl::RightStick,
			ControllerControl::None, ControllerDirection::Right,
			ControllerBindingTrigger::StickDirectional, false },
	};

	require(GameInput::DefaultControllerBindings.size()
		== ExpectedBindings.size(),
		"default controller catalog changed its action count");
	for (std::size_t index = 0; index < ExpectedBindings.size(); index++)
	{
		const ControllerActionBinding& actual =
			GameInput::DefaultControllerBindings[index];
		const ExpectedBinding& expected = ExpectedBindings[index];
		require(actual.action == expected.action
			&& actual.primary == expected.primary
			&& actual.modifier == expected.modifier
			&& actual.direction == expected.direction
			&& actual.trigger == expected.trigger
			&& actual.showDirectionInPrompt == expected.showDirectionInPrompt,
			"default controller catalog diverged at action index "
				+ std::to_string(index));
		require(GameInput::defaultControllerBinding(expected.action) == &actual,
			"default controller lookup diverged at action index "
				+ std::to_string(index));
	}
	require(GameInput::defaultControllerBinding(InputAction::Count) == nullptr,
		"invalid action unexpectedly resolved a controller binding");
	require(GameInput::defaultControllerBindingMatchCount(
		ControllerControl::South,
		ControllerBindingTrigger::ButtonHeld) == 2,
		"shared South binding lost one of its independent actions");
	require(GameInput::findUniqueDefaultControllerAction(
		ControllerControl::South,
		ControllerBindingTrigger::ButtonHeld) == InputAction::Count,
		"ambiguous shared binding incorrectly resolved as a unique action");
	require(GameInput::defaultControllerBindingMatchCount(
		ControllerControl::West,
		ControllerBindingTrigger::ButtonHeld) == 2,
		"shared West attack and secondary binding is incomplete");
	require(GameInput::defaultControllerBindingMatchCount(
		ControllerControl::RightTrigger,
		ControllerBindingTrigger::TriggerThreshold) == 2,
		"shared RT skill and next-page binding is incomplete");
	require(GameInput::defaultControllerBindingMatchCount(
		ControllerControl::RightShoulder,
		ControllerBindingTrigger::ButtonRelease) == 2,
		"shared RB alternate-interaction and next-panel binding is incomplete");
	require(GameInput::findUniqueDefaultControllerAction(
		ControllerControl::LeftStick,
		ControllerBindingTrigger::DirectionalChord,
		ControllerControl::LeftShoulder) == InputAction::Jump,
		"unique directional jump binding did not resolve");
	const GameInput::ControllerControlLocation dpadLeft =
		GameInput::controllerControlForButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
	require(dpadLeft.control == ControllerControl::DPad
		&& dpadLeft.direction == ControllerDirection::Left,
		"D-pad button location did not preserve its direction");
	require(!GameInput::controllerControlForButton(
		SDL_GAMEPAD_BUTTON_MISC1).valid(),
		"unmapped gamepad button unexpectedly resolved a control location");
}

void requireOnlyPressed(
	const PhysicalInputManager& inputManager,
	std::initializer_list<InputAction> expectedActions,
	const std::string& context)
{
	for (std::size_t index = 0; index < GameInput::InputActionCount; index++)
	{
		const InputAction action = static_cast<InputAction>(index);
		const bool expected = containsAction(expectedActions, action);
		require(inputManager.wasActionPressed(action) == expected,
			context + " produced an unexpected pressed-action set at index "
				+ std::to_string(index));
	}
}

void testTouchControlsVisibilityPolicy()
{
	GameInput::TouchControlsVisibilityPolicy policy;
	auto decision = policy.update(0, 0, 0, true);
	require(!decision.showGamepadConnectedMessage
		&& !decision.restoreTouchControls,
		"empty startup produced an external-input visibility action");

	decision = policy.update(1, 1, 0, true);
	require(decision.showGamepadConnectedMessage
		&& !decision.restoreTouchControls,
		"gamepad connection did not produce a notice-only decision");
	decision = policy.update(1, 1, 0, true);
	require(!decision.showGamepadConnectedMessage
		&& !decision.restoreTouchControls,
		"stable removal revision changed touch visibility");
	decision = policy.update(1, 1, 1, false);
	require(!decision.showGamepadConnectedMessage
		&& decision.restoreTouchControls,
		"active gamepad loss did not restore hidden touch controls");

	policy.reset();
	decision = policy.update(1, 1, 0, false, false);
	require(decision.showGamepadConnectedMessage
		&& !decision.restoreTouchControls,
		"desktop startup did not keep never-enabled touch controls hidden");
	decision = policy.update(0, 1, 1, false, false);
	require(!decision.restoreTouchControls,
		"active gamepad loss exposed never-enabled desktop touch controls");

	policy.reset();
	decision = policy.update(1, 1, 0, true);
	decision = policy.update(2, 2, 1, true);
	require(decision.showGamepadConnectedMessage
		&& !decision.restoreTouchControls,
		"additional gamepad connection did not remain notice-only");
	decision = policy.update(2, 2, 1, false);
	require(!decision.restoreTouchControls,
		"resume or re-enumeration incorrectly restored hidden controls");
	decision = policy.update(1, 2, 2, true);
	require(!decision.restoreTouchControls,
		"active gamepad loss redundantly restored already-visible controls");

	policy.reset();
	decision = policy.update(1, 3, 0, true);
	require(decision.showGamepadConnectedMessage,
		"policy reset lost startup connection detection");
}

void testGamepadConnectionObserver()
{
	GameInput::GamepadConnectionObserver observer;
	require(!observer.update(0, 0),
		"empty startup produced a gamepad connection edge");
	require(observer.update(1, 1),
		"first gamepad connection was not detected");
	require(!observer.update(1, 1),
		"stable gamepad registration repeated the connection edge");
	require(observer.update(2, 2),
		"additional gamepad connection was not detected");
	require(!observer.update(1, 2) && !observer.update(0, 2),
		"gamepad removal produced a connection edge");
	require(observer.update(1, 3),
		"gamepad reconnect after removal was not detected");
	require(observer.update(1, 4),
		"same-count gamepad replacement was not detected");

	observer.reset();
	require(observer.update(1, 4),
		"observer reset lost startup connection detection");
}

void testTouchControlsRecoveryGesture()
{
	using GameInput::TouchRecoveryContact;
	GameInput::TouchControlsRecoveryGesture gesture;
	std::vector<TouchRecoveryContact> contacts =
	{
		{ 3, 30, 30 },
		{ 1, 10, 10 },
		{ 2, 20, 20 },
	};
	require(!gesture.update(contacts, 100, false),
		"three-finger recovery triggered on its initial frame");
	require(!gesture.update(contacts, 1099, false),
		"three-finger recovery triggered before the hold interval");
	require(gesture.update(contacts, 1100, false),
		"three-finger recovery did not trigger at the hold interval");
	require(!gesture.update(contacts, 1200, false),
		"three-finger recovery repeated while contacts stayed down");

	gesture.reset();
	require(!gesture.update(contacts, 2000, false),
		"gesture reset did not start a fresh hold");
	contacts[0].x +=
		GameInput::TouchControlsRecoveryGesture::MaximumMovementPixels + 1;
	require(!gesture.update(contacts, 3000, false),
		"movement beyond tolerance incorrectly completed the old hold");
	require(!gesture.update(contacts, 3999, false),
		"movement restart did not reset the hold interval");
	require(gesture.update(contacts, 4000, false),
		"stationary contacts did not complete after a movement restart");

	gesture.reset();
	require(!gesture.update(contacts, 5000, false),
		"fresh contacts did not initialize after reset");
	contacts[0].id = 9;
	require(!gesture.update(contacts, 6000, false),
		"contact identity replacement incorrectly completed the old hold");
	require(!gesture.update(contacts, 6999, false),
		"contact identity replacement did not reset the hold interval");
	require(gesture.update(contacts, 7000, false),
		"replacement contacts did not complete their own hold");
	require(!gesture.update(contacts, 8000, true),
		"visible touch controls produced a recovery action");
}

void testKeyboardTouchControlsShortcut()
{
	SDL_Event event{};
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.scancode = SDL_SCANCODE_H;
	event.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
	event.key.repeat = false;

	PhysicalInputManager desktopInputManager;
	desktopInputManager.beginFrame();
	require(!desktopInputManager.processEvent(event)
		&& !desktopInputManager.wasActionPressed(
			InputAction::ToggleTouchControls),
		"desktop Ctrl+Shift+H was consumed or generated a mobile-only action");

	PhysicalInputManager inputManager(true);
	inputManager.beginFrame();
	require(inputManager.processEvent(event),
		"Ctrl+Shift+H was not consumed as a global shortcut");
	require(inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"Ctrl+Shift+H did not pulse touch-control visibility");
	require(inputManager.action(InputAction::ToggleTouchControls).sourceDeviceID
		== KeyboardInputDeviceID,
		"keyboard touch-control shortcut lost its source device identity");
	inputManager.releaseForContextTransition();
	require(inputManager.wasActionPressed(InputAction::ToggleTouchControls)
		&& inputManager.action(InputAction::ToggleTouchControls).sourceDeviceID
			== KeyboardInputDeviceID,
		"context transition did not preserve the complete global-action frame");

	inputManager.beginFrame();
	event.key.repeat = true;
	require(inputManager.processEvent(event)
		&& !inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"repeated Ctrl+Shift+H produced another toggle");

	inputManager.beginFrame();
	event.key.repeat = false;
	inputManager.setWindowFocused(false);
	require(inputManager.processEvent(event)
		&& !inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"unfocused Ctrl+Shift+H leaked into global actions");
	inputManager.setWindowFocused(true);

	inputManager.beginFrame();
	inputManager.suspendInput();
	require(inputManager.processEvent(event)
		&& !inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"suspended Ctrl+Shift+H leaked into global actions");
	inputManager.resumeInput();

	inputManager.beginFrame();
	event.key.mod = SDL_KMOD_CTRL;
	require(!inputManager.processEvent(event)
		&& !inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"partial keyboard chord was consumed or toggled controls");
}

void testTouchControlsGamepadChordOrdersAndCancellation()
{
	{
		VirtualGamepad gamepad("JXQY Reverse Touch Chord Pad");
		PhysicalInputManager inputManager(true);
		require(inputManager.initialize(),
			"reverse touch-chord input manager did not initialize");
		const std::uint64_t chordStart = SDL_GetTicks();
		runFrame(inputManager, chordStart);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
		runFrame(inputManager, chordStart + 10);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, true);
		runFrame(inputManager, chordStart + 11);
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds - 50);
		require(!inputManager.wasActionPressed(InputAction::ToggleTouchControls),
			"Start+Back chord fired before its hold interval");
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 50);
		require(inputManager.wasActionPressed(InputAction::ToggleTouchControls),
			"Start+Back press order did not pulse touch-control visibility");
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 150);
		require(!inputManager.wasActionPressed(InputAction::ToggleTouchControls),
			"held touch-control chord repeated after its one-shot pulse");
		gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, false);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_START, false);
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 160);
		require(!inputManager.wasActionPressed(InputAction::OpenSystemMenu),
			"reverse touch-control chord leaked the Start action");
		inputManager.shutdown();
	}

	{
		VirtualGamepad gamepad("JXQY Cancelled Touch Chord Pad");
		PhysicalInputManager inputManager(true);
		require(inputManager.initialize(),
			"cancelled touch-chord input manager did not initialize");
		const std::uint64_t chordStart = SDL_GetTicks();
		runFrame(inputManager, chordStart);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, true);
		runFrame(inputManager, chordStart + 10);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
		runFrame(inputManager, chordStart + 11);
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds / 2);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, false);
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds / 2 + 1);
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 50);
		require(!inputManager.wasActionPressed(InputAction::ToggleTouchControls),
			"released touch-control chord completed after cancellation");
		gamepad.setButton(SDL_GAMEPAD_BUTTON_START, false);
		runFrame(inputManager, chordStart
			+ PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 60);
		require(!inputManager.wasActionPressed(InputAction::OpenSystemMenu),
			"cancelled touch-control chord leaked the Start action");
		inputManager.shutdown();
	}
}

void testDefaultBindingTable()
{
	VirtualGamepad gamepad("JXQY Binding Table Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_MISC1, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_MISC1, false);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.hasActiveGamepad(),
		"unmapped fresh button could not claim the binding-test gamepad");

	struct ButtonBinding
	{
		SDL_GamepadButton button;
		std::initializer_list<InputAction> actions;
		const char* name;
	};
	const ButtonBinding buttonBindings[] =
	{
		{ SDL_GAMEPAD_BUTTON_SOUTH,
			{ InputAction::InteractPrimary, InputAction::Confirm }, "South" },
		{ SDL_GAMEPAD_BUTTON_EAST,
			{ InputAction::CastSkill3, InputAction::Cancel }, "East" },
		{ SDL_GAMEPAD_BUTTON_WEST,
			{ InputAction::AttackPrimary, InputAction::Secondary }, "West" },
		{ SDL_GAMEPAD_BUTTON_NORTH,
			{ InputAction::CastSkill2, InputAction::ShowDetails }, "North" },
		{ SDL_GAMEPAD_BUTTON_LEFT_STICK,
			{ InputAction::ToggleMiniMap }, "L3" },
		{ SDL_GAMEPAD_BUTTON_RIGHT_STICK,
			{ InputAction::ToggleSit }, "R3" },
		{ SDL_GAMEPAD_BUTTON_DPAD_UP,
			{ InputAction::NavigateUp }, "D-pad up" },
		{ SDL_GAMEPAD_BUTTON_DPAD_DOWN,
			{ InputAction::NavigateDown }, "D-pad down" },
		{ SDL_GAMEPAD_BUTTON_DPAD_LEFT,
			{ InputAction::NavigateLeft }, "D-pad left" },
		{ SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
			{ InputAction::NavigateRight }, "D-pad right" },
	};
	for (const ButtonBinding& binding : buttonBindings)
	{
		gamepad.setButton(binding.button, true);
		runFrame(inputManager, nowMilliseconds += 10);
		requireOnlyPressed(inputManager, binding.actions, binding.name);
		for (InputAction action : binding.actions)
		{
			require(inputManager.isActionDown(action),
				std::string(binding.name) + " did not hold its bound action");
		}
		runFrame(inputManager, nowMilliseconds += 10);
		requireOnlyPressed(inputManager, {},
			std::string(binding.name) + " held frame");
		gamepad.setButton(binding.button, false);
		runFrame(inputManager, nowMilliseconds += 10);
		for (InputAction action : binding.actions)
		{
			require(inputManager.wasActionReleased(action)
				&& !inputManager.isActionDown(action),
				std::string(binding.name) + " did not release its bound action");
		}
	}

	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager, {}, "Start press");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::OpenSystemMenu }, "Start release");

	gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
	runFrame(inputManager, nowMilliseconds += 10);
	runFrame(inputManager, nowMilliseconds +=
		PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 10);
	require(!inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"desktop Back+Start generated a mobile-only touch-control action");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::OpenSystemMenu },
		"desktop Start release after Back hold");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, false);
	runFrame(inputManager, nowMilliseconds += 10);

	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::PreviousPanel }, "LB release");

	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::InteractAlternate, InputAction::NextPanel }, "RB release");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, triggerAxisValue(0.70f));
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::CycleInteractionTarget, InputAction::PreviousPage }, "LT");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, triggerAxisValue(0.0f));
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.wasActionReleased(InputAction::CycleInteractionTarget)
		&& inputManager.wasActionReleased(InputAction::PreviousPage),
		"LT did not release both independently bound actions");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, triggerAxisValue(0.70f));
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::CastSkill1, InputAction::NextPage }, "RT");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, triggerAxisValue(0.0f));
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.wasActionReleased(InputAction::CastSkill1)
		&& inputManager.wasActionReleased(InputAction::NextPage),
		"RT did not release both independently bound actions");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.8f));
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager, {}, "LB directional jump grace frame");
	runFrame(inputManager,
		nowMilliseconds += PhysicalInputManager::ShoulderChordGraceMilliseconds);
	requireOnlyPressed(inputManager, { InputAction::Jump }, "LB directional jump");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager, {}, "consumed LB release");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	runFrame(inputManager, nowMilliseconds += 10);

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.8f));
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager, {}, "short LB jump press");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager, { InputAction::Jump }, "short LB jump release");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	runFrame(inputManager, nowMilliseconds += 10);

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.8f));
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::UseQuickItem1 },
		"moving LB then RB shoulder chord");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.wasActionPressed(InputAction::Jump),
		"moving LB then RB shoulder chord leaked a jump");

	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::UseQuickItem1 },
		"moving RB then LB shoulder chord");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.wasActionPressed(InputAction::Jump),
		"moving RB then LB shoulder chord leaked a jump");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	runFrame(inputManager, nowMilliseconds += 10);

	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager,
		{ InputAction::UseQuickItem1 }, "LB+RB quick item");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
	runFrame(inputManager, nowMilliseconds += 10);
	requireOnlyPressed(inputManager, {}, "consumed shoulder-pair release");

	struct ChordBinding
	{
		SDL_GamepadButton button;
		InputAction action;
		const char* name;
	};
	const ChordBinding chordBindings[] =
	{
		{ SDL_GAMEPAD_BUTTON_SOUTH, InputAction::CastSkill4, "RB+South" },
		{ SDL_GAMEPAD_BUTTON_EAST, InputAction::CastSkill5, "RB+East" },
		{ SDL_GAMEPAD_BUTTON_WEST, InputAction::UseQuickItem2, "RB+West" },
		{ SDL_GAMEPAD_BUTTON_NORTH, InputAction::UseQuickItem3, "RB+North" },
		{ SDL_GAMEPAD_BUTTON_DPAD_UP, InputAction::OpenSettings, "RB+D-pad up" },
		{ SDL_GAMEPAD_BUTTON_DPAD_DOWN, InputAction::OpenMemo, "RB+D-pad down" },
		{ SDL_GAMEPAD_BUTTON_DPAD_LEFT, InputAction::OpenEquip, "RB+D-pad left" },
		{ SDL_GAMEPAD_BUTTON_DPAD_RIGHT, InputAction::OpenGoods, "RB+D-pad right" },
	};
	for (const ChordBinding& binding : chordBindings)
	{
		gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
		runFrame(inputManager, nowMilliseconds += 10);
		gamepad.setButton(binding.button, true);
		runFrame(inputManager, nowMilliseconds += 10);
		requireOnlyPressed(inputManager, { binding.action }, binding.name);
		gamepad.setButton(binding.button, false);
		runFrame(inputManager, nowMilliseconds += 10);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
		runFrame(inputManager, nowMilliseconds += 10);
		requireOnlyPressed(inputManager, {},
			std::string(binding.name) + " consumed RB release");
	}

	inputManager.shutdown();
}

void testActionsAndAxisHysteresis()
{
	VirtualGamepad gamepad("JXQY Input Action Pad");
	PhysicalInputManager inputManager(true);
	require(inputManager.initialize(), "input manager did not initialize");
	runFrame(inputManager, 0);
	require(inputManager.registeredGamepadCount() == connectedGamepadCount(),
		"startup enumeration did not register the virtual gamepad");
	require(!inputManager.hasActiveGamepad(),
		"enumeration must not implicitly claim a gamepad");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.30f));
	runFrame(inputManager, 10);
	require(!inputManager.hasActiveGamepad(),
		"stick drift below the claim threshold claimed the gamepad");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	runFrame(inputManager, 20);
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.90f));
	runFrame(inputManager, 30);
	require(inputManager.activeGamepadID() == gamepad.id(),
		"fresh stick threshold crossing did not claim the gamepad");
	require(inputManager.wasActionPressed(InputAction::Move)
		&& inputManager.isActionDown(InputAction::Move),
		"left stick did not produce the continuous move action");
	require(inputManager.action(InputAction::Move).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(gamepad.id())
		&& inputManager.action(InputAction::Move).axis.leftStick.magnitude > 0.8f,
		"move action did not preserve its source and processed axis snapshot");
	require(inputManager.axes().running, "run press threshold was not applied");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.68f));
	runFrame(inputManager, 40);
	require(inputManager.axes().running,
		"run state released before crossing the release threshold");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, axisValue(0.60f));
	runFrame(inputManager, 50);
	require(!inputManager.axes().running,
		"run state did not release after crossing the release threshold");
	SDL_Event keyboardToggle{};
	keyboardToggle.type = SDL_EVENT_KEY_DOWN;
	keyboardToggle.key.scancode = SDL_SCANCODE_H;
	keyboardToggle.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
	inputManager.beginFrame();
	require(inputManager.processEvent(keyboardToggle)
		&& inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"keyboard toggle was not accepted while a gamepad was active");
	const auto& keyboardToggleState =
		inputManager.action(InputAction::ToggleTouchControls);
	require(keyboardToggleState.sourceDeviceID == KeyboardInputDeviceID
		&& keyboardToggleState.axis.leftStick.magnitude == 0.0f
		&& keyboardToggleState.axis.rightStick.magnitude == 0.0f
		&& keyboardToggleState.axis.leftTrigger == 0.0f
		&& keyboardToggleState.axis.rightTrigger == 0.0f,
		"keyboard action mixed its source with the active gamepad axes");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, triggerAxisValue(0.60f));
	runFrame(inputManager, 60);
	require(inputManager.wasActionPressed(InputAction::CastSkill1),
		"right trigger did not press skill 1");
	require(inputManager.action(InputAction::CastSkill1).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(gamepad.id())
		&& inputManager.action(InputAction::CastSkill1).axis.rightTrigger
			>= PhysicalInputManager::TriggerPressThreshold,
		"trigger action did not preserve its source and analog snapshot");
	require(inputManager.isActionDown(InputAction::NextPage),
		"right trigger did not hold next-page action");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, triggerAxisValue(0.50f));
	runFrame(inputManager, 70);
	require(inputManager.isActionDown(InputAction::CastSkill1),
		"trigger hysteresis released above the release threshold");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, triggerAxisValue(0.40f));
	runFrame(inputManager, 80);
	require(inputManager.wasActionReleased(InputAction::CastSkill1),
		"trigger action did not synthesize release (value="
			+ std::to_string(inputManager.axes().rightTrigger)
			+ ", down="
			+ std::to_string(inputManager.isActionDown(InputAction::CastSkill1))
			+ ")");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, axisValue(0.80f));
	runFrame(inputManager, 85);
	inputManager.beginFrame();
	SDL_Event interactionPressed{};
	interactionPressed.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
	interactionPressed.gbutton.which = gamepad.id();
	interactionPressed.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
	inputManager.processEvent(interactionPressed);
	SDL_Event aimChanged{};
	aimChanged.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
	aimChanged.gaxis.which = gamepad.id();
	aimChanged.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHTX;
	aimChanged.gaxis.value = axisValue(-0.80f);
	inputManager.processEvent(aimChanged);
	inputManager.update(90);
	require(inputManager.wasActionPressed(InputAction::InteractPrimary),
		"south button did not press primary interaction");
	require(inputManager.action(InputAction::InteractPrimary).sourceDeviceID
			== static_cast<GameInput::InputDeviceID>(gamepad.id())
		&& inputManager.action(InputAction::InteractPrimary).axis.rightStick.x > 0.7f
		&& inputManager.axes().rightStick.x < -0.7f,
		"button action did not preserve its press-edge source and axis snapshot");
	require(inputManager.isActionDown(InputAction::Confirm),
		"south button did not hold confirm");
	require(inputManager.consumePressed(InputAction::Confirm),
		"confirm press could not be consumed");
	require(!inputManager.wasActionPressed(InputAction::Confirm),
		"consumed press remained visible");
	inputManager.beginFrame();
	SDL_Event interactionReleased = interactionPressed;
	interactionReleased.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
	inputManager.processEvent(interactionReleased);
	aimChanged.gaxis.value = 0;
	inputManager.processEvent(aimChanged);
	inputManager.update(100);
	require(inputManager.wasActionReleased(InputAction::InteractPrimary),
		"south button release did not release primary interaction");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
	runFrame(inputManager, 105);

	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
	runFrame(inputManager, 110);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_WEST, true);
	runFrame(inputManager, 120);
	require(inputManager.wasActionPressed(InputAction::UseQuickItem2),
		"right-shoulder chord did not select quick item 2");
	require(!inputManager.wasActionPressed(InputAction::AttackPrimary),
		"right-shoulder chord leaked the unmodified attack action");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_WEST, false);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
	runFrame(inputManager, 130);
	require(inputManager.wasActionReleased(InputAction::UseQuickItem2),
		"right-shoulder chord did not release its action");

	const std::uint64_t chordStart = SDL_GetTicks();
	gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, true);
	runFrame(inputManager, chordStart);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
	runFrame(inputManager, chordStart + 1);
	runFrame(inputManager,
		chordStart + PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 2);
	require(inputManager.wasActionPressed(InputAction::ToggleTouchControls),
		"Back+Start hold did not pulse the global touch-control action");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, false);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, false);
	runFrame(inputManager,
		chordStart + PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 12);
	require(!inputManager.wasActionPressed(InputAction::OpenSystemMenu),
		"consumed Back+Start chord leaked the Start action");

	const std::uint64_t repeatStart = chordStart + 1000;
	gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
	runFrame(inputManager, repeatStart);
	require(inputManager.wasActionPressed(InputAction::NavigateDown),
		"D-pad did not emit its initial navigation press");
	runFrame(inputManager,
		repeatStart + PhysicalInputManager::NavigationRepeatDelayMilliseconds - 1);
	require(!inputManager.wasActionPressed(InputAction::NavigateDown),
		"D-pad repeated before the configured delay");
	runFrame(inputManager,
		repeatStart + PhysicalInputManager::NavigationRepeatDelayMilliseconds);
	require(inputManager.wasActionPressed(InputAction::NavigateDown),
		"D-pad did not repeat after the configured delay");
	const std::uint64_t repressStart = repeatStart
		+ PhysicalInputManager::NavigationRepeatDelayMilliseconds + 10;
	inputManager.beginFrame();
	SDL_Event navigationEdge{};
	navigationEdge.gbutton.which = gamepad.id();
	navigationEdge.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	navigationEdge.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
	inputManager.processEvent(navigationEdge);
	navigationEdge.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
	inputManager.processEvent(navigationEdge);
	inputManager.update(repressStart);
	require(inputManager.wasActionPressed(InputAction::NavigateDown),
		"same-frame D-pad release and press lost its fresh edge");
	runFrame(inputManager,
		repressStart + PhysicalInputManager::NavigationRepeatDelayMilliseconds - 1);
	require(!inputManager.wasActionPressed(InputAction::NavigateDown),
		"same-frame D-pad repress reused the previous repeat timer");
	runFrame(inputManager,
		repressStart + PhysicalInputManager::NavigationRepeatDelayMilliseconds);
	require(inputManager.wasActionPressed(InputAction::NavigateDown),
		"same-frame D-pad repress did not receive a full repeat delay");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
	runFrame(inputManager,
		repressStart + PhysicalInputManager::NavigationRepeatDelayMilliseconds + 10);

	const std::uint64_t scrollStart = repeatStart + 1000;
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, axisValue(0.80f));
	runFrame(inputManager, scrollStart);
	require(inputManager.wasActionPressed(InputAction::ScrollDown)
		&& inputManager.isActionDown(InputAction::ScrollDown),
		"right stick did not press its independent scroll-down action");
	require(inputManager.action(InputAction::ScrollDown).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(gamepad.id())
		&& inputManager.action(InputAction::ScrollDown).axis.rightStick.y > 0.7f,
		"right-stick scroll action lost its source or processed axis snapshot");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, axisValue(0.50f));
	runFrame(inputManager, scrollStart + 10);
	require(inputManager.isActionDown(InputAction::ScrollDown),
		"right-stick scroll released above its hysteresis threshold");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, axisValue(0.40f));
	runFrame(inputManager, scrollStart + 20);
	require(inputManager.wasActionReleased(InputAction::ScrollDown)
		&& !inputManager.isActionDown(InputAction::ScrollDown),
		"right-stick scroll did not release below its hysteresis threshold");

	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, axisValue(-0.80f));
	runFrame(inputManager, scrollStart + 30);
	require(inputManager.wasActionPressed(InputAction::ScrollUp),
		"right stick did not select the opposite vertical direction");
	runFrame(inputManager,
		scrollStart + 30
			+ PhysicalInputManager::NavigationRepeatDelayMilliseconds - 1);
	require(!inputManager.wasActionPressed(InputAction::ScrollUp),
		"right-stick scroll repeated before the configured delay");
	runFrame(inputManager,
		scrollStart + 30
			+ PhysicalInputManager::NavigationRepeatDelayMilliseconds);
	require(inputManager.wasActionPressed(InputAction::ScrollUp),
		"right-stick scroll did not repeat after the configured delay");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, 0);
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, axisValue(0.80f));
	runFrame(inputManager,
		scrollStart + 40
			+ PhysicalInputManager::NavigationRepeatDelayMilliseconds);
	require(inputManager.wasActionReleased(InputAction::ScrollUp)
		&& inputManager.wasActionPressed(InputAction::ScrollRight),
		"right stick did not switch its locked primary direction after release");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
	runFrame(inputManager,
		scrollStart + 50
			+ PhysicalInputManager::NavigationRepeatDelayMilliseconds);
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, axisValue(0.70f));
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, axisValue(0.90f));
	runFrame(inputManager,
		scrollStart + 60
			+ PhysicalInputManager::NavigationRepeatDelayMilliseconds);
	require(inputManager.wasActionPressed(InputAction::ScrollDown)
		&& !inputManager.wasActionPressed(InputAction::ScrollRight),
		"right-stick direction used an intermediate axis event instead of the final dominant axis");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, 0);
	runFrame(inputManager,
		scrollStart + 70
			+ PhysicalInputManager::NavigationRepeatDelayMilliseconds);

	inputManager.shutdown();
}

void testContextAndLifecycleNeutralGates()
{
	VirtualGamepad gamepad("JXQY Lifecycle Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "input manager did not initialize");
	runFrame(inputManager, 0);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, 10);
	require(inputManager.hasActiveGamepad(), "button press did not claim gamepad");
	require(inputManager.isActionDown(InputAction::Confirm),
		"confirm was not held before suspension");

	inputManager.suspendInput();
	require(inputManager.wasActionReleased(InputAction::Confirm),
		"suspension did not synthesize release");
	require(inputManager.action(InputAction::Confirm).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(gamepad.id()),
		"synthesized release lost the disconnected input source identity");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	runFrame(inputManager, 20);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, 30);
	require(!inputManager.isActionDown(InputAction::Confirm),
		"background input leaked into actions");

	inputManager.resumeInput();
	runFrame(inputManager, 40);
	require(!inputManager.hasActiveGamepad(),
		"foreground re-enumeration implicitly restored an active gamepad");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	runFrame(inputManager, 50);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, 60);
	require(inputManager.hasActiveGamepad(),
		"neutral then fresh input did not reclaim after resume");

	inputManager.setWindowFocused(false);
	require(inputManager.wasActionReleased(InputAction::Confirm),
		"focus loss did not release held actions");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	runFrame(inputManager, 70);
	inputManager.setWindowFocused(true);
	runFrame(inputManager, 80);
	require(!inputManager.isActionDown(InputAction::Confirm),
		"focus restoration replayed an old action");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, 90);
	require(inputManager.wasActionPressed(InputAction::Confirm),
		"fresh input after focus neutral gate was lost");

	inputManager.shutdown();
}
}

int main()
{
	try
	{
		VirtualGamepadTest::SDLSession sdlSession;
		testTouchControlsVisibilityPolicy();
		testGamepadConnectionObserver();
		testTouchControlsRecoveryGesture();
		testKeyboardTouchControlsShortcut();
		testTouchControlsGamepadChordOrdersAndCancellation();
		testControllerBindingCatalogContract();
		testDefaultBindingTable();
		testActionsAndAxisHysteresis();
		testContextAndLifecycleNeutralGates();
		std::cout << "input action tests passed\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
