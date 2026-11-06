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
	if (distance >= 0)
	{
		setFlyTo(dir, distance, std::clamp(speed, 0, 1000));
		run();
	}
}

void Camera::flyToEx(int dir, int distance, int speed)
{
	if (distance >= 0)
	{
		setFlyTo(dir, distance, std::clamp(speed, 0, 1000));
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
	// All three native interpreters move before testing the pre-decrement count.
	setDirectionalFrameMove(dir, distance >= 0 ? static_cast<double>(distance) + 1.0 : 0.0, speed, false);
}

void Camera::setDirectionalFrameMove(int dir, double frames, int speed, bool csharpProtocol)
{
	followPlayer = false;
	clearDirectionalFrameMove();
	distanceToFly = { 0.0, 0.0 };
	distanceFlied = { 0.0, 0.0 };
	flying = false;
	directionalMoveDirection = dir >= 0 && dir <= 7 ? dir : 0;
	directionalMoveFramesRemaining = std::max(0.0, frames);
	directionalMoveSpeed = speed;
	csharpDirectionalMove = csharpProtocol;
}

void Camera::moveForFrameCount(int dir, int frameCount, int speed)
{
	if (frameCount <= 0)
	{
		return;
	}

	setDirectionalFrameMove(dir, frameCount, speed, true);
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

	cameraWorldPosition.y = clampVerticalWorldPosition(cameraWorldPosition.y, maph, h);
	position.y = clampInt((int)round(cameraWorldPosition.y / ((float)TILE_HEIGHT / 2.0f)), 0, maph > 0 ? maph - 1 : 0);
	offset.y = cameraWorldPosition.y - (float)position.y * ((float)TILE_HEIGHT / 2.0f);

	cameraWorldPosition.x = clampHorizontalWorldPosition(cameraWorldPosition.x, mapw, maph, w);
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
	csharpDirectionalMove = false;
}

void Camera::updateDirectionalFrameMove(UTime frameTime)
{
	const double frames = std::min(directionalMoveFramesRemaining,
		static_cast<double>(getLegacyMovementFrameCount(static_cast<float>(frameTime), Config::getGameSpeed())));
	const DirectionalFrameMoveStep step =
		calculateDirectionalFrameMoveStep(
			directionalMoveRemainder,
			directionalMoveDirection,
			directionalMoveSpeed,
			frames,
			csharpDirectionalMove);
	directionalMoveRemainder = step.remainder;
	offset.x += static_cast<float>(step.distance.x);
	offset.y += static_cast<float>(step.distance.y);
	updatePosition();
	clampToMapBounds();

	directionalMoveFramesRemaining -= frames;
	if (directionalMoveFramesRemaining <= 0)
	{
		// JxqyHD carries integer pixels between updates and drops the final fraction.
		if (!csharpDirectionalMove)
		{
			offset.x += static_cast<float>(directionalMoveRemainder.x);
			offset.y += static_cast<float>(directionalMoveRemainder.y);
		}
		updatePosition();
		clampToMapBounds();
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
		updateDirectionalFrameMove(frameTime);
	}
	else if (flying)
	{
		float l = hypot(distanceToFly.x, distanceToFly.y);
		if (l <= 0.0001f)
		{
			l = 1.0f;
		}
		const float distance = getProjectedMagicFrameDistance(
			static_cast<float>(flySpeed), static_cast<float>(frameTime), Config::getGameSpeed());
		PointEx frameFlyDistance = { distanceToFly.x / l * distance, distanceToFly.y / l * distance };
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
