#pragma once

#include "Input/PhysicalInputManager.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace VirtualGamepadTest
{
inline void require(bool condition, const std::string& message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

class SDLSession
{
public:
	SDLSession()
	{
		require(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD),
			std::string("SDL initialization failed: ") + SDL_GetError());
	}

	~SDLSession()
	{
		SDL_Quit();
	}

	SDLSession(const SDLSession&) = delete;
	SDLSession& operator=(const SDLSession&) = delete;
};

class VirtualGamepad
{
public:
	explicit VirtualGamepad(const char* name)
	{
		SDL_VirtualJoystickDesc description;
		SDL_INIT_INTERFACE(&description);
		description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
		description.vendor_id = 0x045e;
		description.product_id = nextProductID++;
		description.naxes = SDL_GAMEPAD_AXIS_COUNT;
		description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
		description.button_mask = (Uint32(1) << SDL_GAMEPAD_BUTTON_COUNT) - 1;
		description.axis_mask = (Uint32(1) << SDL_GAMEPAD_AXIS_COUNT) - 1;
		description.name = name;
		instanceID = SDL_AttachVirtualJoystick(&description);
		require(instanceID != 0,
			std::string("virtual gamepad attachment failed: ") + SDL_GetError());
		require(SDL_IsGamepad(instanceID), "virtual joystick was not exposed as a gamepad");
		joystick = SDL_OpenJoystick(instanceID);
		require(joystick != nullptr,
			std::string("virtual joystick open failed: ") + SDL_GetError());
	}

	~VirtualGamepad()
	{
		detach();
	}

	VirtualGamepad(const VirtualGamepad&) = delete;
	VirtualGamepad& operator=(const VirtualGamepad&) = delete;

	SDL_JoystickID id() const
	{
		return instanceID;
	}

	void setButton(SDL_GamepadButton button, bool down)
	{
		require(joystick != nullptr, "cannot update a detached virtual gamepad");
		require(SDL_SetJoystickVirtualButton(joystick, static_cast<int>(button), down),
			std::string("virtual button update failed: ") + SDL_GetError());
	}

	void setAxis(SDL_GamepadAxis axis, Sint16 value)
	{
		require(joystick != nullptr, "cannot update a detached virtual gamepad");
		require(SDL_SetJoystickVirtualAxis(joystick, static_cast<int>(axis), value),
			std::string("virtual axis update failed: ") + SDL_GetError());
	}

	void detach()
	{
		if (joystick != nullptr)
		{
			SDL_CloseJoystick(joystick);
			joystick = nullptr;
		}
		if (instanceID != 0)
		{
			SDL_DetachVirtualJoystick(instanceID);
			instanceID = 0;
		}
	}

private:
	inline static Uint16 nextProductID = 1;
	SDL_JoystickID instanceID = 0;
	SDL_Joystick* joystick = nullptr;
};

inline void runFrame(GameInput::PhysicalInputManager& inputManager,
	std::uint64_t nowMilliseconds)
{
	inputManager.beginFrame();
	SDL_PumpEvents();
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		inputManager.processEvent(event);
	}
	inputManager.update(nowMilliseconds);
}

inline std::size_t connectedGamepadCount()
{
	int count = 0;
	SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&count);
	SDL_free(gamepadIDs);
	return count < 0 ? 0 : static_cast<std::size_t>(count);
}
}
