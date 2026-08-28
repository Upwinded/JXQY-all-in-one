#include "../Engine/Engine.h"
#include "../File/File.h"
#include "../Game/Data/Goods.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/MobileTouchInteraction.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/Object.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/BottomMenu.h"
#include "../Game/Menu/BuySellMenu.h"
#include "../Game/Menu/ControllerFocusParticipant.h"
#include "../Game/Menu/EquipMenu.h"
#include "../Game/Menu/GoodsMenu.h"
#include "../Game/Menu/MagicMenu.h"
#include "../Game/Menu/MapThumbnailMenu.h"
#include "../Game/Menu/PartnerEquipMenu.h"
#include "../Game/Menu/PartnerHeadMenu.h"
#include "../Game/Menu/PracticeMenu.h"
#include "../Game/Menu/System.h"
#include "../Game/Menu/UIFocusManager.h"
#include "../Resource/ResourceManager.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>

class GamepadRPGMenuActionsTestAccess
{
public:
	static std::vector<PElement> collectVisibleControllerFocusCandidates(
		const MenuController& menuController)
	{
		std::vector<PElement> elements;
		for (const auto& candidate :
			menuController.collectVisibleControllerFocusCandidates())
		{
			elements.push_back(candidate.element);
		}
		return elements;
	}

	static void dispatchElementEvents(Element& root)
	{
		root.postTreatment();
		root.allHandleEvents();
	}

	static void makeLongPress(Element& element)
	{
		element.setTime(MOBILE_RIGHT_SCRIPT_LONG_PRESS_MS);
		element.touchingDownTime = 0;
	}

	static bool findUncoveredPointerPosition(
		Element& root,
		int width,
		int height,
		Point& position)
	{
		for (int y = 8; y < height; y += 16)
		{
			for (int x = 8; x < width; x += 16)
			{
				if (root.findPointerHitTargetInTree(x, y) == nullptr)
				{
					position = { x, y };
					return true;
				}
			}
		}
		return false;
	}
};

namespace
{
struct ResourcePackExpectation
{
	std::string id;
	int gameType;
	std::string partnerName;
};

std::vector<ResourcePackExpectation> discoverResourcePackExpectations(
	ResourceManager& resourceManager)
{
	std::vector<ResourcePackExpectation> expectations;
	for (const ResourceManager::ResourcePack& pack :
		resourceManager.getDiscoveredPacks())
	{
		std::string partnerName;
		if (pack.manifest.type == GAME_XJXQY)
		{
			partnerName = "独孤剑";
		}
		else if (pack.manifest.type == GAME_YYCS)
		{
			partnerName = "杨影枫";
		}
		expectations.push_back(
			{ pack.manifest.id, pack.manifest.type, partnerName });
	}
	return expectations;
}

bool readDirectChildResourcePackIds(
	const std::filesystem::path& collectionRoot,
	std::set<std::string>& resourceIds,
	std::string& error)
{
	std::error_code iteratorError;
	for (const std::filesystem::directory_entry& entry :
		std::filesystem::directory_iterator(collectionRoot, iteratorError))
	{
		if (iteratorError)
		{
			error = "cannot enumerate " + collectionRoot.generic_string();
			return false;
		}
		if (!entry.is_directory(iteratorError) || iteratorError)
		{
			iteratorError.clear();
			continue;
		}
		const std::filesystem::path manifestPath =
			entry.path() / "game_profile.ini";
		if (!std::filesystem::is_regular_file(manifestPath, iteratorError))
		{
			iteratorError.clear();
			continue;
		}
		std::ifstream input(manifestPath, std::ios::binary);
		const std::string bytes{
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
		ResourceManifest manifest;
		if (input.bad() || bytes.empty() ||
			bytes.size() > static_cast<std::size_t>(
				std::numeric_limits<int>::max()) ||
			!manifest.loadFromBuffer(
				bytes.data(), static_cast<int>(bytes.size())) ||
			manifest.id.empty())
		{
			error = "invalid direct-child manifest: " +
				manifestPath.generic_string();
			return false;
		}
		if (!resourceIds.insert(manifest.id).second)
		{
			error = "duplicate direct-child Game.Id: " + manifest.id;
			return false;
		}
	}
	if (iteratorError)
	{
		error = "cannot enumerate " + collectionRoot.generic_string();
		return false;
	}
	return true;
}

std::string describeStringSet(const std::set<std::string>& values)
{
	std::string description;
	for (const std::string& value : values)
	{
		if (!description.empty())
		{
			description += ",";
		}
		description += value;
	}
	return description;
}

const ResourcePackExpectation BaseResourcePacks[] =
{
	{ "JXQY2", GAME_JXQY2, "" },
	{ "XJXQY", GAME_XJXQY, "独孤剑" },
	{ "YYCS", GAME_YYCS, "杨影枫" }
};

constexpr const char* ShopGoodsFile =
	"controller_rpg_menu_shop.ini";
constexpr const char* DrugGoodsFile =
	"controller_rpg_menu_drug.ini";
constexpr const char* ScriptGoodsFile =
	"controller_rpg_menu_script.ini";
constexpr const char* ScriptGoodsScript =
	"controller_rpg_menu_script.txt";
constexpr const char* PartnerHeadGoodsAFile =
	"controller_rpg_menu_partner_head_a.ini";
constexpr const char* PartnerHeadGoodsBFile =
	"controller_rpg_menu_partner_head_b.ini";
constexpr int ShopGoodsCost = 100;
constexpr int ShopGoodsSellPrice = 40;
constexpr int DrugLifeRecovery = 37;
constexpr int ScriptLifeRecovery = 23;

bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool checkPack(
	bool condition,
	const ResourcePackExpectation& resourcePack,
	const std::string& expectation)
{
	return check(condition, resourcePack.id + " " + expectation);
}

bool videoSubsystemIsStopped(const std::string& checkpoint)
{
	return check(
		(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		checkpoint + " without initializing SDL video");
}

void dispatchPointerTapThroughElementTree(
	GameManager& gameManager,
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
	// Match production event priority without entering Element::run().
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}

void dispatchPointerDownThroughElementTree(
	GameManager& gameManager,
	int x,
	int y)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEMOTION, TOUCH_MOUSEID, x, y, false));
	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEDOWN, MBC_MOUSE_LEFT, x, y, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}

void dispatchPointerDownWithoutMotionThroughElementTree(
	GameManager& gameManager,
	int x,
	int y)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEDOWN, MBC_MOUSE_LEFT, x, y, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}

void dispatchPointerRightDownThroughElementTree(
	GameManager& gameManager,
	int x,
	int y)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEMOTION, TOUCH_MOUSEID, x, y, false));
	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEDOWN, MBC_MOUSE_RIGHT, x, y, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}

void dispatchPointerRightDownWithoutMotionThroughElementTree(
	GameManager& gameManager,
	int x,
	int y)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEDOWN, MBC_MOUSE_RIGHT, x, y, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}

void dispatchWindowResizeThroughElementTree(
	GameManager& gameManager,
	int width,
	int height)
{
	Engine::getInstance()->setWindowSize(width, height);
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(AEvent(
		ET_WINDOWRESIZE, 0, width, height, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}

#ifdef __MOBILE__
void dispatchFingerEventThroughElementTree(
	GameManager& gameManager,
	EventType eventType,
	EventTouchID pointerID,
	int x,
	int y)
{
	AEvent queuedEvent;
	while (Engine::getInstance()->getEvent(queuedEvent) > 0)
	{
	}
	Engine::getInstance()->pushEvent(AEvent(
		eventType, pointerID, x, y, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
}
#endif

class RightButtonPassThroughProbe : public Element
{
public:
	RightButtonPassThroughProbe()
	{
		coverMouse = false;
		setPriority(epController + 1);
	}

	int rightButtonDownCount = 0;

protected:
	bool onHandleEvent(AEvent& event) override
	{
		if (event.eventType == ET_MOUSEDOWN
			&& event.eventData == MBC_MOUSE_RIGHT)
		{
			rightButtonDownCount++;
			return true;
		}
		return false;
	}
};

class HeadlessRPGMenuUIRoot : public Element
{
public:
	explicit HeadlessRPGMenuUIRoot(GameManager& gameManager)
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

class ImmediateStopSystem : public System
{
private:
	void onRun() override
	{
		stop(erOK);
	}
};

class HeadlessRPGPointerNPC : public NPC
{
public:
	bool mouseInRect(int x, int y) override
	{
		return Element::mouseInRect(x, y);
	}
};

class HeadlessRPGPointerObject : public Object
{
public:
	bool mouseInRect(int x, int y) override
	{
		return Element::mouseInRect(x, y);
	}
};

struct RPGPhysicalActionObservation
{
	bool semanticActionPressed = false;
	bool worldAliasPressed = false;
	bool semanticInputBlocked = false;
	bool worldStateQueuedAfterPress = false;
};

RPGPhysicalActionObservation tapRPGPhysicalButton(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	GameInput::InputAction semanticAction,
	GameInput::InputAction worldAlias,
	GameManager& gameManager,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	RPGPhysicalActionObservation observation;
	HeadlessPhysicalInputTest::FrameCallbacks pressCallbacks;
	pressCallbacks.afterInputUpdate =
		[&observation, semanticAction, worldAlias](
			const GameInput::PhysicalInputManager& inputManager)
	{
		observation.semanticActionPressed =
			inputManager.wasActionPressed(semanticAction);
		observation.worldAliasPressed =
			worldAlias == GameInput::InputAction::Count
				|| inputManager.wasActionPressed(worldAlias);
	};
	pressCallbacks.afterDispatch =
		[&gameManager, &observation](bool semanticInputBlocked)
	{
		observation.semanticInputBlocked = semanticInputBlocked;
		observation.worldStateQueuedAfterPress =
			gameManager.player->nextAction != nullptr;
	};
	frameDriver.tapButton(gamepad, button, pressCallbacks);
	return observation;
}

bool testPersistentHUDWorldInputComposition(
	const ResourcePackExpectation& resourcePack,
	VirtualGamepadTest::VirtualGamepad& gamepad,
	GameManager& gameManager,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	MenuController& menuController = *gameManager.menu;
	auto& player = *gameManager.player;
	menuController.clearMenu();
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	gameManager.npcManager->npcList.clear();
	gameManager.objectManager->objectList.clear();
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->clickIndex = -1;

	bool ok = checkPack(
		menuController.bottomMenu != nullptr
			&& menuController.bottomMenu->visible
			&& (menuController.topMenu == nullptr
				|| menuController.topMenu->visible)
			&& !menuController.menuDisplayed()
			&& !menuController.bottomMenu->isControllerFocusActive()
			&& (menuController.topMenu == nullptr
				|| !menuController.topMenu->isControllerFocusActive()),
		resourcePack,
		"starts with only persistent HUD surfaces and no logical HUD focus");

	const RPGPhysicalActionObservation firstSouth =
		tapRPGPhysicalButton(
			gamepad,
			SDL_GAMEPAD_BUTTON_SOUTH,
			GameInput::InputAction::Confirm,
			GameInput::InputAction::InteractPrimary,
			gameManager,
			frameDriver);
	ok = checkPack(
		firstSouth.semanticActionPressed
			&& firstSouth.worldAliasPressed
			&& !firstSouth.semanticInputBlocked
			&& !firstSouth.worldStateQueuedAfterPress
			&& player.nextAction == nullptr
			&& !menuController.menuDisplayed()
			&& !menuController.bottomMenu->isControllerFocusActive()
			&& (menuController.topMenu == nullptr
				|| !menuController.topMenu->isControllerFocusActive()),
		resourcePack,
		"keeps South as world interaction without attacking when persistent HUD has no explicit focus")
		&& ok;

	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	const RPGPhysicalActionObservation enterHUD =
		tapRPGPhysicalButton(
			gamepad,
			SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
			GameInput::InputAction::NavigateRight,
			GameInput::InputAction::Count,
			gameManager,
			frameDriver);
	const bool bottomFocused =
		menuController.bottomMenu->isControllerFocusActive();
	const bool topFocused = menuController.topMenu != nullptr
		&& menuController.topMenu->isControllerFocusActive();
	ok = checkPack(
		enterHUD.semanticActionPressed
			&& enterHUD.semanticInputBlocked
			&& (bottomFocused != topFocused)
			&& !menuController.menuDisplayed()
			&& player.nextAction == nullptr,
		resourcePack,
		"enters exactly one persistent HUD surface only after a direction press")
		&& ok;

	const RPGPhysicalActionObservation leaveHUD =
		tapRPGPhysicalButton(
			gamepad,
			SDL_GAMEPAD_BUTTON_EAST,
			GameInput::InputAction::Cancel,
			GameInput::InputAction::CastSkill3,
			gameManager,
			frameDriver);
	ok = checkPack(
		leaveHUD.semanticActionPressed
			&& leaveHUD.semanticInputBlocked
			&& !menuController.bottomMenu->isControllerFocusActive()
			&& (menuController.topMenu == nullptr
				|| !menuController.topMenu->isControllerFocusActive())
			&& !menuController.menuDisplayed()
			&& player.nextAction == nullptr,
		resourcePack,
		"Cancel exits persistent HUD focus without leaking its world alias"
		" (blocked=" + std::to_string(leaveHUD.semanticInputBlocked)
		+ ", bottom=" + std::to_string(
			menuController.bottomMenu->isControllerFocusActive())
		+ ", top=" + std::to_string(
			menuController.topMenu != nullptr
				&& menuController.topMenu->isControllerFocusActive())
		+ ", displayed=" + std::to_string(menuController.menuDisplayed())
		+ ", queued=" + std::to_string(player.nextAction != nullptr) + ")")
		&& ok;

	frameDriver.runFrame();
	frameDriver.runFrame();
	const RPGPhysicalActionObservation secondAttack =
		tapRPGPhysicalButton(
			gamepad,
			SDL_GAMEPAD_BUTTON_WEST,
			GameInput::InputAction::Secondary,
			GameInput::InputAction::AttackPrimary,
			gameManager,
			frameDriver);
	ok = checkPack(
		secondAttack.semanticActionPressed
			&& secondAttack.worldAliasPressed
			&& !secondAttack.semanticInputBlocked
			&& player.nextAction != nullptr
			&& player.nextAction->action == acAttack
			&& !menuController.bottomMenu->isControllerFocusActive()
			&& (menuController.topMenu == nullptr
				|| !menuController.topMenu->isControllerFocusActive()),
		resourcePack,
		"returns West to world AttackPrimary after leaving persistent HUD focus")
		&& ok;
	player.cancelQueuedInteraction(false);
	player.nextAction = nullptr;
	return ok;
}

void configureGameManager(
	GameManager& gameManager,
	const ResourceManifest& manifest)
{
	gameManager.global.useWav = manifest.useWav;
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.global.loadUiSettings();
	gameManager.goodsManager.configureLayout();
	gameManager.magicManager.configureLayout();
	gameManager.global.data.canInput = true;
}

bool writeGoodsFixture(
	const std::filesystem::path& goodsDirectory,
	const std::string& fileName,
	const std::string& name,
	int kind,
	const std::string& extraFields)
{
	std::ofstream output(goodsDirectory / fileName, std::ios::binary);
	if (!output)
	{
		return false;
	}
	output
		<< "[Init]\n"
		<< "Name=" << name << "\n"
		<< "Kind=" << kind << "\n"
		<< extraFields;
	return static_cast<bool>(output);
}

bool writeRPGMenuFixtures(const std::filesystem::path& root)
{
	std::error_code errorCode;
	const std::filesystem::path goodsDirectory = root / "ini" / "goods";
	std::filesystem::create_directories(goodsDirectory, errorCode);
	if (errorCode)
	{
		return false;
	}
	const std::filesystem::path goodsScriptDirectory =
		root / "script" / "goods";
	std::filesystem::create_directories(goodsScriptDirectory, errorCode);
	if (errorCode)
	{
		return false;
	}

	std::ofstream scriptOutput(
		goodsScriptDirectory / ScriptGoodsScript,
		std::ios::binary);
	if (!scriptOutput)
	{
		return false;
	}
	scriptOutput
		<< "addlife(" << ScriptLifeRecovery << ");\n"
		<< "delgoods();\n";
	if (!scriptOutput)
	{
		return false;
	}

	return writeGoodsFixture(
		goodsDirectory,
		ShopGoodsFile,
		"Controller RPG Menu Shop Item",
		gkNormal,
		"Cost=" + std::to_string(ShopGoodsCost)
			+ "\nSellPrice=" + std::to_string(ShopGoodsSellPrice) + "\n")
		&& writeGoodsFixture(
			goodsDirectory,
			DrugGoodsFile,
			"Controller RPG Menu Drug",
			gkDrug,
			"Life=" + std::to_string(DrugLifeRecovery) + "\n")
		&& writeGoodsFixture(
			goodsDirectory,
			ScriptGoodsFile,
			"Controller RPG Menu Script Item",
			gkNormal,
			std::string("Script=") + ScriptGoodsScript + "\n")
		&& writeGoodsFixture(
			goodsDirectory,
			PartnerHeadGoodsAFile,
			"Controller RPG Menu Partner Head A",
			gkEquipment,
			"Part=head\n")
		&& writeGoodsFixture(
			goodsDirectory,
			PartnerHeadGoodsBFile,
			"Controller RPG Menu Partner Head B",
			gkEquipment,
			"Part=head\n");
}

bool loadGoodsInfo(
	GoodsInfo& goodsInfo,
	const std::string& fileName,
	int number = 1)
{
	goodsInfo.clear();
	goodsInfo.iniFile = fileName;
	goodsInfo.number = number;
	goodsInfo.goods = std::make_shared<Goods>();
	goodsInfo.goods->initFromIni(fileName);
	if (!goodsInfo.goods->loadSucceeded)
	{
		goodsInfo.clear();
		return false;
	}
	return true;
}

void setEquipmentFixture(GoodsInfo& goodsInfo)
{
	goodsInfo.clear();
	goodsInfo.iniFile = "controller_rpg_menu_head.ini";
	goodsInfo.number = 1;
	goodsInfo.goods = std::make_shared<Goods>();
	goodsInfo.goods->name = "Controller RPG Menu Head Equipment";
	goodsInfo.goods->kind = gkEquipment;
	goodsInfo.goods->part = "head";
}

bool testGoodsEquipAndUnequip(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	const int storeIndex = gameManager.goodsManager.storeBegin();
	const int equipmentIndex = gameManager.goodsManager.equipIndex(0);
	if (!checkPack(
		storeIndex >= 0
			&& storeIndex < gameManager.goodsManager.listLength()
			&& equipmentIndex >= 0
			&& equipmentIndex < gameManager.goodsManager.listLength()
			&& gameManager.goodsManager.isStoreIndex(storeIndex)
			&& gameManager.goodsManager.isEquipIndex(equipmentIndex),
		resourcePack,
		"exposes valid bag and head-equipment slots"))
	{
		return false;
	}

	gameManager.goodsManager.clearItem();
	setEquipmentFixture(gameManager.goodsManager.goodsList[storeIndex]);
	const std::shared_ptr<Goods> equipmentIdentity =
		gameManager.goodsManager.goodsList[storeIndex].goods;
	menuController.goodsMenu->updateGoods();
	menuController.equipMenu->updateGoods();
	menuController.toggleGoodsView();
	ok = checkPack(
		menuController.goodsMenu->visible
			&& menuController.goodsMenu->isControllerFocusActive()
			&& !menuController.goodsMenu->item.empty()
			&& menuController.goodsMenu->focusControllerElement(
				menuController.goodsMenu->item.front()),
		resourcePack,
		"opens the production goods menu with an explicit first-slot focus")
		&& ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::Confirm)
			&& !gameManager.goodsManager.goodsListExists(storeIndex)
			&& gameManager.goodsManager.goodsList[equipmentIndex].goods
				== equipmentIdentity,
		resourcePack,
		"routes Confirm through GoodsMenu and equips the focused item") && ok;

	menuController.toggleEquipView();
	ok = checkPack(
		menuController.equipMenu->visible
			&& menuController.equipMenu->isControllerFocusActive(),
		resourcePack,
		"opens the production equipment menu with controller focus") && ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::Confirm)
			&& gameManager.goodsManager.goodsList[storeIndex].goods
				== equipmentIdentity
			&& !gameManager.goodsManager.goodsListExists(equipmentIndex),
		resourcePack,
		"routes Confirm through EquipMenu and unequips into the first bag slot")
		&& ok;
	gameManager.goodsManager.clearItem();
	menuController.clearMenu();
	return ok;
}

bool testGoodsUseAndArrange(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager,
	const std::filesystem::path& fixtureRoot)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	const int firstStoreIndex = gameManager.goodsManager.storeBegin();
	const int secondStoreIndex = firstStoreIndex + 1;
	const std::string productionRoot =
		resourceManager.getActiveResourceRoot();
	if (!checkPack(
		gameManager.goodsManager.isStoreIndex(firstStoreIndex)
			&& gameManager.goodsManager.isStoreIndex(secondStoreIndex),
		resourcePack,
		"exposes two production bag slots for use and X arrange actions"))
	{
		return false;
	}

	File::setActiveResourceRoot(fixtureRoot.generic_string());
	File::setResourceFallbackRoots({ productionRoot });
	gameManager.goodsManager.clearItem();
	ok = checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[firstStoreIndex],
			DrugGoodsFile),
		resourcePack,
		"loads the non-script drug through the production goods loader") && ok;
	gameManager.player->lifeMax = 1000;
	gameManager.player->calInfo();
	gameManager.player->life = 100;
	menuController.goodsMenu->updateGoods();
	menuController.clearMenu();
	menuController.toggleGoodsView();
	const int lifeBeforeDrug = gameManager.player->life;
	ok = checkPack(
		menuController.goodsMenu->visible
			&& menuController.goodsMenu->isControllerFocusActive()
			&& !menuController.goodsMenu->item.empty()
			&& menuController.goodsMenu->focusControllerElement(
				menuController.goodsMenu->item.front())
			&& menuController.handleUIAction(UIAction::Confirm)
			&& gameManager.player->life
				== lifeBeforeDrug + DrugLifeRecovery
			&& !gameManager.goodsManager.goodsListExists(firstStoreIndex),
		resourcePack,
		"routes bag Confirm through the standard drug-use path and consumes one item")
		&& ok;

	ok = checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[firstStoreIndex],
			ScriptGoodsFile),
		resourcePack,
		"loads the script-backed normal item through the production goods loader")
		&& ok;
	menuController.goodsMenu->updateGoods();
	ok = checkPack(
		!menuController.goodsMenu->item.empty()
			&& menuController.goodsMenu->focusControllerElement(
				menuController.goodsMenu->item.front()),
		resourcePack,
		"restores the script item as the explicit controller target") && ok;
	const int lifeBeforeScript = gameManager.player->life;
	ok = checkPack(
		menuController.handleUIAction(UIAction::Confirm)
			&& gameManager.player->life
				== lifeBeforeScript + ScriptLifeRecovery
			&& !gameManager.goodsManager.goodsListExists(firstStoreIndex)
			&& !gameManager.inEvent,
		resourcePack,
		"runs and completes the real goods script through bag Confirm") && ok;

	ok = checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[firstStoreIndex],
			PartnerHeadGoodsAFile)
			&& loadGoodsInfo(
				gameManager.goodsManager.goodsList[secondStoreIndex],
				PartnerHeadGoodsBFile),
		resourcePack,
		"loads two production equipment records for X arrange") && ok;
	const std::shared_ptr<Goods> firstIdentity =
		gameManager.goodsManager.goodsList[firstStoreIndex].goods;
	const std::shared_ptr<Goods> secondIdentity =
		gameManager.goodsManager.goodsList[secondStoreIndex].goods;
	menuController.goodsMenu->updateGoods();
	ok = checkPack(
		!menuController.goodsMenu->item.empty()
			&& menuController.goodsMenu->focusControllerElement(
				menuController.goodsMenu->item.front())
			&& menuController.handleUIAction(UIAction::Secondary)
			&& menuController.controllerTransfers().active(
				ControllerSlotKind::Goods)
			&& menuController.controllerTransfers().hasSource(),
		resourcePack,
		"starts the production Goods X session from the focused bag slot") && ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::NavigateRight)
			&& menuController.handleUIAction(UIAction::Secondary)
			&& !menuController.controllerTransfers().active()
			&& gameManager.goodsManager.goodsList[firstStoreIndex].goods
				== secondIdentity
			&& gameManager.goodsManager.goodsList[secondStoreIndex].goods
				== firstIdentity,
		resourcePack,
		"moves focus and submits a real same-bag X exchange") && ok;

	menuController.clearMenu();
	gameManager.goodsManager.clearItem();
	resourceManager.setActiveResourcePackById(resourcePack.id);
	return ok;
}

