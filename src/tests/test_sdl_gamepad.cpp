#include "VirtualGamepadTestHarness.h"

#include <array>
#include <iostream>
#include <string>

namespace
{
using GameInput::InputAction;
using GameInput::PhysicalInputManager;
using VirtualGamepadTest::VirtualGamepad;
using VirtualGamepadTest::connectedGamepadCount;
using VirtualGamepadTest::require;
using VirtualGamepadTest::runFrame;

struct AxisClaimCase
{
	const char* name;
	SDL_GamepadAxis axis;
	Sint16 pressedValue;
	Sint16 neutralValue;
	InputAction expectedAction;
};

constexpr std::array<AxisClaimCase, 3> AxisClaimCases =
{
	AxisClaimCase{ "Left Trigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		SDL_JOYSTICK_AXIS_MAX, SDL_JOYSTICK_AXIS_MIN,
		InputAction::CycleInteractionTarget },
	AxisClaimCase{ "Right Trigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
		SDL_JOYSTICK_AXIS_MAX, SDL_JOYSTICK_AXIS_MIN,
		InputAction::CastSkill1 },
	AxisClaimCase{ "Right Stick", SDL_GAMEPAD_AXIS_RIGHTX,
		SDL_JOYSTICK_AXIS_MAX, 0,
		InputAction::ScrollRight },
};

float observedAxisValue(
	const PhysicalInputManager& inputManager,
	SDL_GamepadAxis axis)
{
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
		return inputManager.axes().leftTrigger;
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		return inputManager.axes().rightTrigger;
	case SDL_GAMEPAD_AXIS_RIGHTX:
	case SDL_GAMEPAD_AXIS_RIGHTY:
		return inputManager.axes().rightStick.magnitude;
	default:
		return inputManager.axes().leftStick.magnitude;
	}
}

float observedActionAxisValue(
	const GameInput::InputActionState& actionState,
	SDL_GamepadAxis axis)
{
	switch (axis)
	{
	case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
		return actionState.axis.leftTrigger;
	case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		return actionState.axis.rightTrigger;
	case SDL_GAMEPAD_AXIS_RIGHTX:
	case SDL_GAMEPAD_AXIS_RIGHTY:
		return actionState.axis.rightStick.magnitude;
	default:
		return actionState.axis.leftStick.magnitude;
	}
}

void pressAndRelease(VirtualGamepad& gamepad, SDL_GamepadButton button,
	PhysicalInputManager& inputManager, std::uint64_t& nowMilliseconds)
{
	gamepad.setButton(button, true);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(button, false);
	runFrame(inputManager, nowMilliseconds += 10);
}

void testInitializationLifecycle()
{
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	require(SDL_WasInit(SDL_INIT_GAMEPAD) == 0,
		"gamepad subsystem remained initialized after explicit shutdown");

	PhysicalInputManager inputManager;
	require(!inputManager.initialize(),
		"input manager initialized without the SDL gamepad subsystem");
	inputManager.shutdown();
	inputManager.shutdown();

	require(SDL_InitSubSystem(SDL_INIT_GAMEPAD),
		std::string("SDL gamepad subsystem restoration failed: ")
			+ SDL_GetError());
	require(inputManager.initialize() && inputManager.initialize(),
		"input manager repeated initialization was not idempotent");
	inputManager.shutdown();
	inputManager.shutdown();
	require(SDL_WasInit(SDL_INIT_GAMEPAD) != 0,
		"input manager shutdown closed the externally owned SDL subsystem");
}

