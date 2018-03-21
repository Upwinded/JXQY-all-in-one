#include "MagicManager.h"
#include "../../File/INIReader.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"

MagicManager::MagicManager()
{
}


MagicManager::~MagicManager()
{
	freeResource();
}

MagicInfo * MagicManager::findMagic(const std::string & iniName)
{
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		if (magicList[i].iniFile == iniName)
		{
			return &magicList[i];
		}
	}
	return nullptr;
}

void MagicManager::load()
{
	freeResource();
	std::string iniName = DEFAULT_FOLDER;
	iniName += MAGIC_INI;
	INIReader * ini = new INIReader(iniName);

	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		std::string section = convert::formatString("%d", i + 1);
		magicList[i].iniFile = ini->Get(section, "IniFile", "");
		magicList[i].level = ini->GetInteger(section, "Level", 0);
		magicList[i].exp = ini->GetInteger(section, "Exp", 0);
		if (magicList[i].iniFile != "")
		{
			magicList[i].magic = new Magic;
			magicList[i].magic->initFromIni(magicList[i].iniFile);
		}	
	}

	delete ini;
	
}

void MagicManager::save()
{
	INIReader * ini = new INIReader;
	std::string section = "Head";
	ini->SetInteger(section, "Count", 0);
	int count = 0;
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		if (magicList[i].iniFile != "")
		{
			count++;
			section = convert::formatString("%d", i + 1);
			ini->Set(section, "IniFile", magicList[i].iniFile);
			ini->SetInteger(section, "Level", magicList[i].level);
			ini->SetInteger(section, "Exp", magicList[i].exp);
		}
	}
	section = "Head";
	ini->SetInteger(section, "Count", count);
	std::string iniName = DEFAULT_FOLDER;
	iniName += MAGIC_INI;
	ini->saveToFile(iniName);
	delete ini;
}

void MagicManager::freeResource()
{
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		magicList[i].iniFile = "";
		magicList[i].level = 0;
		magicList[i].exp = 0;
		if (magicList[i].magic != NULL)
		{
			delete magicList[i].magic;
			magicList[i].magic = NULL;
		}		
	}
	for (size_t i = 0; i < attackMagicList.size(); i++)
	{
		if (attackMagicList[i].magic != NULL)
		{
			delete attackMagicList[i].magic;
			attackMagicList[i].magic = NULL;
		}
	}
	attackMagicList.resize(0);
}

void MagicManager::addPracticeExp(int addexp)
{
	if (magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != NULL)
	{
		magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp += addexp;
		gm->practiceMenu->updateExp();
		bool lup = false;
		while (magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level < MAGIC_MAX_LEVEL && magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp >= magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->level[magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level].levelupExp)
		{
			magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level++;
			lup = true;
		}
		if (lup)
		{
			gm->practiceMenu->updateExp();
			gm->practiceMenu->updateLevel();
			gm->showMessage(convert::formatString("%s的等级提升了！", magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->name.c_str(), magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level));
		}
	}
}

void MagicManager::addUseExp(Effect * e, int addexp)
{
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		if (magicList[i].iniFile != "" && magicList[i].magic != NULL && magicList[i].iniFile == e->magic.iniName)
		{
			magicList[i].exp += addexp;
			bool lup = false;
			while (magicList[i].level < MAGIC_MAX_LEVEL && magicList[i].exp >= magicList[i].magic->level[magicList[i].level].levelupExp)
			{
				magicList[i].level++;
				lup = true;
			}
			if (lup)
			{
				gm->practiceMenu->updateExp();
				gm->practiceMenu->updateLevel();
				gm->showMessage(convert::formatString("%s的等级提升了！", magicList[i].magic->name.c_str(), magicList[i].level));
			}
			break;
		}
	}
}

void MagicManager::addMagicExp(const std::string & magicName, int addexp)
{
	MagicInfo * m = findMagic(magicName);
	if (m != NULL)
	{
		m->exp += addexp;
		while (m->level < MAGIC_MAX_LEVEL && m->exp >= m->magic->level[m->level].levelupExp)
		{
			m->level++;
		}
	}
}

void MagicManager::addMagic(const std::string & magicName)
{
	if (magicName == "")
	{
		return;
	}
	MagicInfo * m = findMagic(magicName);
	if (m == NULL)
	{
		for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
		{
			if (magicList[i].iniFile == "")
			{
				magicList[i].iniFile = magicName;
				magicList[i].level = 1;
				magicList[i].exp = 0;
				magicList[i].magic = new Magic;
				magicList[i].magic->initFromIni(magicName);
				updateMenu(i);
				gm->showMessage(convert::formatString("学会了%s!", magicList[i].magic->name.c_str()));
				return;
			}
		}
	}
}

void MagicManager::deleteMagic(const std::string & magicName)
{
	if (magicName == "")
	{
		return;
	}
	MagicInfo * m = findMagic(magicName);
	if (m != NULL)
	{
		m->iniFile = "";
		m->level = 0;
		m->exp = 0;
		if (m->magic != NULL)
		{
			delete m->magic;
			m->magic = NULL;
		}
		updateMenu();
	}
}

void MagicManager::updateMenu(int idx)
{
	if (idx < MAGIC_COUNT)
	{
		gm->magicMenu->updateMagic();
	}
	else if (idx < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
	{
		gm->bottomMenu->updateMagicItem();
	}
	else
	{
		gm->practiceMenu->updateMagic();
	}
}

void MagicManager::updateMenu()
{
	gm->magicMenu->updateMagic();
	gm->bottomMenu->updateMagicItem();
	gm->practiceMenu->updateMagic();
}

void MagicManager::exchange(int index1, int index2)
{
	if (index1 >= 0 && index2 >= 0 && index1 < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT && index2 < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT)
	{
		MagicInfo tempInfo = magicList[index1];
		magicList[index1] = magicList[index2];
		magicList[index2] = tempInfo;
	}
}

Magic * MagicManager::loadAttackMagic(const std::string & name)
{	
	if (name == "")
	{
		return nullptr;
	}
	int found = -1;
	for (size_t i = 0; i < attackMagicList.size(); i++)
	{
		if (attackMagicList[i].name == name)
		{
			found = (int)i;
		}
	}
	if (found >= 0)
	{
		return attackMagicList[found].magic;
	}
	else
	{
		AttackMagic am;
		am.magic = new Magic;
		am.name = name;
		am.magic->initFromIni(name);
		attackMagicList.push_back(am);
		return am.magic;
	}
}
