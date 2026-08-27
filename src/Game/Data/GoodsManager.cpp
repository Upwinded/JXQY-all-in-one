#include "GoodsManager.h"
#include "Player.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <utility>

namespace
{
std::string toLowerAsciiCopy(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool equalsIgnoreAsciiCase(const std::string& left, const std::string& right)
{
	return toLowerAsciiCopy(left) == toLowerAsciiCopy(right);
}

bool equalsGoodsFileName(const std::string& left, const std::string& right)
{
	return !left.empty() && !right.empty() && equalsIgnoreAsciiCase(left, right);
}

int saturatingGoodsCountAdd(int current, int added)
{
	const int64_t total = static_cast<int64_t>(std::max(0, current)) +
		static_cast<int64_t>(std::max(0, added));
	return static_cast<int>(std::min<int64_t>(total, INT_MAX));
}

void limitNpcDrugAttributes(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}
	int lifeMax = npc->getLifeMax();
	if (lifeMax > 0 && npc->life > lifeMax)
	{
		npc->life = lifeMax;
	}
	int thewMax = npc->getThewMax();
	if (thewMax > 0 && npc->thew > thewMax)
	{
		npc->thew = thewMax;
	}
	int manaMax = npc->getManaMax();
	if (manaMax > 0 && npc->mana > manaMax)
	{
		npc->mana = manaMax;
	}
}

void applyDrugEffect(std::shared_ptr<NPC> npc, std::shared_ptr<Goods> goods)
{
	if (npc == nullptr || goods == nullptr)
	{
		return;
	}
	npc->lifeMax += goods->lifeMax;
	npc->thewMax += goods->thewMax;
	npc->manaMax += goods->manaMax;
	auto player = std::dynamic_pointer_cast<Player>(npc);
	if (player != nullptr)
	{
		player->calInfo();
	}
	npc->life += goods->life;
	npc->thew += goods->thew;
	npc->mana += goods->mana;
	switch (goods->effectType)
	{
	case 1:
		npc->clearFrozenState();
		break;
	case 2:
		npc->clearPoisonedState();
		break;
	case 3:
		npc->clearPetrifiedState();
		break;
	default:
		break;
	}
	limitNpcDrugAttributes(npc);
}

void playDrugUseSound()
{
	if (gm == nullptr)
	{
		return;
	}
	gm->scriptAPI.playSound("界-使用物品.wav");
}

bool validateGoodsUserRestriction(std::shared_ptr<Goods> goods, std::shared_ptr<NPC> user, bool equipmentUse, std::string* message)
{
	if (goods == nullptr)
	{
		return false;
	}
	if (user != nullptr)
	{
		std::vector<std::string> userNameCandidates = {
			user->npcName,
			user->getDisplayName(),
			user->showName,
			user->name,
		};
		if (!goods->isAllowedForAnyUserName(userNameCandidates))
		{
			if (message != nullptr)
			{
				*message = "使用者：" + goods->userRestrictionText();
			}
			return false;
		}
	}
	std::shared_ptr<NPC> levelUser = equipmentUse ? user : gm->player;
	if (goods->minUserLevel > 0 && levelUser != nullptr && levelUser->level < goods->minUserLevel)
	{
		if (message != nullptr)
		{
			*message = convert::formatString("需要等级%d", goods->minUserLevel);
		}
		return false;
	}
	if (equipmentUse && goods->sex > 0 && user != nullptr && user->sex > 0 && user->sex != goods->sex)
	{
		if (message != nullptr)
		{
			*message = "无法装备该物品";
		}
		return false;
	}
	return true;
}

int currentBuyPercent()
{
	if (gm != nullptr && gm->menu != nullptr && gm->menu->buySellMenu != nullptr && gm->menu->buySellMenu->visible)
	{
		return gm->menu->buySellMenu->buyPercent;
	}
	return 100;
}

int currentRecyclePercent()
{
	if (gm != nullptr && gm->menu != nullptr && gm->menu->buySellMenu != nullptr && gm->menu->buySellMenu->visible)
	{
		return gm->menu->buySellMenu->recyclePercent;
	}
	return 100;
}

bool shouldInstantiateRandomGoods(const Goods& goods)
{
	return goods.kind == gkEquipment && goods.hasRandomAttributes();
}

bool isItemPerSlotListType()
{
	return gm != nullptr && gm->global.goodsLayout.listType == 1;
}

int findFreeBagIndex(const GoodsManager& manager)
{
	for (int i = manager.storeBegin(); i <= manager.bottomEnd() && i < manager.listLength(); i++)
	{
		if ((manager.isStoreIndex(i) || manager.isBottomIndex(i)) &&
			manager.goodsList[i].iniFile.empty())
		{
			return i;
		}
	}
	return -1;
}

int countFreeBagSlots(const GoodsManager& manager)
{
	int count = 0;
	for (int i = manager.storeBegin(); i <= manager.bottomEnd() && i < manager.listLength(); i++)
	{
		if ((manager.isStoreIndex(i) || manager.isBottomIndex(i)) &&
			manager.goodsList[i].iniFile.empty())
		{
			count++;
		}
	}
	return count;
}

bool setGoodsListSlot(GoodsInfo& goodsInfo, const std::string& itemName, const Goods& templateGoods, int number)
{
	auto goods = std::make_shared<Goods>();
	*goods = templateGoods.sourceFileName == itemName ? templateGoods : Goods();
	if (goods->sourceFileName != itemName)
	{
		goods->initFromIni(itemName);
	}
	if (!goods->loadSucceeded)
	{
		return false;
	}
	goodsInfo.iniFile = itemName;
	goodsInfo.number = number;
	goodsInfo.goods = goods;
	goodsInfo.remainColdMilliseconds = 0;
	return true;
}
}


