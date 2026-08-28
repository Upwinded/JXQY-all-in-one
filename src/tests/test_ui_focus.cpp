#include "../Game/Menu/SlotGridController.h"
#include "../Game/Menu/UIFocusManager.h"
#include "../Game/Menu/MemoMemu.h"
#include "../Game/Menu/Option.h"
#include "../Game/Menu/SaveLoad.h"
#include "../Game/Menu/System.h"
#include "../Game/Menu/ToolTip.h"
#include "../Game/Menu/YesNo.h"
#include "../Component/ConfigDrivenPanel.h"
#include "../Engine/Engine.h"
#include "../Game/GameManager/GameManager.h"
#include "../File/File.h"
#include "../Input/PhysicalInputManager.h"
#include "../Resource/ResourceManager.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "VirtualGamepadTestHarness.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class UIFocusTestEquipMenu : public EquipMenu
{
};

class ConfigDrivenFocusTestPanel : public ConfigDrivenPanel
{
public:
	void setDefinitions(
		std::vector<ConfigDrivenPanel::ComponentDefinition> definitions)
	{
		componentDefinitions = std::move(definitions);
	}
};

class UIFocusTestAccess
{
public:
	enum class MenuRole
	{
		None,
		State,
		Equip,
		Practice,
		PartnerEquipment,
		Goods,
		Magic,
		Memo
	};

	static void resize(Element& element, int width, int height)
	{
		element.resizeAll(width, height);
	}

	static void update(Element& element)
	{
		element.update();
	}

	static bool dispatchEvent(Element& element, AEvent& event)
	{
		return element.onHandleEvent(event);
	}

	static void dispatchElementEvents(Element& root)
	{
		root.postTreatmentAll();
		root.allHandleEvents();
	}

	static void releaseSystemMenu(
		MenuController& menuController,
		std::shared_ptr<System>& system,
		bool menuWasVisible)
	{
		menuController.releaseSystemMenu(system, menuWasVisible);
	}

	static bool isLogicRunning(const Element& element)
	{
		return element.logicRunning;
	}

	static void initializeSlotMenus(MenuController& menuController)
	{
		menuController.upMenu = std::make_shared<Panel>();
		menuController.stateMenu = std::make_shared<StateMenu>();
		menuController.goodsMenu = std::make_shared<GoodsMenu>();
		menuController.magicMenu = std::make_shared<MagicMenu>();
		menuController.memoMenu = std::make_shared<MemoMenu>();
		menuController.practiceMenu = std::make_shared<PracticeMenu>();
		menuController.equipMenu = std::make_shared<UIFocusTestEquipMenu>();
		menuController.bottomMenu = std::make_shared<BottomMenu>();
		menuController.partnerEquipMenu =
			std::make_shared<PartnerEquipMenu>();
		menuController.partnerEquipMenu->visible = false;
		menuController.addChild(menuController.upMenu);
		menuController.upMenu->addChild(menuController.stateMenu);
		menuController.upMenu->addChild(menuController.goodsMenu);
		menuController.upMenu->addChild(menuController.magicMenu);
		menuController.upMenu->addChild(menuController.memoMenu);
		menuController.upMenu->addChild(menuController.practiceMenu);
		menuController.upMenu->addChild(menuController.equipMenu);
		menuController.addChild(menuController.bottomMenu);
		menuController.upMenu->addChild(menuController.partnerEquipMenu);
		menuController.configureControllerTransferDomains();
	}

	static bool ensureMenuOpen(
		MenuController& menuController, MenuRole role)
	{
		return menuController.ensureControllerMenuOpen(
			toControllerMenuRole(role));
	}

	static bool closeFocusedMenu(MenuController& menuController)
	{
		return menuController.closeControllerMenu(
			menuController.controllerFocusedMenu.lock());
	}

	static bool toggleMenu(
		MenuController& menuController, MenuRole role)
	{
		return menuController.toggleControllerMenuRole(
			toControllerMenuRole(role));
	}

	static bool focusedRoleIs(
		const MenuController& menuController, MenuRole role)
	{
		return menuController.controllerFocusedRole
			== toControllerMenuRole(role);
	}

	static void focusGoods(MenuController& menuController)
	{
		menuController.goodsMenu->visible = true;
		menuController.setControllerFocusedMenu(
			menuController.goodsMenu,
			MenuController::ControllerMenuRole::Goods);
		menuController.goodsMenu->focusControllerDefault();
	}

	static void focusMagic(MenuController& menuController)
	{
		menuController.goodsMenu->visible = false;
		menuController.magicMenu->visible = true;
		menuController.setControllerFocusedMenu(
			menuController.magicMenu,
			MenuController::ControllerMenuRole::Magic);
		menuController.magicMenu->focusControllerDefault();
	}

	static void focusPractice(MenuController& menuController)
	{
		menuController.magicMenu->visible = false;
		menuController.practiceMenu->visible = true;
		menuController.setControllerFocusedMenu(
			menuController.practiceMenu,
			MenuController::ControllerMenuRole::Practice);
		menuController.practiceMenu->focusControllerDefault();
	}

	static void focusEquipment(MenuController& menuController)
	{
		menuController.practiceMenu->visible = false;
		menuController.equipMenu->visible = true;
		menuController.setControllerFocusedMenu(
			menuController.equipMenu,
			MenuController::ControllerMenuRole::Equip);
		menuController.equipMenu->focusControllerEquipment();
	}

	template<typename OwnerType>
	static bool restoreDefaultFocus(
		MenuController& menuController,
		const std::shared_ptr<OwnerType>& owner)
	{
		return menuController.restoreControllerFocus(
			MenuController::ControllerMenuOwner::from(owner),
			MenuController::ControllerMenuRole::None);
	}

	static bool restoreGoodsFocus(
		MenuController& menuController,
		const PElement& owner)
	{
		return menuController.restoreControllerFocus(
			owner, MenuController::ControllerMenuRole::Goods);
	}

	static PElement focusedOwner(MenuController& menuController)
	{
		return menuController.controllerFocusedMenu.lock();
	}

	static PElement recoverFocusedOwner(MenuController& menuController)
	{
		return menuController.getControllerFocusedMenu();
	}

	static bool focusedRoleIsMagic(MenuController& menuController)
	{
		return menuController.controllerFocusedRole
			== MenuController::ControllerMenuRole::Magic;
	}

	static bool focusMagicOnEquipment(MenuController& menuController)
	{
		if (menuController.equipMenu == nullptr)
		{
			return false;
		}
		menuController.equipMenu->visible = true;
		if (!menuController.resolveMagicControllerOwner()
			.matches(menuController.equipMenu)
			|| !menuController.equipMenu->focusControllerMagicList())
		{
			return false;
		}
		menuController.setControllerFocusedMenu(
			menuController.equipMenu,
			MenuController::ControllerMenuRole::Magic);
		return true;
	}

	static bool reloadIntegratedEquipmentMenu(MenuController& menuController)
	{
		if (menuController.upMenu == nullptr)
		{
			return false;
		}
		menuController.upMenu->removeChild(menuController.equipMenu);
		menuController.equipMenu =
			std::make_shared<UIFocusTestEquipMenu>();
		menuController.upMenu->addChild(menuController.equipMenu);
		menuController.configureControllerTransferDomains();
		return menuController.isStateEquipIntegrated()
			&& !menuController.equipMenu->magicDisplayItem.empty()
			&& menuController.equipMenu->magicDisplayItem[0] != nullptr;
	}

	static bool buySellControllerActive(const BuySellMenu& buySellMenu)
	{
		return buySellMenu.controllerPaneRouter.isActive();
	}

	static PElement buySellControllerFocusedElement(
		const BuySellMenu& buySellMenu)
	{
		return buySellMenu.controllerPaneRouter.controllerFocusedElement();
	}

	static bool focusNode(SaveLoad& saveLoad, const std::string& focusId)
	{
		return saveLoad.focusManager.focusNode(focusId);
	}

	static std::string focusedNodeId(const SaveLoad& saveLoad)
	{
		return saveLoad.focusManager.getFocusedNodeId();
	}

	static PElement focusedElement(const SaveLoad& saveLoad)
	{
		return saveLoad.focusManager.getFocusedElement();
	}

	static bool focusNode(YesNo& yesNo, const std::string& focusId)
	{
		return yesNo.focusManager.focusNode(focusId);
	}

	static std::string focusedNodeId(const YesNo& yesNo)
	{
		return yesNo.focusManager.getFocusedNodeId();
	}

	static PElement focusedElement(const YesNo& yesNo)
	{
		return yesNo.focusManager.getFocusedElement();
	}

private:
	static MenuController::ControllerMenuRole toControllerMenuRole(
		MenuRole role)
	{
		switch (role)
		{
		case MenuRole::None:
			return MenuController::ControllerMenuRole::None;
		case MenuRole::State:
			return MenuController::ControllerMenuRole::State;
		case MenuRole::Equip:
			return MenuController::ControllerMenuRole::Equip;
		case MenuRole::Practice:
			return MenuController::ControllerMenuRole::Practice;
		case MenuRole::PartnerEquipment:
			return MenuController::ControllerMenuRole::PartnerEquipment;
		case MenuRole::Goods:
			return MenuController::ControllerMenuRole::Goods;
		case MenuRole::Magic:
			return MenuController::ControllerMenuRole::Magic;
		case MenuRole::Memo:
			return MenuController::ControllerMenuRole::Memo;
		default:
			return MenuController::ControllerMenuRole::None;
		}
	}
};

namespace
{
class PointerDragProbe : public Element
{
public:
	int clickCount = 0;
	int dragEndCount = 0;
	int dropCount = 0;
	PElement dragEndTarget;
	PElement droppedSource;

private:
	void onClick() override
	{
		clickCount++;
	}

	void onDragEnd(PElement target, int, int) override
	{
		dragEndCount++;
		dragEndTarget = std::move(target);
	}

	void onDrop(PElement source, int, int) override
	{
		dropCount++;
		droppedSource = std::move(source);
	}
};

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool check(bool condition, const std::string& message)
{
	return check(condition, message.c_str());
}

std::shared_ptr<Element> makeTestFocusElement(
	int x = 0, int y = 0, int width = 20, int height = 20)
{
	auto element = std::make_shared<Element>();
	element->rect = { x, y, width, height };
	return element;
}

bool testCrossControlPointerDragContract()
{
	auto root = std::make_shared<Element>();
	root->coverMouse = false;
	auto source = std::make_shared<PointerDragProbe>();
	source->rect = { 10, 10, 24, 24 };
	source->canDrag = true;
	auto target = std::make_shared<PointerDragProbe>();
	target->rect = { 70, 10, 24, 24 };
	target->canDrop = true;
	root->addChild(source);
	root->addChild(target);

	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 20, 20, false));
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 20, 20, false));
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 80, 20, false));
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 80, 20, false));
	UIFocusTestAccess::dispatchElementEvents(*root);

	bool ok = check(source->dragEndCount == 1
			&& source->dragEndTarget == target,
		"cross-control drag reports the real drop target to its source")
		&& check(target->dropCount == 1
			&& target->droppedSource == source,
		"cross-control drag invokes the real drop target exactly once")
		&& check(source->clickCount == 0 && target->clickCount == 0,
		"cross-control drag does not synthesize a source or hover-only target click");

	auto sameControlRoot = std::make_shared<Element>();
	sameControlRoot->coverMouse = false;
	auto sameControlSource = std::make_shared<PointerDragProbe>();
	sameControlSource->rect = { 10, 10, 24, 24 };
	sameControlSource->canDrag = true;
	sameControlRoot->addChild(sameControlSource);
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 20, 20, false));
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEDOWN, MBC_MOUSE_LEFT, 20, 20, false));
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEMOTION, TOUCH_MOUSEID, 24, 20, false));
	Engine::getInstance()->pushEvent(
		AEvent(ET_MOUSEUP, MBC_MOUSE_LEFT, 24, 20, false));
	UIFocusTestAccess::dispatchElementEvents(*sameControlRoot);
	ok = check(sameControlSource->dragEndCount == 1
			&& sameControlSource->dragEndTarget == sameControlSource,
		"same-control drag did not report one drag-end to its source") && ok;
	ok = check(sameControlSource->clickCount == 0,
		"same-control drag release also committed a click") && ok;
	return ok;
}

class KeyboardUIActionTarget final : public Element
{
public:
	bool handled = true;
	int dispatchCount = 0;
	UIAction lastAction = UIAction::Confirm;

protected:
	bool onHandleUIAction(UIAction action) override
	{
		dispatchCount++;
		lastAction = action;
		return handled;
	}
};

bool testInputEventHelpers()
{
	bool ok = true;
	KeyboardUIActionTarget target;
	struct KeyboardActionCase
	{
		KeyCode key;
		UIAction action;
		bool repeatAllowed;
	};
	const KeyboardActionCase keyboardCases[] =
	{
		{ KEY_UP, UIAction::NavigateUp, true },
		{ KEY_W, UIAction::NavigateUp, true },
		{ KEY_DOWN, UIAction::NavigateDown, true },
		{ KEY_S, UIAction::NavigateDown, true },
		{ KEY_LEFT, UIAction::NavigateLeft, true },
		{ KEY_A, UIAction::NavigateLeft, true },
		{ KEY_RIGHT, UIAction::NavigateRight, true },
		{ KEY_D, UIAction::NavigateRight, true },
		{ KEY_RETURN, UIAction::Confirm, false },
		{ KEY_SPACE, UIAction::Confirm, false },
		{ KEY_ESCAPE, UIAction::Cancel, false }
	};
	for (const KeyboardActionCase& keyboardCase : keyboardCases)
	{
		const int countBeforePress = target.dispatchCount;
		AEvent keyPress(ET_KEYDOWN, keyboardCase.key, 0, 0, false);
		ok = check(dispatchKeyboardUIAction(keyPress, target)
			&& target.dispatchCount == countBeforePress + 1
			&& target.lastAction == keyboardCase.action,
			"keyboard helper changed a non-repeated mapping") && ok;

		const int countBeforeRepeat = target.dispatchCount;
		AEvent keyRepeat(ET_KEYDOWN, keyboardCase.key, 0, 0, true);
		const bool repeatHandled = dispatchKeyboardUIAction(keyRepeat, target);
		ok = check(repeatHandled == keyboardCase.repeatAllowed
			&& target.dispatchCount == countBeforeRepeat
				+ (keyboardCase.repeatAllowed ? 1 : 0)
			&& (!keyboardCase.repeatAllowed
				|| target.lastAction == keyboardCase.action),
			"keyboard helper changed a repeated mapping") && ok;
	}
	const int mappedDispatchCount = target.dispatchCount;
	AEvent ordinaryMenuSkillKey(ET_KEYDOWN, KEY_S, 0, 0, false);
	ok = check(!dispatchKeyboardUIAction(
			ordinaryMenuSkillKey,
			target,
			KeyboardNavigationKeySet::DirectionKeysOnly)
		&& target.dispatchCount == mappedDispatchCount,
		"direction-key-only menu mapping consumed a legacy skill key") && ok;
	AEvent ordinaryMenuDirectionKey(ET_KEYDOWN, KEY_DOWN, 0, 0, false);
	ok = check(dispatchKeyboardUIAction(
			ordinaryMenuDirectionKey,
			target,
			KeyboardNavigationKeySet::DirectionKeysOnly)
		&& target.dispatchCount == mappedDispatchCount + 1
		&& target.lastAction == UIAction::NavigateDown,
		"direction-key-only menu mapping rejected a direction key") && ok;
	const int directionOnlyDispatchCount = target.dispatchCount;

	AEvent keyUp(ET_KEYUP, KEY_W, 0, 0, false);
	ok = check(!dispatchKeyboardUIAction(keyUp, target)
		&& target.dispatchCount == directionOnlyDispatchCount,
		"keyboard helper dispatched a non-keydown event") && ok;
	AEvent unmappedKey(ET_KEYDOWN, KEY_F1, 0, 0, false);
	ok = check(!dispatchKeyboardUIAction(unmappedKey, target)
		&& target.dispatchCount == directionOnlyDispatchCount,
		"keyboard helper dispatched an unmapped key") && ok;

	target.handled = false;
	const int countBeforeUnhandledAction = target.dispatchCount;
	AEvent navigateLeft(ET_KEYDOWN, KEY_LEFT, 0, 0, true);
	ok = check(!dispatchKeyboardUIAction(navigateLeft, target)
		&& target.dispatchCount == countBeforeUnhandledAction + 1
		&& target.lastAction == UIAction::NavigateLeft,
		"keyboard helper did not preserve the target return value") && ok;

	const EventType pointerEvents[] =
	{
		ET_MOUSEMOTION,
		ET_MOUSEDOWN,
		ET_MOUSEUP,
		ET_MOUSEWHEEL,
		ET_FINGERDOWN,
		ET_FINGERUP,
		ET_FINGERMOTION,
		ET_FINGERCANCEL
	};
	const bool allPointerEventsRecognized = std::all_of(
		std::begin(pointerEvents),
		std::end(pointerEvents),
		[](EventType eventType)
		{
			return isPointerTakeoverEvent(AEvent(eventType, 0, 0, 0));
		});
	ok = check(allPointerEventsRecognized,
		"pointer helper omitted a mouse or touch takeover event") && ok;
	ok = check(!isPointerTakeoverEvent(AEvent(ET_KEYDOWN, KEY_W, 0, 0))
		&& !isPointerTakeoverEvent(AEvent(ET_KEYUP, KEY_W, 0, 0))
		&& !isPointerTakeoverEvent(AEvent(ET_WINDOWRESIZE, 0, 0, 0))
		&& !isPointerTakeoverEvent(AEvent(ET_WINDOWCLOSE, 0, 0, 0))
		&& !isPointerTakeoverEvent(AEvent(ET_QUIT, 0, 0, 0)),
		"pointer helper accepted a non-pointer event") && ok;
	return ok;
}

bool testInputAwareFocusPresentation()
{
	bool ok = true;
	resetUIFocusInputPresentation();
	UIFocusManager focus;
	focus.setInputAwarePresentation();
	auto first = makeTestFocusElement();
	auto second = makeTestFocusElement();
	first->rect = { 0, 0, 20, 20 };
	second->rect = { 40, 0, 20, 20 };
	focus.addLinearGroup(
		"input-aware-focus",
		UIFocusLinearAxis::Horizontal,
		{
			{ "first", first },
			{ "second", second }
		});
	focus.setDefaultFocus("first");
	ok = check(focus.focusDefault()
		&& focus.getFocusedNodeId() == "first"
		&& !focus.isFocusPresented()
		&& !first->isFocused(),
		"input-aware focus exposed its logical default without a keyboard or gamepad") && ok;
	ok = check(focus.handleAction(UIAction::NavigateRight)
		&& !focus.isFocusPresented()
		&& focus.getFocusedElement() == second
		&& !second->isFocused()
		&& !first->isFocused(),
		"keyboard semantic navigation did not retain logical focus without a yellow gamepad highlight") && ok;

	AEvent syntheticMotion(
		ET_MOUSEMOTION, TOUCH_MOUSEID, 80, 40, false, true);
	notifyUIFocusInputEvent(syntheticMotion, Engine::getInstance());
	ok = check(focus.getFocusedElement() == second
		&& !focus.isFocusPresented(),
		"synthetic mouse refresh changed retained logical focus") && ok;
	AEvent realMotion(ET_MOUSEMOTION, TOUCH_MOUSEID, 81, 40, false);
	notifyUIFocusInputEvent(realMotion, Engine::getInstance());
	ok = check(!first->isFocused() && !second->isFocused()
		&& !focus.isFocusPresented()
		&& focus.getFocusedNodeId() == "second"
		&& focus.getFocusedElement() == second,
		"real pointer takeover did not hide the retained logical focus") && ok;
	first->touchingDownID = TOUCH_MOUSEID;
	ok = check(adoptUIFocusPointerTarget(TOUCH_MOUSEID)
		&& focus.getFocusedElement() == first
		&& !first->isFocused() && !second->isFocused(),
		"pointer-down adoption did not move retained logical focus without showing a gamepad highlight") && ok;
	first->touchingDownID = TOUCH_UNTOUCHEDID;
	ok = check(focus.handleAction(UIAction::NavigateRight)
		&& focus.getFocusedElement() == second
		&& !first->isFocused() && !second->isFocused(),
		"keyboard navigation after pointer adoption did not execute without showing a gamepad highlight") && ok;
	resetUIFocusInputPresentation();
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"input-aware focus presentation test initialized SDL video") && ok;
	return ok;
}

bool testInputAwarePhysicalFocusPresentation()
{
	bool ok = true;
	auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
		Engine::getInstance()->inputActions());
	try
	{
		VirtualGamepadTest::SDLSession sdlSession;
		inputManager.shutdown();
		VirtualGamepadTest::VirtualGamepad gamepad(
			"JXQY Input-Aware Focus Pad");
		ok = check(inputManager.initialize(),
			"input-aware focus physical manager did not initialize") && ok;
		std::uint64_t nowMilliseconds = 100;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		ok = check(inputManager.hasActiveGamepad(),
			"input-aware focus physical fixture did not claim its gamepad") && ok;

		resetUIFocusInputPresentation();
		dispatchPhysicalUIActions(Engine::getInstance());
		UIFocusManager gamepadFocus;
		gamepadFocus.setInputAwarePresentation();
		auto gamepadElement = makeTestFocusElement();
		gamepadFocus.addNode("gamepad", gamepadElement);
		gamepadFocus.setDefaultFocus("gamepad");
		ok = check(gamepadFocus.focusDefault()
			&& gamepadElement->isFocused()
			&& gamepadFocus.isFocusPresented(),
			"active physical gamepad did not present a new menu default") && ok;
		{
			GameManager gameManager;
			UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
			auto bottomMenu = gameManager.menu->bottomMenu;
			const PElement retainedFocus = bottomMenu != nullptr
				? std::static_pointer_cast<Element>(bottomMenu->equipBtn)
				: nullptr;
			ok = check(retainedFocus != nullptr
				&& bottomMenu->focusControllerElement(retainedFocus)
				&& bottomMenu->controllerFocusedElement() == retainedFocus
				&& retainedFocus->isFocused(),
				"system modal restore fixture could not select a non-default"
				" bottom-menu focus") && ok;

			const bool menuWasVisible = gameManager.menu->visible;
			gameManager.menu->visible = false;
			std::shared_ptr<System> systemMenu =
				std::make_shared<System>();
			std::weak_ptr<System> releasedSystemMenu = systemMenu;
			gameManager.addChild(systemMenu);
			ok = check(systemMenu->focusManager.isFocusPresented()
				&& !retainedFocus->isFocused(),
				"system modal did not suspend the lower focus presenter") && ok;
			UIFocusTestAccess::releaseSystemMenu(
				*gameManager.menu, systemMenu, menuWasVisible);
			ok = check(systemMenu == nullptr
				&& releasedSystemMenu.expired()
				&& gameManager.menu->visible == menuWasVisible
				&& bottomMenu->controllerFocusedElement() == retainedFocus
				&& retainedFocus->isFocused(),
				"closing the system modal did not restore the prior"
				" non-default focus after restoring menu visibility") && ok;
		}
		UIFocusManager unavailableFocus;
		unavailableFocus.setInputAwarePresentation();
		auto unavailableElement = makeTestFocusElement();
		unavailableElement->visible = false;
		unavailableFocus.addNode("unavailable", unavailableElement);
		unavailableFocus.setDefaultFocus("unavailable");
		ok = check(!unavailableFocus.focusDefault()
			&& !unavailableFocus.isFocusPresented()
			&& gamepadElement->isFocused()
			&& gamepadFocus.isFocusPresented(),
			"a focus owner with no available candidate displaced the valid"
			" gamepad presenter") && ok;
		{
			UIFocusManager nestedModalFocus;
			nestedModalFocus.setInputAwarePresentation();
			auto nestedModalElement = makeTestFocusElement();
			nestedModalFocus.addNode("nested-modal", nestedModalElement);
			nestedModalFocus.setDefaultFocus("nested-modal");
			ok = check(nestedModalFocus.focusDefault()
				&& nestedModalElement->isFocused()
				&& nestedModalFocus.isFocusPresented()
				&& !gamepadElement->isFocused()
				&& !gamepadFocus.isFocusPresented(),
				"nested modal did not become the sole gamepad focus presenter")
				&& ok;
		}
		ok = check(gamepadElement->isFocused()
			&& gamepadFocus.isFocusPresented(),
			"closing a nested modal did not restore the previous focus presenter")
			&& ok;
		{
			auto parentOwner = std::make_shared<Panel>();
			auto parentElement = makeTestFocusElement();
			parentOwner->addChild(parentElement);
			UIFocusManager parentFocus;
			parentFocus.setInputAwarePresentation();
			parentFocus.addNode("parent", parentElement);
			parentFocus.setDefaultFocus("parent");

			auto modalOwner = std::make_shared<Panel>();
			auto modalElement = makeTestFocusElement();
			parentOwner->addChild(modalOwner);
			modalOwner->addChild(modalElement);
			UIFocusManager modalFocus;
			modalFocus.setInputAwarePresentation();
			modalFocus.addNode("modal", modalElement);
			modalFocus.setDefaultFocus("modal");

			ok = check(parentFocus.focusDefault()
					&& parentFocus.isFocusPresented()
					&& parentElement->isFocused(),
				"nested-owner presentation fixture did not establish its"
				" parent focus") && ok;
			{
				HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
					modalOwner);
				ok = check(modalFocus.focusDefault()
						&& modalFocus.isFocusPresented()
						&& modalElement->isFocused()
						&& !parentFocus.isFocusPresented()
						&& parentFocus.focusDefault()
						&& !parentFocus.isFocusPresented()
						&& !parentElement->isFocused()
						&& modalFocus.isFocusPresented()
						&& modalElement->isFocused(),
					"a parent focus rebuild displaced the current nested"
					" semantic owner presentation") && ok;
			}
		}

		AEvent keyboardShortcut(ET_KEYDOWN, KEY_F1, 0, 0, false);
		notifyUIFocusInputEvent(keyboardShortcut, Engine::getInstance());
		ok = check(gamepadFocus.getFocusedElement() == gamepadElement
			&& !gamepadElement->isFocused()
			&& !gamepadFocus.isFocusPresented(),
			"a real non-navigation keyboard shortcut left the gamepad highlight visible") && ok;

		gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		dispatchPhysicalUIActions(Engine::getInstance());
		ok = check(gamepadFocus.restoreFocus()
			&& gamepadElement->isFocused()
			&& gamepadFocus.isFocusPresented(),
			"gamepad input did not restore its retained logical highlight after keyboard takeover") && ok;

		gamepad.detach();
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		dispatchPhysicalUIActions(Engine::getInstance());
		ok = check(!inputManager.hasActiveGamepad()
			&& !gamepadElement->isFocused()
			&& !gamepadFocus.isFocusPresented(),
			"active gamepad removal left its yellow focus presentation visible") && ok;
		inputManager.shutdown();
	}
	catch (const std::exception& exception)
	{
		inputManager.shutdown();
		std::cerr << "FAILED: " << exception.what() << '\n';
		ok = false;
	}
	resetUIFocusInputPresentation();
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"input-aware physical focus test initialized SDL video") && ok;
	return ok;
}

