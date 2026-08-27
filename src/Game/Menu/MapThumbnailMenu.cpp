#include "MapThumbnailMenu.h"
#include "../../Engine/Engine.h"
#include "../GameManager/GameManager.h"
#include "../Data/Map.h"
#include "../Data/NPC.h"
#include "../Data/NPCManager.h"
#include "../GameTypes.h"
#include "ControllerPromptPresenter.h"
#include "MenuResource.h"
#include "UIFocusManager.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float ControllerCursorStep = 1.0f / 32.0f;

const std::vector<ControllerPromptItem>& mapControllerPromptItems()
{
	static const std::vector<ControllerPromptItem> items =
	{
		{ GameInput::InputAction::NavigateUp, "移动准星",
			{ GameInput::InputAction::ScrollUp } },
		{ GameInput::InputAction::Confirm, "行走" },
		{ GameInput::InputAction::Secondary, "跑步" },
		{ GameInput::InputAction::Cancel, "关闭" },
		{ GameInput::InputAction::ToggleMiniMap, "关闭" },
	};
	return items;
}
}

MapThumbnailMenu::MapThumbnailMenu()
{
	name = "MapThumbnailMenu";
	visible = false;
	init();
}

MapThumbnailMenu::~MapThumbnailMenu()
{
	removeAllChild();
	freeResource();
}

void MapThumbnailMenu::init()
{
	freeResource();
	loadMenuDefinition(MenuResource::selectByMenuProfile(
		"ini\\ui\\mapthumbnail\\mapthumbnail.menu.ini",
		"ini\\ui\\littlemap\\littlemap.menu.ini",
		"ini\\ui\\littlemap\\littlemap.menu.ini"));

	thumbnailContainer = getComponentByName<ImageContainer>("thumbnailContainer");
	mapNameLabel = getComponentByName<Label>("mapNameLabel");
	closeButton = getComponentByName<Button>("closeButton");
	if (mapNameLabel != nullptr)
	{
		mapNameLabel->setStr("");
		mapNameLabel->visible = false;
	}

	if (thumbnailContainer)
	{
		thumbnailContainer->stretch = true;
		// The thumbnail is a real top-layer pointer target. This prevents a
		// visible virtual control or lower menu from acquiring the same pointer
		// before the map consumes the click through onHandleEvent().
		thumbnailContainer->coverMouse = true;
	}

	setChildRectReferToParent();
	adjustThumbnailContainerRect();
}

void MapThumbnailMenu::forceRegenerate()
{
	if (gm->map != nullptr)
	{
		gm->map->generateThumbnail();
	}
	updateThumbnail();
}

void MapThumbnailMenu::updateThumbnail()
{
	if (gm->map != nullptr)
	{
		if (currentMapName != gm->global.data.mapName)
		{
			gm->map->generateThumbnail();
			currentMapName = gm->global.data.mapName;
		}

		auto thumbnail = gm->map->getThumbnailImage();
		if (thumbnailContainer && thumbnail)
		{
			thumbnailContainer->impImage = thumbnail;
		}
	}

}

void MapThumbnailMenu::setControllerVisible(bool newVisible)
{
	if (!newVisible)
	{
		visible = false;
		deactivateControllerFocus();
		clearControllerCursor();
		return;
	}

	const bool opening = !visible;
	visible = true;
	updateThumbnail();
	if (opening || !controllerCursorActive)
	{
		initializeControllerCursor();
	}
	controllerFocusActive = controllerCursorActive;
}

void MapThumbnailMenu::toggleControllerVisible()
{
	setControllerVisible(!visible);
}

bool MapThumbnailMenu::activateControllerFocus(ControllerFocusTarget target)
{
	if (target != ControllerFocusTarget::Default)
	{
		return false;
	}
	return focusControllerElement(thumbnailContainer);
}

bool MapThumbnailMenu::isControllerFocusActive() const
{
	return controllerFocusActive
		&& isUIFocusElementAvailable(thumbnailContainer);
}

void MapThumbnailMenu::deactivateControllerFocus()
{
	controllerFocusActive = false;
}

std::shared_ptr<Element> MapThumbnailMenu::controllerFocusedElement() const
{
	return isControllerFocusActive() ? thumbnailContainer : nullptr;
}

std::vector<std::shared_ptr<Element>>
MapThumbnailMenu::controllerFocusCandidates() const
{
	if (!isUIFocusElementAvailable(thumbnailContainer))
	{
		return {};
	}
	return { thumbnailContainer };
}

