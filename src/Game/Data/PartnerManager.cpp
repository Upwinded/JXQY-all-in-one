﻿#include "PartnerManager.h"
#include "../GameManager/GameManager.h"


PartnerManager::PartnerManager()
{
}

PartnerManager::~PartnerManager()
{
	freeResource();
}

void PartnerManager::loadPartner()
{
	freeResource();
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i] != nullptr && gm->npcManager->npcList[i]->kind == nkPartner)
		{
			partnerList.push_back(gm->npcManager->npcList[i]);
		}
	}
	for (size_t i = 0; i < partnerList.size(); ++i) {
		gm->npcManager->removeNPCOnlyFromList(partnerList[i]);
	}
}

void PartnerManager::addPartner()
{
	for (size_t i = 0; i < partnerList.size(); i++)
	{
		gm->npcManager->addNPC(partnerList[i]);
	}
	partnerList.resize(0);
}

void PartnerManager::load(int index)
{
	freeResource();
    std::string fName = SAVE_CURRENT_FOLDER;
    fName += PARTNER_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += PARTNER_INI_EXT;
	INIReader ini(fName);
	std::string section = u8"Head";
	int count = ini.GetInteger(section, u8"Count", 0);
	if (count <= 0)
	{
		partnerList.resize(0);
	}
	else
	{
		partnerList.resize(count);
		for (size_t i = 0; i < partnerList.size(); i++)
		{
			partnerList[i] = std::make_shared<NPC>();
			section = convert::formatString("%d", i + 1);
			partnerList[i]->initFromIni(&ini, section);
			gm->npcManager->addNPC(partnerList[i]);
		}
	}
	partnerList.resize(0);
}

void PartnerManager::save(int index)
{
	freeResource();
	std::string fName = PARTNER_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += PARTNER_INI_EXT;
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i] != nullptr && gm->npcManager->npcList[i]->kind == nkPartner)
		{
			partnerList.push_back(gm->npcManager->npcList[i]);
		}
	}

	INIReader ini;
	std::string section = u8"Head";
	ini.SetInteger(section, u8"Count", partnerList.size());
	for (size_t i = 0; i < partnerList.size(); i++)
	{
		section = convert::formatString("%d", i + 1);
		partnerList[i]->saveToIni(&ini, section);
	}
	partnerList.resize(0);
	ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
}

void PartnerManager::freeResource()
{
	partnerList.resize(0);
}
