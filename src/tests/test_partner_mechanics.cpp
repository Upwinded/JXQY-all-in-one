#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Config/Config.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "../Image/IMP.h"
#include "../JxqyEngineVersion.h"
#include "../Resource/ResourceManager.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
class TestNPC : public NPC
{
public:
	void tick(UTime milliseconds)
	{
		frameTime = milliseconds;
		setTime(getTime() + milliseconds);
		onUpdate();
	}
};

bool check(bool condition, const char* message)
{
	if (!condition) std::cerr << "FAILED: " << message << '\n';
	return condition;
}

bool write(const std::filesystem::path& path, const std::string& content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output << content;
	return output.good();
}

bool writeSave(const std::string& fileName, const std::string& content)
{
	return File::writeFileChecked(SaveFileManager::CurrentPath() + fileName,
		content.data(), static_cast<int>(content.size()));
}

std::shared_ptr<NPC> addPartner(GameManager& game, const std::string& name)
{
	auto partner = std::make_shared<NPC>();
	partner->npcName = name;
	partner->kind = nkPartner;
	partner->relation = nrFriendly;
	partner->life = partner->lifeMax = 100;
	partner->thew = partner->thewMax = 100;
	partner->mana = partner->manaMax = 100;
	partner->level = 1;
	partner->levelUpExp = 100;
	partner->npcLevelList.resize(3);
	for (int i = 0; i < 3; ++i)
	{
		partner->npcLevelList[i].lifeMax = 100 + i * 20;
		partner->npcLevelList[i].thewMax = 100 + i * 10;
		partner->npcLevelList[i].manaMax = 100 + i * 10;
		partner->npcLevelList[i].attack = 10 + i * 5;
		partner->npcLevelList[i].levelUpExp = (i + 1) * 100;
	}
	game.npcManager->addNPC(partner);
	return partner;
}

bool checkPartnerFollowWithDisabledAI(GameManager& game)
{
	const Point originalPlayerPosition = game.player->getPosition();
	game.player->setPosition({ 20, 10 });
	game.global.data.NPCAI = false;
	auto partner = addPartner(game, "AI follow partner");
	partner->setPosition({ 10, 10 });
	NPCActionRes movement;
	movement.imagePackage = std::make_shared<IMPImage>();
	movement.imagePackage->directions = 8;
	movement.imagePackage->interval = 100;
	movement.imagePackage->frame.resize(16);
	partner->res.stand = partner->res.walk = partner->res.run = movement;
	partner->setTime(1000);
	partner->beginStand();
	partner->nextFollowCheckTime = 1;
	auto enemy = std::make_shared<NPC>();
	enemy->kind = nkBattle;
	enemy->relation = nrHostile;
	enemy->npcName = "AI follow target";
	enemy->life = 100;
	enemy->setPosition({ 10, 12 });
	game.npcManager->addNPC(enemy);
	partner->currentCombatTarget = enemy;
	partner->lastCombatTarget = enemy;
	partner->lastKnownCombatTarget = enemy;
	partner->hasLastKnownCombatTargetPosition = true;
	partner->actionPlan.state = npsApproaching;
	partner->actionPlan.planTarget = enemy;
	partner->destGE = enemy;
	partner->actionManager->update(1);
	bool ok = check(!partner->isAIEnabled() && partner->isFollower()
		&& (partner->isWalking() || partner->isRunning()),
		"global AI disable preserves the partner's actual owner-follow movement");
	ok = check(partner->currentCombatTarget.expired() && partner->lastCombatTarget.expired()
		&& partner->lastKnownCombatTarget.expired() && !partner->actionPlan.isActive()
		&& partner->destGE.expired(),
		"disabled combat memory and destination cannot block partner following") && ok;
	partner->currentCombatTarget = enemy;
	partner->actionPlan.state = npsApproaching;
	partner->actionPlan.planTarget = enemy;
	const UTime stepTime = partner->stepLastTime * 2;
	partner->setTime(partner->getTime() + stepTime);
	partner->actionManager->update(stepTime);
	ok = check(partner->currentCombatTarget.expired() && !partner->actionPlan.isActive()
		&& !partner->stepList.empty()
		&& game.map->calDistance(partner->stepList.back(), game.player->getPosition())
			<= game.global.getPartnerFollowRadius(),
		"moving partners discard combat work and keep following with global AI disabled") && ok;
	if (!partner->stepList.empty())
	{
		const Point issuedDestination = partner->stepList.back();
		partner->setAIDisabled(true);
		game.player->setPosition({ 26, 18 });
		for (int step = 0; step < 32 && (partner->isWalking() || partner->isRunning()); ++step)
		{
			const UTime elapsed = partner->stepLastTime * 2;
			partner->setTime(partner->getTime() + elapsed);
			partner->actionManager->update(elapsed);
		}
		ok = check(partner->isStanding() && partner->getPosition() == issuedDestination,
			"local AI disable completes the issued follow path without replanning toward the moving owner") && ok;
		game.player->setPosition({ 20, 10 });
	}
	partner->stopMovement();
	partner->setPosition({ 10, 10 });
	partner->setAIDisabled(true);
	partner->nextFollowCheckTime = 1;
	partner->actionManager->update(1);
	ok = check(!partner->isFollowAIEnabled() && partner->isStanding(),
		"per-character AI disable prevents partner follow decisions") && ok;

	for (const bool localDisabled : { false, true })
	{
		partner->setAIDisabled(localDisabled);
		partner->haveAsyncDest = true;
		partner->gotoExDest = { 10, 18 };
		partner->beginWalk(partner->gotoExDest);
		ok = check(!partner->isFollowAIEnabled() && partner->isWalking(),
			"explicit asynchronous script movement bypasses both AI switches") && ok;
		for (int step = 0; step < 32 && partner->isWalking(); ++step)
		{
			const UTime elapsed = partner->stepLastTime * 2;
			partner->setTime(partner->getTime() + elapsed);
			partner->actionManager->update(elapsed);
		}
		ok = check(partner->getPosition() == partner->gotoExDest && partner->isStanding(),
			"partner following cannot replace the script's explicit destination") && ok;
		partner->haveAsyncDest = false;
		partner->setPosition({ 10, 10 });
	}
	partner->setAIDisabled(false);
	partner->destinationMapPosition = { 40, 40 };
	partner->fixedPathTilePositions = { { 10, 10 }, { 10, 18 } };
	partner->nextFollowCheckTime = 1;
	partner->actionManager->update(1);
	ok = check(partner->isFollowAIEnabled() && (partner->isWalking() || partner->isRunning())
		&& !partner->stepList.empty()
		&& game.map->calDistance(partner->stepList.back(), game.player->getPosition())
			<= game.global.getPartnerFollowRadius(),
		"stored destination and fixed-path settings preserve existing owner-follow priority") && ok;
	partner->stopMovement();
	partner->setPosition({ 10, 10 });
	partner->destinationMapPosition = { 0, 0 };
	partner->fixedPathTilePositions.clear();
	enemy->relation = nrFriendly;
	for (const int kind : { nkNormal, nkBattle })
	{
		partner->kind = kind;
		partner->relation = nrHostile;
		partner->followNPC = enemy->npcName;
		game.global.data.NPCAI = true;
		ok = check(partner->isFollower(), "AI follow gate has a real named target") && ok;
		game.global.data.NPCAI = false;
		partner->nextFollowCheckTime = 1;
		partner->actionManager->update(1);
		ok = check(!partner->isFollowAIEnabled() && partner->isStanding(),
			"global AI disable still blocks ordinary FollowNPC and enemy pursuit") && ok;
	}
	partner->kind = nkPartner;
	partner->relation = nrFriendly;
	partner->followNPC.clear();
	partner->setPosition({ 1, 1 });
	game.inEvent = true;
	ok = check(!partner->tryRegroupWithPlayer(), "AI-disabled regroup still respects script events") && ok;
	game.inEvent = false;
	partner->haveAsyncDest = true;
	ok = check(!partner->tryRegroupWithPlayer(), "regroup cannot replace asynchronous script movement") && ok;
	partner->haveAsyncDest = false;
	partner->fixedPathTilePositions = { { 1, 1 }, { 2, 2 } };
	ok = check(!partner->tryRegroupWithPlayer(), "regroup cannot replace a fixed path") && ok;
	partner->fixedPathTilePositions.clear();
	partner->setAIDisabled(true);
	ok = check(!partner->tryRegroupWithPlayer(), "per-character AI disable also blocks distant regroup") && ok;
	partner->setAIDisabled(false);
	auto control = std::make_shared<Effect>();
	game.player->beginControlCharacter(partner, control);
	ok = check(!partner->isFollowAIEnabled() && !partner->tryRegroupWithPlayer(),
		"controlled characters never use autonomous partner follow or regroup") && ok;
	game.player->endControlCharacter();
	ok = check(partner->tryRegroupWithPlayer()
		&& game.map->calDistance(partner->getPosition(), game.player->getPosition()) <= 3,
		"global AI disable preserves distant partner regroup") && ok;
	game.npcManager->removeNPCOnlyFromList(partner);
	game.npcManager->removeNPCOnlyFromList(enemy);
	game.player->setPosition(originalPlayerPosition);
	game.global.data.NPCAI = true;
	return ok;
}