bool MapThumbnailMenu::focusControllerElement(
	const std::shared_ptr<Element>& element)
{
	if (element != thumbnailContainer
		|| !isUIFocusElementAvailable(thumbnailContainer))
	{
		return false;
	}
	if (!controllerCursorActive)
	{
		initializeControllerCursor();
	}
	controllerFocusActive = controllerCursorActive;
	return controllerFocusActive;
}

bool MapThumbnailMenu::thumbnailPixelToTile(int x, int y, Point& tile)
{
	if (thumbnailContainer == nullptr || gm->map == nullptr || gm->map->data == nullptr)
	{
		return false;
	}
	if (!thumbnailContainer->rect.PointInRect(x, y))
	{
		return false;
	}

	int mapWidth = gm->map->data->head.width;
	int mapHeight = gm->map->data->head.height;
	if (mapWidth <= 0 || mapHeight <= 0)
	{
		return false;
	}

	Rect sourceRect = gm->map->getThumbnailSourceRect();
	if (sourceRect.w <= 0 || sourceRect.h <= 0)
	{
		int tilePixelWidth = mapWidth * TILE_WIDTH;
		int tilePixelHeight = (mapHeight - 1) * TILE_HEIGHT / 2;
		sourceRect = { TILE_WIDTH, TILE_HEIGHT, tilePixelWidth - TILE_WIDTH / 2, tilePixelHeight - TILE_HEIGHT / 2 };
	}
	if (sourceRect.w <= 0 || sourceRect.h <= 0 || thumbnailContainer->rect.w <= 0 || thumbnailContainer->rect.h <= 0)
	{
		return false;
	}

	float sourceX = (float)sourceRect.x + (float)(x - thumbnailContainer->rect.x) * (float)sourceRect.w / (float)thumbnailContainer->rect.w;
	float sourceY = (float)sourceRect.y + (float)(y - thumbnailContainer->rect.y) * (float)sourceRect.h / (float)thumbnailContainer->rect.h;
	tile = Map::getMousePosition({ (int)std::round(sourceX), (int)std::round(sourceY) }, { 0, 0 }, { TILE_WIDTH, TILE_HEIGHT }, { 0.0, 0.0 });
	tile.x = std::max(0, std::min(tile.x, mapWidth - 1));
	tile.y = std::max(0, std::min(tile.y, mapHeight - 1));
	return true;
}

bool MapThumbnailMenu::queueMovementAtThumbnailPixel(
	int x, int y, bool running)
{
	if (!canQueueWorldMovement())
	{
		return false;
	}

	Point target;
	if (!thumbnailPixelToTile(x, y, target))
	{
		return false;
	}
	setControllerCursorFromPixel(x, y);
	return queueMovementToTile(target, running);
}

bool MapThumbnailMenu::queueMovementToTile(Point target, bool running)
{
	if (!canQueueWorldMovement())
	{
		return false;
	}

	NextAction action;
	action.action = running ? acRun : acWalk;
	action.dest = target;
	gm->player->addNextAction(action);
	return true;
}

bool MapThumbnailMenu::canQueueWorldMovement() const
{
	if (!visible || gm == nullptr || gm->player == nullptr
		|| gm->menu == nullptr || gm->menu->mapThumbnailMenu.get() != this
		|| Element::isFrameSemanticInputBlocked()
		|| !gm->global.data.canInput || gm->inEvent
		|| gm->isGameplayPaused())
	{
		return false;
	}
	const auto actionActor = gm->player->getActionActor();
	return actionActor != nullptr && actionActor->nowAction != acDeath
		&& actionActor->nowAction != acHide;
}

bool MapThumbnailMenu::onHandleEvent(AEvent& e)
{
	if (!visible || gm == nullptr || gm->player == nullptr || !gm->global.data.canInput)
	{
		return false;
	}
	if (e.eventType != ET_MOUSEDOWN && e.eventType != ET_FINGERDOWN)
	{
		return false;
	}
	if (e.eventType == ET_MOUSEDOWN && e.eventData != MBC_MOUSE_LEFT)
	{
		return false;
	}

	// A thumbnail click is a long-distance travel request. Default it to run;
	// Player::addNextAction still downgrades safely when running is unavailable.
	const bool running = true;
	const bool handled = queueMovementAtThumbnailPixel(
		e.eventX, e.eventY, running);
	if (handled && gm->menu != nullptr)
	{
		gm->menu->adoptControllerPointerFocus(
			getMySharedPtr(), thumbnailContainer);
	}
	return handled;
}

