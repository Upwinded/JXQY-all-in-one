#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Engine/Engine.h"
#include "../Game/Config/Config.h"
#include "../Game/Data/Map.h"
#include "../Game/Data/MemoPersistence.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCManager.h"
#include "../Game/Data/NPCPersistence.h"
#include "../Game/Data/ObjectManager.h"
#include "../Game/Data/PartnerManager.h"
#include "../Game/Data/Player.h"
#include "../Game/Data/Effect.h"
#include "../Game/Data/CollisionDetector.h"
#include "../Game/GameManager/GameManager.h"
#include "../Image/IMP.h"
#include "../Image/SafeImageDecoder.h"
#include "../Resource/ResourceManager.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
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

bool runPlayerPermissionPersistence(GameManager& gameManager,
	const std::filesystem::path& root)
{
	const auto playerPath = saveGameFixturePath(root, "player0.ini");
	const bool savedCanRun = gameManager.player->canRun;
	const bool savedCanJump = gameManager.player->canJump;
	const bool savedCanFight = gameManager.player->canFight;
	bool ok = true;
	for (const bool disabled : {false, true})
	{
		const std::string value = disabled ? "1" : "0";
		const std::string allowed = disabled ? "0" : "1";
		for (const bool legacy : {true, false})
		{
			// Conflicting legacy fields must not override the canonical fields.
			const std::string fields = legacy
				? "IsRunDisabled=" + value + "\nIsJumpDisabled=" + value + "\nIsFightDisabled=" + value + "\n"
				: "CanRun=" + allowed + "\nCanJump=" + allowed + "\nCanFight=" + allowed
					+ "\nIsRunDisabled=" + allowed + "\nIsJumpDisabled=" + allowed + "\nIsFightDisabled=" + allowed + "\n";
			if (!check(writeTextFile(playerPath, "[Init]\n" + fields)
				&& gameManager.player->load(0), "load legacy or canonical player permissions"))
			{
				ok = false;
				continue;
			}
			ok = check(gameManager.player->canRun == !disabled
				&& gameManager.player->canJump == !disabled
				&& gameManager.player->canFight == !disabled,
				"legacy fallback and canonical precedence preserve player permissions") && ok;
			if (!check(gameManager.player->save(0), "save canonical player permissions"))
			{
				ok = false;
				continue;
			}
			INIReader saved("save\\game\\player0.ini");
			ok = check(saved.ParseError() == 0
				&& !saved.HasKey("Init", "IsRunDisabled")
				&& !saved.HasKey("Init", "IsJumpDisabled")
				&& !saved.HasKey("Init", "IsFightDisabled")
				&& saved.GetBoolean("Init", "CanRun", disabled) == !disabled
				&& saved.GetBoolean("Init", "CanJump", disabled) == !disabled
				&& saved.GetBoolean("Init", "CanFight", disabled) == !disabled,
				"new player saves contain only canonical permission fields") && ok;
			gameManager.player->canRun = disabled;
			gameManager.player->canJump = disabled;
			gameManager.player->canFight = disabled;
			ok = check(gameManager.player->load(0)
				&& gameManager.player->canRun == !disabled
				&& gameManager.player->canJump == !disabled
				&& gameManager.player->canFight == !disabled,
				"canonical player permissions survive save and reload") && ok;
		}
	}
	gameManager.player->canRun = savedCanRun;
	gameManager.player->canJump = savedCanJump;
	gameManager.player->canFight = savedCanFight;
	return ok;
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

bool runEntityLifecycleScriptContracts(GameManager& gameManager, const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	gameManager.varList.ensureInitialized();
	gameManager.player->setPosition({ 1, 1 }, false);
	gameManager.global.data.objName = "lifecycle-source.obj";
	const auto execute = [&](const std::string& source, bool asynchronous = false)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		const bool previousLoadAsync = Config::loadAsync;
		Config::loadAsync = asynchronous;
		const int result = gameManager.script.runScript(bytes, static_cast<int>(source.size()));
		Config::loadAsync = previousLoadAsync;
		return result;
	};
	bool ok = check(writeTextFile(root / "ini" / "obj" / "lifecycle-box.ini",
		"[Init]\nObjName=LifecycleBox\nKind=1\nMapX=4\nMapY=4\nDir=6\n") &&
		writeTextFile(root / "ini" / "npc" / "lifecycle-npc.ini",
			"[Init]\nName=LifecycleNpc\nKind=0\nLife=10\nLifeMax=10\nDir=6\n") &&
		writeTextFile(root / "script" / "common" / "lifecycle-delete.txt",
			"delcurobj(); saveobj('lifecycle-owner-deleted.obj'); assign('AfterObjectDeletion',1);"),
		"write entity creation and object-owner deletion fixtures");
	if (!ok)
	{
		return false;
	}
	ok = check(execute("addobj('lifecycle-box.ini',4,4); addobj('lifecycle-box.ini',7,7,3);") == LUA_OK &&
		gameManager.objectManager->objectList.size() == 2,
		"AddObj accepts three or four parameters") && ok;
	if (gameManager.objectManager->objectList.size() != 2)
	{
		return false;
	}
	auto owner = gameManager.objectManager->objectList[0];
	auto duplicate = gameManager.objectManager->objectList[1];
	ok = check(owner->direction == 0 && duplicate->direction == 3 &&
		owner->getPosition() == Point{ 4, 4 } && duplicate->getPosition() == Point{ 7, 7 },
		"AddObj overrides template position and defaults omitted direction to zero") && ok;
	for (const auto& object : { owner, duplicate })
	{
		const Point position = object->getPosition();
		const auto& entries = gameManager.map->dataMap.tile[position.y][position.x].objList;
		ok = check(std::count(entries.begin(), entries.end(), object) == 1,
			"AddObj indexes each object exactly once even when spawn equals template position") && ok;
		ok = check(!gameManager.map->canWalk(position),
			"newly added boxes block walking at both unchanged and overridden template positions") && ok;
	}
	gameManager.runObjScript(owner, "lifecycle-delete.txt");
	ok = check(gameManager.objectManager->objectList.size() == 1 &&
		gameManager.objectManager->objectList.front() == duplicate && !isObjectInDataMap(gameManager, owner) &&
		gameManager.map->canWalk({ 4, 4 }) && gameManager.scriptObj == nullptr &&
		gameManager.varList.getInteger("AfterObjectDeletion") == 1,
		"DelCurObj removes only its owner before the script continues and releases occupancy and context") && ok;
	INIReader deletedOwnerList("save\\game\\lifecycle-owner-deleted.obj");
	ok = check(deletedOwnerList.GetInteger("Head", "Count", -1) == 1 &&
		deletedOwnerList.GetInteger("OBJ000", "MapX", -1) == 7,
		"saving immediately after DelCurObj excludes its removed owner without waiting for a world update") && ok;
	gameManager.scriptObj = duplicate;
	ok = check(execute("delobj('');") == LUA_OK && gameManager.objectManager->objectList.empty() &&
		!isObjectInDataMap(gameManager, duplicate),
		"C++ empty DelObj targets the current object rather than all unnamed objects") && ok;
	gameManager.scriptObj = nullptr;
	ok = check(execute("delcurobj(); delobj(''); addobj('missing-lifecycle.ini',2,2); "
		"addobj('',2,2);") == LUA_OK && gameManager.objectManager->objectList.empty(),
		"missing object context and missing or empty templates do not create placeholder objects") && ok;
	ok = check(execute("addobj('lifecycle-box.ini',4,4); addobj('lifecycle-box.ini',7,7); "
		"addobj('persistence_body.ini',5,5); clearbody();") == LUA_OK &&
		gameManager.objectManager->objectList.size() == 2 && gameManager.map->dataMap.tile[5][5].objList.empty(),
		"ClearBody removes corpse objects and their occupancy without removing boxes") && ok;
	ok = check(execute("delobj('lifecyclebox');") == LUA_OK &&
		gameManager.objectManager->objectList.size() == 2 &&
		execute("delobj('LifecycleBox');") == LUA_OK && gameManager.objectManager->objectList.empty() &&
		gameManager.map->canWalk({ 4, 4 }) && gameManager.map->canWalk({ 7, 7 }),
		"named DelObj is case-sensitive and removes all matches and their occupancy") && ok;

	ok = check(execute("addnpc('lifecycle-npc.ini',3,3); addnpc('lifecycle-npc.ini',6,6,5); "
		"addnpc('missing-lifecycle.ini',2,2,0);") == LUA_OK && gameManager.npcManager->npcList.size() == 2,
		"C++ AddNpc supports an omitted direction and does not add a missing template") && ok;
	if (gameManager.npcManager->npcList.size() != 2)
	{
		return false;
	}
	auto ordinary = gameManager.npcManager->npcList[0];
	auto hidden = gameManager.npcManager->npcList[1];
	ok = check(ordinary->direction == 0 && hidden->direction == 5 && isNpcInDataMap(gameManager, ordinary),
		"AddNpc preserves explicit direction and indexes a visible spawned NPC") && ok;
	hidden->visibleVariableName = "LifecycleHidden";
	hidden->visibleVariableValue = 1;
	hidden->updateVisibleByVariable();
	auto partner = addTestNpc(gameManager, "LifecycleNpc", nkPartner, { 8, 8 });
	const auto previousNpcRuntimeProfile = gameManager.global.npcRuntimeProfile;
	gameManager.global.npcRuntimeProfile = ScriptNpcRuntimeProfile::Trilogy;
	gameManager.camera->followNPC = ordinary;
	auto controlEffect = std::make_shared<Effect>();
	gameManager.player->beginControlCharacter(ordinary, controlEffect);
	ok = check(execute("delnpc('lifecyclenpc');") == LUA_OK && gameManager.npcManager->npcList.size() == 3 &&
		execute("delnpc('LifecycleNpc');") == LUA_OK && gameManager.npcManager->npcList == std::vector<std::shared_ptr<NPC>>{partner} &&
		!isNpcInDataMap(gameManager, ordinary) && !isNpcInDataMap(gameManager, hidden) &&
		isNpcInDataMap(gameManager, partner) && gameManager.player->getControlledCharacter() == nullptr &&
		gameManager.camera->followNPC.expired(),
		"DelNpc cleans ordinary and hidden matches, releases their references and preserves the partner and its occupancy") && ok;
	ok = check(execute("setnpckind('LifecycleNpc',0); delnpc('LifecycleNpc');") == LUA_OK
		&& gameManager.npcManager->npcList.empty() && !isNpcInDataMap(gameManager, partner),
		"explicit departure changes partner Kind before removal and releases its occupancy") && ok;
	gameManager.global.npcRuntimeProfile = ScriptNpcRuntimeProfile::Legacy;
	auto departingPartner = addTestNpc(gameManager, "LifecycleNpc", nkPartner, { 8, 8 });
	gameManager.camera->followNPC = departingPartner;
	gameManager.player->beginControlCharacter(departingPartner, controlEffect);
	ok = check(execute("delnpc('LifecycleNpc');") == LUA_OK
		&& gameManager.npcManager->npcList == std::vector<std::shared_ptr<NPC>>{departingPartner}
		&& isNpcInDataMap(gameManager, departingPartner),
		"Sword II also preserves partners until the script explicitly changes Kind") && ok;
	ok = check(execute("setnpckind('LifecycleNpc',0); delnpc('LifecycleNpc');") == LUA_OK
		&& gameManager.npcManager->npcList.empty() && !isNpcInDataMap(gameManager, departingPartner)
		&& gameManager.camera->followNPC.expired() && gameManager.player->getControlledCharacter() == nullptr,
		"explicit Sword II departure releases the former partner's occupancy, camera and control references") && ok;
	gameManager.global.npcRuntimeProfile = previousNpcRuntimeProfile;
	gameManager.player->direction = 4;
	ok = check(execute("addnpc('lifecycle-npc.ini',-1,-1,-1); addobj('lifecycle-box.ini',-1,9,-1);") == LUA_OK &&
		gameManager.npcManager->npcList.size() == 1 && gameManager.objectManager->objectList.size() == 1 &&
		gameManager.npcManager->npcList.front()->getPosition() == Point{ 1, 1 } &&
		gameManager.npcManager->npcList.front()->direction == 4 &&
		gameManager.objectManager->objectList.front()->getPosition() == Point{ 1, 9 } &&
		gameManager.objectManager->objectList.front()->direction == 4,
		"legacy negative spawn components inherit player coordinates and direction independently") && ok;
	resetRuntime(gameManager);

	for (bool asynchronous : { false, true })
	{
		ok = check(execute("addobj('lifecycle-box.ini',4,4);") == LUA_OK &&
			gameManager.objectManager->objectList.size() == 1,
			"create object before loading and saving its list") && ok;
		gameManager.global.data.objName = "lifecycle-source.obj";
		ok = check(execute("loadobj(); loadobj(''); loadobj('missing-lifecycle.obj'); "
			"saveobj(); saveobj(''); saveobj('lifecycle-renamed.obj');", asynchronous) == LUA_OK &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.global.data.objName == "lifecycle-renamed.obj",
			"LoadObj empty or missing input preserves the list and SaveObj explicitly renames after saving") && ok;
		INIReader sourceList("save\\game\\lifecycle-source.obj");
		INIReader renamedList("save\\game\\lifecycle-renamed.obj");
		ok = check(sourceList.GetInteger("Head", "Count", -1) == 1 &&
			renamedList.Get("OBJ000", "ObjName", "") == "LifecycleBox",
			"default and explicit SaveObj persist the actual list") && ok;
		gameManager.objectManager->clearObj();
		if (asynchronous)
		{
			gameManager.scriptAPI.loadObjectAsync("lifecycle-renamed.obj");
			gameManager.scriptAPI.loadObjectAsync("");
			gameManager.scriptAPI.loadObjectAsync("missing-lifecycle.obj");
		}
		else
		{
			ok = check(gameManager.scriptAPI.loadObject("lifecycle-renamed.obj"),
				"synchronous LoadObj loads a saved object list") && ok;
		}
		ok = check(gameManager.objectManager->objectList.size() == 1 &&
			gameManager.global.data.objName == "lifecycle-renamed.obj" && !gameManager.map->canWalk({ 4, 4 }),
			"sync and async object reload restore the saved box and its blocking occupancy") && ok;
		gameManager.objectManager->clearObj();
	}
	ok = check(execute("lodaobj('lifecycle-renamed.obj');") == LUA_OK &&
		gameManager.objectManager->objectList.size() == 1,
		"LodaObj compatibility alias loads a saved object list") && ok;
	gameManager.objectManager->clearObj();
	return ok;
}

