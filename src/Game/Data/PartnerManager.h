#pragma once
#include "NPC.h"
#include <string>
#include <vector>
#include "../GameTypes.h"

class PartnerManager
{
public:
	PartnerManager();
	virtual ~PartnerManager();

	// 浠巒pc鍒楄〃鎻愬彇鎵€鏈塸artner
    // 璇诲彇npc鏃朵复鏃朵繚瀛樻墍鏈塸artner
	void extractPartnerListFromNPCManager();
	void transferPartnerListToNPCManager();

	std::vector<std::shared_ptr<NPC>> findPartnersFromNPCManager();
	bool isActivePartner(const std::shared_ptr<NPC>& partner) const;
	void setPartnersIsBlockingPlayer(bool value);
	bool equipOnePlayerGoodsOnPartner(
		const std::shared_ptr<NPC>& partner,
		int playerBagIndex,
		int equipmentSlotIndex,
		std::string* message = nullptr);
	bool unequipPartnerGoodsToPlayerBag(
		const std::shared_ptr<NPC>& partner,
		int equipmentSlotIndex,
		std::string* message = nullptr);
	bool exchangePlayerBagWithPartnerEquipment(
		const std::shared_ptr<NPC>& partner,
		int playerBagIndex,
		int equipmentSlotIndex,
		bool sourceIsPlayerBag,
		std::string* message = nullptr);
	bool exchangePartnerEquipmentSlots(
		const std::shared_ptr<NPC>& partner,
		int sourceSlotIndex,
		int targetSlotIndex,
		std::string* message = nullptr);

	virtual void load(int index);
	virtual bool save(int index);
	void freeResource();

private:
	// 涓存椂鐨凱artner鍒楄〃锛屼竴鑸彧鍦ㄨ鍙栥€佷繚瀛樻椂浣跨敤
	std::vector<std::shared_ptr<NPC>> tempPartnerList;

};
