#include "Player.h"
#include "../../Engine/Engine.h"
#include "NPCPersistence.h"
#include "PlayerMovementIntent.h"
#include "MagicHitRate.h"
#include "Map.h"
#include "NPCManager.h"
#include "ColorStyle.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"
#include "../../Resource/ResourceManager.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cctype>
#include <cmath>

#define GOODS_LOW_MSG "物品不足!"

#define THEW_LOW_MSG "体力不足!"
#define MANA_LOW_MSG "内力不足!"
#define LIFE_LOW_MSG "生命不足!"
#define RAGE_LOW_MSG "怒气不足!"

namespace
{
constexpr UTime RunThewLowMessageCooldownMilliseconds = 3500;

int addRepeatedSaturated(int current, int value, int count)
{
	if (count <= 0)
	{
		return current;
	}
	const int64_t total = static_cast<int64_t>(current) +
		static_cast<int64_t>(value) * count;
	if (total > INT_MAX)
	{
		return INT_MAX;
	}
	if (total < INT_MIN)
	{
		return INT_MIN;
	}
	return static_cast<int>(total);
}

std::string normalizeEquipmentPart(std::string part)
{
	std::transform(part.begin(), part.end(), part.begin(),
		[](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return part;
}

bool hasExpectedNPCScript(const std::shared_ptr<NPC>& npc, bool useRightScript)
{
	return npc != nullptr && (useRightScript
		? !npc->scriptFileRight.empty()
		: !npc->scriptFile.empty());
}

bool hasExpectedObjectScript(const std::shared_ptr<Object>& object, bool useRightScript)
{
	return object != nullptr && (useRightScript
		? !object->scriptFileRight.empty()
		: !object->scriptFile.empty());
}

void replaceAll(
	std::string& text,
	const std::string& placeholder,
	const std::string& value)
{
	if (placeholder.empty())
	{
		return;
	}
	std::size_t position = 0;
	while ((position = text.find(placeholder, position)) !=
		std::string::npos)
	{
		text.replace(position, placeholder.size(), value);
		position += value.size();
	}
}

std::string formatLevelUpMessage(
	const std::string& messageTemplate,
	const std::string& playerName,
	int playerLevel)
{
	std::string message = messageTemplate;
	replaceAll(message, "{name}", playerName);
	replaceAll(message, "{level}", std::to_string(playerLevel));
	return message;
}
}

Player::Player()
{
	name = "player";
	coverMouse = false;
	setPriority(epPlayer);
	visible = false;
	needEvents = false;
	kind = nkPlayer;
	recoveryAccumulator = 0.0f;
	equipmentManaRestoreTime = 0;
	magicRestoreTime = 0;
}

Player::~Player()
{
	freeResource();
}

void Player::calInfo()
{
	info.attack = attack;
	info.attack2 = attack2;
	info.attack3 = attack3;
	info.defend = defend;
	info.defend2 = defend2;
	info.defend3 = defend3;
	info.evade = evade;
	info.lifeMax = lifeMax;
	info.thewMax = thewMax;
	info.manaMax = manaMax;
	equipmentIgnoresRunThewCost = false;
	equipmentAttackAdditionalEffect = maeNone;
	equipmentMagicReplacements.clear();
	resetEquipmentMagicEffectBonuses();
	bool hadEquipmentRestoresMana = equipmentRestoresMana;
	equipmentRestoresMana = false;
	magicAddLifeRestorePercent = 0;
	magicAddThewRestorePercent = 0;
	magicAddManaRestorePercent = 0;
	std::map<std::string, int> activeEquipmentMagicIniWhenUseCounts;
	auto applyEquipmentGoods = [&](std::shared_ptr<Goods> goods, const std::string& sourceFile, int count)
	{
		if (goods == nullptr || goods->kind != gkEquipment || count <= 0)
		{
			return;
		}
		info.attack = addRepeatedSaturated(
			info.attack, goods->attack, count);
		info.attack2 = addRepeatedSaturated(
			info.attack2, goods->attack2, count);
		info.attack3 = addRepeatedSaturated(
			info.attack3, goods->attack3, count);
		info.defend = addRepeatedSaturated(
			info.defend, goods->defend, count);
		info.defend2 = addRepeatedSaturated(
			info.defend2, goods->defend2, count);
		info.defend3 = addRepeatedSaturated(
			info.defend3, goods->defend3, count);
		info.evade = addRepeatedSaturated(
			info.evade, goods->evade, count);
		info.lifeMax = addRepeatedSaturated(
			info.lifeMax, goods->lifeMax, count);
		info.thewMax = addRepeatedSaturated(
			info.thewMax, goods->thewMax, count);
		info.manaMax = addRepeatedSaturated(
			info.manaMax, goods->manaMax, count);
		addEquipmentRuntimeEffect(*goods, sourceFile, count);
		std::string part = normalizeEquipmentPart(goods->part);
		if (goods->effectType == 1 && part == "foot")
		{
			equipmentIgnoresRunThewCost = true;
		}
		else if (goods->effectType == 1 && part == "neck")
		{
			equipmentRestoresMana = true;
		}
		else if (part == "hand")
		{
			if (goods->effectType == 1)
			{
				equipmentAttackAdditionalEffect = maeFrozen;
			}
			else if (goods->effectType == 2)
			{
				equipmentAttackAdditionalEffect = maePoison;
			}
			else if (goods->effectType == 3)
			{
				equipmentAttackAdditionalEffect = maePetrified;
			}
		}
		if (!goods->replaceMagic.empty() &&
			!goods->useReplaceMagic.empty())
		{
			equipmentMagicReplacements[goods->replaceMagic] =
				goods->useReplaceMagic;
		}
		if (!goods->magicIniWhenUse.empty())
		{
			int& activeCount =
				activeEquipmentMagicIniWhenUseCounts[
					goods->magicIniWhenUse];
			activeCount = addRepeatedSaturated(
				activeCount, 1, count);
		}
	};
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		int listIndex = gm->goodsManager.equipIndex(static_cast<int>(i));
		if (gm->goodsManager.goodsListExists(listIndex))
		{
			applyEquipmentGoods(gm->goodsManager.goodsList[listIndex].goods, gm->goodsManager.goodsList[listIndex].iniFile, 1);
		}
	}
	for (int listIndex = 0; listIndex < gm->goodsManager.listLength(); listIndex++)
	{
		if (gm->goodsManager.isEquipIndex(listIndex) || !gm->goodsManager.goodsListExists(listIndex))
		{
			continue;
		}
		auto goods = gm->goodsManager.goodsList[listIndex].goods;
		if (goods != nullptr && goods->kind == gkEquipment && goods->noNeedToEquip > 0)
		{
			applyEquipmentGoods(goods, gm->goodsManager.goodsList[listIndex].iniFile, gm->goodsManager.goodsList[listIndex].number);
		}
	}
	syncEquipmentGrantedMagic(activeEquipmentMagicIniWhenUseCounts);
	for (const auto& magicInfo : gm->magicManager.magicList)
	{
		if (magicInfo.magic == nullptr || magicInfo.iniFile.empty() || magicInfo.level < 1)
		{
			continue;
		}
		int magicLevel = std::clamp(magicInfo.level, 1, MAGIC_MAX_LEVEL);
		const auto& levelInfo = magicInfo.magic->level[magicLevel];
		info.attack = addRepeatedSaturated(
			info.attack, levelInfo.attack, 1);
		info.attack2 = addRepeatedSaturated(
			info.attack2, levelInfo.attack2, 1);
		info.attack3 = addRepeatedSaturated(
			info.attack3, levelInfo.attack3, 1);
		info.defend = addRepeatedSaturated(
			info.defend, levelInfo.defend, 1);
		info.defend2 = addRepeatedSaturated(
			info.defend2, levelInfo.defend2, 1);
		info.defend3 = addRepeatedSaturated(
			info.defend3, levelInfo.defend3, 1);
		info.evade = addRepeatedSaturated(
			info.evade, levelInfo.evade, 1);
		info.lifeMax = addRepeatedSaturated(
			info.lifeMax, levelInfo.lifeMax, 1);
		info.thewMax = addRepeatedSaturated(
			info.thewMax, levelInfo.thewMax, 1);
		info.manaMax = addRepeatedSaturated(
			info.manaMax, levelInfo.manaMax, 1);
		magicAddLifeRestorePercent = addRepeatedSaturated(
			magicAddLifeRestorePercent,
			levelInfo.addLifeRestorePercent,
			1);
		magicAddThewRestorePercent = addRepeatedSaturated(
			magicAddThewRestorePercent,
			levelInfo.addThewRestorePercent,
			1);
		magicAddManaRestorePercent = addRepeatedSaturated(
			magicAddManaRestorePercent,
			levelInfo.addManaRestorePercent,
			1);
		addMagicPassiveRuntimeEffect(*magicInfo.magic, magicInfo.iniFile);
	}
	attackAdditionalEffect = equipmentAttackAdditionalEffect;
	rebuildAttackOptions();
	if (equipmentRestoresMana && !hadEquipmentRestoresMana)
	{
		equipmentManaRestoreTime = getTime();
	}
}

void Player::resetEquipmentGrantedMagicSync()
{
	equipmentMagicIniWhenUseCounts.clear();
	equipmentMagicIniWhenUseCountsInitialized = false;
}

void Player::updateLevel()
{
	if (level >= (int)levelList.size())
	{
		return;
	}
	if (level < 1)
	{
		setLevel(1);
	}
	else
	{
		attack += levelList[level].attack - levelList[level - 1].attack;
		attack2 += levelList[level].attack2 - levelList[level - 1].attack2;
		attack3 += levelList[level].attack3 - levelList[level - 1].attack3;
		defend += levelList[level].defend - levelList[level - 1].defend;
		defend2 += levelList[level].defend2 - levelList[level - 1].defend2;
		defend3 += levelList[level].defend3 - levelList[level - 1].defend3;
		evade += levelList[level].evade - levelList[level - 1].evade;
		lifeMax += levelList[level].lifeMax - levelList[level - 1].lifeMax;
		thewMax += levelList[level].thewMax - levelList[level - 1].thewMax;
		manaMax += levelList[level].manaMax - levelList[level - 1].manaMax;
		levelUpExp = levelList[level].levelUpExp;
		level++;
		calInfo();
		fullLife();
		fullThew();
		fullMana();
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::setLevel(int lvl)
{
	if (levelList.empty())
	{
		level = lvl < 1 ? 1 : lvl;
		return;
	}
	if (lvl < 1)
	{
		lvl = 1;
	}
	else if (lvl > (int)levelList.size())
	{
		lvl = (int)levelList.size();
	}
	level = lvl;
	attack = levelList[level - 1].attack;
	attack2 = levelList[level - 1].attack2;
	attack3 = levelList[level - 1].attack3;
	defend = levelList[level - 1].defend;
	defend2 = levelList[level - 1].defend2;
	defend3 = levelList[level - 1].defend3;
	evade = levelList[level - 1].evade;
	lifeMax = levelList[level - 1].lifeMax;
	thewMax = levelList[level - 1].thewMax;
	manaMax = levelList[level - 1].manaMax;
	levelUpExp = levelList[level - 1].levelUpExp;
	calInfo();
	fullLife();
	fullThew();
	fullMana();
	gm->menu->stateMenu->updateLabel();
}

void Player::fullLife()
{
	const bool deathFlowActive = isDying()
		|| (isHiding() && (life <= 0 || (result & erLifeExhaust) != 0));
	if (deathFlowActive)
	{
		reviveFromDeath();
	}
	else
	{
		life = info.lifeMax;
	}
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::fullThew()
{
	thew = info.thewMax;
	gm->menu->stateMenu->updateLabel();
}

void Player::fullMana()
{
	mana = info.manaMax;
	gm->menu->stateMenu->updateLabel();
}

void Player::addLifeMax(int value)
{
	lifeMax = std::max(1, lifeMax + value);
	calInfo();
	limitAttribute();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::addThewMax(int value)
{
	thewMax = std::max(1, thewMax + value);
	calInfo();
	limitAttribute();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::addManaMax(int value)
{
	manaMax = std::max(1, manaMax + value);
	calInfo();
	limitAttribute();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::addLife(int value)
{
	if (value < 0 && (invincible > 0
		|| (gm != nullptr && gm->shouldProtectPlayerFromCheatDamage())))
	{
		return;
	}
    if (value < 0)
    {
        value = (int)round(value * DAMAGE_RATE);
    }
	life += value;
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
	if (life <= 0)
	{
		beginDie();
	}
}

void Player::addLifeWithoutDeath(int value)
{
	life = std::max(0, life + value);
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
}

void Player::addThew(int value)
{
	thew += value;
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
}

void Player::addMana(int value)
{
	mana += value;
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
}

void Player::addAttack(int value, int type)
{
	if (type == 1)
	{
		attack += value;
	}
	else if (type == 2)
	{
		attack2 += value;
	}
	else if (type == 3)
	{
		attack3 += value;
	}
	else
	{
		return;
	}
	calInfo();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::addDefend(int value, int type)
{
	int* target = nullptr;
	if (type == 1)
	{
		target = &defend;
	}
	else if (type == 2)
	{
		target = &defend2;
	}
	else if (type == 3)
	{
		target = &defend3;
	}
	if (target == nullptr)
	{
		return;
	}
	*target = std::max(0, *target + value);
	calInfo();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::addEvade(int value)
{
	evade = std::max(0, evade + value);
	calInfo();
	if (gm != nullptr && gm->menu != nullptr && gm->menu->stateMenu != nullptr)
	{
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::addMoney(int value)
{
	const int64_t updatedMoney = static_cast<int64_t>(money) + value;
	money = static_cast<int>(std::clamp<int64_t>(updatedMoney, INT_MIN, INT_MAX));
	gm->menu->goodsMenu->updateMoney();
}

void Player::setRage(int value)
{
	if (rageMax < 0)
	{
		rageMax = 0;
	}
	rage = std::clamp(value, 0, rageMax);
}

void Player::addRage(int value)
{
	if (gm == nullptr || !gm->global.feature.rageSystem)
	{
		return;
	}
	long long nextRage = static_cast<long long>(rage) + static_cast<long long>(value);
	if (nextRage < 0)
	{
		nextRage = 0;
	}
	if (nextRage > rageMax)
	{
		nextRage = rageMax;
	}
	rage = static_cast<int>(nextRage);
}

float Player::getCriticalChancePercent() const
{
	if (gm == nullptr || !gm->global.feature.rageSystem)
	{
		return 0.0f;
	}

	float chance = static_cast<float>(std::max(0, level)) * 0.1f;
	if (auto effect = attributeChangeEffect.lock())
	{
		if (!effect->vanishing
			&& (effect->doing == ekFlying || effect->doing == ekExploding || effect->doing == ekHiding))
		{
			int effectLevel = std::clamp(effect->level, 1, MAGIC_MAX_LEVEL);
			chance += static_cast<float>(effect->magic.level[effectLevel].critChanceAddValue);
		}
	}
	return std::clamp(chance, 0.0f, 100.0f);
}

int Player::getCriticalDamagePercent() const
{
	if (gm == nullptr || !gm->global.feature.rageSystem)
	{
		return 0;
	}

	int percent = std::max(0, level);
	if (auto effect = attributeChangeEffect.lock())
	{
		if (!effect->vanishing
			&& (effect->doing == ekFlying || effect->doing == ekExploding || effect->doing == ekHiding))
		{
			int effectLevel = std::clamp(effect->level, 1, MAGIC_MAX_LEVEL);
			percent += effect->magic.level[effectLevel].critDamageAddPercent;
		}
	}
	return std::max(0, percent);
}

int Player::applyCriticalDamage(int damage, int roll, bool* wasCritical) const
{
	if (wasCritical != nullptr)
	{
		*wasCritical = false;
	}
	if (damage <= 0 || roll < 0 || roll > 100 || static_cast<float>(roll) > getCriticalChancePercent())
	{
		return damage;
	}
	if (wasCritical != nullptr)
	{
		*wasCritical = true;
	}
	return static_cast<int>(std::round(static_cast<float>(damage)
		* (1.0f + static_cast<float>(getCriticalDamagePercent()) / 100.0f)));
}

void Player::recordActualDamageForRage(int damage)
{
	if (damage > 0)
	{
		addRage(1);
	}
}

std::shared_ptr<Magic> Player::resolveMagicReplacement(std::shared_ptr<Magic> magic)
{
	if (magic == nullptr)
	{
		return magic;
	}
	auto iter = equipmentMagicReplacements.find(magic->iniName);
	if (iter == equipmentMagicReplacements.end() || iter->second.empty())
	{
		return magic;
	}
	auto replacement = gm->magicManager.loadAttackMagic(iter->second);
	if (replacement == nullptr || !replacement->loadSucceeded)
	{
		return magic;
	}
	return replacement;
}

bool Player::tryConsumeMagicCost(std::shared_ptr<Magic> magic, int level, bool showMessage)
{
	if (magic == nullptr || level < 1 || level > MAGIC_MAX_LEVEL)
	{
		return false;
	}
	if (!canUseMagicByState(magic, showMessage))
	{
		return false;
	}
	if (!canUseMana)
	{
		if (showMessage)
		{
			gm->showMessage(MANA_LOW_MSG);
		}
		return false;
	}

	auto& levelInfo = magic->level[level];
	if (mana < levelInfo.manaCost)
	{
		if (showMessage)
		{
			gm->showMessage(MANA_LOW_MSG);
		}
		return false;
	}
	if (thew < levelInfo.thewCost)
	{
		if (showMessage)
		{
			gm->showMessage(THEW_LOW_MSG);
		}
		return false;
	}
	if (gm != nullptr && gm->global.feature.rageSystem && rage < levelInfo.rageCost)
	{
		if (showMessage)
		{
			gm->showMessage(RAGE_LOW_MSG);
		}
		return false;
	}
	if (life < levelInfo.lifeCost)
	{
		if (showMessage)
		{
			gm->showMessage(LIFE_LOW_MSG);
		}
		return false;
	}

	const std::string& goodsName = magic->goodsName;
	if (!goodsName.empty() && gm->goodsManager.getItemNum(goodsName) <= 0)
	{
		if (showMessage)
		{
			gm->showMessage(GOODS_LOW_MSG);
		}
		return false;
	}

	mana -= levelInfo.manaCost;
	thew -= levelInfo.thewCost;
	life -= levelInfo.lifeCost;
	limitAttribute();
	if (!goodsName.empty())
	{
		gm->goodsManager.setItemNum(goodsName, gm->goodsManager.getItemNum(goodsName) - 1);
	}
	return true;
}

void Player::syncEquipmentGrantedMagic(const std::map<std::string, int>& activeCounts)
{
	if (gm == nullptr)
	{
		return;
	}

	auto showMagicAvailable = [](MagicInfo* info)
	{
		if (gm != nullptr && info != nullptr && info->magic != nullptr)
		{
			gm->showMessage(convert::formatString("武功%s已可使用", info->magic->name.c_str()));
		}
	};
	auto showMagicUnavailable = [](MagicInfo* info)
	{
		if (gm != nullptr && info != nullptr && info->magic != nullptr)
		{
			gm->showMessage(convert::formatString("武功%s已不可使用", info->magic->name.c_str()));
		}
	};

	if (!equipmentMagicIniWhenUseCountsInitialized)
	{
		for (const auto& item : activeCounts)
		{
			const std::string& magicName = item.first;
			int activeCount = std::max(0, item.second);
			if (magicName.empty() || activeCount <= 0)
			{
				continue;
			}

			MagicInfo* visibleInfo = gm->magicManager.findPrimaryMagic(magicName);
			if (visibleInfo != nullptr)
			{
				if (visibleInfo->hideCount < activeCount)
				{
					visibleInfo->hideCount = activeCount;
				}
				continue;
			}

			bool wasHidden = gm->magicManager.isMagicHidden(magicName);
			MagicInfo* info = wasHidden
				? gm->magicManager.setMagicHidden(magicName, false, false, true)
				: gm->magicManager.addEquipmentMagic(magicName, true, false);
			if (wasHidden)
			{
				showMagicAvailable(info);
			}
			if (info != nullptr)
			{
				info->hideCount = activeCount;
			}
		}
		equipmentMagicIniWhenUseCounts = activeCounts;
		equipmentMagicIniWhenUseCountsInitialized = true;
		return;
	}

	for (const auto& item : equipmentMagicIniWhenUseCounts)
	{
		const std::string& magicName = item.first;
		int previousCount = std::max(0, item.second);
		auto activeIter = activeCounts.find(magicName);
		int activeCount = activeIter == activeCounts.end() ? 0 : std::max(0, activeIter->second);
		int removalCount = previousCount - activeCount;
		if (removalCount <= 0)
		{
			continue;
		}

		MagicInfo* info = gm->magicManager.findPrimaryMagic(magicName);
		if (info == nullptr)
		{
			continue;
		}
		if (info->hideCount > removalCount)
		{
			info->hideCount -= removalCount;
			gm->magicManager.updateMenu();
			continue;
		}

		info->hideCount = 1;
		info = gm->magicManager.setMagicHidden(magicName, true, false, true);
		if (info != nullptr && info->hideCount == 0)
		{
			showMagicUnavailable(info);
		}
	}

	for (const auto& item : activeCounts)
	{
		const std::string& magicName = item.first;
		int activeCount = std::max(0, item.second);
		if (magicName.empty() || activeCount <= 0)
		{
			continue;
		}
		auto previousIter = equipmentMagicIniWhenUseCounts.find(magicName);
		int previousCount = previousIter == equipmentMagicIniWhenUseCounts.end() ? 0 : std::max(0, previousIter->second);
		int additionCount = activeCount - previousCount;
		if (additionCount <= 0)
		{
			continue;
		}

		bool wasHidden = gm->magicManager.isMagicHidden(magicName);
		MagicInfo* info = gm->magicManager.findPrimaryMagic(magicName);
		if (info != nullptr)
		{
			info->hideCount = addRepeatedSaturated(
				info->hideCount, 1, additionCount);
			gm->magicManager.updateMenu();
			continue;
		}

		info = wasHidden
			? gm->magicManager.setMagicHidden(magicName, false, false, true)
			: gm->magicManager.addEquipmentMagic(magicName, true, false);
		if (wasHidden)
		{
			showMagicAvailable(info);
		}
		if (info != nullptr && additionCount > 1)
		{
			info->hideCount = addRepeatedSaturated(
				info->hideCount, 1, additionCount - 1);
		}
	}

	equipmentMagicIniWhenUseCounts = activeCounts;
}

void Player::resetRecoveryTime(UTime time)
{
	if (time == 0)
	{
		time = getTime();
	}
	recoveryTime = time;
	equipmentManaRestoreTime = time;
	magicRestoreTime = time;
}

void Player::recoverWhenStandingOrWalking()
{
	UTime now = getTime();
	if (now - recoveryTime > THEW_RECOVERY_INTERVAL)
	{
		recoveryTime += THEW_RECOVERY_INTERVAL;
		float recoveryAmount = THEW_RECOVERY_RATE * info.thewMax;
		recoveryAccumulator += recoveryAmount;

		if (recoveryAccumulator >= 1.0f)
		{
			int addAmount = (int)recoveryAccumulator;
			addThew(addAmount);
			recoveryAccumulator -= addAmount;
		}
	}
	if (magicAddLifeRestorePercent == 0 && magicAddThewRestorePercent == 0 && magicAddManaRestorePercent == 0)
	{
		magicRestoreTime = now;
	}
	else if (now - magicRestoreTime > MAGIC_RESTORE_INTERVAL)
	{
		magicRestoreTime += MAGIC_RESTORE_INTERVAL;
		int addLifeAmount = static_cast<int>(info.lifeMax * (magicAddLifeRestorePercent / 1000.0f));
		int addThewAmount = static_cast<int>(info.thewMax * (magicAddThewRestorePercent / 1000.0f));
		int addManaAmount = static_cast<int>(info.manaMax * (magicAddManaRestorePercent / 1000.0f));
		if (addLifeAmount > 0)
		{
			addLife(addLifeAmount);
		}
		if (addThewAmount > 0)
		{
			addThew(addThewAmount);
		}
		if (addManaAmount > 0)
		{
			addMana(addManaAmount);
		}
	}
	if (!equipmentRestoresMana)
	{
		equipmentManaRestoreTime = now;
		return;
	}
	if (now - equipmentManaRestoreTime > EQUIPMENT_MANA_RESTORE_INTERVAL)
	{
		equipmentManaRestoreTime += EQUIPMENT_MANA_RESTORE_INTERVAL;
		int addAmount = static_cast<int>(info.manaMax * EQUIPMENT_MANA_RESTORE_RATE);
		if (addAmount > 0)
		{
			addMana(addAmount);
		}
	}
}

void Player::updateAction(UTime frameTime)
{
	bool wasFighting = fightState.get();
	fightState.update(frameTime);
	if (wasFighting && !fightState.get())
	{
		clearCombatTargetMemory();
	}
	
	actionManager->update(frameTime);
}

void Player::onUpdate()
{
	auto ft = getFrameTime();
	gm->magicManager.updateColdTimes(ft);
	gm->goodsManager.updateColdTimes(ft);
	const unsigned int pendingResult = getResult();
	if (pendingResult & erRunDeathScript)
	{
		// GameManager dispatches player death after every gameplay child has
		// updated, before NPC death events and queued scripts from the same frame.
		result |= erRunDeathScript;
		return;
	}
	if (shieldLife > 0)
	{
		if (getTime() - shieldBeginTime >= shieldLastTime)
		{
			shieldLife = 0;
			if (auto shield = shieldEffect.lock())
			{
				if (!shield->vanishing)
				{
					shield->vanishing = true;
					shield->beginExplode(shield->position);
				}
			}
			shieldEffect.reset();
		}
		else
		{
			shieldBeginTime += ft;
			if (shieldLastTime > ft) { shieldLastTime -= ft; } else { shieldLastTime = 0; }
		}
	}
	else if (auto shield = shieldEffect.lock())
	{
		shieldLife = 0;
		if (!shield->vanishing)
		{
			shield->vanishing = true;
			shield->beginExplode(shield->position);
		}
		shieldEffect.reset();
	}

	if (poisoned && !isDying())
	{
		if (poisonedLastTime >= ft)
		{
			poisonedLastTime -= ft;
			poisonedDamageTimer += ft;
			if (poisonedDamageTimer >= 250)
			{
				poisonedDamageTimer -= 250;
				addLife(-10);
				if (life <= 0)
				{
					handleDeath();
					clearPoisonedState();
					return;
				}
			}
		}
		else
		{
			clearPoisonedState();
		}
	}

	if (updateScriptSpecialActionOverlayForFrame(ft))
	{
		return;
	}

	bool stateActionDone = false;
	bool movementLockedByMagic = disableMoveMilliseconds > 0 && !isDying() && (isWalking() || isRunning() || isJumping());
	updateActionLockTimers(ft);

	if (petrified && !isDying())
	{
		clearFrozenState();
		clearImmobilizedState();
		if (petrifiedLastTime >= ft)
		{
			petrifiedLastTime -= ft;
			setTime(getTime() - ft);
			stateActionDone = true;
		}
		else
		{
			auto lastT = ft - petrifiedLastTime;
			setTime(getTime() - petrifiedLastTime);
			clearPetrifiedState();
			updateAction(lastT);
			stateActionDone = true;
		}
	}
	else if (immobilized && !isDying())
	{
		if (immobilizedLastTime >= ft)
		{
			immobilizedLastTime -= ft;
			setTime(getTime() - ft);
			stateActionDone = true;
		}
		else
		{
			auto lastT = ft - immobilizedLastTime;
			setTime(getTime() - immobilizedLastTime);
			clearImmobilizedState();
			updateAction(lastT);
			stateActionDone = true;
		}
	}
	else if (movementLockedByMagic)
	{
		setTime(getTime() - ft);
		stateActionDone = true;
	}

	if (!stateActionDone)
	{
		if (frozen && !isDying())
		{
			if (frozenLastTime >= ft)
			{
				frozenLastTime -= ft;
				setTime(getTime() - ft / 2);
				ft /= 2;
			}
			else
			{
				auto remainingFreezeTime = frozenLastTime;
				setTime(getTime() - remainingFreezeTime / 2);
				clearFrozenState();
				ft = ft - remainingFreezeTime / 2;
			}
		}
		else
		{
			clearFrozenState();
		}

		updateAction(ft);
	}
	updateEquipmentLifeRestore(ft);

	triggerTouchObjects();
	processControlledNextAction();
	if (!isControllingCharacter() && nextAction == nullptr && isStanding()
		&& nextDest != ndNone && nextDestStrictWorldInteraction)
	{
		if (handleQueuedInteractionAtCurrentPosition())
		{
			return;
		}
		if (resumeStrictQueuedInteraction())
		{
			return;
		}
	}

	//站立时处理接下来的人物动作
	if (nextAction != nullptr && isStanding())
	{
		NextAction queuedAction = *nextAction;
		nextAction = nullptr;
		if (isControllingCharacter())
		{
			dispatchControlledAction(queuedAction);
			return;
		}
		if (queuedAction.action == acWalk || queuedAction.action == acAWalk)
		{
			nextDest = queuedAction.destKind;
			nextDestUseRightScript = queuedAction.useRightScript;
			nextDestStrictWorldInteraction = queuedAction.strictWorldInteraction;
			nextDestRequestedRunning = false;
			destGE = queuedAction.destGE;
			beginWalk(queuedAction.dest);
		}
		else if (queuedAction.action == acRun || queuedAction.action == acARun)
		{
			nextDest = queuedAction.destKind;
			nextDestUseRightScript = queuedAction.useRightScript;
			nextDestStrictWorldInteraction = queuedAction.strictWorldInteraction;
			nextDestRequestedRunning = true;
			destGE = queuedAction.destGE;
			beginRun(queuedAction.dest);
		}
		else if (queuedAction.action == acJump || queuedAction.action == acAJump)
		{
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginJump(queuedAction.dest);
		}
		else if (queuedAction.action == acAttack || queuedAction.action == acAttack1 || queuedAction.action == acAttack2 || queuedAction.action == acSpecialAttack)
		{
            if (canFight)
            {
				nextDest = ndNone;
				nextDestUseRightScript = false;
				nextDestStrictWorldInteraction = false;
				nextDestRequestedRunning = false;
				beginAttack(queuedAction.dest, queuedAction.destGE.lock());
                destGE.reset();
            }
		}
		else if (queuedAction.action == acMagic)
		{
			if (canFight)
            {
				nextDest = ndNone;
				nextDestUseRightScript = false;
				nextDestStrictWorldInteraction = false;
				nextDestRequestedRunning = false;
				if (queuedAction.actionParam >= 0 && queuedAction.actionParam < gm->magicManager.bottomCount())
                {
                    int listIndex = gm->magicManager.bottomIndex(queuedAction.actionParam);
                    if (!gm->magicManager.magicListExists(listIndex) || gm->magicManager.magicList[listIndex].level < 1 || gm->magicManager.magicList[listIndex].level > MAGIC_MAX_LEVEL)
                    {
                        magicIndex = -1;
                        magicDest = queuedAction.dest;
                        beginMagic(queuedAction.dest, queuedAction.destGE.lock());
                    }
                    else
                    {
                        magicIndex = queuedAction.actionParam;
                        magicDest = queuedAction.dest;
                        beginMagic(queuedAction.dest, queuedAction.destGE.lock());
                    }
                }
            }
		}
		else if (queuedAction.action == acSit || queuedAction.action == acSitting)
		{
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginSit();
		}
		else if (queuedAction.action == acHurt)
		{
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginHurt(queuedAction.dest);
		}
		else if (queuedAction.action == acDeath)
		{
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginDie();
		}
		else if (queuedAction.action == acSpecial)
		{
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginSpecial();
		}
	}

	updateEventRunState();
}

void Player::freeResource()
{
	endControlCharacter();
	freeNPCAction(&scriptSpecialActionOverlayResource);
	scriptSpecialActionOverlayActive = false;
	scriptSpecialActionOverlaySupersededByAction = false;
	scriptSpecialActionOverlayElapsed = 0;
	scriptSpecialActionOverlayDuration = 0;
	offset = { 0, 0 };
	npcMagic = nullptr;
	npcMagic2 = nullptr;
	magicToUseWhenBeAttacked = nullptr;
	attackOptions.clear();
	attackAdditionalEffect = maeNone;
	disableMoveMilliseconds = 0;
	disableSkillMilliseconds = 0;
	clearMagicRuntimeStates();
	equipmentAttackAdditionalEffect = maeNone;
	magicAddLifeRestorePercent = 0;
	magicAddThewRestorePercent = 0;
	magicAddManaRestorePercent = 0;
	magicRestoreTime = 0;
	equipmentMagicReplacements.clear();
	resetEquipmentGrantedMagicSync();
	resetEquipmentMagicEffectBonuses();
	actionPlan.reset();
	hasLastUsedAttackOption = false;
	if (gm != nullptr)
	{
		gm->magicManager.tryCleanAttackMagic();
	}
	followNPC = "";
	freeNPCRes();
	stepList.resize(0);
	cancelMoveResumeState();
	haveAsyncDest = false;

	shieldEffects.clear();
	shieldEffect.reset();
	shieldLife = 0;
	shieldLastTime = 0;
	shieldBeginTime = 0;
	attributeChangeEffect.reset();
	rage = 0;
	rageMax = 100;

	nextAction = nullptr;
	nextDest = ndNone;
	nextDestUseRightScript = false;
	nextDestStrictWorldInteraction = false;
	nextDestRequestedRunning = false;
	runThewLowMessageShown = false;
	lastRunThewLowMessageTime = 0;
	destGE.reset();
	scriptPositionTrapSuppressed = false;
	scriptTrapMapName.clear();

	clearFrozenState();
	clearPoisonedState();
	clearCombatTargetMemory();
	clearPetrifiedState();
	clearImmobilizedState();
	invincible = 0;

	fightState.set(false);
	resetRecoveryTime();
	recoveryAccumulator = 0.0f;
}

void Player::hurt(std::shared_ptr<Effect> e)
{
	if (nowAction == acDeath || isHiding() || e == nullptr)
	{
		return;
	}
	bool addexp = false;
	if (e->launcherKind == lkSelf)
	{
		addexp = true;
	}
	if (auto caster = std::dynamic_pointer_cast<NPC>(e->user.lock()))
	{
		caster->recordMagicHitForChange(e->magic, e->level);
	}
	if (hasActiveSelfMagic(mskBlockDamage))
	{
		return;
	}
	int damage = calculateEffectDamage(e);
	int effectLevel = e->level;
	if (effectLevel < 1) effectLevel = 1;
	if (effectLevel > MAGIC_MAX_LEVEL) effectLevel = MAGIC_MAX_LEVEL;
	applyEffectRuntimeStates(*e);
	bool handledSpecialEffect = applyPreDamageMagicStatus(*e, effectLevel);
	const int targetEvade = getEvade();

	const int hitRollMaximum = calculatePlayerMagicDamageHitRollMaximum(
		e->evade, targetEvade);
	if (isMagicDamageHitAgainstPlayer(
		e->evade, targetEvade, engine->getRand(hitRollMaximum)))
	{
		if (addexp)
		{
			gm->magicManager.addHitExp(e, level);
		}
		bool wasCritical = false;
		damage = applyCriticalDamageFromEffect(e, damage, &wasCritical);
		applyEffectManaDamage(e);
		if (invincible > 0
			|| (gm != nullptr && gm->shouldProtectPlayerFromCheatDamage()))
		{
			damage = 0;
		}
		if (damage > 0 && shieldLife > 0)
		{
			if (damage > shieldLife)
			{
				damage -= shieldLife;
				shieldLife = 0;
				if (auto shield = shieldEffect.lock())
				{
					shield->vanishing = true;
					shield->beginExplode(shield->position);
				}
				shieldEffect.reset();
			}
			else
			{
				shieldLife -= damage;
				damage = 0;
				return;
			}
		}
		if (wasCritical && damage > 0)
		{
			showCriticalDamageTip(damage);
		}

		int restoreDamage = damage > life ? life : damage;
		applyEffectRestore(e, restoreDamage);
		recordActualDamageForRage(damage);
		addLife(-damage);
		triggerMagicWhenBeAttacked(*e);
		applyBounceFromEffect(*e);
		applyBounceFlyFromEffect(*e);
		if (auto userPtr = e->user.lock(); NPCManager::isManagedEffectCaster(userPtr))
		{
			lastCombatTarget = userPtr;
			lastCombatTargetTime = getTime();
			rememberCombatTargetPosition(userPtr);
		}
		if (life <= 0)
		{
			life = 0;
			if (addexp)
			{
				gm->player->addExp(exp);
				gm->magicManager.addKillExp(e, exp);
			}
		}
		else
		{
			int d = getDirection(atan2(e->flyingDirection.x, -e->flyingDirection.y));
			Point fd = gm->map->getSubPoint(position, d);
			if (shouldBeginHurtActionAfterMagicDamage(
				engine->getRand(3), handledSpecialEffect, immobilized, petrified))
			{
				beginHurt(fd);
			}
		}
	}
}

void Player::hurtLife(int damage)
{
	if (hasActiveSelfMagic(mskBlockDamage))
	{
		return;
	}
	if (invincible > 0
		|| (gm != nullptr && gm->shouldProtectPlayerFromCheatDamage()))
	{
		return;
	}
	damage -= defend;
	for (auto it = shieldEffects.begin(); it != shieldEffects.end(); )
	{
		if (auto shield = it->lock())
		{
			if (shield->vanishing || (shield->doing != ekFlying && shield->doing != ekExploding && shield->doing != ekHiding))
			{
				it = shieldEffects.erase(it);
				continue;
			}
			int shieldLevel = shield->level;
			damage -= shield->magic.level[shieldLevel].effect;
		}
		else
		{
			it = shieldEffects.erase(it);
			continue;
		}
		++it;
	}
	if (damage < 0) { damage = 0; }
    if (damage > 0 && shieldLife > 0)
	{
		if (damage > shieldLife)
		{
			damage -= shieldLife;
			shieldLife = 0;
			if (auto shield = shieldEffect.lock())
			{
				shield->vanishing = true;
				shield->beginExplode(shield->position);
			}
			shieldEffect.reset();
		}
		else
		{
			shieldLife -= damage;
			return;
		}
	}
	if (damage > 0)
	{
		recordActualDamageForRage(damage);
		addLife(-damage);
	}
}

void Player::addExp(int aExp)
{
	if (levelUpExp <= 0)
	{
		return;
	}
	exp = addRepeatedSaturated(exp, aExp, 1);
	bool up = false;
	const bool useStrictLevelUpThreshold = usesStrictLevelUpThreshold();
	auto reachedLevelThreshold = [this, useStrictLevelUpThreshold]()
	{
		return useStrictLevelUpThreshold ? exp > levelUpExp : exp >= levelUpExp;
	};
	while (reachedLevelThreshold() && level < (int)levelList.size())
	{
		updateLevel();
		up = true;
	}
	if (up)
	{
		levelUp();
	}		
}

bool Player::addExperienceToNextLevel()
{
	if (levelUpExp <= 0 || level >= static_cast<int>(levelList.size()))
	{
		return false;
	}

	const std::int64_t targetExperience =
		static_cast<std::int64_t>(levelUpExp)
		+ (usesStrictLevelUpThreshold() ? 1 : 0);
	const std::int64_t requiredExperience = std::max<std::int64_t>(
		0, targetExperience - static_cast<std::int64_t>(exp));
	if (targetExperience > INT_MAX || requiredExperience > INT_MAX)
	{
		return false;
	}
	const int previousLevel = level;
	addExp(static_cast<int>(requiredExperience));
	return level > previousLevel;
}

bool Player::usesStrictLevelUpThreshold() const
{
	return gm != nullptr &&
		gm->global.levelUpThresholdMode ==
			LevelUpThresholdMode::GreaterThan;
}

void Player::levelUp()
{
	const ResourceManifest& manifest =
		ResourceManager::instance().getActiveManifest();
	std::string effectFile = sex == 2
		? manifest.levelUpFemaleEffect
		: manifest.levelUpMaleEffect;
	if (effectFile.empty() && !manifest.levelUpRandomEffects.empty())
	{
		const int effectIndex = engine->getRand(
			static_cast<int>(manifest.levelUpRandomEffects.size()));
		effectFile = manifest.levelUpRandomEffects[
			static_cast<std::size_t>(effectIndex)];
	}

	if (!effectFile.empty())
	{
		auto levelUpMagic = std::make_shared<Magic>();
		levelUpMagic->initFromIni(effectFile, false);
		if (!levelUpMagic->loadSucceeded)
		{
			GameLog::write(
				"Player: level-up effect magic not found: %s\n",
				effectFile.c_str());
		}
		else if (levelUpMagic->level[1].moveKind != mmkSelf)
		{
			GameLog::write(
				"Player: level-up effect magic must use MoveKind=13: %s\n",
				effectFile.c_str());
		}
		else
		{
			levelUpMagic->loadRes();
			auto effects = Magic::addSelfEffect(
				levelUpMagic,
				gm->player,
				gm->player->position,
				gm->player->position,
				1,
				0,
				0,
				0,
				0);
			for (const auto& effect : effects)
			{
				if (effect != nullptr)
				{
					effect->playSound(ekFlying);
				}
			}
		}
	}

	const std::string message = formatLevelUpMessage(
		manifest.levelUpMessage, npcName, level);
	if (!message.empty())
	{
		gm->showMessage(message);
	}
}

bool Player::addNextAction(NextAction& act)
{
	if (act.action == acRun || act.action == acWalk || act.action == acARun || act.action == acAWalk)
	{
		if (act.destKind == ndNone
			&& !gm->map->canWalkForActor(act.dest, getActionActor()))
		{
			return false;
		}
		bool canPayRunThewCost = gm->inEvent || ignoresRunThewCost() || thew >= RUN_THEW_COST;
		bool alternateAction = act.action == acAWalk || act.action == acARun;
		bool runRequested = act.action == acRun || act.action == acARun;
		if (runRequested && canRun && !canPayRunThewCost)
		{
			const UTime now = getTime();
			if (!runThewLowMessageShown
				|| now < lastRunThewLowMessageTime
				|| now - lastRunThewLowMessageTime >=
					RunThewLowMessageCooldownMilliseconds)
			{
				gm->showMessage(THEW_LOW_MSG);
				runThewLowMessageShown = true;
				lastRunThewLowMessageTime = now;
			}
		}
		else if (canPayRunThewCost)
		{
			runThewLowMessageShown = false;
		}
		bool useRun = shouldUseRunForPlayerMoveIntent(runRequested, walkIsRun, canRun, canPayRunThewCost);
		act.action = alternateAction
			? (useRun ? acARun : acAWalk)
			: (useRun ? acRun : acWalk);
	}

	// 新动作只替换未完成的严格手柄交互；旧鼠标、触摸交互保持原队列语义。
	// 必须在动作验证通过后清理，避免无效地面点击丢失原严格交互。
	cancelQueuedInteraction(true);

	nextAction = std::make_shared<NextAction>();
	nextAction->action = act.action;
	nextAction->dest = act.dest;
	nextAction->destKind = act.destKind;
	nextAction->actionParam = act.actionParam;
	nextAction->destGE = act.destGE;
	nextAction->useRightScript = act.useRightScript;
	nextAction->strictWorldInteraction = act.strictWorldInteraction;
	return true;
}

void Player::cancelQueuedInteraction(bool strictOnly)
{
	const bool stopPlayerMovement = nextDest != ndNone
		&& (!strictOnly || nextDestStrictWorldInteraction);
	const bool stopControlledMovement = controlledNextAction != nullptr
		&& controlledNextAction->destKind != ndNone
		&& (!strictOnly || controlledNextAction->strictWorldInteraction);
	if (nextAction != nullptr && nextAction->destKind != ndNone
		&& (!strictOnly || nextAction->strictWorldInteraction))
	{
		nextAction = nullptr;
	}
	if (stopPlayerMovement)
	{
		nextDest = ndNone;
		nextDestUseRightScript = false;
		nextDestStrictWorldInteraction = false;
		nextDestRequestedRunning = false;
		destGE.reset();
	}
	if (stopControlledMovement)
	{
		clearControlledNextAction();
	}
	if (stopPlayerMovement)
	{
		stopMovement();
	}
	if (stopControlledMovement)
	{
		auto controlled = getControlledCharacter();
		if (controlled != nullptr)
		{
			controlled->stopMovement();
		}
	}
}

std::shared_ptr<NPC> Player::getControlledCharacter() const
{
	auto target = controlledCharacter.lock();
	auto effect = controlledMagicEffect.lock();
	if (target == nullptr || effect == nullptr || (effect->result & erLifeExhaust))
	{
		return nullptr;
	}
	if (target->isDying() || target->isHiding())
	{
		return nullptr;
	}
	if (gm == nullptr || gm->npcManager == nullptr || !gm->npcManager->findNPC(target))
	{
		return nullptr;
	}
	return target;
}

std::shared_ptr<NPC> Player::getActionActor() const
{
	auto controlled = getControlledCharacter();
	if (controlled != nullptr)
	{
		return controlled;
	}
	return gm != nullptr ? gm->player : nullptr;
}

bool Player::isControllingCharacter() const
{
	return getControlledCharacter() != nullptr;
}

void Player::beginControlCharacter(std::shared_ptr<NPC> target, std::shared_ptr<Effect> effect)
{
	if (target == nullptr || effect == nullptr)
	{
		return;
	}

	endControlCharacter();
	controlledCharacter = target;
	controlledMagicEffect = effect;
	controlCharacterSessionActive = true;
	clearControlledNextAction();
	target->stopMovement();
	if (gm != nullptr && gm->camera != nullptr)
	{
		gm->camera->followPlayer = true;
		gm->camera->followNPC = target;
		gm->camera->snapToFollowTarget();
		gm->camera->differencePosition = { 0.0f, 0.0f };
	}
}

void Player::endControlCharacter(const Effect* effect)
{
	auto currentEffect = controlledMagicEffect.lock();
	if (effect != nullptr && currentEffect != nullptr && currentEffect.get() != effect)
	{
		return;
	}
	const bool endingActiveControlSession = controlCharacterSessionActive;

	auto target = controlledCharacter.lock();
	if (endingActiveControlSession && target != nullptr)
	{
		// A controlled NPC must not continue a player-issued approach after its
		// queued interaction metadata and control session are gone.
		target->stopMovement();
	}
	if (target != nullptr && gm != nullptr && gm->npcManager != nullptr)
	{
		gm->npcManager->clearCombatTargetIfEqual(target);
	}
	controlledCharacter.reset();
	controlledMagicEffect.reset();
	controlCharacterSessionActive = false;
	clearControlledNextAction();
	if (endingActiveControlSession && nextAction != nullptr
		&& nextAction->destKind != ndNone && nextAction->strictWorldInteraction)
	{
		nextAction = nullptr;
	}
	if (endingActiveControlSession && gm != nullptr && gm->camera != nullptr)
	{
		auto followTarget = gm->camera->followNPC.lock();
		if (target == nullptr || followTarget == target)
		{
			gm->camera->followNPC.reset();
			gm->camera->followPlayer = true;
			gm->camera->snapToFollowTarget();
			gm->camera->differencePosition = { 0.0f, 0.0f };
		}
	}
}

void Player::clearControlledNextAction()
{
	controlledNextAction = nullptr;
}

void Player::dispatchControlledAction(const NextAction& act)
{
	auto actor = getControlledCharacter();
	if (actor == nullptr)
	{
		clearControlledNextAction();
		return;
	}

	if (act.destKind != ndNone)
	{
		controlledNextAction = std::make_shared<NextAction>(act);
		processControlledNextAction();
		return;
	}

	switch (act.action)
	{
	case acWalk:
	case acRun:
	case acAWalk:
	case acARun:
		actor->beginWalk(act.dest);
		break;
	case acJump:
	case acAJump:
		actor->beginJump(act.dest);
		break;
	case acAttack:
	case acAttack1:
	case acAttack2:
	case acSpecialAttack:
		if (canFight)
		{
			actor->beginAttack(act.dest, act.destGE.lock());
		}
		break;
	case acMagic:
		gm->showMessage("控制中不能使用武功");
		break;
	default:
		break;
	}
}

void Player::processControlledNextAction()
{
	auto actor = getControlledCharacter();
	if (actor == nullptr)
	{
		endControlCharacter();
		return;
	}
	if (controlledNextAction == nullptr)
	{
		return;
	}
	auto beginControlledApproach = [this, &actor](Point destination)
	{
		const bool strictWorldInteraction = controlledNextAction != nullptr
			&& controlledNextAction->strictWorldInteraction;
		const bool pathFindCoolingDown = strictWorldInteraction
			&& actor->lastPathFindFailTime > 0
			&& actor->getTime() - actor->lastPathFindFailTime
				< NPC_PATH_FIND_FAIL_COOLDOWN;
		// A pre-existing NPC cooldown is not evidence that this target is unreachable.
		if (pathFindCoolingDown)
		{
			return;
		}

		actor->beginWalk(destination);
		if (strictWorldInteraction && !actor->isWalking())
		{
			cancelQueuedInteraction(true);
		}
	};

	auto targetElement = controlledNextAction->destGE.lock();
	if (targetElement == nullptr)
	{
		const bool stopStrictMovement = controlledNextAction->strictWorldInteraction;
		clearControlledNextAction();
		if (stopStrictMovement)
		{
			actor->stopMovement();
		}
		return;
	}

	if (controlledNextAction->destKind == ndAttack || controlledNextAction->destKind == ndTalk)
	{
		auto npc = std::dynamic_pointer_cast<NPC>(targetElement);
		if (npc == nullptr || gm->npcManager == nullptr || !gm->npcManager->findNPC(npc))
		{
			const bool stopStrictMovement = controlledNextAction->strictWorldInteraction;
			clearControlledNextAction();
			if (stopStrictMovement)
			{
				actor->stopMovement();
			}
			return;
		}
		if (controlledNextAction->strictWorldInteraction)
		{
			const WorldInteractionIntent intent = controlledNextAction->destKind == ndAttack
				? WorldInteractionIntent::Attack
				: (controlledNextAction->useRightScript
					? WorldInteractionIntent::Alternate
					: WorldInteractionIntent::Primary);
			const bool expectedScriptExists = controlledNextAction->destKind == ndAttack
				|| hasExpectedNPCScript(npc, controlledNextAction->useRightScript);
			if (!expectedScriptExists
				|| !WorldInteractionResolver::isNPCValidForIntent(npc, intent, actor))
			{
				clearControlledNextAction();
				actor->stopMovement();
				return;
			}
		}
		int distance = Map::calDistance(actor->getPosition(), npc->getPosition());
		if (controlledNextAction->destKind == ndAttack)
		{
			if (distance <= actor->attackRadius)
			{
				clearControlledNextAction();
				actor->beginStand();
				if (canFight)
				{
					actor->beginAttack(npc->getPosition(), npc);
				}
			}
			else if (actor->isStanding())
			{
				beginControlledApproach(npc->getPosition());
			}
			return;
		}

		if (npc->canTalkAtDistance(distance))
		{
			bool useRightScript = controlledNextAction->useRightScript;
			clearControlledNextAction();
			controlActorTalkTo(actor, npc, useRightScript);
		}
		else if (actor->isStanding())
		{
			beginControlledApproach(npc->getPosition());
		}
		return;
	}

	if (controlledNextAction->destKind == ndObj)
	{
		auto obj = std::dynamic_pointer_cast<Object>(targetElement);
		if (obj == nullptr || gm->objectManager == nullptr || !gm->objectManager->findObj(obj))
		{
			const bool stopStrictMovement = controlledNextAction->strictWorldInteraction;
			clearControlledNextAction();
			if (stopStrictMovement)
			{
				actor->stopMovement();
			}
			return;
		}
		if (controlledNextAction->strictWorldInteraction)
		{
			const WorldInteractionIntent intent = controlledNextAction->useRightScript
				? WorldInteractionIntent::Alternate
				: WorldInteractionIntent::Primary;
			if (!hasExpectedObjectScript(obj, controlledNextAction->useRightScript)
				|| !WorldInteractionResolver::isObjectValidForIntent(obj, intent))
			{
				clearControlledNextAction();
				actor->stopMovement();
				return;
			}
		}
		int distance = Map::calDistance(actor->getPosition(), obj->position);
		if (obj->canInteractAtDistance(distance))
		{
			bool useRightScript = controlledNextAction->useRightScript;
			clearControlledNextAction();
			controlActorTriggerObject(actor, obj, useRightScript);
		}
		else if (actor->isStanding())
		{
			beginControlledApproach(obj->position);
		}
	}
}

void Player::controlActorTalkTo(std::shared_ptr<NPC> actor, std::shared_ptr<NPC> npc, bool useRightScript)
{
	if (actor == nullptr || npc == nullptr)
	{
		return;
	}

	actor->beginStand();
	npc->beginStand();
	actor->direction = NPC::getDirection(actor->getPosition(), npc->getPosition());
	int oldNpcDirection = npc->direction;
	npc->direction = normalizeDir(actor->direction + 4);
	std::string scriptFile = (useRightScript && npc->scriptFileRight != "") ? npc->scriptFileRight : npc->scriptFile;
	if (scriptFile != "")
	{
		gm->runNPCScript(npc, scriptFile);
	}
	if (gm->npcManager->findNPC(npc) && npc->strollIntent == nsiNone && npc->isStanding())
	{
		npc->direction = oldNpcDirection;
	}
}

void Player::controlActorTriggerObject(std::shared_ptr<NPC> actor, std::shared_ptr<Object> obj, bool useRightScript)
{
	if (actor == nullptr || obj == nullptr)
	{
		return;
	}

	actor->beginStand();
	actor->direction = NPC::getDirection(actor->getPosition(), obj->position);
	std::string scriptFile = obj->getScriptFile(useRightScript);
	if (scriptFile != "")
	{
		gm->runObjScript(obj, scriptFile);
	}
	if (isPickupObjectKind(obj->kind) && gm->objectManager != nullptr && gm->objectManager->findObj(obj))
	{
		gm->objectManager->deleteObject(obj);
	}
}

bool Player::handleQueuedInteractionAtCurrentPosition()
{
	if (nextDest == ndNone)
	{
		return false;
	}
	auto cancelPendingInteraction = [this]()
	{
		nextDest = ndNone;
		nextDestUseRightScript = false;
		nextDestStrictWorldInteraction = false;
		nextDestRequestedRunning = false;
		destGE.reset();
		stopMovement();
	};
	if (destGE.expired())
	{
		if (nextDestStrictWorldInteraction)
		{
			cancelPendingInteraction();
			return true;
		}
		return false;
	}
	auto destGEPtr = destGE.lock();
	if (!destGEPtr)
	{
		if (nextDestStrictWorldInteraction)
		{
			cancelPendingInteraction();
			return true;
		}
		return false;
	}

	NextDest nd = nextDest;
	if (nd == ndAttack || nd == ndTalk)
	{
		auto destNPC = std::dynamic_pointer_cast<NPC>(destGEPtr);
		if (!destNPC || gm->npcManager == nullptr || !gm->npcManager->findNPC(destNPC))
		{
			if (nextDestStrictWorldInteraction)
			{
				cancelPendingInteraction();
				return true;
			}
			return false;
		}
		if (nextDestStrictWorldInteraction)
		{
			const WorldInteractionIntent intent = nd == ndAttack
				? WorldInteractionIntent::Attack
				: (nextDestUseRightScript
					? WorldInteractionIntent::Alternate
					: WorldInteractionIntent::Primary);
			const bool expectedScriptExists = nd == ndAttack
				|| hasExpectedNPCScript(destNPC, nextDestUseRightScript);
			if (!expectedScriptExists
				|| !WorldInteractionResolver::isNPCValidForIntent(destNPC, intent, gm->player))
			{
				cancelPendingInteraction();
				return true;
			}
		}
		int dist = Map::calDistance(position, destNPC->getPosition());
		if (nd == ndAttack && dist <= attackRadius)
		{
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginStand();
			beginAttack(destNPC->getPosition(), destNPC);
			return true;
		}
		else if (nd == ndTalk && destNPC->canTalkAtDistance(dist))
		{
			bool useRightScript = nextDestUseRightScript;
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginStand();
			talkTo(destNPC, useRightScript);
			return true;
		}
	}
	else if (nd == ndObj)
	{
		auto destObj = std::dynamic_pointer_cast<Object>(destGEPtr);
		if (!destObj || gm->objectManager == nullptr || !gm->objectManager->findObj(destObj))
		{
			if (nextDestStrictWorldInteraction)
			{
				cancelPendingInteraction();
				return true;
			}
			return false;
		}
		if (nextDestStrictWorldInteraction)
		{
			const WorldInteractionIntent intent = nextDestUseRightScript
				? WorldInteractionIntent::Alternate
				: WorldInteractionIntent::Primary;
			if (!hasExpectedObjectScript(destObj, nextDestUseRightScript)
				|| !WorldInteractionResolver::isObjectValidForIntent(destObj, intent))
			{
				cancelPendingInteraction();
				return true;
			}
		}
		int dist = Map::calDistance(position, destObj->position);
		if (destObj->canInteractAtDistance(dist))
		{
			bool useRightScript = nextDestUseRightScript;
			nextDest = ndNone;
			nextDestUseRightScript = false;
			nextDestStrictWorldInteraction = false;
			nextDestRequestedRunning = false;
			destGE.reset();
			beginStand();
			triggerObject(destObj, useRightScript);
			return true;
		}
	}
	return false;
}

bool Player::resumeStrictQueuedInteraction()
{
	if (nextDest == ndNone || !nextDestStrictWorldInteraction)
	{
		return false;
	}
	if (gm == nullptr || gm->map == nullptr || immobilized || petrified)
	{
		cancelQueuedInteraction(true);
		return true;
	}

	auto targetElement = destGE.lock();
	Point currentTargetPosition;
	if (nextDest == ndAttack || nextDest == ndTalk)
	{
		auto targetNPC = std::dynamic_pointer_cast<NPC>(targetElement);
		if (targetNPC == nullptr || gm->npcManager == nullptr
			|| !gm->npcManager->findNPC(targetNPC))
		{
			cancelQueuedInteraction(true);
			return true;
		}
		currentTargetPosition = targetNPC->getPosition();
	}
	else if (nextDest == ndObj)
	{
		auto targetObject = std::dynamic_pointer_cast<Object>(targetElement);
		if (targetObject == nullptr || gm->objectManager == nullptr
			|| !gm->objectManager->findObj(targetObject))
		{
			cancelQueuedInteraction(true);
			return true;
		}
		currentTargetPosition = targetObject->position;
	}
	else
	{
		cancelQueuedInteraction(true);
		return true;
	}

	bool continueRunning = nextDestRequestedRunning && canRun && canDoAction(acRun)
		&& (gm->inEvent || ignoresRunThewCost() || thew >= RUN_THEW_COST);
	if (!continueRunning && !canDoAction(acWalk))
	{
		cancelQueuedInteraction(true);
		return true;
	}
	nextDestRequestedRunning = continueRunning;
	if (!startMoveInternal(currentTargetPosition, continueRunning, false))
	{
		cancelQueuedInteraction(true);
	}
	return true;
}

bool Player::startMoveInternal(Point dest, bool running, bool isRetarget)
{
	if (isRetarget && getTime() - lastPathFindFailTime < PATH_FIND_FAIL_COOLDOWN)
	{
		return false;
	}

	bool isSameMoveAction = (running && isRunning()) || (!running && isWalking());
	bool isMoveActionActive = isWalking() || isRunning();
	int interactionRadius = 0;
	if (auto targetElement = destGE.lock())
	{
		if (nextDest == ndTalk)
		{
			if (auto targetNPC = std::dynamic_pointer_cast<NPC>(targetElement))
			{
				interactionRadius = std::max(0, targetNPC->dialogRadius);
			}
		}
		else if (nextDest == ndAttack)
		{
			interactionRadius = std::max(0, attackRadius);
		}
		else if (nextDest == ndObj)
		{
			interactionRadius = 1;
		}
	}
	auto findMovementPath = [this, dest, interactionRadius](Point from)
	{
		return interactionRadius > 0
			? gm->map->getRadiusPath(
				from, dest, interactionRadius, getMoveDirectionCount())
			: gm->map->findPath(from, dest, getMoveDirectionCount());
	};
	auto movementDestinationReached = [this, dest, interactionRadius](Point from)
	{
		return interactionRadius > 0
			? Map::calDistance(from, dest) <= interactionRadius
			: from == dest;
	};
	if (isRetarget && isMoveActionActive && !processingStepIn)
	{
		Point pathStart = position;
		std::deque<Point> preservedStepList;
		if (stepState == ssOut)
		{
			if (stepList.empty())
			{
				return false;
			}
			pathStart = stepList[0];
			preservedStepList.push_back(stepList[0]);
		}
		else if (stepState == ssIn)
		{
			pathStart = position;
			preservedStepList.push_back(position);
		}
		else
		{
			return false;
		}

		auto tailList = findMovementPath(pathStart);
		if (tailList.empty() && !movementDestinationReached(pathStart))
		{
			lastPathFindFailTime = getTime();
			partnerAvoidBlockingPlayer(dest);
			return false;
		}
		for (const auto& step : tailList)
		{
			preservedStepList.push_back(step);
		}
		gm->partnerManager.setPartnersIsBlockingPlayer(false);
		stepList = preservedStepList;
		return true;
	}

	auto tempList = findMovementPath(position);
	if (tempList.size() == 0)
	{
		if (isRetarget)
		{
			lastPathFindFailTime = getTime();
		}
		else
		{
			beginStand();
		}
		partnerAvoidBlockingPlayer(dest);
		return false;
	}

	if (!isRetarget && !gm->map->canWalk(tempList[0]))
	{
		beginStand();
		return false;
	}

	if (running && !gm->inEvent)
	{
		if (!ignoresRunThewCost() && thew < RUN_THEW_COST)
		{
			gm->showMessage(THEW_LOW_MSG);
			beginStand();
			return false;
		}
		if (!ignoresRunThewCost())
		{
			thew -= RUN_THEW_COST;
		}
	}

	gm->partnerManager.setPartnersIsBlockingPlayer(false);
	stepList = tempList;
	direction = getDirection(stepList[0]);

	if (isRetarget)
	{
		if (isSameMoveAction)
		{
			actionManager->getCurrentAction()->retarget();
			return true;
		}
	}
	else if (!running)
	{
		if (!isWalking() && !isStanding())
		{
			resetRecoveryTime();
		}
	}

	actionManager->changeAction(running ? acRun : acWalk);
	return true;
}


bool Player::changeWalk(Point dest)
{
	if (!canDoAction(acWalk))
	{
		return false;
	}
	if (handleQueuedInteractionAtCurrentPosition())
	{
		return true;
	}
	return startMoveInternal(dest, false, true);
}

bool Player::changeRun(Point dest)
{
	if (!canRun || !canDoAction(acRun))
	{
		return false;
	}
	if (handleQueuedInteractionAtCurrentPosition())
	{
		return true;
	}
	return startMoveInternal(dest, true, true);
}

void Player::triggerObject(std::shared_ptr<Object> obj, bool useRightScript)
{
	if (obj == nullptr)
	{
		return;
	}
	beginStand();
	direction = getDirection(position, obj->position);

	std::string scriptFile = obj->getScriptFile(useRightScript);
	if (scriptFile != "")
	{
		gm->runObjScript(obj, scriptFile);
	}
	if (isPickupObjectKind(obj->kind) && gm->objectManager != nullptr && gm->objectManager->findObj(obj))
	{
		gm->objectManager->deleteObject(obj);
	}
}

void Player::triggerTouchObjects()
{
	if (gm == nullptr || gm->inEvent || gm->objectManager == nullptr || nowAction == acDeath || nowAction == acHide)
	{
		return;
	}

	auto objectList = gm->objectManager->objectList;
	for (auto& obj : objectList)
	{
		if (obj == nullptr || !canTriggerObjectTouchScript(obj->scriptFileJustTouch, obj->hasInteractScript(false)))
		{
			continue;
		}
		if (!gm->objectManager->findObj(obj))
		{
			continue;
		}
		if (obj->position == position)
		{
			gm->runObjScript(obj, obj->scriptFile);
		}
	}
}

void Player::talkTo(std::shared_ptr<NPC> npc, bool useRightScript)
{
	if (npc == nullptr)
	{
		return;
	}
	beginStand();
	npc->beginStand();
	direction = getDirection(position, npc->getPosition());
	int tempDir = npc->direction;
	npc->direction = normalizeDir(direction + 4);
	std::string scriptFile = (useRightScript && npc->scriptFileRight != "") ? npc->scriptFileRight : npc->scriptFile;
	if (scriptFile != "")
	{
		gm->runNPCScript(npc, scriptFile);
	}
	if (gm->npcManager->findNPC(npc) && npc->strollIntent == nsiNone && npc->isStanding())
	{
		npc->direction = tempDir;
	}
}

void Player::beginStand()
{
	if (!isWalking() && !isStanding())
	{
		resetRecoveryTime();
	}
	
	actionManager->changeAction(acStand);
}

void Player::forceBeginStand()
{
	if (!isWalking() && !isStanding())
	{
		resetRecoveryTime();
	}

	nextAction = nullptr;
	nextDest = ndNone;
	nextDestUseRightScript = false;
	nextDestStrictWorldInteraction = false;
	nextDestRequestedRunning = false;
	destGE.reset();
	actionManager->resetActionIgnoringTransitions(acStand);
}

void Player::beginWalk(Point dest)
{
	if (!canDoAction(acWalk) || immobilized || petrified)
	{
		return;
	}
	if (handleQueuedInteractionAtCurrentPosition())
	{
		return;
	}
	startMoveInternal(dest, false, false);
	
}

void Player::beginMagic(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canFight || !canDoAction(acMagic) || immobilized || petrified)
	{
		return;
	}
	if (!canUseMana)
	{
		gm->showMessage(MANA_LOW_MSG);
		return;
	}
	clearPreparedMagicAction();
	if (magicIndex < 0 || magicIndex >= gm->magicManager.bottomCount())
	{
		return;
	}

	int listIndex = gm->magicManager.bottomIndex(magicIndex);
	if (!gm->magicManager.magicListExists(listIndex))
	{
		return;
	}

	auto& magicInfo = gm->magicManager.magicList[listIndex];
	if (magicInfo.remainColdMilliseconds > 0)
	{
		gm->showMessage("武功尚未冷却");
		return;
	}
	if (magicInfo.level < 1 || magicInfo.level > MAGIC_MAX_LEVEL)
	{
		return;
	}
	auto preparedMagic = resolveMagicReplacement(magicInfo.magic);
	if (preparedMagic == nullptr)
	{
		return;
	}
	if (!canUseMagicByState(preparedMagic, true))
	{
		return;
	}
	if (!canActToward(dest, getMagicActionDirectionCount(preparedMagic)))
	{
		return;
	}
	setPreparedMagicAction(preparedMagic, dest, magicInfo.level, target, listIndex);
	destGE = target;
	attackDone = false;
	magicDest = dest;
	direction = getDirection(dest);
	
	auto previousAction = actionManager->getCurrentAction();
	actionManager->changeAction(acMagic);
	if (actionManager->getCurrentAction() == previousAction)
	{
		clearPreparedMagicAction();
	}
}

void Player::beginJump(Point dest)
{
	if (!canJump || !canDoAction(acJump) || immobilized || petrified)
	{
		return;
	}
	if (!gm->inEvent)
	{
		if (thew < JUMP_THEW_COST)
		{
			gm->showMessage(THEW_LOW_MSG);
			return;
		}
	}
	if (!canActToward(dest, getJumpDirectionCount()))
	{
		return;
	}
	if (!gm->inEvent)
	{
		thew -= JUMP_THEW_COST;
	}
	Point step = gm->map->getJumpPath(position, dest);
	stepList.resize(1);
	stepList[0] = step;
	direction = getDirection(stepList[0]);
	
	actionManager->changeAction(acJump);
}

void Player::beginRun(Point dest)
{
	if (!canRun || !canDoAction(acRun) || immobilized || petrified)
	{
		return;
	}
	if (handleQueuedInteractionAtCurrentPosition())
	{
		return;
	}
	startMoveInternal(dest, true, false);
}

void Player::beginAttack(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canFight || !canDoAction(acAttack) || immobilized || petrified)
	{
		return;
	}
	if (thew < ATTACK_THEW_COST)
	{
		gm->showMessage(THEW_LOW_MSG);
		return;
	}
	std::shared_ptr<Magic> attackMagicForDirection = nullptr;
	int practiceIndex = gm->magicManager.practiceIndex();
	if (gm->magicManager.magicListExists(practiceIndex))
	{
		attackMagicForDirection = resolveMagicReplacement(gm->magicManager.magicList[practiceIndex].magic);
	}
	if (!canActToward(dest, getAttackActionDirectionCount(attackMagicForDirection)))
	{
		return;
	}
	thew -= ATTACK_THEW_COST;
	destGE = target;
	direction = getDirection(dest);
	attackDone = false;
	magicDest = dest;
	attackReleaseMode = (target == nullptr) ? armGroundTarget : armLockedRelease;
	
	actionManager->changeAction(acAttack);
}

void Player::beginHurt(Point dest)
{
	if (isHurting() || !canDoAction(acHurt) || !canHurt() || immobilized || petrified)
	{
		return;
	}
	destGE.reset();

	if (isWalking() || isRunning())
	{
		resumingMove = true;
		previousMoveAction = nowAction;
		savedDirection = direction;
		hurtBeginStepTime = getTime();
		savedStepState = stepState;
		savedStepPositions = getStepPositions();
	}

	direction = getDirection(dest);
	actionManager->changeAction(acHurt);
}

bool Player::canHurt()
{
	if (isJumping() && (jumpState == jsJumping || (jumpState == jsDown && gm->map->haveTraps(position))))
	{
		return false;
	}
	else if ((isWalking() || isRunning()) && stepState == ssIn && gm->map->haveTraps(position))
	{
		return false;
	}
	return true;
}

void Player::suppressTrapAtScriptPosition()
{
	scriptPositionTrapSuppressed = true;
	scriptTrapPosition = position;
	scriptTrapMapName = gm->global.data.mapName;
}

void Player::checkTrap()
{
	if (scriptPositionTrapSuppressed)
	{
		if (scriptTrapPosition == position && scriptTrapMapName == gm->global.data.mapName)
		{
			return;
		}
		scriptPositionTrapSuppressed = false;
		scriptTrapMapName.clear();
	}
	if (gm->map->haveTraps(position))
	{
		gm->runTrapScript(gm->map->getTrapIndex(position));
	}
}

void Player::partnerAvoidBlockingPlayer(Point dest)
{
	bool isBlocking = false;
	auto partnerList = gm->partnerManager.findPartnersFromNPCManager();
	for (auto& partner: partnerList)
	{
		if (partner->isStanding())
		{
			if (partner->canDoAction(acRun))
			{
				partner->beginRun(dest);
			}
			else
			{
				partner->beginWalk(dest);
			}
			if (!partner->isStanding())
			{
				isBlocking = true;
			}
		}
	}
	if (isBlocking)
	{
		gm->partnerManager.setPartnersIsBlockingPlayer(true);
	}

}

void Player::limitAttribute()
{
	if (life > info.lifeMax)
	{
		life = info.lifeMax;
	}
	if (thew > info.thewMax)
	{
		thew = info.thewMax;
	}
	if (mana > info.manaMax)
	{
		mana = info.manaMax;
	}
}

void Player::loadLevel(const std::string& fileName)
{
	levelList.clear();
	std::string iniName = LEVEL_FOLDER + fileName;
	std::unique_ptr<char[]> s;
	int len = File::readFile(iniName, s);
	kind = nkPlayer;
	if (s == nullptr || len <= 0)
	{
		return;
	}

	INIReader ini(s);
	if (ini.ParseError() != 0)
	{
		return;
	}
	int levelCount = 0;
	if (!NPCPersistence::readLevelCount(ini, levelCount))
	{
		GameLog::write("Player: invalid level count in %s\n", iniName.c_str());
		return;
	}

	levelList.resize(static_cast<size_t>(levelCount));
	for (size_t i = 0; i < levelList.size(); i++)
	{
		std::string section = convert::formatString("Level%d", i + 1);
		levelList[i].levelUpExp = ini.GetInteger(section, "LevelUpExp", 0);
		levelList[i].lifeMax = ini.GetInteger(section, "LifeMax", 0);
		levelList[i].thewMax = ini.GetInteger(section, "ThewMax", 0);
		levelList[i].manaMax = ini.GetInteger(section, "ManaMax", 0);
		levelList[i].attack = ini.GetInteger(section, "Attack", 0);
		levelList[i].attack2 = ini.GetInteger(section, "Attack2", 0);
		levelList[i].attack3 = ini.GetInteger(section, "Attack3", 0);
		levelList[i].defend = ini.GetInteger(section, "Defend", 0);
		levelList[i].defend2 = ini.GetInteger(section, "Defend2", 0);
		levelList[i].defend3 = ini.GetInteger(section, "Defend3", 0);
		levelList[i].evade = ini.GetInteger(section, "Evade", 0);
		levelList[i].newMagic = ini.Get(section, "NewMagic", "");
	}

}

bool Player::doSpecialAttack(Point dest, std::shared_ptr<GameElement> target)
{
	int practiceIndex = gm->magicManager.practiceIndex();
	if (!gm->magicManager.magicListExists(practiceIndex))
	{
		return doAttack(dest, target, attackReleaseMode);
	}
	auto practiceMagic = gm->magicManager.magicList[practiceIndex].magic;
	const auto& linked = practiceMagic->getLinkedLevel(attackLevel);
	if (linked.specialMagic == nullptr)
	{
		return doAttack(dest, target, attackReleaseMode);
	}
	int launcher = 0;
	if (kind == nkPlayer)
	{
		launcher = lkSelf;
	}
	else
	{
		if (relation == nrFriendly)
		{
			launcher = lkFriend;
		}
		else if (relation == nrHostile)
		{
			launcher = lkEnemy;
		}
		else if (relation == nrNeutral)
		{
			launcher = lkNeutral;
		}
	}
	auto magic = resolveMagicReplacement(linked.specialMagic);
	Magic::addEffect(magic, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, attack, getEvade(), launcher, target);
	return true;
}

std::shared_ptr<Magic> Player::prepareSpecialAttackMagicForAction(Point dest, std::shared_ptr<GameElement> target)
{
	clearPreparedAttackMagic();
	int practiceIndex = gm->magicManager.practiceIndex();
	if (!gm->magicManager.magicListExists(practiceIndex))
	{
		return nullptr;
	}
	auto practiceMagic = gm->magicManager.magicList[practiceIndex].magic;
	auto magic = resolveMagicReplacement(practiceMagic->getLinkedLevel(attackLevel).specialMagic);
	if (magic == nullptr)
	{
		return nullptr;
	}
	setPreparedAttackMagic(magic, false);
	return magic;
}

bool Player::releasePreparedSpecialAttackMagic(Point dest, std::shared_ptr<GameElement> target)
{
	if (!hasPreparedAttackMagic || preparedAttackMagic == nullptr)
	{
		return false;
	}

	auto magic = preparedAttackMagic;
	clearPreparedAttackMagic();
	int launcher = 0;
	if (kind == nkPlayer)
	{
		launcher = lkSelf;
	}
	else
	{
		if (relation == nrFriendly)
		{
			launcher = lkFriend;
		}
		else if (relation == nrHostile)
		{
			launcher = lkEnemy;
		}
		else if (relation == nrNeutral)
		{
			launcher = lkNeutral;
		}
	}
	Magic::addEffect(magic, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, attack, getEvade(), launcher, target);
	return true;
}

void Player::drawAlpha(
	Point cenTile,
	Point cenScreen,
	PointEx coffset,
	uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	PointEx drawOffset = getDrawOffset();
	int offsetX, offsetY;
	_shared_image image = getActionImage(&offsetX, &offsetY);
	ColorStyle::drawImage(
		engine,
		image,
		pos.x + (int)round(drawOffset.x) - offsetX,
		pos.y + (int)round(drawOffset.y) - offsetY,
		colorStyle,
		128);
}

void Player::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	PointEx drawOffset = getDrawOffset();
	int offsetX, offsetY;
	_shared_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY);
	image = getActionImage(&offsetX, &offsetY);
	ColorStyle::drawImage(engine, image, pos.x + (int)round(drawOffset.x) - offsetX, pos.y + (int)round(drawOffset.y) - offsetY, colorStyle);
	drawChangeMagicHitVisuals(pos, drawOffset);
	
}

void Player::handleDeath()
{
	beginDie();
}

void Player::beginDie()
{
	if (nowAction == acDeath || nowAction == acHide)
	{
		return;
	}
	if (!canDoAction(acDeath))
	{
		return;
	}
	if (deathScript != "")
	{
		result |= erRunDeathScript;
	}

	if (shieldLife > 0)
	{
		shieldLife = 0;
		if (auto shield = shieldEffect.lock())
		{
			if (!shield->vanishing)
			{
				shield->vanishing = true;
				shield->beginExplode(shield->position);
			}
		}
		shieldEffect.reset();
	}
	for (auto it = shieldEffects.begin(); it != shieldEffects.end(); ++it)
	{
		if (auto shield = it->lock())
		{
			if (!shield->vanishing)
			{
				shield->vanishing = true;
				shield->beginExplode(shield->position);
			}
		}
	}
	shieldEffects.clear();

	actionManager->changeAction(acDeath);
}

bool Player::load(int index, std::string* failureReason)
{
	std::string fName =
		SaveFileManager::CurrentPath() + PLAYER_INI_NAME;
	std::string displayName = PLAYER_INI_NAME;
	if (index >= 0)
	{
		fName += convert::formatString("%d", index);
		displayName += convert::formatString("%d", index);
	}
	fName += PLAYER_INI_EXT;
	displayName += PLAYER_INI_EXT;
	return loadFromFile(fName, displayName, failureReason);
}

bool Player::loadInitialTemplate(
	int index,
	std::string* failureReason)
{
	std::string fileName = PLAYER_INI_NAME;
	if (index >= 0)
	{
		fileName += convert::formatString("%d", index);
	}
	fileName += PLAYER_INI_EXT;
	return loadFromFile(
		std::string(INI_SAVE_FOLDER) + fileName,
		fileName,
		failureReason);
}

bool Player::loadFromFile(
	const std::string& fileName,
	const std::string& displayName,
	std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}

	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readFile(fileName, data, length))
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"玩家数据文件不存在或无法读取：" + displayName;
		}
		GameLog::write("Player: player save is missing or unreadable %s\n", fileName.c_str());
		return false;
	}
	if (length <= 0 || data == nullptr)
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"玩家数据文件为空：" + displayName;
		}
		GameLog::write("Player: player save is empty %s\n", fileName.c_str());
		return false;
	}
	INIReader ini(data);
	if (ini.ParseError() != 0)
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"玩家数据格式错误：" + displayName;
		}
		GameLog::write("Player: invalid player save %s\n", fileName.c_str());
		return false;
	}
	if (!ini.HasSection("Init"))
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"玩家数据缺少 [Init]：" + displayName;
		}
		GameLog::write("Player: player save has no Init section %s\n", fileName.c_str());
		return false;
	}

	freeResource();

	std::string section = "Init";
	initFromIni(&ini, section);

	magic = ini.GetInteger(section, "Magic", 0);
	money = ini.GetInteger(section, "Money", 0);
	setRage(gm != nullptr && gm->global.feature.rageSystem
		? ini.GetInteger(section, "Rage", 0)
		: 0);
	std::string isRunDisabledValue = ini.Get(section, "IsRunDisabled", "");
	if (!isRunDisabledValue.empty())
	{
		setRunDisabled(ini.GetBoolean(section, "IsRunDisabled", false));
	}
	else
	{
		canRun = ini.GetBoolean(section, "CanRun", true);
	}
	std::string isJumpDisabledValue = ini.Get(section, "IsJumpDisabled", "");
	if (!isJumpDisabledValue.empty())
	{
		setJumpDisabled(ini.GetBoolean(section, "IsJumpDisabled", false));
	}
	else
	{
		canJump = ini.GetBoolean(section, "CanJump", true);
	}
	std::string isFightDisabledValue = ini.Get(section, "IsFightDisabled", "");
	if (!isFightDisabledValue.empty())
	{
		setFightDisabled(ini.GetBoolean(section, "IsFightDisabled", false));
	}
	else
	{
		canFight = ini.GetBoolean(section, "CanFight", true);
	}
	canUseMana = ini.GetBoolean(section, "CanUseMana", true);
	walkIsRun = (int)ini.GetInteger(section, "WalkIsRun", 0);
	fightState.set(ini.GetInteger(section, "Fight", 0) == 1);
	levelIni = ini.Get(section, "LevelIni", "");

	loadLevel(levelIni);
	// loadCurrentGame() resets the game clock before reusing this Player. If the
	// player was already standing, changeAction() would keep the old action
	// begin time and unsigned elapsed-time calculations could jump through idle
	// frames until the player moved. A load commit always starts a fresh stand.
	actionManager->restartActionIgnoringTransitions(acStand);
	return true;
}

