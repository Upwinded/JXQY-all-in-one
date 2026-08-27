#pragma once
#include "../../Element/Element.h"

#include <array>
#include <cstdint>
#include <set>

class JoystickPanel;
class SkillsPanel;
class GameElement;
class GamepadWorldRuntimeTestAccess;
struct NextAction;
enum class WorldInteractionIntent;

namespace GameInput
{
enum class InputAction : std::uint8_t;
struct GamepadAxisState;
}

class GameController :
	public Element
{
	friend class GamepadWorldRuntimeTestAccess;
	friend class MobileExternalInputRuntimeTestAccess;
public:
	static constexpr int KeyboardAutoInteractionTileDistance = 13;
	static constexpr int FastInteractionTileDistance = 3;

	GameController();
	virtual ~GameController();

	void freeResource();
	void init();
	virtual void onChildCallBack(PElement child);
	virtual void onEvent();
	virtual void onUpdate();
	virtual void onDrawEnd() override;
	virtual bool onHandleEvent(AEvent & e);
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
	bool onHandleEvent(AEvent&& event)
	{
		return onHandleEvent(event);
	}
#endif
	void setTouchControlsVisible(bool visible);
	void toggleTouchControls();
	bool areTouchControlsVisible() const { return touchControlsVisible; }
	void cancelControllerWorldInteraction();
	void cancelTouchControlInput();
	bool synchronizeInputLifecycle();
	void processPhysicalInputFrame();

protected:
	virtual bool shouldUpdateChild(PElement child) override;
	virtual void onPreviewPointerEvent(AEvent& event) override;

public:
	bool MouseAlreadyDown = false;

	std::shared_ptr<JoystickPanel> joystickPanel = nullptr;
	std::shared_ptr<SkillsPanel> skillPanel = nullptr;
	void setFastSelectBtn(
		int index,
		bool sVisible,
		std::string str = "",
		bool hostile = false);

private:
	int _last_magic_index = -1;
#if defined(__MOBILE__)
	bool touchControlsVisible = true;
#else
	bool touchControlsVisible = false;
#endif
	std::uint32_t activeGamepadInstanceID = 0;
	std::uint64_t observedInputLifecycleRevision = 0;
	bool worldInputContextObserved = false;
	bool worldInputWasEnabled = false;
	bool mouseWorldInputSuppressedUntilRelease = false;
	std::weak_ptr<GameElement> controllerFocusedTarget;
	std::set<EventTouchID> virtualControlPointerTransactions;
	struct FastInteractionPressBinding
	{
		std::weak_ptr<GameElement> target;
		int destinationKind = 0;
		bool useRightScript = false;
		bool active = false;
	};
	std::array<FastInteractionPressBinding, 4>
		fastInteractionPressBindings;
		
	void handlePhysicalInput();
	bool canHandleWorldInput() const;
	void handlePhysicalMovement(float axisX, float axisY, float magnitude, bool running);
	void handlePhysicalAttack(bool running);
	void handlePhysicalSkill(int skillIndex, float aimX, float aimY, float aimMagnitude);
	void handlePhysicalJump(float axisX, float axisY, float magnitude);
	void useQuickItem(int slotIndex);
	void prepareLegacyWorldAction();
	bool tryToggleLegacySit();
	bool submitLegacyWorldAction(NextAction& action);
	void handleLegacyHeldMouseMovement(
		bool leftMousePressed,
		bool pointerInputOwnedByUI);
	void handleLegacyKeyboardMovement(
		bool up, bool down, bool left, bool right, bool running);
	void dispatchPhysicalWorldAction(
		GameInput::InputAction action,
		const GameInput::GamepadAxisState& axes);
	bool queueControllerInteraction(WorldInteractionIntent intent, bool running);
	void cycleControllerInteractionTarget();
	void cancelPendingControllerInteraction(bool clearTarget);
	void validateControllerFocusedTarget();
	void resetTouchControlsInputState();
	void cancelTouchControlPointerTransactions();
	void isolateHeldMouseForTouchControlsVisibilityChange(
		bool leftMousePressed);
	void toggleMinimap();
	Point getPlayerRelativePosition(float angle, float distance, float xFactor);

};
