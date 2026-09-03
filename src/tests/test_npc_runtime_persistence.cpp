#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Engine/Engine.h"
#include "../Game/Data/Map.h"
#include "../Game/Data/MemoPersistence.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCManager.h"
#include "../Game/Data/NPCPersistence.h"
#include "../Game/Data/ObjectManager.h"
#include "../Game/Data/PartnerManager.h"
#include "../Game/Data/Player.h"
#include "../Game/Data/Effect.h"
#include "../Game/GameManager/GameManager.h"
#include "../Image/IMP.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr char NpcPersistenceSaveNamespace[] =
	"npc-runtime-persistence";

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

std::filesystem::path saveGameFixturePath(
	const std::filesystem::path& root,
	const std::string& fileName)
{
	return root / "save" / NpcPersistenceSaveNamespace /
		"game" / fileName;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& content)
{
	std::error_code errorCode;
	std::filesystem::create_directories(path.parent_path(), errorCode);
	std::ofstream output(path, std::ios::binary);
	if (!output)
	{
		return false;
	}
	output << content;
	return true;
}

void resetRuntime(GameManager& gameManager, bool clearObjects = true)
{
	gameManager.npcManager->freeResource();
	if (clearObjects)
	{
		gameManager.objectManager->freeResource();
	}
	gameManager.eventList.clear();
}

bool prepareNpcPersistenceFixtures(const std::filesystem::path& root)
{
	const std::string body =
		"[Init]\n"
		"ObjName=PERSISTENCE_BODY\n"
		"Kind=2\n"
		"Dir=0\n"
		"MapX=0\n"
		"MapY=0\n"
		"OffX=0\n"
		"OffY=0\n"
		"Damage=0\n"
		"Frame=0\n"
		"Height=4\n"
		"Lum=1\n"
		"CanInteractDirectly=0\n"
		"ScriptFileJustTouch=0\n"
		"ScriptFile=\n";
	const std::string dropTable =
		"[Init]\n"
		"Count=1\n"
		"[1]\n"
		"ObjFile=persistence_drop.ini\n"
		"Num=1\n"
		"Odds=1\n"
		"Group=1\n";
	const std::string drop =
		"[Init]\n"
		"ObjName=PERSISTENCE_DROP\n"
		"Kind=7\n"
		"Type=0\n"
		"Dir=0\n"
		"MapX=0\n"
		"MapY=0\n"
		"OffX=0\n"
		"OffY=0\n"
		"Damage=0\n"
		"Frame=0\n"
		"Height=4\n"
		"Lum=1\n"
		"CanInteractDirectly=1\n"
		"ScriptFileJustTouch=0\n"
		"ScriptFile=\n";
	const std::string invalidDropTable =
		"[Init]\n"
		"Count=999999999999999999999\n"
		"[1]\n"
		"ObjFile=persistence_drop.ini\n"
		"Num=999999999999999999999\n";
	return writeTextFile(root / "ini" / "obj" / "persistence_body.ini", body)
		&& writeTextFile(root / "ini" / "obj" / "persistence_drop_table.ini", dropTable)
		&& writeTextFile(root / "ini" / "obj" / "persistence_drop.ini", drop)
		&& writeTextFile(root / "ini" / "obj" / "invalid_drop_table.ini", invalidDropTable);
}

bool runOfflinePartnerMagicPersistence(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager, true);
	gameManager.player->npcName = "ActiveCharacter";
	gameManager.magicManager.clearMagicList();
	const std::string offlinePlayer =
		"[Init]\n"
		"Name=OfflinePartner\n";
	const std::string offlineMagic =
		"[Init]\n"
		"Name=OFFLINE_PARTNER_MAGIC\n"
		"MoveKind=2\n"
		"[Level1]\n"
		"MoveKind=2\n"
		"Speed=20\n";
	const std::string emptyMagicList =
		"[Head]\n"
		"Count=0\n";
	bool ok = check(
		writeTextFile(
			root / "ini" / "magic" / "offline_partner_magic.ini",
			offlineMagic) &&
		writeTextFile(
			saveGameFixturePath(root, "player1.ini"),
			offlinePlayer) &&
		writeTextFile(
			saveGameFixturePath(root, "magic1.ini"),
			emptyMagicList),
		"write offline partner magic fixtures");

	gameManager.scriptAPI.addOneMagic(
		"OfflinePartner",
		"offline_partner_magic.ini");

	MagicManager loadedMagic;
	ok = check(
		loadedMagic.load(1) &&
		loadedMagic.findPrimaryMagic(
			"offline_partner_magic.ini") != nullptr,
		"addOneMagic updates the matching offline character snapshot") && ok;
	ok = check(
		gameManager.player->npcName == "ActiveCharacter" &&
		gameManager.magicManager.findPrimaryMagic(
			"offline_partner_magic.ini") == nullptr,
		"addOneMagic leaves the active character state unchanged") && ok;
	return ok;
}

void prepareMap(GameManager& gameManager)
{
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 16;
	gameManager.map->data->head.height = 16;
	gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
	gameManager.map->createDataMap();
}

std::shared_ptr<NPC> makeDyingNpc(
	GameManager& gameManager,
	const std::string& name,
	UTime reviveMilliseconds = 0,
	const std::string& deathScript = "")
{
	auto npc = std::make_shared<NPC>();
	npc->npcName = name;
	npc->kind = nkBattle;
	npc->relation = nrHostile;
	npc->lifeMax = 100;
	npc->life = 0;
	npc->bodyIni = "persistence_body.ini";
	npc->dropIni = "persistence_drop_table.ini[100]";
	npc->reviveMilliseconds = reviveMilliseconds;
	npc->deathScript = deathScript;
	npc->setPosition({ 4, 4 }, false);
	npc->res.death.imagePackage = std::make_shared<IMPImage>();
	npc->res.death.imagePackage->directions = 1;
	npc->res.death.imagePackage->interval = 50;
	npc->res.death.imagePackage->frame.resize(10);
	gameManager.npcManager->addNPC(npc);
	npc->handleDeath();
	return npc;
}

std::shared_ptr<NPC> loadNpcRoundTrip(GameManager& gameManager, INIReader& ini)
{
	gameManager.npcManager->freeResource();
	auto loaded = std::make_shared<NPC>();
	loaded->initFromIni(&ini, "NPC000");
	gameManager.npcManager->addNPC(loaded);
	return loaded;
}

bool isNpcInDataMap(const GameManager& gameManager, const std::shared_ptr<NPC>& npc)
{
	Point position = npc->getPosition();
	const auto& npcList = gameManager.map->dataMap.tile[position.y][position.x].npcList;
	return std::find(npcList.begin(), npcList.end(), npc) != npcList.end();
}

bool isObjectInDataMap(const GameManager& gameManager, const std::shared_ptr<Object>& object)
{
	Point position = object->getPosition();
	const auto& objectList = gameManager.map->dataMap.tile[position.y][position.x].objList;
	return std::find(objectList.begin(), objectList.end(), object) != objectList.end();
}

bool checkSingleBodyAndDrop(const GameManager& gameManager, const char* message)
{
	if (gameManager.objectManager->objectList.size() != 2)
	{
		return check(false, message);
	}
	bool hasBody = false;
	bool hasDrop = false;
	for (const auto& object : gameManager.objectManager->objectList)
	{
		if (object == nullptr)
		{
			continue;
		}
		hasBody = hasBody || object->objName == "PERSISTENCE_BODY";
		hasDrop = hasDrop || object->objName == "PERSISTENCE_DROP";
	}
	return check(hasBody && hasDrop, message);
}

std::shared_ptr<NPC> addTestNpc(
	GameManager& gameManager,
	const std::string& name,
	NPCKind kind,
	Point position)
{
	auto npc = std::make_shared<NPC>();
	npc->npcName = name;
	npc->kind = kind;
	npc->relation = kind == nkPartner ? nrFriendly : nrHostile;
	npc->life = 10;
	npc->lifeMax = 10;
	npc->setPosition(position, false);
	gameManager.npcManager->addNPC(npc);
	return npc;
}

