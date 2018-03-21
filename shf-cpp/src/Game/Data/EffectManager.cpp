#include "EffectManager.h"



EffectManager::EffectManager()
{
	name = "EffectManager";
	priority = epEffectManager;
	effectList.resize(0);
	needArrangeChild = false;
	canDraw = false;
}

EffectManager::~EffectManager()
{
	freeResource();
}

void EffectManager::pauseAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		effectList[i]->setPaused(true);
	}
}

void EffectManager::resumeAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		effectList[i]->setPaused(false);
	}
}

void EffectManager::addEffect(Effect * effect)
{
	if (effect == NULL)
	{
		return;
	}
	effectList.push_back(effect);
	addChild(effect);
	effect->initTime();
}

void EffectManager::deleteEffect(Effect * effect)
{
	if (effect == NULL)
	{
		return;
	}
	removeEffect(effect);
	delete effect;
}

void EffectManager::removeEffect(Effect * effect)
{
	if (effect == NULL)
	{
		return;
	}
	int idx = -1;
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effect == effectList[i])
		{
			idx = i;
			removeChild(effect);
			effectList[i] = NULL;
			break;
		}
	}
	if (idx >= 0)
	{
		for (int i = idx; i < (int)effectList.size() - 1; i++)
		{
			effectList[i] = effectList[i + 1];
		}
		effectList.resize(effectList.size() - 1);
	}
}

void EffectManager::clearEffect()
{
	std::vector<Effect *> newList;
	newList.resize(0);
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != NULL)
		{
			unsigned int ret = effectList[i]->getResult();
			if (ret & erLifeExhaust)
			{
				removeChild(effectList[i]);
				delete effectList[i];
				effectList[i] = NULL;
			}
			else
			{
				newList.push_back(effectList[i]);
			}
		}
		else
		{
			removeChild(effectList[i]);
		}
	}
	effectList = newList;
}

void EffectManager::load()
{
	freeResource();
	std::string iniName = DEFAULT_FOLDER;
	iniName += EFFECT_INI;
	INIReader * ini = new INIReader(iniName);
	std::string section = "Head";
	int count = ini->GetInteger(section, "Count", 0);
	if (count < 0)
	{
		count = 0;
	}
	effectList.resize(count);
	for (int i = 0; i < count; i++)
	{
		section = convert::formatString("PRO%d", i + 1);
		effectList[i] = new Effect;
		addChild(effectList[i]);
		effectList[i]->initFromIni(ini, section);	
	}
	delete ini;
}

void EffectManager::save()
{
	INIReader * ini = new INIReader;
	int count = effectList.size();
	std::string section = "Head";
	ini->SetInteger(section, "Count", count);
	for (size_t i = 0; i < effectList.size(); i++)
	{
		section = convert::formatString("PRO%d", i + 1);
		effectList[i]->saveToIni(ini, section);
	}
	
	std::string iniName = DEFAULT_FOLDER;
	iniName += EFFECT_INI;
	ini->saveToFile(iniName);
	delete ini;
}

void EffectManager::disableAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != NULL)
		{
			effectList[i]->damage = 0;
		}
	}
}

EffectMap EffectManager::createMap(int x, int y, int w, int h)
{
	EffectMap map;
	map.tile.resize(0);
	if (w <= 0 || h <= 0)
	{
		return map;
	}
	map.tile.resize(h);
	for (int i = 0; i < h; i++)
	{
		map.tile[i].resize(w);
		for (int j = 0; j < w; j++)
		{
			map.tile[i][j].index.resize(0);
		}
	}
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != NULL && effectList[i]->position.x >= x && effectList[i]->position.y >= y && effectList[i]->position.x < x + w && effectList[i]->position.y < y + h)
		{
			map.tile[effectList[i]->position.y - y][effectList[i]->position.x - x].index.push_back(i);
		}
	}
	return map;
}

void EffectManager::freeResource()
{
	removeAllChild();
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != NULL)
		{
			delete effectList[i];
			effectList[i] = NULL;
		}		
	}
	effectList.resize(0);
}

void EffectManager::onUpdate()
{
	CollisionDetector::detectCollision();
	clearEffect();
}