GoodsManager::GoodsManager()
{
	configureLayout();
}


GoodsManager::~GoodsManager()
{
	freeResource();
}

void GoodsManager::freeResource()
{
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		goodsList[i].clear();
	}
}

void GoodsManager::load(int index)
{
	configureLayout();
	freeResource();
	std::string fName =
		SaveFileManager::CurrentPath() + GOODS_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += GOODS_INI_EXT;
	INIReader ini(fName);

	for (size_t i = 0; i < goodsList.size(); i++)
	{
		std::string section = convert::formatString("%d", i + 1);
		goodsList[i].iniFile = ini.Get(section, "IniFile", "");
		goodsList[i].number = ini.GetInteger(section, "Number", 0);
		if (goodsList[i].iniFile.empty())
		{
			goodsList[i].clear();
		}
		else
		{
			if (goodsList[i].number <= 0)
			{
				goodsList[i].clear();
			}
			else
			{
				goodsList[i].goods = std::make_shared<Goods>();
				goodsList[i].goods->initFromIni(goodsList[i].iniFile);
				if (!goodsList[i].goods->loadSucceeded)
				{
					GameLog::write("GoodsManager: ignored invalid saved goods %s\n",
						goodsList[i].iniFile.c_str());
					goodsList[i].clear();
				}
				else
				{
					goodsList[i].remainColdMilliseconds = 0;
				}
			}
		}
	}
	if (gm != nullptr && gm->player != nullptr)
	{
		gm->player->resetEquipmentGrantedMagicSync();
	}
	// The player file already contains the saved current values. Rebuilding
	// equipment maxima during load must not apply their bonuses a second time.
	refreshEquipmentEffects(false);
}

bool GoodsManager::save(int index)
{
	INIReader ini;
	std::string section = "Head";
	ini.SetInteger(section, "Count", 0);
	int count = 0;
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (goodsList[i].iniFile != "" && goodsList[i].number > 0)
		{
			count++;
			section = convert::formatString("%d", i + 1);
			ini.Set(section, "IniFile", goodsList[i].iniFile);
			ini.SetInteger(section, "Number", goodsList[i].number);
		}
	}
	section = "Head";
	ini.SetInteger(section, "Count", count);
    std::string fName = GOODS_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += GOODS_INI_EXT;
	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
	return saved;
}

GoodsInfo * GoodsManager::findGoods(const std::string & itemName)
{
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (equalsGoodsFileName(goodsList[i].iniFile, itemName))
		{
			return &goodsList[i];
		}
	}
	return nullptr;
}

void GoodsManager::clearItem()
{
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		goodsList[i].clear();
	}
	refreshEquipmentEffects();
	updateMenu();
}

int GoodsManager::getItemNum(const std::string & itemName)
{
	int64_t count = 0;
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (equalsGoodsFileName(goodsList[i].iniFile, itemName))
		{
			count += std::max(0, goodsList[i].number);
			if (count >= INT_MAX)
			{
				return INT_MAX;
			}
		}
	}
	return static_cast<int>(count);
}

