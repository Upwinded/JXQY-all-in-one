#include "../Component/Joystick.h"
#include "../Component/RoundButton.h"
#include "../Component/TextButton.h"
#include "../Element/Element.h"
#include "../Engine/Engine.h"
#include "../File/File.h"
#include "../Game/Game.h"
#include "../Game/Data/NPC.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/GoodsMenu.h"
#include "../Game/Menu/JoystickPanel.h"
#include "../Game/Menu/MapThumbnailMenu.h"
#include "../Game/Menu/MsgBox.h"
#include "../Game/Menu/PartnerEquipMenu.h"
#include "../Game/Menu/SkillsPanel.h"
#include "../Game/Menu/SystemNotice.h"
#include "../Game/Scene/TitleTeam.h"
#include "../Input/PhysicalInputManager.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/ResourcePackList.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "VirtualGamepadTestHarness.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef __MOBILE__
class MobileExternalInputRuntimeTestAccess
{
public:
	static GameInput::PhysicalInputManager& inputManager()
	{
		return *Engine::getInstance()->physicalInputManager;
	}

	static void pumpEngineEvents()
	{
		Engine::getInstance()->handleEvent();
	}

	static std::vector<AEvent> drainEngineEvents()
	{
		std::vector<AEvent> events;
		AEvent event;
		while (Engine::getInstance()->getEvent(event) > 0)
		{
			events.push_back(event);
		}
		return events;
	}

	static void configureHeadlessTouchViewport(int width, int height)
	{
		EngineBase* engineBase = static_cast<EngineBase*>(Engine::getInstance());
		engineBase->width = width;
		engineBase->height = height;
		engineBase->rect = { 0, 0, width, height };
	}

	static void dispatchElementEvents(Element& root)
	{
		root.allHandleEvents();
	}

	static bool acquirePointer(
		Element& root,
		EventTouchID pointerID,
		int x,
		int y)
	{
		return root.checkAllTouchDown(pointerID, x, y);
	}

	static void arrangeElementTree(Element& root)
	{
		root.reArrangeChildren();
		for (const auto& child : root.children)
		{
			if (child != nullptr)
			{
				arrangeElementTree(*child);
			}
		}
	}

	static bool hasInteractiveRuntimeState()
	{
		return Engine::getInstance()->window != nullptr
			|| !Element::runningElement.empty();
	}

	static bool hasPointerState(const PElement& element, EventTouchID pointerID)
	{
		return element->touchingID == pointerID
			|| element->touchingDownID == pointerID;
	}

	static void seedPointerState(
		const PElement& element,
		EventTouchID pointerID,
		unsigned int pendingResult,
		bool dragging)
	{
		element->touchingID = pointerID;
		element->touchingDownID = pointerID;
		element->result = pendingResult;
		if (dragging)
		{
			Element::dragging = pointerID;
			Element::currentDragItem = element;
		}
	}

	static bool hasActiveDrag()
	{
		return Element::dragging != TOUCH_UNTOUCHEDID
			|| Element::currentDragItem != nullptr;
	}

	static void seedResourcePackListPointer(
		ResourcePackList& list,
		EventTouchID pointerID)
	{
		list.listPointerDown = true;
		list.listPointerDragging = true;
		list.listPointerId = pointerID;
		list.listPointerDownX = 17;
		list.listPointerDownY = 31;
		list.pointerStartFirstVisibleIndex = 2;
	}

	static bool resourcePackListPointerCleared(const ResourcePackList& list)
	{
		return !list.listPointerDown
			&& !list.listPointerDragging
			&& list.listPointerId == TOUCH_UNTOUCHEDID
			&& list.listPointerDownX == 0
			&& list.listPointerDownY == 0;
	}

	static void seedTitleTeamPointer(TitleTeam& title, EventTouchID pointerID)
	{
		title.activePointer = pointerID;
		title.pointerStartY = 83;
		title.pointerStartLine = 4;
	}

	static bool titleTeamPointerCleared(const TitleTeam& title)
	{
		return title.activePointer == TOUCH_UNTOUCHEDID
			&& title.pointerStartY == 0
			&& title.pointerStartLine == title.firstVisibleLine;
	}

	static void synchronizeControllerLifecycle(
		GameController& controller,
		std::uint64_t revision)
	{
		controller.observedInputLifecycleRevision = revision;
	}

	static bool ownsVirtualControlPointerTransaction(
		const GameController& controller,
		EventTouchID pointerID)
	{
		return controller.virtualControlPointerTransactions.find(pointerID)
			!= controller.virtualControlPointerTransactions.end();
	}

	static bool suppressesMouseWorldInputUntilRelease(
		const GameController& controller)
	{
		return controller.mouseWorldInputSuppressedUntilRelease;
	}

	static void previewControllerPointerEvent(
		GameController& controller,
		AEvent& event)
	{
		controller.onPreviewPointerEvent(event);
	}

	static void seedSkillsTransientState(SkillsPanel& skillsPanel)
	{
		skillsPanel.clickIndex = SKILL_PANEL_JUMP;
		skillsPanel.dragEndPosition = { 23, 47 };
		skillsPanel._jumpBtnDagging = true;
		skillsPanel.dragBeginTime = 99;
	}

	static bool skillsTransientStateCleared(const SkillsPanel& skillsPanel)
	{
		return skillsPanel.clickIndex == SKILL_PANEL_NONE
			&& skillsPanel.dragEndPosition.x == 0
			&& skillsPanel.dragEndPosition.y == 0
			&& !skillsPanel._jumpBtnDagging
			&& skillsPanel.dragBeginTime == 0;
	}

	static void dispatchSkillButtonResult(
		SkillsPanel& skillsPanel,
		const PElement& button,
		unsigned int result)
	{
		button->result = result;
		skillsPanel.onChildCallBack(button);
	}

	static bool updateRecoveryGesture(
		GameManager& gameManager,
		std::vector<GameInput::TouchRecoveryContact> contacts,
		std::uint64_t nowMilliseconds)
	{
		return gameManager.touchControlsRecoveryGesture.update(
			std::move(contacts), nowMilliseconds, false);
	}

	static void processGlobalInputFrameWithContacts(
		GameManager& gameManager,
		bool toggleTouchControls,
		std::vector<GameInput::TouchRecoveryContact> contacts,
		std::uint64_t nowMilliseconds)
	{
		gameManager.processGlobalInputFrameWithContacts(
			toggleTouchControls,
			std::move(contacts),
			nowMilliseconds);
	}

	static void pushEngineEvent(const AEvent& event)
	{
		AEvent mutableEvent = event;
		Engine::getInstance()->pushEvent(mutableEvent);
	}

	static const std::string& pendingExternalInputMessage(
		const GameManager& gameManager)
	{
		return gameManager.pendingExternalInputMessage;
	}

	static void updateGameManager(GameManager& gameManager)
	{
		gameManager.onUpdate();
	}
};

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

