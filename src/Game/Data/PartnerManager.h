#pragma once
#include "NPC.h"
#include <vector>
#include "../GameTypes.h"

class PartnerManager
{
public:
	PartnerManager();
	virtual ~PartnerManager();

	//读取npc时临时保存所有partner
	virtual void loadPartner();
	virtual void addPartner();

	virtual void load();
	virtual void save();
	void freeResource();

private:
	std::vector<NPC *> partnerList;

};