int GoodsManager::getItemNumByDisplayName(const std::string& name)
{
	int64_t count = 0;
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (goodsList[i].goods != nullptr &&
			equalsIgnoreAsciiCase(goodsList[i].goods->name, name))
		{
			count += std::max(0, goodsList[i].number);
			if (count >= INT_MAX)
			{
				return INT_MAX;
			}
		}
	}
	return static_cast<int>(count);
}

void GoodsManager::setItemNum(const std::string & itemName, int num)
{
	if (num <= 0)
	{
		for (size_t i = 0; i < goodsList.size(); i++)
		{
			if (equalsGoodsFileName(goodsList[i].iniFile, itemName))
			{
				goodsList[i].clear();
				updateMenu(static_cast<int>(i));
			}
		}
		refreshEquipmentEffects();
	}
	else
	{
		if (isItemPerSlotListType())
		{
			int currentCount = getItemNum(itemName);
			if (currentCount > num)
			{
				int deleteCount = currentCount - num;
				for (int i = 0; i < deleteCount; i++)
				{
					deleteItem(itemName);
				}
			}
			else if (currentCount < num)
			{
				addItem(itemName, num - currentCount);
			}
			return;
		}
		GoodsInfo * g = findGoods(itemName);
		if (g != nullptr)
		{
			g->number = num;
			refreshEquipmentEffects();
		}
		else
		{
			addItem(itemName, num);
		}
		updateMenu();
	}
}

void GoodsManager::exchange(int index1, int index2)
{
	if (index1 >= 0 && index1 < listLength() && index2 >= 0 && index2 < listLength())
	{
		GoodsInfo tempInfo = goodsList[index1];
		goodsList[index1] = goodsList[index2];
		goodsList[index2] = tempInfo;
		refreshEquipmentEffects();
	}
}

void GoodsManager::sellItem(const std::string & itemName)
{
	if (itemName.empty())
	{
		return;
	}
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (equalsGoodsFileName(goodsList[i].iniFile, itemName) && goodsList[i].goods != nullptr)
		{
			int price = goodsList[i].goods->getSellPrice(currentRecyclePercent());
			if (price <= 0)
			{
				return;
			}
			goodsList[i].number--;
			gm->player->addMoney(price);
			if (goodsList[i].number <= 0)
			{
				goodsList[i].clear();
			}
			refreshEquipmentEffects();
			updateMenu(i);
			return;
		}
	}
}

bool GoodsManager::addItem(const std::string & itemName, int num)
{
	if (itemName.empty())
	{
		return false;
	}
	if (num <= 0)
	{
		return false;
	}

	Goods templateGoods;
	templateGoods.initFromIni(itemName);
	if (!templateGoods.loadSucceeded)
	{
		GameLog::write("GoodsManager: cannot add invalid goods %s\n", itemName.c_str());
		return false;
	}
	if (shouldInstantiateRandomGoods(templateGoods))
	{
		if (countFreeBagSlots(*this) < num)
		{
			return false;
		}

		std::vector<Goods> instances;
		instances.reserve(static_cast<size_t>(num));
		for (int i = 0; i < num; i++)
		{
			Goods instance = templateGoods.createNonRandomInstance();
			if (instance.sourceFileName.empty() || !instance.saveRuntimeIni(instance.sourceFileName))
			{
				return false;
			}
			instances.push_back(std::move(instance));
		}
		int lastIndex = -1;
		for (const Goods& instance : instances)
		{
			const int freeIndex = findFreeBagIndex(*this);
			if (freeIndex < 0 ||
				!setGoodsListSlot(goodsList[freeIndex], instance.sourceFileName, instance, 1))
			{
				return false;
			}
			lastIndex = freeIndex;
			updateMenu(freeIndex);
		}
		refreshEquipmentEffects();
		if (lastIndex >= 0 && goodsList[lastIndex].goods != nullptr)
		{
			gm->showMessage(convert::formatString("得到%s!", goodsList[lastIndex].goods->name.c_str()));
		}
		return true;
	}

	if (!isItemPerSlotListType())
	{
		for (size_t i = 0; i < goodsList.size(); i++)
		{
			if (equalsGoodsFileName(goodsList[i].iniFile, itemName))
			{
				goodsList[i].number = saturatingGoodsCountAdd(goodsList[i].number, num);
				refreshEquipmentEffects();
				updateMenu(static_cast<int>(i));
				gm->showMessage(convert::formatString("得到%s!", goodsList[i].goods->name.c_str()));
				return true;
			}
		}
	}

	if (isItemPerSlotListType())
	{
		if (countFreeBagSlots(*this) < num)
		{
			return false;
		}
		int lastIndex = -1;
		for (int count = 0; count < num; count++)
		{
			int freeIndex = findFreeBagIndex(*this);
			if (freeIndex < 0)
			{
				return false;
			}
			if (!setGoodsListSlot(goodsList[freeIndex], itemName, templateGoods, 1))
			{
				return false;
			}
			lastIndex = freeIndex;
			updateMenu(freeIndex);
		}
		refreshEquipmentEffects();
		if (lastIndex >= 0 && goodsList[lastIndex].goods != nullptr)
		{
			gm->showMessage(convert::formatString("得到%s!", goodsList[lastIndex].goods->name.c_str()));
		}
		return true;
	}

	int freeIndex = findFreeBagIndex(*this);
	if (freeIndex >= 0)
	{
		if (!setGoodsListSlot(goodsList[freeIndex], itemName, templateGoods, num))
		{
			return false;
		}
		refreshEquipmentEffects();
		updateMenu(freeIndex);
		gm->showMessage(convert::formatString("得到%s!", goodsList[freeIndex].goods->name.c_str()));
		return true;
	}
	return false;
}