bool checkStrictlyHeadless(const std::string& context)
{
	const bool videoInactive =
		(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
	return check(videoInactive,
		context + ": SDL video subsystem was initialized")
		&& check(!MobileExternalInputRuntimeTestAccess::hasInteractiveRuntimeState(),
			context + ": a window or Element::run owner was created");
}

class PointerCommitProbe : public RoundButton
{
public:
	int clickCount = 0;
	int dragEndCount = 0;

protected:
	void onClick() override
	{
		clickCount++;
	}

	void onDragEnd(PElement, int, int) override
	{
		dragEndCount++;
	}
};

class ChildResultCapture : public Element
{
public:
	std::vector<unsigned int> childResults;

	void onChildCallBack(PElement child) override
	{
		if (child != nullptr)
		{
			childResults.push_back(child->getResult());
		}
	}
};

class RawPointerGateProbe : public RoundButton
{
public:
	int keyDownCount = 0;
	int mouseWheelCount = 0;
	int eventCount = 0;
	int clickCount = 0;

	RawPointerGateProbe()
	{
		rect = { 0, 0, 100, 100 };
	}

protected:
	bool onHandleEvent(AEvent& event) override
	{
		if (event.eventType == ET_KEYDOWN)
		{
			keyDownCount++;
			return true;
		}
		if (event.eventType == ET_MOUSEWHEEL)
		{
			mouseWheelCount++;
			return true;
		}
		return false;
	}

	void onEvent() override
	{
		eventCount++;
	}

	void onClick() override
	{
		clickCount++;
	}
};

class TestJoystick : public Joystick
{
public:
	void activate()
	{
		rect = { 10000, 10000, 100, 100 };
		roundRange = 50;
		touchPosition = { 80, 50 };
	}

	void configureTouchArea()
	{
		rect = { 0, 0, 100, 100 };
		roundRange = 50;
		resetInput();
	}
};

constexpr unsigned int PointerResultBits = erClick | erMouseLDown
	| erMouseLUp | erMouseRDown | erMouseRUp | erDragEnd | erDragging
	| erDropped | erScrollbarSlided | erShowHint | erHideHint;

constexpr unsigned int BusinessResultBits = erExit | erActionEnd
	| erLifeExhaust | erReturnToTitle;

bool pushAndTranslateFingerEvent(
	Uint32 sdlEventType,
	EventType expectedEventType,
	EventTouchID pointerID)
{
	MobileExternalInputRuntimeTestAccess::drainEngineEvents();
	SDL_Event event{};
	event.type = sdlEventType;
	event.tfinger.fingerID = static_cast<SDL_FingerID>(pointerID);
	event.tfinger.x = 0.5f;
	event.tfinger.y = 0.5f;
	if (!check(SDL_PushEvent(&event),
		"SDL rejected a synthetic 64-bit finger event"))
	{
		return false;
	}
	MobileExternalInputRuntimeTestAccess::pumpEngineEvents();
	const std::vector<AEvent> translatedEvents =
		MobileExternalInputRuntimeTestAccess::drainEngineEvents();
	for (const AEvent& translatedEvent : translatedEvents)
	{
		if (translatedEvent.eventType == expectedEventType
			&& translatedEvent.eventData == pointerID)
		{
			return true;
		}
	}
	return check(false,
		"EngineBase truncated or dropped a 64-bit finger event");
}

bool testEnginePreserves64BitFingerIDs()
{
	MobileExternalInputRuntimeTestAccess::configureHeadlessTouchViewport(800, 600);
	constexpr EventTouchID pointerID =
		static_cast<EventTouchID>(std::numeric_limits<std::int32_t>::max())
		+ 0x123456LL;
	bool ok = check(pointerID > std::numeric_limits<std::int32_t>::max(),
		"64-bit finger fixture did not exceed INT32_MAX");
	ok = pushAndTranslateFingerEvent(
		SDL_EVENT_FINGER_DOWN, ET_FINGERDOWN, pointerID) && ok;
	ok = pushAndTranslateFingerEvent(
		SDL_EVENT_FINGER_UP, ET_FINGERUP, pointerID) && ok;
	ok = pushAndTranslateFingerEvent(
		SDL_EVENT_FINGER_MOTION, ET_FINGERMOTION, pointerID) && ok;
	ok = pushAndTranslateFingerEvent(
		SDL_EVENT_FINGER_CANCELED, ET_FINGERCANCEL, pointerID) && ok;
	return ok;
}

struct MobilePanelFixture
{
	std::shared_ptr<JoystickPanel> joystickPanel;
	std::shared_ptr<TestJoystick> joystick;
	std::shared_ptr<SkillsPanel> skillsPanel;
	std::shared_ptr<PointerCommitProbe> skillProbe;
};

MobilePanelFixture attachMobilePanels(GameManager& gameManager)
{
	MobilePanelFixture fixture;
	fixture.joystickPanel = std::make_shared<JoystickPanel>();
	fixture.joystickPanel->removeAllChild();
	fixture.joystick = std::make_shared<TestJoystick>();
	fixture.joystickPanel->joystick = fixture.joystick;
	fixture.joystickPanel->addChild(fixture.joystick);

	fixture.skillsPanel = std::make_shared<SkillsPanel>();
	fixture.skillsPanel->removeAllChild();
	fixture.skillsPanel->minimapButton = nullptr;
	fixture.skillProbe = std::make_shared<PointerCommitProbe>();
	fixture.skillProbe->rect = { 10000, 10000, 100, 100 };
	fixture.skillsPanel->attackBtn = fixture.skillProbe;
	fixture.skillsPanel->addChild(fixture.skillProbe);
	for (int buttonIndex = 0; buttonIndex < FASTBTN_COUNT; buttonIndex++)
	{
		fixture.skillsPanel->fastBtn[buttonIndex] =
			std::make_shared<TextButton>();
		fixture.skillsPanel->addChild(
			fixture.skillsPanel->fastBtn[buttonIndex]);
	}

	gameManager.controller->joystickPanel = fixture.joystickPanel;
	gameManager.controller->skillPanel = fixture.skillsPanel;
	gameManager.controller->addChild(fixture.joystickPanel);
	gameManager.controller->addChild(fixture.skillsPanel);
	return fixture;
}

bool testFastSelectHostilePresentation(
	GameManager& gameManager,
	MobilePanelFixture& panels)
{
	constexpr unsigned int DefaultTextColor = 0xFF000000;
	constexpr unsigned int HostileTextColor = 0xFFFF0000;
	const std::shared_ptr<TextButton>& button = panels.skillsPanel->fastBtn[0];

	gameManager.controller->setFastSelectBtn(0, true, "敌人", true);
	bool ok = check(
		button->visible
			&& button->getTextColor() == HostileTextColor,
		"hostile fast-select target did not use red text");

	gameManager.controller->setFastSelectBtn(0, true, "友方", false);
	ok = check(
		button->visible
			&& button->getTextColor() == DefaultTextColor,
		"reused fast-select target retained the hostile text color") && ok;

	gameManager.controller->setFastSelectBtn(0, false);
	ok = check(
		!button->visible
			&& button->getTextColor() == DefaultTextColor,
		"hidden fast-select target retained the hostile color") && ok;

	return ok;
}

std::uint64_t runPhysicalInputFrameAtCurrentTime(
	GameInput::PhysicalInputManager& inputManager,
	std::uint64_t updateOffsetMilliseconds = 0)
{
	static std::uint64_t lastUpdateMilliseconds = 0;
	inputManager.beginFrame();
	SDL_PumpEvents();
	SDL_Event event{};
	while (SDL_PollEvent(&event))
	{
		inputManager.processEvent(event);
	}
	std::uint64_t nowMilliseconds =
		static_cast<std::uint64_t>(SDL_GetTicks());
	if (nowMilliseconds <= lastUpdateMilliseconds)
	{
		nowMilliseconds = lastUpdateMilliseconds + 1;
	}
	nowMilliseconds += updateOffsetMilliseconds;
	lastUpdateMilliseconds = nowMilliseconds;
	inputManager.update(nowMilliseconds);
	return nowMilliseconds;
}

bool pushKeyboardToggleEvent(Uint32 eventType)
{
	SDL_Event event{};
	event.type = eventType;
	event.key.scancode = SDL_SCANCODE_H;
	event.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
	event.key.repeat = false;
	return check(SDL_PushEvent(&event),
		"SDL rejected the synthetic Ctrl+Shift+H event");
}

bool pressKeyboardToggleShortcut(
	GameInput::PhysicalInputManager& inputManager,
	const std::string& context)
{
	MobileExternalInputRuntimeTestAccess::drainEngineEvents();
	if (!pushKeyboardToggleEvent(SDL_EVENT_KEY_DOWN))
	{
		return false;
	}
	MobileExternalInputRuntimeTestAccess::pumpEngineEvents();
	MobileExternalInputRuntimeTestAccess::drainEngineEvents();
	const auto& toggleState = inputManager.action(
		GameInput::InputAction::ToggleTouchControls);
	return check(toggleState.pressed,
		context + ": Ctrl+Shift+H did not create a toggle press edge")
		&& check(toggleState.sourceDeviceID
			== GameInput::KeyboardInputDeviceID,
			context + ": Ctrl+Shift+H lost its keyboard source identity");
}

bool releaseKeyboardToggleShortcut(
	GameInput::PhysicalInputManager& inputManager,
	const std::string& context)
{
	if (!pushKeyboardToggleEvent(SDL_EVENT_KEY_UP))
	{
		return false;
	}
	MobileExternalInputRuntimeTestAccess::pumpEngineEvents();
	const bool releasedWithoutPress = check(!inputManager.wasActionPressed(
		GameInput::InputAction::ToggleTouchControls),
		context + ": Ctrl+Shift+H key-up created a toggle press edge");
	MobileExternalInputRuntimeTestAccess::drainEngineEvents();
	return checkStrictlyHeadless(context + ": keyboard release")
		&& releasedWithoutPress;
}

bool queueExternalMouseTapAndWheel(const std::string& context)
{
	SDL_Event motion{};
	motion.type = SDL_EVENT_MOUSE_MOTION;
	motion.motion.x = 50.0f;
	motion.motion.y = 50.0f;
	if (!check(SDL_PushEvent(&motion),
		context + ": SDL rejected external mouse motion"))
	{
		return false;
	}

	SDL_Event buttonDown{};
	buttonDown.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	buttonDown.button.button = SDL_BUTTON_LEFT;
	buttonDown.button.x = 50.0f;
	buttonDown.button.y = 50.0f;
	if (!check(SDL_PushEvent(&buttonDown),
		context + ": SDL rejected external mouse down"))
	{
		return false;
	}

	SDL_Event buttonUp = buttonDown;
	buttonUp.type = SDL_EVENT_MOUSE_BUTTON_UP;
	if (!check(SDL_PushEvent(&buttonUp),
		context + ": SDL rejected external mouse up"))
	{
		return false;
	}

	SDL_Event wheel{};
	wheel.type = SDL_EVENT_MOUSE_WHEEL;
	wheel.wheel.y = 1.0f;
	wheel.wheel.mouse_x = 50.0f;
	wheel.wheel.mouse_y = 50.0f;
	return check(SDL_PushEvent(&wheel),
		context + ": SDL rejected external mouse wheel");
}

void dispatchPointerTap(
	GameManager& gameManager,
	bool finger,
	EventTouchID pointerID,
	int x,
	int y)
{
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(
			finger ? ET_FINGERDOWN : ET_MOUSEDOWN,
			finger ? pointerID : MBC_MOUSE_LEFT,
			x,
			y,
			false));
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(
			finger ? ET_FINGERUP : ET_MOUSEUP,
			finger ? pointerID : MBC_MOUSE_LEFT,
			x,
			y,
			false));
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
}

void dispatchPointerEdge(
	GameManager& gameManager,
	bool finger,
	bool pointerDown,
	EventTouchID pointerID,
	int x,
	int y)
{
	if (!finger && pointerDown)
	{
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, x, y, false));
	}
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(
			finger
				? (pointerDown ? ET_FINGERDOWN : ET_FINGERUP)
				: (pointerDown ? ET_MOUSEDOWN : ET_MOUSEUP),
			finger ? pointerID : MBC_MOUSE_LEFT,
			x,
			y,
			false));
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
}

void seedTouchControlsTransientState(
	GameManager& gameManager,
	MobilePanelFixture& panels,
	EventTouchID pointerID)
{
	panels.joystick->activate();
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.joystickPanel, pointerID,
		erMouseLDown | erActionEnd, false);
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.joystick, pointerID + 1,
		erMouseLDown | erActionEnd, false);
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.skillsPanel, pointerID + 2,
		erMouseLDown | erActionEnd, false);
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.skillProbe, pointerID + 3,
		erClick | erDragEnd | erActionEnd, true);
	MobileExternalInputRuntimeTestAccess::seedSkillsTransientState(
		*panels.skillsPanel);
	gameManager.fastSelectingList.clear();
	gameManager.fastSelectingList.push_back(NextAction{});
}

bool checkTouchControlsStateAfterToggle(
	GameManager& gameManager,
	const MobilePanelFixture& panels,
	bool expectedVisible,
	bool expectedControllerVisible,
	bool expectedControllerActivated,
	const std::string& context)
{
	bool ok = check(gameManager.controller->areTouchControlsVisible()
		== expectedVisible,
		context + ": controller touch visibility did not toggle")
		&& check(panels.joystickPanel->visible == expectedVisible
			&& panels.skillsPanel->visible == expectedVisible,
			context + ": mobile panel visibility did not follow the toggle")
		&& check(gameManager.controller->visible == expectedControllerVisible
			&& gameManager.controller->activated
				== expectedControllerActivated,
			context + ": toggle hid or deactivated the controller itself");
	ok = check(!panels.joystick->isWalking()
		&& !panels.joystick->isRunning(),
		context + ": stale joystick movement survived the toggle") && ok;
	ok = check(panels.joystickPanel->touchingID == TOUCH_UNTOUCHEDID
		&& panels.joystickPanel->touchingDownID == TOUCH_UNTOUCHEDID
		&& (panels.joystickPanel->result & PointerResultBits) == 0
		&& (panels.joystickPanel->result & erActionEnd) != 0,
		context + ": joystick panel pointer state was not selectively cleared") && ok;
	ok = check(panels.joystick->touchingID == TOUCH_UNTOUCHEDID
		&& panels.joystick->touchingDownID == TOUCH_UNTOUCHEDID
		&& (panels.joystick->result & PointerResultBits) == 0
		&& (panels.joystick->result & erActionEnd) != 0,
		context + ": joystick pointer state was not selectively cleared") && ok;
	ok = check(panels.skillsPanel->touchingID == TOUCH_UNTOUCHEDID
		&& panels.skillsPanel->touchingDownID == TOUCH_UNTOUCHEDID
		&& (panels.skillsPanel->result & PointerResultBits) == 0
		&& (panels.skillsPanel->result & erActionEnd) != 0,
		context + ": skills panel pointer state was not selectively cleared") && ok;
	ok = check(panels.skillProbe->touchingID == TOUCH_UNTOUCHEDID
		&& panels.skillProbe->touchingDownID == TOUCH_UNTOUCHEDID
		&& (panels.skillProbe->result & PointerResultBits) == 0
		&& (panels.skillProbe->result & erActionEnd) != 0,
		context + ": skill pointer state was not selectively cleared") && ok;
	ok = check(MobileExternalInputRuntimeTestAccess::skillsTransientStateCleared(
		*panels.skillsPanel),
		context + ": skill drag transient state survived the toggle") && ok;
	ok = check(gameManager.fastSelectingList.empty(),
		context + ": fast-select state survived the toggle") && ok;
	ok = check(!MobileExternalInputRuntimeTestAccess::hasActiveDrag()
		&& panels.skillProbe->clickCount == 0
		&& panels.skillProbe->dragEndCount == 0,
		context + ": cancellation committed or retained a pointer action") && ok;
	return ok;
}

