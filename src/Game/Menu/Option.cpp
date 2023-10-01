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

	player = addComponent<CheckBox>("ini\\ui\\option\\cbplayer.ini");
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

	player->activated = true;
	player->checked = !config->playerAlpha;

	speedCB->activated = false;
	speedCB->checked = true;
	speed->activated = false;
	dyLoad->activated = false;
	dyLoad->checked = false;
	
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
	if (player != nullptr && player->getResult(erClick))
	{
		config->playerAlpha = !player->checked;
		config->save();
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