bool MapThumbnailMenu::onHandleUIAction(UIAction action)
{
	if (!visible || !controllerFocusActive)
	{
		return false;
	}

	switch (action)
	{
	case UIAction::NavigateUp:
	case UIAction::ScrollUp:
		if (!controllerCursorActive)
		{
			initializeControllerCursor();
		}
		return moveControllerCursor(0.0f, -ControllerCursorStep);
	case UIAction::NavigateDown:
	case UIAction::ScrollDown:
		if (!controllerCursorActive)
		{
			initializeControllerCursor();
		}
		return moveControllerCursor(0.0f, ControllerCursorStep);
	case UIAction::NavigateLeft:
	case UIAction::ScrollLeft:
		if (!controllerCursorActive)
		{
			initializeControllerCursor();
		}
		return moveControllerCursor(-ControllerCursorStep, 0.0f);
	case UIAction::NavigateRight:
	case UIAction::ScrollRight:
		if (!controllerCursorActive)
		{
			initializeControllerCursor();
		}
		return moveControllerCursor(ControllerCursorStep, 0.0f);
	case UIAction::Confirm:
	case UIAction::Secondary:
	{
		if (!controllerCursorActive)
		{
			initializeControllerCursor();
		}
		const Point cursor = getControllerCursorPixel();
		queueMovementAtThumbnailPixel(
			cursor.x, cursor.y, action == UIAction::Secondary);
		return true;
	}
	case UIAction::Cancel:
		if (gm != nullptr && gm->menu != nullptr)
		{
			gm->menu->setMapThumbnailVisible(false);
		}
		else
		{
			setControllerVisible(false);
		}
		return true;
	case UIAction::Details:
		return true;
	case UIAction::PanelPrevious:
	case UIAction::PanelNext:
	case UIAction::PagePrevious:
	case UIAction::PageNext:
	default:
		return false;
	}
}

void MapThumbnailMenu::onEvent()
{
	if (closeButton && closeButton->getResult(erClick))
	{
		if (gm != nullptr && gm->menu != nullptr)
		{
			gm->menu->setMapThumbnailVisible(false);
		}
		else
		{
			setControllerVisible(false);
		}
	}
}

void MapThumbnailMenu::onUpdate()
{
	if (!visible)
	{
		clearControllerCursor();
		return;
	}

	if (thumbnailContainer && (thumbnailContainer->impImage == nullptr || currentMapName != gm->global.data.mapName))
	{
		updateThumbnail();
	}
}

void MapThumbnailMenu::onDrawEnd()
{
	if (!visible || thumbnailContainer == nullptr || gm->map == nullptr)
	{
		return;
	}

	drawEntityMarkers();
	const bool presentControllerFocus =
		controllerFocusActive && shouldPresentGamepadFocus(engine);
	if (presentControllerFocus)
	{
		drawControllerCursor();
	}
	if (!presentControllerFocus)
	{
		return;
	}
	if (!ControllerPromptPresenter::canPresentForOwner(
		this, ControllerPromptOwnerPolicy::ActiveNonModalOwner))
	{
		return;
	}

	float scaleX = 1.0f;
	float scaleY = 1.0f;
	getChildScaleFactor(scaleX, scaleY);
	ControllerPromptDrawOptions promptOptions;
	promptOptions.x = thumbnailContainer->rect.x + 4;
	promptOptions.y = thumbnailContainer->rect.y
		+ std::max(0, thumbnailContainer->rect.h
			- std::max(42, static_cast<int>(std::round(50.0f * scaleY))));
	promptOptions.width = std::max(1, thumbnailContainer->rect.w - 8);
	promptOptions.height = std::min(thumbnailContainer->rect.h,
		std::max(42, static_cast<int>(std::round(50.0f * scaleY))));
	promptOptions.fontSize = std::clamp(
		static_cast<int>(std::round(14.0f * std::min(scaleX, scaleY))), 11, 17);
	promptOptions.itemGap = std::max(8,
		static_cast<int>(std::round(12.0f * scaleX)));
	ControllerPromptPresenter::draw(
		engine, engine->inputActions(), mapControllerPromptItems(), promptOptions);
}