size_t countNpcKind(const GameManager& gameManager, NPCKind kind)
{
	return static_cast<size_t>(std::count_if(
		gameManager.npcManager->npcList.begin(),
		gameManager.npcManager->npcList.end(),
		[kind](const std::shared_ptr<NPC>& npc)
		{
			return npc != nullptr && npc->kind == kind;
		}));
}

bool containsNpc(const GameManager& gameManager, const std::shared_ptr<NPC>& expected)
{
	return std::find(
		gameManager.npcManager->npcList.begin(),
		gameManager.npcManager->npcList.end(),
		expected) != gameManager.npcManager->npcList.end();
}

bool runLoadOneNpcPartialFailureTest(GameManager& gameManager)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	Engine* engine = Engine::getInstance();
	engine->resetApplicationQuitRequest();
	gameManager.result = erNone;
	gameManager.scriptAPI.loadOneNpc(
		{ "valid.npc", "missing-after-success.npc" });
	const bool ok = check(
		!engine->isApplicationQuitRequested() &&
			gameManager.result == erOK &&
			gameManager.map->data == nullptr,
		"loadOneNpc discards a partially changed world and returns to title without terminating the application");
	engine->resetApplicationQuitRequest();
	return ok;
}

bool runNpcCollectionLoadSafety(GameManager& gameManager, const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	auto originalObject = std::make_shared<Object>();
	originalObject->objName = "OriginalObject";
	originalObject->setPosition({ 1, 1 });
	gameManager.objectManager->objectList.push_back(originalObject);
	gameManager.objectManager->addChild(originalObject);
	gameManager.map->addObjectToDataMap(originalObject->getPosition(), originalObject);
	bool ok = check(isObjectInDataMap(gameManager, originalObject),
		"object load failure fixture starts in the data map");
	ok = check(
		!gameManager.objectManager->load("missing-object-list.obj") &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.objectManager->objectList.front() == originalObject &&
			isObjectInDataMap(gameManager, originalObject),
		"missing object list preserves the previous collection and data-map entries") && ok;

	auto originalNpc = addTestNpc(gameManager, "OriginalNpc", nkBattle, { 2, 2 });
	auto originalPartner = addTestNpc(gameManager, "OriginalPartner", nkPartner, { 3, 3 });
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getPartnerIdx("$PartnerIdx");
	ok = check(gameManager.varList.getInteger("$PartnerIdx") == 1,
		"GetPartnerIdx preserves the YYCS/XJXQY fallback when partneridx.ini is missing") && ok;
	originalNpc->isVisibleByVariable = false;
	auto hiddenMatches = gameManager.npcManager->findNPC("OriginalNpc");
	ok = check(hiddenMatches.size() == 1 && hiddenMatches.front() == originalNpc
		&& !gameManager.npcManager->findNPC(originalNpc),
		"name lookup includes variable-hidden NPCs while pointer interaction lookup excludes them") && ok;
	originalNpc->isVisibleByVariable = true;

	gameManager.npcManager->load("missing.npc", true);
	ok = check(containsNpc(gameManager, originalNpc) && containsNpc(gameManager, originalPartner),
		"missing NPC list leaves the current NPC and partner collection unchanged") && ok;

	const std::string invalidCountNpcList =
		"[Head]\n"
		"Count=1junk\n"
		"[NPC000]\n"
		"Name=InvalidReplacement\n"
		"Kind=1\n";
	ok = check(writeTextFile(saveGameFixturePath(root, "invalid_count.npc"), invalidCountNpcList),
		"write invalid NPC count fixture") && ok;
	gameManager.npcManager->load("invalid_count.npc", true);
	ok = check(containsNpc(gameManager, originalNpc) && containsNpc(gameManager, originalPartner),
		"invalid NPC count is rejected before replacing the current collection") && ok;

	const std::string validNpcList =
		"[Head]\n"
		"Count=1\n"
		"[NPC000]\n"
		"Name=LoadedNpc\n"
		"Kind=1\n"
		"Relation=1\n"
		"Life=10\n"
		"LifeMax=10\n"
		"MapX=5\n"
		"MapY=5\n";
	ok = check(writeTextFile(saveGameFixturePath(root, "valid.npc"), validNpcList),
		"write valid NPC list fixture") && ok;
	int npcPreparationCheckpointCount = 0;
	int cancelledNpcMutationCount = 0;
	const bool cancelledNpcLoad =
		gameManager.npcManager->load(
			"valid.npc",
			true,
			[&cancelledNpcMutationCount]()
			{
				++cancelledNpcMutationCount;
			},
			[&npcPreparationCheckpointCount]()
			{
				return ++npcPreparationCheckpointCount < 2;
			});
	ok = check(
		!cancelledNpcLoad &&
			npcPreparationCheckpointCount == 2 &&
			cancelledNpcMutationCount == 0 &&
			containsNpc(gameManager, originalNpc) &&
			containsNpc(gameManager, originalPartner),
		"NPC preparation checkpoints can cancel after constructing a candidate but before replacing the live collection") &&
		ok;
	gameManager.global.data.npcName = "previous.npc";
	gameManager.scriptAPI.loadOneNpc(
		{ "missing-first.npc", "valid.npc" });
	ok = check(gameManager.npcManager->npcList.size() == 2
		&& countNpcKind(gameManager, nkBattle) == 1
		&& countNpcKind(gameManager, nkPartner) == 1
		&& containsNpc(gameManager, originalPartner)
		&& !containsNpc(gameManager, originalNpc)
		&& gameManager.global.data.npcName.empty(),
		"loadOneNpc ignores an initial failed candidate, atomically replaces normal NPCs, and clears the aggregate source name") && ok;
	const std::string randomNpcListA =
		"[Head]\n"
		"Count=3\n"
		"[NPC000]\nName=RandomA0\nKind=1\nRelation=1\nMapX=5\nMapY=5\n"
		"[NPC001]\nName=RandomA1\nKind=1\nRelation=1\nMapX=6\nMapY=5\n"
		"[NPC002]\nName=RandomA2\nKind=1\nRelation=1\nMapX=7\nMapY=5\n";
	const std::string randomNpcListB =
		"[Head]\n"
		"Count=2\n"
		"[NPC000]\nName=RandomB0\nKind=1\nRelation=1\nMapX=5\nMapY=6\n"
		"[NPC001]\nName=RandomB1\nKind=1\nRelation=1\nMapX=6\nMapY=6\n";
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "random-a.npc"),
			randomNpcListA) &&
		writeTextFile(
			saveGameFixturePath(root, "random-b.npc"),
			randomNpcListB),
		"write LoadOneNpc random-selection fixtures") && ok;
	gameManager.scriptAPI.loadOneNpc(
		{ "random-a.npc", "random-b.npc" });
	const std::size_t randomACount = static_cast<std::size_t>(
		std::count_if(
			gameManager.npcManager->npcList.begin(),
			gameManager.npcManager->npcList.end(),
			[](const std::shared_ptr<NPC>& npc)
			{
				return npc != nullptr && npc->npcName.rfind("RandomA", 0) == 0;
			}));
	const std::size_t randomBCount = static_cast<std::size_t>(
		std::count_if(
			gameManager.npcManager->npcList.begin(),
			gameManager.npcManager->npcList.end(),
			[](const std::shared_ptr<NPC>& npc)
			{
				return npc != nullptr && npc->npcName.rfind("RandomB", 0) == 0;
			}));
	ok = check(randomACount == 1
		&& randomBCount == 1
		&& countNpcKind(gameManager, nkPartner) == 1
		&& containsNpc(gameManager, originalPartner),
		"LoadOneNpc preserves partners and loads one random NPC from each source list") && ok;
	const std::string emptyNpcList =
		"[Head]\n"
		"Count=0\n";
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "empty.npc"),
			emptyNpcList),
		"write empty NPC merge fixture") &&
		ok;
	int mergeMutationCount = 0;
	const std::size_t retainedNpcCount =
		gameManager.npcManager->npcList.size();
	const bool emptyMergeLoaded =
		gameManager.npcManager->load(
			"empty.npc",
			false,
			[&mergeMutationCount]()
			{
				++mergeMutationCount;
			});
	ok = check(
		emptyMergeLoaded,
		"an empty NPC merge remains a valid load") &&
		ok;
	ok = check(
		mergeMutationCount == 0,
		"an empty NPC merge performs no mutation or redundant action reload") &&
		ok;
	ok = check(
		gameManager.npcManager->npcList.size() ==
			retainedNpcCount,
		"an empty NPC merge retains the existing NPC collection") &&
		ok;
	const std::vector<std::uint8_t> emptyNpcBytes(
		emptyNpcList.begin(),
		emptyNpcList.end());
	mergeMutationCount = 0;
	const bool exactEmptyMergeLoaded =
		gameManager.npcManager->loadExactResourceBytes(
			"empty.npc",
			emptyNpcBytes,
			false,
			[&mergeMutationCount]()
			{
				++mergeMutationCount;
			});
	ok = check(
		exactEmptyMergeLoaded,
		"an exact-root empty NPC merge remains a valid load") &&
		ok;
	ok = check(
		mergeMutationCount == 0,
		"an exact-root empty NPC merge performs no mutation or redundant action reload") &&
		ok;
	ok = check(
		gameManager.npcManager->npcList.size() ==
			retainedNpcCount,
		"an exact-root empty NPC merge retains the existing NPC collection") &&
		ok;

	resetRuntime(gameManager);
	auto normalNpc = addTestNpc(gameManager, "PersistentNormal", nkBattle, { 2, 2 });
	auto oldPartner = addTestNpc(gameManager, "OldPartner", nkPartner, { 3, 3 });
	auto observer = addTestNpc(gameManager, "Observer", nkBattle, { 4, 4 });
	observer->currentCombatTarget = oldPartner;
	observer->currentCombatTargetTime = 123;
	observer->fightState.set(true);
	auto controlEffect = std::make_shared<Effect>();
	gameManager.player->beginControlCharacter(oldPartner, controlEffect);

	const std::string validPartnerList =
		"[Head]\n"
		"Count=1\n"
		"[Partner000]\n"
		"Name=LoadedPartner\n"
		"Kind=3\n"
		"Relation=0\n"
		"Life=10\n"
		"LifeMax=10\n"
		"MapX=6\n"
		"MapY=6\n";
	ok = check(writeTextFile(saveGameFixturePath(root, "partner2.ini"), validPartnerList),
		"write valid partner fixture") && ok;
	ok = check(gameManager.partnerManager.load(2),
		"load valid partner fixture") && ok;
	ok = check(gameManager.npcManager->npcList.size() == 3
		&& countNpcKind(gameManager, nkPartner) == 1
		&& containsNpc(gameManager, normalNpc)
		&& !containsNpc(gameManager, oldPartner),
		"partner load replaces the previous character's partners without removing normal NPCs") && ok;
	ok = check(observer->currentCombatTarget.expired()
		&& observer->currentCombatTargetTime == 0
		&& !observer->fightState.get()
		&& gameManager.player->getControlledCharacter() == nullptr,
		"removing a replaced partner clears combat and player-control references") && ok;
	ok = check(gameManager.partnerManager.load(2)
		&& gameManager.npcManager->npcList.size() == 3
		&& countNpcKind(gameManager, nkPartner) == 1,
		"reloading the same partner file does not duplicate partners") && ok;

	const std::string legacyNpcPartnerList =
		"[Head]\n"
		"Count=1\n"
		"[NPC000]\n"
		"Name=LegacyNpcPartner\n"
		"Kind=3\n"
		"Relation=0\n"
		"Life=10\n"
		"LifeMax=10\n"
		"MapX=7\n"
		"MapY=7\n";
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "partner2.ini"),
			legacyNpcPartnerList),
		"write legacy NPC-section partner fixture") && ok;
	ok = check(gameManager.partnerManager.load(2),
		"load legacy NPC-section partner fixture") && ok;
	auto legacyNpcPartners =
		gameManager.partnerManager.findPartnersFromNPCManager();
	ok = check(
		legacyNpcPartners.size() == 1 &&
			legacyNpcPartners.front()->npcName == "LegacyNpcPartner",
		"legacy NPC000 partner sections remain loadable") && ok;

	const std::string legacyNumericPartnerList =
		"[Head]\n"
		"Count=1\n"
		"[1]\n"
		"Name=LegacyNumericPartner\n"
		"Kind=3\n"
		"Relation=0\n"
		"Life=10\n"
		"LifeMax=10\n"
		"MapX=8\n"
		"MapY=8\n";
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "partner2.ini"),
			legacyNumericPartnerList),
		"write legacy numeric-section partner fixture") && ok;
	ok = check(gameManager.partnerManager.load(2),
		"load legacy numeric-section partner fixture") && ok;
	auto legacyNumericPartners =
		gameManager.partnerManager.findPartnersFromNPCManager();
	ok = check(
		legacyNumericPartners.size() == 1 &&
			legacyNumericPartners.front()->npcName == "LegacyNumericPartner",
		"legacy one-based numeric partner sections remain loadable") && ok;

	auto loadedPartners = gameManager.partnerManager.findPartnersFromNPCManager();
	auto loadedPartner = loadedPartners.empty() ? nullptr : loadedPartners.front();
	const std::string invalidPartnerList =
		"[Head]\n"
		"Count=999999999999999999999\n";
	ok = check(writeTextFile(saveGameFixturePath(root, "partner2.ini"), invalidPartnerList),
		"write invalid partner count fixture") && ok;
	std::string invalidPartnerFailureReason;
	ok = check(!gameManager.partnerManager.load(
			2,
			&invalidPartnerFailureReason)
		&& loadedPartner != nullptr && containsNpc(gameManager, loadedPartner)
		&& countNpcKind(gameManager, nkPartner) == 1
		&& containsNpc(gameManager, normalNpc)
		&& containsNpc(gameManager, observer)
		&& invalidPartnerFailureReason.find(u8"数量") !=
			std::string::npos,
		"invalid target-character partner data fails without replacing the live partners") && ok;

	const std::string legacyPartnerNpcTemplate =
		"[Init]\n"
		"Name=LegacyCharacterTemplate\n"
		"Kind=3\n";
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "partner2.ini"),
			legacyPartnerNpcTemplate) &&
			gameManager.partnerManager.load(2) &&
			countNpcKind(gameManager, nkPartner) == 0 &&
			containsNpc(gameManager, normalNpc) &&
			containsNpc(gameManager, observer),
		"a legacy first-party NPC template named partnerN.ini remains an empty partner list") && ok;
	const std::string legacyPartnerBodyTemplate =
		"[Common]\n"
		"Image=npc080_body.asf\n";
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "partner2.ini"),
			legacyPartnerBodyTemplate) &&
			gameManager.partnerManager.load(2) &&
			countNpcKind(gameManager, nkPartner) == 0,
		"a legacy first-party body template named partnerN.ini remains an empty partner list") && ok;

	ok = check(gameManager.partnerManager.load(3)
		&& countNpcKind(gameManager, nkPartner) == 0
		&& containsNpc(gameManager, normalNpc)
		&& containsNpc(gameManager, observer),
		"missing character partner file clears stale partners and preserves normal NPCs") && ok;

	gameManager.player->npcName = "RetainedPlayer";
	gameManager.player->rage = 73;
	ok = check(
		!gameManager.player->load(4) &&
			gameManager.player->npcName == "RetainedPlayer" &&
			gameManager.player->rage == 73,
		"missing player data is rejected before clearing the live player") && ok;
	std::string emptyPlayerFailureReason;
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "player4.ini"),
			"") &&
			!gameManager.player->load(
				4,
				&emptyPlayerFailureReason) &&
			gameManager.player->npcName == "RetainedPlayer" &&
			gameManager.player->rage == 73 &&
			emptyPlayerFailureReason.find(u8"为空") !=
				std::string::npos,
		"zero-byte player data is rejected with a concrete reason before clearing the live player") && ok;
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "player4.ini"),
			"[Init\nName=Broken\n") &&
			!gameManager.player->load(4) &&
			gameManager.player->npcName == "RetainedPlayer" &&
			gameManager.player->rage == 73,
		"malformed player data is rejected before clearing the live player") && ok;
	ok = check(
		writeTextFile(
			saveGameFixturePath(root, "player4.ini"),
			"[Other]\nName=WrongSection\n") &&
			!gameManager.player->load(4) &&
			gameManager.player->npcName == "RetainedPlayer" &&
			gameManager.player->rage == 73,
		"player data without Init is rejected before clearing the live player") && ok;

	resetRuntime(gameManager);
	gameManager.npcManager->addNPC("missing.ini", 1, 1, 0);
	ok = check(gameManager.npcManager->npcList.empty(),
		"missing single-NPC resource does not create a blank runtime NPC") && ok;
	const std::string invalidNpcResource = "[Init\nName=Broken\n";
	ok = check(writeTextFile(root / "ini" / "npc" / "invalid.ini", invalidNpcResource),
		"write invalid single-NPC resource fixture") && ok;
	gameManager.npcManager->addNPC("invalid.ini", 1, 1, 0);
	ok = check(gameManager.npcManager->npcList.empty(),
		"invalid single-NPC resource does not create a blank runtime NPC") && ok;

	const std::string invalidLevelList =
		"[Head]\n"
		"Levels=999999999999999999999\n";
	ok = check(writeTextFile(root / "ini" / "level" / "invalid-level.ini", invalidLevelList),
		"write invalid level count fixture") && ok;
	gameManager.player->loadLevel("invalid-level.ini");
	ok = check(gameManager.player->levelList.empty(),
		"invalid player level count is rejected without allocating an unbounded list") && ok;
	gameManager.player->setLevel(5);
	ok = check(gameManager.player->level == 5,
		"setting a player level remains safe when the level table is unavailable") && ok;
	auto npcWithoutLevelTable = std::make_shared<NPC>();
	npcWithoutLevelTable->loadLevel("invalid-level.ini");
	ok = check(npcWithoutLevelTable->npcLevelList.empty(),
		"invalid NPC level count is rejected without allocating an unbounded list") && ok;

	gameManager.npcManager->npcList.assign(
		static_cast<size_t>(NPCPersistence::MaximumRuntimeNpcCount), nullptr);
	auto overLimitNpc = std::make_shared<NPC>();
	overLimitNpc->npcName = "OverLimit";
	gameManager.npcManager->addNPC(overLimitNpc);
	ok = check(gameManager.npcManager->npcList.size()
			== static_cast<size_t>(NPCPersistence::MaximumRuntimeNpcCount)
		&& !containsNpc(gameManager, overLimitNpc),
		"runtime NPC additions stop at the same bound accepted by persistence") && ok;
	gameManager.npcManager->npcList.clear();

	resetRuntime(gameManager);
	auto invalidDropNpc = makeDyingNpc(gameManager, "InvalidDropOwner");
	invalidDropNpc->dropIni = "invalid_drop_table.ini";
	invalidDropNpc->noAddBody = true;
	invalidDropNpc->setTime(invalidDropNpc->getTime() + 500);
	invalidDropNpc->actionManager->update(500);
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.objectManager->objectList.empty(),
		"invalid drop-table counts are consumed safely without spawning the table as an object") && ok;
	return ok;
}

