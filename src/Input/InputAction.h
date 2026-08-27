#pragma once

#include <cstddef>
#include <cstdint>

namespace GameInput
{
using InputDeviceID = std::int64_t;

constexpr InputDeviceID UnknownInputDeviceID = 0;
constexpr InputDeviceID KeyboardInputDeviceID = -1;

enum class InputAction : std::uint8_t
{
	AttackPrimary = 0,
	CastSkill1,
	CastSkill2,
	CastSkill3,
	CastSkill4,
	CastSkill5,
	UseQuickItem1,
	UseQuickItem2,
	UseQuickItem3,
	Jump,
	InteractPrimary,
	InteractAlternate,
	CycleInteractionTarget,
	ToggleMiniMap,
	ToggleSit,
	OpenSystemMenu,
	OpenSettings,
	OpenMemo,
	OpenEquip,
	OpenGoods,
	NavigateUp,
	NavigateDown,
	NavigateLeft,
	NavigateRight,
	Confirm,
	Cancel,
	Secondary,
	ShowDetails,
	PreviousPanel,
	NextPanel,
	PreviousPage,
	NextPage,
	ToggleTouchControls,
	Move,
	ScrollUp,
	ScrollDown,
	ScrollLeft,
	ScrollRight,
	Count
};

constexpr std::size_t InputActionCount = static_cast<std::size_t>(InputAction::Count);

struct AnalogStickState
{
	float x = 0.0f;
	float y = 0.0f;
	float magnitude = 0.0f;
};

struct GamepadAxisState
{
	AnalogStickState leftStick;
	AnalogStickState rightStick;
	float leftTrigger = 0.0f;
	float rightTrigger = 0.0f;
	bool running = false;
};

struct InputActionState
{
	bool down = false;
	bool pressed = false;
	bool released = false;
	GamepadAxisState axis;
	InputDeviceID sourceDeviceID = UnknownInputDeviceID;
};
}
