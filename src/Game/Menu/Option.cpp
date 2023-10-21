#include "Option.h"

Option::Option()
{
	init();	
}

Option::~Option()
{
	freeResource();
}

void Option::init()
{
	freeResource();

	initFromIniFileName("ini\\ui\\option\\window.ini");
	rtnBtn = addComponent<Button>("ini\\ui\\option\\btreturn.ini");

	music = addComponent<Scrollbar>("ini\\ui\\option\\sbmusic.ini");
	sound = addComponent<Scrollbar>("ini\\ui\\option\\sbsound.ini");
	speed = addComponent<Scrollbar>("ini\\ui\\option\\sbspeed.ini");

	musicCB = addComponent<CheckBox>("ini\\ui\\option\\cbmusic.ini");
	soundCB = addComponent<CheckBox>("ini\\ui\\option\\cbsound.ini");
	speedCB = addComponent<CheckBox>("ini\\ui\\option\\cbspeed.ini");

	playerAlpha = addComponent<CheckBox>("ini\\ui\\option\\cbplayer.ini");
	shadow = addComponent<CheckBox>("ini\\ui\\option\\cbshadow.ini");
	dyLoad = addComponent<CheckBox>("ini\\ui\\option\\cbdyload.ini");

	playerBg = addComponent<ImageContainer>("ini\\ui\\option\\lbplayer.ini");
	shadowBg = addComponent<ImageContainer>("ini\\ui\\option\\lbshadow.ini");
	dyLoadBg = addComponent<ImageContainer>("ini\\ui\\option\\lbdyload.ini");


	setChildRectReferToParent();

	if (Config::getMusicVolume() <= 0)
	{
		music->setPosition(music->min);
		musicCB->checked = true;
	}
	else
	{
		int pos = Config::getMusicVolume() > 1.0f ? music->max : (int)(Config::getMusicVolume() * (float)(music->max - music->min) + music->min);
		music->setPosition(pos);
		musicCB->checked = false;
	}
	musicPos = music->position;

	if (Config::getSoundVolume() <= 0)
	{
		sound->setPosition(sound->min);
		soundCB->checked = true;
	}
	else
	{
		int pos = Config::getSoundVolume() > 1.0f ? sound->max : (int)(Config::getSoundVolume() * (float)(sound->max - sound->min) + sound->min);
		sound->setPosition(pos);
		soundCB->checked = false;
	}
	soundPos = sound->position;

	playerAlpha->activated = true;
	playerAlpha->checked = !config->playerAlpha;

	speedCB->activated = true;
	speedCB->checked = false;
	speed->activated = true;

	speed->setPosition(speedToPos(Config::getGameSpeed()));

	dyLoad->activated = true;
	dyLoad->checked = !Config::loadWithThread;
	
	shadow->activated = false;
	shadow->checked = false;

	//speed->visible = false;
	//speedCB->visible = false;
	//dyLoad->visible = false;
	//shadow->visible = false;
	//dyLoadBg->visible = false;
	//shadowBg->visible = false;
}

void Option::freeResource()
{

	impImage = nullptr;

	freeCom(rtnBtn);
	freeCom(music);
	freeCom(sound);
	freeCom(speed);
	freeCom(playerAlpha);
	freeCom(dyLoad);
	freeCom(shadow);
	freeCom(playerBg);
	freeCom(dyLoadBg);
	freeCom(shadowBg);
	freeCom(musicCB);
	freeCom(soundCB);
	freeCom(speedCB);

}

int Option::speedToPos(double spd)
{
	if (spd <= SPEED_TIME_MIN)
	{
		return speed->min;
	}
	else if (spd >= SPEED_TIME_MAX)
	{
		return speed->max;
	}
	return (int)round((spd - SPEED_TIME_MIN) / (SPEED_TIME_MAX - SPEED_TIME_MIN) * (speed->max - speed->min) + speed->min);
}

double Option::posToSpeed(int pos)
{
	if (pos <= speed->min)
	{
		return SPEED_TIME_MIN;
	}
	else if (pos >= speed->max)
	{
		return SPEED_TIME_MAX;
	}
	return ((double)(pos - speed->min)) / (speed->max - speed->min) * (SPEED_TIME_MAX - SPEED_TIME_MIN) + SPEED_TIME_MIN;
}

void Option::onEvent()
{
	if (musicPos != music->position)
	{
		if (music->position > music->min)
		{
			musicCB->checked = false;
		}
		else
		{
			musicCB->checked = true;
		}
		musicPos = music->position;
		float volume = (((float)musicPos - music->min) / (float)(music->max - music->min));
		config->setMusicVolume(volume);
		config->save();		
	}
	if (soundPos != sound->position)
	{
		if (sound->position > sound->min)
		{
			soundCB->checked = false;
		}
		else
		{
			soundCB->checked = true;
		}
		soundPos = sound->position;
		float volume = (((float)soundPos - sound->min) / (float)(sound->max - sound->min));
		config->setSoundVolume(volume);
		config->save();
	}
	if (speedPos != speed->position)
	{
		if (speed->position > speed->min)
		{
			speedCB->checked = false;
		}
		else
		{
			speedCB->checked = true;
		}
		speedPos = speed->position;
		config->setGameSpeed(posToSpeed(speedPos));
		config->save();
	}
	if (musicCB->getResult(erClick))
	{
		if (musicCB->checked && music->position > music->min)
		{
			music->setPosition(music->min);
			musicPos = music->position;
			float volume = 0.0f;
			config->setMusicVolume(volume);
			config->save();
		}
		else if (!musicCB->checked && music->position < music->max)
		{
			music->setPosition(music->max);
			musicPos = music->position;
			float volume = 1.0f;
			config->setMusicVolume(volume);
			config->save();
		}
	}
	if (soundCB->getResult(erClick))
	{
		if (soundCB->checked && sound->position > sound->min)
		{
			sound->setPosition(sound->min);
			soundPos = sound->position;
			float volume = 0.0f;
			config->setSoundVolume(volume);
			config->save();
		}
		else if (!soundCB->checked && sound->position < sound->max)
		{
			sound->setPosition(sound->max);
			soundPos = sound->position;
			float volume = 1.0f;
			config->setSoundVolume(volume);
			config->save();
		}
	}
	if (speedCB->getResult(erClick))
	{
		auto default_pos = speedToPos(SPEED_TIME_DEFAULT);
		if (speed->position != default_pos)
		{
			speed->setPosition(default_pos);
			speedPos = speed->position;
			config->setGameSpeed(posToSpeed(default_pos));
			config->save();
		}
	}
	if (playerAlpha != nullptr && playerAlpha->getResult(erClick))
	{
		config->playerAlpha = !playerAlpha->checked;
		config->save();
	}
	if (dyLoad != nullptr && dyLoad->getResult(erClick))
	{
		Config::loadWithThread = !dyLoad->checked;
		Config::save();
	}
	if (rtnBtn->getResult(erClick))
	{
		running = false;
		result = erOK;
	}
}

bool Option::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_ESCAPE)
		{
			running = false;
			result = erOK;
			return true;
		}
	}
	return false;
}
