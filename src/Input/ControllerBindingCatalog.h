#pragma once

#include "InputAction.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace GameInput
{
enum class ControllerControl : std::uint8_t
{
	None,
	South,
	East,
	West,
	North,
	Back,
	Start,
	LeftShoulder,
	RightShoulder,
	LeftStickButton,
	RightStickButton,
	DPad,
	LeftTrigger,
	RightTrigger,
	LeftStick,
	RightStick
};

enum class ControllerDirection : std::uint8_t
{
	None,
	Up,
	Down,
	Left,
	Right
};

enum class ControllerBindingTrigger : std::uint8_t
{
	ButtonHeld,
	ButtonRelease,
	ButtonChordHeld,
	TriggerThreshold,
	StickContinuous,
	StickDirectional,
	LongPressChord,
	DirectionalChord
};

struct ControllerActionBinding
{
	InputAction action = InputAction::Count;
	ControllerControl primary = ControllerControl::None;
	ControllerControl modifier = ControllerControl::None;
	ControllerDirection direction = ControllerDirection::None;
	ControllerBindingTrigger trigger =
		ControllerBindingTrigger::ButtonHeld;
	bool showDirectionInPrompt = false;
};

struct ControllerControlLocation
{
	ControllerControl control = ControllerControl::None;
	ControllerDirection direction = ControllerDirection::None;

	constexpr bool valid() const
	{
		return control != ControllerControl::None;
	}
};

constexpr ControllerActionBinding makeControllerBinding(
	InputAction action,
	ControllerControl primary,
	ControllerBindingTrigger trigger,
	ControllerControl modifier = ControllerControl::None,
	ControllerDirection direction = ControllerDirection::None,
	bool showDirectionInPrompt = false)
{
	return
	{
		action,
		primary,
		modifier,
		direction,
		trigger,
		showDirectionInPrompt
	};
}

inline constexpr std::array<ControllerActionBinding, InputActionCount>
	DefaultControllerBindings =
{
	makeControllerBinding(InputAction::AttackPrimary,
		ControllerControl::West, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::CastSkill1,
		ControllerControl::RightTrigger,
		ControllerBindingTrigger::TriggerThreshold),
	makeControllerBinding(InputAction::CastSkill2,
		ControllerControl::North, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::CastSkill3,
		ControllerControl::East, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::CastSkill4,
		ControllerControl::South, ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder),
	makeControllerBinding(InputAction::CastSkill5,
		ControllerControl::East, ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder),
	makeControllerBinding(InputAction::UseQuickItem1,
		ControllerControl::LeftShoulder,
		ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder),
	makeControllerBinding(InputAction::UseQuickItem2,
		ControllerControl::West, ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder),
	makeControllerBinding(InputAction::UseQuickItem3,
		ControllerControl::North, ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder),
	makeControllerBinding(InputAction::Jump,
		ControllerControl::LeftStick,
		ControllerBindingTrigger::DirectionalChord,
		ControllerControl::LeftShoulder),
	makeControllerBinding(InputAction::InteractPrimary,
		ControllerControl::South, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::InteractAlternate,
		ControllerControl::RightShoulder,
		ControllerBindingTrigger::ButtonRelease),
	makeControllerBinding(InputAction::CycleInteractionTarget,
		ControllerControl::LeftTrigger,
		ControllerBindingTrigger::TriggerThreshold),
	makeControllerBinding(InputAction::ToggleMiniMap,
		ControllerControl::LeftStickButton,
		ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::ToggleSit,
		ControllerControl::RightStickButton,
		ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::OpenSystemMenu,
		ControllerControl::Start,
		ControllerBindingTrigger::ButtonRelease),
	makeControllerBinding(InputAction::OpenSettings,
		ControllerControl::DPad,
		ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder,
		ControllerDirection::Up,
		true),
	makeControllerBinding(InputAction::OpenMemo,
		ControllerControl::DPad,
		ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder,
		ControllerDirection::Down,
		true),
	makeControllerBinding(InputAction::OpenEquip,
		ControllerControl::DPad,
		ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder,
		ControllerDirection::Left,
		true),
	makeControllerBinding(InputAction::OpenGoods,
		ControllerControl::DPad,
		ControllerBindingTrigger::ButtonChordHeld,
		ControllerControl::RightShoulder,
		ControllerDirection::Right,
		true),
	makeControllerBinding(InputAction::NavigateUp,
		ControllerControl::DPad, ControllerBindingTrigger::ButtonHeld,
		ControllerControl::None, ControllerDirection::Up),
	makeControllerBinding(InputAction::NavigateDown,
		ControllerControl::DPad, ControllerBindingTrigger::ButtonHeld,
		ControllerControl::None, ControllerDirection::Down),
	makeControllerBinding(InputAction::NavigateLeft,
		ControllerControl::DPad, ControllerBindingTrigger::ButtonHeld,
		ControllerControl::None, ControllerDirection::Left),
	makeControllerBinding(InputAction::NavigateRight,
		ControllerControl::DPad, ControllerBindingTrigger::ButtonHeld,
		ControllerControl::None, ControllerDirection::Right),
	makeControllerBinding(InputAction::Confirm,
		ControllerControl::South, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::Cancel,
		ControllerControl::East, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::Secondary,
		ControllerControl::West, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::ShowDetails,
		ControllerControl::North, ControllerBindingTrigger::ButtonHeld),
	makeControllerBinding(InputAction::PreviousPanel,
		ControllerControl::LeftShoulder,
		ControllerBindingTrigger::ButtonRelease),
	makeControllerBinding(InputAction::NextPanel,
		ControllerControl::RightShoulder,
		ControllerBindingTrigger::ButtonRelease),
	makeControllerBinding(InputAction::PreviousPage,
		ControllerControl::LeftTrigger,
		ControllerBindingTrigger::TriggerThreshold),
	makeControllerBinding(InputAction::NextPage,
		ControllerControl::RightTrigger,
		ControllerBindingTrigger::TriggerThreshold),
	makeControllerBinding(InputAction::ToggleTouchControls,
		ControllerControl::Start,
		ControllerBindingTrigger::LongPressChord,
		ControllerControl::Back),
	makeControllerBinding(InputAction::Move,
		ControllerControl::LeftStick,
		ControllerBindingTrigger::StickContinuous),
	makeControllerBinding(InputAction::ScrollUp,
		ControllerControl::RightStick,
		ControllerBindingTrigger::StickDirectional,
		ControllerControl::None, ControllerDirection::Up),
	makeControllerBinding(InputAction::ScrollDown,
		ControllerControl::RightStick,
		ControllerBindingTrigger::StickDirectional,
		ControllerControl::None, ControllerDirection::Down),
	makeControllerBinding(InputAction::ScrollLeft,
		ControllerControl::RightStick,
		ControllerBindingTrigger::StickDirectional,
		ControllerControl::None, ControllerDirection::Left),
	makeControllerBinding(InputAction::ScrollRight,
		ControllerControl::RightStick,
		ControllerBindingTrigger::StickDirectional,
		ControllerControl::None, ControllerDirection::Right)
};

constexpr bool defaultControllerBindingIsWellFormed(
	const ControllerActionBinding& binding)
{
	const bool hasDirection = binding.direction != ControllerDirection::None;
	const bool hasModifier = binding.modifier != ControllerControl::None;
	if (binding.showDirectionInPrompt
		&& (binding.primary != ControllerControl::DPad || !hasDirection))
	{
		return false;
	}
	auto isFaceControl = [](ControllerControl control)
	{
		return control == ControllerControl::South
			|| control == ControllerControl::East
			|| control == ControllerControl::West
			|| control == ControllerControl::North;
	};
	switch (binding.trigger)
	{
	case ControllerBindingTrigger::ButtonHeld:
		return !hasModifier
			&& (isFaceControl(binding.primary)
				|| binding.primary == ControllerControl::LeftStickButton
				|| binding.primary == ControllerControl::RightStickButton
				|| binding.primary == ControllerControl::DPad)
			&& (binding.primary == ControllerControl::DPad) == hasDirection;
	case ControllerBindingTrigger::ButtonRelease:
		return !hasModifier && !hasDirection
			&& (binding.primary == ControllerControl::Start
				|| binding.primary == ControllerControl::LeftShoulder
				|| binding.primary == ControllerControl::RightShoulder);
	case ControllerBindingTrigger::ButtonChordHeld:
		return binding.modifier == ControllerControl::RightShoulder
			&& (isFaceControl(binding.primary)
				|| binding.primary == ControllerControl::LeftShoulder
				|| binding.primary == ControllerControl::DPad)
			&& (binding.primary == ControllerControl::DPad) == hasDirection;
	case ControllerBindingTrigger::TriggerThreshold:
		return !hasModifier && !hasDirection
			&& (binding.primary == ControllerControl::LeftTrigger
				|| binding.primary == ControllerControl::RightTrigger);
	case ControllerBindingTrigger::StickContinuous:
		return binding.primary == ControllerControl::LeftStick
			&& !hasModifier && !hasDirection;
	case ControllerBindingTrigger::StickDirectional:
		return binding.primary == ControllerControl::RightStick
			&& !hasModifier && hasDirection;
	case ControllerBindingTrigger::LongPressChord:
		return binding.primary == ControllerControl::Start
			&& binding.modifier == ControllerControl::Back
			&& !hasDirection;
	case ControllerBindingTrigger::DirectionalChord:
		return binding.primary == ControllerControl::LeftStick
			&& binding.modifier == ControllerControl::LeftShoulder
			&& !hasDirection;
	default:
		return false;
	}
}

constexpr bool defaultControllerBindingsAreCompleteAndWellFormed()
{
	for (std::size_t index = 0; index < DefaultControllerBindings.size(); index++)
	{
		if (static_cast<std::size_t>(DefaultControllerBindings[index].action)
			!= index
			|| DefaultControllerBindings[index].primary
				== ControllerControl::None
			|| !defaultControllerBindingIsWellFormed(
				DefaultControllerBindings[index]))
		{
			return false;
		}
	}
	return true;
}

static_assert(defaultControllerBindingsAreCompleteAndWellFormed(),
	"every InputAction must have one ordered, valid controller binding");

constexpr const ControllerActionBinding* defaultControllerBinding(
	InputAction action)
{
	const std::size_t index = static_cast<std::size_t>(action);
	return index < DefaultControllerBindings.size()
		? &DefaultControllerBindings[index]
		: nullptr;
}

constexpr std::size_t defaultControllerBindingMatchCount(
	ControllerControl primary,
	ControllerBindingTrigger trigger,
	ControllerControl modifier = ControllerControl::None,
	ControllerDirection direction = ControllerDirection::None)
{
	std::size_t count = 0;
	for (const ControllerActionBinding& binding : DefaultControllerBindings)
	{
		if (binding.primary == primary
			&& binding.modifier == modifier
			&& binding.direction == direction
			&& binding.trigger == trigger)
		{
			count++;
		}
	}
	return count;
}

constexpr InputAction findUniqueDefaultControllerAction(
	ControllerControl primary,
	ControllerBindingTrigger trigger,
	ControllerControl modifier = ControllerControl::None,
	ControllerDirection direction = ControllerDirection::None)
{
	if (defaultControllerBindingMatchCount(
		primary, trigger, modifier, direction) != 1)
	{
		return InputAction::Count;
	}
	for (const ControllerActionBinding& binding : DefaultControllerBindings)
	{
		if (binding.primary == primary
			&& binding.modifier == modifier
			&& binding.direction == direction
			&& binding.trigger == trigger)
		{
			return binding.action;
		}
	}
	return InputAction::Count;
}

constexpr std::array<InputAction, 4> defaultControllerDirectionalActions(
	ControllerControl control,
	ControllerBindingTrigger trigger)
{
	return
	{
		findUniqueDefaultControllerAction(control, trigger,
			ControllerControl::None, ControllerDirection::Up),
		findUniqueDefaultControllerAction(control, trigger,
			ControllerControl::None, ControllerDirection::Down),
		findUniqueDefaultControllerAction(control, trigger,
			ControllerControl::None, ControllerDirection::Left),
		findUniqueDefaultControllerAction(control, trigger,
			ControllerControl::None, ControllerDirection::Right)
	};
}

constexpr ControllerControlLocation controllerControlForButton(
	SDL_GamepadButton button)
{
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_SOUTH:
		return { ControllerControl::South, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_EAST:
		return { ControllerControl::East, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_WEST:
		return { ControllerControl::West, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_NORTH:
		return { ControllerControl::North, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_BACK:
		return { ControllerControl::Back, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_START:
		return { ControllerControl::Start, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
		return { ControllerControl::LeftShoulder, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
		return { ControllerControl::RightShoulder, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_LEFT_STICK:
		return { ControllerControl::LeftStickButton, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
		return { ControllerControl::RightStickButton, ControllerDirection::None };
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		return { ControllerControl::DPad, ControllerDirection::Up };
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		return { ControllerControl::DPad, ControllerDirection::Down };
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		return { ControllerControl::DPad, ControllerDirection::Left };
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		return { ControllerControl::DPad, ControllerDirection::Right };
	default:
		return {};
	}
}
}
