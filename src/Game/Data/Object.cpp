#include "Object.h"
#include "../../Engine/Engine.h"
#include "../../Engine/AudioDecodeSafety.h"
#include "ColorStyle.h"
#include "MediaPathResolver.h"
#include "MobileTouchInteraction.h"
#include "../GameManager/GameManager.h"

#include <charconv>
#include <cctype>

namespace
{
	int getObjectDirectionFrameIndex(_shared_imp image, int direction, bool lastFrame)
	{
		if (image == nullptr || image->frame.empty())
		{
			return 0;
		}

		int directions = image->directions;
		if (directions < 1)
		{
			directions = 1;
		}
		if (direction < 0)
		{
			direction = 0;
		}
		if (direction >= directions)
		{
			direction = direction % directions;
		}

		int framePerDirection = (int)image->frame.size() / directions;
		if (framePerDirection <= 0)
		{
			framePerDirection = 1;
		}

		int index = direction * framePerDirection;
		if (lastFrame)
		{
			index += framePerDirection - 1;
		}
		if (index < 0)
		{
			index = 0;
		}
		if (index >= (int)image->frame.size())
		{
			index = (int)image->frame.size() - 1;
		}
		return index;
	}

	UTime readNonNegativeTime(INIReader * ini, const std::string & section, const std::string & name, UTime defaultValue)
	{
		if (ini == nullptr)
		{
			return defaultValue;
		}
		std::string text = ini->Get(section, name, "");
		auto begin = text.begin();
		auto end = text.end();
		while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0)
		{
			++begin;
		}
		while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0)
		{
			--end;
		}
		if (begin == end || *begin == '-')
		{
			return defaultValue;
		}
		UTime value = 0;
		const char* first = &*begin;
		const char* last = first + (end - begin);
		auto result = std::from_chars(first, last, value, 10);
		return result.ec == std::errc() && result.ptr == last ? value : defaultValue;
	}

	UTime readPositiveTime(INIReader * ini, const std::string & section, const std::string & name, UTime defaultValue)
	{
		const UTime value = readNonNegativeTime(ini, section, name, defaultValue);
		return value > 0 ? value : defaultValue;
	}
}

Object::Object()
{
	setPriority(epOBJ);
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
	nowAction = oaOpening;
	restartActionClock();
	actionLastTime = IMP::getIMPImageActionTime(res.image);
}

void Object::closeBox()
{
	if (kind != okBox)
	{
		return;
	}
	nowAction = oaClosing;
	restartActionClock();
	actionLastTime = IMP::getIMPImageActionTime(res.image);
}

std::string Object::getScriptFile(bool useRightScript) const
{
	if (useRightScript && scriptFileRight != "")
	{
		return scriptFileRight;
	}
	return scriptFile;
}

bool Object::hasInteractScript(bool useRightScript) const
{
	return getScriptFile(useRightScript) != "";
}

bool Object::hasAnyInteractScript() const
{
	return scriptFile != "" || scriptFileRight != "";
}

bool Object::shouldUseRightScriptForPrimaryInteraction() const
{
	return shouldUseObjectRightScriptForPrimaryInteraction(scriptFile, scriptFileRight);
}

bool Object::canSelectForInteraction() const
{
	return canSelectObjectForInteraction(scriptFileJustTouch, hasAnyInteractScript());
}

UTime Object::getTrapDamageElapsedMilliseconds() const
{
	if (damageTime > getTime())
	{
		return 0;
	}
	return getTime() - damageTime;
}

UTime Object::getActionElapsedMilliseconds() const
{
	return combineObjectActionElapsed(actionElapsedBase, getTime(), actionBeginTime);
}

void Object::restartActionClock()
{
	actionElapsedBase = 0;
	actionBeginTime = getTime();
}

