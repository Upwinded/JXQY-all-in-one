#pragma once

#include "InputAction.h"

#include <SDL3/SDL.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

namespace GameInput
{
class PhysicalInputManager
{
public:
	static constexpr std::uint64_t ToggleTouchControlsHoldMilliseconds = 600;
	static constexpr std::uint64_t ShoulderChordGraceMilliseconds = 80;
	static constexpr float StickDeadZone = 0.20f;
	static constexpr float StickOuterDeadZone = 0.95f;
	static constexpr float RunPressThreshold = 0.65f;
	static constexpr float RunReleaseThreshold = 0.60f;
	static constexpr float TriggerPressThreshold = 0.55f;
	static constexpr float TriggerReleaseThreshold = 0.45f;
	static constexpr float UIStickPressThreshold = 0.55f;
	static constexpr float UIStickReleaseThreshold = 0.35f;
	static constexpr float GamepadClaimStickThreshold = 0.55f;
	static constexpr float GamepadClaimTriggerThreshold = 0.55f;
	static constexpr float DirectionalJumpThreshold = 0.35f;
	static constexpr std::uint64_t NavigationRepeatDelayMilliseconds = 300;
	static constexpr std::uint64_t NavigationRepeatIntervalMilliseconds = 100;

	explicit PhysicalInputManager(bool touchControlsAvailable = false);
	~PhysicalInputManager();

	PhysicalInputManager(const PhysicalInputManager&) = delete;
	PhysicalInputManager& operator=(const PhysicalInputManager&) = delete;

	bool initialize();
	void shutdown();
	// EngineBase calls beginFrame before SDL_PollEvent and update after draining it.
	// Pressed/released edges remain valid between those calls and the next beginFrame.
	void beginFrame();
	// Returns true only when a global shortcut consumes the ordinary SDL event.
	bool processEvent(const SDL_Event& event);
	void update(std::uint64_t nowMilliseconds);
	void suspendInput();
	void resumeInput();
	void setWindowFocused(bool focused);
	void releaseAllInputs();
	void releaseForContextTransition();

	const InputActionState& action(InputAction inputAction) const;
	bool isActionDown(InputAction inputAction) const;
	bool wasActionPressed(InputAction inputAction) const;
	bool wasActionReleased(InputAction inputAction) const;
	bool consumePressed(InputAction inputAction);

	const GamepadAxisState& axes() const;
	bool hasActiveGamepad() const;
	bool isInputContextActive() const;
	std::uint64_t inputLifecycleRevision() const;
	std::uint64_t activeGamepadRemovalRevision() const;
	std::uint64_t gamepadAdditionRevision() const;
	std::uint64_t gamepadButtonPressRevision() const;
	SDL_JoystickID activeGamepadID() const;
	const std::string& activeGamepadName() const;
	SDL_GamepadButtonLabel buttonLabel(SDL_GamepadButton button) const;
	SDL_GamepadButtonLabel presentationButtonLabel(
		SDL_GamepadButton button) const;
	bool isButtonDown(SDL_GamepadButton button) const;
	std::size_t registeredGamepadCount() const;

private:
	using ButtonActionBindings = std::bitset<InputActionCount>;

	struct RegisteredGamepad
	{
		SDL_Gamepad* gamepad = nullptr;
		SDL_JoystickID instanceID = 0;
		std::string name;
		SDL_GamepadType type = SDL_GAMEPAD_TYPE_UNKNOWN;
		std::array<SDL_GamepadButtonLabel, SDL_GAMEPAD_BUTTON_COUNT> buttonLabels{};
		std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttonStates{};
		std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> rawAxisValues{};
		bool awaitingNeutral = true;
		bool claimReady = false;
	};

	void enumerateGamepads();
	bool registerGamepad(SDL_JoystickID instanceID);
	void unregisterGamepad(SDL_JoystickID instanceID);
	void closeAllGamepads(bool synthesizeRelease);
	void activateGamepad(RegisteredGamepad& gamepad);
	RegisteredGamepad* findRegisteredGamepad(SDL_JoystickID instanceID);
	const RegisteredGamepad* findRegisteredGamepad(SDL_JoystickID instanceID) const;
	void refreshRegisteredGamepadMetadata(RegisteredGamepad& gamepad);
	void refreshRegisteredGamepadSnapshot(RegisteredGamepad& gamepad);
	void updateNeutralGates();
	bool isRegisteredGamepadNeutral(const RegisteredGamepad& gamepad) const;
	bool axisCrossedClaimThreshold(const RegisteredGamepad& gamepad,
		SDL_GamepadAxis axis, Sint16 value) const;
	void processGamepadDeviceEvent(const SDL_Event& event);
	void processGamepadButtonEvent(const SDL_Event& event);
	void processGamepadAxisEvent(const SDL_Event& event);
	bool processKeyboardEvent(const SDL_Event& event);

