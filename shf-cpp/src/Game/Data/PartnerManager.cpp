#include "PartnerManager.h"
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
	for (size_t i = 0; i < gm->npcManager.npcList.size(); i++)
	{
		if (gm->npcManager.npcList[i] != NULL && gm->npcManager.npcList[i]->kind == nkPartner)
		{
			partnerList.push_back(gm->npcManager.npcList[i]);
			gm->npcManager.npcList[i] = NULL;
		}
	}
}

void PartnerManager::addPartner()
{
	for (size_t i = 0; i < partnerList.size(); i++)
	{
		gm->npcManager.addNPC(partnerList[i]);
	}
	
	partnerList.resize(0);
}

void PartnerManager::load()
{
	freeResource();
	std::string fileName = DEFAULT_FOLDER;
	fileName += PARTNER_INI;
	INIReader * ini = new INIReader(fileName);
	std::string section = "Head";
	int count = ini->GetInteger(section, "Count", 0);
	if (count <= 0)
	{
		partnerList.resize(0);
	}
	else
	{
		partnerList.resize(count);
		for (size_t i = 0; i < partnerList.size(); i++)
		{
			partnerList[i] = new NPC;
			section = convert::formatString("%d", i + 1);
			partnerList[i]->initFromIni(ini, section);
			gm->npcManager.addNPC(partnerList[i]);
		}
	}
	partnerList.resize(0);
	delete ini;
}

void PartnerManager::save()
{
	freeResource();
	std::string fileName = DEFAULT_FOLDER;
	fileName += PARTNER_INI;
	for (size_t i = 0; i < gm->npcManager.npcList.size(); i++)
	{
		if (gm->npcManager.npcList[i] != NULL && gm->npcManager.npcList[i]->kind == nkPartner)
		{
			partnerList.push_back(gm->npcManager.npcList[i]);
		}
	}

	INIReader * ini = new INIReader;
	std::string section = "Head";
	ini->SetInteger(section, "Count", partnerList.size());
	for (size_t i = 0; i < partnerList.size(); i++)
	{
		section = convert::formatString("%d", i + 1);
		partnerList[i]->saveToIni(ini, section);
	}
	partnerList.resize(0);
	ini->saveToFile(fileName);
	delete ini;
}

void PartnerManager::freeResource()
{
	partnerList.resize(0);
}