std::vector<MagicInfo> loadProductionMagicInfos(
	const std::filesystem::path& resourceRoot,
	int requestedCount)
{
	std::vector<std::filesystem::path> magicFiles;
	std::error_code errorCode;
	const std::filesystem::path magicDirectory =
		resourceRoot / "ini" / "magic";
	for (std::filesystem::directory_iterator iterator(
		magicDirectory, errorCode), end;
		!errorCode && iterator != end;
		iterator.increment(errorCode))
	{
		if (iterator->is_regular_file(errorCode)
			&& iterator->path().extension() == ".ini")
		{
			magicFiles.push_back(iterator->path());
		}
	}
	std::sort(magicFiles.begin(), magicFiles.end(),
		[](const std::filesystem::path& left,
			const std::filesystem::path& right)
		{
			return left.generic_u8string() < right.generic_u8string();
		});

	std::vector<MagicInfo> result;
	for (const std::filesystem::path& magicFile : magicFiles)
	{
		MagicInfo magicInfo;
		magicInfo.iniFile = magicFile.filename().u8string();
		magicInfo.magic = std::make_shared<Magic>();
		magicInfo.magic->initFromIni(magicInfo.iniFile, false);
		if (!magicInfo.magic->loadSucceeded)
		{
			continue;
		}
		magicInfo.level = 1;
		result.push_back(std::move(magicInfo));
		if (static_cast<int>(result.size()) >= requestedCount)
		{
			break;
		}
	}
	return result;
}

void clearMagicList(GameManager& gameManager)
{
	std::fill(
		gameManager.magicManager.magicList.begin(),
		gameManager.magicManager.magicList.end(),
		MagicInfo());
}

bool openProductionMagicMenu(GameManager& gameManager)
{
	gameManager.menu->clearMenu();
	gameManager.menu->toggleMagicView();
	if (gameManager.global.feature.magicButtonOpensIntegratedEquip)
	{
		return gameManager.menu->equipMenu->visible
			&& !gameManager.menu->magicMenu->visible
			&& gameManager.menu->equipMenu->isControllerFocusActive();
	}
	return gameManager.menu->magicMenu->visible
		&& gameManager.menu->magicMenu->isControllerFocusActive();
}

bool testMagicActions(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	const int storeIndex = gameManager.magicManager.storeBegin();
	const int secondStoreIndex = storeIndex + 1;
	const int bottomCount = gameManager.magicManager.bottomCount();
	const int requestedMagicCount = std::max(2, bottomCount + 1);
	const std::vector<MagicInfo> productionMagics =
		loadProductionMagicInfos(
			resourceManager.getActiveResourceRoot(),
			requestedMagicCount);
	if (!checkPack(
		gameManager.magicManager.isStoreIndex(storeIndex)
			&& gameManager.magicManager.isStoreIndex(secondStoreIndex)
			&& bottomCount > 0
			&& static_cast<int>(productionMagics.size())
				>= requestedMagicCount,
		resourcePack,
		"loads enough real production magic records for controller actions"))
	{
		return false;
	}

	clearMagicList(gameManager);
	gameManager.magicManager.magicList[storeIndex] = productionMagics[0];
	gameManager.magicManager.updateMenu();
	ok = checkPack(
		openProductionMagicMenu(gameManager),
		resourcePack,
		gameManager.global.feature.magicButtonOpensIntegratedEquip
			? "opens the integrated production MagicList owner"
			: "opens the standalone production MagicList owner") && ok;
	const std::shared_ptr<Magic> quickMagicIdentity =
		gameManager.magicManager.magicList[storeIndex].magic;
	ok = checkPack(
		menuController.handleUIAction(UIAction::Confirm)
			&& !gameManager.magicManager.magicListExists(storeIndex)
			&& gameManager.magicManager.magicList[
				gameManager.magicManager.bottomBegin()].magic
				== quickMagicIdentity,
		resourcePack,
		"routes MagicList Confirm to the first free quick-magic slot") && ok;

	clearMagicList(gameManager);
	gameManager.magicManager.magicList[storeIndex] = productionMagics[0];
	gameManager.magicManager.updateMenu();
	if (!gameManager.global.feature.practiceMenuDisabled)
	{
		menuController.clearMenu();
		menuController.togglePracticeView();
		menuController.toggleMagicView();
		const std::shared_ptr<Magic> practiceMagicIdentity =
			gameManager.magicManager.magicList[storeIndex].magic;
		ok = checkPack(
			menuController.practiceMenu->visible
				&& menuController.handleUIAction(UIAction::Confirm)
				&& !gameManager.magicManager.magicListExists(storeIndex)
				&& gameManager.magicManager.magicList[
					gameManager.magicManager.practiceIndex()].magic
					== practiceMagicIdentity,
			resourcePack,
			"routes MagicList Confirm to the visible production Practice slot")
			&& ok;
	}
	else
	{
		menuController.togglePracticeView();
		ok = checkPack(
			!menuController.practiceMenu->visible,
			resourcePack,
			"keeps the resource-disabled Practice surface unavailable") && ok;
	}

	clearMagicList(gameManager);
	gameManager.magicManager.magicList[storeIndex] = productionMagics[0];
	std::vector<std::shared_ptr<Magic>> quickMagicIdentities;
	for (int slot = 0; slot < bottomCount; slot++)
	{
		const int targetIndex = gameManager.magicManager.bottomIndex(slot);
		gameManager.magicManager.magicList[targetIndex] =
			productionMagics[slot + 1];
		quickMagicIdentities.push_back(
			gameManager.magicManager.magicList[targetIndex].magic);
	}
	gameManager.magicManager.updateMenu();
	ok = checkPack(
		openProductionMagicMenu(gameManager),
		resourcePack,
		"reopens the production MagicList owner before the full quick-bar rejection")
		&& ok;
	const std::shared_ptr<Magic> rejectedMagicIdentity =
		gameManager.magicManager.magicList[storeIndex].magic;
	bool quickSlotsUnchanged = true;
	const bool fullActionHandled =
		menuController.handleUIAction(UIAction::Confirm);
	for (int slot = 0; slot < bottomCount; slot++)
	{
		quickSlotsUnchanged = quickSlotsUnchanged
			&& gameManager.magicManager.magicList[
				gameManager.magicManager.bottomIndex(slot)].magic
				== quickMagicIdentities[slot];
	}
	ok = checkPack(
		fullActionHandled
			&& gameManager.magicManager.magicList[storeIndex].magic
				== rejectedMagicIdentity
			&& quickSlotsUnchanged,
		resourcePack,
		"consumes full quick-bar Confirm while preserving every magic slot")
		&& ok;

	clearMagicList(gameManager);
	gameManager.magicManager.magicList[storeIndex] = productionMagics[0];
	gameManager.magicManager.magicList[secondStoreIndex] = productionMagics[1];
	gameManager.magicManager.updateMenu();
	ok = checkPack(
		openProductionMagicMenu(gameManager),
		resourcePack,
		"reopens the production MagicList owner before the X exchange") && ok;
	const std::shared_ptr<Magic> firstIdentity =
		gameManager.magicManager.magicList[storeIndex].magic;
	const std::shared_ptr<Magic> secondIdentity =
		gameManager.magicManager.magicList[secondStoreIndex].magic;
	ok = checkPack(
		menuController.handleUIAction(UIAction::Secondary)
			&& menuController.controllerTransfers().active(
				ControllerSlotKind::Magic)
			&& menuController.controllerTransfers().hasSource(),
		resourcePack,
		"starts the production Magic X session") && ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::NavigateRight)
			&& menuController.handleUIAction(UIAction::Secondary)
			&& !menuController.controllerTransfers().active()
			&& gameManager.magicManager.magicList[storeIndex].magic
				== secondIdentity
			&& gameManager.magicManager.magicList[secondStoreIndex].magic
				== firstIdentity,
		resourcePack,
		"submits a real same-list Magic X exchange") && ok;

	menuController.clearMenu();
	clearMagicList(gameManager);
	gameManager.magicManager.updateMenu();
	return ok;
}

bool testPartnerActions(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager,
	const std::filesystem::path& fixtureRoot)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	if (!gameManager.global.feature.partnerHeadMenu)
	{
		return checkPack(
			resourcePack.partnerName.empty()
				&& !menuController.partnerHeadMenu->hasControllerPartners(),
			resourcePack,
			"keeps the unsupported PartnerHead controller page unavailable");
	}
	if (!checkPack(
		!resourcePack.partnerName.empty(),
		resourcePack,
		"provides a production partner-head resource expectation"))
	{
		return false;
	}

	const std::string productionRoot =
		resourceManager.getActiveResourceRoot();
	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->npcName = resourcePack.partnerName;
	partner->canEquip = 1;
	partner->level = 10;
	gameManager.npcManager->addNPC(partner);
	menuController.partnerHeadMenu->visible = true;
	menuController.partnerHeadMenu->activated = true;
	menuController.goodsMenu->updateGoods();
	menuController.clearMenu();
	menuController.toggleEquipView();
	for (int switchCount = 0;
		switchCount < 4
			&& !menuController.partnerHeadMenu->isControllerFocusActive();
		switchCount++)
	{
		menuController.handleUIAction(UIAction::PanelNext);
	}
	ok = checkPack(
		menuController.partnerHeadMenu->hasControllerPartners()
			&& menuController.partnerHeadMenu->isControllerFocusActive()
			&& menuController.handleUIAction(UIAction::Confirm)
			&& menuController.partnerEquipMenu->visible
			&& menuController.partnerEquipMenu->getPartner() == partner,
		resourcePack,
		"cycles into PartnerHead and routes Confirm through MenuController")
		&& ok;

	File::setActiveResourceRoot(fixtureRoot.generic_string());
	File::setResourceFallbackRoots({ productionRoot });
	gameManager.goodsManager.clearItem();
	const int storeIndex = gameManager.goodsManager.storeBegin();
	ok = checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[storeIndex],
			PartnerHeadGoodsAFile),
		resourcePack,
		"loads the player equipment used by PartnerEquip A") && ok;
	menuController.goodsMenu->updateGoods();

	if (!checkPack(
		!menuController.goodsMenu->item.empty()
			&& menuController.goodsMenu->item.front() != nullptr,
		resourcePack,
		"exposes the underlying Goods item for modal pointer isolation"))
	{
		menuController.closePartnerEquipment(false);
		gameManager.npcManager->removeNPCOnlyFromList(partner);
		gameManager.goodsManager.clearItem();
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}
	const PElement underlyingGoodsItem =
		menuController.goodsMenu->item.front();
	const Rect underlyingGoodsRect = underlyingGoodsItem->rect;
	underlyingGoodsItem->rect = { 8, 8, 36, 36 };
	underlyingGoodsItem->cancelPointerInteraction();
	const int modalOutsideX =
		underlyingGoodsItem->rect.x + underlyingGoodsItem->rect.w / 2;
	const int modalOutsideY =
		underlyingGoodsItem->rect.y + underlyingGoodsItem->rect.h / 2;
	const std::shared_ptr<Goods> modalGoodsIdentity =
		gameManager.goodsManager.goodsList[storeIndex].goods;
	const int modalGoodsCount =
		gameManager.goodsManager.goodsList[storeIndex].number;
	ok = checkPack(
		!menuController.partnerEquipMenu->rect.PointInRect(
			modalOutsideX, modalOutsideY),
		resourcePack,
		"places the pointer outside the PartnerEquip panel but inside Goods")
		&& ok;
	dispatchPointerDownThroughElementTree(
		gameManager, modalOutsideX, modalOutsideY);
	ok = checkPack(
		menuController.partnerEquipMenu->visible
			&& menuController.partnerEquipMenu->isControllerFocusActive()
			&& !menuController.goodsMenu->isControllerFocusActive()
			&& underlyingGoodsItem->touchingID == TOUCH_MOUSEID
			&& underlyingGoodsItem->touchingDownID == TOUCH_MOUSEID
			&& menuController.partnerEquipMenu->controllerFocusedElement()
				== underlyingGoodsItem
			&& !menuController.controllerTransfers().active()
			&& gameManager.goodsManager.goodsListExists(storeIndex)
			&& gameManager.goodsManager.goodsList[storeIndex].goods
				== modalGoodsIdentity
			&& gameManager.goodsManager.goodsList[storeIndex].number
				== modalGoodsCount
			&& partner->getEquipmentFileByPartIndex(0).empty(),
		resourcePack,
		"keeps the borrowed Goods bag interactive inside the PartnerEquip"
		" modal scope without activating its standalone owner") && ok;
	underlyingGoodsItem->cancelPointerInteraction();
	menuController.partnerEquipMenu->focusControllerEquipment();
	underlyingGoodsItem->rect = underlyingGoodsRect;

	ok = checkPack(
		menuController.handleUIAction(UIAction::PanelNext)
			&& menuController.partnerEquipMenu->isControllerFocusActive()
			&& menuController.handleUIAction(UIAction::Confirm)
			&& partner->getEquipmentFileByPartIndex(0)
				== PartnerHeadGoodsAFile
			&& !gameManager.goodsManager.goodsListExists(storeIndex),
		resourcePack,
		"switches to the player bag and equips the focused item with PartnerEquip A")
		&& ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::PanelNext)
			&& menuController.handleUIAction(UIAction::Confirm)
			&& partner->getEquipmentFileByPartIndex(0).empty()
			&& gameManager.goodsManager.goodsListExists(storeIndex)
			&& gameManager.goodsManager.goodsList[storeIndex].iniFile
				== PartnerHeadGoodsAFile,
		resourcePack,
		"returns to partner equipment and unequips the focused slot with A")
		&& ok;

	gameManager.goodsManager.clearItem();
	ok = checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[storeIndex],
			PartnerHeadGoodsBFile)
			&& partner->setEquipmentFileByPartIndex(
				0, PartnerHeadGoodsAFile),
		resourcePack,
		"prepares two loaded singleton head items for precise PartnerEquip X")
		&& ok;
	menuController.goodsMenu->updateGoods();
	menuController.partnerEquipMenu->updateGoods();
	ok = checkPack(
		menuController.partnerEquipMenu->focusControllerPlayerBag()
			&& menuController.handleUIAction(UIAction::Secondary)
			&& menuController.controllerTransfers().active(
				ControllerSlotKind::PartnerGoods)
			&& menuController.controllerTransfers().hasSource(),
		resourcePack,
		"starts PartnerGoods X from the real borrowed player-bag pane") && ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::PanelNext)
			&& menuController.handleUIAction(UIAction::Secondary)
			&& menuController.controllerTransfers().active(
				ControllerSlotKind::PartnerGoods)
			&& !menuController.controllerTransfers().hasSource()
			&& partner->getEquipmentFileByPartIndex(0)
				== PartnerHeadGoodsBFile
			&& gameManager.goodsManager.goodsList[storeIndex].iniFile
				== PartnerHeadGoodsAFile,
		resourcePack,
		"cycles to partner equipment and submits the contextual X exchange")
		&& ok;

	menuController.cancelControllerInteraction();
	menuController.closePartnerEquipment(false);
	gameManager.npcManager->removeNPCOnlyFromList(partner);
	gameManager.goodsManager.clearItem();
	resourceManager.setActiveResourcePackById(resourcePack.id);
	return ok;
}

bool initializeShopGoods(BuySellMenu& buySellMenu)
{
	buySellMenu.clearGoodsList();
	GoodsInfo& shopGoods = buySellMenu.goodsList[0];
	shopGoods.iniFile = ShopGoodsFile;
	shopGoods.number = 2;
	shopGoods.goods = std::make_shared<Goods>();
	shopGoods.goods->initFromIni(ShopGoodsFile);
	return shopGoods.goods->loadSucceeded;
}