bool checkSummonAttackPersistence(GameManager& game, const std::shared_ptr<NPC>& shared,
	const std::shared_ptr<NPC>& partnerOwner, int award)
{
	bool ok = true;
	auto magic = std::make_shared<Magic>();
	magic->initFromIni("summon-attack.ini");
	if (!check(magic->loadSucceeded, "summon attack persistence fixture loads")) return false;
	for (int route = 0; route < 6; ++route)
	{
		game.effectManager->freeResource();
		auto owner = route < 2 ? std::static_pointer_cast<NPC>(game.player)
			: route < 4 ? partnerOwner : addPartner(game, "DepartedSummoner");
		auto ownership = std::make_shared<Effect>();
		ownership->user = owner;
		ownership->magic.npcFile = "summon.ini";
		ownership->magic.maxCount = 10;
		ok = check(ownership->beginSummon({ 10, 10 }), "summon before saving its lingering attack") && ok;
		auto summon = ownership->summonedNPC.lock();
		if (summon == nullptr) continue;
		auto projectile = std::make_shared<Effect>();
		projectile->level = 1;
		projectile->user = summon;
		projectile->initFromMagic(magic);
		projectile->launcherKind = lkFriend;
		projectile->damage = 10000;
		game.effectManager->addEffect(projectile);
		game.effectManager->addDelayedMagic(magic, summon, { 10, 10 }, { 12, 12 },
			1, lkFriend, nullptr, 500);
		if (route % 2 == 0)
		{
			summon->res.death.imagePackage = std::make_shared<IMPImage>();
			summon->res.death.imagePackage->directions = 8;
			summon->res.death.imagePackage->interval = 100;
			summon->res.death.imagePackage->frame.resize(8);
			summon->life = 0;
			summon->beginDie();
		}
		else ownership->clearSummonedNPC(true);
		game.npcManager->removeNPCOnlyFromList(summon);
		ownership.reset();
		if (route == 0)
		{
			const auto snapshot = game.effectManager->captureCasterSnapshot(owner);
			game.effectManager->replaceCasterReferences(owner, snapshot);
			ok = check(snapshot != nullptr && summon->getExperienceOwner() == snapshot
				&& projectile->user.lock() == summon,
				"character switching snapshots an owner referenced only through a summon") && ok;
		}
		if (route == 2)
		{
			const auto snapshot = game.effectManager->captureCasterSnapshot(summon);
			ok = check(snapshot != nullptr && snapshot->getExperienceOwner() == owner,
				"standalone caster snapshots retain summon experience ownership") && ok;
		}
		if (route >= 4)
		{
			owner->life = 0;
			game.npcManager->removeNPCOnlyFromList(owner);
		}
		const std::string ownerName = owner->npcName;
		std::weak_ptr<NPC> originalSummon = summon;
		std::weak_ptr<NPC> originalOwner = owner;
		ok = check(game.effectManager->save(), "save lingering summon projectile to disk") && ok;
		projectile.reset();
		summon.reset();
		owner.reset();
		ok = check(game.effectManager->load() && originalSummon.expired()
			&& (route < 4 || originalOwner.expired()), "reload replaces original summon and detached owner objects") && ok;
		// A second disk round trip must retain the restored ownership as well.
		ok = check(game.effectManager->save() && game.effectManager->load(), "resave restored summon ownership") && ok;
		if (!check(game.effectManager->effectList.size() == 1, "one lingering attack survives reload"))
		{
			ok = false;
			continue;
		}
		projectile = game.effectManager->effectList.front();
		auto caster = std::dynamic_pointer_cast<NPC>(projectile->user.lock());
		auto restoredOwner = caster != nullptr ? caster->getExperienceOwner() : nullptr;
		ok = check(caster != nullptr && caster->npcName == "Summon"
			&& caster->summonedByMagicEffect.expired() && restoredOwner != nullptr
			&& restoredOwner != caster && restoredOwner->npcName == ownerName
			&& game.effectManager->getPendingDelayedMagicUser(0) == caster,
			"projectile and delayed attack preserve their actual caster and separate experience owner") && ok;
		const int playerBefore = game.player->exp;
		const int sharedBefore = shared->exp;
		auto victim = std::make_shared<NPC>();
		victim->kind = nkBattle;
		victim->relation = nrHostile;
		victim->life = 1;
		victim->exp = 10;
		victim->directHurt(projectile);
		victim->awardDefeatedNpcExperience(projectile);
		ok = check(victim->life <= 0 && game.player->exp == playerBefore + award
			&& shared->exp == sharedBefore + award,
			"summon exits, save/reload, then projectile kills: player and shared partner gain experience once") && ok;
	}
	game.effectManager->freeResource();
	for (int invalid = 0; invalid < 4; ++invalid)
	{
		INIReader saved(SaveFileManager::CurrentPath() + EFFECT_INI);
		ok = check(saved.GetInteger("Head", "DetachedCasterCount", 0) == 2,
			"detached owner uses the existing deduplicated caster collection") && ok;
		if (invalid == 0)
		{
			saved.Remove("DetachedCaster001", "SummonExperienceOwnerReferenceKind");
			saved.Remove("DetachedCaster001", "SummonExperienceOwnerReferenceIndex");
		}
		else
		{
			saved.SetInteger("DetachedCaster001", "SummonExperienceOwnerReferenceIndex",
				invalid == 1 ? 0 : invalid == 2 ? 999 : 1);
			if (invalid == 3)
			{
				saved.SetInteger("DetachedCaster002", "SummonExperienceOwnerReferenceKind", 4);
				saved.SetInteger("DetachedCaster002", "SummonExperienceOwnerReferenceIndex", 0);
			}
		}
		game.effectManager->loadFromIni(saved);
		if (!check(game.effectManager->effectList.size() == 1, "legacy or malformed owner references keep the attack loadable"))
		{
			ok = false;
			continue;
		}
		auto caster = std::dynamic_pointer_cast<NPC>(game.effectManager->effectList.front()->user.lock());
		ok = check(caster != nullptr && caster->getExperienceOwner() != nullptr
			&& (invalid == 3 || caster->summonExperienceOwner == nullptr),
			"missing, self, out-of-range and cyclic owner references do not create owner cycles") && ok;
		std::weak_ptr<NPC> casterProbe = caster;
		std::weak_ptr<NPC> ownerProbe = caster != nullptr ? caster->summonExperienceOwner : nullptr;
		caster.reset();
		game.effectManager->freeResource();
		ok = check(casterProbe.expired() && ownerProbe.expired(), "loaded owner graph is released after its last attack") && ok;
	}
	return ok;
}
}

