#include "../Game/Data/Map.h"
#include "../Game/Data/Effect.h"
#include "../Engine/Engine.h"
#include "../Game/Data/MobileTouchInteraction.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/Dialog.h"
#include "../Game/Menu/EquipMenu.h"
#include "../Game/Menu/GoodsMenu.h"
#include "../Game/Menu/MapThumbnailMenu.h"
#include "../Game/Menu/MemoMemu.h"
#include "../Game/Menu/MsgBox.h"
#include "../Game/Menu/UIFocusManager.h"
#include "../Image/IMP.h"
#include "../Input/InputAction.h"
#include "../Resource/ResourceManager.h"
#include "HeadlessPhysicalInputTestHarness.h"

#include <array>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class GamepadWorldRuntimeTestAccess
{
public:
	static GameInput::PhysicalInputManager& inputManager()
	{
		return *Engine::getInstance()->physicalInputManager;
	}

	static void setRunningOwner(PElement owner)
	{
		Element::runningElement = { std::move(owner) };
	}

	static void clearRunningOwner()
	{
		Element::runningElement.clear();
	}

	static void dispatchElementEvents(Element& root)
	{
		root.allHandleEvents();
	}

	static void arrangeChildren(Element& root)
	{
		root.reArrangeChildren();
		for (const auto& child : root.children)
		{
			if (child != nullptr)
			{
				arrangeChildren(*child);
			}
		}
	}

	static void prepareHeadlessMapThumbnail(
		MapThumbnailMenu& menu,
		const Rect& bounds,
		const std::string& currentMapName)
	{
		menu.removeAllChild();
		menu.thumbnailContainer = std::make_shared<ImageContainer>();
		menu.thumbnailContainer->rect = bounds;
		menu.thumbnailContainer->coverMouse = true;
		menu.addChild(menu.thumbnailContainer);
		menu.currentMapName = currentMapName;
		menu.visible = false;
		menu.controllerCursorNormalizedX = 0.5f;
		menu.controllerCursorNormalizedY = 0.5f;
		menu.controllerCursorActive = false;
		menu.controllerFocusActive = false;
	}

	static Point mapCursorPixel(const MapThumbnailMenu& menu)
	{
		return menu.getControllerCursorPixel();
	}

	static bool mapCursorActive(const MapThumbnailMenu& menu)
	{
		return menu.controllerCursorActive;
	}

	static bool mapThumbnailCapturesPointer(
		const MapThumbnailMenu& menu)
	{
		return menu.thumbnailContainer != nullptr
			&& menu.thumbnailContainer->coverMouse;
	}

	static void setMapCursorToTile(
		MapThumbnailMenu& menu,
		Point target)
	{
		const Point pixel = menu.tileToThumbnailPixel(
			target, { 0.0, 0.0 });
		menu.setControllerCursorFromPixel(pixel.x, pixel.y);
	}

	static PElement focusedMenu(const MenuController& menuController)
	{
		return menuController.controllerFocusedMenu.lock();
	}

	static void focusMemoMenu(MenuController& menuController)
	{
		if (menuController.memoMenu == nullptr)
		{
			return;
		}
		menuController.memoMenu->visible = true;
		menuController.memoMenu->activated = true;
		if (menuController.memoMenu->scrollbar != nullptr)
		{
			menuController.memoMenu->scrollbar->visible = true;
			menuController.memoMenu->scrollbar->activated = true;
			menuController.memoMenu->scrollbar->rect = { 20, 20, 16, 120 };
			if (menuController.memoMenu->scrollbar->parent == nullptr)
			{
				menuController.memoMenu->addChild(
					menuController.memoMenu->scrollbar);
			}
			menuController.memoMenu->configureControllerFocus();
		}
		menuController.memoMenu->activateControllerFocus(
			ControllerFocusTarget::Default);
		menuController.setControllerFocusedMenu(
			menuController.memoMenu,
			MenuController::ControllerMenuRole::Memo);
	}

	static void prepareHeadlessMemoFocus(MemoMenu& menu)
	{
		menu.removeAllChild();
		menu.memoText = nullptr;
		menu.scrollbar = std::make_shared<Scrollbar>();
		menu.scrollbar->rect = { 20, 20, 16, 120 };
		menu.scrollbar->min = 0;
		menu.scrollbar->max = 4;
		menu.scrollbar->position = 0;
		menu.scrollbar->visible = true;
		menu.scrollbar->activated = true;
		menu.addChild(menu.scrollbar);
		menu.configureControllerFocus();
		menu.visible = false;
	}

	static std::shared_ptr<GameElement> focusedTarget(
		const GameController& controller)
	{
		return controller.controllerFocusedTarget.lock();
	}

	static void setFocusedTarget(
		GameController& controller,
		const std::shared_ptr<GameElement>& target)
	{
		controller.controllerFocusedTarget = target;
	}

	static bool canHandleWorldInput(const GameController& controller)
	{
		return controller.canHandleWorldInput();
	}

	static void move(
		GameController& controller,
		float axisX,
		float axisY,
		float magnitude,
		bool running)
	{
		controller.handlePhysicalMovement(
			axisX, axisY, magnitude, running);
	}

	static void moveWithLegacyKeyboard(
		GameController& controller,
		bool up,
		bool down,
		bool left,
		bool right,
		bool running)
	{
		controller.handleLegacyKeyboardMovement(
			up, down, left, right, running);
	}

	static bool handleLegacyEvent(GameController& controller, AEvent event)
	{
		return controller.onHandleEvent(event);
	}

	static void previewControllerPointerEvent(
		GameController& controller,
		AEvent event)
	{
		controller.onPreviewPointerEvent(event);
	}

	static void synchronizeMenuInputLifecycle(MenuController& menuController)
	{
		menuController.synchronizeInputLifecycle();
	}

	static void seedHeadlessHeldMouseSuppression(
		GameController& controller)
	{
		// SDL's synthetic AEvent queue deliberately does not mutate
		// SDL_GetMouseState(). Model the exact post-lifecycle state that
		// synchronizeInputLifecycle() records when a real left button is still
		// physically held, without initializing a video window.
		controller.mouseWorldInputSuppressedUntilRelease = true;
	}

	static bool suppressesHeldMouseWorldInput(
		const GameController& controller)
	{
		return controller.mouseWorldInputSuppressedUntilRelease;
	}

	static void seedHeadlessHeldWorldMouse(GameController& controller)
	{
		controller.MouseAlreadyDown = true;
		controller.touchingID = TOUCH_MOUSEID;
	}

	static void runHeldWorldMouseFrame(
		GameController& controller,
		bool leftMousePressed,
		bool pointerInputOwnedByUI)
	{
		controller.handleLegacyHeldMouseMovement(
			leftMousePressed,
			pointerInputOwnedByUI);
	}

	static bool heldWorldMouseActive(const GameController& controller)
	{
		return controller.MouseAlreadyDown;
	}

	static void isolateHeldMouseForTouchControlsVisibilityChange(
		GameController& controller,
		bool leftMousePressed)
	{
		controller.isolateHeldMouseForTouchControlsVisibilityChange(
			leftMousePressed);
	}

	static bool submitLegacyWorldAction(
		GameController& controller,
		NextAction& action)
	{
		return controller.submitLegacyWorldAction(action);
	}

	static void submitLegacyMobileTouch(Element& element, int x, int y)
	{
		element.onMouseLeftDown(x, y);
	}

	static bool dispatchMobileChildPointerDownBeforeParent(
		Element& child,
		GameController& controller,
		EventTouchID pointerID,
		int x,
		int y)
	{
		// Element::allHandleEvents commits checkAllTouchDown() before its later
		// handleEvent() pass can reach GameController::onHandleEvent(). Preserve
		// that production order while exercising the real virtual child callback.
		child.onMouseLeftDown(x, y);
		return controller.onHandleEvent(
			AEvent(ET_FINGERDOWN, pointerID, x, y, false));
	}

	static bool dispatchMobileChildPointerUpBeforeParent(
		Element& child,
		GameController& controller,
		EventTouchID pointerID,
		int x,
		int y)
	{
		child.setTime(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS);
		child.touchingDownTime = 0;
		child.mouseLDownX = x;
		child.mouseLDownY = y;
		child.onMouseLeftUp(x, y);
		return controller.onHandleEvent(
			AEvent(ET_FINGERUP, pointerID, x, y, false));
	}

	static void prepareLongPressRelease(Element& child, int x, int y)
	{
		child.setTime(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS);
		child.touchingDownTime = 0;
		child.mouseLDownX = x;
		child.mouseLDownY = y;
	}

	static void dispatch(
		GameController& controller,
		GameInput::InputAction action,
		const GameInput::GamepadAxisState& axes = {})
	{
		controller.dispatchPhysicalWorldAction(action, axes);
	}
};

namespace
{
constexpr Point PlayerPosition = { 20, 20 };
constexpr int MapWidth = 48;
constexpr int MapHeight = 48;

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool samePoint(Point left, Point right)
{
	return left.x == right.x && left.y == right.y;
}

std::shared_ptr<IMPImage> makeActionImage()
{
	auto image = std::make_shared<IMPImage>();
	image->directions = 8;
	image->interval = 16;
	image->frame.resize(8);
	return image;
}

class HeadlessPointerNPC : public NPC
{
public:
	bool mouseInRect(int x, int y) override
	{
		// Production NPC hit testing uses the current IMP frame. This headless
		// fixture has no raster resource, so preserve the production Element
		// bounds and event ordering while substituting only the pixel-mask test.
		return Element::mouseInRect(x, y);
	}
};

class HeadlessPointerObject : public Object
{
public:
	bool mouseInRect(int x, int y) override
	{
		return Element::mouseInRect(x, y);
	}
};

class GamepadWorldFixture
{
public:
	GamepadWorldFixture()
	{
		gameManager.global.data.canInput = true;
		gameManager.inEvent = false;
		gameManager.setGameplayPaused(false);

		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = MapWidth;
		gameManager.map->data->head.height = MapHeight;
		gameManager.map->data->tile.assign(
			MapHeight, std::vector<MapTile>(MapWidth));
		gameManager.map->dataMap.tile.assign(
			MapHeight, std::vector<DataTile>(MapWidth));

		gameManager.player->setPosition(PlayerPosition);
		gameManager.player->direction = 6;
		gameManager.player->canRun = true;
		gameManager.player->canJump = true;
		gameManager.player->canFight = true;
		gameManager.player->thew = 100;
		gameManager.player->info.thewMax = 100;
		gameManager.player->mana = 0;
		gameManager.player->info.manaMax = 100;

		auto actionImage = makeActionImage();
		gameManager.player->res.stand.imagePackage = actionImage;
		gameManager.player->res.walk.imagePackage = actionImage;
		gameManager.player->res.run.imagePackage = actionImage;
		gameManager.player->res.jump.imagePackage = actionImage;
		gameManager.player->res.attack.imagePackage = actionImage;
		gameManager.player->res.magic.imagePackage = actionImage;
		gameManager.player->res.sit.imagePackage = actionImage;
		gameManager.player->res.astand.imagePackage = actionImage;
		gameManager.player->res.awalk.imagePackage = actionImage;
		gameManager.player->res.arun.imagePackage = actionImage;
		gameManager.player->res.ajump.imagePackage = actionImage;
	}

	std::shared_ptr<Object> addObject(
		Point position,
		const std::string& name,
		const std::string& primaryScript = "controller_primary.lua")
	{
		auto object = std::make_shared<Object>();
		object->objName = name;
		object->scriptFile = primaryScript;
		object->setPosition(position);
		gameManager.objectManager->objectList.push_back(object);
		return object;
	}

	std::shared_ptr<HeadlessPointerObject> addHeadlessPointerObject(
		Point position,
		const std::string& name)
	{
		auto object = std::make_shared<HeadlessPointerObject>();
		object->objName = name;
		object->scriptFile = "controller_primary.lua";
		object->setPosition(position);
		gameManager.objectManager->objectList.push_back(object);
		return object;
	}

	std::shared_ptr<NPC> addNPC(
		Point position,
		const std::string& name,
		const std::string& primaryScript = "controller_npc_primary.lua",
		const std::string& alternateScript = "")
	{
		auto npc = std::make_shared<NPC>();
		npc->npcName = name;
		npc->kind = nkNormal;
		npc->relation = nrFriendly;
		npc->scriptFile = primaryScript;
		npc->scriptFileRight = alternateScript;
		npc->setPosition(position);
		gameManager.npcManager->npcList.push_back(npc);
		return npc;
	}

	std::shared_ptr<HeadlessPointerNPC> addHeadlessPointerNPC(
		Point position,
		const std::string& name)
	{
		auto npc = std::make_shared<HeadlessPointerNPC>();
		npc->npcName = name;
		npc->kind = nkNormal;
		npc->relation = nrFriendly;
		npc->scriptFile = "controller_npc_primary.lua";
		npc->setPosition(position);
		gameManager.npcManager->npcList.push_back(npc);
		return npc;
	}

	GameManager gameManager;
};

void runControllerInputFrame(
	GameController& controller,
	GameInput::PhysicalInputManager& inputManager,
	std::uint64_t& nowMilliseconds,
	std::uint64_t elapsedMilliseconds = 10)
{
	nowMilliseconds += elapsedMilliseconds;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	controller.processPhysicalInputFrame();
}

void setButtonAndRunControllerFrame(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	bool down,
	GameController& controller,
	GameInput::PhysicalInputManager& inputManager,
	std::uint64_t& nowMilliseconds)
{
	gamepad.setButton(button, down);
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
}

void tapButtonThroughController(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	GameController& controller,
	GameInput::PhysicalInputManager& inputManager,
	std::uint64_t& nowMilliseconds)
{
	setButtonAndRunControllerFrame(gamepad, button, true,
		controller, inputManager, nowMilliseconds);
	setButtonAndRunControllerFrame(gamepad, button, false,
		controller, inputManager, nowMilliseconds);
	// A shoulder tap may be resolved after the chord grace window. Advancing an
	// idle production frame keeps this test valid for that input contract.
	runControllerInputFrame(controller, inputManager, nowMilliseconds,
		GameInput::PhysicalInputManager::ShoulderChordGraceMilliseconds + 1);
}

void setAxisAndRunControllerFrame(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadAxis axis,
	Sint16 value,
	GameController& controller,
	GameInput::PhysicalInputManager& inputManager,
	std::uint64_t& nowMilliseconds)
{
	gamepad.setAxis(axis, value);
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
}

struct TriggerPulseObservation
{
	bool pressedBeforeControllerDispatch = false;
	bool releasedAfterControllerDispatch = false;
	float neutralAxisValue = 0.0f;
};

TriggerPulseObservation pulseTriggerThroughController(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadAxis trigger,
	GameController& controller,
	GameInput::PhysicalInputManager& inputManager,
	std::uint64_t& nowMilliseconds)
{
	const GameInput::InputAction action = trigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER
		? GameInput::InputAction::CycleInteractionTarget
		: GameInput::InputAction::CastSkill1;
	gamepad.setAxis(trigger, SDL_JOYSTICK_AXIS_MAX);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	TriggerPulseObservation observation;
	observation.pressedBeforeControllerDispatch =
		inputManager.wasActionPressed(action);
	controller.processPhysicalInputFrame();

	// SDL_SetJoystickVirtualAxis accepts the underlying signed joystick range;
	// virtual gamepad triggers are neutral at SDL_JOYSTICK_AXIS_MIN, not zero.
	gamepad.setAxis(trigger, SDL_JOYSTICK_AXIS_MIN);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	controller.processPhysicalInputFrame();
	observation.releasedAfterControllerDispatch =
		!inputManager.isActionDown(action);
	observation.neutralAxisValue = trigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER
		? inputManager.axes().leftTrigger
		: inputManager.axes().rightTrigger;
	return observation;
}

class HeadlessGamepadUIRoot : public Element
{
public:
	explicit HeadlessGamepadUIRoot(GameManager& gameManager)
		: gameManager(gameManager)
	{
	}

private:
	bool onHandleUIAction(UIAction action) override
	{
		return gameManager.menu != nullptr
			&& gameManager.menu->handleUIAction(action);
	}

	GameManager& gameManager;
};

class WorldPointerDispatchProbe : public Element
{
public:
	WorldPointerDispatchProbe()
	{
		setPriority(epItem);
		coverMouse = false;
	}