void MapThumbnailMenu::onWindowResize(int width, int height)
{
	const bool cursorWasActive = controllerCursorActive;
	const float savedCursorX = controllerCursorNormalizedX;
	const float savedCursorY = controllerCursorNormalizedY;
	ConfigDrivenPanel::onWindowResize(width, height);
	if (!visible)
	{
		return;
	}
	updateThumbnail();
	if (cursorWasActive && thumbnailContainer != nullptr
		&& thumbnailContainer->rect.w > 0 && thumbnailContainer->rect.h > 0)
	{
		controllerCursorNormalizedX = std::clamp(savedCursorX, 0.0f, 1.0f);
		controllerCursorNormalizedY = std::clamp(savedCursorY, 0.0f, 1.0f);
		controllerCursorActive = true;
	}
	else
	{
		initializeControllerCursor();
	}
}

Rect MapThumbnailMenu::getScaledPanelRect(int x, int y, int width, int height)
{
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	getChildScaleFactor(scaleX, scaleY);

	return {
		rect.x + (int)std::round((float)x * scaleX),
		rect.y + (int)std::round((float)y * scaleY),
		std::max(1, (int)std::round((float)width * scaleX)),
		std::max(1, (int)std::round((float)height * scaleY))
	};
}

Rect MapThumbnailMenu::expandAndClampRect(Rect target, int marginX, int marginY, Rect bounds)
{
	target.x -= marginX;
	target.y -= marginY;
	target.w += marginX * 2;
	target.h += marginY * 2;

	if (bounds.w <= 0 || bounds.h <= 0)
	{
		return target;
	}

	int left = std::max(target.x, bounds.x);
	int top = std::max(target.y, bounds.y);
	int right = std::min(target.x + target.w, bounds.x + bounds.w);
	int bottom = std::min(target.y + target.h, bounds.y + bounds.h);

	target.x = left;
	target.y = top;
	target.w = std::max(1, right - left);
	target.h = std::max(1, bottom - top);

	return target;
}

void MapThumbnailMenu::adjustThumbnailContainerRect()
{
	if (thumbnailContainer == nullptr || gm == nullptr)
	{
		return;
	}

	float scaleX = 1.0f;
	float scaleY = 1.0f;
	getChildScaleFactor(scaleX, scaleY);

	if (gm->global.feature.mapThumbnailLayout == mtlpYycs)
	{
		Rect nativeMapRect = getScaledPanelRect(160, 120, 320, 240);
		int marginX = std::max(1, (int)std::round(8.0f * scaleX));
		int marginY = std::max(1, (int)std::round(8.0f * scaleY));
		thumbnailContainer->rect = expandAndClampRect(nativeMapRect, marginX, marginY, rect);
	}
	else if (gm->global.feature.mapThumbnailLayout == mtlpXjxqy)
	{
		int marginX = std::max(1, (int)std::round(12.0f * scaleX));
		int marginY = std::max(1, (int)std::round(12.0f * scaleY));
		thumbnailContainer->rect = expandAndClampRect(thumbnailContainer->rect, marginX, marginY, rect);
	}
}

void MapThumbnailMenu::initializeControllerCursor()
{
	controllerCursorNormalizedX = 0.5f;
	controllerCursorNormalizedY = 0.5f;
	controllerCursorActive = thumbnailContainer != nullptr
		&& thumbnailContainer->rect.w > 0
		&& thumbnailContainer->rect.h > 0;
	if (!controllerCursorActive || gm == nullptr || gm->player == nullptr
		|| gm->map == nullptr || gm->map->data == nullptr)
	{
		return;
	}

	const Point playerPixel = tileToThumbnailPixel(
		gm->player->getPosition(), gm->player->getOffset());
	if (thumbnailContainer->rect.PointInRect(playerPixel.x, playerPixel.y))
	{
		setControllerCursorFromPixel(playerPixel.x, playerPixel.y);
	}
}

void MapThumbnailMenu::clearControllerCursor()
{
	controllerCursorNormalizedX = 0.5f;
	controllerCursorNormalizedY = 0.5f;
	controllerCursorActive = false;
}

bool MapThumbnailMenu::moveControllerCursor(float deltaX, float deltaY)
{
	if (!controllerCursorActive)
	{
		return false;
	}
	const float previousX = controllerCursorNormalizedX;
	const float previousY = controllerCursorNormalizedY;
	controllerCursorNormalizedX = std::clamp(
		controllerCursorNormalizedX + deltaX, 0.0f, 1.0f);
	controllerCursorNormalizedY = std::clamp(
		controllerCursorNormalizedY + deltaY, 0.0f, 1.0f);
	return controllerCursorNormalizedX != previousX
		|| controllerCursorNormalizedY != previousY;
}