bool testKeyboardEventRoutingPrecedence()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	GameManager gameManager;
	gameManager.global.data.canInput = true;
	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, UIFocusTestAccess::MenuRole::Goods),
		"keyboard routing test could not open the goods menu") && ok;
	AEvent repeatedEscape(ET_KEYDOWN, KEY_ESCAPE, 0, 0, true);
	ok = check(UIFocusTestAccess::dispatchEvent(
		*gameManager.menu, repeatedEscape)
		&& !gameManager.menu->goodsMenu->visible
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu) == nullptr,
		"MenuController did not keep its repeated Escape close path ahead of mapping") && ok;

	gameManager.menu->partnerEquipMenu->visible = true;
	ok = check(UIFocusTestAccess::dispatchEvent(
		*gameManager.menu->partnerEquipMenu, repeatedEscape)
		&& !gameManager.menu->partnerEquipMenu->visible,
		"PartnerEquip did not keep its dedicated Escape close path ahead of mapping") && ok;

	auto modalPartner = std::make_shared<NPC>();
	modalPartner->kind = nkPartner;
	modalPartner->canEquip = 1;
	ok = check(gameManager.menu->openPartnerEquipment(
			modalPartner, false),
		"keyboard routing test could not open the PartnerEquip modal") && ok;
	const bool stateVisible = gameManager.menu->stateMenu->visible;
	const bool equipVisible = gameManager.menu->equipMenu->visible;
	const bool practiceVisible = gameManager.menu->practiceMenu->visible;
	const bool goodsVisible = gameManager.menu->goodsMenu->visible;
	const bool magicVisible = gameManager.menu->magicMenu->visible;
	const bool memoVisible = gameManager.menu->memoMenu->visible;
	const KeyCode lowerMenuShortcuts[] =
	{
		KEY_F1, KEY_F2, KEY_F3, KEY_F4,
		KEY_F5, KEY_F6, KEY_F7, KEY_F8,
		KEY_M, KEY_TAB
	};
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	for (KeyCode shortcut : lowerMenuShortcuts)
	{
		Engine::getInstance()->pushEvent(
			AEvent(ET_KEYDOWN, shortcut, 0, 0, false));
		UIFocusTestAccess::dispatchElementEvents(gameManager);
	}
	ok = check(gameManager.menu->partnerEquipMenu->visible
			&& gameManager.menu->stateMenu->visible == stateVisible
			&& gameManager.menu->equipMenu->visible == equipVisible
			&& gameManager.menu->practiceMenu->visible == practiceVisible
			&& gameManager.menu->goodsMenu->visible == goodsVisible
			&& gameManager.menu->magicMenu->visible == magicVisible
			&& gameManager.menu->memoMenu->visible == memoVisible,
		"unhandled lower-menu shortcuts changed surfaces behind PartnerEquip")
		&& ok;
	Engine::getInstance()->pushEvent(
		AEvent(ET_KEYDOWN, KEY_ESCAPE, 0, 0, false));
	UIFocusTestAccess::dispatchElementEvents(gameManager);
	ok = check(!gameManager.menu->partnerEquipMenu->visible,
		"Escape did not close only the active PartnerEquip modal") && ok;

	System systemMenu;
	systemMenu.setRunning(true);
	AEvent quitEvent(ET_QUIT, 0, 0, 0);
	ok = check(UIFocusTestAccess::dispatchEvent(systemMenu, quitEvent)
		&& (systemMenu.result & erExit) != 0
		&& !UIFocusTestAccess::isLogicRunning(systemMenu),
		"System did not keep its ET_QUIT path ahead of keyboard mapping") && ok;
	return ok;
}

void setControllerTestGoods(
	GoodsInfo& goodsInfo,
	const std::string& fileName)
{
	goodsInfo.clear();
	goodsInfo.iniFile = fileName;
	goodsInfo.number = 1;
	goodsInfo.goods = std::make_shared<Goods>();
	goodsInfo.goods->kind = gkNormal;
}

void setControllerTestMagic(
	MagicInfo& magicInfo,
	const std::string& fileName)
{
	magicInfo = MagicInfo();
	magicInfo.iniFile = fileName;
	magicInfo.level = 1;
	magicInfo.magic = std::make_shared<Magic>();
}

bool testFocusGraph()
{
	bool ok = true;
	Engine* testEngine = Engine::getInstance();
	int previousWindowWidth = 0;
	int previousWindowHeight = 0;
	testEngine->getWindowSize(previousWindowWidth, previousWindowHeight);
	testEngine->setWindowSize(2000, 500);
	UIFocusManager focus;
	auto left = makeTestFocusElement();
	auto right = makeTestFocusElement();
	auto below = makeTestFocusElement();
	left->rect = { 0, 0, 20, 20 };
	right->rect = { 40, 0, 20, 20 };
	below->rect = { 0, 40, 20, 20 };
	int confirms = 0;
	focus.addNode("left", left, [&confirms]() { confirms++; });
	focus.addNode("right", right);
	focus.addNode("below", below);
	focus.setDefaultFocus("left");
	focus.setNeighbour("left", UIFocusDirection::Right, "right");

	ok = check(focus.focusDefault() && left->isFocused(),
		"default focus marks the configured element") && ok;
	ok = check(focus.handleAction(UIAction::Confirm) && confirms == 1,
		"confirm invokes the focused semantic action once") && ok;
	ok = check(focus.handleAction(UIAction::NavigateRight)
		&& right->isFocused() && !left->isFocused(),
		"explicit neighbour moves focus without hover state") && ok;
	ok = check(focus.handleAction(UIAction::NavigateLeft)
		&& left->isFocused(),
		"missing reverse edge falls back to spatial navigation") && ok;
	right->visible = false;
	ok = check(!focus.handleAction(UIAction::NavigateRight)
		&& left->isFocused(),
		"hidden explicit neighbour is skipped") && ok;
	ok = check(focus.handleAction(UIAction::NavigateDown)
		&& below->isFocused(),
		"spatial navigation selects the closest available node") && ok;
	focus.suspendFocus();
	ok = check(!below->isFocused() && focus.getFocusedNodeId() == "below",
		"suspend clears visuals but preserves logical focus") && ok;
	ok = check(focus.restoreFocus() && below->isFocused(),
		"restore returns to the preserved logical node") && ok;
	below->activated = false;
	focus.suspendFocus();
	right->visible = true;
	ok = check(focus.handleAction(UIAction::NavigateRight)
			&& right->isFocused(),
		"the first direction after invalid focus stopped at recovery instead"
		" of applying the requested movement") && ok;

	UIFocusManager lexicographicFocus;
	auto source = makeTestFocusElement(0, 0, 20, 20);
	auto offAxisOnePixelCloser = makeTestFocusElement(1200, 20, 20, 20);
	auto directionAligned = makeTestFocusElement(0, 21, 20, 20);
	lexicographicFocus.addNode("source", source);
	lexicographicFocus.addNode(
		"off-axis-one-pixel-closer", offAxisOnePixelCloser);
	lexicographicFocus.addNode("direction-aligned", directionAligned);
	lexicographicFocus.setDefaultFocus("source");
	ok = check(lexicographicFocus.focusDefault()
			&& lexicographicFocus.handleAction(UIAction::NavigateDown)
			&& lexicographicFocus.getFocusedElement() == directionAligned,
		"spatial focus left the 45-degree direction cone for a candidate"
		" that was only one primary-axis pixel closer") && ok;
	testEngine->setWindowSize(
		previousWindowWidth, previousWindowHeight);
	return ok;
}

bool testFocusCandidateAPIs()
{
	bool ok = true;
	Engine* testEngine = Engine::getInstance();
	int previousWindowWidth = 0;
	int previousWindowHeight = 0;
	testEngine->getWindowSize(previousWindowWidth, previousWindowHeight);
	testEngine->setWindowSize(320, 240);

	auto parent = makeTestFocusElement(0, 0, 320, 240);
	auto valid = makeTestFocusElement(20, 20);
	auto zeroSize = makeTestFocusElement(40, 20, 0, 20);
	auto offscreen = makeTestFocusElement(2000, 20);
	auto notDrawn = makeTestFocusElement(60, 20);
	notDrawn->canDraw = false;
	auto expired = makeTestFocusElement(80, 20);
	parent->addChild(valid);
	parent->addChild(zeroSize);
	parent->addChild(offscreen);
	parent->addChild(notDrawn);
	parent->addChild(expired);

	UIFocusManager focus;
	focus.addNode("valid", valid);
	focus.addNode("zero-size", zeroSize);
	focus.addNode("offscreen", offscreen);
	focus.addNode("not-drawn", notDrawn);
	focus.addNode("expired", expired);
	parent->removeChild(expired);
	expired.reset();

	ok = check(focus.getAvailableFocusElements()
			== std::vector<PElement>({ valid })
		&& focus.focusElement(valid)
		&& focus.getFocusedElement() == valid,
		"focus candidate APIs did not filter invalid, expired, zero-size, or offscreen nodes") && ok;
	auto unknown = makeTestFocusElement(100, 20);
	ok = check(!focus.focusElement(unknown),
		"focus-by-element accepted an unregistered node") && ok;

	parent->visible = false;
	ok = check(focus.getAvailableFocusElements().empty()
			&& focus.getFocusedElement() == nullptr,
		"hidden ancestor left a focus candidate available") && ok;
	parent->visible = true;
	parent->activated = false;
	ok = check(focus.getAvailableFocusElements().empty(),
		"disabled ancestor left a focus candidate available") && ok;
	parent->activated = true;
	parent->canDraw = false;
	ok = check(focus.getAvailableFocusElements().empty(),
		"non-drawing ancestor left a focus candidate available") && ok;
	parent->canDraw = true;
	parent->needEvents = false;
	ok = check(focus.getAvailableFocusElements().empty(),
		"event-disabled ancestor left a focus candidate available") && ok;
	parent->needEvents = true;
	valid->needEvents = false;
	ok = check(focus.getAvailableFocusElements().empty()
			&& !focus.focusElement(valid),
		"event-disabled focus node remained selectable") && ok;

	if (previousWindowWidth > 0 && previousWindowHeight > 0)
	{
		testEngine->setWindowSize(previousWindowWidth, previousWindowHeight);
	}
	return ok;
}

bool testLinearFocusGroups()
{
	bool ok = true;

	UIFocusManager verticalFocus;
	auto first = makeTestFocusElement();
	auto middle = makeTestFocusElement();
	auto last = makeTestFocusElement();
	first->rect = { 0, 0, 20, 20 };
	middle->rect = { 0, 40, 20, 20 };
	last->rect = { 0, 80, 20, 20 };
	int confirms = 0;
	int secondaryActions = 0;
	int detailsActions = 0;
	int navigationActions = 0;
	auto omitted = makeTestFocusElement();
	const std::vector<std::string> verticalIds =
		verticalFocus.addLinearGroup(
			"vertical-actions",
			UIFocusLinearAxis::Vertical,
			{
				{ "first", first, [&confirms]() { confirms++; },
					[&secondaryActions]() { secondaryActions++; },
					[&detailsActions]() { detailsActions++; },
					[&navigationActions](UIFocusDirection direction)
					{
						if (direction != UIFocusDirection::Right)
						{
							return false;
						}
						navigationActions++;
						return true;
					} },
				{ "", omitted },
				{ "first", omitted },
				{ "missing", nullptr },
				{ "middle", middle },
				{ "last", last }
			});
	ok = check(verticalIds == std::vector<std::string>(
		{ "first", "middle", "last" }),
		"linear focus group did not return only actually registered nodes") && ok;
	if (!verticalIds.empty())
	{
		verticalFocus.setDefaultFocus(verticalIds.front());
	}
	ok = check(!verticalIds.empty() && verticalFocus.focusDefault()
		&& first->isFocused(),
		"vertical linear group did not focus its configured default") && ok;
	ok = check(verticalFocus.handleAction(UIAction::Confirm)
		&& confirms == 1,
		"linear group did not preserve its confirm binding") && ok;
	ok = check(verticalFocus.handleAction(UIAction::Secondary)
		&& secondaryActions == 1,
		"linear group did not preserve its secondary binding") && ok;
	ok = check(verticalFocus.handleAction(UIAction::Details)
		&& detailsActions == 1,
		"linear group did not preserve its details binding") && ok;
	ok = check(verticalFocus.handleAction(UIAction::NavigateRight)
		&& navigationActions == 1 && first->isFocused(),
		"linear group did not preserve its navigation binding") && ok;
	ok = check(verticalFocus.handleAction(UIAction::NavigateUp)
		&& last->isFocused(),
		"wrapped vertical group did not connect first to last") && ok;
	ok = check(verticalFocus.handleAction(UIAction::NavigateDown)
		&& first->isFocused(),
		"wrapped vertical group did not connect last to first") && ok;
	ok = check(verticalFocus.handleAction(UIAction::NavigateDown)
		&& middle->isFocused()
		&& verticalFocus.handleAction(UIAction::NavigateUp)
		&& first->isFocused(),
		"vertical group did not connect adjacent registered nodes") && ok;

	verticalFocus.focusNode("middle");
	middle->activated = false;
	ok = check(verticalFocus.restoreFocus()
		&& verticalFocus.getFocusedNodeId() == "first",
		"vertical group coordinates counted a structurally missing node") && ok;

	UIFocusManager horizontalFocus;
	auto left = makeTestFocusElement();
	auto center = makeTestFocusElement();
	auto right = makeTestFocusElement();
	left->rect = { 0, 0, 20, 20 };
	center->rect = { 40, 0, 20, 20 };
	right->rect = { 80, 0, 20, 20 };
	const std::vector<std::string> horizontalIds =
		horizontalFocus.addLinearGroup(
			"horizontal-actions",
			UIFocusLinearAxis::Horizontal,
			{
				{ "left", left },
				{ "missing", nullptr },
				{ "center", center },
				{ "right", right }
			},
			false);
	ok = check(horizontalIds == std::vector<std::string>(
		{ "left", "center", "right" }),
		"horizontal group did not skip a structurally missing node") && ok;
	horizontalFocus.focusNode("left");
	ok = check(!horizontalFocus.handleAction(UIAction::NavigateLeft)
		&& left->isFocused(),
		"non-wrapped horizontal group unexpectedly wrapped at its first node") && ok;
	ok = check(horizontalFocus.handleAction(UIAction::NavigateRight)
		&& center->isFocused(),
		"horizontal group did not connect adjacent registered nodes") && ok;
	ok = check(horizontalFocus.handleAction(UIAction::NavigateLeft)
		&& left->isFocused(),
		"horizontal group did not connect its reverse adjacent node") && ok;
	horizontalFocus.focusNode("right");
	ok = check(!horizontalFocus.handleAction(UIAction::NavigateRight)
		&& right->isFocused(),
		"non-wrapped horizontal group unexpectedly wrapped at its last node") && ok;
	horizontalFocus.focusNode("center");
	center->visible = false;
	ok = check(horizontalFocus.restoreFocus()
		&& horizontalFocus.getFocusedNodeId() == "left",
		"horizontal group coordinates counted a structurally missing node") && ok;

	UIFocusManager singleFocus;
	auto only = makeTestFocusElement();
	const std::vector<std::string> singleIds = singleFocus.addLinearGroup(
		"single-action",
		UIFocusLinearAxis::Vertical,
		{
			{ "only", only }
		});
	ok = check(singleIds.size() == 1 && singleIds.front() == "only"
		&& singleFocus.focusNode("only"),
		"single-node linear group was not registered") && ok;
	ok = check(!singleFocus.handleAction(UIAction::NavigateUp)
		&& !singleFocus.handleAction(UIAction::NavigateDown)
		&& only->isFocused(),
		"single-node linear group created a self-loop") && ok;

	UIFocusManager optionLikeFocus;
	auto optionFirst = makeTestFocusElement();
	auto optionSecond = makeTestFocusElement();
	optionFirst->rect = { 0, 0, 20, 20 };
	optionSecond->rect = { 0, 40, 20, 20 };
	int horizontalAdjustments = 0;
	auto optionNavigation = [&horizontalAdjustments](UIFocusDirection direction)
	{
		if (direction != UIFocusDirection::Left
			&& direction != UIFocusDirection::Right)
		{
			return false;
		}
		horizontalAdjustments += direction == UIFocusDirection::Left ? -1 : 1;
		return true;
	};
	optionLikeFocus.addLinearGroup(
		"option-like-rows",
		UIFocusLinearAxis::Vertical,
		{
			{ "first", optionFirst, {}, {}, {}, optionNavigation },
			{ "second", optionSecond, {}, {}, {}, optionNavigation }
		});
	optionLikeFocus.setDefaultFocus("first");
	ok = check(optionLikeFocus.focusDefault()
		&& optionLikeFocus.handleAction(UIAction::NavigateRight)
		&& horizontalAdjustments == 1
		&& optionFirst->isFocused(),
		"vertical linear group did not let a row consume horizontal navigation")
		&& ok;
	ok = check(optionLikeFocus.handleAction(UIAction::NavigateDown)
		&& optionSecond->isFocused(),
		"vertical linear group did not continue after a row declined vertical navigation")
		&& ok;

	UIFocusManager saveLoadLikeFocus;
	auto loadAction = makeTestFocusElement();
	auto saveAction = makeTestFocusElement();
	loadAction->rect = { 0, 0, 20, 20 };
	saveAction->rect = { 40, 0, 20, 20 };
	int slotMoves = 0;
	auto slotNavigation = [&slotMoves](UIFocusDirection direction)
	{
		if (direction == UIFocusDirection::Up)
		{
			slotMoves--;
			return true;
		}
		if (direction == UIFocusDirection::Down)
		{
			slotMoves++;
			return true;
		}
		return false;
	};
	saveLoadLikeFocus.addLinearGroup(
		"save-load-like-actions",
		UIFocusLinearAxis::Horizontal,
		{
			{ "load", loadAction, {}, {}, {}, slotNavigation },
			{ "save", saveAction, {}, {}, {}, slotNavigation }
		});
	saveLoadLikeFocus.setDefaultFocus("load");
	ok = check(saveLoadLikeFocus.focusDefault()
		&& saveLoadLikeFocus.handleAction(UIAction::NavigateDown)
		&& slotMoves == 1
		&& loadAction->isFocused(),
		"horizontal linear group did not let an action consume vertical navigation")
		&& ok;
	ok = check(saveLoadLikeFocus.handleAction(UIAction::NavigateRight)
		&& saveAction->isFocused(),
		"horizontal linear group did not continue after an action declined panel navigation")
		&& ok;

	UIFocusManager singleSaveLoadActionFocus;
	auto singleSaveAction = makeTestFocusElement();
	int singleActionSlotMoves = 0;
	auto singleActionNavigation =
		[&singleActionSlotMoves](UIFocusDirection direction)
	{
		if (direction != UIFocusDirection::Up
			&& direction != UIFocusDirection::Down)
		{
			return false;
		}
		singleActionSlotMoves++;
		return true;
	};
	const std::vector<std::string> singleActionIds =
		singleSaveLoadActionFocus.addLinearGroup(
			"single-save-load-action",
			UIFocusLinearAxis::Horizontal,
			{
				{ "load", nullptr, {}, {}, {}, singleActionNavigation },
				{ "save", singleSaveAction, {}, {}, {}, singleActionNavigation }
			});
	singleSaveLoadActionFocus.setDefaultFocus(
		singleActionIds.empty() ? std::string() : singleActionIds.front());
	ok = check(singleActionIds == std::vector<std::string>({ "save" })
		&& singleSaveLoadActionFocus.focusDefault()
		&& !singleSaveLoadActionFocus.handleAction(UIAction::NavigateLeft)
		&& !singleSaveLoadActionFocus.handleAction(UIAction::NavigateRight)
		&& singleSaveAction->isFocused(),
		"save-only action did not become the default without a self-loop") && ok;
	ok = check(singleSaveLoadActionFocus.handleAction(UIAction::NavigateUp)
		&& singleActionSlotMoves == 1
		&& singleSaveAction->isFocused(),
		"single save/load action lost its vertical slot navigation") && ok;

	UIFocusManager availabilityFocus;
	auto hidden = makeTestFocusElement();
	hidden->visible = false;
	const std::vector<std::string> availabilityIds =
		availabilityFocus.addLinearGroup(
			"availability",
			UIFocusLinearAxis::Vertical,
			{
				{ "hidden", hidden }
			});
	availabilityFocus.setDefaultFocus("hidden");
	ok = check(availabilityIds.size() == 1
		&& availabilityIds.front() == "hidden"
		&& !availabilityFocus.focusDefault(),
		"linear group changed the manager's existing availability policy") && ok;
	return ok;
}

