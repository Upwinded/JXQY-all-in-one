#include "PhysicalInputManager.h"
#include "ControllerBindingCatalog.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
float normalizeSignedAxis(Sint16 value)
{
	if (value < 0)
	{
		return static_cast<float>(value) / 32768.0f;
	}
	return static_cast<float>(value) / 32767.0f;
}

GameInput::AnalogStickState applyRadialDeadZone(
	Sint16 rawX, Sint16 rawY, float deadZone, float outerDeadZone)
{
	GameInput::AnalogStickState state;
	const float normalizedX = normalizeSignedAxis(rawX);
	const float normalizedY = normalizeSignedAxis(rawY);
	const float rawMagnitude = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY);
	if (rawMagnitude <= deadZone)
	{
		return state;
	}

	const float clampedMagnitude = std::min(rawMagnitude, outerDeadZone);
	const float scaledMagnitude = (clampedMagnitude - deadZone)
		/ (outerDeadZone - deadZone);
	const float directionScale = scaledMagnitude / rawMagnitude;
	state.x = normalizedX * directionScale;
	state.y = normalizedY * directionScale;
	state.magnitude = scaledMagnitude;
	return state;
}

float normalizeTrigger(Sint16 value)
{
	return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
}

constexpr GameInput::InputAction ShoulderPairAction =
	GameInput::findUniqueDefaultControllerAction(
		GameInput::ControllerControl::LeftShoulder,
		GameInput::ControllerBindingTrigger::ButtonChordHeld,
		GameInput::ControllerControl::RightShoulder);
constexpr GameInput::InputAction DirectionalJumpAction =
	GameInput::findUniqueDefaultControllerAction(
		GameInput::ControllerControl::LeftStick,
		GameInput::ControllerBindingTrigger::DirectionalChord,
		GameInput::ControllerControl::LeftShoulder);
constexpr GameInput::InputAction TouchControlsChordAction =
	GameInput::findUniqueDefaultControllerAction(
		GameInput::ControllerControl::Start,
		GameInput::ControllerBindingTrigger::LongPressChord,
		GameInput::ControllerControl::Back);
constexpr GameInput::InputAction MoveAction =
	GameInput::findUniqueDefaultControllerAction(
		GameInput::ControllerControl::LeftStick,
		GameInput::ControllerBindingTrigger::StickContinuous);
constexpr std::array<GameInput::InputAction, 4> NavigationActions =
	GameInput::defaultControllerDirectionalActions(
		GameInput::ControllerControl::DPad,
		GameInput::ControllerBindingTrigger::ButtonHeld);
constexpr std::array<GameInput::InputAction, 4> ScrollActions =
	GameInput::defaultControllerDirectionalActions(
		GameInput::ControllerControl::RightStick,
		GameInput::ControllerBindingTrigger::StickDirectional);

constexpr bool defaultRuntimeActionsAreValid()
{
	if (ShoulderPairAction == GameInput::InputAction::Count
		|| DirectionalJumpAction == GameInput::InputAction::Count
		|| TouchControlsChordAction == GameInput::InputAction::Count
		|| MoveAction == GameInput::InputAction::Count)
	{
		return false;
	}
	for (GameInput::InputAction action : NavigationActions)
	{
		if (action == GameInput::InputAction::Count)
		{
			return false;
		}
	}
	for (GameInput::InputAction action : ScrollActions)
	{
		if (action == GameInput::InputAction::Count)
		{
			return false;
		}
	}
	return true;
}

static_assert(defaultRuntimeActionsAreValid(),
	"controller runtime gestures must resolve to input actions");
}