bool runEntityPropertyScriptContracts(GameManager& gameManager, const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	gameManager.varList.ensureInitialized();
	gameManager.player->npcName = "PropertyPlayer";
	gameManager.global.data.npcName = "properties.npc";
	gameManager.global.data.objName = "properties.obj";
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	const std::string offlineList = "[Head]\nCount=2\n"
		"[NPC000]\nName=Offline\nKind=1\nLife=10\nLifeMax=10\nScriptFile=Old.lua\n"
		"[NPC001]\nName=Offline\nKind=1\nLife=10\nLifeMax=10\n";
	bool ok = check(writeTextFile(root / "ini" / "save" / "offline-binding.npc", offlineList) &&
		writeTextFile(root / "ini" / "obj" / "property-box.ini",
			"[Init]\nObjName=PropertyBox\nKind=1\nScriptFile=Old.lua\n") &&
		writeTextFile(root / "script" / "common" / "BoundDeath.lua", "assign('BoundDeathReached',1);") &&
		writeTextFile(root / "script" / "common" / "ChangedCase.lua", "assign('OfflineBindingReached',1);") &&
		writeTextFile(root / "script" / "common" / "BoundObject.lua", "assign('BoundObjectReached',1);"),
		"write property and offline binding fixtures");
	if (!ok)
	{
		return false;
	}
	auto first = addTestNpc(gameManager, "PropertyTwin", nkNormal, { 2, 2 });
	auto second = addTestNpc(gameManager, "PropertyTwin", nkNormal, { 3, 3 });
	auto hidden = addTestNpc(gameManager, "PropertyTwin", nkNormal, { 4, 4 });
	auto lowerCase = addTestNpc(gameManager, "propertytwin", nkNormal, { 5, 5 });
	hidden->visibleVariableName = "PropertyVisibility";
	hidden->visibleVariableValue = 1;
	hidden->updateVisibleByVariable();
	ok = check(execute("setnpcscript('PropertyTwin','First.lua'); setnpcdeathscript('PropertyTwin','FirstDeath.lua');") == LUA_OK &&
		first->scriptFile == "First.lua" && first->deathScript == "FirstDeath.lua" &&
		second->scriptFile.empty() && hidden->deathScript.empty() && lowerCase->scriptFile.empty(),
		"singular named script setters select only the first exact-name NPC") && ok;
	gameManager.scriptNPC = second;
	ok = check(execute("setnpcscript('','Owner.lua'); setnpcdeathscript('','OwnerDeath.lua');") == LUA_OK &&
		second->scriptFile == "Owner.lua" && second->deathScript == "OwnerDeath.lua" && first->scriptFile == "First.lua",
		"empty-name binding setters use the current NPC script owner") && ok;
	gameManager.scriptNPC = nullptr;
	ok = check(execute("setallnpcscript('PropertyTwin','BoundClick.lua'); "
		"setallnpcdeathscript('PropertyTwin','BoundDeath.lua'); setnpckind('PropertyTwin',1); "
		"setnpcrelation('PropertyTwin',2); setnpcdir('PropertyTwin',6); setnpcpos('PropertyTwin',7,8);") == LUA_OK &&
		first->scriptFile == "BoundClick.lua" && second->scriptFile == "BoundClick.lua" &&
		hidden->scriptFile == "BoundClick.lua" && hidden->deathScript == "BoundDeath.lua" &&
		first->kind == 1 && second->kind == 1 && hidden->kind == 1 &&
		first->relation == 2 && second->relation == 2 && hidden->relation == 2 &&
		lowerCase->kind == nkNormal && lowerCase->scriptFile.empty() &&
		first->direction == 6 && first->getPosition() == Point{ 7, 8 } && second->getPosition() == Point{ 3, 3 } &&
		gameManager.map->dataMap.tile[2][2].npcList.empty() && isNpcInDataMap(gameManager, first),
		"all-name bindings and kind/relation include hidden matches, but position/direction only change the first") && ok;
	ok = check(execute("shownpc('PropertyTwin',0);") == LUA_OK && hidden->scriptHidden &&
		!first->scriptHidden && !second->scriptHidden && !isNpcInDataMap(gameManager, hidden),
		"ShowNpc uses the last match and keeps variable-hidden NPCs out of map occupancy") && ok;
	gameManager.scriptNPC = first;
	ok = check(execute("setnpcdir(-1); setnpcpos(-1,9);") == LUA_OK &&
		first->direction == 6 && first->getPosition() == Point{ 7, 9 },
		"owner-relative negative direction and position components retain their previous values") && ok;
	gameManager.scriptNPC = nullptr;
	ok = check(execute("addobj('property-box.ini',10,10); addobj('property-box.ini',11,11); "
		"setobjscript('PropertyBox','BoundObject.lua'); setobjofs('PropertyBox',-3,5);") == LUA_OK &&
		gameManager.objectManager->objectList.size() == 2,
		"create and bind a named object for persistence") && ok;
	if (gameManager.objectManager->objectList.size() != 2)
	{
		return false;
	}
	auto object = gameManager.objectManager->objectList[0];
	auto otherObject = gameManager.objectManager->objectList[1];
	gameManager.scriptObj = otherObject;
	ok = check(execute("setobjscript('','OwnerObject.lua'); setobjofs(2,-4);") == LUA_OK &&
		object->scriptFile == "BoundObject.lua" && object->offset.x == -3 && object->offset.y == 5 &&
		otherObject->scriptFile == "OwnerObject.lua" && otherObject->offset.x == 2 && otherObject->offset.y == -4 &&
		object->getPosition() == Point{ 10, 10 } && isObjectInDataMap(gameManager, object),
		"object bindings select the first name or owner and offsets do not change occupancy") && ok;
	gameManager.scriptObj = nullptr;
	ok = check(execute("savenpc('properties.npc'); saveobj('properties.obj'); "
		"setnpcscript('Offline','ChangedCase.lua','offline-binding.npc'); "
		"setnpcdeathscript('Offline','ChangedDeath.lua','offline-binding.npc');") == LUA_OK,
		"save changed live fields and update an offline NPC list") && ok;
	INIReader templateList("ini\\save\\offline-binding.npc");
	INIReader savedList("save\\game\\offline-binding.npc");
	ok = check(templateList.Get("NPC000", "ScriptFile", "") == "Old.lua" &&
		!templateList.HasKey("NPC001", "ScriptFile") &&
		savedList.Get("NPC000", "ScriptFile", "") == "ChangedCase.lua" &&
		savedList.Get("NPC001", "ScriptFile", "") == "ChangedCase.lua" &&
		savedList.Get("NPC001", "DeathScript", "") == "ChangedDeath.lua" && first->scriptFile == "BoundClick.lua" &&
		gameManager.global.data.npcName == "properties.npc",
		"offline setters update all matches and absent keys in the save copy without changing the template or live list") && ok;
	for (bool asynchronous : { false, true })
	{
		resetRuntime(gameManager);
		if (asynchronous)
		{
			gameManager.scriptAPI.loadNPCAsync("properties.npc");
			gameManager.scriptAPI.loadObjectAsync("properties.obj");
		}
		else
		{
			ok = check(gameManager.scriptAPI.loadNPC("properties.npc") && gameManager.scriptAPI.loadObject("properties.obj"),
				"synchronously reload changed entity properties") && ok;
		}
		auto twins = gameManager.npcManager->findNPC("PropertyTwin");
		auto loadedObject = gameManager.objectManager->findObj("PropertyBox");
		ok = check(twins.size() == 3 && loadedObject != nullptr,
			"sync and async property reload restore NPCs and objects") && ok;
		if (twins.size() != 3 || loadedObject == nullptr)
		{
			return false;
		}
		ok = check(twins[0]->scriptFile == "BoundClick.lua" && twins[0]->deathScript == "BoundDeath.lua" &&
			twins[0]->kind == 1 && twins[0]->relation == 2 && twins[0]->direction == 6 &&
			twins[0]->getPosition() == Point{ 7, 9 } && !twins[2]->scriptHidden && !twins[2]->isVisibleByVariable &&
			loadedObject->scriptFile == "BoundObject.lua" && loadedObject->offset.x == -3 && loadedObject->offset.y == 5,
			"binding, kind, relation, position, direction and offsets round-trip; script hiding is transient but variable hiding persists") && ok;
		gameManager.runNPCDeathScript(twins[0], twins[0]->deathScript, "properties.map");
		gameManager.runObjScript(loadedObject, loadedObject->scriptFile);
		ok = check(gameManager.varList.getInteger("BoundDeathReached") == 1 &&
			gameManager.varList.getInteger("BoundObjectReached") == 1,
			"reloaded death and object bindings execute their actual script files") && ok;
		gameManager.varList.setInteger("BoundDeathReached", 0);
		gameManager.varList.setInteger("BoundObjectReached", 0);
		if (asynchronous)
		{
			gameManager.scriptAPI.loadNPCAsync("offline-binding.npc");
		}
		else
		{
			ok = check(gameManager.scriptAPI.loadNPC("offline-binding.npc"),
				"load the NPC list changed while another map was active") && ok;
		}
		auto offline = gameManager.npcManager->findNPC("Offline");
		ok = check(offline.size() == 2 && offline.front()->scriptFile == "ChangedCase.lua" &&
			offline.back()->deathScript == "ChangedDeath.lua", "offline changes survive the next list load") && ok;
		if (!offline.empty())
		{
			gameManager.runNPCScript(offline.front());
			ok = check(gameManager.varList.getInteger("OfflineBindingReached") == 1,
				"interacting with a later-loaded NPC runs its newly bound script") && ok;
			gameManager.varList.setInteger("OfflineBindingReached", 0);
		}
	}
	const std::string malformedCases[] =
	{
		"[Head]\nCount=4097\n[NPC000]\nName=Offline\nScriptFile=Keep.lua\n",
		"[Head]\nCount=1invalid\n[NPC000]\nName=Offline\nScriptFile=Keep.lua\n",
	};
	for (const auto& malformed : malformedCases)
	{
		ok = check(writeTextFile(saveGameFixturePath(root, "invalid-binding.npc"), malformed),
			"write invalid offline count") && ok;
		execute("setnpcscript('Offline','MustNotChange.lua','invalid-binding.npc');");
		INIReader unchanged("save\\game\\invalid-binding.npc");
		ok = check(unchanged.Get("NPC000", "ScriptFile", "") == "Keep.lua",
			"offline script editing rejects oversized or partially numeric Count before writing") && ok;
	}
	ok = check(writeTextFile(saveGameFixturePath(root, "missing-binding-name.npc"),
		"[Head]\nCount=2\n[NPC000]\nKind=1\n"), "write missing entity name and section") && ok;
	execute("setnpcscript('','MustNotCreate.lua','missing-binding-name.npc');");
	INIReader missingName("save\\game\\missing-binding-name.npc");
	ok = check(!missingName.HasKey("NPC000", "ScriptFile") && !missingName.HasSection("NPC001"),
		"an empty offline target name must not create phantom entities from absent names or sections") && ok;
	ok = check(writeTextFile(saveGameFixturePath(root, "invalid-binding.obj"),
		"[Head]\nCount=4097\n[OBJ000]\nObjName=Offline\nScriptFile=Keep.lua\n") &&
		writeTextFile(saveGameFixturePath(root, "missing-binding-name.obj"),
			"[Head]\nCount=2\n[OBJ000]\nKind=1\n") &&
		writeTextFile(root / "ini" / "save" / "offline-binding.obj",
			"[Head]\nCount=2\n[OBJ000]\nObjName=Offline\nScriptFile=Old.lua\n[OBJ001]\nObjName=Offline\n"),
		"write offline object binding boundary fixtures") && ok;
	execute("setobjscript('Offline','MustNotChange.lua','invalid-binding.obj'); "
		"setobjscript('','MustNotCreate.lua','missing-binding-name.obj'); "
		"setobjscript('Offline','BoundObject.lua','offline-binding.obj');");
	INIReader invalidObject("save\\game\\invalid-binding.obj");
	INIReader missingObjectName("save\\game\\missing-binding-name.obj");
	INIReader offlineObject("save\\game\\offline-binding.obj");
	ok = check(invalidObject.Get("OBJ000", "ScriptFile", "") == "Keep.lua" &&
		!missingObjectName.HasKey("OBJ000", "ScriptFile") && !missingObjectName.HasSection("OBJ001"),
		"object script editing shares the count and explicit-name checks") && ok;
	ok = check(offlineObject.Get("OBJ000", "ScriptFile", "") == "BoundObject.lua" &&
		offlineObject.Get("OBJ001", "ScriptFile", "") == "BoundObject.lua",
		"valid offline object binding still updates all matches and adds missing script keys") && ok;
	resetRuntime(gameManager);
	return ok;
}

