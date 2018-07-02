#include "Effect.h"
#include "../GameManager/GameManager.h"


Effect::Effect()
{
	coverMouse = false;
}

Effect::~Effect()
{
	freeResource();
}

void Effect::eventRun()
{
	run();
}

unsigned int Effect::getUpdateTime()
{
	return updateTime;
}

unsigned int Effect::getFlyingImageTime()
{
	if (getMoveKind() == mmkThrow)
	{
		return IMP::getIMPImageActionTime(magic.explodeImage);
	}
	else
	{
		return IMP::getIMPImageActionTime(magic.flyImage);
	}
}

unsigned int Effect::getExplodingTime()
{
	if (getMoveKind() == mmkSelf)
	{
		if (magic.level[level].specialKind == mskAddShield)
		{
			return (unsigned int)(((double)magic.level[level].lifeFrame) * EFFECT_FRAME_TIME);
		}
		else
		{
			return IMP::getIMPImageActionTime(magic.flyImage);
		}	
	}
	else if (getMoveKind() == mmkPoint)
	{
		return IMP::getIMPImageActionTime(magic.flyImage);
	}
	else if (getMoveKind() == mmkThrow)
	{
		return 0;
	}
	else
	{
		return IMP::getIMPImageActionTime(magic.explodeImage);
	}
}

unsigned int Effect::getSuperImageTime()
{
	return IMP::getIMPImageActionTime(magic.superImage);
}

void Effect::beginExplode()
{
	doing = ekExploding;
	if (flyingDirection.x == 0 && flyingDirection.y == 0)
	{
		offset = { 0, 0 };
	}
	else
	{
		double angle = atan2(-flyingDirection.y, flyingDirection.x);

		if (angle >= pi * 7 / 8 || angle < - pi * 7 / 8 )
		{
			offset = { TILE_WIDTH / 2, 0 };
		}
		else if (angle < pi * 7 / 8 && angle >= pi * 5 / 8)
		{
			offset = { TILE_WIDTH / 4, TILE_HEIGHT / 4 };
		}
		else if (angle < pi * 5 / 8 && angle >= pi * 3 / 8)
		{
			offset = { 0, TILE_HEIGHT / 2 };
		}
		else if (angle < pi * 3 / 8 && angle >= pi / 8)
		{
			offset = { -TILE_WIDTH / 4, TILE_HEIGHT / 4 };
		}
		else if (angle < pi / 8 && angle >= -pi / 8)
		{
			offset = { -TILE_WIDTH / 2, 0 };
		}
		else if (angle < -pi / 8 && angle >= -pi * 3 / 8)
		{
			offset = { -TILE_WIDTH / 4, -TILE_HEIGHT / 4 };
		}
		else if (angle < -pi * 3 / 8 && angle >= -pi * 5 / 8)
		{
			offset = { 0, -TILE_HEIGHT / 2 };
		}
		else
		{
			offset = { TILE_WIDTH / 4, -TILE_HEIGHT / 4 };
		}
	}
	if (magic.level[level].moveKind != mmkPoint)
	{
		beginTime = getTime();
		lifeTime = getExplodingTime();
		playSound(doing);
	}
	if (lifeTime == 0)
	{
		doing = ekHiding;
		result = erLifeExhaust;
	}
}

void Effect::beginFly()
{
	if (getMoveKind() == mmkThrow)
	{
		doing = ekThrowing;
		beginTime = getTime();
		lifeTime = getFlyingTime();
		playSound(ekFlying);
	}
	else
	{
		doing = ekFlying;
		beginTime = getTime();
		lifeTime = getFlyingTime();
		playSound(doing);
	}
}

void Effect::beginDrop()
{
	doing = ekHiding;
	lifeTime = 0;
	result = erLifeExhaust;
	magic.addThrowExplodeEffect(user, dest, dest, level, damage, evade, launcherKind);
	playSound(ekExploding);
}