bool dispatchGlobalToggle(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager,
	bool expectedVisible,
	const std::string& expectedMessage,
	const std::string& context)
{
	Element::dispatchFrameGlobalInput(Engine::getInstance());
	bool ok = check(gameManager.controller->areTouchControlsVisible()
		== expectedVisible,
		context + ": production global handler did not toggle visibility");
	ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::ToggleTouchControls),
		context + ": production global handler did not consume the toggle edge")
		&& ok;
	ok = check(gameManager.menu != nullptr
		&& gameManager.menu->systemNotice != nullptr
		&& gameManager.menu->systemNotice->currentMessage ==
			std::string(u8"系统：") + expectedMessage,
		context + ": visibility feedback message was incorrect") && ok;
	return ok;
}

bool checkGameManagerUpdateDoesNotRetoggle(
	GameManager& gameManager,
	bool expectedVisible,
	const std::string& context)
{
	const bool wasPaused = gameManager.isGameplayPaused();
	gameManager.setGameplayPaused(true);
	MobileExternalInputRuntimeTestAccess::updateGameManager(gameManager);
	const bool unchanged = gameManager.controller->areTouchControlsVisible()
		== expectedVisible;
	gameManager.setGameplayPaused(wasPaused);
	return check(unchanged,
		context + ": GameManager::onUpdate toggled the already-consumed action again");
}

bool checkGamepadTogglePulse(
	GameInput::PhysicalInputManager& inputManager,
	SDL_JoystickID expectedGamepadID,
	const std::string& context)
{
	const auto& toggleState = inputManager.action(
		GameInput::InputAction::ToggleTouchControls);
	return check(toggleState.pressed,
		context + ": held Back+Start did not create a toggle press edge")
		&& check(toggleState.sourceDeviceID
			== static_cast<GameInput::InputDeviceID>(expectedGamepadID),
			context + ": held Back+Start lost its gamepad source identity")
		&& check(!inputManager.wasActionPressed(
			GameInput::InputAction::OpenSystemMenu),
			context + ": Back+Start leaked an OpenSystemMenu press");
}

bool releaseGamepadChord(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	GameInput::PhysicalInputManager& inputManager,
	SDL_GamepadButton firstButton,
	SDL_GamepadButton secondButton,
	const std::string& context)
{
	gamepad.setButton(firstButton, false);
	runPhysicalInputFrameAtCurrentTime(inputManager);
	bool ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::OpenSystemMenu),
		context + ": first chord release leaked OpenSystemMenu");
	gamepad.setButton(secondButton, false);
	runPhysicalInputFrameAtCurrentTime(inputManager);
	ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::OpenSystemMenu),
		context + ": second chord release leaked OpenSystemMenu") && ok;
	return ok;
}

struct GlobalTouchControlsFixture
{
	GameManager gameManager;
	ScopedGameInputRegistration inputRegistration;
	MobilePanelFixture panels;
	std::shared_ptr<SystemNotice> systemNotice;

	GlobalTouchControlsFixture()
		: panels(attachMobilePanels(gameManager)),
		systemNotice(std::make_shared<SystemNotice>())
	{
		gameManager.menu->systemNotice = systemNotice;
	}
};

bool testGlobalTouchControlsInputFlow(
	GameInput::PhysicalInputManager& inputManager)
{
	GlobalTouchControlsFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	MobilePanelFixture& panels = fixture.panels;
	gameManager.global.data.canInput = true;
	gameManager.inEvent = false;
	gameManager.setGameplayPaused(false);
	gameManager.controller->setTouchControlsVisible(true);
	gameManager.processGlobalInputFrame();
	MobileExternalInputRuntimeTestAccess::synchronizeControllerLifecycle(
		*gameManager.controller, inputManager.inputLifecycleRevision());

	const bool controllerVisible = gameManager.controller->visible;
	const bool controllerActivated = gameManager.controller->activated;
	bool ok = checkStrictlyHeadless("global touch fixture initialization");
	ok = check(gameManager.controller->areTouchControlsVisible()
		&& panels.joystickPanel->visible && panels.skillsPanel->visible,
		"global touch fixture did not start with visible controls") && ok;

	gameManager.inEvent = true;
	seedTouchControlsTransientState(gameManager, panels, 201);
	ok = pressKeyboardToggleShortcut(
		inputManager, "in-event keyboard hide") && ok;
	ok = dispatchGlobalToggle(
		gameManager, inputManager, false, "已隐藏触控操作区",
		"in-event keyboard hide") && ok;
	ok = checkTouchControlsStateAfterToggle(
		gameManager, panels, false, controllerVisible, controllerActivated,
		"in-event keyboard hide") && ok;
	ok = checkGameManagerUpdateDoesNotRetoggle(
		gameManager, false, "in-event keyboard hide") && ok;
	ok = checkStrictlyHeadless("after in-event keyboard hide") && ok;
	ok = releaseKeyboardToggleShortcut(
		inputManager, "in-event keyboard hide") && ok;
	gameManager.inEvent = false;

	gameManager.global.data.canInput = false;
	ok = pressKeyboardToggleShortcut(
		inputManager, "canInput keyboard restore") && ok;
	ok = dispatchGlobalToggle(
		gameManager, inputManager, true, "已显示触控操作区",
		"canInput keyboard restore") && ok;
	ok = checkTouchControlsStateAfterToggle(
		gameManager, panels, true, controllerVisible, controllerActivated,
		"canInput keyboard restore") && ok;
	ok = checkStrictlyHeadless("after canInput keyboard restore") && ok;
	ok = releaseKeyboardToggleShortcut(
		inputManager, "canInput keyboard restore") && ok;
	gameManager.global.data.canInput = true;

	const std::size_t gamepadCountBeforeAttach =
		inputManager.registeredGamepadCount();
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY Global Touch Controls Pad");
	runPhysicalInputFrameAtCurrentTime(inputManager);
	gameManager.processGlobalInputFrame();
	ok = check(inputManager.registeredGamepadCount()
		== gamepadCountBeforeAttach + 1,
		"global touch fixture did not register its virtual gamepad") && ok;
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"gamepad connection automatically hid touch controls") && ok;
	ok = check(fixture.systemNotice->currentMessage
		== "系统：检测到手柄；触控操作区保持显示，可长按 Back+Start 切换",
		"gamepad connection notice did not describe the manual toggle") && ok;
	ok = checkStrictlyHeadless("after global touch gamepad connection") && ok;
	// A second neutral frame makes the claim gate explicit before Back claims it.
	runPhysicalInputFrameAtCurrentTime(inputManager);

	gameManager.menu->mapThumbnailMenu =
		std::make_shared<MapThumbnailMenu>();
	gameManager.menu->mapThumbnailMenu->visible = true;
	ok = check(gameManager.menu->blocksWorldInput(),
		"map thumbnail fixture did not block world input") && ok;
	ok = checkStrictlyHeadless("after blocking-menu construction") && ok;

	gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, true);
	runPhysicalInputFrameAtCurrentTime(inputManager);
	ok = check(inputManager.activeGamepadID() == gamepad.id(),
		"Back did not claim the virtual gamepad") && ok;

	gameManager.menu->mapThumbnailMenu->visible = false;
	ok = check(!gameManager.menu->blocksWorldInput(),
		"virtual-control coexistence fixture retained a semantic world barrier")
		&& ok;
	panels.skillProbe->rect = { 40, 40, 80, 80 };
	panels.skillsPanel->visible = true;
	panels.skillsPanel->activated = true;
	auto menuHitProbe = std::make_shared<PointerCommitProbe>();
	menuHitProbe->rect = panels.skillProbe->rect;
	menuHitProbe->setPriority(epMax);
	gameManager.menu->addChild(menuHitProbe);
	dispatchPointerTap(gameManager, false, TOUCH_MOUSEID, 80, 80);
	ok = check(menuHitProbe->clickCount == 1
			&& panels.skillProbe->clickCount == 0,
		"overlapping menu and virtual button did not choose the top menu hit")
		&& ok;
	menuHitProbe->visible = false;
	gameManager.player->nextAction = nullptr;
	dispatchPointerTap(gameManager, false, TOUCH_MOUSEID, 80, 80);
	dispatchPointerTap(gameManager, true, 30201, 80, 80);
	ok = check(panels.skillProbe->clickCount == 2
			&& gameManager.player->nextAction == nullptr,
		"visible virtual button rejected mouse or touch while a gamepad"
		" and menu semantic barrier were active, or the same edge leaked"
		" into world input") && ok;
	panels.skillsPanel->visible = false;
	dispatchPointerTap(gameManager, false, TOUCH_MOUSEID, 80, 80);
	ok = check(panels.skillProbe->clickCount == 2,
		"hidden virtual button remained in pointer hit testing") && ok;
	panels.skillsPanel->visible = true;
	gameManager.menu->removeChild(menuHitProbe);
	panels.skillProbe->clickCount = 0;
	gameManager.menu->mapThumbnailMenu->visible = true;

	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
	runPhysicalInputFrameAtCurrentTime(inputManager);
	ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::ToggleTouchControls),
		"Back then Start toggled before the hold threshold") && ok;
	ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::OpenSystemMenu),
		"Back then Start leaked OpenSystemMenu on press") && ok;
	MobileExternalInputRuntimeTestAccess::synchronizeControllerLifecycle(
		*gameManager.controller, inputManager.inputLifecycleRevision());
	seedTouchControlsTransientState(gameManager, panels, 301);
	runPhysicalInputFrameAtCurrentTime(
		inputManager,
		GameInput::PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 1);
	ok = checkGamepadTogglePulse(
		inputManager, gamepad.id(), "blocking-menu Back then Start hide") && ok;
	ok = dispatchGlobalToggle(
		gameManager, inputManager, false, "已隐藏触控操作区",
		"blocking-menu Back then Start hide") && ok;
	ok = checkTouchControlsStateAfterToggle(
		gameManager, panels, false, controllerVisible, controllerActivated,
		"blocking-menu Back then Start hide") && ok;
	ok = checkGameManagerUpdateDoesNotRetoggle(
		gameManager, false, "blocking-menu Back then Start hide") && ok;
	ok = releaseGamepadChord(
		gamepad, inputManager,
		SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
		"blocking-menu Back then Start hide") && ok;
	ok = checkStrictlyHeadless("after blocking-menu gamepad hide") && ok;
	gameManager.menu->mapThumbnailMenu->visible = false;
	// Button-down edges use SDL_GetTicks(), so let the real clock catch the
	// monotonic future hold frame before starting the reverse-order chord.
	SDL_Delay(GameInput::PhysicalInputManager::ToggleTouchControlsHoldMilliseconds
		+ 10);

	gameManager.setGameplayPaused(true);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_START, true);
	runPhysicalInputFrameAtCurrentTime(inputManager);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_BACK, true);
	runPhysicalInputFrameAtCurrentTime(inputManager);
	ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::ToggleTouchControls),
		"Start then Back toggled before the hold threshold") && ok;
	ok = check(!inputManager.wasActionPressed(
		GameInput::InputAction::OpenSystemMenu),
		"Start then Back leaked OpenSystemMenu on press") && ok;
	runPhysicalInputFrameAtCurrentTime(
		inputManager,
		GameInput::PhysicalInputManager::ToggleTouchControlsHoldMilliseconds + 1);
	ok = checkGamepadTogglePulse(
		inputManager, gamepad.id(), "paused Start then Back restore") && ok;
	ok = dispatchGlobalToggle(
		gameManager, inputManager, true, "已显示触控操作区",
		"paused Start then Back restore") && ok;
	ok = checkTouchControlsStateAfterToggle(
		gameManager, panels, true, controllerVisible, controllerActivated,
		"paused Start then Back restore") && ok;
	ok = checkGameManagerUpdateDoesNotRetoggle(
		gameManager, true, "paused Start then Back restore") && ok;
	ok = releaseGamepadChord(
		gamepad, inputManager,
		SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
		"paused Start then Back restore") && ok;
	gameManager.setGameplayPaused(false);
	ok = checkStrictlyHeadless("after paused gamepad restore") && ok;

	gamepad.detach();
	runPhysicalInputFrameAtCurrentTime(inputManager);
	gameManager.processGlobalInputFrame();
	ok = check(inputManager.registeredGamepadCount()
		== gamepadCountBeforeAttach,
		"global touch fixture left its virtual gamepad registered") && ok;
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"gamepad teardown changed the restored touch visibility") && ok;
	ok = checkStrictlyHeadless("after global touch gamepad teardown") && ok;
	return ok;
}