bool Player::save(int index)
{
	INIReader ini;
	std::string section = "Init";

	Point tempPos = position;
	if (isJumping() && jumpState == jsJumping)
	{
		position = stepList[0];
	}

	saveToIni(&ini, section);

	position = tempPos;

	ini.SetInteger(section, "Magic", magic);
	ini.SetInteger(section, "Money", money);
	if (gm != nullptr && gm->global.feature.rageSystem)
	{
		ini.SetInteger(section, "Rage", rage);
	}
	ini.SetBoolean(section, "CanRun", canRun);
	ini.SetBoolean(section, "CanJump", canJump);
	ini.SetBoolean(section, "CanFight", canFight);
	ini.SetBoolean(section, "IsRunDisabled", isRunDisabled());
	ini.SetBoolean(section, "IsJumpDisabled", isJumpDisabled());
	ini.SetBoolean(section, "IsFightDisabled", isFightDisabled());
	ini.SetBoolean(section, "CanUseMana", canUseMana);
	ini.SetInteger(section, "WalkIsRun", walkIsRun);
	ini.SetInteger(section, "Fight", fightState.get() ? 1 : 0);
	ini.Set(section, "LevelIni", levelIni);

	std::string fName = PLAYER_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += PLAYER_INI_EXT;
	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
	return saved;
}