unsigned int Effect::getFlyingTime()
{
	if (getMoveKind() == mmkThrow)
	{
		return getFlyingImageTime();
	}
	else if (magic.level[level].lifeFrame <= 0)
	{
		return getFlyingImageTime();
	}
	else
	{
		return (unsigned int)((double)magic.level[level].lifeFrame * EFFECT_FRAME_TIME);
	}
}

void Effect::initParam()
{
	fileName = magic.iniName;
	speed = magic.level[level].speed;
}

void Effect::calTime()
{
	lifeTime = (unsigned int)(((double)magic.level[level].lifeFrame) * EFFECT_FRAME_TIME);
	waitTime = (unsigned int)(((double)magic.level[level].waitFrame) * EFFECT_FRAME_TIME);
}

//计算一个较远处的目标，新版寻路已不需要
void Effect::calDest()
{
	dest = src;
	if (flyingDirection.x == 0 && flyingDirection.y == 0)
	{
		return;
	}
	int distance = (int)((speed + 5) * (double)SPEED_TIME * ((double)lifeTime + 5000));
	if (flyingDirection.x < 10 || flyingDirection.y < 10)
	{
		dest.x = flyingDirection.x * 100 * distance + src.x;
		dest.y = flyingDirection.y * 100 * distance * 2 + src.y;
	}
	else
	{
		dest.x = flyingDirection.x * distance * 10 + src.x;
		dest.y = flyingDirection.y * distance * 10 * 2 + src.y;
	}
}

std::deque<Point> Effect::getPassPath(Point from, PointEx fromOffset, Point to, PointEx toOffset)
{	
	std::deque<Point> tempPath[3], result = {};
	tempPath[0] = gm->map.getPassPathEx(from, fromOffset, to, toOffset, flyingDirection);
	double l = hypot(flyingDirection.x, flyingDirection.y);
	int tempX = max((int)(((double)flyingDirection.x) / l * width * TILE_HEIGHT / 2) - 1, 1);
	int tempY = max((int)(((double)flyingDirection.y) / l * width * TILE_WIDTH / 2) - 1, 1);
	Point tempFrom, tempTo;
	PointEx tempFromOffset, tempToOffset;
	getNewPosition(from, { fromOffset.x + tempY , fromOffset.y - tempX }, &tempFrom, &tempFromOffset);
	getNewPosition(to, { toOffset.x + tempY , toOffset.y - tempX }, &tempTo, &tempToOffset);
	tempPath[1] = gm->map.getPassPathEx(tempFrom, tempFromOffset, tempTo, tempToOffset, flyingDirection);
	getNewPosition(from, { fromOffset.x - tempY , fromOffset.y + tempX }, &tempFrom, &tempFromOffset);
	getNewPosition(to, { toOffset.x - tempY , toOffset.y + tempX }, &tempTo, &tempToOffset);
	tempPath[2] = gm->map.getPassPathEx(tempFrom, tempFromOffset, tempTo, tempToOffset, flyingDirection);
	auto maxStep = max(max(tempPath[0].size(), tempPath[1].size()), tempPath[2].size());
	for (size_t i = 0; i < maxStep; i++)
	{
		for (size_t j = 0; j < 3; j++)
		{
			if (tempPath[j].size() > i)
			{
				result.push_back(tempPath[j][i]);
			}
		}
	}
	return result;
}

void Effect::changeFollowTarget(GameElement * newTarget)
{
	target = newTarget;
	src = position;
	srcOffset = offset;
	dest = ((NPC *)newTarget)->position;
	destOffset = { 0, 0 };
	flyingDirection = Map::getTilePosition(dest, src);
	flyingDirection.x -= (int)srcOffset.x;
	flyingDirection.y -= (int)srcOffset.y;
	flyingDirection.y = (int)((double)flyingDirection.y * TILE_WIDTH / TILE_HEIGHT);
	direction = getDirection(flyingDirection);
	lifeTime -= getUpdateTime() - beginTime;
	beginTime = getUpdateTime();
}

unsigned char Effect::getLum()
{
	if (doing == ekFlying)
	{
		return magic.level[level].flyingLum;
	}
	else if (doing == ekExploding)
	{
		if (magic.level[level].moveKind == mmkSelf)
		{
			return magic.level[level].flyingLum;
		}
		else
		{
			return magic.level[level].vanishLum;
		}
	}
	return 0;
}