bool testPartnerModalCancelsHeldTouchControls()
{
	GameManager gameManager;
	MobilePanelFixture panels = attachMobilePanels(gameManager);
	gameManager.global.data.canInput = true;
	gameManager.menu->upMenu = std::make_shared<Panel>();
	gameManager.menu->goodsMenu = std::make_shared<GoodsMenu>();
	gameManager.menu->partnerEquipMenu =
		std::make_shared<PartnerEquipMenu>();
	gameManager.menu->addChild(gameManager.menu->upMenu);
	gameManager.menu->upMenu->addChild(gameManager.menu->goodsMenu);
	gameManager.menu->upMenu->addChild(
		gameManager.menu->partnerEquipMenu);
	// Production runs the global lifecycle stage before any pointer hit-test.
	// Synchronize this standalone fixture before seeding the held contact so
	// the first mouse down belongs to the current lifecycle instead of being
	// cleared as stale state.
	gameManager.processGlobalInputFrame();

	constexpr EventTouchID heldPointerID = 0x5A10;
	seedTouchControlsTransientState(gameManager, panels, heldPointerID);
	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->canEquip = 1;
	bool ok = check(gameManager.menu->openPartnerEquipment(partner, false),
		"partner modal did not open for held virtual-control cancellation");
	ok = checkTouchControlsStateAfterToggle(
		gameManager,
		panels,
		true,
		gameManager.controller->visible,
		gameManager.controller->activated,
		"partner modal opening") && ok;
	ok = check(gameManager.menu->partnerEquipMenu->visible
		&& gameManager.menu->partnerEquipMenu->isControllerFocusActive(),
		"partner modal lost its focus after canceling lower touch controls") && ok;

	const std::vector<PElement> modalCandidates =
		gameManager.menu->partnerEquipMenu->controllerFocusCandidates();
	const std::vector<PElement> goodsCandidates =
		gameManager.menu->goodsMenu->controllerFocusCandidates();
	auto menuOwnsPoint = [&](int x, int y)
	{
		if (gameManager.menu->partnerEquipMenu->rect.PointInRect(x, y)
			|| gameManager.menu->goodsMenu->rect.PointInRect(x, y))
		{
			return true;
		}
		auto candidateOwnsPoint = [x, y](
			const std::vector<PElement>& candidates)
		{
			return std::any_of(
				candidates.begin(), candidates.end(),
				[x, y](const PElement& candidate)
				{
					return candidate != nullptr
						&& candidate->visible && candidate->activated
						&& candidate->rect.PointInRect(x, y);
				});
		};
		return candidateOwnsPoint(modalCandidates)
			|| candidateOwnsPoint(goodsCandidates);
	};
	int virtualX = -1;
	int virtualY = -1;
	for (int y = 40; y < 560 && virtualX < 0; y += 40)
	{
		for (int x = 40; x < 760; x += 40)
		{
			if (!menuOwnsPoint(x, y))
			{
				virtualX = x;
				virtualY = y;
				break;
			}
		}
	}
	ok = check(virtualX >= 0,
		"partner modal left no uncovered point for virtual-control testing")
		&& ok;
	if (virtualX >= 0)
	{
		panels.skillProbe->rect =
			{ virtualX - 16, virtualY - 16, 32, 32 };
		gameManager.player->nextAction = nullptr;
		dispatchPointerTap(
			gameManager, false, TOUCH_MOUSEID, virtualX, virtualY);
		dispatchPointerTap(
			gameManager, true, heldPointerID + 20, virtualX, virtualY);
		ok = check(panels.skillProbe->clickCount == 2
				&& gameManager.player->nextAction == nullptr,
			"visible virtual control rejected mouse or touch behind"
			" PartnerEquip, or leaked the same edge into the world") && ok;
	}

	const int clickCountBeforeStaleRelease =
		panels.skillProbe->clickCount;
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(AEvent(
		ET_FINGERUP,
		heldPointerID + 3,
		10,
		10,
		false));
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	ok = check(panels.skillProbe->clickCount
			== clickCountBeforeStaleRelease
		&& panels.skillProbe->dragEndCount == 0
		&& panels.skillProbe->touchingID == TOUCH_UNTOUCHEDID
		&& panels.skillProbe->touchingDownID == TOUCH_UNTOUCHEDID,
		"release captured by the partner modal committed a lower virtual control")
		&& ok;
	gameManager.menu->closePartnerEquipment(false);
	return ok;
}

bool testDeferredTouchControlsToggleTransaction(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager,
	MobilePanelFixture& panels)
{
	gameManager.controller->setTouchControlsVisible(true);
	gameManager.processGlobalInputFrame();
	bool ok = check(!Element::isRawPointerInputBlocked(),
		"deferred-toggle fixture started with raw pointer input blocked");
	ok = pressKeyboardToggleShortcut(
		inputManager, "late-update keyboard hide") && ok;

	const bool wasPaused = gameManager.isGameplayPaused();
	gameManager.setGameplayPaused(true);
	MobileExternalInputRuntimeTestAccess::updateGameManager(gameManager);
	gameManager.setGameplayPaused(wasPaused);
	ok = check(gameManager.controller->areTouchControlsVisible()
		&& !Element::isRawPointerInputBlocked()
		&& !inputManager.wasActionPressed(
			GameInput::InputAction::ToggleTouchControls),
		"late update applied visibility outside the pre-pointer transaction") && ok;

	auto probe = std::make_shared<RawPointerGateProbe>();
	{
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(probe);
		ok = queueExternalMouseTapAndWheel(
			"queued transition-frame external mouse input") && ok;
		MobileExternalInputRuntimeTestAccess::pumpEngineEvents();
		gameManager.processGlobalInputFrame();
		ok = check(!gameManager.controller->areTouchControlsVisible()
			&& !panels.joystickPanel->visible
			&& !panels.skillsPanel->visible
			&& Element::isRawPointerInputBlocked(),
			"deferred toggle did not hide controls through the global transaction") && ok;
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*probe);
	}
	ok = check(probe->clickCount == 0 && probe->mouseWheelCount == 0,
		"transition-frame mouse tap or wheel bypassed the raw pointer gate") && ok;

	gameManager.processGlobalInputFrame();
	ok = check(!Element::isRawPointerInputBlocked()
		&& !gameManager.controller->areTouchControlsVisible(),
		"deferred-toggle pointer gate did not drain while controls stayed hidden") && ok;
	ok = releaseKeyboardToggleShortcut(
		inputManager, "late-update keyboard hide") && ok;

	{
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(probe);
		ok = queueExternalMouseTapAndWheel(
			"post-drain external mouse input") && ok;
		MobileExternalInputRuntimeTestAccess::pumpEngineEvents();
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*probe);
	}
	ok = check(probe->clickCount == 1 && probe->mouseWheelCount == 1
		&& !gameManager.controller->areTouchControlsVisible()
		&& !panels.joystickPanel->visible
		&& !panels.skillsPanel->visible,
		"external mouse input did not remain usable after touch controls were hidden") && ok;

	gameManager.requestTouchControlsToggle();
	ok = check(!gameManager.controller->areTouchControlsVisible()
		&& !Element::isRawPointerInputBlocked(),
		"explicit restore request changed visibility before the global transaction") && ok;
	gameManager.processGlobalInputFrame();
	ok = check(gameManager.controller->areTouchControlsVisible()
		&& Element::isRawPointerInputBlocked(),
		"explicit restore request did not enter the global pointer transaction") && ok;
	gameManager.processGlobalInputFrame();
	ok = check(!Element::isRawPointerInputBlocked(),
		"explicit restore request left raw pointer input blocked") && ok;
	return ok;
}

void prepareWalkableMobileWorld(GameManager& gameManager)
{
	constexpr int mapWidth = 9;
	constexpr int mapHeight = 9;
	gameManager.global.data.canInput = true;
	gameManager.inEvent = false;
	gameManager.setGameplayPaused(false);
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = mapWidth;
	gameManager.map->data->head.height = mapHeight;
	gameManager.map->data->tile.assign(
		mapHeight, std::vector<MapTile>(mapWidth));
	gameManager.map->dataMap.tile.assign(
		mapHeight, std::vector<DataTile>(mapWidth));
	gameManager.player->setPosition({ 4, 4 });
	gameManager.player->direction = 6;
	gameManager.player->canRun = true;
	gameManager.player->thew = 100;
	gameManager.player->info.thewMax = 100;
	gameManager.player->nextAction = nullptr;
}

bool testVirtualAttackWithoutTargetQueuesFacingAttack(
	GameManager& gameManager,
	MobilePanelFixture& panels)
{
	prepareWalkableMobileWorld(gameManager);
	gameManager.npcManager->npcList.clear();
	gameManager.npcManager->clickIndex = -1;
	gameManager.player->direction = 2;
	const Point playerPosition = gameManager.player->getPosition();
	const Point expectedFacingPoint = Map::getSubPoint(
		playerPosition, gameManager.player->direction);

	MobileExternalInputRuntimeTestAccess::dispatchSkillButtonResult(
		*panels.skillsPanel,
		panels.skillsPanel->attackBtn,
		erClick);

	const std::shared_ptr<NextAction>& action = gameManager.player->nextAction;
	return check(
		action != nullptr
			&& action->action == acAttack
			&& action->destKind == ndNone
			&& action->destGE.expired()
			&& action->dest.x == expectedFacingPoint.x
			&& action->dest.y == expectedFacingPoint.y
			&& gameManager.player->getPosition().x == playerPosition.x
			&& gameManager.player->getPosition().y == playerPosition.y,
		"virtual attack without a hostile target queued forward movement"
		" instead of a facing attack");
}