bool runCompatibleStoryEntityListLoads(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	struct Fixture
	{
		std::string fileName;
		std::string content;
	};
	const std::vector<Fixture> npcFixtures = {
		{ "story-empty.npc", "" },
		{
			"story-wrong-sections.npc",
			"[Head]\nCount=1\n[OBJ000]\nObjName=WrongType\n"
		},
		{ "story-malformed.npc", "[Head\nCount=1\n" }
	};
	const std::vector<Fixture> objectFixtures = {
		{ "story-empty.obj", "" },
		{
			"story-wrong-sections.obj",
			"[Head]\nCount=1\n[NPC000]\nName=WrongType\n"
		},
		{ "story-malformed.obj", "[Head\nCount=1\n" }
	};

	bool ok = true;
	prepareMap(gameManager);
	for (const auto& fixture : npcFixtures)
	{
		ok = check(
			writeTextFile(
				saveGameFixturePath(root, fixture.fileName),
				fixture.content),
			"write compatible story NPC fixture") && ok;
		gameManager.npcManager->freeResource();
		auto retainedNpc = addTestNpc(
			gameManager,
			"RetainedNpc",
			nkBattle,
			{ 2, 2 });
		ok = check(
			!gameManager.npcManager->load(fixture.fileName, true) &&
				containsNpc(gameManager, retainedNpc),
			"direct NPC manager loads remain strict for invalid story files") && ok;

		gameManager.global.data.npcName = "previous.npc";
		gameManager.global.data.objName = "retained.obj";
		ok = check(
			gameManager.scriptAPI.loadNPC(fixture.fileName) &&
				gameManager.npcManager->npcList.empty() &&
				gameManager.global.data.npcName == fixture.fileName,
			"ordinary story NPC loads accept an empty compatible list and bind its file name") && ok;
		gameManager.scriptAPI.saveNPC("");
		INIReader savedNpc(
			"save\\game\\" + fixture.fileName);
		ok = check(
			savedNpc.ParseError() == 0 &&
				savedNpc.GetInteger("Head", "Count", -1) == 0,
			"SaveNPC writes the compatible empty list back to the bound file") && ok;
	}

	for (const auto& fixture : objectFixtures)
	{
		ok = check(
			writeTextFile(
				saveGameFixturePath(root, fixture.fileName),
				fixture.content),
			"write compatible story object fixture") && ok;
		gameManager.objectManager->freeResource();
		auto retainedObject = std::make_shared<Object>();
		retainedObject->objName = "RetainedObject";
		retainedObject->setPosition({ 3, 3 });
		gameManager.objectManager->objectList.push_back(retainedObject);
		gameManager.objectManager->addChild(retainedObject);
		gameManager.map->addObjectToDataMap(
			retainedObject->getPosition(),
			retainedObject);
		ok = check(
			!gameManager.objectManager->load(fixture.fileName) &&
				gameManager.objectManager->objectList.size() == 1 &&
				gameManager.objectManager->objectList.front() == retainedObject,
			"direct object manager loads remain strict for invalid story files") && ok;

		gameManager.global.data.npcName = "retained.npc";
		gameManager.global.data.objName = "previous.obj";
		ok = check(
			gameManager.scriptAPI.loadObject(fixture.fileName) &&
				gameManager.objectManager->objectList.empty() &&
				gameManager.global.data.objName == fixture.fileName,
			"ordinary story object loads accept an empty compatible list and bind its file name") && ok;
		gameManager.scriptAPI.saveObject("");
		INIReader savedObject(
			"save\\game\\" + fixture.fileName);
		ok = check(
			savedObject.ParseError() == 0 &&
				savedObject.GetInteger("Head", "Count", -1) == 0,
			"SaveObj writes the compatible empty list back to the bound file") && ok;
	}

	gameManager.npcManager->freeResource();
	auto retainedNpc = addTestNpc(
		gameManager,
		"MissingFileRetainedNpc",
		nkBattle,
		{ 4, 4 });
	gameManager.global.data.npcName = "retained-missing.npc";
	ok = check(
		!gameManager.scriptAPI.loadNPC("missing-story.npc") &&
			containsNpc(gameManager, retainedNpc) &&
			gameManager.global.data.npcName == "retained-missing.npc",
		"ordinary story NPC loads still reject a missing file without rebinding") && ok;

	gameManager.objectManager->freeResource();
	auto retainedObject = std::make_shared<Object>();
	retainedObject->objName = "MissingFileRetainedObject";
	gameManager.objectManager->objectList.push_back(retainedObject);
	gameManager.global.data.objName = "retained-missing.obj";
	ok = check(
		!gameManager.scriptAPI.loadObject("missing-story.obj") &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.objectManager->objectList.front() == retainedObject &&
			gameManager.global.data.objName == "retained-missing.obj",
		"ordinary story object loads still reject a missing file without rebinding") && ok;
	return ok;
}