void Object::drawAlpha(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	if (selecting && canSelectForInteraction())
	{
		_shared_image image = getActionImage(&offsetX, &offsetY);
		engine->setImageAlpha(image, 128);
		engine->drawImageWithMaskEx(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, 200, 200, 0, 150);
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

void Object::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
	image = getActionImage(&offsetX, &offsetY);
	if (selecting && canSelectForInteraction())
	{
		engine->drawImageWithMaskEx(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, 200, 200, 0, 150);
	}
	else
	{
		ColorStyle::drawImage(engine, image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, colorStyle);
		
	}
}

_shared_image Object::getActionImage(int * offsetx, int * offsety)
{
	switch (kind)
	{
	case okOrnament:
		return IMP::loadImageForDirection(res.image, direction, getActionElapsedMilliseconds(), offsetx, offsety);
		break;
	case okBox:
		switch (nowAction)
		{
		case oaStay:
			return IMP::loadImage(res.image, frame, offsetx, offsety);
			break;
		case oaPlaying:
			return IMP::loadImageForDirection(res.image, direction, getActionElapsedMilliseconds(), offsetx, offsety);
			break;
		case oaOpening:
			return IMP::loadImageForDirection(res.image, direction, getActionElapsedMilliseconds(), offsetx, offsety, true);
			break;
		case oaClosing:
			return IMP::loadImageForDirection(res.image, direction, getActionElapsedMilliseconds(), offsetx, offsety, true, true);
			break;
		default:
			return nullptr;
			break;
		}
		break;
	case okBody:
	case okPickup:
	case okPickupLegacy:
		if (isPickupObjectKind(kind) && nowAction == oaPlaying && res.animation != nullptr)
		{
			UTime elapsed = getActionElapsedMilliseconds();
			return IMP::loadImageForDirection(res.animation, direction, elapsed, offsetx, offsety, true);
		}
		return IMP::loadImageForDirection(res.image, direction, getActionElapsedMilliseconds(), offsetx, offsety);
		break;
	case okSound:
	case okRndSound:
	case okDoor:
		return IMP::loadImage(res.image, frame, offsetx, offsety);
		break;
	case okTrap:
		return IMP::loadImageForTime(res.image, getActionElapsedMilliseconds(), offsetx, offsety);
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
		return IMP::loadImageForDirection(res.shadow, direction, getActionElapsedMilliseconds(), offsetx, offsety);
		break;
	case okBox:
		switch (nowAction)
		{
		case oaStay:
			return IMP::loadImage(res.shadow, frame, offsetx, offsety);
			break;
		case oaPlaying:
			return IMP::loadImageForDirection(res.shadow, direction, getActionElapsedMilliseconds(), offsetx, offsety);
			break;
		case oaOpening:
			return IMP::loadImageForDirection(res.shadow, direction, getActionElapsedMilliseconds(), offsetx, offsety, true);
			break;
		case oaClosing:
			return IMP::loadImageForDirection(res.shadow, direction, getActionElapsedMilliseconds(), offsetx, offsety, true, true);
			break;
		default:
			return nullptr;
			break;
		}
		break;
	case okBody:
	case okPickup:
	case okPickupLegacy:
		if (isPickupObjectKind(kind) && nowAction == oaPlaying && res.animationShadow != nullptr)
		{
			UTime elapsed = getActionElapsedMilliseconds();
			return IMP::loadImageForDirection(res.animationShadow, direction, elapsed, offsetx, offsety, true);
		}
		return IMP::loadImageForDirection(res.shadow, direction, getActionElapsedMilliseconds(), offsetx, offsety);
		break;
	case okSound:
	case okRndSound:
	case okDoor:
		return IMP::loadImage(res.shadow, frame, offsetx, offsety);
		break;
	case okTrap:
		return IMP::loadImageForTime(res.shadow, getActionElapsedMilliseconds(), offsetx, offsety);
		break;
	default:
		break;
	}
	return nullptr;
}

void Object::initSound(const std::string & fileName)
{
	freeSound();
	if (fileName.empty())
	{
		return;
	}
	std::unique_ptr<char[]> s;
	int len = 0;
	if (!File::readFile(resolveSoundAssetPath(fileName), s, len,
		static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes)) ||
		len <= 0 || s == nullptr)
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
		randSoundTime = getTime();
	}
}

void Object::setKind(int newKind)
{
	kind = newKind;

	if (kind == okSound || kind == okRndSound)
	{
		initSound(wavFile);
		randSoundTime = getTime();
	}
	else
	{
		freeSound();
	}

	if (kind == okTrap)
	{
		damageTime = getTime();
		lastTrapDamageCycle = OBJECT_TRAP_DAMAGE_CYCLE_UNSET;
		if (res.image != nullptr)
		{
			res.image->directions = 1;
		}
		damageInterval = IMP::getIMPImageActionTime(res.image);
	}
	else
	{
		damageTime = 0;
		damageInterval = 0;
		lastTrapDamageCycle = OBJECT_TRAP_DAMAGE_CYCLE_UNSET;
	}
}

