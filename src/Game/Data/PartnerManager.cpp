#include "PartnerManager.h"
#include "NPCPersistence.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"
#include "../../File/File.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <utility>

namespace
{
void setTransferMessage(std::string* message, const std::string& value)
{
	if (message != nullptr)
	{
		*message = value;
	}
}

GoodsInfo makeSingleGoodsInfo(const std::string& fileName)
{
	GoodsInfo goodsInfo;
	if (fileName.empty())
	{
		return goodsInfo;
	}
	goodsInfo.iniFile = fileName;
	goodsInfo.number = 1;
	goodsInfo.goods = std::make_shared<Goods>();
	goodsInfo.goods->initFromIni(fileName);
	goodsInfo.remainColdMilliseconds = 0;
	return goodsInfo;
}

bool equalsGoodsFileName(
	const std::string& first,
	const std::string& second)
{
	return !first.empty() && first.size() == second.size()
		&& std::equal(
			first.begin(),
			first.end(),
			second.begin(),
			[](unsigned char firstCharacter, unsigned char secondCharacter)
			{
				return std::tolower(firstCharacter)
					== std::tolower(secondCharacter);
			});
}

void removeCurrentPartners()
{
	std::vector<int> partnerIndices;
	for (size_t i = 0; i < gm->npcManager->npcList.size(); ++i)
	{
		const auto& npc = gm->npcManager->npcList[i];
		if (npc != nullptr && npc->kind == nkPartner)
		{
			partnerIndices.push_back(static_cast<int>(i));
		}
	}
	gm->npcManager->deleteNPC(partnerIndices);
}
}

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
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i] != nullptr && gm->npcManager->npcList[i]->kind == nkPartner)
		{
			tempList.push_back(gm->npcManager->npcList[i]);
		}
	}
	return tempList;
}

bool PartnerManager::isActivePartner(
	const std::shared_ptr<NPC>& partner) const
{
	if (partner == nullptr || gm == nullptr || gm->npcManager == nullptr)
	{
		return false;
	}
	return std::find(
		gm->npcManager->npcList.begin(),
		gm->npcManager->npcList.end(),
		partner) != gm->npcManager->npcList.end()
		&& partner->kind == nkPartner;
}

void PartnerManager::setPartnersIsBlockingPlayer(bool value)
{
	auto tempList = findPartnersFromNPCManager();
	for (auto& partner: tempList)
	{
		partner->isPartnerBlockingPlayer = value;
	}
}

bool PartnerManager::equipOnePlayerGoodsOnPartner(
	const std::shared_ptr<NPC>& partner,
	int playerBagIndex,
	int equipmentSlotIndex,
	std::string* message)
{
	if (!isActivePartner(partner) || partner->canEquip <= 0)
	{
		setTransferMessage(message, "同伴当前不能更换装备");
		return false;
	}
	if ((!gm->goodsManager.isStoreIndex(playerBagIndex)
			&& !gm->goodsManager.isBottomIndex(playerBagIndex))
		|| !gm->goodsManager.goodsListExists(playerBagIndex))
	{
		setTransferMessage(message, "包裹位置无效");
		return false;
	}
	if (equipmentSlotIndex < 0 || equipmentSlotIndex >= GOODS_BODY_COUNT)
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}

	GoodsInfo& playerGoods = gm->goodsManager.goodsList[playerBagIndex];
	if (playerGoods.goods == nullptr || playerGoods.goods->kind != gkEquipment)
	{
		setTransferMessage(message, "只能装备武器防具！");
		return false;
	}
	if (!gm->goodsManager.canEquipGoodsAt(
		playerGoods.goods, equipmentSlotIndex, partner, message))
	{
		return false;
	}

	const std::string newItemFile = playerGoods.iniFile;
	const std::string oldItemFile =
		partner->getEquipmentFileByPartIndex(equipmentSlotIndex);
	if (newItemFile == oldItemFile)
	{
		setTransferMessage(message, "");
		return true;
	}

	int oldItemDestination = -1;
	if (!oldItemFile.empty())
	{
		for (int index = 0; index < gm->goodsManager.listLength(); index++)
		{
			if ((gm->goodsManager.isStoreIndex(index)
					|| gm->goodsManager.isBottomIndex(index))
				&& gm->goodsManager.goodsList[index].iniFile == oldItemFile)
			{
				oldItemDestination = index;
				break;
			}
		}
		if (oldItemDestination < 0)
		{
			for (int index = 0; index < gm->goodsManager.listLength(); index++)
			{
				if ((gm->goodsManager.isStoreIndex(index)
						|| gm->goodsManager.isBottomIndex(index))
					&& gm->goodsManager.goodsList[index].iniFile.empty())
				{
					oldItemDestination = index;
					break;
				}
			}
		}
		if (oldItemDestination < 0 && playerGoods.number == 1)
		{
			// Consuming the singleton source frees this exact slot, so a full
			// bag can still complete the replacement atomically in place.
			oldItemDestination = playerBagIndex;
		}
		if (oldItemDestination < 0)
		{
			setTransferMessage(message, "物品栏位置已满！");
			return false;
		}
	}

	if (!partner->setEquipmentFileByPartIndex(
		equipmentSlotIndex, newItemFile))
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}
	playerGoods.number--;
	if (playerGoods.number <= 0)
	{
		playerGoods.clear();
	}
	if (oldItemDestination >= 0)
	{
		GoodsInfo& destination =
			gm->goodsManager.goodsList[oldItemDestination];
		if (destination.iniFile == oldItemFile)
		{
			destination.number++;
		}
		else
		{
			destination = makeSingleGoodsInfo(oldItemFile);
		}
	}

	gm->goodsManager.refreshEquipmentEffects();
	if (gm->player != nullptr)
	{
		gm->player->limitAttribute();
	}
	setTransferMessage(message, "");
	return true;
}