void MapThumbnailMenu::setControllerCursorFromPixel(int pixelX, int pixelY)
{
	if (thumbnailContainer == nullptr || thumbnailContainer->rect.w <= 0
		|| thumbnailContainer->rect.h <= 0)
	{
		return;
	}
	const Rect& bounds = thumbnailContainer->rect;
	const int horizontalRange = std::max(1, bounds.w - 1);
	const int verticalRange = std::max(1, bounds.h - 1);
	controllerCursorNormalizedX = std::clamp(
		static_cast<float>(pixelX - bounds.x)
			/ static_cast<float>(horizontalRange),
		0.0f,
		1.0f);
	controllerCursorNormalizedY = std::clamp(
		static_cast<float>(pixelY - bounds.y)
			/ static_cast<float>(verticalRange),
		0.0f,
		1.0f);
}

Point MapThumbnailMenu::getControllerCursorPixel() const
{
	if (thumbnailContainer == nullptr || thumbnailContainer->rect.w <= 0
		|| thumbnailContainer->rect.h <= 0)
	{
		return { 0, 0 };
	}
	const Rect& bounds = thumbnailContainer->rect;
	return
	{
		bounds.x + static_cast<int>(std::round(
			controllerCursorNormalizedX * static_cast<float>(
				std::max(0, bounds.w - 1)))),
		bounds.y + static_cast<int>(std::round(
			controllerCursorNormalizedY * static_cast<float>(
				std::max(0, bounds.h - 1))))
	};
}

Point MapThumbnailMenu::tileToThumbnailPixel(Point tile, PointEx offset)
{
	if (gm->map == nullptr || gm->map->data == nullptr)
	{
		return { 0, 0 };
	}

	int mapWidth = gm->map->data->head.width;
	int mapHeight = gm->map->data->head.height;
	if (mapWidth <= 0 || mapHeight <= 0)
	{
		return { 0, 0 };
	}

	int paddingX = TILE_WIDTH;
	int paddingY = TILE_HEIGHT;

	Point cenScreen = { paddingX, paddingY };
	Point cenTile = { 0, 0 };
	PointEx tileOffset = { 0.0, 0.0 };

	PointEx canvasPixel = Map::getTilePositionEx(tile, cenTile, cenScreen, tileOffset);
	canvasPixel.x += offset.x;
	canvasPixel.y += offset.y;

	Rect sourceRect = gm->map->getThumbnailSourceRect();
	if (sourceRect.w <= 0 || sourceRect.h <= 0)
	{
		int tilePixelWidth = mapWidth * TILE_WIDTH;
		int tilePixelHeight = (mapHeight - 1) * TILE_HEIGHT / 2;
		sourceRect = { paddingX, paddingY, tilePixelWidth - TILE_WIDTH / 2, tilePixelHeight - TILE_HEIGHT / 2 };
	}

	if (sourceRect.w <= 0 || sourceRect.h <= 0)
	{
		return { thumbnailContainer->rect.x, thumbnailContainer->rect.y };
	}

	float scaleX = (float)thumbnailContainer->rect.w / (float)sourceRect.w;
	float scaleY = (float)thumbnailContainer->rect.h / (float)sourceRect.h;

	int pixelX = thumbnailContainer->rect.x + (int)std::round((canvasPixel.x - (float)sourceRect.x) * scaleX);
	int pixelY = thumbnailContainer->rect.y + (int)std::round((canvasPixel.y - (float)sourceRect.y) * scaleY);

	return { pixelX, pixelY };
}

void MapThumbnailMenu::drawMarker(int pixelX, int pixelY, int radius, float r, float g, float b, float a)
{
	if (a <= 0.0f)
	{
		return;
	}

	Vertex vertices[4];
	SDL_FPoint positions[4] =
	{
		{(float)(pixelX - radius), (float)(pixelY - radius)},
		{(float)(pixelX + radius), (float)(pixelY - radius)},
		{(float)(pixelX + radius), (float)(pixelY + radius)},
		{(float)(pixelX - radius), (float)(pixelY + radius)},
	};
	for (int k = 0; k < 4; k++)
	{
		vertices[k].position = positions[k];
		vertices[k].tex_coord = { 0, 0 };
		vertices[k].color = { r, g, b, a };
	}
	std::vector<Vertex> vertexVec(vertices, vertices + 4);
	std::vector<int> indices = { 0, 1, 2, 0, 2, 3 };
	engine->drawGeometry(nullptr, vertexVec, indices);
}