namespace GameInput
{
PhysicalInputManager::PhysicalInputManager(bool touchControlsAreAvailable)
	: touchControlsAvailable(touchControlsAreAvailable)
{
}

PhysicalInputManager::~PhysicalInputManager()
{
	shutdown();
}

bool PhysicalInputManager::initialize()
{
	if (initialized)
	{
		return true;
	}
	if (!SDL_WasInit(SDL_INIT_GAMEPAD))
	{
		return false;
	}

	initialized = true;
	enumerateGamepads();
	return true;
}

void PhysicalInputManager::shutdown()
{
	if (!initialized && registeredGamepads.empty())
	{
		return;
	}

	closeAllGamepads(true);
	inputSuspended = false;
	windowFocused = true;
	initialized = false;
}

void PhysicalInputManager::beginFrame()
{
	clearActionEdges();
}

bool PhysicalInputManager::processEvent(const SDL_Event& event)
{
	if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
	{
		return processKeyboardEvent(event);
	}
	if (!initialized)
	{
		return false;
	}

	switch (event.type)
	{
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_GAMEPAD_REMAPPED:
		processGamepadDeviceEvent(event);
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		processGamepadButtonEvent(event);
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		processGamepadAxisEvent(event);
		break;
	default:
		break;
	}
	return false;
}

void PhysicalInputManager::update(std::uint64_t nowMilliseconds)
{
	if (inputSuspended || !windowFocused)
	{
		return;
	}
	updateNeutralGates();
	const RegisteredGamepad* activeDevice = findRegisteredGamepad(activeInstanceID);
	if (activeDevice != nullptr && activeDevice->awaitingNeutral)
	{
		return;
	}
	updateRightStickScrollActions();
	updateNavigationRepeat(nowMilliseconds);
	resolvePendingDirectionalJump(nowMilliseconds);
	if (!touchControlsAvailable ||
		!touchControlsChordActive ||
		touchControlsChordFired)
	{
		return;
	}
	if (!isButtonDown(SDL_GAMEPAD_BUTTON_BACK) || !isButtonDown(SDL_GAMEPAD_BUTTON_START))
	{
		endTouchControlsChord();
		return;
	}
	if (nowMilliseconds - touchControlsChordStartedAt >= ToggleTouchControlsHoldMilliseconds)
	{
		pulseAction(TouchControlsChordAction);
		touchControlsChordFired = true;
	}
}

void PhysicalInputManager::suspendInput()
{
	if (inputSuspended)
	{
		return;
	}
	releaseAllInputs();
	inputSuspended = true;
	lifecycleRevision++;
}

void PhysicalInputManager::resumeInput()
{
	// Refresh the SDL device list instead of trusting handles and hotplug events
	// retained while the application was suspended. Re-enumerated devices are
	// registered but remain unclaimed until they return to neutral and produce a
	// fresh input.
	const SDL_JoystickID previouslyActiveInstanceID = activeInstanceID != 0
		? activeInstanceID : previouslyActiveInstanceIDAwaitingReclaim;
	releaseAllInputs();
	closeAllGamepads(false);
	inputSuspended = false;
	enumerateGamepads();
	if (previouslyActiveInstanceID != 0)
	{
		if (findRegisteredGamepad(previouslyActiveInstanceID) == nullptr)
		{
			// SDL may coalesce or delay a hotplug removal while the app is suspended.
			// Comparing the refreshed enumeration with the last claimed device records
			// the real loss exactly once; a later stale remove event finds no entry.
			activeGamepadRemovalRevisionValue++;
		}
		else
		{
			// Re-enumeration deliberately leaves the device unclaimed. Remember that it
			// was the active device until fresh input reclaims a gamepad or it is
			// removed, so a detach in that interval can restore hidden touch controls.
			previouslyActiveInstanceIDAwaitingReclaim =
				previouslyActiveInstanceID;
		}
	}
	lifecycleRevision++;
}

void PhysicalInputManager::setWindowFocused(bool focused)
{
	if (windowFocused == focused)
	{
		return;
	}
	// Window focus is independent from application suspension. Releasing on
	// both transitions makes inputs held across Alt-Tab pass AwaitNeutral before
	// they can produce a fresh action.
	releaseAllInputs();
	windowFocused = focused;
	lifecycleRevision++;
}

void PhysicalInputManager::releaseAllInputs()
{
	const GamepadAxisState releasedAxis = axisState;
	const InputDeviceID releasedSourceDeviceID = resolveSourceDeviceID(
		UnknownInputDeviceID);
	for (std::size_t index = 0; index < InputActionCount; index++)
	{
		InputActionState& state = actionStates[index];
		state.pressed = false;
		if (state.down)
		{
			state.down = false;
			state.released = true;
			state.axis = releasedAxis;
			state.sourceDeviceID = releasedSourceDeviceID;
		}
		else if (!state.released)
		{
			state.axis = {};
			state.sourceDeviceID = UnknownInputDeviceID;
		}
	}
	buttonStates.fill(false);
	rawAxisValues.fill(0);
	navigationNextRepeatAt.fill(0);
	rightStickScrollNextRepeatAt.fill(0);
	rightStickScrollDirection = -1;
	for (ButtonActionBindings& bindings : buttonActionBindings)
	{
		bindings.reset();
	}
	rightShoulderChordButtons.reset();
	axisState = {};
	leftShoulderConsumed = false;
	rightShoulderConsumed = false;
	shoulderPairChordActive = false;
	cancelPendingDirectionalJump();
	startConsumed = false;
	endTouchControlsChord();
	leftTriggerPressed = false;
	rightTriggerPressed = false;
	for (RegisteredGamepad& gamepad : registeredGamepads)
	{
		gamepad.awaitingNeutral = true;
		gamepad.claimReady = false;
	}
}

void PhysicalInputManager::releaseForContextTransition()
{
	const InputActionState toggleTouchControlsState =
		action(InputAction::ToggleTouchControls);
	releaseAllInputs();
	if (toggleTouchControlsState.pressed)
	{
		actionStates[actionIndex(InputAction::ToggleTouchControls)] =
			toggleTouchControlsState;
	}
}

const InputActionState& PhysicalInputManager::action(InputAction inputAction) const
{
	static const InputActionState emptyState;
	const std::size_t index = actionIndex(inputAction);
	if (index >= InputActionCount)
	{
		return emptyState;
	}
	return actionStates[index];
}

bool PhysicalInputManager::isActionDown(InputAction inputAction) const
{
	return action(inputAction).down;
}

bool PhysicalInputManager::wasActionPressed(InputAction inputAction) const
{
	return action(inputAction).pressed;
}

bool PhysicalInputManager::wasActionReleased(InputAction inputAction) const
{
	return action(inputAction).released;
}

bool PhysicalInputManager::consumePressed(InputAction inputAction)
{
	const std::size_t index = actionIndex(inputAction);
	if (index >= InputActionCount || !actionStates[index].pressed)
	{
		return false;
	}
	actionStates[index].pressed = false;
	return true;
}

const GamepadAxisState& PhysicalInputManager::axes() const
{
	return axisState;
}

bool PhysicalInputManager::hasActiveGamepad() const
{
	return activeGamepad != nullptr;
}

bool PhysicalInputManager::isInputContextActive() const
{
	return !inputSuspended && windowFocused;
}

std::uint64_t PhysicalInputManager::inputLifecycleRevision() const
{
	return lifecycleRevision;
}

std::uint64_t PhysicalInputManager::activeGamepadRemovalRevision() const
{
	return activeGamepadRemovalRevisionValue;
}

std::uint64_t PhysicalInputManager::gamepadAdditionRevision() const
{
	return gamepadAdditionRevisionValue;
}

std::uint64_t PhysicalInputManager::gamepadButtonPressRevision() const
{
	return gamepadButtonPressRevisionValue;
}

SDL_JoystickID PhysicalInputManager::activeGamepadID() const
{
	return activeInstanceID;
}

const std::string& PhysicalInputManager::activeGamepadName() const
{
	return gamepadName;
}

SDL_GamepadButtonLabel PhysicalInputManager::buttonLabel(SDL_GamepadButton button) const
{
	const RegisteredGamepad* gamepad = findRegisteredGamepad(activeInstanceID);
	if (gamepad == nullptr || !isValidButton(button))
	{
		return SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
	}
	return gamepad->buttonLabels[static_cast<std::size_t>(button)];
}

SDL_GamepadButtonLabel PhysicalInputManager::presentationButtonLabel(
	SDL_GamepadButton button) const
{
	if (!isValidButton(button))
	{
		return SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
	}
	const RegisteredGamepad* gamepad = findRegisteredGamepad(activeInstanceID);
	if (gamepad == nullptr && !registeredGamepads.empty())
	{
		gamepad = &registeredGamepads.back();
	}
	return gamepad == nullptr
		? SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN
		: gamepad->buttonLabels[static_cast<std::size_t>(button)];
}

bool PhysicalInputManager::isButtonDown(SDL_GamepadButton button) const
{
	if (!isValidButton(button))
	{
		return false;
	}
	return buttonStates[static_cast<std::size_t>(button)];
}

std::size_t PhysicalInputManager::registeredGamepadCount() const
{
	return registeredGamepads.size();
}

void PhysicalInputManager::enumerateGamepads()
{
	if (!initialized)
	{
		return;
	}
	int gamepadCount = 0;
	SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&gamepadCount);
	if (gamepadIDs == nullptr)
	{
		return;
	}