bool PartnerManager::unequipPartnerGoodsToPlayerBag(
	const std::shared_ptr<NPC>& partner,
	int equipmentSlotIndex,
	std::string* message)
{
	if (!isActivePartner(partner) || partner->canEquip <= 0)
	{
		setTransferMessage(message, "同伴当前不能更换装备");
		return false;
	}
	if (equipmentSlotIndex < 0 || equipmentSlotIndex >= GOODS_BODY_COUNT)
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}

	const std::string itemFile =
		partner->getEquipmentFileByPartIndex(equipmentSlotIndex);
	if (itemFile.empty())
	{
		setTransferMessage(message, "同伴装备已不存在");
		return false;
	}

	int destinationIndex = -1;
	const bool canStackReturnedGoods =
		gm->global.goodsLayout.listType != 1;
	for (int index = 0;
		canStackReturnedGoods && index < gm->goodsManager.listLength();
		++index)
	{
		if ((gm->goodsManager.isStoreIndex(index)
				|| gm->goodsManager.isBottomIndex(index))
			&& gm->goodsManager.goodsListExists(index)
			&& equalsGoodsFileName(
				gm->goodsManager.goodsList[index].iniFile, itemFile)
			&& gm->goodsManager.goodsList[index].number < INT_MAX)
		{
			destinationIndex = index;
			break;
		}
	}
	if (destinationIndex < 0)
	{
		for (int index = 0; index < gm->goodsManager.listLength(); ++index)
		{
			if ((gm->goodsManager.isStoreIndex(index)
					|| gm->goodsManager.isBottomIndex(index))
				&& gm->goodsManager.goodsList[index].iniFile.empty())
			{
				destinationIndex = index;
				break;
			}
		}
	}
	if (destinationIndex < 0)
	{
		setTransferMessage(message, "物品栏位置已满！");
		return false;
	}

	GoodsInfo& destination = gm->goodsManager.goodsList[destinationIndex];
	GoodsInfo returnedGoods;
	if (destination.iniFile.empty())
	{
		returnedGoods = makeSingleGoodsInfo(itemFile);
		if (returnedGoods.goods == nullptr
			|| !returnedGoods.goods->loadSucceeded)
		{
			setTransferMessage(message, "同伴装备资源无效");
			return false;
		}
	}
	if (!partner->setEquipmentFileByPartIndex(equipmentSlotIndex, ""))
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}
	if (equalsGoodsFileName(destination.iniFile, itemFile))
	{
		destination.number++;
	}
	else
	{
		destination = std::move(returnedGoods);
	}

	gm->goodsManager.refreshEquipmentEffects();
	if (gm->player != nullptr)
	{
		gm->player->limitAttribute();
	}
	setTransferMessage(message, "");
	return true;
}

