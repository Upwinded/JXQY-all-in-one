#include "GameElement.h"
#include "Map.h"
#include "../GameManager/GameManager.h"


GameElement::GameElement()
{
}

GameElement::~GameElement()
{
	freeResource();
}

void GameElement::playSoundFile(const std::string & fileName, float x, float y)
{
	if (fileName == "")
	{
		return;
	}
	std::string soundName = SOUND_FOLDER + fileName;
	char * s = NULL;
	int len = PakFile::readFile(soundName, &s);
	if (len > 0 && s != NULL)
	{
		engine->playSound(s, len, x, y);
		delete s;
	}
}

void GameElement::getNewPosition(Point pos, PointEx off, Point * newPos, PointEx * newOff)
{
	Point newpos = Map::getElementPosition({ (int)off.x , (int)off.y }, pos, { 0, 0 }, { 0, 0 });
	if (newpos.x != pos.x || newpos.y != pos.y)
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

void GameElement::updateEffectPosition(unsigned int ftime, double flySpeed)
{
	if (flySpeed == 0)
	{
		return;
	}
	double l = hypot(flyingDirection.x, flyingDirection.y);
	if (l == 0)
	{
		return;
	}

	double distance = flySpeed * (double)SPEED_TIME * (double)ftime / 2;
	offset.x += distance / l * (double)flyingDirection.x * (double)TILE_WIDTH;
	offset.y += distance / l * (double)flyingDirection.y * (double)TILE_HEIGHT;
	Point newpos = Map::getElementPosition({ (int)offset.x , (int)offset.y }, position, { 0, 0 }, { 0, 0 });
	if (newpos.x != position.x || newpos.y != position.y)
	{
		Point newtilepos = Map::getTilePosition(newpos, position, { 0, 0 }, { 0, 0 });
		offset.x -= newtilepos.x;
		offset.y -= newtilepos.y;
		position = newpos;
	}
}

void GameElement::updateFlyingPosition(unsigned int ftime, double flySpeed)
{
	if (flySpeed == 0)
	{
		return;
	}
	double l = hypot(flyingDirection.x, flyingDirection.y);
	if (l == 0)
	{
		return;
	}

	double distance = flySpeed * (double)SPEED_TIME * (double)ftime;
	offset.x += distance / l * (double)flyingDirection.x * (double)TILE_WIDTH;
	offset.y += distance / l * (double)flyingDirection.y * (double)TILE_WIDTH;
	Point newpos = Map::getElementPosition({ (int)offset.x , (int)offset.y }, position, { 0, 0 }, { 0, 0 });
	if (newpos.x != position.x || newpos.y != position.y)
	{
		Point newtilepos = Map::getTilePosition(newpos, position, { 0, 0 }, { 0, 0 });
		offset.x -= newtilepos.x;
		offset.y -= newtilepos.y ;
		position = newpos;
	}
}

void GameElement::updatePosition(unsigned int ftime)
{
	
}

unsigned int GameElement::getFrameTime()
{
	unsigned int frameTime = getTime() - LastTime;
	if (frameTime > MAX_FRAME_TIME)
	{
		frameTime = MAX_FRAME_TIME;
	}
	frameTime = (int)(((double)frameTime) * timeSlow + 0.5);
	engine->setTime(&timer, LastTime + frameTime);
	LastTime += frameTime;
	return frameTime;
}

void GameElement::beginNewState(int newState)
{
	state = newState;
	initTime();
	stateBeginTime = getTime();
}

Point GameElement::getPosition(Point cenTile, PointEx cenOffset)
{
	int w, h;
	engine->getWindowSize(&w, &h);
	Point pos = Map::getTilePosition(position, cenTile, { w / 2, h / 2 }, cenOffset);
	pos.x += (int)offset.x;
	pos.y += (int)offset.y;
	return pos;
}

Point GameElement::getPosition(GameElement * camera)
{
	if (camera == NULL)
	{
		return { 0, 0 };
	}
	PointEx coffset = camera->offset;
	coffset.x -= offset.x;
	coffset.y -= offset.y;
	return Map::getTilePosition(position, camera->position, { 0, 0 }, coffset);
}

bool GameElement::checkCollide(GameElement * ge1, GameElement * ge2)
{
	if (ge1 == NULL || ge2 == NULL)
	{
		return false;
	}
	if (ge1->position.x != ge2->position.x || ge1->position.y != ge2->position.y)
	{
		return false;
		
	}
	else
	{
		return true;
	}
	/*
	Point p = ge2->getPosition(ge1);
	double x = (double)p.x / TILE_WIDTH;
	double y = (double)p.y / TILE_HEIGHT;
	double rad = ge1->radius + ge2->radius;
	if (hypot(x, y) <= rad)
	{
		return true;
	}
	else
	{
		return false;
	}
	*/
}

bool GameElement::checkCollide(GameElement * ge)
{
	return checkCollide(this, ge);
}

