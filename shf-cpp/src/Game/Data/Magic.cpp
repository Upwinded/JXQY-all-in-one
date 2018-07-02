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
	name = "";
	intro = "";

	image = "";
	icon = "";

	flyingImage = "";
	flyingSound = "";
	vanishImage = "";
	vanishSound = "";
	actionFile = "";
	actionShadowFile = "";
	attackFile = "";
	superModeImage = "";

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

void Magic::initFromIni(const std::string & fileName)
{
	initFromIni(fileName, true);
}

void Magic::initFromIni(const std::string & fileName, bool loadSpecialMagic)
{
	reset();
	freeResource();
	iniName = fileName;
	std::string tempName = INI_MAGIC_FOLDER + iniName;
	char * s = NULL;
	int len = PakFile::readFile(tempName, &s);
	if (s != NULL && len > 0)
	{
		INIReader * ini = new INIReader(s);
		delete[] s;
		std::string section = "Init";
		name = ini->Get(section, "Name", "");
		intro = ini->Get(section, "Intro", "");
		image = ini->Get(section, "Image", "");
		icon = ini->Get(section, "Icon", "");
		flyingImage = ini->Get(section, "FlyingImage", "");
		flyingSound = ini->Get(section, "FlyingSound", "");
		vanishImage = ini->Get(section, "VanishImage", "");
		vanishSound = ini->Get(section, "VanishSound", "");
		actionFile = ini->Get(section, "ActionFile", "");
		actionShadowFile = ini->Get(section, "ActionShadowFile", "");
		attackFile = ini->Get(section, "AttackFile", "");
		superModeImage = ini->Get(section, "SuperModeImage", "");

		for (int i = 0; i < MAGIC_MAX_LEVEL + 1; i++)
		{
			if (i == 0)
			{
				section = "Init";
			}
			else
			{
				section = convert::formatString("Level%d", i);
			}
			int idx = i - 1 < 0 ? 0 : i - 1;
			level[i].effect = ini->GetInteger(section, "Effect", level[idx].effect);
			level[i].levelupExp = ini->GetInteger(section, "LevelupExp", level[idx].levelupExp);
			level[i].lifeCost = ini->GetInteger(section, "LifeCost", level[idx].lifeCost);
			level[i].manaCost = ini->GetInteger(section, "ManaCost", level[idx].manaCost);
			level[i].thewCost = ini->GetInteger(section, "ThewCost", level[idx].thewCost);

			level[i].moveKind = ini->GetInteger(section, "MoveKind", level[idx].moveKind);
			level[i].specialKind = ini->GetInteger(section, "SpecialKind", level[idx].specialKind);
			level[i].region = ini->GetInteger(section, "Region", level[idx].region);
			level[i].speed = ini->GetInteger(section, "Speed", level[idx].speed);
			level[i].flyingLum = ini->GetInteger(section, "FlyingLum", level[idx].flyingLum);
			level[i].vanishLum = ini->GetInteger(section, "VanishLum", level[idx].vanishLum);
			level[i].waitFrame = ini->GetInteger(section, "WaitFrame", level[idx].waitFrame);
			level[i].lifeFrame = ini->GetInteger(section, "LifeFrame", level[idx].lifeFrame);

			level[i].attackRadius = ini->GetInteger(section, "AttackRadius", level[idx].attackRadius);

		}

		createRes();
		delete ini;

		if (loadSpecialMagic && attackFile != "")
		{
			specialMagic = new Magic;
			specialMagic->initFromIni(attackFile, false);
		}
	}
}