bool runEmptyNpcScriptLoad(GameManager& gameManager, const std::filesystem::path& root)
{
	const std::string savedList = "[Head]\nCount=0\n";
	bool ok = check(writeTextFile(saveGameFixturePath(root, "before-clear.npc"), savedList),
		"write the previous NPC list before a scripted clear");
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	for (bool asynchronous : { false, true })
	{
		resetRuntime(gameManager);
		prepareMap(gameManager);
		gameManager.varList.ensureInitialized();
		gameManager.global.data.npcName = "before-clear.npc";
		gameManager.global.data.mapName = "clear-scene.map";
		gameManager.global.data.objName = "clear-scene.obj";
		gameManager.traps.set("clear-scene.map", 7, "keep-trap.txt");
		gameManager.traps.markTriggered(7);
		gameManager.player->setPosition({ 1, 2 }, false);
		const auto mapData = gameManager.map->data;
		auto ordinary = addTestNpc(gameManager, "ClearOwner", nkBattle, { 2, 2 });
		auto hidden = addTestNpc(gameManager, "HiddenNpc", nkNormal, { 4, 4 });
		hidden->visibleVariableName = "ClearVisibility";
		hidden->visibleVariableValue = 1;
		hidden->updateVisibleByVariable();
		auto partner = addTestNpc(gameManager, "KeptPartner", nkPartner, { 3, 3 });
		partner->life = 7;
		partner->direction = 5;
		auto object = std::make_shared<Object>();
		object->setPosition({ 6, 6 });
		gameManager.objectManager->objectList.push_back(object);
		gameManager.objectManager->addChild(object);
		gameManager.map->addObjectToDataMap(object->getPosition(), object);
		gameManager.camera->followNPC = ordinary;
		auto controlEffect = std::make_shared<Effect>();
		gameManager.player->beginControlCharacter(ordinary, controlEffect);

		if (asynchronous)
		{
			gameManager.scriptAPI.loadNPCAsync("missing-clear-list.npc");
			ok = check(containsNpc(gameManager, ordinary) && containsNpc(gameManager, hidden) &&
				containsNpc(gameManager, partner) && gameManager.global.data.npcName == "before-clear.npc",
				"async missing named NPC list preserves the live collection and source name") && ok;
			gameManager.scriptAPI.loadNPCAsync("");
		}
		else
		{
			ok = check(execute("loadnpc(''); assign('AfterEmptyNpcList',1);") == LUA_OK &&
				gameManager.varList.getInteger("AfterEmptyNpcList") == 1,
				"explicit empty LoadNpc commits before the next Lua statement") && ok;
		}
		ok = check(gameManager.npcManager->npcList.size() == 1 &&
			containsNpc(gameManager, partner) && !containsNpc(gameManager, ordinary) &&
			!containsNpc(gameManager, hidden) && gameManager.global.data.npcName.empty(),
			"sync and async empty LoadNpc clear visible and hidden ordinary NPCs and reset the list name") && ok;
		ok = check(partner->life == 7 && partner->direction == 5 &&
			partner->getPosition().x == 3 && partner->getPosition().y == 3 &&
			isNpcInDataMap(gameManager, partner) && !isNpcInDataMap(gameManager, ordinary) &&
			gameManager.player->getControlledCharacter() == nullptr && gameManager.camera->followNPC.expired(),
			"empty NPC replacement retains partner state and occupancy and detaches removed controlled/camera targets") && ok;
		ok = check(gameManager.map->data == mapData && isObjectInDataMap(gameManager, object) &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.player->getPosition().x == 1 && gameManager.player->getPosition().y == 2 &&
			gameManager.global.data.mapName == "clear-scene.map" &&
			gameManager.global.data.objName == "clear-scene.obj" &&
			gameManager.traps.get("clear-scene.map", 7) == "keep-trap.txt" && gameManager.traps.hasTriggered(7),
			"empty NPC replacement leaves the map, objects, player position and traps unchanged") && ok;
		gameManager.scriptAPI.saveNPC("");
		INIReader previous("save\\game\\before-clear.npc");
		ok = check(previous.GetInteger("Head", "Count", -1) == 0,
			"default SaveNpc after clearing cannot overwrite the previous named list") && ok;
	}

	ok = check(writeTextFile(root / "script" / "common" / "clear-death.txt",
		"loadnpc(''); setnpcdir('',6); setnpcpos('KeptPartner',8,8); assign('AfterDeathClear',1);") &&
		writeTextFile(root / "ini" / "npc" / "clear-clone.ini",
			"[Init]\nName=ClearClone\nKind=1\nLife=10\nLifeMax=10\n"),
		"write death-script clear and clone fixtures") && ok;
	auto deathOwner = addTestNpc(gameManager, "DeathClearOwner", nkBattle, { 2, 2 });
	gameManager.runNPCDeathScript(deathOwner, "clear-death.txt", "clear-scene.map");
	ok = check(!containsNpc(gameManager, deathOwner) && deathOwner->direction == 6 &&
		gameManager.varList.getInteger("AfterDeathClear") == 1 &&
		gameManager.scriptNPC == nullptr && !gameManager.inEvent &&
		gameManager.npcManager->npcList.size() == 1 &&
		gameManager.npcManager->npcList.front()->getPosition().x == 8,
		"a death script can clear its owner, continue with the retained partner and release its script context") && ok;
	ok = check(execute("addnpc('clear-clone.ini',4,4,0); addnpc('clear-clone.ini',5,5,0); "
		"loadnpc(''); addnpc('clear-clone.ini',6,6,0);") == LUA_OK &&
		countNpcKind(gameManager, nkBattle) == 1 && countNpcKind(gameManager, nkPartner) == 1,
		"successive clone batches separated by empty LoadNpc do not accumulate previous NPCs") && ok;
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
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	ok = check(execute("mergenpc('valid.npc'); savenpc();") == LUA_OK &&
		gameManager.global.data.npcName == "previous.npc" &&
		containsNpc(gameManager, originalNpc) && containsNpc(gameManager, originalPartner) &&
		countNpcKind(gameManager, nkBattle) == 2,
		"Lua MergeNpc appends immediately and default SaveNpc retains the base list name") && ok;
	INIReader mergedSave("save\\game\\previous.npc");
	INIReader mergeSource("save\\game\\valid.npc");
	ok = check(mergedSave.GetInteger("Head", "Count", -1) == 2 &&
		mergeSource.GetInteger("Head", "Count", -1) == 1,
		"default SaveNpc writes the merged non-partner list without overwriting the merge source") && ok;
	ok = check(execute("loadnpc('missing-list-contract.npc'); loadnpc(); "
		"assign('AfterMissingNpcList',1);") == LUA_OK &&
		gameManager.varList.getInteger("AfterMissingNpcList") == 1 &&
		gameManager.global.data.npcName == "previous.npc" &&
		containsNpc(gameManager, originalNpc) && containsNpc(gameManager, originalPartner) &&
		countNpcKind(gameManager, nkBattle) == 2,
		"missing files and absent LoadNpc arguments preserve the live list and Lua continuation") && ok;
	ok = check(execute("savenpc('merged-copy.npc'); loadmapnpc('merged-copy.npc');") == LUA_OK &&
		gameManager.global.data.npcName == "merged-copy.npc" &&
		!containsNpc(gameManager, originalNpc) && containsNpc(gameManager, originalPartner) &&
		countNpcKind(gameManager, nkBattle) == 2,
		"explicit SaveNpc rebinds and LoadMapNpc reloads the committed merged list while preserving partners") && ok;
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
			preparedLoad.needsNormalization() &&
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

bool runPlayerChangeAttributeIsolation(
	GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	gameManager.magicManager.freeResource();
	gameManager.goodsManager.freeResource();
	const std::string targetEquipment =
		"[Init]\nName=SwitchTargetEquipment\nKind=1\nPart=Head\n"
		"LifeMax=50\nThewMax=40\nManaMax=30\n";
	const std::string sourceEquipment =
		"[Init]\nName=SwitchSourceEquipment\nKind=1\nPart=Head\n"
		"MagicIniWhenUse=switch_source_magic.ini\n";
	bool ok = check(
		writeTextFile(root / "ini/goods/switch_target.ini", targetEquipment) &&
		writeTextFile(root / "ini/goods/switch_source.ini", sourceEquipment) &&
		writeTextFile(root / "ini/magic/switch_source_magic.ini",
			"[Init]\nName=SwitchSourceMagic\nMoveKind=2\n[Level1]\nMoveKind=2\n"),
		"write player-change attribute isolation fixtures");
	gameManager.varList.ensureInitialized();
	Script script;
	const auto changePlayer = [&](const std::string& command)
	{
		const std::string text = command + " assign('switch_continued', 1);";
		auto bytes = std::make_unique<char[]>(text.size());
		std::copy(text.begin(), text.end(), bytes.get());
		gameManager.varList.setInteger("switch_continued", 0);
		ok = check(script.runScript(bytes, static_cast<int>(text.size())) == LUA_OK &&
			gameManager.varList.getInteger("switch_continued") == 1,
			"PlayerChange returns synchronously to the calling Lua script") && ok;
	};
	const auto equip = [&](const std::string& fileName)
	{
		gameManager.goodsManager.freeResource();
		auto& item = gameManager.goodsManager.goodsList[
			gameManager.goodsManager.equipIndex(0)];
		item.iniFile = fileName;
		item.number = 1;
		item.goods = std::make_shared<Goods>();
		item.goods->initFromIni(fileName);
		gameManager.player->resetEquipmentGrantedMagicSync();
		gameManager.player->calInfo();
		return item.goods->loadSucceeded;
	};
	gameManager.player->lifeMax = 100;
	gameManager.player->thewMax = 100;
	gameManager.player->manaMax = 100;
	gameManager.player->npcName = "SwitchTarget";
	ok = check(equip("switch_target.ini"), "load target equipment") && ok;
	gameManager.player->life = 140;
	gameManager.player->thew = 130;
	gameManager.player->mana = 120;
	ok = check(gameManager.player->save(1) &&
		gameManager.magicManager.save(1) && gameManager.goodsManager.save(1),
		"save target character with equipment-expanded current attributes") && ok;

	gameManager.global.data.characterIndex = 0;
	gameManager.player->npcName = "SwitchSource";
	ok = check(equip("switch_source.ini"), "load source equipment") && ok;
	gameManager.player->life = 80;
	gameManager.player->thew = 70;
	gameManager.player->mana = 60;
	ok = check(gameManager.magicManager.findPrimaryMagic("switch_source_magic.ini") != nullptr,
		"source equipment actually grants its magic before switching") && ok;
	changePlayer("playerchange();");
	ok = check(gameManager.global.data.characterIndex == 0 && gameManager.player->npcName == "SwitchSource",
		"PlayerChange with no arguments preserves the current character") && ok;
	changePlayer("playerchange('1', 123);");
	ok = check(gameManager.global.data.characterIndex == 1 &&
		gameManager.player->npcName == "SwitchTarget",
		"attribute isolation switches to the target character") && ok;
	ok = check(gameManager.player->life == 140 &&
		gameManager.player->thew == 130 && gameManager.player->mana == 120,
		"player change does not clamp target current attributes against outgoing equipment") && ok;
	ok = check(gameManager.player->getLifeMax() == 150 &&
		gameManager.player->getThewMax() == 140 && gameManager.player->getManaMax() == 130,
		"player change rebuilds maxima from target equipment") && ok;
	ok = check(gameManager.magicManager.findPrimaryMagic("switch_source_magic.ini") == nullptr,
		"player change does not insert outgoing equipment magic into the target list") && ok;
	changePlayer("playerchange(0);");
	ok = check(gameManager.global.data.characterIndex == 0 &&
		gameManager.player->npcName == "SwitchSource" &&
		gameManager.player->life == 80 && gameManager.player->thew == 70 &&
		gameManager.player->mana == 60 &&
		gameManager.magicManager.findPrimaryMagic("switch_source_magic.ini") != nullptr,
		"switching back restores the outgoing snapshot and its own equipment magic") && ok;
	changePlayer("playerchange(1);");
	ok = check(gameManager.player->life == 140 &&
		gameManager.player->thew == 130 && gameManager.player->mana == 120 &&
		gameManager.magicManager.findPrimaryMagic("switch_source_magic.ini") == nullptr,
		"repeated player change preserves the saved target without cross-character contamination") && ok;
	ok = check(gameManager.player->save(2) &&
		writeTextFile(saveGameFixturePath(root, "magic2.ini"), "[Head]\nCount=0\n") &&
		writeTextFile(saveGameFixturePath(root, "goods2.ini"), "[Head\nCount=0\n"),
		"prepare a later Goods failure for the equipment-expanded outgoing character") && ok;
	changePlayer("playerchange(2);");
	ok = check(gameManager.global.data.characterIndex == 1 &&
		gameManager.player->npcName == "SwitchTarget" &&
		gameManager.player->life == 140 && gameManager.player->thew == 130 &&
		gameManager.player->mana == 120 &&
		gameManager.magicManager.findPrimaryMagic("switch_source_magic.ini") == nullptr,
		"failed player change restores the complete outgoing attributes and equipment list") && ok;
	gameManager.magicManager.freeResource();
	gameManager.goodsManager.freeResource();
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

template <class Actor>
class BoundedDeathActor final : public Actor
{
public:
	int deathEntries = 0;
	bool recursionCapped = false;
	bool nestedEntryKeptFrozen = false;
	void setJumpPhaseForTest(unsigned int phase)
	{
		this->jumpState = phase;
	}

	void beginDie() override
	{
		++deathEntries;
		if (deathEntries > 6)
		{
			recursionCapped = true;
			return;
		}
		if (deathEntries > 1 && this->frozen)
		{
			nestedEntryKeptFrozen = true;
		}
		Actor::beginDie();
	}
};

bool runDeathReentryContracts(GameManager& gameManager, const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	const auto originalPlayer = gameManager.player;
	const std::string projectile = "[Init]\nMoveKind=2\nSpeed=8\nLifeFrame=100\n";
	if (!check(writeTextFile(root / "ini/magic/DeathLoop.ini", projectile + "DieAfterUse=1\n") &&
		writeTextFile(root / "ini/magic/DeathDerived.ini", projectile +
			"RandMagicProbability=100\nRandMagicFile=DeathLoop.ini\n"),
		"write direct and derived DieAfterUse death magic fixtures"))
	{
		return false;
	}
	bool ok = true;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	const auto runActor = [&](auto actor)
	{
		const auto player = std::dynamic_pointer_cast<Player>(actor);
		const std::string label = player != nullptr ? "Player " : "NPC ";
		if (player != nullptr)
		{
			gameManager.player = player;
		}
		else
		{
			gameManager.npcManager->addNPC(actor);
		}
		actor->lifeMax = actor->life = 1000;
		actor->attackLevel = 1;
		actor->setPosition({ 4, 4 }, false);
		if (player != nullptr)
		{
			player->calInfo();
		}
		auto deathImage = std::make_shared<IMPImage>();
		deathImage->directions = 8;
		deathImage->interval = 50;
		deathImage->frame.resize(16);
		actor->res.death.imagePackage = deathImage;
		for (const std::string file : { "DeathLoop.ini", "DeathDerived.ini" })
		{
			for (int cycle = 0; cycle < 2; ++cycle)
			{
				actor->reviveFromDeath();
				actor->deathEntries = 0;
				actor->recursionCapped = false;
				actor->nestedEntryKeptFrozen = false;
				actor->magicToUseWhenDeath = gameManager.magicManager.loadAttackMagic(file);
				ok = check(actor->magicToUseWhenDeath != nullptr && actor->magicToUseWhenDeath->loadSucceeded,
					"actual reader resolves the death magic including its derived child") && ok;
				actor->frozen = true;
				actor->frozenLastTime = 1000;
				actor->frozenVisualEffect = false;
				actor->life = 0;
				gameManager.effectManager->effectList.clear();
				actor->handleDeath();
				const std::size_t expectedEffects = file == "DeathLoop.ini" ? 1 : 2;
				ok = check(!actor->recursionCapped && actor->deathEntries == 2 &&
					gameManager.effectManager->effectList.size() == expectedEffects && actor->isDying(),
					(label + file + " invokes each death effect once and rejects recursive death side effects").c_str()) && ok;
				ok = check(actor->nestedEntryKeptFrozen && !actor->frozen,
					(label + " keeps pre-death status during death magic, then clears it on entering the death action").c_str()) && ok;
			}
		}
		actor->reviveFromDeath();
		actor->deathEntries = 0;
		actor->magicToUseWhenDeath = gameManager.magicManager.loadAttackMagic("CounterDeath.ini");
		actor->res.death.imagePackage.reset();
		gameManager.effectManager->effectList.clear();
		actor->handleDeath();
		ok = check(!actor->isDying() && gameManager.effectManager->effectList.empty(),
			"the existing missing-animation refusal remains unchanged") && ok;
		actor->res.death.imagePackage = deathImage;
		actor->handleDeath();
		ok = check(actor->isDying() && gameManager.effectManager->effectList.size() == 1,
			"a refused death attempt does not lock a later valid death transition") && ok;
		for (int mode : { 0, 1, 2 })
		{
			for (int facing = 0; facing < 8; ++facing)
			{
				for (int attackerState : { 0, 1, 2 }) // absent, alive reference, expired reference
				{
					for (bool movingProjectile : { false, true })
					{
						actor->reviveFromDeath();
						actor->deathEntries = 0;
						actor->clearCombatTargetMemory();
						actor->direction = facing;
						actor->magicDirectionWhenDeath = mode;
						auto attacker = std::make_shared<NPC>();
						attacker->setPosition({ 8, 8 }, false);
						attacker->removeFromDataMap();
						if (attackerState != 0)
						{
							actor->lastCombatTarget = attacker;
						}
						if (attackerState == 2)
						{
							attacker.reset();
							ok = check(actor->lastCombatTarget.expired(),
								"the expired-attacker fixture releases map occupancy ownership too") && ok;
						}
						Point incoming = Map::getTilePosition(
							Map::getSubPoint(actor->getPosition(), facing), actor->getPosition());
						incoming.y *= MapXRatio;
						actor->lastCombatMagicDirection = movingProjectile
							? PointEx{ static_cast<float>(incoming.x), static_cast<float>(incoming.y) }
							: PointEx{ 0, 0 };
						actor->hasLastCombatMagicDirection = movingProjectile;
						Point destination = Map::getSubPoint(actor->getPosition(), facing);
						if (mode == 0 && attackerState == 1)
						{
							destination = attacker->getPosition();
						}
						if (mode == 1 && movingProjectile)
						{
							destination = Map::getSubPoint(actor->getPosition(), (facing + 4) % 8);
						}
						gameManager.effectManager->effectList.clear();
						actor->life = 0;
						actor->handleDeath();
						ok = check(actor->isDying() && gameManager.effectManager->effectList.size() == 1,
							"death dispatch survives absent or expired attacker references") && ok;
						if (gameManager.effectManager->effectList.size() == 1)
						{
							const auto& effect = gameManager.effectManager->effectList.front();
							Point expected = Map::getTilePosition(destination, effect->src == destination
								? actor->getPosition() : effect->src);
							expected.y = static_cast<int>(std::round(MapXRatio * expected.y));
							const std::string detail = label + "death direction mode=" + std::to_string(mode) +
								" facing=" + std::to_string(facing) + " attacker=" + std::to_string(attackerState) +
								" moving=" + std::to_string(movingProjectile);
							ok = check(effect->flyingDirection == expected, detail.c_str()) && ok;
						}
					}
				}
			}
		}
		for (const std::string file : { "", "AbsentDeath.ini" })
		{
			actor->reviveFromDeath();
			actor->deathEntries = 0;
			actor->magicToUseWhenDeath = gameManager.magicManager.loadAttackMagic(file);
			gameManager.effectManager->effectList.clear();
			actor->handleDeath();
			ok = check(actor->isDying() && gameManager.effectManager->effectList.empty(),
				"empty or missing death magic does not block the death action") && ok;
		}
		actor->magicToUseWhenDeath = gameManager.magicManager.loadAttackMagic("CounterDeath.ini");
		if (player != nullptr)
		{
			actor->reviveFromDeath();
			actor->deathEntries = 0;
			actor->res.jump.imagePackage = deathImage;
			actor->actionManager->changeAction(acJump);
			actor->setJumpPhaseForTest(jsJumping);
			gameManager.effectManager->effectList.clear();
			actor->handleDeath();
			actor->handleDeath();
			ok = check(actor->isJumping() && gameManager.effectManager->effectList.empty(),
				"Player does not dispatch death magic while the existing airborne transition is refused") && ok;
			actor->setJumpPhaseForTest(jsDown);
			actor->handleDeath();
			ok = check(actor->isDying() && gameManager.effectManager->effectList.size() == 1,
				"Player dispatches death magic once when the jump phase permits death") && ok;
		}
		actor->magicToUseWhenDeath = gameManager.magicManager.loadAttackMagic("DeathLoop.ini");
		const auto originalAddLifeMode = gameManager.global.addLifeMode;
		for (auto mode : { ScriptAddLifeMode::PlayerRules, ScriptAddLifeMode::DirectClamp })
		{
			actor->reviveFromDeath();
			actor->deathEntries = 0;
			actor->recursionCapped = false;
			gameManager.global.addLifeMode = mode;
			actor->npcName = "DeathScriptTarget";
			gameManager.effectManager->effectList.clear();
			const std::string command = player != nullptr ? "addlife(-100000);"
				: "setnpcaction('DeathScriptTarget',11);";
			ok = check(execute(command) == LUA_OK && actor->isDying() &&
				!actor->recursionCapped && gameManager.effectManager->effectList.size() == 1,
				"noncombat Lua death dispatches a DieAfterUse death magic once without an attacker") && ok;
		}
		gameManager.global.addLifeMode = originalAddLifeMode;
	};
	runActor(std::make_shared<BoundedDeathActor<NPC>>());
	runActor(std::make_shared<BoundedDeathActor<Player>>());
	gameManager.effectManager->effectList.clear();
	gameManager.player = originalPlayer;
	resetRuntime(gameManager);
	return ok;
}

bool runYuchenDropContracts(GameManager& gameManager)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	const auto originalPlayer = gameManager.player;
	const bool originalNpcAI = gameManager.global.data.NPCAI;
	const bool originalDropDisabled = gameManager.global.data.dropDisabled;
	gameManager.global.data.NPCAI = false;
	gameManager.global.data.dropDisabled = false;
	gameManager.player = std::make_shared<Player>();
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	File::setResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(u8"江湖余尘")).u8string(),
		(assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() });
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = true;
	for (bool boss : { false, true })
	{
		INIReader npcFile(boss ? "ini/save/map010_empty.npc" : "ini/save/wudangshanding1.npc");
		const std::string expectedTable = boss ? u8"1档随机掉落-boss.ini" : u8"1档随机掉落.ini";
		int totalDrops = 0;
		int collectedCoins = 0;
		int multipleDrops = 0;
		int overlappingDrops = 0;
		for (int trial = 0; trial < 32; ++trial)
		{
			resetRuntime(gameManager);
			auto npc = std::make_shared<NPC>();
			npc->initFromIni(&npcFile, boss ? "NPC002" : "NPC001");
			gameManager.npcManager->addNPC(npc);
			npc->setPosition({ 4, 4 }, false);
			if (!boss)
			{
				ok = check(execute(u8"setnpcrelation('清水',1);") == LUA_OK,
					"use the actual Wudang challenge hostility command") && ok;
			}
			ok = check(npc->dropIni == expectedTable && npc->kind == nkBattle && npc->relation == nrHostile,
				"formal Yuchen NPC retains its published table and combat eligibility") && ok;
			// Isolate drop processing from corpse rendering, revival and the full story event.
			npc->bodyIni.clear();
			npc->deathScript.clear();
			npc->reviveMilliseconds = 0;
			npc->res.death.imagePackage = std::make_shared<IMPImage>();
			npc->res.death.imagePackage->directions = 8;
			npc->res.death.imagePackage->interval = 50;
			npc->res.death.imagePackage->frame.resize(16);
			npc->life = 0;
			npc->handleDeath();
			npc->setTime(npc->getTime() + npc->actionLastTime);
			npc->actionManager->update(npc->actionLastTime);
			gameManager.npcManager->onUpdate();
			ok = check(gameManager.npcManager->npcList.empty(),
				"actual death animation completion reaches manager drop cleanup") && ok;
			const auto originalDrops = gameManager.objectManager->objectList;
			totalDrops += static_cast<int>(originalDrops.size());
			multipleDrops += originalDrops.size() > 1 ? 1 : 0;
			if (originalDrops.size() > 1)
			{
				const auto first = originalDrops.front();
				overlappingDrops += std::all_of(originalDrops.begin(), originalDrops.end(), [&](const auto& object)
					{
						return object->getPosition() == first->getPosition() && object->getOffset() == first->getOffset();
					}) ? 1 : 0;
			}
			ok = check(gameManager.objectManager->save("yuchen-drops.obj") &&
				gameManager.objectManager->load("yuchen-drops.obj"),
				"actual multi-drop objects survive a file save and fresh load before pickup") && ok;
			const auto drops = gameManager.objectManager->objectList;
			ok = check(drops.size() == originalDrops.size(), "multi-drop reload preserves object count") && ok;
			int equipmentGroup = 0;
			int coinCounts[3] = {};
			int drugs = 0;
			for (std::size_t index = 0; index < drops.size(); ++index)
			{
				const auto object = drops[index];
				if (index < originalDrops.size())
				{
					ok = check(object != originalDrops[index] && object->scriptFile == originalDrops[index]->scriptFile &&
						object->getPosition() == originalDrops[index]->getPosition() &&
						object->getOffset() == originalDrops[index]->getOffset() && isObjectInDataMap(gameManager, object),
						"drop reload reconstructs scripts, coordinates, offsets and map indexing") && ok;
				}
				if (object->scriptFile == u8"衣-1档.txt" || object->scriptFile == u8"头-1档.txt" ||
					object->scriptFile == u8"剑-1档.txt" || (boss && object->scriptFile == u8"极-1档.txt"))
				{
					++equipmentGroup;
				}
				if (object->scriptFile == u8"药-1档.txt")
				{
					++drugs;
				}
				for (int coin = 0; coin < 3; ++coin)
				{
					if (object->scriptFile != std::to_string(coin + 1) + u8"级钱.txt")
					{
						continue;
					}
					++coinCounts[coin];
					const int moneyBefore = gameManager.player->money;
					gameManager.player->setPosition(object->getPosition(), false);
					gameManager.player->triggerObject(object);
					const int gained = gameManager.player->money - moneyBefore;
					const int minimum[] = { 10, 50, 100 };
					const int maximum[] = { 50, 100, 150 };
					ok = check(gained >= minimum[coin] && gained <= maximum[coin] &&
						!gameManager.objectManager->findObj(object),
						"formal money script rewards the player and removes only its collected object") && ok;
					++collectedCoins;
				}
			}
			ok = check(equipmentGroup <= 1 && drugs <= (boss ? 3 : 4) &&
				(coinCounts[0] == 0 || coinCounts[0] == (boss ? 7 : 1)) &&
				(coinCounts[1] == 0 || coinCounts[1] == (boss ? 3 : 2)) &&
				(coinCounts[2] == 0 || coinCounts[2] == 5),
				"actual table respects exclusive equipment group and per-entry quantities") && ok;
			const auto remaining = gameManager.objectManager->objectList.size();
			gameManager.npcManager->onUpdate();
			ok = check(gameManager.objectManager->objectList.size() == remaining,
				"later manager updates do not repeat a defeated NPC's drops") && ok;
		}
		ok = check(totalDrops > 0 && collectedCoins > 0 && multipleDrops > 0,
			"bounded formal-table trials actually exercise multi-drop and money pickup") && ok;
		std::cout << "T41 " << (boss ? "boss" : "ordinary") << " deaths=32 objects=" << totalDrops
			<< " coinsCollected=" << collectedCoins << " multiDrops=" << multipleDrops
			<< " allOverlapping=" << overlappingDrops << '\n';
	}
	resetRuntime(gameManager);
	gameManager.player = originalPlayer;
	gameManager.global.data.NPCAI = originalNpcAI;
	gameManager.global.data.dropDisabled = originalDropDisabled;
	File::setResourceFallbackRoots({});
	return ok;
}

