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
	config = Config::getInstance();

	initFromIni("ini\\ui\\Option\\Window.ini");
	rtnBtn = addButton("ini\\ui\\Option\\BTReturn.ini");

	music = addScrollbar("ini\\ui\\Option\\SBMusic.ini");
	sound = addScrollbar("ini\\ui\\Option\\SBSound.ini");
	speed = addScrollbar("ini\\ui\\Option\\SBSpeed.ini");

	musicCB = addCheckBox("ini\\ui\\Option\\CBMusic.ini");
	soundCB = addCheckBox("ini\\ui\\Option\\CBSound.ini");
	speedCB = addCheckBox("ini\\ui\\Option\\CBSpeed.ini");

	player = addCheckBox("ini\\ui\\Option\\CBPlayer.ini");
	shadow = addCheckBox("ini\\ui\\Option\\CBShadow.ini");
	dyLoad = addCheckBox("ini\\ui\\Option\\CBDyLoad.ini");

	playerBg = addImageContainer("ini\\ui\\Option\\LBPlayer.ini");
	shadowBg = addImageContainer("ini\\ui\\Option\\LBShadow.ini");
	dyLoadBg = addImageContainer("ini\\ui\\Option\\LBDyLoad.ini");

	setChildRect();

	if (config->getMusicVolume() <= 0)
	{
		music->setPosition(music->min);
		musicCB->checked = true;
	}
	else
	{
		int pos = config->getMusicVolume() > 1.0f ? music->max : (int)(config->getMusicVolume() * (float)(music->max - music->min) + music->min);
		music->setPosition(pos);
		musicCB->checked = false;
	}
	musicPos = music->position;

	if (config->getSoundVolume() <= 0)
	{
		sound->setPosition(sound->min);
		soundCB->checked = true;
	}
	else
	{
		int pos = config->getSoundVolume() > 1.0f ? sound->max : (int)(config->getSoundVolume() * (float)(sound->max - sound->min) + sound->min);
		sound->setPosition(pos);
		soundCB->checked = false;
	}
	soundPos = sound->position;

	if (config->playerAlpha)
	{
		player->checked = false;
	}
	else
	{
		player->checked = true;
	}
	speedCB->activated = false;
	speedCB->checked = true;
	speed->activated = false;
	dyLoad->activated = false;
	dyLoad->checked = false;
	shadow->activated = false;
	shadow->checked = false;
}

void Option::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}

	freeCom(rtnBtn);
	freeCom(music);
	freeCom(sound);
	freeCom(speed);
	freeCom(player);
	freeCom(dyLoad);
	freeCom(shadow);
	freeCom(playerBg);
	freeCom(dyLoadBg);
	freeCom(shadowBg);
	freeCom(musicCB);
	freeCom(soundCB);
	freeCom(speedCB);

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
	}
	if (player != NULL && player->getResult(erClick))
	{
		if (player->checked)
		{
			config->playerAlpha = false;
		}
		else
		{
			config->playerAlpha = true;
		}
		config->save();
	}
	if (rtnBtn->getResult(erClick))
	{
		running = false;
		result = erOK;
	}
}

bool Option::onHandleEvent(AEvent * e)
{
	if (e->eventType == KEYDOWN)
	{
		if (e->eventData == KEY_ESCAPE)
		{
			running = false;
			result = erOK;
			return true;
		}
	}
	return false;
}