bool PartnerManager::exchangePlayerBagWithPartnerEquipment(
	const std::shared_ptr<NPC>& partner,
	int playerBagIndex,
	int equipmentSlotIndex,
	bool sourceIsPlayerBag,
	std::string* message)
{
	if (!isActivePartner(partner) || partner->canEquip <= 0)
	{
		setTransferMessage(message, "同伴当前不能更换装备");
		return false;
	}
	if (!gm->goodsManager.isStoreIndex(playerBagIndex)
		|| playerBagIndex < 0
		|| playerBagIndex >= gm->goodsManager.listLength())
	{
		setTransferMessage(message, "包裹位置无效");
		return false;
	}
	if (equipmentSlotIndex < 0 || equipmentSlotIndex >= GOODS_BODY_COUNT)
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}

	GoodsInfo& playerGoods = gm->goodsManager.goodsList[playerBagIndex];
	const bool playerGoodsExists =
		gm->goodsManager.goodsListExists(playerBagIndex);
	const std::string partnerGoodsFile =
		partner->getEquipmentFileByPartIndex(equipmentSlotIndex);
	if (sourceIsPlayerBag && !playerGoodsExists)
	{
		setTransferMessage(message, "拿起的物品已不存在");
		return false;
	}
	if (!sourceIsPlayerBag && partnerGoodsFile.empty())
	{
		setTransferMessage(message, "拿起的同伴装备已不存在");
		return false;
	}

	const bool playerGoodsWouldEnterPartner = playerGoodsExists
		&& (sourceIsPlayerBag || playerGoods.iniFile != partnerGoodsFile);
	if (playerGoodsWouldEnterPartner
		&& !gm->goodsManager.canEquipGoodsAt(
			playerGoods.goods,
			equipmentSlotIndex,
			partner,
			message))
	{
		return false;
	}

	if (sourceIsPlayerBag)
	{
		if (playerGoods.iniFile == partnerGoodsFile)
		{
			setTransferMessage(message, "");
			return true;
		}
		if (!partnerGoodsFile.empty() && playerGoods.number > 1)
		{
			setTransferMessage(
				message,
				"堆叠物品不能与已装备物品精确交换，请使用 A 装备");
			return false;
		}

		const std::string newPartnerGoodsFile = playerGoods.iniFile;
		if (!partner->setEquipmentFileByPartIndex(
			equipmentSlotIndex, newPartnerGoodsFile))
		{
			setTransferMessage(message, "同伴装备位置无效");
			return false;
		}
		if (partnerGoodsFile.empty())
		{
			playerGoods.number--;
			if (playerGoods.number <= 0)
			{
				playerGoods.clear();
			}
		}
		else
		{
			playerGoods = makeSingleGoodsInfo(partnerGoodsFile);
		}
		setTransferMessage(message, "");
		return true;
	}

	if (!playerGoodsExists)
	{
		if (!partner->setEquipmentFileByPartIndex(equipmentSlotIndex, ""))
		{
			setTransferMessage(message, "同伴装备位置无效");
			return false;
		}
		playerGoods = makeSingleGoodsInfo(partnerGoodsFile);
		setTransferMessage(message, "");
		return true;
	}
	if (playerGoods.iniFile == partnerGoodsFile)
	{
		if (!partner->setEquipmentFileByPartIndex(equipmentSlotIndex, ""))
		{
			setTransferMessage(message, "同伴装备位置无效");
			return false;
		}
		playerGoods.number++;
		setTransferMessage(message, "");
		return true;
	}
	if (playerGoods.number != 1)
	{
		setTransferMessage(
			message,
			"堆叠物品不能与同伴装备精确交换，请选择空格");
		return false;
	}

	const std::string newPartnerGoodsFile = playerGoods.iniFile;
	if (!partner->setEquipmentFileByPartIndex(
		equipmentSlotIndex, newPartnerGoodsFile))
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}
	playerGoods = makeSingleGoodsInfo(partnerGoodsFile);
	setTransferMessage(message, "");
	return true;
}

