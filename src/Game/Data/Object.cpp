#include "Object.h"
#include "../GameManager/GameManager.h"


Object::Object()
{
	priority = epOBJ;
	rect.w = TILE_WIDTH;
	rect.h = (int)((float)TILE_HEIGHT * 3);
}

Object::~Object()
{
	freeResource();
}

void Object::openBox()
{
	if (kind != okBox)
	{
		return;
	}
	if (res.image != nullptr)
	{
		res.image->directions = 1;
	}
	nowAction = oaOpening;
	actionBeginTime = getUpdateTime();
	actionLastTime = IMP::getIMPImageActionTime(res.image);
}

void Object::closeBox()
{
	if (kind != okBox)
	{
		return;
	}
	if (res.image != nullptr)
	{
		res.image->directions = 1;
	}
	nowAction = oaClosing;
	actionBeginTime = getUpdateTime();
	actionLastTime = IMP::getIMPImageActionTime(res.image);
}

UTime Object::getUpdateTime()
{
	return updateTime;
}

void Object::drawAlpha(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	if (selecting && scriptFile != "")
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 200, 0, 150);
		engine->setImageAlpha(image, 255);
	}
	else
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
		engine->setImageAlpha(image, 255);
	}
}

void Object::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
	image = getActionImage(&offsetX, &offsetY);
	if (selecting && scriptFile != "")
	{
		engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 200, 0, 150);
	}
	else
	{
		if ((colorStyle & 0xFFFFFF) == 0xFFFFFF)
		{
			engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
		}
		else
		{
			engine->drawImageWithColor(image, pos.x - offsetX, pos.y - offsetY, (colorStyle >> 16) & 0xFF, (colorStyle >> 8) & 0xFF, colorStyle & 0xFF);
		}
		
	}
}