bool testVisualSpatialFocusGroups()
{
	bool ok = true;
	UIFocusManager focus;
	auto equip = makeTestFocusElement(0, 0, 21, 41);
	auto goods = makeTestFocusElement(39, 0, 24, 41);
	auto notes = makeTestFocusElement(0, 41, 63, 19);
	auto options = makeTestFocusElement(21, 9, 18, 32);
	const std::vector<std::string> ids = focus.addVisualSpatialGroup(
		"xjxqy-top-buttons",
		{
			{ "equip", equip },
			{ "goods", goods },
			{ "notes", notes },
			{ "options", options },
		});
	focus.setDefaultFocus(ids.empty() ? std::string() : ids.front());

	ok = check(ids == std::vector<std::string>(
			{ "equip", "options", "notes", "goods" })
			&& focus.focusDefault()
			&& focus.getFocusedNodeId() == "equip",
		"visual spatial group did not retain the previous leftmost default")
		&& ok;
	ok = check(focus.handleAction(UIAction::NavigateRight)
			&& focus.getFocusedNodeId() == "options"
			&& focus.handleAction(UIAction::NavigateRight)
			&& focus.getFocusedNodeId() == "goods",
		"XJXQY corner-menu right navigation did not follow equip-options-goods")
		&& ok;
	ok = check(focus.handleAction(UIAction::NavigateLeft)
			&& focus.getFocusedNodeId() == "options"
			&& focus.handleAction(UIAction::NavigateDown)
			&& focus.getFocusedNodeId() == "notes"
			&& focus.handleAction(UIAction::NavigateUp)
			&& focus.getFocusedNodeId() == "options",
		"XJXQY corner-menu vertical navigation did not follow drawn geometry")
		&& ok;
	ok = check(focus.focusNode("equip")
			&& focus.handleAction(UIAction::NavigateDown)
			&& focus.getFocusedNodeId() == "notes",
		"direction-cone priority did not keep equip-down inside the lower"
		" corner-menu region") && ok;
	return ok;
}

bool testConfigDrivenFocusNavigation()
{
	bool ok = true;
	ConfigDrivenFocusTestPanel panel;
	ConfigDrivenPanel::ComponentDefinition sourceDefinition;
	sourceDefinition.name = "sourceComponent";
	sourceDefinition.controllerUp = "unregisteredTargetComponent";
	sourceDefinition.controllerDown = "unavailableTargetComponent";
	sourceDefinition.controllerLeft = "missingTargetComponent";
	sourceDefinition.controllerRight = "explicitTargetComponent";
	ConfigDrivenPanel::ComponentDefinition explicitTargetDefinition;
	explicitTargetDefinition.name = "explicitTargetComponent";
	ConfigDrivenPanel::ComponentDefinition unavailableTargetDefinition;
	unavailableTargetDefinition.name = "unavailableTargetComponent";
	ConfigDrivenPanel::ComponentDefinition unregisteredTargetDefinition;
	unregisteredTargetDefinition.name = "unregisteredTargetComponent";
	panel.setDefinitions(
		{
			sourceDefinition,
			explicitTargetDefinition,
			unavailableTargetDefinition,
			unregisteredTargetDefinition,
		});

	UIFocusManager focus;
	auto source = makeTestFocusElement(100, 100);
	auto explicitTarget = makeTestFocusElement(0, 0);
	auto spatialUp = makeTestFocusElement(100, 60);
	auto spatialDown = makeTestFocusElement(100, 140);
	auto spatialLeft = makeTestFocusElement(60, 100);
	auto unavailableTarget = makeTestFocusElement(100, 180);
	unavailableTarget->visible = false;
	focus.addVisualSpatialGroup(
		"config-driven-navigation",
		{
			{ "source", source },
			{ "explicit-target", explicitTarget },
			{ "spatial-up", spatialUp },
			{ "spatial-down", spatialDown },
			{ "spatial-left", spatialLeft },
			{ "unavailable-target", unavailableTarget },
		});
	focus.applyConfigDrivenFocusNavigation(
		panel,
		{
			{ "sourceComponent", "source" },
			{ "explicitTargetComponent", "explicit-target" },
			{ "unavailableTargetComponent", "unavailable-target" },
			{ "unregisteredTargetComponent", "unregistered-target" },
		});

	ok = check(focus.focusNode("source")
			&& focus.handleAction(UIAction::NavigateRight)
			&& focus.getFocusedNodeId() == "explicit-target",
		"config-driven focus did not override geometry with a valid explicit target")
		&& ok;
	ok = check(focus.focusNode("source")
			&& focus.handleAction(UIAction::NavigateLeft)
			&& focus.getFocusedNodeId() == "spatial-left",
		"missing config-driven target suppressed spatial focus fallback") && ok;
	ok = check(focus.focusNode("source")
			&& focus.handleAction(UIAction::NavigateDown)
			&& focus.getFocusedNodeId() == "spatial-down",
		"unavailable config-driven target suppressed spatial focus fallback") && ok;
	unavailableTarget->visible = true;
	ok = check(focus.focusNode("source")
			&& focus.handleAction(UIAction::NavigateDown)
			&& focus.getFocusedNodeId() == "unavailable-target",
		"config-driven focus did not restore an explicit edge after its target"
		" became available") && ok;
	ok = check(focus.focusNode("source")
			&& focus.handleAction(UIAction::NavigateUp)
			&& focus.getFocusedNodeId() == "spatial-up",
		"unregistered config-driven target suppressed spatial focus fallback") && ok;

	ConfigDrivenFocusTestPanel guardedPanel;
	ConfigDrivenPanel::ComponentDefinition guardedSourceDefinition;
	guardedSourceDefinition.name = "guardedSourceComponent";
	guardedSourceDefinition.controllerUp = "duplicateTargetComponent";
	guardedSourceDefinition.controllerDown = "guardedSourceComponent";
	guardedSourceDefinition.controllerLeft = "CaseTargetComponent";
	ConfigDrivenPanel::ComponentDefinition duplicateTargetDefinition;
	duplicateTargetDefinition.name = "duplicateTargetComponent";
	ConfigDrivenPanel::ComponentDefinition caseTargetDefinition;
	caseTargetDefinition.name = "caseTargetComponent";
	guardedPanel.setDefinitions(
		{
			guardedSourceDefinition,
			duplicateTargetDefinition,
			duplicateTargetDefinition,
			caseTargetDefinition,
		});

	UIFocusManager guardedFocus;
	auto guardedSource = makeTestFocusElement(100, 100);
	auto guardedSpatialUp = makeTestFocusElement(100, 60);
	auto guardedSpatialDown = makeTestFocusElement(100, 140);
	auto guardedSpatialLeft = makeTestFocusElement(60, 100);
	auto duplicateTarget = makeTestFocusElement(100, 180);
	auto caseTarget = makeTestFocusElement(180, 100);
	guardedFocus.addVisualSpatialGroup(
		"guarded-config-driven-navigation",
		{
			{ "guarded-source", guardedSource },
			{ "guarded-spatial-up", guardedSpatialUp },
			{ "guarded-spatial-down", guardedSpatialDown },
			{ "guarded-spatial-left", guardedSpatialLeft },
			{ "duplicate-target", duplicateTarget },
			{ "case-target", caseTarget },
		});
	guardedFocus.applyConfigDrivenFocusNavigation(
		guardedPanel,
		{
			{ "guardedSourceComponent", "guarded-source" },
			{ "duplicateTargetComponent", "duplicate-target" },
			{ "caseTargetComponent", "case-target" },
		});

	ok = check(guardedFocus.focusNode("guarded-source")
			&& guardedFocus.handleAction(UIAction::NavigateUp)
			&& guardedFocus.getFocusedNodeId() == "guarded-spatial-up",
		"duplicate config-driven target name suppressed spatial focus fallback")
		&& ok;
	ok = check(guardedFocus.focusNode("guarded-source")
			&& guardedFocus.handleAction(UIAction::NavigateDown)
			&& guardedFocus.getFocusedNodeId() == "guarded-spatial-down",
		"self-referential config-driven target suppressed spatial focus fallback")
		&& ok;
	ok = check(guardedFocus.focusNode("guarded-source")
			&& guardedFocus.handleAction(UIAction::NavigateLeft)
			&& guardedFocus.getFocusedNodeId() == "guarded-spatial-left",
		"case-mismatched config-driven target suppressed spatial focus fallback")
		&& ok;
	return ok;
}

bool testConfigDrivenFocusNavigationMenuParsing()
{
	bool ok = true;
	std::error_code pathError;
	const std::filesystem::path temporaryBase =
		std::filesystem::temp_directory_path(pathError);
	if (pathError)
	{
		return check(false,
			"config-driven focus parser test could not resolve a temporary directory");
	}
	const std::string uniqueSuffix = std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count());
	const std::filesystem::path temporaryRoot =
		temporaryBase / ("jxqy-ui-focus-navigation-" + uniqueSuffix);
	if (!std::filesystem::create_directory(temporaryRoot, pathError)
		|| pathError)
	{
		return check(false,
			"config-driven focus parser test could not create its temporary root");
	}

	const std::filesystem::path componentPath =
		temporaryRoot / "controller-component.ini";
	const std::filesystem::path menuPath =
		temporaryRoot / "controller-navigation.menu.ini";
	const std::filesystem::path firstSubMenuPath =
		temporaryRoot / "controller-navigation-child-one.menu.ini";
	const std::filesystem::path secondSubMenuPath =
		temporaryRoot / "controller-navigation-child-two.menu.ini";
	{
		std::ofstream componentFile(componentPath, std::ios::binary);
		componentFile
			<< "[Init]\n"
			<< "Left=0\n"
			<< "Top=0\n"
			<< "Width=1\n"
			<< "Height=1\n";
		ok = check(componentFile.good(),
			"config-driven focus parser test could not write its component fixture")
			&& ok;
	}
	{
		std::ofstream menuFile(menuPath, std::ios::binary);
		menuFile
			<< "[menu]\n"
			<< "name=controllerNavigationTest\n"
			<< "[component1]\n"
			<< "type=Item\n"
			<< "name=sourceComponent\n"
			<< "file=controller-component.ini\n"
			<< "controllerup=upComponent\n"
			<< "controllerdown=downComponent\n"
			<< "controllerleft=leftComponent\n"
			<< "controllerright=rightComponent\n";
		const std::array<const char*, 4> targetNames =
		{
			"upComponent",
			"downComponent",
			"leftComponent",
			"rightComponent",
		};
		for (std::size_t index = 0; index < targetNames.size(); index++)
		{
			menuFile
				<< "[component" << index + 2 << "]\n"
				<< "type=Item\n"
				<< "name=" << targetNames[index] << "\n"
				<< "file=controller-component.ini\n";
		}
		menuFile
			<< "[submenu1]\n"
			<< "name=childOne\n"
			<< "file=controller-navigation-child-one.menu.ini\n"
			<< "[submenu2]\n"
			<< "name=childTwo\n"
			<< "file=controller-navigation-child-two.menu.ini\n";
		ok = check(menuFile.good(),
			"config-driven focus parser test could not write its menu fixture")
			&& ok;
	}
	auto writeSubMenu = [&ok, &componentPath](
		const std::filesystem::path& path,
		const char* menuName)
	{
		std::ofstream menuFile(path, std::ios::binary);
		menuFile
			<< "[menu]\n"
			<< "name=" << menuName << "\n"
			<< "[component1]\n"
			<< "type=Item\n"
			<< "name=sourceComponent\n"
			<< "file=" << componentPath.filename().generic_string() << "\n"
			<< "controllerup=upComponent\n"
			<< "controllerdown=temporarilyHiddenComponent\n"
			<< "controllerleft=unregisteredDecoration\n"
			<< "controllerright=rightComponent\n"
			<< "[component2]\n"
			<< "type=Item\n"
			<< "name=rightComponent\n"
			<< "file=" << componentPath.filename().generic_string() << "\n"
			<< "[component3]\n"
			<< "type=Item\n"
			<< "name=temporarilyHiddenComponent\n"
			<< "file=" << componentPath.filename().generic_string() << "\n"
			<< "[component4]\n"
			<< "type=Item\n"
			<< "name=spatialUpComponent\n"
			<< "file=" << componentPath.filename().generic_string() << "\n"
			<< "[component5]\n"
			<< "type=Item\n"
			<< "name=spatialDownComponent\n"
			<< "file=" << componentPath.filename().generic_string() << "\n"
			<< "[component6]\n"
			<< "type=Item\n"
			<< "name=spatialLeftComponent\n"
			<< "file=" << componentPath.filename().generic_string() << "\n"
			<< "[component7]\n"
			<< "type=Item\n"
			<< "name=unregisteredDecoration\n"
			<< "file=" << componentPath.filename().generic_string() << "\n";
		ok = check(menuFile.good(),
			std::string("config-driven focus parser test could not write ")
				+ menuName + " fixture") && ok;
	};
	writeSubMenu(firstSubMenuPath, "childOne");
	writeSubMenu(secondSubMenuPath, "childTwo");

	ConfigDrivenFocusTestPanel parsedPanel;
	const std::string previousActiveResourceRoot =
		File::getActiveResourceRoot();
	File::setActiveResourceRoot(temporaryRoot.generic_string());
	parsedPanel.loadMenuDefinition("controller-navigation.menu.ini");
	File::setActiveResourceRoot(previousActiveResourceRoot);

	UIFocusManager parsedFocus;
	auto parsedSource = makeTestFocusElement(100, 100);
	auto parsedUp = makeTestFocusElement(100, 180);
	auto parsedDown = makeTestFocusElement(100, 20);
	auto parsedLeft = makeTestFocusElement(180, 100);
	auto parsedRight = makeTestFocusElement(20, 100);
	parsedFocus.addVisualSpatialGroup(
		"parsed-config-driven-navigation",
		{
			{ "parsed-source", parsedSource },
			{ "parsed-up", parsedUp },
			{ "parsed-down", parsedDown },
			{ "parsed-left", parsedLeft },
			{ "parsed-right", parsedRight },
		});
	parsedFocus.applyConfigDrivenFocusNavigation(
		parsedPanel,
		{
			{ "sourceComponent", "parsed-source" },
			{ "upComponent", "parsed-up" },
			{ "downComponent", "parsed-down" },
			{ "leftComponent", "parsed-left" },
			{ "rightComponent", "parsed-right" },
		});

	ok = check(parsedFocus.focusNode("parsed-source")
			&& parsedFocus.handleAction(UIAction::NavigateUp)
			&& parsedFocus.getFocusedNodeId() == "parsed-up",
		"canonical controllerup key was not parsed into production focus navigation")
		&& ok;
	ok = check(parsedFocus.focusNode("parsed-source")
			&& parsedFocus.handleAction(UIAction::NavigateDown)
			&& parsedFocus.getFocusedNodeId() == "parsed-down",
		"canonical controllerdown key was not parsed into production focus navigation")
		&& ok;
	ok = check(parsedFocus.focusNode("parsed-source")
			&& parsedFocus.handleAction(UIAction::NavigateLeft)
			&& parsedFocus.getFocusedNodeId() == "parsed-left",
		"canonical controllerleft key was not parsed into production focus navigation")
		&& ok;
	ok = check(parsedFocus.focusNode("parsed-source")
			&& parsedFocus.handleAction(UIAction::NavigateRight)
			&& parsedFocus.getFocusedNodeId() == "parsed-right",
		"canonical controllerright key was not parsed into production focus navigation")
		&& ok;

	const ConfigDrivenPanel* firstSubMenu = parsedPanel.getSubMenuPanel(0);
	const ConfigDrivenPanel* secondSubMenu = parsedPanel.getSubMenuPanel(1);
	ok = check(parsedPanel.getSubMenuDefinitions().size() == 2
			&& firstSubMenu != nullptr
			&& secondSubMenu != nullptr
			&& firstSubMenu != secondSubMenu
			&& firstSubMenu->name == "childOne"
			&& secondSubMenu->name == "childTwo"
			&& parsedPanel.getSubMenuPanel(2) == nullptr,
		"config-driven panel did not expose stable read-only submenu scopes")
		&& ok;
	auto oldFirstSource = firstSubMenu != nullptr
		? firstSubMenu->getComponentByName("sourceComponent") : nullptr;
	auto oldSecondSource = secondSubMenu != nullptr
		? secondSubMenu->getComponentByName("sourceComponent") : nullptr;
	ok = check(oldFirstSource != nullptr && oldSecondSource != nullptr
			&& oldFirstSource != oldSecondSource,
		"config-driven submenu scopes shared component instances") && ok;

	auto exerciseSubMenuScope =
		[&ok](const ConfigDrivenPanel* subMenu,
			UIFocusManager& scopedFocus,
			const std::string& scopeId,
			const std::string& expectedRightNode)
	{
		if (!check(subMenu != nullptr,
			scopeId + " submenu scope was not loaded"))
		{
			ok = false;
			return;
		}

		auto source = subMenu->getComponentByName("sourceComponent");
		auto right = subMenu->getComponentByName("rightComponent");
		auto temporarilyHidden =
			subMenu->getComponentByName("temporarilyHiddenComponent");
		auto spatialUp = subMenu->getComponentByName("spatialUpComponent");
		auto spatialDown = subMenu->getComponentByName("spatialDownComponent");
		auto spatialLeft = subMenu->getComponentByName("spatialLeftComponent");
		auto unregistered =
			subMenu->getComponentByName("unregisteredDecoration");
		if (!check(source != nullptr && right != nullptr
			&& temporarilyHidden != nullptr && spatialUp != nullptr
			&& spatialDown != nullptr && spatialLeft != nullptr
			&& unregistered != nullptr,
			scopeId + " submenu components were not loaded"))
		{
			ok = false;
			return;
		}

		source->rect = { 100, 100, 20, 20 };
		right->rect = { 20, 100, 20, 20 };
		temporarilyHidden->rect = { 100, 180, 20, 20 };
		temporarilyHidden->visible = false;
		spatialUp->rect = { 100, 60, 20, 20 };
		spatialDown->rect = { 100, 140, 20, 20 };
		spatialLeft->rect = { 60, 100, 20, 20 };
		unregistered->rect = { 80, 100, 20, 20 };

		scopedFocus.clear();
		scopedFocus.addVisualSpatialGroup(
			scopeId,
			{
				{ scopeId + "/source", source },
				{ expectedRightNode, right },
				{ scopeId + "/temporarily-hidden", temporarilyHidden },
				{ scopeId + "/spatial-up", spatialUp },
				{ scopeId + "/spatial-down", spatialDown },
				{ scopeId + "/spatial-left", spatialLeft },
			});
		scopedFocus.applyConfigDrivenFocusNavigation(
			*subMenu,
			{
				{ "sourceComponent", scopeId + "/source" },
				{ "rightComponent", expectedRightNode },
				{ "temporarilyHiddenComponent",
					scopeId + "/temporarily-hidden" },
				{ "spatialUpComponent", scopeId + "/spatial-up" },
				{ "spatialDownComponent", scopeId + "/spatial-down" },
				{ "spatialLeftComponent", scopeId + "/spatial-left" },
			});

		ok = check(scopedFocus.focusNode(scopeId + "/source")
				&& scopedFocus.handleAction(UIAction::NavigateRight)
				&& scopedFocus.getFocusedNodeId() == expectedRightNode,
			scopeId + " explicit submenu edge was not applied in its own scope")
			&& ok;
		ok = check(scopedFocus.focusNode(scopeId + "/source")
				&& scopedFocus.handleAction(UIAction::NavigateUp)
				&& scopedFocus.getFocusedNodeId() == scopeId + "/spatial-up",
			scopeId + " cross-scope target did not fall back inside its scope")
			&& ok;
		ok = check(scopedFocus.focusNode(scopeId + "/source")
				&& scopedFocus.handleAction(UIAction::NavigateLeft)
				&& scopedFocus.getFocusedNodeId() == scopeId + "/spatial-left",
			scopeId + " unregistered submenu target acquired focus")
			&& ok;
		ok = check(scopedFocus.focusNode(scopeId + "/source")
				&& scopedFocus.handleAction(UIAction::NavigateDown)
				&& scopedFocus.getFocusedNodeId() == scopeId + "/spatial-down",
			scopeId + " hidden submenu target suppressed spatial fallback")
			&& ok;
		temporarilyHidden->visible = true;
		ok = check(scopedFocus.focusNode(scopeId + "/source")
				&& scopedFocus.handleAction(UIAction::NavigateDown)
				&& scopedFocus.getFocusedNodeId()
					== scopeId + "/temporarily-hidden",
			scopeId + " explicit submenu edge did not recover with its target")
			&& ok;
	};

	UIFocusManager firstSubMenuFocus;
	UIFocusManager secondSubMenuFocus;
	exerciseSubMenuScope(
		firstSubMenu,
		firstSubMenuFocus,
		"child-one",
		"child-one/right");
	exerciseSubMenuScope(
		secondSubMenu,
		secondSubMenuFocus,
		"child-two",
		"child-two/right");
	ok = check(firstSubMenuFocus.focusNode("child-one/source")
			&& secondSubMenuFocus.focusNode("child-two/source")
			&& firstSubMenuFocus.handleAction(UIAction::NavigateRight)
			&& firstSubMenuFocus.getFocusedNodeId() == "child-one/right"
			&& secondSubMenuFocus.handleAction(UIAction::NavigateLeft)
			&& secondSubMenuFocus.getFocusedNodeId()
				== "child-two/spatial-left"
			&& firstSubMenuFocus.getFocusedNodeId() == "child-one/right",
		"simultaneously active submenu focus managers crossed scope boundaries")
		&& ok;
	ok = check(parsedFocus.focusNode("parsed-source")
			&& parsedFocus.handleAction(UIAction::NavigateRight)
			&& parsedFocus.getFocusedNodeId() == "parsed-right",
		"same-named submenu components changed the root scope navigation")
		&& ok;

	File::setActiveResourceRoot(temporaryRoot.generic_string());
	parsedPanel.init();
	File::setActiveResourceRoot(previousActiveResourceRoot);
	const ConfigDrivenPanel* reloadedFirstSubMenu =
		parsedPanel.getSubMenuPanel(0);
	const ConfigDrivenPanel* reloadedSecondSubMenu =
		parsedPanel.getSubMenuPanel(1);
	auto reloadedFirstSource = reloadedFirstSubMenu != nullptr
		? reloadedFirstSubMenu->getComponentByName("sourceComponent") : nullptr;
	auto reloadedSecondSource = reloadedSecondSubMenu != nullptr
		? reloadedSecondSubMenu->getComponentByName("sourceComponent") : nullptr;
	ok = check(parsedPanel.getSubMenuDefinitions().size() == 2
			&& parsedPanel.getSubMenuDefinitions()[0].name == "childOne"
			&& parsedPanel.getSubMenuDefinitions()[1].name == "childTwo"
			&& reloadedFirstSubMenu != nullptr
			&& reloadedSecondSubMenu != nullptr
			&& reloadedFirstSubMenu != reloadedSecondSubMenu
			&& reloadedFirstSubMenu->name == "childOne"
			&& reloadedSecondSubMenu->name == "childTwo"
			&& parsedPanel.getSubMenuPanel(2) == nullptr
			&& reloadedFirstSource != nullptr
			&& reloadedSecondSource != nullptr
			&& oldFirstSource != nullptr
			&& oldSecondSource != nullptr
			&& reloadedFirstSource != oldFirstSource
			&& reloadedSecondSource != oldSecondSource
			&& oldFirstSource->parent == nullptr
			&& oldSecondSource->parent == nullptr,
		"config-driven submenu scope order was not stable after reload")
		&& ok;
	UIFocusManager reloadedFirstSubMenuFocus;
	UIFocusManager reloadedSecondSubMenuFocus;
	exerciseSubMenuScope(
		reloadedFirstSubMenu,
		reloadedFirstSubMenuFocus,
		"child-one",
		"child-one/right");
	exerciseSubMenuScope(
		reloadedSecondSubMenu,
		reloadedSecondSubMenuFocus,
		"child-two",
		"child-two/right");

	std::error_code cleanupError;
	auto removeFixturePath = [&ok, &cleanupError](
		const std::filesystem::path& path)
	{
		cleanupError.clear();
		const bool removed = std::filesystem::remove(path, cleanupError);
		ok = check(removed && !cleanupError,
			"config-driven focus parser test did not clean " + path.string())
			&& ok;
	};
	removeFixturePath(menuPath);
	removeFixturePath(firstSubMenuPath);
	removeFixturePath(secondSubMenuPath);
	removeFixturePath(componentPath);
	removeFixturePath(temporaryRoot);
	return ok;
}