	int pointerDownCount = 0;

private:
	bool onHandleEvent(AEvent& event) override
	{
		if (event.eventType != ET_MOUSEDOWN
			&& event.eventType != ET_FINGERDOWN)
		{
			return false;
		}
		pointerDownCount++;
		// Observe entry into the world layer without replacing the production
		// GameController behavior that follows at epController.
		return false;
	}
};

void dispatchPointerTapThroughElementTree(
	Element& root,
	int x,
	int y)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	AEvent pointerMotion(
		ET_MOUSEMOTION, TOUCH_MOUSEID, x, y, false);
	AEvent pointerDown(ET_MOUSEDOWN, MBC_MOUSE_LEFT, x, y, false);
	AEvent pointerUp(ET_MOUSEUP, MBC_MOUSE_LEFT, x, y, false);
	Engine::getInstance()->pushEvent(pointerMotion);
	Engine::getInstance()->pushEvent(pointerDown);
	Engine::getInstance()->pushEvent(pointerUp);
	GamepadWorldRuntimeTestAccess::dispatchElementEvents(root);
}

void dispatchPointerEventThroughElementTree(
	Element& root,
	AEvent event)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(event);
	GamepadWorldRuntimeTestAccess::dispatchElementEvents(root);
}

void dispatchPointerEventsThroughElementTree(
	Element& root,
	std::initializer_list<AEvent> events)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	for (AEvent event : events)
	{
		Engine::getInstance()->pushEvent(event);
	}
	GamepadWorldRuntimeTestAccess::dispatchElementEvents(root);
}

void synchronizePointerTestLifecycle(GameManager& gameManager)
{
	gameManager.controller->synchronizeInputLifecycle();
	GamepadWorldRuntimeTestAccess::synchronizeMenuInputLifecycle(
		*gameManager.menu);
}

void attachHeadlessWorldPointerTarget(
	const PElement& manager,
	const PElement& target,
	const Rect& hitBounds)
{
	target->rect = hitBounds;
	target->coverMouse = true;
	target->visible = true;
	target->activated = true;
	manager->addChild(target);
}

bool runMapPointerElementTreeTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	auto mapMenu = std::make_shared<MapThumbnailMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMapThumbnail(
		*mapMenu, { 240, 120, 200, 160 }, "pointer routing map");
	gameManager.menu->mapThumbnailMenu = mapMenu;
	gameManager.menu->addChild(mapMenu);
	mapMenu->setControllerVisible(true);

	auto worldProbe = std::make_shared<WorldPointerDispatchProbe>();
	gameManager.addChild(worldProbe);
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);

	auto& player = *gameManager.player;
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = -1;
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	dispatchPointerTapThroughElementTree(gameManager, 40, 40);
	bool ok = check(worldProbe->pointerDownCount == 1
			&& player.nextAction != nullptr
			&& player.nextAction->action == acWalk
			&& player.nextAction->destGE.expired(),
		"nonmodal map swallowed or changed a production ground click outside"
		" its actual hit surface");

	auto pointerNPC = fixture.addHeadlessPointerNPC(
		{ 21, 20 }, "map pointer-tree NPC");
	attachHeadlessWorldPointerTarget(
		gameManager.npcManager, pointerNPC, { 45, 45, 10, 10 });
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->clickIndex = -1;
	const int pointerDownsBeforeNPC = worldProbe->pointerDownCount;
	dispatchPointerTapThroughElementTree(gameManager, 50, 50);
	ok = check(worldProbe->pointerDownCount == pointerDownsBeforeNPC + 1
			&& player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerNPC
			&& player.nextAction->destKind == ndTalk,
		"nonmodal map swallowed or changed a production NPC click outside"
		" its actual hit surface") && ok;

	auto pointerObject = fixture.addHeadlessPointerObject(
		{ 22, 20 }, "map pointer-tree item");
	attachHeadlessWorldPointerTarget(
		gameManager.objectManager, pointerObject, { 58, 58, 10, 10 });
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = 0;
	const int pointerDownsBeforeObject = worldProbe->pointerDownCount;
	dispatchPointerTapThroughElementTree(gameManager, 60, 60);
	ok = check(worldProbe->pointerDownCount == pointerDownsBeforeObject + 1
			&& player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerObject
			&& player.nextAction->destKind == ndObj,
		"nonmodal map swallowed or changed a production item click outside"
		" its actual hit surface") && ok;

	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = -1;
	const int pointerDownsBeforeMap = worldProbe->pointerDownCount;
	dispatchPointerTapThroughElementTree(gameManager, 300, 180);
	ok = check(worldProbe->pointerDownCount == pointerDownsBeforeMap
			&& player.nextAction != nullptr
			&& player.nextAction->destGE.expired(),
		"map hit continued through MenuController into the world dispatch layer")
		&& ok;
	const auto mapCandidates = mapMenu->controllerFocusCandidates();
	ok = check(mapMenu->isControllerFocusActive()
			&& mapCandidates.size() == 1
			&& mapMenu->controllerFocusedElement() == mapCandidates.front(),
		"map hit did not retain its exact logical focus target") && ok;

	auto modalDialog = std::make_shared<Dialog>();
	modalDialog->visible = true;
	gameManager.menu->dialog = modalDialog;
	gameManager.menu->addChild(modalDialog);
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	const int pointerDownsBeforeModal = worldProbe->pointerDownCount;
	dispatchPointerTapThroughElementTree(gameManager, 70, 70);
	ok = check(gameManager.menu->blocksWorldPointerInput()
			&& worldProbe->pointerDownCount == pointerDownsBeforeModal
			&& player.nextAction == nullptr,
		"explicit modal dialog did not stop an outside click before the world"
		" dispatch probe") && ok;
	gameManager.menu->removeChild(modalDialog);
	gameManager.menu->dialog.reset();
	return ok;
}

bool runPointerTransactionDragAndHideTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	auto mapMenu = std::make_shared<MapThumbnailMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMapThumbnail(
		*mapMenu, { 240, 120, 200, 160 }, "pointer transaction map");
	gameManager.menu->mapThumbnailMenu = mapMenu;
	gameManager.menu->addChild(mapMenu);
	mapMenu->setControllerVisible(true);

	auto pointerNPC = fixture.addHeadlessPointerNPC(
		{ 21, 20 }, "transaction drag NPC");
	auto pointerObject = fixture.addHeadlessPointerObject(
		{ 22, 20 }, "transaction drag Object");
	attachHeadlessWorldPointerTarget(
		gameManager.npcManager, pointerNPC, { 45, 45, 10, 10 });
	attachHeadlessWorldPointerTarget(
		gameManager.objectManager, pointerObject, { 58, 58, 10, 10 });
	auto worldProbe = std::make_shared<WorldPointerDispatchProbe>();
	gameManager.addChild(worldProbe);
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	synchronizePointerTestLifecycle(gameManager);

	auto& player = *gameManager.player;
	bool ok = true;
	dispatchPointerEventsThroughElementTree(gameManager,
		{
			AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 300, 180, false),
			AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 300, 180, false)
		});
	ok = check(gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID)
			&& mapMenu->hasPointerDownInTree(TOUCH_MOUSEID),
		"map mouse-down did not acquire one UI pointer transaction") && ok;

	// The map is allowed to queue its own movement on the original down. Clear
	// that expected action so any later NPC/world action is observable.
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->clickIndex = -1;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 50, 50, false));
	ok = check(!mapMenu->hasPointerDownInTree(TOUCH_MOUSEID)
			&& gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID)
			&& player.nextAction == nullptr,
		"dragging a held map mouse transaction onto an NPC lost ownership"
		" or produced a second world action") && ok;

	gameManager.menu->setMapThumbnailVisible(false);
	ok = check(gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID)
			&& player.nextAction == nullptr,
		"hiding a nonmodal map released its held mouse transaction early")
		&& ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 50, 50, false));
	ok = check(!gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID)
			&& player.nextAction == nullptr,
		"map-origin mouse release over an NPC committed a second world action"
		" or retained pointer ownership") && ok;

	const int pointerDownsBeforeFreshMouse = worldProbe->pointerDownCount;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 50, 50, false));
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 50, 50, false));
	ok = check(worldProbe->pointerDownCount
				== pointerDownsBeforeFreshMouse + 1
			&& !gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID)
			&& player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerNPC,
		"a fresh mouse down after the map-origin release did not restore"
		" NPC world interaction") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 50, 50, false));

	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.menu->setMapThumbnailVisible(true);
	const EventTouchID objectPointerID = 7101;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, objectPointerID, 300, 180, false));
	ok = check(gameManager.menu->ownsPointerTransaction(objectPointerID)
			&& mapMenu->hasPointerDownInTree(objectPointerID),
		"map touch-down did not acquire one UI pointer transaction") && ok;
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = 0;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERMOTION, objectPointerID, 60, 60, false));
	ok = check(!mapMenu->hasPointerDownInTree(objectPointerID)
			&& gameManager.menu->ownsPointerTransaction(objectPointerID)
			&& player.nextAction == nullptr,
		"dragging a held map touch transaction onto an Object lost ownership"
		" or produced a second world action") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, objectPointerID, 60, 60, false));
	ok = check(!gameManager.menu->ownsPointerTransaction(objectPointerID)
			&& player.nextAction == nullptr,
		"map-origin touch release over an Object committed a second world"
		" action or retained pointer ownership") && ok;
	const int pointerDownsBeforeFreshTouch = worldProbe->pointerDownCount;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, objectPointerID, 60, 60, false));
	bool freshObjectTouchReachedWorld = worldProbe->pointerDownCount
			== pointerDownsBeforeFreshTouch + 1
		&& !gameManager.menu->ownsPointerTransaction(objectPointerID);
	freshObjectTouchReachedWorld = freshObjectTouchReachedWorld
		&& player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == pointerObject;
	ok = check(freshObjectTouchReachedWorld,
		"a fresh touch down after the map-origin release did not restore"
		" Object world interaction") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, objectPointerID, 60, 60, false));
	return ok;
}

bool runPointerTransactionFingerCancelTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	auto mapMenu = std::make_shared<MapThumbnailMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMapThumbnail(
		*mapMenu, { 240, 120, 200, 160 }, "finger cancel map");
	gameManager.menu->mapThumbnailMenu = mapMenu;
	gameManager.menu->addChild(mapMenu);
	mapMenu->setControllerVisible(true);
	auto pointerObject = fixture.addHeadlessPointerObject(
		{ 22, 20 }, "finger cancel Object");
	attachHeadlessWorldPointerTarget(
		gameManager.objectManager, pointerObject, { 58, 58, 10, 10 });
	auto worldProbe = std::make_shared<WorldPointerDispatchProbe>();
	gameManager.addChild(worldProbe);
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	synchronizePointerTestLifecycle(gameManager);

	const EventTouchID pointerID = 7201;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, pointerID, 300, 180, false));
	bool ok = check(gameManager.menu->ownsPointerTransaction(pointerID)
			&& mapMenu->hasPointerDownInTree(pointerID),
		"cancel fixture did not acquire the map touch transaction");
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERCANCEL, pointerID, 300, 180, false));
	ok = check(!gameManager.menu->ownsPointerTransaction(pointerID)
			&& !mapMenu->hasPointerDownInTree(pointerID)
			&& gameManager.player->nextAction == nullptr,
		"ET_FINGERCANCEL retained UI ownership, touch state, or committed"
		" a world action") && ok;

	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = 0;
	const int pointerDownsBeforeFreshTouch = worldProbe->pointerDownCount;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, pointerID, 60, 60, false));
	bool freshObjectTouchReachedWorld = worldProbe->pointerDownCount
			== pointerDownsBeforeFreshTouch + 1
		&& !gameManager.menu->ownsPointerTransaction(pointerID);
	freshObjectTouchReachedWorld = freshObjectTouchReachedWorld
		&& gameManager.player->nextAction != nullptr
		&& gameManager.player->nextAction->destGE.lock() == pointerObject;
	ok = check(freshObjectTouchReachedWorld,
		"the same finger ID could not start a fresh Object transaction after"
		" ET_FINGERCANCEL") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, pointerID, 60, 60, false));
	return ok;
}

bool runCrossPlatformTouchWorldInteractionTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	Player& player = *gameManager.player;
	auto pointerObject = fixture.addHeadlessPointerObject(
		{ 22, 20 }, "cross-platform touch Object");
	auto pointerNPC = fixture.addHeadlessPointerNPC(
		{ 21, 20 }, "cross-platform touch NPC");
	attachHeadlessWorldPointerTarget(
		gameManager.objectManager, pointerObject, { 58, 58, 10, 10 });
	attachHeadlessWorldPointerTarget(
		gameManager.npcManager, pointerNPC, { 45, 45, 10, 10 });
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	synchronizePointerTestLifecycle(gameManager);

	const EventTouchID objectPointerID = 7301;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, objectPointerID, 60, 60, false));
	bool ok = check(player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerObject
			&& player.nextAction->destKind == ndObj,
		"cross-platform Object finger hit did not submit its child-owned action");
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, objectPointerID, 60, 60, false));
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	pointerObject->scriptFileRight = "cross_platform_object_right.lua";
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, objectPointerID, 60, 60, false));
	ok = check(player.nextAction == nullptr,
		"cross-platform Object with two scripts did not defer until release")
		&& ok;
	GamepadWorldRuntimeTestAccess::prepareLongPressRelease(
		*pointerObject, 60, 60);
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, objectPointerID, 60, 60, false));
	ok = check(player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerObject
			&& player.nextAction->useRightScript,
		"cross-platform Object long press did not select the alternate script")
		&& ok;

	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	const EventTouchID npcPointerID = 7302;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, npcPointerID, 50, 50, false));
	ok = check(player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerNPC
			&& player.nextAction->destKind == ndTalk,
		"cross-platform NPC finger hit did not submit its child-owned action")
		&& ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, npcPointerID, 50, 50, false));
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	pointerNPC->scriptFileRight = "cross_platform_npc_right.lua";
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERDOWN, npcPointerID, 50, 50, false));
	ok = check(player.nextAction == nullptr,
		"cross-platform NPC with two scripts did not defer until release")
		&& ok;
	GamepadWorldRuntimeTestAccess::prepareLongPressRelease(
		*pointerNPC, 50, 50);
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_FINGERUP, npcPointerID, 50, 50, false));
	ok = check(player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == pointerNPC
			&& player.nextAction->useRightScript,
		"cross-platform NPC long press did not select the alternate script")
		&& ok;
	return ok;
}

bool runHeldMouseLifecycleTransactionTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	GameController& controller = *gameManager.controller;
	auto mapMenu = std::make_shared<MapThumbnailMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMapThumbnail(
		*mapMenu, { 240, 120, 200, 160 }, "lifecycle pointer map");
	gameManager.menu->mapThumbnailMenu = mapMenu;
	gameManager.menu->addChild(mapMenu);
	mapMenu->setControllerVisible(true);
	auto pointerNPC = fixture.addHeadlessPointerNPC(
		{ 21, 20 }, "lifecycle pointer NPC");
	attachHeadlessWorldPointerTarget(
		gameManager.npcManager, pointerNPC, { 45, 45, 10, 10 });
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);

	auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();
	inputManager.setWindowFocused(true);
	controller.synchronizeInputLifecycle();
	GamepadWorldRuntimeTestAccess::synchronizeMenuInputLifecycle(
		*gameManager.menu);
	dispatchPointerEventsThroughElementTree(gameManager,
		{
			AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 300, 180, false),
			AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 300, 180, false)
		});
	bool ok = check(gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID),
		"lifecycle fixture did not acquire a map mouse transaction");
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;

	const std::uint64_t revisionBeforeLoss =
		inputManager.inputLifecycleRevision();
	inputManager.setWindowFocused(false);
	ok = check(inputManager.inputLifecycleRevision() > revisionBeforeLoss,
		"window-focus loss did not advance the input lifecycle revision") && ok;
	controller.synchronizeInputLifecycle();
	GamepadWorldRuntimeTestAccess::synchronizeMenuInputLifecycle(
		*gameManager.menu);
	GamepadWorldRuntimeTestAccess::seedHeadlessHeldMouseSuppression(controller);
	ok = check(!gameManager.menu->ownsPointerTransaction(TOUCH_MOUSEID)
			&& !mapMenu->hasPointerDownInTree(TOUCH_MOUSEID)
			&& GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller),
		"lifecycle reset did not hand a physically held UI mouse transaction"
		" to the release gate") && ok;

	AEvent heldMotion(
		ET_MOUSEMOTION, TOUCH_MOUSEID, 50, 50, false);
	GamepadWorldRuntimeTestAccess::previewControllerPointerEvent(
		controller, heldMotion);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, heldMotion);
	ok = check(GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller)
			&& gameManager.player->nextAction == nullptr,
		"a lifecycle revision replayed the held UI mouse transaction into"
		" the world before release") && ok;

	AEvent release(
		ET_MOUSEUP, MBC_MOUSE_LEFT, 50, 50, false);
	GamepadWorldRuntimeTestAccess::previewControllerPointerEvent(
		controller, release);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(controller, release);
	ok = check(!GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller)
			&& gameManager.player->nextAction == nullptr,
		"mouse release did not clear the lifecycle-held world suppression")
		&& ok;

	gameManager.menu->setMapThumbnailVisible(false);
	inputManager.setWindowFocused(true);
	controller.synchronizeInputLifecycle();
	GamepadWorldRuntimeTestAccess::synchronizeMenuInputLifecycle(
		*gameManager.menu);
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->clickIndex = -1;
	dispatchPointerEventsThroughElementTree(gameManager,
		{
			AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 50, 50, false),
			AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 50, 50, false)
		});
	ok = check(gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->destGE.lock() == pointerNPC,
		"a fresh mouse down did not restore NPC interaction after lifecycle"
		" release recovery") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 50, 50, false));
	return ok;
}