bool runPreparedNpcLoadWithoutSource(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);

	NPCManager::PreparedLoad preparedLoad;
	bool ok = check(
		gameManager.npcManager->prepareLoad(
			"valid.npc",
			preparedLoad) &&
			preparedLoad.isValid() &&
			preparedLoad.npcCount() == 1,
		"worker-facing NPC preparation parses the source once");
	std::error_code errorCode;
	std::filesystem::remove(
		saveGameFixturePath(root, "valid.npc"),
		errorCode);
	int mutationCount = 0;
	ok = check(
		gameManager.npcManager->commitPreparedLoad(
			preparedLoad,
			true,
			[&mutationCount]()
			{
				++mutationCount;
			}) &&
			mutationCount == 1 &&
			gameManager.npcManager->npcList.size() == 1 &&
			gameManager.npcManager->npcList.front() != nullptr &&
			gameManager.npcManager->npcList.front()->npcName ==
				"LoadedNpc",
		"a prepared NPC list commits after its source is removed without rereading or reparsing it") &&
		ok;
	return ok;
}

bool runLegacyOverstatedNpcCountCompatibility(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	if (!writeTextFile(
			saveGameFixturePath(root, "legacy-overstated.npc"),
			"[Head]\n"
			"Count=2\n"
			"[NPC000]\n"
			"Name=LegacyNpc\n"
			"Kind=1\n"
			"MapX=5\n"
			"MapY=5\n") ||
		!writeTextFile(
			saveGameFixturePath(root, "partner.ini"),
			"[Head]\n"
			"Count=2\n"
			"[Partner000]\n"
			"Name=LegacyPartner\n"
			"Kind=3\n"
			"MapX=4\n"
			"MapY=4\n"))
	{
		return check(false, "write incomplete compatible entity fixtures");
	}

	NPCManager::PreparedLoad preparedLoad;
	bool ok = check(
		!gameManager.npcManager->prepareLoad(
			"legacy-overstated.npc",
			preparedLoad),
		"ordinary NPC loads keep rejecting a declared missing section");
	ok = check(
		gameManager.npcManager->prepareLoad(
			"legacy-overstated.npc",
			preparedLoad,
			true) &&
			preparedLoad.isValid() &&
			preparedLoad.npcCount() == 1 &&
			gameManager.npcManager->commitPreparedLoad(
				preparedLoad,
				true) &&
			gameManager.npcManager->npcList.size() == 1 &&
			gameManager.npcManager->npcList.front() != nullptr &&
			gameManager.npcManager->npcList.front()->npcName ==
				"LegacyNpc",
		"compatible save loading truncates an old total-population count to the contiguous NPC sections") &&
		ok;
	gameManager.partnerManager.load(-1);
	ok = check(
		gameManager.npcManager->npcList.size() == 2 &&
		gameManager.npcManager->findNPC("LegacyPartner").size() == 1,
		"compatible partner loading stops at the first missing section without creating a blank partner") &&
		ok;
	return ok;
}

