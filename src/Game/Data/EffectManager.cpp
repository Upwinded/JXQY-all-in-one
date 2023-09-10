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
	if (effect == nullptr)
	{
		return;
	}
	effectList.push_back(effect);
	addChild(effect);
	effect->initTime();
}

void EffectManager::deleteEffect(Effect * effect)
{
	if (effect == nullptr)
	{
		return;
	}
	delete effect;
}

void EffectManager::removeEffect(Effect * effect)
{
	if (effect == nullptr)
	{
		return;
	}

	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effect == effectList[i])
		{
			effectList.erase(effectList.begin() + i);
            removeChild(effect);
			break;
		}
	}
}

void EffectManager::clearEffect()
{
	std::vector<Effect*> newList, deleteList;
	newList.resize(0);
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
		{
			unsigned int ret = effectList[i]->getResult();
			if (ret & erLifeExhaust)
			{
				deleteList.push_back(effectList[i]);
			}
			else
			{
				newList.push_back(effectList[i]);
			}
		}
	}
	for (size_t i = 0; i < deleteList.size(); i++)
	{
		delete deleteList[i];
	}
	effectList = newList;
}

void EffectManager::load()
{
	freeResource();
	std::string iniName = SAVE_CURRENT_FOLDER;
	iniName += EFFECT_INI;
	INIReader ini(iniName);
	std::string section = "Head";
	int count = ini.GetInteger(section, "Count", 0);
	if (count < 0)
	{
		count = 0;
	}
	for (int i = 0; i < count; i++)
	{
		section = convert::formatString("PRO%d", i + 1);
		auto effect = new Effect;
		addChild(effect);
		effect->initFromIni(&ini, section);
		effectList.push_back(effect);
	}
}

void EffectManager::save()
{
	INIReader ini;
	int count = effectList.size();
	std::string section = "Head";
	ini.SetInteger(section, "Count", count);
	for (size_t i = 0; i < effectList.size(); i++)
	{
		section = convert::formatString("PRO%d", i + 1);
		effectList[i]->saveToIni(&ini, section);
	}
	
	std::string iniName = SAVE_CURRENT_FOLDER;
	iniName += EFFECT_INI;
	ini.saveToFile(iniName);
}

void EffectManager::disableAllEffect()
{
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
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
		if (effectList[i] != nullptr && effectList[i]->position.x >= x && effectList[i]->position.y >= y && effectList[i]->position.x < x + w && effectList[i]->position.y < y + h)
		{
			map.tile[effectList[i]->position.y - y][effectList[i]->position.x - x].index.push_back(i);
		}
	}
	return map;
}

void EffectManager::freeResource()
{
	removeAllChild();
	auto deleteList = effectList;
	for (size_t i = 0; i < deleteList.size(); i++)
	{
		if (deleteList[i] != nullptr)
		{
			delete deleteList[i];
		}
	}
	effectList.resize(0);
}

void EffectManager::onUpdate()
{
	CollisionDetector::detectCollision();
	clearEffect();
}