	for (int index = 0; index < gamepadCount; index++)
	{
		registerGamepad(gamepadIDs[index]);
	}
	SDL_free(gamepadIDs);
}

bool PhysicalInputManager::registerGamepad(SDL_JoystickID instanceID)
{
	if (!initialized || instanceID == 0 || findRegisteredGamepad(instanceID) != nullptr)
	{
		return false;
	}

	SDL_Gamepad* gamepad = SDL_OpenGamepad(instanceID);
	if (gamepad == nullptr)
	{
		return false;
	}

	RegisteredGamepad registeredGamepad;
	registeredGamepad.gamepad = gamepad;
	registeredGamepad.instanceID = instanceID;
	refreshRegisteredGamepadMetadata(registeredGamepad);
	refreshRegisteredGamepadSnapshot(registeredGamepad);
	registeredGamepads.push_back(std::move(registeredGamepad));
	gamepadAdditionRevisionValue++;
	return true;
}

void PhysicalInputManager::unregisterGamepad(SDL_JoystickID instanceID)
{
	auto iterator = std::find_if(registeredGamepads.begin(), registeredGamepads.end(),
		[instanceID](const RegisteredGamepad& gamepad)
		{
			return gamepad.instanceID == instanceID;
		});
	if (iterator == registeredGamepads.end())
	{
		return;
	}

	const bool wasActive = instanceID == activeInstanceID;
	const bool wasPreviouslyActiveAwaitingReclaim =
		instanceID == previouslyActiveInstanceIDAwaitingReclaim;
	if (wasActive)
	{
		releaseAllInputs();
		activeGamepad = nullptr;
		activeInstanceID = 0;
		gamepadName.clear();
		gamepadType = SDL_GAMEPAD_TYPE_UNKNOWN;
	}
	if (wasActive || wasPreviouslyActiveAwaitingReclaim)
	{
		previouslyActiveInstanceIDAwaitingReclaim = 0;
		lifecycleRevision++;
		activeGamepadRemovalRevisionValue++;
	}
	SDL_CloseGamepad(iterator->gamepad);
	registeredGamepads.erase(iterator);
}

void PhysicalInputManager::closeAllGamepads(bool synthesizeRelease)
{
	if (synthesizeRelease && activeGamepad != nullptr)
	{
		releaseAllInputs();
		lifecycleRevision++;
	}
	for (RegisteredGamepad& gamepad : registeredGamepads)
	{
		if (gamepad.gamepad != nullptr)
		{
			SDL_CloseGamepad(gamepad.gamepad);
		}
	}
	registeredGamepads.clear();
	activeGamepad = nullptr;
	activeInstanceID = 0;
	previouslyActiveInstanceIDAwaitingReclaim = 0;
	gamepadName.clear();
	gamepadType = SDL_GAMEPAD_TYPE_UNKNOWN;
}

void PhysicalInputManager::activateGamepad(RegisteredGamepad& gamepad)
{
	if (activeGamepad != nullptr || gamepad.gamepad == nullptr)
	{
		return;
	}
	activeGamepad = gamepad.gamepad;
	activeInstanceID = gamepad.instanceID;
	previouslyActiveInstanceIDAwaitingReclaim = 0;
	gamepadName = gamepad.name;
	gamepadType = gamepad.type;
	gamepad.awaitingNeutral = false;
	gamepad.claimReady = false;
	buttonStates.fill(false);
	rawAxisValues = gamepad.rawAxisValues;
	axisState = {};
	updateAxisState();
}

