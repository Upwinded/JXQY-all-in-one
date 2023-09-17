#pragma once
#include "NPC.h"
#include <vector>
#include "../GameTypes.h"

class PartnerManager
{
public:
	PartnerManager();
	virtual ~PartnerManager();

	// 从npc列表读取所有partner
    // 读取npc时临时保存所有partner
	virtual void loadPartner();
	virtual void addPartner();

	virtual void load(int index);
	virtual void save(int index);
	void freeResource();

private:
	std::vector<NPC *> partnerList;

};