bool testBuySellTransactions(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager,
	const std::filesystem::path& fixtureRoot)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	BuySellMenu& buySellMenu = *menuController.buySellMenu;
	const int storeIndex = gameManager.goodsManager.storeBegin();
	const std::string productionRoot =
		resourceManager.getActiveResourceRoot();

	gameManager.goodsManager.clearItem();
	File::setActiveResourceRoot(fixtureRoot.generic_string());
	File::setResourceFallbackRoots({ productionRoot });
	if (!checkPack(
		initializeShopGoods(buySellMenu),
		resourcePack,
		"loads the isolated shop goods fixture"))
	{
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}

	buySellMenu.bsKind = bsBuy;
	buySellMenu.numberValid = true;
	buySellMenu.canSellSelfGoods = true;
	buySellMenu.buyPercent = 150;
	buySellMenu.recyclePercent = 50;
	buySellMenu.visible = true;
	menuController.goodsMenu->visible = true;
	buySellMenu.setRunning(true);
	gameManager.player->money = 1000;
	buySellMenu.updateGoods();

	const int expectedBuyPrice = ShopGoodsCost * buySellMenu.buyPercent / 100;
	const int expectedSellPrice =
		ShopGoodsSellPrice * buySellMenu.recyclePercent / 100;
	ok = checkPack(
		buySellMenu.handleUIAction(UIAction::Confirm)
			&& gameManager.player->money == 1000 - expectedBuyPrice
			&& gameManager.goodsManager.goodsListExists(storeIndex)
			&& gameManager.goodsManager.goodsList[storeIndex].iniFile
				== ShopGoodsFile
			&& gameManager.goodsManager.goodsList[storeIndex].number == 1
			&& buySellMenu.goodsList[0].number == 1,
		resourcePack,
		"buys one item through the production shop Confirm action") && ok;

	ok = checkPack(
		buySellMenu.handleUIAction(UIAction::PanelNext),
		resourcePack,
		"switches from the shop pane to the production player-bag pane") && ok;
	ok = checkPack(
		buySellMenu.handleUIAction(UIAction::Confirm)
			&& gameManager.player->money
				== 1000 - expectedBuyPrice + expectedSellPrice
			&& !gameManager.goodsManager.goodsListExists(storeIndex)
			&& buySellMenu.goodsList[0].number == 2,
		resourcePack,
		"sells one item through the production player-bag Confirm action") && ok;

	ok = checkPack(
		buySellMenu.handleUIAction(UIAction::PanelPrevious)
			&& buySellMenu.handleUIAction(UIAction::Confirm)
			&& gameManager.goodsManager.goodsListExists(storeIndex)
			&& buySellMenu.goodsList[0].number == 1,
		resourcePack,
		"buys a second item before testing the sell prohibition") && ok;
	ok = checkPack(
		buySellMenu.handleUIAction(UIAction::PanelNext),
		resourcePack,
		"returns to the player-bag pane for the sell prohibition") && ok;
	buySellMenu.canSellSelfGoods = false;
	const int moneyBeforeRejectedSale = gameManager.player->money;
	const int bagCountBeforeRejectedSale =
		gameManager.goodsManager.goodsList[storeIndex].number;
	const int shopCountBeforeRejectedSale = buySellMenu.goodsList[0].number;
	ok = checkPack(
		buySellMenu.handleUIAction(UIAction::Confirm)
			&& gameManager.player->money == moneyBeforeRejectedSale
			&& gameManager.goodsManager.goodsList[storeIndex].number
				== bagCountBeforeRejectedSale
			&& buySellMenu.goodsList[0].number
				== shopCountBeforeRejectedSale,
		resourcePack,
		"consumes a forbidden sell without changing money or either inventory")
		&& ok;

	buySellMenu.handleUIAction(UIAction::Cancel);
	buySellMenu.visible = false;
	gameManager.goodsManager.clearItem();
	resourceManager.setActiveResourcePackById(resourcePack.id);
	return ok;
}

bool testBottomSameOwnerSpatialNavigation(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	if (menuController.topMenu != nullptr)
	{
		// Top-layout packs intentionally move their menu buttons out of Bottom.
		return true;
	}
	if (!checkPack(
		menuController.bottomMenu != nullptr
			&& menuController.bottomMenu->visible,
		resourcePack,
		"exposes the production Bottom HUD for same-owner navigation"))
	{
		return false;
	}

	const std::shared_ptr<BottomMenu>& bottomMenu =
		menuController.bottomMenu;
	const std::vector<PElement> candidates =
		bottomMenu->controllerFocusCandidates();
	const std::array<PElement, 7> menuButtons =
	{
		bottomMenu->stateBtn,
		bottomMenu->equipBtn,
		bottomMenu->xiulianBtn,
		bottomMenu->goodsBtn,
		bottomMenu->magicBtn,
		bottomMenu->notesBtn,
		bottomMenu->optionBtn
	};
	std::vector<PElement> quickSlots;
	for (const PElement& item : bottomMenu->goodsItem)
	{
		quickSlots.push_back(item);
	}
	for (const PElement& item : bottomMenu->magicItem)
	{
		quickSlots.push_back(item);
	}
	auto firstRegistered = [&candidates](
		const auto& elements) -> PElement
	{
		for (const PElement& element : elements)
		{
			if (element != nullptr
				&& std::find(
					candidates.begin(), candidates.end(), element)
					!= candidates.end())
			{
				return element;
			}
		}
		return nullptr;
	};
	const PElement menuButton = firstRegistered(menuButtons);
	const PElement quickSlot = firstRegistered(quickSlots);
	if (!checkPack(
		menuButton != nullptr && quickSlot != nullptr,
		resourcePack,
		"registers both a Bottom menu button and quick slot as real candidates"))
	{
		return false;
	}

	struct CandidateState
	{
		PElement element;
		bool visible = false;
		bool activated = false;
		Rect rect;
	};
	std::vector<CandidateState> savedStates;
	for (const PElement& candidate : candidates)
	{
		if (candidate == nullptr)
		{
			continue;
		}
		savedStates.push_back(
			{ candidate, candidate->visible, candidate->activated,
				candidate->rect });
		candidate->visible =
			candidate == menuButton || candidate == quickSlot;
		candidate->activated =
			candidate == menuButton || candidate == quickSlot;
	}
	menuButton->rect = { 300, 500, 36, 28 };
	quickSlot->rect = { 300, 560, 36, 36 };

	bool ok = checkPack(
		menuController.adoptControllerPointerFocus(
			bottomMenu, menuButton)
			&& bottomMenu->controllerFocusedElement() == menuButton,
		resourcePack,
		"establishes the real Bottom menu-button navigation anchor");
	ok = checkPack(
		menuController.handleUIAction(UIAction::NavigateDown)
			&& bottomMenu->isControllerFocusActive()
			&& bottomMenu->controllerFocusedElement() == quickSlot,
		resourcePack,
		"moves from a Bottom menu button to a quick slot in the same owner")
		&& ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::NavigateUp)
			&& bottomMenu->isControllerFocusActive()
			&& bottomMenu->controllerFocusedElement() == menuButton,
		resourcePack,
		"moves from a Bottom quick slot back to a menu button in the same owner")
		&& ok;

	for (const CandidateState& state : savedStates)
	{
		state.element->visible = state.visible;
		state.element->activated = state.activated;
		state.element->rect = state.rect;
	}
	bottomMenu->deactivateControllerFocus();
	return ok;
}

bool testYycsBottomColumnSeam(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	if (resourcePack.gameType != GAME_YYCS)
	{
		return true;
	}
	const auto& bottomMenu = gameManager.menu->bottomMenu;
	const auto& columnMenu = gameManager.menu->columnMenu;
	return checkPack(
		bottomMenu != nullptr
			&& columnMenu != nullptr
			&& columnMenu->rect.x + columnMenu->rect.w
				== bottomMenu->rect.x,
		resourcePack,
		"places the YYCS Bottom panel flush against the Column panel");
}

Point elementCenter(const PElement& element)
{
	return {
		element->rect.x + element->rect.w / 2,
		element->rect.y + element->rect.h / 2
	};
}

bool candidateCenterLiesInDirection(
	const Rect& source,
	const Rect& candidate,
	UIFocusDirection direction)
{
	const long long sourceCenterX =
		static_cast<long long>(source.x) + source.w / 2;
	const long long sourceCenterY =
		static_cast<long long>(source.y) + source.h / 2;
	const long long candidateCenterX =
		static_cast<long long>(candidate.x) + candidate.w / 2;
	const long long candidateCenterY =
		static_cast<long long>(candidate.y) + candidate.h / 2;
	switch (direction)
	{
	case UIFocusDirection::Up:
		return candidateCenterY < sourceCenterY;
	case UIFocusDirection::Down:
		return candidateCenterY > sourceCenterY;
	case UIFocusDirection::Left:
		return candidateCenterX < sourceCenterX;
	case UIFocusDirection::Right:
		return candidateCenterX > sourceCenterX;
	}
	return false;
}

bool sameRect(const Rect& left, const Rect& right)
{
	return left.x == right.x && left.y == right.y
		&& left.w == right.w && left.h == right.h;
}

bool hasFullyCoveredCandidate(const std::vector<PElement>& candidates)
{
	for (std::size_t candidateIndex = 0;
		candidateIndex < candidates.size();
		candidateIndex++)
	{
		const PElement& candidate = candidates[candidateIndex];
		if (candidate == nullptr || candidate->rect.w <= 0
			|| candidate->rect.h <= 0)
		{
			continue;
		}
		for (std::size_t coveringIndex = 0;
			coveringIndex < candidates.size();
			coveringIndex++)
		{
			const PElement& covering = candidates[coveringIndex];
			if (coveringIndex == candidateIndex || covering == nullptr
				|| covering->rect.w <= 0 || covering->rect.h <= 0)
			{
				continue;
			}
			if (covering->rect.x <= candidate->rect.x
				&& covering->rect.y <= candidate->rect.y
				&& covering->rect.x + covering->rect.w
					>= candidate->rect.x + candidate->rect.w
				&& covering->rect.y + covering->rect.h
					>= candidate->rect.y + candidate->rect.h)
			{
				return true;
			}
		}
	}
	return false;
}

struct PhysicalNavigationOwner
{
	std::string name;
	PElement element;
	ControllerFocusParticipant* participant = nullptr;
	std::vector<PElement> candidates;
};

struct PhysicalNavigationCandidate
{
	std::size_t ownerIndex = 0;
	PElement element;
};

std::vector<PhysicalNavigationOwner> collectPhysicalNavigationOwners(
	MenuController& menuController)
{
	std::vector<PhysicalNavigationOwner> owners;
	std::set<const Element*> registeredOwners;
	const std::vector<PElement> productionCandidates =
		GamepadRPGMenuActionsTestAccess::
			collectVisibleControllerFocusCandidates(menuController);
	std::set<const Element*> productionCandidateSet;
	for (const PElement& candidate : productionCandidates)
	{
		if (candidate != nullptr)
		{
			productionCandidateSet.insert(candidate.get());
		}
	}
	auto addOwner =
		[&owners, &registeredOwners, &productionCandidateSet](
			const std::string& name,
			const PElement& element,
			ControllerFocusParticipant* participant)
	{
		if (element == nullptr || participant == nullptr
			|| !element->visible || !element->activated
			|| !registeredOwners.insert(element.get()).second)
		{
			return;
		}
		std::vector<PElement> candidates;
		for (const PElement& candidate :
			participant->controllerFocusCandidates())
		{
			if (isUIFocusElementAvailable(candidate)
				&& productionCandidateSet.find(candidate.get())
					!= productionCandidateSet.end()
				&& std::find(
					candidates.begin(), candidates.end(), candidate)
					== candidates.end())
			{
				candidates.push_back(candidate);
			}
		}
		if (!candidates.empty())
		{
			owners.push_back(
				{ name, element, participant, std::move(candidates) });
		}
	};

	addOwner(
		"Equip",
		menuController.equipMenu,
		menuController.equipMenu.get());
	addOwner(
		"Practice",
		menuController.practiceMenu,
		menuController.practiceMenu.get());
	addOwner(
		"PartnerHead",
		menuController.partnerHeadMenu,
		menuController.partnerHeadMenu.get());
	addOwner(
		"PartnerEquip",
		menuController.partnerEquipMenu,
		menuController.partnerEquipMenu.get());
	addOwner(
		"Goods",
		menuController.goodsMenu,
		menuController.goodsMenu.get());
	addOwner(
		"Magic",
		menuController.magicMenu,
		menuController.magicMenu.get());
	addOwner(
		"Memo",
		menuController.memoMenu,
		menuController.memoMenu.get());
	addOwner(
		"Bottom",
		menuController.bottomMenu,
		menuController.bottomMenu.get());
	addOwner(
		"Top",
		menuController.topMenu,
		menuController.topMenu.get());
	addOwner(
		"Map",
		menuController.mapThumbnailMenu,
		menuController.mapThumbnailMenu.get());
	return owners;
}