bool testProductionMobileControlCreationAndDispatch()
{
	MobileExternalInputRuntimeTestAccess::configureHeadlessTouchViewport(
		1024, 768);
	MobileExternalInputRuntimeTestAccess::drainEngineEvents();

	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});

	ResourceManager& resourceManager = ResourceManager::instance();
	if (!check(
		resourceManager.initialize(assetsRoot.generic_string()),
		"production mobile-controls fixture did not initialize the real"
		" resource collection")
		|| !check(
			resourceManager.setActiveResourcePackById("JXQY2"),
			"production mobile-controls fixture did not select an enabled"
			" resource pack"))
	{
		return false;
	}

	const ResourceManifest& manifest = resourceManager.getActiveManifest();
	bool ok = check(
		manifest.id == "JXQY2",
		"production mobile-controls fixture selected an unexpected resource"
		" manifest");

	GameManager gameManager;
	gameManager.global.useWav = manifest.useWav;
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.global.loadUiSettings();
	gameManager.goodsManager.configureLayout();
	gameManager.magicManager.configureLayout();
	gameManager.global.data.canInput = true;
	gameManager.menu->init();
	gameManager.menu->clearMenu();
	gameManager.controller->init();
	prepareWalkableMobileWorld(gameManager);

	const std::shared_ptr<SkillsPanel> skillsPanel =
		gameManager.controller->skillPanel;
	const std::shared_ptr<JoystickPanel> joystickPanel =
		gameManager.controller->joystickPanel;
	const std::shared_ptr<TextButton> fastButton =
		skillsPanel != nullptr ? skillsPanel->fastBtn[0] : nullptr;
	const std::shared_ptr<Joystick> joystick =
		joystickPanel != nullptr ? joystickPanel->joystick : nullptr;
	ok = check(
		skillsPanel != nullptr
			&& joystickPanel != nullptr
			&& fastButton != nullptr
			&& joystick != nullptr,
		"GameController::init did not create the production mobile panel"
		" hierarchy from menu definitions")
		&& ok;
	if (fastButton == nullptr || joystick == nullptr)
	{
		return false;
	}
	ok = check(
		fastButton->rect.w > 0
			&& fastButton->rect.h > 0
			&& joystick->rect.w > 0
			&& joystick->rect.h > 0,
		"production mobile menu components did not retain usable INI layout"
		" rectangles")
		&& ok;

	auto originalTarget = std::make_shared<Object>();
	originalTarget->position = { 5, 4 };
	originalTarget->scriptFile = "production-original.lua";
	auto reboundTarget = std::make_shared<Object>();
	reboundTarget->position = { 6, 4 };
	reboundTarget->scriptFile = "production-rebound.lua";
	gameManager.objectManager->objectList =
		{ originalTarget, reboundTarget };

	NextAction originalBinding;
	originalBinding.destGE = originalTarget;
	originalBinding.destKind = ndObj;
	NextAction reboundBinding;
	reboundBinding.destGE = reboundTarget;
	reboundBinding.destKind = ndObj;
	gameManager.fastSelectingList = { originalBinding };
	fastButton->visible = true;
	const int fastButtonX = fastButton->rect.x + fastButton->rect.w / 2;
	const int fastButtonY = fastButton->rect.y + fastButton->rect.h / 2;
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
	Element* fastButtonHit =
		gameManager.findPointerHitTargetInTree(fastButtonX, fastButtonY);
	ok = check(
		fastButtonHit == fastButton.get(),
		std::string(
			"production pointer traversal resolved the fast-interaction"
			" coordinates to ")
			+ (fastButtonHit != nullptr
				? fastButtonHit->name
				: std::string("<none>")))
		&& ok;
	dispatchPointerEdge(
		gameManager,
		false,
		true,
		TOUCH_MOUSEID,
		fastButtonX,
		fastButtonY);
	ok = check(
		MobileExternalInputRuntimeTestAccess::hasPointerState(
			fastButton, TOUCH_MOUSEID)
			&& MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, TOUCH_MOUSEID),
		"Engine queue and Element traversal did not acquire the real fast"
		" interaction button")
		&& ok;

	gameManager.objectManager->objectList = { reboundTarget };
	gameManager.fastSelectingList[0] = reboundBinding;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	dispatchPointerEdge(
		gameManager,
		false,
		false,
		TOUCH_MOUSEID,
		fastButtonX,
		fastButtonY);
	ok = check(
		gameManager.player->nextAction == nullptr
			&& !MobileExternalInputRuntimeTestAccess::hasPointerState(
				fastButton, TOUCH_MOUSEID)
			&& !MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, TOUCH_MOUSEID),
		"real fast-interaction release submitted its rebound/default target"
		" after the press-time target left the manager")
		&& ok;

	originalTarget->scriptFile = "production-original.lua";
	originalTarget->scriptFileRight.clear();
	gameManager.objectManager->objectList = { originalTarget };
	gameManager.fastSelectingList = { originalBinding };
	dispatchPointerEdge(
		gameManager,
		false,
		true,
		TOUCH_MOUSEID,
		fastButtonX,
		fastButtonY);
	originalTarget->scriptFile.clear();
	originalTarget->scriptFileRight = "production-original-right.lua";
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	dispatchPointerEdge(
		gameManager,
		false,
		false,
		TOUCH_MOUSEID,
		fastButtonX,
		fastButtonY);
	ok = check(
		gameManager.player->nextAction == nullptr,
		"real fast-interaction release committed a stale script side after"
		" the press-time target changed its primary fallback semantics")
		&& ok;

	constexpr EventTouchID joystickPointerID = 95101;
	const int joystickX = joystick->rect.x + joystick->rect.w / 4;
	const int joystickY = joystick->rect.y + joystick->rect.h / 2;
	const int joystickCenterX = joystick->rect.x + joystick->rect.w / 2;
	const int joystickCenterY = joystick->rect.y + joystick->rect.h / 2;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(AEvent(
		ET_MOUSEMOTION,
		TOUCH_MOUSEID,
		joystickX,
		joystickY,
		false,
		true));
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	ok = check(
		joystick->touchingDownID == TOUCH_UNTOUCHEDID
			&& !joystick->isWalking()
			&& !joystick->isRunning()
			&& gameManager.player->nextAction == nullptr,
		"synthetic mouse refresh without a press activated the production"
		" virtual joystick or queued a NextAction")
		&& ok;

	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(AEvent(
		ET_FINGERDOWN,
		joystickPointerID,
		joystickCenterX,
		joystickCenterY,
		false));
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(AEvent(
		ET_MOUSEMOTION,
		TOUCH_MOUSEID,
		joystickCenterX,
		joystickCenterY,
		false,
		true));
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(AEvent(
		ET_FINGERMOTION,
		joystickPointerID,
		joystickX,
		joystickY,
		false));
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	ok = check(
		MobileExternalInputRuntimeTestAccess::hasPointerState(
			joystick, joystickPointerID)
			&& joystick->touchingDownID == joystickPointerID
			&& MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, joystickPointerID)
			&& (joystick->isWalking() || joystick->isRunning())
			&& gameManager.player->nextAction != nullptr,
		"Engine synthetic mouse refresh stole the production virtual"
		" joystick finger owner or blocked its down-plus-motion action")
		&& ok;

	gameManager.scriptAPI.disableInput();
	ok = check(
		!gameManager.global.data.canInput
			&& !joystick->isWalking()
			&& !joystick->isRunning()
			&& !MobileExternalInputRuntimeTestAccess::hasPointerState(
				joystick, joystickPointerID)
			&& !MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, joystickPointerID),
		"DisableInput retained the real virtual joystick contact or"
		" controller transaction")
		&& ok;
	dispatchPointerEdge(
		gameManager,
		true,
		false,
		joystickPointerID,
		joystickX,
		joystickY);
	gameManager.scriptAPI.enableInput();
	gameManager.fastSelectingList.clear();
	gameManager.objectManager->objectList.clear();
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	return checkStrictlyHeadless(
		"after production mobile-control creation and event distribution")
		&& ok;
}

bool testFastInteractionPressBindingAndDisableInput(
	GameManager& gameManager,
	MobilePanelFixture& panels)
{
	prepareWalkableMobileWorld(gameManager);
	auto originalTarget = std::make_shared<Object>();
	originalTarget->position = { 5, 4 };
	originalTarget->scriptFile = "original.lua";
	auto reboundTarget = std::make_shared<Object>();
	reboundTarget->position = { 6, 4 };
	reboundTarget->scriptFile = "rebound.lua";
	gameManager.objectManager->objectList =
		{ originalTarget, reboundTarget };

	NextAction originalBinding;
	originalBinding.destGE = originalTarget;
	originalBinding.destKind = ndObj;
	originalBinding.useRightScript = false;
	gameManager.fastSelectingList = { originalBinding };
	MobileExternalInputRuntimeTestAccess::dispatchSkillButtonResult(
		*panels.skillsPanel,
		panels.skillsPanel->fastBtn[0],
		erMouseLDown);

	NextAction reboundBinding;
	reboundBinding.destGE = reboundTarget;
	reboundBinding.destKind = ndObj;
	reboundBinding.useRightScript = false;
	gameManager.fastSelectingList[0] = reboundBinding;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	MobileExternalInputRuntimeTestAccess::dispatchSkillButtonResult(
		*panels.skillsPanel,
		panels.skillsPanel->fastBtn[0],
		erClick);
	bool ok = check(
		gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->destGE.lock()
				== originalTarget,
		"a held fast-interaction button executed the target rebound after"
		" pointer-down instead of its press-time target");

	gameManager.fastSelectingList = { originalBinding };
	MobileExternalInputRuntimeTestAccess::dispatchSkillButtonResult(
		*panels.skillsPanel,
		panels.skillsPanel->fastBtn[0],
		erMouseLDown);
	gameManager.objectManager->objectList = { reboundTarget };
	gameManager.fastSelectingList[0] = reboundBinding;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	MobileExternalInputRuntimeTestAccess::dispatchSkillButtonResult(
		*panels.skillsPanel,
		panels.skillsPanel->fastBtn[0],
		erClick);
	ok = check(
		gameManager.player->nextAction == nullptr,
		"a held fast-interaction button submitted the rebound target or a"
		" default movement after its press-time target left the manager")
		&& ok;

	constexpr EventTouchID joystickPointerID = 95001;
	panels.joystick->activate();
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.joystick, joystickPointerID, erMouseLDown, false);
	MobileExternalInputRuntimeTestAccess::synchronizeControllerLifecycle(
		*gameManager.controller,
		MobileExternalInputRuntimeTestAccess::inputManager()
			.inputLifecycleRevision());
	AEvent pointerDown(
		ET_FINGERDOWN, joystickPointerID, 80, 50, false);
	MobileExternalInputRuntimeTestAccess::previewControllerPointerEvent(
		*gameManager.controller, pointerDown);
	ok = check(
		MobileExternalInputRuntimeTestAccess::
			ownsVirtualControlPointerTransaction(
				*gameManager.controller, joystickPointerID)
			&& (panels.joystick->isWalking()
				|| panels.joystick->isRunning()),
		"DisableInput fixture did not establish a virtual joystick contact")
		&& ok;
	gameManager.scriptAPI.disableInput();
	ok = check(
		!gameManager.global.data.canInput
			&& !panels.joystick->isWalking()
			&& !panels.joystick->isRunning()
			&& !MobileExternalInputRuntimeTestAccess::hasPointerState(
				panels.joystick, joystickPointerID)
			&& !MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, joystickPointerID),
		"DisableInput retained the virtual joystick contact or transaction")
		&& ok;
	gameManager.scriptAPI.enableInput();
	gameManager.fastSelectingList.clear();
	gameManager.objectManager->objectList.clear();
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	return ok;
}