PhysicalInputManager::RegisteredGamepad* PhysicalInputManager::findRegisteredGamepad(
	SDL_JoystickID instanceID)
{
	if (instanceID == 0)
	{
		return nullptr;
	}
	auto iterator = std::find_if(registeredGamepads.begin(), registeredGamepads.end(),
		[instanceID](const RegisteredGamepad& gamepad)
		{
			return gamepad.instanceID == instanceID;
		});
	return iterator == registeredGamepads.end() ? nullptr : &(*iterator);
}

const PhysicalInputManager::RegisteredGamepad* PhysicalInputManager::findRegisteredGamepad(
	SDL_JoystickID instanceID) const
{
	if (instanceID == 0)
	{
		return nullptr;
	}
	auto iterator = std::find_if(registeredGamepads.begin(), registeredGamepads.end(),
		[instanceID](const RegisteredGamepad& gamepad)
		{
			return gamepad.instanceID == instanceID;
		});
	return iterator == registeredGamepads.end() ? nullptr : &(*iterator);
}

void PhysicalInputManager::refreshRegisteredGamepadMetadata(RegisteredGamepad& gamepad)
{
	if (gamepad.gamepad == nullptr)
	{
		return;
	}
	const char* name = SDL_GetGamepadName(gamepad.gamepad);
	gamepad.name = name == nullptr ? std::string() : std::string(name);
	gamepad.type = SDL_GetGamepadType(gamepad.gamepad);
	for (int buttonIndex = SDL_GAMEPAD_BUTTON_SOUTH;
		buttonIndex < SDL_GAMEPAD_BUTTON_COUNT; buttonIndex++)
	{
		gamepad.buttonLabels[static_cast<std::size_t>(buttonIndex)] =
			SDL_GetGamepadButtonLabel(gamepad.gamepad,
				static_cast<SDL_GamepadButton>(buttonIndex));
	}
}

void PhysicalInputManager::refreshRegisteredGamepadSnapshot(RegisteredGamepad& gamepad)
{
	if (gamepad.gamepad == nullptr)
	{
		return;
	}
	for (int buttonIndex = SDL_GAMEPAD_BUTTON_SOUTH;
		buttonIndex < SDL_GAMEPAD_BUTTON_COUNT; buttonIndex++)
	{
		gamepad.buttonStates[static_cast<std::size_t>(buttonIndex)] =
			SDL_GetGamepadButton(gamepad.gamepad,
				static_cast<SDL_GamepadButton>(buttonIndex));
	}
	for (int axisIndex = SDL_GAMEPAD_AXIS_LEFTX;
		axisIndex < SDL_GAMEPAD_AXIS_COUNT; axisIndex++)
	{
		gamepad.rawAxisValues[static_cast<std::size_t>(axisIndex)] =
			SDL_GetGamepadAxis(gamepad.gamepad,
				static_cast<SDL_GamepadAxis>(axisIndex));
	}
}

void PhysicalInputManager::updateNeutralGates()
{
	for (RegisteredGamepad& gamepad : registeredGamepads)
	{
		if (!gamepad.awaitingNeutral)
		{
			continue;
		}
		refreshRegisteredGamepadSnapshot(gamepad);
		if (!isRegisteredGamepadNeutral(gamepad))
		{
			continue;
		}
		gamepad.awaitingNeutral = false;
		gamepad.claimReady = gamepad.instanceID != activeInstanceID;
	}
}

bool PhysicalInputManager::isRegisteredGamepadNeutral(
	const RegisteredGamepad& gamepad) const
{
	for (bool buttonDown : gamepad.buttonStates)
	{
		if (buttonDown)
		{
			return false;
		}
	}

	const AnalogStickState leftStick = applyRadialDeadZone(
		gamepad.rawAxisValues[SDL_GAMEPAD_AXIS_LEFTX],
		gamepad.rawAxisValues[SDL_GAMEPAD_AXIS_LEFTY],
		StickDeadZone, StickOuterDeadZone);
	const AnalogStickState rightStick = applyRadialDeadZone(
		gamepad.rawAxisValues[SDL_GAMEPAD_AXIS_RIGHTX],
		gamepad.rawAxisValues[SDL_GAMEPAD_AXIS_RIGHTY],
		StickDeadZone, StickOuterDeadZone);
	if (leftStick.magnitude > 0.0f || rightStick.magnitude > 0.0f)
	{
		return false;
	}

	const int triggerNeutralLimit = static_cast<int>(TriggerReleaseThreshold * 32767.0f);
	return gamepad.rawAxisValues[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] <= triggerNeutralLimit
		&& gamepad.rawAxisValues[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] <= triggerNeutralLimit;
}

