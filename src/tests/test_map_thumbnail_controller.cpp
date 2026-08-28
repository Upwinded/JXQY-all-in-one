#include "../Game/Menu/MapThumbnailMenu.h"
#include "../Engine/Engine.h"
#include "../Game/Menu/MsgBox.h"
#include "../Game/Menu/UIFocusManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../File/File.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}
}

class MapThumbnailMenuTestAccess
{
public:
	static void prepare(MapThumbnailMenu& menu, const Rect& bounds)
	{
		menu.thumbnailContainer = std::make_shared<ImageContainer>();
		menu.thumbnailContainer->rect = bounds;
		menu.visible = true;
		menu.controllerCursorNormalizedX = 0.5f;
		menu.controllerCursorNormalizedY = 0.5f;
		menu.controllerCursorActive = true;
		menu.controllerFocusActive = true;
	}

	static bool dispatch(MapThumbnailMenu& menu, UIAction action)
	{
		return menu.onHandleUIAction(action);
	}

	static bool dispatchPointer(MapThumbnailMenu& menu, AEvent event)
	{
		return menu.onHandleEvent(event);
	}

	static Point cursorPixel(const MapThumbnailMenu& menu)
	{
		return menu.getControllerCursorPixel();
	}

	static void setBounds(MapThumbnailMenu& menu, const Rect& bounds)
	{
		menu.thumbnailContainer->rect = bounds;
	}

	static float cursorX(const MapThumbnailMenu& menu)
	{
		return menu.controllerCursorNormalizedX;
	}

	static bool cursorActive(const MapThumbnailMenu& menu)
	{
		return menu.controllerCursorActive;
	}

	static bool mapNameHidden(const MapThumbnailMenu& menu)
	{
		return menu.mapNameLabel != nullptr
			&& !menu.mapNameLabel->visible;
	}

	static bool queueMovement(
		MapThumbnailMenu& menu, Point target, bool running)
	{
		return menu.queueMovementToTile(target, running);
	}
};