void Object::initRes(const std::string & fileName)
{
	freeRes();
	auto readObjectResourceImage = [](const std::string& resourceFile, std::string& imageFile,
		std::string& shadowFile) -> bool
	{
		std::unique_ptr<char[]> data;
		int length = File::readFile(OBJECT_RES_INI_FOLDER + resourceFile, data);
		if (length <= 0 || data == nullptr)
		{
			return false;
		}
		INIReader ini(data);
		const std::string section = "common";
		imageFile = ini.Get(section, "Image", "");
		shadowFile = ini.Get(section, "Shade", "");
		return true;
	};

	std::unique_ptr<char[]> data;
	int length = fileName.empty() ? 0 : File::readFile(OBJECT_RES_INI_FOLDER + fileName, data);
	if (length > 0 && data != nullptr)
	{
		INIReader ini(data);
		const std::string section = "common";
		res.imageFile = ini.Get(section, "Image", "");
		res.shadowFile = ini.Get(section, "Shade", "");
		res.soundFile = ini.Get(section, "Sound", "");
		res.animationFile = ini.Get(section, "Animation", "");
	}
	if (res.animationFile.empty() && !objectFileMovie.empty())
	{
		readObjectResourceImage(objectFileMovie, res.animationFile, res.animationShadowFile);
	}
	res.image = gm->objectManager->loadObjectImage(res.imageFile);
	res.shadow = gm->objectManager->loadObjectImage(res.shadowFile);
	res.animation = gm->objectManager->loadObjectImage(res.animationFile);
	res.animationShadow = gm->objectManager->loadObjectImage(res.animationShadowFile);
}

void Object::saveToIni(INIReader * ini, const std::string & section)
{
	if (ini == nullptr)
	{
		return;
	}

	ini->Set(section, "ObjName", objName);
	ini->Set(section, "ObjFile", objectFile);
	if (!objectFileMovie.empty())
	{
		ini->Set(section, "ObjFileMovie", objectFileMovie);
	}
	ini->Set(section, "ScriptFile", scriptFile);
	ini->Set(section, "ScriptFileRight", scriptFileRight);
	ini->Set(section, "TimerScriptFile", timerScriptFile);
	ini->Set(section, "ReviveNpcIni", reviveNpcIni);
	ini->Set(section, "WavFile", wavFile);
	ini->SetInteger(section, "Kind", kind);
	if (objectType != 0)
	{
		ini->SetInteger(section, "Type", objectType);
	}
	ini->SetInteger(section, "Dir", direction);
	ini->SetInteger(section, "MapX", position.x);
	ini->SetInteger(section, "MapY", position.y);
	ini->SetInteger(section, "OffsetX", (int)round(offset.x));
	ini->SetInteger(section, "OffsetY", (int)round(offset.y));

	ini->SetInteger(section, "CanInteractDirectly", canInteractDirectly);
	ini->SetInteger(section, "ScriptFileJustTouch", scriptFileJustTouch);
	ini->SetTime(section, "TimerScriptInterval", timerScriptInterval);
	ini->SetTime(section, "MillisecondsToRemove", millisecondsToRemove);
	ini->SetInteger(section, "Height", height);
	ini->SetInteger(section, "Lum", lum);
	ini->SetInteger(section, "Damage", damage);
	ini->SetInteger(section, "Frame", frame);
	ini->SetInteger(section, "State", nowAction);
	ini->SetTime(section, "ActionTime", getActionElapsedMilliseconds());
}