int findPhysicalNavigationCandidate(
	const std::vector<PhysicalNavigationCandidate>& candidates,
	const PElement& element)
{
	for (std::size_t index = 0; index < candidates.size(); index++)
	{
		if (candidates[index].element == element)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

int findActivePhysicalNavigationCandidate(
	const std::vector<PhysicalNavigationOwner>& owners,
	const std::vector<PhysicalNavigationCandidate>& candidates,
	int& activeOwnerIndex)
{
	activeOwnerIndex = -1;
	PElement focusedElement;
	for (std::size_t ownerIndex = 0; ownerIndex < owners.size(); ownerIndex++)
	{
		if (!owners[ownerIndex].participant->isControllerFocusActive())
		{
			continue;
		}
		if (activeOwnerIndex >= 0)
		{
			activeOwnerIndex = -2;
			return -1;
		}
		activeOwnerIndex = static_cast<int>(ownerIndex);
		focusedElement =
			owners[ownerIndex].participant->controllerFocusedElement();
	}
	return activeOwnerIndex >= 0
		? findPhysicalNavigationCandidate(candidates, focusedElement)
		: -1;
}

SDL_GamepadButton physicalButtonForDirection(UIFocusDirection direction)
{
	switch (direction)
	{
	case UIFocusDirection::Up:
		return SDL_GAMEPAD_BUTTON_DPAD_UP;
	case UIFocusDirection::Down:
		return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	case UIFocusDirection::Left:
		return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	case UIFocusDirection::Right:
		return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
	}
	return SDL_GAMEPAD_BUTTON_INVALID;
}

GameInput::InputAction physicalActionForDirection(
	UIFocusDirection direction)
{
	switch (direction)
	{
	case UIFocusDirection::Up:
		return GameInput::InputAction::NavigateUp;
	case UIFocusDirection::Down:
		return GameInput::InputAction::NavigateDown;
	case UIFocusDirection::Left:
		return GameInput::InputAction::NavigateLeft;
	case UIFocusDirection::Right:
		return GameInput::InputAction::NavigateRight;
	}
	return GameInput::InputAction::Count;
}

bool verifyPhysicalVisibleMenuNavigationGraph(
	const ResourcePackExpectation& resourcePack,
	const std::string& scenario,
	GameManager& gameManager,
	VirtualGamepadTest::VirtualGamepad& gamepad,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	MenuController& menuController = *gameManager.menu;
	const std::vector<PhysicalNavigationOwner> owners =
		collectPhysicalNavigationOwners(menuController);
	if (!checkPack(
		!owners.empty(),
		resourcePack,
		scenario + " exposes at least one physical navigation owner"))
	{
		return false;
	}

	std::vector<PhysicalNavigationCandidate> candidates;
	std::set<const Element*> registeredCandidates;
	for (std::size_t ownerIndex = 0; ownerIndex < owners.size(); ownerIndex++)
	{
		for (const PElement& candidate : owners[ownerIndex].candidates)
		{
			if (!registeredCandidates.insert(candidate.get()).second)
			{
				return checkPack(
					false,
					resourcePack,
					scenario
						+ " registers a candidate under more than one owner");
			}
			candidates.push_back({ ownerIndex, candidate });
		}
	}
	if (!checkPack(
		!candidates.empty()
			&& !hasFullyCoveredCandidate([&candidates]()
			{
				std::vector<PElement> elements;
				for (const PhysicalNavigationCandidate& candidate :
					candidates)
				{
					elements.push_back(candidate.element);
				}
				return elements;
			}()),
		resourcePack,
		scenario
			+ " exposes unique production candidates without one candidate"
			" fully covering another"))
	{
		return false;
	}

	std::vector<std::set<std::size_t>> edges(candidates.size());
	const std::array<UIFocusDirection, 4> directions =
	{
		UIFocusDirection::Up,
		UIFocusDirection::Down,
		UIFocusDirection::Left,
		UIFocusDirection::Right
	};
	constexpr int NavigationProbeStepLimit = 96;
	bool graphValid = true;
	std::string graphFailure;
	for (std::size_t sourceIndex = 0;
		sourceIndex < candidates.size() && graphValid;
		sourceIndex++)
	{
		for (UIFocusDirection direction : directions)
		{
			const PhysicalNavigationCandidate& source =
				candidates[sourceIndex];
			const PhysicalNavigationOwner& sourceOwner =
				owners[source.ownerIndex];
			if (menuController.mapThumbnailMenu != nullptr
				&& menuController.mapThumbnailMenu->visible)
			{
				// Reset the cursor before every edge probe so a previous map
				// boundary walk cannot influence this source/direction pair.
				menuController.setMapThumbnailVisible(false);
				menuController.setMapThumbnailVisible(true);
			}
			if (!menuController.adoptControllerPointerFocus(
				sourceOwner.element, source.element))
			{
				graphValid = false;
				graphFailure = "cannot seed " + sourceOwner.name
					+ " candidate " + std::to_string(sourceIndex);
				break;
			}

			int previousCandidateIndex = static_cast<int>(sourceIndex);
			for (int step = 0; step < NavigationProbeStepLimit; step++)
			{
				bool actionPressed = false;
				HeadlessPhysicalInputTest::FrameCallbacks callbacks;
				callbacks.afterInputUpdate =
					[&actionPressed, direction](
						const GameInput::PhysicalInputManager& inputManager)
				{
					actionPressed = inputManager.wasActionPressed(
						physicalActionForDirection(direction));
				};
				gameManager.player->cancelQueuedInteraction(false);
				gameManager.player->nextAction = nullptr;
				const bool handled = frameDriver.tapButton(
					gamepad,
					physicalButtonForDirection(direction),
					callbacks);
				int activeOwnerIndex = -1;
				const int currentCandidateIndex =
					findActivePhysicalNavigationCandidate(
						owners, candidates, activeOwnerIndex);
				if (!actionPressed || activeOwnerIndex < 0
					|| currentCandidateIndex < 0
					|| gameManager.player->nextAction != nullptr)
				{
					graphValid = false;
					graphFailure = sourceOwner.name + " "
						+ std::to_string(sourceIndex) + " direction "
						+ std::to_string(static_cast<int>(direction))
						+ " lost physical focus or leaked into world";
					break;
				}
				edges[static_cast<std::size_t>(previousCandidateIndex)].insert(
					static_cast<std::size_t>(currentCandidateIndex));
				if (currentCandidateIndex != previousCandidateIndex
					&& !candidateCenterLiesInDirection(
						candidates[static_cast<std::size_t>(
							previousCandidateIndex)].element->rect,
						candidates[static_cast<std::size_t>(
							currentCandidateIndex)].element->rect,
						direction))
				{
					graphValid = false;
					graphFailure =
						"physical focus movement contradicts the displayed"
						" direction";
					break;
				}
				const bool crossedOwner =
					activeOwnerIndex
						!= static_cast<int>(source.ownerIndex);
				if (crossedOwner)
				{
					const Rect& sourceRect = candidates[
						static_cast<std::size_t>(
							previousCandidateIndex)].element->rect;
					const Rect& destinationRect = candidates[
						static_cast<std::size_t>(
							currentCandidateIndex)].element->rect;
					if (!scoreUIFocusSpatialCandidate(
						sourceRect,
						destinationRect,
						direction,
						static_cast<std::size_t>(
							currentCandidateIndex)))
					{
						graphValid = false;
						graphFailure =
							"owner boundary contradicts the displayed direction";
					}
					break;
				}
				if (!handled
					&& currentCandidateIndex == previousCandidateIndex)
				{
					break;
				}
				previousCandidateIndex = currentCandidateIndex;
			}
		}
	}

	std::string missingPaths;
	if (graphValid)
	{
		for (std::size_t sourceIndex = 0;
			sourceIndex < candidates.size();
			sourceIndex++)
		{
			std::set<std::size_t> reachable = { sourceIndex };
			std::queue<std::size_t> pending;
			pending.push(sourceIndex);
			while (!pending.empty())
			{
				const std::size_t current = pending.front();
				pending.pop();
				for (std::size_t destination : edges[current])
				{
					if (reachable.insert(destination).second)
					{
						pending.push(destination);
					}
				}
			}
			if (reachable.size() == candidates.size())
			{
				continue;
			}
			if (!missingPaths.empty())
			{
				missingPaths += ",";
			}
			missingPaths += std::to_string(sourceIndex) + ":"
				+ std::to_string(
					candidates.size() - reachable.size());
		}
	}
	const bool ok = checkPack(
		graphValid && missingPaths.empty(),
		resourcePack,
		scenario
			+ " physical D-pad graph reaches every production candidate"
			+ (graphValid ? "" : "; failure=" + graphFailure)
			+ (missingPaths.empty()
				? ""
				: "; missing-count-by-source=" + missingPaths));
	if (ok)
	{
		std::cout << "GAMEPAD_MENU_COVERAGE pack=" << resourcePack.id
			<< " scenario=" << scenario
			<< " owners=" << owners.size()
			<< " candidates=" << candidates.size()
			<< " probes=" << candidates.size() * directions.size()
			<< '\n';
	}
	return ok;
}

struct PhysicalModalNavigationGroup
{
	std::string name;
	std::vector<PElement> candidates;
};

struct PhysicalModalCandidateLocation
{
	int groupIndex = -1;
	int candidateIndex = -1;
	int flattenedIndex = -1;
};

struct PhysicalModalActionObservation
{
	bool semanticActionPressed = false;
	bool semanticInputBlocked = false;
	bool worldActionQueued = false;
	bool worldTargetChanged = false;
};

PhysicalModalCandidateLocation findPhysicalModalCandidate(
	const std::vector<PhysicalModalNavigationGroup>& groups,
	const PElement& element)
{
	int flattenedIndex = 0;
	for (std::size_t groupIndex = 0; groupIndex < groups.size();
		groupIndex++)
	{
		for (std::size_t candidateIndex = 0;
			candidateIndex < groups[groupIndex].candidates.size();
			candidateIndex++, flattenedIndex++)
		{
			if (groups[groupIndex].candidates[candidateIndex] == element)
			{
				return
				{
					static_cast<int>(groupIndex),
					static_cast<int>(candidateIndex),
					flattenedIndex,
				};
			}
		}
	}
	return {};
}

PElement findPresentedPhysicalModalCandidate(
	const std::vector<PhysicalModalNavigationGroup>& groups)
{
	PElement focusedElement;
	for (const PhysicalModalNavigationGroup& group : groups)
	{
		for (const PElement& candidate : group.candidates)
		{
			if (candidate == nullptr || !candidate->isFocused())
			{
				continue;
			}
			if (focusedElement != nullptr)
			{
				return nullptr;
			}
			focusedElement = candidate;
		}
	}
	return focusedElement;
}

struct PhysicalModalReferenceScore
{
	int directionConeTier = 1;
	long long edgeDistanceSquared = 0;
	long long crossAxisEdgeGap = 0;
	long long centerDistanceSquared = 0;
	std::size_t fixtureOrder = 0;
};

long long physicalModalReferenceIntervalGap(
	long long firstBegin,
	long long firstEnd,
	long long secondBegin,
	long long secondEnd)
{
	if (firstEnd < secondBegin)
	{
		return secondBegin - firstEnd;
	}
	if (secondEnd < firstBegin)
	{
		return firstBegin - secondEnd;
	}
	return 0;
}

std::optional<PhysicalModalReferenceScore>
	makePhysicalModalReferenceScore(
		const Rect& source,
		const Rect& candidate,
		UIFocusDirection direction,
		std::size_t fixtureOrder)
{
	const long long sourceCenterX =
		static_cast<long long>(source.x) + source.w / 2;
	const long long sourceCenterY =
		static_cast<long long>(source.y) + source.h / 2;
	const long long candidateCenterX =
		static_cast<long long>(candidate.x) + candidate.w / 2;
	const long long candidateCenterY =
		static_cast<long long>(candidate.y) + candidate.h / 2;
	const long long deltaX = candidateCenterX - sourceCenterX;
	const long long deltaY = candidateCenterY - sourceCenterY;
	const long long sourceLeft = source.x;
	const long long sourceTop = source.y;
	const long long sourceRight = sourceLeft + source.w;
	const long long sourceBottom = sourceTop + source.h;
	const long long candidateLeft = candidate.x;
	const long long candidateTop = candidate.y;
	const long long candidateRight = candidateLeft + candidate.w;
	const long long candidateBottom = candidateTop + candidate.h;

	long long forwardCenterDistance = 0;
	long long crossAxisCenterDistance = 0;
	long long forwardEdgeGap = 0;
	long long crossAxisEdgeGap = 0;
	switch (direction)
	{
	case UIFocusDirection::Up:
		forwardCenterDistance = -deltaY;
		crossAxisCenterDistance = std::abs(deltaX);
		forwardEdgeGap = std::max(
			0LL, sourceTop - candidateBottom);
		crossAxisEdgeGap = physicalModalReferenceIntervalGap(
			sourceLeft, sourceRight, candidateLeft, candidateRight);
		break;
	case UIFocusDirection::Down:
		forwardCenterDistance = deltaY;
		crossAxisCenterDistance = std::abs(deltaX);
		forwardEdgeGap = std::max(
			0LL, candidateTop - sourceBottom);
		crossAxisEdgeGap = physicalModalReferenceIntervalGap(
			sourceLeft, sourceRight, candidateLeft, candidateRight);
		break;
	case UIFocusDirection::Left:
		forwardCenterDistance = -deltaX;
		crossAxisCenterDistance = std::abs(deltaY);
		forwardEdgeGap = std::max(
			0LL, sourceLeft - candidateRight);
		crossAxisEdgeGap = physicalModalReferenceIntervalGap(
			sourceTop, sourceBottom, candidateTop, candidateBottom);
		break;
	case UIFocusDirection::Right:
		forwardCenterDistance = deltaX;
		crossAxisCenterDistance = std::abs(deltaY);
		forwardEdgeGap = std::max(
			0LL, candidateLeft - sourceRight);
		crossAxisEdgeGap = physicalModalReferenceIntervalGap(
			sourceTop, sourceBottom, candidateTop, candidateBottom);
		break;
	}
	if (forwardCenterDistance <= 0)
	{
		return std::nullopt;
	}

	PhysicalModalReferenceScore score;
	score.directionConeTier =
		crossAxisCenterDistance <= forwardCenterDistance ? 0 : 1;
	score.edgeDistanceSquared =
		forwardEdgeGap * forwardEdgeGap
		+ crossAxisEdgeGap * crossAxisEdgeGap;
	score.crossAxisEdgeGap = crossAxisEdgeGap;
	score.centerDistanceSquared =
		deltaX * deltaX + deltaY * deltaY;
	score.fixtureOrder = fixtureOrder;
	return score;
}

bool physicalModalReferenceScoreIsBetter(
	const PhysicalModalReferenceScore& candidate,
	const PhysicalModalReferenceScore& current)
{
	if (candidate.directionConeTier != current.directionConeTier)
	{
		return candidate.directionConeTier < current.directionConeTier;
	}
	if (candidate.edgeDistanceSquared != current.edgeDistanceSquared)
	{
		return candidate.edgeDistanceSquared < current.edgeDistanceSquared;
	}
	if (candidate.crossAxisEdgeGap != current.crossAxisEdgeGap)
	{
		return candidate.crossAxisEdgeGap < current.crossAxisEdgeGap;
	}
	if (candidate.centerDistanceSquared != current.centerDistanceSquared)
	{
		return candidate.centerDistanceSquared
			< current.centerDistanceSquared;
	}
	return candidate.fixtureOrder < current.fixtureOrder;
}

int physicalModalDirectionalDestination(
	const PhysicalModalNavigationGroup& group,
	int sourceIndex,
	UIFocusDirection direction)
{
	if (sourceIndex < 0
		|| sourceIndex >= static_cast<int>(group.candidates.size()))
	{
		return -1;
	}
	const PElement& source = group.candidates[
		static_cast<std::size_t>(sourceIndex)];
	if (source == nullptr)
	{
		return -1;
	}
	std::optional<PhysicalModalReferenceScore> bestScore;
	int destinationIndex = sourceIndex;
	for (std::size_t candidateIndex = 0;
		candidateIndex < group.candidates.size(); candidateIndex++)
	{
		const PElement& candidate = group.candidates[candidateIndex];
		if (static_cast<int>(candidateIndex) == sourceIndex
			|| !isUIFocusElementAvailable(candidate))
		{
			continue;
		}
		const std::optional<PhysicalModalReferenceScore> score =
			makePhysicalModalReferenceScore(
				source->rect, candidate->rect, direction, candidateIndex);
		if (score
			&& (!bestScore
				|| physicalModalReferenceScoreIsBetter(
					*score, *bestScore)))
		{
			bestScore = score;
			destinationIndex = static_cast<int>(candidateIndex);
		}
	}
	return destinationIndex;
}

std::vector<UIFocusDirection> findPhysicalModalDirectionPath(
	const PhysicalModalNavigationGroup& group,
	int sourceIndex,
	int destinationIndex)
{
	if (sourceIndex < 0 || destinationIndex < 0
		|| sourceIndex >= static_cast<int>(group.candidates.size())
		|| destinationIndex >= static_cast<int>(group.candidates.size()))
	{
		return {};
	}
	if (sourceIndex == destinationIndex)
	{
		return {};
	}
	const std::array<UIFocusDirection, 4> directions =
	{
		UIFocusDirection::Up,
		UIFocusDirection::Down,
		UIFocusDirection::Left,
		UIFocusDirection::Right,
	};
	std::vector<int> previous(group.candidates.size(), -1);
	std::vector<UIFocusDirection> previousDirection(
		group.candidates.size(), UIFocusDirection::Up);
	std::queue<int> pending;
	previous[static_cast<std::size_t>(sourceIndex)] = sourceIndex;
	pending.push(sourceIndex);
	while (!pending.empty()
		&& previous[static_cast<std::size_t>(destinationIndex)] < 0)
	{
		const int currentIndex = pending.front();
		pending.pop();
		for (UIFocusDirection direction : directions)
		{
			const int nextIndex = physicalModalDirectionalDestination(
				group, currentIndex, direction);
			if (nextIndex < 0 || nextIndex == currentIndex
				|| previous[static_cast<std::size_t>(nextIndex)] >= 0)
			{
				continue;
			}
			previous[static_cast<std::size_t>(nextIndex)] = currentIndex;
			previousDirection[static_cast<std::size_t>(nextIndex)] =
				direction;
			pending.push(nextIndex);
		}
	}
	if (previous[static_cast<std::size_t>(destinationIndex)] < 0)
	{
		return {};
	}
	std::vector<UIFocusDirection> reversePath;
	for (int currentIndex = destinationIndex;
		currentIndex != sourceIndex;
		currentIndex = previous[static_cast<std::size_t>(currentIndex)])
	{
		reversePath.push_back(
			previousDirection[static_cast<std::size_t>(currentIndex)]);
	}
	return std::vector<UIFocusDirection>(
		reversePath.rbegin(), reversePath.rend());
}

PhysicalModalActionObservation tapPhysicalModalAction(
	VirtualGamepadTest::VirtualGamepad& gamepad,
	SDL_GamepadButton button,
	GameInput::InputAction semanticAction,
	GameManager& gameManager,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver)
{
	PhysicalModalActionObservation observation;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	const int npcClickIndexBefore = gameManager.npcManager->clickIndex;
	const int objectClickIndexBefore = gameManager.objectManager->clickIndex;
	HeadlessPhysicalInputTest::FrameCallbacks callbacks;
	callbacks.afterInputUpdate =
		[&observation, semanticAction](
			const GameInput::PhysicalInputManager& inputManager)
	{
		observation.semanticActionPressed =
			observation.semanticActionPressed
			|| inputManager.wasActionPressed(semanticAction);
	};
	callbacks.afterDispatch =
		[&gameManager, &observation](bool semanticInputBlocked)
	{
		observation.semanticInputBlocked =
			observation.semanticInputBlocked || semanticInputBlocked;
		observation.worldActionQueued =
			observation.worldActionQueued
			|| gameManager.player->nextAction != nullptr;
	};
	frameDriver.tapButton(gamepad, button, callbacks, callbacks);
	observation.worldActionQueued =
		observation.worldActionQueued
		|| gameManager.player->nextAction != nullptr;
	observation.worldTargetChanged =
		gameManager.npcManager->clickIndex != npcClickIndexBefore
		|| gameManager.objectManager->clickIndex != objectClickIndexBefore;
	return observation;
}

bool verifyPhysicalModalCandidateNavigation(
	const ResourcePackExpectation& resourcePack,
	const std::string& scenario,
	GameManager& gameManager,
	const PElement& runningOwner,
	const std::vector<PhysicalModalNavigationGroup>& groups,
	int expectedCandidateCount,
	int defaultGroupIndex,
	const std::function<PElement()>& focusedElement,
	const std::function<bool()>& ownerInvariant)
{
	bool ok = videoSubsystemIsStopped(
		resourcePack.id + " " + scenario + " starts");
	if (runningOwner == nullptr || groups.empty()
		|| defaultGroupIndex < 0
		|| defaultGroupIndex >= static_cast<int>(groups.size())
		|| groups[static_cast<std::size_t>(defaultGroupIndex)]
			.candidates.empty())
	{
		return checkPack(
			false,
			resourcePack,
			scenario + " has a valid physical modal owner and default pane");
	}

	int candidateCount = 0;
	std::set<const Element*> registeredCandidates;
	bool candidateSetValid = true;
	for (const PhysicalModalNavigationGroup& group : groups)
	{
		if (group.candidates.empty())
		{
			candidateSetValid = false;
		}
		for (const PElement& candidate : group.candidates)
		{
			candidateCount++;
			candidateSetValid = candidateSetValid
				&& isUIFocusElementAvailable(candidate)
				&& registeredCandidates.insert(candidate.get()).second;
		}
	}
	if (!checkPack(
		candidateSetValid && candidateCount == expectedCandidateCount,
		resourcePack,
		scenario + " exposes one unique modal owner with exactly "
			+ std::to_string(expectedCandidateCount)
			+ " available candidates"))
	{
		return false;
	}

	const std::array<UIFocusDirection, 4> directions =
	{
		UIFocusDirection::Up,
		UIFocusDirection::Down,
		UIFocusDirection::Left,
		UIFocusDirection::Right,
	};
	std::vector<std::set<std::size_t>> edges(
		static_cast<std::size_t>(candidateCount));
	int directionProbeCount = 0;
	int paneTransitionCount = 0;
	bool traversalValid = true;
	std::string traversalFailure;
	{
		VirtualGamepadTest::SDLSession sdlSession;
		const std::string gamepadName =
			resourcePack.id + " " + scenario + " Pad";
		VirtualGamepadTest::VirtualGamepad gamepad(gamepadName.c_str());
		gamepad.setAxis(
			SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
		gamepad.setAxis(
			SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
		HeadlessPhysicalInputTest::ScopedRunningOwner runningScope(
			runningOwner);
		auto& inputManager =
			const_cast<GameInput::PhysicalInputManager&>(
				Engine::getInstance()->inputActions());
		HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
			inputManager);
		if (!inputScope.isInitialized())
		{
			traversalValid = false;
			traversalFailure = "cannot initialize physical input";
		}
		else
		{
			std::uint64_t nowMilliseconds = SDL_GetTicks();
			HeadlessPhysicalInputTest::FrameDriver frameDriver(
				inputManager,
				nowMilliseconds,
				[]()
				{
					return dispatchPhysicalUIActions(
						Engine::getInstance());
				},
				[&gameManager]()
				{
					gameManager.controller->processPhysicalInputFrame();
				});

			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_UP, false);
			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			if (inputManager.activeGamepadID() != gamepad.id())
			{
				traversalValid = false;
				traversalFailure = "cannot claim the virtual gamepad";
			}
			inputManager.releaseForContextTransition();
			frameDriver.runFrame();
			frameDriver.runFrame();

			auto performAction =
				[&](SDL_GamepadButton button,
					GameInput::InputAction semanticAction,
					const std::string& actionName,
					PElement& destination) -> bool
			{
				const PElement source = focusedElement();
				const PhysicalModalCandidateLocation sourceLocation =
					findPhysicalModalCandidate(groups, source);
				const PhysicalModalActionObservation observation =
					tapPhysicalModalAction(
						gamepad,
						button,
						semanticAction,
						gameManager,
						frameDriver);
				destination = focusedElement();
				const PhysicalModalCandidateLocation destinationLocation =
					findPhysicalModalCandidate(groups, destination);
				if (!observation.semanticActionPressed
					|| observation.worldActionQueued
					|| observation.worldTargetChanged
					|| destinationLocation.flattenedIndex < 0
					|| !ownerInvariant()
					|| gameManager.menu == nullptr
					|| !gameManager.menu->blocksWorldInput())
				{
					traversalFailure = actionName
						+ " lost the modal owner/candidate or leaked into world"
						+ "; source="
						+ std::to_string(sourceLocation.flattenedIndex)
						+ "; destination="
						+ std::to_string(
							destinationLocation.flattenedIndex)
						+ "; semantic-blocked="
						+ std::to_string(
							observation.semanticInputBlocked ? 1 : 0);
					return false;
				}
				if (sourceLocation.flattenedIndex >= 0)
				{
					edges[static_cast<std::size_t>(
						sourceLocation.flattenedIndex)].insert(
							static_cast<std::size_t>(
								destinationLocation.flattenedIndex));
				}
				return true;
			};

			PElement initialFocus;
			if (traversalValid)
			{
				const PhysicalModalActionObservation initialObservation =
					tapPhysicalModalAction(
						gamepad,
						SDL_GAMEPAD_BUTTON_DPAD_UP,
						GameInput::InputAction::NavigateUp,
						gameManager,
						frameDriver);
				initialFocus = focusedElement();
				const PElement expectedDefault =
					groups[static_cast<std::size_t>(defaultGroupIndex)]
						.candidates.front();
				if (!initialObservation.semanticActionPressed
					|| initialObservation.worldActionQueued
					|| initialObservation.worldTargetChanged
					|| initialFocus != expectedDefault
					|| !ownerInvariant()
					|| !gameManager.menu->blocksWorldInput())
				{
					traversalValid = false;
					traversalFailure =
						"fresh physical D-pad does not retain the real"
						" modal default without world leakage";
				}
			}

			auto navigateToCandidate =
				[&](int targetGroupIndex,
					int targetCandidateIndex) -> bool
			{
				PElement currentFocus = focusedElement();
				PhysicalModalCandidateLocation currentLocation =
					findPhysicalModalCandidate(groups, currentFocus);
				for (std::size_t paneStep = 0;
					currentLocation.groupIndex != targetGroupIndex
						&& paneStep < groups.size();
					paneStep++)
				{
					PElement paneDestination;
					if (!performAction(
						SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
						GameInput::InputAction::NextPanel,
						"physical NextPanel",
						paneDestination))
					{
						return false;
					}
					paneTransitionCount++;
					currentLocation =
						findPhysicalModalCandidate(
							groups, paneDestination);
				}
				if (currentLocation.groupIndex != targetGroupIndex)
				{
					traversalFailure =
						"physical NextPanel cannot reach target modal pane";
					return false;
				}

				const PhysicalModalNavigationGroup& targetGroup =
					groups[static_cast<std::size_t>(targetGroupIndex)];
				const std::vector<UIFocusDirection> path =
					findPhysicalModalDirectionPath(
						targetGroup,
						currentLocation.candidateIndex,
						targetCandidateIndex);
				if (currentLocation.candidateIndex != targetCandidateIndex
					&& path.empty())
				{
					traversalFailure =
						"no displayed D-pad path exists inside modal pane "
						+ targetGroup.name;
					return false;
				}
				int expectedIndex = currentLocation.candidateIndex;
				for (UIFocusDirection direction : path)
				{
					const int pathSourceIndex = expectedIndex;
					expectedIndex =
						physicalModalDirectionalDestination(
							targetGroup, expectedIndex, direction);
					bool reachedExpectedIndex = false;
					for (int scrollRetry = 0; scrollRetry < 96;
						scrollRetry++)
					{
						PElement pathDestination;
						if (!performAction(
							physicalButtonForDirection(direction),
							physicalActionForDirection(direction),
							"physical D-pad path",
							pathDestination))
						{
							return false;
						}
						const PhysicalModalCandidateLocation pathLocation =
							findPhysicalModalCandidate(
								groups, pathDestination);
						if (pathLocation.groupIndex != targetGroupIndex)
						{
							traversalFailure =
								"physical D-pad path leaves the modal pane";
							return false;
						}
						if (pathLocation.candidateIndex == expectedIndex)
						{
							reachedExpectedIndex = true;
							break;
						}
						// SlotGridController gives a top/bottom-row scroll
						// operation priority over spatial movement. The focus
						// therefore remains on the source until repeated fresh
						// physical edges reach the scrollbar boundary.
						if (pathLocation.candidateIndex
							!= pathSourceIndex)
						{
							traversalFailure =
								"physical D-pad path reaches an unexpected"
								" modal candidate; group="
								+ targetGroup.name + "; source="
								+ std::to_string(pathSourceIndex)
								+ "; expected="
								+ std::to_string(expectedIndex)
								+ "; actual="
								+ std::to_string(
									pathLocation.candidateIndex);
							return false;
						}
					}
					if (!reachedExpectedIndex)
					{
						traversalFailure =
							"physical D-pad path cannot pass the modal"
							" scrollbar boundary";
						return false;
					}
				}
				const PhysicalModalCandidateLocation finalLocation =
					findPhysicalModalCandidate(groups, focusedElement());
				return finalLocation.groupIndex == targetGroupIndex
					&& finalLocation.candidateIndex
						== targetCandidateIndex;
			};

			for (std::size_t groupIndex = 0;
				groupIndex < groups.size() && traversalValid;
				groupIndex++)
			{
				for (std::size_t candidateIndex = 0;
					candidateIndex < groups[groupIndex].candidates.size()
						&& traversalValid;
					candidateIndex++)
				{
					for (UIFocusDirection direction : directions)
					{
						if (!navigateToCandidate(
							static_cast<int>(groupIndex),
							static_cast<int>(candidateIndex)))
						{
							traversalValid = false;
							break;
						}
						const int expectedDestinationIndex =
							physicalModalDirectionalDestination(
								groups[groupIndex],
								static_cast<int>(candidateIndex),
								direction);
						PhysicalModalCandidateLocation probeLocation;
						bool reachedReferenceDestination = false;
						for (int scrollRetry = 0;
							scrollRetry < 96;
							scrollRetry++)
						{
							PElement probeDestination;
							if (!performAction(
								physicalButtonForDirection(direction),
								physicalActionForDirection(direction),
								"physical four-direction probe",
								probeDestination))
							{
								traversalValid = false;
								break;
							}
							probeLocation = findPhysicalModalCandidate(
								groups, probeDestination);
							if (probeLocation.groupIndex
								!= static_cast<int>(groupIndex))
							{
								traversalValid = false;
								traversalFailure =
									"physical four-direction probe leaves"
									" the modal pane";
								break;
							}
							if (probeLocation.candidateIndex
								== expectedDestinationIndex)
							{
								reachedReferenceDestination = true;
								break;
							}
							if (probeLocation.candidateIndex
								!= static_cast<int>(candidateIndex))
							{
								traversalValid = false;
								traversalFailure =
									"physical four-direction probe skips"
									" the independent reference neighbour;"
									" group=" + groups[groupIndex].name
									+ "; source="
									+ std::to_string(candidateIndex)
									+ "; expected="
									+ std::to_string(
										expectedDestinationIndex)
									+ "; actual="
									+ std::to_string(
										probeLocation.candidateIndex);
								break;
							}
							// Scrollable logical edges retain the source until
							// fresh physical edges reach the boundary. Keep
							// probing: a permanent no-op is invalid when the
							// independent rectangle oracle has a neighbour.
						}
						if (!traversalValid)
						{
							break;
						}
						if (!reachedReferenceDestination)
						{
							traversalValid = false;
							traversalFailure =
								"physical four-direction probe cannot reach"
								" the independent reference neighbour";
							break;
						}
						if (probeLocation.candidateIndex
								!= static_cast<int>(candidateIndex)
							&& !candidateCenterLiesInDirection(
								groups[groupIndex].candidates[
									candidateIndex]->rect,
								groups[groupIndex].candidates[
									static_cast<std::size_t>(
										probeLocation.candidateIndex)]->rect,
								direction))
						{
							traversalValid = false;
							traversalFailure =
								"physical modal movement contradicts the"
								" displayed direction";
							break;
						}
						directionProbeCount++;
					}
				}
			}

			if (traversalValid
				&& !navigateToCandidate(defaultGroupIndex, 0))
			{
				traversalValid = false;
			}
		}
	}

	ok = videoSubsystemIsStopped(
		resourcePack.id + " " + scenario + " finishes") && ok;
	std::string missingPaths;
	if (traversalValid)
	{
		for (std::size_t sourceIndex = 0;
			sourceIndex < edges.size(); sourceIndex++)
		{
			std::set<std::size_t> reachable = { sourceIndex };
			std::queue<std::size_t> pending;
			pending.push(sourceIndex);
			while (!pending.empty())
			{
				const std::size_t currentIndex = pending.front();
				pending.pop();
				for (std::size_t destinationIndex : edges[currentIndex])
				{
					if (reachable.insert(destinationIndex).second)
					{
						pending.push(destinationIndex);
					}
				}
			}
			if (reachable.size() != edges.size())
			{
				if (!missingPaths.empty())
				{
					missingPaths += ",";
				}
				missingPaths += std::to_string(sourceIndex)
					+ ":" + std::to_string(
						edges.size() - reachable.size());
			}
		}
	}
	const bool graphOk = checkPack(
		ok && traversalValid && missingPaths.empty()
			&& directionProbeCount == expectedCandidateCount * 4
			&& paneTransitionCount >= 2,
		resourcePack,
		scenario
			+ " physically probes all four directions from every candidate"
			" and keeps the single-owner modal graph fully reachable"
			+ (traversalFailure.empty()
				? "" : "; failure=" + traversalFailure)
			+ (missingPaths.empty()
				? "" : "; missing-count-by-source=" + missingPaths));
	if (graphOk)
	{
		std::cout << "GAMEPAD_MODAL_COVERAGE pack=" << resourcePack.id
			<< " scenario=" << scenario
			<< " owners=1 candidates=" << candidateCount
			<< " probes=" << directionProbeCount
			<< " pane-transitions=" << paneTransitionCount
			<< '\n';
	}
	return graphOk;
}

std::vector<PElement> makePhysicalModalCandidates(
	const std::vector<std::shared_ptr<Item>>& items)
{
	std::vector<PElement> candidates;
	candidates.reserve(items.size());
	for (const std::shared_ptr<Item>& item : items)
	{
		candidates.push_back(item);
	}
	return candidates;
}

bool testPhysicalBuySellModalCandidateNavigation(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager,
	const std::filesystem::path& fixtureRoot)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	BuySellMenu& buySellMenu = *menuController.buySellMenu;
	const std::string productionRoot =
		resourceManager.getActiveResourceRoot();
	const int storeIndex = gameManager.goodsManager.storeBegin();

	menuController.clearMenu();
	gameManager.goodsManager.clearItem();
	File::setActiveResourceRoot(fixtureRoot.generic_string());
	File::setResourceFallbackRoots({ productionRoot });
	const bool fixtureLoaded =
		initializeShopGoods(buySellMenu)
		&& loadGoodsInfo(
			gameManager.goodsManager.goodsList[storeIndex],
			PartnerHeadGoodsAFile);
	if (!checkPack(
		fixtureLoaded,
		resourcePack,
		"loads the physical buy-sell modal shop and player-bag fixture"))
	{
		gameManager.goodsManager.clearItem();
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}

	buySellMenu.bsKind = bsBuy;
	buySellMenu.numberValid = true;
	buySellMenu.canSellSelfGoods = true;
	buySellMenu.buyPercent = 100;
	buySellMenu.recyclePercent = 100;
	menuController.goodsMenu->visible = true;
	menuController.goodsMenu->updateGoods();
	buySellMenu.addChild(menuController.goodsMenu);
	buySellMenu.visible = true;
	buySellMenu.setRunning(true);
	buySellMenu.updateGoods();
	buySellMenu.clearControllerFocus();
	menuController.goodsMenu->deactivateControllerFocus();

	const std::vector<PhysicalModalNavigationGroup> groups =
	{
		{ "shop", makePhysicalModalCandidates(buySellMenu.item) },
		{ "player-bag",
			makePhysicalModalCandidates(menuController.goodsMenu->item) },
	};
	auto focusedElement = [&groups]()
	{
		return findPresentedPhysicalModalCandidate(groups);
	};
	auto ownerInvariant =
		[&buySellMenu, &menuController]()
	{
		return Element::isCurrentRunOwner(&buySellMenu)
			&& buySellMenu.visible && buySellMenu.activated
			&& menuController.goodsMenu != nullptr
			&& menuController.goodsMenu->visible
			&& !menuController.goodsMenu->isControllerFocusActive();
	};
	ok = verifyPhysicalModalCandidateNavigation(
		resourcePack,
		"buy-sell-shop-player-bag",
		gameManager,
		menuController.buySellMenu,
		groups,
		18,
		0,
		focusedElement,
		ownerInvariant) && ok;

	buySellMenu.clearControllerFocus();
	buySellMenu.visible = false;
	buySellMenu.setRunning(false);
	if (menuController.upMenu != nullptr)
	{
		menuController.upMenu->addChild(menuController.goodsMenu);
	}
	menuController.goodsMenu->visible = false;
	gameManager.goodsManager.clearItem();
	ok = checkPack(
		resourceManager.setActiveResourcePackById(resourcePack.id),
		resourcePack,
		"restores the production resource root after physical buy-sell"
		" modal navigation") && ok;
	return ok;
}

bool testPhysicalPartnerEquipmentModalCandidateNavigation(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager,
	const std::filesystem::path& fixtureRoot)
{
	bool ok = true;
	MenuController& menuController = *gameManager.menu;
	const std::string productionRoot =
		resourceManager.getActiveResourceRoot();
	const int storeIndex = gameManager.goodsManager.storeBegin();

	menuController.clearMenu();
	gameManager.goodsManager.clearItem();
	File::setActiveResourceRoot(fixtureRoot.generic_string());
	File::setResourceFallbackRoots({ productionRoot });
	if (!checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[storeIndex],
			PartnerHeadGoodsAFile),
		resourcePack,
		"loads the physical partner-equipment borrowed-bag fixture"))
	{
		gameManager.goodsManager.clearItem();
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}
	menuController.goodsMenu->updateGoods();

	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->npcName = resourcePack.partnerName.empty()
		? "Controller Partner"
		: resourcePack.partnerName;
	partner->canEquip = 1;
	partner->level = 10;
	gameManager.npcManager->addNPC(partner);
	if (!checkPack(
		menuController.openPartnerEquipment(partner, false),
		resourcePack,
		"opens the production partner-equipment modal at its real default"))
	{
		gameManager.npcManager->removeNPCOnlyFromList(partner);
		gameManager.goodsManager.clearItem();
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}

	PartnerEquipMenu& partnerEquipMenu =
		*menuController.partnerEquipMenu;
	const std::vector<PElement> ownerCandidates =
		partnerEquipMenu.controllerFocusCandidates();
	const std::vector<PElement> bagCandidates =
		makePhysicalModalCandidates(menuController.goodsMenu->item);
	std::set<const Element*> bagCandidateSet;
	for (const PElement& candidate : bagCandidates)
	{
		if (candidate != nullptr)
		{
			bagCandidateSet.insert(candidate.get());
		}
	}
	std::vector<PElement> equipmentCandidates;
	std::set<const Element*> ownerCandidateSet;
	for (const PElement& candidate : ownerCandidates)
	{
		if (candidate != nullptr)
		{
			ownerCandidateSet.insert(candidate.get());
			if (bagCandidateSet.find(candidate.get())
				== bagCandidateSet.end())
			{
				equipmentCandidates.push_back(candidate);
			}
		}
	}
	bool ownerCandidateSetMatches =
		ownerCandidateSet.size() == ownerCandidates.size()
		&& ownerCandidates.size() == 16
		&& bagCandidates.size() == 9
		&& equipmentCandidates.size() == 7;
	for (const PElement& candidate : bagCandidates)
	{
		ownerCandidateSetMatches = ownerCandidateSetMatches
			&& candidate != nullptr
			&& ownerCandidateSet.find(candidate.get())
				!= ownerCandidateSet.end();
	}
	if (!checkPack(
		ownerCandidateSetMatches,
		resourcePack,
		"partner-equipment modal exclusively owns its 9 borrowed bag"
		" candidates and 7 equipment candidates"))
	{
		menuController.closePartnerEquipment(false);
		gameManager.npcManager->removeNPCOnlyFromList(partner);
		gameManager.goodsManager.clearItem();
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}

	// Put the production default pane first so the graph walk begins at the
	// equipment slot selected by openPartnerEquipment(), not at a test-seeded
	// borrowed Goods focus.
	const std::vector<PhysicalModalNavigationGroup> groups =
	{
		{ "partner-equipment", equipmentCandidates },
		{ "borrowed-player-bag", bagCandidates },
	};
	const PElement realDefaultFocus =
		partnerEquipMenu.controllerFocusedElement();
	ok = checkPack(
		realDefaultFocus == equipmentCandidates.front()
			&& partnerEquipMenu.isControllerFocusActive()
			&& !menuController.goodsMenu->isControllerFocusActive(),
		resourcePack,
		"opens partner-equipment with one modal owner on its first"
		" equipment candidate") && ok;

	auto runningRoot =
		std::make_shared<HeadlessRPGMenuUIRoot>(gameManager);
	auto focusedElement = [&partnerEquipMenu]()
	{
		return partnerEquipMenu.controllerFocusedElement();
	};
	auto ownerInvariant =
		[&partnerEquipMenu, &menuController, &partner]()
	{
		return partnerEquipMenu.visible && partnerEquipMenu.activated
			&& partnerEquipMenu.getPartner() == partner
			&& partnerEquipMenu.isControllerFocusActive()
			&& menuController.goodsMenu != nullptr
			&& menuController.goodsMenu->visible
			&& !menuController.goodsMenu->isControllerFocusActive();
	};
	ok = verifyPhysicalModalCandidateNavigation(
		resourcePack,
		"partner-equipment-bag-equipment",
		gameManager,
		runningRoot,
		groups,
		16,
		0,
		focusedElement,
		ownerInvariant) && ok;

	menuController.closePartnerEquipment(false);
	gameManager.npcManager->removeNPCOnlyFromList(partner);
	gameManager.goodsManager.clearItem();
	ok = checkPack(
		resourceManager.setActiveResourcePackById(resourcePack.id),
		resourcePack,
		"restores the production resource root after physical"
		" partner-equipment modal navigation") && ok;
	return ok;
}