bool testFingerCancellationDoesNotCommit()
{
	auto root = std::make_shared<Element>();
	auto probe = std::make_shared<PointerCommitProbe>();
	auto otherProbe = std::make_shared<PointerCommitProbe>();
	auto joystick = std::make_shared<TestJoystick>();
	auto resourcePackList = std::make_shared<ResourcePackList>();
	auto titleTeam = std::make_shared<TitleTeam>("", "");
	probe->rect = { 10000, 10000, 100, 100 };
	otherProbe->rect = { 10200, 10000, 100, 100 };
	root->addChild(probe);
	root->addChild(otherProbe);
	root->addChild(joystick);
	root->addChild(resourcePackList);
	root->addChild(titleTeam);
	constexpr EventTouchID pointerID =
		static_cast<EventTouchID>(std::numeric_limits<std::int32_t>::max())
		+ 0x654321LL;
	constexpr EventTouchID otherPointerID = pointerID + 1;
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		probe, pointerID, PointerResultBits | BusinessResultBits, true);
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		otherProbe, otherPointerID, erClick | erReturnToTitle, false);
	joystick->activate();
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		joystick, pointerID, erMouseLDown | erLifeExhaust, false);
	MobileExternalInputRuntimeTestAccess::seedResourcePackListPointer(
		*resourcePackList, pointerID);
	resourcePackList->result = erDragging | erActionEnd;
	MobileExternalInputRuntimeTestAccess::seedTitleTeamPointer(
		*titleTeam, pointerID);
	titleTeam->result = erClick | erExit;
	root->touchingID = otherPointerID;
	root->touchingDownID = otherPointerID;
	root->result = erMouseRDown | erReturnToTitle;

	SDL_Event event{};
	event.type = SDL_EVENT_FINGER_CANCELED;
	event.tfinger.fingerID = static_cast<SDL_FingerID>(pointerID);
	if (!check(SDL_PushEvent(&event),
		"SDL rejected the synthetic finger-canceled event"))
	{
		return false;
	}
	MobileExternalInputRuntimeTestAccess::pumpEngineEvents();
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*root);

	bool ok = true;
	ok = check(probe->touchingID == TOUCH_UNTOUCHEDID
		&& probe->touchingDownID == TOUCH_UNTOUCHEDID,
		"finger cancellation left pointer ownership active") && ok;
	ok = check((probe->result & PointerResultBits) == 0
		&& (probe->result & BusinessResultBits) == BusinessResultBits,
		"finger cancellation did not isolate pointer and business results") && ok;
	ok = check(MobileExternalInputRuntimeTestAccess::hasPointerState(
		otherProbe, otherPointerID)
		&& otherProbe->result == (erClick | erReturnToTitle),
		"finger cancellation modified a different active contact") && ok;
	ok = check(joystick->touchingID == TOUCH_UNTOUCHEDID
		&& joystick->touchingDownID == TOUCH_UNTOUCHEDID
		&& !joystick->isWalking() && !joystick->isRunning()
		&& joystick->result == erLifeExhaust,
		"finger cancellation left Joystick-owned state or lost its business result") && ok;
	ok = check(MobileExternalInputRuntimeTestAccess::resourcePackListPointerCleared(
		*resourcePackList)
		&& resourcePackList->result == erActionEnd,
		"finger cancellation left ResourcePackList-owned state") && ok;
	ok = check(MobileExternalInputRuntimeTestAccess::titleTeamPointerCleared(
		*titleTeam)
		&& titleTeam->result == erExit,
		"finger cancellation left TitleTeam-owned state") && ok;
	ok = check(root->touchingID == otherPointerID
		&& root->touchingDownID == otherPointerID
		&& root->result == (erMouseRDown | erReturnToTitle),
		"directed cancellation modified an unrelated root contact/result") && ok;
	ok = check(!MobileExternalInputRuntimeTestAccess::hasActiveDrag(),
		"finger cancellation left the global drag transaction active") && ok;
	ok = check(probe->clickCount == 0 && probe->dragEndCount == 0,
		"finger cancellation committed click or drag-end callbacks") && ok;
	return ok;
}

bool testSkillDragReleaseDoesNotAlsoClick()
{
	auto root = std::make_shared<ChildResultCapture>();
	root->canCallBack = true;
	auto skillsPanel = std::make_shared<SkillsPanel>();
	skillsPanel->removeAllChild();
	auto skillButton = std::make_shared<DragRoundButton>();
	skillButton->rect = { 100, 100, 100, 100 };
	skillButton->setRange(100);
	skillsPanel->skillBtn[0] = skillButton;
	skillsPanel->addChild(skillButton);
	skillsPanel->resetInput();
	root->addChild(skillsPanel);

	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 150, 150, false));
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 165, 150, false));
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 165, 150, false));
	MobileExternalInputRuntimeTestAccess::arrangeElementTree(*root);
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*root);

	const int clickResultCount = static_cast<int>(std::count_if(
		root->childResults.begin(),
		root->childResults.end(),
		[](unsigned int result) { return (result & erClick) != 0; }));
	const int dragEndResultCount = static_cast<int>(std::count_if(
		root->childResults.begin(),
		root->childResults.end(),
		[](unsigned int result) { return (result & erDragEnd) != 0; }));
	bool ok = check(clickResultCount == 0,
		"a skill drag released inside its button also emitted erClick");
	ok = check(dragEndResultCount == 1
		&& skillsPanel->getClickIndex() == SKILL_PANEL_SKILL1,
		"a skill drag did not emit exactly one directional erDragEnd") && ok;
	ok = check(!MobileExternalInputRuntimeTestAccess::hasActiveDrag(),
		"skill drag release left the global drag transaction active") && ok;
	return ok;
}

bool testStoryVisibilityCancelsVirtualPointerTransactions(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager,
	MobilePanelFixture& panels)
{
	prepareWalkableMobileWorld(gameManager);
	gameManager.controller->setTouchControlsVisible(true);
	gameManager.controller->onUpdate();
	gameManager.processGlobalInputFrame();
	MobileExternalInputRuntimeTestAccess::synchronizeControllerLifecycle(
		*gameManager.controller, inputManager.inputLifecycleRevision());
	panels.skillProbe->rect = { 60, 60, 40, 40 };

	auto runCase = [&](
		bool finger,
		EventTouchID pointerID,
		EventType terminalEventType,
		const std::string& context)
	{
		gameManager.inEvent = false;
		gameManager.controller->onUpdate();
		gameManager.controller->setTouchControlsVisible(true);
		const int clickCountBefore = panels.skillProbe->clickCount;
		const int dragEndCountBefore = panels.skillProbe->dragEndCount;
		AEvent pointerDownEvent(
			finger ? ET_FINGERDOWN : ET_MOUSEDOWN,
			finger ? pointerID : MBC_MOUSE_LEFT,
			80,
			80,
			false);
		MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
		const bool pointerAcquired =
			MobileExternalInputRuntimeTestAccess::acquirePointer(
				*panels.skillsPanel, pointerID, 80, 80);
		MobileExternalInputRuntimeTestAccess::previewControllerPointerEvent(
			*gameManager.controller, pointerDownEvent);
		bool caseOK = check(
			pointerAcquired
			&& MobileExternalInputRuntimeTestAccess::hasPointerState(
				panels.skillProbe, pointerID),
			context + ": virtual control did not acquire pointer state");
		caseOK = check(
			MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, pointerID),
			context + ": controller did not record virtual pointer ownership")
			&& caseOK;

		gameManager.inEvent = true;
		gameManager.controller->onUpdate();
		caseOK = check(!gameManager.controller->visible
			&& !MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, pointerID)
			&& !MobileExternalInputRuntimeTestAccess::hasPointerState(
				panels.skillProbe, pointerID),
			context + ": entering a story event left virtual pointer ownership")
			&& caseOK;
		if (!finger)
		{
			caseOK = check(
				MobileExternalInputRuntimeTestAccess::
					suppressesMouseWorldInputUntilRelease(
						*gameManager.controller),
				context + ": hidden virtual mouse press was not isolated"
				" from world input") && caseOK;
		}

		MobileExternalInputRuntimeTestAccess::pushEngineEvent(AEvent(
			terminalEventType,
			finger ? pointerID : MBC_MOUSE_LEFT,
			80,
			80,
			false));
		MobileExternalInputRuntimeTestAccess::arrangeElementTree(gameManager);
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
		gameManager.inEvent = false;
		gameManager.controller->onUpdate();
		gameManager.controller->onEvent();
		caseOK = check(gameManager.controller->visible
			&& !MobileExternalInputRuntimeTestAccess::
				ownsVirtualControlPointerTransaction(
					*gameManager.controller, pointerID)
			&& !MobileExternalInputRuntimeTestAccess::hasPointerState(
				panels.skillProbe, pointerID)
			&& panels.skillProbe->clickCount == clickCountBefore
			&& panels.skillProbe->dragEndCount == dragEndCountBefore,
			context + ": hidden terminal event committed or retained a"
			" virtual-control transaction") && caseOK;
		if (!finger)
		{
			caseOK = check(
				!MobileExternalInputRuntimeTestAccess::
					suppressesMouseWorldInputUntilRelease(
						*gameManager.controller),
				context + ": released hidden mouse transaction still"
				" suppressed world input") && caseOK;
		}
		return caseOK;
	};

	bool ok = runCase(
		false, TOUCH_MOUSEID, ET_MOUSEUP, "hidden mouse up");
	ok = runCase(true, 43001, ET_FINGERUP, "hidden finger up") && ok;
	ok = runCase(true, 43002, ET_FINGERCANCEL, "hidden finger cancel") && ok;
	return ok;
}

