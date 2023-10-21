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

void MagicManager::load(int index)
{
	freeResource();
    std::string fName = SAVE_CURRENT_FOLDER;
    fName += MAGIC_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += MAGIC_INI_EXT;
	INIReader ini(fName);

	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		std::string section = convert::formatString("%d", i + 1);
		magicList[i].iniFile = ini.Get(section, "IniFile", "");
		magicList[i].level = ini.GetInteger(section, "Level", 0);
		magicList[i].exp = ini.GetInteger(section, "Exp", 0);
		if (magicList[i].iniFile != "")
		{
			magicList[i].magic = std::make_shared<Magic>();
			magicList[i].magic->initFromIni(magicList[i].iniFile);
		}	
	}
}

void MagicManager::save(int index)
{
	INIReader ini;
	std::string section = "Head";
	ini.SetInteger(section, "Count", 0);
	int count = 0;
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		if (magicList[i].iniFile != "")
		{
			count++;
			section = convert::formatString("%d", i + 1);
			ini.Set(section, "IniFile", magicList[i].iniFile);
			ini.SetInteger(section, "Level", magicList[i].level);
			ini.SetInteger(section, "Exp", magicList[i].exp);
		}
	}
	section = "Head";
	ini.SetInteger(section, "Count", count);
    std::string fName = MAGIC_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += MAGIC_INI_EXT;
	ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
}

void MagicManager::freeResource()
{
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		magicList[i].iniFile = "";
		magicList[i].level = 0;
		magicList[i].exp = 0;
		if (magicList[i].magic != nullptr)
		{
			magicList[i].magic = nullptr;
		}		
	}
	attackMagicList.clear();
}

void MagicManager::addPracticeExp(int addexp)
{
	if (magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr)
	{
		magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp += addexp;
		gm->menu->practiceMenu->updateExp();
		bool lup = false;
		while (magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level < MAGIC_MAX_LEVEL && magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].exp >= magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->level[magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level].levelupExp)
		{
			magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level++;
			lup = true;
		}
		if (lup)
		{
			gm->menu->practiceMenu->updateExp();
			gm->menu->practiceMenu->updateLevel();
			gm->showMessage(convert::formatString(u8"%s的等级提升了！", magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->name.c_str(), magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level));
		}
	}
}

void MagicManager::addUseExp(std::shared_ptr<Effect> e, int addexp)
{
	for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
	{
		if (magicList[i].iniFile != "" && magicList[i].magic != nullptr && magicList[i].iniFile == e->magic.iniName)
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
				gm->menu->practiceMenu->updateExp();
				gm->menu->practiceMenu->updateLevel();
				gm->showMessage(convert::formatString(u8"%s的等级提升了！", magicList[i].magic->name.c_str(), magicList[i].level));
			}
			break;
		}
	}
}

void MagicManager::addMagicExp(const std::string & magicName, int addexp)
{
	MagicInfo * m = findMagic(magicName);
	if (m != nullptr)
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
	if (m == nullptr)
	{
		for (size_t i = 0; i < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT + MAGIC_PRACTISE_COUNT; i++)
		{
			if (magicList[i].iniFile == "")
			{
				magicList[i].iniFile = magicName;
				magicList[i].level = 1;
				magicList[i].exp = 0;
				magicList[i].magic = std::make_shared<Magic>();
				magicList[i].magic->initFromIni(magicName);
				updateMenu(i);
				gm->showMessage(convert::formatString(u8"学会了%s！", magicList[i].magic->name.c_str()));
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
	if (m != nullptr)
	{
		m->iniFile = "";
		m->level = 0;
		m->exp = 0;
		if (m->magic != nullptr)
		{
			m->magic = nullptr;
		}
		updateMenu();
	}
}

void MagicManager::updateMenu(int idx)
{
	if (idx < MAGIC_COUNT)
	{
		gm->menu->magicMenu->updateMagic();
	}
	else if (idx < MAGIC_COUNT + MAGIC_TOOLBAR_COUNT)
	{
		gm->menu->bottomMenu->updateMagicItem();
	}
	else
	{
		gm->menu->practiceMenu->updateMagic();
	}
}

void MagicManager::updateMenu()
{
	gm->menu->magicMenu->updateMagic();
	gm->menu->bottomMenu->updateMagicItem();
	gm->menu->practiceMenu->updateMagic();
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

bool MagicManager::magicListExists(int index)
{
	if (index >= 0 && index < magicListLength)
	{
		return magicList[index].magic != nullptr && !magicList[index].iniFile.empty();
	}
	return false;
}

std::shared_ptr<Magic> MagicManager::loadAttackMagic(const std::string & name)
{	
	if (name == "")
	{
		return std::shared_ptr<Magic>(nullptr);
	}

	auto m = attackMagicList.find(name);
	if (m != attackMagicList.end())
	{
		return m->second;
	}

	std::shared_ptr<Magic> am = std::make_shared<Magic>();
	am->initFromIni(name);
	attackMagicList[name] = am;
	return am;
}

void MagicManager::tryCleanAttackMagic()
{
	auto iter = attackMagicList.begin();
	while (iter != attackMagicList.end())
	{
		if (iter->second.use_count() <= 1)
		{
			iter->second->freeResource();
			iter->second = nullptr;
			iter = attackMagicList.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}