int Effect::getMoveKind()
{
	return magic.level[level].moveKind;
}

void Effect::initFromMagic(Magic * m)
{
	if (m == NULL)
	{
		magic.reset();

	}
	else
	{
		magic.copy(m);
	}
	initParam();
	calTime();
}

void Effect::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	if (ini == NULL)
	{
		return;
	}
	getFrameTime();
	user = NULL;
	fileName = ini->Get(section, "FileName", "");
	doing = ini->GetInteger(section, "Doing", ekExploding);
	position.x = ini->GetInteger(section, "MapX", 0);
	position.y = ini->GetInteger(section, "MapY", 0);
	flyingDirection.x = ini->GetInteger(section, "DirectionX", 0);
	flyingDirection.y = ini->GetInteger(section, "DirectionY", 0);
	offset.x = ini->GetReal(section, "OffsetX", 0.0);
	offset.y = ini->GetReal(section, "OffsetY", 0.0);
	src.x = ini->GetInteger(section, "SrcX", 0);
	src.y = ini->GetInteger(section, "SrcY", 0);
	srcOffset.x = ini->GetReal(section, "SrcOffsetX", 0);
	srcOffset.y = ini->GetReal(section, "SrcOffsetY", 0);
	dest.x = ini->GetInteger(section, "DestX", 0);
	dest.y = ini->GetInteger(section, "DestY", 0);
	destOffset.x = ini->GetReal(section, "DestOffsetX", 0);
	destOffset.y = ini->GetReal(section, "DestOffsetY", 0);
	beginTime = ini->GetInteger(section, "BeginTime", 0);
	beginTime = getTime() - beginTime;
	lifeTime = ini->GetInteger(section, "LifeTime", 0);
	waitTime = ini->GetInteger(section, "WaitTime", 0);
	damage = ini->GetInteger(section, "Damage", 0);
	evade = ini->GetInteger(section, "Evade", 0);
	launcherKind = ini->GetInteger(section, "Launcher", lkSelf);
	direction = ini->GetInteger(section, "Direction", 0);
	level = ini->GetInteger(section, "Level", 0);
	magic.initFromIni(fileName);
	initParam();
}

void Effect::saveToIni(INIReader * ini, const std::string & section)
{
	if (ini == NULL)
	{
		return;
	}
	ini->Set(section, "FileName", fileName);
	ini->SetInteger(section, "Doing", doing);
	ini->SetInteger(section, "MapX", position.x);
	ini->SetInteger(section, "MapY", position.y);
	ini->SetInteger(section, "DirectionX", flyingDirection.x);
	ini->SetInteger(section, "DirectionY", flyingDirection.y);
	ini->SetInteger(section, "SrcX", src.x);
	ini->SetInteger(section, "SrcY", src.y);
	ini->SetReal(section, "SrcOffsetX", srcOffset.x);
	ini->SetReal(section, "SrcOffsetY", srcOffset.y);
	ini->SetInteger(section, "DestX", dest.x);
	ini->SetInteger(section, "DestY", dest.y);
	ini->SetReal(section, "DestOffsetX", destOffset.x);
	ini->SetReal(section, "DestOffsetY", destOffset.y);
	ini->SetReal(section, "OffsetX", offset.x);
	ini->SetReal(section, "OffsetY", offset.y);
	
	unsigned int tempBeginTime = getTime() - beginTime;
	ini->SetInteger(section, "BeginTime", tempBeginTime);
	ini->SetInteger(section, "LifeTime", lifeTime);
	ini->SetInteger(section, "WaitTime", waitTime);
	ini->SetInteger(section, "Damage", damage);
	ini->SetInteger(section, "Evade", evade);
	ini->SetInteger(section, "Launcher", launcherKind);
	ini->SetInteger(section, "Direction", direction);
	ini->SetInteger(section, "Level", level);
}