bool runPartnerMechanicsTests()
{
	const int previousExperienceMultiplier = Config::experienceMultiplier;
	Config::experienceMultiplier = 3;
	// No renderer, window or game loop: exercise the real runtime objects directly.
	const auto root = makeUniqueTestDirectory("jxqy_partner_mechanics");
	File::setAssetsCollectionRoot(root.string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setPlatformStateParentForTests(root.string());
	File::setActiveSaveNamespace("partner-mechanics");
	bool ok = write(root / "partneridx.ini", "[Init]\n1=Registered\n2=Shared\n")
		&& write(root / "map/fixture.map", "headless map presence fixture")
		&& write(root / "ini/save/player1.ini", "[Init]\nName=Registered\nLevel=1\nLifeMax=100\nThewMax=100\nManaMax=100\nExp=0\nLevelUpExp=100\nAttack=10\nMoney=123\nCanRun=1\n")
		&& write(root / "ini/save/player2.ini", "[Init]\nName=Shared\nLevel=1\nLifeMax=100\nExp=0\nLevelUpExp=100\n")
		&& write(root / "ini/goods/head.ini", "[Init]\nName=Head\nKind=1\nPart=head\nLifeMax=40\nAttack=5\n")
		&& write(root / "ini/npc/summon.ini", "[Init]\nName=Summon\nKind=1\nRelation=0\nLife=100\nLifeMax=100\n")
		&& write(root / "ini/magic/summon-attack.ini", "[Init]\nName=SummonAttack\nMoveKind=2\nLifeFrame=100\n[Level1]\nMoveKind=2\nSpeed=10\n")
		&& write(root / "ini/level/missing-next.ini", "[Head]\nLevels=2\n[Level1]\nLifeMax=100\nLevelUpExp=100\n")
		&& write(root / "ini/level/empty-next.ini", "[Head]\nLevels=2\n[Level1]\nLifeMax=100\nLevelUpExp=100\n[Level2]\nLifeMax=0\n")
		&& write(root / "ini/level/last-level.ini", "[Head]\nLevels=2\n[Level1]\nLifeMax=100\nLevelUpExp=100\n[Level2]\nLifeMax=120\nLevelUpExp=0\n")
		&& write(root / "ini/npc/fresh.ini", "[Init]\nName=Fresh\nKind=3\nRelation=0\nLife=1\nLifeMax=90\nThew=0\nThewMax=80\nMana=0\nManaMax=70\n");
	{
		GameManager game;
		ok = check(SDL_Init(0) && SDL_WasInit(SDL_INIT_VIDEO) == 0, "initialize only SDL main-thread bookkeeping") && ok;
		game.global.data.mapName = "fixture.map";
		game.traps.resetToEmpty();
		game.varList.ensureInitialized();
		game.global.data.NPCAI = true;
		game.global.data.PartnerCombat = true;
		game.global.data.characterIndex = 0;
		game.inEvent = false;
		game.map->data = std::make_shared<MapData>();
		game.map->data->head.width = 64;
		game.map->data->head.height = 64;
		game.map->data->tile.assign(64, std::vector<MapTile>(64));
		game.map->dataMap.tile.assign(64, std::vector<DataTile>(64));
		game.player->setPosition({ 32, 32 });
		ok = checkPartnerFollowWithDisabledAI(game) && ok;
		auto partner = addPartner(game, "Registered");
		partner->life = partner->thew = partner->mana = 10;
		partner->updatePartnerRecovery(5000);
		ok = check(partner->life == 10, "recovery waits five seconds") && ok;
		partner->updatePartnerRecovery(1000);
		ok = check(partner->life == 12 && partner->thew == 14 && partner->mana == 13,
			"all three recovery rates use effective maxima") && ok;
		partner->poisoned = true;
		partner->updatePartnerRecovery(10000);
		ok = check(partner->life == 12, "poison pauses and resets recovery") && ok;
		partner->poisoned = false;
		game.inEvent = true;
		partner->updatePartnerRecovery(10000);
		ok = check(partner->mana == 13, "script events pause recovery") && ok;
		game.inEvent = false;
		for (bool* abnormal : { &partner->frozen, &partner->petrified })
		{
			*abnormal = true;
			partner->updatePartnerRecovery(10000);
			ok = check(partner->life == 12 && partner->thew == 14 && partner->mana == 13,
				"freeze and petrification pause all recovery") && ok;
			*abnormal = false;
			partner->updatePartnerRecovery(5000);
			ok = check(partner->life == 12, "clearing an abnormal state starts a fresh quiet period") && ok;
		}
		auto recoveryEnemy = std::make_shared<NPC>();
		recoveryEnemy->kind = nkBattle;
		recoveryEnemy->relation = nrHostile;
		recoveryEnemy->life = 100;
		partner->currentCombatTarget = recoveryEnemy;
		partner->updatePartnerRecovery(10000);
		ok = check(partner->life == 12, "a live combat target blocks recovery") && ok;
		partner->currentCombatTarget.reset();
		partner->haveAsyncDest = true;
		partner->updatePartnerRecovery(10000);
		ok = check(partner->life == 12, "script-directed movement blocks recovery") && ok;
		partner->haveAsyncDest = false;
		game.global.data.NPCAI = false;
		partner->updatePartnerRecovery(10000);
		ok = check(partner->life == 12, "disabled NPC AI does not recover") && ok;
		game.global.data.NPCAI = true;
		partner->life = partner->thew = partner->mana = 99;
		partner->updatePartnerRecovery(15000);
		ok = check(partner->life == 100 && partner->thew == 100 && partner->mana == 100,
			"long game frames clamp all recovery to effective maxima") && ok;
		partner->life = 99;
		partner->updatePartnerRecovery(0);
		ok = check(partner->life == 99, "paused game time and cleared full-value fractions cannot heal") && ok;
		partner->lifeMax = 3;
		partner->life = 1;
		partner->resetPartnerRecovery();
		partner->updatePartnerRecovery(5000);
		for (int i = 0; i < 20; ++i) partner->updatePartnerRecovery(1000);
		ok = check(partner->life == 2, "fractional recovery works for low maxima") && ok;
		partner->lifeMax = 100;
		partner->kind = nkNormal;
		ok = check(partner->setKindAndRelation(nkPartner, nrFriendly)
			&& partner->life == 100 && partner->thew == 100 && partner->mana == 100, "joining refills resources") && ok;
		partner->life = 25;
		partner->setKindAndRelation(nkPartner, nrFriendly);
		ok = check(partner->life == 25, "repeating membership does not heal") && ok;
		partner->setKindAndRelation(nkNormal, nrFriendly);
		partner->setKindAndRelation(nkPartner, nrFriendly);
		ok = check(partner->life == 100, "real rejoining refills again") && ok;
		partner->life = 0;
		partner->finishPartnerJoin();
		ok = check(partner->life == 0, "joining never revives a dead partner") && ok;
		partner->life = 25;
		game.npcManager->addNPC("fresh.ini", 10, 10, 0);
		const auto fresh = game.npcManager->findNPC("Fresh").front();
		ok = check(fresh->life == 90 && fresh->mana == 70 && fresh->thew == 80, "resource spawn is a real join") && ok;
		ok = check(game.partnerManager.save(0) && game.partnerManager.load(0), "ordinary partner save/load succeeds") && ok;
		partner = game.npcManager->findNPC("Registered").front();
		ok = check(partner->life == 25, "ordinary reload preserves current resources") && ok;
		partner->setPosition({ 1, 1 });
		game.inEvent = true;
		ok = check(!partner->tryRegroupWithPlayer(), "regroup cannot bypass scripts") && ok;
		game.inEvent = false;
		ok = check(!partner->canDoAction(acWalk) && !partner->canDoAction(acAWalk)
			&& partner->tryRegroupWithPlayer()
			&& game.map->calDistance(partner->getPosition(), game.player->getPosition()) <= 3, "distant normal follower regroups") && ok;
		partner->setPosition({ 1, 1 });
		partner->disableMoveMilliseconds = 1000;
		ok = check(!partner->tryRegroupWithPlayer(), "regroup still respects an explicit movement lock") && ok;
		partner->disableMoveMilliseconds = 0;
		partner->isPartnerBlockingPlayer = true;
		ok = check(!partner->tryRegroupWithPlayer(), "stationary partner is not teleported") && ok;
		partner->isPartnerBlockingPlayer = false;
		game.global.feature.restorePartnerAttributes = true;
		partner->canEquip = 0;
		ok = check(partner->canChangePartnerEquipment(), "registered partner can equip without CanEquip") && ok;
		const int bagIndex = game.goodsManager.storeBegin();
		auto& item = game.goodsManager.goodsList[bagIndex];
		item.iniFile = "head.ini";
		item.number = 1;
		item.goods = std::make_shared<Goods>();
		item.goods->initFromIni("head.ini");
		ok = check(game.partnerManager.equipOnePlayerGoodsOnPartner(partner, bagIndex, 0), "registered partner equips inventory item") && ok;
		partner->exp = 45;
		partner->npcLevelIni = "level-test.ini";
		ok = check(game.partnerManager.saveCharacterSnapshots(), "character and equipment snapshots save") && ok;
		INIReader character(SaveFileManager::CurrentPath() + "player1.ini");
		INIReader equipment(SaveFileManager::CurrentPath() + "goods1.ini");
		ok = check(character.GetInteger("Init", "Exp", 0) == 45 && character.GetInteger("Init", "Money", 0) == 123
			&& character.GetInteger("Init", "LifeMax", 0) == 100
			&& equipment.Get(std::to_string(game.goodsManager.equipIndex(0) + 1), "IniFile", "") == "head.ini",
			"snapshot preserves player-only fields and excludes equipment maxima") && ok;
		partner->setKindAndRelation(nkNormal, nrFriendly);
		partner->exp = 9999;
		partner->headEquip.clear();
		partner->updateEquipmentAttributes(false);
		partner->setKindAndRelation(nkPartner, nrFriendly);
		ok = check(partner->exp == 45 && partner->headEquip == "head.ini" && partner->life == 140,
			"same-level rejoin restores experience and equipment, then refills") && ok;
		partner->life = 30;
		ok = check(game.partnerManager.save(0) && game.partnerManager.load(0), "new-sword partner reload") && ok;
		partner = game.npcManager->findNPC("Registered").front();
		ok = check(partner->life == 30 && partner->getLifeMax() == 140 && partner->exp == 45,
			"new-sword reload neither heals nor doubles equipment") && ok;
		const std::string headSlot = std::to_string(game.goodsManager.equipIndex(0) + 1);
		INIReader stackedEquipment = equipment;
		stackedEquipment.SetInteger(headSlot, "Number", 4);
		ok = check(stackedEquipment.saveToFile(SaveFileManager::CurrentPath() + "goods1.ini")
			&& game.partnerManager.saveCharacterSnapshots(), "snapshot migrates legacy stacked companion equipment") && ok;
		INIReader normalizedEquipment(SaveFileManager::CurrentPath() + "goods1.ini");
		const std::string firstBagSlot = std::to_string(game.goodsManager.storeBegin() + 1);
		ok = check(normalizedEquipment.GetInteger(headSlot, "Number", 0) == 1
			&& normalizedEquipment.Get(firstBagSlot, "IniFile", "") == "head.ini"
			&& normalizedEquipment.GetInteger(firstBagSlot, "Number", 0) == 3
			&& partner->life == 30, "companion snapshot preserves surplus copies and current life") && ok;
		for (int i = game.goodsManager.storeBegin(); i <= game.goodsManager.storeEnd(); ++i)
		{
			stackedEquipment.Set(std::to_string(i + 1), "IniFile", "occupied.ini");
			stackedEquipment.SetInteger(std::to_string(i + 1), "Number", 1);
		}
		ok = check(stackedEquipment.saveToFile(SaveFileManager::CurrentPath() + "goods1.ini")
			&& game.partnerManager.saveCharacterSnapshots(), "a full companion record retains its old surplus while saving") && ok;
		INIReader fullEquipment(SaveFileManager::CurrentPath() + "goods1.ini");
		ok = check(fullEquipment.GetInteger(headSlot, "Number", 0) == 4
			&& !game.partnerManager.unequipPartnerGoodsToPlayerBag(partner, 0)
			&& partner->headEquip == "head.ini" && game.goodsManager.getItemNum("head.ini") == 0,
			"unresolved companion surplus is preserved and blocks unequip before inventory changes") && ok;
		ok = equipment.saveToFile(SaveFileManager::CurrentPath() + "goods1.ini") && ok;
		for (int firstOperation = 0; firstOperation < 2; ++firstOperation)
		{
			INIReader legacy;
			legacy.SetInteger("Head", "Count", 1);
			partner->saveToIni(&legacy, "Partner000");
			legacy.Set("Partner000", "HeadEquip", "");
			legacy.SetInteger("Partner000", "Life", 30);
			legacy.SetInteger("Partner000", "Thew", 21);
			legacy.SetInteger("Partner000", "Mana", 12);
			ok = legacy.saveToFile(SaveFileManager::CurrentPath() + "partner0.ini") && ok;
			ok = writeSave(GLOBAL_INI, "[Save]\nEngineVersion=1.1.4\n") && ok;
			ok = check(game.partnerManager.load(0), "legacy companion load imports the character equipment record") && ok;
			partner = game.npcManager->findNPC("Registered").front();
			ok = check(partner->headEquip == "head.ini" && partner->getLifeMax() == 140
				&& partner->life == 30 && partner->thew == 21 && partner->mana == 12,
				"legacy equipment migration preserves all current resource values") && ok;
			if (firstOperation == 0)
			{
				ok = check(game.partnerManager.save(0), "first legacy save succeeds") && ok;
			}
			else
			{
				game.scriptAPI.playerChange(1);
				ok = check(game.global.data.characterIndex == 1 && game.goodsManager.getItemNum("head.ini") == 1,
					"first legacy protagonist change retains exactly one equipped item") && ok;
				game.scriptAPI.playerChange(0);
			}
			INIReader savedEquipment(SaveFileManager::CurrentPath() + "goods1.ini");
			ok = check(savedEquipment.Get(headSlot, "IniFile", "") == "head.ini"
				&& savedEquipment.GetInteger(headSlot, "Number", 0) == 1,
				"first legacy writeback never clears equipment") && ok;
		}
		INIReader incompleteLegacy(SaveFileManager::CurrentPath() + "partner0.ini");
		incompleteLegacy.SetInteger("Partner000", "Level", 0);
		ok = incompleteLegacy.saveToFile(SaveFileManager::CurrentPath() + "partner0.ini") && ok;
		ok = check(game.partnerManager.load(0), "legacy attributes and equipment can both be restored") && ok;
		partner = game.npcManager->findNPC("Registered").front();
		ok = check(partner->level == 1 && partner->life == 140 && partner->thew == 100 && partner->mana == 100,
			"actual attribute restoration refills only after legacy equipment is included") && ok;
		ok = writeSave("goods1.ini", "[Head]\nCount=1\n[" + headSlot + "]\nIniFile=missing.ini\nNumber=1\n") && ok;
		ok = check(game.partnerManager.load(0), "invalid legacy equipment retains the companion instead of discarding the party") && ok;
		partner = game.npcManager->findNPC("Registered").front();
		ok = check(!partner->canChangePartnerEquipment() && !game.partnerManager.saveCharacterSnapshots(),
			"unresolved legacy equipment blocks destructive writeback and inventory transfers") && ok;
		const auto execute = [&](const std::string& source)
		{
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			return game.script.runScript(bytes, static_cast<int>(source.size()));
		};
		for (const std::string& command : { "setnpckind('Registered',1)", "setnpcrelation('Registered',1)",
			"setnpckind('',1)", "addnpcproperty('Registered','Kind',-2)" })
		{
			game.scriptNPC = partner;
			game.varList.setInteger("DepartureContinued", 0);
			ok = check(execute(command + "; assign('DepartureContinued',1)") != LUA_OK
				&& game.varList.getInteger("DepartureContinued") == 0
				&& partner->kind == nkPartner && partner->relation == nrFriendly,
				"failed partner writeback stops the Lua scene and preserves membership") && ok;
		}
		game.scriptNPC.reset();
		game.varList.setInteger("DepartureContinued", 0);
		ok = check(execute("pcall(setnpckind,'Registered',1); assign('DepartureContinued',1)") != LUA_OK
			&& game.varList.getInteger("DepartureContinued") == 0,
			"catching a departure error cannot execute another registered command") && ok;
		ok = check(execute("pcall(setnpckind,'Registered',1); return") != LUA_OK,
			"catching a departure error cannot report the scene as completed") && ok;
		ok = write(root / "script/common/departure-child.txt", "setnpckind('Registered',1)") && ok;
		for (const std::string& command : { "runscript('departure-child.txt')",
			"randrun('DepartureChance','departure-child.txt','departure-child.txt')" })
		{
			game.varList.setInteger("DepartureContinued", 0);
			ok = check(execute(command + "; assign('DepartureContinued',1)") != LUA_OK
				&& game.varList.getInteger("DepartureContinued") == 0,
				"failed departure in a child script also stops its parent scene") && ok;
		}
		ok = check(execute("assign('AfterDepartureFailure',1)") == LUA_OK
			&& game.varList.getInteger("AfterDepartureFailure") == 1,
			"a failed departure does not poison the next independent script") && ok;
		ok = check(!game.saveGame(0), "unresolved equipment also rejects a complete save") && ok;
		INIReader rejectedSave(SaveFileManager::CurrentPath() + GLOBAL_INI);
		ok = check(rejectedSave.Get("Save", "EngineVersion", "") == "1.1.4",
			"failed migration never advances metadata past the legacy equipment boundary") && ok;
		ok = equipment.saveToFile(SaveFileManager::CurrentPath() + "goods1.ini") && ok;
		ok = check(game.partnerManager.load(0), "valid legacy equipment can be loaded again") && ok;
		partner = game.npcManager->findNPC("Registered").front();
		ok = check(game.saveGame(0), "complete save succeeds after legacy equipment is restored") && ok;
		INIReader migratedSave(SaveFileManager::CurrentPath() + GLOBAL_INI);
		ok = check(migratedSave.Get("Save", "EngineVersion", "") == JxqyBuildVersion::EngineVersion, "successful migration advances existing save metadata") && ok;
		ok = check(game.partnerManager.unequipPartnerGoodsToPlayerBag(partner, 0)
			&& game.partnerManager.save(0) && game.partnerManager.load(0), "deliberately unequipped new save reloads") && ok;
		partner = game.npcManager->findNPC("Registered").front();
		ok = check(partner->headEquip.empty() && game.goodsManager.getItemNum("head.ini") == 1,
			"new saves do not resurrect removed equipment or duplicate inventory") && ok;
		ok = check(game.partnerManager.equipOnePlayerGoodsOnPartner(partner, bagIndex, 0), "restore equipment for subsequent round trips") && ok;
		auto shared = addPartner(game, "Shared");
		auto killer = addPartner(game, "Killer");
		killer->canLevelUp = 1;
		auto other = addPartner(game, "Other");
		other->canLevelUp = 1;
		game.player->levelUpExp = 100000;
		game.player->exp = 0;
		auto enemy = std::make_shared<NPC>();
		enemy->kind = nkBattle;
		enemy->relation = nrHostile;
		enemy->exp = 10;
		enemy->awardDefeatedNpcExperience(nullptr, killer);
		const int award = game.player->exp;
		ok = check(award > 0 && killer->exp == award && other->exp == 0 && shared->exp == award,
			"registered party shares once while only the eligible unregistered killer grows") && ok;
		enemy->awardDefeatedNpcExperience(nullptr, killer);
		ok = check(game.player->exp == award, "same death cannot award twice") && ok;
		game.global.feature.restorePartnerAttributes = false;
		enemy->defeatedExperienceAwarded = false;
		killer->canLevelUp = 0;
		enemy->awardDefeatedNpcExperience(nullptr, killer);
		ok = check(killer->exp == award && shared->exp == award && game.player->exp == award * 2,
			"common mode respects CanLevelUp and has no team sharing") && ok;
		killer->canLevelUp = 1;
		for (int route = 0; route < 4; ++route)
		{
			auto victim = std::make_shared<TestNPC>();
			victim->kind = nkBattle;
			victim->relation = nrHostile;
			victim->life = 1;
			victim->lifeMax = 100;
			victim->exp = 10;
			victim->noAddBody = true;
			auto effect = std::make_shared<Effect>();
			effect->user = killer;
			effect->launcherKind = lkFriend;
			effect->damage = 10000;
			effect->level = 1;
			const int before = game.player->exp;
			if (route == 0) victim->hurt(effect, 0);
			else if (route == 1) victim->directHurt(effect);
			else if (route == 2)
			{
				victim->poisoned = true;
				victim->poisonedLastTime = 1000;
				victim->poisonedBy = killer;
				victim->tick(250);
			}
			else
			{
				auto summon = std::make_shared<NPC>();
				auto ownership = std::make_shared<Effect>();
				ownership->user = killer;
				summon->summonedByMagicEffect = ownership;
				effect->user = summon;
				victim->directHurt(effect);
			}
			ok = check(victim->life <= 0 && game.player->exp == before + award,
				"normal, direct, poison and summoned kills share reward attribution") && ok;
			victim->awardDefeatedNpcExperience(effect);
			ok = check(game.player->exp == before + award, "lethal damage settles only once") && ok;
		}
		auto fallen = addPartner(game, "Fallen");
		fallen->life = 0;
		game.npcManager->removeNPCOnlyFromList(fallen);
		auto lingering = std::make_shared<Effect>();
		lingering->user = fallen;
		lingering->launcherKind = lkFriend;
		auto lingeringVictim = std::make_shared<NPC>();
		lingeringVictim->relation = nrHostile;
		lingeringVictim->exp = 10;
		const int beforeLingering = game.player->exp;
		lingeringVictim->awardDefeatedNpcExperience(lingering);
		ok = check(game.player->exp == beforeLingering + award && fallen->exp == 0,
			"a fallen partner's outstanding attack still credits the player without growing the dead partner") && ok;
		fallen->kind = nkBattle;
		lingeringVictim->defeatedExperienceAwarded = false;
		lingeringVictim->awardDefeatedNpcExperience(lingering);
		ok = check(game.player->exp == beforeLingering + award, "ordinary friendly fighters are not companion killers") && ok;
		game.global.feature.restorePartnerAttributes = true;
		shared->levelUpExp = 100000;
		for (int route = 0; route < 4; ++route)
		{
			auto ownership = std::make_shared<Effect>();
			ownership->user = route < 2 ? std::static_pointer_cast<NPC>(game.player) : killer;
			ownership->magic.npcFile = "summon.ini";
			ownership->magic.maxCount = 10;
			ok = check(ownership->beginSummon({ 10, 10 }), "create a real summon for delayed rewards") && ok;
			auto summon = ownership->summonedNPC.lock();
			if (summon == nullptr) continue;
			auto projectile = std::make_shared<Effect>();
			projectile->user = summon;
			projectile->launcherKind = lkFriend;
			projectile->damage = 10000;
			projectile->level = 1;
			auto poisonVictim = std::make_shared<TestNPC>();
			poisonVictim->kind = nkBattle;
			poisonVictim->relation = nrHostile;
			poisonVictim->life = 1;
			poisonVictim->lifeMax = 100;
			poisonVictim->exp = 10;
			poisonVictim->noAddBody = true;
			poisonVictim->poisoned = true;
			poisonVictim->poisonedLastTime = 1000;
			poisonVictim->rememberPoisonSource(summon);
			if (route % 2 == 1)
			{
				INIReader poisonSave;
				poisonVictim->saveToIni(&poisonSave, "Victim");
				poisonVictim = std::make_shared<TestNPC>();
				poisonVictim->initFromIni(&poisonSave, "Victim");
				ok = check(!poisonVictim->poisonedByCharacterName.empty(),
					"poison persistence records the summon owner rather than the temporary summon") && ok;
			}
			if (route % 2 == 0)
			{
				summon->res.death.imagePackage = std::make_shared<IMPImage>();
				summon->res.death.imagePackage->directions = 8;
				summon->res.death.imagePackage->interval = 100;
				summon->res.death.imagePackage->frame.resize(8);
				summon->life = 0;
				summon->beginDie();
			}
			else ownership->clearSummonedNPC(true);
			game.npcManager->removeNPCOnlyFromList(summon);
			ownership.reset();
			ok = check(summon->summonedByMagicEffect.expired() && projectile->user.lock() == summon,
				"summon cleanup removes the live link without replacing the actual attack caster") && ok;
			const int playerBefore = game.player->exp;
			const int partnerBefore = shared->exp;
			auto projectileVictim = std::make_shared<NPC>();
			projectileVictim->kind = nkBattle;
			projectileVictim->relation = nrHostile;
			projectileVictim->life = 1;
			projectileVictim->exp = 10;
			projectileVictim->directHurt(projectile);
			projectile.reset();
			summon.reset();
			poisonVictim->tick(250);
			if (game.player->exp != playerBefore + award * 2 || shared->exp != partnerBefore + award * 2)
			{
				std::cerr << "summon route=" << route << " player delta=" << game.player->exp - playerBefore
					<< " partner delta=" << shared->exp - partnerBefore << " expected=" << award * 2
					<< " poison source=" << poisonVictim->poisonedByCharacterName << '\n';
			}
			ok = check(game.player->exp == playerBefore + award * 2 && shared->exp == partnerBefore + award * 2,
				"player and partner summons retain projectile and poison rewards after death or expiration") && ok;
		}
		ok = checkSummonAttackPersistence(game, shared, killer, award) && ok;
		killer->attack = 50;
		killer->exp = 0;
		killer->level = 1;
		killer->levelUpExp = 100;
		killer->lifeMax = 100;
		killer->addPartnerExperience(250);
		ok = check(killer->level == 3 && killer->attack == 60 && killer->life == 140,
			"multiple level gains preserve bonuses and refill resources") && ok;
		killer->npcLevelList.clear();
		killer->level = 1;
		killer->addPartnerExperience(1000);
		ok = check(killer->level == 1, "missing level table does not force upgrades") && ok;
		for (const auto* table : { "missing-next.ini", "empty-next.ini", "last-level.ini" })
		{
			killer->level = 1;
			killer->levelUpExp = 100;
			killer->exp = 0;
			killer->lifeMax = 100;
			killer->life = 17;
			killer->loadLevel(table);
			killer->addPartnerExperience(250);
			const bool validLastLevel = std::string(table) == "last-level.ini";
			ok = check(validLastLevel ? (killer->level == 2 && killer->life == 120 && killer->levelUpExp == 0)
				: (killer->level == 1 && killer->life == 17),
				"missing or empty target records never upgrade or refill; a valid final level may have a zero next threshold") && ok;
		}
		auto healer = addPartner(game, "Healer");
		healer->life = 10;
		healer->res.magic.imagePackage = std::make_shared<IMPImage>();
		healer->res.magic.imagePackage->directions = 8;
		healer->res.magic.imagePackage->interval = 100;
		healer->res.magic.imagePackage->frame.resize(8);
		healer->magicToUseWhenLifeLow = std::make_shared<Magic>();
		healer->magicToUseWhenLifeLow->loadSucceeded = true;
		healer->magicToUseWhenLifeLow->level[1].moveKind = mmkPoint;
		game.global.data.PartnerCombat = false;
		ok = check(!game.npcManager->scheduleBattleAction(healer), "healer respects partner combat switch") && ok;
		game.global.data.PartnerCombat = true;
		ok = check(game.npcManager->scheduleBattleAction(healer) && healer->isMagicing(),
			"configured low-life spell works without normal attack assets") && ok;
		game.global.feature.restorePartnerAttributes = true;
		partner->life = 10;
		partner->thew = 0;
		partner->mana = 0;
		auto& drug = game.goodsManager.goodsList[bagIndex];
		drug.iniFile = "drug.ini";
		drug.number = 2;
		drug.goods = std::make_shared<Goods>();
		drug.goods->kind = gkDrug;
		drug.goods->life = 20;
		drug.goods->thew = 10;
		drug.goods->mana = 15;
		drug.goods->lifeMax = 5;
		const int previousMaximum = partner->lifeMax;
		game.player->life = game.player->getLifeMax();
		ok = check(game.goodsManager.useItem(bagIndex) && drug.number == 1
			&& partner->life == 30 && partner->thew == 10 && partner->mana == 15 && partner->lifeMax == previousMaximum,
			"full player can share instant medicine without duplicating permanent gains") && ok;
		drug.goods->followPartnerHasDrugEffect = 1;
		ok = check(game.goodsManager.useItem(bagIndex) && partner->life == 50 && partner->lifeMax == previousMaximum + 5,
			"explicit medicine sharing retains permanent effects without duplicate application") && ok;
		for (int effect = 1; effect <= 3; ++effect)
		{
			drug.iniFile = "cure.ini";
			drug.number = 1;
			drug.goods = std::make_shared<Goods>();
			drug.goods->kind = gkDrug;
			drug.goods->effectType = effect;
			drug.goods->fighterFriendHasDrugEffect = 1;
			drug.goods->followPartnerHasDrugEffect = 1;
			partner->frozen = partner->poisoned = partner->petrified = true;
			ok = check(game.goodsManager.useItem(bagIndex) && !game.goodsManager.goodsListExists(bagIndex)
				&& partner->frozen == (effect != 1) && partner->poisoned == (effect != 2)
				&& partner->petrified == (effect != 3), "one shared medicine cures only its configured state") && ok;
		}
		partner->frozen = partner->poisoned = partner->petrified = false;
		const int partnerMaximum = partner->getLifeMax();
		const int partnerAttack = partner->getAttack();
		partner->setKindAndRelation(nkNormal, nrFriendly);
		game.npcManager->removeNPCOnlyFromList(partner);
		game.scriptAPI.playerChange(1);
		ok = check(game.global.data.characterIndex == 1 && game.player->exp == partner->exp
			&& game.player->getLifeMax() == partnerMaximum && game.player->getAttack() == partnerAttack
			&& game.goodsManager.getItemNum("head.ini") == 1,
			"changing to a departed partner restores grown attributes and one copy of equipment") && ok;
		game.player->exp += 7;
		const int switchedExperience = game.player->exp;
		game.scriptAPI.playerChange(0);
		game.npcManager->addNPC(partner);
		partner->setKindAndRelation(nkPartner, nrFriendly);
		ok = check(game.global.data.characterIndex == 0 && partner->exp == switchedExperience
			&& partner->getLifeMax() == partnerMaximum && partner->headEquip == "head.ini",
			"returning from protagonist retains same-level experience and equipment") && ok;
		game.npcManager->freeResource();
	}
	{
		GameManager game;
		game.global.data.characterIndex = 0;
		auto neutral = addPartner(game, "Neutral");
		neutral->setKindAndRelation(nkPartner, nrNeutral);
		neutral->canEquip = 1;
		neutral->canLevelUp = 1;
		neutral->life = neutral->thew = neutral->mana = 10;
		ok = check(neutral->canChangePartnerEquipment() && !neutral->isLivingPartner(),
			"explicit neutral equipment permission is independent from friendly partner mechanics") && ok;
		neutral->updatePartnerRecovery(15000);
		neutral->addPartnerExperience(1000);
		ok = check(neutral->life == 10 && neutral->thew == 10 && neutral->mana == 10
			&& neutral->exp == 0 && neutral->level == 1,
			"allowing neutral equipment does not grant recovery or partner experience") && ok;
		neutral->canEquip = 0;
		ok = check(!neutral->canChangePartnerEquipment(), "neutral followers without equipment permission remain read-only") && ok;
		neutral->canEquip = 1;
		neutral->headEquip = "head.ini";
		neutral->updateEquipmentAttributes(false);
		game.partnerManager.extractPartnerListFromNPCManager();
		ok = check(game.partnerManager.findPartnersFromNPCManager().empty(), "neutral partner is carried outside the map list") && ok;
		game.partnerManager.transferPartnerListToNPCManager();
		ok = check(game.partnerManager.isActivePartner(neutral) && neutral->relation == nrNeutral
			&& neutral->headEquip == "head.ini", "neutral transfer preserves identity, relation and equipment") && ok;
		ok = check(game.partnerManager.save(0) && game.partnerManager.load(0), "neutral equipped companion saves and reloads") && ok;
		const auto reloaded = game.partnerManager.findPartnersFromNPCManager();
		ok = check(reloaded.size() == 1 && reloaded.front()->relation == nrNeutral
			&& reloaded.front()->canChangePartnerEquipment() && reloaded.front()->headEquip == "head.ini",
			"neutral equipment permission and worn item survive the existing partner format") && ok;
		if (!reloaded.empty())
		{
			neutral = reloaded.front();
			game.global.feature.restorePartnerAttributes = true;
			neutral->npcName = "Registered";
			neutral->canEquip = 0;
			ok = check(neutral->getPartnerCharacterIndex() == 1 && !neutral->canChangePartnerEquipment(),
				"registration alone does not enable equipment on a neutral follower") && ok;
			ok = writeSave("player1.ini", "[Init]\nName=Registered\nLevel=20\nLife=400\nLifeMax=500\nAttack=222\nMoney=777\n") && ok;
			ok = writeSave("goods1.ini", "[Head]\nCount=0\n") && ok;
			neutral->canEquip = 1;
			ok = check(game.partnerManager.saveCharacterSnapshot(neutral), "neutral registered equipment record saves") && ok;
			INIReader character(SaveFileManager::CurrentPath() + "player1.ini");
			INIReader equipment(SaveFileManager::CurrentPath() + "goods1.ini");
			const std::string headSlot = std::to_string(game.goodsManager.equipIndex(0) + 1);
			ok = check(equipment.Get(headSlot, "IniFile", "") == "head.ini"
				&& equipment.GetInteger(headSlot, "Number", 0) == 1
				&& character.GetInteger("Init", "Level", 0) == 20
				&& character.GetInteger("Init", "Life", 0) == 400
				&& character.GetInteger("Init", "Attack", 0) == 222
				&& character.GetInteger("Init", "Money", 0) == 777,
				"neutral equipment writes back without replacing the registered character's attributes") && ok;
			neutral->headEquip.clear();
			neutral->updateEquipmentAttributes(false);
			ok = check(neutral->setKindAndRelation(nkPartner, nrFriendly) && neutral->headEquip.empty()
				&& neutral->level == 20, "becoming friendly retains a neutral companion's deliberate unequip") && ok;
		}
		game.npcManager->freeResource();
		game.global.feature.restorePartnerAttributes = true;
		game.global.data.characterIndex = 0;
		auto multiplierShared = addPartner(game, "Shared");
		auto multiplierKiller = addPartner(game, "MultiplierKiller");
		auto multiplierOther = addPartner(game, "MultiplierOther");
		multiplierKiller->canLevelUp = multiplierOther->canLevelUp = 1;
		multiplierKiller->level = 2;
		multiplierShared->levelUpExp = multiplierKiller->levelUpExp = 1000000;
		game.player->level = 3;
		game.player->levelUpExp = 1000000;
		for (int multiplier : { 1, 2, 3 })
		{
			Config::experienceMultiplier = multiplier;
			for (int storedExperience : { 10, 0 })
			{
				for (bool partnerKill : { false, true })
				{
					game.player->exp = multiplierShared->exp = multiplierKiller->exp = multiplierOther->exp = 0;
					auto victim = std::make_shared<NPC>();
					victim->kind = nkBattle;
					victim->relation = nrHostile;
					victim->level = 4;
					victim->exp = storedExperience;
					auto owner = partnerKill ? multiplierKiller : std::static_pointer_cast<NPC>(game.player);
					victim->awardDefeatedNpcExperience(nullptr, owner);
					const int playerAward = (storedExperience == 0 ? 12 : 10) * multiplier;
					const int killerAward = partnerKill ? (storedExperience == 0 ? 8 : 10) * multiplier : 0;
					ok = check(game.player->exp == playerAward && multiplierShared->exp == playerAward &&
						multiplierKiller->exp == killerAward && multiplierOther->exp == 0,
						"player choices scale shared and killer-owned rewards using each recipient's fallback level") && ok;
					victim->awardDefeatedNpcExperience(nullptr, owner);
					ok = check(game.player->exp == playerAward && multiplierShared->exp == playerAward &&
						multiplierKiller->exp == killerAward,
						"each multiplier still settles a defeated NPC only once") && ok;
				}
			}
		}
		game.npcManager->freeResource();
	}
	Config::experienceMultiplier = previousExperienceMultiplier;
	File::setActiveSaveNamespace("");
	File::setPlatformStateParentForTests("");
	std::error_code error;
	std::filesystem::remove_all(root, error);
	return ok;
}