bool testGroupedFocusRecovery()
{
	bool ok = true;
	UIFocusManager focus;
	auto domainDefault = makeTestFocusElement(0, 0);
	auto anchor = makeTestFocusElement(20, 20);
	auto sameRow = makeTestFocusElement(40, 20);
	auto sameColumn = makeTestFocusElement(20, 0);
	auto otherGroup = makeTestFocusElement(60, 20);
	focus.addNode(
		"default", domainDefault, { "fallback", 0, 0 });
	focus.addNode(
		"anchor", anchor, { "inventory", 1, 1 });
	focus.addNode(
		"same-row", sameRow, { "inventory", 1, 2 });
	focus.addNode(
		"same-column", sameColumn, { "inventory", 0, 1 });
	focus.addNode(
		"other-group", otherGroup, { "equipment", 1, 1 });
	focus.setDefaultFocus("default");

	ok = check(focus.focusNode("anchor"),
		"grouped recovery test could not focus its anchor") && ok;
	anchor->visible = false;
	ok = check(focus.restoreFocus()
		&& focus.getFocusedNodeId() == "same-row"
		&& sameRow->isFocused() && !anchor->isFocused(),
		"hidden focus did not recover to the nearest same-group node") && ok;

	anchor->visible = true;
	sameRow->activated = false;
	focus.focusNode("anchor");
	anchor->activated = false;
	ok = check(focus.restoreFocus()
		&& focus.getFocusedNodeId() == "same-column",
		"disabled focus did not skip an unavailable same-group node") && ok;

	anchor->activated = true;
	sameColumn->activated = false;
	focus.focusNode("anchor");
	anchor->activated = false;
	ok = check(focus.restoreFocus()
		&& focus.getFocusedNodeId() == "default",
		"focus did not use the domain default after its group became unavailable") && ok;

	UIFocusManager deletedFocus;
	auto deletedAnchor = makeTestFocusElement(0, 40);
	auto deletedFallback = makeTestFocusElement(0, 60);
	deletedFocus.addNode(
		"deleted", deletedAnchor, { "deleted-group", 2, 0 });
	deletedFocus.addNode(
		"survivor", deletedFallback, { "deleted-group", 3, 0 });
	deletedFocus.focusNode("deleted");
	deletedAnchor.reset();
	ok = check(deletedFocus.restoreFocus()
		&& deletedFocus.getFocusedNodeId() == "survivor",
		"expired focus element did not recover within its group") && ok;

	UIFocusManager legacyFocus;
	auto legacyDefault = makeTestFocusElement(0, 80);
	auto legacyAnchor = makeTestFocusElement(20, 80);
	auto legacyNearby = makeTestFocusElement(40, 80);
	legacyFocus.addNode("legacy-default", legacyDefault);
	legacyFocus.addNode("legacy-anchor", legacyAnchor);
	legacyFocus.addNode("legacy-nearby", legacyNearby);
	legacyFocus.setDefaultFocus("legacy-default");
	legacyFocus.focusNode("legacy-anchor");
	legacyAnchor->visible = false;
	ok = check(legacyFocus.restoreFocus()
		&& legacyFocus.getFocusedNodeId() == "legacy-default",
		"legacy nodes without layout metadata changed fallback behavior") && ok;

	UIFocusManager navigationFocus;
	auto navigationAnchor = makeTestFocusElement(0, 100);
	auto navigationFallback = makeTestFocusElement(20, 100);
	navigationFocus.addNode(
		"navigation-anchor",
		navigationAnchor,
		{ "navigation-group", 0, 0 },
		UIFocusManager::ActionHandler(),
		UIFocusManager::ActionHandler(),
		[navigationAnchor](UIFocusDirection)
		{
			navigationAnchor->activated = false;
			return false;
		});
	navigationFocus.addNode(
		"navigation-fallback",
		navigationFallback,
		{ "navigation-group", 0, 1 });
	navigationFocus.focusNode("navigation-anchor");
	ok = check(navigationFocus.handleAction(UIAction::NavigateRight)
		&& navigationFocus.getFocusedNodeId() == "navigation-fallback",
		"navigation-time invalidation did not use grouped focus recovery") && ok;

	UIFocusManager emptyFocus;
	auto onlyNode = makeTestFocusElement(0, 120);
	emptyFocus.addNode("only", onlyNode, { "only-group", 0, 0 });
	emptyFocus.focusNode("only");
	onlyNode->activated = false;
	ok = check(!emptyFocus.restoreFocus()
		&& emptyFocus.getFocusedNodeId().empty()
		&& !onlyNode->isFocused(),
		"empty focus domain retained an invalid logical or visual focus") && ok;
	return ok;
}

bool testMemoRightStickScrolling()
{
	bool ok = true;
	MemoMenu memo;
	memo.memoText = nullptr;
	memo.scrollbar = std::make_shared<Scrollbar>();
	memo.scrollbar->min = 0;
	memo.scrollbar->max = 4;
	memo.scrollbar->position = 0;
	ok = check(memo.handleUIAction(UIAction::ScrollDown)
		&& memo.scrollbar->position == 1,
		"memo did not route right-stick scroll down through its scrollbar") && ok;
	ok = check(memo.handleUIAction(UIAction::ScrollUp)
		&& memo.scrollbar->position == 0,
		"memo did not route right-stick scroll up through its scrollbar") && ok;
	ok = check(!memo.handleUIAction(UIAction::ScrollUp)
			&& memo.scrollbar->position == 0,
		"memo did not release a right-stick scroll at its upper boundary") && ok;
	return ok;
}

bool testSlotGridScrollingAndSelection()
{
	bool ok = true;
	std::vector<std::shared_ptr<Item>> items;
	for (int index = 0; index < 6; index++)
	{
		auto item = std::make_shared<Item>();
		item->rect = { (index % 3) * 30, (index / 3) * 30, 20, 20 };
		items.push_back(item);
	}
	auto scrollbar = std::make_shared<Scrollbar>();
	scrollbar->min = 0;
	scrollbar->max = 3;
	scrollbar->position = 0;
	scrollbar->lineSize = 3;
	scrollbar->pageSize = 6;
	int refreshes = 0;
	int confirmedLogicalIndex = -1;
	int detailedLogicalIndex = -1;

	SlotGridView view;
	view.items = items;
	view.scrollbar = scrollbar;
	view.resolveLogicalIndex = [scrollbar](int visibleIndex)
	{
		return visibleIndex + scrollbar->position * scrollbar->lineSize;
	};
	view.refreshAfterScroll = [&refreshes]() { refreshes++; };

	SlotGridBinding binding;
	binding.focusIdPrefix = "test-slot-";
	binding.fixedColumnCount = 3;
	binding.primary = [&confirmedLogicalIndex](int logicalIndex, int)
	{
		confirmedLogicalIndex = logicalIndex;
	};
	binding.details = [&detailedLogicalIndex](int logicalIndex, int)
	{
		detailedLogicalIndex = logicalIndex;
	};
	applySlotGridView(binding, std::move(view));
	ok = check(binding.focusIdPrefix == "test-slot-"
		&& binding.fixedColumnCount == 3
		&& static_cast<bool>(binding.primary)
		&& static_cast<bool>(binding.details)
		&& binding.items == items
		&& binding.scrollbar == scrollbar
		&& static_cast<bool>(binding.resolveLogicalIndex)
		&& static_cast<bool>(binding.refreshAfterScroll),
		"slot-grid view transfer preserves consumer-owned binding behavior") && ok;

	SlotGridController grid;
	grid.bind(std::move(binding));
	ok = check(grid.activate() && grid.focusedVisibleIndex() == 0,
		"slot grid activates its first available item") && ok;
	ok = check(grid.handleAction(UIAction::NavigateRight)
		&& grid.focusedVisibleIndex() == 1,
		"slot grid navigates within a row") && ok;
	ok = check(grid.handleAction(UIAction::NavigateDown)
		&& grid.focusedVisibleIndex() == 4,
		"slot grid navigates to the next visible row") && ok;
	ok = check(grid.handleAction(UIAction::NavigateDown)
		&& scrollbar->position == 1 && grid.focusedVisibleIndex() == 4
		&& grid.focusedLogicalIndex() == 7 && refreshes == 1,
		"leaving the bottom row scrolls one logical row and preserves the column") && ok;
	ok = check(grid.handleAction(UIAction::Confirm)
		&& confirmedLogicalIndex == 7,
		"actions resolve the post-scroll logical index") && ok;
	ok = check(grid.handleAction(UIAction::Details)
		&& detailedLogicalIndex == 7,
		"slot-grid view transfer preserves consumer-owned details behavior") && ok;
	ok = check(grid.handleAction(UIAction::PageNext)
		&& scrollbar->position == 3 && refreshes == 2,
		"page navigation advances by the visible row count and clamps") && ok;
	grid.refreshSelection([](int logicalIndex) { return logicalIndex == 10; });
	ok = check(items[1]->isTransferSelected() && !items[0]->isTransferSelected(),
		"transfer highlight follows logical rather than visible index") && ok;
	grid.deactivate();
	ok = check(!items[4]->isFocused() && !grid.isActive(),
		"deactivation clears focus visuals") && ok;
	ok = check(grid.activate() && grid.focusedVisibleIndex() == 4,
		"reactivation restores the last visible focus") && ok;
	return ok;
}

bool testSlotGridRebindFocusRecovery()
{
	bool ok = true;
	auto makeItems = []()
	{
		std::vector<std::shared_ptr<Item>> items;
		for (int index = 0; index < 6; index++)
		{
			auto item = std::make_shared<Item>();
			item->rect =
			{
				(index % 3) * 30,
				(index / 3) * 30,
				20,
				20
			};
			items.push_back(item);
		}
		return items;
	};
	auto makeScrollbar = []()
	{
		auto scrollbar = std::make_shared<Scrollbar>();
		scrollbar->min = 0;
		scrollbar->max = 3;
		scrollbar->position = 0;
		scrollbar->lineSize = 3;
		scrollbar->pageSize = 6;
		return scrollbar;
	};
	auto makeBinding = [](
		const std::vector<std::shared_ptr<Item>>& items,
		const std::shared_ptr<Scrollbar>& scrollbar,
		int& confirmedLogicalIndex,
		int& refreshes)
	{
		SlotGridBinding binding;
		binding.focusIdPrefix = "rebind-slot-";
		binding.items = items;
		binding.scrollbar = scrollbar;
		binding.resolveLogicalIndex = [scrollbar](int visibleIndex)
		{
			return visibleIndex
				+ scrollbar->position * scrollbar->lineSize;
		};
		binding.primary = [&confirmedLogicalIndex](int logicalIndex, int)
		{
			confirmedLogicalIndex = logicalIndex;
		};
		binding.refreshAfterScroll = [&refreshes]() { refreshes++; };
		return binding;
	};

	int confirmedLogicalIndex = -1;
	int refreshes = 0;
	auto originalItems = makeItems();
	auto originalScrollbar = makeScrollbar();
	SlotGridController grid;
	grid.bind(makeBinding(
		originalItems, originalScrollbar, confirmedLogicalIndex, refreshes));
	ok = check(grid.activate(),
		"config-driven slot owner activates before resize") && ok;
	grid.handleAction(UIAction::NavigateRight);
	grid.handleAction(UIAction::NavigateDown);
	grid.handleAction(UIAction::NavigateDown);
	ok = check(grid.focusedLogicalIndex() == 7
		&& originalScrollbar->position == 1,
		"slot owner reaches a scrolled logical focus before resize") && ok;

	grid.clear();
	auto rebuiltItems = makeItems();
	auto rebuiltScrollbar = makeScrollbar();
	grid.bind(makeBinding(
		rebuiltItems, rebuiltScrollbar, confirmedLogicalIndex, refreshes));
	ok = check(!grid.isActive() && rebuiltScrollbar->position == 1
		&& refreshes == 2,
		"slot rebinding preserves scroll state but waits for its visible owner") && ok;
	int dispatches = 0;
	ok = check(dispatchUIActionWithFocusRecovery(
		UIAction::Confirm,
		[&grid, &dispatches](UIAction action)
		{
			dispatches++;
			return grid.handleAction(action);
		},
		[&grid]() { return grid.isActive(); },
		[&grid]() { return grid.activate(); }),
		"first semantic action restores a rebuilt slot owner and is replayed") && ok;
	ok = check(dispatches == 2 && grid.isActive()
		&& grid.focusedLogicalIndex() == 7
		&& confirmedLogicalIndex == 7
		&& grid.controllerFocusedElement() == rebuiltItems[4]
		&& !rebuiltItems[4]->isFocused(),
		"resize recovery keeps logical focus and executes the original action once") && ok;
	grid.reset();
	auto resetItems = makeItems();
	auto resetScrollbar = makeScrollbar();
	grid.bind(makeBinding(
		resetItems, resetScrollbar, confirmedLogicalIndex, refreshes));
	ok = check(grid.activate() && grid.focusedVisibleIndex() == 0
		&& resetScrollbar->position == 0,
		"terminal reset discards focus and scroll memory before a new owner binding") && ok;
	return ok;
}

bool testPointerTakeoverFocusRecovery()
{
	bool ok = true;
	auto first = std::make_shared<Item>();
	auto second = std::make_shared<Item>();
	first->rect = { 0, 0, 20, 20 };
	second->rect = { 30, 0, 20, 20 };
	int confirmedLogicalIndex = -1;
	SlotGridBinding binding;
	binding.focusIdPrefix = "pointer-takeover-slot-";
	binding.items = { first, second };
	binding.fixedColumnCount = 2;
	binding.resolveLogicalIndex = [](int visibleIndex)
	{
		return 40 + visibleIndex;
	};
	binding.primary = [&confirmedLogicalIndex](int logicalIndex, int)
	{
		confirmedLogicalIndex = logicalIndex;
	};
	SlotGridController grid;
	grid.bind(std::move(binding));
	grid.activate();
	grid.handleAction(UIAction::NavigateRight);
	grid.deactivate();
	ok = check(!grid.isActive() && !second->isFocused()
		&& grid.focusedLogicalIndex() == 41,
		"pointer takeover suspends visuals but preserves logical slot ownership") && ok;

	int dispatches = 0;
	ok = check(dispatchUIActionWithFocusRecovery(
		UIAction::Confirm,
		[&grid, &dispatches](UIAction action)
		{
			dispatches++;
			return grid.handleAction(action);
		},
		[&grid]() { return grid.isActive(); },
		[&grid]() { return grid.activate(); }),
		"fresh controller action recovers focus after pointer takeover") && ok;
	ok = check(dispatches == 2 && grid.isActive()
		&& grid.controllerFocusedElement() == second
		&& !second->isFocused()
		&& confirmedLogicalIndex == 41,
		"pointer recovery replays the same action on the preserved logical slot") && ok;
	int recoveries = 0;
	ok = check(!dispatchUIActionWithFocusRecovery(
		UIAction::Cancel,
		[&grid](UIAction action) { return grid.handleAction(action); },
		[&grid]() { return grid.isActive(); },
		[&recoveries]()
		{
			recoveries++;
			return true;
		}) && recoveries == 0,
		"unsupported action does not reactivate an already active owner") && ok;
	return ok;
}

bool testConfigDrivenMenuResizeFocusContinuity()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	GameManager gameManager;

	System system;
	const auto previousSystemOption = system.optionBtn;
	ok = check(previousSystemOption != nullptr
			&& system.focusManager.focusNode("options"),
		"system resize fixture could not select its non-default option node")
		&& ok;
	UIFocusTestAccess::resize(system, 1024, 768);
	ok = check(system.optionBtn != nullptr
			&& system.optionBtn != previousSystemOption
			&& system.focusManager.getFocusedNodeId() == "options"
			&& system.focusManager.getFocusedElement() == system.optionBtn,
		"system resize did not restore the same logical focus node") && ok;

	Option option;
	const auto previousOptionSound = option.sound != nullptr
		? option.sound->slideBtn : nullptr;
	ok = check(previousOptionSound != nullptr
			&& option.focusManager.focusNode("sound"),
		"option resize fixture could not select its sound node") && ok;
	UIFocusTestAccess::resize(option, 1024, 768);
	ok = check(option.sound != nullptr && option.sound->slideBtn != nullptr
			&& option.sound->slideBtn != previousOptionSound
			&& option.focusManager.getFocusedNodeId() == "sound"
			&& option.focusManager.getFocusedElement() == option.sound->slideBtn,
		"option resize did not restore the same logical focus node") && ok;

	SaveLoad saveLoad(true, true);
	const auto previousSaveButton = saveLoad.saveBtn;
	ok = check(previousSaveButton != nullptr
			&& UIFocusTestAccess::focusNode(saveLoad, "save"),
		"save-load resize fixture could not select its save node") && ok;
	UIFocusTestAccess::resize(saveLoad, 1024, 768);
	ok = check(saveLoad.saveBtn != nullptr
			&& saveLoad.saveBtn != previousSaveButton
			&& UIFocusTestAccess::focusedNodeId(saveLoad) == "save"
			&& UIFocusTestAccess::focusedElement(saveLoad) == saveLoad.saveBtn,
		"save-load resize did not restore the same logical focus node") && ok;

	YesNo yesNo("resize focus continuity");
	const auto previousYesButton = yesNo.yes;
	ok = check(previousYesButton != nullptr
			&& UIFocusTestAccess::focusNode(yesNo, "yes"),
		"yes-no resize fixture could not select its yes node") && ok;
	UIFocusTestAccess::resize(yesNo, 1024, 768);
	ok = check(yesNo.yes != nullptr
			&& yesNo.yes != previousYesButton
			&& UIFocusTestAccess::focusedNodeId(yesNo) == "yes"
			&& UIFocusTestAccess::focusedElement(yesNo) == yesNo.yes,
		"yes-no resize did not restore the same logical focus node") && ok;

	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	auto bottomMenu = gameManager.menu->bottomMenu;
	const auto previousBottomGoodsButton =
		bottomMenu != nullptr ? bottomMenu->goodsBtn : nullptr;
	ok = check(bottomMenu != nullptr
			&& previousBottomGoodsButton != nullptr
			&& bottomMenu->focusControllerElement(previousBottomGoodsButton),
		"bottom resize fixture could not select a menu-button node") && ok;
	if (bottomMenu != nullptr)
	{
		UIFocusTestAccess::resize(*bottomMenu, 1024, 768);
		ok = check(bottomMenu->goodsBtn != nullptr
				&& bottomMenu->goodsBtn != previousBottomGoodsButton
				&& bottomMenu->isControllerFocusActive()
				&& bottomMenu->controllerFocusedElement()
					== bottomMenu->goodsBtn,
			"bottom resize did not restore its menu-button focus region and node")
			&& ok;

		ok = check(bottomMenu->focusControllerMagicQuick()
				&& bottomMenu->handleUIAction(UIAction::NavigateRight)
				&& bottomMenu->magicItem[1] != nullptr
				&& bottomMenu->controllerFocusedElement()
					== bottomMenu->magicItem[1],
			"bottom resize fixture could not select a non-default pane item")
			&& ok;
		const auto previousMagicQuickItem = bottomMenu->magicItem[1];
		UIFocusTestAccess::resize(*bottomMenu, 1024, 768);
		ok = check(bottomMenu->magicItem[1] != nullptr
				&& bottomMenu->magicItem[1] != previousMagicQuickItem
				&& bottomMenu->isControllerFocusActive()
				&& bottomMenu->controllerFocusedElement()
					== bottomMenu->magicItem[1],
			"bottom resize did not restore its active pane and logical item")
			&& ok;
	}
	return ok;
}

bool testBottomMenuSpatialQuickSlotConnectivity()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	Engine* testEngine = Engine::getInstance();
	int previousWindowWidth = 0;
	int previousWindowHeight = 0;
	testEngine->getWindowSize(previousWindowWidth, previousWindowHeight);
	testEngine->setWindowSize(800, 600);

	GameManager gameManager;
	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	const auto bottomMenu = gameManager.menu->bottomMenu;
	if (!check(bottomMenu != nullptr,
		"JXQY2 bottom spatial fixture did not create the bottom menu"))
	{
		return false;
	}

	const std::vector<PElement> menuButtons =
	{
		bottomMenu->stateBtn,
		bottomMenu->equipBtn,
		bottomMenu->xiulianBtn,
		bottomMenu->goodsBtn,
		bottomMenu->magicBtn,
		bottomMenu->notesBtn,
		bottomMenu->optionBtn,
	};
	std::vector<PElement> quickSlots;
	for (const auto& item : bottomMenu->goodsItem)
	{
		quickSlots.push_back(item);
	}
	for (const auto& item : bottomMenu->magicItem)
	{
		quickSlots.push_back(item);
	}
	const std::vector<PElement> candidates =
		bottomMenu->controllerFocusCandidates();
	ok = check(menuButtons.size() == 7
			&& quickSlots.size() == 8
			&& candidates.size() == menuButtons.size() + quickSlots.size()
			&& std::all_of(
				candidates.begin(),
				candidates.end(),
				[](const PElement& candidate)
				{
					return isUIFocusElementAvailable(candidate);
				}),
		"JXQY2 bottom spatial fixture did not expose seven buttons and eight"
		" available quick slots") && ok;
	if (!ok)
	{
		if (previousWindowWidth > 0 && previousWindowHeight > 0)
		{
			testEngine->setWindowSize(
				previousWindowWidth, previousWindowHeight);
		}
		return false;
	}

	const std::array<std::pair<UIAction, UIFocusDirection>, 4> directions =
	{{
		{ UIAction::NavigateUp, UIFocusDirection::Up },
		{ UIAction::NavigateDown, UIFocusDirection::Down },
		{ UIAction::NavigateLeft, UIFocusDirection::Left },
		{ UIAction::NavigateRight, UIFocusDirection::Right },
	}};
	std::vector<std::vector<std::size_t>> directedEdges(candidates.size());
	int buttonToQuickSlotEdges = 0;
	int quickSlotToButtonEdges = 0;
	for (std::size_t sourceIndex = 0;
		sourceIndex < candidates.size(); sourceIndex++)
	{
		const PElement& source = candidates[sourceIndex];
		for (const auto& direction : directions)
		{
			std::optional<UIFocusSpatialScore> bestScore;
			std::size_t expectedIndex = candidates.size();
			for (std::size_t candidateIndex = 0;
				candidateIndex < candidates.size(); candidateIndex++)
			{
				const std::optional<UIFocusSpatialScore> score =
					scoreUIFocusSpatialCandidate(
						source->rect,
						candidates[candidateIndex]->rect,
						direction.second,
						candidateIndex);
				if (score && (!bestScore || *score < *bestScore))
				{
					bestScore = score;
					expectedIndex = candidateIndex;
				}
			}

			ok = check(bottomMenu->focusControllerElement(source),
				"JXQY2 bottom spatial fixture could not reset a source focus")
				&& ok;
			const bool handled =
				bottomMenu->handleUIAction(direction.first);
			const PElement actual =
				bottomMenu->controllerFocusedElement();
			const std::string transition =
				"JXQY2 bottom spatial navigation source "
				+ std::to_string(sourceIndex) + " action "
				+ std::to_string(static_cast<int>(direction.first));
			if (expectedIndex < candidates.size())
			{
				ok = check(handled && actual == candidates[expectedIndex],
					transition
						+ " did not choose the nearest control in the"
						" requested screen direction") && ok;
				if (actual == candidates[expectedIndex]
					&& expectedIndex != sourceIndex)
				{
					directedEdges[sourceIndex].push_back(expectedIndex);
					const bool sourceIsButton =
						std::find(
							menuButtons.begin(), menuButtons.end(), source)
							!= menuButtons.end();
					const bool targetIsButton =
						std::find(
							menuButtons.begin(),
							menuButtons.end(),
							candidates[expectedIndex])
							!= menuButtons.end();
					if (sourceIsButton && !targetIsButton)
					{
						buttonToQuickSlotEdges++;
					}
					if (!sourceIsButton && targetIsButton)
					{
						quickSlotToButtonEdges++;
					}
				}
			}
			else
			{
				ok = check(!handled && actual == source,
					transition
						+ " moved despite having no control in that"
						" screen direction") && ok;
			}
		}
	}

	auto isReachable =
		[&directedEdges](std::size_t source, std::size_t target) -> bool
		{
			std::vector<bool> visited(directedEdges.size(), false);
			std::vector<std::size_t> pending = { source };
			visited[source] = true;
			for (std::size_t pendingIndex = 0;
				pendingIndex < pending.size(); pendingIndex++)
			{
				const std::size_t current = pending[pendingIndex];
				for (const std::size_t next : directedEdges[current])
				{
					if (!visited[next])
					{
						visited[next] = true;
						pending.push_back(next);
					}
				}
			}
			return visited[target];
		};
	for (const PElement& button : menuButtons)
	{
		const auto buttonPosition =
			std::find(candidates.begin(), candidates.end(), button);
		for (const PElement& quickSlot : quickSlots)
		{
			const auto slotPosition =
				std::find(candidates.begin(), candidates.end(), quickSlot);
			ok = check(
				buttonPosition != candidates.end()
					&& slotPosition != candidates.end()
					&& isReachable(
						static_cast<std::size_t>(
							std::distance(candidates.begin(), buttonPosition)),
						static_cast<std::size_t>(
							std::distance(candidates.begin(), slotPosition)))
					&& isReachable(
						static_cast<std::size_t>(
							std::distance(candidates.begin(), slotPosition)),
						static_cast<std::size_t>(
							std::distance(candidates.begin(), buttonPosition))),
				"JXQY2 bottom D-pad graph was not bidirectionally connected"
				" between every menu button and quick slot") && ok;
		}
	}
	ok = check(buttonToQuickSlotEdges > 0 && quickSlotToButtonEdges > 0,
		"JXQY2 bottom D-pad graph did not contain direct transitions in both"
		" directions between button and quick-slot regions") && ok;

	if (previousWindowWidth > 0 && previousWindowHeight > 0)
	{
		testEngine->setWindowSize(
			previousWindowWidth, previousWindowHeight);
	}
	return ok;
}

