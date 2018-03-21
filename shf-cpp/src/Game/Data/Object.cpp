#include "Object.h"
#include "../GameManager/GameManager.h"


Object::Object()
{
	priority = epOBJ;
	onlyCheckRect = true;
	rect.w = TILE_WIDTH;
	rect.h = (int)((double)TILE_HEIGHT * 3);
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
	if (res.image != NULL)
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
	if (res.image != NULL)
	{
		res.image->directions = 1;
	}
	nowAction = oaClosing;
	actionBeginTime = getUpdateTime();
	actionLastTime = IMP::getIMPImageActionTime(res.image);
}

unsigned int Object::getUpdateTime()
{
	return updateTime;
}

void Object::drawAlpha(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	if (selected && scriptFile != "")
	{
		_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 200, 0, 150);
		engine->setImageAlpha(image, 255);
	}
	else
	{
		_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
		engine->setImageAlpha(image, 255);
	}
}

void Object::draw(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
	image = getActionImage(&offsetX, &offsetY);
	if (!selected)
	{
		engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
	}
	else
	{
		if (scriptFile != "")
		{		
			engine->drawImageWithMaskEx(image, pos.x - offsetX, pos.y - offsetY, 200, 200, 0, 150);
		}
		else
		{
			engine->drawImage(image, pos.x - offsetX, pos.y - offsetY);
		}
	}
}

_image Object::getActionImage(int * offsetx, int * offsety)
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
			return IMP::loadImageForTime(res.image, getUpdateTime() - actionBeginTime, offsetx, offsety);
			break;
		case oaClosing:
			return IMP::loadImageForTime(res.image, actionLastTime - (getUpdateTime() - actionBeginTime), offsetx, offsety);
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

_image Object::getActionShadow(int * offsetx, int * offsety)
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
	char * s = NULL;
	int len = PakFile::readFile(SOUND_FOLDER + fileName, &s);
	if (len <= 0 || s == NULL)
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
	delete[] s;
}

void Object::initRes(const std::string & fileName)
{
	freeRes();
	std::string iniName = OBJECT_RES_INI_FOLDER + fileName;
	char * s = NULL;
	int len = PakFile::readFile(iniName, &s);
	if (len <= 0 || s == NULL)
	{
		return;
	}
	INIReader * ini = new INIReader(s);
	delete[] s;
	std::string section = "common";
	res.imageFile = ini->Get(section, "Image", "");
	res.shadowFile = ini->Get(section, "Shade", "");
	res.soundFile = ini->Get(section, "Sound", "");
	res.image = gm->objectManager.loadObjectImage(res.imageFile);
	res.shadow = gm->objectManager.loadObjectImage(res.shadowFile);
}

void Object::saveToIni(INIReader * ini, const std::string & section)
{
	if (ini == NULL)
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
	ini->SetInteger(section, "OffsetX", (int)offset.x);
	ini->SetInteger(section, "OffsetY", (int)offset.y);

	ini->SetInteger(section, "Lum", lum);
	ini->SetInteger(section, "Damage", damage);
	ini->SetInteger(section, "Frame", frame);
	ini->SetInteger(section, "State", state);
}

void Object::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	if (ini == NULL)
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
	state = ini->GetInteger(section, "State", 0);

	initRes(objectFile);
	initSound(wavFile);


	randSoundTime = getTime();
	actionBeginTime = getTime();
	if (kind == okTrap)
	{
		damageTime = getTime();
		if (res.image != NULL)
		{
			res.image->directions = 1;		
		}
		damageInterval = IMP::getIMPImageActionTime(res.image);
		
	}

}

bool Object::mouseInRect()
{
	_image image = getActionImage(NULL, NULL);
	int mx = 0, my = 0;
	engine->getMouse(&mx, &my);
	if (engine->pointInImage(image, mx - rect.x, my - rect.y))
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
	if (channel != NULL)
	{
		engine->stopMusic(channel);
		channel = NULL;
	}
	if (sound != NULL)
	{
		engine->freeMusic(sound);
		sound = NULL;
	}
}

void Object::freeRes()
{
	res.imageFile = "";
	res.shadowFile = "";
	res.soundFile = "";
	res.image = NULL;
	res.shadow = NULL;
}

void Object::onUpdate()
{
	unsigned int ft = getFrameTime();
	updateTime = getTime();
	int w, h;
	engine->getWindowSize(&w, &h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 3;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;
	Point cenTile = gm->camera.position;

	if (position.x >= cenTile.x - xscal && position.x < cenTile.x + xscal && position.y >= cenTile.y - yscal && position.y < cenTile.y + yscal + tileHeightScal)
	{
		PointEx posoffset;
		posoffset.x = (gm->camera.offset.x - offset.x);
		posoffset.y = (gm->camera.offset.y - offset.y);
		Point pos = Map::getTilePosition(position, cenTile, cenScreen, posoffset);
		if (scriptFile != "")
		{
			int ox = 0, oy = 0, iw = 0, ih = 0;
			engine->getImageSize(getActionImage(&ox, &oy), &iw, &ih);
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

		if (( kind == okSound) && (channel != NULL))
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
		if (kind == okSound && channel != NULL)
		{
			engine->setMusicPosition(channel, SOUND_FAREST, SOUND_FAREST);
		}	
	}

	if (kind == okTrap && damageInterval > 0)
	{
		if (getUpdateTime() - damageTime > damageInterval)
		{
			damageTime += damageInterval;
			if (!gm->inEvent && !(gm->player.isJumping() && gm->player.jumpState == jsJumping))
			{
				if (gm->player.position.x == position.x && gm->player.position.y == position.y)
				{
					gm->player.hurtLife(damage);
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
				if (res.image != NULL)
				{
					frame = res.image->frameCount - 1;
				}
			}
			else
			{
				if (res.image != NULL)
				{
					frame = 0;
				}
			}
			nowAction = oaStay;
		}
	}

}

void Object::onEvent()
{
	
	if (mouseMoveIn)
	{
		selected = true;
	}
	else
	{
		selected = false;
	}
	
}

bool Object::onHandleEvent(AEvent * e)
{
	if (e->eventType == MOUSEDOWN)
	{
		if (e->eventData == MOUSE_LEFT)
		{
			result = erMouseLDown;
			return true;
		}
		else if (e->eventData == MOUSE_RIGHT)
		{
			result = erMouseRDown;
			return true;
		}
	}
	return false;
}

