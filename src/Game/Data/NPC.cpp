#include "NPC.h"
#include "../GameManager/GameManager.h"

NPC::NPC()
{
	name = "npc";
	priority = epNPC;
	rect.w = TILE_WIDTH - 20;
	rect.h = (int)((float)TILE_HEIGHT * 2.5);
}

NPC::~NPC()
{
	freeResource();
	if (type)
	{
		return;
	}
}

unsigned int NPC::eventRun()
{
	coverMouse = false;
	unsigned int ret = run();
	coverMouse = true;
	return ret;
}

void NPC::jumpTo(Point dest)
{
	destGE = nullptr;
	beginJump(dest);
	eventRun();
}

void NPC::runTo(Point dest)
{
	destGE = nullptr;
	if (position == dest)
	{
		return;
	}
	int dir = calDirection(position, dest);
	while (position != dest)
	{
		beginRun(dest);
		if (stepList.size() == 0)
		{
			break;
		}
		eventRun();
	}
	direction = dir;
}

void NPC::goTo(Point dest)
{
	destGE = nullptr;
	haveDest = false;
	if (position == dest)
	{
		return;
	}
	int dir = calDirection(position, dest);
	while (position != dest)
	{
		beginWalk(dest);
		if (stepList.size() == 0)
		{
			//beginStand();
			break;
		}
		eventRun();
	}
	direction = dir;	
}

void NPC::goToEx(Point dest)
{
	destGE = nullptr;
	haveDest = true;
	gotoExDest = dest;
	beginWalk(dest);
}

void NPC::goToDir(int dir, int distance)
{
	destGE = nullptr;
	Point pos = position;
	for (int i = 0; i < distance; i++)
	{
		pos = gm->map->getSubPoint(pos, dir);
	}
	goTo(pos);
}

void NPC::attackTo(Point dest, std::shared_ptr<GameElement> target)
{
	beginAttack(dest, target);
	eventRun();
}

void NPC::doSpecialAction(const std::string & fileName)
{
	loadSpecialAction(fileName);
	beginSpecial();
	eventRun();
}

void NPC::setLevel(int lvl)
{
	level = lvl;
	life = lifeMax;
	thew = thewMax;
	mana = manaMax;
}

void NPC::setFight(int f)
{
	fight = f;
	if (fight)
	{
		fightTime = getTime();
	}
}

