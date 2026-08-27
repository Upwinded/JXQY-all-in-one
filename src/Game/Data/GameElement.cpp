#include "GameElement.h"
#include "../../Engine/Engine.h"
#include "Map.h"
#include "MediaPathResolver.h"
#include "ProjectedMovement.h"
#include "../../Engine/AudioDecodeSafety.h"
#include "../GameManager/GameManager.h"


GameElement::GameElement()
{
	soundVolume = engine->getSoundVolume();
}

GameElement::~GameElement()
{
	freeResource();
}

void GameElement::updateFrameTime()
{
	Element::updateFrameTime();
	if (timeSlow != 1.0f)
	{
		auto slowFrameTime = (UTime)((float)frameTime * timeSlow + 0.5f);
		unifiedTime -= frameTime;
		unifiedTime += slowFrameTime;
		frameTime = slowFrameTime;
	}
}

_channel GameElement::playSoundFile(const std::string & fileName, float x, float y, float volume)
{
	if (fileName.empty())
	{
		return nullptr;
	}
	std::unique_ptr<char[]> s;
	int len = 0;
	if (File::readFile(resolveSoundAssetPath(fileName), s, len,
		static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes)) &&
		len > 0 && s != nullptr)
	{
		if (volume == -1.0f)
		{
			return engine->playSound(s, len, x, y);
		}
		else
		{
			return engine->playSound(s, len, x, y, volume);
		}
	}
	return nullptr;
}

void GameElement::getNewPosition(Point pos, PointEx off, Point * newPos, PointEx * newOff)
{
	Point newpos = Map::getElementPosition({ (int)off.x , (int)off.y }, pos, { 0, 0 }, { 0, 0 });
	if (newpos != pos)
	{
		Point newtilepos = Map::getTilePosition(newpos, pos, { 0, 0 }, { 0, 0 });
		off.x -= newtilepos.x;
		off.y -= newtilepos.y;
		pos = newpos;
	}
	if (newPos != nullptr)
	{
		*newPos = newpos;
	}
	if (newOff != nullptr)
	{
		*newOff = off;
	}
}

void GameElement::updateEffectPosition(UTime ftime, float flySpeed)
{
	if (flySpeed <= 0.0f || ftime == 0)
	{
		return;
	}
	PointEx movementVector = getEffectProjectedMovementVector(flyingDirection);
	if (getProjectedMovementLength(movementVector) <= 0.0f)
	{
		return;
	}

	float distance = getProjectedMagicFrameDistance(flySpeed, (float)ftime, Config::getGameSpeed());
	offset = advanceProjectedMovement(offset, movementVector, distance);
	updatePosition();
}

void GameElement::updateJumpingPosition(UTime ftime, float flySpeed)
{
	if (flySpeed <= 0.0f || ftime == 0)
	{
		return;
	}
	PointEx movementVector = { (float)flyingDirection.x, (float)flyingDirection.y };
	if (getProjectedMovementLength(movementVector) <= 0.0f)
	{
		return;
	}

	float distance = getProjectedFrameDistance(flySpeed, (float)ftime, Config::getGameSpeed());
	offset = advanceProjectedMovement(offset, movementVector, distance);
	updatePosition();
}

void GameElement::updatePosition()
{
	getNewPosition(position, offset, &position, &offset);
}

Point GameElement::getScreenPosition(Point cenTile, PointEx cenOffset)
{
	int w, h;
	engine->getWindowSize(w, h);
	PointEx posoffset;
	posoffset.x = cenOffset.x - offset.x;
	posoffset.y = cenOffset.y - offset.y;
	PointEx pos = Map::getTilePositionEx(position, cenTile, { w / 2, h / 2 }, posoffset);
	return { (int)round(pos.x), (int)round(pos.y) };
}

Point GameElement::getDrawPosition(std::shared_ptr<GameElement> camera)
{
	if (camera == nullptr)
	{
		return { 0, 0 };
	}
	PointEx coffset = camera->offset;
	coffset.x -= offset.x;
	coffset.y -= offset.y;
	PointEx pos = Map::getTilePositionEx(position, camera->position, { 0, 0 }, coffset);
	return { (int)round(pos.x), (int)round(pos.y) };
}

bool GameElement::checkCollide(std::shared_ptr<GameElement> ge1, std::shared_ptr<GameElement> ge2)
{
	if (ge1 == nullptr || ge2 == nullptr)
	{
		return false;
	}
	if (ge1->position != ge2->position)
	{
		return false;
	}
	else
	{
		return true;
	}
}

bool GameElement::checkCollide(std::shared_ptr<GameElement> ge)
{
	return checkCollide(std::dynamic_pointer_cast<GameElement>(getMySharedPtr()), ge);
}