bool testPhysicalAllVisibleMenuSpatialNavigation(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	bool ok = videoSubsystemIsStopped(
		resourcePack.id
			+ " all-visible-menu physical navigation starts");
	MenuController& menuController = *gameManager.menu;
	VirtualGamepadTest::SDLSession sdlSession;
	const std::string gamepadName = resourcePack.id
		+ " All Visible Menu Navigation Pad";
	VirtualGamepadTest::VirtualGamepad gamepad(gamepadName.c_str());
	gamepad.setAxis(
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
	gamepad.setAxis(
		SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
	auto runningRoot =
		std::make_shared<HeadlessRPGMenuUIRoot>(gameManager);
	HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(runningRoot);
	auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
		Engine::getInstance()->inputActions());
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	if (!checkPack(
		inputScope.isInitialized(),
		resourcePack,
		"initializes physical input for the all-menu navigation sweep"))
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	HeadlessPhysicalInputTest::FrameDriver frameDriver(
		inputManager,
		nowMilliseconds,
		[]()
		{
			return dispatchPhysicalUIActions(Engine::getInstance());
		},
		[&gameManager]()
		{
			gameManager.controller->processPhysicalInputFrame();
		});
	VirtualGamepadTest::runFrame(
		inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
	VirtualGamepadTest::runFrame(
		inputManager, nowMilliseconds += 10);
	gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
	VirtualGamepadTest::runFrame(
		inputManager, nowMilliseconds += 10);
	ok = checkPack(
		inputManager.activeGamepadID() == gamepad.id(),
		resourcePack,
		"claims the all-menu navigation sweep from a fresh physical D-pad edge")
		&& ok;
	inputManager.releaseForContextTransition();
	frameDriver.runFrame();
	frameDriver.runFrame();

	auto prepareBase = [&]()
	{
		menuController.clearMenu();
		menuController.cancelControllerInteraction();
		gameManager.player->cancelQueuedInteraction(false);
		gameManager.player->nextAction = nullptr;
	};
	auto verifyScenario = [&](const std::string& scenario)
	{
		frameDriver.runFrame();
		frameDriver.runFrame();
		ok = verifyPhysicalVisibleMenuNavigationGraph(
			resourcePack,
			scenario,
			gameManager,
			gamepad,
			frameDriver) && ok;
	};

	prepareBase();
	verifyScenario("hud-only");

	prepareBase();
	menuController.toggleStateView();
	menuController.setMapThumbnailVisible(true);
	const bool stateOwnerVisible =
		(menuController.stateMenu != nullptr
			&& menuController.stateMenu->visible)
		|| (menuController.isStateEquipIntegrated()
			&& menuController.equipMenu != nullptr
			&& menuController.equipMenu->visible);
	ok = checkPack(
		stateOwnerVisible,
		resourcePack,
		"opens the standalone State surface or integrated State/Equip owner"
		" beside the physical spatial graph")
		&& ok;
	verifyScenario("state-map-hud");

	prepareBase();
	menuController.toggleEquipView();
	menuController.setMapThumbnailVisible(true);
	ok = checkPack(
		menuController.equipMenu != nullptr
			&& menuController.equipMenu->visible
			&& !menuController.equipMenu->controllerFocusCandidates().empty(),
		resourcePack,
		"opens the production Equip candidate set for the physical sweep")
		&& ok;
	verifyScenario("equip-map-hud");

	prepareBase();
	menuController.togglePracticeView();
	if (menuController.practiceMenu != nullptr
		&& menuController.practiceMenu->visible)
	{
		menuController.setMapThumbnailVisible(true);
		ok = checkPack(
			!menuController.practiceMenu->controllerFocusCandidates().empty(),
			resourcePack,
			"opens the production Practice candidate set for the physical sweep")
			&& ok;
		verifyScenario("practice-map-hud");
	}
	else
	{
		std::cout << "GAMEPAD_MENU_COVERAGE pack=" << resourcePack.id
			<< " scenario=practice-map-hud policy=resource-disabled\n";
	}

	prepareBase();
	menuController.toggleGoodsView();
	menuController.setMapThumbnailVisible(true);
	ok = checkPack(
		menuController.goodsMenu != nullptr
			&& menuController.goodsMenu->visible
			&& !menuController.goodsMenu->controllerFocusCandidates().empty(),
		resourcePack,
		"opens every production Goods slot for the physical sweep")
		&& ok;
	verifyScenario("goods-map-hud");

	prepareBase();
	menuController.toggleMagicView();
	menuController.setMapThumbnailVisible(true);
	const bool magicCandidateOwnerVisible =
		(menuController.magicMenu != nullptr
			&& menuController.magicMenu->visible
			&& !menuController.magicMenu->controllerFocusCandidates().empty())
		|| (menuController.equipMenu != nullptr
			&& menuController.equipMenu->visible
			&& !menuController.equipMenu->controllerFocusCandidates().empty());
	ok = checkPack(
		magicCandidateOwnerVisible,
		resourcePack,
		"opens the standalone or integrated Magic candidate set for the"
		" physical sweep") && ok;
	verifyScenario("magic-map-hud");

	prepareBase();
	gameManager.memo.memo.clear();
	for (int index = 0; index < MEMO_LINE + 4; index++)
	{
		gameManager.memo.memo.push_back(
			"Controller memo navigation line "
			+ std::to_string(index + 1));
	}
	menuController.toggleMemoView();
	menuController.memoMenu->reRange(
		static_cast<int>(gameManager.memo.memo.size()) - 1);
	menuController.memoMenu->reset();
	menuController.setMapThumbnailVisible(true);
	ok = checkPack(
		menuController.memoMenu != nullptr
			&& menuController.memoMenu->visible
			&& !menuController.memoMenu->controllerFocusCandidates().empty(),
		resourcePack,
		"opens the scrollable Memo candidate for the physical sweep")
		&& ok;
	verifyScenario("memo-map-hud");
	gameManager.memo.memo.clear();

	std::string partnerHeadFixtureName;
	if (gameManager.global.feature.menuResourceProfile == mrpXjxqy)
	{
		partnerHeadFixtureName = "独孤剑";
	}
	else if (gameManager.global.feature.menuResourceProfile == mrpYycs)
	{
		partnerHeadFixtureName = "杨影枫";
	}

	std::shared_ptr<NPC> partner;
	if (gameManager.global.feature.partnerHeadMenu
		&& !partnerHeadFixtureName.empty())
	{
		const std::string partnerHeadAssetPath =
			"asf\\ui\\littlehead\\" + partnerHeadFixtureName + ".asf";
		ok = checkPack(
			File::fileExist(partnerHeadAssetPath),
			resourcePack,
			"resolves the UI-profile-compatible PartnerHead image before"
			" testing its navigation graph") && ok;

		partner = std::make_shared<NPC>();
		partner->kind = nkPartner;
		partner->npcName = partnerHeadFixtureName;
		partner->canEquip = 1;
		partner->level = 10;
		gameManager.npcManager->addNPC(partner);
		menuController.partnerHeadMenu->visible = true;
		menuController.partnerHeadMenu->activated = true;
		menuController.partnerHeadMenu->refreshPartnerButtons();
		prepareBase();
		menuController.partnerHeadMenu->refreshPartnerButtons();
		menuController.setMapThumbnailVisible(true);
		ok = checkPack(
			menuController.partnerHeadMenu->hasControllerPartners()
				&& !menuController.partnerHeadMenu
					->controllerFocusCandidates().empty(),
			resourcePack,
			"opens the UI-profile-compatible production PartnerHead entry"
			" for the physical sweep")
			&& ok;
		verifyScenario("partner-head-map-hud");
		gameManager.npcManager->removeNPCOnlyFromList(partner);
		menuController.partnerHeadMenu->refreshPartnerButtons();
	}
	else
	{
		std::cout << "GAMEPAD_MENU_COVERAGE pack=" << resourcePack.id
			<< " scenario=partner-head-map-hud policy=resource-disabled\n";
	}

	prepareBase();
	gameManager.memo.memo.clear();
	ok = videoSubsystemIsStopped(
		resourcePack.id
			+ " all-visible-menu physical navigation finishes") && ok;
	return ok;
}

bool testOrdinaryControlRightClickWorldIsolation(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	menuController.clearMenu();
	menuController.setMapThumbnailVisible(true);
	auto closeButton = menuController.mapThumbnailMenu != nullptr
		? menuController.mapThumbnailMenu->getComponentByName<Button>(
			"closeButton")
		: nullptr;
	if (!checkPack(
		closeButton != nullptr && closeButton->visible
			&& closeButton->rect.w > 0 && closeButton->rect.h > 0,
		resourcePack,
		"loads the production nonmodal map Button for right-click routing"))
	{
		menuController.clearMenu();
		return false;
	}

	auto npc = std::make_shared<HeadlessRPGPointerNPC>();
	npc->scriptFile = "primary.lua";
	npc->scriptFileRight = "alternate.lua";
	gameManager.npcManager->npcList = { npc };
	gameManager.npcManager->clickIndex = 0;
	gameManager.objectManager->objectList.clear();
	gameManager.objectManager->clickIndex = -1;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	const Point closeCenter = elementCenter(closeButton);
	auto passThroughProbe = std::make_shared<RightButtonPassThroughProbe>();
	gameManager.addChild(passThroughProbe);
	menuController.cancelPointerInteraction();
	dispatchPointerRightDownWithoutMotionThroughElementTree(
		gameManager, closeCenter.x, closeCenter.y);
	bool ok = checkPack(
		gameManager.player->nextAction == nullptr
			&& passThroughProbe->rightButtonDownCount == 0,
		resourcePack,
		"a right-click without prior MouseMotion on the production nonmodal"
		" Button does not also queue the hovered NPC alternate interaction");

	Point uncoveredPosition = {};
	ok = checkPack(
		GamepadRPGMenuActionsTestAccess::findUncoveredPointerPosition(
			menuController, 1024, 768, uncoveredPosition),
		resourcePack,
		"finds ordinary nonmodal menu space that has no concrete pointer hit")
		&& ok;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	menuController.cancelPointerInteraction();
	dispatchPointerRightDownThroughElementTree(
		gameManager, closeCenter.x, closeCenter.y);
	ok = checkPack(
		closeButton->touchingID == TOUCH_MOUSEID,
		resourcePack,
		"seeds the production Button hover before the stale-hover"
		" pass-through check") && ok;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	passThroughProbe->rightButtonDownCount = 0;
	dispatchPointerRightDownWithoutMotionThroughElementTree(
		gameManager, uncoveredPosition.x, uncoveredPosition.y);
	ok = checkPack(
		gameManager.player->nextAction == nullptr
			&& passThroughProbe->rightButtonDownCount == 1,
		resourcePack,
		"a stale Button hover does not consume a no-MouseMotion right-click"
		" before it passes through the production GameController") && ok;

	menuController.setMapThumbnailVisible(false);
	if (menuController.topMenu != nullptr)
	{
		const std::vector<PElement> topCandidates =
			menuController.topMenu->controllerFocusCandidates();
		if (!checkPack(
			!topCandidates.empty() && topCandidates.front() != nullptr
				&& std::dynamic_pointer_cast<CheckBox>(
					topCandidates.front()) != nullptr,
			resourcePack,
			"loads a production Top CheckBox for right-click routing"))
		{
			ok = false;
		}
		else
		{
			auto object = std::make_shared<Object>();
			object->position = { 5, 4 };
			object->scriptFile = "primary.lua";
			object->scriptFileRight = "alternate.lua";
			gameManager.objectManager->objectList = { object };
			gameManager.objectManager->clickIndex = 0;
			gameManager.npcManager->npcList.clear();
			gameManager.npcManager->clickIndex = -1;
			gameManager.player->cancelQueuedInteraction(false);
			gameManager.player->nextAction = nullptr;
			const Point checkCenter = elementCenter(topCandidates.front());
			menuController.cancelPointerInteraction();
			passThroughProbe->rightButtonDownCount = 0;
			dispatchPointerRightDownWithoutMotionThroughElementTree(
				gameManager, checkCenter.x, checkCenter.y);
			ok = checkPack(
				gameManager.player->nextAction == nullptr
					&& passThroughProbe->rightButtonDownCount == 0,
				resourcePack,
				"a right-click without prior MouseMotion on the production"
				" nonmodal CheckBox does not also queue the hovered Object"
				" alternate interaction") && ok;

			gameManager.player->cancelQueuedInteraction(false);
			gameManager.player->nextAction = nullptr;
			menuController.cancelPointerInteraction();
			dispatchPointerRightDownThroughElementTree(
				gameManager, checkCenter.x, checkCenter.y);
			ok = checkPack(
				topCandidates.front()->touchingID == TOUCH_MOUSEID,
				resourcePack,
				"seeds the production CheckBox hover before the stale-hover"
				" pass-through check") && ok;
			gameManager.player->cancelQueuedInteraction(false);
			gameManager.player->nextAction = nullptr;
			passThroughProbe->rightButtonDownCount = 0;
			dispatchPointerRightDownWithoutMotionThroughElementTree(
				gameManager, uncoveredPosition.x, uncoveredPosition.y);
			ok = checkPack(
				gameManager.player->nextAction == nullptr
					&& passThroughProbe->rightButtonDownCount == 1,
				resourcePack,
				"a stale CheckBox hover does not consume a no-MouseMotion"
				" right-click before it passes through the production"
				" GameController") && ok;
		}
	}

	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	gameManager.npcManager->npcList.clear();
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->objectList.clear();
	gameManager.objectManager->clickIndex = -1;
	gameManager.removeChild(passThroughProbe);
	menuController.clearMenu();
	return ok;
}

bool testSameFrameNPCPointerHit(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	menuController.clearMenu();
	Point pointerPosition = {};
	if (!checkPack(
		GamepadRPGMenuActionsTestAccess::findUncoveredPointerPosition(
			menuController, 1024, 768, pointerPosition),
		resourcePack,
		"finds world space for the same-frame NPC pointer regression"))
	{
		return false;
	}

	auto npc = std::make_shared<HeadlessRPGPointerNPC>();
	npc->rect =
	{
		pointerPosition.x - 8,
		pointerPosition.y - 8,
		16,
		16
	};
	npc->setPosition({ 9, 89 });
	npc->scriptFile = "npc_primary.lua";
	gameManager.npcManager->npcList = { npc };
	gameManager.npcManager->addChild(npc);
	gameManager.npcManager->clickIndex = -1;
	gameManager.objectManager->objectList.clear();
	gameManager.objectManager->clickIndex = -1;
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	gameManager.cancelPointerInteraction();

	dispatchPointerDownThroughElementTree(
		gameManager, pointerPosition.x, pointerPosition.y);
	bool ok = checkPack(
		npc->touchingDownID == TOUCH_MOUSEID
			&& gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->destGE.lock() == npc
			&& gameManager.player->nextAction->destKind == ndTalk,
		resourcePack,
		"queues the NPC selected by MouseMotion and MouseDown in one frame");

	gameManager.cancelPointerInteraction();
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	gameManager.npcManager->clickIndex = -1;
	gameManager.controller->touchingID = TOUCH_MOUSEID;
	dispatchPointerDownWithoutMotionThroughElementTree(
		gameManager, pointerPosition.x, pointerPosition.y);
	ok = checkPack(
		gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->destGE.lock() == npc
			&& gameManager.player->nextAction->destKind == ndTalk,
		resourcePack,
		"prefers the current NPC hit over stale GameController world hover")
		&& ok;

	gameManager.cancelPointerInteraction();
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	gameManager.npcManager->removeChild(npc);
	gameManager.npcManager->npcList.clear();
	gameManager.npcManager->clickIndex = -1;
	return ok;
}

bool testNestedSystemRunClearsMenuPointerTransaction(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	menuController.clearMenu();
	menuController.setMapThumbnailVisible(true);
	const std::vector<PElement> mapCandidates =
		menuController.mapThumbnailMenu != nullptr
			? menuController.mapThumbnailMenu->controllerFocusCandidates()
			: std::vector<PElement>();
	if (!checkPack(
		mapCandidates.size() == 1 && mapCandidates.front() != nullptr,
		resourcePack,
		"loads the production map hit target for the nested-run transaction"))
	{
		menuController.clearMenu();
		return false;
	}

	const Point mapCenter = elementCenter(mapCandidates.front());
	dispatchPointerDownThroughElementTree(
		gameManager, mapCenter.x, mapCenter.y);
	bool ok = checkPack(
		menuController.ownsPointerTransaction(TOUCH_MOUSEID),
		resourcePack,
		"acquires a production left-button menu transaction before System run");

	auto system = std::make_shared<ImmediateStopSystem>();
	gameManager.addChild(system);
	PElement runningOwner(&gameManager, [](Element*) {});
	{
		HeadlessPhysicalInputTest::ScopedRunningOwner ownerScope(runningOwner);
		system->run();
	}
	gameManager.removeChild(system);
	ok = checkPack(
		!menuController.ownsPointerTransaction(TOUCH_MOUSEID),
		resourcePack,
		"nested System::run clears the outer left-button menu transaction")
		&& ok;

	Engine::getInstance()->pushEvent(AEvent(
		ET_MOUSEUP, MBC_MOUSE_LEFT, mapCenter.x, mapCenter.y, false));
	GamepadRPGMenuActionsTestAccess::dispatchElementEvents(gameManager);
	menuController.clearMenu();
	return ok;
}

bool testTopHUDFocusLifecycle(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	if (menuController.topMenu == nullptr)
	{
		return true;
	}
	menuController.clearMenu();
	const std::vector<PElement> candidates =
		menuController.topMenu->controllerFocusCandidates();
	using TopControlMember = std::shared_ptr<CheckBox> TopMenu::*;
	const std::array<TopControlMember, 6> nonDefaultMembers =
	{
		&TopMenu::optionBtn,
		&TopMenu::notesBtn,
		&TopMenu::magicBtn,
		&TopMenu::goodsBtn,
		&TopMenu::xiulianBtn,
		&TopMenu::equipBtn
	};
	const std::array<TopControlMember, 7> allMembers =
	{
		&TopMenu::stateBtn,
		&TopMenu::equipBtn,
		&TopMenu::xiulianBtn,
		&TopMenu::goodsBtn,
		&TopMenu::magicBtn,
		&TopMenu::notesBtn,
		&TopMenu::optionBtn
	};
	auto configuredTopControls = [&menuController, &allMembers]()
	{
		std::vector<PElement> controls;
		for (TopControlMember member : allMembers)
		{
			const std::shared_ptr<CheckBox>& control =
				menuController.topMenu.get()->*member;
			if (control != nullptr)
			{
				controls.push_back(control);
			}
		}
		return controls;
	};
	const std::vector<PElement> configuredControlsBeforeResize =
		configuredTopControls();
	bool ok = true;
	if (resourcePack.id == "YYCS")
	{
		ok = checkPack(
			menuController.topMenu->stateBtn != nullptr
				&& menuController.topMenu->stateBtn->stretch
				&& menuController.topMenu->stateBtn->rect.w == 38
				&& menuController.topMenu->stateBtn->rect.h == 38,
			resourcePack,
			"applies the configured two-times scale to the Top CheckBox icon")
			&& ok;
	}
	TopControlMember stableFocusMember = nullptr;
	PElement stableFocusBeforeResize = nullptr;
	for (TopControlMember member : nonDefaultMembers)
	{
		const std::shared_ptr<CheckBox>& control =
			menuController.topMenu.get()->*member;
		if (control != nullptr
			&& std::find(candidates.begin(), candidates.end(), control)
				!= candidates.end()
			&& !candidates.empty()
			&& control != candidates.front())
		{
			stableFocusMember = member;
			stableFocusBeforeResize = control;
			break;
		}
	}
	if (!checkPack(
		candidates.size() >= 2 && stableFocusMember != nullptr
			&& stableFocusBeforeResize != nullptr
			&& menuController.adoptControllerPointerFocus(
				menuController.topMenu, stableFocusBeforeResize),
		resourcePack,
		"focuses a stable non-default production Top HUD node"))
	{
		return false;
	}

	gameManager.scriptAPI.hideInterface();
	ok = checkPack(
		!menuController.topMenu->visible
			&& !menuController.topMenu->isControllerFocusActive()
			&& std::none_of(
				candidates.begin(),
				candidates.end(),
				[](const PElement& candidate)
				{
					return candidate != nullptr && candidate->isFocused();
				}),
		resourcePack,
		"HideInterface hides Top HUD and removes its active focus presentation");
	dispatchWindowResizeThroughElementTree(gameManager, 1280, 720);
	const std::vector<PElement> hiddenResizedCandidates =
		menuController.topMenu->controllerFocusCandidates();
	const std::vector<PElement> hiddenConfiguredControls =
		configuredTopControls();
	const PElement hiddenStableControl =
		menuController.topMenu.get()->*stableFocusMember;
	ok = checkPack(
		!menuController.topMenu->visible,
		resourcePack,
		"a production ET_WINDOWRESIZE does not reshow hidden Top") && ok;
	ok = checkPack(
		!menuController.topMenu->isControllerFocusActive(),
		resourcePack,
		"a production ET_WINDOWRESIZE does not activate hidden Top focus") && ok;
	ok = checkPack(
		hiddenResizedCandidates.empty(),
		resourcePack,
		"hidden Top exposes no available controller candidates after resize")
		&& ok;
	ok = checkPack(
		hiddenConfiguredControls.size()
			== configuredControlsBeforeResize.size(),
		resourcePack,
		"hidden Top resize preserves the configured controller-node count")
		&& ok;
	ok = checkPack(
		std::none_of(
				hiddenConfiguredControls.begin(),
				hiddenConfiguredControls.end(),
				[](const PElement& candidate)
				{
					return candidate != nullptr && candidate->isFocused();
				}),
		resourcePack,
		"hidden Top resize leaves every rebuilt control presentation neutral")
		&& ok;
	ok = checkPack(
		hiddenStableControl != nullptr
			&& hiddenStableControl != stableFocusBeforeResize,
		resourcePack,
		"hidden Top resize rebuilds the selected stable member for later"
		" node-ID restoration") && ok;

	gameManager.scriptAPI.showInterface();
	const std::vector<PElement> shownCandidates =
		menuController.topMenu->controllerFocusCandidates();
	const PElement stableFocusAfterResize =
		menuController.topMenu.get()->*stableFocusMember;
	ok = checkPack(
		menuController.topMenu->visible
			&& menuController.topMenu->isControllerFocusActive(),
		resourcePack,
		"ShowInterface makes the resized Top owner visible and active") && ok;
	ok = checkPack(
		shownCandidates.size() == candidates.size(),
		resourcePack,
		"ShowInterface restores the resized Top available-candidate count")
		&& ok;
	ok = checkPack(
		stableFocusAfterResize != nullptr
			&& !shownCandidates.empty()
			&& menuController.topMenu->controllerFocusedElement()
				== stableFocusAfterResize
			&& stableFocusAfterResize != shownCandidates.front()
			&& stableFocusAfterResize != stableFocusBeforeResize,
		resourcePack,
		"ShowInterface restores the same stable non-default Top node ID after"
		" the hidden production resize") && ok;
	menuController.topMenu->deactivateControllerFocus();
	dispatchWindowResizeThroughElementTree(gameManager, 1024, 768);
	return ok;
}

#ifdef __MOBILE__
bool testDeferredWorldTouchDoesNotCrossMenuOpen(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	menuController.clearMenu();
	Point worldTouchPosition = {};
	if (!checkPack(
		GamepadRPGMenuActionsTestAccess::findUncoveredPointerPosition(
			menuController, 1024, 768, worldTouchPosition),
		resourcePack,
		"finds a production world-touch point outside persistent menu controls"))
	{
		return false;
	}
	auto npc = std::make_shared<HeadlessRPGPointerNPC>();
	npc->rect =
	{
		worldTouchPosition.x - 8,
		worldTouchPosition.y - 8,
		16,
		16
	};
	npc->scriptFile = "primary.lua";
	npc->scriptFileRight = "alternate.lua";
	npc->coverMouse = true;
	gameManager.npcManager->npcList = { npc };
	gameManager.npcManager->addChild(npc);

	const EventTouchID npcPointerID = 94001;
	dispatchFingerEventThroughElementTree(
		gameManager,
		ET_FINGERDOWN,
		npcPointerID,
		worldTouchPosition.x,
		worldTouchPosition.y);
	bool ok = checkPack(
		npc->touchingDownID == npcPointerID,
		resourcePack,
		"Engine finger-down reaches the production GameManager, GameController,"
		" NPCManager, and NPC long-press transaction");
	GamepadRPGMenuActionsTestAccess::makeLongPress(*npc);
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	menuController.setMapThumbnailVisible(true);
	dispatchFingerEventThroughElementTree(
		gameManager,
		ET_FINGERUP,
		npcPointerID,
		worldTouchPosition.x,
		worldTouchPosition.y);
	ok = checkPack(
		npc->touchingDownID == TOUCH_UNTOUCHEDID
			&& gameManager.player->nextAction == nullptr,
		resourcePack,
		"an Engine-dispatched deferred NPC long press does not commit after"
		" the map opens") && ok;
	menuController.setMapThumbnailVisible(false);
	gameManager.npcManager->removeChild(npc);
	gameManager.npcManager->npcList.clear();

	auto object = std::make_shared<HeadlessRPGPointerObject>();
	object->position = { 5, 4 };
	object->rect =
	{
		worldTouchPosition.x - 8,
		worldTouchPosition.y - 8,
		16,
		16
	};
	object->scriptFile = "primary.lua";
	object->scriptFileRight = "alternate.lua";
	object->coverMouse = true;
	gameManager.objectManager->objectList = { object };
	gameManager.objectManager->addChild(object);
	const EventTouchID objectPointerID = 94002;
	dispatchFingerEventThroughElementTree(
		gameManager,
		ET_FINGERDOWN,
		objectPointerID,
		worldTouchPosition.x,
		worldTouchPosition.y);
	ok = checkPack(
		object->touchingDownID == objectPointerID,
		resourcePack,
		"Engine finger-down reaches the production GameManager, GameController,"
		" ObjectManager, and Object long-press transaction")
		&& ok;
	GamepadRPGMenuActionsTestAccess::makeLongPress(*object);
	gameManager.player->cancelQueuedInteraction(false);
	gameManager.player->nextAction = nullptr;
	menuController.toggleGoodsView();
	dispatchFingerEventThroughElementTree(
		gameManager,
		ET_FINGERUP,
		objectPointerID,
		worldTouchPosition.x,
		worldTouchPosition.y);
	ok = checkPack(
		object->touchingDownID == TOUCH_UNTOUCHEDID
			&& gameManager.player->nextAction == nullptr,
		resourcePack,
		"an Engine-dispatched deferred Object long press does not commit after"
		" an ordinary menu opens") && ok;
	menuController.clearMenu();
	gameManager.objectManager->removeChild(object);
	gameManager.objectManager->objectList.clear();
	return ok;
}
#endif

bool testVisibleMenuCrossNavigation(
	const ResourcePackExpectation& resourcePack,
	GameManager& gameManager)
{
	MenuController& menuController = *gameManager.menu;
	menuController.clearMenu();
	menuController.toggleGoodsView();
	menuController.setMapThumbnailVisible(true);
	if (!checkPack(
		menuController.goodsMenu != nullptr
			&& menuController.goodsMenu->visible
			&& menuController.goodsMenu->item.size() >= 2
			&& menuController.goodsMenu->item[0] != nullptr
			&& menuController.goodsMenu->item[1] != nullptr
			&& menuController.bottomMenu != nullptr
			&& menuController.mapThumbnailMenu != nullptr
			&& menuController.mapThumbnailMenu->visible,
		resourcePack,
		"loads Goods, Bottom, and map surfaces for cross-menu navigation"))
	{
		return false;
	}

	const std::vector<PElement> bottomCandidates =
		menuController.bottomMenu->controllerFocusCandidates();
	if (!checkPack(
		!bottomCandidates.empty(),
		resourcePack,
		"loads at least one resource-specific Bottom focus target"))
	{
		menuController.clearMenu();
		return false;
	}
	const std::vector<PElement> topCandidates =
		menuController.topMenu != nullptr
			? menuController.topMenu->controllerFocusCandidates()
			: std::vector<PElement>();
	if (!checkPack(
		menuController.topMenu == nullptr || !topCandidates.empty(),
		resourcePack,
		"loads focus targets for the production Top HUD when it is enabled"))
	{
		menuController.clearMenu();
		return false;
	}
	const std::vector<PElement> mapCandidates =
		menuController.mapThumbnailMenu->controllerFocusCandidates();
	if (!checkPack(
		mapCandidates.size() == 1 && mapCandidates.front() != nullptr
			&& mapCandidates.front()->coverMouse,
		resourcePack,
		"registers the production map thumbnail as one focus and hit surface"))
	{
		return false;
	}
	std::vector<std::pair<PElement, Rect>> productionLayout;
	auto rememberLayout = [&productionLayout](const PElement& element)
	{
		if (element != nullptr)
		{
			productionLayout.emplace_back(element, element->rect);
		}
	};
	for (const PElement& item : menuController.goodsMenu->item)
	{
		rememberLayout(item);
	}
	for (const PElement& candidate : bottomCandidates)
	{
		rememberLayout(candidate);
	}
	for (const PElement& candidate : topCandidates)
	{
		rememberLayout(candidate);
	}
	for (const PElement& candidate : mapCandidates)
	{
		rememberLayout(candidate);
	}

	bool ok = checkPack(
		menuController.mapThumbnailMenu->isControllerFocusActive(),
		resourcePack,
		"newly shown map is the initial logical focus without hiding Goods")
		&& checkPack(
			menuController.goodsMenu->visible
				&& menuController.bottomMenu->visible,
			resourcePack,
			"keeps earlier nonmodal menus visible after opening the map");
	std::vector<PElement> allVisibleCandidates =
		menuController.goodsMenu->controllerFocusCandidates();
	allVisibleCandidates.insert(
		allVisibleCandidates.end(),
		bottomCandidates.begin(),
		bottomCandidates.end());
	allVisibleCandidates.insert(
		allVisibleCandidates.end(),
		topCandidates.begin(),
		topCandidates.end());
	allVisibleCandidates.insert(
		allVisibleCandidates.end(),
		mapCandidates.begin(),
		mapCandidates.end());
	ok = checkPack(
		!hasFullyCoveredCandidate(
			menuController.goodsMenu->controllerFocusCandidates())
			&& !hasFullyCoveredCandidate(bottomCandidates)
			&& !hasFullyCoveredCandidate(topCandidates)
			&& !hasFullyCoveredCandidate(mapCandidates)
			&& !hasFullyCoveredCandidate(allVisibleCandidates),
		resourcePack,
		"enabled controls contain no fully covered focus candidate within or"
		" across visible owners")
		&& ok;
	struct NavigationOwner
	{
		std::string name;
		PElement element;
		ControllerFocusParticipant* participant = nullptr;
		std::vector<PElement> candidates;
	};
	std::vector<NavigationOwner> navigationOwners;
	auto addNavigationOwner =
		[&navigationOwners](
			const std::string& name,
			const PElement& element,
			ControllerFocusParticipant* participant)
	{
		if (element != nullptr && participant != nullptr
			&& element->visible && element->activated)
		{
			navigationOwners.push_back(
				{ name, element, participant,
					participant->controllerFocusCandidates() });
		}
	};
	addNavigationOwner(
		"Goods", menuController.goodsMenu, menuController.goodsMenu.get());
	addNavigationOwner(
		"Bottom", menuController.bottomMenu, menuController.bottomMenu.get());
	if (menuController.topMenu != nullptr)
	{
		addNavigationOwner(
			"Top", menuController.topMenu, menuController.topMenu.get());
	}
	addNavigationOwner(
		"Map",
		menuController.mapThumbnailMenu,
		menuController.mapThumbnailMenu.get());
	bool graphDispatchValid = std::all_of(
		navigationOwners.begin(),
		navigationOwners.end(),
		[](const NavigationOwner& owner)
		{
			return !owner.candidates.empty();
		});
	std::string graphDispatchFailure;
	std::vector<std::set<std::size_t>> ownerEdges(
		navigationOwners.size());
	auto activeOwnerIndex = [&navigationOwners]()
	{
		int activeIndex = -1;
		for (std::size_t index = 0; index < navigationOwners.size(); index++)
		{
			if (!navigationOwners[index].participant
				->isControllerFocusActive())
			{
				continue;
			}
			if (activeIndex >= 0)
			{
				return -2;
			}
			activeIndex = static_cast<int>(index);
		}
		return activeIndex;
	};
	const std::array<UIAction, 4> directions =
	{
		UIAction::NavigateUp,
		UIAction::NavigateDown,
		UIAction::NavigateLeft,
		UIAction::NavigateRight
	};
	constexpr int NavigationProbeStepLimit = 96;
	for (std::size_t sourceIndex = 0;
		sourceIndex < navigationOwners.size();
		sourceIndex++)
	{
		const NavigationOwner& source = navigationOwners[sourceIndex];
		for (const PElement& candidate : source.candidates)
		{
			for (UIAction direction : directions)
			{
				if (!menuController.adoptControllerPointerFocus(
					source.element, candidate)
					|| activeOwnerIndex() != static_cast<int>(sourceIndex))
				{
					graphDispatchValid = false;
					graphDispatchFailure =
						"cannot activate " + source.name + " candidate";
					continue;
				}
				for (int step = 0;
					step < NavigationProbeStepLimit;
					step++)
				{
					const PElement focusedBefore =
						source.participant->controllerFocusedElement();
					const bool handled =
						menuController.handleUIAction(direction);
					const int destinationIndex = activeOwnerIndex();
					if (destinationIndex < 0)
					{
						graphDispatchValid = false;
						graphDispatchFailure =
							"directional dispatch from " + source.name
							+ " did not leave exactly one active owner";
						break;
					}
					if (destinationIndex != static_cast<int>(sourceIndex))
					{
						// Record only the first owner boundary crossed from
						// this real source candidate and direction.
						ownerEdges[sourceIndex].insert(
							static_cast<std::size_t>(destinationIndex));
						break;
					}
					const PElement focusedAfter =
						source.participant->controllerFocusedElement();
					if (!handled && focusedAfter == focusedBefore)
					{
						break;
					}
				}
			}
		}
	}
	std::string missingPaths;
	for (std::size_t sourceIndex = 0;
		sourceIndex < navigationOwners.size();
		sourceIndex++)
	{
		std::set<std::size_t> reachable = { sourceIndex };
		std::vector<std::size_t> pending = { sourceIndex };
		for (std::size_t pendingIndex = 0;
			pendingIndex < pending.size();
			pendingIndex++)
		{
			for (std::size_t destination :
				ownerEdges[pending[pendingIndex]])
			{
				if (reachable.insert(destination).second)
				{
					pending.push_back(destination);
				}
			}
		}
		for (std::size_t destinationIndex = 0;
			destinationIndex < navigationOwners.size();
			destinationIndex++)
		{
			if (reachable.find(destinationIndex) != reachable.end())
			{
				continue;
			}
			if (!missingPaths.empty())
			{
				missingPaths += ",";
			}
			missingPaths += navigationOwners[sourceIndex].name + "->"
				+ navigationOwners[destinationIndex].name;
		}
	}
	std::string observedEdges;
	for (std::size_t sourceIndex = 0;
		sourceIndex < navigationOwners.size();
		sourceIndex++)
	{
		for (std::size_t destinationIndex : ownerEdges[sourceIndex])
		{
			if (!observedEdges.empty())
			{
				observedEdges += ",";
			}
			observedEdges += navigationOwners[sourceIndex].name + "->"
				+ navigationOwners[destinationIndex].name;
		}
	}
	std::string graphExpectation =
		"real production candidates and rectangles form a strongly connected"
		" directed graph across every visible owner";
	if (!graphDispatchValid || !missingPaths.empty())
	{
		graphExpectation += "; dispatch=";
		graphExpectation += graphDispatchValid
			? "valid"
			: graphDispatchFailure;
		graphExpectation += "; missing=";
		graphExpectation += missingPaths.empty() ? "none" : missingPaths;
		graphExpectation += "; edges=";
		graphExpectation += observedEdges.empty() ? "none" : observedEdges;
	}
	ok = checkPack(
		graphDispatchValid && missingPaths.empty(),
		resourcePack,
		graphExpectation) && ok;

	dispatchPointerTapThroughElementTree(
		gameManager,
		menuController.goodsMenu->item[0]->rect.x
			+ menuController.goodsMenu->item[0]->rect.w / 2,
		menuController.goodsMenu->item[0]->rect.y
			+ menuController.goodsMenu->item[0]->rect.h / 2);
	ok = checkPack(
		menuController.goodsMenu->isControllerFocusActive()
			&& menuController.goodsMenu->controllerFocusedElement()
				== menuController.goodsMenu->item[0]
			&& !menuController.mapThumbnailMenu->isControllerFocusActive(),
		resourcePack,
		"adopts the exact Goods control selected by a real Element-tree"
		" mouse down/up transaction") && ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::NavigateRight)
			&& menuController.goodsMenu->controllerFocusedElement()
				== menuController.goodsMenu->item[1],
		resourcePack,
		"continues gamepad navigation from the pointer-selected control") && ok;
	dispatchPointerRightDownThroughElementTree(
		gameManager,
		menuController.goodsMenu->item[0]->rect.x
			+ menuController.goodsMenu->item[0]->rect.w / 2,
		menuController.goodsMenu->item[0]->rect.y
			+ menuController.goodsMenu->item[0]->rect.h / 2);
	ok = checkPack(
		menuController.goodsMenu->isControllerFocusActive()
			&& menuController.goodsMenu->controllerFocusedElement()
				== menuController.goodsMenu->item[0],
		resourcePack,
		"right-click adopts the exact hovered Item before its business action")
		&& ok;
	ok = checkPack(
		menuController.handleUIAction(UIAction::NavigateRight)
			&& menuController.goodsMenu->controllerFocusedElement()
				== menuController.goodsMenu->item[1],
		resourcePack,
		"gamepad navigation continues from the right-click-selected Item")
		&& ok;

	menuController.setMapThumbnailVisible(false);
	ok = checkPack(
		menuController.goodsMenu->isControllerFocusActive()
			&& menuController.goodsMenu->controllerFocusedElement()
				== menuController.goodsMenu->item[1],
		resourcePack,
		"closing an unfocused map preserves the current valid focus") && ok;
	ok = checkPack(
		std::all_of(
			productionLayout.begin(),
			productionLayout.end(),
			[](const std::pair<PElement, Rect>& state)
			{
				return state.first != nullptr
					&& sameRect(state.first->rect, state.second);
			}),
		resourcePack,
		"real cross-menu connectivity test does not rewrite production rects")
		&& ok;
	menuController.clearMenu();
	return ok;
}