bool runHeldWorldMouseAcrossMenuTransitionTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	GameController& controller = *gameManager.controller;
	auto mapMenu = std::make_shared<MapThumbnailMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMapThumbnail(
		*mapMenu, { 240, 120, 200, 160 }, "world-held transition map");
	gameManager.menu->mapThumbnailMenu = mapMenu;
	gameManager.menu->addChild(mapMenu);
	auto pointerObject = fixture.addHeadlessPointerObject(
		{ 22, 20 }, "world-held transition Object");
	attachHeadlessWorldPointerTarget(
		gameManager.objectManager, pointerObject, { 58, 58, 10, 10 });
	GamepadWorldRuntimeTestAccess::arrangeChildren(gameManager);
	synchronizePointerTestLifecycle(gameManager);

	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = 0;
	dispatchPointerEventsThroughElementTree(gameManager,
		{
			AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 60, 60, false),
			AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 60, 60, false)
		});
	bool ok = check(gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->destGE.lock() == pointerObject,
		"world-held transition fixture could not start its Object transaction"
		" (object-hit="
			+ std::to_string(pointerObject->mouseInRect(60, 60))
			+ ", object-down="
			+ std::to_string(pointerObject->touchingDownID == TOUCH_MOUSEID)
			+ ", controller-down="
			+ std::to_string(controller.touchingDownID == TOUCH_MOUSEID)
			+ ", click-index="
			+ std::to_string(gameManager.objectManager->clickIndex)
			+ ", queued="
			+ std::to_string(gameManager.player->nextAction != nullptr)
			+ ", target="
			+ std::to_string(gameManager.player->nextAction != nullptr
				&& gameManager.player->nextAction->destGE.lock()
					== pointerObject)
			+ ")");

	gameManager.menu->setMapThumbnailVisible(true);
	GamepadWorldRuntimeTestAccess::seedHeadlessHeldMouseSuppression(controller);
	ok = check(mapMenu->visible
			&& gameManager.player->nextAction == nullptr
			&& GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller),
		"opening the map did not cancel and gate the held world mouse"
		" transaction") && ok;
	gameManager.menu->setMapThumbnailVisible(false);
	AEvent heldMotion(
		ET_MOUSEMOTION, TOUCH_MOUSEID, 60, 60, false);
	GamepadWorldRuntimeTestAccess::previewControllerPointerEvent(
		controller, heldMotion);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, heldMotion);
	ok = check(!mapMenu->visible
			&& GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller)
			&& gameManager.player->nextAction == nullptr,
		"closing the map replayed the pre-menu held mouse transaction")
		&& ok;

	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 60, 60, false));
	ok = check(!GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller)
			&& gameManager.player->nextAction == nullptr,
		"releasing the pre-menu world transaction committed again or left"
		" the release gate active") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 60, 60, false));
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 60, 60, false));
	ok = check(gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->destGE.lock() == pointerObject,
		"a fresh mouse down after the menu transition did not restore"
		" Object interaction") && ok;
	dispatchPointerEventThroughElementTree(gameManager,
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 60, 60, false));
	return ok;
}

bool runTouchControlsVisibilityHeldMouseTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	GameController& controller = *gameManager.controller;
	GamepadWorldRuntimeTestAccess::seedHeadlessHeldWorldMouse(controller);
	GamepadWorldRuntimeTestAccess::isolateHeldMouseForTouchControlsVisibilityChange(
		controller, true);

	bool ok = check(
		!GamepadWorldRuntimeTestAccess::heldWorldMouseActive(controller)
			&& GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller),
		"touch-controls visibility change did not isolate the held world mouse");
	GamepadWorldRuntimeTestAccess::runHeldWorldMouseFrame(
		controller, true, false);
	ok = check(gameManager.player->nextAction == nullptr
			&& GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
				controller),
		"held mouse resumed world movement after the visibility gate drained")
		&& ok;

	AEvent release(ET_MOUSEUP, MBC_MOUSE_LEFT, 60, 60, false);
	GamepadWorldRuntimeTestAccess::previewControllerPointerEvent(
		controller, release);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(controller, release);
	ok = check(!GamepadWorldRuntimeTestAccess::suppressesHeldMouseWorldInput(
			controller),
		"held-mouse visibility isolation survived the physical release") && ok;
	return ok;
}

bool runHeldWorldMouseBehindOrdinaryMenuTests()
{
	GamepadWorldFixture fixture;
	GameManager& gameManager = fixture.gameManager;
	GameController& controller = *gameManager.controller;
	Player& player = *gameManager.player;
	auto memoMenu = std::make_shared<MemoMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMemoFocus(*memoMenu);
	gameManager.menu->memoMenu = memoMenu;
	gameManager.menu->addChild(memoMenu);
	memoMenu->visible = true;
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = -1;
	GamepadWorldRuntimeTestAccess::seedHeadlessHeldWorldMouse(controller);

	GamepadWorldRuntimeTestAccess::runHeldWorldMouseFrame(
		controller, true, false);
	bool ok = check(gameManager.menu->blocksWorldInput()
			&& !gameManager.menu->blocksWorldPointerInput()
			&& player.nextAction != nullptr
			&& player.nextAction->action == acWalk,
		"an ordinary menu blocked the first held-mouse world movement frame");
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	GamepadWorldRuntimeTestAccess::runHeldWorldMouseFrame(
		controller, true, false);
	ok = check(player.nextAction != nullptr
			&& player.nextAction->action == acWalk
			&& GamepadWorldRuntimeTestAccess::heldWorldMouseActive(controller),
		"an ordinary menu stopped held-mouse movement before release") && ok;

	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	GamepadWorldRuntimeTestAccess::runHeldWorldMouseFrame(
		controller, true, true);
	ok = check(player.nextAction == nullptr,
		"a UI-owned mouse transaction leaked continuous movement into the world")
		&& ok;

	auto dialog = std::make_shared<Dialog>();
	dialog->visible = true;
	gameManager.menu->dialog = dialog;
	GamepadWorldRuntimeTestAccess::runHeldWorldMouseFrame(
		controller, true, false);
	ok = check(gameManager.menu->blocksWorldPointerInput()
			&& player.nextAction == nullptr,
		"a modal pointer barrier leaked held-mouse movement into the world")
		&& ok;
	GamepadWorldRuntimeTestAccess::runHeldWorldMouseFrame(
		controller, false, false);
	ok = check(!GamepadWorldRuntimeTestAccess::heldWorldMouseActive(controller),
		"mouse release did not end the held-world movement transaction") && ok;
	return ok;
}

bool runProductionSemanticAndGameplayFrame(
	HeadlessPhysicalInputTest::FrameDriver& frameDriver,
	std::uint64_t elapsedMilliseconds = 10)
{
	return frameDriver.runFrame(elapsedMilliseconds);
}

bool setButtonAndRunProductionFrame(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	bool down,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver,
	const HeadlessPhysicalInputTest::FrameCallbacks& callbacks = {})
{
	return frameDriver.runButtonFrame(gamepad, button, down, callbacks);
}

bool tapButtonThroughProductionFrame(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	return frameDriver.tapButton(gamepad, button);
}

bool runProductionInputDispatchTests()
{
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad primaryGamepad(
		"JXQY World Primary Pad");
	VirtualGamepadTest::VirtualGamepad standbyGamepad(
		"JXQY World Standby Pad");
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& controller = *gameManager.controller;
	auto& player = *gameManager.player;
	auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	bool ok = check(inputScope.isInitialized(),
		"production physical input manager did not initialize");
	if (!inputScope.isInitialized())
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
		controller, inputManager, nowMilliseconds);
	ok = check(inputManager.activeGamepadID() == primaryGamepad.id(),
		"fresh virtual input did not claim the production input channel") && ok;
	auto memoMenu = std::make_shared<MemoMenu>();
	memoMenu->memoText = nullptr;
	memoMenu->scrollbar = std::make_shared<Scrollbar>();
	memoMenu->scrollbar->min = 0;
	memoMenu->scrollbar->max = 4;
	memoMenu->scrollbar->position = 0;
	GamepadWorldRuntimeTestAccess::setRunningOwner(memoMenu);
	primaryGamepad.setAxis(
		SDL_GAMEPAD_AXIS_RIGHTY, SDL_JOYSTICK_AXIS_MAX);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	const bool scrollPressedBeforeDispatch =
		inputManager.wasActionPressed(GameInput::InputAction::ScrollDown);
	const bool scrollDispatched = dispatchPhysicalUIActions(
		Engine::getInstance());
	ok = check(scrollPressedBeforeDispatch
		&& scrollDispatched
		&& memoMenu->scrollbar->position == 1
		&& !inputManager.wasActionPressed(GameInput::InputAction::ScrollDown),
		"virtual right stick did not dispatch and consume scrolling through the running menu") && ok;
	GamepadWorldRuntimeTestAccess::clearRunningOwner();
	primaryGamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTY, 0);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);

	auto executionReadyAttackTarget = fixture.addNPC(
		{ 21, 20 }, "controller attack-radius target");
	executionReadyAttackTarget->kind = nkBattle;
	executionReadyAttackTarget->relation = nrHostile;
	auto focusedAttackTarget = fixture.addNPC(
		{ 28, 20 }, "controller focused attack target");
	focusedAttackTarget->kind = nkBattle;
	focusedAttackTarget->relation = nrHostile;
	fixture.addObject({ 22, 22 }, "mouse residue object");
	player.attackRadius = 3;
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->clickIndex = 0;
	GamepadWorldRuntimeTestAccess::setFocusedTarget(
		controller, focusedAttackTarget);
	player.cancelQueuedInteraction(false);
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destKind == ndAttack
		&& player.nextAction->destGE.lock() == focusedAttackTarget
		&& player.nextAction->strictWorldInteraction
		&& gameManager.npcManager->clickIndex == 0
		&& gameManager.objectManager->clickIndex == 0,
		"physical X did not prefer controller focus independently of mouse residue") && ok;

	player.cancelQueuedInteraction(false);
	GamepadWorldRuntimeTestAccess::setFocusedTarget(controller, nullptr);
	gameManager.npcManager->clickIndex = 1;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destKind == ndAttack
		&& player.nextAction->destGE.lock() == executionReadyAttackTarget
		&& player.nextAction->strictWorldInteraction
		&& gameManager.npcManager->clickIndex == 1,
		"physical X followed stale mouse clickIndex instead of Attack sorting") && ok;

	player.cancelQueuedInteraction(false);
	gameManager.npcManager->npcList.clear();
	gameManager.objectManager->objectList.clear();
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = -1;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_SOUTH,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction == nullptr,
		"physical A without an interaction target leaked a facing attack") && ok;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acAttack
		&& player.nextAction->destKind == ndNone
		&& player.nextAction->destGE.expired(),
		"physical X without a hostile target did not preserve its facing fallback") && ok;
	player.nextAction = nullptr;

	gameManager.global.data.canInput = false;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction == nullptr,
		"canInput=false leaked a production gamepad world action") && ok;
	gameManager.global.data.canInput = true;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	// Returning to the world releases once at the controller boundary. A second
	// neutral frame clears AwaitNeutral before this fixture starts a new input.
	runControllerInputFrame(controller, inputManager, nowMilliseconds);

	gameManager.inEvent = true;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction == nullptr,
		"inEvent=true leaked a production gamepad world action") && ok;
	gameManager.inEvent = false;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	runControllerInputFrame(controller, inputManager, nowMilliseconds);

	gameManager.setGameplayPaused(true);
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction == nullptr,
		"gameplay pause leaked a production gamepad world action") && ok;
	gameManager.setGameplayPaused(false);
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	runControllerInputFrame(controller, inputManager, nowMilliseconds);

	gameManager.menu->mapThumbnailMenu = std::make_shared<MapThumbnailMenu>();
	gameManager.menu->mapThumbnailMenu->visible = true;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_WEST,
		controller, inputManager, nowMilliseconds);
	ok = check(gameManager.menu->blocksWorldInput()
		&& player.nextAction == nullptr,
		"visible menu leaked a production gamepad world action") && ok;
	gameManager.menu->mapThumbnailMenu->visible = false;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	auto firstTarget = fixture.addObject({ 21, 20 }, "first target");
	auto secondTarget = fixture.addObject({ 22, 20 }, "second target");
	firstTarget->scriptFileRight = "controller_alternate.lua";
	const auto candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Primary,
		GameController::KeyboardAutoInteractionTileDistance, 2);
	ok = check(candidates.size() == 2
		&& candidates[0].getTarget() == firstTarget
		&& candidates[1].getTarget() == secondTarget,
		"target-cycle fixture did not expose two ordered production candidates") && ok;
	const TriggerPulseObservation firstCyclePulse = pulseTriggerThroughController(
		primaryGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(firstCyclePulse.pressedBeforeControllerDispatch,
		"first LT pulse did not produce a production press edge") && ok;
	ok = check(firstCyclePulse.releasedAfterControllerDispatch,
		"first LT pulse remained logically held after virtual-axis release") && ok;
	ok = check(firstCyclePulse.neutralAxisValue
		<= GameInput::PhysicalInputManager::TriggerReleaseThreshold,
		"virtual LT release did not return the production axis below threshold") && ok;
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller)
		== firstTarget,
		"first LT trigger did not focus the first production candidate") && ok;
	const TriggerPulseObservation secondCyclePulse = pulseTriggerThroughController(
		primaryGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(secondCyclePulse.pressedBeforeControllerDispatch,
		"second LT pulse was not a new production input edge after release") && ok;
	ok = check(secondCyclePulse.releasedAfterControllerDispatch,
		"second LT pulse remained logically held after virtual-axis release") && ok;
	ok = check(secondCyclePulse.neutralAxisValue
		<= GameInput::PhysicalInputManager::TriggerReleaseThreshold,
		"second virtual LT release remained above production threshold") && ok;
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller)
		== secondTarget,
		"second LT trigger did not cycle to the next production candidate") && ok;
	const TriggerPulseObservation wrapCyclePulse = pulseTriggerThroughController(
		primaryGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(wrapCyclePulse.pressedBeforeControllerDispatch,
		"wrap LT pulse did not produce a fresh press edge") && ok;
	ok = check(wrapCyclePulse.releasedAfterControllerDispatch,
		"wrap LT pulse remained logically held after release") && ok;
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller)
		== firstTarget,
		"target cycling did not wrap to the first candidate") && ok;

	player.nextAction = nullptr;
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_SOUTH,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == firstTarget
		&& player.nextAction->strictWorldInteraction,
		"A did not queue the focused target through the production action path") && ok;
	player.cancelQueuedInteraction(false);
	tapButtonThroughController(primaryGamepad,
		SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == firstTarget
		&& player.nextAction->useRightScript
		&& player.nextAction->strictWorldInteraction,
		"RB did not queue the focused alternate interaction through production input") && ok;
	player.cancelQueuedInteraction(false);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller)
		== firstTarget,
		"directional-skill focus-clear precondition lost the LT target") && ok;

	setAxisAndRunControllerFrame(
		primaryGamepad,
		SDL_GAMEPAD_AXIS_RIGHTX,
		SDL_JOYSTICK_AXIS_MAX,
		controller,
		inputManager,
		nowMilliseconds);
	player.nextAction = nullptr;
	const TriggerPulseObservation positiveSkillPulse =
		pulseTriggerThroughController(
			primaryGamepad,
			SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
			controller, inputManager, nowMilliseconds);
	const Point positiveAimDestination = player.nextAction == nullptr
		? Point{ 0, 0 } : player.nextAction->dest;
	ok = check(positiveSkillPulse.pressedBeforeControllerDispatch
		&& positiveSkillPulse.releasedAfterControllerDispatch
		&& player.nextAction != nullptr
		&& player.nextAction->action == acMagic
		&& player.nextAction->destGE.expired()
		&& GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"virtual right stick and RT did not dispatch directional skill through the production path"
			" or retained the previous entity focus"
			" (skill slots="
			+ std::to_string(gameManager.magicManager.bottomCount())
			+ ", next action="
			+ std::to_string(player.nextAction == nullptr
				? -1 : static_cast<int>(player.nextAction->action))
			+ ")") && ok;

	setAxisAndRunControllerFrame(
		primaryGamepad,
		SDL_GAMEPAD_AXIS_RIGHTX,
		SDL_JOYSTICK_AXIS_MIN,
		controller,
		inputManager,
		nowMilliseconds);
	player.nextAction = nullptr;
	pulseTriggerThroughController(primaryGamepad,
		SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acMagic
		&& !samePoint(positiveAimDestination, player.nextAction->dest),
		"opposite virtual right-stick directions did not preserve action-frame aiming") && ok;
	setAxisAndRunControllerFrame(
		primaryGamepad,
		SDL_GAMEPAD_AXIS_RIGHTX,
		0,
		controller,
		inputManager,
		nowMilliseconds);
	pulseTriggerThroughController(primaryGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) != nullptr,
		"movement-clear precondition could not restore a controller target") && ok;

	setAxisAndRunControllerFrame(primaryGamepad, SDL_GAMEPAD_AXIS_LEFTX, 32767,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"manual stick movement did not clear the controller target") && ok;
	setAxisAndRunControllerFrame(primaryGamepad, SDL_GAMEPAD_AXIS_LEFTX, 0,
		controller, inputManager, nowMilliseconds);
	player.nextAction = nullptr;

	pulseTriggerThroughController(primaryGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	tapButtonThroughController(primaryGamepad, SDL_GAMEPAD_BUTTON_SOUTH,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) != nullptr
		&& player.nextAction != nullptr,
		"disconnect precondition did not create focused queued interaction") && ok;
	primaryGamepad.detach();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(!inputManager.hasActiveGamepad()
		&& GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr
		&& player.nextAction == nullptr,
		"active device removal did not clear target and queued interaction") && ok;

	tapButtonThroughController(standbyGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
		controller, inputManager, nowMilliseconds);
	ok = check(inputManager.activeGamepadID() == standbyGamepad.id(),
		"standby gamepad could not claim after active device removal") && ok;
	pulseTriggerThroughController(standbyGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) != nullptr,
		"lifecycle test could not establish a focused target") && ok;
	standbyGamepad.setAxis(
		SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	ok = check(inputManager.isActionDown(GameInput::InputAction::Move),
		"lifecycle test could not hold a continuous move action") && ok;
	player.nextAction = nullptr;
	inputManager.suspendInput();
	controller.processPhysicalInputFrame();
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr
		&& !inputManager.isActionDown(GameInput::InputAction::Move)
		&& player.nextAction == nullptr,
		"input suspension replayed released movement or retained the controller target") && ok;
	standbyGamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	inputManager.resumeInput();
	controller.processPhysicalInputFrame();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);

	tapButtonThroughController(standbyGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
		controller, inputManager, nowMilliseconds);
	pulseTriggerThroughController(standbyGamepad,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) != nullptr,
		"focus-loss test could not re-establish a controller target") && ok;
	inputManager.setWindowFocused(false);
	controller.processPhysicalInputFrame();
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"window focus lifecycle did not clear the controller target") && ok;
	inputManager.setWindowFocused(true);
	controller.processPhysicalInputFrame();
	return ok;
}

bool runProductionTargetPromptAndInvalidationTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"target prompt test started with SDL video initialized");
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY World Target Prompt Pad");
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& controller = *gameManager.controller;
	auto& player = *gameManager.player;
	auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();
	auto messageBox = std::make_shared<MsgBox>();
	gameManager.menu->messageBox = messageBox;
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	ok = check(inputScope.isInitialized(),
		"target prompt input manager did not initialize") && ok;
	if (!inputScope.isInitialized())
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	tapButtonThroughController(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
		controller, inputManager, nowMilliseconds);
	ok = check(inputManager.activeGamepadID() == gamepad.id(),
		"target prompt gamepad did not claim the production input channel") && ok;

	auto object = fixture.addObject(
		{ 21, 20 }, "机关木箱", "object_primary.lua");
	auto npc = fixture.addNPC(
		{ 22, 20 }, "守门弟子", "npc_primary.lua");
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == object
		&& messageBox->currentMessage == object->objName,
		"first LT did not show the exact Object name") && ok;
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc
		&& messageBox->currentMessage == npc->npcName,
		"second LT did not show the exact NPC name") && ok;

	gameManager.objectManager->objectList.clear();
	gameManager.npcManager->npcList.clear();
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr
		&& messageBox->currentMessage == "附近没有可交互目标",
		"LT without candidates did not clear focus and show its exact notice") && ok;

	gameManager.objectManager->objectList.push_back(object);
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == object,
		"RB no-candidate fixture could not focus its primary-only Object") && ok;
	player.nextAction = nullptr;
	tapButtonThroughController(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
		controller, inputManager, nowMilliseconds);
	ok = check(player.nextAction == nullptr
		&& GamepadWorldRuntimeTestAccess::focusedTarget(controller) == object
		&& messageBox->currentMessage == "附近没有可用的右侧交互目标",
		"RB without Alternate candidates queued Primary or lost its exact notice") && ok;

	gameManager.objectManager->objectList.clear();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained a removed Object target") && ok;

	gameManager.npcManager->npcList.push_back(npc);
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc,
		"hidden-NPC fixture could not focus its visible target") && ok;
	npc->isVisibleByVariable = false;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained a hidden NPC target") && ok;

	npc->isVisibleByVariable = true;
	npc->scriptFile = "npc_primary.lua";
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc,
		"script-invalidation fixture could not refocus its NPC") && ok;
	npc->scriptFile.clear();
	npc->scriptFileRight.clear();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained an NPC without interaction scripts") && ok;

	npc->scriptFile = "npc_primary.lua";
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc,
		"death-invalidation fixture could not refocus its NPC") && ok;
	npc->nowAction = acDeath;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained a dying NPC target") && ok;

	npc->nowAction = acStand;
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc,
		"hide-invalidation fixture could not refocus its NPC") && ok;
	npc->nowAction = acHide;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained an action-hidden NPC target") && ok;

	npc->nowAction = acStand;
	npc->setPosition({ 22, 20 });
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc,
		"range-invalidation fixture could not refocus its nearby NPC") && ok;
	npc->setPosition({ 40, 20 });
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained an NPC beyond the search radius") && ok;

	npc->setPosition({ 22, 20 });
	pulseTriggerThroughController(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
		controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == npc,
		"line-of-sight fixture could not refocus its visible NPC") && ok;
	gameManager.map->data->tile[20][21].obstacle = toObstacle;
	ok = check(!gameManager.map->canSee(PlayerPosition, npc->getPosition()),
		"line-of-sight fixture did not block the focused NPC") && ok;
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	ok = check(GamepadWorldRuntimeTestAccess::focusedTarget(controller) == nullptr,
		"neutral production frame retained an NPC behind an obstacle") && ok;
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"target prompt test initialized SDL video") && ok;
	return ok;
}

bool runWorldInputContextTransitionTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"world-input context-transition test started with SDL video initialized");
	{
		VirtualGamepadTest::SDLSession sdlSession;
		VirtualGamepadTest::VirtualGamepad gamepad(
			"JXQY World Context Transition Pad");
		GamepadWorldFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& controller = *gameManager.controller;
		auto& player = *gameManager.player;
		auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();
		HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
			inputManager);
		ok = check(inputScope.isInitialized(),
			"world-input context-transition input manager did not initialize") && ok;
		if (inputScope.isInitialized())
		{
			std::uint64_t nowMilliseconds = SDL_GetTicks();
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			tapButtonThroughController(
				gamepad,
				SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
				controller,
				inputManager,
				nowMilliseconds);
			ok = check(inputManager.activeGamepadID() == gamepad.id(),
				"virtual gamepad did not claim the world context-transition channel")
				&& ok;

			player.nextAction = nullptr;
			gameManager.inEvent = true;
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			// The entry transition releases the device into AwaitNeutral. One idle
			// production frame makes a subsequent input genuinely originate inside
			// the blocked story context.
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			gamepad.setAxis(
				SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			ok = check(inputManager.isActionDown(GameInput::InputAction::Move)
				&& player.nextAction == nullptr,
				"story context did not retain the held physical input solely inside its input manager")
				&& ok;

			gameManager.inEvent = false;
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			ok = check(!inputManager.isActionDown(GameInput::InputAction::Move)
				&& player.nextAction == nullptr,
				"leaving a blocked story context replayed its held movement into the world")
				&& ok;
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			ok = check(!inputManager.isActionDown(GameInput::InputAction::Move)
				&& player.nextAction == nullptr,
				"held movement bypassed the return-to-world neutral gate") && ok;

			gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
			runControllerInputFrame(controller, inputManager, nowMilliseconds);
			player.nextAction = nullptr;
			setAxisAndRunControllerFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_LEFTX,
				SDL_JOYSTICK_AXIS_MAX,
				controller,
				inputManager,
				nowMilliseconds);
			ok = check(player.nextAction != nullptr
				&& player.nextAction->action == acARun,
				"fresh movement did not resume after return-to-world neutral recovery")
				&& ok;
			gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
			runControllerInputFrame(controller, inputManager, nowMilliseconds);

			auto uiRoot = std::make_shared<HeadlessGamepadUIRoot>(gameManager);
			HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(uiRoot);
			HeadlessPhysicalInputTest::FrameDriver frameDriver(
				inputManager,
				nowMilliseconds,
				[]()
				{
					return dispatchPhysicalUIActions(Engine::getInstance());
				},
				[&controller]()
				{
					controller.processPhysicalInputFrame();
				});

			player.nextAction = nullptr;
			gameManager.inEvent = true;
			// Enter the story barrier first, then clear its AwaitNeutral gate so
			// West genuinely becomes held inside an already-stable blocked context.
			runProductionSemanticAndGameplayFrame(frameDriver);
			runProductionSemanticAndGameplayFrame(frameDriver);
			bool attackPressedInsideEvent = false;
			HeadlessPhysicalInputTest::FrameCallbacks eventAttackCallbacks;
			eventAttackCallbacks.afterInputUpdate =
				[&attackPressedInsideEvent](
					const GameInput::PhysicalInputManager& frameInputManager)
			{
				attackPressedInsideEvent =
					frameInputManager.wasActionPressed(
						GameInput::InputAction::AttackPrimary)
					&& frameInputManager.isActionDown(
						GameInput::InputAction::AttackPrimary);
			};
			setButtonAndRunProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_WEST,
				true,
				frameDriver,
				eventAttackCallbacks);
			ok = check(attackPressedInsideEvent
				&& inputManager.isActionDown(
					GameInput::InputAction::AttackPrimary)
				&& player.nextAction == nullptr,
				"West pressed inside inEvent did not stay isolated from AttackPrimary")
				&& ok;

			gameManager.inEvent = false;
			runProductionSemanticAndGameplayFrame(frameDriver);
			ok = check(!inputManager.isActionDown(
					GameInput::InputAction::AttackPrimary)
				&& player.nextAction == nullptr,
				"leaving inEvent replayed held West as AttackPrimary") && ok;
			runProductionSemanticAndGameplayFrame(frameDriver);
			ok = check(player.nextAction == nullptr,
				"held West bypassed AwaitNeutral after leaving inEvent") && ok;

			setButtonAndRunProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_WEST,
				false,
				frameDriver);
			runProductionSemanticAndGameplayFrame(frameDriver);
			player.nextAction = nullptr;
			setButtonAndRunProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_WEST,
				true,
				frameDriver);
			ok = check(player.nextAction != nullptr
				&& player.nextAction->action == acAttack,
				"fresh West did not restore AttackPrimary after inEvent neutral recovery")
				&& ok;
			setButtonAndRunProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_WEST,
				false,
				frameDriver);

			auto memoMenu = std::make_shared<MemoMenu>();
			memoMenu->memoText = nullptr;
			memoMenu->scrollbar = std::make_shared<Scrollbar>();
			memoMenu->scrollbar->min = 0;
			memoMenu->scrollbar->max = 4;
			memoMenu->scrollbar->position = 0;
			gameManager.menu->memoMenu = memoMenu;
			GamepadWorldRuntimeTestAccess::focusMemoMenu(*gameManager.menu);
			player.nextAction = nullptr;
			// The first frame observes the world-to-menu transition; the second
			// clears AwaitNeutral before the input genuinely begins in the menu.
			runProductionSemanticAndGameplayFrame(frameDriver);
			runProductionSemanticAndGameplayFrame(frameDriver);

			bool skillAliasPressedInsideMenu = false;
			HeadlessPhysicalInputTest::FrameCallbacks menuSkillCallbacks;
			menuSkillCallbacks.afterInputUpdate =
				[&skillAliasPressedInsideMenu](
					const GameInput::PhysicalInputManager& frameInputManager)
			{
				skillAliasPressedInsideMenu =
					frameInputManager.wasActionPressed(
						GameInput::InputAction::NextPage)
					&& frameInputManager.wasActionPressed(
						GameInput::InputAction::CastSkill1);
			};
			const bool menuInputBlocked = frameDriver.runAxisFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
				SDL_JOYSTICK_AXIS_MAX,
				menuSkillCallbacks);
			ok = check(skillAliasPressedInsideMenu
				&& menuInputBlocked
				&& inputManager.isActionDown(GameInput::InputAction::CastSkill1)
				&& player.nextAction == nullptr
				&& memoMenu->isControllerFocusActive(),
				"ordinary menu did not consume NextPage"
				" while isolating its RT CastSkill1 alias from the world"
				" (alias=" + std::to_string(skillAliasPressedInsideMenu)
				+ ", blocked=" + std::to_string(menuInputBlocked)
				+ ", held=" + std::to_string(inputManager.isActionDown(
					GameInput::InputAction::CastSkill1))
				+ ", queued=" + std::to_string(player.nextAction != nullptr)
				+ ", focus="
				+ std::to_string(memoMenu->isControllerFocusActive())
				+ ")") && ok;

			gameManager.menu->clearMenu();
			runProductionSemanticAndGameplayFrame(frameDriver);
			ok = check(!inputManager.isActionDown(
					GameInput::InputAction::CastSkill1)
				&& player.nextAction == nullptr,
				"leaving an ordinary menu replayed held RT as CastSkill1") && ok;
			runProductionSemanticAndGameplayFrame(frameDriver);
			ok = check(player.nextAction == nullptr,
				"held RT bypassed AwaitNeutral after leaving an ordinary menu")
				&& ok;

			frameDriver.runAxisFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
				SDL_JOYSTICK_AXIS_MIN);
			runProductionSemanticAndGameplayFrame(frameDriver);
			player.nextAction = nullptr;
			frameDriver.runAxisFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
				SDL_JOYSTICK_AXIS_MAX);
			ok = check(player.nextAction != nullptr
				&& player.nextAction->action == acMagic
				&& player.nextAction->actionParam == 0,
				"fresh RT did not restore CastSkill1 after menu neutral recovery")
				&& ok;
			frameDriver.runAxisFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
				SDL_JOYSTICK_AXIS_MIN);
		}
	}
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"world-input context-transition test initialized SDL video") && ok;
	return ok;
}

bool runMapThumbnailPhysicalLinkTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"map physical-link test started with SDL video initialized");
	{
		VirtualGamepadTest::SDLSession sdlSession;
		VirtualGamepadTest::VirtualGamepad gamepad(
			"JXQY Headless Map Physical-Link Pad");
		GamepadWorldFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& controller = *gameManager.controller;
		auto& player = *gameManager.player;
		auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();

		auto mapMenu = std::make_shared<MapThumbnailMenu>();
		gameManager.menu->mapThumbnailMenu = mapMenu;
		GamepadWorldRuntimeTestAccess::prepareHeadlessMapThumbnail(
			*mapMenu,
			{ 20, 20, 641, 361 },
			gameManager.global.data.mapName);

		auto uiRoot = std::make_shared<HeadlessGamepadUIRoot>(gameManager);
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(uiRoot);
		HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
			inputManager);
		ok = check(inputScope.isInitialized(),
			"map physical-link input manager did not initialize") && ok;
		if (inputScope.isInitialized())
		{
			std::uint64_t nowMilliseconds = SDL_GetTicks();
			HeadlessPhysicalInputTest::FrameDriver frameDriver(
				inputManager,
				nowMilliseconds,
				[]()
				{
					return dispatchPhysicalUIActions(Engine::getInstance());
				},
				[&controller]()
				{
					controller.processPhysicalInputFrame();
				});
			runProductionSemanticAndGameplayFrame(frameDriver);
			tapButtonThroughProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
				frameDriver);
			ok = check(inputManager.activeGamepadID() == gamepad.id(),
				"virtual gamepad did not claim the map physical-link channel") && ok;

			auto pendingWorldTarget = fixture.addObject(
				{ 24, 24 }, "map-open pending world target");
			GamepadWorldRuntimeTestAccess::setFocusedTarget(
				controller, pendingWorldTarget);
			const bool pendingWorldActionQueued =
				gameManager.queueObjectScriptInteraction(
					pendingWorldTarget,
					WorldInteractionScriptSide::Primary,
					true);
			ok = check(pendingWorldActionQueued
				&& player.nextAction != nullptr
				&& player.nextAction->strictWorldInteraction,
				"map-open fixture could not queue a strict world interaction") && ok;

			const bool mapOpenBlocked = tapButtonThroughProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_LEFT_STICK,
				frameDriver);
			ok = check(!mapOpenBlocked
				&& mapMenu->visible
				&& GamepadWorldRuntimeTestAccess::mapCursorActive(*mapMenu)
				&& GamepadWorldRuntimeTestAccess::mapThumbnailCapturesPointer(
					*mapMenu)
				&& GamepadWorldRuntimeTestAccess::focusedMenu(
					*gameManager.menu) == mapMenu
				&& GamepadWorldRuntimeTestAccess::focusedTarget(
					controller) == nullptr
				&& player.nextAction == nullptr,
				"L3 did not open/focus the map or cancel the pending world interaction") && ok;
			// Opening a semantic overlay is an input-context transition. Let the
			// production neutral gate observe one full idle frame before expecting
			// a fresh navigation edge from the same physical device.
			runProductionSemanticAndGameplayFrame(frameDriver);

			const Point cursorBeforeNavigation =
				GamepadWorldRuntimeTestAccess::mapCursorPixel(*mapMenu);
			bool navigationPressed = false;
			HeadlessPhysicalInputTest::FrameCallbacks navigationCallbacks;
			navigationCallbacks.afterInputUpdate =
				[&navigationPressed](
					const GameInput::PhysicalInputManager& frameInputManager)
			{
				navigationPressed = frameInputManager.wasActionPressed(
					GameInput::InputAction::NavigateDown);
			};
			const bool navigationBlocked = setButtonAndRunProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_DPAD_DOWN,
				true,
				frameDriver,
				navigationCallbacks);
			setButtonAndRunProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_DPAD_DOWN,
				false,
				frameDriver);
			const Point cursorAfterNavigation =
				GamepadWorldRuntimeTestAccess::mapCursorPixel(*mapMenu);
			ok = check(navigationPressed
				&& navigationBlocked
				&& cursorAfterNavigation.x == cursorBeforeNavigation.x
				&& cursorAfterNavigation.y > cursorBeforeNavigation.y,
				"visible map did not consume physical D-pad cursor navigation"
				" (pressed=" + std::to_string(navigationPressed)
				+ ", blocked=" + std::to_string(navigationBlocked)
				+ ", before=" + std::to_string(cursorBeforeNavigation.y)
				+ ", after=" + std::to_string(cursorAfterNavigation.y)
				+ ")") && ok;

			player.nextAction = nullptr;
			const Point cursorBeforeRightStick =
				GamepadWorldRuntimeTestAccess::mapCursorPixel(*mapMenu);
			bool scrollPressed = false;
			bool scrollRightDownBeforeDispatch = false;
			bool scrollSourceMatched = false;
			bool scrollAxisMatched = false;
			bool anotherScrollPressed = false;
			bool scrollConsumed = false;
			bool scrollStayedDownAfterDispatch = false;
			bool moveActionLeaked = false;
			bool scrollWorldStateQueued = false;
			HeadlessPhysicalInputTest::FrameCallbacks scrollCallbacks;
			scrollCallbacks.afterInputUpdate =
				[&scrollPressed,
					&scrollRightDownBeforeDispatch,
					&scrollSourceMatched,
					&scrollAxisMatched,
					&anotherScrollPressed,
					&gamepad](
					const GameInput::PhysicalInputManager& frameInputManager)
			{
				scrollPressed = frameInputManager.wasActionPressed(
					GameInput::InputAction::ScrollRight);
				scrollRightDownBeforeDispatch = frameInputManager.isActionDown(
					GameInput::InputAction::ScrollRight);
				scrollSourceMatched = frameInputManager.action(
					GameInput::InputAction::ScrollRight).sourceDeviceID
					== gamepad.id();
				scrollAxisMatched = frameInputManager.axes().rightStick.x
					> GameInput::PhysicalInputManager::UIStickPressThreshold;
				anotherScrollPressed = frameInputManager.wasActionPressed(
					GameInput::InputAction::ScrollUp)
					|| frameInputManager.wasActionPressed(
						GameInput::InputAction::ScrollDown)
					|| frameInputManager.wasActionPressed(
						GameInput::InputAction::ScrollLeft);
			};
			scrollCallbacks.afterDispatch =
				[&](bool)
			{
				scrollConsumed = !inputManager.wasActionPressed(
					GameInput::InputAction::ScrollRight);
				scrollStayedDownAfterDispatch = inputManager.isActionDown(
					GameInput::InputAction::ScrollRight);
				moveActionLeaked = inputManager.isActionDown(
					GameInput::InputAction::Move);
				scrollWorldStateQueued = player.nextAction != nullptr
					|| player.nextDest != ndNone
					|| !player.destGE.expired();
			};
			const bool scrollBlocked = frameDriver.runAxisFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_RIGHTX,
				SDL_JOYSTICK_AXIS_MAX,
				scrollCallbacks);
			const Point cursorAfterRightStick =
				GamepadWorldRuntimeTestAccess::mapCursorPixel(*mapMenu);
			bool scrollReleased = false;
			bool releaseMoveActionLeaked = false;
			HeadlessPhysicalInputTest::FrameCallbacks scrollReleaseCallbacks;
			scrollReleaseCallbacks.afterInputUpdate =
				[&scrollReleased, &releaseMoveActionLeaked](
					const GameInput::PhysicalInputManager& frameInputManager)
			{
				scrollReleased = frameInputManager.action(
						GameInput::InputAction::ScrollRight).released
					&& !frameInputManager.isActionDown(
						GameInput::InputAction::ScrollRight)
					&& !frameInputManager.wasActionPressed(
						GameInput::InputAction::ScrollRight);
				releaseMoveActionLeaked = frameInputManager.isActionDown(
					GameInput::InputAction::Move);
			};
			frameDriver.runAxisFrame(
				gamepad,
				SDL_GAMEPAD_AXIS_RIGHTX,
				0,
				scrollReleaseCallbacks);
			const Point cursorAfterRightStickRelease =
				GamepadWorldRuntimeTestAccess::mapCursorPixel(*mapMenu);
			ok = check(scrollPressed
				&& scrollRightDownBeforeDispatch
				&& scrollSourceMatched
				&& scrollAxisMatched
				&& !anotherScrollPressed
				&& scrollBlocked
				&& scrollConsumed
				&& scrollStayedDownAfterDispatch
				&& !moveActionLeaked
				&& !scrollWorldStateQueued
				&& cursorAfterRightStick.x > cursorBeforeRightStick.x
				&& cursorAfterRightStick.y == cursorBeforeRightStick.y
				&& scrollReleased
				&& !releaseMoveActionLeaked
				&& cursorAfterRightStickRelease.x == cursorAfterRightStick.x
				&& cursorAfterRightStickRelease.y == cursorAfterRightStick.y,
				"visible map did not consume physical right-stick cursor scrolling") && ok;

			const Point walkTarget = { 21, 20 };
			GamepadWorldRuntimeTestAccess::setMapCursorToTile(
				*mapMenu, walkTarget);
			player.nextAction = nullptr;
			const bool confirmBlocked = tapButtonThroughProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_SOUTH,
				frameDriver);
			ok = check(confirmBlocked
				&& player.nextAction != nullptr
				&& player.nextAction->action == acWalk,
				"map A/Confirm did not queue walking without leaking InteractPrimary"
				" (blocked=" + std::to_string(confirmBlocked)
				+ ", action=" + std::to_string(player.nextAction == nullptr
					? -1 : static_cast<int>(player.nextAction->action))
				+ ")") && ok;

			const Point runTarget = { 22, 20 };
			GamepadWorldRuntimeTestAccess::setMapCursorToTile(
				*mapMenu, runTarget);
			player.nextAction = nullptr;
			const bool secondaryBlocked = tapButtonThroughProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_WEST,
				frameDriver);
			ok = check(secondaryBlocked
				&& player.nextAction != nullptr
				&& player.nextAction->action == acRun,
				"map X/Secondary did not queue running without leaking AttackPrimary"
				" (blocked=" + std::to_string(secondaryBlocked)
				+ ", action=" + std::to_string(player.nextAction == nullptr
					? -1 : static_cast<int>(player.nextAction->action))
				+ ")") && ok;

			const bool mapCloseBlocked = tapButtonThroughProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_LEFT_STICK,
				frameDriver);
			ok = check(!mapCloseBlocked
				&& !mapMenu->visible
				&& !GamepadWorldRuntimeTestAccess::mapCursorActive(*mapMenu),
				"second L3 press did not close the map through production input") && ok;

			GamepadWorldRuntimeTestAccess::focusMemoMenu(*gameManager.menu);
			const PElement focusBeforeMap =
				GamepadWorldRuntimeTestAccess::focusedMenu(*gameManager.menu);
			ok = check(focusBeforeMap == gameManager.menu->memoMenu,
				"map focus-restore fixture did not establish the prior menu") && ok;
			gameManager.menu->setMapThumbnailVisible(true);
			ok = check(mapMenu->visible
				&& GamepadWorldRuntimeTestAccess::focusedMenu(
					*gameManager.menu) == mapMenu,
				"map did not take controller focus before physical cancel") && ok;
			// The prior L3 close left the device awaiting neutral on its return-to-
			// world edge. The first frame below both clears that gate and observes
			// this programmatic map-open edge; the second clears the new menu gate.
			runProductionSemanticAndGameplayFrame(frameDriver);
			runProductionSemanticAndGameplayFrame(frameDriver);
			player.nextAction = nullptr;
			const bool cancelBlocked = tapButtonThroughProductionFrame(
				gamepad,
				SDL_GAMEPAD_BUTTON_EAST,
				frameDriver);
			ok = check(cancelBlocked
				&& !mapMenu->visible
				&& GamepadWorldRuntimeTestAccess::focusedMenu(
					*gameManager.menu) == focusBeforeMap
				&& player.nextAction == nullptr,
				"map B/Cancel did not close, restore focus, or block CastSkill3") && ok;

			gameManager.menu->setMapThumbnailVisible(true);
			runProductionSemanticAndGameplayFrame(frameDriver);
			runProductionSemanticAndGameplayFrame(frameDriver);
			ok = check(mapMenu->isControllerFocusActive()
				&& shouldPresentGamepadFocus(Engine::getInstance()),
				"active map focus did not expose gamepad presentation before disconnect")
				&& ok;
			gamepad.detach();
			runProductionSemanticAndGameplayFrame(frameDriver);
			ok = check(!inputManager.hasActiveGamepad()
				&& mapMenu->isControllerFocusActive()
				&& GamepadWorldRuntimeTestAccess::mapCursorActive(*mapMenu)
				&& !shouldPresentGamepadFocus(Engine::getInstance()),
				"gamepad disconnect did not immediately hide map presentation"
				" while retaining valid logical focus") && ok;

			VirtualGamepadTest::VirtualGamepad reconnectedGamepad(
				"JXQY Headless Reconnected Map Pad");
			runProductionSemanticAndGameplayFrame(frameDriver);
			tapButtonThroughProductionFrame(
				reconnectedGamepad,
				SDL_GAMEPAD_BUTTON_DPAD_UP,
				frameDriver);
			ok = check(inputManager.activeGamepadID()
					== reconnectedGamepad.id()
				&& mapMenu->isControllerFocusActive()
				&& shouldPresentGamepadFocus(Engine::getInstance()),
				"map navigation did not recover from retained focus after reconnect")
				&& ok;
			gameManager.menu->setMapThumbnailVisible(false);
		}
	}
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"map physical-link test initialized SDL video") && ok;
	return ok;
}

bool runWorldInputGateTests()
{
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& controller = *gameManager.controller;
	bool ok = check(
		GamepadWorldRuntimeTestAccess::canHandleWorldInput(controller),
		"ready world state did not enable controller input");

	gameManager.global.data.canInput = false;
	ok = check(
		!GamepadWorldRuntimeTestAccess::canHandleWorldInput(controller),
		"canInput=false did not block controller world input") && ok;
	gameManager.global.data.canInput = true;

	gameManager.inEvent = true;
	ok = check(
		!GamepadWorldRuntimeTestAccess::canHandleWorldInput(controller),
		"inEvent=true did not block controller world input") && ok;
	gameManager.inEvent = false;

	gameManager.setGameplayPaused(true);
	ok = check(
		!GamepadWorldRuntimeTestAccess::canHandleWorldInput(controller),
		"gameplay pause did not block controller world input") && ok;
	gameManager.setGameplayPaused(false);

	gameManager.menu->mapThumbnailMenu =
		std::make_shared<MapThumbnailMenu>();
	gameManager.menu->mapThumbnailMenu->visible = true;
	ok = check(gameManager.menu->blocksWorldInput()
		&& !GamepadWorldRuntimeTestAccess::canHandleWorldInput(controller),
		"visible semantic menu did not block controller world input") && ok;
	gameManager.menu->mapThumbnailMenu->visible = false;

	ok = check(
		GamepadWorldRuntimeTestAccess::canHandleWorldInput(controller),
		"clearing every gate did not restore controller world input") && ok;
	return ok;
}

bool runMovementAndAttackTests()
{
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;
	auto& controller = *gameManager.controller;
	bool ok = true;

	GamepadWorldRuntimeTestAccess::move(
		controller, 1.0f, 0.0f, 0.50f, false);
	const Point expectedStep = Map::getSubPoint(PlayerPosition, 6);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acAWalk
		&& samePoint(player.nextAction->dest, expectedStep),
		"left stick walk did not queue one alternate-walk map step") && ok;

	player.nextAction = nullptr;
	GamepadWorldRuntimeTestAccess::move(
		controller, 1.0f, 0.0f, 1.0f, true);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acARun
		&& samePoint(player.nextAction->dest, expectedStep),
		"left stick run did not queue one alternate-run map step") && ok;

	auto controlledActor = fixture.addNPC(
		PlayerPosition, "controlled actor without run resources");
	controlledActor->res.walk.imagePackage = makeActionImage();
	controlledActor->res.awalk.imagePackage = makeActionImage();
	auto controlEffect = std::make_shared<Effect>();
	player.beginControlCharacter(controlledActor, controlEffect);
	NextAction touchRunAction;
	touchRunAction.action = acRun;
	touchRunAction.dest = expectedStep;
	player.nextAction = nullptr;
	ok = check(GamepadWorldRuntimeTestAccess::submitLegacyWorldAction(
			controller, touchRunAction)
		&& player.nextAction != nullptr
		&& player.nextAction->action == acWalk,
		"touch run did not use the controlled actor's walk fallback") && ok;
	NextAction virtualJoystickRunAction;
	virtualJoystickRunAction.action = acARun;
	virtualJoystickRunAction.dest = expectedStep;
	player.nextAction = nullptr;
	ok = check(GamepadWorldRuntimeTestAccess::submitLegacyWorldAction(
			controller, virtualJoystickRunAction)
		&& player.nextAction != nullptr
		&& player.nextAction->action == acAWalk,
		"virtual joystick run did not use the controlled actor's alternate-walk fallback") && ok;
	player.endControlCharacter(controlEffect.get());

	player.nextAction = nullptr;
	GameInput::GamepadAxisState axes;
	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::AttackPrimary, axes);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acAttack
		&& player.nextAction->destKind == ndNone
		&& player.nextAction->destGE.expired()
		&& samePoint(player.nextAction->dest, expectedStep),
		"attack without a hostile target did not queue the facing fallback") && ok;
	return ok;
}