bool testControllerLifecycleReset(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager,
	MobilePanelFixture& panels)
{
	prepareWalkableMobileWorld(gameManager);
	inputManager.setWindowFocused(true);
	MobileExternalInputRuntimeTestAccess::synchronizeControllerLifecycle(
		*gameManager.controller, inputManager.inputLifecycleRevision());
	panels.joystick->activate();
	bool ok = check(panels.joystick->isWalking()
		|| panels.joystick->isRunning(),
		"stale-joystick fixture did not represent a movement input");
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.joystick, 81, erMouseLDown, false);
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		panels.skillProbe, 82, erClick | erDragEnd, true);
	MobileExternalInputRuntimeTestAccess::seedSkillsTransientState(
		*panels.skillsPanel);
	gameManager.fastSelectingList.push_back(NextAction{});
	gameManager.touchingID = 91;
	gameManager.touchingDownID = 91;
	gameManager.result = erMouseLDown | erExit;
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		std::static_pointer_cast<Element>(gameManager.menu),
		92, erClick | erReturnToTitle, false);
	MobileExternalInputRuntimeTestAccess::seedPointerState(
		std::static_pointer_cast<Element>(gameManager.controller),
		93, erDragEnd | erActionEnd, false);

	inputManager.setWindowFocused(false);
	// Mirror the production frame order: global lifecycle processing precedes
	// pointer/onEvent dispatch, followed by physical gameplay dispatch.
	gameManager.processGlobalInputFrame();
	const bool sceneRootPointerCleared =
		gameManager.touchingID == TOUCH_UNTOUCHEDID
		&& gameManager.touchingDownID == TOUCH_UNTOUCHEDID
		&& gameManager.result == erExit;
	const bool menuPointerCleared =
		gameManager.menu->touchingID == TOUCH_UNTOUCHEDID
		&& gameManager.menu->touchingDownID == TOUCH_UNTOUCHEDID
		&& gameManager.menu->result == erReturnToTitle;
	const bool controllerPointerCleared =
		gameManager.controller->touchingID == TOUCH_UNTOUCHEDID
		&& gameManager.controller->touchingDownID == TOUCH_UNTOUCHEDID
		&& gameManager.controller->result == erActionEnd;
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	gameManager.controller->processPhysicalInputFrame();

	ok = check(!panels.joystick->isWalking()
		&& !panels.joystick->isRunning(),
		"input lifecycle change left joystick movement active") && ok;
	ok = check(panels.joystick->touchingID == TOUCH_UNTOUCHEDID
		&& panels.joystick->touchingDownID == TOUCH_UNTOUCHEDID,
		"input lifecycle change left joystick pointer ownership active") && ok;
	ok = check(panels.skillProbe->touchingID == TOUCH_UNTOUCHEDID
		&& panels.skillProbe->touchingDownID == TOUCH_UNTOUCHEDID
		&& panels.skillProbe->result == erNone,
		"input lifecycle change left skill pointer state active") && ok;
	ok = check(MobileExternalInputRuntimeTestAccess::skillsTransientStateCleared(
		*panels.skillsPanel),
		"input lifecycle change left skill drag state active") && ok;
	ok = check(gameManager.fastSelectingList.empty(),
		"input lifecycle change left fast-select state active") && ok;
	ok = check(gameManager.player->nextAction == nullptr,
		"real frame order queued a stale virtual-joystick NextAction") && ok;
	ok = check(sceneRootPointerCleared,
		"lifecycle reset did not preserve the scene root business result") && ok;
	ok = check(menuPointerCleared,
		"lifecycle reset did not clear menu pointer state selectively") && ok;
	ok = check(controllerPointerCleared,
		"lifecycle reset did not clear controller pointer state selectively") && ok;
	ok = check(!MobileExternalInputRuntimeTestAccess::hasActiveDrag()
		&& panels.skillProbe->clickCount == 0
		&& panels.skillProbe->dragEndCount == 0,
		"lifecycle cancellation committed a pointer action") && ok;

	inputManager.setWindowFocused(true);
	gameManager.processGlobalInputFrame();
	gameManager.controller->processPhysicalInputFrame();
	return ok;
}

bool testRecoveryGestureLifecycleReset(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager)
{
	const std::vector<GameInput::TouchRecoveryContact> contacts =
	{
		{ 1, 10, 10 },
		{ 2, 20, 20 },
		{ 3, 30, 30 },
	};
	gameManager.controller->setTouchControlsVisible(false);
	gameManager.processGlobalInputFrame();
	bool ok = check(!MobileExternalInputRuntimeTestAccess::updateRecoveryGesture(
		gameManager, contacts, 100),
		"three-finger recovery triggered on its initial frame");

	inputManager.setWindowFocused(false);
	gameManager.processGlobalInputFrame();
	ok = check(!MobileExternalInputRuntimeTestAccess::updateRecoveryGesture(
		gameManager, contacts, 1100),
		"inactive input context retained a mature three-finger hold") && ok;
	inputManager.setWindowFocused(true);
	gameManager.processGlobalInputFrame();

	ok = check(!MobileExternalInputRuntimeTestAccess::updateRecoveryGesture(
		gameManager, contacts, 2000),
		"three-finger recovery did not start a fresh active-context hold") && ok;
	inputManager.suspendInput();
	inputManager.resumeInput();
	gameManager.processGlobalInputFrame();
	ok = check(!MobileExternalInputRuntimeTestAccess::updateRecoveryGesture(
		gameManager, contacts, 3000),
		"lifecycle revision change retained a mature three-finger hold") && ok;
	return ok;
}

bool testRecoveryGestureProductionFlow(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager,
	MobilePanelFixture& panels)
{
	auto systemNotice = std::make_shared<SystemNotice>();
	gameManager.menu->systemNotice = systemNotice;
	gameManager.global.data.canInput = true;
	gameManager.inEvent = false;
	gameManager.controller->setTouchControlsVisible(false);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 4990);

	const std::vector<GameInput::TouchRecoveryContact> contacts =
	{
		{ 11, 10, 10 },
		{ 22, 20, 20 },
		{ 33, 30, 30 },
	};
	bool ok = true;
	auto nestedProbe = std::make_shared<RawPointerGateProbe>();
	{
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(nestedProbe);
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERMOTION, 22, 50, 50));
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERDOWN, 22, 50, 50));
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*nestedProbe);
		ok = check(nestedProbe->touchingDownID == 22,
			"nested run owner did not establish its pre-gate pointer state") && ok;

		MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
			gameManager, false, contacts, 5000);
		ok = check(Element::isRawPointerInputBlocked(),
			"three-finger candidate did not block raw pointer dispatch") && ok;
		ok = check(nestedProbe->touchingID == TOUCH_UNTOUCHEDID
			&& nestedProbe->touchingDownID == TOUCH_UNTOUCHEDID,
			"global recovery gate did not cancel a pre-existing nested pointer") && ok;
		ok = check(!gameManager.controller->areTouchControlsVisible(),
			"three-finger candidate restored controls before the hold matured") && ok;

		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERMOTION, 22, 50, 50));
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERUP, 22, 50, 50));
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_KEYDOWN, KEY_I, 0, 0));
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*nestedProbe);
	}
	ok = check(nestedProbe->touchingID == TOUCH_UNTOUCHEDID
		&& nestedProbe->touchingDownID == TOUCH_UNTOUCHEDID
		&& nestedProbe->clickCount == 0,
		"global recovery gate leaked pointer input into a nested run owner") && ok;
	ok = check(nestedProbe->keyDownCount == 1
		&& nestedProbe->eventCount == 2,
		"raw pointer gate blocked keyboard or per-frame onEvent processing") && ok;

	panels.joystick->configureTouchArea();
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_FINGERMOTION, 11, 80, 50));
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_FINGERDOWN, 11, 80, 50));
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	ok = check(!panels.joystick->isWalking()
		&& !panels.joystick->isRunning()
		&& gameManager.player->nextAction == nullptr,
		"recovery candidate leaked a queued touch event into world input") && ok;

	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, contacts, 6000);
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"mature three-finger hold did not restore touch controls") && ok;
	ok = check(Element::isRawPointerInputBlocked(),
		"recovery trigger unlocked pointer dispatch before all fingers released") && ok;
	ok = check(systemNotice->currentMessage
		== "系统：已通过三指长按恢复触控操作区",
		"three-finger recovery notice was incorrect") && ok;

	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_FINGERMOTION, 22, 75, 50));
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	ok = check(!panels.joystick->isWalking()
		&& !panels.joystick->isRunning()
		&& gameManager.player->nextAction == nullptr,
		"recovery trigger frame leaked into the restored touch controls") && ok;

	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 6010);
	ok = check(Element::isRawPointerInputBlocked(),
		"recovery release frame did not keep its queued pointer events isolated") && ok;
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"release after recovery hid the restored touch controls") && ok;
	ok = check(!panels.joystick->isWalking()
		&& !panels.joystick->isRunning(),
		"recovery release left virtual joystick input active") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 6020);
	ok = check(!Element::isRawPointerInputBlocked(),
		"recovery did not unlock after the empty queue-drain frame") && ok;

	inputManager.setWindowFocused(false);
	gameManager.processGlobalInputFrame();
	inputManager.setWindowFocused(true);
	gameManager.processGlobalInputFrame();
	ok = check(!Element::isRawPointerInputBlocked(),
		"recovery lifecycle reset left raw input blocked") && ok;
	gameManager.menu->systemNotice = nullptr;
	return ok;
}