float MapThumbnailMenu::getMarkerEdgeAlpha(int pixelX, int pixelY) const
{
	if (thumbnailContainer == nullptr || thumbnailContainer->rect.w <= 0 || thumbnailContainer->rect.h <= 0)
	{
		return 0.0f;
	}

	const Rect& bounds = thumbnailContainer->rect;
	const float featherX = std::max(1.0f,
		static_cast<float>(bounds.w) * MapThumbnailStyle::FeatherPixels / MapThumbnailStyle::CanvasWidth);
	const float featherY = std::max(1.0f,
		static_cast<float>(bounds.h) * MapThumbnailStyle::FeatherPixels / MapThumbnailStyle::CanvasHeight);
	const float distanceX = std::min(
		static_cast<float>(pixelX - bounds.x),
		static_cast<float>(bounds.x + bounds.w - 1 - pixelX));
	const float distanceY = std::min(
		static_cast<float>(pixelY - bounds.y),
		static_cast<float>(bounds.y + bounds.h - 1 - pixelY));
	const float alphaX = std::clamp(distanceX / featherX, 0.0f, 1.0f);
	const float alphaY = std::clamp(distanceY / featherY, 0.0f, 1.0f);
	return std::min(alphaX, alphaY);
}

void MapThumbnailMenu::drawControllerCursor()
{
	if (!controllerCursorActive || thumbnailContainer == nullptr
		|| thumbnailContainer->rect.w <= 0
		|| thumbnailContainer->rect.h <= 0)
	{
		return;
	}

	const Rect& bounds = thumbnailContainer->rect;
	const Point cursor = getControllerCursorPixel();
	const int radius = std::clamp(
		std::min(bounds.w, bounds.h) / 32, 5, 10);
	auto fillClamped = [this, &bounds](
		Rect target,
		unsigned char red,
		unsigned char green,
		unsigned char blue,
		unsigned char alpha)
	{
		const int left = std::max(target.x, bounds.x);
		const int top = std::max(target.y, bounds.y);
		const int right = std::min(
			target.x + target.w, bounds.x + bounds.w);
		const int bottom = std::min(
			target.y + target.h, bounds.y + bounds.h);
		if (right > left && bottom > top)
		{
			engine->fillRect(
				left, top, right - left, bottom - top,
				red, green, blue, alpha);
		}
	};

	fillClamped(
		{ cursor.x - radius, cursor.y - 1, radius * 2 + 1, 3 },
		0, 0, 0, 220);
	fillClamped(
		{ cursor.x - 1, cursor.y - radius, 3, radius * 2 + 1 },
		0, 0, 0, 220);
	fillClamped(
		{ cursor.x - radius, cursor.y, radius * 2 + 1, 1 },
		255, 220, 92, 255);
	fillClamped(
		{ cursor.x, cursor.y - radius, 1, radius * 2 + 1 },
		255, 220, 92, 255);
}

void MapThumbnailMenu::drawEntityMarkers()
{
	if (gm->player != nullptr)
	{
		Point playerPos = tileToThumbnailPixel(gm->player->getPosition(), gm->player->getOffset());
		drawMarker(playerPos.x, playerPos.y, 4, 0.0f, 1.0f, 0.0f,
			getMarkerEdgeAlpha(playerPos.x, playerPos.y));
	}

	if (gm->npcManager != nullptr)
	{
		for (auto& npc : gm->npcManager->npcList)
		{
			if (npc == nullptr || npc == gm->player)
			{
				continue;
			}

			Point npcPos = tileToThumbnailPixel(npc->getPosition(), npc->getOffset());
			const float markerAlpha = getMarkerEdgeAlpha(npcPos.x, npcPos.y);

			switch (npc->relation)
			{
			case nrFriendly:
				drawMarker(npcPos.x, npcPos.y, 3, 0.0f, 0.8f, 1.0f, markerAlpha);
				break;
			case nrHostile:
				drawMarker(npcPos.x, npcPos.y, 3, 1.0f, 0.0f, 0.0f, markerAlpha);
				break;
			case nrNeutral:
				drawMarker(npcPos.x, npcPos.y, 3, 1.0f, 1.0f, 0.0f, markerAlpha);
				break;
			default:
				drawMarker(npcPos.x, npcPos.y, 3, 0.7f, 0.7f, 0.7f, markerAlpha);
				break;
			}
		}
	}
}

void MapThumbnailMenu::freeResource()
{
	clearControllerCursor();
	thumbnailContainer = nullptr;
	mapNameLabel = nullptr;
	closeButton = nullptr;
	currentMapName.clear();
	ConfigDrivenPanel::freeResource();
}