bool testRuntimeSlotMenuResizeRecovery()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	GameManager gameManager;
	gameManager.global.data.canInput = true;
	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	ok = check(gameManager.menu->goodsMenu != nullptr
		&& gameManager.menu->magicMenu != nullptr
		&& gameManager.menu->practiceMenu != nullptr
		&& gameManager.menu->equipMenu != nullptr
		&& gameManager.menu->bottomMenu != nullptr,
		"runtime creates every shared slot-menu owner") && ok;
	if (!ok)
	{
		return false;
	}

	gameManager.global.feature.largeMenuImages = false;
	auto setVisualGoods = [](GoodsInfo& goodsInfo)
	{
		setControllerTestGoods(goodsInfo, "resize_visual_goods.ini");
		goodsInfo.goods->image = "goods-cloth-1-书生服.mpc";
		goodsInfo.goods->icon = "goods-cloth-1-书生服s.mpc";
	};
	auto setVisualMagic = [](MagicInfo& magicInfo)
	{
		setControllerTestMagic(magicInfo, "resize_visual_magic.ini");
		magicInfo.magic->image = "白虹贯日.mpc";
		magicInfo.magic->icon = "白虹贯日s.mpc";
	};
	const int storeGoodsIndex = gameManager.goodsManager.storeBegin();
	const int quickGoodsIndex = gameManager.goodsManager.bottomBegin();
	const int equipmentGoodsIndex = gameManager.goodsManager.equipIndex(0);
	const int storeMagicIndex = gameManager.magicManager.storeBegin();
	const int quickMagicIndex = gameManager.magicManager.bottomBegin();
	const int practiceMagicIndex = gameManager.magicManager.practiceIndex();
	setVisualGoods(gameManager.goodsManager.goodsList[storeGoodsIndex]);
	setVisualGoods(gameManager.goodsManager.goodsList[quickGoodsIndex]);
	setVisualGoods(gameManager.goodsManager.goodsList[equipmentGoodsIndex]);
	setVisualMagic(gameManager.magicManager.magicList[storeMagicIndex]);
	setVisualMagic(gameManager.magicManager.magicList[quickMagicIndex]);
	setVisualMagic(gameManager.magicManager.magicList[practiceMagicIndex]);
	gameManager.menu->goodsMenu->updateGoods();
	gameManager.menu->bottomMenu->updateGoodsItem();
	gameManager.menu->equipMenu->updateGoods();
	gameManager.menu->magicMenu->updateMagic();
	gameManager.menu->bottomMenu->updateMagicItem();
	gameManager.menu->practiceMenu->updateMagic();
	ok = check(!gameManager.menu->goodsMenu->item.empty()
			&& gameManager.menu->goodsMenu->item[0]->impImage != nullptr
			&& gameManager.menu->bottomMenu->goodsItem[0]->impImage != nullptr
			&& gameManager.menu->equipMenu->item[0]->impImage != nullptr
			&& !gameManager.menu->magicMenu->item.empty()
			&& gameManager.menu->magicMenu->item[0]->impImage != nullptr
			&& gameManager.menu->bottomMenu->magicItem[0]->impImage != nullptr
			&& gameManager.menu->practiceMenu->magic->impImage != nullptr,
		"resize icon fixture could not bind its dynamic goods and magic images")
		&& ok;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(!gameManager.menu->goodsMenu->item.empty()
			&& gameManager.menu->goodsMenu->item[0]->impImage != nullptr
			&& gameManager.menu->bottomMenu->goodsItem[0]->impImage != nullptr
			&& gameManager.menu->equipMenu->item[0]->impImage != nullptr
			&& !gameManager.menu->magicMenu->item.empty()
			&& gameManager.menu->magicMenu->item[0]->impImage != nullptr
			&& gameManager.menu->bottomMenu->magicItem[0]->impImage != nullptr
			&& gameManager.menu->practiceMenu->magic->impImage != nullptr,
		"menu resize discarded dynamic goods or magic images") && ok;
	gameManager.goodsManager.goodsList[storeGoodsIndex].clear();
	gameManager.goodsManager.goodsList[quickGoodsIndex].clear();
	gameManager.goodsManager.goodsList[equipmentGoodsIndex].clear();
	gameManager.magicManager.magicList[storeMagicIndex] = MagicInfo();
	gameManager.magicManager.magicList[quickMagicIndex] = MagicInfo();
	gameManager.magicManager.magicList[practiceMagicIndex] = MagicInfo();
	gameManager.menu->goodsMenu->updateGoods();
	gameManager.menu->bottomMenu->updateGoodsItem();
	gameManager.menu->equipMenu->updateGoods();
	gameManager.menu->magicMenu->updateMagic();
	gameManager.menu->bottomMenu->updateMagicItem();
	gameManager.menu->practiceMenu->updateMagic();
	SlotInteractionBinding factoryBinding =
		MenuController::makeControllerSlotInteractionBinding(
			&gameManager,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::Practice);
	ok = check(factoryBinding.transfers
			== &gameManager.menu->controllerTransfers()
		&& factoryBinding.kind == ControllerSlotKind::Magic
		&& factoryBinding.domain == ControllerSlotDomain::Practice
		&& static_cast<bool>(factoryBinding.showMessage)
		&& !factoryBinding.resolveContext,
		"slot interaction factory injects only shared controller services") && ok;
	SlotInteractionBinding detachedFactoryBinding =
		MenuController::makeControllerSlotInteractionBinding(
			nullptr,
			ControllerSlotKind::PartnerGoods,
			ControllerSlotDomain::PartnerEquipment);
	ok = check(detachedFactoryBinding.transfers == nullptr
		&& detachedFactoryBinding.kind == ControllerSlotKind::PartnerGoods
		&& detachedFactoryBinding.domain
			== ControllerSlotDomain::PartnerEquipment
		&& static_cast<bool>(detachedFactoryBinding.showMessage),
		"slot interaction factory preserves detached binding semantics") && ok;

	UIFocusTestAccess::focusGoods(*gameManager.menu);
	ok = check(gameManager.menu->goodsMenu->item.size() >= 2
		&& gameManager.menu->goodsMenu->controllerFocusedElement()
			== gameManager.menu->goodsMenu->item[0],
		"goods owner starts with controller focus") && ok;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(gameManager.menu->handleUIAction(UIAction::NavigateRight)
		&& gameManager.menu->goodsMenu->item.size() >= 2
		&& gameManager.menu->goodsMenu->controllerFocusedElement()
			== gameManager.menu->goodsMenu->item[1],
		"goods owner recovers and replays navigation after resize") && ok;
	gameManager.menu->goodsMenu->deactivateControllerFocus();
	ok = check(gameManager.menu->goodsMenu->item.size() >= 3
		&& gameManager.menu->handleUIAction(UIAction::NavigateRight)
		&& gameManager.menu->goodsMenu->controllerFocusedElement()
			== gameManager.menu->goodsMenu->item[2],
		"visible goods owner recovers the first action after pointer takeover") && ok;
	ok = check(!gameManager.menu->goodsMenu->activateControllerFocus(
		ControllerFocusTarget::MagicList)
		&& gameManager.menu->goodsMenu->isControllerFocusActive(),
		"goods focus participant rejects unrelated targets without losing focus") && ok;
	const bool integratedMagicOwner =
		gameManager.global.feature.magicButtonOpensIntegratedEquip
		&& gameManager.menu->isStateEquipIntegrated();
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& !gameManager.menu->goodsMenu->visible
		&& (integratedMagicOwner
			? gameManager.menu->equipMenu->visible
				&& gameManager.menu->equipMenu->isControllerFocusActive()
			: gameManager.menu->magicMenu->visible
				&& gameManager.menu->magicMenu->isControllerFocusActive()),
		"right-menu descriptor advances from goods to the configured magic owner") && ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& gameManager.menu->goodsMenu->visible
		&& gameManager.menu->goodsMenu->isControllerFocusActive(),
		"right-menu descriptor returns from magic to goods") && ok;

	UIFocusTestAccess::focusMagic(*gameManager.menu);
	ok = check(gameManager.menu->magicMenu->item.size() >= 2
		&& gameManager.menu->magicMenu->controllerFocusedElement()
			== gameManager.menu->magicMenu->item[0],
		"magic owner starts with controller focus") && ok;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(gameManager.menu->handleUIAction(UIAction::NavigateRight)
		&& gameManager.menu->magicMenu->item.size() >= 2
		&& gameManager.menu->magicMenu->controllerFocusedElement()
			== gameManager.menu->magicMenu->item[1],
		"magic owner recovers and replays navigation after resize") && ok;
	ok = check(!gameManager.menu->magicMenu->activateControllerFocus(
		ControllerFocusTarget::GoodsBag)
		&& gameManager.menu->magicMenu->isControllerFocusActive(),
		"magic focus participant rejects unrelated targets without losing focus") && ok;

	UIFocusTestAccess::focusPractice(*gameManager.menu);
	ok = check(gameManager.menu->practiceMenu->magic != nullptr
		&& gameManager.menu->practiceMenu->controllerFocusedElement()
			== gameManager.menu->practiceMenu->magic,
		"practice owner starts with controller focus") && ok;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(gameManager.menu->handleUIAction(UIAction::Details)
		&& gameManager.menu->practiceMenu->magic != nullptr
		&& gameManager.menu->practiceMenu->controllerFocusedElement()
			== gameManager.menu->practiceMenu->magic,
		"practice owner recovers and replays details after resize") && ok;
	ok = check(!gameManager.menu->practiceMenu->activateControllerFocus(
		ControllerFocusTarget::GoodsBag)
		&& gameManager.menu->practiceMenu->isControllerFocusActive(),
		"practice focus participant rejects unrelated targets") && ok;

	UIFocusTestAccess::focusEquipment(*gameManager.menu);
	ok = check(gameManager.menu->equipMenu->item[0] != nullptr
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->item[0],
		"equipment owner starts with controller focus") && ok;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(gameManager.menu->handleUIAction(UIAction::NavigateRight)
		&& gameManager.menu->equipMenu->item[1] != nullptr
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->item[1],
		"equipment owner recovers its semantic graph after resize") && ok;
	ok = check(!gameManager.menu->equipMenu->activateControllerFocus(
		ControllerFocusTarget::GoodsBag)
		&& gameManager.menu->equipMenu->isControllerFocusActive(),
		"equipment focus participant rejects unrelated targets") && ok;
	gameManager.global.feature.magicButtonOpensIntegratedEquip = true;
	gameManager.global.feature.stateEquipIntegratedLayout = true;
	File::setActiveResourceRoot(
		(assetsRoot / "xjxqy").generic_string());
	ok = check(UIFocusTestAccess::reloadIntegratedEquipmentMenu(
		*gameManager.menu),
		"resize recovery loads a focusable integrated equipment layout") && ok;
	gameManager.menu->magicMenu->visible = true;
	ok = check(gameManager.menu->magicMenu->focusControllerDefault(),
		"standalone magic focus is active before the layout switch") && ok;
	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->canEquip = 1;
	gameManager.npcManager->npcList.push_back(partner);
	const std::shared_ptr<Scrollbar> partnerBagScrollbar =
		gameManager.menu->goodsMenu->scrollbar;
	const bool partnerBagHasGrid = partnerBagScrollbar != nullptr
		&& partnerBagScrollbar->lineSize > 0
		&& !gameManager.menu->goodsMenu->item.empty();
	const int partnerBagVisibleRows = partnerBagHasGrid
		? std::max(1, (static_cast<int>(
			gameManager.menu->goodsMenu->item.size())
			+ partnerBagScrollbar->lineSize - 1)
			/ partnerBagScrollbar->lineSize)
		: 0;
	const int partnerBagTargetPosition = partnerBagHasGrid
		? std::min(
			partnerBagScrollbar->max,
			partnerBagScrollbar->position + partnerBagVisibleRows)
		: -1;
	const int partnerBagLogicalIndex = partnerBagHasGrid
		? gameManager.goodsManager.storeBegin()
			+ partnerBagTargetPosition * partnerBagScrollbar->lineSize
		: -1;
	const bool partnerBagPageAvailable = partnerBagHasGrid
		&& partnerBagTargetPosition > partnerBagScrollbar->position
		&& gameManager.goodsManager.isStoreIndex(partnerBagLogicalIndex)
		&& partnerBagLogicalIndex < gameManager.goodsManager.listLength();
	ok = check(partnerBagPageAvailable,
		"runtime player bag exposes a second logical page for borrowed views") && ok;
	if (partnerBagPageAvailable)
	{
		setControllerTestGoods(
			gameManager.goodsManager.goodsList[partnerBagLogicalIndex],
			"controller_partner_borrowed_view.ini");
		gameManager.menu->goodsMenu->updateGoods();
	}
	ok = check(UIFocusTestAccess::focusMagicOnEquipment(*gameManager.menu)
		&& !gameManager.menu->magicMenu->isControllerFocusActive(),
		"integrated magic role resolves to equipment and clears the old owner")
		&& ok;
	ok = check(gameManager.menu->openPartnerEquipment(partner, false),
		"integrated magic owner opens partner equipment with return context") && ok;
	ok = check(gameManager.menu->partnerEquipMenu->focusControllerPlayerBag()
		&& !gameManager.menu->goodsMenu->item.empty()
		&& gameManager.menu->partnerEquipMenu->controllerFocusedElement()
			== gameManager.menu->goodsMenu->item[0],
		"partner equipment focuses the shared player bag") && ok;
	ok = check(partnerBagPageAvailable
		&& gameManager.menu->partnerEquipMenu->handleUIAction(
			UIAction::PageNext)
		&& gameManager.menu->goodsMenu->scrollbar != nullptr
		&& !gameManager.menu->goodsMenu->item.empty()
		&& gameManager.menu->goodsMenu->scrollbar->position
			== partnerBagTargetPosition
		&& gameManager.menu->goodsMenu->item[0]->dragIndex
			== partnerBagLogicalIndex,
		"partner borrowed view pages through the producer's logical mapping") && ok;
	const auto previousPlayerBagItem = gameManager.menu->goodsMenu->item[0];
	const auto previousPlayerBagScrollbar =
		gameManager.menu->goodsMenu->scrollbar;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(!gameManager.menu->goodsMenu->item.empty()
		&& gameManager.menu->goodsMenu->item[0] != previousPlayerBagItem
		&& gameManager.menu->goodsMenu->scrollbar != nullptr
		&& gameManager.menu->goodsMenu->scrollbar
			!= previousPlayerBagScrollbar
		&& gameManager.menu->goodsMenu->scrollbar->position
			== partnerBagTargetPosition
		&& gameManager.menu->goodsMenu->item[0]->dragIndex
			== partnerBagLogicalIndex
		&& gameManager.menu->partnerEquipMenu->isControllerFocusActive()
		&& gameManager.menu->partnerEquipMenu->controllerFocusedElement()
			== gameManager.menu->goodsMenu->item[0],
		"partner equipment rebinds the recreated player bag after resize") && ok;
	ok = check(gameManager.menu->partnerEquipMenu->handleUIAction(
		UIAction::Secondary),
		"partner borrowed view starts transfer through its production action") && ok;
	const std::optional<ControllerSlotAddress> borrowedPartnerSource =
		gameManager.menu->controllerTransfers().source();
	ok = check(borrowedPartnerSource.has_value()
		&& borrowedPartnerSource->kind == ControllerSlotKind::PartnerGoods
		&& borrowedPartnerSource->domain == ControllerSlotDomain::PartnerBag
		&& borrowedPartnerSource->logicalIndex == partnerBagLogicalIndex
		&& borrowedPartnerSource->context
			== std::static_pointer_cast<const void>(partner),
		"partner borrowed view preserves transfer domain, index, and context") && ok;
	gameManager.menu->controllerTransfers().cancel();
	gameManager.menu->hideBottomWnd();
	gameManager.menu->showBottomWnd();
	ok = check(gameManager.menu->equipMenu->visible
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== gameManager.menu->equipMenu
		&& UIFocusTestAccess::focusedRoleIsMagic(*gameManager.menu),
		"non-restoring partner close preserves the shared owner's magic role") && ok;
	gameManager.menu->equipMenu->visible = false;
	gameManager.menu->equipMenu->deactivateControllerFocus();
	File::setActiveResourceRoot(activeRoot.generic_string());

	ok = check(gameManager.menu->controllerTransfers().start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsQuick)
		&& gameManager.menu->bottomMenu->goodsItem[0] != nullptr
		&& gameManager.menu->bottomMenu->controllerFocusedElement()
			== gameManager.menu->bottomMenu->goodsItem[0],
		"bottom quick-slot owner starts through the transfer domain") && ok;
	UIFocusTestAccess::resize(*gameManager.menu, 1024, 768);
	ok = check(gameManager.menu->handleUIAction(UIAction::NavigateRight)
		&& gameManager.menu->bottomMenu->goodsItem[1] != nullptr
		&& gameManager.menu->bottomMenu->controllerFocusedElement()
			== gameManager.menu->bottomMenu->goodsItem[1],
		"bottom owner is reactivated by its current domain after resize") && ok;
	UIFocusTestAccess::focusGoods(*gameManager.menu);
	ok = check(!gameManager.menu->bottomMenu->isControllerFocusActive()
		&& gameManager.menu->goodsMenu->isControllerFocusActive(),
		"descriptor focus deactivates a transfer-domain-only owner") && ok;
	gameManager.menu->cancelControllerInteraction();
	return ok;
}