bool runPreparedNpcCacheRetention(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	if (!writeTextFile(
			root / "ini" / "npcres" / "prepared-cache.ini",
			"[stand]\nImage=prepared-cache.asf\n") ||
		!writeTextFile(
			saveGameFixturePath(root, "prepared-cache.npc"),
			"[Head]\nCount=1\n"
			"[NPC000]\n"
			"Name=PreparedCacheNpc\n"
			"NPCIni=prepared-cache.ini\n"
			"Kind=1\n"
			"MapX=5\n"
			"MapY=5\n"))
	{
		return check(false, "write prepared NPC cache fixtures");
	}

	auto preparedImage = std::make_shared<IMPImage>();
	auto playerImage = std::make_shared<IMPImage>();
	auto partnerImage = std::make_shared<IMPImage>();
	gameManager.npcManager->actionImageList["prepared-cache.asf"] =
		preparedImage;
	gameManager.npcManager->actionImageList["player-cache.asf"] =
		playerImage;
	gameManager.npcManager->actionImageList["partner-cache.asf"] =
		partnerImage;
	gameManager.player->res.stand.imageFile = "player-cache.asf";
	gameManager.player->res.stand.imagePackage = playerImage;

	auto partner = std::make_shared<NPC>();
	partner->npcName = "CachePartner";
	partner->kind = nkPartner;
	partner->res.stand.imageFile = "partner-cache.asf";
	partner->res.stand.imagePackage = partnerImage;
	partner->setPosition({ 4, 4 }, false);
	gameManager.npcManager->addNPC(partner);

	std::weak_ptr<IMPImage> discardedImage;
	{
		auto oldImage = std::make_shared<IMPImage>();
		discardedImage = oldImage;
		gameManager.npcManager->actionImageList["discarded-cache.asf"] =
			oldImage;
		auto oldNpc = std::make_shared<NPC>();
		oldNpc->npcName = "DiscardedCacheNpc";
		oldNpc->kind = nkBattle;
		oldNpc->res.stand.imageFile = "discarded-cache.asf";
		oldNpc->res.stand.imagePackage = oldImage;
		oldNpc->setPosition({ 3, 3 }, false);
		gameManager.npcManager->addNPC(oldNpc);
	}

	NPCManager::PreparedLoad preparedLoad;
	bool ok = check(
		gameManager.npcManager->prepareLoad(
			"prepared-cache.npc",
			preparedLoad) &&
		gameManager.npcManager->commitPreparedLoad(
			preparedLoad,
			true),
		"prepared NPC cache fixture commits successfully");
	const auto loadedNpc = gameManager.npcManager->findNPC(
		"PreparedCacheNpc");
	const auto preparedCache = gameManager.npcManager->actionImageList.find(
		"prepared-cache.asf");
	const auto playerCache = gameManager.npcManager->actionImageList.find(
		"player-cache.asf");
	const auto partnerCache = gameManager.npcManager->actionImageList.find(
		"partner-cache.asf");
	ok = check(
		loadedNpc.size() == 1 &&
		loadedNpc.front()->res.stand.imagePackage == preparedImage &&
		gameManager.player->res.stand.imagePackage == playerImage &&
		partner->res.stand.imagePackage == partnerImage &&
		preparedCache != gameManager.npcManager->actionImageList.end() &&
		preparedCache->second == preparedImage &&
		playerCache != gameManager.npcManager->actionImageList.end() &&
		playerCache->second == playerImage &&
		partnerCache != gameManager.npcManager->actionImageList.end() &&
		partnerCache->second == partnerImage &&
		gameManager.npcManager->actionImageList.find(
			"discarded-cache.asf") ==
			gameManager.npcManager->actionImageList.end() &&
		discardedImage.expired(),
		"prepared NPC commit retains live image caches without reloading every NPC and prunes discarded images") &&
		ok;

	gameManager.player->res.stand.imageFile.clear();
	gameManager.player->res.stand.imagePackage = nullptr;
	resetRuntime(gameManager);
	return ok;
}

bool runPlayerChangePersistenceAndPartnerContinuity(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	gameManager.global.data.characterIndex = 0;
	gameManager.player->npcName = "TargetCharacter";
	gameManager.player->setPosition({ 8, 8 }, false);
	bool ok = check(gameManager.player->save(1),
		"write target-character player fixture");
	ok = check(gameManager.magicManager.save(1),
		"write target-character magic fixture") && ok;
	ok = check(gameManager.goodsManager.save(1),
		"write target-character goods fixture") && ok;
	gameManager.player->npcName = "SourceCharacter";

	const std::string staleTargetPartnerList =
		"[Head]\n"
		"Count=1\n"
		"[Partner000]\n"
		"Name=StaleTargetPartner\n"
		"Kind=3\n"
		"Relation=0\n"
		"Life=10\n"
		"LifeMax=10\n"
		"MapX=9\n"
		"MapY=9\n";
	ok = check(writeTextFile(saveGameFixturePath(root, "partner1.ini"), staleTargetPartnerList),
		"write stale target-character partner fixture") && ok;

	auto normalNpc = addTestNpc(gameManager, "PersistentNormal", nkBattle, { 6, 6 });
	auto currentPartner = addTestNpc(gameManager, "CurrentStoryPartner", nkPartner, { 7, 7 });
	gameManager.memo.memo = { "memo saved before player change" };
	gameManager.scriptAPI.playerChange(1);

	auto partners = gameManager.partnerManager.findPartnersFromNPCManager();
	ok = check(gameManager.global.data.characterIndex == 1
		&& gameManager.player->npcName == "TargetCharacter",
		"player change loads the requested character") && ok;
	ok = check(partners.size() == 1
		&& partners.front() == currentPartner
		&& containsNpc(gameManager, normalNpc),
		"player change preserves the live story partner collection") && ok;
	ok = check(!File::fileExist("save\\game\\partner0.ini"),
		"player change does not snapshot partners under the outgoing character") && ok;
	std::unique_ptr<char[]> savedMemo;
	int savedMemoLength = 0;
	std::deque<std::string> savedMemoLines;
	ok = check(
		File::readFile(
			"save\\game\\memo.txt",
			savedMemo,
			savedMemoLength) &&
			savedMemo != nullptr &&
			MemoPersistence::parseText(
				std::string(
				savedMemo.get(),
				static_cast<std::size_t>(savedMemoLength)),
				savedMemoLines) &&
			savedMemoLines ==
				std::deque<std::string>{
					"memo saved before player change" },
		"player change persists the shared memo before switching characters") && ok;

	return ok;
}

