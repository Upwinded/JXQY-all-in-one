#include "GoodsManager.h"
#include "Player.h"
#include "SaveIniPersistence.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <utility>
#include <unordered_set>

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

void applyDrugEffect(std::shared_ptr<NPC> npc, std::shared_ptr<Goods> goods, bool includePermanentAttributes = true)
{
	if (npc == nullptr || goods == nullptr)
	{
		return;
	}
	if (includePermanentAttributes)
	{
		npc->lifeMax += goods->lifeMax;
		npc->thewMax += goods->thewMax;
		npc->manaMax += goods->manaMax;
	}
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
	if (equipmentUse && goods->sex > 0 && user != nullptr
		&& (user->sex == 0 ? 1 : user->sex) != goods->sex)
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

bool GoodsManager::load(int index, std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}
	std::string fName =
		SaveFileManager::CurrentPath() + GOODS_INI_NAME;
	std::string displayName = GOODS_INI_NAME;
	if (index >= 0)
	{
		fName += convert::formatString("%d", index);
		displayName += convert::formatString("%d", index);
	}
	fName += GOODS_INI_EXT;
	displayName += GOODS_INI_EXT;

	std::shared_ptr<INIReader> loadedIni;
	const SaveIniPersistence::ReadStatus status =
		SaveIniPersistence::read(fName, loadedIni);
	if (status == SaveIniPersistence::ReadStatus::Empty ||
		status == SaveIniPersistence::ReadStatus::Unreadable ||
		status == SaveIniPersistence::ReadStatus::Malformed)
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"物品数据文件无法读取或格式错误：" +
				displayName;
		}
		return false;
	}

	GoodsManager loadedManager;
	if (status == SaveIniPersistence::ReadStatus::Loaded)
	{
		if (loadedIni == nullptr || !loadedIni->HasSection("Head"))
		{
			if (failureReason != nullptr)
			{
				*failureReason = u8"物品数据缺少 [Head]：" + displayName;
			}
			return false;
		}
		const long count = loadedIni->GetInteger("Head", "Count", -1);
		if (count < 0)
		{
			if (failureReason != nullptr)
			{
				*failureReason = u8"物品数据数量无效：" + displayName;
			}
			return false;
		}

		for (size_t i = 0; i < loadedManager.goodsList.size(); i++)
		{
			const std::string section =
				convert::formatString("%d", i + 1);
			GoodsInfo& loadedGoods = loadedManager.goodsList[i];
			loadedGoods.iniFile =
				loadedIni->Get(section, "IniFile", "");
			loadedGoods.number = static_cast<int>(
				loadedIni->GetInteger(section, "Number", 0));
			if (loadedGoods.iniFile.empty() || loadedGoods.number <= 0)
			{
				loadedGoods.clear();
			}
			else
			{
				loadedGoods.goods = std::make_shared<Goods>();
				loadedGoods.goods->initFromIni(loadedGoods.iniFile);
				if (!loadedGoods.goods->loadSucceeded)
				{
					GameLog::write("GoodsManager: ignored invalid saved goods %s\n",
						loadedGoods.iniFile.c_str());
					loadedGoods.clear();
				}
				else
				{
					loadedGoods.remainColdMilliseconds = 0;
				}
			}
		}
	}

	const bool equipmentNormalized = loadedManager.normalizeEquipmentStacks();
	if (!equipmentNormalized)
	{
		GameLog::write("GoodsManager: equipment overflow retained until bag space is available\n");
	}
	goodsList = std::move(loadedManager.goodsList);
	if (!equipmentNormalized && gm) gm->showMessage(u8"背包已满，多余装备将在腾出空间后自动归包");
	if (gm != nullptr && gm->player != nullptr)
	{
		gm->player->resetEquipmentGrantedMagicSync();
	}
	// The player file already contains the saved current values. Rebuilding
	// equipment maxima during load must not apply their bonuses a second time.
	refreshEquipmentEffects(false);
	return true;
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

