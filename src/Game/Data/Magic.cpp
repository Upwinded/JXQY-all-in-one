#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include "Magic.h"
#include "Map.h"
#include "Effect.h"
#include "EffectManager.h"
#include "../GameManager/GameManager.h"

Magic::Magic()
{
}

Magic::~Magic()
{
	freeResource();
}

void Magic::reset()
{
	name = u8"";
	intro = u8"";

	image = u8"";
	icon = u8"";

	flyingImage = u8"";
	flyingSound = u8"";
	vanishImage = u8"";
	vanishSound = u8"";
	actionFile = u8"";
	actionShadowFile = u8"";
	attackFile = u8"";
	superModeImage = u8"";

	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		level[i].effect = 0;
		level[i].levelupExp = 0;
		level[i].lifeCost = 0;
		level[i].manaCost = 0;
		level[i].thewCost = 0;
		level[i].moveKind = mmkPoint;
		level[i].specialKind = 0;
		level[i].region = 0;
		level[i].speed = 0;
		level[i].flyingLum = 0;
		level[i].vanishLum = 0;
		level[i].waitFrame = 0;
		level[i].lifeFrame = 0;
	}
}

void Magic::initFromIni(const std::string& fileName)
{
	initFromIni(fileName, true);
}

void Magic::initFromIni(const std::string & fileName, bool loadSpecialMagic)
{
	reset();
	freeResource();
	iniName = fileName;
	std::string tempName = INI_MAGIC_FOLDER + iniName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(tempName, s);
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		std::string section = u8"Init";
		name = ini.Get(section, u8"Name", u8"");
		intro = ini.Get(section, u8"Intro", u8"");
		image = ini.Get(section, u8"Image", u8"");
		icon = ini.Get(section, u8"Icon", u8"");
		flyingImage = ini.Get(section, u8"FlyingImage", u8"");
		flyingSound = ini.Get(section, u8"FlyingSound", u8"");
		vanishImage = ini.Get(section, u8"VanishImage", u8"");
		vanishSound = ini.Get(section, u8"VanishSound", u8"");
		actionFile = ini.Get(section, u8"ActionFile", u8"");
		actionShadowFile = ini.Get(section, u8"ActionShadowFile", u8"");
		attackFile = ini.Get(section, u8"AttackFile", u8"");
		superModeImage = ini.Get(section, u8"SuperModeImage", u8"");

		for (int i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
		{
			if (i == 0)
			{
				section = u8"Init";
			}
			else
			{
				section = convert::formatString(u8"Level%d", i);
			}
			int idx = i - 1 < 0 ? 0 : i - 1;
			level[i].effect = ini.GetInteger(section, u8"Effect", level[idx].effect);
			level[i].levelupExp = ini.GetInteger(section, u8"LevelupExp", level[idx].levelupExp);
			level[i].lifeCost = ini.GetInteger(section, u8"LifeCost", level[idx].lifeCost);
			level[i].manaCost = ini.GetInteger(section, u8"ManaCost", level[idx].manaCost);
			level[i].thewCost = ini.GetInteger(section, u8"ThewCost", level[idx].thewCost);

			level[i].moveKind = ini.GetInteger(section, u8"MoveKind", level[idx].moveKind);
			level[i].specialKind = ini.GetInteger(section, u8"SpecialKind", level[idx].specialKind);
			level[i].region = ini.GetInteger(section, u8"Region", level[idx].region);
			level[i].speed = ini.GetInteger(section, u8"Speed", level[idx].speed);
			level[i].flyingLum = ini.GetInteger(section, u8"FlyingLum", level[idx].flyingLum);
			level[i].vanishLum = ini.GetInteger(section, u8"VanishLum", level[idx].vanishLum);
			level[i].waitFrame = ini.GetInteger(section, u8"WaitFrame", level[idx].waitFrame);
			level[i].lifeFrame = ini.GetInteger(section, u8"LifeFrame", level[idx].lifeFrame);

			level[i].attackRadius = ini.GetInteger(section, u8"AttackRadius", level[idx].attackRadius);

		}

		createRes();

		if (loadSpecialMagic && attackFile != u8"")
		{
			specialMagic = std::make_shared<Magic>();
			specialMagic->initFromIni(attackFile, false);
		}
	}
}