bool PartnerManager::exchangePartnerEquipmentSlots(
	const std::shared_ptr<NPC>& partner,
	int sourceSlotIndex,
	int targetSlotIndex,
	std::string* message)
{
	if (!isActivePartner(partner) || partner->canEquip <= 0)
	{
		setTransferMessage(message, "同伴当前不能更换装备");
		return false;
	}
	if (sourceSlotIndex < 0 || sourceSlotIndex >= GOODS_BODY_COUNT
		|| targetSlotIndex < 0 || targetSlotIndex >= GOODS_BODY_COUNT)
	{
		setTransferMessage(message, "同伴装备位置无效");
		return false;
	}
	const std::string sourceFile =
		partner->getEquipmentFileByPartIndex(sourceSlotIndex);
	const std::string targetFile =
		partner->getEquipmentFileByPartIndex(targetSlotIndex);
	if (sourceFile.empty())
	{
		setTransferMessage(message, "拿起的同伴装备已不存在");
		return false;
	}
	if (sourceSlotIndex == targetSlotIndex)
	{
		setTransferMessage(message, "");
		return true;
	}

	auto sourceGoods = std::make_shared<Goods>();
	sourceGoods->initFromIni(sourceFile);
	if (!gm->goodsManager.canEquipGoodsAt(
		sourceGoods, targetSlotIndex, partner, message))
	{
		return false;
	}
	if (!targetFile.empty())
	{
		auto targetGoods = std::make_shared<Goods>();
		targetGoods->initFromIni(targetFile);
		if (!gm->goodsManager.canEquipGoodsAt(
			targetGoods, sourceSlotIndex, partner, message))
		{
			return false;
		}
	}

	partner->setEquipmentFileByPartIndex(sourceSlotIndex, targetFile);
	partner->setEquipmentFileByPartIndex(targetSlotIndex, sourceFile);
	setTransferMessage(message, "");
	return true;
}

void PartnerManager::load(int index)
{
	freeResource();
	std::string fName =
		SaveFileManager::CurrentPath() + PARTNER_INI_NAME;
	if (index >= 0)
	{
		fName += convert::formatString("%d", index);
	}
	fName += PARTNER_INI_EXT;
	if (!File::fileExist(fName))
	{
		removeCurrentPartners();
		return;
	}

	INIReader ini(fName);
	if (ini.ParseError() != 0)
	{
		GameLog::write("PartnerManager: invalid partner file %s\n", fName.c_str());
		removeCurrentPartners();
		return;
	}

	int count = 0;
	if (!NPCPersistence::readCount(ini, NPCPersistence::MaximumPartnerCount, count))
	{
		GameLog::write("PartnerManager: invalid partner count in %s\n", fName.c_str());
		removeCurrentPartners();
		return;
	}
	int loadCount = 0;
	for (int index = 0; index < count; ++index)
	{
		const std::string section =
			convert::formatString("Partner%03d", index);
		if (!ini.HasSection(section))
		{
			GameLog::write(
				"PartnerManager: compatible partner list %s stops before missing section %s\n",
				fName.c_str(),
				section.c_str());
			break;
		}
		loadCount = index + 1;
	}

	const size_t normalNpcCount = static_cast<size_t>(std::count_if(
		gm->npcManager->npcList.begin(),
		gm->npcManager->npcList.end(),
		[](const std::shared_ptr<NPC>& npc)
		{
			return npc != nullptr && npc->kind != nkPartner;
		}));
	if (normalNpcCount + static_cast<size_t>(loadCount)
		> static_cast<size_t>(NPCPersistence::MaximumRuntimeNpcCount))
	{
		GameLog::write("PartnerManager: partner list exceeds runtime capacity in %s\n", fName.c_str());
		removeCurrentPartners();
		return;
	}

	std::vector<std::shared_ptr<NPC>> loadedPartnerList;
	loadedPartnerList.reserve(static_cast<size_t>(loadCount));
	for (int i = 0; i < loadCount; ++i)
	{
		std::string section = convert::formatString("Partner%03d", i);
		auto partner = std::make_shared<NPC>();
		partner->initFromIni(&ini, section);
		loadedPartnerList.push_back(partner);
	}

	removeCurrentPartners();
	for (const auto& partner : loadedPartnerList)
	{
		gm->npcManager->addNPC(partner);
	}
}

bool PartnerManager::save(int index)
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
		if (NPCManager::shouldPersistNPC(gm->npcManager->npcList[i])
			&& gm->npcManager->npcList[i]->kind == nkPartner)
		{
			tempPartnerList.push_back(gm->npcManager->npcList[i]);
		}
	}

	INIReader ini;
	std::string section = "Head";
	ini.SetInteger(section, "Count", tempPartnerList.size());
	for (size_t i = 0; i < tempPartnerList.size(); i++)
	{
		section = convert::formatString("Partner%03d", i);
		tempPartnerList[i]->saveToIni(&ini, section);
	}
	tempPartnerList.resize(0);
	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
	return saved;
}

void PartnerManager::freeResource()
{
	tempPartnerList.resize(0);
}