void GoodsManager::clearItem(bool adjustCurrentValues)
{
	for (size_t i = 0; i < goodsList.size(); i++)
	{
		goodsList[i].clear();
	}
	// Keep the previous grant counts so recalculation can remove equipment-only Magic.
	refreshEquipmentEffects(adjustCurrentValues);
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

void GoodsManager::setItemNum(const std::string& itemName, int num)
{
    int64_t current = 0;
    for (const auto& item : goodsList)
        if (equalsGoodsFileName(item.iniFile, itemName)) current += std::max(0, item.number);
    const int desired = std::max(0, num);
    if (current < desired)
    {
        addItem(itemName, static_cast<int>(desired - current));
        return;
    }
    int64_t remaining = current - desired;
    // Remove carried copies first, retaining the worn item whenever possible.
    for (int equippedPass = 0; equippedPass < 2; ++equippedPass)
    {
        for (int i = 0; i < listLength() && remaining > 0; ++i)
        {
            auto& item = goodsList[i];
            if (isEquipIndex(i) != (equippedPass != 0) || !equalsGoodsFileName(item.iniFile, itemName)) continue;
            const int removed = static_cast<int>(std::min<int64_t>(remaining, std::max(0, item.number)));
            item.number -= removed;
            remaining -= removed;
            if (item.number <= 0) item.clear();
        }
    }
    refreshEquipmentEffects();
    updateMenu();
}

int GoodsManager::returnToBag(const GoodsInfo& item, int count, int preferredIndex)
{
    const int requested = count;
    if (!isItemPerSlotListType())
    {
        for (int i = storeBegin(); i <= storeEnd() && count > 0; ++i)
        {
            if (!equalsGoodsFileName(goodsList[i].iniFile, item.iniFile)) continue;
            const int moved = std::min(count, INT_MAX - std::max(0, goodsList[i].number));
            goodsList[i].number += moved;
            count -= moved;
        }
    }
    auto fill = [&](int index)
    {
        if ((!isStoreIndex(index) && !(index == preferredIndex && isBottomIndex(index)))
            || index < 0 || index >= listLength() || count <= 0
            || !goodsList[index].iniFile.empty()) return;
        goodsList[index] = item;
        goodsList[index].number = isItemPerSlotListType() ? 1 : count;
        count -= goodsList[index].number;
    };
    fill(preferredIndex);
    for (int i = storeBegin(); i <= storeEnd() && count > 0; ++i) fill(i);
    return requested - count;
}

bool GoodsManager::normalizeEquipmentStacks(bool* changed)
{
    if (changed) *changed = false;
    bool complete = true;
    for (int i = equipBegin(); i <= equipEnd() && i < listLength(); ++i)
    {
        auto& equipped = goodsList[i];
        if (equipped.iniFile.empty() || equipped.number <= 1) continue;
        const int moved = returnToBag(equipped, equipped.number - 1);
        equipped.number -= moved;
        if (changed && moved > 0) *changed = true;
        complete = complete && equipped.number == 1;
    }
    return complete;
}

bool GoodsManager::normalizeEquipmentRecord(INIReader& ini)
{
    GoodsManager record;
    for (int i = 0; i < record.listLength(); ++i)
    {
        const std::string section = std::to_string(i + 1);
        record.goodsList[i].iniFile = ini.Get(section, "IniFile", "");
        record.goodsList[i].number = static_cast<int>(ini.GetInteger(section, "Number", 0));
        if (record.goodsList[i].number <= 0) record.goodsList[i].clear();
    }
    const bool complete = record.normalizeEquipmentStacks();
    for (int i = 0; i < record.listLength(); ++i)
    {
        const auto& item = record.goodsList[i];
        if (item.iniFile.empty()) continue;
        const std::string section = std::to_string(i + 1);
        ini.Set(section, "IniFile", item.iniFile);
        ini.SetInteger(section, "Number", item.number);
    }
    return complete;
}

bool GoodsManager::exchange(int index1, int index2, std::string* message)
{
    auto reject = [&](const std::string& reason)
    {
        if (message) *message = reason;
        else if (gm) gm->showMessage(reason);
        return false;
    };
    if (message) message->clear();
    if (index1 < 0 || index1 >= listLength() || index2 < 0 || index2 >= listLength())
        return reject(u8"物品位置无效");
    if (index1 == index2) return true;
    if (isEquipIndex(index1) != isEquipIndex(index2))
    {
        const int equipmentIndex = isEquipIndex(index1) ? index1 : index2;
        const int bagIndex = isEquipIndex(index1) ? index2 : index1;
        if (!isStoreIndex(bagIndex) && !isBottomIndex(bagIndex)) return reject(u8"包裹位置无效");
        if (goodsListExists(bagIndex))
        {
            if (equalsGoodsFileName(goodsList[bagIndex].iniFile, goodsList[equipmentIndex].iniFile))
            {
                if (index1 != equipmentIndex || isItemPerSlotListType()) return true;
                if (goodsList[equipmentIndex].number > INT_MAX - goodsList[bagIndex].number)
                    return reject(u8"该物品数量已达到上限");
                goodsList[bagIndex].number += goodsList[equipmentIndex].number;
                goodsList[equipmentIndex].clear();
                refreshEquipmentEffects();
                updateMenu();
                return true;
            }
            std::string reason;
            if (!canEquipGoodsAt(bagIndex, equipSlot(equipmentIndex), gm->player, &reason)) return reject(reason);
        }
        GoodsManager changed;
        changed.goodsList = goodsList;
        const GoodsInfo previous = changed.goodsList[equipmentIndex];
        changed.goodsList[equipmentIndex] = changed.goodsList[bagIndex];
        if (changed.goodsListExists(bagIndex))
        {
            changed.goodsList[equipmentIndex].number = 1;
            if (--changed.goodsList[bagIndex].number == 0) changed.goodsList[bagIndex].clear();
        }
        if (!previous.iniFile.empty() && previous.number > 0
            && changed.returnToBag(previous, previous.number, bagIndex) != previous.number)
            return reject(u8"包裹没有足够空间放回原装备");
        std::move(changed.goodsList.begin(), changed.goodsList.end(), goodsList.begin());
    }
    else
    {
        std::swap(goodsList[index1], goodsList[index2]);
    }
    refreshEquipmentEffects();
    updateMenu();
    return true;
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
			if (!isEquipIndex(static_cast<int>(i)) && equalsGoodsFileName(goodsList[i].iniFile, itemName))
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
		}
	}
	if (cleared)
	{
		refreshEquipmentEffects();
		updateMenu();
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
		if (count > 0 && deletedCount >= count)
		{
			break;
		}
	}
	refreshEquipmentEffects();
	updateMenu();
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
		gm->player->setMoney(static_cast<int64_t>(gm->player->money) - totalCost);
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
		std::unordered_set<const NPC*> treated{ gm->player.get() };
		auto shareDrug = [&](const std::shared_ptr<NPC>& recipient, bool includePermanentAttributes)
		{
			if (recipient == nullptr || recipient->life <= 0 || recipient->isDying() || recipient->isHiding()
				|| !recipient->isVisibleForRuntime() || !treated.insert(recipient.get()).second) return;
			applyDrugEffect(recipient, goods, includePermanentAttributes);
		};
		if (goods->fighterFriendHasDrugEffect > 0)
		{
			auto friendFighters = gm->npcManager->findFriendFighters();
			for (auto& friendFighter : friendFighters)
			{
				shareDrug(friendFighter, true);
			}
		}
		else if (goods->followPartnerHasDrugEffect > 0)
		{
			auto partners = gm->partnerManager.findPartnersFromNPCManager();
			for (auto& partner : partners)
			{
				shareDrug(partner, true);
			}
		}
		if (gm->global.feature.restorePartnerAttributes)
		{
			for (const auto& partner : gm->partnerManager.findPartnersFromNPCManager())
			{
				if (partner != nullptr && partner->isLivingPartner()) shareDrug(partner, false);
			}
		}
		playDrugUseSound();
		goodsList[itemIndex].number--;
		if (goodsList[itemIndex].number <= 0)
		{
			goodsList[itemIndex].clear();
		}
		normalizeEquipmentStacks();
		updateMenu();
		return true;
	}
	else if (goodsList[itemIndex].goods->kind == gkEquipment)
	{
		if (goodsList[itemIndex].goods->noNeedToEquip > 0)
		{
			refreshEquipmentEffects();
			return true;
		}
		int partIndex = NPC::getEquipmentPartIndex(goodsList[itemIndex].goods->part);
		if (partIndex >= 0)
		{
			const int equipmentIndex = equipIndex(partIndex);
			if (!exchange(itemIndex, equipmentIndex)) return false;
			updateMenu(equipmentIndex);
			gm->player->limitAttribute();
			updateMenu(itemIndex);
			return true;
		}
	}
	return false;
}