	void handleButtonPressed(SDL_GamepadButton button, std::uint64_t nowMilliseconds);
	void handleButtonReleased(SDL_GamepadButton button);
	void bindDefaultButtonActions(SDL_GamepadButton button);
	void pulseDefaultButtonReleaseActions(SDL_GamepadButton button);
	bool bindRightShoulderChord(SDL_GamepadButton button);
	void releaseRightShoulderChordBindings();
	void beginTouchControlsChord(std::uint64_t nowMilliseconds);
	void endTouchControlsChord();
	void beginDirectionalJumpIfEligible();
	void resolvePendingDirectionalJump(std::uint64_t nowMilliseconds);
	void cancelPendingDirectionalJump();

	void bindButtonAction(SDL_GamepadButton button, InputAction inputAction);
	void releaseButtonActions(SDL_GamepadButton button);
	void setActionDown(
		InputAction inputAction,
		bool down,
		InputDeviceID sourceDeviceID = UnknownInputDeviceID);
	void pulseAction(
		InputAction inputAction,
		InputDeviceID sourceDeviceID = UnknownInputDeviceID);
	void clearActionEdges();
	void updateNavigationRepeat(std::uint64_t nowMilliseconds);
	void updateAxisState();
	void updateTriggerActions();
	void updateRightStickScrollActions();
	void captureActionSnapshot(
		InputActionState& state,
		InputDeviceID sourceDeviceID);
	InputDeviceID resolveSourceDeviceID(InputDeviceID sourceDeviceID) const;

	static std::size_t actionIndex(InputAction inputAction);
	static bool isValidButton(SDL_GamepadButton button);
	static bool isValidAxis(SDL_GamepadAxis axis);

	// All gamepads stay open so a neutral device can claim the single-player input
	// channel with a fresh button press or axis threshold crossing.
	bool initialized = false;
	std::vector<RegisteredGamepad> registeredGamepads;
	SDL_Gamepad* activeGamepad = nullptr;
	SDL_JoystickID activeInstanceID = 0;
	SDL_JoystickID previouslyActiveInstanceIDAwaitingReclaim = 0;
	std::string gamepadName;
	SDL_GamepadType gamepadType = SDL_GAMEPAD_TYPE_UNKNOWN;
	bool inputSuspended = false;
	bool windowFocused = true;
	bool touchControlsAvailable = false;
	std::uint64_t lifecycleRevision = 0;
	std::uint64_t activeGamepadRemovalRevisionValue = 0;
	std::uint64_t gamepadAdditionRevisionValue = 0;
	std::uint64_t gamepadButtonPressRevisionValue = 0;

	std::array<InputActionState, InputActionCount> actionStates{};
	std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttonStates{};
	std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> rawAxisValues{};
	std::array<ButtonActionBindings, SDL_GAMEPAD_BUTTON_COUNT> buttonActionBindings{};
	std::bitset<SDL_GAMEPAD_BUTTON_COUNT> rightShoulderChordButtons;
	GamepadAxisState axisState;

	bool leftShoulderConsumed = false;
	bool rightShoulderConsumed = false;
	bool shoulderPairChordActive = false;
	bool directionalJumpPending = false;
	bool directionalJumpTimerStarted = false;
	std::uint64_t directionalJumpStartedAt = 0;
	bool startConsumed = false;
	bool touchControlsChordActive = false;
	bool touchControlsChordFired = false;
	std::uint64_t touchControlsChordStartedAt = 0;
	bool leftTriggerPressed = false;
	bool rightTriggerPressed = false;
	std::array<std::uint64_t, 4> navigationNextRepeatAt{};
	std::array<std::uint64_t, 4> rightStickScrollNextRepeatAt{};
	int rightStickScrollDirection = -1;
};
}
