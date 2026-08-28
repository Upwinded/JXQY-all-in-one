#include "../File/File.h"
#include "../Game/Data/Goods.h"
#include "../Game/Data/NPC.h"
#include "../Game/GameManager/GameManager.h"
#include "TestTemporaryDirectory.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool writeEquipmentFixture(
	const std::filesystem::path& root,
	const std::string& fileName,
	const std::string& part,
	int lifeMax = 0,
	int thewMax = 0,
	int manaMax = 0)
{
	std::error_code errorCode;
	std::filesystem::create_directories(
		root / "ini" / "goods", errorCode);
	std::ofstream output(
		root / "ini" / "goods" / fileName,
		std::ios::binary);
	if (!output)
	{
		return false;
	}
	output
		<< "[Init]\n"
		<< "Name=" << fileName << "\n"
		<< "Kind=1\n"
		<< "Part=" << part << "\n"
		<< "LifeMax=" << lifeMax << "\n"
		<< "ThewMax=" << thewMax << "\n"
		<< "ManaMax=" << manaMax << "\n";
	return true;
}

bool writeMagicFixture(
	const std::filesystem::path& root,
	const std::string& fileName)
{
	std::error_code errorCode;
	std::filesystem::create_directories(
		root / "ini" / "magic", errorCode);
	std::ofstream output(
		root / "ini" / "magic" / fileName,
		std::ios::binary);
	if (!output)
	{
		return false;
	}
	output
		<< "[Init]\n"
		<< "Name=Large Stack Magic\n";
	return true;
}

void setPlayerGoods(
	GoodsInfo& goodsInfo,
	const std::string& fileName,
	int number)
{
	goodsInfo.clear();
	goodsInfo.iniFile = fileName;
	goodsInfo.number = number;
	goodsInfo.goods = std::make_shared<Goods>();
	goodsInfo.goods->initFromIni(fileName);
}

void fillPlayerCarryDomains(GoodsManager& goodsManager)
{
	for (int index = 0; index < goodsManager.listLength(); index++)
	{
		if (!goodsManager.isStoreIndex(index)
			&& !goodsManager.isBottomIndex(index))
		{
			continue;
		}
		GoodsInfo& goodsInfo = goodsManager.goodsList[index];
		goodsInfo.clear();
		goodsInfo.iniFile = "full_slot_" + std::to_string(index) + ".ini";
		goodsInfo.number = 1;
		goodsInfo.goods = std::make_shared<Goods>();
		goodsInfo.goods->kind = gkNormal;
	}
}

bool hasSameGoodsState(
	const std::vector<GoodsInfo>& first,
	const std::vector<GoodsInfo>& second)
{
	if (first.size() != second.size())
	{
		return false;
	}
	for (size_t index = 0; index < first.size(); ++index)
	{
		if (first[index].iniFile != second[index].iniFile
			|| first[index].number != second[index].number
			|| first[index].goods != second[index].goods
			|| first[index].remainColdMilliseconds
				!= second[index].remainColdMilliseconds)
		{
			return false;
		}
	}
	return true;
}
}