void Effect::playSound(int act)
{
	PointEx soundOffset;
	soundOffset.x = gm->camera.offset.x - offset.x;
	soundOffset.y = gm->camera.offset.y - offset.y;
	Point pos = Map::getTilePosition(position, gm->camera.position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	switch (act)
	{
	case ekFlying:
		playSoundFile(magic.flyingSound, x, y);
		break;
	case ekExploding:
		playSoundFile(magic.vanishSound, x, y);
		break;
	case ekHiding:
		break;
	default:
		break;
	}
}

int Effect::getDirection(Point fDir)
{
	fDir.x = - fDir.x;
	double angle = atan2((double)fDir.x, (double)fDir.y);

	if (angle < 0)
	{
		angle += 2 * pi;
	}
	angle += pi / 16;
	if (angle > 2 * pi)
	{
		angle -= 2 * pi;
	}
	return (int)(angle / (pi / 8));	
}

int Effect::calDirection()
{
	return direction = getDirection(flyingDirection);
}

void Effect::draw(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_image image = loadImage(&offsetX, &offsetY);
	int offsetH = 0;
	if (getMoveKind() == mmkThrow && doing == ekThrowing)
	{
		auto l1 = Map::getTileDistance(src, srcOffset, position, offset);
		auto l2 = Map::getTileDistance(src, srcOffset, dest, destOffset);
		//抛物线Y坐标偏移
		offsetH = int(MAGIC_THROW_HEIGHT * TILE_HEIGHT * l2 * (1 - pow(l1 / (l2 / 2) - 1, 2)) / speed);
	}	
	engine->drawImage(image, pos.x + (int)offset.x - offsetX, pos.y + (int)offset.y - offsetY - offsetH);	
}

_image Effect::loadImage(int * x, int * y)
{
	if (doing == ekExploding)
	{
		if (magic.level[level].moveKind != mmkSelf && magic.level[level].moveKind != mmkPoint)
		{
			return IMP::loadImageForDirection(magic.explodeImage, direction, getUpdateTime() - beginTime, x, y);
		}
		else
		{
			return IMP::loadImageForDirection(magic.flyImage, direction, getUpdateTime() - beginTime, x, y);
		}			
	}
	else if (doing == ekFlying)
	{
		if (getMoveKind() == mmkThrow)
		{
			return IMP::loadImageForDirection(magic.explodeImage, direction, getUpdateTime() - beginTime, x, y);
		}
		else
		{
			return IMP::loadImageForDirection(magic.flyImage, direction, getUpdateTime() - beginTime, x, y);
		}		
	}
	else if (doing == ekSuperMode)
	{
		return IMP::loadImageForDirection(magic.superImage, direction, getUpdateTime() - beginTime, x, y);
	}
	else if (doing == ekThrowing)
	{
		return IMP::loadImageForDirection(magic.flyImage, direction, getUpdateTime() - beginTime, x, y);
	}
	else
	{
		return nullptr;
	}
}

void Effect::freeResource()
{
	magic.freeResource();
}

void Effect::onUpdate()
{
	unsigned int ft = getFrameTime();
	updateTime = getTime();
	if (doing == ekSuperMode)
	{
		if (getUpdateTime() - beginTime >= lifeTime)
		{
			if (running)
			{
				running = false;
			}
		}
	}
	else if (doing == ekThrowing)
	{
		Point from = position;
		PointEx fromOffset = offset;
		updateEffectPosition(ft, (double)magic.level[level].speed);
		Point to = position;
		PointEx toOffset = offset;
		//passPath = gm->map.getPassPath(from, to, flyingDirection, dest);
		passPath = getPassPath(from, fromOffset, to, toOffset);
		if ((position.x == dest.x && position.y == dest.y) || Map::getTileDistance(src, srcOffset, position, offset) >= Map::getTileDistance(src, srcOffset, dest, destOffset))
		{
			beginDrop();
		}
		else if (getUpdateTime() - beginTime > lifeTime)
		{
			if (running)
			{
				running = false;
			}
			doing = ekHiding;
			result = erLifeExhaust;
		}
	}
	else if (doing == ekFlying)
	{		
		if (lifeTime > 0)
		{	
			Point from = position;
			PointEx fromOffset = offset;
			updateEffectPosition(ft, (double)magic.level[level].speed);
			Point to = position;
			PointEx toOffset = offset;
			//passPath = gm->map.getPassPath(from, to, flyingDirection, dest);
			passPath = getPassPath(from, fromOffset, to, toOffset);
			if (getUpdateTime() - beginTime >= lifeTime)
			{
				if (running)
				{
					running = false;
				}
				else if (magic.level[level].moveKind == mmkRegion)
				{
					beginExplode();
					result = erExplode;
					if (lifeTime == 0)
					{
						doing = ekHiding;
						result = erLifeExhaust;
					}
				}
				else
				{
					doing = ekHiding;
					result = erLifeExhaust;
				}			
			}
			else
			{
				if (magic.level[level].moveKind == mmkFollow && getUpdateTime() - beginTime > MAGIC_FOLLOW_DELAY)
				{
					if (gm->npcManager.findNPC((NPC *)target))
					{
						if (dest.x != ((NPC *)target)->position.x || dest.y != ((NPC *)target)->position.y)
						{
							changeFollowTarget(target);
						}
					}
					else
					{
						int tempLK = lkFriend;
						if (launcherKind == lkFriend || launcherKind == lkSelf)
						{
							tempLK = lkEnemy;
						}
						auto tempNPC = gm->npcManager.findNearestViewNPC(tempLK, position, MAGIC_FOLLOW_RADIUS);
						if (tempNPC != nullptr)
						{
							changeFollowTarget(tempNPC);
						}
					}
				}
			}
		}
		else
		{
			if (running)
			{
				running = false;
			}
			else
			{
				beginExplode();
				result = erExplode;
				if (lifeTime == 0)
				{
					doing = ekHiding;
					result = erLifeExhaust;
				}
			}		
		}	
	}
	else if (doing == ekExploding)
	{
		if (getUpdateTime() - beginTime >= lifeTime)
		{
			doing = ekHiding;
			result = erLifeExhaust;
		}
		if (magic.level[level].moveKind == mmkSelf)
		{
			if (gm->npcManager.findNPC((NPC *)user))
			{
				position = user->position;
				offset = user->offset;
			}
		}
	}
	else if (doing == ekHiding)
	{
		if (getUpdateTime() - beginTime >= waitTime)
		{
			if (getMoveKind() == mmkThrow)
			{
				doing = ekThrowing;
				beginTime += waitTime;
				playSound(ekFlying);
			}
			else
			{
				doing = ekFlying;
				beginTime += waitTime;
				playSound(doing);
			}
			if (lifeTime > 0)
			{
				PointEx newSpeed;
				newSpeed.x = (double)magic.level[level].speed / hypot(flyingDirection.x, flyingDirection.y) * (double)flyingDirection.x;
				newSpeed.y = (double)magic.level[level].speed / hypot(flyingDirection.x, flyingDirection.y) * (double)flyingDirection.y;
				newSpeed.y *= TILE_HEIGHT / TILE_WIDTH;
				double flySpeed = hypot(newSpeed.y, newSpeed.x);
				Point from = position;
				PointEx fromOffset = offset;
				updateEffectPosition(getUpdateTime() - beginTime, (double)flySpeed);
				Point to = position;
				PointEx toOffset = offset;
				//passPath = gm->map.getPassPath(from, to, flyingDirection, dest);
				passPath = getPassPath(from, fromOffset, to, toOffset);
				if (getUpdateTime() - beginTime >= lifeTime)
				{
					if (magic.level[level].moveKind == mmkRegion)
					{
						beginExplode();
						result = erExplode;
						if (lifeTime == 0)
						{
							doing = ekHiding;
							result = erLifeExhaust;
						}
					}
					else
					{
						doing = ekHiding;
						result = erLifeExhaust;
					}
				}
			}
			else
			{
				beginExplode();
				if (magic.level[level].moveKind == mmkSelf)
				{
					position = gm->player.position;
					offset = gm->player.offset;
				}
			}
		}
	}

}
