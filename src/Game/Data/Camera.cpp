#include "Camera.h"
#include "../../Engine/Engine.h"
#include "Map.h"
#include "../GameManager/GameManager.h"

Camera::Camera()
{
	setPriority(epCamera);
	needEvents = true;
}


Camera::~Camera()
{
}

void Camera::flyTo(int dir, int distance, int speed)
{
	if (distance > 0)
	{
		setFlyTo(dir, distance, speed);
		run();
	}
}

void Camera::flyToEx(int dir, int distance, int speed)
{
	if (distance > 0)
	{
		setFlyTo(dir, distance, speed);
	}
}

void Camera::flyToPosition(int x, int y, int speed)
{
	followPlayer = false;
	clearDirectionalFrameMove();
	distanceToFly = { 0.0, 0.0 };
	distanceFlied = { 0.0, 0.0 };
	flyStartPosition = position;
	flyStartOffset = offset;
	flySpeed = normalizeFlySpeed(speed);
	Point targetPixel = Map::getTilePosition({ x, y }, position, { 0, 0 }, offset);
	distanceToFly.x = (float)targetPixel.x;
	distanceToFly.y = (float)targetPixel.y;
	if (isZeroFlightDistance(distanceToFly))
	{
		flying = false;
		logicRunning = false;
		return;
	}
	flying = true;
}

void Camera::setFlyTo(int dir, int distance, int speed)
{
	followPlayer = false;
	clearDirectionalFrameMove();
	distanceToFly = { 0.0, 0.0 };
	distanceFlied = { 0.0, 0.0 };
	flyStartPosition = position;
	flyStartOffset = offset;
	flyDirection = normalizeDir(dir);
	flySpeed = normalizeFlySpeed(speed);
	static const PointEx dirOffset[8] = {
		{ 0.0,  1.0},
		{-0.5,  0.5},
		{-0.5,  0.0},
		{-0.5, -0.5},
		{ 0.0, -1.0},
		{ 0.5, -0.5},
		{ 0.5,  0.0},
		{ 0.5,  0.5}
	};
	distanceToFly.x = dirOffset[flyDirection].x * flyRatio * distance * TILE_WIDTH;
	distanceToFly.y = dirOffset[flyDirection].y * flyRatio * distance * TILE_HEIGHT;
	flying = true;
}

void Camera::moveForFrameCount(int dir, int frameCount, int speed)
{
	if (frameCount <= 0)
	{
		return;
	}

	followPlayer = false;
	flying = false;
	distanceToFly = { 0.0, 0.0 };
	distanceFlied = { 0.0, 0.0 };
	directionalMoveDirection = dir >= 0 && dir <= 7 ? dir : 0;
	directionalMoveFramesRemaining = frameCount;
	directionalMoveSpeed = speed;
	directionalMoveRemainder = { 0.0, 0.0 };
	run();
}

void Camera::setFollowPlayer()
{
	followPlayer = true;
}

void Camera::snapToFollowTarget()
{
	if (gm == nullptr || gm->map == nullptr || gm->map->data == nullptr)
	{
		return;
	}

	std::shared_ptr<NPC> npcPlayer = gm->player;
	auto followNPCPtr = followNPC.lock();
	if (followNPCPtr && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(followNPCPtr)))
	{
		npcPlayer = std::dynamic_pointer_cast<NPC>(followNPCPtr);
	}
	else
	{
		followNPC.reset();
	}
	if (npcPlayer == nullptr)
	{
		return;
	}

	position = npcPlayer->getPosition();
	offset = npcPlayer->getOffset();
	clampToMapBounds();
}