bool testControllerToolTipRouting()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "xjxqy";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	Engine* testEngine = Engine::getInstance();
	int previousWindowWidth = 0;
	int previousWindowHeight = 0;
	testEngine->getWindowSize(
		previousWindowWidth, previousWindowHeight);
	auto restoreWindowSize = [testEngine,
		previousWindowWidth, previousWindowHeight]()
	{
		if (previousWindowWidth > 0 && previousWindowHeight > 0)
		{
			testEngine->setWindowSize(
				previousWindowWidth, previousWindowHeight);
		}
	};
	testEngine->setWindowSize(1024, 768);

	GameManager gameManager;
	gameManager.global.data.canInput = true;
	gameManager.global.feature.menuResourceProfile = mrpXjxqy;
	gameManager.menu->init();
	ok = check(gameManager.menu->toolTip != nullptr
		&& gameManager.menu->upMenu != nullptr
		&& gameManager.menu->goodsMenu != nullptr
		&& gameManager.menu->partnerEquipMenu != nullptr,
		"XJXQY runtime creates the shared controller tooltip owners") && ok;
	if (!ok)
	{
		restoreWindowSize();
		return false;
	}
	auto findFocusedItem = [](const ControllerFocusParticipant& participant)
	{
		return std::dynamic_pointer_cast<Item>(
			participant.controllerFocusedElement());
	};
	auto controllerToolTipMatches = [&gameManager, testEngine](
		const std::string& expectedName,
		const PElement& anchor)
	{
		if (anchor == nullptr || gameManager.menu->toolTip == nullptr
			|| gameManager.menu->toolTip->name == nullptr)
		{
			return false;
		}
		int windowWidth = 0;
		int windowHeight = 0;
		testEngine->getWindowSize(windowWidth, windowHeight);
		int expectedX = anchor->rect.x + anchor->rect.w + 8;
		if (expectedX + gameManager.menu->toolTip->rect.w > windowWidth)
		{
			expectedX = anchor->rect.x
				- gameManager.menu->toolTip->rect.w - 8;
		}
		expectedX = std::max(0, std::min(
			expectedX,
			windowWidth - gameManager.menu->toolTip->rect.w));
		const int expectedY = std::max(0, std::min(
			anchor->rect.y,
			windowHeight - gameManager.menu->toolTip->rect.h));
		return gameManager.menu->toolTip->visible
			&& gameManager.menu->toolTip->parent
				== gameManager.menu->upMenu.get()
			&& gameManager.menu->toolTip->name->getStr() == expectedName
			&& gameManager.menu->toolTip->rect.x == expectedX
			&& gameManager.menu->toolTip->rect.y == expectedY;
	};

	const int goodsIndex = gameManager.goodsManager.storeBegin();
	GoodsInfo& goodsInfo = gameManager.goodsManager.goodsList[goodsIndex];
	setControllerTestGoods(goodsInfo, "controller_tooltip_goods.ini");
	goodsInfo.goods->name = "controller tooltip goods";
	goodsInfo.goods->intro = "controller tooltip intro";
	gameManager.menu->goodsMenu->updateGoods();

	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->canEquip = 1;
	ok = check(
		gameManager.menu->openPartnerEquipment(partner, false)
		&& gameManager.menu->partnerEquipMenu->focusControllerPlayerBag(),
		"partner equipment exposes the shared player-bag focus target") && ok;

	std::shared_ptr<Item> controllerAnchor = findFocusedItem(
		*gameManager.menu->partnerEquipMenu);
	ok = check(controllerAnchor != nullptr,
		"partner equipment selects a concrete player-bag tooltip anchor") && ok;
	if (controllerAnchor != nullptr)
	{
		controllerAnchor->rect = { 360, 180, 32, 32 };
		auto previousParent = std::make_shared<Panel>();
		previousParent->addChild(gameManager.menu->toolTip);
		ok = check(gameManager.menu->partnerEquipMenu->handleUIAction(
			UIAction::Details),
			"partner equipment routes Details through its production pane") && ok;

		ok = check(controllerToolTipMatches(
			"controller tooltip goods", controllerAnchor),
			"partner controller details attach, populate, and anchor after XJXQY reflow")
			&& ok;
		gameManager.menu->goodsMenu->visible = false;
		UIFocusTestAccess::update(*gameManager.menu->toolTip);
		ok = check(gameManager.menu->toolTip->visible,
			"tooltip ownership follows the partner menu rather than its shared bag anchor")
			&& ok;
		gameManager.menu->goodsMenu->visible = true;
		gameManager.menu->partnerEquipMenu->visible = false;
		UIFocusTestAccess::update(*gameManager.menu->toolTip);
		ok = check(!gameManager.menu->toolTip->visible,
			"hiding the controller tooltip owner clears its details") && ok;

		gameManager.menu->closePartnerEquipment(false);
		gameManager.menu->goodsMenu->visible = true;
		ok = check(gameManager.menu->goodsMenu->focusControllerDefault(),
			"goods controller tooltip route activates its production grid") && ok;
		controllerAnchor = findFocusedItem(*gameManager.menu->goodsMenu);
		if (controllerAnchor != nullptr)
		{
			controllerAnchor->rect = { 260, 120, 30, 30 };
		}
		ok = check(controllerAnchor != nullptr
			&& gameManager.menu->goodsMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip goods", controllerAnchor),
			"goods menu routes controller details through the shared tooltip facade")
			&& ok;

		const int magicIndex = gameManager.magicManager.storeBegin();
		MagicInfo& magicInfo = gameManager.magicManager.magicList[magicIndex];
		setControllerTestMagic(magicInfo, "controller_tooltip_magic.ini");
		magicInfo.magic->name = "controller tooltip list magic";
		gameManager.menu->magicMenu->visible = true;
		gameManager.menu->magicMenu->updateMagic();
		ok = check(gameManager.menu->magicMenu->focusControllerDefault(),
			"magic controller tooltip route activates its production grid") && ok;
		std::shared_ptr<Item> magicAnchor = findFocusedItem(
			*gameManager.menu->magicMenu);
		if (magicAnchor != nullptr)
		{
			magicAnchor->rect = { 300, 140, 30, 30 };
		}
		ok = check(magicAnchor != nullptr
			&& gameManager.menu->magicMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip list magic", magicAnchor),
			"magic menu routes controller details through the shared tooltip facade")
			&& ok;

		const int practiceIndex = gameManager.magicManager.practiceIndex();
		MagicInfo& practiceInfo =
			gameManager.magicManager.magicList[practiceIndex];
		setControllerTestMagic(
			practiceInfo, "controller_tooltip_practice.ini");
		practiceInfo.magic->name = "controller tooltip practice magic";
		gameManager.menu->practiceMenu->visible = true;
		gameManager.menu->practiceMenu->updateMagic();
		ok = check(gameManager.menu->practiceMenu->focusControllerDefault(),
			"practice controller tooltip route activates its production slot") && ok;
		const PElement practiceAnchor = gameManager.menu->practiceMenu->magic;
		if (practiceAnchor != nullptr)
		{
			practiceAnchor->rect = { 340, 160, 30, 30 };
		}
		ok = check(practiceAnchor != nullptr
			&& gameManager.menu->practiceMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip practice magic", practiceAnchor),
			"practice menu routes controller details through the shared tooltip facade")
			&& ok;

		const int equipmentIndex = gameManager.goodsManager.equipIndex(0);
		GoodsInfo& equipmentInfo =
			gameManager.goodsManager.goodsList[equipmentIndex];
		setControllerTestGoods(
			equipmentInfo, "controller_tooltip_equipment.ini");
		equipmentInfo.goods->name = "controller tooltip equipment";
		gameManager.menu->equipMenu->visible = true;
		gameManager.menu->equipMenu->updateGoods();
		ok = check(gameManager.menu->equipMenu->focusControllerEquipment(),
			"equipment controller tooltip route activates its production pane") && ok;
		const PElement equipmentAnchor = gameManager.menu->equipMenu->item[0];
		if (equipmentAnchor != nullptr)
		{
			equipmentAnchor->rect = { 380, 200, 30, 30 };
		}
		ok = check(equipmentAnchor != nullptr
			&& gameManager.menu->equipMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip equipment", equipmentAnchor),
			"equipment menu routes controller details through the shared tooltip facade")
			&& ok;

		const int quickGoodsIndex = gameManager.goodsManager.bottomBegin();
		GoodsInfo& quickGoodsInfo =
			gameManager.goodsManager.goodsList[quickGoodsIndex];
		setControllerTestGoods(
			quickGoodsInfo, "controller_tooltip_quick.ini");
		quickGoodsInfo.goods->name = "controller tooltip quick goods";
		gameManager.menu->bottomMenu->visible = true;
		gameManager.menu->bottomMenu->updateGoodsItem();
		ok = check(gameManager.menu->bottomMenu->focusControllerGoodsQuick(),
			"bottom controller tooltip route activates its goods pane") && ok;
		const PElement quickGoodsAnchor =
			gameManager.menu->bottomMenu->goodsItem[0];
		if (quickGoodsAnchor != nullptr)
		{
			quickGoodsAnchor->rect = { 420, 220, 30, 30 };
		}
		ok = check(quickGoodsAnchor != nullptr
			&& gameManager.menu->bottomMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip quick goods", quickGoodsAnchor),
			"bottom menu routes controller details through the shared tooltip facade")
			&& ok;

		gameManager.menu->buySellMenu->clearGoodsList();
		setControllerTestGoods(
			gameManager.menu->buySellMenu->goodsList[0],
			"controller_tooltip_shop.ini");
		gameManager.menu->buySellMenu->goodsList[0].goods->name =
			"controller tooltip shop goods";
		gameManager.menu->buySellMenu->bsKind = bsBuy;
		gameManager.menu->buySellMenu->visible = true;
		gameManager.menu->buySellMenu->updateGoods();
		const PElement shopAnchor = !gameManager.menu->buySellMenu->item.empty()
			? gameManager.menu->buySellMenu->item[0]
			: nullptr;
		if (shopAnchor != nullptr)
		{
			shopAnchor->rect = { 460, 240, 30, 30 };
		}
		ok = check(shopAnchor != nullptr
			&& gameManager.menu->buySellMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip shop goods", shopAnchor),
			"buy-sell menu routes controller details through the shared tooltip facade")
			&& ok;

		const std::shared_ptr<Scrollbar> buySellPlayerScrollbar =
			gameManager.menu->goodsMenu->scrollbar;
		const bool buySellPlayerHasGrid = buySellPlayerScrollbar != nullptr
			&& buySellPlayerScrollbar->lineSize > 0
			&& !gameManager.menu->goodsMenu->item.empty();
		const int buySellPlayerVisibleRows = buySellPlayerHasGrid
			? std::max(1, (static_cast<int>(
				gameManager.menu->goodsMenu->item.size())
				+ buySellPlayerScrollbar->lineSize - 1)
				/ buySellPlayerScrollbar->lineSize)
			: 0;
		const int buySellPlayerTargetPosition =
			buySellPlayerHasGrid
			? std::min(
				buySellPlayerScrollbar->max,
				buySellPlayerScrollbar->position + buySellPlayerVisibleRows)
			: -1;
		const int buySellPlayerLogicalIndex =
			buySellPlayerHasGrid
			? gameManager.goodsManager.storeBegin()
				+ buySellPlayerTargetPosition
					* buySellPlayerScrollbar->lineSize
			: -1;
		const bool buySellPlayerPageAvailable =
			buySellPlayerHasGrid
			&& buySellPlayerTargetPosition > buySellPlayerScrollbar->position
			&& gameManager.goodsManager.isStoreIndex(
				buySellPlayerLogicalIndex)
			&& buySellPlayerLogicalIndex
				< gameManager.goodsManager.listLength();
		ok = check(buySellPlayerPageAvailable,
			"buy-sell borrowed player bag exposes a second logical page") && ok;
		if (buySellPlayerPageAvailable)
		{
			GoodsInfo& pagedGoods = gameManager.goodsManager.goodsList[
				buySellPlayerLogicalIndex];
			setControllerTestGoods(
				pagedGoods, "controller_tooltip_player_page.ini");
			pagedGoods.goods->name = "controller tooltip player page goods";
			gameManager.menu->goodsMenu->updateGoods();
		}
		ok = check(buySellPlayerPageAvailable
			&& gameManager.menu->buySellMenu->handleUIAction(
				UIAction::PanelNext)
			&& gameManager.menu->buySellMenu->handleUIAction(
				UIAction::PageNext)
			&& gameManager.menu->goodsMenu->scrollbar != nullptr
			&& !gameManager.menu->goodsMenu->item.empty()
			&& gameManager.menu->goodsMenu->scrollbar->position
				== buySellPlayerTargetPosition
			&& gameManager.menu->goodsMenu->item[0]->dragIndex
				== buySellPlayerLogicalIndex,
			"buy-sell borrowed view preserves producer scrolling and refresh") && ok;
		const PElement playerPageAnchor =
			!gameManager.menu->goodsMenu->item.empty()
			? gameManager.menu->goodsMenu->item[0]
			: nullptr;
		if (playerPageAnchor != nullptr)
		{
			playerPageAnchor->rect = { 500, 260, 30, 30 };
		}
		ok = check(playerPageAnchor != nullptr
			&& gameManager.menu->buySellMenu->handleUIAction(UIAction::Details)
			&& controllerToolTipMatches(
				"controller tooltip player page goods", playerPageAnchor),
			"buy-sell borrowed view resolves details from the refreshed logical page")
			&& ok;

		auto magic = std::make_shared<Magic>();
		magic->name = "controller tooltip magic";
		magic->intro = "controller magic intro";
		ok = check(gameManager.menu->showMagicToolTip(
			gameManager.menu->magicMenu,
			magic,
			7,
			controllerAnchor)
			&& gameManager.menu->toolTip->visible
			&& gameManager.menu->toolTip->name != nullptr
			&& gameManager.menu->toolTip->name->getStr()
				== "controller tooltip magic"
			&& gameManager.menu->toolTip->cost != nullptr
			&& gameManager.menu->toolTip->cost->getStr() == "等级： 7",
			"shared controller tooltip facade presents magic content") && ok;
		ok = check(!gameManager.menu->showGoodsToolTip(
			gameManager.menu->magicMenu,
			goodsInfo.goods,
			nullptr)
			&& !gameManager.menu->toolTip->visible,
			"invalid controller tooltip requests hide stale details") && ok;
	}

	gameManager.menu->hideToolTip();
	restoreWindowSize();
	return ok;
}

bool testControllerMenuTransitions()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	GameManager gameManager;
	gameManager.global.data.canInput = true;
	gameManager.global.feature.practiceMenuDisabled = false;
	gameManager.global.feature.magicButtonOpensIntegratedEquip = false;
	gameManager.global.feature.stateEquipIntegratedLayout = false;
	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	using MenuRole = UIFocusTestAccess::MenuRole;
	auto buttonChecked = [](const std::shared_ptr<CheckBox>& button)
	{
		return button != nullptr && button->checked;
	};

	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Goods)
		&& gameManager.menu->goodsMenu->visible
		&& !gameManager.menu->magicMenu->visible
		&& !gameManager.menu->memoMenu->visible
		&& gameManager.menu->goodsMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== gameManager.menu->goodsMenu
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Goods)
		&& buttonChecked(gameManager.menu->bottomMenu->goodsBtn),
		"managed goods transition opens, focuses, and selects one right menu")
		&& ok;
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Goods)
		&& gameManager.menu->goodsMenu->visible
		&& gameManager.menu->goodsMenu->isControllerFocusActive(),
		"ensure-open is idempotent for an already visible goods menu") && ok;
	ok = check(UIFocusTestAccess::closeFocusedMenu(*gameManager.menu)
		&& !gameManager.menu->goodsMenu->visible
		&& !buttonChecked(gameManager.menu->bottomMenu->goodsBtn),
		"managed close deterministically hides goods and clears selection") && ok;
	ok = check(UIFocusTestAccess::toggleMenu(
		*gameManager.menu, MenuRole::Goods)
		&& gameManager.menu->goodsMenu->visible
		&& UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Goods)
		&& !gameManager.menu->goodsMenu->visible,
		"goods shortcut remains a two-state toggle over managed transitions") && ok;

	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Magic)
		&& gameManager.menu->magicMenu->visible
		&& gameManager.menu->magicMenu->isControllerFocusActive()
		&& UIFocusTestAccess::ensureMenuOpen(
			*gameManager.menu, MenuRole::Magic)
		&& gameManager.menu->magicMenu->visible
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Magic),
		"standalone magic uses the same idempotent transition pipeline") && ok;
	ok = check(UIFocusTestAccess::closeFocusedMenu(*gameManager.menu)
		&& !gameManager.menu->magicMenu->visible,
		"standalone magic closes through the shared transition pipeline") && ok;

	if (gameManager.menu->memoMenu->scrollbar != nullptr)
	{
		gameManager.menu->memoMenu->reRange(
			gameManager.menu->memoMenu->scrollbar->min + 4);
	}
	ok = check(UIFocusTestAccess::ensureMenuOpen(
			*gameManager.menu, MenuRole::Memo)
		&& gameManager.menu->memoMenu->isControllerFocusActive(),
		"scrollable memo did not establish the active-focus precondition") && ok;
	if (gameManager.menu->memoMenu->scrollbar != nullptr)
	{
		gameManager.menu->memoMenu->reRange(
			gameManager.menu->memoMenu->scrollbar->min);
	}
	ok = check(!gameManager.menu->memoMenu->isControllerFocusActive()
		&& (gameManager.menu->memoMenu->scrollbar == nullptr
			|| !gameManager.menu->memoMenu->scrollbar->isFocused())
		&& UIFocusTestAccess::recoverFocusedOwner(*gameManager.menu)
			!= gameManager.menu->memoMenu,
		"focused memo retained an invalid box after its range became empty") && ok;
	ok = check(UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Memo)
		&& !gameManager.menu->memoMenu->visible,
		"empty memo cleanup could not close the no-focus surface") && ok;

	if (gameManager.menu->memoMenu->scrollbar != nullptr)
	{
		gameManager.menu->memoMenu->scrollbar->max =
			gameManager.menu->memoMenu->scrollbar->min;
	}
	ok = check(UIFocusTestAccess::restoreDefaultFocus(
			*gameManager.menu, gameManager.menu->bottomMenu),
		"bottom HUD could not establish the focus-preservation precondition")
		&& ok;
	const PElement focusBeforeEmptySurface =
		UIFocusTestAccess::focusedOwner(*gameManager.menu);
	ok = check(focusBeforeEmptySurface == gameManager.menu->bottomMenu
		&& UIFocusTestAccess::ensureMenuOpen(
			*gameManager.menu, MenuRole::Memo)
		&& gameManager.menu->memoMenu->visible
		&& UIFocusTestAccess::ensureMenuOpen(
			*gameManager.menu, MenuRole::Memo)
		&& gameManager.menu->memoMenu->visible
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== focusBeforeEmptySurface
		&& !gameManager.menu->memoMenu->isControllerFocusActive(),
		"an unscrollable memo replaced the existing valid focus owner") && ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::Cancel)
			&& !gameManager.menu->memoMenu->visible
			&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
				== focusBeforeEmptySurface,
		"Cancel cleared the HUD focus instead of closing an unscrollable memo")
		&& ok;

	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::State)
		&& gameManager.menu->stateMenu->visible
		&& UIFocusTestAccess::ensureMenuOpen(
			*gameManager.menu, MenuRole::State)
		&& gameManager.menu->stateMenu->visible
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== focusBeforeEmptySurface,
		"display-only state menu displaced the existing valid focus owner") && ok;
	ok = check(gameManager.menu->controllerTransfers().start(
			ControllerSlotKind::Goods,
			ControllerSlotDomain::GoodsQuick)
			&& gameManager.menu->handleUIAction(UIAction::Cancel)
			&& !gameManager.menu->controllerTransfers().active()
			&& gameManager.menu->stateMenu->visible,
		"Cancel closed the display-only State surface before canceling the"
		" active HUD transfer") && ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::Cancel)
			&& !gameManager.menu->stateMenu->visible
			&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
				== focusBeforeEmptySurface,
		"Cancel cleared the HUD focus instead of closing display-only State")
		&& ok;
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Equip)
		&& !gameManager.menu->stateMenu->visible
		&& gameManager.menu->equipMenu->visible
		&& gameManager.menu->equipMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Equip),
		"opening equipment hides the prior left-side surface") && ok;
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Practice)
		&& !gameManager.menu->equipMenu->visible
		&& gameManager.menu->practiceMenu->visible
		&& gameManager.menu->practiceMenu->isControllerFocusActive(),
		"opening practice reuses left-group conflict handling") && ok;
	ok = check(UIFocusTestAccess::closeFocusedMenu(*gameManager.menu)
		&& !gameManager.menu->practiceMenu->visible,
		"left-side managed close clears the active surface") && ok;

	if (gameManager.menu->memoMenu->scrollbar != nullptr)
	{
		gameManager.menu->memoMenu->reRange(
			gameManager.menu->memoMenu->scrollbar->min + 4);
	}
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Goods)
		&& gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& gameManager.menu->magicMenu->visible
		&& gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& gameManager.menu->memoMenu->visible
		&& gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& gameManager.menu->goodsMenu->visible
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& gameManager.menu->memoMenu->visible
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& gameManager.menu->magicMenu->visible
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& gameManager.menu->goodsMenu->visible,
		"right-menu descriptors cycle forward and backward without toggling targets off")
		&& ok;
	ok = check(UIFocusTestAccess::closeFocusedMenu(*gameManager.menu),
		"right-menu cycle cleanup closes its final managed surface") && ok;

	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->canEquip = 1;
	ok = check(gameManager.menu->openPartnerEquipment(partner, false)
		&& gameManager.menu->goodsMenu->visible
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== gameManager.menu->partnerEquipMenu
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::PartnerEquipment),
		"partner equipment opens as the exclusive controller owner") && ok;
	const std::vector<PElement> partnerCandidates =
		gameManager.menu->partnerEquipMenu->controllerFocusCandidates();
	std::vector<bool> partnerCandidateVisibility;
	partnerCandidateVisibility.reserve(partnerCandidates.size());
	for (const PElement& candidate : partnerCandidates)
	{
		partnerCandidateVisibility.push_back(
			candidate != nullptr && candidate->visible);
		if (candidate != nullptr)
		{
			candidate->visible = false;
		}
	}
	ok = check(!partnerCandidates.empty()
		&& UIFocusTestAccess::recoverFocusedOwner(*gameManager.menu) == nullptr
		&& !gameManager.menu->goodsMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu) == nullptr,
		"invalid modal candidates do not restore focus to the visible lower bag")
		&& ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::Cancel)
			&& !gameManager.menu->partnerEquipMenu->visible,
		"a PartnerEquip modal with no focus candidate could not close on Cancel")
		&& ok;
	for (std::size_t index = 0; index < partnerCandidates.size(); index++)
	{
		if (partnerCandidates[index] != nullptr)
		{
			partnerCandidates[index]->visible =
				partnerCandidateVisibility[index];
		}
	}
	ok = check(gameManager.menu->openPartnerEquipment(partner, false)
			&& UIFocusTestAccess::recoverFocusedOwner(*gameManager.menu)
			== gameManager.menu->partnerEquipMenu
		&& UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Goods)
		&& !gameManager.menu->partnerEquipMenu->visible
		&& gameManager.menu->goodsMenu->visible
		&& gameManager.menu->goodsMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== gameManager.menu->goodsMenu
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Goods),
		"goods toggle closes partner equipment before reading shared bag visibility")
		&& ok;
	ok = check(UIFocusTestAccess::toggleMenu(
		*gameManager.menu, MenuRole::Goods)
		&& !gameManager.menu->goodsMenu->visible
		&& UIFocusTestAccess::ensureMenuOpen(
			*gameManager.menu, MenuRole::Goods)
		&& gameManager.menu->openPartnerEquipment(partner, false)
		&& UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Goods)
		&& !gameManager.menu->partnerEquipMenu->visible
		&& !gameManager.menu->goodsMenu->visible,
		"goods toggle uses the restored pre-modal state when it was already open")
		&& ok;

	ok = check(gameManager.menu->openPartnerEquipment(partner, false)
		&& UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Magic)
		&& !gameManager.menu->partnerEquipMenu->visible
		&& gameManager.menu->magicMenu->visible
		&& UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Magic)
		&& !gameManager.menu->magicMenu->visible,
		"standalone magic toggle resolves visibility after closing partner equipment")
		&& ok;
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Memo)
		&& gameManager.menu->openPartnerEquipment(partner, false)
		&& UIFocusTestAccess::toggleMenu(
			*gameManager.menu, MenuRole::Memo)
		&& !gameManager.menu->partnerEquipMenu->visible
		&& !gameManager.menu->memoMenu->visible,
		"memo toggle preserves toggle-off semantics across the partner modal") && ok;

	gameManager.global.feature.magicButtonOpensIntegratedEquip = true;
	gameManager.global.feature.stateEquipIntegratedLayout = true;
	const std::filesystem::path integratedRoot = assetsRoot / "xjxqy";
	File::setActiveResourceRoot(integratedRoot.generic_string());
	ok = check(UIFocusTestAccess::reloadIntegratedEquipmentMenu(
		*gameManager.menu),
		"transition test loads a focusable integrated equipment layout") && ok;
	if (!ok)
	{
		return false;
	}
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Magic)
		&& gameManager.menu->equipMenu->visible
		&& !gameManager.menu->magicMenu->visible
		&& gameManager.menu->equipMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(*gameManager.menu)
			== gameManager.menu->equipMenu
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Magic)
		&& buttonChecked(gameManager.menu->bottomMenu->equipBtn)
		&& buttonChecked(gameManager.menu->bottomMenu->magicBtn),
		"integrated magic keeps the equipment owner and magic semantic role") && ok;
	ok = check(gameManager.menu->equipMenu->item[0] != nullptr
		&& !gameManager.menu->equipMenu->magicDisplayItem.empty()
		&& gameManager.menu->equipMenu->magicDisplayItem[0] != nullptr
		&& gameManager.menu->adoptControllerPointerFocus(
			gameManager.menu->equipMenu,
			gameManager.menu->equipMenu->magicDisplayItem[0])
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->magicDisplayItem[0]
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Magic)
		&& gameManager.menu->adoptControllerPointerFocus(
			gameManager.menu->equipMenu,
			gameManager.menu->equipMenu->item[0])
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->item[0]
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Equip),
		"shared-owner exact focus resolves integrated magic and equipment roles")
		&& ok;
	ok = check(gameManager.menu->equipMenu->focusControllerEquipment()
		&& UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Magic)
		&& gameManager.menu->equipMenu->visible
		&& !gameManager.menu->equipMenu->magicDisplayItem.empty()
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->magicDisplayItem[0]
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Magic),
		"integrated magic ensure-open retargets the shared owner to MagicList") && ok;
	ok = check(UIFocusTestAccess::toggleMenu(
		*gameManager.menu, MenuRole::Equip)
		&& !gameManager.menu->equipMenu->visible
		&& !buttonChecked(gameManager.menu->bottomMenu->equipBtn)
		&& !buttonChecked(gameManager.menu->bottomMenu->magicBtn),
		"equipment shortcut closes a shared magic owner and clears both selections")
		&& ok;
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Equip)
		&& gameManager.menu->equipMenu->visible,
		"integrated equipment opens through the same managed transition") && ok;
	gameManager.menu->goodsMenu->visible = true;
	ok = check(UIFocusTestAccess::toggleMenu(
		*gameManager.menu, MenuRole::Magic)
		&& !gameManager.menu->equipMenu->visible
		&& !gameManager.menu->goodsMenu->visible
		&& !buttonChecked(gameManager.menu->bottomMenu->equipBtn)
		&& !buttonChecked(gameManager.menu->bottomMenu->magicBtn),
		"magic shortcut closes an equipment-role shared owner using magic conflicts")
		&& ok;

	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, MenuRole::Goods)
		&& gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& gameManager.menu->equipMenu->visible
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Magic)
		&& gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& gameManager.menu->memoMenu->visible
		&& !gameManager.menu->equipMenu->visible,
		"right-menu cycling treats integrated magic as a role, not a separate group")
		&& ok;
	ok = check(UIFocusTestAccess::closeFocusedMenu(*gameManager.menu),
		"integrated right-menu cycle cleanup closes memo") && ok;

	ok = check(gameManager.menu->controllerTransfers().start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsQuick)
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& gameManager.menu->controllerTransfers().active()
		&& gameManager.menu->controllerTransfers().activeDomain()
			== ControllerSlotDomain::GoodsBag
		&& gameManager.menu->controllerTransfers().activeOwner()
			== gameManager.menu->goodsMenu
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Goods),
		"goods transfer domain cycling remains active through the menu refactor")
		&& ok;
	gameManager.menu->controllerTransfers().cancel();
	if (gameManager.menu->goodsMenu->visible)
	{
		UIFocusTestAccess::toggleMenu(*gameManager.menu, MenuRole::Goods);
	}
	ok = check(gameManager.menu->controllerTransfers().start(
		ControllerSlotKind::Magic, ControllerSlotDomain::MagicQuick)
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& gameManager.menu->controllerTransfers().active()
		&& gameManager.menu->controllerTransfers().activeDomain()
			== ControllerSlotDomain::MagicList
		&& gameManager.menu->controllerTransfers().activeOwner()
			== gameManager.menu->equipMenu
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, MenuRole::Magic),
		"integrated magic transfer cycling keeps the existing transfer lifecycle")
		&& ok;
	gameManager.menu->cancelControllerInteraction();
	return ok;
}