_shared_image NPC::getActionImage(int * offsetx, int * offsety)
{
	switch (nowAction)
	{
	case acStand:
		return IMP::loadImageForDirection(res.stand.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acStand1:
		return IMP::loadImageForDirection(res.stand1.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acWalk:
		return IMP::loadImageForDirection(res.walk.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acRun:
		return IMP::loadImageForDirection(res.run.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acJump:
		return IMP::loadImageForDirection(res.jump.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAttack:
		return IMP::loadImageForDirection(res.attack.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAttack1:
		return IMP::loadImageForDirection(res.attack1.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAttack2:
		return IMP::loadImageForDirection(res.attack2.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acSpecialAttack:
		return IMP::loadImageForDirection(res.specialAttack.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acMagic:
		return IMP::loadImageForDirection(res.magic.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acHurt:
		return IMP::loadImageForDirection(res.hurt.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acDeath:
		return IMP::loadImageForDirection(res.death.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acSit:
		return IMP::loadImageForLastFrame(res.sit.image, direction, offsetx, offsety);
		break;
	case acSitting:
		return IMP::loadImageForDirection(res.sit.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acSpecial:
		return IMP::loadImageForDirection(res.special.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;	
	case acAStand:
		return IMP::loadImageForDirection(res.astand.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acAWalk:
		return IMP::loadImageForDirection(res.awalk.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acARun:
		return IMP::loadImageForDirection(res.arun.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acAJump:
		return IMP::loadImageForDirection(res.ajump.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	default:
		return nullptr;
		break;
	}
}

_shared_image NPC::getActionShadow(int * offsetx, int * offsety)
{
	switch (nowAction)
	{
	case acStand:
		return IMP::loadImageForDirection(res.stand.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acStand1:
		return IMP::loadImageForDirection(res.stand1.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acWalk:
		return IMP::loadImageForDirection(res.walk.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acRun:
		return IMP::loadImageForDirection(res.run.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acJump:
		return IMP::loadImageForDirection(res.jump.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAttack:
		return IMP::loadImageForDirection(res.attack.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAttack1:
		return IMP::loadImageForDirection(res.attack1.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAttack2:
		return IMP::loadImageForDirection(res.attack2.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acSpecialAttack:
		return IMP::loadImageForDirection(res.specialAttack.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acMagic:
		return IMP::loadImageForDirection(res.magic.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acHurt:
		return IMP::loadImageForDirection(res.hurt.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acDeath:
		return IMP::loadImageForDirection(res.death.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;		
	case acSit:
		return IMP::loadImageForLastFrame(res.sit.shadow, direction, offsetx, offsety);
		break;
	case acSitting:
		return IMP::loadImageForDirection(res.sit.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acSpecial:
		return IMP::loadImageForDirection(res.special.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	case acAStand:
		return IMP::loadImageForDirection(res.astand.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acAWalk:
		return IMP::loadImageForDirection(res.awalk.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acARun:
		return IMP::loadImageForDirection(res.arun.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case acAJump:
		return IMP::loadImageForDirection(res.ajump.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
		break;
	default:
		return nullptr;
		break;
	}
}

void NPC::calOffset(UTime nowTime, UTime totalTime)
{
	double percent = ((double)nowTime) / ((double)totalTime);
	if (percent < 0)
	{
		percent = 0;
	}
	else if (percent > 1)
	{
		percent = 1;
	}
	if (stepState == ssIn)
	{
		percent = 1 - percent;
		switch (direction)
		{
		case 0:
			offset.x = 0;
			offset.y = -percent * (TILE_HEIGHT / 2);
			break;
		case 1:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		case 2:
			offset.x = percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 3:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		case 4:
			offset.x = 0;
			offset.y = percent * (TILE_HEIGHT / 2);
			break;
		case 5:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		case 6:
			offset.x = -percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 7:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		}	
	}
	else if (stepState == ssOut)
	{
		switch (direction)
		{
		case 0:
			offset.x = 0;
			offset.y = percent * (TILE_HEIGHT / 2);
			break;
		case 1:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		case 2:
			offset.x = -percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 3:
			offset.x = -percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		case 4:
			offset.x = 0;
			offset.y = -percent * (TILE_HEIGHT / 2);
			break;
		case 5:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = -percent * (TILE_HEIGHT / 4);
			break;
		case 6:
			offset.x = percent * (TILE_WIDTH / 2);
			offset.y = 0;
			break;
		case 7:
			offset.x = percent * (TILE_WIDTH / 4);
			offset.y = percent * (TILE_HEIGHT / 4);
			break;
		}	
	}
	else
	{
		offset.x = 0;
		offset.y = 0;
	}
}

int NPC::getInvertDirection(int dir)
{
	int result = dir + 4;
	if (result > 7)
	{
		result -= 8;
	}
	return result;
}

int NPC::calDirection(Point dest)
{
	return calDirection(position, dest);
}

UTime NPC::calStepLastTime()
{
	int speed = walkSpeed;
	if (isRunning())
	{
		speed = runSpeed;
	}
	stepLastTime = (UTime)(0.5 / ((double)speed) / (double)Config::getGameSpeed());
	if (direction % 4 == 2)
	{
		stepLastTime = (UTime)round(1.414 * stepLastTime);
	}
	else if (direction % 2 != 0)
	{
		stepLastTime = (UTime)round(1.732 / 2 * stepLastTime);
	}
	return stepLastTime;
}

int NPC::calDirection(double angle)
{
#ifdef pi
#undef pi
#endif // pi
#define pi 3.1415926
	if (angle < 0)
	{
		angle += 2 * pi;
	}
	angle += pi / 8;
	if (angle > 2 * pi)
	{
		angle -= 2 * pi;
	}
	return (int)(angle / (pi / 4));
}

int NPC::calDirection(Point src, Point dest)
{
	Point pos = Map::getTilePosition(dest, src, { 0, 0 }, { 0, 0 });
	PointEx dir;
	dir.x = ((double)pos.x) / TILE_HEIGHT / MapXRatio;
	dir.y = ((double)pos.y) / TILE_HEIGHT;
	dir.x = -dir.x;
	double angle = atan2(dir.x, dir.y);
	return calDirection(angle);
}

void NPC::saveToIni(INIReader * ini, const std::string & section)
{
	if (ini == nullptr)
	{
		return;
	}

	ini->Set(section, "Name", npcName);
	ini->SetInteger(section, "Kind", kind);
	ini->Set(section, "NPCIni", npcIni);
	ini->SetInteger(section, "Dir", direction);
	ini->SetInteger(section, "MapX", position.x);
	ini->SetInteger(section, "MapY", position.y);
	ini->SetInteger(section, "CurrPos", currPos);
	ini->SetInteger(section, "Action", action);
	ini->SetInteger(section, "WalkSpeed", walkSpeed);
	ini->SetInteger(section, "StandSpeed", standSpeed);
	ini->SetInteger(section, "PathFinder", pathFinder);
	ini->SetInteger(section, "DialogRadius", dialogRadius);
	ini->Set(section, "ScriptFile", scriptFile);
	ini->SetInteger(section, "State", state);
	ini->SetInteger(section, "Relation", relation);
	ini->SetInteger(section, "Life", life);
	ini->SetInteger(section, "LifeMax", lifeMax);
	ini->SetInteger(section, "Thew", thew);
	ini->SetInteger(section, "ThewMax", thewMax);
	ini->SetInteger(section, "Mana", mana);
	ini->SetInteger(section, "ManaMax", manaMax);
	ini->SetInteger(section, "Attack", attack);
	ini->SetInteger(section, "Defend", defend);
	ini->SetInteger(section, "Evade", evade);
	ini->SetInteger(section, "Duck", duck);
	ini->SetInteger(section, "Exp", exp);

	ini->SetInteger(section, "LevelUpExp", levelUpExp);
	ini->SetInteger(section, "Level", level);
	ini->SetInteger(section, "AttackLevel", attackLevel);
	ini->SetInteger(section, "MagicLevel", magicLevel);
	ini->SetInteger(section, "Lum", lum);
	ini->SetInteger(section, "VisionRadius", visionRadius);
	ini->SetInteger(section, "AttackRadius", attackRadius);
	ini->Set(section, "BodyIni", bodyIni);
	ini->Set(section, "FlyIni", flyIni);
	ini->Set(section, "FlyIni2", flyIni2);
	ini->Set(section, "MagicIni", magicIni);
	ini->Set(section, "DeathScript", deathScript);
}

void NPC::loadActionRes(NPCActionRes * npcAction)
{
	if (npcAction == nullptr)
	{
		return;
	}
	npcAction->image = gm->npcManager->loadActionImage(npcAction->imageFile);
	npcAction->shadow = gm->npcManager->loadActionImage(npcAction->shadowFile);
}

void NPC::initActionFromIni(NPCActionRes * npcAction, INIReader * iniReader, const std::string & section)
{
	if (npcAction == nullptr || iniReader == nullptr)
	{
		return;
	}
	npcAction->imageFile = iniReader->Get(section, "Image", "");
	npcAction->shadowFile = iniReader->Get(section, "Shade", "");
	npcAction->soundFile = iniReader->Get(section, "Sound", "");
	loadActionRes(npcAction);
}

void NPC::loadSpecialAction(const std::string & fileName)
{
	res.special.imageFile = fileName;
	res.special.shadowFile = convert::extractFileName(fileName) + ".shd";
	res.special.soundFile = "";
	loadActionRes(&res.special);
}

void NPC::initRes(const std::string & fileName)
{
	freeNPCRes();
	std::string iniName = NPC_RES_INI_FOLDER + fileName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(iniName, s);
	if (len <= 0 || s == nullptr)
	{
		return;
	}
	INIReader ini(s);

#define initNPCAction(act) initActionFromIni(&res.act, &ini, #act)

	initNPCAction(stand);
	initNPCAction(stand1);
	initNPCAction(walk);
	initNPCAction(run);
	initNPCAction(jump);
	initNPCAction(attack);
	initNPCAction(attack1);
	initNPCAction(attack2);
	initNPCAction(magic);
	initNPCAction(hurt);
	initNPCAction(death);
	initNPCAction(sit);

	initNPCAction(astand);
	initNPCAction(awalk);
	initNPCAction(arun);
	initNPCAction(ajump);
}

void NPC::loadActionFile(const std::string & fileName, int act)
{
	switch (act)
	{
	case acStand:
		res.stand.imageFile = fileName;
		res.stand.shadowFile = convert::extractFileName(fileName) + ".shd";
		res.stand.soundFile = "";
		loadActionRes(&res.stand);
		if (nowAction == acStand)
		{
			beginStand();
		}
		break;
	case acStand1:
		res.stand1.imageFile = fileName;
		res.stand1.shadowFile = convert::extractFileName(fileName) + ".shd";
		res.stand1.soundFile = "";
		loadActionRes(&res.stand1);
		if (nowAction == acStand1)
		{
			beginStand();
		}
		break;
	default:
		break;
	}
}

void NPC::addLife(int value)
{
	life += value;
}

void NPC::addThew(int value)
{
	thew += value;
}

void NPC::addMana(int value)
{
	mana += value;
}

bool NPC::isSitting()
{
	if (nowAction == acSit || nowAction == acSitting)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isStanding()
{
	if (nowAction == acStand || nowAction == acStand1 || nowAction == acAStand)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isAttacking()
{
	if (nowAction == acAttack || nowAction == acAttack1 || nowAction == acAttack2 || nowAction == acSpecialAttack)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isMagicing()
{
	if (nowAction == acMagic)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isRunning()
{
	if (nowAction == acRun || nowAction == acARun)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isWalking()
{
	if (nowAction == acWalk || nowAction == acAWalk)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isJumping()
{
	if (nowAction == acJump || nowAction == acAJump)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isDying()
{
	if (nowAction == acDeath)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isHurting()
{
	if (nowAction == acHurt)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::isDoingSpecialAction()
{
	if (nowAction == acSpecial)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::canDoAction(NPCActionRes * act)
{
	if (act == nullptr)
	{
		return false;
	}
	if (act->image == nullptr)
	{
		return false;
	}
	if (IMP::getIMPImageActionTime(act->image) > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool NPC::canDoAction(int act)
{
	switch (act)
	{
	case acStand:
		return canDoAction(&res.stand);
		break;
	case acStand1:
		return canDoAction(&res.stand1);
		break;
	case acWalk:
		return canDoAction(&res.walk);
		break;
	case acRun:
		return canDoAction(&res.run);
		break;
	case acJump:
		return canDoAction(&res.jump);
		break;
	case acAttack:
		return canDoAction(&res.attack);
		break;
	case acAttack1:
		return canDoAction(&res.attack1);
		break;
	case acAttack2:
		return canDoAction(&res.attack2);
		break;
	case acSpecialAttack:
		return canDoAction(&res.specialAttack);
		break;
	case acMagic:
		return canDoAction(&res.magic);
		break;
	case acHurt:
		return canDoAction(&res.hurt);
		break;
	case acDeath:
		return canDoAction(&res.death);
		break;
	case acSit:
		return canDoAction(&res.sit);
		break;
	case acSitting:
		return canDoAction(&res.sit);
		break;
	case acSpecial:
		return canDoAction(&res.special);
		break;
	case acAStand:
		return canDoAction(&res.astand);
		break;
	case acAWalk:
		return canDoAction(&res.awalk);
		break;
	case acARun:
		return canDoAction(&res.arun);
		break;
	case acAJump:
		return canDoAction(&res.ajump);
		break;
	default:
		break;
	}
	return false;
}

UTime NPC::getActionTime(int act)
{
	if (canDoAction(act))
	{
		switch (act)
		{
		case acStand:
			return IMP::getIMPImageActionTime(res.stand.image);
			break;
		case acStand1:
			return IMP::getIMPImageActionTime(res.stand1.image);
			break;
		case acWalk:
			return IMP::getIMPImageActionTime(res.walk.image);
			break;
		case acRun:
			return IMP::getIMPImageActionTime(res.run.image);
			break;
		case acJump:
			return IMP::getIMPImageActionTime(res.jump.image);
			break;
		case acAttack:
			return IMP::getIMPImageActionTime(res.attack.image);
			break;
		case acAttack1:
			return IMP::getIMPImageActionTime(res.attack1.image);
			break;
		case acAttack2:
			return IMP::getIMPImageActionTime(res.attack2.image);
			break;
		case acSpecialAttack:
			return IMP::getIMPImageActionTime(res.specialAttack.image);
			break;
		case acMagic:
			return IMP::getIMPImageActionTime(res.magic.image);
			break;
		case acHurt:
			return IMP::getIMPImageActionTime(res.hurt.image);
			break;
		case acDeath:
			return IMP::getIMPImageActionTime(res.death.image);
			break;
		case acSit:
			return IMP::getIMPImageActionTime(res.sit.image);
			break;
		case acSpecial:
			return IMP::getIMPImageActionTime(res.special.image);
			break;
		case acAStand:
			return IMP::getIMPImageActionTime(res.astand.image);
			break;
		case acAWalk:
			return IMP::getIMPImageActionTime(res.awalk.image);
			break;
		case acARun:
			return IMP::getIMPImageActionTime(res.arun.image);
			break;
		case acAJump:
			return IMP::getIMPImageActionTime(res.ajump.image);
			break;
		default:
			break;
		}
	}
	return 0;
	
}

bool NPC::canView(Point dest)
{
	if (gm->map->calDistance(position, dest) > visionRadius)
	{
		return false;
	}
	
	Point subPoint = gm->map->getSubPoint(position, calDirection(position, dest));
	if (gm->map->canView(position, dest) || (gm->map->canViewTile(subPoint) && gm->map->canView(subPoint, dest)))
	{
		return true;
	}
	/*
	subPoint = gm->map->getSubPoint(position, calDirection(position, dest) + 1);
	if (gm->map->canView(position, dest) || (gm->map->canViewTile(subPoint) && gm->map->canView(subPoint, dest)))
	{
		return true;
	}
	subPoint = gm->map->getSubPoint(position, calDirection(position, dest) - 1);
	if (gm->map->canView(position, dest) || (gm->map->canViewTile(subPoint) && gm->map->canView(subPoint, dest)))
	{
		return true;
	}
	*/
	return false;
}

bool NPC::mouseInRect(int x, int y)
{
	_shared_image image = getActionImage(nullptr, nullptr);
	if (engine->pointInImage(image, x - rect.x, y - rect.y))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void NPC::hurt(std::shared_ptr<Effect> e)
{
	if (nowAction == acDeath || nowAction == acHide || e == nullptr)
	{
		return;
	}
	bool addexp = false;
	if (e->launcherKind == lkSelf)
	{
		addexp = true;
	}
	int damage = e->damage;
	damage -= getDefend();
	damage = damage > 10 ? damage : 10;
	int evd = getEvade() - e->evade;

	// TODO: FIXME: 
    //妈的不知道为什么，假独孤剑打不过赵升权，此处加了特殊处理,假独孤剑打其它人时不掉血，通常使用应删除
//    if (npcName == "独孤剑" && e->user != gm->player)
//    {
//        damage = 0;
//    }

	if (engine->getRand(100) > evd)
	{
		if (damage >= life)
		{
			life = 0;
			if (addexp)
			{
				gm->player->addExp((int)round(exp * EXP_RATE));
				gm->magicManager.addPracticeExp((int)round(exp * EXP_RATE));
				gm->magicManager.addUseExp(e, (int)round(exp * EXP_RATE));
			}
			checkDie();
		}
		else
		{
			life -= damage;
			int d = calDirection(atan2(e->flyingDirection.x, -e->flyingDirection.y));
			Point fd = gm->map->getSubPoint(position, d);
			if (engine->getRand(100) > evd)
			{
				if (/*!frozen && */e->magic.level[e->level].moveKind == 2 && e->magic.level[e->level].specialKind == 1)
				{
					UTime newFrozenTime = (e->level + 1) * 1000;
					frozenLastTime = (frozen &&  frozenLastTime > newFrozenTime) ? frozenLastTime : newFrozenTime;
					frozen = true;
				}
				else if (!frozen && engine->getRand(20 + getEvade()) >= getEvade())
				{
					beginHurt(fd);
				}
			}		
		}
	}
	
}

void NPC::directHurt(std::shared_ptr<Effect> e)
{
	if (nowAction == acDeath || nowAction == acHide || e == nullptr)
	{
		return;
	}
	bool addexp = false;
	if (e->launcherKind == lkSelf)
	{
		addexp = true;
	}
	int damage = e->damage;
	damage -= defend;
	damage = damage > 50 ? damage : 50;
	if (damage >= life)
	{
		life = 0;
		if (addexp)
		{
			gm->player->addExp((int)round(exp * EXP_RATE));
			gm->magicManager.addPracticeExp((int)round(exp * EXP_RATE));
			gm->magicManager.addUseExp(e, (int)round(exp * EXP_RATE));
		}
		checkDie();
	}
	else
	{
		life -= damage;
		if (engine->getRand(100) > evade)
		{	
			if (!frozen)
			{
				beginHurt();
			}
		}
	}	
}

void NPC::beginJump(Point dest)
{
	if (!canDoAction(acJump))
	{
		return;
	}
	Point step = gm->map->getJumpPath(position, dest);
	stepList.resize(1);
	stepList[0] = step;
	direction = calDirection(stepList[0]);
	actionBeginTime = getUpdateTime();
	gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
	if (fight > 0 && canDoAction(acAJump))
	{
		nowAction = acAJump;
		actionLastTime = getActionTime(acAJump);
		playSound(acAJump);
	}
	else
	{
		fight = 0;
		nowAction = acJump;
		actionLastTime = getActionTime(acJump);
		playSound(acJump);
	}
	flyingDirection = Map::getTilePosition(step, position, { 0, 0 }, { 0, 0 });

	jumpSpeed = hypot((double)flyingDirection.x / TILE_WIDTH, (double)flyingDirection.y / TILE_WIDTH) / (double)(actionLastTime / 3) / Config::getGameSpeed();
	jumpState = 0;
	
}

UTime NPC::getUpdateTime()
{
	return updateTime;
}

void NPC::playSound(int act)
{
	PointEx soundOffset;
	soundOffset.x = gm->camera->offset.x - offset.x;
	soundOffset.y = gm->camera->offset.y - offset.y;
	Point pos = Map::getTilePosition(position, gm->camera->position, { 0, 0 }, soundOffset);
	float x = float(pos.x) * SOUND_FACTOR / TILE_WIDTH;
	float y = float(pos.y) * SOUND_FACTOR / TILE_HEIGHT;
	switch (act)
	{
	case acStand:
		playSoundFile(res.stand.soundFile, x, y);
		break;
	case acStand1:
		playSoundFile(res.stand1.soundFile, x, y);
		break;
	case acWalk:
		playSoundFile(res.walk.soundFile, x, y);
		break;
	case acRun:
		playSoundFile(res.run.soundFile, x, y);
		break;
	case acJump:
		playSoundFile(res.jump.soundFile, x, y);
		break;
	case acAttack:
		playSoundFile(res.attack.soundFile, x, y);
		break;
	case acAttack1:
		playSoundFile(res.attack1.soundFile, x, y);
		break;
	case acAttack2:
		playSoundFile(res.attack2.soundFile, x, y);
		break;
	case acMagic:
		playSoundFile(res.magic.soundFile, x, y);
		break;
	case acHurt:
		playSoundFile(res.hurt.soundFile, x, y);
		break;
	case acDeath:
		playSoundFile(res.death.soundFile, x, y);
		break;
	case acSit:
		playSoundFile(res.sit.soundFile, x, y);
		break;
	case acSpecial:
		playSoundFile(res.special.soundFile, x, y);
		break;
	case acSitting:
		break;
	case acAStand:
		playSoundFile(res.astand.soundFile, x, y);
		break;
	case acAWalk:
		playSoundFile(res.awalk.soundFile, x, y);
		break;
	case acARun:
		playSoundFile(res.arun.soundFile, x, y);
		break;
	case acAJump:
		playSoundFile(res.ajump.soundFile, x, y);
		break;
	default:
		break;
	}
}

void NPC::reloadAction()
{

#define reloadAction(act) loadActionRes(&res.act)

	reloadAction(stand);
	reloadAction(stand1);
	reloadAction(walk);
	reloadAction(run);
	reloadAction(jump);
	reloadAction(attack);
	reloadAction(attack1);
	reloadAction(attack2);
	reloadAction(magic);
	reloadAction(hurt);
	reloadAction(death);
	reloadAction(sit);

	reloadAction(astand);
	reloadAction(awalk);
	reloadAction(arun);
	reloadAction(ajump);
}

void NPC::doAttack(Point dest, std::shared_ptr<GameElement> target)
{
	int launcher = 0;
	if (kind == nkPlayer)
	{
		launcher = lkSelf;
	}
	else
	{
		if (relation == nrFriendly)
		{
			launcher = lkFriend;
		}
		else if (relation == nrHostile)
		{
			launcher = lkEnemy;
		}
		else if (relation == nrNeutral)
		{
			launcher = lkNeutral;
		}
	}
	if (npcMagic2 == nullptr || npcMagic == nullptr)
	{
		if (npcMagic != nullptr)
		{
			Magic::addEffect(npcMagic, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, getAttack(), getEvade(), launcher, target);
		}
		else if (npcMagic2 != nullptr)
		{
			Magic::addEffect(npcMagic2, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, getAttack(), getEvade(), launcher, target);
		}
	}
	else 
	{
		int idx = engine->getRand(2);
		if (idx)
		{
			Magic::addEffect(npcMagic, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, getAttack(), getEvade(), launcher, target);
		}
		else
		{
			Magic::addEffect(npcMagic2, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, getAttack(), getEvade(), launcher, target);
		}
	}

}

void NPC::useMagic(std::shared_ptr<Magic> m, Point dest, int level, std::shared_ptr<GameElement> target)
{
	if (m == nullptr)
	{
		return;
	}
	if (level < 1)
	{
		level = 1;
	}
	else if (level > MAGIC_MAX_LEVEL)
	{
		level = MAGIC_MAX_LEVEL;
	}
	int launcher = 0;
	if (kind == nkPlayer)
	{
		launcher = lkSelf;
	}
	else
	{
		if (relation == nrFriendly)
		{
			launcher = lkFriend;
		}
		else if (relation == nrHostile)
		{
			launcher = lkEnemy;
		}
		else if (relation == nrNeutral)
		{
			launcher = lkNeutral;
		}
	}
	Magic::addEffect(m, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, level, m->level[level].effect, getEvade(), launcher, target);
}

void NPC::addBody()
{
	if (bodyIni != "")
	{
		gm->objectManager->addObject(bodyIni, position.x, position.y, direction);
	}
}

void NPC::clearStep()
{
	if (stepList.size() > 0)
	{
		if ((isWalking() || isRunning()) && stepState == ssOut)
		{
			gm->map->deleteStepFromDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		}
		else if (isJumping() && jumpState != jsDown)
		{
			gm->map->deleteStepFromDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		}
	}
}

void NPC::beginStand()
{
	destGE = nullptr;
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	if (fight > 0 && canDoAction(acAStand))
	{
		nowAction = acAStand;
		actionLastTime = getActionTime(acAStand);
		playSound(acAStand);
	}
	else
	{
		fight = 0;
		nowAction = acStand;
		actionLastTime = getActionTime(acStand);
		playSound(acStand);
	}
}

void NPC::beginWalk(Point dest)
{
	if (!canDoAction(acWalk))
	{
		return;
	}

	auto tempList = gm->map->getPath(position, dest);
	if (tempList.size() > 0)
	{	
		if (!gm->map->canWalk(tempList[0]))
		{
			tempList.resize(0);
			beginStand();
			return;
		}
		stepList = tempList;
		direction = calDirection(stepList[0]);
		stepState = ssOut;
		nowAction = acWalk;
		calStepLastTime();
		stepBeginTime = getUpdateTime();
		actionBeginTime = getUpdateTime();
		gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		if (fight > 0 && canDoAction(acAWalk))
		{
			nowAction = acAWalk;
			actionLastTime = getActionTime(acAWalk);
			playSound(acAWalk);
		}
		else
		{
			fight = 0;
			nowAction = acWalk;
			actionLastTime = getActionTime(acWalk);
			playSound(acWalk);
		}
	}
}

void NPC::beginHurt(Point dest)
{
	if (frozen)
	{
		return;
	}
	direction = calDirection(dest);
	beginHurt();
}

void NPC::beginHurt()
{
	if (isHurting() || !canDoAction(acHurt))
	{
		return;
	}
	destGE = nullptr;
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	nowAction = acHurt;
	actionLastTime = getActionTime(acHurt);
	fight = 1;
	fightTime = getUpdateTime();
	playSound(acHurt);
}

void NPC::checkDie()
{
	if (deathScript != "")
	{
		beginDieScript();
	}
	else
	{
		beginDie();
	}
}

void NPC::beginDieScript()
{
	beginDie();
	if (deathScript != "")
	{
		result |= erRunDeathScript;
	}
}

void NPC::beginDie()
{
	if (nowAction == acDeath || nowAction == acHide)
	{
		return;
	}
	if (!canDoAction(acDeath))
	{
		return;
	}
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	playSound(acDeath);
	nowAction = acDeath;
	actionLastTime = getActionTime(acDeath);
	actionBeginTime = getUpdateTime();
}

void NPC::beginAttack(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canDoAction(acAttack))
	{
		GameLog::write("can not do action attack\n");
		return;
	}
	attackTarget = target;
    std::vector<int> attackList;
    attackList.push_back(0);
    
    if (canDoAction(acAttack1))
    {
        attackList.push_back(1);
    }
    
    if (canDoAction(acAttack2))
    {
        attackList.push_back(2);
    }

    int attackNum = 0;
    if (attackList.size() > 0)
    {
        attackNum = attackList[engine->getRand((int)attackList.size() - 1)];
    }
    
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	direction = calDirection(dest);
	attackDest = dest;
	attackDone = false;
	if (attackNum == 0)
	{
		nowAction = acAttack;
		actionLastTime = getActionTime(acAttack);
		playSound(acAttack);
	}
	else if (attackNum == 1)
	{
		nowAction = acAttack1;
		actionLastTime = getActionTime(acAttack1);
		playSound(acAttack1);
	}
	else
	{
		nowAction = acAttack2;
		actionLastTime = getActionTime(acAttack2);
		playSound(acAttack2);
	}
}

void NPC::beginSpecial()
{
	if (!canDoAction(acSpecial))
	{
		return;
	}
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	nowAction = acSpecial;
	actionLastTime = getActionTime(acSpecial);
}

void NPC::beginSit()
{
	if (!canDoAction(acSit))
	{
		return;
	}
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	nowAction = acSitting;
	sitState = ssSitting;
	actionLastTime = getActionTime(acSit);
	playSound(acSit);
}

void NPC::beginMagic(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canDoAction(acMagic))
	{
		return;
	}
	attackTarget = target;
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	direction = calDirection(dest);
	actionBeginTime = getUpdateTime();
	attackDone = false;
	nowAction = acMagic;
	actionLastTime = getActionTime(acMagic);
	fight = 1;
	fightTime = getUpdateTime();
	playSound(acMagic);
}

void NPC::beginRun(Point dest)
{
	if (!canDoAction(acRun))
	{
		return;
	}
	auto tempList = gm->map->getPath(position, dest);
	if (tempList.size() > 0)
	{
		if (!gm->map->canWalk(tempList[0]))
		{
			tempList.resize(0);
			beginStand();
			return;
		}
		stepList = tempList;
		direction = calDirection(stepList[0]);
		stepState = ssOut;
		nowAction = acRun;
		calStepLastTime();
		stepBeginTime = getUpdateTime();
		actionBeginTime = getUpdateTime();
		gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		if (fight > 0 && canDoAction(acARun))
		{
			nowAction = acARun;
			actionLastTime = getActionTime(acARun);
			playSound(acARun);
		}
		else
		{
			fight = 0;
			nowAction = acRun;
			actionLastTime = getActionTime(acRun);
			playSound(acRun);
		}
	}
}

void NPC::beginRadiusStep(Point dest, int radius)
{
	if (!canDoAction(acWalk))
	{
		return;
	}
	int stepCount = gm->map->calDistance(position, dest) - radius;
	stepCount = stepCount > NPC_STEP_MAX_COUNT ? NPC_STEP_MAX_COUNT : stepCount;
	auto tempList = gm->map->getStepPath(position, dest, stepCount);
	if (tempList.size() > 0)
	{
		stepList = tempList;
		direction = calDirection(stepList[0]);
		stepState = ssOut;
		nowAction = acWalk;
		calStepLastTime();
		stepBeginTime = getUpdateTime();
		actionBeginTime = getUpdateTime();
		gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		if (fight > 0 && canDoAction(acAWalk))
		{
			nowAction = acAWalk;
			actionLastTime = getActionTime(acAWalk);
			playSound(acAWalk);
		}
		else
		{
			fight = 0;
			nowAction = acWalk;
			actionLastTime = getActionTime(acWalk);
			playSound(acWalk);
		}
	}
}

void NPC::changeRadiusStep(Point dest, int radius)
{
	if (!canDoAction(acWalk))
	{
		return;
	}

	auto tempList = gm->map->getStepPath(position, dest, radius);
	if (tempList.size() > 0)
	{
		stepList = tempList;
		direction = calDirection(stepList[0]);
		stepState = ssOut;
		stepBeginTime += stepLastTime;
		nowAction = acWalk;
		calStepLastTime();
		gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		if (fight > 0 && canDoAction(acAWalk))
		{
			nowAction = acAWalk;
			actionLastTime = getActionTime(acAWalk);
			playSound(acAWalk);
		}
		else
		{
			fight = 0;
			nowAction = acWalk;
			actionLastTime = getActionTime(acWalk);
			playSound(acWalk);
		}
		calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
	}
}

void NPC::beginRadiusWalk(Point dest, int radius)
{
	if (!canDoAction(acWalk))
	{
		return;
	}

	auto tempList = gm->map->getRadiusPath(position, dest, radius);
	if (tempList.size() > 0)
	{
		stepList = tempList;
		direction = calDirection(stepList[0]);
		stepState = ssOut;
		nowAction = acWalk;
		calStepLastTime();
		stepBeginTime = getUpdateTime();
		actionBeginTime = getUpdateTime();
		gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		if (fight > 0 && canDoAction(acAWalk))
		{
			nowAction = acAWalk;
			actionLastTime = getActionTime(acAWalk);
			playSound(acAWalk);
		}
		else
		{
			fight = 0;
			nowAction = acWalk;
			actionLastTime = getActionTime(acWalk);
			playSound(acWalk);
		}
	}
}

void NPC::changeRadiusWalk(Point dest, int radius)
{
	if (isWalking())
	{
		if (!canDoAction(acWalk))
		{
			return;
		}
		auto tempList = gm->map->getRadiusPath(position, dest, radius);
		if (tempList.size() > 0)
		{
			stepList = tempList;
		}
		else
		{
			beginStand();
		}
	}
	else
	{
		beginRadiusWalk(dest, radius);
	}
}

bool NPC::isFollower()
{
	if (kind == nkPartner)
	{
		return true;
	}
	else if (followNPC != "")
	{
		auto fnpc = gm->npcManager->findNPC(followNPC);

		if (fnpc.size() > 0)
		{
			return true;
		}
		else
		{
			followNPC = "";
		}
	}

	return false;
}

bool NPC::isFollowAttack(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return false;
	}
	if ((kind == nkBattle) && (npc->kind == nkBattle || npc->kind == nkPlayer))
	{
		if ((npc->kind == nkBattle && relation != npc->relation) || (npc->kind == nkPlayer && relation != nrFriendly))
		{
			return true;
		}
	}
	return false;
}

void NPC::beginFollowWalk(Point dest)
{
	beginRadiusWalk(dest, NPC_FOLLOW_RADIUS);
}

void NPC::beginFollowAttack(Point dest)
{
	beginRadiusWalk(dest, NPC_FOLLOW_RADIUS);
}

void NPC::changeFollowWalk(Point dest)
{
	changeRadiusWalk(dest, NPC_FOLLOW_RADIUS);
}

void NPC::changeFollowAttack(Point dest)
{
	changeRadiusWalk(dest, attackRadius);
}

void NPC::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
	if (selecting && relation == nrHostile)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, 200, 0, 0, 220);
	}
	else if (selecting && scriptFile != "")
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, 200, 200, 0, 180);
	}
	else
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		if ((colorStyle & 0xFFFFFF) == 0xFFFFFF)
		{
			engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
		}
		else
		{
			engine->drawImageWithColor(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, (colorStyle >> 16) & 0xFF, (colorStyle >> 8) & 0xFF, colorStyle & 0xFF);
		}
	}	
}

void NPC::drawNPCAlpha(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	if (relation == nrHostile)
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, 200, 0, 0, 220);
		engine->setImageAlpha(image, 255);
	}
	else if (scriptFile != "")
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, 200, 200, 0, 180);
		engine->setImageAlpha(image, 255);
	}
	else
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
		engine->setImageAlpha(image, 255);
	}
}

void NPC::limitDir(int * d)
{
	if (d == nullptr)
	{
		return;
	}
	while (*d < 0)
	{
		*d += 8;
	}
	while (*d > 7)
	{
		*d -= 8;
	}
}

void NPC::limitDir()
{
	limitDir(&direction);
}

void NPC::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	
	if (ini == nullptr)
	{
		return;
	}

	//std::string section = "Init";
	getFrameTime();
	destGE = nullptr;
	attackTarget = nullptr;
	npcName = ini->Get(section, "Name", "");
	kind = ini->GetInteger(section, "Kind", nkNormal);
	npcIni = ini->Get(section, "NPCIni", "");
	direction = ini->GetInteger(section, "Dir", 0);
	position.x = ini->GetInteger(section, "MapX", 0);
	position.y = ini->GetInteger(section, "MapY", 0);
	//int dir = 0; //direction
	//int mapX = 0; //position.x
	//int mapY = 0; //position.y
	currPos = ini->GetInteger(section, "CurrPos", 0);
	action = ini->GetInteger(section, "Action", naNone);
	walkSpeed = ini->GetInteger(section, "WalkSpeed", 1);
	if (walkSpeed == 0)
	{
		walkSpeed = 1;
	}
	standSpeed = ini->GetInteger(section, "StandSpeed", 0);
	pathFinder = ini->GetInteger(section, "PathFinder", pfSingle);
	dialogRadius = ini->GetInteger(section, "DialogRadius", 1);
	scriptFile = ini->Get(section, "ScriptFile", "");
	state = ini->GetInteger(section, "State", state);
	relation = ini->GetInteger(section, "Relation", nrFriendly);
	life = ini->GetInteger(section, "Life", 0);
	lifeMax = ini->GetInteger(section, "LifeMax", 0);
	thew = ini->GetInteger(section, "Thew", 0);
	thewMax = ini->GetInteger(section, "ThewMax", 0);
	mana = ini->GetInteger(section, "Mana", 0);
	manaMax = ini->GetInteger(section, "ManaMax", 0);
	attack = ini->GetInteger(section, "Attack", 0);
	defend = ini->GetInteger(section, "Defend", 0);
	evade = ini->GetInteger(section, "Evade", 0);
	duck = ini->GetInteger(section, "Duck", 0);
	exp = ini->GetInteger(section, "Exp", 0);

	levelUpExp = ini->GetInteger(section, "LevelUpExp", 0);
	level = ini->GetInteger(section, "Level", 0);
	attackLevel = ini->GetInteger(section, "AttackLevel", 0);
	magicLevel = ini->GetInteger(section, "MagicLevel", 0);
	lum = ini->GetInteger(section, "Lum", nlNone);
	visionRadius = ini->GetInteger(section, "VisionRadius", 0);
	attackRadius = ini->GetInteger(section, "AttackRadius", 0);
	bodyIni = ini->Get(section, "BodyIni", "");
	flyIni = ini->Get(section, "FlyIni", "");
	flyIni2 = ini->Get(section, "FlyIni2", "");
	magicIni = ini->Get(section, "MagicIni", "");
	deathScript = ini->Get(section, "DeathScript", "");
	npcMagic = gm->magicManager.loadAttackMagic(flyIni);
	npcMagic2 = gm->magicManager.loadAttackMagic(flyIni2);
	initRes(npcIni);
	walkTime = getTime() + engine->getRand(NPC_WALK_INTERVAL_RANGE * 2);
	beginStand();

	if (name.compare("player") != 0)
	{
		name = "npc-" + npcName;
	}
}

void NPC::freeResource()
{
	freeNPCRes();
	npcMagic = nullptr;
	npcMagic2 = nullptr;
	if (gm != nullptr)
	{
		gm->magicManager.tryCleanAttackMagic();
	}
	stepList.resize(0);
}

void NPC::freeNPCRes()
{
	freeNPCAction(&res.stand);
	freeNPCAction(&res.stand1);
	freeNPCAction(&res.walk);
	freeNPCAction(&res.run);
	freeNPCAction(&res.jump);
	freeNPCAction(&res.attack);
	freeNPCAction(&res.attack1);
	freeNPCAction(&res.attack2);
	freeNPCAction(&res.magic);
	freeNPCAction(&res.hurt);
	freeNPCAction(&res.death);
	freeNPCAction(&res.sit);
	freeNPCAction(&res.special);
	freeNPCAction(&res.astand);
	freeNPCAction(&res.awalk);
	freeNPCAction(&res.arun);
	freeNPCAction(&res.ajump);
}

void NPC::freeNPCAction(NPCActionRes * act)
{
	if (act == nullptr)
	{
		return;
	}
	freeActionImage(act);
	act->imageFile = "";
	act->shadowFile = "";
	act->soundFile = "";
}

void NPC::freeActionImage(NPCActionRes * act)
{
	if (act == nullptr)
	{
		return;
	}
	act->image = nullptr;
	act->shadow = nullptr;
}

void NPC::onUpdate()
{
	auto ft = getFrameTime();
	updateTime = getTime();

	if (running)
	{
		if (isStanding())
		{
			running = false;
		}
	}

	if (frozen && nowAction != acDeath)
	{
		if (frozenLastTime >= ft)
		{
			frozenLastTime -= ft;
			timer.set(getUpdateTime() - ft);
			updateTime -= ft;
			LastFrameTime -= ft;
		}
		else
		{
			frozen = false;
			auto lastT = ft - frozenLastTime;
			timer.set(getUpdateTime() - frozenLastTime);
			updateTime -= frozenLastTime;
			LastFrameTime -= frozenLastTime;
			frozenLastTime = 0;
			updateAction(lastT);
		}
	}
	else
	{
		frozen = false;
		updateAction(ft);
	}
	
	if (kind != nkBattle && scriptFile == "")
	{
		//设置无用人物检测范围到屏幕外
		rect.x = -rect.w - 100;
		rect.y = -rect.h - 100;
	}
	else if (kind == nkBattle && relation == nrFriendly && scriptFile == "")
	{
		//设置无用人物检测范围到屏幕外
		rect.x = -rect.w - 100;
		rect.y = -rect.h - 100;
	}
	else
	{
		int w, h;
		engine->getWindowSize(w, h);
		Point cenScreen;
		cenScreen.x = (int)w / 2;
		cenScreen.y = (int)h / 2;
		int xscal, yscal;
		xscal = cenScreen.x / TILE_WIDTH + 3;
		yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
		int tileHeightScal = 10;
		Point cenTile = gm->camera->position;

		if (position.x >= cenTile.x - xscal && position.x < cenTile.x + xscal && position.y >= cenTile.y - yscal && position.y < cenTile.y + yscal + tileHeightScal)
		{
			PointEx posoffset;
			posoffset.x = (gm->camera->offset.x - offset.x);
			posoffset.y = (gm->camera->offset.y - offset.y);
			Point pos = Map::getTilePosition(position, cenTile, cenScreen, posoffset);

			int ox = 0, oy = 0, iw = 0, ih = 0;
			engine->getImageSize(getActionImage(&ox, &oy), iw, ih);
			rect.w = iw;
			rect.h = ih;
			rect.x = pos.x - ox;
			rect.y = pos.y - oy;
		}
		else
		{
			rect.x = -rect.w - 100;
			rect.y = -rect.h - 100;
		}
	}

	if (running)
	{
		if (isStanding())
		{
			running = false;
		}
	}
}

void NPC::updateAction(UTime frameTime)
{
	if (isStanding())
	{
		if (haveDest)
		{
			if (position != gotoExDest)
			{	
				if (gm->map->canWalk(gotoExDest))
				{
					beginWalk(gotoExDest);
				}
			}
			else
			{
				haveDest = false;
			}
		}	
		else if (followNPC != "" || kind == nkPartner)
		{
			std::shared_ptr<NPC> fnpc = nullptr;
			if (kind == nkPartner)
			{
				fnpc = gm->player;
			}
			else
			{
				auto fnpcList = gm->npcManager->findNPC(followNPC);
				if (fnpcList.size() > 0)
				{
					fnpc = fnpcList[0];
				}				 
			}
			
			if (fnpc != nullptr || kind == nkPartner)
			{
				if (isFollowAttack(fnpc) && kind != nkPartner)
				{
					if (gm->map->calDistance(position, fnpc->position) > attackRadius)
					{
						beginFollowAttack(fnpc->position);
					}
					else
					{
						beginAttack(fnpc->position, fnpc);
					}
				}
				else
				{
					if (gm->map->calDistance(position, fnpc->position) > NPC_FOLLOW_RADIUS)
					{
						beginFollowWalk(fnpc->position);
					}
				}
			}
			else
			{
				followNPC = "";
			}
		}	
		else if (isStanding() && getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			actionBeginTime += actionLastTime;
			if (nowAction == acStand)
			{
				if (engine->getRand(5) == 0 && canDoAction(&res.stand1))
				{
					nowAction = acStand1;
					actionLastTime = getActionTime(acStand1);
					playSound(acStand1);
				}
				else
				{
					playSound(acStand);
				}
			}
			else
			{
				if (engine->getRand(5) == 0)
				{
					nowAction = acStand;
					actionLastTime = getActionTime(acStand);
					playSound(acStand);
				}
				else
				{
					playSound(acStand1);
				}
			}
		}
	}
	else if (isWalking())
	{
		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			playSound(acWalk);
			actionBeginTime += actionLastTime;
		}
		if (getUpdateTime() - stepBeginTime >= stepLastTime)
		{
			if (stepState == ssIn)
			{			
				bool canWalkNextStep = false;
				stepList.erase(stepList.begin());
				if (followNPC != "" || kind == nkPartner)
				{
					canWalkNextStep = true;
					std::shared_ptr<NPC> fnpc = nullptr;
					if (kind == nkPartner)
					{
						fnpc = gm->player;
					}
					else
					{
						auto fnpcList = gm->npcManager->findNPC(followNPC);
						if (fnpcList.size() > 0)
						{
							fnpc = fnpcList[0];
						}
					}

					if (fnpc != nullptr || kind == nkPartner)
					{
						if (isFollowAttack(fnpc) && kind != nkPartner)
						{
							offset = { 0, 0 };
							stepList.resize(0);
							if (gm->map->calDistance(position, fnpc->position) > attackRadius)
							{
								changeFollowAttack(fnpc->position);

							}
							else
							{
								beginAttack(fnpc->position, fnpc);
								return;
							}
						}
						else
						{
							offset = { 0, 0 };
							stepList.resize(0);
							if (gm->map->calDistance(position, fnpc->position) > NPC_FOLLOW_RADIUS)
							{
								changeFollowWalk(fnpc->position);						
							}
							else
							{
								beginStand();
								return;
							}
						}
					}
					else
					{
						followNPC = "";
					}
				}
				if (stepList.size() > 0 && (gm->map->canWalk(stepList[0])))
				{
					canWalkNextStep = true;
					direction = calDirection(stepList[0]); 
					if (direction % 2 == 0)
					{
						int dir1 = direction + 1;
						int dir2 = direction - 1;

						limitDir(&dir1);
						limitDir(&dir2);
						if (gm->map->canPass(gm->map->getSubPoint(position, dir1)) && gm->map->canPass(gm->map->getSubPoint(position, dir2)))
						{
							canWalkNextStep = true;
							stepState = ssOut;
							gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
							direction = calDirection(stepList[0]);
							stepBeginTime += stepLastTime;
							calStepLastTime();
							calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
						}
						else
						{
							canWalkNextStep = false;
						}
					}
					else
					{
						canWalkNextStep = true;
						stepState = ssOut;
						stepBeginTime += stepLastTime;
						gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
						direction = calDirection(stepList[0]);	
						calStepLastTime();
						calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
					}
					
				}
				if (!canWalkNextStep)
				{
					stepList.resize(0);
					nowAction = acStand;
					actionBeginTime = stepBeginTime + stepLastTime;
					actionLastTime = getActionTime(acStand);
					offset.x = 0;
					offset.y = 0;
				}
			}
			else if (stepState == ssOut)
			{
				gm->map->deleteNPCFromDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				gm->map->deleteStepFromDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				position = stepList[0];
				gm->map->addNPCToDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				stepState = ssIn;
				stepBeginTime += stepLastTime;
				calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
			}
		}
		else
		{
			calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
		}
	}
	else if (isAttacking())
	{
		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			doAttack(attackDest, attackTarget);
			beginStand();
		}
	}
	else if (isHurting())
	{
		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			beginStand();
		}
	}
	else if (isDying())
	{
		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			result |= erLifeExhaust;
			nowAction = acHide;
		}
	}
	else if (isDoingSpecialAction())
	{
		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			beginStand();
		}
	}
}

void NPC::onEvent()
{
	if (touchingID != TOUCH_UNTOUCHEDID)
	{
		selecting = true;
	}
	else
	{
		selecting = false;
	}
}

void NPC::onMouseLeftDown(int x, int y)
{
#ifdef __MOBILE__
    auto player = gm->player;
    if (player->nowAction != acDeath && player->nowAction != acHide)
    {
        NextAction act;
        if (player->canRun && (player->thew > (int)round((double)player->info.thewMax * MIN_THEW_RATE_TO_RUN)  || player->thew > MIN_THEW_LIMIT_TO_RUN))
        {
            act.action = acRun;
        }
        else
        {
            act.action = acWalk;
        }
        act.destGE = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
        if (kind == nkBattle && relation == nrHostile)
        {
            act.type = ndAttack;
        }
        else
        {
            act.type = ndTalk;
        }
        act.dest = position;
        player->addNextAction(act);
    }
#endif
}

void NPC::onMouseLeftUp(int x, int y)
{
}

void NPC::onMouseMoveIn(int x, int y)
{

}

void NPC::onMouseMoveOut()
{
	selecting = false;
}