void GoodsManager::refreshEquipmentEffects(bool adjustCurrentValues)
{
	bool equipmentMoved = false;
	normalizeEquipmentStacks(&equipmentMoved);
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
	if (equipmentMoved) updateMenu();
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
    GoodsManager changed;
    changed.goodsList = goodsList;
    const GoodsInfo item = goodsList[equipmentIndex];
    if (changed.returnToBag(item, item.number) != item.number)
    {
        if (message) *message = u8"包裹没有足够空间";
        return false;
    }
    changed.goodsList[equipmentIndex].clear();
    std::move(changed.goodsList.begin(), changed.goodsList.end(), goodsList.begin());
    refreshEquipmentEffects();
    updateMenu();
    if (message) message->clear();
    return true;
}

void GoodsManager::updateMenu(int idx)
{
	if (gm == nullptr || gm->menu == nullptr)
	{
		return;
	}
	if (isStoreIndex(idx))
	{
		if (gm->menu->goodsMenu != nullptr)
		{
			gm->menu->goodsMenu->updateGoods();
		}
	}
	else if (isBottomIndex(idx))
	{
		if (gm->menu->bottomMenu != nullptr)
		{
			gm->menu->bottomMenu->updateGoodsItem();
		}
	}
	else
	{
		if (gm->menu->equipMenu != nullptr)
		{
			gm->menu->equipMenu->updateGoods();
		}
	}
}

void GoodsManager::updateMenu()
{
	if (gm == nullptr || gm->menu == nullptr)
	{
		return;
	}
	if (gm->menu->goodsMenu != nullptr)
	{
		gm->menu->goodsMenu->updateGoods();
	}
	if (gm->menu->bottomMenu != nullptr)
	{
		gm->menu->bottomMenu->updateGoodsItem();
	}
	if (gm->menu->equipMenu != nullptr)
	{
		gm->menu->equipMenu->updateGoods();
	}
}

bool GoodsManager::goodsListExists(int index)
{
	if (index >= 0 && index < listLength())
	{
		return !goodsList[index].iniFile.empty() && goodsList[index].goods != nullptr && goodsList[index].number > 0;
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