bool PhysicalInputManager::axisCrossedClaimThreshold(
	const RegisteredGamepad& gamepad, SDL_GamepadAxis axis, Sint16 value) const
{
	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
	{
		const float oldValue = normalizeTrigger(
			gamepad.rawAxisValues[static_cast<std::size_t>(axis)]);
		const float newValue = normalizeTrigger(value);
		return oldValue < GamepadClaimTriggerThreshold
			&& newValue >= GamepadClaimTriggerThreshold;
	}

	std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> newAxisValues = gamepad.rawAxisValues;
	newAxisValues[static_cast<std::size_t>(axis)] = value;
	const bool leftStickAxis = axis == SDL_GAMEPAD_AXIS_LEFTX
		|| axis == SDL_GAMEPAD_AXIS_LEFTY;
	const SDL_GamepadAxis xAxis = leftStickAxis
		? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
	const SDL_GamepadAxis yAxis = leftStickAxis
		? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY;
	const float oldMagnitude = std::sqrt(
		normalizeSignedAxis(gamepad.rawAxisValues[xAxis])
			* normalizeSignedAxis(gamepad.rawAxisValues[xAxis])
		+ normalizeSignedAxis(gamepad.rawAxisValues[yAxis])
			* normalizeSignedAxis(gamepad.rawAxisValues[yAxis]));
	const float newMagnitude = std::sqrt(
		normalizeSignedAxis(newAxisValues[xAxis])
			* normalizeSignedAxis(newAxisValues[xAxis])
		+ normalizeSignedAxis(newAxisValues[yAxis])
			* normalizeSignedAxis(newAxisValues[yAxis]));
	return oldMagnitude < GamepadClaimStickThreshold
		&& newMagnitude >= GamepadClaimStickThreshold;
}

void PhysicalInputManager::processGamepadDeviceEvent(const SDL_Event& event)
{
	if (event.type == SDL_EVENT_GAMEPAD_ADDED)
	{
		registerGamepad(event.gdevice.which);
		return;
	}
	if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
	{
		unregisterGamepad(event.gdevice.which);
		return;
	}
	if (event.type == SDL_EVENT_GAMEPAD_REMAPPED)
	{
		RegisteredGamepad* gamepad = findRegisteredGamepad(event.gdevice.which);
		if (gamepad == nullptr)
		{
			registerGamepad(event.gdevice.which);
			return;
		}
		if (gamepad->instanceID == activeInstanceID)
		{
			releaseAllInputs();
			lifecycleRevision++;
		}
		refreshRegisteredGamepadMetadata(*gamepad);
		gamepad->buttonStates.fill(false);
		gamepad->rawAxisValues.fill(0);
		gamepad->awaitingNeutral = true;
		gamepad->claimReady = false;
		if (gamepad->instanceID == activeInstanceID)
		{
			gamepadName = gamepad->name;
			gamepadType = gamepad->type;
		}
	}
}

void PhysicalInputManager::processGamepadButtonEvent(const SDL_Event& event)
{
	RegisteredGamepad* gamepad = findRegisteredGamepad(event.gbutton.which);
	if (gamepad == nullptr)
	{
		return;
	}

	const SDL_GamepadButton button = static_cast<SDL_GamepadButton>(event.gbutton.button);
	if (!isValidButton(button))
	{
		return;
	}
	const std::size_t buttonIndex = static_cast<std::size_t>(button);
	const bool wasDown = gamepad->buttonStates[buttonIndex];
	const bool isDown = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
	gamepad->buttonStates[buttonIndex] = isDown;
	if (!inputSuspended && windowFocused && isDown && !wasDown)
	{
		gamepadButtonPressRevisionValue++;
	}

	if (inputSuspended || !windowFocused || gamepad->awaitingNeutral)
	{
		return;
	}
	if (activeGamepad == nullptr && gamepad->claimReady && isDown && !wasDown)
	{
		activateGamepad(*gamepad);
	}
	if (gamepad->instanceID != activeInstanceID)
	{
		return;
	}

	if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
	{
		if (buttonStates[buttonIndex])
		{
			return;
		}
		buttonStates[buttonIndex] = true;
		handleButtonPressed(button, SDL_GetTicks());
	}
	else
	{
		if (!buttonStates[buttonIndex])
		{
			return;
		}
		buttonStates[buttonIndex] = false;
		handleButtonReleased(button);
	}
}

void PhysicalInputManager::processGamepadAxisEvent(const SDL_Event& event)
{
	RegisteredGamepad* gamepad = findRegisteredGamepad(event.gaxis.which);
	if (gamepad == nullptr)
	{
		return;
	}

	const SDL_GamepadAxis axis = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
	if (!isValidAxis(axis))
	{
		return;
	}
	const bool crossedClaimThreshold = axisCrossedClaimThreshold(
		*gamepad, axis, event.gaxis.value);
	gamepad->rawAxisValues[static_cast<std::size_t>(axis)] = event.gaxis.value;

	if (inputSuspended || !windowFocused || gamepad->awaitingNeutral)
	{
		return;
	}
	if (activeGamepad == nullptr && gamepad->claimReady && crossedClaimThreshold)
	{
		activateGamepad(*gamepad);
	}
	if (gamepad->instanceID != activeInstanceID)
	{
		return;
	}
	rawAxisValues[static_cast<std::size_t>(axis)] = event.gaxis.value;
	updateAxisState();
}

bool PhysicalInputManager::processKeyboardEvent(const SDL_Event& event)
{
	if (!touchControlsAvailable ||
		event.type != SDL_EVENT_KEY_DOWN ||
		event.key.scancode != SDL_SCANCODE_H)
	{
		return false;
	}
	const bool controlDown = (event.key.mod & SDL_KMOD_CTRL) != 0;
	const bool shiftDown = (event.key.mod & SDL_KMOD_SHIFT) != 0;
	if (!controlDown || !shiftDown)
	{
		return false;
	}
	if (!inputSuspended && windowFocused && !event.key.repeat)
	{
		pulseAction(InputAction::ToggleTouchControls, KeyboardInputDeviceID);
	}
	return true;
}