bool testControllerTransferDomainDescriptors()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	GameManager gameManager;
	gameManager.global.data.canInput = true;
	gameManager.global.feature.practiceMenuDisabled = false;
	gameManager.global.feature.magicButtonOpensIntegratedEquip = false;
	gameManager.global.feature.stateEquipIntegratedLayout = false;
	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	auto& transfers = gameManager.menu->controllerTransfers();
	std::string transferMessage;

	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, UIFocusTestAccess::MenuRole::Magic),
		"transaction test starts from a focused ordinary menu") && ok;
	for (const std::shared_ptr<Item>& item : gameManager.menu->goodsMenu->item)
	{
		if (item != nullptr)
		{
			item->visible = false;
		}
	}
	ok = check(!transfers.start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsBag)
		&& !transfers.active()
		&& gameManager.menu->magicMenu->visible
		&& gameManager.menu->magicMenu->isControllerFocusActive()
		&& !gameManager.menu->goodsMenu->visible
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Magic)
		&& gameManager.menu->bottomMenu->magicBtn != nullptr
		&& gameManager.menu->bottomMenu->magicBtn->checked
		&& gameManager.menu->bottomMenu->goodsBtn != nullptr
		&& !gameManager.menu->bottomMenu->goodsBtn->checked,
		"failed target focus leaves ordinary visibility and selection unchanged")
		&& ok;
	for (const std::shared_ptr<Item>& item : gameManager.menu->goodsMenu->item)
	{
		if (item != nullptr)
		{
			item->visible = true;
		}
	}
	UIFocusTestAccess::closeFocusedMenu(*gameManager.menu);

	const int quickGoodsIndex = gameManager.goodsManager.bottomBegin();
	GoodsInfo& quickGoods =
		gameManager.goodsManager.goodsList[quickGoodsIndex];
	setControllerTestGoods(quickGoods, "controller_quick_source.ini");
	const std::shared_ptr<Goods> quickGoodsIdentity = quickGoods.goods;
	const ControllerSlotAddress quickGoodsSource =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsQuick,
		quickGoodsIndex
	};
	ok = check(transfers.start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsQuick)
		&& transfers.begin(quickGoodsSource)
		&& transfers.hasSource()
		&& gameManager.menu->bottomMenu->goodsItem[0] != nullptr
		&& gameManager.menu->bottomMenu->goodsItem[0]->isTransferSelected(),
		"production goods quick domain starts with a real source") && ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.activeDomain() == ControllerSlotDomain::GoodsBag
		&& transfers.source().has_value()
		&& transfers.source().value() == quickGoodsSource
		&& transfers.activeOwner() == gameManager.menu->goodsMenu
		&& gameManager.menu->goodsMenu->visible
		&& gameManager.menu->goodsMenu->isControllerFocusActive()
		&& !gameManager.menu->bottomMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Goods)
		&& quickGoods.goods == quickGoodsIdentity
		&& gameManager.menu->bottomMenu->goodsItem[0]->isTransferSelected(),
		"goods transfer switch preserves the real quick-slot source") && ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.activeDomain()
			== ControllerSlotDomain::PlayerEquipment
		&& transfers.source().has_value()
		&& transfers.source().value() == quickGoodsSource
		&& transfers.activeOwner() == gameManager.menu->equipMenu
		&& gameManager.menu->equipMenu->visible
		&& gameManager.menu->equipMenu->isControllerFocusActive()
		&& !gameManager.menu->goodsMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Equip),
		"goods transfer reaches equipment without cancelling its source") && ok;

	gameManager.menu->goodsMenu->activated = false;
	gameManager.menu->bottomMenu->visible = false;
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelNext)
		&& transfers.activeDomain()
			== ControllerSlotDomain::PlayerEquipment
		&& transfers.source().has_value()
		&& transfers.source().value() == quickGoodsSource
		&& gameManager.menu->equipMenu->isControllerFocusActive(),
		"unavailable goods domains roll back focus and preserve the source") && ok;
	gameManager.menu->goodsMenu->activated = true;
	gameManager.menu->bottomMenu->visible = true;
	transfers.cancel();
	ok = check(!transfers.active()
		&& gameManager.menu->bottomMenu->isControllerFocusActive()
		&& !gameManager.menu->bottomMenu->goodsItem[0]->isTransferSelected(),
		"ending a switched goods session restores its quick-slot origin") && ok;
	const int bagGoodsIndex = gameManager.goodsManager.storeBegin();
	gameManager.goodsManager.goodsList[bagGoodsIndex].clear();
	ok = check(transfers.start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsQuick)
		&& transfers.begin(quickGoodsSource)
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.submit(
			{
				ControllerSlotKind::Goods,
				ControllerSlotDomain::GoodsBag,
				bagGoodsIndex
			},
			transferMessage) == ControllerTransferSubmitResult::Completed
		&& !transfers.hasSource()
		&& gameManager.goodsManager.goodsList[bagGoodsIndex].goods
			== quickGoodsIdentity
		&& !gameManager.goodsManager.goodsListExists(quickGoodsIndex),
		"production goods policy submits the preserved source across domains")
		&& ok;
	transfers.cancel();

	const int quickMagicIndex = gameManager.magicManager.bottomBegin();
	MagicInfo& quickMagic =
		gameManager.magicManager.magicList[quickMagicIndex];
	setControllerTestMagic(quickMagic, "controller_quick_magic.ini");
	const std::shared_ptr<Magic> quickMagicIdentity = quickMagic.magic;
	const ControllerSlotAddress quickMagicSource =
	{
		ControllerSlotKind::Magic,
		ControllerSlotDomain::MagicQuick,
		quickMagicIndex
	};
	ok = check(transfers.start(
		ControllerSlotKind::Magic, ControllerSlotDomain::MagicQuick)
		&& transfers.begin(quickMagicSource)
		&& gameManager.menu->bottomMenu->magicItem[0] != nullptr
		&& gameManager.menu->bottomMenu->magicItem[0]->isTransferSelected()
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.activeDomain() == ControllerSlotDomain::MagicList
		&& transfers.source().has_value()
		&& transfers.source().value() == quickMagicSource
		&& transfers.activeOwner() == gameManager.menu->magicMenu
		&& gameManager.menu->magicMenu->isControllerFocusActive()
		&& !gameManager.menu->bottomMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Magic)
		&& quickMagic.magic == quickMagicIdentity,
		"standalone magic list preserves a real quick-magic source") && ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.activeDomain() == ControllerSlotDomain::Practice
		&& transfers.source().has_value()
		&& transfers.source().value() == quickMagicSource
		&& transfers.activeOwner() == gameManager.menu->practiceMenu
		&& gameManager.menu->practiceMenu->isControllerFocusActive()
		&& !gameManager.menu->magicMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Practice),
		"practice domain uses the same source-preserving executor") && ok;
	transfers.cancel();
	const int listMagicIndex = gameManager.magicManager.storeBegin();
	gameManager.magicManager.magicList[listMagicIndex] = MagicInfo();
	ok = check(transfers.start(
		ControllerSlotKind::Magic, ControllerSlotDomain::MagicQuick)
		&& transfers.begin(quickMagicSource)
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.submit(
			{
				ControllerSlotKind::Magic,
				ControllerSlotDomain::MagicList,
				listMagicIndex
			},
			transferMessage) == ControllerTransferSubmitResult::Completed
		&& !transfers.hasSource()
		&& gameManager.magicManager.magicList[listMagicIndex].magic
			== quickMagicIdentity
		&& !gameManager.magicManager.magicListExists(quickMagicIndex),
		"production magic policy submits the preserved source across domains")
		&& ok;
	transfers.cancel();

	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->canEquip = 1;
	gameManager.npcManager->addNPC(partner);
	ok = check(gameManager.menu->openPartnerEquipment(partner, false),
		"partner modal opens for transfer-domain checks") && ok;
	ok = check(!transfers.start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsBag)
		&& !transfers.start(
			ControllerSlotKind::Goods, ControllerSlotDomain::GoodsQuick)
		&& !transfers.start(
			ControllerSlotKind::Magic, ControllerSlotDomain::MagicList)
		&& !transfers.start(
			ControllerSlotKind::Magic, ControllerSlotDomain::MagicQuick)
		&& !transfers.active(),
		"partner modal rejects ordinary transfer domains without partial state")
		&& ok;
	const int partnerBagIndex = gameManager.goodsManager.storeBegin();
	setControllerTestGoods(
		gameManager.goodsManager.goodsList[partnerBagIndex],
		"controller_partner_source.ini");
	const std::shared_ptr<Goods> partnerSourceIdentity =
		gameManager.goodsManager.goodsList[partnerBagIndex].goods;
	const ControllerSlotAddress partnerSource =
	{
		ControllerSlotKind::PartnerGoods,
		ControllerSlotDomain::PartnerBag,
		partnerBagIndex,
		std::static_pointer_cast<const void>(partner)
	};
	ok = check(transfers.start(
		ControllerSlotKind::PartnerGoods,
		ControllerSlotDomain::PartnerBag)
		&& transfers.begin(partnerSource)
		&& transfers.cycleDomain(1)
		&& transfers.activeDomain()
			== ControllerSlotDomain::PartnerEquipment
		&& transfers.source().has_value()
		&& transfers.source().value() == partnerSource
		&& transfers.activeOwner() == gameManager.menu->partnerEquipMenu
		&& gameManager.menu->partnerEquipMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu,
			UIFocusTestAccess::MenuRole::PartnerEquipment),
		"partner domains preserve the contextual source on one modal owner") && ok;
	auto replacementPartner = std::make_shared<NPC>();
	replacementPartner->kind = nkPartner;
	replacementPartner->canEquip = 1;
	gameManager.npcManager->addNPC(replacementPartner);
	gameManager.menu->partnerEquipMenu->setPartner(replacementPartner);
	transferMessage.clear();
	const ControllerTransferSubmitResult invalidContextSubmit = transfers.submit(
		{
			ControllerSlotKind::PartnerGoods,
			ControllerSlotDomain::PartnerEquipment,
			0,
			std::static_pointer_cast<const void>(replacementPartner)
		},
		transferMessage);
	ok = check(!transfers.active()
		&& !transfers.hasSource()
		&& invalidContextSubmit == ControllerTransferSubmitResult::Rejected
		&& gameManager.goodsManager.goodsList[partnerBagIndex].goods
			== partnerSourceIdentity
		&& replacementPartner->getEquipmentFileByPartIndex(0).empty(),
		"changing the partner cancels its contextual source without a ghost submit")
		&& ok;
	gameManager.menu->closePartnerEquipment(false);

	setControllerTestMagic(quickMagic, "controller_integrated_magic.ini");
	gameManager.global.feature.magicButtonOpensIntegratedEquip = true;
	gameManager.global.feature.stateEquipIntegratedLayout = true;
	gameManager.global.feature.hideRightMenusWithIntegratedEquip = true;
	gameManager.global.feature.practiceMenuDisabled = true;
	File::setActiveResourceRoot((assetsRoot / "xjxqy").generic_string());
	ok = check(UIFocusTestAccess::reloadIntegratedEquipmentMenu(
		*gameManager.menu),
		"transfer descriptor test loads integrated equipment resources") && ok;
	ok = check(UIFocusTestAccess::ensureMenuOpen(
		*gameManager.menu, UIFocusTestAccess::MenuRole::Equip)
		&& gameManager.menu->equipMenu->item[0] != nullptr
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->item[0],
		"shared-owner rollback starts from the equipment pane") && ok;
	for (const std::shared_ptr<Item>& item :
		gameManager.menu->equipMenu->magicDisplayItem)
	{
		if (item != nullptr)
		{
			item->visible = false;
		}
	}
	ok = check(!transfers.start(
		ControllerSlotKind::Magic, ControllerSlotDomain::MagicList)
		&& !transfers.active()
		&& gameManager.menu->equipMenu->visible
		&& gameManager.menu->equipMenu->isControllerFocusActive()
		&& gameManager.menu->equipMenu->controllerFocusedElement()
			== gameManager.menu->equipMenu->item[0]
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Equip),
		"failed shared-owner target restores the prior equipment pane") && ok;
	for (const std::shared_ptr<Item>& item :
		gameManager.menu->equipMenu->magicDisplayItem)
	{
		if (item != nullptr)
		{
			item->visible = true;
		}
	}
	gameManager.menu->stateMenu->visible = true;
	gameManager.menu->practiceMenu->visible = true;
	gameManager.menu->goodsMenu->visible = true;
	gameManager.menu->magicMenu->visible = true;
	gameManager.menu->memoMenu->visible = true;
	ok = check(transfers.start(
		ControllerSlotKind::Magic, ControllerSlotDomain::MagicQuick)
		&& transfers.begin(quickMagicSource)
		&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.activeDomain() == ControllerSlotDomain::MagicList
		&& transfers.source().has_value()
		&& transfers.source().value() == quickMagicSource
		&& transfers.activeOwner() == gameManager.menu->equipMenu
		&& gameManager.menu->equipMenu->isControllerFocusActive()
		&& UIFocusTestAccess::focusedRoleIs(
			*gameManager.menu, UIFocusTestAccess::MenuRole::Magic)
		&& !gameManager.menu->stateMenu->visible
		&& !gameManager.menu->practiceMenu->visible
		&& !gameManager.menu->goodsMenu->visible
		&& !gameManager.menu->magicMenu->visible
		&& !gameManager.menu->memoMenu->visible
		&& !gameManager.menu->equipMenu->magicDisplayItem.empty()
		&& std::find(
			gameManager.menu->equipMenu->magicDisplayItem.begin(),
			gameManager.menu->equipMenu->magicDisplayItem.end(),
			gameManager.menu->equipMenu->controllerFocusedElement())
				!= gameManager.menu->equipMenu->magicDisplayItem.end(),
		"integrated magic resolves the shared owner without losing its source")
		&& ok;
	ok = check(gameManager.menu->handleUIAction(UIAction::PanelPrevious)
		&& transfers.activeDomain() == ControllerSlotDomain::MagicQuick
		&& transfers.source().has_value()
		&& transfers.source().value() == quickMagicSource
		&& gameManager.menu->bottomMenu->isControllerFocusActive(),
		"disabled practice domain is skipped without losing the magic source")
		&& ok;
	transfers.cancel();

	setControllerTestGoods(quickGoods, "controller_lifecycle_source.ini");
	ok = check(transfers.start(
		ControllerSlotKind::Goods, ControllerSlotDomain::GoodsQuick)
		&& transfers.begin(quickGoodsSource),
		"lifecycle cleanup starts from a real goods source") && ok;
	gameManager.menu->clearMenu();
	ok = check(!transfers.active()
		&& gameManager.menu->bottomMenu->goodsItem[0] != nullptr
		&& !gameManager.menu->bottomMenu->goodsItem[0]->isTransferSelected(),
		"ordinary menu lifecycle cleanup ends transfer state and highlight") && ok;
	return ok;
}

bool testControllerTransferLifecycleIntegration()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path activeRoot = assetsRoot / "jxqy2";
	const std::filesystem::path commonRoot = assetsRoot / "common";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot(activeRoot.generic_string());
	File::setCommonResourceRoot(commonRoot.generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, commonRoot.generic_string());

	GameManager gameManager;
	gameManager.global.data.canInput = true;
	gameManager.global.feature.practiceMenuDisabled = false;
	gameManager.global.feature.magicButtonOpensIntegratedEquip = false;
	gameManager.global.feature.stateEquipIntegratedLayout = false;
	UIFocusTestAccess::initializeSlotMenus(*gameManager.menu);
	auto& transfers = gameManager.menu->controllerTransfers();
	auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
		Engine::getInstance()->inputActions());
	const int sourceIndex = gameManager.goodsManager.bottomBegin();
	const int targetIndex = gameManager.goodsManager.storeBegin();
	const ControllerSlotAddress sourceAddress =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsQuick,
		sourceIndex
	};
	const ControllerSlotAddress targetAddress =
	{
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsBag,
		targetIndex
	};
	std::string transferMessage;

	auto beginCrossDomainTransfer = [&](const std::string& fileName)
	{
		transfers.cancel();
		gameManager.goodsManager.goodsList[targetIndex].clear();
		setControllerTestGoods(
			gameManager.goodsManager.goodsList[sourceIndex], fileName);
		const std::shared_ptr<Goods> sourceIdentity =
			gameManager.goodsManager.goodsList[sourceIndex].goods;
		const bool started = transfers.start(
			ControllerSlotKind::Goods,
			ControllerSlotDomain::GoodsQuick)
			&& transfers.begin(sourceAddress)
			&& gameManager.menu->handleUIAction(UIAction::PanelPrevious)
			&& transfers.activeDomain()
				== ControllerSlotDomain::GoodsBag
			&& transfers.source().has_value()
			&& transfers.source().value() == sourceAddress;
		return std::make_pair(started, sourceIdentity);
	};
	auto checkCancelledWithoutSubmission = [&](
		const std::shared_ptr<Goods>& sourceIdentity,
		const char* expectation)
	{
		transferMessage.clear();
		const ControllerTransferSubmitResult submitResult =
			transfers.submit(targetAddress, transferMessage);
		return check(!transfers.active()
			&& !transfers.hasSource()
			&& submitResult == ControllerTransferSubmitResult::Rejected
			&& gameManager.menu->bottomMenu->goodsItem[0] != nullptr
			&& !gameManager.menu->bottomMenu->goodsItem[0]
				->isTransferSelected()
			&& gameManager.goodsManager.goodsList[sourceIndex].goods
				== sourceIdentity
			&& !gameManager.goodsManager.goodsListExists(targetIndex),
			expectation);
	};

	{
		const auto [started, sourceIdentity] = beginCrossDomainTransfer(
			"controller_close_lifecycle_source.ini");
		ok = check(started,
			"menu-close lifecycle starts a real cross-domain transfer") && ok;
		ok = check(UIFocusTestAccess::closeFocusedMenu(*gameManager.menu),
			"closing the active transfer menu uses the production close path")
			&& ok;
		ok = checkCancelledWithoutSubmission(
			sourceIdentity,
			"menu close cancels the source and prevents a cross-domain ghost submit")
			&& ok;
	}

	{
		const auto [started, sourceIdentity] = beginCrossDomainTransfer(
			"controller_owner_lifecycle_source.ini");
		ok = check(started,
			"owner lifecycle starts a real cross-domain transfer") && ok;
		gameManager.menu->goodsMenu->activated = false;
		gameManager.menu->handleUIAction(UIAction::NavigateRight);
		gameManager.menu->goodsMenu->activated = true;
		ok = checkCancelledWithoutSubmission(
			sourceIdentity,
			"invalid active owner cancels the source and prevents a ghost submit")
			&& ok;
	}

	inputManager.setWindowFocused(true);
	gameManager.controller->processPhysicalInputFrame();
	gameManager.menu->handleUIAction(UIAction::NavigateUp);
	{
		const auto [started, sourceIdentity] = beginCrossDomainTransfer(
			"controller_focus_lifecycle_source.ini");
		ok = check(started,
			"window-focus lifecycle starts a real cross-domain transfer") && ok;
		const std::uint64_t lifecycleBeforeFocusLoss =
			inputManager.inputLifecycleRevision();
		inputManager.setWindowFocused(false);
		gameManager.controller->processPhysicalInputFrame();
		ok = check(inputManager.inputLifecycleRevision()
				> lifecycleBeforeFocusLoss
			&& !inputManager.isInputContextActive(),
			"window focus loss advances the production input lifecycle") && ok;
		ok = checkCancelledWithoutSubmission(
			sourceIdentity,
			"window focus loss cancels the source and prevents a ghost submit")
			&& ok;
		inputManager.setWindowFocused(true);
		gameManager.controller->processPhysicalInputFrame();
		ok = check(!transfers.active() && !transfers.hasSource(),
			"window focus recovery does not resurrect a cancelled transfer") && ok;
	}

	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"transfer lifecycle test starts without SDL video") && ok;
	try
	{
		VirtualGamepadTest::SDLSession sdlSession;
		ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
			"event/gamepad initialization does not initialize SDL video") && ok;
		inputManager.shutdown();
		VirtualGamepadTest::VirtualGamepad gamepad(
			"JXQY Transfer Lifecycle Pad");
		ok = check(inputManager.initialize(),
			"production physical input manager initializes for disconnect testing")
			&& ok;
		std::uint64_t nowMilliseconds = 100;
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		ok = check(inputManager.activeGamepadID() == gamepad.id(),
			"disconnect lifecycle test claims the production gamepad channel") && ok;
		gameManager.controller->processPhysicalInputFrame();
		gameManager.menu->handleUIAction(UIAction::NavigateUp);

		const auto [started, sourceIdentity] = beginCrossDomainTransfer(
			"controller_disconnect_lifecycle_source.ini");
		ok = check(started,
			"gamepad-disconnect lifecycle starts a real cross-domain transfer")
			&& ok;
		const std::uint64_t lifecycleBeforeDisconnect =
			inputManager.inputLifecycleRevision();
		gamepad.detach();
		VirtualGamepadTest::runFrame(inputManager, nowMilliseconds += 10);
		gameManager.controller->processPhysicalInputFrame();
		ok = check(inputManager.inputLifecycleRevision()
				> lifecycleBeforeDisconnect
			&& !inputManager.hasActiveGamepad(),
			"active gamepad disconnect advances the production input lifecycle")
			&& ok;
		ok = checkCancelledWithoutSubmission(
			sourceIdentity,
			"gamepad disconnect cancels the source and prevents a ghost submit")
			&& ok;
		inputManager.shutdown();
		ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
			"disconnect lifecycle remains strictly headless") && ok;
	}
	catch (const std::exception& exception)
	{
		inputManager.shutdown();
		std::cerr << "FAILED: " << exception.what() << '\n';
		ok = false;
	}
	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"transfer lifecycle test ends without SDL video") && ok;
	return ok;
}

bool testExplicitEquipmentGraph()
{
	bool ok = true;
	std::vector<std::shared_ptr<Item>> items;
	for (int index = 0; index < 7; index++)
	{
		auto item = std::make_shared<Item>();
		item->rect = { index * 10, index * 10, 8, 8 };
		items.push_back(item);
	}
	SlotGridBinding binding;
	binding.focusIdPrefix = "equipment-slot-";
	binding.items = items;
	binding.fixedColumnCount = 3;
	binding.resolveLogicalIndex = [](int visibleIndex) { return 200 + visibleIndex; };
	SlotGridController grid;
	grid.bind(std::move(binding));
	grid.setNeighbour(0, UIFocusDirection::Down, 5);
	grid.setNeighbour(5, UIFocusDirection::Right, 2);
	grid.setNeighbour(2, UIFocusDirection::Down, 6);

	ok = check(grid.activate(), "explicit equipment graph activates") && ok;
	ok = check(grid.handleAction(UIAction::NavigateDown)
		&& grid.focusedVisibleIndex() == 5,
		"explicit graph overrides array-grid order") && ok;
	ok = check(grid.handleAction(UIAction::NavigateRight)
		&& grid.focusedVisibleIndex() == 2,
		"explicit graph follows semantic middle-row order") && ok;
	ok = check(grid.handleAction(UIAction::NavigateDown)
		&& grid.focusedLogicalIndex() == 206,
		"explicit graph reaches the semantic bottom slot") && ok;
	return ok;
}

bool testSlotInteractionAdapter()
{
	bool ok = true;
	auto item = std::make_shared<Item>();
	item->rect = { 0, 0, 20, 20 };
	auto sourceObject = std::make_shared<int>(7);
	auto transferContext = std::make_shared<int>(9);
	ControllerTransferCoordinator transfers;
	ControllerTransferCoordinator::Policy policy;
	policy.domainOrder = { ControllerSlotDomain::GoodsBag };
	policy.identifySource = [sourceObject, transferContext](
		const ControllerSlotAddress& address)
	{
		if (address.logicalIndex != 12
			|| address.context
				!= std::static_pointer_cast<const void>(transferContext))
		{
			return ControllerSlotIdentity();
		}
		return ControllerSlotIdentity{ sourceObject, "source" };
	};
	int submissions = 0;
	policy.submit = [&submissions](
		const ControllerSlotAddress&,
		const ControllerSlotAddress&,
		std::string&)
	{
		submissions++;
		return ControllerTransferSubmitResult::Completed;
	};
	transfers.setPolicy(ControllerSlotKind::Goods, std::move(policy));
	ControllerTransferCoordinator::DomainBinding domain;
	domain.kind = ControllerSlotKind::Goods;
	domain.domain = ControllerSlotDomain::GoodsBag;
	domain.activate = []() { return true; };
	transfers.registerDomain(std::move(domain));

	int primaryActions = 0;
	int hiddenDetails = 0;
	SlotInteractionBinding binding;
	binding.grid.focusIdPrefix = "interaction-slot-";
	binding.grid.items = { item };
	binding.grid.resolveLogicalIndex = [](int) { return 12; };
	binding.grid.primary = [&primaryActions](int, int) { primaryActions++; };
	binding.grid.hideDetails = [&hiddenDetails]() { hiddenDetails++; };
	binding.transfers = &transfers;
	binding.kind = ControllerSlotKind::Goods;
	binding.domain = ControllerSlotDomain::GoodsBag;
	binding.resolveContext = [transferContext]()
	{
		return std::static_pointer_cast<const void>(transferContext);
	};

	SlotInteractionController controller;
	controller.bind(std::move(binding));
	ok = check(controller.activate(),
		"slot interaction adapter activates its shared grid") && ok;
	ok = check(controller.handleAction(UIAction::Confirm)
		&& primaryActions == 1 && !transfers.active(),
		"confirm invokes business semantics outside transfer mode") && ok;
	ok = check(controller.handleAction(UIAction::Secondary)
		&& transfers.hasSource() && transfers.source().has_value()
		&& transfers.source()->context
			== std::static_pointer_cast<const void>(transferContext),
		"secondary starts a context-scoped shared transfer session") && ok;
	controller.refreshTransferSelection();
	ok = check(item->isTransferSelected(),
		"adapter derives transfer highlight from coordinator state") && ok;
	ok = check(controller.handleAction(UIAction::Confirm)
		&& submissions == 1 && primaryActions == 1 && !transfers.active(),
		"confirm submits through the coordinator when a source exists") && ok;
	controller.refreshTransferSelection();
	ok = check(!item->isTransferSelected(),
		"completed same-domain transfer clears the source highlight") && ok;
	controller.deactivate();
	ok = check(hiddenDetails > 0 && !controller.isActive(),
		"deactivation shares focus and details cleanup") && ok;
	return ok;
}