void Camera::clampToMapBounds()
{
	if (gm == nullptr || gm->map == nullptr || gm->map->data == nullptr)
	{
		return;
	}
	int mapw = gm->map->data->head.width;
	int maph = gm->map->data->head.height;
	if (mapw <= 0 || maph <= 0)
	{
		return;
	}

	int w, h;
	engine->getWindowSize(w, h);
	constexpr int topBoundaryTileMargin = 1;

	auto clampFloat = [](float value, float minValue, float maxValue) {
		if (value < minValue)
		{
			return minValue;
		}
		if (value > maxValue)
		{
			return maxValue;
		}
		return value;
	};
	auto clampInt = [](int value, int minValue, int maxValue) {
		if (value < minValue)
		{
			return minValue;
		}
		if (value > maxValue)
		{
			return maxValue;
		}
		return value;
	};

	PointEx cameraWorldPosition = Map::getTilePositionEx(position, { 0, 0 }, { 0, 0 }, { 0, 0 }) + offset;

	int hscal = h / TILE_HEIGHT * 2 + 2;
	if (hscal + 1 > maph)
	{
		// Center rows 0..maph-1 in world Y; avoid integer truncation on odd heights.
		cameraWorldPosition.y = (float)(maph - 1) * ((float)TILE_HEIGHT / 2.0f) / 2.0f;
	}
	else
	{
		int line2 = std::abs(hscal / 2 - 1) % 2;
		int line3 = std::abs(maph - hscal / 2 - 2) % 2;
		float minWorldY = (float)(hscal / 2 - 1 - line2) * ((float)TILE_HEIGHT / 2.0f);
		float maxWorldY = (float)(maph - hscal / 2 - 2 - line3) * ((float)TILE_HEIGHT / 2.0f);
		minWorldY += (float)topBoundaryTileMargin * ((float)TILE_HEIGHT / 2.0f);
		if (maxWorldY < minWorldY)
		{
			cameraWorldPosition.y = (minWorldY + maxWorldY) / 2.0f;
		}
		else
		{
			cameraWorldPosition.y = clampFloat(cameraWorldPosition.y, minWorldY, maxWorldY);
		}
	}
	position.y = clampInt((int)round(cameraWorldPosition.y / ((float)TILE_HEIGHT / 2.0f)), 0, maph > 0 ? maph - 1 : 0);
	offset.y = cameraWorldPosition.y - (float)position.y * ((float)TILE_HEIGHT / 2.0f);

	int wscal = w / TILE_WIDTH + 1;
	if (wscal + 2 > mapw)
	{
		// Center the visual bounds. Multi-row maps include the odd-row TILE_WIDTH/2 shift;
		// a single-row map has no odd-row extension and is centered by columns.
		if (maph <= 1)
		{
			cameraWorldPosition.x = (float)(mapw - 1) * (float)TILE_WIDTH / 2.0f;
		}
		else
		{
			cameraWorldPosition.x = ((float)mapw * (float)TILE_WIDTH - (float)TILE_WIDTH / 2.0f) / 2.0f;
		}
	}
	else
	{
		// Odd map rows start half a tile to the right. Move the viewport far
		// enough into the map that the inward-shifted edge row is also hidden
		// by half a tile, preventing the staggered row boundary from appearing.
		const float halfViewportWidth = (float)w / 2.0f;
		const float mapPixelWidth = (float)(mapw - 1) * (float)TILE_WIDTH;
		const float halfTileWidth = static_cast<float>(TILE_WIDTH) / 2.0f;
		float minWorldX = halfViewportWidth +
			static_cast<float>(TILE_WIDTH);
		float maxWorldX = mapPixelWidth - halfViewportWidth - halfTileWidth;
		if (maxWorldX < minWorldX)
		{
			cameraWorldPosition.x = (minWorldX + maxWorldX) / 2.0f;
		}
		else
		{
			cameraWorldPosition.x = clampFloat(cameraWorldPosition.x, minWorldX, maxWorldX);
		}
	}
	int line = std::abs(position.y) % 2;
	if (line == 0)
	{
		position.x = clampInt((int)round(cameraWorldPosition.x / (float)TILE_WIDTH), 0, mapw > 0 ? mapw - 1 : 0);
	}
	else
	{
		position.x = clampInt((int)round((cameraWorldPosition.x - (float)TILE_WIDTH / 2.0f) / (float)TILE_WIDTH), 0, mapw > 0 ? mapw - 1 : 0);
	}
	PointEx cameraTileWorldPosition = Map::getTilePositionEx(position, { 0, 0 }, { 0, 0 }, { 0, 0 });
	offset.x = cameraWorldPosition.x - cameraTileWorldPosition.x;
	offset.y = cameraWorldPosition.y - cameraTileWorldPosition.y;
}

void Camera::resetView()
{
	clearVibrationOffset();
	vibratingDegree = 0;
	followPlayer = true;
	followNPC.reset();
	flying = false;
	clearDirectionalFrameMove();
	snapToFollowTarget();
	// Map changes are teleports; do not send a large camera delta to visual systems.
	differencePosition = { 0.0, 0.0 };
}

void Camera::vibrate(int degree)
{
	vibratingDegree = resolveVibrationDegree(vibratingDegree, degree);
}

void Camera::clearVibrationOffset()
{
	removeVibrationOffset();
	vibrationOffset = { 0.0f, 0.0f };
}

