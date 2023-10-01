#include "EffectManager.h"
#include "../GameManager/GameManager.h"


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

void EffectManager::addEffect(std::shared_ptr<Effect> effect)
{
	if (effect.get() == nullptr)
	{
		return;
	}
	effectList.push_back(effect);
	addChild(effect);
	effect->initTime();
}

void EffectManager::deleteEffect(std::shared_ptr<Effect> effect)
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
	std::vector<std::shared_ptr<Effect>> newList;
	newList.resize(0);
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
		{
			unsigned int ret = effectList[i]->getResult();
			if (ret & erLifeExhaust)
			{
				removeChild(effectList[i]);
				effectList[i] = nullptr;
			}
			else
			{
				newList.push_back(effectList[i]);
			}
		}
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
		auto effect = std::make_shared<Effect>();
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
	
	std::string iniName = EFFECT_INI;
	ini.saveToFile(SaveFileManager::CurrentPath() + iniName);
    
    SaveFileManager::AppendFile(iniName);
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
	for (size_t i = 0; i < effectList.size(); i++)
	{
		if (effectList[i] != nullptr)
		{
			removeChild(effectList[i]);
			effectList[i] = nullptr;
		}
	}
	effectList.resize(0);
}

void EffectManager::onUpdate()
{
	CollisionDetector::detectCollision();
	clearEffect();
}