bool runEntityListScriptSavePolicy(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	const auto readVirtualText = [](const std::string& fileName)
	{
		std::unique_ptr<char[]> data;
		int length = 0;
		if (!File::readFile(fileName, data, length) ||
			length < 0)
		{
			return std::string();
		}
		return std::string(
			data == nullptr ? "" : data.get(),
			static_cast<std::size_t>(length));
	};

	const std::string retainedGlobal =
		"[State]\n"
		"Map=retained.map\n";
	bool ok = check(
		writeTextFile(
			saveGameFixturePath(root, "game.ini"),
			retainedGlobal),
		"write entity-list script save guard fixture");
	gameManager.global.data.npcName = "retained.npc";
	gameManager.global.data.objName = "retained.obj";
	gameManager.scriptAPI.saveNPC("game.ini");
	ok = check(
		gameManager.global.data.npcName == "retained.npc" &&
			readVirtualText("save\\game\\game.ini") ==
				retainedGlobal,
		"SaveNPC rejects a core save name without updating the global NPC label or overwriting game.ini") &&
		ok;

	gameManager.global.data.objName = "Shared.INI";
	gameManager.scriptAPI.saveNPC("shared.ini");
	ok = check(
		gameManager.global.data.npcName == "retained.npc" &&
			!File::fileExist("save\\game\\shared.ini"),
		"SaveNPC rejects a case-insensitive collision with the object list before writing or updating state") &&
		ok;

	std::error_code errorCode;
	const std::filesystem::path blockedNpcPath =
		saveGameFixturePath(root, "blocked.npc");
	std::filesystem::create_directories(
		blockedNpcPath,
		errorCode);
	const bool blockedNpcReady = !errorCode;
	ok = check(
		blockedNpcReady,
		"create a safe-name NPC write-failure fixture") && ok;
	if (blockedNpcReady)
	{
		gameManager.global.data.objName = "retained.obj";
		gameManager.scriptAPI.saveNPC("blocked.npc");
		errorCode.clear();
		ok = check(
			gameManager.global.data.npcName == "retained.npc" &&
				std::filesystem::is_directory(
					blockedNpcPath,
					errorCode) &&
				!errorCode,
			"SaveNPC updates the global NPC label only after the entity list write succeeds") &&
			ok;
	}

	errorCode.clear();
	const std::filesystem::path blockedObjectPath =
		saveGameFixturePath(root, "blocked.obj");
	std::filesystem::create_directories(
		blockedObjectPath,
		errorCode);
	const bool blockedObjectReady = !errorCode;
	ok = check(
		blockedObjectReady,
		"create a safe-name object write-failure fixture") && ok;
	if (blockedObjectReady)
	{
		gameManager.global.data.objName = "retained.obj";
		gameManager.scriptAPI.saveObject("blocked.obj");
		errorCode.clear();
		ok = check(
			gameManager.global.data.objName == "retained.obj" &&
				std::filesystem::is_directory(
					blockedObjectPath,
					errorCode) &&
				!errorCode,
			"SaveObj updates the global object label only after the entity list write succeeds") &&
			ok;
	}

	gameManager.scriptAPI.saveNPC("accepted.npc");
	INIReader savedNpc("save\\game\\accepted.npc");
	ok = check(
		gameManager.global.data.npcName == "accepted.npc" &&
			savedNpc.ParseError() == 0 &&
			savedNpc.GetInteger("Head", "Count", -1) == 0,
		"SaveNPC publishes an ordinary list and updates its global label after the successful write") &&
		ok;

	gameManager.scriptAPI.saveObject("memo.ini");
	INIReader savedLegacyObject(
		"save\\game\\memo.ini");
	ok = check(
		gameManager.global.data.objName == "memo.ini" &&
			savedLegacyObject.ParseError() == 0 &&
			savedLegacyObject.GetInteger(
				"Head", "Count", -1) == 0,
		"SaveObj keeps memo.ini available as a legacy object-list name") &&
		ok;

	gameManager.global.data.npcName.clear();
	std::vector<std::string> filesBeforeEmptySave =
		File::listFiles("save\\game");
	std::sort(
		filesBeforeEmptySave.begin(),
		filesBeforeEmptySave.end());
	gameManager.scriptAPI.saveNPC("");
	std::vector<std::string> filesAfterEmptySave =
		File::listFiles("save\\game");
	std::sort(
		filesAfterEmptySave.begin(),
		filesAfterEmptySave.end());
	ok = check(
		gameManager.global.data.npcName.empty() &&
			filesAfterEmptySave == filesBeforeEmptySave,
		"SaveNPC keeps an explicit empty current list as a no-op") &&
		ok;
	return ok;
}

bool runDyingAnimationRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "DyingOwner");
	bool ok = check(npc->isDying() && npc->actionLastTime == 500,
		"fixture enters a 500ms death animation");
	npc->setTime(npc->getTime() + 200);

	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	ok = check(ini.GetBoolean("NPC000", "IsDeathInvoked", false)
		&& !ini.GetBoolean("NPC000", "IsDeath", true)
		&& ini.GetInteger("NPC000", "DeathActionRemainingMilliseconds", 0) == 300,
		"death animation save records invoked state and remaining time") && ok;

	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(loaded->life == 0 && loaded->isDying() && !loaded->isHiding()
		&& loaded->actionLastTime == 300
		&& isNpcInDataMap(gameManager, loaded),
		"death animation reloads as dying instead of a zero-life standing NPC") && ok;
	loaded->setTime(loaded->getTime() + 300);
	loaded->actionManager->update(300);
	ok = check(loaded->isHiding() && (loaded->result & erLifeExhaust),
		"restored death animation reaches cleanup exactly once") && ok;
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.npcManager->npcList.empty(),
		"non-reviving NPC is removed after restored death cleanup") && ok;
	ok = checkSingleBodyAndDrop(gameManager,
		"restored death cleanup creates one body and one drop") && ok;
	gameManager.npcManager->onUpdate();
	ok = checkSingleBodyAndDrop(gameManager,
		"repeated manager updates do not duplicate body or drop") && ok;
	return ok;
}

bool runLiveNpcRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = std::make_shared<NPC>();
	npc->npcName = "LivingOwner";
	npc->kind = nkBattle;
	npc->lifeMax = 100;
	npc->life = 75;
	npc->setPosition({ 4, 4 }, false);
	gameManager.npcManager->addNPC(npc);

	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	bool ok = check(!ini.GetBoolean("NPC000", "IsDeathInvoked", true)
		&& !ini.GetBoolean("NPC000", "IsDeath", true),
		"live NPC save does not enter terminal persistence");
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(loaded->life == 75
		&& loaded->isStanding()
		&& !loaded->isDying()
		&& !loaded->isHiding()
		&& isNpcInDataMap(gameManager, loaded)
		&& (loaded->result & (erRunDeathScript | erLifeExhaust)) == 0,
		"live NPC round trip preserves the normal standing path") && ok;
	return ok;
}