bool runMapThumbnailControllerTests()
{
	bool ok = true;
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot((assetsRoot / "jxqy2").generic_string());
	File::setCommonResourceRoot((assetsRoot / "common").generic_string());
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots(
		{}, true, (assetsRoot / "common").generic_string());
	MapThumbnailMenu menu;
	ok = check(MapThumbnailMenuTestAccess::mapNameHidden(menu),
		"map thumbnail still exposes its configured map-name label") && ok;
	MapThumbnailMenuTestAccess::prepare(menu, { 10, 20, 321, 161 });

	Point cursor = MapThumbnailMenuTestAccess::cursorPixel(menu);
	ok = check(cursor.x == 170 && cursor.y == 100,
		"map cursor starts at the normalized thumbnail center") && ok;
	ok = check(MapThumbnailMenuTestAccess::dispatch(
		menu, UIAction::NavigateRight),
		"map cursor consumes semantic navigation") && ok;
	cursor = MapThumbnailMenuTestAccess::cursorPixel(menu);
	ok = check(cursor.x == 180 && cursor.y == 100,
		"map cursor advances by one normalized controller step") && ok;
	ok = check(MapThumbnailMenuTestAccess::dispatch(
		menu, UIAction::ScrollLeft),
		"map cursor consumes independent right-stick scroll semantics") && ok;
	cursor = MapThumbnailMenuTestAccess::cursorPixel(menu);
	ok = check(cursor.x == 170 && cursor.y == 100,
		"right-stick scroll semantics reuse normalized cursor movement") && ok;

	for (int index = 0; index < 64; index++)
	{
		MapThumbnailMenuTestAccess::dispatch(
			menu, UIAction::NavigateRight);
	}
	ok = check(std::abs(MapThumbnailMenuTestAccess::cursorX(menu) - 1.0f)
		< 0.0001f,
		"map cursor clamps at the thumbnail boundary") && ok;
	ok = check(!MapThumbnailMenuTestAccess::dispatch(
		menu, UIAction::NavigateRight),
		"map cursor releases directional navigation at its boundary") && ok;
	ok = check(!shouldPresentGamepadFocus(Engine::getInstance()),
		"map cursor requested gamepad presentation without an active gamepad")
		&& ok;
	MapThumbnailMenuTestAccess::setBounds(menu, { 100, 50, 641, 321 });
	cursor = MapThumbnailMenuTestAccess::cursorPixel(menu);
	ok = check(cursor.x == 740 && cursor.y == 210,
		"normalized cursor position survives thumbnail resizing") && ok;

	ok = check(MapThumbnailMenuTestAccess::dispatch(menu, UIAction::Confirm),
		"map confirm is consumed even when no runtime map is available") && ok;
	ok = check(MapThumbnailMenuTestAccess::dispatch(menu, UIAction::Details),
		"map details action is consumed without moving the world") && ok;
	ok = check(MapThumbnailMenuTestAccess::dispatch(menu, UIAction::Cancel)
		&& !menu.visible && !MapThumbnailMenuTestAccess::cursorActive(menu),
		"map cancel closes the overlay and clears its cursor") && ok;

	{
		GameManager gameManager;
		gameManager.global.data.canInput = true;
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = 3;
		gameManager.map->data->head.height = 3;
		gameManager.map->data->tile.resize(3);
		for (auto& row : gameManager.map->data->tile)
		{
			row.resize(3);
		}
		gameManager.map->createDataMap();
		gameManager.player->canRun = true;
		gameManager.player->thew = 100;
		gameManager.player->info.thewMax = 100;
		gameManager.menu->messageBox = std::make_shared<MsgBox>();

		auto runtimeMenu = std::make_shared<MapThumbnailMenu>();
		gameManager.menu->mapThumbnailMenu = runtimeMenu;
		MapThumbnailMenuTestAccess::prepare(
			*runtimeMenu, { 10, 20, 321, 161 });
		ok = check(MapThumbnailMenuTestAccess::dispatchPointer(
			*runtimeMenu,
			AEvent(ET_FINGERDOWN, 11, 170, 100, false))
			&& gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->action == acRun,
			"map pointer travel defaults to running across shared game profiles") && ok;
		gameManager.player->nextAction = nullptr;
		ok = check(MapThumbnailMenuTestAccess::queueMovement(
			*runtimeMenu, { 1, 1 }, false)
			&& gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->action == acWalk,
			"map confirm queues the shared walking semantic action") && ok;
		gameManager.player->nextAction = nullptr;
		ok = check(MapThumbnailMenuTestAccess::queueMovement(
			*runtimeMenu, { 1, 1 }, true)
			&& gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->action == acRun,
			"map secondary action queues the shared running semantic action") && ok;
		gameManager.player->nextAction = nullptr;
		gameManager.player->thew = 0;
		ok = check(MapThumbnailMenuTestAccess::queueMovement(
			*runtimeMenu, { 1, 1 }, true)
			&& gameManager.player->nextAction != nullptr
			&& gameManager.player->nextAction->action == acWalk
			&& gameManager.menu->messageBox->currentMessage == "体力不足!",
			"an unaffordable explicit run reports low stamina before walking") && ok;
		gameManager.player->thew = 100;

		gameManager.player->nextAction = nullptr;
		gameManager.inEvent = true;
		ok = check(!MapThumbnailMenuTestAccess::queueMovement(
			*runtimeMenu, { 1, 1 }, false)
			&& gameManager.player->nextAction == nullptr,
			"map input cannot queue world movement during a story event") && ok;
		gameManager.inEvent = false;
		gameManager.setGameplayPaused(true);
		ok = check(!MapThumbnailMenuTestAccess::queueMovement(
			*runtimeMenu, { 1, 1 }, true)
			&& gameManager.player->nextAction == nullptr,
			"map input cannot queue world movement while gameplay is paused") && ok;
		gameManager.setGameplayPaused(false);
		gameManager.player->nowAction = acDeath;
		ok = check(!MapThumbnailMenuTestAccess::queueMovement(
			*runtimeMenu, { 1, 1 }, false)
			&& gameManager.player->nextAction == nullptr,
			"map input cannot queue movement for a dead action actor") && ok;
		gameManager.player->nowAction = acStand;

		MapThumbnailMenu orphanMenu;
		MapThumbnailMenuTestAccess::prepare(
			orphanMenu, { 10, 20, 321, 161 });
		ok = check(!MapThumbnailMenuTestAccess::queueMovement(
			orphanMenu, { 1, 1 }, false)
			&& gameManager.player->nextAction == nullptr,
			"only the active map overlay owner can queue world movement") && ok;
	}
	return ok;
}