void GoodsManager::clearItemByFileName(const std::string& itemName)
{
	if (itemName.empty())
	{
		return;
	}
	bool cleared = false;
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (equalsGoodsFileName(goodsList[i].iniFile, itemName))
		{
			goodsList[i].clear();
			cleared = true;
			updateMenu(static_cast<int>(i));
			if (!isItemPerSlotListType())
			{
				break;
			}
		}
	}
	if (cleared)
	{
		refreshEquipmentEffects();
	}
}

void GoodsManager::deleteItem(const std::string & itemName)
{
	if (itemName.empty())
	{
		return;
	}
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (equalsGoodsFileName(goodsList[i].iniFile, itemName))
		{
			goodsList[i].number--;
			if (goodsList[i].number <= 0)
			{
				goodsList[i].clear();
			}
			refreshEquipmentEffects();
			updateMenu((int)i);
			return;
		}
	}
}

void GoodsManager::deleteItemByDisplayName(const std::string& name, int count)
{
	if (name.empty())
	{
		return;
	}
	int64_t deletedCount = 0;
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		if (goodsList[i].goods == nullptr || !equalsIgnoreAsciiCase(goodsList[i].goods->name, name))
		{
			continue;
		}
		if (count <= 0 || static_cast<int64_t>(goodsList[i].number) <=
			static_cast<int64_t>(count) - deletedCount)
		{
			deletedCount += goodsList[i].number;
			goodsList[i].clear();
		}
		else
		{
			goodsList[i].number -= static_cast<int>(static_cast<int64_t>(count) - deletedCount);
			deletedCount = count;
		}
		refreshEquipmentEffects();
		updateMenu((int)i);
		if (count > 0 && deletedCount >= count)
		{
			return;
		}
	}
}

bool GoodsManager::buyItem(const std::string & itemName, int num)
{
	if (num <= 0)
	{
		return false;
	}
	Goods goods;
	goods.initFromIni(itemName);
	if (!goods.loadSucceeded)
	{
		gm->showMessage("物品数据无效！");
		return false;
	}
	const int unitCost = goods.getBuyPrice(currentBuyPercent());
	const int64_t totalCost = static_cast<int64_t>(unitCost) * num;
	if (totalCost > INT_MAX || gm->player->money < totalCost)
	{
		gm->showMessage("金钱不足！");
		return false;
	}
	if (addItem(itemName, num))
	{
		gm->player->money -= static_cast<int>(totalCost);
		return true;
	}
	gm->showMessage("物品栏位置已满！");
	return false;
}