bool runPartnerEquipmentTransferTests()
{
	const std::filesystem::path root =
		makeUniqueTestDirectory("jxqy_partner_equipment_transfer_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	bool ok = check(
		writeEquipmentFixture(root, "head_a.ini", "head")
		&& writeEquipmentFixture(root, "head_b.ini", "head")
		&& writeEquipmentFixture(root, "body.ini", "body")
		&& writeEquipmentFixture(
			root, "vitality_head.ini", "head", 40, 20, 30)
		&& writeMagicFixture(root, "large_stack_magic.ini"),
		"partner equipment fixtures are created");
	if (!ok)
	{
		return false;
	}
	File::setAssetsCollectionRoot(root.string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});

	GameManager gameManager;
	gameManager.player->lifeMax = 100;
	gameManager.player->thewMax = 80;
	gameManager.player->manaMax = 60;
	gameManager.player->life = 25;
	gameManager.player->thew = 30;
	gameManager.player->mana = 10;
	gameManager.player->calInfo();
	const int vitalityBagIndex = gameManager.goodsManager.storeBegin();
	const int vitalityEquipIndex = gameManager.goodsManager.equipIndex(0);
	setPlayerGoods(
		gameManager.goodsManager.goodsList[vitalityBagIndex],
		"vitality_head.ini",
		1);
	gameManager.goodsManager.exchange(vitalityBagIndex, vitalityEquipIndex);
	ok = check(
		gameManager.player->getLifeMax() == 140
			&& gameManager.player->life == 65
			&& gameManager.player->getThewMax() == 100
			&& gameManager.player->thew == 50
			&& gameManager.player->getManaMax() == 90
			&& gameManager.player->mana == 40,
		"equipping maximum attributes increases the player's current values by the same deltas") && ok;
	gameManager.player->life = 17;
	gameManager.player->thew = 11;
	gameManager.player->mana = 5;
	const GoodsInfo loadedVitalityEquipment =
		gameManager.goodsManager.goodsList[vitalityEquipIndex];
	gameManager.goodsManager.goodsList[vitalityEquipIndex].clear();
	gameManager.player->calInfo();
	gameManager.goodsManager.goodsList[vitalityEquipIndex] =
		loadedVitalityEquipment;
	gameManager.goodsManager.refreshEquipmentEffects(false);
	ok = check(
		gameManager.player->getLifeMax() == 140
			&& gameManager.player->life == 17
			&& gameManager.player->getThewMax() == 100
			&& gameManager.player->thew == 11
			&& gameManager.player->getManaMax() == 90
			&& gameManager.player->mana == 5,
		"rebuilding loaded equipment preserves saved current attribute values") && ok;
	gameManager.goodsManager.exchange(vitalityEquipIndex, vitalityBagIndex);
	ok = check(
		gameManager.player->getLifeMax() == 100
			&& gameManager.player->life == 0
			&& gameManager.player->getThewMax() == 80
			&& gameManager.player->thew == 0
			&& gameManager.player->getManaMax() == 60
			&& gameManager.player->mana == 0,
		"unequipping maximum attributes removes the same current-value bonuses without underflow") && ok;
	gameManager.goodsManager.goodsList[vitalityBagIndex].clear();

	GoodsInfo& largeStack =
		gameManager.goodsManager.goodsList[vitalityBagIndex];
	largeStack.iniFile = "large_no_need_equipment.ini";
	largeStack.number = std::numeric_limits<int>::max();
	largeStack.goods = std::make_shared<Goods>();
	largeStack.goods->kind = gkEquipment;
	largeStack.goods->noNeedToEquip = 1;
	largeStack.goods->attack = 2;
	largeStack.goods->attack2 = -2;
	largeStack.goods->lifeMax = 2;
	largeStack.goods->changeMoveSpeedPercent = 2;
	largeStack.goods->addMagicEffectPercent = 2;
	largeStack.goods->magicIniWhenUse = "large_stack_magic.ini";
	gameManager.player->calInfo();
	Magic testMagic;
	MagicInfo* largeStackMagic =
		gameManager.magicManager.findPrimaryMagic(
			"large_stack_magic.ini");
	ok = check(
		gameManager.player->getAttack()
			== std::numeric_limits<int>::max()
			&& gameManager.player->getAttack2()
				== std::numeric_limits<int>::min()
			&& gameManager.player->getLifeMax()
				== std::numeric_limits<int>::max()
			&& std::isfinite(
				gameManager.player->getAdjustedWalkSpeed())
			&& gameManager.player->applyMagicEffectBonus(testMagic, 100)
				== std::numeric_limits<int>::max()
			&& largeStackMagic != nullptr
			&& largeStackMagic->hideCount
				== std::numeric_limits<int>::max(),
		"large no-need equipment stacks aggregate in constant time with saturated attributes") && ok;
	largeStackMagic = gameManager.magicManager.setMagicHidden(
		"large_stack_magic.ini", false, false, false);
	ok = check(
		largeStackMagic != nullptr
			&& largeStackMagic->hideCount
				== std::numeric_limits<int>::max(),
		"equipment magic reference counts saturate without signed overflow") && ok;
	largeStack.clear();
	gameManager.player->calInfo();
	ok = check(
		gameManager.player->getAttack() == 0
			&& gameManager.player->getAttack2() == 0
			&& gameManager.player->getLifeMax() == 100
			&& gameManager.magicManager.isMagicHidden(
				"large_stack_magic.ini"),
		"removing a saturated no-need equipment stack restores base attributes") && ok;
	gameManager.magicManager.clearPrimaryMagicList();

	auto partner = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->npcName = "TEST_PARTNER";
	partner->canEquip = 1;
	partner->level = 99;
	partner->lifeMax = 100;
	partner->thewMax = 80;
	partner->manaMax = 60;
	partner->life = 25;
	partner->thew = 30;
	partner->mana = 10;
	gameManager.npcManager->addNPC(partner);
	partner->setEquipmentFileByPartIndex(0, "vitality_head.ini");
	ok = check(
		partner->getLifeMax() == 140 && partner->life == 65
			&& partner->getThewMax() == 100 && partner->thew == 50
			&& partner->getManaMax() == 90 && partner->mana == 40,
		"equipping maximum attributes also adjusts a partner's current values") && ok;
	partner->setEquipmentFileByPartIndex(0, "");
	ok = check(
		partner->getLifeMax() == 100 && partner->life == 25
			&& partner->getThewMax() == 80 && partner->thew == 30
			&& partner->getManaMax() == 60 && partner->mana == 10,
		"partner maximum-attribute removal is symmetric") && ok;
	partner->headEquip = "vitality_head.ini";
	partner->updateEquipmentAttributes();
	ok = check(
		partner->getLifeMax() == 140 && partner->life == 25
			&& partner->getThewMax() == 100 && partner->thew == 30
			&& partner->getManaMax() == 90 && partner->mana == 10,
		"rebuilding loaded partner equipment preserves saved current values") && ok;
	partner->headEquip.clear();
	partner->updateEquipmentAttributes();

	const int bagIndex = gameManager.goodsManager.storeBegin();
	GoodsInfo& bag = gameManager.goodsManager.goodsList[bagIndex];
	std::string message;

	setPlayerGoods(bag, "head_a.ini", 1);
	partner->setEquipmentFileByPartIndex(0, "");
	ok = check(
		gameManager.partnerManager.exchangePlayerBagWithPartnerEquipment(
			partner, bagIndex, 0, true, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_a.ini"
		&& bag.iniFile.empty() && bag.number == 0,
		"precise transfer equips a singleton from the player bag") && ok;

	ok = check(
		gameManager.partnerManager.exchangePlayerBagWithPartnerEquipment(
			partner, bagIndex, 0, false, &message)
		&& partner->getEquipmentFileByPartIndex(0).empty()
		&& bag.iniFile == "head_a.ini" && bag.number == 1,
		"precise transfer unequips into the selected empty bag slot") && ok;

	setPlayerGoods(bag, "head_b.ini", 1);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	ok = check(
		gameManager.partnerManager.exchangePlayerBagWithPartnerEquipment(
			partner, bagIndex, 0, true, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_b.ini"
		&& bag.iniFile == "head_a.ini" && bag.number == 1,
		"precise transfer swaps two singleton head items in place") && ok;

	setPlayerGoods(bag, "head_b.ini", 2);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		!gameManager.partnerManager.exchangePlayerBagWithPartnerEquipment(
			partner, bagIndex, 0, true, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_a.ini"
		&& bag.iniFile == "head_b.ini" && bag.number == 2
		&& !message.empty(),
		"ambiguous stacked replacement is rejected transactionally") && ok;

	setPlayerGoods(bag, "head_a.ini", 3);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	ok = check(
		gameManager.partnerManager.exchangePlayerBagWithPartnerEquipment(
			partner, bagIndex, 0, false, &message)
		&& partner->getEquipmentFileByPartIndex(0).empty()
		&& bag.iniFile == "head_a.ini" && bag.number == 4,
		"unequipping onto an identical stack increments that exact stack") && ok;

	setPlayerGoods(bag, "body.ini", 1);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		!gameManager.partnerManager.exchangePlayerBagWithPartnerEquipment(
			partner, bagIndex, 0, true, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_a.ini"
		&& bag.iniFile == "body.ini" && bag.number == 1
		&& !message.empty(),
		"wrong-part equipment is rejected without mutating either side") && ok;

	fillPlayerCarryDomains(gameManager.goodsManager);
	setPlayerGoods(bag, "head_b.ini", 1);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		gameManager.partnerManager.equipOnePlayerGoodsOnPartner(
			partner, bagIndex, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_b.ini"
		&& bag.iniFile == "head_a.ini" && bag.number == 1,
		"direct equip reuses a consumed singleton source slot in a full bag") && ok;

	fillPlayerCarryDomains(gameManager.goodsManager);
	setPlayerGoods(bag, "head_b.ini", 2);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		!gameManager.partnerManager.equipOnePlayerGoodsOnPartner(
			partner, bagIndex, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_a.ini"
		&& bag.iniFile == "head_b.ini" && bag.number == 2
		&& !message.empty(),
		"direct equip keeps a full-bag stacked source replacement transactional") && ok;

	const int quickSlotIndex = gameManager.goodsManager.bottomBegin();
	GoodsInfo& quickSlot = gameManager.goodsManager.goodsList[quickSlotIndex];
	quickSlot.clear();
	setPlayerGoods(quickSlot, "head_b.ini", 1);
	partner->setEquipmentFileByPartIndex(0, "");
	message.clear();
	ok = check(
		gameManager.partnerManager.equipOnePlayerGoodsOnPartner(
			partner, quickSlotIndex, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_b.ini"
		&& quickSlot.iniFile.empty() && quickSlot.number == 0,
		"direct equip preserves the legacy quick-slot drag source path") && ok;

	const int returnedGoodsIndex = gameManager.goodsManager.storeBegin() + 1;
	gameManager.goodsManager.goodsList[returnedGoodsIndex].clear();
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		gameManager.partnerManager.unequipPartnerGoodsToPlayerBag(
			partner, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0).empty()
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].iniFile
			== "head_a.ini"
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].number == 1,
		"direct unequip returns partner equipment to an available player slot") && ok;

	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		gameManager.partnerManager.unequipPartnerGoodsToPlayerBag(
			partner, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0).empty()
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].iniFile
			== "head_a.ini"
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].number == 2,
		"stacking layouts merge an unequipped item into an identical stack") && ok;

	partner->setEquipmentFileByPartIndex(0, "head_b.ini");
	gameManager.npcManager->removeNPCOnlyFromList(partner);
	const GoodsInfo returnedGoodsBefore =
		gameManager.goodsManager.goodsList[returnedGoodsIndex];
	message.clear();
	ok = check(
		!gameManager.partnerManager.unequipPartnerGoodsToPlayerBag(
			partner, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_b.ini"
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].iniFile
			== returnedGoodsBefore.iniFile
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].number
			== returnedGoodsBefore.number
		&& !message.empty(),
		"direct unequip rejects an inactive partner without mutating either side") && ok;

	gameManager.npcManager->addNPC(partner);
	gameManager.global.goodsLayout.listType = 1;
	const int separateDestinationIndex = returnedGoodsIndex + 1;
	gameManager.goodsManager.goodsList[separateDestinationIndex].clear();
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	message.clear();
	ok = check(
		gameManager.partnerManager.unequipPartnerGoodsToPlayerBag(
			partner, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0).empty()
		&& gameManager.goodsManager.goodsList[returnedGoodsIndex].number == 2
		&& gameManager.goodsManager.goodsList[separateDestinationIndex].iniFile
			== "head_a.ini"
		&& gameManager.goodsManager.goodsList[separateDestinationIndex].number == 1,
		"item-per-slot layouts keep an unequipped item in a separate slot") && ok;

	fillPlayerCarryDomains(gameManager.goodsManager);
	partner->setEquipmentFileByPartIndex(0, "head_a.ini");
	const std::vector<GoodsInfo> fullBagBefore =
		gameManager.goodsManager.goodsList;
	message.clear();
	ok = check(
		!gameManager.partnerManager.unequipPartnerGoodsToPlayerBag(
			partner, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "head_a.ini"
		&& hasSameGoodsState(
			fullBagBefore, gameManager.goodsManager.goodsList)
		&& !message.empty(),
		"direct unequip leaves a full item-per-slot bag unchanged") && ok;

	gameManager.global.goodsLayout.listType = 0;
	gameManager.goodsManager.goodsList[returnedGoodsIndex].clear();
	partner->setEquipmentFileByPartIndex(0, "missing.ini");
	const std::vector<GoodsInfo> invalidGoodsBefore =
		gameManager.goodsManager.goodsList;
	message.clear();
	ok = check(
		!gameManager.partnerManager.unequipPartnerGoodsToPlayerBag(
			partner, 0, &message)
		&& partner->getEquipmentFileByPartIndex(0) == "missing.ini"
		&& hasSameGoodsState(
			invalidGoodsBefore, gameManager.goodsManager.goodsList)
		&& !message.empty(),
		"invalid equipment resources are rejected before either side mutates") && ok;

	return ok;
}