bool testPhysicalBottomMenuInputComposition(
	const ResourcePackExpectation& resourcePack,
	ResourceManager& resourceManager,
	GameManager& gameManager,
	const std::filesystem::path& fixtureRoot)
{
	bool ok = videoSubsystemIsStopped(
		std::string(resourcePack.id)
			+ " physical Bottom composition starts");
	MenuController& menuController = *gameManager.menu;
	const int firstStoreIndex = gameManager.goodsManager.storeBegin();
	const int secondStoreIndex = firstStoreIndex + 1;
	const int headEquipmentIndex = gameManager.goodsManager.equipIndex(0);
	if (!checkPack(
		gameManager.goodsManager.isStoreIndex(firstStoreIndex)
			&& gameManager.goodsManager.isStoreIndex(secondStoreIndex)
			&& gameManager.goodsManager.isEquipIndex(headEquipmentIndex),
		resourcePack,
		"exposes two bag slots and the head slot for the physical Goods composition"))
	{
		return false;
	}

	const std::string productionRoot =
		resourceManager.getActiveResourceRoot();
	const int magicStoreIndex = gameManager.magicManager.storeBegin();
	const std::vector<MagicInfo> productionMagics =
		loadProductionMagicInfos(productionRoot, 1);
	if (!checkPack(
		gameManager.magicManager.isStoreIndex(magicStoreIndex)
			&& gameManager.magicManager.bottomCount() >= 2
			&& !productionMagics.empty(),
		resourcePack,
		"loads a real magic source for the physical Bottom composition"))
	{
		return false;
	}
	File::setActiveResourceRoot(fixtureRoot.generic_string());
	File::setResourceFallbackRoots({ productionRoot });
	gameManager.goodsManager.clearItem();
	if (!checkPack(
		loadGoodsInfo(
			gameManager.goodsManager.goodsList[firstStoreIndex],
			DrugGoodsFile),
		resourcePack,
		"loads the drug used by the physical A composition"))
	{
		resourceManager.setActiveResourcePackById(resourcePack.id);
		return false;
	}

	gameManager.global.data.canInput = true;
	gameManager.inEvent = false;
	gameManager.setGameplayPaused(false);
	gameManager.player->lifeMax = 1000;
	gameManager.player->calInfo();
	gameManager.player->life = 100;
	gameManager.player->nextAction = nullptr;
	menuController.goodsMenu->updateGoods();

	{
		VirtualGamepadTest::SDLSession sdlSession;
		const std::string gamepadName = std::string(resourcePack.id)
			+ " Headless RPG Goods Physical-Link Pad";
		VirtualGamepadTest::VirtualGamepad gamepad(gamepadName.c_str());
		gamepad.setAxis(
			SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
		gamepad.setAxis(
			SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);

		auto runningRoot =
			std::make_shared<HeadlessRPGMenuUIRoot>(gameManager);
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
			runningRoot);
		auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
			Engine::getInstance()->inputActions());
		HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
			inputManager);
		ok = checkPack(
			inputScope.isInitialized(),
			resourcePack,
			"initializes the production physical input manager without video")
			&& ok;
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
				[&gameManager]()
				{
					gameManager.controller->processPhysicalInputFrame();
				});
			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			gamepad.setButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
			VirtualGamepadTest::runFrame(
				inputManager, nowMilliseconds += 10);
			ok = checkPack(
				inputManager.activeGamepadID() == gamepad.id(),
				resourcePack,
				"claims the physical Goods composition with fresh virtual input")
				&& ok;

			inputManager.releaseForContextTransition();
			frameDriver.runFrame();
			frameDriver.runFrame();

			ok = testPersistentHUDWorldInputComposition(
				resourcePack,
				gamepad,
				gameManager,
				frameDriver) && ok;

			// Persistent-HUD navigation intentionally remembers the last slot
			// in each Bottom pane. Isolate the later 0 -> 1 MagicQuick transfer
			// assertion from whichever HUD target this resource layout selected.
			ok = checkPack(
				menuController.bottomMenu->magicItem[0] != nullptr
					&& menuController.adoptControllerPointerFocus(
						menuController.bottomMenu,
						menuController.bottomMenu->magicItem[0])
					&& menuController.bottomMenu->controllerFocusedElement()
						== menuController.bottomMenu->magicItem[0],
				resourcePack,
				"seeds the physical MagicQuick composition at its first slot")
				&& ok;
			menuController.cancelControllerInteraction();

			menuController.clearMenu();
			menuController.toggleGoodsView();
			ok = checkPack(
				menuController.goodsMenu->visible
					&& menuController.goodsMenu->isControllerFocusActive(),
				resourcePack,
				"opens a focused production Goods owner for physical input")
				&& ok;
			// The first neutral frame observes the world-to-menu transition and
			// releases input. The second clears AwaitNeutral before the A edge.
			frameDriver.runFrame();
			frameDriver.runFrame();

			const int lifeBeforeDrug = gameManager.player->life;
			const RPGPhysicalActionObservation drugUse =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				drugUse.semanticActionPressed
					&& drugUse.worldAliasPressed
					&& drugUse.semanticInputBlocked
					&& !drugUse.worldStateQueuedAfterPress
					&& gameManager.player->life
						== lifeBeforeDrug + DrugLifeRecovery
					&& !gameManager.goodsManager.goodsListExists(
						firstStoreIndex),
				resourcePack,
				"routes physical A through Goods Confirm without leaking InteractPrimary")
				&& ok;

			ok = checkPack(
				loadGoodsInfo(
					gameManager.goodsManager.goodsList[firstStoreIndex],
					PartnerHeadGoodsAFile)
					&& loadGoodsInfo(
						gameManager.goodsManager.goodsList[secondStoreIndex],
						PartnerHeadGoodsBFile),
				resourcePack,
				"loads two singleton goods for the physical X exchange")
				&& ok;
			const std::shared_ptr<Goods> firstIdentity =
				gameManager.goodsManager.goodsList[firstStoreIndex].goods;
			const std::shared_ptr<Goods> secondIdentity =
				gameManager.goodsManager.goodsList[secondStoreIndex].goods;
			menuController.goodsMenu->updateGoods();
			menuController.clearMenu();
			menuController.toggleGoodsView();
			gameManager.player->nextAction = nullptr;
			frameDriver.runFrame();

			const RPGPhysicalActionObservation pickup =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				pickup.semanticActionPressed
					&& pickup.worldAliasPressed
					&& pickup.semanticInputBlocked
					&& !pickup.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Goods)
					&& menuController.controllerTransfers().hasSource(),
				resourcePack,
				"routes the first physical X into a Goods transfer without leaking AttackPrimary")
				&& ok;

			const RPGPhysicalActionObservation navigate =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
					GameInput::InputAction::NavigateRight,
					GameInput::InputAction::Count,
					gameManager,
					frameDriver);
			ok = checkPack(
				navigate.semanticActionPressed
					&& navigate.semanticInputBlocked
					&& menuController.controllerTransfers().hasSource(),
				resourcePack,
				"routes physical D-pad right to the transfer target slot")
				&& ok;

			const RPGPhysicalActionObservation exchange =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				exchange.semanticActionPressed
					&& exchange.worldAliasPressed
					&& exchange.semanticInputBlocked
					&& !exchange.worldStateQueuedAfterPress
					&& !menuController.controllerTransfers().active()
					&& gameManager.goodsManager.goodsList[
						firstStoreIndex].goods == secondIdentity
					&& gameManager.goodsManager.goodsList[
						secondStoreIndex].goods == firstIdentity,
				resourcePack,
				"routes X-D-pad-X through a real same-bag Goods exchange")
				&& ok;

			const ControllerSlotAddress goodsQuickSource =
			{
				ControllerSlotKind::Goods,
				ControllerSlotDomain::GoodsBag,
				secondStoreIndex
			};
			const RPGPhysicalActionObservation goodsQuickPickup =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				goodsQuickPickup.semanticActionPressed
					&& goodsQuickPickup.worldAliasPressed
					&& goodsQuickPickup.semanticInputBlocked
					&& !goodsQuickPickup.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Goods)
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::GoodsBag
					&& menuController.controllerTransfers().source()
						== goodsQuickSource,
				resourcePack,
				"starts the physical GoodsQuick transfer without leaking AttackPrimary")
				&& ok;

			auto releaseRightShoulder = [&]()
			{
				RPGPhysicalActionObservation observation;
				HeadlessPhysicalInputTest::FrameCallbacks releaseCallbacks;
				releaseCallbacks.afterInputUpdate =
					[&observation](
						const GameInput::PhysicalInputManager& currentInputManager)
				{
					observation.semanticActionPressed =
						currentInputManager.wasActionPressed(
							GameInput::InputAction::NextPanel);
					observation.worldAliasPressed =
						currentInputManager.wasActionPressed(
							GameInput::InputAction::InteractAlternate);
				};
				releaseCallbacks.afterDispatch =
					[&gameManager, &observation](bool semanticInputBlocked)
				{
					observation.semanticInputBlocked = semanticInputBlocked;
					observation.worldStateQueuedAfterPress =
						gameManager.player->nextAction != nullptr;
				};
				frameDriver.tapButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
					{},
					releaseCallbacks);
				return observation;
			};

			const RPGPhysicalActionObservation goodsQuickDomainCycle =
				releaseRightShoulder();
			ok = checkPack(
				goodsQuickDomainCycle.semanticActionPressed
					&& goodsQuickDomainCycle.worldAliasPressed
					&& goodsQuickDomainCycle.semanticInputBlocked
					&& !goodsQuickDomainCycle.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::GoodsQuick
					&& menuController.controllerTransfers().source()
						== goodsQuickSource
					&& menuController.controllerTransfers().activeOwner()
						== menuController.bottomMenu
					&& menuController.bottomMenu->isControllerFocusActive()
					&& menuController.bottomMenu->goodsItem[0] != nullptr
					&& menuController.bottomMenu->goodsItem[0]->isFocused()
					&& !menuController.goodsMenu->isControllerFocusActive(),
				resourcePack,
				"routes physical RB release to GoodsQuick without leaking InteractAlternate")
				&& ok;

			const RPGPhysicalActionObservation goodsQuickNavigate =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
					GameInput::InputAction::NavigateRight,
					GameInput::InputAction::Count,
					gameManager,
					frameDriver);
			ok = checkPack(
				goodsQuickNavigate.semanticActionPressed
					&& goodsQuickNavigate.semanticInputBlocked
					&& !goodsQuickNavigate.worldStateQueuedAfterPress
					&& menuController.bottomMenu->goodsItem[1] != nullptr
					&& menuController.bottomMenu->goodsItem[1]->isFocused(),
				resourcePack,
				"moves physical D-pad focus within the GoodsQuick Bottom row")
				&& ok;

			const RPGPhysicalActionObservation goodsQuickCommit =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_SOUTH,
					GameInput::InputAction::Confirm,
					GameInput::InputAction::InteractPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				goodsQuickCommit.semanticActionPressed
					&& goodsQuickCommit.worldAliasPressed
					&& goodsQuickCommit.semanticInputBlocked
					&& !goodsQuickCommit.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Goods)
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::GoodsQuick
					&& !menuController.controllerTransfers().hasSource()
					&& !gameManager.goodsManager.goodsListExists(
						secondStoreIndex)
					&& gameManager.goodsManager.goodsList[
						gameManager.goodsManager.bottomIndex(1)].goods
						== firstIdentity,
				resourcePack,
				"commits the GoodsBag source into the focused Bottom goods slot with physical A")
				&& ok;

			const RPGPhysicalActionObservation goodsQuickExit =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_EAST,
					GameInput::InputAction::Cancel,
					GameInput::InputAction::CastSkill3,
					gameManager,
					frameDriver);
			ok = checkPack(
				goodsQuickExit.semanticActionPressed
					&& goodsQuickExit.worldAliasPressed
					&& goodsQuickExit.semanticInputBlocked
					&& !goodsQuickExit.worldStateQueuedAfterPress
					&& !menuController.controllerTransfers().active()
					&& menuController.goodsMenu->visible
					&& menuController.goodsMenu->isControllerFocusActive()
					&& !menuController.bottomMenu->isControllerFocusActive()
					&& menuController.goodsMenu->item.size() >= 2
					&& menuController.goodsMenu->item[1] != nullptr
					&& menuController.goodsMenu->item[1]->isFocused(),
				resourcePack,
				"exits GoodsQuick to the originating GoodsBag without leaking CastSkill3")
				&& ok;

			const RPGPhysicalActionObservation equipmentSourceNavigate =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_DPAD_LEFT,
					GameInput::InputAction::NavigateLeft,
					GameInput::InputAction::Count,
					gameManager,
					frameDriver);
			ok = checkPack(
				equipmentSourceNavigate.semanticActionPressed
					&& equipmentSourceNavigate.semanticInputBlocked
					&& !equipmentSourceNavigate.worldStateQueuedAfterPress
					&& menuController.goodsMenu->item[0] != nullptr
					&& menuController.goodsMenu->item[0]->isFocused(),
				resourcePack,
				"moves physical D-pad focus to the independent equipment source")
				&& ok;

			const ControllerSlotAddress equipmentSource =
			{
				ControllerSlotKind::Goods,
				ControllerSlotDomain::GoodsBag,
				firstStoreIndex
			};
			const RPGPhysicalActionObservation equipmentPickup =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				equipmentPickup.semanticActionPressed
					&& equipmentPickup.worldAliasPressed
					&& equipmentPickup.semanticInputBlocked
					&& !equipmentPickup.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Goods)
					&& menuController.controllerTransfers().source()
						== equipmentSource,
				resourcePack,
				"starts an independent physical equipment transfer without leaking AttackPrimary")
				&& ok;

			const RPGPhysicalActionObservation equipmentQuickDomainCycle =
				releaseRightShoulder();
			ok = checkPack(
				equipmentQuickDomainCycle.semanticActionPressed
					&& equipmentQuickDomainCycle.worldAliasPressed
					&& equipmentQuickDomainCycle.semanticInputBlocked
					&& !equipmentQuickDomainCycle.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::GoodsQuick
					&& menuController.controllerTransfers().source()
						== equipmentSource
					&& menuController.controllerTransfers().activeOwner()
						== menuController.bottomMenu,
				resourcePack,
				"preserves the independent equipment source through GoodsQuick")
				&& ok;

			const RPGPhysicalActionObservation equipmentDomainCycle =
				releaseRightShoulder();
			ok = checkPack(
				equipmentDomainCycle.semanticActionPressed
					&& equipmentDomainCycle.worldAliasPressed
					&& equipmentDomainCycle.semanticInputBlocked
					&& !equipmentDomainCycle.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::PlayerEquipment
					&& menuController.controllerTransfers().source()
						== equipmentSource
					&& menuController.controllerTransfers().activeOwner()
						== menuController.equipMenu
					&& menuController.equipMenu->isControllerFocusActive(),
				resourcePack,
				"routes the second physical RB release to PlayerEquipment")
				&& ok;

			const RPGPhysicalActionObservation equipmentCommit =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				equipmentCommit.semanticActionPressed
					&& equipmentCommit.worldAliasPressed
					&& equipmentCommit.semanticInputBlocked
					&& !equipmentCommit.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Goods)
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::PlayerEquipment
					&& !menuController.controllerTransfers().hasSource()
					&& !gameManager.goodsManager.goodsListExists(
						firstStoreIndex)
					&& gameManager.goodsManager.goodsList[
						headEquipmentIndex].goods == secondIdentity,
				resourcePack,
				"commits the preserved GoodsBag source to head equipment with physical X")
				&& ok;

			const RPGPhysicalActionObservation transferExit =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_EAST,
					GameInput::InputAction::Cancel,
					GameInput::InputAction::CastSkill3,
					gameManager,
					frameDriver);
			ok = checkPack(
				transferExit.semanticActionPressed
					&& transferExit.worldAliasPressed
					&& transferExit.semanticInputBlocked
					&& !transferExit.worldStateQueuedAfterPress
					&& !menuController.controllerTransfers().active()
					&& menuController.goodsMenu->visible
					&& menuController.goodsMenu->isControllerFocusActive()
					&& (menuController.equipMenu->visible
						== !gameManager.global.feature
							.hideRightMenusWithIntegratedEquip)
					&& !menuController.equipMenu->isControllerFocusActive(),
				resourcePack,
				"exits the cross-domain transfer to GoodsBag without leaking CastSkill3")
				&& ok;

			clearMagicList(gameManager);
			gameManager.magicManager.magicList[magicStoreIndex] =
				productionMagics[0];
			gameManager.magicManager.updateMenu();
			gameManager.player->nextAction = nullptr;
			const RPGPhysicalActionObservation magicMenuSwitch =
				releaseRightShoulder();
			const bool magicOwnerActive =
				gameManager.global.feature.magicButtonOpensIntegratedEquip
					? menuController.equipMenu->visible
						&& menuController.equipMenu->isControllerFocusActive()
					: menuController.magicMenu->visible
						&& menuController.magicMenu->isControllerFocusActive();
			ok = checkPack(
				magicMenuSwitch.semanticActionPressed
					&& magicMenuSwitch.worldAliasPressed
					&& magicMenuSwitch.semanticInputBlocked
					&& !magicMenuSwitch.worldStateQueuedAfterPress
					&& magicOwnerActive,
				resourcePack,
				"routes physical RB from Goods to the production MagicList owner without leaking InteractAlternate")
				&& ok;

			const ControllerSlotAddress magicSource =
			{
				ControllerSlotKind::Magic,
				ControllerSlotDomain::MagicList,
				magicStoreIndex
			};
			const RPGPhysicalActionObservation magicPickup =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				magicPickup.semanticActionPressed
					&& magicPickup.worldAliasPressed
					&& magicPickup.semanticInputBlocked
					&& !magicPickup.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Magic)
					&& menuController.controllerTransfers().source()
						== magicSource,
				resourcePack,
				"starts a physical MagicList transfer without leaking AttackPrimary")
				&& ok;

			const RPGPhysicalActionObservation magicQuickDomainCycle =
				releaseRightShoulder();
			ok = checkPack(
				magicQuickDomainCycle.semanticActionPressed
					&& magicQuickDomainCycle.worldAliasPressed
					&& magicQuickDomainCycle.semanticInputBlocked
					&& !magicQuickDomainCycle.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().activeDomain()
						== ControllerSlotDomain::MagicQuick
					&& menuController.controllerTransfers().source()
						== magicSource
					&& menuController.controllerTransfers().activeOwner()
						== menuController.bottomMenu
					&& menuController.bottomMenu->isControllerFocusActive()
					&& menuController.bottomMenu->magicItem[0] != nullptr
					&& menuController.bottomMenu->magicItem[0]->isFocused(),
				resourcePack,
				"routes physical RB release to MagicQuick without leaking InteractAlternate")
				&& ok;

			const RPGPhysicalActionObservation magicQuickNavigate =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
					GameInput::InputAction::NavigateRight,
					GameInput::InputAction::Count,
					gameManager,
					frameDriver);
			ok = checkPack(
				magicQuickNavigate.semanticActionPressed
					&& magicQuickNavigate.semanticInputBlocked
					&& menuController.bottomMenu->magicItem[1] != nullptr
					&& menuController.bottomMenu->magicItem[1]->isFocused(),
				resourcePack,
				"moves physical D-pad focus within the MagicQuick Bottom row")
				&& ok;

			const std::shared_ptr<Magic> magicIdentity =
				gameManager.magicManager.magicList[magicStoreIndex].magic;
			const RPGPhysicalActionObservation magicQuickCommit =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_WEST,
					GameInput::InputAction::Secondary,
					GameInput::InputAction::AttackPrimary,
					gameManager,
					frameDriver);
			ok = checkPack(
				magicQuickCommit.semanticActionPressed
					&& magicQuickCommit.worldAliasPressed
					&& magicQuickCommit.semanticInputBlocked
					&& !magicQuickCommit.worldStateQueuedAfterPress
					&& menuController.controllerTransfers().active(
						ControllerSlotKind::Magic)
					&& !menuController.controllerTransfers().hasSource()
					&& !gameManager.magicManager.magicListExists(
						magicStoreIndex)
					&& gameManager.magicManager.magicList[
						gameManager.magicManager.bottomIndex(1)].magic
						== magicIdentity,
				resourcePack,
				"commits the MagicList source into the focused Bottom magic slot with physical X")
				&& ok;

			const RPGPhysicalActionObservation magicTransferExit =
				tapRPGPhysicalButton(
					gamepad,
					SDL_GAMEPAD_BUTTON_EAST,
					GameInput::InputAction::Cancel,
					GameInput::InputAction::CastSkill3,
					gameManager,
					frameDriver);
			const bool magicOwnerRestored =
				gameManager.global.feature.magicButtonOpensIntegratedEquip
					? menuController.equipMenu->visible
						&& menuController.equipMenu->isControllerFocusActive()
					: menuController.magicMenu->visible
						&& menuController.magicMenu->isControllerFocusActive();
			ok = checkPack(
				magicTransferExit.semanticActionPressed
					&& magicTransferExit.worldAliasPressed
					&& magicTransferExit.semanticInputBlocked
					&& !magicTransferExit.worldStateQueuedAfterPress
					&& !menuController.controllerTransfers().active()
					&& magicOwnerRestored,
				resourcePack,
				"exits the Bottom magic transfer and restores MagicList without leaking CastSkill3")
				&& ok;
		}
	}

	menuController.clearMenu();
	gameManager.goodsManager.clearItem();
	clearMagicList(gameManager);
	gameManager.magicManager.updateMenu();
	ok = checkPack(
		resourceManager.setActiveResourcePackById(resourcePack.id),
		resourcePack,
		"restores the production resource root after physical Bottom input")
		&& ok;
	ok = videoSubsystemIsStopped(
		std::string(resourcePack.id)
			+ " physical Bottom composition finishes") && ok;
	return ok;
}
}