bool runSkillAndJumpTests()
{
	GamepadWorldFixture fixture;
	auto& player = *fixture.gameManager.player;
	auto& controller = *fixture.gameManager.controller;
	bool ok = check(fixture.gameManager.magicManager.bottomCount() >= 5,
		"runtime magic layout does not expose five controller skill slots");

	const std::array<GameInput::InputAction, 5> skillActions =
	{
		GameInput::InputAction::CastSkill1,
		GameInput::InputAction::CastSkill2,
		GameInput::InputAction::CastSkill3,
		GameInput::InputAction::CastSkill4,
		GameInput::InputAction::CastSkill5,
	};
	GameInput::GamepadAxisState axes;
	axes.rightStick.x = 1.0f;
	axes.rightStick.magnitude = 1.0f;
	for (int skillIndex = 0;
		skillIndex < static_cast<int>(skillActions.size());
		skillIndex++)
	{
		player.nextAction = nullptr;
		GamepadWorldRuntimeTestAccess::dispatch(
			controller, skillActions[skillIndex], axes);
		ok = check(player.nextAction != nullptr
			&& player.nextAction->action == acMagic
			&& player.nextAction->actionParam == skillIndex
			&& player.nextAction->destGE.expired(),
			"controller skill action did not preserve actionParam "
				+ std::to_string(skillIndex)) && ok;
	}

	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::CastSkill1, axes);
	const Point positiveAimDestination = player.nextAction->dest;
	axes.rightStick.x = -1.0f;
	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::CastSkill1, axes);
	const Point negativeAimDestination = player.nextAction->dest;
	ok = check(!samePoint(positiveAimDestination, negativeAimDestination)
		&& player.nextAction->destGE.expired(),
		"opposite right-stick directions did not produce directional skill destinations") && ok;

	axes = {};
	axes.leftStick.x = 1.0f;
	axes.leftStick.magnitude = 1.0f;
	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::Jump, axes);
	const Point positiveJumpDestination = player.nextAction->dest;
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acJump,
		"directional jump did not queue the player jump action") && ok;
	axes.leftStick.x = -1.0f;
	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::Jump, axes);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acJump
		&& !samePoint(positiveJumpDestination, player.nextAction->dest),
		"opposite left-stick directions did not change the jump destination") && ok;
	return ok;
}

std::shared_ptr<Goods> makeScriptGoods(const std::string& name)
{
	auto goods = std::make_shared<Goods>();
	goods->name = name;
	goods->kind = gkNormal;
	goods->script = name + ".lua";
	return goods;
}

void setGoodsSlot(
	GoodsManager& manager,
	int index,
	const std::shared_ptr<Goods>& goods)
{
	manager.goodsList[index].iniFile = goods->name + ".ini";
	manager.goodsList[index].number = 1;
	manager.goodsList[index].goods = goods;
}

bool primeStrictControllerQueue(
	GamepadWorldFixture& fixture,
	const std::shared_ptr<Object>& target)
{
	auto& gameManager = fixture.gameManager;
	gameManager.player->cancelQueuedInteraction(false);
	GamepadWorldRuntimeTestAccess::setFocusedTarget(
		*gameManager.controller, target);
	return gameManager.queueObjectScriptInteraction(
		target, WorldInteractionScriptSide::Primary, true);
}

void primePendingStrictControllerMovement(
	GamepadWorldFixture& fixture,
	const std::shared_ptr<Object>& target)
{
	auto& player = *fixture.gameManager.player;
	player.cancelQueuedInteraction(false);
	player.nextDest = ndObj;
	player.nextDestUseRightScript = false;
	player.nextDestStrictWorldInteraction = true;
	player.nextDestRequestedRunning = true;
	player.destGE = target;
	GamepadWorldRuntimeTestAccess::setFocusedTarget(
		*fixture.gameManager.controller, target);
}

bool legacyWorldInputOwnsState(const GamepadWorldFixture& fixture)
{
	const auto& gameManager = fixture.gameManager;
	const auto& player = *gameManager.player;
	return GamepadWorldRuntimeTestAccess::focusedTarget(
			*gameManager.controller) == nullptr
		&& player.nextDest == ndNone
		&& !player.nextDestUseRightScript
		&& !player.nextDestStrictWorldInteraction
		&& !player.nextDestRequestedRunning
		&& player.destGE.expired()
		&& (player.nextAction == nullptr
			|| !player.nextAction->strictWorldInteraction);
}

bool strictControllerStatePreserved(
	const GamepadWorldFixture& fixture,
	const std::shared_ptr<Object>& target)
{
	const auto& gameManager = fixture.gameManager;
	const auto& player = *gameManager.player;
	return GamepadWorldRuntimeTestAccess::focusedTarget(
			*gameManager.controller) == target
		&& player.nextAction != nullptr
		&& player.nextAction->strictWorldInteraction
		&& player.nextAction->destKind == ndObj
		&& player.nextAction->destGE.lock() == target;
}

bool runWorldMenuShortcutPhysicalLinkTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"world-menu shortcut test started with SDL video initialized");
	Engine::getInstance()->setWindowSize(800, 600);
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	ResourceManager& resourceManager = ResourceManager::instance();
	if (!check(resourceManager.initialize(assetsRoot.generic_string()),
		"world-menu shortcut test initializes the production resource collection")
		|| !check(resourceManager.setActiveResourcePackById("JXQY2"),
			"world-menu shortcut test selects the JXQY2 production UI"))
	{
		return false;
	}
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY World Menu Shortcut Pad");
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& controller = *gameManager.controller;
	auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();
	auto& menuController = *gameManager.menu;
	const ResourceManifest& manifest = resourceManager.getActiveManifest();
	gameManager.global.useWav = manifest.useWav;
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.global.loadUiSettings();
	gameManager.goodsManager.configureLayout();
	gameManager.magicManager.configureLayout();
	gameManager.global.data.canInput = true;
	menuController.init();
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	ok = check(inputScope.isInitialized(),
		"world-menu shortcut input manager did not initialize") && ok;
	if (!inputScope.isInitialized())
	{
		return false;
	}

	if (!check(menuController.memoMenu != nullptr
			&& menuController.equipMenu != nullptr
			&& menuController.goodsMenu != nullptr,
		"world-menu shortcut test creates the production RPG menus"))
	{
		return false;
	}
	menuController.memoMenu->reRange(4);
	menuController.memoMenu->visible = true;
	menuController.equipMenu->visible = true;
	menuController.goodsMenu->visible = true;
	const bool productionFocusDomainsReady =
		!menuController.memoMenu->controllerFocusCandidates().empty()
		&& !menuController.equipMenu->controllerFocusCandidates().empty()
		&& !menuController.goodsMenu->controllerFocusCandidates().empty();
	menuController.memoMenu->visible = false;
	menuController.equipMenu->visible = false;
	menuController.goodsMenu->visible = false;
	if (!check(productionFocusDomainsReady,
		"world-menu shortcut test creates the production RPG menu focus domains"))
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	runControllerInputFrame(controller, inputManager, nowMilliseconds);
	setButtonAndRunControllerFrame(
		gamepad, SDL_GAMEPAD_BUTTON_MISC1, true,
		controller, inputManager, nowMilliseconds);
	setButtonAndRunControllerFrame(
		gamepad, SDL_GAMEPAD_BUTTON_MISC1, false,
		controller, inputManager, nowMilliseconds);
	ok = check(inputManager.activeGamepadID() == gamepad.id(),
		"world-menu shortcut gamepad did not claim the input channel") && ok;

	auto strictTarget = fixture.addObject(
		{ 21, 20 }, "world-menu strict target");
	struct MenuShortcutCase
	{
		const char* name;
		SDL_GamepadButton directionButton;
		GameInput::InputAction shortcutAction;
		GameInput::InputAction forbiddenNavigationAction;
		PElement menu;
		void (MenuController::*toggleMenu)();
	};
	const std::array<MenuShortcutCase, 3> shortcutCases =
	{
		MenuShortcutCase{
			"Memo", SDL_GAMEPAD_BUTTON_DPAD_DOWN,
			GameInput::InputAction::OpenMemo,
			GameInput::InputAction::NavigateDown,
			menuController.memoMenu,
			&MenuController::toggleMemoView },
		MenuShortcutCase{
			"Equip", SDL_GAMEPAD_BUTTON_DPAD_LEFT,
			GameInput::InputAction::OpenEquip,
			GameInput::InputAction::NavigateLeft,
			menuController.equipMenu,
			&MenuController::toggleEquipView },
		MenuShortcutCase{
			"Goods", SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
			GameInput::InputAction::OpenGoods,
			GameInput::InputAction::NavigateRight,
			menuController.goodsMenu,
			&MenuController::toggleGoodsView },
	};
	struct MenuShortcutObservation
	{
		GameInput::InputActionState shortcutState;
		bool navigationLeaked = false;
		bool shortcutEdgeConsumed = false;
		bool strictStateClearedAfterDispatch = false;
		bool strictStatePreservedAfterDispatch = false;
		bool shoulderAliasLeaked = false;
	};
	auto runShortcutChord = [&](const MenuShortcutCase& shortcutCase)
	{
		setButtonAndRunControllerFrame(
			gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true,
			controller, inputManager, nowMilliseconds);

		gamepad.setButton(shortcutCase.directionButton, true);
		nowMilliseconds += 10;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
		MenuShortcutObservation observation;
		observation.shortcutState =
			inputManager.action(shortcutCase.shortcutAction);
		observation.navigationLeaked = inputManager.wasActionPressed(
			shortcutCase.forbiddenNavigationAction)
			|| inputManager.isActionDown(
				shortcutCase.forbiddenNavigationAction);
		controller.processPhysicalInputFrame();
		observation.shortcutEdgeConsumed = !inputManager.wasActionPressed(
			shortcutCase.shortcutAction);
		observation.strictStateClearedAfterDispatch =
			legacyWorldInputOwnsState(fixture);
		observation.strictStatePreservedAfterDispatch =
			strictControllerStatePreserved(fixture, strictTarget);

		setButtonAndRunControllerFrame(
			gamepad, shortcutCase.directionButton, false,
			controller, inputManager, nowMilliseconds);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
		nowMilliseconds += 10;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
		observation.shoulderAliasLeaked = inputManager.wasActionPressed(
			GameInput::InputAction::InteractAlternate)
			|| inputManager.wasActionPressed(
				GameInput::InputAction::NextPanel);
		controller.processPhysicalInputFrame();
		return observation;
	};

	for (const MenuShortcutCase& shortcutCase : shortcutCases)
	{
		ok = check(primeStrictControllerQueue(fixture, strictTarget),
			std::string(shortcutCase.name)
				+ " shortcut could not prime a strict world interaction") && ok;
		const MenuShortcutObservation observation =
			runShortcutChord(shortcutCase);
		ok = check(observation.shortcutState.pressed
			&& observation.shortcutState.sourceDeviceID == gamepad.id()
			&& !observation.navigationLeaked
			&& observation.shortcutEdgeConsumed
			&& shortcutCase.menu->visible
			&& GamepadWorldRuntimeTestAccess::focusedMenu(menuController)
				== shortcutCase.menu
			&& menuController.blocksWorldInput()
			&& observation.strictStateClearedAfterDispatch,
			std::string("RB+") + shortcutCase.name
				+ " did not open its menu, isolate navigation, or clear strict world state") && ok;
		ok = check(!observation.shoulderAliasLeaked
			&& gameManager.player->nextAction == nullptr,
			std::string("RB+") + shortcutCase.name
				+ " leaked a shoulder-release alias") && ok;

		(menuController.*shortcutCase.toggleMenu)();
		ok = check(!shortcutCase.menu->visible,
			std::string(shortcutCase.name)
				+ " shortcut fixture could not close its menu") && ok;
		// Leaving a menu is an input-context transition. One neutral frame
		// releases the old context and the next clears AwaitNeutral.
		runControllerInputFrame(controller, inputManager, nowMilliseconds);
		runControllerInputFrame(controller, inputManager, nowMilliseconds);
	}

	auto memoMenu = menuController.memoMenu;
	menuController.memoMenu.reset();
	ok = check(primeStrictControllerQueue(fixture, strictTarget),
		"missing-menu shortcut could not prime strict world state") && ok;
	const MenuShortcutObservation missingMemoObservation =
		runShortcutChord(shortcutCases[0]);
	ok = check(missingMemoObservation.shortcutState.pressed
		&& !missingMemoObservation.navigationLeaked
		&& missingMemoObservation.shortcutEdgeConsumed
		&& !missingMemoObservation.shoulderAliasLeaked
		&& missingMemoObservation.strictStatePreservedAfterDispatch,
		"missing Memo consumed or leaked the chord instead of preserving strict world state") && ok;
	menuController.memoMenu = memoMenu;
	gameManager.player->cancelQueuedInteraction(false);
	GamepadWorldRuntimeTestAccess::setFocusedTarget(controller, nullptr);

	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"world-menu shortcut test initialized SDL video") && ok;
	return ok;
}

