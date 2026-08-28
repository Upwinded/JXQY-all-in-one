#pragma once

#include "../Element/Element.h"
#include "VirtualGamepadTestHarness.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace HeadlessPhysicalInputTest
{
namespace Detail
{
class ElementStateAccess : public Element
{
public:
	static std::vector<PElement> replaceRunningOwner(PElement owner)
	{
		std::vector<PElement> previousOwners = std::move(runningElement);
		runningElement = { std::move(owner) };
		return previousOwners;
	}

	static void restoreRunningOwners(std::vector<PElement> owners)
	{
		runningElement = std::move(owners);
	}

	static bool replaceSemanticInputBlocked(bool blocked)
	{
		return std::exchange(frameSemanticInputBlocked, blocked);
	}
};
}

class ScopedPhysicalInputManager
{
public:
	explicit ScopedPhysicalInputManager(
		GameInput::PhysicalInputManager& inputManager)
		: inputManager(inputManager)
	{
		inputManager.shutdown();
		initialized = inputManager.initialize();
	}

	~ScopedPhysicalInputManager()
	{
		inputManager.shutdown();
	}

	ScopedPhysicalInputManager(const ScopedPhysicalInputManager&) = delete;
	ScopedPhysicalInputManager& operator=(const ScopedPhysicalInputManager&) = delete;

	bool isInitialized() const
	{
		return initialized;
	}

private:
	GameInput::PhysicalInputManager& inputManager;
	bool initialized = false;
};

class ScopedRunningOwner
{
public:
	explicit ScopedRunningOwner(PElement owner)
		: previousOwners(
			Detail::ElementStateAccess::replaceRunningOwner(std::move(owner)))
	{
	}

	~ScopedRunningOwner()
	{
		Detail::ElementStateAccess::restoreRunningOwners(
			std::move(previousOwners));
	}

	ScopedRunningOwner(const ScopedRunningOwner&) = delete;
	ScopedRunningOwner& operator=(const ScopedRunningOwner&) = delete;

private:
	std::vector<PElement> previousOwners;
};

class ScopedSemanticInputBarrier
{
public:
	explicit ScopedSemanticInputBarrier(bool blocked)
		: previousBlocked(
			Detail::ElementStateAccess::replaceSemanticInputBlocked(blocked))
	{
	}

	~ScopedSemanticInputBarrier()
	{
		Detail::ElementStateAccess::replaceSemanticInputBlocked(previousBlocked);
	}

	ScopedSemanticInputBarrier(const ScopedSemanticInputBarrier&) = delete;
	ScopedSemanticInputBarrier& operator=(const ScopedSemanticInputBarrier&) = delete;

private:
	bool previousBlocked = false;
};

struct FrameCallbacks
{
	std::function<void(const GameInput::PhysicalInputManager&)>
		afterInputUpdate;
	std::function<void(bool)> afterDispatch;
};

class FrameDriver
{
public:
	using SemanticDispatch = std::function<bool()>;
	using GameplayDispatch = std::function<void()>;

	FrameDriver(
		GameInput::PhysicalInputManager& inputManager,
		std::uint64_t& nowMilliseconds,
		SemanticDispatch semanticDispatch,
		GameplayDispatch gameplayDispatch)
		: inputManager(inputManager),
		nowMilliseconds(nowMilliseconds),
		semanticDispatch(std::move(semanticDispatch)),
		gameplayDispatch(std::move(gameplayDispatch))
	{
	}

	bool runFrame(
		std::uint64_t elapsedMilliseconds = 10,
		const FrameCallbacks& callbacks = {})
	{
		nowMilliseconds += elapsedMilliseconds;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
		if (callbacks.afterInputUpdate)
		{
			callbacks.afterInputUpdate(inputManager);
		}

		const bool semanticInputBlocked = semanticDispatch
			? semanticDispatch()
			: false;
		if (gameplayDispatch)
		{
			ScopedSemanticInputBarrier semanticInputBarrier(
				semanticInputBlocked);
			gameplayDispatch();
		}

		if (callbacks.afterDispatch)
		{
			callbacks.afterDispatch(semanticInputBlocked);
		}
		return semanticInputBlocked;
	}

	bool runButtonFrame(
		VirtualGamepadTest::VirtualGamepad& gamepad,
		SDL_GamepadButton button,
		bool down,
		const FrameCallbacks& callbacks = {})
	{
		gamepad.setButton(button, down);
		return runFrame(10, callbacks);
	}

	bool tapButton(
		VirtualGamepadTest::VirtualGamepad& gamepad,
		SDL_GamepadButton button,
		const FrameCallbacks& pressCallbacks = {},
		const FrameCallbacks& releaseCallbacks = {})
	{
		const bool pressBlocked = runButtonFrame(
			gamepad, button, true, pressCallbacks);
		runButtonFrame(gamepad, button, false, releaseCallbacks);
		return pressBlocked;
	}

	bool runAxisFrame(
		VirtualGamepadTest::VirtualGamepad& gamepad,
		SDL_GamepadAxis axis,
		Sint16 value,
		const FrameCallbacks& callbacks = {})
	{
		gamepad.setAxis(axis, value);
		return runFrame(10, callbacks);
	}

	bool pulseAxis(
		VirtualGamepadTest::VirtualGamepad& gamepad,
		SDL_GamepadAxis axis,
		Sint16 pressedValue,
		Sint16 neutralValue,
		const FrameCallbacks& pressCallbacks = {},
		const FrameCallbacks& releaseCallbacks = {})
	{
		const bool pressBlocked = runAxisFrame(
			gamepad, axis, pressedValue, pressCallbacks);
		runAxisFrame(gamepad, axis, neutralValue, releaseCallbacks);
		return pressBlocked;
	}

private:
	GameInput::PhysicalInputManager& inputManager;
	std::uint64_t& nowMilliseconds;
	SemanticDispatch semanticDispatch;
	GameplayDispatch gameplayDispatch;
};
}