void PhysicalInputManager::handleButtonPressed(SDL_GamepadButton button, std::uint64_t nowMilliseconds)
{
	if (touchControlsAvailable &&
		button == SDL_GAMEPAD_BUTTON_BACK)
	{
		if (isButtonDown(SDL_GAMEPAD_BUTTON_START))
		{
			beginTouchControlsChord(nowMilliseconds);
		}
		return;
	}
	if (button == SDL_GAMEPAD_BUTTON_START)
	{
		startConsumed = false;
		if (touchControlsAvailable &&
			isButtonDown(SDL_GAMEPAD_BUTTON_BACK))
		{
			beginTouchControlsChord(nowMilliseconds);
		}
		return;
	}

	if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
	{
		leftShoulderConsumed = false;
		if (isButtonDown(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) && !rightShoulderConsumed)
		{
			cancelPendingDirectionalJump();
			leftShoulderConsumed = true;
			rightShoulderConsumed = true;
			shoulderPairChordActive = true;
			setActionDown(ShoulderPairAction, true);
		}
		else
		{
			beginDirectionalJumpIfEligible();
		}
		return;
	}
	if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
	{
		rightShoulderConsumed = false;
		if (isButtonDown(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && !leftShoulderConsumed)
		{
			cancelPendingDirectionalJump();
			leftShoulderConsumed = true;
			rightShoulderConsumed = true;
			shoulderPairChordActive = true;
			setActionDown(ShoulderPairAction, true);
		}
		return;
	}

	if (isButtonDown(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
	{
		if (shoulderPairChordActive || bindRightShoulderChord(button))
		{
			return;
		}
	}

	bindDefaultButtonActions(button);
}

void PhysicalInputManager::handleButtonReleased(SDL_GamepadButton button)
{
	if (touchControlsAvailable &&
		button == SDL_GAMEPAD_BUTTON_BACK)
	{
		endTouchControlsChord();
		return;
	}
	if (button == SDL_GAMEPAD_BUTTON_START)
	{
		if (!startConsumed)
		{
			pulseDefaultButtonReleaseActions(button);
		}
		startConsumed = false;
		if (touchControlsAvailable)
		{
			endTouchControlsChord();
		}
		return;
	}

	if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
	{
		if (directionalJumpPending && !leftShoulderConsumed
			&& axisState.leftStick.magnitude >= DirectionalJumpThreshold
			&& !isButtonDown(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
		{
			leftShoulderConsumed = true;
			pulseAction(DirectionalJumpAction);
		}
		cancelPendingDirectionalJump();
		if (shoulderPairChordActive)
		{
			setActionDown(ShoulderPairAction, false);
			shoulderPairChordActive = false;
		}
		if (!leftShoulderConsumed)
		{
			pulseDefaultButtonReleaseActions(button);
		}
		leftShoulderConsumed = false;
		return;
	}
	if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
	{
		if (shoulderPairChordActive)
		{
			setActionDown(ShoulderPairAction, false);
			shoulderPairChordActive = false;
		}
		releaseRightShoulderChordBindings();
		if (!rightShoulderConsumed)
		{
			pulseDefaultButtonReleaseActions(button);
		}
		rightShoulderConsumed = false;
		return;
	}

	releaseButtonActions(button);
	rightShoulderChordButtons.reset(static_cast<std::size_t>(button));
}

void PhysicalInputManager::bindDefaultButtonActions(SDL_GamepadButton button)
{
	const ControllerControlLocation location = controllerControlForButton(button);
	if (!location.valid())
	{
		return;
	}
	for (const ControllerActionBinding& binding : DefaultControllerBindings)
	{
		if (binding.trigger == ControllerBindingTrigger::ButtonHeld
			&& binding.modifier == ControllerControl::None
			&& binding.primary == location.control
			&& binding.direction == location.direction)
		{
			bindButtonAction(button, binding.action);
		}
	}
}

void PhysicalInputManager::pulseDefaultButtonReleaseActions(
	SDL_GamepadButton button)
{
	const ControllerControlLocation location = controllerControlForButton(button);
	if (!location.valid())
	{
		return;
	}
	for (const ControllerActionBinding& binding : DefaultControllerBindings)
	{
		if (binding.trigger == ControllerBindingTrigger::ButtonRelease
			&& binding.modifier == ControllerControl::None
			&& binding.primary == location.control
			&& binding.direction == location.direction)
		{
			pulseAction(binding.action);
		}
	}
}

bool PhysicalInputManager::bindRightShoulderChord(SDL_GamepadButton button)
{
	const ControllerControlLocation location = controllerControlForButton(button);
	if (!location.valid())
	{
		return false;
	}
	bool matched = false;
	for (const ControllerActionBinding& binding : DefaultControllerBindings)
	{
		if (binding.trigger == ControllerBindingTrigger::ButtonChordHeld
			&& binding.modifier == ControllerControl::RightShoulder
			&& binding.primary == location.control
			&& binding.direction == location.direction)
		{
			bindButtonAction(button, binding.action);
			matched = true;
		}
	}
	if (!matched)
	{
		return false;
	}

	rightShoulderConsumed = true;
	rightShoulderChordButtons.set(static_cast<std::size_t>(button));
	return true;
}

void PhysicalInputManager::releaseRightShoulderChordBindings()
{
	for (std::size_t index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; index++)
	{
		if (rightShoulderChordButtons.test(index))
		{
			releaseButtonActions(static_cast<SDL_GamepadButton>(index));
		}
	}
	rightShoulderChordButtons.reset();
}

void PhysicalInputManager::beginTouchControlsChord(std::uint64_t nowMilliseconds)
{
	if (touchControlsChordActive)
	{
		return;
	}
	touchControlsChordActive = true;
	touchControlsChordFired = false;
	touchControlsChordStartedAt = nowMilliseconds;
	startConsumed = true;
}

void PhysicalInputManager::endTouchControlsChord()
{
	touchControlsChordActive = false;
	touchControlsChordFired = false;
	touchControlsChordStartedAt = 0;
}

void PhysicalInputManager::beginDirectionalJumpIfEligible()
{
	if (!isButtonDown(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || leftShoulderConsumed ||
		axisState.leftStick.magnitude < DirectionalJumpThreshold)
	{
		return;
	}
	if (!directionalJumpPending)
	{
		directionalJumpPending = true;
		directionalJumpTimerStarted = false;
		directionalJumpStartedAt = 0;
	}
}

void PhysicalInputManager::resolvePendingDirectionalJump(
	std::uint64_t nowMilliseconds)
{
	if (!directionalJumpPending)
	{
		return;
	}
	if (!isButtonDown(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
		|| leftShoulderConsumed
		|| axisState.leftStick.magnitude < DirectionalJumpThreshold)
	{
		cancelPendingDirectionalJump();
		return;
	}
	if (!directionalJumpTimerStarted)
	{
		directionalJumpTimerStarted = true;
		directionalJumpStartedAt = nowMilliseconds;
		return;
	}
	if (nowMilliseconds - directionalJumpStartedAt
		< ShoulderChordGraceMilliseconds)
	{
		return;
	}
	cancelPendingDirectionalJump();
	leftShoulderConsumed = true;
	pulseAction(DirectionalJumpAction);
}

void PhysicalInputManager::cancelPendingDirectionalJump()
{
	directionalJumpPending = false;
	directionalJumpTimerStarted = false;
	directionalJumpStartedAt = 0;
}

void PhysicalInputManager::bindButtonAction(SDL_GamepadButton button, InputAction inputAction)
{
	if (!isValidButton(button) || actionIndex(inputAction) >= InputActionCount)
	{
		return;
	}
	ButtonActionBindings& bindings = buttonActionBindings[static_cast<std::size_t>(button)];
	bindings.set(actionIndex(inputAction));
	setActionDown(inputAction, true);
}

void PhysicalInputManager::releaseButtonActions(SDL_GamepadButton button)
{
	if (!isValidButton(button))
	{
		return;
	}
	ButtonActionBindings& bindings = buttonActionBindings[static_cast<std::size_t>(button)];
	for (std::size_t index = 0; index < InputActionCount; index++)
	{
		if (bindings.test(index))
		{
			setActionDown(static_cast<InputAction>(index), false);
		}
	}
	bindings.reset();
}

void PhysicalInputManager::setActionDown(
	InputAction inputAction,
	bool down,
	InputDeviceID sourceDeviceID)
{
	const std::size_t index = actionIndex(inputAction);
	if (index >= InputActionCount)
	{
		return;
	}
	InputActionState& state = actionStates[index];
	if (state.down == down)
	{
		if (down)
		{
			captureActionSnapshot(state, sourceDeviceID);
		}
		return;
	}
	state.down = down;
	captureActionSnapshot(state, sourceDeviceID);
	for (std::size_t index = 0; index < NavigationActions.size(); index++)
	{
		if (NavigationActions[index] == inputAction)
		{
			navigationNextRepeatAt[index] = 0;
			break;
		}
	}
	for (std::size_t index = 0; index < ScrollActions.size(); index++)
	{
		if (ScrollActions[index] == inputAction)
		{
			rightStickScrollNextRepeatAt[index] = 0;
			break;
		}
	}
	if (down)
	{
		state.pressed = true;
	}
	else
	{
		state.released = true;
	}
}

void PhysicalInputManager::pulseAction(
	InputAction inputAction,
	InputDeviceID sourceDeviceID)
{
	const std::size_t index = actionIndex(inputAction);
	if (index < InputActionCount)
	{
		InputActionState& state = actionStates[index];
		state.pressed = true;
		captureActionSnapshot(state, sourceDeviceID);
	}
}

void PhysicalInputManager::clearActionEdges()
{
	for (InputActionState& state : actionStates)
	{
		state.pressed = false;
		state.released = false;
		if (!state.down)
		{
			state.axis = {};
			state.sourceDeviceID = UnknownInputDeviceID;
		}
	}
}

void PhysicalInputManager::updateNavigationRepeat(std::uint64_t nowMilliseconds)
{
	auto updateRepeats = [this, nowMilliseconds](
		const std::array<InputAction, 4>& actions,
		std::array<std::uint64_t, 4>& nextRepeatAt)
	{
		for (std::size_t index = 0; index < nextRepeatAt.size(); index++)
		{
			if (!isActionDown(actions[index]))
			{
				nextRepeatAt[index] = 0;
				continue;
			}
			if (nextRepeatAt[index] == 0)
			{
				nextRepeatAt[index] = nowMilliseconds
					+ NavigationRepeatDelayMilliseconds;
				continue;
			}
			if (nowMilliseconds >= nextRepeatAt[index])
			{
				pulseAction(actions[index]);
				nextRepeatAt[index] = nowMilliseconds
					+ NavigationRepeatIntervalMilliseconds;
			}
		}
	};
	updateRepeats(NavigationActions, navigationNextRepeatAt);
	updateRepeats(ScrollActions, rightStickScrollNextRepeatAt);
}

void PhysicalInputManager::updateAxisState()
{
	axisState.leftStick = applyRadialDeadZone(
		rawAxisValues[SDL_GAMEPAD_AXIS_LEFTX],
		rawAxisValues[SDL_GAMEPAD_AXIS_LEFTY],
		StickDeadZone,
		StickOuterDeadZone);
	axisState.rightStick = applyRadialDeadZone(
		rawAxisValues[SDL_GAMEPAD_AXIS_RIGHTX],
		rawAxisValues[SDL_GAMEPAD_AXIS_RIGHTY],
		StickDeadZone,
		StickOuterDeadZone);
	axisState.leftTrigger = normalizeTrigger(rawAxisValues[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]);
	axisState.rightTrigger = normalizeTrigger(rawAxisValues[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]);

	if (!axisState.running && axisState.leftStick.magnitude >= RunPressThreshold)
	{
		axisState.running = true;
	}
	else if (axisState.running && axisState.leftStick.magnitude <= RunReleaseThreshold)
	{
		axisState.running = false;
	}
	if (axisState.leftStick.magnitude < DirectionalJumpThreshold)
	{
		cancelPendingDirectionalJump();
	}
	else
	{
		beginDirectionalJumpIfEligible();
	}
	setActionDown(MoveAction, axisState.leftStick.magnitude > 0.0f);
	updateTriggerActions();
}

void PhysicalInputManager::updateTriggerActions()
{
	auto updateTrigger = [this](ControllerControl control,
		float value, bool& pressed)
	{
		bool nextPressed = pressed;
		if (!pressed && value >= TriggerPressThreshold)
		{
			nextPressed = true;
		}
		else if (pressed && value <= TriggerReleaseThreshold)
		{
			nextPressed = false;
		}
		if (nextPressed == pressed)
		{
			return;
		}
		pressed = nextPressed;
		for (const ControllerActionBinding& binding : DefaultControllerBindings)
		{
			if (binding.trigger == ControllerBindingTrigger::TriggerThreshold
				&& binding.modifier == ControllerControl::None
				&& binding.primary == control)
			{
				setActionDown(binding.action, pressed);
			}
		}
	};
	updateTrigger(ControllerControl::LeftTrigger,
		axisState.leftTrigger, leftTriggerPressed);
	updateTrigger(ControllerControl::RightTrigger,
		axisState.rightTrigger, rightTriggerPressed);
}

void PhysicalInputManager::updateRightStickScrollActions()
{
	auto directionValue = [this](int direction)
	{
		switch (direction)
		{
		case 0:
			return -axisState.rightStick.y;
		case 1:
			return axisState.rightStick.y;
		case 2:
			return -axisState.rightStick.x;
		case 3:
			return axisState.rightStick.x;
		default:
			return 0.0f;
		}
	};

	if (rightStickScrollDirection >= 0
		&& directionValue(rightStickScrollDirection) <= UIStickReleaseThreshold)
	{
		setActionDown(ScrollActions[rightStickScrollDirection], false);
		rightStickScrollDirection = -1;
	}
	if (rightStickScrollDirection < 0)
	{
		const float absoluteX = std::abs(axisState.rightStick.x);
		const float absoluteY = std::abs(axisState.rightStick.y);
		if (std::max(absoluteX, absoluteY) >= UIStickPressThreshold)
		{
			if (absoluteX >= absoluteY)
			{
				rightStickScrollDirection =
					axisState.rightStick.x < 0.0f ? 2 : 3;
			}
			else
			{
				rightStickScrollDirection =
					axisState.rightStick.y < 0.0f ? 0 : 1;
			}
			setActionDown(ScrollActions[rightStickScrollDirection], true);
		}
	}
}

void PhysicalInputManager::captureActionSnapshot(
	InputActionState& state,
	InputDeviceID sourceDeviceID)
{
	const InputDeviceID resolvedSourceDeviceID = resolveSourceDeviceID(
		sourceDeviceID);
	state.axis = resolvedSourceDeviceID > UnknownInputDeviceID
		? axisState : GamepadAxisState{};
	state.sourceDeviceID = resolvedSourceDeviceID;
}

InputDeviceID PhysicalInputManager::resolveSourceDeviceID(
	InputDeviceID sourceDeviceID) const
{
	if (sourceDeviceID != UnknownInputDeviceID)
	{
		return sourceDeviceID;
	}
	return activeInstanceID == 0
		? UnknownInputDeviceID
		: static_cast<InputDeviceID>(activeInstanceID);
}

std::size_t PhysicalInputManager::actionIndex(InputAction inputAction)
{
	return static_cast<std::size_t>(inputAction);
}

bool PhysicalInputManager::isValidButton(SDL_GamepadButton button)
{
	return button >= SDL_GAMEPAD_BUTTON_SOUTH && button < SDL_GAMEPAD_BUTTON_COUNT;
}

bool PhysicalInputManager::isValidAxis(SDL_GamepadAxis axis)
{
	return axis >= SDL_GAMEPAD_AXIS_LEFTX && axis < SDL_GAMEPAD_AXIS_COUNT;
}
}