bool runLegacyWorldInputCompatibilityTests()
{
	VirtualGamepadTest::SDLSession sdlSession;
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& controller = *gameManager.controller;
	auto& player = *gameManager.player;
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"legacy world-input fixture initialized SDL video");

	auto controllerTarget = fixture.addObject(
		{ 21, 20 }, "controller focus");
	controllerTarget->scriptFileRight = "controller_focus_right.lua";
	auto pointerObject = fixture.addObject(
		{ 23, 20 }, "pointer object");
	pointerObject->scriptFileRight = "pointer_object_right.lua";
	auto pointerNPC = fixture.addHeadlessPointerNPC(
		{ 22, 22 }, "pointer npc");
	attachHeadlessWorldPointerTarget(
		gameManager.npcManager, pointerNPC, { 0, 0, 6, 6 });

	primePendingStrictControllerMovement(fixture, controllerTarget);
	GamepadWorldRuntimeTestAccess::moveWithLegacyKeyboard(
		controller, false, false, true, false, true);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->action == acARun
		&& player.nextAction->destKind == ndNone
		&& legacyWorldInputOwnsState(fixture),
		"legacy keyboard movement did not replace strict controller state") && ok;

	gameManager.map->data->tile[PlayerPosition.y][PlayerPosition.x - 1].obstacle =
		toObstacle;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"blocked movement precondition could not queue controller interaction") && ok;
	GamepadWorldRuntimeTestAccess::moveWithLegacyKeyboard(
		controller, false, false, true, false, true);
	ok = check(strictControllerStatePreserved(fixture, controllerTarget),
		"blocked legacy movement discarded strict controller state") && ok;
	NextAction blockedPointerMovement;
	blockedPointerMovement.action = acWalk;
	blockedPointerMovement.dest = { PlayerPosition.x - 1, PlayerPosition.y };
	ok = check(!GamepadWorldRuntimeTestAccess::submitLegacyWorldAction(
			controller, blockedPointerMovement)
		&& strictControllerStatePreserved(fixture, controllerTarget),
		"blocked legacy pointer movement discarded strict controller state") && ok;
	gameManager.map->data->tile[PlayerPosition.y][PlayerPosition.x - 1].obstacle = 0;

	const Point reservedStep =
		{ PlayerPosition.x - 1, PlayerPosition.y };
	player.forceBeginStand();
	player.beginWalk(reservedStep);
	auto actionActor = player.getActionActor();
	bool actorReservedStep = false;
	for (const auto& stepNPC : gameManager.map->dataMap.tile[
		reservedStep.y][reservedStep.x].stepNPCList)
	{
		if (stepNPC == actionActor)
		{
			actorReservedStep = true;
			break;
		}
	}
	ok = check(player.isWalking() && actorReservedStep,
		"actor-aware takeover fixture did not reserve the current walk step") && ok;
	primePendingStrictControllerMovement(fixture, controllerTarget);
	NextAction reservedStepTakeover;
	reservedStepTakeover.action = acAWalk;
	reservedStepTakeover.dest = reservedStep;
	ok = check(GamepadWorldRuntimeTestAccess::submitLegacyWorldAction(
			controller, reservedStepTakeover)
		&& player.nextAction != nullptr
		&& player.nextAction->action == acAWalk
		&& legacyWorldInputOwnsState(fixture),
		"legacy movement did not take over the actor's own reserved step") && ok;
	gameManager.map->dataMap.tile[
		reservedStep.y][reservedStep.x].stepNPCList.remove(actionActor);

	player.forceBeginStand();
	auto blockingNPC = std::make_shared<NPC>();
	blockingNPC->kind = nkNormal;
	blockingNPC->relation = nrFriendly;
	blockingNPC->setPosition(reservedStep);
	gameManager.map->dataMap.tile[
		reservedStep.y][reservedStep.x].stepNPCList.push_back(blockingNPC);
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"other-actor blocker precondition could not queue controller interaction") && ok;
	NextAction otherActorBlockedMovement;
	otherActorBlockedMovement.action = acWalk;
	otherActorBlockedMovement.dest = reservedStep;
	ok = check(!GamepadWorldRuntimeTestAccess::submitLegacyWorldAction(
			controller, otherActorBlockedMovement)
		&& strictControllerStatePreserved(fixture, controllerTarget),
		"other actor's reserved step did not preserve strict controller state") && ok;
	gameManager.map->dataMap.tile[
		reservedStep.y][reservedStep.x].stepNPCList.remove(blockingNPC);

	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"Q compatibility precondition could not queue controller interaction") && ok;
	AEvent objectKey(ET_KEYDOWN, KEY_Q, 0, 0, false);
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, objectKey)
		&& player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == controllerTarget
		&& player.nextAction->destKind == ndObj
		&& !player.nextAction->strictWorldInteraction
		&& legacyWorldInputOwnsState(fixture),
		"legacy Q object interaction changed semantics or retained controller state") && ok;

	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"E compatibility precondition could not queue controller interaction") && ok;
	AEvent npcKey(ET_KEYDOWN, KEY_E, 0, 0, false);
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, npcKey)
		&& player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == pointerNPC
		&& player.nextAction->destKind == ndTalk
		&& !player.nextAction->strictWorldInteraction
		&& legacyWorldInputOwnsState(fixture),
		"legacy E NPC interaction changed semantics or retained controller state") && ok;

	controllerTarget->setPosition({ 47, 47 });
	pointerObject->setPosition({ 47, 45 });
	pointerNPC->setPosition({ 45, 47 });
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"empty Q precondition could not queue controller interaction") && ok;
	ok = check(!GamepadWorldRuntimeTestAccess::handleLegacyEvent(
			controller, objectKey)
		&& strictControllerStatePreserved(fixture, controllerTarget),
		"empty legacy Q discarded strict controller state") && ok;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"empty E precondition could not queue controller interaction") && ok;
	ok = check(!GamepadWorldRuntimeTestAccess::handleLegacyEvent(
			controller, npcKey)
		&& strictControllerStatePreserved(fixture, controllerTarget),
		"empty legacy E discarded strict controller state") && ok;
	controllerTarget->setPosition({ 21, 20 });
	pointerObject->setPosition({ 23, 20 });
	pointerNPC->setPosition({ 22, 22 });

	gameManager.npcManager->clickIndex = 0;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"skill compatibility precondition could not queue controller interaction") && ok;
	AEvent skillKey(ET_KEYDOWN, KEY_A, 0, 0, false);
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, skillKey)
		&& player.nextAction != nullptr
		&& player.nextAction->action == acMagic
		&& player.nextAction->actionParam == 0
		&& player.nextAction->destGE.lock() == pointerNPC
		&& legacyWorldInputOwnsState(fixture),
		"legacy keyboard skill changed targeting or retained controller state") && ok;

	auto quickItem = makeScriptGoods("legacy_keyboard_quick_item");
	setGoodsSlot(gameManager.goodsManager,
		gameManager.goodsManager.bottomBegin(), quickItem);
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"quick-item compatibility precondition could not queue controller interaction") && ok;
	gameManager.inEvent = true;
	const std::size_t taskCountBeforeItem = gameManager.scriptTaskList.size();
	AEvent itemKey(ET_KEYDOWN, KEY_Z, 0, 0, false);
	const bool itemHandled = GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, itemKey);
	gameManager.inEvent = false;
	ok = check(itemHandled
		&& gameManager.scriptTaskList.size() == taskCountBeforeItem + 1
		&& gameManager.scriptTaskList.back().goods == quickItem
		&& legacyWorldInputOwnsState(fixture),
		"legacy keyboard quick item did not run or retained controller state") && ok;

	gameManager.goodsManager.goodsList[
		gameManager.goodsManager.bottomBegin()].clear();
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"empty quick-item precondition could not queue controller interaction") && ok;
	const std::size_t taskCountBeforeEmptyItem =
		gameManager.scriptTaskList.size();
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
			controller, itemKey)
		&& gameManager.scriptTaskList.size() == taskCountBeforeEmptyItem
		&& strictControllerStatePreserved(fixture, controllerTarget),
		"empty legacy quick item discarded strict controller state") && ok;

	auto memoMenu = std::make_shared<MemoMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMemoFocus(*memoMenu);
	gameManager.menu->memoMenu = memoMenu;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"menu-open precondition could not queue controller interaction") && ok;
	gameManager.menu->toggleMemoView();
	ok = check(memoMenu->visible
		&& GamepadWorldRuntimeTestAccess::focusedMenu(
			*gameManager.menu) == memoMenu
		&& legacyWorldInputOwnsState(fixture),
		"successful menu opening did not cancel strict controller state") && ok;
	gameManager.menu->toggleMemoView();
	gameManager.menu->memoMenu = nullptr;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"failed menu-open precondition could not queue controller interaction") && ok;
	gameManager.menu->toggleMemoView();
	ok = check(strictControllerStatePreserved(fixture, controllerTarget),
		"failed menu opening discarded strict controller state") && ok;

	auto ordinaryMenu = std::make_shared<MemoMenu>();
	GamepadWorldRuntimeTestAccess::prepareHeadlessMemoFocus(*ordinaryMenu);
	ordinaryMenu->visible = true;
	gameManager.menu->memoMenu = ordinaryMenu;
	GamepadWorldRuntimeTestAccess::focusMemoMenu(*gameManager.menu);
	ok = check(gameManager.menu->blocksWorldInput()
		&& !gameManager.menu->blocksWorldKeyboardInput()
		&& !gameManager.menu->blocksWorldPointerInput(),
		"ordinary RPG menu did not preserve its semantic barrier and world-pointer pass-through") && ok;
	AEvent ordinarySkillKey(ET_KEYDOWN, KEY_S, 0, 0, false);
	ok = check(!gameManager.menu->onHandleEvent(ordinarySkillKey),
		"ordinary RPG menu consumed a legacy skill key as menu navigation") && ok;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"ordinary-menu keyboard skill precondition could not queue controller interaction") && ok;
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
			controller, ordinarySkillKey)
		&& player.nextAction != nullptr
		&& player.nextAction->action == acMagic
		&& player.nextAction->actionParam == 1
		&& legacyWorldInputOwnsState(fixture),
		"ordinary RPG menu blocked the legacy keyboard skill path") && ok;

	setGoodsSlot(gameManager.goodsManager,
		gameManager.goodsManager.bottomBegin(), quickItem);
	gameManager.inEvent = true;
	const std::size_t ordinaryMenuItemTaskCount =
		gameManager.scriptTaskList.size();
	const bool ordinaryMenuItemHandled =
		GamepadWorldRuntimeTestAccess::handleLegacyEvent(controller, itemKey);
	gameManager.inEvent = false;
	ok = check(ordinaryMenuItemHandled
		&& gameManager.scriptTaskList.size() == ordinaryMenuItemTaskCount + 1
		&& gameManager.scriptTaskList.back().goods == quickItem,
		"ordinary RPG menu blocked the legacy keyboard quick-item path") && ok;
	gameManager.goodsManager.goodsList[
		gameManager.goodsManager.bottomBegin()].clear();

	player.forceBeginStand();
	AEvent ordinarySitKey(ET_KEYDOWN, KEY_V, 0, 0, false);
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
			controller, ordinarySitKey)
		&& player.isSitting(),
		"ordinary RPG menu blocked the legacy keyboard sit path") && ok;
	player.forceBeginStand();

	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"mouse NPC compatibility precondition could not queue controller interaction") && ok;
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->clickIndex = -1;
	AEvent npcClick(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 4, 4, false);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(controller, npcClick);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == pointerNPC
		&& player.nextAction->destKind == ndTalk
		&& !player.nextAction->useRightScript
		&& legacyWorldInputOwnsState(fixture),
		"ordinary-menu mouse NPC click changed semantics or retained controller state") && ok;

	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"mouse Object compatibility precondition could not queue controller interaction") && ok;
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = 1;
	AEvent objectClick(ET_MOUSEDOWN, MBC_MOUSE_RIGHT, 8, 8, false);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(controller, objectClick);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == pointerObject
		&& player.nextAction->destKind == ndObj
		&& player.nextAction->useRightScript
		&& legacyWorldInputOwnsState(fixture),
		"ordinary-menu mouse Object click changed semantics or retained controller state") && ok;

	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"primary mouse Object compatibility precondition could not queue controller interaction") && ok;
	AEvent primaryObjectClick(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 8, 8, false);
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, primaryObjectClick);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == pointerObject
		&& player.nextAction->destKind == ndObj
		&& !player.nextAction->useRightScript
		&& legacyWorldInputOwnsState(fixture),
		"ordinary-menu primary mouse Object click did not preserve legacy interaction") && ok;

	ordinaryMenu->visible = false;
	auto checkModalPointerBarrier = [&](const std::string& modalName)
	{
		return check(gameManager.menu->blocksWorldInput()
			&& gameManager.menu->blocksWorldKeyboardInput()
			&& gameManager.menu->blocksWorldPointerInput(),
			modalName + " did not retain semantic, keyboard, and pointer barriers");
	};

	auto modalDialog = std::make_shared<Dialog>();
	modalDialog->visible = true;
	gameManager.menu->dialog = modalDialog;
	ok = checkModalPointerBarrier("modal dialog") && ok;
	gameManager.menu->dialog = nullptr;

	auto modalChoose = std::make_shared<ChooseMenu>();
	modalChoose->visible = true;
	gameManager.menu->chooseMenu = modalChoose;
	ok = checkModalPointerBarrier("modal choice") && ok;
	gameManager.menu->chooseMenu = nullptr;

	auto modalBuySell = std::make_shared<BuySellMenu>();
	modalBuySell->visible = true;
	gameManager.menu->buySellMenu = modalBuySell;
	ok = checkModalPointerBarrier("modal buy/sell menu") && ok;
	gameManager.menu->buySellMenu = nullptr;

	auto modalGamble = std::make_shared<GambleMenu>();
	modalGamble->visible = true;
	gameManager.menu->gambleMenu = modalGamble;
	ok = checkModalPointerBarrier("modal gamble menu") && ok;
	gameManager.menu->gambleMenu = nullptr;

	auto modalPartnerEquip = std::make_shared<PartnerEquipMenu>();
	modalPartnerEquip->visible = true;
	gameManager.menu->partnerEquipMenu = modalPartnerEquip;
	ok = checkModalPointerBarrier("modal partner equipment") && ok;
	gameManager.menu->partnerEquipMenu = nullptr;

	auto nonModalMap = std::make_shared<MapThumbnailMenu>();
	nonModalMap->visible = true;
	gameManager.menu->mapThumbnailMenu = nonModalMap;
	ok = check(gameManager.menu->blocksWorldInput()
		&& !gameManager.menu->blocksWorldKeyboardInput()
		&& !gameManager.menu->blocksWorldPointerInput(),
		"map thumbnail did not retain semantic navigation ownership"
		" with world-pointer pass-through") && ok;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"map pointer pass-through precondition could not queue controller interaction") && ok;
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->clickIndex = -1;
	GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, npcClick);
	ok = check(player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == pointerNPC
		&& legacyWorldInputOwnsState(fixture),
		"map thumbnail swallowed an outside mouse NPC interaction") && ok;
	nonModalMap->visible = false;
	gameManager.menu->mapThumbnailMenu = nullptr;
	gameManager.menu->memoMenu = nullptr;

#ifdef __MOBILE__
	auto mobileOrdinaryMenu = std::make_shared<MemoMenu>();
	mobileOrdinaryMenu->visible = true;
	gameManager.menu->memoMenu = mobileOrdinaryMenu;
	auto touchObject = fixture.addObject(
		{ 24, 20 }, "touch object", "touch_object.lua");
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"touch Object compatibility precondition could not queue controller interaction") && ok;
	GamepadWorldRuntimeTestAccess::submitLegacyMobileTouch(
		*touchObject, 12, 12);
	bool objectTouchOK = check(player.nextAction != nullptr,
		"legacy mobile Object touch did not queue an action");
	objectTouchOK = check(player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == touchObject,
		"legacy mobile Object touch changed the queued target") && objectTouchOK;
	objectTouchOK = check(player.nextAction != nullptr
			&& player.nextAction->destKind == ndObj
			&& !player.nextAction->useRightScript,
		"legacy mobile Object touch changed the interaction side") && objectTouchOK;
	objectTouchOK = check(legacyWorldInputOwnsState(fixture),
		"legacy mobile Object touch retained controller state") && objectTouchOK;
	ok = objectTouchOK && ok;

	auto touchNPC = fixture.addNPC(
		{ 24, 22 }, "touch npc", "touch_npc.lua");
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"touch NPC compatibility precondition could not queue controller interaction") && ok;
	GamepadWorldRuntimeTestAccess::submitLegacyMobileTouch(
		*touchNPC, 32, 12);
	bool npcTouchOK = check(player.nextAction != nullptr,
		"legacy mobile NPC touch did not queue an action");
	npcTouchOK = check(player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == touchNPC,
		"legacy mobile NPC touch changed the queued target") && npcTouchOK;
	npcTouchOK = check(player.nextAction != nullptr
			&& player.nextAction->destKind == ndTalk
			&& !player.nextAction->useRightScript,
		"legacy mobile NPC touch changed the interaction side") && npcTouchOK;
	npcTouchOK = check(legacyWorldInputOwnsState(fixture),
		"legacy mobile NPC touch retained controller state") && npcTouchOK;
	ok = npcTouchOK && ok;

	mobileOrdinaryMenu->visible = false;
	auto mobileNonModalMap = std::make_shared<MapThumbnailMenu>();
	mobileNonModalMap->visible = true;
	gameManager.menu->mapThumbnailMenu = mobileNonModalMap;
	player.cancelQueuedInteraction(false);
	const bool mapObjectDownHandled =
		GamepadWorldRuntimeTestAccess::dispatchMobileChildPointerDownBeforeParent(
			*touchObject, controller, 701, 12, 12);
	ok = check(!mapObjectDownHandled && player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == touchObject,
		"mobile map swallowed an outside Object pointer-down or the parent"
		" consumed the child-owned event again") && ok;

	player.cancelQueuedInteraction(false);
	const bool mapNPCDownHandled =
		GamepadWorldRuntimeTestAccess::dispatchMobileChildPointerDownBeforeParent(
			*touchNPC, controller, 702, 32, 12);
	ok = check(!mapNPCDownHandled && player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == touchNPC,
		"mobile map swallowed an outside NPC pointer-down or the parent"
		" consumed the child-owned event again") && ok;

	touchObject->scriptFileRight = "touch_object_right.lua";
	player.cancelQueuedInteraction(false);
	const bool mapObjectUpHandled =
		GamepadWorldRuntimeTestAccess::dispatchMobileChildPointerUpBeforeParent(
			*touchObject, controller, 703, 12, 12);
	ok = check(!mapObjectUpHandled && player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == touchObject
			&& player.nextAction->useRightScript,
		"mobile map swallowed an outside Object pointer-up or the parent"
		" consumed the child-owned event again") && ok;

	touchNPC->scriptFileRight = "touch_npc_right.lua";
	player.cancelQueuedInteraction(false);
	const bool mapNPCUpHandled =
		GamepadWorldRuntimeTestAccess::dispatchMobileChildPointerUpBeforeParent(
			*touchNPC, controller, 704, 32, 12);
	ok = check(!mapNPCUpHandled && player.nextAction != nullptr
			&& player.nextAction->destGE.lock() == touchNPC
			&& player.nextAction->useRightScript,
		"mobile map swallowed an outside NPC pointer-up or the parent"
		" consumed the child-owned event again") && ok;
	mobileNonModalMap->visible = false;
	gameManager.menu->mapThumbnailMenu = nullptr;
	gameManager.menu->memoMenu = nullptr;