bool runGamepadRPGMenuActionTests()
{
	bool ok = true;
	ok = videoSubsystemIsStopped(
		"gamepad RPG menu action tests start") && ok;
	Engine::getInstance()->setWindowSize(1024, 768);
	ok = videoSubsystemIsStopped(
		"setting the headless RPG menu logical viewport completes") && ok;

	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	const std::filesystem::path fixtureRoot =
		makeUniqueTestDirectory("jxqy_gamepad_rpg_menu_actions");
	std::error_code errorCode;
	std::filesystem::remove_all(fixtureRoot, errorCode);
	if (!check(writeRPGMenuFixtures(fixtureRoot),
		"gamepad RPG menu action tests create the isolated RPG fixtures"))
	{
		return false;
	}

	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	ResourceManager& resourceManager = ResourceManager::instance();
	if (!check(resourceManager.initialize(assetsRoot.generic_string()),
		"gamepad RPG menu action tests initialize the production resource collection"))
	{
		std::filesystem::remove_all(fixtureRoot, errorCode);
		return false;
	}
	const std::vector<ResourcePackExpectation> resourcePacks =
		discoverResourcePackExpectations(resourceManager);
	ok = check(!resourcePacks.empty(),
		"gamepad RPG menu action tests discover resource directories") && ok;
	std::set<std::string> discoveredIds;
	for (const ResourcePackExpectation& resourcePack : resourcePacks)
	{
		discoveredIds.insert(resourcePack.id);
	}
	std::set<std::string> directChildIds;
	std::string directChildError;
	const bool directChildrenRead = readDirectChildResourcePackIds(
		assetsRoot, directChildIds, directChildError);
	std::string resourceSetExpectation =
		"direct-child game_profile.ini files exactly match"
		" ResourceManager::getDiscoveredPacks";
	if (!directChildrenRead || directChildIds != discoveredIds ||
		discoveredIds.size() != resourcePacks.size())
	{
		resourceSetExpectation += "; direct={" +
			describeStringSet(directChildIds) + "}; discovered={" +
			describeStringSet(discoveredIds) + "}";
		if (!directChildError.empty())
		{
			resourceSetExpectation += "; error=" + directChildError;
		}
	}
	ok = check(
		directChildrenRead && directChildIds == discoveredIds &&
			discoveredIds.size() == resourcePacks.size(),
		resourceSetExpectation) && ok;

	for (const ResourcePackExpectation& resourcePack : resourcePacks)
	{
		if (!checkPack(
			resourceManager.setActiveResourcePackById(resourcePack.id),
			resourcePack,
			"enabled resource pack can be selected for menu-input boundaries"))
		{
			ok = false;
			continue;
		}
		const ResourceManifest& manifest = resourceManager.getActiveManifest();
		ok = checkPack(
			manifest.type == resourcePack.gameType,
			resourcePack,
			"resource pack exposes the expected game type") && ok;

		{
			GameManager gameManager;
			configureGameManager(gameManager, manifest);
			gameManager.menu->init();
			ok = testSameFrameNPCPointerHit(
				resourcePack, gameManager) && ok;
			ok = testOrdinaryControlRightClickWorldIsolation(
				resourcePack, gameManager) && ok;
			ok = testNestedSystemRunClearsMenuPointerTransaction(
				resourcePack, gameManager) && ok;
			ok = testTopHUDFocusLifecycle(
				resourcePack, gameManager) && ok;
#ifdef __MOBILE__
			ok = testDeferredWorldTouchDoesNotCrossMenuOpen(
				resourcePack, gameManager) && ok;
#endif
			ok = testVisibleMenuCrossNavigation(
				resourcePack, gameManager) && ok;
		}
		ok = videoSubsystemIsStopped(
			resourcePack.id
				+ " dynamic production menu-input boundaries complete") && ok;
	}

	for (const ResourcePackExpectation& resourcePack : BaseResourcePacks)
	{
		if (!checkPack(
			resourceManager.setActiveResourcePackById(resourcePack.id),
			resourcePack,
			"base resource pack can be selected for detailed RPG actions"))
		{
			ok = false;
			continue;
		}
		const ResourceManifest& manifest = resourceManager.getActiveManifest();
		ok = checkPack(
			manifest.type == resourcePack.gameType,
			resourcePack,
			"base resource pack exposes the expected game type") && ok;

		{
			GameManager gameManager;
			configureGameManager(gameManager, manifest);
			gameManager.menu->init();
			ok = testYycsBottomColumnSeam(
				resourcePack, gameManager) && ok;
			ok = testBottomSameOwnerSpatialNavigation(
				resourcePack, gameManager) && ok;
			ok = testGoodsEquipAndUnequip(resourcePack, gameManager) && ok;
			ok = testGoodsUseAndArrange(
				resourcePack,
				resourceManager,
				gameManager,
				fixtureRoot) && ok;
			ok = testMagicActions(
				resourcePack,
				resourceManager,
				gameManager) && ok;
			ok = testPartnerActions(
				resourcePack,
				resourceManager,
				gameManager,
				fixtureRoot) && ok;
			ok = testBuySellTransactions(
				resourcePack,
				resourceManager,
				gameManager,
				fixtureRoot) && ok;
		}
		ok = videoSubsystemIsStopped(
			std::string(resourcePack.id)
				+ " production RPG menu actions complete") && ok;
	}

	for (const ResourcePackExpectation& physicalResourcePack :
		BaseResourcePacks)
	{
		if (checkPack(
			resourceManager.setActiveResourcePackById(physicalResourcePack.id),
			physicalResourcePack,
			"selects the resource pack for physical Bottom composition"))
		{
			GameManager gameManager;
			configureGameManager(
				gameManager, resourceManager.getActiveManifest());
			gameManager.menu->init();
			ok = testPhysicalBottomMenuInputComposition(
				physicalResourcePack,
				resourceManager,
				gameManager,
				fixtureRoot) && ok;
		}
		else
		{
			ok = false;
		}
	}

	for (const ResourcePackExpectation& physicalResourcePack :
		resourcePacks)
	{
		if (checkPack(
			resourceManager.setActiveResourcePackById(physicalResourcePack.id),
			physicalResourcePack,
			"selects the resource pack for physical all-menu navigation"))
		{
			GameManager gameManager;
			configureGameManager(
				gameManager, resourceManager.getActiveManifest());
			gameManager.menu->init();
			ok = testPhysicalAllVisibleMenuSpatialNavigation(
				physicalResourcePack,
				gameManager) && ok;
		}
		else
		{
			ok = false;
		}
	}

	for (const ResourcePackExpectation& physicalResourcePack :
		BaseResourcePacks)
	{
		if (checkPack(
			resourceManager.setActiveResourcePackById(physicalResourcePack.id),
			physicalResourcePack,
			"selects the resource pack for physical modal candidate"
			" navigation"))
		{
			GameManager gameManager;
			configureGameManager(
				gameManager, resourceManager.getActiveManifest());
			gameManager.menu->init();
			ok = testPhysicalBuySellModalCandidateNavigation(
				physicalResourcePack,
				resourceManager,
				gameManager,
				fixtureRoot) && ok;
			ok = testPhysicalPartnerEquipmentModalCandidateNavigation(
				physicalResourcePack,
				resourceManager,
				gameManager,
				fixtureRoot) && ok;
		}
		else
		{
			ok = false;
		}
	}

	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	ok = check(resourceManager.setActiveResourcePackById("JXQY2"),
		"gamepad RPG menu action tests restore the default resource pack") && ok;
	std::filesystem::remove_all(fixtureRoot, errorCode);
	ok = videoSubsystemIsStopped(
		"gamepad RPG menu action tests finish") && ok;
	return ok;
}