bool runYuchenRewardContracts(GameManager& gameManager, const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	const auto originalPlayer = gameManager.player;
	const auto originalLayout = gameManager.global.goodsLayout;
	const auto originalGoods = gameManager.goodsManager.goodsList;
	gameManager.player = std::make_shared<Player>();
	gameManager.player->setPosition({ 4, 4 }, false);
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	std::vector<std::string> resourceRoots = { (assetsRoot / std::filesystem::u8path(u8"江湖余尘")).u8string(),
		(assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() };
	// Optional isolated conversion output, used before installing a verified resource repair.
	if (const char* overlay = std::getenv("JXQY_TEST_YUCHEN_REWARD_ROOT"))
	{
		resourceRoots.insert(resourceRoots.begin(), overlay);
	}
	File::setResourceFallbackRoots(resourceRoots);
	bool ok = check(writeTextFile(root / "ini/goods/reward-bag-filler.ini",
		"[Init]\nName=RewardBagFiller\nKind=2\n"), "write an isolated full-bag filler");
	const char* objectFiles[] = { u8"衣-1档.ini", u8"头-1档.ini", u8"剑-1档.ini", u8"极-1档.ini", u8"药-1档.ini" };
	const char* goodsFiles[] = { u8"goods-b06-皂罗袍.ini", u8"goods-h02-缳纱帽.ini", u8"goods-w01-柳叶剑.ini",
		u8"goods-w00-青铜剑-极.ini", u8"goods-yaowu-1-金创药（小）.ini" };
	for (int reward = 0; reward < 5; ++reward)
	{
		Goods sourceGoods;
		sourceGoods.initFromIni(goodsFiles[reward]);
		ok = check(sourceGoods.loadSucceeded, "formal reward goods definition loads") && ok;
		if (sourceGoods.loadSucceeded)
		{
			ok = check((reward != 0 || (sourceGoods.manaMax == 100 && sourceGoods.defend == 10)) &&
				(reward != 1 || (sourceGoods.defend == 5 && sourceGoods.evade == 2)) &&
				(reward != 2 || sourceGoods.attack == 25) &&
				(reward != 3 || sourceGoods.attack == 0) &&
				(reward != 4 || sourceGoods.life == 100),
				"reward attributes preserve MOD overrides and actual published MG values") && ok;
			for (const auto& package : { sourceGoods.createGoodsImage(), sourceGoods.createGoodsIcon() })
			{
				int width = 0;
				int height = 0;
				bool decoded = false;
				if (package != nullptr && !package->frame.empty())
				{
					const auto& frame = package->frame.front();
					if (!frame.pixelData.empty())
					{
						width = frame.pixelWidth;
						height = frame.pixelHeight;
						decoded = width > 0 && height > 0 && frame.pixelData.size() ==
							static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
					}
					else if (auto surface = SafeImageDecoder::loadSurface(frame.data.get(), frame.dataLen))
					{
						width = surface->w;
						height = surface->h;
						decoded = surface->pixels != nullptr && width > 0 && height > 0;
						SDL_DestroySurface(surface);
					}
				}
				ok = check(decoded, "actual reward image and icon decode through existing path fallback") && ok;
				std::cout << "T44 image reward=" << reward << " width=" << width << " height=" << height << '\n';
			}
		}
		for (int listType : { 0, 1 })
		{
			gameManager.global.goodsLayout.listType = listType;
			for (int bagState : { 0, 1, 2 })
			{
				gameManager.goodsManager.configureLayout();
				resetRuntime(gameManager);
				if (bagState != 0)
				{
					for (int index = gameManager.goodsManager.storeBegin(); index <= gameManager.goodsManager.bottomEnd(); ++index)
					{
						if (!gameManager.goodsManager.isStoreIndex(index) && !gameManager.goodsManager.isBottomIndex(index))
						{
							continue;
						}
						auto& slot = gameManager.goodsManager.goodsList[index];
						slot.iniFile = "reward-bag-filler.ini";
						slot.number = 1;
						slot.goods = std::make_shared<Goods>();
						slot.goods->initFromIni(slot.iniFile);
					}
					if (bagState == 2)
					{
						auto& slot = gameManager.goodsManager.goodsList[gameManager.goodsManager.storeBegin()];
						slot.iniFile = goodsFiles[reward];
						slot.goods = std::make_shared<Goods>(sourceGoods);
					}
				}
				auto object = gameManager.objectManager->addObject(objectFiles[reward], 4, 4, 0);
				ok = check(object != nullptr, "formal reward object template loads") && ok;
				if (object == nullptr)
				{
					continue;
				}
				ok = check(gameManager.objectManager->save("yuchen-rewards.obj") &&
					gameManager.objectManager->load("yuchen-rewards.obj") &&
					gameManager.objectManager->objectList.size() == 1,
					"reward object survives save and reload before interaction") && ok;
				if (gameManager.objectManager->objectList.size() != 1)
				{
					continue;
				}
				object = gameManager.objectManager->objectList.front();
				const int before = bagState == 2 ? 1 : 0;
				const int awarded = bagState == 0 || (bagState == 2 && listType == 0) ? 1 : 0;
				gameManager.player->triggerObject(object);
				ok = check(gameManager.goodsManager.getItemNum(goodsFiles[reward]) == before + awarded &&
					!gameManager.objectManager->findObj(object),
					"actual reward script grants only accepted goods and consumes its object") && ok;
				gameManager.player->triggerObject(object);
				ok = check(gameManager.goodsManager.getItemNum(goodsFiles[reward]) == before + awarded,
					"stale interaction cannot reward a removed object twice") && ok;
				auto prior = gameManager.goodsManager.findGoods(goodsFiles[reward]);
				const auto priorGoods = prior != nullptr ? prior->goods : nullptr;
				ok = check(gameManager.goodsManager.save(0) && gameManager.objectManager->save("yuchen-rewards.obj"),
					"save actual awarded inventory and consumed object state") && ok;
				gameManager.goodsManager.configureLayout();
				ok = check(gameManager.goodsManager.load(0) && gameManager.objectManager->load("yuchen-rewards.obj") &&
					gameManager.goodsManager.getItemNum(goodsFiles[reward]) == before + awarded &&
					gameManager.objectManager->objectList.empty(),
					"fresh inventory and object load preserves reward quantity without respawning the object") && ok;
				const auto restored = gameManager.goodsManager.findGoods(goodsFiles[reward]);
				if (before + awarded > 0)
				{
					ok = check(restored != nullptr && restored->goods != nullptr && restored->goods != priorGoods &&
						restored->goods->attack == sourceGoods.attack && restored->goods->life == sourceGoods.life &&
						restored->goods->manaMax == sourceGoods.manaMax && restored->goods->defend == sourceGoods.defend &&
						restored->goods->evade == sourceGoods.evade,
						"fresh reward goods instance retains actual published attributes") && ok;
				}
				ok = check(gameManager.goodsManager.getItemNum(u8"goods-w15-狼皮护腕-极.ini") == 0,
					"published Count=1 does not expose an extra undeclared reward section") && ok;
			}
		}
		std::cout << "T44 reward=" << reward << " layouts=2 bagStates=3\n";
	}
	resetRuntime(gameManager);
	gameManager.player = originalPlayer;
	gameManager.global.goodsLayout = originalLayout;
	gameManager.goodsManager.goodsList = originalGoods;
	File::setResourceFallbackRoots({});
	return ok;
}

bool runCounterImpactContracts(GameManager& gameManager, const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	const auto originalPlayer = gameManager.player;
	if (!check(writeTextFile(root / "ini/magic/CounterDeath.ini",
		"[Init]\nName=COUNTER_DEATH_ORDER\nMoveKind=2\nSpeed=8\nLifeFrame=100\n"),
		"write a distinct death effect to observe damage/counter ordering"))
	{
		return false;
	}
	auto counterMagic = gameManager.magicManager.loadAttackMagic("CounterCase.ini");
	auto deathMagic = gameManager.magicManager.loadAttackMagic("CounterDeath.ini");
	bool ok = check(counterMagic->loadSucceeded && deathMagic->loadSucceeded,
		"counter impact fixtures load through the actual magic reader");
	struct ImpactCase
	{
		const char* name;
		int shield;
		bool miss;
		bool blockMagic;
		bool invincible;
		bool lethal;
		int deathDirection = 0;
	};
	const ImpactCase cases[] =
	{
		{ "miss", 0, true, false, false, false },
		{ "hit", 0, false, false, false, false },
		{ "absorbed shield", 1000, false, false, false, false },
		{ "exact shield", 200, false, false, false, false },
		{ "broken shield", 50, false, false, false, false },
		{ "block magic", 0, false, true, false, false },
		{ "invincible", 0, false, false, true, false },
		{ "lethal", 0, false, false, false, true },
		{ "lethal reverse", 0, false, false, false, true, 1 },
	};
	for (bool playerTarget : { false, true })
	{
		for (const auto& impact : cases)
		{
			bool exercised = false;
			for (int attempt = 0; attempt < 64 && !exercised; ++attempt)
			{
				resetRuntime(gameManager);
				gameManager.effectManager->effectList.clear();
				gameManager.player = std::make_shared<Player>();
				std::shared_ptr<NPC> actor = playerTarget ? gameManager.player
					: addTestNpc(gameManager, "ImpactTarget", nkBattle, { 4, 4 });
				actor->kind = playerTarget ? nkPlayer : nkBattle;
				actor->relation = nrHostile;
				actor->lifeMax = 1000;
				actor->life = impact.lethal ? 1 : 1000;
				actor->evade = 0;
				actor->defend = 0;
				actor->attackLevel = 2;
				actor->invincible = impact.invincible ? 1 : 0;
				actor->shieldLife = impact.shield;
				actor->magicToUseWhenBeAttacked = counterMagic;
				actor->magicToUseWhenBeAttackedFile = "CounterCase.ini";
				actor->magicDirectionWhenBeAttacked = 0;
				actor->equipmentMagicToUseWhenAttacked.push_back({ "CounterCase.ini", counterMagic, 0 });
				actor->res.death.imagePackage = std::make_shared<IMPImage>();
				actor->res.death.imagePackage->directions = 8;
				actor->res.death.imagePackage->interval = 50;
				actor->res.death.imagePackage->frame.resize(16);
				if (playerTarget)
				{
					gameManager.player->calInfo();
					// calInfo owns equipment-derived state; the fixture supplies this channel afterwards.
					actor->equipmentMagicToUseWhenAttacked = { { "CounterCase.ini", counterMagic, 0 } };
				}
				actor->setPosition({ 4, 4 }, false);
				if (impact.lethal)
				{
					actor->magicToUseWhenDeath = deathMagic;
					actor->magicDirectionWhenDeath = impact.deathDirection;
				}
				auto block = std::make_shared<Effect>();
				if (impact.blockMagic)
				{
					block->level = 1;
					block->doing = ekFlying;
					block->magic.level[1].moveKind = mmkSelf;
					block->magic.level[1].specialKind = mskBlockDamage;
					actor->shieldEffects.push_back(block);
				}
				auto attacker = addTestNpc(gameManager, "ImpactAttacker", nkBattle, { 8, 8 });
				auto staleAttacker = addTestNpc(gameManager, "PreviousAttacker", nkBattle, { 2, 8 });
				actor->lastCombatTarget = staleAttacker;
				auto incoming = std::make_shared<Effect>();
				incoming->user = attacker;
				incoming->level = 1;
				incoming->doing = ekFlying;
				incoming->lifeTime = 1000;
				incoming->magic.attackAll = 1;
				incoming->damage = 200;
				incoming->evade = impact.miss ? -1000 : 100000;
				incoming->launcherKind = playerTarget ? lkEnemy : lkFriend;
				incoming->position = actor->getPosition();
				incoming->flyingDirection = { 0, 1 };
				ok = check(CollisionDetector::detectCollision(actor, incoming),
					"a real colliding projectile reaches the ordinary NPC/Player hurt path") && ok;
				// Player's legacy random range includes zero: observe a real hit, never assume one.
				exercised = impact.miss || impact.blockMagic || impact.invincible ||
					actor->life != (impact.lethal ? 1 : 1000) || actor->shieldLife != impact.shield;
				if (!exercised)
				{
					continue;
				}
				const std::string label = std::string(playerTarget ? "Player " : "NPC ") + impact.name;
				int counters = 0;
				for (const auto& effect : gameManager.effectManager->effectList)
				{
					if (effect->magic.iniName == "CounterCase.ini")
					{
						++counters;
						Point expectedDirection = Map::getTilePosition(attacker->getPosition(), effect->src);
						expectedDirection.y = static_cast<int>(std::round(MapXRatio * expectedDirection.y));
						ok = check(effect->user.lock() == actor && effect->flyingDirection == expectedDirection &&
							effect->level == 2 && effect->launcherKind == (playerTarget ? lkSelf : lkEnemy),
							"counter projectiles belong to the defender, aim at the attacker and retain level/faction") && ok;
					}
				}
				ok = check(counters == 2, (label + " dispatches both counter channels exactly once").c_str()) && ok;
				int damage = impact.miss || impact.blockMagic || impact.invincible ? 0 : std::max(0, 200 - impact.shield);
				if (playerTarget)
				{
					damage = static_cast<int>(std::round(damage * DAMAGE_RATE));
				}
				ok = check(actor->life == std::max(0, (impact.lethal ? 1 : 1000) - damage) &&
					actor->shieldLife == std::max(0, impact.shield - (impact.miss ? 0 : 200)),
					(label + " retains existing damage and shield arithmetic").c_str()) && ok;
				if (impact.lethal)
				{
					ok = check(actor->isDying() && gameManager.effectManager->effectList.size() == 3 &&
						gameManager.effectManager->effectList.front()->magic.iniName == "CounterDeath.ini",
						(label + " resolves death before the two counter effects, as in C#").c_str()) && ok;
					const auto death = std::find_if(gameManager.effectManager->effectList.begin(),
						gameManager.effectManager->effectList.end(), [](const auto& item)
						{
							return item->magic.iniName == "CounterDeath.ini";
						});
					if (death != gameManager.effectManager->effectList.end())
					{
						const Point destination = impact.deathDirection == 1
							? Map::getSubPoint(actor->getPosition(), 4) : attacker->getPosition();
						Point expected = Map::getTilePosition(destination, (*death)->src == destination
							? actor->getPosition() : (*death)->src);
						expected.y = static_cast<int>(std::round(MapXRatio * expected.y));
						ok = check((*death)->user.lock() == actor && (*death)->flyingDirection == expected,
							(label + " death magic targets the current attacker or the reverse incoming direction").c_str()) && ok;
					}
					gameManager.effectManager->effectList.clear();
					actor->hurt(incoming);
					actor->beginDie();
					ok = check(gameManager.effectManager->effectList.empty(),
						"a later hurt call on an already dying actor does not repeat the counter") && ok;
				}
			}
			ok = check(exercised, "bounded collision trials actually exercised the requested hit/shield branch") && ok;
		}
	}
	resetRuntime(gameManager);
	gameManager.effectManager->effectList.clear();
	gameManager.player = std::make_shared<Player>();
	gameManager.player->npcName = "CounterLevelOwner";
	gameManager.player->attackLevel = 1;
	gameManager.player->lifeMax = gameManager.player->life = 1000;
	gameManager.player->calInfo();
	auto levelNpc = addTestNpc(gameManager, "CounterLevelOwner", nkBattle, { 4, 4 });
	levelNpc->attackLevel = 1;
	levelNpc->lifeMax = levelNpc->life = 1000;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	ok = check(execute("setnpcmagictousewhenbeattacked('CounterLevelOwner','CounterCase.ini',0); "
		"setplayermagictousewhenbeattacked('CounterCase.ini',0); "
		"addnpcproperty('CounterLevelOwner','Level',1);") == LUA_OK &&
		levelNpc->attackLevel == 1 && gameManager.player->attackLevel == 1,
		"Level and AttackLevel remain independent after both counter setters") && ok;
	ok = check(execute("addnpcproperty('CounterLevelOwner','AttackLevel',2);") == LUA_OK &&
		levelNpc->attackLevel == 3 && gameManager.player->attackLevel == 3,
		"Lua can change AttackLevel after the counter file was already bound") && ok;
	const std::shared_ptr<NPC> levelActors[] = { levelNpc, gameManager.player };
	for (bool playerTarget : { false, true })
	{
		auto actor = levelActors[playerTarget ? 1 : 0];
		actor->setPosition({ 4, 4 }, false);
		auto incoming = std::make_shared<Effect>();
		incoming->level = 1;
		incoming->damage = 10;
		incoming->launcherKind = lkEnemy;
		incoming->user = levelNpc;
		gameManager.effectManager->effectList.clear();
		actor->directHurt(incoming);
		ok = check(gameManager.effectManager->effectList.size() == 1 &&
			gameManager.effectManager->effectList.front()->level == 3 &&
			gameManager.effectManager->effectList.front()->magic.level[3].effect == 33,
			"bound counter uses the updated AttackLevel without rebinding") && ok;
		actor->magicToUseWhenDeathFile = "CounterDeath.ini";
		INIReader saved;
		actor->saveToIni(&saved, "Init");
		ok = check(saved.saveToFile("save/game/counter-level.ini"),
			"write the new counter level and death file before constructing a replacement actor") && ok;
		INIReader read("save/game/counter-level.ini");
		std::shared_ptr<NPC> restored = playerTarget
			? std::static_pointer_cast<NPC>(std::make_shared<Player>()) : std::make_shared<NPC>();
		restored->initFromIni(&read, "Init");
		if (auto restoredPlayer = std::dynamic_pointer_cast<Player>(restored))
		{
			restoredPlayer->calInfo();
		}
		restored->res.death.imagePackage = std::make_shared<IMPImage>();
		restored->res.death.imagePackage->directions = 8;
		restored->res.death.imagePackage->interval = 50;
		restored->res.death.imagePackage->frame.resize(16);
		ok = check(restored != actor && restored->attackLevel == 3 && restored->magicToUseWhenDeath != nullptr &&
			restored->magicToUseWhenDeath->loadSucceeded, "fresh NPC/Player reads the actual saved level and death magic") && ok;
		gameManager.effectManager->effectList.clear();
		incoming->damage = 10000;
		restored->directHurt(incoming);
		ok = check(restored->isDying() && gameManager.effectManager->effectList.size() == 2 &&
			gameManager.effectManager->effectList.front()->magic.iniName == "CounterDeath.ini" &&
			gameManager.effectManager->effectList.back()->magic.iniName == "CounterCase.ini" &&
			gameManager.effectManager->effectList.back()->level == 3,
			"direct damage after real file reload retains level and resolves death before counter") && ok;
	}

	resetRuntime(gameManager);
	gameManager.effectManager->effectList.clear();
	gameManager.player = std::make_shared<Player>();
	gameManager.player->setPosition({ 8, 8 }, false);
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	File::setResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(u8"潇湘行")).u8string(),
		(assetsRoot / "jxqy2").u8string(), (assetsRoot / "common").u8string() });
	INIReader published("ini/save/trj-1.npc");
	auto publishedNpc = std::make_shared<NPC>();
	publishedNpc->initFromIni(&published, "NPC010");
	gameManager.npcManager->addNPC(publishedNpc);
	publishedNpc->setPosition({ 4, 4 }, false);
	ok = check(publishedNpc->npcName == u8"天忍教弟子" && publishedNpc->attackLevel == 5 &&
		publishedNpc->magicToUseWhenBeAttackedFile == u8"magic-沙暴攻击.ini" &&
		publishedNpc->magicToUseWhenBeAttacked != nullptr && publishedNpc->magicToUseWhenBeAttacked->loadSucceeded &&
		execute(u8"setnpcrelation('天忍教弟子',1);") == LUA_OK && publishedNpc->relation == nrHostile,
		"actual Xiaoxiang first-floor NPC and counter magic load with the entrance script's hostility change") && ok;
	INIReader savedPublished;
	publishedNpc->saveToIni(&savedPublished, "Init");
	ok = check(savedPublished.saveToFile("save/game/counter-published.ini"), "persist the actual configured NPC") && ok;
	for (bool reload : { false, true })
	{
		auto actor = publishedNpc;
		if (reload)
		{
			INIReader read("save/game/counter-published.ini");
			actor = std::make_shared<NPC>();
			actor->initFromIni(&read, "Init");
			gameManager.npcManager->freeResource();
			gameManager.npcManager->addNPC(actor);
		}
		const int lifeBefore = actor->life;
		auto incoming = std::make_shared<Effect>();
		incoming->user = gameManager.player;
		incoming->level = 1;
		incoming->damage = 100;
		incoming->evade = -1000;
		incoming->launcherKind = lkSelf;
		incoming->doing = ekFlying;
		incoming->lifeTime = 1000;
		incoming->position = actor->getPosition();
		gameManager.effectManager->effectList.clear();
		const bool collided = CollisionDetector::detectCollision(actor, incoming);
		ok = check(collided && actor->life == lifeBefore &&
			!gameManager.effectManager->effectList.empty(),
			"actual Xiaoxiang NPC counters an evaded projectile before and after file reload") && ok;
		for (const auto& effect : gameManager.effectManager->effectList)
		{
			ok = check(effect->magic.iniName == u8"magic-沙暴攻击.ini" && effect->level == 5 &&
				effect->user.lock() == actor && effect->launcherKind == lkEnemy,
				"all emitted sandstorm projectiles keep the actual NPC's owner, level and hostile faction") && ok;
		}
	}
	File::setResourceFallbackRoots({});
	gameManager.effectManager->effectList.clear();
	gameManager.player = originalPlayer;
	resetRuntime(gameManager);
	return ok;
}

