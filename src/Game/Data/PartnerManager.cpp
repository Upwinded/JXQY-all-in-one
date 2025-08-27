#include "PartnerManager.h"
#include "../GameManager/GameManager.h"


PartnerManager::PartnerManager()
{
}

PartnerManager::~PartnerManager()
{
	freeResource();
}

void PartnerManager::extractPartnerListFromNPCManager()
{
	freeResource();
	tempPartnerList = findPartnersFromNPCManager();
	for (size_t i = 0; i < tempPartnerList.size(); ++i) {
		gm->npcManager->removeNPCOnlyFromList(tempPartnerList[i]);
	}
}

void PartnerManager::transferPartnerListToNPCManager()
{
	for (size_t i = 0; i < tempPartnerList.size(); i++)
	{
		gm->npcManager->addNPC(tempPartnerList[i]);
	}
	tempPartnerList.clear();
}


std::vector<std::shared_ptr<NPC>> PartnerManager::findPartnersFromNPCManager()
{
	std::vector<std::shared_ptr<NPC>> tempList;
	auto npcList = gm->npcManager->npcList;
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i] != nullptr && gm->npcManager->npcList[i]->kind == nkPartner)
		{
			tempList.push_back(gm->npcManager->npcList[i]);
		}
	}
	return tempList;
}

void PartnerManager::setPartnersIsBlockingPlayer(bool value)
{
	auto tempList = findPartnersFromNPCManager();
	for (auto& partner: tempList)
	{
		if (partner->isStanding() || value)
		{
			partner->isPartnerBlockingPlayer = value;
		}
	}
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
		tempPartnerList.clear();
	}
	else
	{
		tempPartnerList.resize(count);
		for (size_t i = 0; i < tempPartnerList.size(); i++)
		{
			tempPartnerList[i] = std::make_shared<NPC>();
			section = convert::formatString("Partner%03d", i);
			tempPartnerList[i]->initFromIni(&ini, section);
			gm->npcManager->addNPC(tempPartnerList[i]);
		}
	}
	tempPartnerList.clear();
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
			tempPartnerList.push_back(gm->npcManager->npcList[i]);
		}
	}

	INIReader ini;
	std::string section = u8"Head";
	ini.SetInteger(section, u8"Count", tempPartnerList.size());
	for (size_t i = 0; i < tempPartnerList.size(); i++)
	{
		section = convert::formatString("Partner%03d", i);
		tempPartnerList[i]->saveToIni(&ini, section);
	}
	tempPartnerList.resize(0);
	ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
}

void PartnerManager::freeResource()
{
	tempPartnerList.resize(0);
}