void Object::initFromIni(INIReader * ini, const std::string & section)
{
	freeResource();
	if (ini == nullptr)
	{
		return;
	}

	objName = ini->Get(section, "ObjName", ini->Get(section, "Name", ""));
	objectFile = ini->Get(section, "ObjFile", "");
	objectFileMovie = ini->Get(section, "ObjFileMovie", "");
	scriptFile = ini->Get(section, "ScriptFile", "");
	scriptFileRight = ini->Get(section, "ScriptFileRight", "");
	timerScriptFile = ini->Get(section, "TimerScriptFile", "");
	reviveNpcIni = ini->Get(section, "ReviveNpcIni", "");
	wavFile = ini->Get(section, "WavFile", "");
	int loadedKind = ini->GetInteger(section, "Kind", okOrnament);
	objectType = ini->GetInteger(section, "Type", 0);
	direction = ini->GetInteger(section, "Dir", 0);
	position.x = ini->GetInteger(section, "MapX", 0);
	position.y = ini->GetInteger(section, "MapY", 0);
	offset.x = (float)ini->GetInteger(section, "OffsetX", ini->GetInteger(section, "OffX", 0));
	offset.y = (float)ini->GetInteger(section, "OffsetY", ini->GetInteger(section, "OffY", 0));

	canInteractDirectly = ini->GetInteger(section, "CanInteractDirectly", 0);
	scriptFileJustTouch = ini->GetInteger(section, "ScriptFileJustTouch", 0);
	timerScriptInterval = readPositiveTime(ini, section, "TimerScriptInterval", DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL);
	timerScriptElapsed = 0;
	millisecondsToRemove = readPositiveTime(ini, section, "MillisecondsToRemove", 0);
	height = ini->GetInteger(section, "Height", 0);
	lum = ini->GetInteger(section, "Lum", olNone);
	damage = ini->GetInteger(section, "Damage", 0);
	frame = ini->GetInteger(section, "Frame", 0);
	const bool hasPersistedState = !ini->Get(section, "State", "").empty();
	nowAction = ini->GetInteger(section, "State", oaStay);
	actionElapsedBase = readNonNegativeTime(ini, section, "ActionTime", 0);
	actionBeginTime = getTime();

	initRes(objectFile);
	setKind(loadedKind);
	if (shouldStartObjectResourceAnimation(kind, res.animation != nullptr, hasPersistedState))
	{
		nowAction = oaPlaying;
		restartActionClock();
	}
	if (isPickupObjectKind(kind) && nowAction == oaPlaying)
	{
		actionLastTime = IMP::getIMPImageActionTime(res.animation);
		if (actionLastTime == 0)
		{
			nowAction = oaStay;
		}
	}

	if (gm != nullptr && gm->map != nullptr && gm->map->data != nullptr)
	{
		if (!gm->map->isInMap(position))
		{
			position = gm->map->clampToWalkable(position);
		}
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

bool Object::applyTrapDamage()
{
	if (damage <= 0 || gm == nullptr || gm->inEvent)
	{
		return false;
	}

	if (gm->npcManager != nullptr)
	{
		for (const auto& npc : gm->npcManager->npcList)
		{
			if (npc == nullptr || npc == gm->player || !canObjectTrapDamageNpcKind(npc->kind))
			{
				continue;
			}
			if (npc->isDying() || npc->isHiding() || npc->getPosition() != position)
			{
				continue;
			}
			if (!gm->npcManager->findNPC(npc))
			{
				continue;
			}
			npc->hurtLife(damage);
		}
	}

	if (gm->player != nullptr
		&& !(gm->player->isJumping() && gm->player->getJumpState() == jsJumping)
		&& gm->player->getPosition() == position)
	{
		gm->player->hurtLife(damage);
	}
	return true;
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
	res.animationFile = "";
	res.animationShadowFile = "";
	res.image = nullptr;
	res.shadow = nullptr;
	res.animation = nullptr;
	res.animationShadow = nullptr;
}

void Object::onUpdate()
{
	UTime ft = getFrameTime();
	UTime actionElapsed = getActionElapsedMilliseconds();
	if (isObjectResourceAnimationFinished(kind, nowAction, actionElapsed, actionLastTime))
	{
		nowAction = oaStay;
		restartActionClock();
		actionLastTime = 0;
	}
	if (millisecondsToRemove > 0)
	{
		if (ft >= millisecondsToRemove)
		{
			millisecondsToRemove = 0;
			removeSelf();
			return;
		}
		millisecondsToRemove -= ft;
	}

	if (timerScriptFile != "" && timerScriptInterval > 0 && gm != nullptr)
	{
		timerScriptElapsed += ft;
		if (timerScriptElapsed >= timerScriptInterval)
		{
			timerScriptElapsed -= timerScriptInterval;
			auto self = std::dynamic_pointer_cast<Object>(getMySharedPtr());
			gm->runObjScript(self, timerScriptFile, false);
			if (gm->objectManager == nullptr || !gm->objectManager->findObj(self))
			{
				return;
			}
		}
	}

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
		if (canSelectForInteraction())
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
			if (engine->getSoundVolume() != soundVolume)
			{
				soundVolume = engine->getSoundVolume();
				engine->setMusicVolume(channel, soundVolume);
			}

			engine->setMusicPosition(channel,
				SOUND_FACTOR * (float)(pos.x - cenScreen.x) / (float)TILE_WIDTH,
				SOUND_FACTOR * (float)(pos.y - cenScreen.y) / (float)TILE_HEIGHT);
		}
		else if (kind == okRndSound)
		{
			float soundX = SOUND_FACTOR * (float)(pos.x - cenScreen.x) / (float)TILE_WIDTH;
			float soundY = SOUND_FACTOR * (float)(pos.y - cenScreen.y) / (float)TILE_HEIGHT;
			if (channel != nullptr && engine->getMusicPlaying(channel))
			{
				if (engine->getSoundVolume() != soundVolume)
				{
					soundVolume = engine->getSoundVolume();
					engine->setMusicVolume(channel, soundVolume);
				}
				engine->setMusicPosition(channel, soundX, soundY);
			}
			else if (getTime() - randSoundTime > SOUND_RAND_INTERVAL)
			{
				randSoundTime = getTime();
				channel = engine->playSound(sound, soundX, soundY);
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
		UTime elapsed = getTrapDamageElapsedMilliseconds();
		if (isObjectTrapDamageCycleDue(elapsed, damageInterval, lastTrapDamageCycle))
		{
			UTime cycle = getObjectTrapDamageCycle(elapsed, damageInterval);
			if (applyTrapDamage())
			{
				lastTrapDamageCycle = cycle;
			}
		}
	}

	if (nowAction == oaOpening || nowAction == oaClosing)
	{
		if (getActionElapsedMilliseconds() > actionLastTime)
		{
			if (nowAction == oaOpening)
			{
				if (res.image != nullptr)
				{
					frame = getObjectDirectionFrameIndex(res.image, direction, true);
				}
			}
			else
			{
				if (res.image != nullptr)
				{
					frame = getObjectDirectionFrameIndex(res.image, direction, false);
				}
			}
			nowAction = oaStay;
			restartActionClock();
		}
	}

}

void Object::onMouseLeftDown(int x, int y)
{
	// Finger interaction is owned by the hit Object. A physical mouse continues
	// through GameController's click-index path so one press cannot queue twice.
	if (touchingDownID == TOUCH_MOUSEID)
	{
		return;
	}
	if (gm == nullptr || gm->blocksWorldPointerInput())
	{
		return;
	}
	if (shouldDeferMobileRightScriptChoice(scriptFile, scriptFileRight))
	{
		return;
	}

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
	act.destKind = ndObj;
	act.dest = position;
	act.useRightScript = shouldUseRightScriptForPrimaryInteraction();
	if (gm->controller != nullptr)
	{
		gm->controller->cancelControllerWorldInteraction();
	}
	player->addNextAction(act);
}

void Object::onMouseLeftUp(int x, int y)
{
	if (touchingDownID == TOUCH_MOUSEID)
	{
		return;
	}
	if (gm == nullptr || gm->blocksWorldPointerInput())
	{
		return;
	}
	if (!shouldDeferMobileRightScriptChoice(scriptFile, scriptFileRight))
	{
		return;
	}
	auto self = std::dynamic_pointer_cast<Object>(getMySharedPtr());
	if (self == nullptr || gm == nullptr)
	{
		return;
	}

	Point delta = getTouchingDownMoveDelta(x, y);
	bool useRightScript = shouldUseMobileRightScript(getTouchingDownElapsedTime(), delta.x, delta.y);
	gm->queueObjectInteraction(self, useRightScript);
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

void Object::setPosition(Point newPos)
{
	if (position == newPos)
	{
		return;
	}
	
	gm->map->deleteObjectFromDataMap(position, std::dynamic_pointer_cast<Object>(getMySharedPtr()));
	position = newPos;
	gm->map->addObjectToDataMap(position, std::dynamic_pointer_cast<Object>(getMySharedPtr()));
}

void Object::setOffset(PointEx newOffset)
{
	offset = newOffset;
}

void Object::removeFromDataMap()
{
	if (gm == nullptr || gm->map == nullptr)
	{
		return;
	}
	
	std::shared_ptr<Object> self = std::dynamic_pointer_cast<Object>(getMySharedPtr());
	if (self == nullptr)
	{
		return;
	}
	
	gm->map->deleteObjectFromDataMap(position, self);
}

void Object::removeSelf()
{
	if (gm == nullptr || gm->objectManager == nullptr)
	{
		return;
	}

	std::shared_ptr<Object> self = std::dynamic_pointer_cast<Object>(getMySharedPtr());
	if (self == nullptr)
	{
		return;
	}

	gm->objectManager->deleteObject(self);
}