bool runStatusDurationInputSafety()
{
	INIReader ini;
	ini.Set("NPC000", "PoisonSeconds", "1.25");
	ini.Set("NPC000", "PetrifiedSeconds", "nan");
	ini.Set("NPC000", "FrozenSeconds", "inf");
	ini.Set("NPC000", "ImmobilizedSeconds", "1e20");
	NPC npc;
	npc.initFromIni(&ini, "NPC000");
	bool ok = check(
		npc.poisoned && npc.poisonedLastTime == 1250,
		"finite NPC status seconds retain millisecond precision");
	ok = check(
		!npc.petrified && npc.petrifiedLastTime == 0
			&& !npc.frozen && npc.frozenLastTime == 0,
		"non-finite NPC status seconds are ignored") && ok;
	ok = check(
		npc.immobilized
			&& npc.immobilizedLastTime ==
				std::numeric_limits<UTime>::max(),
		"oversized finite NPC status seconds saturate safely") && ok;
	return ok;
}

bool runLegacyNpcObjectDefaults()
{
	INIReader missingRadiusIni;
	missingRadiusIni.SetInteger("NPC000", "Kind", nkBattle);
	NPC missingRadiusNpc;
	missingRadiusNpc.initFromIni(&missingRadiusIni, "NPC000");
	bool ok = check(
		missingRadiusNpc.visionRadius == 9
			&& missingRadiusNpc.dialogRadius == 1
			&& missingRadiusNpc.attackRadius == 1
			&& missingRadiusNpc.walkSpeed == 1
			&& missingRadiusNpc.lifeLowPercent == 20
			&& missingRadiusNpc.hurtPlayerRadius == 1
			&& missingRadiusNpc.timerScriptInterval == DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL,
		"missing NPC fields use the legacy trilogy-compatible defaults");

	INIReader zeroRadiusIni;
	zeroRadiusIni.SetInteger("NPC000", "Kind", nkBattle);
	zeroRadiusIni.SetInteger("NPC000", "VisionRadius", 0);
	zeroRadiusIni.SetInteger("NPC000", "DialogRadius", 0);
	zeroRadiusIni.SetInteger("NPC000", "AttackRadius", 0);
	zeroRadiusIni.SetInteger("NPC000", "WalkSpeed", 0);
	NPC zeroRadiusNpc;
	zeroRadiusNpc.initFromIni(&zeroRadiusIni, "NPC000");
	ok = check(
		zeroRadiusNpc.visionRadius == 9
			&& zeroRadiusNpc.dialogRadius == 1
			&& zeroRadiusNpc.attackRadius == 1
			&& zeroRadiusNpc.walkSpeed == 1,
		"zero NPC fields retain the version-validated property sentinel semantics") && ok;

	INIReader negativeRadiusIni;
	negativeRadiusIni.SetInteger("NPC000", "VisionRadius", -9);
	negativeRadiusIni.SetInteger("NPC000", "DialogRadius", -2);
	negativeRadiusIni.SetInteger("NPC000", "AttackRadius", -1);
	negativeRadiusIni.SetInteger("NPC000", "WalkSpeed", -3);
	NPC negativeRadiusNpc;
	negativeRadiusNpc.initFromIni(&negativeRadiusIni, "NPC000");
	ok = check(
		negativeRadiusNpc.visionRadius == -9
			&& negativeRadiusNpc.dialogRadius == -2
			&& negativeRadiusNpc.attackRadius == -1
			&& negativeRadiusNpc.walkSpeed == 1,
		"NPC radius getters preserve nonzero values while WalkSpeed clamps values below one") && ok;

	INIReader missingObjectFieldsIni;
	Object missingObjectFields;
	missingObjectFields.initFromIni(&missingObjectFieldsIni, "OBJ000");
	ok = check(
		missingObjectFields.kind == okOrnament
			&& missingObjectFields.direction == 0
			&& missingObjectFields.damage == 0
			&& missingObjectFields.timerScriptInterval == DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL,
		"missing Object Kind uses the legacy map-entry default") && ok;

	INIReader explicitBodyIni;
	explicitBodyIni.SetInteger("OBJ000", "Kind", okBody);
	Object explicitBody;
	explicitBody.initFromIni(&explicitBodyIni, "OBJ000");
	ok = check(
		explicitBody.kind == okBody,
		"explicit Object Kind remains unchanged") && ok;
	return ok;
}

bool runZeroLifeStoryNpcRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = std::make_shared<NPC>();
	npc->npcName = "ZeroLifeStoryNpc";
	npc->kind = nkNormal;
	npc->life = 0;
	npc->lifeMax = 0;
	npc->setPosition({ 4, 4 }, false);
	gameManager.npcManager->addNPC(npc);

	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	bool ok = check(
		ini.GetInteger("NPC000", "DeathPersistenceVersion", 0) == 1
			&& !ini.GetBoolean("NPC000", "IsDeathInvoked", true)
			&& npc->life == 0,
		"zero-life story NPC save explicitly records a live state");
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(
		loaded->life == 0
			&& loaded->isStanding()
			&& !loaded->isDying()
			&& !loaded->isHiding()
			&& loaded->isVisibleForRuntime()
			&& isNpcInDataMap(gameManager, loaded),
		"zero-life story NPC round trip honors the explicit live state")
		&& ok;
	return ok;
}

bool runCorruptDeathDurationFallback(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "CorruptDeathDuration");
	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	ini.SetInteger("NPC000", "DeathActionRemainingMilliseconds", 999999999);
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	return check(loaded->isDying() && loaded->actionLastTime == 600000,
		"corrupt death-animation duration is capped to a bounded recovery time");
}

bool runCleanupPendingRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "CleanupPending");
	npc->setTime(npc->getTime() + 500);
	npc->actionManager->update(500);
	bool ok = check(npc->isHiding() && (npc->result & erLifeExhaust),
		"fixture reaches manager-cleanup-pending state");

	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	ok = check(ini.GetBoolean("NPC000", "IsDeathInvoked", false)
		&& ini.GetBoolean("NPC000", "IsDeath", false),
		"cleanup-pending save records completed death state") && ok;
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(loaded->isHiding()
		&& (loaded->result & erLifeExhaust)
		&& !isNpcInDataMap(gameManager, loaded),
		"cleanup-pending reload remains hidden and requeues manager cleanup") && ok;
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.npcManager->npcList.empty(),
		"cleanup-pending reload removes non-reviving NPC") && ok;
	ok = checkSingleBodyAndDrop(gameManager,
		"cleanup-pending reload creates body and drop once") && ok;
	return ok;
}

bool runReviveCountdownRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "RevivingOwner", 1000);
	npc->setTime(npc->getTime() + 500);
	npc->actionManager->update(500);
	gameManager.npcManager->onUpdate();
	bool ok = check(gameManager.npcManager->npcList.size() == 1
		&& npc->isHiding()
		&& npc->isBodyIniAdded == 1
		&& npc->leftMillisecondsToRevive == 1000,
		"reviving NPC enters hidden countdown after one cleanup");
	ok = checkSingleBodyAndDrop(gameManager,
		"reviving NPC creates body and drop before countdown") && ok;
	npc->updateReviveCountdown(400);

	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(loaded->isHiding()
		&& loaded->isBodyIniAdded == 1
		&& loaded->leftMillisecondsToRevive == 600
		&& !isNpcInDataMap(gameManager, loaded),
		"revive countdown reloads hidden with exact remaining time") && ok;
	gameManager.npcManager->onUpdate();
	ok = checkSingleBodyAndDrop(gameManager,
		"reloaded revive countdown does not duplicate body or drop") && ok;
	loaded->updateReviveCountdown(600);
	ok = check(!loaded->isHiding()
		&& loaded->isStanding()
		&& loaded->life == loaded->getLifeMax()
		&& loaded->leftMillisecondsToRevive == 0
		&& isNpcInDataMap(gameManager, loaded),
		"reloaded revive countdown returns NPC to live standing state") && ok;
	return ok;
}