void testAxisClaims()
{
	for (const AxisClaimCase& claimCase : AxisClaimCases)
	{
		const std::string gamepadName = std::string("JXQY ")
			+ claimCase.name + " Claim Pad";
		VirtualGamepad gamepad(gamepadName.c_str());
		PhysicalInputManager inputManager;
		require(inputManager.initialize(),
			std::string(claimCase.name) + " input manager did not initialize");
		std::uint64_t nowMilliseconds = 0;
		runFrame(inputManager, nowMilliseconds);
		require(!inputManager.hasActiveGamepad(),
			std::string(claimCase.name) + " fixture started with an active gamepad");

		gamepad.setAxis(claimCase.axis, claimCase.pressedValue);
		runFrame(inputManager, nowMilliseconds += 10);
		const GameInput::InputActionState actionState =
			inputManager.action(claimCase.expectedAction);
		require(inputManager.activeGamepadID() == gamepad.id(),
			std::string(claimCase.name) + " did not claim from a fresh axis edge");
		require(actionState.pressed && actionState.down,
			std::string(claimCase.name) + " claim did not produce its action");
		require(actionState.sourceDeviceID
			== static_cast<GameInput::InputDeviceID>(gamepad.id()),
			std::string(claimCase.name) + " action lost its source device");
		require(observedAxisValue(inputManager, claimCase.axis) > 0.8f
			&& observedActionAxisValue(actionState, claimCase.axis) > 0.8f,
			std::string(claimCase.name) + " claim lost its live or action axis snapshot");

		gamepad.setAxis(claimCase.axis, claimCase.neutralValue);
		runFrame(inputManager, nowMilliseconds += 10);
		require(inputManager.wasActionReleased(claimCase.expectedAction)
			&& !inputManager.isActionDown(claimCase.expectedAction),
			std::string(claimCase.name) + " action did not release at neutral");
		inputManager.shutdown();
	}
}

void testMultiDeviceRegistrationAndClaim()
{
	VirtualGamepad firstGamepad("JXQY First Pad");
	VirtualGamepad secondGamepad("JXQY Second Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);
	require(inputManager.registeredGamepadCount() == connectedGamepadCount(),
		"startup enumeration did not open every gamepad");
	require(!inputManager.hasActiveGamepad(),
		"startup enumeration selected an active gamepad");

	secondGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == secondGamepad.id(),
		"the first fresh input did not win the claim");
	require(!inputManager.activeGamepadName().empty(),
		"active gamepad metadata did not retain its name");
	require(inputManager.buttonLabel(SDL_GAMEPAD_BUTTON_SOUTH)
			!= SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
		"active gamepad metadata did not retain its button label theme");
	require(inputManager.wasActionPressed(InputAction::Confirm),
		"claiming button did not reach the action layer");
	require(inputManager.action(InputAction::Confirm).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(secondGamepad.id()),
		"claiming action did not record the winning gamepad source");
	secondGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	runFrame(inputManager, nowMilliseconds += 10);

	firstGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == secondGamepad.id(),
		"an inactive device stole an existing claim");
	require(!inputManager.wasActionPressed(InputAction::Cancel),
		"inactive device leaked an action");
	firstGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, false);
	runFrame(inputManager, nowMilliseconds += 10);
	for (const AxisClaimCase& claimCase : AxisClaimCases)
	{
		firstGamepad.setAxis(claimCase.axis, claimCase.pressedValue);
		runFrame(inputManager, nowMilliseconds += 10);
		require(inputManager.activeGamepadID() == secondGamepad.id(),
			std::string("inactive ") + claimCase.name
				+ " stole the active-device claim");
		require(!inputManager.wasActionPressed(claimCase.expectedAction)
			&& !inputManager.isActionDown(claimCase.expectedAction)
			&& observedAxisValue(inputManager, claimCase.axis) <= 0.01f,
			std::string("inactive ") + claimCase.name
				+ " leaked an action or global axis");
		firstGamepad.setAxis(claimCase.axis, claimCase.neutralValue);
		runFrame(inputManager, nowMilliseconds += 10);
	}

	secondGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.isActionDown(InputAction::Cancel),
		"active device action was not held before disconnect");
	const std::uint64_t lifecycleBeforeDisconnect =
		inputManager.inputLifecycleRevision();
	const SDL_JoystickID disconnectedGamepadID = secondGamepad.id();
	secondGamepad.detach();
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.registeredGamepadCount() == connectedGamepadCount(),
		"removed gamepad remained registered");
	require(!inputManager.hasActiveGamepad(),
		"active removal automatically claimed another device");
	require(inputManager.wasActionReleased(InputAction::Cancel),
		"active removal did not synthesize release");
	require(inputManager.action(InputAction::Cancel).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(disconnectedGamepadID),
		"disconnect release lost the removed gamepad source identity (expected="
			+ std::to_string(disconnectedGamepadID)
			+ ", actual="
			+ std::to_string(inputManager.action(
				InputAction::Cancel).sourceDeviceID) + ")");
	require(inputManager.inputLifecycleRevision() > lifecycleBeforeDisconnect,
		"active removal did not advance the lifecycle revision");

	runFrame(inputManager, nowMilliseconds += 10);
	pressAndRelease(firstGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
		inputManager, nowMilliseconds);
	require(inputManager.activeGamepadID() == firstGamepad.id(),
		"remaining neutral device could not claim with fresh input");
	require(inputManager.action(InputAction::NavigateRight).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(firstGamepad.id()),
		"replacement device action retained the disconnected device source");

	VirtualGamepad thirdGamepad("JXQY Hotplug Pad");
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.registeredGamepadCount() == connectedGamepadCount(),
		"runtime-added gamepad was not registered");
	require(inputManager.activeGamepadID() == firstGamepad.id(),
		"hotplug changed the active gamepad");

	inputManager.shutdown();
}