bool runAttackedMagicAndPropertyContracts(GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	gameManager.varList.ensureInitialized();
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	if (!check(writeTextFile(root / "ini" / "magic" / "CounterCase.ini",
		"[Init]\nName=COUNTER_CONTRACT\nMoveKind=2\nSpeed=8\nLifeFrame=100\n"
		"[Level1]\nEffect=11\n[Level2]\nEffect=22\n[Level3]\nEffect=33\n"),
		"write a real multilevel counter magic in the isolated resource root"))
	{
		return false;
	}
	const auto originalPlayer = gameManager.player;
	gameManager.player = std::make_shared<Player>();
	gameManager.player->npcName = "CounterTwin";
	gameManager.player->lifeMax = gameManager.player->life = 1000;
	gameManager.player->attackLevel = 3;
	gameManager.player->calInfo();
	gameManager.player->setPosition({ 8, 8 }, false);
	auto first = addTestNpc(gameManager, "CounterTwin", nkBattle, { 4, 4 });
	auto second = addTestNpc(gameManager, "CounterTwin", nkBattle, { 6, 4 });
	auto differentCase = addTestNpc(gameManager, "countertwin", nkBattle, { 8, 4 });
	first->attackLevel = 1;
	second->attackLevel = 2;
	first->life = first->lifeMax = second->life = second->lifeMax = 1000;
	second->scriptHidden = true;
	bool ok = check(execute("setnpcmagictousewhenbeattacked('CounterTwin','CounterCase.ini',1);") == LUA_OK &&
		first->magicToUseWhenBeAttacked != nullptr && first->magicToUseWhenBeAttacked->loadSucceeded &&
		second->magicToUseWhenBeAttacked == first->magicToUseWhenBeAttacked &&
		first->magicDirectionWhenBeAttacked == 1 && second->magicDirectionWhenBeAttacked == 1 &&
		differentCase->magicToUseWhenBeAttacked == nullptr && gameManager.player->magicToUseWhenBeAttacked == nullptr,
		"NPC counter setter loads every exact-name NPC including hidden matches, excluding the player");
	ok = check(execute("setplayermagictousewhenbeattacked('CounterCase.ini',2); "
		"setplayermagictousewhenbeattacked(); setnpcmagictousewhenbeattacked('CounterTwin'); "
		"setnpcmagictousewhenbeattacked('Missing','CounterCase.ini',0); assign('AfterCounter',1);") == LUA_OK &&
		gameManager.player->magicToUseWhenBeAttacked != nullptr &&
		gameManager.player->magicToUseWhenBeAttacked->loadSucceeded &&
		gameManager.player->magicDirectionWhenBeAttacked == 2 && first->magicDirectionWhenBeAttacked == 1 &&
		gameManager.varList.getInteger("AfterCounter") == 1,
		"Player counter setter is independent, missing parameters and names do not interrupt Lua") && ok;
	ok = check(execute("addnpcproperty('CounterTwin','Attack',7); "
		"addnpcproperty('CounterTwin','LifeMax',20); addnpcproperty('CounterTwin','ExpBonus',5); "
		"addnpcproperty('CounterTwin','UnknownProperty',100); addnpcproperty('CounterTwin');") == LUA_OK &&
		first->attack == 7 && second->attack == 7 && gameManager.player->attack == 7 &&
		gameManager.player->getAttack() == 7 && first->lifeMax == 1020 && second->lifeMax == 1020 &&
		gameManager.player->getLifeMax() == 1020 && first->expBonus == 5 && second->expBonus == 5 &&
		gameManager.player->expBonus == 5 && differentCase->attack == 0,
		"AddNpcProperty applies integer additions to all exact-name NPCs and the player without guessing unknown keys") && ok;
	second->scriptHidden = false;
	auto attacker = addTestNpc(gameManager, "CounterAttacker", nkBattle, { 10, 10 });
	auto incoming = std::make_shared<Effect>();
	incoming->user = attacker;
	incoming->level = 1;
	incoming->damage = 1;
	incoming->evade = 100000;
	incoming->launcherKind = lkEnemy;
	incoming->flyingDirection = { 0, 1 };
	const std::shared_ptr<NPC> actors[] = { first, second, gameManager.player };
	for (const auto& actor : actors)
	{
		actor->direction = 6;
		for (int direction : { 0, 1, 2, 9 })
		{
			const std::string call = actor == gameManager.player
				? "setplayermagicwhenattacked('CounterCase.ini',"
				: "setnpcmagictousewhenbeatacked('CounterTwin','CounterCase.ini',";
			ok = check(execute(call + std::to_string(direction) + ");") == LUA_OK,
				"counter aliases dispatch to the existing setters") && ok;
			gameManager.effectManager->freeResource();
			const int lifeBefore = actor->life;
			// Direct damage avoids random Player miss rolls while exercising the real damage-to-counter chain.
			actor->directHurt(incoming);
			ok = check(actor->life < lifeBefore && gameManager.effectManager->effectList.size() == 1,
				"direct damage emits exactly one configured counter effect") && ok;
			if (gameManager.effectManager->effectList.size() == 1)
			{
				const auto& counter = gameManager.effectManager->effectList.front();
				Point destination = attacker->getPosition();
				if (direction == 1) destination = Map::getSubPoint(actor->getPosition(), 4);
				if (direction == 2) destination = Map::getSubPoint(actor->getPosition(), 6);
				const Point start = actor->getPosition();
				const Point next = Map::getSubPoint(start, NPC::getDirection(start, destination));
				Point expectedDirection = Map::getTilePosition(destination, next == destination ? start : next);
				expectedDirection.y *= MapXRatio;
				ok = check(counter->user.lock() == actor && counter->level == actor->attackLevel &&
					counter->magic.iniName == "CounterCase.ini" && counter->flyingDirection == expectedDirection &&
					counter->magic.level[counter->level].effect == actor->attackLevel * 11,
					"flying counter uses its owner's level, direction mode and case-preserved file") && ok;
			}
		}
	}
	for (const auto& actor : actors)
	{
		actor->magicDirectionWhenBeAttacked = 1;
		for (int incomingDirection = 0; incomingDirection < 8; ++incomingDirection)
		{
			incoming->flyingDirection = Map::getTilePosition(
				Map::getSubPoint(actor->getPosition(), incomingDirection), actor->getPosition());
			incoming->flyingDirection.y *= MapXRatio;
			gameManager.effectManager->freeResource();
			actor->directHurt(incoming);
			ok = check(gameManager.effectManager->effectList.size() == 1,
				"each incoming direction dispatches one counter") && ok;
			if (gameManager.effectManager->effectList.size() == 1)
			{
				const Point actual = gameManager.effectManager->effectList.front()->flyingDirection;
				ok = check(actual.x * incoming->flyingDirection.x + actual.y * incoming->flyingDirection.y < 0 &&
					actual.x * incoming->flyingDirection.y == actual.y * incoming->flyingDirection.x,
					"direction one counters opposite all eight incoming directions for NPCs and Player") && ok;
			}
		}
		incoming->flyingDirection = { 0, 0 };
		gameManager.effectManager->freeResource();
		actor->directHurt(incoming);
		ok = check(gameManager.effectManager->effectList.size() == 1 &&
			gameManager.effectManager->effectList.front()->flyingDirection.x > 0 &&
			gameManager.effectManager->effectList.front()->flyingDirection.y == 0,
			"stationary incoming magic falls back to the owner's current facing") && ok;
		const auto configuredMagic = actor->magicToUseWhenBeAttacked;
		actor->equipmentMagicToUseWhenAttacked.push_back({ "CounterEquipment.ini", configuredMagic, 1 });
		incoming->flyingDirection = { 0, 1 };
		gameManager.effectManager->freeResource();
		actor->directHurt(incoming);
		ok = check(gameManager.effectManager->effectList.size() == 2 &&
			gameManager.effectManager->effectList[0]->flyingDirection.y < 0 &&
			gameManager.effectManager->effectList[1]->flyingDirection.y < 0,
			"configured and equipment counter channels both use the corrected opposite direction") && ok;
		actor->magicToUseWhenBeAttacked = nullptr;
		gameManager.effectManager->freeResource();
		actor->directHurt(incoming);
		ok = check(gameManager.effectManager->effectList.size() == 1 &&
			gameManager.effectManager->effectList.front()->flyingDirection.y < 0,
			"equipment counter still dispatches independently when the scripted counter is absent") && ok;
		actor->magicToUseWhenBeAttacked = configuredMagic;
		actor->equipmentMagicToUseWhenAttacked.clear();
	}
	gameManager.effectManager->freeResource();
	ok = check(execute("setnpcmagicwhenattacked('CounterTwin','CounterCase.ini',1); "
		"setplayermagictousewhenbeatacked('CounterCase.ini',2);") == LUA_OK,
		"both remaining legacy counter aliases execute") && ok;
	INIReader saved;
	first->saveToIni(&saved, "NPC000");
	second->saveToIni(&saved, "NPC001");
	gameManager.player->saveToIni(&saved, "Player");
	ok = check(saved.saveToFile("save\\game\\counter-contract.ini"),
		"persist counter settings and script-added attributes to a real file") && ok;
	INIReader loaded("save\\game\\counter-contract.ini");
	for (int index = 0; index < 3; ++index)
	{
		const bool isPlayer = index == 2;
		const std::string section = isPlayer ? "Player" : index == 0 ? "NPC000" : "NPC001";
		std::shared_ptr<NPC> restored = isPlayer ? std::static_pointer_cast<NPC>(std::make_shared<Player>()) : std::make_shared<NPC>();
		restored->initFromIni(&loaded, section);
		if (isPlayer) std::static_pointer_cast<Player>(restored)->calInfo();
		ok = check(restored != actors[index] && restored->magicToUseWhenBeAttackedFile == "CounterCase.ini" &&
			restored->magicToUseWhenBeAttacked != nullptr && restored->magicToUseWhenBeAttacked->loadSucceeded &&
			restored->magicDirectionWhenBeAttacked == (isPlayer ? 2 : 1) && restored->attackLevel == index + 1 &&
			restored->attack == 7 && restored->getLifeMax() == 1020,
			"fresh Player and NPC instances rebuild loaded counter magic and retain script-added attack and maximum life") && ok;
		gameManager.effectManager->freeResource();
		restored->directHurt(incoming);
		ok = check(gameManager.effectManager->effectList.size() == 1 &&
			gameManager.effectManager->effectList.front()->user.lock() == restored &&
			gameManager.effectManager->effectList.front()->level == index + 1,
			"counter magic actually dispatches from each newly restored instance") && ok;
	}
	gameManager.effectManager->freeResource();
	ok = check(execute("setnpcmagictousewhenbeattacked('CounterTwin','MissingCounter.ini',0); "
		"setplayermagictousewhenbeattacked('MissingCounter.ini',0);") == LUA_OK &&
		first->magicToUseWhenBeAttacked != nullptr && gameManager.player->magicToUseWhenBeAttacked != nullptr &&
		!first->magicToUseWhenBeAttacked->loadSucceeded && !gameManager.player->magicToUseWhenBeAttacked->loadSucceeded,
		"missing counter files remain a non-throwing disabled setting rather than a fabricated magic") && ok;
	first->directHurt(incoming);
	gameManager.player->directHurt(incoming);
	ok = check(gameManager.effectManager->effectList.empty() &&
		execute("setnpcmagictousewhenbeattacked('CounterTwin','',0); setplayermagictousewhenbeattacked('',0);") == LUA_OK &&
		first->magicToUseWhenBeAttacked == nullptr && second->magicToUseWhenBeAttacked == nullptr &&
		gameManager.player->magicToUseWhenBeAttacked == nullptr,
		"missing or explicitly cleared counter magic does not dispatch a stale effect") && ok;
	gameManager.player = originalPlayer;
	resetRuntime(gameManager);
	return ok;
}

