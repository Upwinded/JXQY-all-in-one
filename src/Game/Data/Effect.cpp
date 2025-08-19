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

UTime Effect::getUpdateTime()
{
	return updateTime;
}

UTime Effect::getFlyingImageTime()
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

UTime Effect::getExplodinUTime()
{
	if (getMoveKind() == mmkSelf)
	{
		if (magic.level[level].specialKind == mskAddShield)
		{
			return (unsigned int)(((float)magic.level[level].lifeFrame) * EFFECT_FRAME_TIME);
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

UTime Effect::getSuperImageTime()
{
	return IMP::getIMPImageActionTime(magic.superImage);
}

void Effect::beginExplode(Point pos)
{
	doing = ekExploding;
	if (flyingDirection.is_zero() || pos == src)
	{
		offset = srcOffset;
		position = pos;
	}
	else
	{
		offset = getCollideOffset(pos);
		auto l1 = hypot(offset.x, offset.y);
		if (l1 < TILE_HEIGHT)
		{
			auto l2 = hypot(flyingDirection.x * MapXRatio,flyingDirection.y);
			offset.x -= flyingDirection.x * (TILE_HEIGHT - l1) / l2 * MapXRatio;
			offset.y -= flyingDirection.y * (TILE_HEIGHT - l1) / l2;
		}
		position = pos;
		updatePosition();
	}
	if (magic.level[level].moveKind != mmkPoint)
	{
		beginTime = getTime();
		lifeTime = getExplodinUTime();
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
		lifeTime = getFlyinUTime();
		playSound(ekFlying);
	}
	else
	{
		doing = ekFlying;
		beginTime = getTime();
		lifeTime = getFlyinUTime();
		playSound(doing);
	}
}

void Effect::beginDrop()
{
	doing = ekHiding;
	lifeTime = 0;
	result = erLifeExhaust;
	auto tempMagic = std::make_shared<Magic>();
	tempMagic->copy(magic);
	Magic::addThrowExplodeEffect(tempMagic, user, dest, dest, level, damage, evade, launcherKind);
	playSound(ekExploding);
}

UTime Effect::getFlyinUTime()
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
		return (unsigned int)((float)magic.level[level].lifeFrame * EFFECT_FRAME_TIME);
	}
}

void Effect::updateSound()
{
	if (channel == nullptr)
	{
		return;
	}
	if (!engine->getMusicPlaying(channel))
	{
		channel = nullptr;
		return;
	}
	if (gm == nullptr || gm->camera == nullptr)
	{
		return;
	}

	PointEx soundOffset = gm->camera->offset - offset;
	Point pos = Map::getTilePosition(position, gm->camera->position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	engine->setMusicPosition(channel, x, y);
}

void Effect::initParam()
{
	fileName = magic.iniName;
	speed = magic.level[level].speed;
}

void Effect::calTime()
{
	lifeTime = (unsigned int)(((float)magic.level[level].lifeFrame) * EFFECT_FRAME_TIME);
	waitTime = (unsigned int)(((float)magic.level[level].waitFrame) * EFFECT_FRAME_TIME);
}

//计算一个较远处的目标，新版轨迹计算已不需要
void Effect::calDest()
{
	dest = src;
	if (flyingDirection.is_zero())
	{
		return;
	}
	int distance = (int)((speed + 5) * Config::getGameSpeed() * ((float)lifeTime + 5000));
	if (flyingDirection.x < 10 || flyingDirection.y < 10)
	{
		dest.x = flyingDirection.x * 100 * distance + src.x;
		dest.y = (int)round(((float)flyingDirection.y) * 100 * distance * MapXRatio) + src.y;
	}
	else
	{
		dest.x = flyingDirection.x * distance * 10 + src.x;
		dest.y = (int)round(((float)flyingDirection.y) * distance * 10 * MapXRatio) + src.y;
	}
}

auto Effect::getPassPath(Point from, PointEx fromOffset, Point to, PointEx toOffset)
{	
	std::deque<Point> result, tempPath[3];
	std::vector<float> resultDistance;
	PointEx distanceOffset[3];
	distanceOffset[0] = { 0, 0 };
	tempPath[0] = gm->map->getPassPathEx(from, fromOffset, to, toOffset, flyingDirection);
	float l = hypot(flyingDirection.x, flyingDirection.y);
	int tempX = convert_max((int)round(((float)flyingDirection.x) / l * width * TILE_WIDTH / 2) - 1, 1);
	int tempY = convert_max((int)round((((float)flyingDirection.y) / l * width * TILE_WIDTH / 2) - 1), 1);
	Point tempFrom, tempTo;
	PointEx tempFromOffset, tempToOffset;
	getNewPosition(from, { fromOffset.x + tempY , fromOffset.y - tempX }, &tempFrom, &tempFromOffset);
	getNewPosition(to, { toOffset.x + tempY , toOffset.y - tempX }, &tempTo, &tempToOffset);
	distanceOffset[1] = { (float)tempY, (float)- tempX};
	tempPath[1] = gm->map->getPassPathEx(tempFrom, tempFromOffset, tempTo, tempToOffset, flyingDirection);
	getNewPosition(from, { fromOffset.x - tempY , fromOffset.y + tempX }, &tempFrom, &tempFromOffset);
	getNewPosition(to, { toOffset.x - tempY , toOffset.y + tempX }, &tempTo, &tempToOffset);
	distanceOffset[1] = { (float)-tempY, (float)tempX };
	tempPath[2] = gm->map->getPassPathEx(tempFrom, tempFromOffset, tempTo, tempToOffset, flyingDirection);
	auto maxStep = convert_max(convert_max(tempPath[0].size(), tempPath[1].size()), tempPath[2].size());
	for (size_t i = 0; i < maxStep; i++)
	{
		for (size_t j = 0; j < 3; j++)
		{
			if (tempPath[j].size() > i)
			{
				auto toOffset = getCollideOffset(tempPath[j][i]);
				auto l = Map::getTileDistance(src, srcOffset + distanceOffset[j], tempPath[j][i], toOffset);
				bool found = false;
				for (size_t k = 0; k < result.size(); k++)
				{
					if (result[k] == tempPath[j][i])
					{
						found = true;
						break;
					}
					else
					{
						if (l < resultDistance[k])
						{
							found = true;
							result.insert(result.begin() + k, tempPath[j][i]);
							resultDistance.insert(resultDistance.begin() + k, l);
							break;
						}
					}
				}
				if (!found)
				{
					result.push_back(tempPath[j][i]);
					resultDistance.push_back(l);
				}
			}
		}
	}
	return result;
}

void Effect::changeFollowTarget(std::shared_ptr<GameElement> newTarget)
{
	target = newTarget;
	src = position;
	srcOffset = offset;
	dest = (std::dynamic_pointer_cast<NPC>(newTarget))->position;
	destOffset = { 0, 0 };
	flyingDirection = Map::getTilePosition(dest, src);
	flyingDirection.x -= (int)srcOffset.x;
	flyingDirection.y -= (int)srcOffset.y;
	flyingDirection.y = (int)((float)flyingDirection.y * MapXRatio);
	direction = getDirection(flyingDirection);
	lifeTime -= getUpdateTime() - beginTime;
	beginTime = getUpdateTime();
}

unsigned char Effect::getLum()
{
	if (noLum)
	{
		return 0;
	}
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

void Effect::initFromMagic(std::shared_ptr<Magic> m)
{
	if (m == nullptr)
	{
		magic.reset();

	}
	else
	{
		magic.copy(*m.get());
	}
	initParam();
	calTime();
}

void Effect::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	if (ini == nullptr)
	{
		return;
	}
	getFrameTime();
	user = nullptr;
	target = nullptr;
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
	if (ini == nullptr)
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
	
	auto tempBeginTime = getTime() - beginTime;
	ini->SetTime(section, "BeginTime", tempBeginTime);
	ini->SetTime(section, "LifeTime", lifeTime);
	ini->SetTime(section, "WaitTime", waitTime);
	ini->SetInteger(section, "Damage", damage);
	ini->SetInteger(section, "Evade", evade);
	ini->SetInteger(section, "Launcher", launcherKind);
	ini->SetInteger(section, "Direction", direction);
	ini->SetInteger(section, "Level", level);
}

void Effect::playSound(int act)
{
	PointEx soundOffset = gm->camera->offset - offset;
	Point pos = Map::getTilePosition(position, gm->camera->position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	float soundFactor = 1.0f;
	switch (act)
	{
	case ekFlying:
		// 多重飞行技能释放时，声音叠加过大，此处降低音量处理
		if (magic.level[level].moveKind == mmkHeartCircle || magic.level[level].moveKind == mmkCircle 
			|| magic.level[level].moveKind == mmkHelixCircle)
		{
			soundFactor = 0.05f;
		}
		else if (magic.level[level].moveKind == mmkSector || magic.level[level].moveKind == mmkRandSector ||
			(magic.level[level].moveKind == mmkRegion && magic.level[level].region == mrWave))
		{
			if (level < 4)
			{
				soundFactor = 0.35f;
			}
			else if (level < 7)
			{
				soundFactor = 0.2f;
			}
			else
			{
				soundFactor = 0.1f;
			}
		}
		else if (magic.level[level].moveKind == mmkRegion)
		{
			if (level < 4)
			{
				soundFactor = 0.15f;
			}
			else if (level < 7)
			{
				soundFactor = 0.1f;
			}
			else
			{
				soundFactor = 0.05f;
			}
		}
		channel = playSoundFile(magic.flyingSound, x, y, engine->getSoundVolume() * soundFactor);
		break;
	case ekExploding:
		if (magic.level[level].moveKind == mmkFullScreen)
		{
			soundFactor = 0.2f;
		}
		channel = playSoundFile(magic.vanishSound, x, y, engine->getSoundVolume() * soundFactor);
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
	float angle = atan2((float)fDir.x, (float)fDir.y);

	if (angle < 0)
	{
		angle += 2 * pi;
	}

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

void Effect::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = loadImage(&offsetX, &offsetY);
	int offsetH = 0;
	if (getMoveKind() == mmkThrow && doing == ekThrowing)
	{
		auto l1 = Map::getTileDistance(src, srcOffset, position, offset);
		auto l2 = Map::getTileDistance(src, srcOffset, dest, destOffset);
		//抛物线Y坐标偏移
		offsetH = int(MAGIC_THROW_HEIGHT * TILE_HEIGHT * l2 * (1 - pow(l1 / (l2 / 2) - 1, 2)) / speed);
	}
	if ((colorStyle & 0xFFFFFF) == 0xFFFFFF)
	{
		engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY - offsetH);
	}
	else
	{
		engine->drawImageWithColor(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY - offsetH, (colorStyle >> 16) & 0xFF, (colorStyle >> 8) & 0xFF, colorStyle & 0xFF);
	}
}

_shared_image Effect::loadImage(int * x, int * y)
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
	auto ft = getFrameTime();
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
		updateEffectPosition(ft, (float)magic.level[level].speed);
		Point to = position;
		PointEx toOffset = offset;
		//passPath = gm->map->getPassPath(from, to, flyingDirection, dest);
		passPath = getPassPath(from, fromOffset, to, toOffset);
		if ((position == dest) || Map::getTileDistance(src, srcOffset, position, offset) >= Map::getTileDistance(src, srcOffset, dest, destOffset))
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
			updateEffectPosition(ft, (float)magic.level[level].speed);
			Point to = position;
			PointEx toOffset = offset;
			//passPath = gm->map->getPassPath(from, to, flyingDirection, dest);
			passPath = getPassPath(from, fromOffset, to, toOffset);
			if (getUpdateTime() - beginTime >= lifeTime)
			{
				if (running)
				{
					running = false;
				}
				else if (magic.level[level].moveKind == mmkRegion)
				{
					beginExplode(position);
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
					if (gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(target)))
					{
						if (dest != (std::dynamic_pointer_cast<NPC>(target))->position)
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
						auto tempNPC = gm->npcManager->findNearestViewNPC(tempLK, position, MAGIC_FOLLOW_RADIUS);
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
				beginExplode(position);
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
			if (gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(user)))
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
				newSpeed.x = (float)magic.level[level].speed / hypot(flyingDirection.x, flyingDirection.y) * (float)flyingDirection.x;
				newSpeed.y = (float)magic.level[level].speed / hypot(flyingDirection.x, flyingDirection.y) * (float)flyingDirection.y;
				newSpeed.y *= TILE_HEIGHT / TILE_WIDTH;
				float flySpeed = hypot(newSpeed.y, newSpeed.x);
				Point from = position;
				PointEx fromOffset = offset;
				updateEffectPosition(getUpdateTime() - beginTime, (float)flySpeed);
				Point to = position;
				PointEx toOffset = offset;
				//passPath = gm->map->getPassPath(from, to, flyingDirection, dest);
				passPath = getPassPath(from, fromOffset, to, toOffset);
				if (getUpdateTime() - beginTime >= lifeTime)
				{
					if (magic.level[level].moveKind == mmkRegion)
					{
						beginExplode(position);
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
				beginExplode(position);
				if (magic.level[level].moveKind == mmkSelf)
				{
					position = gm->player->position;
					offset = gm->player->offset;
				}
			}
		}
	}
	updateSound();
}

PointEx Effect::getCollideOffset(Point pos)
{
	PointEx result;
	auto p0 = Map::getTilePosition(src, pos, { 0, 0 }, srcOffset);
	if (flyingDirection.x == 0)
	{
		result.x = (int)round(p0.x);
		result.y = 0;
	}
	else if (flyingDirection.y == 0)
	{
		result.y = (int)round(p0.y);
		result.x = 0;
	}
	else
	{
		float k0 = ((float)flyingDirection.y) / ((float)flyingDirection.x * MapXRatio);
		Point p1;
		p1.x = (int)round(((k0 * p0.x) - p0.y) / (k0 + 1 / k0));
		p1.y = (int)round(-p1.x / k0);
		//p1.x = (int)round(p1.x / MapXRatio);
		result.x = p1.x;
		result.y = p1.y;
	}

	return result;
}