bool GoodsManager::useItem(int itemIndex)
{
	if (itemIndex < 0 || itemIndex >= listLength())
	{
		return false;
	}
	if (!goodsListExists(itemIndex))
	{
		goodsList[itemIndex].clear();
		return false;
	}
	std::string message;
	if (!canUseGoods(itemIndex, gm->player, &message))
	{
		if (!message.empty())
		{
			gm->showMessage(message);
		}
		return false;
	}
	std::string goodsScript = goodsList[itemIndex].goods->script;
	if (goodsScript != "")
	{
		gm->runGoodsScript(goodsList[itemIndex].goods);
		return true;
	}
	else if (goodsList[itemIndex].goods->kind == gkDrug)
	{
		auto& goodsInfo = goodsList[itemIndex];
		if (goodsInfo.remainColdMilliseconds > 0)
		{
			gm->showMessage("该物品尚未冷却");
			return false;
		}
		auto goods = goodsList[itemIndex].goods;
		if (goods->coldMilliSeconds > 0)
		{
			goodsInfo.remainColdMilliseconds = goods->coldMilliSeconds;
		}
		applyDrugEffect(gm->player, goods);
		gm->player->limitAttribute();
		if (goods->fighterFriendHasDrugEffect > 0)
		{
			auto friendFighters = gm->npcManager->findFriendFighters();
			for (auto& friendFighter : friendFighters)
			{
				applyDrugEffect(friendFighter, goods);
			}
		}
		else if (goods->followPartnerHasDrugEffect > 0)
		{
			auto partners = gm->partnerManager.findPartnersFromNPCManager();
			for (auto& partner : partners)
			{
				applyDrugEffect(partner, goods);
			}
		}
		playDrugUseSound();
		goodsList[itemIndex].number--;
		if (goodsList[itemIndex].number <= 0)
		{
			goodsList[itemIndex].clear();
		}
		if (isStoreIndex(itemIndex))
		{
			gm->menu->goodsMenu->updateGoods();
		}
		else if (isBottomIndex(itemIndex))
		{
			gm->menu->bottomMenu->updateGoodsItem();
		}
		return true;
	}
	else if (goodsList[itemIndex].goods->kind == gkEquipment)
	{
		if (goodsList[itemIndex].goods->noNeedToEquip > 0)
		{
			refreshEquipmentEffects();
			return true;
		}
		int partIndex = gm->menu->equipMenu->getPartIndex(goodsList[itemIndex].goods->part);
		if (partIndex >= 0)
		{
			exchange(itemIndex, equipIndex(partIndex));
			gm->menu->equipMenu->updateGoods();
			gm->player->limitAttribute();
			if (isStoreIndex(itemIndex))
			{
				gm->menu->goodsMenu->updateGoods();
			}
			else if (isBottomIndex(itemIndex))
			{
				gm->menu->bottomMenu->updateGoodsItem();
			}
			return true;
		}
	}
	return false;
}

void GoodsManager::refreshEquipmentEffects(bool adjustCurrentValues)
{
	if (gm == nullptr || gm->player == nullptr)
	{
		return;
	}
	const int previousLifeMax = gm->player->getLifeMax();
	const int previousThewMax = gm->player->getThewMax();
	const int previousManaMax = gm->player->getManaMax();
	gm->player->calInfo();
	if (adjustCurrentValues)
	{
		gm->player->adjustCurrentAttributesAfterMaximumChange(
			previousLifeMax,
			previousThewMax,
			previousManaMax);
	}
	else
	{
		gm->player->limitAttribute();
	}
}

bool GoodsManager::canUseGoods(int itemIndex, std::shared_ptr<NPC> user, std::string* message) const
{
	if (itemIndex < 0 || itemIndex >= listLength())
	{
		return false;
	}
	if (goodsList[itemIndex].iniFile.empty() || goodsList[itemIndex].goods == nullptr || goodsList[itemIndex].number <= 0)
	{
		return false;
	}
	auto goods = goodsList[itemIndex].goods;
	bool equipmentUse = goods != nullptr && goods->kind == gkEquipment;
	return validateGoodsUserRestriction(goods, user, equipmentUse, message);
}

bool GoodsManager::canEquipGoodsAt(int itemIndex, int partIndex, std::shared_ptr<NPC> user, std::string* message) const
{
	if (!canUseGoods(itemIndex, user, message))
	{
		return false;
	}
	return canEquipGoodsAt(goodsList[itemIndex].goods, partIndex, user, message);
}

bool GoodsManager::canEquipGoodsAt(
	const std::shared_ptr<Goods>& goods,
	int partIndex,
	std::shared_ptr<NPC> user,
	std::string* message) const
{
	if (!validateGoodsUserRestriction(goods, user, true, message))
	{
		return false;
	}
	if (goods == nullptr || goods->kind != gkEquipment)
	{
		if (message != nullptr)
		{
			*message = "只能装备武器防具";
		}
		return false;
	}
	if (goods->noNeedToEquip > 0)
	{
		if (message != nullptr)
		{
			*message = "无需装备";
		}
		return false;
	}
	if (NPC::getEquipmentPartIndex(goods->part) != partIndex)
	{
		if (message != nullptr)
		{
			*message = "装备位置不符";
		}
		return false;
	}
	return true;
}

