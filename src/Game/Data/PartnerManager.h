#pragma once
#include "NPC.h"
#include <vector>
#include "../GameTypes.h"

class PartnerManager
{
public:
	PartnerManager();
	virtual ~PartnerManager();

	// 从npc列表提取所有partner
    // 读取npc时临时保存所有partner
	void extractPartnerListFromNPCManager();
	void transferPartnerListToNPCManager();

	std::vector<std::shared_ptr<NPC>> findPartnersFromNPCManager();
	void setPartnersIsBlockingPlayer(bool value);

	virtual void load(int index);
	virtual void save(int index);
	void freeResource();

private:
	// 临时的Partner列表，一般只在读取、保存时使用
	std::vector<std::shared_ptr<NPC>> tempPartnerList;

};