void testRawButtonPressRevisionAndPresentationLabels()
{
	VirtualGamepad gamepad("JXQY Controller Help Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);

	require(!inputManager.hasActiveGamepad(),
		"controller-help fixture unexpectedly started with an active gamepad");
	require(inputManager.buttonLabel(SDL_GAMEPAD_BUTTON_SOUTH)
			== SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
		"inactive gamepad unexpectedly provided active-device labels");
	require(inputManager.presentationButtonLabel(SDL_GAMEPAD_BUTTON_SOUTH)
			!= SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
		"registered inactive gamepad did not provide presentation labels");

	const std::uint64_t initialRevision =
		inputManager.gamepadButtonPressRevision();
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.gamepadButtonPressRevision() == initialRevision,
		"gamepad stick motion advanced the raw button press revision");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	runFrame(inputManager, nowMilliseconds += 10);

	gamepad.setButton(SDL_GAMEPAD_BUTTON_MISC1, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.gamepadButtonPressRevision() == initialRevision + 1,
		"unmapped gamepad button press did not advance the raw revision");
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.gamepadButtonPressRevision() == initialRevision + 1,
		"held gamepad button repeated the raw press revision");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_MISC1, false);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.gamepadButtonPressRevision() == initialRevision + 1,
		"gamepad button release advanced the raw press revision");

	inputManager.suspendInput();
	gamepad.setButton(SDL_GAMEPAD_BUTTON_NORTH, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.gamepadButtonPressRevision() == initialRevision + 1,
		"suspended gamepad button press advanced the raw revision");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_NORTH, false);
	runFrame(inputManager, nowMilliseconds += 10);
	inputManager.resumeInput();
	runFrame(inputManager, nowMilliseconds += 10);

	gamepad.setButton(SDL_GAMEPAD_BUTTON_MISC1, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.gamepadButtonPressRevision() == initialRevision + 2,
		"fresh gamepad press after resume did not advance the raw revision");
	gamepad.setButton(SDL_GAMEPAD_BUTTON_MISC1, false);
	runFrame(inputManager, nowMilliseconds += 10);
	inputManager.shutdown();
}

void testGamepadAdditionRevisionTracksSameFrameReplacement()
{
	VirtualGamepad originalGamepad("JXQY Original Help Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(),
		"same-frame replacement input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);
	require(inputManager.registeredGamepadCount() == 1,
		"same-frame replacement fixture did not start with one gamepad");
	const std::uint64_t initialAdditionRevision =
		inputManager.gamepadAdditionRevision();

	originalGamepad.detach();
	VirtualGamepad replacementGamepad("JXQY Replacement Help Pad");
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.registeredGamepadCount() == 1,
		"same-frame replacement changed the final registered count");
	require(inputManager.gamepadAdditionRevision()
			== initialAdditionRevision + 1,
		"same-frame replacement did not advance the gamepad addition revision");
	inputManager.shutdown();
}