void GoodsManager::updateColdTimes(UTime frameTime)
{
	if (frameTime == 0)
	{
		return;
	}
	for (auto& goodsInfo : goodsList)
	{
		goodsInfo.updateColdTime(frameTime);
	}
}

bool GoodsManager::unequipToFirstStoreSlot(
	int equipmentIndex, std::string* message)
{
	if (!isEquipIndex(equipmentIndex)
		|| equipmentIndex < 0 || equipmentIndex >= listLength())
	{
		if (message != nullptr)
		{
			*message = "装备位置无效";
		}
		return false;
	}
	if (!goodsListExists(equipmentIndex))
	{
		if (message != nullptr)
		{
			*message = "该位置没有装备";
		}
		return false;
	}
	for (int index = storeBegin();
		index <= storeEnd() && index < listLength(); index++)
	{
		if (isStoreIndex(index) && !goodsListExists(index))
		{
			exchange(equipmentIndex, index);
			updateMenu();
			if (message != nullptr)
			{
				message->clear();
			}
			return true;
		}
	}
	if (message != nullptr)
	{
		*message = "包裹没有空位";
	}
	return false;
}

void GoodsManager::updateMenu(int idx)
{
	if (isStoreIndex(idx))
	{
		gm->menu->goodsMenu->updateGoods();
	}
	else if (isBottomIndex(idx))
	{
		gm->menu->bottomMenu->updateGoodsItem();
	}
	else
	{
		gm->menu->equipMenu->updateGoods();
	}
}

void GoodsManager::updateMenu()
{
	gm->menu->goodsMenu->updateGoods();
	gm->menu->bottomMenu->updateGoodsItem();
	gm->menu->equipMenu->updateGoods();
}

bool GoodsManager::goodsListExists(int index)
{
	if (index >= 0 && index < listLength())
	{
		return !gm->goodsManager.goodsList[index].iniFile.empty() && gm->goodsManager.goodsList[index].goods != nullptr && gm->goodsManager.goodsList[index].number > 0;
	}
	return false;
}

void GoodsManager::configureLayout()
{
	int length = GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT;
	if (gm != nullptr)
	{
		length = gm->global.goodsLayout.listLength();
	}
	if (length < 1)
	{
		length = GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT;
	}
	goodsList.assign(static_cast<size_t>(length), GoodsInfo());
}

int GoodsManager::listLength() const
{
	return static_cast<int>(goodsList.size());
}

int GoodsManager::storeBegin() const
{
	return gm != nullptr ? gm->global.goodsLayout.storeBegin : 0;
}

int GoodsManager::storeEnd() const
{
	return gm != nullptr ? gm->global.goodsLayout.storeEnd : GOODS_COUNT - 1;
}

int GoodsManager::bottomCount() const
{
	return gm != nullptr ? gm->global.goodsLayout.bottomCount() : GOODS_TOOLBAR_COUNT;
}

int GoodsManager::bottomBegin() const
{
	return gm != nullptr ? gm->global.goodsLayout.bottomBegin : GOODS_COUNT;
}

int GoodsManager::bottomEnd() const
{
	return gm != nullptr ? gm->global.goodsLayout.bottomEnd : GOODS_COUNT + GOODS_TOOLBAR_COUNT - 1;
}

int GoodsManager::equipBegin() const
{
	return gm != nullptr ? gm->global.goodsLayout.equipBegin : GOODS_COUNT + GOODS_TOOLBAR_COUNT;
}

int GoodsManager::equipEnd() const
{
	return gm != nullptr ? gm->global.goodsLayout.equipEnd : GOODS_COUNT + GOODS_TOOLBAR_COUNT + GOODS_BODY_COUNT - 1;
}

int GoodsManager::bottomIndex(int index) const
{
	return bottomBegin() + index;
}

int GoodsManager::equipIndex(int index) const
{
	return equipBegin() + index;
}

int GoodsManager::bottomSlot(int index) const
{
	return index - bottomBegin();
}

int GoodsManager::equipSlot(int index) const
{
	return index - equipBegin();
}

bool GoodsManager::isStoreIndex(int index) const
{
	return index >= storeBegin() && index <= storeEnd();
}

bool GoodsManager::isBottomIndex(int index) const
{
	return index >= bottomBegin() && index <= bottomEnd();
}

bool GoodsManager::isEquipIndex(int index) const
{
	return index >= equipBegin() && index <= equipEnd();
}