void Camera::removeVibrationOffset()
{
	if (vibrationOffset.x == 0.0f && vibrationOffset.y == 0.0f)
	{
		return;
	}
	offset.x -= vibrationOffset.x;
	offset.y -= vibrationOffset.y;
}

void Camera::updateVibrationOffset()
{
	if (vibratingDegree <= 0 || engine == nullptr)
	{
		return;
	}

	int xSign = engine->getRand(1) == 0 ? -1 : 1;
	int ySign = engine->getRand(1) == 0 ? -1 : 1;
	PointEx proposedAddition =
	{
		(float)(xSign * engine->getRand(vibratingDegree)),
		(float)(ySign * engine->getRand(vibratingDegree))
	};
	vibrationOffset = resolveVibrationOffsetAfterFrame(
		vibrationOffset,
		vibratingDegree,
		proposedAddition);
	offset.x += vibrationOffset.x;
	offset.y += vibrationOffset.y;
	vibratingDegree = decayVibrationDegreeAfterFrame(vibratingDegree);
}

void Camera::clearDirectionalFrameMove()
{
	directionalMoveDirection = 0;
	directionalMoveFramesRemaining = 0;
	directionalMoveSpeed = 0;
	directionalMoveRemainder = { 0.0, 0.0 };
}

void Camera::updateDirectionalFrameMove()
{
	const DirectionalFrameMoveStep step =
		calculateDirectionalFrameMoveStep(
			directionalMoveRemainder,
			directionalMoveDirection,
			directionalMoveSpeed);
	directionalMoveRemainder = step.remainder;
	offset.x += static_cast<float>(step.distance.x);
	offset.y += static_cast<float>(step.distance.y);
	updatePosition();
	clampToMapBounds();

	--directionalMoveFramesRemaining;
	if (directionalMoveFramesRemaining <= 0)
	{
		clearDirectionalFrameMove();
		logicRunning = false;
		if (gm != nullptr && gm->player != nullptr &&
			position == gm->player->getPosition())
		{
			setFollowPlayer();
		}
	}
}

void Camera::reArrangeNPC()
{
	gm->npcManager->sortChildrenByY();
}

void Camera::onUpdate()
{
	auto frameTime = getFrameTime();
	auto lastPosition = position;
	auto lastOffset = offset;
	removeVibrationOffset();
	if (followPlayer)
	{
		snapToFollowTarget();
	}
	else if (directionalMoveFramesRemaining > 0)
	{
		updateDirectionalFrameMove();
	}
	else if (flying)
	{
		float l = hypot(distanceToFly.x, distanceToFly.y);
		if (l <= 0.0001f)
		{
			l = 1.0f;
		}
		PointEx frameFlyDistance = { distanceToFly.x / l * cameraSpeed * frameTime * flySpeed * Config::getGameSpeed(), distanceToFly.y / l * cameraSpeed * frameTime * flySpeed * Config::getGameSpeed()};
		distanceFlied = distanceFlied + frameFlyDistance;
		offset = flyStartOffset + distanceFlied;
		position = flyStartPosition;
		if (std::abs(distanceFlied.x) >= std::abs(distanceToFly.x) && std::abs(distanceFlied.y) >= std::abs(distanceToFly.y))
		{
			flying = false;
			logicRunning = false;
			offset = flyStartOffset + distanceToFly;
			position = flyStartPosition;
			distanceToFly = { 0.0, 0.0 };
			distanceFlied = { 0.0, 0.0 };
		}
		updatePosition();
		clampToMapBounds();
		if (!flying)
		{
			if (position == gm->player->getPosition())
			{
				setFollowPlayer();
			}
		}
	}
	updateVibrationOffset();
	auto differencePoint = gm->map->getTilePosition(position, lastPosition);
	differencePosition.y = ((float)differencePoint.y) - lastOffset.y + offset.y;
	differencePosition.x = ((float)differencePoint.x) - lastOffset.x + offset.x;

}

void Camera::onEvent()
{
	reArrangeNPC();
}

void Camera::onWindowResize(int width, int height)
{
	(void)width;
	(void)height;

	// A resize can be dispatched while a nested system menu has paused gameplay,
	// so the regular update loop cannot be relied on to apply the new map bounds.
	clearVibrationOffset();
	if (followPlayer)
	{
		snapToFollowTarget();
	}
	else
	{
		clampToMapBounds();
	}

	// Resizing the viewport is not camera movement. Avoid shifting weather
	// particles by the potentially large boundary correction on the next frame.
	differencePosition = { 0.0, 0.0 };
}
