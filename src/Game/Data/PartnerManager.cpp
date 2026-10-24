#include "PartnerManager.h"
#include "NPCPersistence.h"
#include "SaveIniPersistence.h"
#include "../../Resource/SemanticVersion.h"
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

SaveIniPersistence::ReadStatus readCharacterRecord(const std::string& fileName, std::shared_ptr<INIReader>& ini)
{
	auto status = SaveIniPersistence::read(SaveFileManager::CurrentPath() + fileName, ini);
	if (status == SaveIniPersistence::ReadStatus::Missing)
	{
		status = SaveIniPersistence::read(std::string(INI_SAVE_FOLDER) + fileName, ini);
	}
	return status;
}

bool canReplaceRecordedEquipment(const std::shared_ptr<NPC>& partner, int slot,
	const std::string& replacement, std::string* message)
{
	const int index = partner->getPartnerCharacterIndex();
	if (index < 0) return true;
	std::shared_ptr<INIReader> ini;
	const auto status = readCharacterRecord(std::string(GOODS_INI_NAME)
		+ std::to_string(index) + GOODS_INI_EXT, ini);
	if (status == SaveIniPersistence::ReadStatus::Missing) return true;
	if (status != SaveIniPersistence::ReadStatus::Loaded || !ini->HasSection("Head")
		|| ini->GetInteger("Head", "Count", -1) < 0)
	{
		setTransferMessage(message, u8"角色装备记录无法读取");
		return false;
	}
	GoodsManager::normalizeEquipmentRecord(*ini);
	const std::string section = std::to_string(gm->goodsManager.equipIndex(slot) + 1);
	if (ini->GetInteger(section, "Number", 0) > 1
		&& !equalsGoodsFileName(ini->Get(section, "IniFile", ""), replacement))
	{
		setTransferMessage(message, u8"该角色背包已满，请切换为主角并腾出空间后再更换此装备");
		return false;
	}
	return true;
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

bool PartnerManager::restoreCharacterEquipment(NPC& partner, int characterIndex)
{
	partner.characterEquipmentSynchronized = false;
	const std::string fileName = std::string(GOODS_INI_NAME) + std::to_string(characterIndex) + GOODS_INI_EXT;
	std::shared_ptr<INIReader> ini;
	const auto status = readCharacterRecord(fileName, ini);
	if (status == SaveIniPersistence::ReadStatus::Missing)
	{
		partner.characterEquipmentSynchronized = true;
		return true;
	}
	if (status != SaveIniPersistence::ReadStatus::Loaded || !ini->HasSection("Head")
		|| ini->GetInteger("Head", "Count", -1) < 0)
	{
		GameLog::write("PartnerManager: invalid equipment record %s\n", fileName.c_str());
		return false;
	}
	std::string* slots[] = { &partner.headEquip, &partner.neckEquip, &partner.bodyEquip,
		&partner.backEquip, &partner.handEquip, &partner.wristEquip, &partner.footEquip };
	std::string equipment[GOODS_BODY_COUNT];
	for (int i = 0; i < GOODS_BODY_COUNT; ++i)
	{
		const std::string section = std::to_string(gm->goodsManager.equipIndex(i) + 1);
		equipment[i] = ini->GetInteger(section, "Number", 0) > 0 ? ini->Get(section, "IniFile", "") : "";
		if (!equipment[i].empty())
		{
			Goods goods;
			goods.initFromIni(equipment[i]);
			if (!goods.loadSucceeded || goods.kind != gkEquipment)
			{
				GameLog::write("PartnerManager: invalid equipment %s in %s\n", equipment[i].c_str(), fileName.c_str());
				return false;
			}
		}
	}
	for (int i = 0; i < GOODS_BODY_COUNT; ++i) *slots[i] = equipment[i];
	partner.updateEquipmentAttributes(false);
	partner.characterEquipmentSynchronized = true;
	return true;
}

bool PartnerManager::saveCharacterSnapshot(const std::shared_ptr<NPC>& partner)
{
	const bool equipmentOnly = partner != nullptr && partner->relation == nrNeutral && partner->canEquip > 0;
	if (gm == nullptr || !gm->global.feature.restorePartnerAttributes || partner == nullptr
		|| !isActivePartner(partner) || (partner->relation != nrFriendly && !equipmentOnly)) return true;
	const int index = partner->getPartnerCharacterIndex();
	if (index < 0) return true;
	if (!partner->characterEquipmentSynchronized)
	{
		GameLog::write("PartnerManager: unresolved legacy equipment for %s\n", partner->npcName.c_str());
		return false;
	}
	File::ResourceLookupScope resourceLookup;
	const std::string playerFile = std::string(PLAYER_INI_NAME) + std::to_string(index) + PLAYER_INI_EXT;
	const std::string goodsFile = std::string(GOODS_INI_NAME) + std::to_string(index) + GOODS_INI_EXT;
	std::shared_ptr<INIReader> playerIni;
	std::shared_ptr<INIReader> goodsIni;
	int savedLevel = 0;
	if (readCharacterRecord(playerFile, playerIni) != SaveIniPersistence::ReadStatus::Loaded
		|| playerIni->Get("Init", "Name", "") != partner->npcName
		|| !NPCPersistence::readBoundedInteger(*playerIni, "Init", "Level", 1,
			NPCPersistence::MaximumLevelCount, savedLevel))
	{
		GameLog::write("PartnerManager: cannot update character record %s\n", playerFile.c_str());
		return false;
	}
	if (savedLevel > partner->level && !equipmentOnly) return true;
	const auto goodsStatus = readCharacterRecord(goodsFile, goodsIni);
	if (goodsStatus == SaveIniPersistence::ReadStatus::Missing) goodsIni = std::make_shared<INIReader>();
	else if (goodsStatus != SaveIniPersistence::ReadStatus::Loaded || !goodsIni->HasSection("Head")
		|| goodsIni->GetInteger("Head", "Count", -1) < 0)
	{
		GameLog::write("PartnerManager: cannot update equipment record %s\n", goodsFile.c_str());
		return false;
	}
    GoodsManager::normalizeEquipmentRecord(*goodsIni);
    for (int i = 0; i < GOODS_BODY_COUNT; ++i)
    {
        const std::string section = std::to_string(gm->goodsManager.equipIndex(i) + 1);
        if (goodsIni->GetInteger(section, "Number", 0) > 1
            && !equalsGoodsFileName(goodsIni->Get(section, "IniFile", ""), partner->getEquipmentFileByPartIndex(i)))
        {
            GameLog::write("PartnerManager: cannot replace equipment while saved bag overflow remains in %s\n", goodsFile.c_str());
            gm->showMessage(u8"角色背包已满，需先腾出空间保存多余装备");
            return false;
        }
    }
	const std::pair<const char*, int> attributes[] = {
		{ "Level", partner->level }, { "Exp", partner->exp }, { "LevelUpExp", partner->levelUpExp },
		{ "Life", partner->life }, { "LifeMax", partner->lifeMax },
		{ "Thew", partner->thew }, { "ThewMax", partner->thewMax },
		{ "Mana", partner->mana }, { "ManaMax", partner->manaMax },
		{ "Attack", partner->attack }, { "Attack2", partner->attack2 }, { "Attack3", partner->attack3 },
		{ "Defend", partner->defend }, { "Defend2", partner->defend2 }, { "Defend3", partner->defend3 },
		{ "Evade", partner->evade }
	};
	if (!equipmentOnly)
	{
		for (const auto& attribute : attributes) playerIni->SetInteger("Init", attribute.first, attribute.second);
		playerIni->Set("Init", "LevelIni", partner->npcLevelIni);
	}
	for (int i = 0; i < GOODS_BODY_COUNT; ++i)
	{
		const std::string section = std::to_string(gm->goodsManager.equipIndex(i) + 1);
		const std::string equipment = partner->getEquipmentFileByPartIndex(i);
		goodsIni->Set(section, "IniFile", equipment);
		goodsIni->SetInteger(section, "Number", equipment.empty() ? 0
			: std::max<long>(1, goodsIni->GetInteger(section, "Number", 0)));
	}
	int count = 0;
	for (const auto& section : goodsIni->GetSectionNames())
	{
		if (!goodsIni->Get(section, "IniFile", "").empty() && goodsIni->GetInteger(section, "Number", 0) > 0) ++count;
	}
	goodsIni->SetInteger("Head", "Count", count);
	if ((!equipmentOnly && !playerIni->saveToFile(SaveFileManager::CurrentPath() + playerFile))
		|| !goodsIni->saveToFile(SaveFileManager::CurrentPath() + goodsFile)) return false;
	if (!equipmentOnly) SaveFileManager::AppendFile(playerFile);
	SaveFileManager::AppendFile(goodsFile);
	return true;
}

bool PartnerManager::saveCharacterSnapshots()
{
	File::ResourceLookupScope resourceLookup;
	for (const auto& partner : findPartnersFromNPCManager())
	{
		if (!saveCharacterSnapshot(partner)) return false;
	}
	return true;
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
	if (!isActivePartner(partner) || !partner->canChangePartnerEquipment())
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
			if (gm->global.goodsLayout.listType != 1
				&& (gm->goodsManager.isStoreIndex(index)
					|| gm->goodsManager.isBottomIndex(index))
				&& equalsGoodsFileName(gm->goodsManager.goodsList[index].iniFile, oldItemFile)
				&& gm->goodsManager.goodsList[index].number < INT_MAX)
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

	if (!canReplaceRecordedEquipment(partner, equipmentSlotIndex, newItemFile, message)) return false;
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
		if (equalsGoodsFileName(destination.iniFile, oldItemFile))
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
	if (!isActivePartner(partner) || !partner->canChangePartnerEquipment())
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
	if (!canReplaceRecordedEquipment(partner, equipmentSlotIndex, "", message)) return false;
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
	if (!isActivePartner(partner) || !partner->canChangePartnerEquipment())
	{
		setTransferMessage(message, "同伴当前不能更换装备");
		return false;
	}
	if ((!gm->goodsManager.isStoreIndex(playerBagIndex)
			&& !gm->goodsManager.isBottomIndex(playerBagIndex))
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
	const std::string replacement = !sourceIsPlayerBag && equalsGoodsFileName(playerGoods.iniFile, partnerGoodsFile)
		? "" : playerGoods.iniFile;
	if (!canReplaceRecordedEquipment(partner, equipmentSlotIndex, replacement, message)) return false;

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
		gm->goodsManager.refreshEquipmentEffects();
		setTransferMessage(message, "");
		return true;
	}

	if (!playerGoodsExists)
	{
		GoodsInfo* destination = &playerGoods;
		if (gm->goodsManager.isStoreIndex(playerBagIndex) && gm->global.goodsLayout.listType != 1)
		{
			for (int i = gm->goodsManager.storeBegin(); i <= gm->goodsManager.storeEnd(); ++i)
			{
				auto& item = gm->goodsManager.goodsList[i];
				if (gm->goodsManager.goodsListExists(i) && item.number < INT_MAX
					&& equalsGoodsFileName(item.iniFile, partnerGoodsFile))
				{
					destination = &item;
					break;
				}
			}
		}
		if (!partner->setEquipmentFileByPartIndex(equipmentSlotIndex, ""))
		{
			setTransferMessage(message, "同伴装备位置无效");
			return false;
		}
		if (destination == &playerGoods)
		{
			playerGoods = makeSingleGoodsInfo(partnerGoodsFile);
		}
		else
		{
			destination->number++;
		}
		setTransferMessage(message, "");
		return true;
	}
	if (playerGoods.iniFile == partnerGoodsFile)
	{
		if (gm->global.goodsLayout.listType == 1)
		{
			setTransferMessage(message, "");
			return true;
		}
		if (playerGoods.number == INT_MAX)
		{
			setTransferMessage(message, u8"该物品数量已达到上限");
			return false;
		}
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
	if (!isActivePartner(partner) || !partner->canChangePartnerEquipment())
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

	if (!canReplaceRecordedEquipment(partner, sourceSlotIndex, targetFile, message)
		|| !canReplaceRecordedEquipment(partner, targetSlotIndex, sourceFile, message)) return false;
	partner->setEquipmentFileByPartIndex(sourceSlotIndex, targetFile);
	partner->setEquipmentFileByPartIndex(targetSlotIndex, sourceFile);
	setTransferMessage(message, "");
	return true;
}

bool PartnerManager::load(int index, std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}
	std::string fName =
		SaveFileManager::CurrentPath() + PARTNER_INI_NAME;
	std::string displayName = PARTNER_INI_NAME;
	if (index >= 0)
	{
		fName += convert::formatString("%d", index);
		displayName += convert::formatString("%d", index);
	}
	fName += PARTNER_INI_EXT;
	displayName += PARTNER_INI_EXT;

	std::shared_ptr<INIReader> loadedIni;
	const SaveIniPersistence::ReadStatus status =
		SaveIniPersistence::read(fName, loadedIni);
	if (status == SaveIniPersistence::ReadStatus::Missing)
	{
		freeResource();
		removeCurrentPartners();
		return true;
	}
	if (status != SaveIniPersistence::ReadStatus::Loaded ||
		loadedIni == nullptr)
	{
		GameLog::write("PartnerManager: invalid partner file %s\n", fName.c_str());
		if (failureReason != nullptr)
		{
			*failureReason = u8"同伴数据文件无法读取或格式错误：" +
				displayName;
		}
		return false;
	}
	INIReader& ini = *loadedIni;
	if (!ini.HasSection("Head") &&
		(ini.HasSection("Init") || ini.HasSection("Common")))
	{
		GameLog::write(
			"PartnerManager: compatible NPC template %s is not a partner list; loading an empty list\n",
			fName.c_str());
		freeResource();
		removeCurrentPartners();
		return true;
	}

	int count = 0;
	if (!NPCPersistence::readCount(ini, NPCPersistence::MaximumPartnerCount, count))
	{
		GameLog::write("PartnerManager: invalid partner count in %s\n", fName.c_str());
		if (failureReason != nullptr)
		{
			*failureReason = u8"同伴数据数量无效：" + displayName;
		}
		return false;
	}
	std::string sectionPrefix = "Partner";
	int sectionIndexOffset = 0;
	if (count > 0 && !ini.HasSection("Partner000"))
	{
		if (ini.HasSection("NPC000"))
		{
			sectionPrefix = "NPC";
		}
		else if (ini.HasSection("1"))
		{
			sectionPrefix.clear();
			sectionIndexOffset = 1;
		}
	}
	const auto partnerSection =
		[&sectionPrefix, sectionIndexOffset](int index)
		{
			return sectionPrefix.empty()
				? std::to_string(index + sectionIndexOffset)
				: convert::formatString(
					"%s%03d", sectionPrefix.c_str(), index);
		};
	int loadCount = 0;
	for (int index = 0; index < count; ++index)
	{
		const std::string section = partnerSection(index);
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
		if (failureReason != nullptr)
		{
			*failureReason = u8"同伴数量超过运行上限：" + displayName;
		}
		return false;
	}

	std::vector<std::shared_ptr<NPC>> loadedPartnerList;
	loadedPartnerList.reserve(static_cast<size_t>(loadCount));
	bool legacyEquipment = false;
	if (gm->global.feature.restorePartnerAttributes && loadCount > 0)
	{
		std::shared_ptr<INIReader> globalIni;
		const auto globalStatus = SaveIniPersistence::read(SaveFileManager::CurrentPath() + GLOBAL_INI, globalIni);
		const auto savedVersion = ModRelease::parseSemanticVersion(globalStatus == SaveIniPersistence::ReadStatus::Loaded
			? globalIni->Get("Save", "EngineVersion", "1.0.0") : "");
		legacyEquipment = savedVersion.succeeded()
			&& ModRelease::compareSemanticVersionPrecedence(savedVersion.version,
				ModRelease::parseSemanticVersion("1.1.5").version) < 0;
	}
	for (int i = 0; i < loadCount; ++i)
	{
		const std::string section = partnerSection(i);
		auto partner = std::make_shared<NPC>();
		partner->initFromIni(&ini, section);
		const int characterIndex = partner->getPartnerCharacterIndex();
		// Before 1.1.5 these partners could not equip items: goodsN.ini owns their equipment.
		if (legacyEquipment && characterIndex >= 0 && partner->relation == nrFriendly && partner->canEquip <= 0)
		{
			partner->characterEquipmentSynchronized = false;
			std::shared_ptr<INIReader> character;
			const std::string characterFile = std::string(PLAYER_INI_NAME) + std::to_string(characterIndex) + PLAYER_INI_EXT;
			if (readCharacterRecord(characterFile, character) != SaveIniPersistence::ReadStatus::Loaded
				|| character->Get("Init", "Name", "") != partner->npcName
				|| !restoreCharacterEquipment(*partner, characterIndex))
			{
				GameLog::write("PartnerManager: cannot restore legacy equipment for %s\n", characterFile.c_str());
			}
			else if (partner->level > ini.GetInteger(section, "Level", 1)) partner->refillPartnerResources();
		}
		loadedPartnerList.push_back(partner);
	}

	freeResource();
	removeCurrentPartners();
	for (const auto& partner : loadedPartnerList)
	{
		gm->npcManager->addNPC(partner);
	}
	return true;
}

bool PartnerManager::save(int index)
{
	if (!saveCharacterSnapshots()) return false;
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

void PartnerManager::clearCurrentPartners()
{
	freeResource();
	removeCurrentPartners();
}

void PartnerManager::freeResource()
{
	tempPartnerList.resize(0);
}