bool runPartnerIndexAndDropContracts(GameManager& gameManager,
	const std::filesystem::path& root)
{
	resetRuntime(gameManager);
	prepareMap(gameManager);
	gameManager.varList.ensureInitialized();
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = check(execute("assign('partnerno',99); getpartneridx('PartnerNo'); getpartneridx();") == LUA_OK &&
		gameManager.varList.getInteger("PartnerNo") == 0 &&
		gameManager.varList.getInteger("partnerno") == 99,
		"GetPartnerIdx writes only the exact output variable and returns zero without partners");
	auto partner = addTestNpc(gameManager, "UnknownPartner", nkPartner, { 4, 4 });
	ok = check(execute("getpartneridx('PartnerNo');") == LUA_OK &&
		gameManager.varList.getInteger("PartnerNo") == 1,
		"a partner without an index table keeps the legacy fallback of one") && ok;
	ok = check(writeTextFile(root / "partneridx.ini",
		u8"[init]\n1=纳兰真\n2=月眉儿\n3=紫轩\n4=蔷薇\n"),
		"create the released four-entry partner mapping in the isolated test root") && ok;
	INIReader partnerTable("partneridx.ini");
	const char* partnerNames[] = { u8"纳兰真", u8"月眉儿", u8"紫轩", u8"蔷薇" };
	for (int index = 1; index <= 4; ++index)
	{
		partner->npcName = partnerNames[index - 1];
		ok = check(partnerTable.Get("Init", std::to_string(index), "") == partner->npcName &&
			execute("getpartneridx('PartnerNo',123); assign('AfterPartnerIndex',1);") == LUA_OK &&
			gameManager.varList.getInteger("PartnerNo") == index &&
			gameManager.varList.getInteger("AfterPartnerIndex") == 1,
			"GetPartnerIdx reads the four-entry partner mapping and continues synchronously") && ok;
	}
	partner->npcName = "UnknownPartner";
	ok = check(execute("getpartneridx('PartnerNo');") == LUA_OK &&
		gameManager.varList.getInteger("PartnerNo") == 5,
		"an unlisted partner follows the four named partners in the released table") && ok;
	partner->npcName = u8"紫轩";
	partner->relation = nrHostile;
	addTestNpc(gameManager, u8"纳兰真", nkPartner, { 5, 4 });
	ok = check(execute("getpartneridx('PartnerNo');") == LUA_OK &&
		gameManager.varList.getInteger("PartnerNo") == 3 &&
		gameManager.partnerManager.save(0),
		"partner lookup uses the first Kind=partner entry rather than relation or partner count") && ok;
	resetRuntime(gameManager);
	ok = check(gameManager.partnerManager.load(0) &&
		execute("getpartneridx('PartnerNo');") == LUA_OK &&
		gameManager.varList.getInteger("PartnerNo") == 3 &&
		gameManager.partnerManager.findPartnersFromNPCManager().front() != partner,
		"partner file reload constructs new NPC instances and retains the lookup identity and order") && ok;
	resetRuntime(gameManager);

	gameManager.player->npcName = "DropPlayer";
	auto first = addTestNpc(gameManager, "DropTarget", nkBattle, { 4, 4 });
	auto second = addTestNpc(gameManager, "DropTarget", nkBattle, { 5, 4 });
	auto playerNameNpc = addTestNpc(gameManager, "DropPlayer", nkBattle, { 6, 4 });
	ok = check(execute("setdropini('DropTarget','MixedCase.ini[100]',123); "
		"setdropini('DropPlayer','PlayerDrop.ini'); setdropini('Missing','ignored.ini'); "
		"setdropini('DropTarget');") == LUA_OK &&
		first->dropIni == "MixedCase.ini[100]" && second->dropIni.empty() &&
		gameManager.player->dropIni == "PlayerDrop.ini" && playerNameNpc->dropIni.empty(),
		"SetDropIni preserves its string, changes the first target, prefers the player and tolerates missing targets/arguments") && ok;
	first->noDropWhenDie = 1;
	INIReader npcSave;
	first->saveToIni(&npcSave, "NPC000");
	ok = check(npcSave.saveToFile("save/game/drop-contract.npc"),
		"write DropIni and NoDropWhenDie through the NPC serializer") && ok;
	INIReader npcRead("save/game/drop-contract.npc");
	auto loaded = loadNpcRoundTrip(gameManager, npcRead);
	ok = check(loaded != first && loaded->dropIni == "MixedCase.ini[100]" && loaded->noDropWhenDie == 1 &&
		execute("setdropini('DropTarget','');") == LUA_OK && loaded->dropIni.empty(),
		"a new NPC restores both drop fields and SetDropIni can clear the reference") && ok;

	struct DropCase
	{
		const char* command;
		const char* dropFile;
		int noDrop;
		bool hostile;
		bool expectedDrop;
	};
	const DropCase cases[] =
	{
		{ "enabledrop()", "persistence_drop.ini[100]", 0, true, true },
		{ "enabeldrop()", "persistence_drop_table.ini[100]", 0, true, true },
		{ "disabledrop()", "persistence_drop.ini[100]", 0, true, false },
		{ "enabledrop()", "persistence_drop.ini", 1, true, false },
		{ "enabledrop()", "persistence_drop.ini", 0, false, false },
		{ "enabledrop()", "persistence_drop.ini[-1]", 0, true, false },
		{ "enabledrop()", "missing-drop.ini", 0, true, false },
		{ "enabledrop()", "persistence_drop.ini", 0, true, true },
	};
	for (const auto& dropCase : cases)
	{
		resetRuntime(gameManager);
		auto dying = makeDyingNpc(gameManager, "DropDying");
		dying->relation = dropCase.hostile ? nrHostile : nrFriendly;
		dying->noDropWhenDie = dropCase.noDrop;
		const std::string source = std::string(dropCase.command) +
			"; setdropini('DropDying','" + dropCase.dropFile + "'); assign('AfterDropSetup',1);";
		ok = check(execute(source) == LUA_OK && gameManager.varList.getInteger("AfterDropSetup") == 1,
			"drop commands execute through Lua and continue without waiting for death") && ok;
		dying->setTime(dying->getTime() + 500);
		dying->actionManager->update(500);
		gameManager.npcManager->onUpdate();
		const auto countDrops = [&]()
		{
			return std::count_if(gameManager.objectManager->objectList.begin(),
				gameManager.objectManager->objectList.end(), [](const std::shared_ptr<Object>& object)
				{
					return object != nullptr && object->objName == "PERSISTENCE_DROP" &&
						object->getPosition().x == 4 && object->getPosition().y == 4;
				});
		};
		const std::string message = std::string("actual death respects ") + dropCase.command + " / " + dropCase.dropFile;
		ok = check(gameManager.npcManager->npcList.empty() &&
			countDrops() == (dropCase.expectedDrop ? 1 : 0) &&
			gameManager.objectManager->objectList.size() == (dropCase.expectedDrop ? 2u : 1u),
			message.c_str()) && ok;
		gameManager.npcManager->onUpdate();
		ok = check(countDrops() == (dropCase.expectedDrop ? 1 : 0),
			"a second death-cleanup update does not duplicate or recreate a suppressed drop") && ok;
	}
	gameManager.player->dropIni.clear();
	gameManager.global.data.dropDisabled = false;
	resetRuntime(gameManager);
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

bool runExperienceAndLowLifePersistence(GameManager& gameManager, const std::filesystem::path& root)
{
	const auto profileRoot = root / "field-profile";
	ResourceManager& manager = ResourceManager::instance();
	bool ok = true;
	for (const char* name : { u8"可捡武器.ini", u8"可捡防具.ini" })
	{
		ok = check(writeTextFile(profileRoot / "ini" / "obj" / std::filesystem::u8path(name),
			"[Init]\nObjName=FIELD_BONUS_DROP\nKind=7\n"), "write the two actual default bonus-drop paths") && ok;
	}
	const std::pair<int, int> bonusCases[] = { { -1, 5 }, { 0, 5 }, { 7, 5 }, { -1, 0 }, { -1, -5 }, { 7, -7 }, { 7, -100 } };
	gameManager.varList.ensureInitialized();
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	for (int gameType : { GAME_JXQY2, GAME_YYCS, GAME_XJXQY })
	{
		const bool usesBonus = gameType != GAME_JXQY2;
		const std::string profile = "[Game]\nId=FIELD_PERSISTENCE\nName=Field persistence\nVersion=1.0.6\nType=" +
			std::to_string(gameType) + "\n[Experience]\nDefeatedNpcExperienceMode=" +
			(usesBonus ? "LevelProductWithBonus" : "StoredExperience") +
			"\nExperienceMultiplier=3.0\n[Save]\nNamespace=field-persistence\n";
		if (!check(writeTextFile(profileRoot / "game_profile.ini", profile) &&
			manager.initialize(profileRoot.string()) && manager.setActiveResourcePackById("FIELD_PERSISTENCE") &&
			(manager.getActiveManifest().resolvedDefeatedNpcExperienceMode() ==
				DefeatedNpcExperienceMode::LevelProductWithBonus) == usesBonus,
			"activate a real parsed profile with the trilogy's experience mode")) return false;
		gameManager.global.applyResourceManifestFeatures(manager.getActiveManifest());
		for (const char* name : { u8"可捡武器.ini", u8"可捡防具.ini" })
		{
			INIReader drop(std::string("ini/obj/") + name);
			ok = check(drop.Get("Init", "ObjName", "") == "FIELD_BONUS_DROP",
				"UTF-8 fixture names resolve through the actual resource reader") && ok;
		}
		for (const auto [originalBonus, delta] : bonusCases)
		{
			resetRuntime(gameManager);
			INIReader seed;
			seed.Set("Init", "Name", "FieldOwner");
			seed.SetInteger("Init", "Kind", nkBattle);
			seed.SetInteger("Init", "Relation", nrHostile);
			seed.SetInteger("Init", "Life", 1000);
			seed.SetInteger("Init", "LifeMax", 1000);
			seed.SetInteger("Init", "Exp", 123);
			seed.SetInteger("Init", "Level", 4);
			seed.SetInteger("Init", "LevelUpExp", 1000000);
			if (originalBonus >= 0) seed.SetInteger("Init", "ExpBonus", originalBonus);
			auto npc = std::make_shared<NPC>();
			npc->initFromIni(&seed, "Init");
			gameManager.npcManager->addNPC(npc);
			gameManager.player->initFromIni(&seed, "Init");
			gameManager.player->calInfo();
			const int loadedBonus = std::max(0, originalBonus);
			ok = check(npc->expBonus == loadedBonus && gameManager.player->expBonus == loadedBonus &&
				execute("addnpcproperty('FieldOwner','ExpBonus'," + std::to_string(delta) + ");") == LUA_OK &&
				npc->expBonus == loadedBonus + delta && gameManager.player->expBonus == loadedBonus + delta,
				"both experience modes retain ExpBonus for NPC/player scripts and non-experience behavior") && ok;
			INIReader saved;
			npc->saveToIni(&saved, "NPC000");
			gameManager.player->saveToIni(&saved, "Init");
			ok = check(saved.saveToFile("save/game/field-roundtrip.ini"), "write actual field persistence file") && ok;
			INIReader disk("save/game/field-roundtrip.ini");
			auto restored = std::make_shared<NPC>();
			restored->initFromIni(&disk, "NPC000");
			Player restoredPlayer;
			restoredPlayer.initFromIni(&disk, "Init");
			const int expectedBonus = loadedBonus + delta;
			const bool expectedBonusKey = originalBonus >= 0 || expectedBonus != 0;
			ok = check(restored->expBonus == expectedBonus && restoredPlayer.expBonus == expectedBonus &&
				disk.HasKey("NPC000", "ExpBonus") == expectedBonusKey && disk.HasKey("Init", "ExpBonus") == expectedBonusKey,
				"nonzero ExpBonus survives a new NPC/player file load even when the original template lacked the key") && ok;
			gameManager.npcManager->addNPC(restored);
			auto hit = std::make_shared<Effect>();
			hit->user = gameManager.player;
			hit->launcherKind = lkSelf;
			hit->level = 1;
			hit->damage = 2000;
			for (const auto& victim : { npc, restored })
			{
				victim->res.death.imagePackage = std::make_shared<IMPImage>();
				victim->res.death.imagePackage->directions = 1;
				victim->res.death.imagePackage->interval = 50;
				victim->res.death.imagePackage->frame.resize(2);
				gameManager.player->exp = 0;
				gameManager.player->level = 3;
				gameManager.player->levelUpExp = 1000000;
				victim->directHurt(hit);
				const int expectedExperience = usesBonus ? std::max(4, 3 * 4 + loadedBonus + delta) * 3 : 123 * 3;
				ok = check(victim->life == 0 && gameManager.player->exp == expectedExperience,
					"actual lethal damage awards the same mode-specific experience before and after file reload") && ok;
				if (expectedBonus > 0)
				{
					gameManager.objectManager->freeResource();
					gameManager.global.data.dropDisabled = false;
					victim->setTime(victim->getTime() + victim->actionLastTime);
					victim->actionManager->update(victim->actionLastTime);
					gameManager.npcManager->onUpdate();
					ok = check(gameManager.objectManager->objectList.size() == 1 &&
						gameManager.objectManager->objectList.front()->objName == "FIELD_BONUS_DROP",
						"positive bonus retains the guaranteed default equipment-drop branch after reload") && ok;
				}
			}
		}
		for (int percent : { -5, 0, 20, 150 })
		{
			resetRuntime(gameManager);
			INIReader seed;
			seed.Set("Init", "Name", "LowLifeOwner");
			seed.SetInteger("Init", "Life", 5);
			seed.SetInteger("Init", "LifeMax", 100);
			auto npc = std::make_shared<NPC>();
			npc->initFromIni(&seed, "Init");
			gameManager.npcManager->addNPC(npc);
			gameManager.player->initFromIni(&seed, "Init");
			ok = check(npc->lifeLowPercent == 20 && execute("addnpcproperty('LowLifeOwner','LifeLowPercent'," +
				std::to_string(percent - 20) + ");") == LUA_OK && npc->lifeLowPercent == percent,
				"missing LifeLowPercent defaults to 20 but a script may explicitly change the threshold") && ok;
			const bool lowBefore = npc->isLifeLowForAI();
			INIReader saved;
			npc->saveToIni(&saved, "NPC000");
			gameManager.player->saveToIni(&saved, "Init");
			ok = check(saved.saveToFile("save/game/low-life-roundtrip.ini"), "write low-life threshold file") && ok;
			INIReader disk("save/game/low-life-roundtrip.ini");
			NPC restored;
			restored.initFromIni(&disk, "NPC000");
			Player restoredPlayer;
			restoredPlayer.initFromIni(&disk, "Init");
			ok = check(restored.lifeLowPercent == percent && restoredPlayer.lifeLowPercent == percent &&
				lowBefore == (percent >= 5) && restored.isLifeLowForAI() == lowBefore,
				"explicit zero/negative low-life thresholds survive reload without enabling the AI trigger") && ok;
		}
	}
	resetRuntime(gameManager);
	std::error_code errorCode;
	const auto emptyCollection = root / "empty-collection";
	std::filesystem::create_directories(emptyCollection, errorCode);
	ok = check(!errorCode && manager.initialize(emptyCollection.string()), "clear the isolated active profile") && ok;
	File::setActiveResourceRoot(root.string());
	File::setActiveSaveNamespace(NpcPersistenceSaveNamespace);
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
	ok = runEntityLifecycleScriptContracts(gameManager, root) && ok;
	ok = runEntityPropertyScriptContracts(gameManager, root) && ok;
	ok = runEmptyNpcScriptLoad(gameManager, root) && ok;
	ok = runCompatibleStoryEntityListLoads(gameManager, root) && ok;
	ok = runStatusDurationInputSafety() && ok;
	ok = runLegacyNpcObjectDefaults() && ok;
	ok = runPlayerPermissionPersistence(gameManager, root) && ok;
	ok = runPlayerChangePersistenceAndPartnerContinuity(
		gameManager,
		root) && ok;
	ok = runPlayerChangeAttributeIsolation(gameManager, root) && ok;
	ok = runOfflinePartnerMagicPersistence(
		gameManager,
		root) && ok;
	ok = runEntityListScriptSavePolicy(
		gameManager,
		root) && ok;
	ok = runLiveNpcRoundTrip(gameManager) && ok;
	ok = runAttackedMagicAndPropertyContracts(gameManager, root) && ok;
	ok = runCounterImpactContracts(gameManager, root) && ok;
	ok = runDeathReentryContracts(gameManager, root) && ok;
	ok = runYuchenDropContracts(gameManager) && ok;
	ok = runYuchenRewardContracts(gameManager, root) && ok;
	ok = runPartnerIndexAndDropContracts(gameManager, root) && ok;
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
	ok = runExperienceAndLowLifePersistence(gameManager, root) && ok;
	resetRuntime(gameManager);
	File::setActiveSaveNamespace("");
	File::setPlatformStateParentForTests("");
	std::filesystem::remove_all(root, errorCode);
	return ok;
}