bool runLegacyZeroLifeCompatibility(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "LegacyZeroLife");
	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	ini.Remove("NPC000", "DeathPersistenceVersion");
	ini.Remove("NPC000", "IsDeathInvoked");
	ini.Remove("NPC000", "IsDeath");
	ini.Remove("NPC000", "DeathActionRemainingMilliseconds");
	ini.Remove("NPC000", "PendingDeathScript");
	ini.Remove("NPC000", "UseSpecialDeath");
	ini.Remove("NPC000", "SpecialDeathAction");

	auto loaded = loadNpcRoundTrip(gameManager, ini);
	bool ok = check(loaded->life == 0
		&& loaded->isStanding()
		&& !loaded->isDying()
		&& !loaded->isHiding()
		&& isNpcInDataMap(gameManager, loaded)
		&& (loaded->result & (erRunDeathScript | erLifeExhaust)) == 0,
		"unversioned zero-life NPC keeps the legacy live story-character semantics");
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.npcManager->npcList.size() == 1
		&& gameManager.npcManager->npcList.front() == loaded
		&& gameManager.objectManager->objectList.empty(),
		"legacy zero-life story NPC remains present without body or drop cleanup") && ok;

	loaded->res.death.imagePackage = std::make_shared<IMPImage>();
	loaded->res.death.imagePackage->directions = 1;
	loaded->res.death.imagePackage->interval = 50;
	loaded->res.death.imagePackage->frame.resize(10);
	loaded->hurtLife(1);
	ok = check(loaded->life == 0
		&& loaded->isDying()
		&& !loaded->isHiding()
		&& (loaded->result & erLifeExhaust) == 0,
		"runtime damage still starts normal death for a legacy zero-life NPC") && ok;
	loaded->setTime(loaded->getTime() + loaded->actionLastTime);
	loaded->actionManager->update(loaded->actionLastTime);
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.npcManager->npcList.empty(),
		"runtime death still removes a legacy zero-life NPC after its death action") && ok;
	ok = checkSingleBodyAndDrop(gameManager,
		"runtime death still creates the configured body and drop for a legacy zero-life NPC") && ok;
	return ok;
}

bool runSpecialDeathBodySuppressionRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "SpecialDeathOwner");
	npc->useSpecialDeath = true;
	npc->specialDeathAction = "missing_special_death.asf";
	npc->noAddBody = true;
	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	bool ok = check(ini.GetBoolean("NPC000", "UseSpecialDeath", false)
		&& ini.Get("NPC000", "SpecialDeathAction", "") == "missing_special_death.asf"
		&& ini.GetInteger("NPC000", "IsNodAddBody", 0) == 1,
		"special death save records its visual identity and body suppression");
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(loaded->isDying()
		&& loaded->noAddBody
		&& loaded->specialDeathAction == "missing_special_death.asf",
		"special death reload preserves body suppression even when the visual resource is unavailable") && ok;
	loaded->setTime(loaded->getTime() + loaded->actionLastTime);
	loaded->actionManager->update(loaded->actionLastTime);
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.npcManager->npcList.empty()
		&& gameManager.objectManager->objectList.size() == 1
		&& gameManager.objectManager->objectList[0] != nullptr
		&& gameManager.objectManager->objectList[0]->objName == "PERSISTENCE_DROP",
		"special death cleanup suppresses only the body and still creates one drop") && ok;
	return ok;
}

bool runPendingDeathScriptRoundTrip(GameManager& gameManager)
{
	resetRuntime(gameManager);
	auto npc = makeDyingNpc(gameManager, "ScriptedOwner", 0, "death_once.txt");
	bool ok = check(npc->result & erRunDeathScript,
		"fixture queues its death script once");
	INIReader ini;
	npc->saveToIni(&ini, "NPC000");
	ok = check(ini.GetBoolean("NPC000", "PendingDeathScript", false),
		"save records an unconsumed death-script event") && ok;
	auto loaded = loadNpcRoundTrip(gameManager, ini);
	ok = check(loaded->result & erRunDeathScript,
		"reload requeues the pending death script") && ok;
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.eventList.size() == 1
		&& gameManager.eventList[0].npc == loaded
		&& gameManager.eventList[0].scriptName == "death_once.txt"
		&& loaded->deathScript.empty(),
		"manager converts the restored death-script bit into one event") && ok;
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.eventList.size() == 1,
		"repeated manager updates do not duplicate the restored death script") && ok;

	resetRuntime(gameManager);
	auto completedNpc = makeDyingNpc(gameManager, "CompletedScriptedOwner", 0, "death_then_cleanup.txt");
	completedNpc->setTime(completedNpc->getTime() + 500);
	completedNpc->actionManager->update(500);
	INIReader completedIni;
	completedNpc->saveToIni(&completedIni, "NPC000");
	auto completedLoaded = loadNpcRoundTrip(gameManager, completedIni);
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.eventList.size() == 1
		&& gameManager.npcManager->npcList.size() == 1
		&& (completedLoaded->result & erLifeExhaust)
		&& gameManager.objectManager->objectList.empty(),
		"completed death defers cleanup until its restored death script has been queued") && ok;
	gameManager.eventList.clear();
	gameManager.npcManager->onUpdate();
	ok = check(gameManager.npcManager->npcList.empty(),
		"completed scripted death cleans up on the following manager update") && ok;
	ok = checkSingleBodyAndDrop(gameManager,
		"completed scripted death creates body and drop once after script ordering") && ok;
	return ok;
}
}

bool runNpcRuntimePersistenceTests()
{
	auto root = makeUniqueTestDirectory("jxqy_npc_runtime_persistence_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	File::setAssetsCollectionRoot(root.string());
	File::setActiveResourceRoot(root.string());
	File::setPlatformStateParentForTests(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(NpcPersistenceSaveNamespace);
	if (!prepareNpcPersistenceFixtures(root))
	{
		File::setActiveSaveNamespace("");
		File::setPlatformStateParentForTests("");
		return check(false, "write NPC persistence fixtures");
	}

	GameManager gameManager;
	prepareMap(gameManager);
	bool ok = check(
		NPCPersistence::runtimePopulationFits(
			NPCPersistence::MaximumRuntimeNpcCount - 1,
			1) &&
			!NPCPersistence::runtimePopulationFits(
				NPCPersistence::MaximumRuntimeNpcCount,
				1),
		"runtime NPC population validation rejects combined NPC and partner overflow");
	ok = runNpcCollectionLoadSafety(gameManager, root) && ok;
	ok = runCompatibleStoryEntityListLoads(gameManager, root) && ok;
	ok = runStatusDurationInputSafety() && ok;
	ok = runLegacyNpcObjectDefaults() && ok;
	ok = runPlayerChangePersistenceAndPartnerContinuity(
		gameManager,
		root) && ok;
	ok = runOfflinePartnerMagicPersistence(
		gameManager,
		root) && ok;
	ok = runEntityListScriptSavePolicy(
		gameManager,
		root) && ok;
	ok = runLiveNpcRoundTrip(gameManager) && ok;
	ok = runZeroLifeStoryNpcRoundTrip(gameManager) && ok;
	ok = runDyingAnimationRoundTrip(gameManager) && ok;
	ok = runCorruptDeathDurationFallback(gameManager) && ok;
	ok = runCleanupPendingRoundTrip(gameManager) && ok;
	ok = runReviveCountdownRoundTrip(gameManager) && ok;
	ok = runLegacyZeroLifeCompatibility(gameManager) && ok;
	ok = runSpecialDeathBodySuppressionRoundTrip(gameManager) && ok;
	ok = runPendingDeathScriptRoundTrip(gameManager) && ok;
	ok = runLoadOneNpcPartialFailureTest(gameManager) && ok;
	ok = runPreparedNpcCacheRetention(gameManager, root) && ok;
	ok = runPreparedNpcLoadWithoutSource(gameManager, root) && ok;
	ok = runLegacyOverstatedNpcCountCompatibility(
		gameManager,
		root) && ok;
	resetRuntime(gameManager);
	File::setActiveSaveNamespace("");
	File::setPlatformStateParentForTests("");
	std::filesystem::remove_all(root, errorCode);
	return ok;
}