void testStandbyAxisMustReturnToNeutralAfterActiveRemoval()
{
	VirtualGamepad primaryGamepad("JXQY Axis Primary Pad");
	VirtualGamepad standbyGamepad("JXQY Axis Standby Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "axis standby input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);

	pressAndRelease(primaryGamepad, SDL_GAMEPAD_BUTTON_SOUTH,
		inputManager, nowMilliseconds);
	require(inputManager.activeGamepadID() == primaryGamepad.id(),
		"axis standby fixture could not claim its primary gamepad");

	standbyGamepad.setAxis(
		SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == primaryGamepad.id()
		&& !inputManager.isActionDown(InputAction::ScrollRight),
		"non-neutral standby axis stole or leaked before primary removal");

	const SDL_JoystickID standbyGamepadID = standbyGamepad.id();
	primaryGamepad.detach();
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.hasActiveGamepad()
		&& !inputManager.isActionDown(InputAction::ScrollRight),
		"held standby axis was claimed when the primary disconnected");

	standbyGamepad.setAxis(
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.hasActiveGamepad()
		&& !inputManager.wasActionPressed(InputAction::CycleInteractionTarget)
		&& !inputManager.isActionDown(InputAction::CycleInteractionTarget),
		"fresh standby trigger edge bypassed the neutral gate");

	standbyGamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
	standbyGamepad.setAxis(
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.hasActiveGamepad()
		&& !inputManager.wasActionPressed(InputAction::ScrollRight)
		&& !inputManager.wasActionPressed(InputAction::CycleInteractionTarget),
		"standby neutral frame claimed the input channel or generated an action");

	standbyGamepad.setAxis(
		SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == standbyGamepadID
		&& inputManager.wasActionPressed(InputAction::ScrollRight)
		&& inputManager.isActionDown(InputAction::ScrollRight),
		"fresh standby axis did not claim after full neutral");
	require(inputManager.action(InputAction::ScrollRight).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(standbyGamepadID),
		"fresh standby axis claim lost its source device");

	standbyGamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
	runFrame(inputManager, nowMilliseconds += 10);
	inputManager.shutdown();
}

void testStandbyMustReturnToNeutralAfterActiveRemoval()
{
	VirtualGamepad primaryGamepad("JXQY Primary Pad");
	VirtualGamepad standbyGamepad("JXQY Standby Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);

	primaryGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == primaryGamepad.id(),
		"primary gamepad did not claim the input channel");
	primaryGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	runFrame(inputManager, nowMilliseconds += 10);

	standbyGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == primaryGamepad.id(),
		"held standby input stole the primary claim");
	require(!inputManager.wasActionPressed(InputAction::Cancel)
		&& !inputManager.isActionDown(InputAction::Cancel),
		"held standby input leaked an action before primary removal");

	const SDL_JoystickID standbyGamepadID = standbyGamepad.id();
	primaryGamepad.detach();
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.hasActiveGamepad(),
		"non-neutral standby gamepad was claimed when primary disconnected");
	require(!inputManager.wasActionPressed(InputAction::Cancel)
		&& !inputManager.isActionDown(InputAction::Cancel),
		"non-neutral standby gamepad produced an action after primary removal");

	standbyGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.hasActiveGamepad(),
		"fresh standby edge bypassed the neutral gate while another button was held");
	require(!inputManager.wasActionPressed(InputAction::Cancel)
		&& !inputManager.isActionDown(InputAction::Cancel)
		&& !inputManager.wasActionPressed(InputAction::Confirm)
		&& !inputManager.isActionDown(InputAction::Confirm),
		"fresh standby edge leaked through the neutral gate");

	standbyGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, false);
	standbyGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	runFrame(inputManager, nowMilliseconds += 10);
	require(!inputManager.hasActiveGamepad(),
		"standby neutral frame claimed the input channel without a fresh edge");
	require(!inputManager.wasActionPressed(InputAction::Cancel)
		&& !inputManager.wasActionReleased(InputAction::Cancel)
		&& !inputManager.isActionDown(InputAction::Cancel)
		&& !inputManager.wasActionPressed(InputAction::Confirm)
		&& !inputManager.wasActionReleased(InputAction::Confirm)
		&& !inputManager.isActionDown(InputAction::Confirm),
		"standby neutral frame generated an action");

	standbyGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == standbyGamepadID,
		"fresh standby input did not claim after the neutral frame");
	require(inputManager.wasActionPressed(InputAction::Cancel)
		&& inputManager.isActionDown(InputAction::Cancel),
		"fresh standby input did not produce the expected cancel action");
	require(inputManager.action(InputAction::Cancel).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(standbyGamepadID),
		"fresh standby action did not retain the standby source device");

	standbyGamepad.setButton(SDL_GAMEPAD_BUTTON_EAST, false);
	runFrame(inputManager, nowMilliseconds += 10);
	inputManager.shutdown();
}