bool testRecoveryGestureEdgeTransitions(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager)
{
	const std::vector<GameInput::TouchRecoveryContact> threeContacts =
	{
		{ 101, 10, 10 },
		{ 202, 20, 20 },
		{ 303, 30, 30 },
	};
	const std::vector<GameInput::TouchRecoveryContact> twoContacts =
	{
		threeContacts[0],
		threeContacts[1],
	};
	bool ok = true;

	gameManager.controller->setTouchControlsVisible(false);
	gameManager.needEvents = false;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, threeContacts, 7000);
	ok = check(Element::isRawPointerInputBlocked()
		&& !gameManager.needEvents,
		"three-finger gate reused or overwrote the general event flag") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, twoContacts, 7100);
	ok = check(Element::isRawPointerInputBlocked(),
		"3-to-2 contact change unlocked pointer input before all release") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 7110);
	ok = check(Element::isRawPointerInputBlocked()
		&& !gameManager.needEvents,
		"all-finger release frame did not preserve its event gate") && ok;
	MobileExternalInputRuntimeTestAccess::pushEngineEvent(
		AEvent(ET_FINGERUP, 303, 30, 30));
	MobileExternalInputRuntimeTestAccess::dispatchElementEvents(gameManager);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, { threeContacts.front() }, 7120);
	ok = check(!Element::isRawPointerInputBlocked()
		&& !gameManager.needEvents,
		"a fresh touch after queue drain was retained by the old gate") && ok;
	gameManager.needEvents = true;

	gameManager.controller->setTouchControlsVisible(false);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, threeContacts, 8000);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, true, threeContacts, 8010);
	ok = check(gameManager.controller->areTouchControlsVisible()
		&& Element::isRawPointerInputBlocked(),
		"external show did not retain the active contact gate") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, true, threeContacts, 10050);
	ok = check(!gameManager.controller->areTouchControlsVisible()
		&& Element::isRawPointerInputBlocked(),
		"external show-to-hide released contacts or reused the old hold timer") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, threeContacts, 12050);
	ok = check(!gameManager.controller->areTouchControlsVisible(),
		"old three-finger timer retriggered after an external visibility cycle") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 12060);
	ok = check(Element::isRawPointerInputBlocked(),
		"external visibility release frame dropped its queue gate") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 12070);
	ok = check(!Element::isRawPointerInputBlocked(),
		"external visibility cycle did not unlock after queue drain") && ok;

	gameManager.controller->setTouchControlsVisible(false);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, true, { threeContacts.front() }, 13000);
	ok = check(gameManager.controller->areTouchControlsVisible()
		&& Element::isRawPointerInputBlocked(),
		"hidden-to-visible transition inherited an already-held pointer") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 13010);
	ok = check(Element::isRawPointerInputBlocked(),
		"single held pointer release frame dropped its queue gate") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 13020);
	ok = check(!Element::isRawPointerInputBlocked(),
		"single held pointer transition did not unlock after queue drain") && ok;

	gameManager.controller->setTouchControlsVisible(false);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, threeContacts, 14000);
	inputManager.setWindowFocused(false);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, threeContacts, 16000);
	ok = check(Element::isRawPointerInputBlocked()
		&& !gameManager.controller->areTouchControlsVisible(),
		"inactive lifecycle unlocked or triggered a held recovery gesture") && ok;
	inputManager.setWindowFocused(true);
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, threeContacts, 18000);
	ok = check(Element::isRawPointerInputBlocked()
		&& !gameManager.controller->areTouchControlsVisible(),
		"focus recovery replayed a pre-lifecycle three-finger hold") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 18010);
	ok = check(Element::isRawPointerInputBlocked(),
		"lifecycle-reset release frame dropped its queue gate") && ok;
	auto queuedTapProbe = std::make_shared<RawPointerGateProbe>();
	{
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(queuedTapProbe);
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERUP, 303, 30, 30));
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*queuedTapProbe);
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERMOTION, 404, 50, 50));
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERDOWN, 404, 50, 50));
		MobileExternalInputRuntimeTestAccess::pushEngineEvent(
			AEvent(ET_FINGERUP, 404, 50, 50));
		MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
			gameManager, true, {}, 18020);
		MobileExternalInputRuntimeTestAccess::dispatchElementEvents(*queuedTapProbe);
	}
	ok = check(gameManager.controller->areTouchControlsVisible()
		&& Element::isRawPointerInputBlocked()
		&& queuedTapProbe->clickCount == 0,
		"post-drain visibility transition leaked a queued same-frame tap") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, false, {}, 18030);
	ok = check(!Element::isRawPointerInputBlocked(),
		"post-drain visibility gate did not release on the next frame") && ok;
	return ok;
}

bool testActiveGamepadRemovalPolicy(
	GameManager& gameManager,
	GameInput::PhysicalInputManager& inputManager)
{
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY Mobile External Input Pad");
	std::uint64_t nowMilliseconds = 4000;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	gameManager.processGlobalInputFrame();
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	bool ok = check(inputManager.activeGamepadID() == gamepad.id(),
		"virtual gamepad did not claim the production input manager");

	gameManager.controller->setTouchControlsVisible(false);
	const std::uint64_t removalRevisionBeforeResume =
		inputManager.activeGamepadRemovalRevision();
	inputManager.suspendInput();
	inputManager.resumeInput();
	gameManager.processGlobalInputFrame();
	ok = check(inputManager.activeGamepadRemovalRevision()
		== removalRevisionBeforeResume,
		"resume or re-enumeration reported a real active-gamepad removal") && ok;
	ok = check(!gameManager.controller->areTouchControlsVisible(),
		"resume or re-enumeration falsely restored hidden touch controls") && ok;

	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadID() == gamepad.id(),
		"re-enumerated virtual gamepad did not reclaim input") && ok;

	gameManager.controller->setTouchControlsVisible(true);
	gamepad.detach();
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadRemovalRevision()
		== removalRevisionBeforeResume + 1,
		"real active-gamepad removal did not advance its narrow revision") && ok;
	MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
		gameManager, true, {}, nowMilliseconds);
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"active-gamepad removal did not override a same-frame hide request") && ok;
	ok = check(MobileExternalInputRuntimeTestAccess::pendingExternalInputMessage(
		gameManager) == "手柄已断开，已恢复触控操作区",
		"real active-gamepad removal did not produce the recovery notice") && ok;

	VirtualGamepadTest::VirtualGamepad resumedUnclaimedGamepad(
		"JXQY Resume Unclaimed Removal Pad");
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	gameManager.processGlobalInputFrame();
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	resumedUnclaimedGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	resumedUnclaimedGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadID() == resumedUnclaimedGamepad.id(),
		"resume-unclaimed gamepad did not claim production input") && ok;
	gameManager.controller->setTouchControlsVisible(false);
	gameManager.processGlobalInputFrame();
	const std::uint64_t removalRevisionBeforeUnclaimedDetach =
		inputManager.activeGamepadRemovalRevision();
	inputManager.suspendInput();
	inputManager.resumeInput();
	gameManager.processGlobalInputFrame();
	ok = check(inputManager.activeGamepadID() == 0
		&& inputManager.activeGamepadRemovalRevision()
			== removalRevisionBeforeUnclaimedDetach
		&& !gameManager.controller->areTouchControlsVisible(),
		"resume changed removal state before fresh gamepad input") && ok;
	resumedUnclaimedGamepad.detach();
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadRemovalRevision()
		== removalRevisionBeforeUnclaimedDetach + 1,
		"gamepad detached before post-resume reclaim did not advance removal revision") && ok;
	gameManager.processGlobalInputFrame();
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"gamepad detached before post-resume reclaim did not restore touch controls") && ok;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadRemovalRevision()
		== removalRevisionBeforeUnclaimedDetach + 1,
		"post-resume unclaimed detach advanced removal revision twice") && ok;

	VirtualGamepadTest::VirtualGamepad suspendedGamepad(
		"JXQY Suspended Removal Pad");
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	gameManager.processGlobalInputFrame();
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	suspendedGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	suspendedGamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadID() == suspendedGamepad.id(),
		"suspended-removal gamepad did not claim production input") && ok;
	gameManager.controller->setTouchControlsVisible(false);
	gameManager.processGlobalInputFrame();
	const std::uint64_t removalRevisionBeforeSuspendedDetach =
		inputManager.activeGamepadRemovalRevision();
	inputManager.suspendInput();
	suspendedGamepad.detach();
	// Do not pump the queued removal event: resume must compare the refreshed
	// enumeration against the device that was active before suspension.
	inputManager.resumeInput();
	ok = check(inputManager.activeGamepadRemovalRevision()
		== removalRevisionBeforeSuspendedDetach + 1,
		"resume did not detect an active gamepad detached while suspended") && ok;
	gameManager.processGlobalInputFrame();
	ok = check(gameManager.controller->areTouchControlsVisible(),
		"suspended active-gamepad loss did not restore touch controls") && ok;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
	ok = check(inputManager.activeGamepadRemovalRevision()
		== removalRevisionBeforeSuspendedDetach + 1,
		"queued detach event advanced the narrow removal revision twice") && ok;
	return ok;
}
}
#endif

bool runMobileExternalInputRuntimeTests()
{
#ifndef __MOBILE__
	std::cerr << "mobile external-input runtime tests require "
		"JXQY_FORCE_MOBILE_UI\n";
	return false;
#else
	bool ok = checkStrictlyHeadless("before SDL event/gamepad initialization");
	try
	{
		VirtualGamepadTest::SDLSession sdlSession;
		ok = checkStrictlyHeadless("after SDL event/gamepad initialization") && ok;
		{
			auto& inputManager =
				MobileExternalInputRuntimeTestAccess::inputManager();
			HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
				inputManager);
			ok = check(inputScope.isInitialized(),
				"production physical input manager did not initialize") && ok;
			ok = testProductionMobileControlCreationAndDispatch() && ok;
			ok = checkStrictlyHeadless(
				"after production mobile-control fixture teardown") && ok;
			ok = testEnginePreserves64BitFingerIDs() && ok;
			ok = checkStrictlyHeadless("after 64-bit finger translation") && ok;
			ok = testFingerCancellationDoesNotCommit() && ok;
			ok = checkStrictlyHeadless("after finger cancellation") && ok;
			ok = testSkillDragReleaseDoesNotAlsoClick() && ok;
			ok = checkStrictlyHeadless(
				"after skill drag single-commit regression") && ok;
			ok = testGlobalTouchControlsInputFlow(inputManager) && ok;
			ok = checkStrictlyHeadless(
				"after global touch fixture teardown") && ok;
			ok = testPartnerModalCancelsHeldTouchControls() && ok;
			ok = checkStrictlyHeadless(
				"after partner-modal touch cancellation") && ok;

			{
				GameManager gameManager;
				MobilePanelFixture panels = attachMobilePanels(gameManager);
				ok = testFastSelectHostilePresentation(
					gameManager, panels) && ok;
				ok = testVirtualAttackWithoutTargetQueuesFacingAttack(
					gameManager, panels) && ok;
				ok = testFastInteractionPressBindingAndDisableInput(
					gameManager, panels) && ok;
				ok = testDeferredTouchControlsToggleTransaction(
					gameManager, inputManager, panels) && ok;
				ok = checkStrictlyHeadless(
					"after deferred touch-controls transaction") && ok;
				ok = testControllerLifecycleReset(
					gameManager, inputManager, panels) && ok;
				ok = testStoryVisibilityCancelsVirtualPointerTransactions(
					gameManager, inputManager, panels) && ok;
				ok = testRecoveryGestureLifecycleReset(
					gameManager, inputManager) && ok;
				ok = testRecoveryGestureProductionFlow(
					gameManager, inputManager, panels) && ok;
				ok = testRecoveryGestureEdgeTransitions(
					gameManager, inputManager) && ok;
				ok = checkStrictlyHeadless("after lifecycle resets") && ok;
				ok = testActiveGamepadRemovalPolicy(
					gameManager, inputManager) && ok;
				ok = checkStrictlyHeadless(
					"after gamepad removal recovery") && ok;

				gameManager.controller->setTouchControlsVisible(false);
				MobileExternalInputRuntimeTestAccess::processGlobalInputFrameWithContacts(
						gameManager,
						false,
						{ { 501, 10, 10 }, { 502, 20, 20 }, { 503, 30, 30 } },
						20000);
				ok = check(Element::isRawPointerInputBlocked(),
					"teardown fixture did not establish an active pointer gate") && ok;
			}
			ok = check(!Element::isRawPointerInputBlocked(),
				"GameManager teardown left the global pointer gate active") && ok;
		}
		ok = checkStrictlyHeadless("after runtime fixture teardown") && ok;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAILED: " << exception.what() << '\n';
		ok = false;
	}
	ok = checkStrictlyHeadless("after SDL shutdown") && ok;
	if (ok)
	{
		std::cout << "Mobile external-input runtime tests passed "
			"without initializing SDL video or entering a game loop\n";
	}
	return ok;
#endif
}