std::vector<std::shared_ptr<Effect>> Magic::addEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, std::shared_ptr<GameElement> target)
{
	if (lvl < 1)
	{
		lvl = 1;
	}
	else if (lvl > MAGIC_MAX_LEVEL)
	{
		lvl = MAGIC_MAX_LEVEL;
	}

	switch (srcMagic->level[lvl].moveKind)
	{
	case mmkPoint:
	{
		{
			return addPointEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;	
	}
	case mmkFly:
	{
		{
			return addFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkFlyContinuous:
	{	
		{
			return addContinuousFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkCircle:
	{
		{
			return addCircleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkHeartCircle:
	{
		{
			return addHeartCircleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkHelixCircle:
	{
		{
			return addHelixCircleEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkSector:
	{
		{
			return addSectorEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, false);
		}
		break;
	}
	case mmkRandSector:
	{
		{
			return addSectorEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, true);
		}
		break;
	}
	case mmkLine:
	{
		{
			return addLineEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkMoveLine:
	{
		{
			return addMoveLineEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkRegion:
	{
		{
			switch (srcMagic->level[lvl].region)
			{
			case mrSquare:
			{
				return addSquareEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
				break;
			}
			case mrWave:
			{
				return addWaveEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
				break;
			}
			case mrCross:
			{
				return addCrossEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
				break;
			}		
			default:
				break;
			}
		}
		break;
	}	
	case mmkSelf:
	{
		{
			return addSelfEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, srcMagic->level[lvl].specialKind);
		}
		break;
	}
	case mmkFullScreen:
	{
		{
			return addFullScreenEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkFollow:
	{
		{
			return addFollowEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, target);
		}
		break;
	}
	case mmkThrow:
	{
		{
			return addThrowEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	default:
		break;
	}
	return {};
}

std::vector<std::shared_ptr<Effect>> Magic::addPointEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->level = lvl;
	e->user = user;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = to;
	e->src = e->position;
    int dir = 0;
    if (srcMagic->flyImage != nullptr)
    {
        dir = calDirection(from, to, srcMagic->flyImage->directions);
    }
    else
    {
        dir = calDirection(from, to);
    }
	e->direction = dir;
	if (srcMagic->level[lvl].lifeFrame <= 0)
	{
		e->lifeTime = e->getFlyingImageTime();
	}
	else
	{
		e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
	}
	e->launcherKind = launcher;
	e->damage = damage;
	e->evade = evade;

	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		e->beginFly();
	}
	e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addFlyEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->level = lvl;
	e->user = user;
	e->initFromMagic(srcMagic);

	if (from == to)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = calDirection(from, to);
	Point src = from;
	from = Map::getSubPoint(from, NPC::calDirection(from, to));
	if (from != to)
	{
        if (srcMagic->flyImage != nullptr)
        {
            dir = calDirection(from, to, srcMagic->flyImage->directions);
        }
		else
        {
            dir = calDirection(from, to);
        }
		e->flyingDirection = Map::getTilePosition(to, from);
	}
	else
	{
        if (srcMagic->flyImage != nullptr)
        {
            dir = calDirection(src, to, srcMagic->flyImage->directions);
        }
        else
        {
            dir = calDirection(src, to);
        }
		e->flyingDirection = Map::getTilePosition(to, src);
	}
	if (srcMagic->level[lvl].lifeFrame <= 0)
	{
		e->lifeTime = e->getFlyingImageTime();
	}
	else
	{
		e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
	}
	e->direction = dir;
	e->position = from;
	e->src = from;
	e->damage = damage;
	e->evade = evade;

	e->launcherKind = launcher;
	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		//e->doing = ekFlying;
		e->beginFly();
	}
	e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addContinuousFlyEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	if (from == to)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = calDirection(from, to);
	Point src = from;
	from = Map::getSubPoint(from, NPC::calDirection(from, to));
	if (from != to)
	{
		dir = calDirection(from, to);
	}
	gm->effectManager->setPaused(true);
	for (int i = 0; i < lvl; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);

        if (from != to)
        {
            if (srcMagic->flyImage != nullptr)
            {
                dir = calDirection(from, to, srcMagic->flyImage->directions);
            }
            else
            {
                dir = calDirection(from, to);
            }
            e->flyingDirection = Map::getTilePosition(to, from);
        }
        else
        {
            if (srcMagic->flyImage != nullptr)
            {
                dir = calDirection(src, to, srcMagic->flyImage->directions);
            }
            else
            {
                dir = calDirection(src, to);
            }
            e->flyingDirection = Map::getTilePosition(to, src);
        }
		e->direction = dir;
		e->position = from;
		e->src = e->position;
		e->damage = damage;
		e->evade = evade;

		e->launcherKind = launcher;
		e->waitTime += i * MAGIC_CONTINUOUS_INTERVAL;
		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}
		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	float angle = -M_PI;
	gm->effectManager->setPaused(true);
	for (size_t i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);
        if (srcMagic->flyImage != nullptr)
        {
            e->direction = calDirection(-angle - M_PI / 2, srcMagic->flyImage->directions);
        }
        else
        {
            e->direction = calDirection(-angle - M_PI / 2);
        }
		e->flyingDirection.x = (int)(cos(angle) * 1000.0);// / TILE_HEIGHT);
		e->flyingDirection.y = (int)(-sin(angle) * 1000.0);// / TILE_WIDTH);
		e->position = from;// = Map::getSubPoint(from, NPC::calDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addHeartCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	float angle = -M_PI;
	gm->effectManager->setPaused(true);
	for (size_t i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);

        if (srcMagic->flyImage != nullptr)
        {
            e->direction = calDirection(-angle - M_PI / 2, srcMagic->flyImage->directions);
        }
        else
        {
            e->direction = calDirection(-angle - M_PI / 2);
        }
		e->flyingDirection.x = (int)(cos(angle) * 1000.0);// / TILE_HEIGHT);
		e->flyingDirection.y = (int)(-sin(angle) * 1000.0);// / TILE_WIDTH);
		e->position = from;// = Map::getSubPoint(from, NPC::calDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}

		if (i < MAGIC_CIRCLE_COUNT / 4)
		{
			int count = i;
			e->waitTime += (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DELAY;
			e->speed = int(floor(e->speed * (1.0 - count * MAGIC_HEART_DECAY) + 0.5));
		}
		else if (i < MAGIC_CIRCLE_COUNT / 2)
		{
			int count = i - MAGIC_CIRCLE_COUNT / 4;
			e->waitTime += count * MAGIC_HEART_DELAY;
			e->speed = int(floor(e->speed * (1.0 - (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DECAY) + 0.5));
		}
		else if (i < 3 * MAGIC_CIRCLE_COUNT / 4)
		{
			int count = i - MAGIC_CIRCLE_COUNT / 2;
			e->waitTime += count * MAGIC_HEART_DELAY + MAGIC_HEART_DELAY * MAGIC_CIRCLE_COUNT / 4;
			e->speed = int(floor(e->speed * (1.0 + count * MAGIC_HEART_DECAY) + 0.5));
		}
		else
		{
			int count = i - 3 * MAGIC_CIRCLE_COUNT / 4;
			e->waitTime += (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DELAY + MAGIC_HEART_DELAY * MAGIC_CIRCLE_COUNT / 4;
			e->speed = int(floor(e->speed * (1.0 + (MAGIC_CIRCLE_COUNT / 4 - count) * MAGIC_HEART_DECAY) + 0.5));
		}

		if (e->speed <= 0)
		{
			e->speed = 1;
		}

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addHelixCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int startDir = calDirection(from, to, MAGIC_CIRCLE_COUNT);
	startDir -= MAGIC_CIRCLE_COUNT / 4;
	if (startDir < 0)
	{
		startDir += MAGIC_CIRCLE_COUNT;
	}
	startDir = MAGIC_CIRCLE_COUNT - startDir;
	float angle = -M_PI;
	for (int i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		std::shared_ptr<Effect> e = std::make_shared<Effect>();
		e->level = lvl;
		e->user = user;
		e->initFromMagic(srcMagic);

        if (srcMagic->flyImage != nullptr)
        {
            e->direction = calDirection(-angle - M_PI / 2, srcMagic->flyImage->directions);
        }
        else
        {
            e->direction = calDirection(-angle - M_PI / 2);
        }
		e->flyingDirection.x = (int)(cos(angle) * 1000);
		e->flyingDirection.y = (int)(-sin(angle) * 1000);
		e->position = from; //Map::getSubPoint(from, NPC::calDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (srcMagic->level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}
		int count = i - startDir;
		if (count < 0)
		{
			count += MAGIC_CIRCLE_COUNT;
		}
		e->waitTime += count * MAGIC_CIRCLE_HELIX_INTERVAL;

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			//e->doing = ekFlying;
			e->beginFly();
		}
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addSectorEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, bool randTime)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	if (from == to)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = NPC::calDirection(from, to);
	Point src = from;
	//from = Map::getSubPoint(from, NPC::calDirection(from, to));
	auto tempPos = Map::getTilePosition(to, from);
	tempPos.y = (int)round(MapXRatio * tempPos.y);
	float angle = atan2(-tempPos.x, tempPos.y); //dir * M_PI / 4;
	if (lvl < 4)
	{
		angle -= M_PI / 12;
		for (size_t i = 0; i < 3; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
            if (srcMagic->flyImage != nullptr)
            {
                e->direction = calDirection(limitAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = calDirection(limitAngle(angle));
            }
//			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}
			if (srcMagic->level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();

			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 12;

			ret.push_back(e);
		}
	}
	else if (lvl < 7)
	{
		angle -= M_PI / 10;
		for (size_t i = 0; i < 5; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
            if (srcMagic->flyImage != nullptr)
            {
                e->direction = calDirection(limitAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = calDirection(limitAngle(angle));
            }
//			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 20;
			ret.push_back(e);
		}
	}
	else if (lvl < MAGIC_MAX_LEVEL)
	{
		angle -= M_PI / 5;
		for (size_t i = 0; i < 7; i++)
		{

			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);

            if (srcMagic->flyImage != nullptr)
            {
                e->direction = calDirection(limitAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = calDirection(limitAngle(angle));
            }
            //			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 15;
			ret.push_back(e);
		}
	}
	else
	{
		angle -= M_PI / 4;
		for (size_t i = 0; i < 9; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
            if (srcMagic->flyImage != nullptr)
            {
                e->direction = calDirection(limitAngle(angle), srcMagic->flyImage->directions);
            }
            else
            {
                e->direction = calDirection(limitAngle(angle));
            }
//			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (randTime)
			{
				e->waitTime += Engine::getInstance()->getRand(200);
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			angle += M_PI / 16;
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addLineEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	{
		Point tempTo = to;
		if (from == to)
		{
			tempTo = Map::getSubPoint(to, 0);
		}
		int dir = NPC::calDirection(from, tempTo);
		int magicDir = dir * 2;
		dir += 2;
		if (dir > 7)
		{
			dir -= 8;
		}
		int dir2 = dir + 4;
		if (dir2 > 7)
		{
			dir2 -= 8;
		}
		Point pos = to;
		for (int i = 0; i < lvl; i++)
		{
			pos = Map::getSubPoint(pos, dir2);
		}
		for (int i = 0; i < lvl * 2 + 1; i++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
			e->direction = magicDir;
			e->flyingDirection = { 0, 0 };
			if (srcMagic->level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			e->position = pos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (launcher == lkSelf)
			{
				e->waitTime += PLAYER_MAGIC_DELAY;
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			pos = Map::getSubPoint(pos, dir);
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addMoveLineEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	{
		Point tempTo = to;
		if (from == to)
		{
			tempTo = Map::getSubPoint(to, 0);
		}
		int srcDir = NPC::calDirection(from, tempTo);
		int dir = srcDir;
		int magicDir = dir * 2;
		dir += 2;
		if (dir > 7)
		{
			dir -= 8;
		}
		int dir2 = dir + 4;
		if (dir2 > 7)
		{
			dir2 -= 8;
		}
		Point pos = from;
		for (int i = 0; i < lvl; i++)
		{
			pos = Map::getSubPoint(pos, dir2);
		}
		for (int i = 0; i < lvl * 2 + 1; i++)
		{

			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->level = lvl;
			e->user = user;
			e->initFromMagic(srcMagic);
			e->direction = magicDir;
			e->flyingDirection = Map::getTilePosition(Map::getSubPoint({ 0, 0 }, srcDir), { 0, 0 });
			e->flyingDirection.y = (int)round(MapXRatio * e->flyingDirection.y);
			e->position = pos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (i % 3 != 1)
			{
				e->noLum = true;
			}
			if (srcMagic->level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((float)srcMagic->level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			pos = Map::getSubPoint(pos, dir);
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addSquareEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, int range)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int mrange = 3 + ((lvl - 1) / 3) * 2;
	if (range >= 0)
	{
		mrange = range;
	}	
	Point pos = to;
	for (int i = 0; i < (int)(mrange / 2); i++)
	{
		pos = Map::getSubPoint(pos, 0);
	}
	for (int i = 0; i < mrange; i++)
	{
		Point newPos = pos;
		for (int j = 0; j < mrange; j++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = newPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (i % 3 != 1 || j % 3 != 1)
			{
				e->noLum = true;
			}
			e->lifeTime = e->getFlyinUTime();
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			newPos = Map::getSubPoint(newPos, 5);
			ret.push_back(e);
		}
		pos = Map::getSubPoint(pos, 3);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addWaveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int hrange = 3 + ((lvl - 1) / 3) * 2;
	int wrange = 2;
	Point tempTo = to;
	if (from == to)
	{
		tempTo = Map::getSubPoint(to, 0);
	}
	int srcDir = NPC::calDirection(from, tempTo);
	//int magicDir = calDirection(from, tempTo);
	int dir = srcDir;
	int magicDir = dir * 2;
	dir += 2;
	if (dir > 7)
	{
		dir -= 8;
	}
	int dir2 = dir + 4;
	if (dir2 > 7)
	{
		dir2 -= 8;
	}
	Point pos = Map::getSubPoint(from, srcDir);
	for (int i = 0; i < wrange; i++)
	{
		pos = Map::getSubPoint(pos, dir2);
	}
	for (int i = 0; i < hrange; i++)
	{
		Point newPos = pos;
		for (int j = 0; j < wrange * 2 + 1; j++)
		{
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = newPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (i % 2 == 0 || j % 2 == 0)
			{
				e->noLum = true;
			}
			e->lifeTime = e->getFlyinUTime();
			e->waitTime += i * 60;
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(EFFECT_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			newPos = Map::getSubPoint(newPos, dir);
			ret.push_back(e);
		}
		pos = Map::getSubPoint(pos, srcDir);
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addCrossEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	int mrange = 3 + ((lvl - 1) / 3) * 2;
	Point pos[4] = { from, from, from, from };
	for (int i = 0; i < mrange; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			pos[j] = Map::getSubPoint(pos[j], j * 2 + 1);
			std::shared_ptr<Effect> e = std::make_shared<Effect>();
			e->user = user;
			e->level = lvl;
			e->initFromMagic(srcMagic);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = pos[j];
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			e->lifeTime = e->getFlyinUTime();
			if (e->lifeTime == 0)
			{
				e->lifeTime = (unsigned int)(EFFECT_FRAME_TIME * 2);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				//e->doing = ekFlying;
				e->beginFly();
			}
			e->calDest();
			gm->effectManager->addEffect(e);
			e->beginTime = e->getTime();
			ret.push_back(e);
		}
	}
	gm->effectManager->setPaused(false);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addSelfEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, int specialKind)
{
	std::vector<std::shared_ptr<Effect>> ret;
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->user = user;
	e->level = lvl;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = user->position;
	e->src = e->position;
	e->offset = user->offset;
	e->direction = 0;
	e->lifeTime = 0;
	e->launcherKind = launcher;

	e->damage = damage;
	e->evade = evade;
	if (e->waitTime > 0)
	{
		e->doing = ekHiding;
	}
	else
	{
		e->doing = ekExploding;
		e->lifeTime = e->getExplodinUTime();
	}
	e->calDest();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	switch (specialKind)
	{
	case mskAddLife:
		(std::dynamic_pointer_cast<NPC>(user))->addLife(damage);
		break;
	case mskAddThew:
		(std::dynamic_pointer_cast<NPC>(user))->addThew(damage);
		break;
	case mskAddShield:
		if (user != gm->player)
		{
			break;
		}
		gm->player->shieldBeginTime = gm->player->getTime();
		gm->player->shieldLastTime = e->lifeTime;
		gm->player->shieldLife = damage;
		gm->effectManager->deleteEffect(gm->player->shieldEffect);
		gm->player->shieldEffect = e;
		break;
	default:
		break;
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addFullScreenEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->user = user;
	e->level = lvl;
	e->initFromMagic(srcMagic);
	e->flyingDirection = { 0, 0 };
	e->position = gm->player->position;
	e->src = e->position;
	e->offset = gm->player->offset;
	e->direction = 0;
	e->lifeTime = 0;
	e->launcherKind = launcher;

	e->damage = damage;
	e->evade = evade;
	e->lifeTime = e->getSuperImageTime();
	e->calDest();
	gm->npcManager->setPaused(true);
	gm->player->setPaused(true);
	gm->objectManager->setPaused(true);
	gm->effectManager->pauseAllEffect();
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	e->doing = ekSuperMode;
	e->run();
	gm->npcManager->setPaused(false);
	gm->player->setPaused(false);
	gm->objectManager->setPaused(false);
	gm->effectManager->resumeAllEffect();
	auto damageList = gm->npcManager->findNPC(lkEnemy, user->position, 20);
	std::shared_ptr<Effect> oriE = e;
	std::vector<std::shared_ptr<Effect>> ret;
	gm->effectManager->setPaused(true);
	for (size_t i = 0; i < damageList.size(); i++)
	{
		damageList[i]->directHurt(oriE);
		e = std::make_shared<Effect>();
		e->user = user;
		e->level = lvl;
		e->initFromMagic(srcMagic);
		e->flyingDirection = { 0, 0 };
		e->position = damageList[i]->position;
		e->src = e->position;
		e->offset = damageList[i]->offset;
		e->direction = 0;
		e->lifeTime = 0;
		e->launcherKind = launcher;

		e->damage = damage;
		e->evade = evade;
		e->lifeTime = e->getExplodinUTime();
		e->calDest();
		gm->effectManager->addEffect(e);
		e->beginExplode(e->position);		
	}
	gm->effectManager->setPaused(false);
	gm->effectManager->deleteEffect(oriE);
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addFollowEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, std::shared_ptr<GameElement> target)
{
	auto ret = addFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
	for (size_t i = 0; i < ret.size(); i++)
	{
		(ret[i])->target = target;
		(ret[i])->dest = to;
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addThrowEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	auto ret = addFlyEffect(srcMagic, user, from, to, lvl, damage, evade, launcher);
	for (size_t i = 0; i < ret.size(); i++)
	{
		if ((ret[i])->doing == ekFlying)
		{
			(ret[i])->doing = ekThrowing;
		}
		(ret[i])->dest = to;
	}
	return ret;
}

std::vector<std::shared_ptr<Effect>> Magic::addThrowExplodeEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	auto ret = addSquareEffect(srcMagic, user, from, to, lvl, damage, evade, launcher, (int)((lvl - 1) / 3 + 1));
	for (size_t i = 0; i < ret.size(); i++)
	{
		auto e = ret[i];
		e->waitTime = 0;
		e->doing = ekFlying;
		e->lifeTime = e->getFlyinUTime();
		if (e->lifeTime == 0)
		{
			e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
		}
	}
	return ret;
}

float Magic::calAngle(Point from, Point to)
{
	Point pos = Map::getTilePosition(to, from, { 0, 0 }, { 0, 0 });
	PointEx dir;
	dir.x = ((float)pos.x) / TILE_WIDTH * MapXRatio;
	dir.y = ((float)pos.y) / TILE_HEIGHT;
	dir.x = -dir.x;
	return atan2(dir.x, dir.y);
}

int Magic::calDirection(Point from, Point to)
{
	return calDirection(calAngle(from, to));
}

int Magic::calDirection(float angle)
{
	return calDirection(angle, 16);
}

int Magic::calDirection(Point from, Point to, int maxDir)
{
	return calDirection(calAngle(from, to), maxDir);
}

int Magic::calDirection(float angle, int maxDir)
{
	if (angle < 0)
	{
		angle += 2 * M_PI;
	}
	angle += M_PI / maxDir;
	int result = (int)(angle / (2 * M_PI / maxDir));
	if (result > maxDir)
	{
		result -= maxDir;
	}
    else if (result < 0)
    {
        result += maxDir;
    }
	return result;
}

void Magic::copy(Magic & magic)
{
#define copyData(a); a = magic.a;

	copyData(iniName);
	copyData(name);
	copyData(intro);
	copyData(image);
	copyData(icon);
	copyData(flyingImage);
	copyData(flyingSound);
	copyData(vanishImage);
	copyData(vanishSound);
	copyData(superModeImage);
	copyData(actionFile);
	copyData(actionShadowFile)
	copyData(attackFile);

	for (size_t i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
	{
		copyData(level[i].effect);
		copyData(level[i].levelupExp);
		copyData(level[i].lifeCost);
		copyData(level[i].manaCost);
		copyData(level[i].thewCost);
		copyData(level[i].moveKind);
		copyData(level[i].specialKind);
		copyData(level[i].region);
		copyData(level[i].speed);
		copyData(level[i].flyingLum);
		copyData(level[i].vanishLum);
		copyData(level[i].waitFrame);
		copyData(level[i].lifeFrame);
	}
	freeResource();
	imageSelfCreated = false;
	copyData(flyImage);
	copyData(explodeImage);
	copyData(superImage);
}

void Magic::freeResource()
{
	actionImage = nullptr;

	actionShadow = nullptr;

	specialMagic = nullptr;

	flyImage = nullptr;
		
	explodeImage = nullptr;
		
	superImage = nullptr;
	
}

void Magic::createRes()
{
	freeResource();
	imageSelfCreated = true;
	flyImage = createMagicFlyingImage();
	explodeImage = createMagicVanishImage();
	superImage = createMagicSuperModeImage();
	actionImage = createActionImage();
	actionShadow = createActionShadow();
}

_shared_imp Magic::createActionImage()
{
	if (actionFile.empty())
	{
		return nullptr;
	}
	std::string imgName = NPC_RES_FOLDER;
	imgName += actionFile;
	return IMP::createIMPImage(imgName);
}

_shared_imp Magic::createActionShadow()
{
	if (actionShadowFile.empty())
	{
		return nullptr;
	}
	std::string imgName = NPC_RES_FOLDER;
	imgName += actionShadowFile;
	return IMP::createIMPImage(imgName);
}

_shared_imp Magic::createMagicFlyingImage()
{
	if (flyingImage.empty())
	{
		return nullptr;
	}
	std::string imgName = EFFECT_RES_FOLDER;
	imgName += flyingImage;
	return IMP::createIMPImage(imgName);
}

_shared_imp Magic::createMagicVanishImage()
{
	if (vanishImage.empty())
	{
		return nullptr;
	}
	std::string imgName = EFFECT_RES_FOLDER;
	imgName += vanishImage;
	return IMP::createIMPImage(imgName);
}

_shared_imp Magic::createMagicSuperModeImage()
{
	if (superModeImage.empty())
	{
		return nullptr;
	}
	std::string imgName = EFFECT_RES_FOLDER;
	imgName += superModeImage;
	return IMP::createIMPImage(imgName);
}

_shared_imp Magic::createMagicImage()
{
	if (image.empty())
	{
		return nullptr;
	}
	std::string fileName = MAGIC_RES_FOLDER + image;
	return IMP::createIMPImage(fileName);
}

_shared_imp Magic::createMagicIcon()
{
	if (icon.empty())
	{
		return nullptr;
	}
	std::string fileName = MAGIC_RES_FOLDER + icon;
	return IMP::createIMPImage(fileName);
}

float Magic::limitAngle(float angle)
{
	while (angle < 0)
	{
		angle += 2 * M_PI;
	}
	while (angle > 2 * M_PI)
	{
		angle -= 2 * M_PI;
	}
	return angle;
}