void testRemapAndReenumeration()
{
	VirtualGamepad gamepad("JXQY Remap Pad");
	PhysicalInputManager inputManager;
	require(inputManager.initialize(), "input manager did not initialize");
	std::uint64_t nowMilliseconds = 0;
	runFrame(inputManager, nowMilliseconds);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.isActionDown(InputAction::Confirm),
		"gamepad was not active before remapping");
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.isActionDown(InputAction::Move),
		"continuous move action was not held before remapping");

	char* currentMapping = SDL_GetGamepadMappingForID(gamepad.id());
	require(currentMapping != nullptr,
		std::string("virtual gamepad mapping unavailable: ") + SDL_GetError());
	std::string remappedMapping(currentMapping);
	SDL_free(currentMapping);
	const std::size_t nameStart = remappedMapping.find(',');
	const std::size_t nameEnd = nameStart == std::string::npos
		? std::string::npos : remappedMapping.find(',', nameStart + 1);
	require(nameStart != std::string::npos && nameEnd != std::string::npos,
		"virtual gamepad mapping has an unexpected format");
	remappedMapping.replace(nameStart + 1, nameEnd - nameStart - 1,
		"JXQY Remapped Pad");
	require(SDL_SetGamepadMapping(gamepad.id(), remappedMapping.c_str()),
		std::string("virtual gamepad remap failed: ") + SDL_GetError());
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.activeGamepadID() == gamepad.id(),
		"remapping changed the active instance ID");
	require(inputManager.wasActionReleased(InputAction::Confirm),
		"remapping did not synthesize action release");
	require(inputManager.action(InputAction::Confirm).sourceDeviceID
		== static_cast<GameInput::InputDeviceID>(gamepad.id()),
		"remap release lost the preserved gamepad instance source");
	require(!inputManager.isActionDown(InputAction::Confirm),
		"held input replayed immediately after remapping");
	require(inputManager.wasActionReleased(InputAction::Move)
		&& !inputManager.isActionDown(InputAction::Move)
		&& inputManager.action(InputAction::Move).sourceDeviceID
			== static_cast<GameInput::InputDeviceID>(gamepad.id())
		&& inputManager.action(InputAction::Move).axis.leftStick.magnitude > 0.8f,
		"remapping did not preserve the continuous move release snapshot");

	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.wasActionPressed(InputAction::Confirm),
		"fresh input after remap neutral gate was lost");

	inputManager.suspendInput();
	inputManager.resumeInput();
	runFrame(inputManager, nowMilliseconds += 10);
	require(inputManager.registeredGamepadCount() == connectedGamepadCount(),
		"foreground resume did not re-enumerate gamepads");
	require(!inputManager.hasActiveGamepad(),
		"foreground resume restored the old active claim");

	inputManager.shutdown();
}
}

int main()
{
	try
	{
		VirtualGamepadTest::SDLSession sdlSession;
		require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
			"SDL video subsystem was initialized before headless gamepad tests");
		testInitializationLifecycle();
		testAxisClaims();
		testMultiDeviceRegistrationAndClaim();
		testRawButtonPressRevisionAndPresentationLabels();
		testGamepadAdditionRevisionTracksSameFrameReplacement();
		testStandbyAxisMustReturnToNeutralAfterActiveRemoval();
		testStandbyMustReturnToNeutralAfterActiveRemoval();
		testRemapAndReenumeration();
		require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
			"SDL video subsystem was initialized by headless gamepad tests");
		std::cout << "SDL gamepad tests passed\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