bool testControllerPaneRouter()
{
	bool ok = true;
	ControllerPaneRouter router(
		ControllerPaneActionPolicy::CyclePanes);
	ControllerActionTarget& actionTarget = router;
	bool secondPaneAvailable = false;
	int activePane = -1;
	int handledActions = 0;
	auto makePane = [&activePane, &handledActions](
		int paneId, const std::function<bool()>& available,
		bool activationSucceeds = true)
	{
		ControllerPaneDescriptor pane;
		pane.id = paneId;
		pane.isAvailable = available;
		pane.activate = [&activePane, paneId, activationSucceeds]()
		{
			if (!activationSucceeds)
			{
				return false;
			}
			activePane = paneId;
			return true;
		};
		pane.deactivate = [&activePane, paneId]()
		{
			if (activePane == paneId)
			{
				activePane = -1;
			}
		};
		pane.handleAction = [&handledActions](UIAction)
		{
			handledActions++;
			return true;
		};
		return pane;
	};
	router.registerPane(makePane(10, []() { return true; }));
	router.registerPane(makePane(20,
		[&secondPaneAvailable]() { return secondPaneAvailable; }));
	router.registerPane(makePane(30, []() { return true; }));
	router.registerPane(makePane(40, []() { return true; }, false));
	router.setDefaultPane(10);

	ok = check(actionTarget.activate() && activePane == 10,
		"pane router activates its default pane through the action target protocol")
		&& ok;
	ok = check(actionTarget.handleAction(UIAction::PanelNext)
		&& activePane == 30 && router.selectedPaneId() == 30,
		"cycle policy skips unavailable panes through the action target protocol") && ok;
	ok = check(!router.activatePane(40) && activePane == 30,
		"failed pane activation restores the previous active pane") && ok;
	actionTarget.deactivate();
	ok = check(activePane == -1 && !actionTarget.isActive()
		&& router.selectedPaneId() == 30,
		"virtual deactivation preserves the selected logical pane") && ok;
	ok = check(actionTarget.handleAction(UIAction::Confirm)
		&& activePane == 30 && handledActions == 1,
		"next semantic action resumes and delegates to the selected pane") && ok;
	secondPaneAvailable = true;
	ok = check(actionTarget.handleAction(UIAction::PanelPrevious)
		&& activePane == 20,
		"pane router includes panes that become available later") && ok;
	return ok;
}

bool testControllerPaneRouterDestructionDoesNotInvokeBindings()
{
	int deactivationCount = 0;
	{
		ControllerPaneRouter router;
		ControllerPaneDescriptor pane;
		pane.id = 10;
		pane.activate = []()
		{
			return true;
		};
		pane.deactivate = [&deactivationCount]()
		{
			deactivationCount++;
		};
		router.registerPane(std::move(pane));
		router.setDefaultPane(10);
		if (!check(router.activateDefaultPane(),
			"pane router activates before destruction"))
		{
			return false;
		}
	}
	return check(deactivationCount == 0,
		"pane router destruction does not call borrowed bindings");
}

class FakeControllerFocusParticipant :
	public Element,
	public ControllerFocusParticipant
{
public:
	bool activationSucceeds = true;
	bool active = false;
	int activationCount = 0;
	ControllerFocusTarget lastTarget = ControllerFocusTarget::Default;

	bool activateControllerFocus(ControllerFocusTarget target) override
	{
		activationCount++;
		lastTarget = target;
		active = activationSucceeds;
		return active;
	}

	bool isControllerFocusActive() const override
	{
		return active;
	}

	void deactivateControllerFocus() override
	{
		active = false;
	}
};

bool testControllerFocusRestoreTransaction()
{
	bool ok = true;
	MenuController menuController;
	auto previousOwner = std::make_shared<FakeControllerFocusParticipant>();
	auto mismatchedOwner = std::make_shared<FakeControllerFocusParticipant>();
	auto failingOwner = std::make_shared<FakeControllerFocusParticipant>();
	failingOwner->activationSucceeds = false;

	ok = check(UIFocusTestAccess::restoreDefaultFocus(
		menuController, previousOwner)
		&& previousOwner->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(menuController) == previousOwner,
		"focus restoration commits an owner after activation succeeds") && ok;
	ok = check(!UIFocusTestAccess::restoreGoodsFocus(
		menuController, mismatchedOwner)
		&& mismatchedOwner->activationCount == 0
		&& previousOwner->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(menuController) == previousOwner,
		"descriptor owner mismatch preserves the previous focus state") && ok;
	ok = check(!UIFocusTestAccess::restoreDefaultFocus(
		menuController, failingOwner)
		&& previousOwner->isControllerFocusActive()
		&& !failingOwner->isControllerFocusActive()
		&& UIFocusTestAccess::focusedOwner(menuController) == previousOwner,
		"failed focus restoration preserves the previous owner") && ok;
	return ok;
}

class FakeControllerActionTarget : public ControllerActionTarget
{
public:
	bool activationSucceeds = true;
	bool remainsActiveAfterFailedActivation = false;
	bool active = false;
	int activationCount = 0;
	int deactivationCount = 0;
	int handledActionCount = 0;
	UIAction lastAction = UIAction::Cancel;
	PElement focusedElement;
	std::vector<PElement> focusCandidates;

	bool activate() override
	{
		activationCount++;
		active = activationSucceeds
			|| remainsActiveAfterFailedActivation;
		return activationSucceeds;
	}

	void deactivate() override
	{
		deactivationCount++;
		active = false;
	}

	bool isActive() const override
	{
		return active;
	}

	bool handleAction(UIAction action) override
	{
		handledActionCount++;
		lastAction = action;
		return true;
	}

	PElement controllerFocusedElement() const override
	{
		return focusedElement;
	}

	std::vector<PElement> controllerFocusCandidates() const override
	{
		return focusCandidates;
	}

	bool focusControllerElement(const PElement& element) override
	{
		if (std::find(focusCandidates.begin(), focusCandidates.end(), element)
			== focusCandidates.end())
		{
			return false;
		}
		focusedElement = element;
		return true;
	}
};

bool testControllerPaneRouterPassThroughPolicy()
{
	bool ok = true;
	ControllerPaneRouter router(
		ControllerPaneActionPolicy::PassThrough);
	FakeControllerActionTarget defaultTarget;
	FakeControllerActionTarget nextTarget;
	router.registerTargetPane(10, defaultTarget);
	router.registerTargetPane(20, nextTarget);
	router.setDefaultPane(10);
	ControllerActionTarget& actionTarget = router;

	ok = check(actionTarget.activate()
		&& defaultTarget.isActive()
		&& router.selectedPaneId() == 10,
		"pass-through router activates through the action target protocol") && ok;
	ok = check(!actionTarget.handleAction(UIAction::PanelNext)
		&& defaultTarget.isActive()
		&& nextTarget.activationCount == 0
		&& router.selectedPaneId() == 10,
		"pass-through policy leaves panel actions for the parent owner") && ok;
	ok = check(actionTarget.handleAction(UIAction::Confirm)
		&& defaultTarget.handledActionCount == 1
		&& defaultTarget.lastAction == UIAction::Confirm,
		"pass-through policy still delegates ordinary semantic actions") && ok;
	actionTarget.deactivate();
	const int activationCountAfterDeactivate =
		defaultTarget.activationCount;
	ok = check(!actionTarget.handleAction(UIAction::PanelPrevious)
		&& !actionTarget.isActive()
		&& defaultTarget.activationCount == activationCountAfterDeactivate,
		"a passed-through panel action does not resume a suspended child target") && ok;
	ok = check(actionTarget.activate()
		&& defaultTarget.activationCount
			== activationCountAfterDeactivate + 1,
		"virtual reactivation restores the selected child target") && ok;
	return ok;
}

bool testControllerActionTargetPaneRegistration()
{
	bool ok = true;
	ControllerPaneRouter router;
	FakeControllerActionTarget defaultTarget;
	FakeControllerActionTarget unavailableTarget;
	FakeControllerActionTarget nextTarget;
	FakeControllerActionTarget failingTarget;
	failingTarget.activationSucceeds = false;
	bool unavailable = false;
	int defaultPreparations = 0;
	int nextPreparations = 0;
	int failedPreparations = 0;
	auto defaultCandidate = makeTestFocusElement(0, 0);
	auto unavailableCandidate = makeTestFocusElement(30, 0);
	auto nextCandidate = makeTestFocusElement(60, 0);
	defaultTarget.focusCandidates = { defaultCandidate };
	defaultTarget.focusedElement = defaultCandidate;
	unavailableTarget.focusCandidates = { unavailableCandidate };
	unavailableTarget.focusedElement = unavailableCandidate;
	nextTarget.focusCandidates = { nextCandidate };
	nextTarget.focusedElement = nextCandidate;

	router.registerTargetPane(
		10,
		defaultTarget,
		[]() { return true; },
		[&defaultPreparations]() { defaultPreparations++; });
	router.registerTargetPane(
		20,
		unavailableTarget,
		[&unavailable]() { return unavailable; });
	router.registerTargetPane(
		30,
		nextTarget,
		[]() { return true; },
		[&nextPreparations]() { nextPreparations++; });
	router.registerTargetPane(
		40,
		failingTarget,
		[]() { return true; },
		[&failedPreparations]() { failedPreparations++; });
	router.setDefaultPane(10);

	ok = check(router.activateDefaultPane()
		&& defaultTarget.isActive()
		&& defaultTarget.activationCount == 1
		&& defaultPreparations == 1
		&& router.controllerFocusedElement() == defaultCandidate
		&& router.controllerFocusCandidates()
			== std::vector<PElement>({ defaultCandidate, nextCandidate }),
		"target pane activates the default target after preparing it") && ok;
	ok = check(router.focusControllerElement(nextCandidate)
		&& !defaultTarget.isActive()
		&& nextTarget.isActive()
		&& router.selectedPaneId() == 30
		&& router.controllerFocusedElement() == nextCandidate,
		"target pane did not activate the owning pane and focus an exact candidate") && ok;
	ok = check(router.focusControllerElement(defaultCandidate)
		&& defaultTarget.isActive()
		&& !nextTarget.isActive()
		&& router.selectedPaneId() == 10,
		"target pane did not return to an exact candidate in another pane") && ok;
	ok = check(router.handleAction(UIAction::Confirm)
		&& defaultTarget.handledActionCount == 1
		&& defaultTarget.lastAction == UIAction::Confirm,
		"target pane delegates semantic actions through the virtual interface") && ok;
	ok = check(router.handleAction(UIAction::PanelNext)
		&& !defaultTarget.isActive()
		&& defaultTarget.deactivationCount == 2
		&& unavailableTarget.activationCount == 0
		&& nextTarget.isActive()
		&& nextPreparations == 2
		&& router.selectedPaneId() == 30,
		"target pane cycling deactivates the old target and skips unavailable targets") && ok;
	ok = check(!router.activatePane(40)
		&& failedPreparations == 1
		&& failingTarget.activationCount == 1
		&& !failingTarget.isActive()
		&& nextTarget.isActive()
		&& nextTarget.activationCount == 3
		&& router.selectedPaneId() == 30,
		"failed target activation restores the previously selected target") && ok;

	nextTarget.activationSucceeds = false;
	nextTarget.remainsActiveAfterFailedActivation = true;
	ok = check(!router.activatePane(40)
		&& failedPreparations == 2
		&& !router.isActive()
		&& !nextTarget.isActive()
		&& router.selectedPaneId() == 30,
		"failed rollback activation cleans up the previously selected target") && ok;
	return ok;
}

bool testThreeResourceBuySellControllerRouting()
{
	bool ok = true;
	auto dispatchPointerEvents = [](
		GameManager& gameManager, std::vector<AEvent> events)
	{
		Engine* engine = Engine::getInstance();
		if (engine == nullptr || engine->getEventCount() != 0)
		{
			return false;
		}
		for (AEvent& event : events)
		{
			engine->pushEvent(event);
		}
		UIFocusTestAccess::dispatchElementEvents(gameManager);
		return engine->getEventCount() == 0;
	};
	auto dispatchPointerFrame =
		[&dispatchPointerEvents](GameManager& gameManager, AEvent event)
	{
		return dispatchPointerEvents(gameManager, { event });
	};
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	ResourceManager& resourceManager = ResourceManager::instance();
	ok = check(resourceManager.initialize(assetsRoot.generic_string()),
		"three-resource buy-sell routing initializes the resource collection")
		&& ok;

	struct ResourcePackExpectation
	{
		const char* id;
		int gameType;
	};
	const ResourcePackExpectation resourcePacks[] =
	{
		{ "JXQY2", GAME_JXQY2 },
		{ "XJXQY", GAME_XJXQY },
		{ "YYCS", GAME_YYCS }
	};

	for (const ResourcePackExpectation& resourcePack : resourcePacks)
	{
		auto checkPack = [&ok, &resourcePack](
			bool condition, const char* expectation)
		{
			const std::string message = std::string(resourcePack.id)
				+ " " + expectation;
			ok = check(condition, message.c_str()) && ok;
			return condition;
		};

		if (!checkPack(resourceManager.setActiveResourcePackById(resourcePack.id),
			"resource pack can be selected for buy-sell routing"))
		{
			continue;
		}
		const ResourceManifest& manifest = resourceManager.getActiveManifest();
		checkPack(manifest.type == resourcePack.gameType,
			"resource pack exposes the expected game type");

		GameManager gameManager;
		gameManager.global.useWav = manifest.useWav;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.loadUiSettings();
		gameManager.goodsManager.configureLayout();
		gameManager.magicManager.configureLayout();
		gameManager.global.data.canInput = true;
		gameManager.menu->init();

		const std::shared_ptr<BuySellMenu>& buySellMenu =
			gameManager.menu->buySellMenu;
		const bool completeMenu = buySellMenu != nullptr
			&& buySellMenu->title != nullptr
			&& buySellMenu->image != nullptr
			&& buySellMenu->closeBtn != nullptr
			&& buySellMenu->scrollbar != nullptr
			&& buySellMenu->item.size() == 9
			&& std::all_of(
				buySellMenu->item.begin(),
				buySellMenu->item.end(),
				[](const std::shared_ptr<Item>& item)
				{
					return item != nullptr;
				});
		if (!checkPack(completeMenu,
			"loads the production buy-sell menu with all nine slots"))
		{
			continue;
		}
		checkPack(buySellMenu->scrollbar->pageSize == 9
			&& buySellMenu->scrollbar->lineSize == 3,
			"keeps the production three-by-three shop grid contract");
		const bool completePlayerPane = gameManager.menu->goodsMenu != nullptr
			&& gameManager.menu->goodsMenu->scrollbar != nullptr
			&& !gameManager.menu->goodsMenu->item.empty()
			&& gameManager.menu->goodsMenu->item[0] != nullptr
			&& gameManager.menu->toolTip != nullptr
			&& gameManager.menu->toolTip->name != nullptr;
		if (!checkPack(completePlayerPane,
			"loads the player inventory pane used by the shop router"))
		{
			continue;
		}

		buySellMenu->clearGoodsList();
		setControllerTestGoods(
			buySellMenu->goodsList[0],
			"controller_three_resource_shop.ini");
		const std::string expectedGoodsName = std::string(resourcePack.id)
			+ " controller shop goods";
		buySellMenu->goodsList[0].goods->name = expectedGoodsName;
		buySellMenu->bsKind = bsBuy;
		buySellMenu->visible = true;
		gameManager.menu->goodsMenu->visible = true;
		buySellMenu->setRunning(true);
		buySellMenu->updateGoods();
		checkPack(dispatchPointerFrame(
				gameManager,
				AEvent(
					ET_MOUSEMOTION,
					TOUCH_MOUSEID,
					-100000,
					-100000,
					false,
					true)),
			"settles production shop pane state before controller input");
		checkPack(buySellMenu->handleUIAction(UIAction::NavigateRight)
				&& UIFocusTestAccess::buySellControllerActive(*buySellMenu)
				&& UIFocusTestAccess::buySellControllerFocusedElement(
					*buySellMenu) == buySellMenu->item[1],
			"routes directional input through the production shop grid");
		const std::shared_ptr<Item> pointerHintItem = buySellMenu->item[0];
		const int pointerHintX =
			pointerHintItem->rect.x + std::max(1, pointerHintItem->rect.w / 2);
		const int pointerHintY =
			pointerHintItem->rect.y + std::max(1, pointerHintItem->rect.h / 2);
		checkPack(dispatchPointerFrame(
				gameManager,
				AEvent(
					ET_MOUSEMOTION,
					TOUCH_MOUSEID,
					pointerHintX,
					pointerHintY,
					false,
					true))
				&& pointerHintItem->touchingID == TOUCH_MOUSEID
				&& UIFocusTestAccess::buySellControllerActive(*buySellMenu)
				&& UIFocusTestAccess::buySellControllerFocusedElement(
					*buySellMenu) == buySellMenu->item[1],
			"keeps shop controller focus while synthetic hover starts");
		pointerHintItem->setTime(
			pointerHintItem->getTime()
				+ pointerHintItem->beginShowHintTime + 1);
		checkPack(dispatchPointerFrame(
				gameManager,
				AEvent(
					ET_MOUSEMOTION,
					TOUCH_MOUSEID,
					pointerHintX,
					pointerHintY,
					false,
					true))
				&& gameManager.menu->toolTip->visible
				&& gameManager.menu->toolTip->name->getStr()
					== expectedGoodsName
				&& UIFocusTestAccess::buySellControllerActive(*buySellMenu)
				&& UIFocusTestAccess::buySellControllerFocusedElement(
					*buySellMenu) == buySellMenu->item[1],
			"shows the production Item tooltip after its delay without"
			" synthetic hover suspending shop controller focus");
		if (resourcePack.gameType == GAME_XJXQY)
		{
			int windowWidth = 0;
			int windowHeight = 0;
			Engine::getInstance()->getWindowSize(windowWidth, windowHeight);
			int expectedToolTipX = pointerHintItem->rect.x
				+ pointerHintItem->rect.w + 8;
			if (expectedToolTipX + gameManager.menu->toolTip->rect.w
				> windowWidth)
			{
				expectedToolTipX = pointerHintItem->rect.x
					- gameManager.menu->toolTip->rect.w - 8;
			}
			expectedToolTipX = std::max(0, std::min(
				expectedToolTipX,
				windowWidth - gameManager.menu->toolTip->rect.w));
			const int expectedToolTipY = std::max(0, std::min(
				pointerHintItem->rect.y,
				windowHeight - gameManager.menu->toolTip->rect.h));
			checkPack(gameManager.menu->toolTip->rect.x == expectedToolTipX
					&& gameManager.menu->toolTip->rect.y == expectedToolTipY,
				"anchors the pointer tooltip beside its concrete shop slot");
		}
		checkPack(dispatchPointerEvents(
				gameManager,
				{
					AEvent(
						ET_MOUSEMOTION,
						TOUCH_MOUSEID,
						pointerHintX,
						pointerHintY,
						false,
						false),
					AEvent(
						ET_MOUSEDOWN,
						MBC_MOUSE_LEFT,
						pointerHintX,
						pointerHintY,
						false)
				})
				&& pointerHintItem->touchingDownID == TOUCH_MOUSEID,
			"keeps concrete pointer-down acquisition for the production"
			" shop slot");
		checkPack(dispatchPointerFrame(
				gameManager,
				AEvent(
					ET_MOUSEUP,
					MBC_MOUSE_LEFT,
					pointerHintX,
					pointerHintY,
					false))
				&& !UIFocusTestAccess::buySellControllerActive(*buySellMenu)
				&& UIFocusTestAccess::buySellControllerFocusedElement(
					*buySellMenu) == nullptr,
			"keeps real shop click cleanup after pointer takeover");
		checkPack(buySellMenu->handleUIAction(UIAction::NavigateRight),
			"restores shop controller focus after real pointer cleanup");
		checkPack(buySellMenu->handleUIAction(UIAction::PanelNext),
			"routes panel-next from the shop grid to the player inventory");
		checkPack(buySellMenu->handleUIAction(UIAction::PanelPrevious),
			"restores the previous shop focus when returning from inventory");
		checkPack(buySellMenu->handleUIAction(UIAction::NavigateLeft)
			&& buySellMenu->handleUIAction(UIAction::Details)
			&& gameManager.menu->toolTip->visible
			&& gameManager.menu->toolTip->name->getStr() == expectedGoodsName,
			"routes shop details through the shared production tooltip");
		checkPack(buySellMenu->handleUIAction(UIAction::Cancel)
			&& !UIFocusTestAccess::isLogicRunning(*buySellMenu)
			&& !gameManager.menu->toolTip->visible
			&& std::none_of(
				buySellMenu->item.begin(),
				buySellMenu->item.end(),
				[](const std::shared_ptr<Item>& item)
				{
					return item != nullptr && item->isFocused();
				}),
			"consumes cancel and clears controller focus without entering run()");
	}
	ok = check(resourceManager.setActiveResourcePackById("JXQY2"),
		"three-resource buy-sell routing restores the default resource pack")
		&& ok;
	return ok;
}
}

bool runUIFocusTests()
{
	bool ok = true;
	ok = testInputEventHelpers() && ok;
	ok = testCrossControlPointerDragContract() && ok;
	ok = testInputAwareFocusPresentation() && ok;
	ok = testKeyboardEventRoutingPrecedence() && ok;
	ok = testFocusGraph() && ok;
	ok = testLinearFocusGroups() && ok;
	ok = testVisualSpatialFocusGroups() && ok;
	ok = testConfigDrivenFocusNavigation() && ok;
	ok = testConfigDrivenFocusNavigationMenuParsing() && ok;
	ok = testGroupedFocusRecovery() && ok;
	ok = testMemoRightStickScrolling() && ok;
	ok = testSlotGridScrollingAndSelection() && ok;
	ok = testSlotGridRebindFocusRecovery() && ok;
	ok = testPointerTakeoverFocusRecovery() && ok;
	ok = testConfigDrivenMenuResizeFocusContinuity() && ok;
	ok = testBottomMenuSpatialQuickSlotConnectivity() && ok;
	ok = testRuntimeSlotMenuResizeRecovery() && ok;
	ok = testControllerMenuTransitions() && ok;
	ok = testControllerTransferDomainDescriptors() && ok;
	ok = testControllerTransferLifecycleIntegration() && ok;
	ok = testExplicitEquipmentGraph() && ok;
	ok = testSlotInteractionAdapter() && ok;
	ok = testControllerPaneRouter() && ok;
	ok = testControllerPaneRouterDestructionDoesNotInvokeBindings() && ok;
	ok = testControllerFocusRestoreTransaction() && ok;
	ok = testControllerPaneRouterPassThroughPolicy() && ok;
	ok = testControllerActionTargetPaneRegistration() && ok;
	ok = testControllerToolTipRouting() && ok;
	ok = testFocusCandidateAPIs() && ok;
	// ResourceManager is process-global and has no reset API. Keep this after
	// all other resource-dependent tests so its File root changes cannot affect
	// peers.
	ok = testThreeResourceBuySellControllerRouting() && ok;
	// The virtual controller fixture advances the process-global physical-input
	// lifecycle revision. Run it last so manually initialized MenuController
	// fixtures cannot mistake that test-only transition for a live disconnect.
	ok = testInputAwarePhysicalFocusPresentation() && ok;
	return ok;
}