#endif

	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = -1;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"sit compatibility precondition could not queue controller interaction") && ok;
	AEvent sitKey(ET_KEYDOWN, KEY_V, 0, 0, false);
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
		controller, sitKey)
		&& player.isSitting()
		&& legacyWorldInputOwnsState(fixture),
		"legacy sit key did not preserve sit behavior or clear controller state") && ok;

	player.forceBeginStand();
	player.immobilized = true;
	ok = check(primeStrictControllerQueue(fixture, controllerTarget),
		"blocked sit precondition could not queue controller interaction") && ok;
	ok = check(GamepadWorldRuntimeTestAccess::handleLegacyEvent(
			controller, sitKey)
		&& !player.isSitting()
		&& strictControllerStatePreserved(fixture, controllerTarget),
		"blocked legacy sit discarded strict controller state") && ok;
	player.immobilized = false;

	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"legacy world-input tests initialized SDL video") && ok;
	return ok;
}

bool runQuickItemAndSitTests()
{
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;
	auto& controller = *gameManager.controller;
	auto& goodsManager = gameManager.goodsManager;
	bool ok = check(goodsManager.bottomCount() >= 3,
		"runtime goods layout does not expose three controller quick slots");

	const std::array<GameInput::InputAction, 3> quickItemActions =
	{
		GameInput::InputAction::UseQuickItem1,
		GameInput::InputAction::UseQuickItem2,
		GameInput::InputAction::UseQuickItem3,
	};
	std::array<std::shared_ptr<Goods>, 3> expectedGoods;
	for (int slotIndex = 0;
		slotIndex < static_cast<int>(quickItemActions.size());
		slotIndex++)
	{
		expectedGoods[slotIndex] = makeScriptGoods(
			"controller_quick_item_" + std::to_string(slotIndex + 1));
		setGoodsSlot(goodsManager,
			goodsManager.bottomBegin() + slotIndex,
			expectedGoods[slotIndex]);

		if (slotIndex < goodsManager.listLength()
			&& slotIndex != goodsManager.bottomBegin() + slotIndex)
		{
			setGoodsSlot(goodsManager, slotIndex,
				makeScriptGoods("wrong_store_slot_"
					+ std::to_string(slotIndex + 1)));
		}
	}

	gameManager.inEvent = true;
	GameInput::GamepadAxisState axes;
	for (int slotIndex = 0;
		slotIndex < static_cast<int>(quickItemActions.size());
		slotIndex++)
	{
		GamepadWorldRuntimeTestAccess::dispatch(
			controller, quickItemActions[slotIndex], axes);
		ok = check(gameManager.scriptTaskList.size()
				== static_cast<std::size_t>(slotIndex + 1)
			&& gameManager.scriptTaskList.back().type == stGoods
			&& gameManager.scriptTaskList.back().goods
				== expectedGoods[slotIndex],
			"quick item action did not call GoodsManager with bottom slot "
				+ std::to_string(slotIndex)) && ok;
	}
	gameManager.inEvent = false;

	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::ToggleSit, axes);
	ok = check(player.isSitting(),
		"ToggleSit did not enter the real player sit action") && ok;
	GamepadWorldRuntimeTestAccess::dispatch(
		controller, GameInput::InputAction::ToggleSit, axes);
	ok = check(player.isStanding(),
		"second ToggleSit did not return the player to standing") && ok;
	return ok;
}

bool runGP03PhysicalLinkTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"GP03 physical-link test started with SDL video initialized");
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY GP03 World Actions Pad");
	GamepadWorldFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& controller = *gameManager.controller;
	auto& player = *gameManager.player;
	auto& goodsManager = gameManager.goodsManager;
	auto& inputManager = GamepadWorldRuntimeTestAccess::inputManager();
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	ok = check(inputScope.isInitialized(),
		"GP03 production physical input manager did not initialize") && ok;
	if (!inputScope.isInitialized())
	{
		return false;
	}
	ok = check(gameManager.magicManager.bottomCount() >= 5,
		"GP03 physical-link fixture lacks five skill slots") && ok;
	ok = check(goodsManager.bottomCount() >= 3,
		"GP03 physical-link fixture lacks three quick-item slots") && ok;
	if (gameManager.magicManager.bottomCount() < 5
		|| goodsManager.bottomCount() < 3)
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	auto updateButton = [&](SDL_GamepadButton button, bool down)
	{
		gamepad.setButton(button, down);
		nowMilliseconds += 10;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	};
	auto updateAxis = [&](SDL_GamepadAxis axis, Sint16 value)
	{
		gamepad.setAxis(axis, value);
		nowMilliseconds += 10;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	};
	auto dispatchFrame = [&]()
	{
		controller.processPhysicalInputFrame();
	};

	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	dispatchFrame();
	// MISC1 is deliberately unmapped: it claims the device without creating a
	// gameplay action that could contaminate the first mapping assertion.
	updateButton(SDL_GAMEPAD_BUTTON_MISC1, true);
	dispatchFrame();
	updateButton(SDL_GAMEPAD_BUTTON_MISC1, false);
	dispatchFrame();
	ok = check(inputManager.activeGamepadID() == gamepad.id(),
		"GP03 virtual gamepad did not claim the production input channel") && ok;

	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	dispatchFrame();

	player.nextAction = nullptr;
	updateAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
	const GameInput::InputActionState triggerSkillState =
		inputManager.action(GameInput::InputAction::CastSkill1);
	dispatchFrame();
	ok = check(triggerSkillState.pressed
		&& triggerSkillState.sourceDeviceID == gamepad.id()
		&& triggerSkillState.axis.rightStick.x > 0.9f
		&& triggerSkillState.axis.rightTrigger
			>= GameInput::PhysicalInputManager::TriggerPressThreshold
		&& player.nextAction != nullptr
		&& player.nextAction->action == acMagic
		&& player.nextAction->actionParam == 0,
		"RT skill 1 did not preserve its physical source, aim snapshot, or slot")
		&& ok;
	updateAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
	dispatchFrame();

	struct DirectSkillCase
	{
		SDL_GamepadButton button;
		GameInput::InputAction action;
		int skillIndex;
	};
	const std::array<DirectSkillCase, 2> directSkillCases =
	{
		DirectSkillCase{ SDL_GAMEPAD_BUTTON_NORTH,
			GameInput::InputAction::CastSkill2, 1 },
		DirectSkillCase{ SDL_GAMEPAD_BUTTON_EAST,
			GameInput::InputAction::CastSkill3, 2 },
	};
	for (const DirectSkillCase& skillCase : directSkillCases)
	{
		player.nextAction = nullptr;
		updateButton(skillCase.button, true);
		const GameInput::InputActionState actionState =
			inputManager.action(skillCase.action);
		dispatchFrame();
		ok = check(actionState.pressed
			&& actionState.sourceDeviceID == gamepad.id()
			&& actionState.axis.rightStick.x > 0.9f
			&& player.nextAction != nullptr
			&& player.nextAction->action == acMagic
			&& player.nextAction->actionParam == skillCase.skillIndex,
			"direct skill did not preserve its physical source, aim snapshot, or slot "
				+ std::to_string(skillCase.skillIndex)) && ok;
		updateButton(skillCase.button, false);
		dispatchFrame();
	}

	struct ChordSkillCase
	{
		SDL_GamepadButton button;
		GameInput::InputAction action;
		GameInput::InputAction forbiddenAction;
		int skillIndex;
	};
	const std::array<ChordSkillCase, 2> chordSkillCases =
	{
		ChordSkillCase{ SDL_GAMEPAD_BUTTON_SOUTH,
			GameInput::InputAction::CastSkill4,
			GameInput::InputAction::InteractPrimary, 3 },
		ChordSkillCase{ SDL_GAMEPAD_BUTTON_EAST,
			GameInput::InputAction::CastSkill5,
			GameInput::InputAction::CastSkill3, 4 },
	};
	for (const ChordSkillCase& skillCase : chordSkillCases)
	{
		player.nextAction = nullptr;
		updateButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
		dispatchFrame();
		updateButton(skillCase.button, true);
		const GameInput::InputActionState actionState =
			inputManager.action(skillCase.action);
		const bool leakedUnmodifiedAction =
			inputManager.wasActionPressed(skillCase.forbiddenAction)
			|| inputManager.isActionDown(skillCase.forbiddenAction);
		dispatchFrame();
		ok = check(actionState.pressed
			&& actionState.sourceDeviceID == gamepad.id()
			&& actionState.axis.rightStick.x > 0.9f
			&& !leakedUnmodifiedAction
			&& player.nextAction != nullptr
			&& player.nextAction->action == acMagic
			&& player.nextAction->actionParam == skillCase.skillIndex,
			"RB skill chord leaked its base action or selected the wrong slot "
				+ std::to_string(skillCase.skillIndex)) && ok;
		updateButton(skillCase.button, false);
		dispatchFrame();
		updateButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
		const bool leakedShoulderRelease =
			inputManager.wasActionPressed(
				GameInput::InputAction::InteractAlternate)
			|| inputManager.wasActionPressed(
				GameInput::InputAction::NextPanel);
		dispatchFrame();
		ok = check(!leakedShoulderRelease,
			"RB skill chord leaked a shoulder release alias") && ok;
	}

	gamepad.setAxis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	dispatchFrame();
	for (int slotIndex = 0; slotIndex < 3; slotIndex++)
	{
		auto& slot = goodsManager.goodsList[
			goodsManager.bottomBegin() + slotIndex];
		slot.clear();
		slot.iniFile = "gp03_invalid_quick_slot_"
			+ std::to_string(slotIndex) + ".ini";
		slot.number = 1;
		// A missing Goods object makes useItem clear only the addressed slot and
		// return before script, sound, or menu-update paths are reachable.
		slot.goods.reset();
	}
	struct QuickItemChordCase
	{
		SDL_GamepadButton button;
		GameInput::InputAction action;
		GameInput::InputAction forbiddenAction;
		int slotIndex;
	};
	const std::array<QuickItemChordCase, 3> quickItemCases =
	{
		QuickItemChordCase{ SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
			GameInput::InputAction::UseQuickItem1,
			GameInput::InputAction::Jump, 0 },
		QuickItemChordCase{ SDL_GAMEPAD_BUTTON_WEST,
			GameInput::InputAction::UseQuickItem2,
			GameInput::InputAction::AttackPrimary, 1 },
		QuickItemChordCase{ SDL_GAMEPAD_BUTTON_NORTH,
			GameInput::InputAction::UseQuickItem3,
			GameInput::InputAction::CastSkill2, 2 },
	};
	for (const QuickItemChordCase& quickItemCase : quickItemCases)
	{
		updateButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
		dispatchFrame();
		updateButton(quickItemCase.button, true);
		const GameInput::InputActionState actionState =
			inputManager.action(quickItemCase.action);
		const bool leakedUnmodifiedAction =
			inputManager.wasActionPressed(quickItemCase.forbiddenAction)
			|| inputManager.isActionDown(quickItemCase.forbiddenAction);
		dispatchFrame();
		bool addressedOnlyExpectedSlots = true;
		for (int slotIndex = 0; slotIndex < 3; slotIndex++)
		{
			const bool slotWasCleared = goodsManager.goodsList[
				goodsManager.bottomBegin() + slotIndex].iniFile.empty();
			addressedOnlyExpectedSlots = addressedOnlyExpectedSlots
				&& slotWasCleared == (slotIndex <= quickItemCase.slotIndex);
		}
		ok = check(actionState.pressed
			&& actionState.sourceDeviceID == gamepad.id()
			&& !leakedUnmodifiedAction
			&& addressedOnlyExpectedSlots
			&& gameManager.scriptTaskList.empty()
			&& !gameManager.inEvent,
			"quick-item chord leaked its base action or addressed the wrong bottom slot "
				+ std::to_string(quickItemCase.slotIndex)) && ok;
		updateButton(quickItemCase.button, false);
		const bool leakedPrimaryRelease =
			inputManager.wasActionPressed(
				GameInput::InputAction::PreviousPanel);
		dispatchFrame();
		updateButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
		const bool leakedShoulderRelease =
			inputManager.wasActionPressed(
				GameInput::InputAction::InteractAlternate)
			|| inputManager.wasActionPressed(
				GameInput::InputAction::PreviousPanel)
			|| inputManager.wasActionPressed(
				GameInput::InputAction::NextPanel);
		dispatchFrame();
		ok = check(!leakedPrimaryRelease && !leakedShoulderRelease,
			"quick-item chord leaked a shoulder release alias") && ok;
	}

	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MAX);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	dispatchFrame();
	player.nextAction = nullptr;
	updateButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);
	const bool jumpedBeforeGrace = inputManager.wasActionPressed(
		GameInput::InputAction::Jump);
	dispatchFrame();
	ok = check(!jumpedBeforeGrace,
		"directional jump fired before the shoulder chord grace elapsed") && ok;
	player.nextAction = nullptr;
	nowMilliseconds +=
		GameInput::PhysicalInputManager::ShoulderChordGraceMilliseconds + 1;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	const GameInput::InputActionState jumpState = inputManager.action(
		GameInput::InputAction::Jump);
	dispatchFrame();
	ok = check(jumpState.pressed
		&& jumpState.sourceDeviceID == gamepad.id()
		&& jumpState.axis.leftStick.x > 0.9f
		&& player.nextAction != nullptr
		&& player.nextAction->action == acJump,
		"directional LB jump did not preserve its source, axis, or gameplay action") && ok;
	updateButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
	const bool jumpReleaseLeakedPanel = inputManager.wasActionPressed(
		GameInput::InputAction::PreviousPanel);
	dispatchFrame();
	ok = check(!jumpReleaseLeakedPanel,
		"consumed directional jump leaked PreviousPanel on LB release") && ok;
	gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
	nowMilliseconds += 10;
	VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
	dispatchFrame();
	ok = check(!inputManager.isActionDown(GameInput::InputAction::Move)
		&& !inputManager.isActionDown(GameInput::InputAction::Jump),
		"directional jump did not return to a neutral action state") && ok;

	player.forceBeginStand();
	updateButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
	const GameInput::InputActionState firstSitState = inputManager.action(
		GameInput::InputAction::ToggleSit);
	dispatchFrame();
	ok = check(firstSitState.pressed
		&& firstSitState.sourceDeviceID == gamepad.id()
		&& player.isSitting(),
		"R3 did not enter the real player sit state") && ok;
	updateButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
	const bool sitRepeatedOnRelease = inputManager.wasActionPressed(
		GameInput::InputAction::ToggleSit);
	dispatchFrame();
	ok = check(!sitRepeatedOnRelease && player.isSitting(),
		"R3 release repeated or reversed the sit action") && ok;
	updateButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
	const GameInput::InputActionState secondSitState = inputManager.action(
		GameInput::InputAction::ToggleSit);
	dispatchFrame();
	ok = check(secondSitState.pressed
		&& secondSitState.sourceDeviceID == gamepad.id()
		&& player.isStanding(),
		"second R3 press did not return the real player to standing") && ok;
	updateButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
	dispatchFrame();

	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"GP03 physical-link test initialized SDL video") && ok;
	return ok;
}
}

bool runGamepadWorldRuntimeTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"gamepad world runtime tests started with SDL video initialized");
	try
	{
		ok = runProductionInputDispatchTests() && ok;
		ok = runProductionTargetPromptAndInvalidationTests() && ok;
		ok = runWorldInputContextTransitionTests() && ok;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAILED: production gamepad fixture: "
			<< exception.what() << '\n';
		ok = false;
	}
	try
	{
		ok = runMapThumbnailPhysicalLinkTests() && ok;
		ok = runMapPointerElementTreeTests() && ok;
		ok = runPointerTransactionDragAndHideTests() && ok;
		ok = runPointerTransactionFingerCancelTests() && ok;
		ok = runCrossPlatformTouchWorldInteractionTests() && ok;
		ok = runHeldMouseLifecycleTransactionTests() && ok;
		ok = runHeldWorldMouseAcrossMenuTransitionTests() && ok;
		ok = runTouchControlsVisibilityHeldMouseTests() && ok;
		ok = runHeldWorldMouseBehindOrdinaryMenuTests() && ok;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAILED: map physical-link fixture: "
			<< exception.what() << '\n';
		ok = false;
	}
	ok = runWorldInputGateTests() && ok;
	ok = runMovementAndAttackTests() && ok;
	ok = runSkillAndJumpTests() && ok;
	ok = runLegacyWorldInputCompatibilityTests() && ok;
	ok = runQuickItemAndSitTests() && ok;
	ok = runGP03PhysicalLinkTests() && ok;
	// ResourceManager has no reset API. Keep the production resource-backed
	// shortcut fixture last so its active roots cannot affect headless peers.
	ok = runWorldMenuShortcutPhysicalLinkTests() && ok;
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"gamepad world runtime tests left SDL video initialized") && ok;
	if (ok)
	{
		std::cout << "gamepad world runtime tests passed\n";
	}
	return ok;
}