_shared_image Object::getActionImage(int * offsetx, int * offsety)
{
	switch (kind)
	{
	case okOrnament:
		return IMP::loadImageForDirection(res.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case okBox:
		switch (nowAction)
		{
		case oaStay:
			return IMP::loadImage(res.image, frame, offsetx, offsety);
			break;
		case oaPlaying:
			return IMP::loadImageForDirection(res.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
			break;
		case oaOpening:
			return IMP::loadImageForTime(res.image, getUpdateTime() - actionBeginTime, offsetx, offsety, true);
			break;
		case oaClosing:
			return IMP::loadImageForTime(res.image, getUpdateTime() - actionBeginTime, offsetx, offsety, true, true);
			break;
		default:
			return nullptr;
			break;
		}
		break;
	case okBody:
		return IMP::loadImageForDirection(res.image, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case okSound:
	case okRndSound:
	case okDoor:
		return IMP::loadImage(res.image, frame, offsetx, offsety);
		break;
	case okTrap:
		return IMP::loadImageForTime(res.image, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	default:
		break;
	}
	return nullptr;
}

_shared_image Object::getActionShadow(int * offsetx, int * offsety)
{
	switch (kind)
	{
	case okOrnament:
		return IMP::loadImageForDirection(res.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case okBox:
		switch (nowAction)
		{
		case oaStay:
			return IMP::loadImage(res.shadow, frame, offsetx, offsety);
			break;
		case oaPlaying:
			return IMP::loadImageForDirection(res.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
			break;
		case oaOpening:
			return IMP::loadImageForTime(res.shadow, getUpdateTime() - actionBeginTime, offsetx, offsety);
			break;
		case oaClosing:
			return IMP::loadImageForTime(res.shadow, actionLastTime - (getUpdateTime() - actionBeginTime), offsetx, offsety);
			break;
		default:
			return nullptr;
			break;
		}
		break;
	case okBody:
		return IMP::loadImageForDirection(res.shadow, direction, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	case okSound:
	case okRndSound:
	case okDoor:
		return IMP::loadImage(res.shadow, frame, offsetx, offsety);
		break;
	case okTrap:
		return IMP::loadImageForTime(res.shadow, getUpdateTime() - actionBeginTime, offsetx, offsety);
		break;
	default:
		break;
	}
	return nullptr;
}

void Object::initSound(const std::string & fileName)
{
	if (fileName == "")
	{
		return;
	}
	freeSound();
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(SOUND_FOLDER + fileName, s);
	if (len <= 0 || s == nullptr)
	{
		return;
	}
	if (kind == okSound)
	{
		sound = engine->loadCircleSound(s, len);
		channel = engine->playSound(sound, SOUND_FAREST, SOUND_FAREST);
	}
	else if (kind == okRndSound)
	{
		sound = engine->loadSound(s, len);
		randSoundTime = getUpdateTime();
	}
}

void Object::initRes(const std::string & fileName)
{
	freeRes();
	std::string iniName = OBJECT_RES_INI_FOLDER + fileName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(iniName, s);
	if (len <= 0 || s == nullptr)
	{
		return;
	}
	INIReader ini(s);

	std::string section = "common";
	res.imageFile = ini.Get(section, "Image", "");
	res.shadowFile = ini.Get(section, "Shade", "");
	res.soundFile = ini.Get(section, "Sound", "");
	res.image = gm->objectManager->loadObjectImage(res.imageFile);
	res.shadow = gm->objectManager->loadObjectImage(res.shadowFile);
}

void Object::saveToIni(INIReader * ini, const std::string & section)
{
	if (ini == nullptr)
	{
		return;
	}

	ini->Set(section, "ObjName", objName);
	ini->Set(section, "ObjFile", objectFile);
	ini->Set(section, "ScriptFile", scriptFile);
	ini->Set(section, "WavFile", wavFile);
	ini->SetInteger(section, "Kind", kind);
	ini->SetInteger(section, "Dir", direction);
	ini->SetInteger(section, "MapX", position.x);
	ini->SetInteger(section, "MapY", position.y);
	ini->SetInteger(section, "OffsetX", (int)round(offset.x));
	ini->SetInteger(section, "OffsetY", (int)round(offset.y));

	ini->SetInteger(section, "Lum", lum);
	ini->SetInteger(section, "Damage", damage);
	ini->SetInteger(section, "Frame", frame);
	ini->SetInteger(section, "State", nowAction);
	ini->SetTime(section, "ActionTime", getUpdateTime() - actionBeginTime);
}

void Object::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	if (ini == nullptr)
	{
		return;
	}

	objName = ini->Get(section, "ObjName", "");
	objectFile = ini->Get(section, "ObjFile", "");
	scriptFile = ini->Get(section, "ScriptFile", "");
	wavFile = ini->Get(section, "WavFile", "");
	kind = ini->GetInteger(section, "Kind", okBody);
	direction = ini->GetInteger(section, "Dir", 0);
	position.x = ini->GetInteger(section, "MapX", 0);
	position.y = ini->GetInteger(section, "MapY", 0);
	offset.x = (float)ini->GetInteger(section, "OffsetX", 0);
	offset.y = (float)ini->GetInteger(section, "OffsetY", 0);

	lum = ini->GetInteger(section, "Lum", olNone);
	damage = ini->GetInteger(section, "Damage", 0);
	frame = ini->GetInteger(section, "Frame", 0);
	state = ini->GetInteger(section, "State", oaStay);
	actionBeginTime = getUpdateTime() - ini->GetTime(section, "ActionTime", 0);

	initRes(objectFile);
	initSound(wavFile);


	randSoundTime = getTime();
	actionBeginTime = getTime();
	if (kind == okTrap)
	{
		damageTime = getTime();
		if (res.image != nullptr)
		{
			res.image->directions = 1;		
		}
		damageInterval = IMP::getIMPImageActionTime(res.image);
		
	}

}

bool Object::mouseInRect(int x, int y)
{
	_shared_image image = getActionImage(nullptr, nullptr);
	if (Element::mouseInRect(x, y) && engine->pointInImage(image, x - rect.x, y - rect.y))
	{
		return true;
	}
	else
	{
		return false;
	}
}

void Object::freeResource()
{
	freeRes();
	freeSound();
}

void Object::freeSound()
{
	if (channel != nullptr)
	{
		engine->stopMusic(channel);
		channel = nullptr;
	}
	if (sound != nullptr)
	{
		engine->freeMusic(sound);
		sound = nullptr;
	}
}

void Object::freeRes()
{
	res.imageFile = "";
	res.shadowFile = "";
	res.soundFile = "";
	res.image = nullptr;
	res.shadow = nullptr;
}

void Object::onUpdate()
{
	UTime ft = getFrameTime();
	updateTime = getTime();
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
		if (scriptFile != "")
		{
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

		if (( kind == okSound) && (channel != nullptr))
		{
			engine->setMusicPosition(channel, SOUND_FACTOR * (float)(pos.x - cenScreen.x) / (float)TILE_WIDTH, SOUND_FACTOR * (float)(pos.y - cenScreen.y) / (float)TILE_HEIGHT);
		}
		else if (kind == okRndSound)
		{
			if (getUpdateTime() - randSoundTime > SOUND_RAND_INTERVAL)
			{
				randSoundTime = getUpdateTime();
				channel = engine->playSound(sound, SOUND_FACTOR * (float)(pos.x - cenScreen.x) / (float)TILE_WIDTH, SOUND_FACTOR * (float)(pos.y - cenScreen.y) / (float)TILE_HEIGHT);
			}
			else
			{
				engine->setMusicPosition(channel, SOUND_FACTOR * (float)(pos.x - cenScreen.x) / (float)TILE_WIDTH, SOUND_FACTOR * (float)(pos.y - cenScreen.y) / (float)TILE_HEIGHT);
			}
		}
	}
	else
	{
		rect.x = -rect.w - 100;
		rect.y = -rect.h - 100;
		if (kind == okSound && channel != nullptr)
		{
			engine->setMusicPosition(channel, SOUND_FAREST, SOUND_FAREST);
		}	
	}

	if (kind == okTrap && damageInterval > 0)
	{
		if (getUpdateTime() - damageTime > damageInterval)
		{
			damageTime += damageInterval;
			if (!gm->inEvent && !(gm->player->isJumping() && gm->player->jumpState == jsJumping))
			{
				if (gm->player->position == position)
				{
					gm->player->hurtLife(damage);
				}
			}
		}
	}

	if (nowAction == oaOpening || nowAction == oaClosing)
	{
		if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
			if (nowAction == oaOpening)
			{
				if (res.image != nullptr)
				{
					frame = res.image->frame.size() - 1;
				}
			}
			else
			{
				if (res.image != nullptr)
				{
					frame = 0;
				}
			}
			nowAction = oaStay;
		}
	}

}

void Object::onMouseLeftDown(int x, int y)
{
#ifdef __MOBILE__
	auto player = gm->player;
	NextAction act;
    if (player->canRun && (player->thew > (int)round((float)player->info.thewMax * MIN_THEW_RATE_TO_RUN)  || player->thew > MIN_THEW_LIMIT_TO_RUN))
    {
		act.action = acRun;
	}
	else
	{
		act.action = acWalk;
	}
	act.destGE = std::dynamic_pointer_cast<Object>(getMySharedPtr());
	act.type = ndObj;
	act.dest = position;
	player->addNextAction(act);
#endif
}

void Object::onEvent()
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

void Object::onMouseMoveOut()
{
	selecting = false;
}