std::vector<void *> Magic::addEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, GameElement * target)
{
	if (lvl < 1)
	{
		lvl = 1;
	}
	else if (lvl > MAGIC_MAX_LEVEL)
	{
		lvl = MAGIC_MAX_LEVEL;
	}

	switch (level[lvl].moveKind)
	{
	case mmkPoint:
	{
		{
			return addPointEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;	
	}
	case mmkFly:
	{
		{
			return addFlyEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkFlyContinuous:
	{	
		{
			return addContinuousFlyEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkCircle:
	{
		{
			return addCircleEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkHeartCircle:
	{
		{
			return addHeartCircleEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkHelixCircle:
	{
		{
			return addHelixCircleEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkSector:
	{
		{
			return addSectorEffect(user, from, to, lvl, damage, evade, launcher, false);
		}
		break;
	}
	case mmkRandSector:
	{
		{
			return addSectorEffect(user, from, to, lvl, damage, evade, launcher, true);
		}
		break;
	}
	case mmkLine:
	{
		{
			return addLineEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkMoveLine:
	{
		{
			return addMoveLineEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkRegion:
	{
		{
			switch (level[lvl].region)
			{
			case mrSquare:
			{
				return addSquareEffect(user, from, to, lvl, damage, evade, launcher);
				break;
			}
			case mrWave:
			{
				return addWaveEffect(user, from, to, lvl, damage, evade, launcher);
				break;
			}
			case mrCross:
			{
				return addCrossEffect(user, from, to, lvl, damage, evade, launcher);
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
			return addSelfEffect(user, from, to, lvl, damage, evade, launcher, level[lvl].specialKind);
		}
		break;
	}
	case mmkFullScreen:
	{
		{
			return addFullScreenEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	case mmkFollow:
	{
		{
			return addFollowEffect(user, from, to, lvl, damage, evade, launcher, target);
		}
		break;
	}
	case mmkThrow:
	{
		{
			return addThrowEffect(user, from, to, lvl, damage, evade, launcher);
		}
		break;
	}
	default:
		break;
	}
	return {};
}

std::vector<void *> Magic::addPointEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	Effect * e = new Effect;
	e->level = lvl;
	e->user = user;
	e->initFromMagic(this);
	e->flyingDirection = { 0, 0 };
	e->position = to;
	e->src = e->position;
	int dir = calDirection(from, to);
	e->direction = dir;
	if (level[lvl].lifeFrame <= 0)
	{
		e->lifeTime = e->getFlyingImageTime();
	}
	else
	{
		e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
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
		e->doing = ekFlying;
	}
	e->flyingDirection.y *= TILE_WIDTH / TILE_HEIGHT;
	e->calDest();
	gm->effectManager.addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

std::vector<void *> Magic::addFlyEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	Effect * e = new Effect;
	e->level = lvl;
	e->user = user;
	e->initFromMagic(this);

	if (from.x == to.x && from.y == to.y)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = calDirection(from, to);
	Point src = from;
	from = Map::getSubPoint(from, NPC::calDirection(from, to));
	if (from.x != to.x || from.y != to.y)
	{
		dir = calDirection(from, to);
		e->flyingDirection = Map::getTilePosition(to, from);
	}
	else
	{
		e->flyingDirection = Map::getTilePosition(to, src);
	}
	if (level[lvl].lifeFrame <= 0)
	{
		e->lifeTime = e->getFlyingImageTime();
	}
	else
	{
		e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
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
		e->doing = ekFlying;
	}
	e->flyingDirection.y *= TILE_WIDTH / TILE_HEIGHT;
	e->calDest();
	gm->effectManager.addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	return ret;
}

std::vector<void *> Magic::addContinuousFlyEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	if (from.x == to.x && from.y == to.y)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = calDirection(from, to);
	Point src = from;
	from = Map::getSubPoint(from, NPC::calDirection(from, to));
	if (from.x != to.x || from.y != to.y)
	{
		dir = calDirection(from, to);
	}
	gm->effectManager.setPaused(true);
	for (int i = 0; i < lvl; i++)
	{
		Effect * e = new Effect;
		e->level = lvl;
		e->user = user;
		e->initFromMagic(this);

		if (from.x != to.x || from.y != to.y)
		{
			e->flyingDirection = Map::getTilePosition(to, from);
		}
		else
		{
			e->flyingDirection = Map::getTilePosition(to, src);
		}
		e->direction = dir;
		e->position = from;
		e->src = e->position;
		e->damage = damage;
		e->evade = evade;

		e->launcherKind = launcher;
		e->waitTime += i * MAGIC_CONTINUOUS_INTERVAL;
		if (level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}
		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			e->doing = ekFlying;
		}
		e->flyingDirection.y *= TILE_WIDTH / TILE_HEIGHT;
		e->calDest();
		gm->effectManager.addEffect(e);
		e->beginTime = e->getTime();
		ret.push_back(e);
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addCircleEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	double angle = -pi;
	gm->effectManager.setPaused(true);
	for (size_t i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		Effect * e = new Effect;
		e->level = lvl;
		e->user = user;
		e->initFromMagic(this);

		e->direction = calDirection(-angle - pi / 2);
		e->flyingDirection.x = (int)(cos(angle) * 1000.0);// / TILE_HEIGHT);
		e->flyingDirection.y = (int)(-sin(angle) * 1000.0);// / TILE_WIDTH);
		e->position = from;// = Map::getSubPoint(from, NPC::calDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
		}

		if (e->waitTime > 0)
		{
			e->doing = ekHiding;
		}
		else
		{
			e->doing = ekFlying;
		}
		e->calDest();
		gm->effectManager.addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addHeartCircleEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	double angle = -pi;
	gm->effectManager.setPaused(true);
	for (size_t i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		Effect * e = new Effect;
		e->level = lvl;
		e->user = user;
		e->initFromMagic(this);

		e->direction = calDirection(-angle - pi / 2);
		e->flyingDirection.x = (int)(cos(angle) * 1000.0);// / TILE_HEIGHT);
		e->flyingDirection.y = (int)(-sin(angle) * 1000.0);// / TILE_WIDTH);
		e->position = from;// = Map::getSubPoint(from, NPC::calDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
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
			e->doing = ekFlying;
		}
		e->calDest();
		gm->effectManager.addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addHelixCircleEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	int startDir = calDirection(from, to, MAGIC_CIRCLE_COUNT);
	startDir -= MAGIC_CIRCLE_COUNT / 4;
	if (startDir < 0)
	{
		startDir += MAGIC_CIRCLE_COUNT;
	}
	startDir = MAGIC_CIRCLE_COUNT - startDir;
	double angle = -pi;
	for (int i = 0; i < MAGIC_CIRCLE_COUNT; i++)
	{
		Effect * e = new Effect;
		e->level = lvl;
		e->user = user;
		e->initFromMagic(this);

		e->direction = calDirection(-angle - pi / 2);
		e->flyingDirection.x = (int)(cos(angle) * 1000);
		e->flyingDirection.y = (int)(-sin(angle) * 1000);
		e->position = from; Map::getSubPoint(from, NPC::calDirection(angle));
		e->src = e->position;
		e->launcherKind = launcher;
		e->damage = damage;
		e->evade = evade;

		if (level[lvl].lifeFrame <= 0)
		{
			e->lifeTime = e->getFlyingImageTime();
		}
		else
		{
			e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
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
			e->doing = ekFlying;
		}
		e->calDest();
		gm->effectManager.addEffect(e);
		e->beginTime = e->getTime();
		angle += MAGIC_CIRCLE_ANGLE_SPACE;
		ret.push_back(e);
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addSectorEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, bool randTime)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	if (from.x == to.x && from.y == to.y)
	{
		to = Map::getSubPoint(to, 0);
	}
	int dir = NPC::calDirection(from, to);
	Point src = from;
	from = Map::getSubPoint(from, NPC::calDirection(from, to));
	double angle = dir * pi / 4;
	if (lvl < 4)
	{
		angle -= pi / 12;
		for (size_t i = 0; i < 3; i++)
		{
			Effect * e = new Effect;
			e->level = lvl;
			e->user = user;
			e->initFromMagic(this);

			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += rand() % 200;
			}
			if (level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				e->doing = ekFlying;

			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			angle += pi / 12;
			ret.push_back(e);
		}
	}
	else if (lvl < 7)
	{
		angle -= pi / 8;
		for (size_t i = 0; i < 5; i++)
		{
			Effect * e = new Effect;
			e->level = lvl;
			e->user = user;
			e->initFromMagic(this);

			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += rand() % 200;
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				e->doing = ekFlying;

			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			angle += pi / 16;
			ret.push_back(e);
		}
	}
	else if (lvl < MAGIC_MAX_LEVEL)
	{
		angle -= pi / 5;
		for (size_t i = 0; i < 7; i++)
		{

			Effect * e = new Effect;
			e->level = lvl;
			e->user = user;
			e->initFromMagic(this);

			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (randTime)
			{
				e->waitTime += rand() % 200;
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				e->doing = ekFlying;

			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			angle += pi / 15;
			ret.push_back(e);
		}
	}
	else
	{
		angle -= pi / 4;
		for (size_t i = 0; i < 9; i++)
		{
			Effect * e = new Effect;
			e->level = lvl;
			e->user = user;
			e->initFromMagic(this);

			e->direction = calDirection(limitAngle(angle));
			e->flyingDirection.x = (int)(-sin(angle) * 1000.0);// / TILE_HEIGHT);
			e->flyingDirection.y = (int)(cos(angle) * 1000.0);// / TILE_WIDTH);
			e->position = from;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;
			if (randTime)
			{
				e->waitTime += rand() % 200;
			}

			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				e->doing = ekFlying;

			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			angle += pi / 16;
			ret.push_back(e);
		}
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addLineEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	{
		Point tempTo = to;
		if (from.x == to.x && from.y == to.y)
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
			Effect * e = new Effect;
			e->level = lvl;
			e->user = user;
			e->initFromMagic(this);
			e->direction = magicDir;
			e->flyingDirection = { 0, 0 };
			if (level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
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
				e->doing = ekFlying;

			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			pos = Map::getSubPoint(pos, dir);
			ret.push_back(e);
		}
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addMoveLineEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	{
		Point tempTo = to;
		if (from.x == to.x && from.y == to.y)
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
			Effect * e = new Effect;
			e->level = lvl;
			e->user = user;
			e->initFromMagic(this);
			e->direction = magicDir;
			e->flyingDirection = Map::getTilePosition(Map::getSubPoint({ 0, 0 }, srcDir), { 0, 0 });
			e->flyingDirection.y *= TILE_WIDTH / TILE_HEIGHT;
			e->position = pos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			if (level[lvl].lifeFrame <= 0)
			{
				e->lifeTime = e->getFlyingImageTime();
			}
			else
			{
				e->lifeTime = (unsigned int)((double)level[lvl].lifeFrame * EFFECT_FRAME_TIME);
			}
			if (e->waitTime > 0)
			{
				e->doing = ekHiding;
			}
			else
			{
				e->doing = ekFlying;

			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			pos = Map::getSubPoint(pos, dir);
			ret.push_back(e);
		}
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addSquareEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, int range)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
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
			Effect * e = new Effect;
			e->user = user;
			e->level = lvl;
			e->initFromMagic(this);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = newPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			e->lifeTime = e->getFlyingTime();
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
				e->doing = ekFlying;
			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			newPos = Map::getSubPoint(newPos, 5);
			ret.push_back(e);
		}
		pos = Map::getSubPoint(pos, 3);
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addWaveEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	int hrange = 3 + ((lvl - 1) / 3) * 2;
	int wrange = 2;
	Point tempTo = to;
	if (from.x == to.x && from.y == to.y)
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
			Effect * e = new Effect;
			e->user = user;
			e->level = lvl;
			e->initFromMagic(this);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = newPos;
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			e->lifeTime = e->getFlyingTime();
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
				e->doing = ekFlying;
			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			newPos = Map::getSubPoint(newPos, dir);
			ret.push_back(e);
		}
		pos = Map::getSubPoint(pos, srcDir);
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addCrossEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	int mrange = 3 + ((lvl - 1) / 3) * 2;
	Point pos[4] = { from, from, from, from };
	for (int i = 0; i < mrange; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			pos[j] = Map::getSubPoint(pos[j], j * 2 + 1);
			Effect * e = new Effect;
			e->user = user;
			e->level = lvl;
			e->initFromMagic(this);
			e->direction = 0;
			e->flyingDirection = { 0, 0 };
			e->position = pos[j];
			e->src = e->position;
			e->launcherKind = launcher;
			e->damage = damage;
			e->evade = evade;

			e->lifeTime = e->getFlyingTime();
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
				e->doing = ekFlying;
			}
			e->calDest();
			gm->effectManager.addEffect(e);
			e->beginTime = e->getTime();
			ret.push_back(e);
		}
	}
	gm->effectManager.setPaused(false);
	return ret;
}

std::vector<void *> Magic::addSelfEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, int specialKind)
{
	std::vector<void *> ret = {};
	Effect * e = new Effect;
	e->user = user;
	e->level = lvl;
	e->initFromMagic(this);
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
		e->lifeTime = e->getExplodingTime();
	}
	e->calDest();
	gm->effectManager.addEffect(e);
	e->beginTime = e->getTime();
	ret.push_back(e);
	switch (specialKind)
	{
	case mskAddLife:
		((NPC *)user)->addLife(damage);
		break;
	case mskAddThew:
		((NPC *)user)->addThew(damage);
		break;
	case mskAddShield:
		if (user != &gm->player)
		{
			break;
		}
		gm->player.shieldBeginTime = gm->player.getTime();
		gm->player.shieldLastTime = e->lifeTime;
		gm->player.shieldLife = damage;
		gm->effectManager.deleteEffect(gm->player.shieldEffect);
		gm->player.shieldEffect = e;
		break;
	default:
		break;
	}
	return ret;
}

std::vector<void *> Magic::addFullScreenEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	Effect * e = new Effect;
	e->user = user;
	e->level = lvl;
	e->initFromMagic(this);
	e->flyingDirection = { 0, 0 };
	e->position = gm->player.position;
	e->src = e->position;
	e->offset = gm->player.offset;
	e->direction = 0;
	e->lifeTime = 0;
	e->launcherKind = launcher;

	e->damage = damage;
	e->evade = evade;
	e->lifeTime = e->getSuperImageTime();
	e->calDest();
	gm->npcManager.setPaused(true);
	gm->player.setPaused(true);
	gm->objectManager.setPaused(true);
	gm->effectManager.pauseAllEffect();
	gm->effectManager.addEffect(e);
	e->beginTime = e->getTime();
	e->doing = ekSuperMode;
	e->run();
	gm->effectManager.removeEffect(e);
	gm->npcManager.setPaused(false);
	gm->player.setPaused(false);
	gm->objectManager.setPaused(false);
	gm->effectManager.resumeAllEffect();
	auto damageList = gm->npcManager.findNPC(lkEnemy, user->position, 20);
	Effect * oriE = e;
	std::vector<void *> ret = {};
	gm->effectManager.setPaused(true);
	for (size_t i = 0; i < damageList.size(); i++)
	{
		damageList[i]->directHurt(oriE);
		e = new Effect;
		e->user = user;
		e->level = lvl;
		e->initFromMagic(this);
		e->flyingDirection = { 0, 0 };
		e->position = damageList[i]->position;
		e->src = e->position;
		e->offset = damageList[i]->offset;
		e->direction = 0;
		e->lifeTime = 0;
		e->launcherKind = launcher;

		e->damage = damage;
		e->evade = evade;
		e->lifeTime = e->getExplodingTime();
		e->calDest();
		e->doing = ekExploding;
		gm->effectManager.addEffect(e);
		e->beginTime = e->getTime();
		e->playSound(ekExploding);
		ret.push_back(e);
	}
	gm->effectManager.setPaused(false);
	delete oriE;
	return ret;
}

std::vector<void *> Magic::addFollowEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, GameElement * target)
{
	auto ret = addFlyEffect(user, from, to, lvl, damage, evade, launcher);
	for (size_t i = 0; i < ret.size(); i++)
	{
		((Effect *)ret[i])->target = target;
		((Effect *)ret[i])->dest = to;
	}
	return ret;
}

std::vector<void *> Magic::addThrowEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	auto ret = addFlyEffect(user, from, to, lvl, damage, evade, launcher);
	for (size_t i = 0; i < ret.size(); i++)
	{
		if (((Effect *)ret[i])->doing == ekFlying)
		{
			((Effect *)ret[i])->doing = ekThrowing;
		}
		((Effect *)ret[i])->dest = to;
	}
	return ret;
}

std::vector<void*> Magic::addThrowExplodeEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher)
{
	auto ret = addSquareEffect(user, from, to, lvl, damage, evade, launcher, (int)((lvl - 1) / 3 + 1));
	for (size_t i = 0; i < ret.size(); i++)
	{
		auto e = (Effect *)ret[i];
		e->waitTime = 0;
		e->doing = ekFlying;
		e->lifeTime = e->getFlyingTime();
		if (e->lifeTime == 0)
		{
			e->lifeTime = (unsigned int)(MAX_FRAME_TIME * 2);
		}
	}
	return ret;
}

double Magic::calAngle(Point from, Point to)
{
	Point pos = Map::getTilePosition(to, from, { 0, 0 }, { 0, 0 });
	PointEx dir;
	dir.x = ((double)pos.x);// / TILE_WIDTH;
	dir.y = ((double)pos.y);// / TILE_HEIGHT;
	dir.x = -dir.x;
	return atan2(dir.x, dir.y);
}

int Magic::calDirection(Point from, Point to)
{
	return calDirection(calAngle(from, to));
}

int Magic::calDirection(double angle)
{
	return calDirection(angle, 16);
}

int Magic::calDirection(Point from, Point to, int maxDir)
{
	return calDirection(calAngle(from, to), maxDir);
}

int Magic::calDirection(double angle, int maxDir)
{
	if (angle < 0)
	{
		angle += 2 * pi;
	}
	angle += pi / maxDir;
	int result = (int)(angle / (2 * pi / maxDir));
	if (result > maxDir)
	{
		result -= maxDir;
	}
	return result;
}

void Magic::copy(Magic * magic)
{
	if (magic == NULL)
	{
		return;
	}

#define copyData(a); a = magic->a;

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
	if (actionImage != NULL)
	{
		IMP::clearIMPImage(actionImage);
		delete actionImage;
		actionImage = NULL;
	}
	if (actionShadow != NULL)
	{
		IMP::clearIMPImage(actionShadow);
		delete actionShadow;
		actionShadow = NULL;
	}
	if (specialMagic != NULL)
	{
		delete specialMagic;
		specialMagic = NULL;
	}
	if (imageSelfCreated)
	{
		if (flyImage != NULL)
		{
			IMP::clearIMPImage(flyImage);
			delete flyImage;
			flyImage = NULL;
		}
		if (explodeImage != NULL)
		{
			IMP::clearIMPImage(explodeImage);
			delete explodeImage;
			explodeImage = NULL;
		}
		if (superImage != NULL)
		{
			IMP::clearIMPImage(superImage);
			delete superImage;
			superImage = NULL;
		}
	}
	else
	{
		flyImage = NULL;
		explodeImage = NULL;
		superImage = NULL;
	}
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

IMPImage * Magic::createActionImage()
{
	if (actionFile == "")
	{
		return nullptr;
	}
	std::string imgName = NPC_RES_FOLDER;
	imgName += actionFile;
	return IMP::createIMPImage(imgName);
}

IMPImage * Magic::createActionShadow()
{
	if (actionShadowFile == "")
	{
		return nullptr;
	}
	std::string imgName = NPC_RES_FOLDER;
	imgName += actionShadowFile;
	return IMP::createIMPImage(imgName);
}

IMPImage * Magic::createMagicFlyingImage()
{
	if (flyingImage == "")
	{
		return nullptr;
	}
	std::string imgName = EFFECT_RES_FOLDER;
	imgName += flyingImage;
	return IMP::createIMPImage(imgName);
}

IMPImage * Magic::createMagicVanishImage()
{
	if (vanishImage == "")
	{
		return nullptr;
	}
	std::string imgName = EFFECT_RES_FOLDER;
	imgName += vanishImage;
	return IMP::createIMPImage(imgName);
}

IMPImage * Magic::createMagicSuperModeImage()
{
	if (superModeImage == "")
	{
		return nullptr;
	}
	std::string imgName = EFFECT_RES_FOLDER;
	imgName += superModeImage;
	return IMP::createIMPImage(imgName);
}

IMPImage * Magic::createMagicImage()
{
	if (image == "")
	{
		return nullptr;
	}
	std::string fileName = MAGIC_RES_FOLDER + image;
	IMPImage * img = IMP::createIMPImage(fileName);
	return img;
}

IMPImage * Magic::createMagicIcon()
{
	if (icon == "")
	{
		return nullptr;
	}
	std::string fileName = MAGIC_RES_FOLDER + icon;
	IMPImage * img = IMP::createIMPImage(fileName);
	return img;
}

double Magic::limitAngle(double angle)
{
	while (angle < 0)
	{
		angle += 2 * pi;
	}
	while (angle > 2 * pi)
	{
		angle -= 2 * pi;
	}
	return angle;
}
