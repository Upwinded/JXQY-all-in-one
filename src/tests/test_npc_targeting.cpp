#include "../Game/Data/CollisionDetector.h"
#include "../Game/Data/NPCManager.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/Map.h"
#include "../Game/GameManager/GameManager.h"
#include "../File/File.h"
#include "../Image/IMP.h"
#include "MapV3ContractFixture.h"
#include "TestTemporaryDirectory.h"

#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

bool runGambleMenuRuntimeTests();
bool runMagicDerivedRuntimeTests();
bool runMagicExperienceTests();
bool runReplacementExperienceOwnershipTests();
bool runEffectRuntimePersistenceTests();
bool runNpcRuntimePersistenceTests();
bool runObjectAnimationRuntimeTests();
bool runMediaRuntimeTests();
bool runCoreLifecycleTests();
bool runScriptMovementRuntimeTests();
bool runCurrentModCompatibilityTests();
bool runQingyuUiTests();
bool runSaveWriteSharingRuntimeTests();
bool runSaveStabilityTests();
bool runXiaoxiangMissingObjectRouteTests();
bool runXiaoxiangTournamentRouteTests();
bool runXiaoxiangPrisonEndingTests();
bool runXiaoxiangCompanionHistoryTests(int requestedLoadMode = -1);
bool runXiaoxiangLegacyEntranceTests();
bool runMoonlightTrapRouteTests();
bool runMoonlightDepartureTests();
bool runNewSwordBoatRouteTests();
bool runNewSwordYangYingRouteTests();
bool runMoonlightEndingTwoRouteTests();
bool runSwordTwoPartnerDepartureTests();
bool runSwordTwoHistoricalScriptTests();
bool runBilibiliStoryFeedbackTests();
bool runMergedEntitySaveTests();
bool runProductionAttackFileRuntimeTests();
bool runFullAttackSaveRuntimeTests();
bool runEquipmentReplacementSaveRuntimeTests();
bool runDynamicMagicListSaveRuntimeTests();
bool runFullExperienceSaveRuntimeTests();
bool runCharacterCasterSaveRuntimeTests();
bool runUIFocusTests();
bool runMapThumbnailControllerTests();
bool runPartnerEquipmentTransferTests();
bool runWorldInteractionRuntimeTests();
bool runGamepadWorldRuntimeTests();
bool runGamepadEssentialUITests();
bool runGamepadRPGMenuActionTests();
bool runGamepadSurfaceContractTests();
bool runMobileExternalInputRuntimeTests();
bool runScriptEngineRuntimeTests();
bool runEditorRunSceneRuntimeTests();
bool runMapV3RuntimeTests();

class ProjectileCollisionTestAccess
{
public:
	static void beginFrame(EffectManager& manager)
	{
		manager.onPreTreatment();
	}

	static void update(Effect& effect)
	{
		effect.onUpdate();
	}

	static void advance(Effect& effect, UTime milliseconds)
	{
		effect.frameTime = milliseconds;
		effect.setTime(effect.getTime() + milliseconds);
		effect.onUpdate();
	}
};

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

NPCActionRes makeActionWithDirections(int directions)
{
	NPCActionRes action;
	action.imagePackage = std::make_shared<IMPImage>();
	action.imagePackage->directions = directions;
	return action;
}

NPCManager& makeDetachedNPCManager()
{
	// NPCManager teardown expects a full GameManager graph; these state-only tests
	// keep detached managers alive until process exit.
	return *new NPCManager();
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

class ScopedProductionStateIsolation final
{
public:
	explicit ScopedProductionStateIsolation(
		const std::string& testName) :
		root(makeUniqueTestDirectory(testName))
	{
		std::error_code errorCode;
		std::filesystem::remove_all(root, errorCode);
		errorCode.clear();
		std::filesystem::create_directories(root, errorCode);
		ready = !errorCode;
		if (ready)
		{
			File::setPlatformStateParentForTests(
				root.generic_string());
		}
	}

	~ScopedProductionStateIsolation()
	{
		File::setPlatformStateParentForTests("");
		std::error_code errorCode;
		std::filesystem::remove_all(root, errorCode);
	}

	bool valid() const
	{
		return ready;
	}

private:
	std::filesystem::path root;
	bool ready = false;
};

bool testModeUsesProductionResources(const std::string& mode)
{
	return mode == "--core-lifecycle" || mode == "--gameplay-automation" ||
		mode == "--save-write-sharing" ||
		mode == "--save-stability" ||
		mode == "--xiaoxiang-missing-object-routes" ||
		mode == "--xiaoxiang-tournament-routes" ||
		mode == "--xiaoxiang-prison-ending" ||
		mode == "--xiaoxiang-companion-history" ||
		mode == "--xiaoxiang-companion-history-sync" ||
		mode == "--xiaoxiang-companion-history-async" ||
		mode == "--xiaoxiang-legacy-entrances" ||
		mode == "--moonlight-trap-routes" ||
		mode == "--moonlight-departure" ||
		mode == "--new-sword-boat-routes" ||
		mode == "--new-sword-yang-ying-routes" ||
		mode == "--moonlight-ending-two-routes" ||
		mode == "--sword-two-partner-departures" ||
		mode == "--sword-two-historical-scripts" ||
		mode == "--bilibili-story-feedback" ||
		mode == "--merged-entity-save" ||
		mode == "--production-attack-files" ||
		mode == "--full-attack-save" ||
		mode == "--equipment-replacement-save" ||
		mode == "--dynamic-magic-list-save" ||
		mode == "--full-experience-save" ||
		mode == "--character-caster-save" ||
		mode == "--ui-focus" ||
		mode == "--gamepad-world-runtime" ||
		mode == "--gamepad-essential-ui" ||
		mode == "--gamepad-rpg-menu-actions" ||
		mode == "--gamepad-surface-contract" ||
		mode == "--mobile-external-input-runtime";
}

bool runParasiticIntervalDefaultTest()
{
	auto root = makeUniqueTestDirectory("jxqy_magic_parasitic_interval_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "magic", errorCode);
	File::setAssetsCollectionRoot(root.string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});

	const std::string defaultIntervalMagic =
		"[Init]\n"
		"Name=DEFAULT_PARASITIC_INTERVAL\n"
		"Parasitic=1\n"
		"MoveKind=2\n"
		"[Level1]\n"
		"MoveKind=2\n";
	if (!writeTextFile(root / "ini" / "magic" / "default_parasitic_interval.ini", defaultIntervalMagic))
	{
		std::cerr << "FAILED: write default parasitic interval fixture\n";
		return false;
	}

	const std::string zeroIntervalMagic =
		"[Init]\n"
		"Name=ZERO_PARASITIC_INTERVAL\n"
		"Parasitic=1\n"
		"ParasiticInterval=0\n"
		"MoveKind=2\n"
		"[Level1]\n"
		"MoveKind=2\n";
	if (!writeTextFile(root / "ini" / "magic" / "zero_parasitic_interval.ini", zeroIntervalMagic))
	{
		std::cerr << "FAILED: write zero parasitic interval fixture\n";
		return false;
	}

	bool ok = true;
	Magic defaultInterval;
	defaultInterval.initFromIni("default_parasitic_interval.ini", false);
	ok = check(defaultInterval.parasitic == 1, "parasitic fixture loads Parasitic") && ok;
	ok = check(defaultInterval.parasiticInterval == 1000, "missing ParasiticInterval defaults to JxqyHD 1000 ms") && ok;

	Magic zeroInterval;
	zeroInterval.initFromIni("zero_parasitic_interval.ini", false);
	ok = check(zeroInterval.parasiticInterval == 0, "explicit ParasiticInterval=0 remains explicit") && ok;
	return ok;
}

bool runMapObstacleSemanticsTest()
{
	bool truthTableMatches = true;
	for (int value = 0; value <= 0xFF; value++)
	{
		const uint8_t obstacle = static_cast<uint8_t>(value);
		const bool expectedWalk = (obstacle & 0xC0) == 0;
		const bool expectedJump = obstacle == 0 || (obstacle & 0x20) != 0;
		const bool expectedMagic = obstacle == 0 || (obstacle & 0x40) != 0;
		const bool expectedSight = (obstacle & 0x80) == 0;
		if (tileObstacleAllowsWalk(obstacle) != expectedWalk ||
			tileObstacleAllowsJump(obstacle) != expectedJump ||
			tileObstacleAllowsMagic(obstacle) != expectedMagic ||
			tileObstacleAllowsSight(obstacle) != expectedSight)
		{
			std::cerr << "FAILED: obstacle truth table at 0x" << std::hex
				<< value << std::dec << '\n';
			truthTableMatches = false;
			break;
		}
	}

	Map map;
	map.data = std::make_shared<MapData>();
	map.data->head.width = 5;
	map.data->head.height = 5;
	map.data->tile.resize(5, std::vector<MapTile>(5));
	map.dataMap.tile.resize(5, std::vector<DataTile>(5));
	bool ok = check(truthTableMatches,
		"all 256 obstacle bytes match original bit formulas");
	auto previousData = map.data;
	std::unique_ptr<char[]> invalidMap = std::make_unique<char[]>(MAP_HEAD_LEN);
	std::memset(invalidMap.get(), 0, MAP_HEAD_LEN);
	ok = check(!map.load(invalidMap, MAP_HEAD_LEN) && map.data == previousData,
		"invalid map buffer preserves the current map") && ok;
	std::vector<uint8_t> oversizedBuffer = MapV3ContractFixture::build();
	const int oversizedWidth = MapSafety::MaximumDimension + 1;
	const int oversizedDataLength = oversizedWidth * MapV3ContractFixture::TileLength;
	MapV3ContractFixture::writeInt32(oversizedBuffer, 64, oversizedDataLength);
	MapV3ContractFixture::writeInt32(
		oversizedBuffer, 68, oversizedWidth);
	oversizedBuffer.resize(
		static_cast<size_t>(MapV3ContractFixture::HeaderLength) +
		static_cast<size_t>(MapV3ContractFixture::MpcCount) *
			MapV3ContractFixture::InfoLength +
		static_cast<size_t>(oversizedDataLength),
		0);
	std::unique_ptr<char[]> oversizedMap = std::make_unique<char[]>(oversizedBuffer.size());
	std::memcpy(oversizedMap.get(), oversizedBuffer.data(), oversizedBuffer.size());
	ok = check(!map.load(oversizedMap, static_cast<int>(oversizedBuffer.size())) &&
		map.data == previousData,
		"oversized map dimensions are rejected transactionally") && ok;
	ok = check(Map::getSubPoint({ INT_MAX, INT_MAX }, 6) == Point{ INT_MAX, INT_MAX }
		&& Map::getSubPoint({ 10, 10 }, 17) == Map::getSubPoint({ 10, 10 }, 1),
		"map neighbor geometry normalizes directions and saturates coordinates") && ok;
	ok = check(Map::calDistance({ INT_MIN, INT_MIN }, { INT_MAX, INT_MAX }) == INT_MAX,
		"map distance saturates extreme coordinates") && ok;
	const Point saturatedTilePosition = Map::getTilePosition(
		{ INT_MAX, INT_MAX }, { INT_MIN, INT_MIN });
	ok = check(saturatedTilePosition.x == INT_MAX && saturatedTilePosition.y == INT_MAX,
		"map tile projection saturates extreme pixel coordinates") && ok;
	for (const Point origin : { Point{ 2, 2 }, Point{ 2, 3 } })
	{
		for (int direction = 0; direction < 8; ++direction)
		{
			const Point destination = Map::getSubPoint(origin, direction);
			const Point projected = Map::getTilePosition(destination, origin);
			const auto steps = map.getPassPath(origin, destination, projected, destination);
			ok = check(!steps.empty() && steps.front() == origin &&
				steps.size() == (direction % 2 == 0 ? 3 : 1),
				"all eight ray directions retain their endpoint and corner checks") && ok;
		}
	}

	auto verifyTile = [&](uint8_t obstacle, bool walk, bool jump,
		bool magic, bool sight, const char* label) {
		const Point position = { 2, 2 };
		map.data->tile[position.y][position.x].obstacle = obstacle;
		const bool matches = map.canWalk(position) == walk &&
			map.canJump(position) == jump &&
			map.canFly(position) == magic &&
			map.canSeeTile(position) == sight;
		if (!matches)
			std::cerr << "FAILED: Map obstacle integration " << label << '\n';
		return matches;
	};

	ok = verifyTile(0x00, true, true, true, true, "0x00") && ok;
	ok = verifyTile(0x01, true, false, false, true, "0x01") && ok;
	ok = verifyTile(0x20, true, true, false, true, "0x20") && ok;
	ok = verifyTile(0x40, false, false, true, true, "0x40") && ok;
	ok = verifyTile(0x60, false, true, true, true, "0x60") && ok;
	ok = verifyTile(0x80, false, false, false, false, "0x80") && ok;
	ok = verifyTile(0xA0, false, true, false, false, "0xA0") && ok;
	ok = verifyTile(0x41, false, false, true, true, "0x41") && ok;
	ok = verifyTile(0x62, false, true, true, true, "0x62") && ok;
	ok = verifyTile(0x83, false, false, false, false, "0x83") && ok;

	for (auto& row : map.data->tile)
		for (MapTile& tile : row)
			tile.obstacle = 0;
	map.data->tile[1][1].obstacle = 0x60;
	map.data->tile[2][1].obstacle = 0x60;
	map.data->tile[3][0].obstacle = 0x60;
	ok = check(map.getJumpPath({ 2, 0 }, { 0, 4 }) == Point{ 0, 4 },
		"southwest jump crosses transparent fencing and reaches its walkable landing") && ok;
	Map fence;
	fence.data = std::make_shared<MapData>();
	fence.data->head.width = 5;
	fence.data->head.height = 9;
	fence.data->tile.assign(9, std::vector<MapTile>(5));
	fence.dataMap.tile.assign(9, std::vector<DataTile>(5));
	for (auto& row : fence.data->tile)
		for (MapTile& tile : row)
			tile.obstacle = 0x40;
	for (const Point point : { Point{ 4, 0 }, Point{ 3, 1 }, Point{ 3, 2 }, Point{ 2, 3 },
		Point{ 2, 4 }, Point{ 1, 5 }, Point{ 1, 6 }, Point{ 0, 7 }, Point{ 0, 8 } })
		fence.data->tile[point.y][point.x].obstacle = point.y == 0 || point.y == 3 || point.y == 8 ? 0 : 0x60;
	ok = check(fence.getJumpPath({ 4, 0 }, { 0, 8 }) == Point{ 0, 8 },
		"southwest fence jump retains its ray beside jump-blocking terrain") && ok;
	for (auto& row : map.data->tile)
		for (MapTile& tile : row)
			tile.obstacle = 0;
	const Point from = { 0, 2 };
	const Point to = { 4, 2 };
	map.data->tile[2][2].obstacle = 0x01;
	ok = check(map.canSee(from, to),
		"low-bit metadata does not block an intermediate sight tile") && ok;
	map.data->tile[2][2].obstacle = 0x80;
	ok = check(!map.canSee(from, to),
		"hard obstacle blocks an intermediate sight tile") && ok;
	map.data->tile[2][2].obstacle = 0;
	map.data->tile[to.y][to.x].obstacle = 0x01;
	ok = check(map.canSee(from, to)
			&& !tileObstacleAllowsMagic(
				map.data->tile[to.y][to.x].obstacle),
		"the target tile does not self-occlude while remaining unavailable to magic") && ok;
	map.data->tile[to.y][to.x].obstacle = 0x80;
	ok = check(map.canSee(from, to),
		"an opaque target tile does not hide the entity occupying that tile") && ok;

	map.data->head.width = 32;
	map.data->head.height = 64;
	map.data->tile.assign(64, std::vector<MapTile>(32));
	map.dataMap.tile.assign(64, std::vector<DataTile>(32));
	for (int y = 0; y < map.data->head.height; ++y)
	{
		map.data->tile[y][16].obstacle = 0x80;
	}
	const Point radiusPathStart = { 10, 32 };
	const Point radiusPathTarget = { 18, 32 };
	const auto reachableRadiusPath = map.getRadiusPath(
		radiusPathStart, radiusPathTarget, 3, 8);
	ok = check(
		!reachableRadiusPath.empty()
			&& reachableRadiusPath.size() == 5
			&& Map::calDistance(
				reachableRadiusPath.back(), radiusPathTarget) <= 3
			&& reachableRadiusPath.back().x < 16,
		"radius path reaches the closest valid tile without crossing a disconnected wall") && ok;
	const auto unreachableRadiusPath = map.getRadiusPath(
		radiusPathStart, radiusPathTarget, 1, 8);
	ok = check(unreachableRadiusPath.empty(),
		"radius path remains empty when every candidate is disconnected") && ok;
	return ok;
}

bool runNonCombatPartnerTargetingTest()
{
	GameManager gameManager;
	gameManager.global.data.NPCAI = true;
	gameManager.global.data.PartnerCombat = true;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 32;
	gameManager.map->data->head.height = 32;
	gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
	gameManager.player->setPosition({ 5, 7 }, false);
	auto partner = std::make_shared<NPC>();
	auto enemy = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->relation = nrFriendly;
	partner->visionRadius = 10;
	partner->attackLevel = 1;
	partner->setPosition({ 6, 7 }, false);
	enemy->kind = nkBattle;
	enemy->relation = nrHostile;
	enemy->life = 100;
	enemy->setPosition({ 9, 7 }, false);
	gameManager.npcManager->npcList = { partner, enemy };
	gameManager.map->createDataMap();
	NPCActionRes action = makeActionWithDirections(8);
	action.imagePackage->interval = 100;
	action.imagePackage->frame.resize(8);
	partner->res.stand = action;
	partner->res.walk = action;
	partner->res.run = action;
	partner->res.attack = action;
	partner->setTime(1000);
	bool ok = check(!gameManager.npcManager->scheduleBattleAction(partner)
		&& partner->currentCombatTarget.expired() && partner->isStanding(),
		"ordinary partner without attack magic stays with the player instead of approaching an enemy");
	partner->clearCombatTargetMemory();
	partner->stopMovement();
	partner->currentCombatTarget = enemy;
	partner->fightState.set(true);
	gameManager.npcManager->scheduleBattleAction(partner);
	ok = check(partner->currentCombatTarget.expired() && !partner->fightState.get(),
		"non-combat partner releases stale combat targeting") && ok;
	partner->stopMovement();
	gameManager.player->setPosition({ 5, 15 }, false);
	partner->nextFollowCheckTime = 1;
	partner->actionManager->update(1);
	ok = check((partner->isWalking() || partner->isRunning())
		&& partner->currentCombatTarget.expired(),
		"non-combat partner still follows a moving player with partner combat enabled") && ok;
	partner->stopMovement();
	gameManager.player->setPosition({ 5, 7 }, false);
	auto magic = std::make_shared<Magic>();
	magic->loadSucceeded = true;
	magic->level[1].moveKind = mmkPoint;
	magic->level[1].lifeFrame = 100;
	NPCAttackOption option;
	option.magic = magic;
	option.moveKind = mmkPoint;
	option.configuredUseDistance = 1;
	option.hasExplicitUseDistance = true;
	partner->attackOptions.push_back(option);
	ok = check(gameManager.npcManager->scheduleBattleAction(partner)
		&& partner->currentCombatTarget.lock() == enemy,
		"equipped combat partner can still acquire and approach an enemy") && ok;
	partner->stopMovement();
	partner->clearCombatTargetMemory();
	partner->res.attack = NPCActionRes();
	ok = check(!gameManager.npcManager->scheduleBattleAction(partner)
		&& partner->currentCombatTarget.expired(),
		"partner without an attack animation does not chase with unused attack magic") && ok;
	partner->res.attack = action;
	partner->attackOptions.clear();
	for (const bool secondary : { false, true })
	{
		partner->stopMovement();
		partner->clearCombatTargetMemory();
		partner->npcMagic = secondary ? nullptr : magic;
		partner->npcMagic2 = secondary ? magic : nullptr;
		ok = check(gameManager.npcManager->scheduleBattleAction(partner)
			&& partner->currentCombatTarget.lock() == enemy,
			"legacy primary and secondary attack magic retain partner combat") && ok;
	}
	return ok;
}

bool runPartnerCombatOwnerLeashTest()
{
	GameManager gameManager;
	gameManager.global.data.NPCAI = true;
	gameManager.global.data.PartnerCombat = true;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 32;
	gameManager.map->data->head.height = 32;
	gameManager.map->data->tile.assign(32, std::vector<MapTile>(32));
	gameManager.map->createDataMap();
	gameManager.player->setPosition({ 20, 1 }, false);

	auto partner = std::make_shared<NPC>();
	auto enemy = std::make_shared<NPC>();
	partner->kind = nkPartner;
	partner->setPosition({ 1, 1 }, false);
	enemy->setPosition({ 2, 1 }, false);
	partner->fightState.set(true);
	partner->currentCombatTarget = enemy;
	partner->lastCombatTarget = enemy;
	partner->lastKnownCombatTarget = enemy;
	partner->lastKnownCombatTargetPosition = enemy->getPosition();
	partner->lastKnownCombatTargetTime = 1;
	partner->hasLastKnownCombatTargetPosition = true;
	partner->actionPlan.state = npsApproaching;
	partner->actionPlan.planTarget = enemy;
	partner->actionPlan.planStartTime = 1;

	bool ok = check(partner->abandonPartnerCombatForPlayerFollow(),
		"distant partner abandons combat for owner following");
	ok = check(!partner->fightState.get()
		&& partner->currentCombatTarget.expired()
		&& partner->lastCombatTarget.expired()
		&& partner->lastKnownCombatTarget.expired()
		&& !partner->actionPlan.isActive(),
		"owner-follow priority clears all partner combat work") && ok;

	partner->setTime(NPC::PartnerOwnerFollowStallTimeoutMilliseconds - 1);
	partner->setPosition({ 10, 1 }, false);
	partner->fightState.set(true);
	partner->currentCombatTarget = enemy;
	ok = check(partner->abandonPartnerCombatForPlayerFollow()
		&& !partner->fightState.get()
		&& partner->currentCombatTarget.expired(),
		"returning partner does not reacquire at the ten-tile boundary") && ok;

	partner->setTime(NPC::PartnerOwnerFollowStallTimeoutMilliseconds * 2 - 2);
	ok = check(partner->abandonPartnerCombatForPlayerFollow(),
		"owner-follow progress restarts the stall timeout") && ok;

	partner->setTime(NPC::PartnerOwnerFollowStallTimeoutMilliseconds * 2 - 1);
	partner->fightState.set(true);
	partner->currentCombatTarget = enemy;
	ok = check(!partner->abandonPartnerCombatForPlayerFollow()
		&& partner->fightState.get()
		&& partner->currentCombatTarget.lock() == enemy,
		"stalled owner following temporarily releases combat targeting") && ok;

	const UTime retryTime = partner->partnerOwnerFollowRetryTime;
	partner->setTime(retryTime - 1);
	partner->setPosition({ 1, 1 }, false);
	ok = check(!partner->abandonPartnerCombatForPlayerFollow()
		&& partner->currentCombatTarget.lock() == enemy,
		"partner can keep targeting during the owner-follow retry cooldown") && ok;

	partner->setTime(retryTime);
	ok = check(partner->abandonPartnerCombatForPlayerFollow()
		&& !partner->fightState.get()
		&& partner->currentCombatTarget.expired(),
		"partner retries owner following after the combat window") && ok;

	partner->setTime(retryTime + 1);
	partner->setPosition(gameManager.player->getPosition(), false);
	partner->fightState.set(true);
	partner->currentCombatTarget = enemy;
	ok = check(!partner->abandonPartnerCombatForPlayerFollow()
		&& partner->fightState.get()
		&& partner->currentCombatTarget.lock() == enemy,
		"partner releases owner-follow priority inside the normal follow radius") && ok;
	return ok;
}

bool runFollowerAttackAIGateTest()
{
	bool ok = true;
	for (const bool running : { false, true })
	{
		// Positive control, global DisableNpcAI, and the per-NPC disable flag.
		for (const int disableMode : { 0, 1, 2 })
		{
			GameManager gameManager;
			gameManager.global.data.NPCAI = true;
			gameManager.map->data = std::make_shared<MapData>();
			gameManager.map->data->head.width = 16;
			gameManager.map->data->head.height = 16;
			gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
			gameManager.player->setPosition({ 14, 14 }, false);
			auto follower = std::make_shared<NPC>();
			auto target = std::make_shared<NPC>();
			follower->kind = nkBattle;
			follower->relation = nrHostile;
			follower->stopFindingTarget = 1;
			follower->followNPC = "AI_GATE_TARGET";
			follower->setPosition({ 3, 7 }, false);
			follower->visionRadius = 10;
			follower->attackLevel = 1;
			follower->idle = 0;
			target->kind = nkBattle;
			target->relation = nrFriendly;
			target->npcName = "AI_GATE_TARGET";
			target->life = 100;
			target->setPosition({ 5, 7 }, false);
			gameManager.npcManager->npcList = { follower, target };
			gameManager.map->createDataMap();

			NPCActionRes action = makeActionWithDirections(8);
			action.imagePackage->interval = 100;
			action.imagePackage->frame.resize(8);
			follower->res.stand = action;
			follower->res.walk = action;
			follower->res.run = action;
			follower->res.attack = action;
			auto magic = std::make_shared<Magic>();
			magic->iniName = "ai-gate-attack.ini";
			magic->loadSucceeded = true;
			magic->level[1].moveKind = mmkPoint;
			magic->level[1].lifeFrame = 100;
			NPCAttackOption option;
			option.magic = magic;
			option.moveKind = mmkPoint;
			option.configuredUseDistance = 1;
			option.hasExplicitUseDistance = true;
			follower->attackOptions.push_back(option);
			follower->setTime(1000);
			if (running)
			{
				follower->beginRun({ 4, 7 });
			}
			else
			{
				follower->beginWalk({ 4, 7 });
			}
			const bool moveStarted = running ? follower->isRunning() : follower->isWalking();
			ok = check(moveStarted && follower->stepLastTime > 0,
				"AI gate fixture starts a real walk/run step") && ok;
			if (!moveStarted || follower->stepLastTime == 0)
			{
				continue;
			}
			if (disableMode == 1)
			{
				gameManager.scriptAPI.disableNPCAI();
			}
			else if (disableMode == 2)
			{
				follower->setAIDisabled(true);
			}
			const UTime elapsed = follower->stepLastTime * 2;
			follower->setTime(follower->getTime() + elapsed);
			follower->actionManager->update(elapsed);
			const std::string caseName = std::string(running ? "run" : "walk")
				+ " follower AI mode " + std::to_string(disableMode);
			ok = check(follower->getPosition() == Point{ 4, 7 }
				&& follower->isAttacking() == (disableMode == 0),
				(caseName + " completes the step and starts a new attack only while AI is enabled").c_str()) && ok;
			if (disableMode != 0)
			{
				ok = check(follower->isStanding() && follower->stepList.empty(),
					(caseName + " does not extend the completed path to chase the target").c_str()) && ok;
				// Explicit scripted attacks remain allowed with autonomous AI off.
				follower->beginAttack(target->getPosition(), target);
			}
			gameManager.scriptAPI.disableNPCAI();
			ok = check(follower->isAttacking(),
				"disabling AI does not cancel an already prepared or explicit attack") && ok;
			const UTime attackDuration = follower->actionLastTime;
			follower->setTime(follower->getTime() + attackDuration);
			follower->actionManager->update(attackDuration);
			ok = check(follower->isStanding()
				&& gameManager.effectManager->effectList.size() == 1,
				"the prepared attack still releases exactly one effect with AI disabled") && ok;
			if (disableMode != 0)
			{
				gameManager.global.data.NPCAI = disableMode != 1;
				const Point scriptedDestination = { 4, 11 };
				if (running)
				{
					follower->beginRun(scriptedDestination);
				}
				else
				{
					follower->beginWalk(scriptedDestination);
				}
				ok = check(follower->stepList.size() > 1,
					"AI-disabled scripted movement has multiple planned steps") && ok;
				for (int step = 0; step < 16 && (follower->isWalking() || follower->isRunning()); ++step)
				{
					const UTime stepDuration = follower->stepLastTime * 2;
					follower->setTime(follower->getTime() + stepDuration);
					follower->actionManager->update(stepDuration);
				}
				ok = check(follower->isStanding()
					&& follower->getPosition() == scriptedDestination
					&& gameManager.effectManager->effectList.size() == 1,
					(caseName + " preserves every explicitly issued movement step without attacking").c_str()) && ok;
				gameManager.scriptAPI.enableNPCAI();
				follower->setAIDisabled(false);
				follower->setPosition({ 4, 7 }, false);
				follower->nextFollowCheckTime = 1;
				follower->actionManager->update(1);
				ok = check(follower->isAttacking(),
					"reenabling AI permits the hostile follower to attack again") && ok;
			}
		}
	}
	return ok;
}

bool runProjectileTilePathTest()
{
	GameManager gameManager;
	auto& map = *gameManager.map;
	map.data = std::make_shared<MapData>();
	map.data->head.width = 64;
	map.data->head.height = 64;
	map.data->tile.assign(64, std::vector<MapTile>(64));
	map.createDataMap();
	bool ok = true;
	// Recorded desert projectile steps: only the middle trajectory crosses the rock.
	struct Step
	{
		Point from;
		PointEx fromOffset;
		Point to;
		PointEx toOffset;
		Point direction;
		std::deque<Point> expected;
	};
	const Step steps[] =
	{
		{ {29, 30}, {-18.767f, -2.590f}, {28, 31}, {-1.272f, -14.759f}, {-884, 467},
			{ {29, 30}, {28, 31} } },
		{ {29, 29}, {0.0f, 0.0f}, {28, 30}, {18.378f, -2.748f}, {-946, 323},
			{ {29, 29}, {29, 30}, {28, 29}, {28, 30} } },
		{ {27, 33}, {10.883f, -3.675f}, {26, 35}, {17.753f, -6.248f}, {-696, 717},
			{ {27, 33}, {27, 34}, {26, 35} } }
	};
	for (int reflection = 0; reflection < 4; ++reflection)
	{
		auto reflect = [reflection](Point point)
		{
			if (reflection & 1) point.x = 63 - point.x - point.y % 2;
			if (reflection & 2) point.y = 64 - point.y;
			return point;
		};
		for (auto step : steps)
		{
			step.from = reflect(step.from);
			step.to = reflect(step.to);
			for (auto& point : step.expected) point = reflect(point);
			const int xSign = (reflection & 1) ? -1 : 1;
			const int ySign = (reflection & 2) ? -1 : 1;
			const auto path = map.getPassPathEx(step.from,
				{step.fromOffset.x * xSign, step.fromOffset.y * ySign}, step.to,
				{step.toOffset.x * xSign, step.toOffset.y * ySign},
				{step.direction.x * xSign, step.direction.y * ySign});
			ok = check(path == step.expected,
				"projectile path preserves pixel offsets and stops at the actual frame endpoint") && ok;
		}
	}
	const auto& step = steps[0];
	auto effect = std::make_shared<Effect>();
	effect->doing = ekFlying;
	effect->position = step.to;
	effect->passPath = map.getPassPathEx(step.from, step.fromOffset, step.to, step.toOffset, step.direction);
	gameManager.effectManager->addEffect(effect);
	map.data->tile[29][28].obstacle = 0x80;
	CollisionDetector::detectCollision();
	ok = check(effect->doing == ekFlying,
		"nearby rock outside the projectile segment does not cause an explosion") && ok;
	map.data->tile[step.to.y][step.to.x].obstacle = 0x80;
	CollisionDetector::detectCollision();
	ok = check(effect->doing != ekFlying,
		"rock on the projectile segment still blocks the projectile") && ok;
	map.data->tile[step.to.y][step.to.x].obstacle = 0;
	map.data->tile[35][27].obstacle = 0x80;
	map.data->tile[36][28].obstacle = 0x80;
	auto target = std::make_shared<NPC>();
	target->kind = nkBattle;
	target->relation = nrFriendly;
	target->radius = 0.4f;
	target->setPosition({25, 35}, false);
	gameManager.npcManager->npcList.push_back(target);
	map.createDataMap();
	// Exercise real movement, width expansion and collision at different frame durations.
	for (UTime milliseconds : {1, 8, 16, 17, 33, 50})
	{
		gameManager.effectManager->clearEffect();
		effect = std::make_shared<Effect>();
		effect->doing = ekFlying;
		effect->level = 5;
		effect->magic.level[5].moveKind = mmkSector;
		effect->magic.attackAll = 1;
		effect->evade = -1000;
		effect->lifeTime = 10000;
		effect->beginTime = 0;
		effect->setTime(0);
		effect->speed = 8;
		effect->position = effect->src = {29, 29};
		effect->dest = {25, 35};
		effect->flyingDirection = {-800, 600};
		gameManager.effectManager->addEffect(effect);
		for (int frame = 0; frame < 2000 && effect->doing == ekFlying; ++frame)
		{
			ProjectileCollisionTestAccess::advance(*effect, milliseconds);
			CollisionDetector::detectCollision();
		}
		const bool reachedTarget = Map::getTileDistance(effect->position, effect->offset,
			target->getPosition(), target->getOffset()) <= 0.66f;
		if (!reachedTarget)
		{
			std::cerr << "Projectile stopped at " << effect->position.x << ',' << effect->position.y
				<< " with frame duration " << milliseconds << " ms\n";
		}
		ok = check(reachedTarget,
			"center projectile crosses the rock gap and reaches the target at every frame duration") && ok;
	}
	return ok;
}

bool runNeutralProjectileCollisionTest()
{
	GameManager gameManager;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 12;
	gameManager.map->data->head.height = 12;
	gameManager.map->data->tile.assign(12, std::vector<MapTile>(12));
	auto caster = std::make_shared<NPC>();
	caster->kind = nkBattle;
	caster->setPosition({ 2, 2 }, false);
	auto target = std::make_shared<NPC>();
	target->setPosition({ 5, 5 }, false);
	gameManager.npcManager->npcList = { caster, target };
	gameManager.map->createDataMap();

	auto verifyCollision = [&](int kind, int relation, int casterRelation,
		int attackAll, uint8_t obstacle, bool expectedExplosion, const char* description)
	{
		target->kind = kind;
		target->relation = relation;
		target->life = 100;
		target->lifeMax = 100;
		caster->relation = casterRelation;
		gameManager.map->data->tile[5][5].obstacle = obstacle;
		gameManager.effectManager->effectList.clear();
		auto effect = std::make_shared<Effect>();
		effect->doing = ekFlying;
		effect->position = { 5, 5 };
		effect->user = caster;
		effect->launcherKind = lkEnemy;
		effect->magic.attackAll = attackAll;
		effect->evade = -1000; // Misses still distinguish collision from damage.
		effect->width = 0.5f;
		effect->lifeTime = 1;
		gameManager.effectManager->effectList.push_back(effect);
		CollisionDetector::detectCollision();
		return check(effect->doing == (expectedExplosion ? ekExploding : ekFlying)
			&& target->life == 100, description);
	};

	bool ok = verifyCollision(nkNormal, nrFriendly, nrHostile, 0, 0, false,
		"ordinary spectator does not stop hostile projectile on clear tile");
	ok = verifyCollision(nkBattle, nrNeutral, nrFriendly, 0, 0, false,
		"neutral fighter does not stop friendly projectile on clear tile") && ok;
	ok = verifyCollision(nkBattle, nrNeutral, nrHostile, 0, 0, false,
		"neutral fighter does not stop hostile projectile on clear tile") && ok;
	ok = verifyCollision(nkBattle, nrNeutral, nrHostile, 1, 0, true,
		"AttackAll projectile collides with neutral fighter before missed damage") && ok;
	ok = verifyCollision(nkBattle, nrNeutral, nrNone, 0, 0, true,
		"None-relation caster projectile collides with neutral fighter") && ok;
	ok = verifyCollision(nkNormal, nrFriendly, nrHostile, 1, 0, false,
		"AttackAll still excludes ordinary spectator from character collision") && ok;
	ok = verifyCollision(nkBattle, nrNeutral, nrHostile, 0, 0x02, true,
		"blocked tile explodes hostile projectile while neutral fighter remains untouched") && ok;
	return ok;
}

bool runSweptProjectileCollisionTest()
{
	GameManager gameManager;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 12;
	gameManager.map->data->head.height = 12;
	gameManager.map->data->tile.assign(12, std::vector<MapTile>(12));
	gameManager.map->createDataMap();

	auto target = std::make_shared<NPC>();
	target->kind = nkBattle;
	target->relation = nrHostile;
	target->radius = 0.4f;
	gameManager.npcManager->npcList.push_back(target);
	target->setPosition({ 5, 5 }, false);
	gameManager.map->createDataMap();

	auto makeEffect = []()
	{
		auto effect = std::make_shared<Effect>();
		effect->doing = ekFlying;
		effect->magic.attackAll = 1;
		effect->evade = -1000;
		effect->width = 0.5f;
		effect->lifeTime = 1;
		return effect;
	};

	auto crossingEffect = makeEffect();
	crossingEffect->collisionSweepStartPosition = { 2, 5 };
	crossingEffect->collisionSweepStartOffset = { 0.0f, 0.0f };
	crossingEffect->collisionSweepInitialized = true;
	crossingEffect->position = { 8, 5 };
	crossingEffect->offset = { 0.0f, 0.0f };
	crossingEffect->passPath =
	{
		{ 3, 5 }, { 4, 5 }, { 5, 5 },
		{ 6, 5 }, { 7, 5 }, { 8, 5 }
	};
	gameManager.effectManager->effectList.push_back(crossingEffect);
	CollisionDetector::detectCollision();
	bool ok = check(
		crossingEffect->doing == ekExploding,
		"path-cell broad phase detects an NPC crossed between frame endpoints");
	const float crossingCollisionDistance = Map::getTileDistance(
		crossingEffect->position,
		crossingEffect->offset,
		target->getPosition(),
		target->getOffset());
	ok = check(
		std::abs(crossingCollisionDistance -
			(crossingEffect->width * 0.5f + target->radius)) <= 0.02f &&
		crossingEffect->position != Point{ 8, 5 },
		"swept NPC collision resolves at the first contact position") && ok;
	gameManager.effectManager->effectList.clear();

	auto passThroughEffect = makeEffect();
	passThroughEffect->magic.passThrough = 1;
	passThroughEffect->magic.passThroughWithDestroyEffect = 1;
	passThroughEffect->collisionSweepStartPosition = { 2, 5 };
	passThroughEffect->collisionSweepStartOffset = { 0.0f, 0.0f };
	passThroughEffect->collisionSweepInitialized = true;
	passThroughEffect->position = { 8, 5 };
	passThroughEffect->offset = { 0.0f, 0.0f };
	passThroughEffect->passPath = crossingEffect->passPath;
	gameManager.effectManager->effectList.push_back(passThroughEffect);
	CollisionDetector::detectCollision();
	auto passThroughHitEffect =
		gameManager.effectManager->effectList.size() == 2
		? gameManager.effectManager->effectList.back()
		: nullptr;
	ok = check(
		passThroughEffect->position == Point{ 8, 5 } &&
		passThroughEffect->offset == PointEx{ 0.0f, 0.0f } &&
		passThroughHitEffect != nullptr &&
		std::abs(Map::getTileDistance(
			passThroughHitEffect->position,
			passThroughHitEffect->offset,
			target->getPosition(),
			target->getOffset()) -
			(passThroughEffect->width * 0.5f + target->radius)) <= 0.02f,
		"pass-through projectile keeps its frame endpoint and places the hit visual at first contact") && ok;
	gameManager.effectManager->effectList.clear();

	auto nearMissEffect = makeEffect();
	nearMissEffect->collisionSweepStartPosition = { 2, 3 };
	nearMissEffect->collisionSweepStartOffset = { 0.0f, 0.0f };
	nearMissEffect->collisionSweepInitialized = true;
	nearMissEffect->position = { 8, 3 };
	nearMissEffect->offset = { 0.0f, 0.0f };
	ok = check(
		!CollisionDetector::detectCollision(target, nearMissEffect),
		"swept projectile does not hit an NPC outside the combined radius") && ok;

	auto newEffect = makeEffect();
	newEffect->position = { 8, 5 };
	newEffect->offset = { 0.0f, 0.0f };
	ok = check(
		!CollisionDetector::detectCollision(target, newEffect),
		"new projectile without a frame start uses endpoint collision only") && ok;
	return ok;
}

bool runSweptProjectileInteractionTest()
{
	GameManager gameManager;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 12;
	gameManager.map->data->head.height = 12;
	gameManager.map->data->tile.assign(12, std::vector<MapTile>(12));
	gameManager.map->createDataMap();

	auto makeProjectile = [](
		Point start,
		Point end,
		int launcherKind)
	{
		auto effect = std::make_shared<Effect>();
		effect->doing = ekFlying;
		effect->launcherKind = launcherKind;
		effect->width = 0.1f;
		effect->lifeTime = 1;
		effect->magic.passThroughWall = 1;
		effect->collisionSweepStartPosition = start;
		effect->collisionSweepStartOffset = { 0.0f, 0.0f };
		effect->collisionSweepInitialized = true;
		effect->position = end;
		effect->offset = { 0.0f, 0.0f };
		return effect;
	};

	auto orderedDiscard = makeProjectile({ 1, 8 }, { 9, 8 }, lkFriend);
	orderedDiscard->magic.discardOppositeMagic = 1;
	auto laterTarget = makeProjectile({ 7, 8 }, { 7, 8 }, lkEnemy);
	auto earlierTarget = makeProjectile({ 3, 8 }, { 3, 8 }, lkEnemy);
	gameManager.effectManager->effectList =
	{
		orderedDiscard,
		laterTarget,
		earlierTarget
	};
	CollisionDetector::detectCollision();
	bool ok = check(
		orderedDiscard->doing == ekHiding &&
		earlierTarget->doing == ekHiding &&
		laterTarget->doing == ekFlying,
		"projectile collisions resolve by sweep time before target index");

	auto tiedDiscard = makeProjectile({ 1, 4 }, { 9, 4 }, lkFriend);
	tiedDiscard->magic.discardOppositeMagic = 1;
	auto firstTiedTarget = makeProjectile({ 5, 4 }, { 5, 4 }, lkEnemy);
	auto secondTiedTarget = makeProjectile({ 5, 4 }, { 5, 4 }, lkEnemy);
	gameManager.effectManager->effectList =
	{
		tiedDiscard,
		firstTiedTarget,
		secondTiedTarget
	};
	CollisionDetector::detectCollision();
	ok = check(
		tiedDiscard->doing == ekHiding &&
		firstTiedTarget->doing == ekHiding &&
		secondTiedTarget->doing == ekFlying,
		"equal-time projectile collisions resolve by snapshot index") && ok;

	auto exchange = makeProjectile({ 2, 10 }, { 8, 10 }, lkFriend);
	exchange->magic.exchangeUser = 1;
	exchange->flyingDirection = { 1000, 0 };
	exchange->speed = 32;
	auto reflected = makeProjectile({ 8, 10 }, { 2, 10 }, lkEnemy);
	reflected->flyingDirection = { 0, 1000 };
	reflected->speed = 16;
	const Point reflectedEndPosition = reflected->position;
	const PointEx reflectedEndOffset = reflected->offset;
	gameManager.effectManager->effectList = { exchange, reflected };
	CollisionDetector::detectCollision();
	const Point reflectedCollisionPosition = reflected->position;
	const PointEx reflectedCollisionOffset = reflected->offset;
	const float projectileCollisionDistance = Map::getTileDistance(
		exchange->position,
		exchange->offset,
		reflected->position,
		reflected->offset);
	ok = check(
		exchange->doing == ekHiding &&
		reflected->doing == ekFlying &&
		reflected->launcherKind == lkFriend &&
		(reflected->position != reflectedEndPosition ||
			reflected->offset != reflectedEndOffset) &&
		std::abs(projectileCollisionDistance -
			(exchange->width + reflected->width) * 0.5f) <= 0.02f &&
		reflected->src == reflected->position &&
		reflected->srcOffset == reflected->offset,
		"exchange starts the new trajectory at the swept contact position") && ok;
	reflected->updateEffectPosition(40, (float)reflected->speed);
	ok = check(
		reflected->position != reflectedCollisionPosition ||
		reflected->offset != reflectedCollisionOffset,
		"exchanged projectile moves along its new trajectory on the next update") && ok;

	auto delayedDiscard = makeProjectile({ 5, 6 }, { 5, 6 }, lkFriend);
	delayedDiscard->magic.discardOppositeMagic = 1;
	auto newbornTarget = makeProjectile({ 5, 6 }, { 5, 6 }, lkEnemy);
	gameManager.effectManager->effectList.clear();
	gameManager.effectManager->addEffect(delayedDiscard);
	gameManager.effectManager->addEffect(newbornTarget);
	CollisionDetector::detectCollision();
	ok = check(
		delayedDiscard->doing == ekFlying && newbornTarget->doing == ekFlying,
		"projectiles created in the current frame do not collide") && ok;
	ProjectileCollisionTestAccess::beginFrame(*gameManager.effectManager);
	CollisionDetector::detectCollision();
	ok = check(
		delayedDiscard->doing == ekHiding && newbornTarget->doing == ekHiding,
		"projectiles created in the previous frame can collide") && ok;
	return ok;
}

bool runProjectileCreationFrameTest()
{
	GameManager gameManager;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 16;
	gameManager.map->data->head.height = 16;
	gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
	gameManager.map->createDataMap();

	auto makePointMagic = [](const std::string& name)
	{
		auto magic = std::make_shared<Magic>();
		magic->iniName = name;
		magic->loadSucceeded = true;
		magic->passThroughWall = 1;
		magic->level[1].moveKind = mmkPoint;
		magic->level[1].lifeFrame = 100;
		return magic;
	};
	auto makePassiveProjectile = []()
	{
		auto effect = std::make_shared<Effect>();
		effect->doing = ekFlying;
		effect->launcherKind = lkEnemy;
		effect->width = 0.1f;
		effect->lifeTime = 1;
		effect->magic.passThroughWall = 1;
		effect->collisionSweepStartPosition = { 5, 5 };
		effect->collisionSweepStartOffset = { 0.0f, 0.0f };
		effect->collisionSweepInitialized = true;
		effect->position = { 5, 5 };
		effect->offset = { 0.0f, 0.0f };
		return effect;
	};

	bool ok = true;
	auto verifyDeferredCollision = [&](
		const std::shared_ptr<Effect>& createdEffect,
		const char* creationMessage,
		const char* currentFrameMessage,
		const char* nextFrameMessage)
	{
		bool scenarioOk = check(
			createdEffect != nullptr &&
			createdEffect->projectileCollisionCreationFrame ==
				gameManager.effectManager->getProjectileCollisionFrame(),
			creationMessage);
		if (createdEffect == nullptr)
		{
			gameManager.effectManager->freeResource();
			return false;
		}

		createdEffect->doing = ekFlying;
		createdEffect->width = 0.1f;
		createdEffect->lifeTime = 1;
		createdEffect->magic.discardOppositeMagic = 1;
		createdEffect->magic.passThroughWall = 1;
		createdEffect->collisionSweepStartPosition = { 5, 5 };
		createdEffect->collisionSweepStartOffset = { 0.0f, 0.0f };
		createdEffect->collisionSweepInitialized = true;
		createdEffect->position = { 5, 5 };
		createdEffect->offset = { 0.0f, 0.0f };
		auto passiveEffect = makePassiveProjectile();
		gameManager.effectManager->effectList.push_back(passiveEffect);

		CollisionDetector::detectCollision();
		scenarioOk = check(
			createdEffect->doing == ekFlying &&
			passiveEffect->doing == ekFlying,
			currentFrameMessage) && scenarioOk;

		ProjectileCollisionTestAccess::beginFrame(*gameManager.effectManager);
		CollisionDetector::detectCollision();
		scenarioOk = check(
			createdEffect->doing == ekHiding &&
			passiveEffect->doing == ekHiding,
			nextFrameMessage) && scenarioOk;
		gameManager.effectManager->freeResource();
		return scenarioOk;
	};

	ProjectileCollisionTestAccess::beginFrame(*gameManager.effectManager);
	auto delayedMagic = makePointMagic("deferred_collision_delayed.ini");
	delayedMagic->discardOppositeMagic = 1;
	gameManager.effectManager->addDelayedMagic(
		delayedMagic,
		gameManager.player,
		{ 5, 5 },
		{ 5, 5 },
		1,
		lkFriend,
		nullptr,
		0);
	gameManager.effectManager->onUpdate();
	auto delayedEffect = gameManager.effectManager->effectList.empty()
		? nullptr
		: gameManager.effectManager->effectList.back();
	ok = verifyDeferredCollision(
		delayedEffect,
		"delayed magic records its collision creation frame",
		"delayed magic does not collide in its creation frame",
		"delayed magic collides in the next frame") && ok;

	ProjectileCollisionTestAccess::beginFrame(*gameManager.effectManager);
	auto flyChildMagic = makePointMagic("deferred_collision_fly_child.ini");
	flyChildMagic->discardOppositeMagic = 1;
	auto flyParentMagic = makePointMagic("deferred_collision_fly_parent.ini");
	flyParentMagic->level[1].moveKind = mmkSelf;
	flyParentMagic->linkedLevel[1].flyMagic = flyChildMagic;
	flyParentMagic->linkedLevel[1].flyInterval = 0;
	auto flyParentEffect = std::make_shared<Effect>();
	flyParentEffect->level = 1;
	flyParentEffect->user = gameManager.player;
	flyParentEffect->launcherKind = lkFriend;
	flyParentEffect->position = { 4, 5 };
	flyParentEffect->dest = { 5, 5 };
	flyParentEffect->initFromMagic(flyParentMagic);
	flyParentEffect->doing = ekFlying;
	flyParentEffect->lifeTime = 100000;
	ProjectileCollisionTestAccess::update(*flyParentEffect);
	auto flyChildEffect = gameManager.effectManager->effectList.empty()
		? nullptr
		: gameManager.effectManager->effectList.back();
	ok = verifyDeferredCollision(
		flyChildEffect,
		"flying derived magic records its collision creation frame",
		"flying derived magic does not collide in its creation frame",
		"flying derived magic collides in the next frame") && ok;

	ProjectileCollisionTestAccess::beginFrame(*gameManager.effectManager);
	auto explodeChildMagic = makePointMagic("deferred_collision_explode_child.ini");
	explodeChildMagic->discardOppositeMagic = 1;
	auto collisionMagic = makePointMagic("deferred_collision_parent.ini");
	collisionMagic->level[1].moveKind = mmkFly;
	collisionMagic->attackAll = 1;
	collisionMagic->explodeMagicsByLevel[1] = explodeChildMagic;
	auto collisionEffect = std::make_shared<Effect>();
	collisionEffect->level = 1;
	collisionEffect->user = gameManager.player;
	collisionEffect->launcherKind = lkFriend;
	collisionEffect->position = { 6, 6 };
	collisionEffect->src = collisionEffect->position;
	collisionEffect->initFromMagic(collisionMagic);
	collisionEffect->doing = ekFlying;
	collisionEffect->lifeTime = 100000;
	auto collisionTarget = std::make_shared<NPC>();
	collisionTarget->kind = nkBattle;
	collisionTarget->relation = nrHostile;
	collisionTarget->radius = 0.4f;
	collisionTarget->setPosition(collisionEffect->position, false);
	gameManager.npcManager->npcList.push_back(collisionTarget);
	const bool collisionTriggered =
		CollisionDetector::detectCollision(collisionTarget, collisionEffect);
	auto collisionChildEffect = gameManager.effectManager->effectList.empty()
		? nullptr
		: gameManager.effectManager->effectList.back();
	ok = check(collisionTriggered,
		"NPC collision callback creates its linked projectile") && ok;
	ok = verifyDeferredCollision(
		collisionChildEffect,
		"collision-created magic records its collision creation frame",
		"collision-created magic does not collide in its creation frame",
		"collision-created magic collides in the next frame") && ok;
	return ok;
}

}

bool runGameplayAutomationRuntimeTests();

int main(int argc, char** argv)
{
#if defined(__MOBILE__)
	if (argc > 1)
	{
		const std::string mode = argv[1];
		if (mode == "--magic-derived" ||
			mode == "--magic-experience" ||
			mode == "--replacement-experience" ||
			mode == "--effect-persistence" ||
			mode == "--map-thumbnail-controller" ||
			mode == "--partner-equipment-transfer" ||
			mode == "--world-interaction-runtime" ||
			mode == "--editor-run-scene-runtime")
		{
			std::cout
				<< "SKIP: desktop host-resource fixture is not a mobile surrogate acceptance test: "
				<< mode << '\n';
			return 0;
		}
	}
#endif
	std::unique_ptr<ScopedProductionStateIsolation>
		productionStateIsolation;
	if (argc > 1 && testModeUsesProductionResources(argv[1]))
	{
		productionStateIsolation =
			std::make_unique<ScopedProductionStateIsolation>(
				"jxqy_production_state_isolation_test");
		if (!productionStateIsolation->valid())
		{
			std::cerr <<
				"FAILED: create isolated config/save parent for production resource test\n";
			return 1;
		}
	}
	if (argc > 1 && std::string(argv[1]) == "--gamble-menu")
	{
		return runGambleMenuRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--gameplay-automation")
	{
		return runGameplayAutomationRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--script-movement")
	{
		return runScriptMovementRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--magic-derived")
	{
		return runMagicDerivedRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--magic-experience")
	{
		return runMagicExperienceTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--replacement-experience")
	{
		return runReplacementExperienceOwnershipTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--effect-persistence")
	{
		return runEffectRuntimePersistenceTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--npc-persistence")
	{
		return runNpcRuntimePersistenceTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--object-animation")
	{
		return runObjectAnimationRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--media-runtime")
	{
		return runMediaRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--core-lifecycle")
	{
		return runCoreLifecycleTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--current-mod-compatibility")
	{
		return runCurrentModCompatibilityTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--qingyu-ui")
	{
		return runQingyuUiTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--save-write-sharing")
	{
		return runSaveWriteSharingRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--save-stability")
	{
		return runSaveStabilityTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-missing-object-routes")
	{
		return runXiaoxiangMissingObjectRouteTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-tournament-routes")
	{
		return runXiaoxiangTournamentRouteTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-prison-ending")
	{
		return runXiaoxiangPrisonEndingTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-companion-history")
	{
		return runXiaoxiangCompanionHistoryTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-companion-history-sync")
	{
		return runXiaoxiangCompanionHistoryTests(0) ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-companion-history-async")
	{
		return runXiaoxiangCompanionHistoryTests(1) ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--xiaoxiang-legacy-entrances")
	{
		return runXiaoxiangLegacyEntranceTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--moonlight-trap-routes")
	{
		return runMoonlightTrapRouteTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--moonlight-departure")
	{
		return runMoonlightDepartureTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--new-sword-boat-routes")
	{
		return runNewSwordBoatRouteTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--new-sword-yang-ying-routes")
	{
		return runNewSwordYangYingRouteTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--moonlight-ending-two-routes")
	{
		return runMoonlightEndingTwoRouteTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--sword-two-partner-departures")
	{
		return runSwordTwoPartnerDepartureTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--sword-two-historical-scripts")
	{
		return runSwordTwoHistoricalScriptTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--bilibili-story-feedback")
	{
		return runBilibiliStoryFeedbackTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--merged-entity-save")
	{
		return runMergedEntitySaveTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--production-attack-files")
	{
		return runProductionAttackFileRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--full-attack-save")
	{
		return runFullAttackSaveRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--equipment-replacement-save")
	{
		return runEquipmentReplacementSaveRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--dynamic-magic-list-save")
	{
		return runDynamicMagicListSaveRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--full-experience-save")
	{
		return runFullExperienceSaveRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--character-caster-save")
	{
		return runCharacterCasterSaveRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--ui-focus")
	{
		return runUIFocusTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--map-thumbnail-controller")
	{
		return runMapThumbnailControllerTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--partner-equipment-transfer")
	{
		return runPartnerEquipmentTransferTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--world-interaction-runtime")
	{
		return runWorldInteractionRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--gamepad-world-runtime")
	{
		return runGamepadWorldRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--gamepad-essential-ui")
	{
		return runGamepadEssentialUITests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--gamepad-rpg-menu-actions")
	{
		return runGamepadRPGMenuActionTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--gamepad-surface-contract")
	{
		return runGamepadSurfaceContractTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--mobile-external-input-runtime")
	{
		return runMobileExternalInputRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--script-engine-runtime")
	{
		return runScriptEngineRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 &&
		std::string(argv[1]) == "--editor-run-scene-runtime")
	{
		return runEditorRunSceneRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--map-v3-load")
	{
		return runMapV3RuntimeTests() ? 0 : 1;
	}
	if (argc > 1)
	{
		std::cerr << "Unknown test mode: " << argv[1] << '\n'
			<< "Expected one of: --gamble-menu, --magic-derived, "
				"--effect-persistence, --npc-persistence, --object-animation, --current-mod-compatibility, "
				"--media-runtime, --core-lifecycle, --save-write-sharing, --xiaoxiang-missing-object-routes, --xiaoxiang-tournament-routes, --xiaoxiang-prison-ending, --xiaoxiang-companion-history, --xiaoxiang-companion-history-sync, --xiaoxiang-companion-history-async, --xiaoxiang-legacy-entrances, --production-attack-files, --full-attack-save, --script-engine-runtime, "
				"--merged-entity-save, "
				"--equipment-replacement-save, "
				"--dynamic-magic-list-save, "
				"--full-experience-save, "
				"--character-caster-save, "
				"--editor-run-scene-runtime, "
				"--ui-focus, --map-thumbnail-controller, "
				"--partner-equipment-transfer, --world-interaction-runtime, "
				"--gamepad-world-runtime, --gamepad-essential-ui, "
				"--gamepad-rpg-menu-actions, "
				"--gamepad-surface-contract, "
				"--mobile-external-input-runtime, "
				"--map-v3-load\n";
		return 2;
	}

	bool ok = true;

	ok = runParasiticIntervalDefaultTest() && ok;
	ok = runMapObstacleSemanticsTest() && ok;
	ok = runNonCombatPartnerTargetingTest() && ok;
	ok = runPartnerCombatOwnerLeashTest() && ok;
	ok = runFollowerAttackAIGateTest() && ok;
	ok = runProjectileTilePathTest() && ok;
	ok = runNeutralProjectileCollisionTest() && ok;
	ok = runSweptProjectileCollisionTest() && ok;
	ok = runSweptProjectileInteractionTest() && ok;
	ok = runProjectileCreationFrameTest() && ok;

	ok = check(NPCManager::getLauncherHitPriority(nrFriendly, 0, nrHostile, 0) == 0,
		"friendly projectile hits hostile target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrFriendly, 0, nrNeutral, 0) == INT_MAX,
		"friendly projectile ignores true neutral target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrFriendly, 0, nrFriendly, 0) == INT_MAX,
		"friendly projectile ignores friendly target") && ok;
	ok = check(!NPCManager::isEnemyOf(nrFriendly, nrNeutral),
		"friendly AI does not target true neutral relation") && ok;
	ok = check(NPCManager::isEnemyOf(nrFriendly, nrNone),
		"friendly AI can target RelationType.None fighter") && ok;

	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 3, nrFriendly, 0) == 0,
		"hostile projectile prioritizes player-side target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 3, nrNeutral, 0) == INT_MAX,
		"hostile projectile ignores true neutral target") && ok;
	ok = check(!NPCManager::isEnemyOf(nrHostile, nrNeutral),
		"hostile AI does not target true neutral relation") && ok;
	ok = check(NPCManager::getAutomaticTargetPriority(
		nkBattle, nrHostile, 274, 0, nkNormal, nrFriendly, 270, false) == INT_MAX,
		"hostile AI ignores ordinary friendly non-combat NPCs") && ok;
	ok = check(NPCManager::getAutomaticTargetPriority(
		nkBattle, nrHostile, 274, 0, nkBattle, nrFriendly, 272, false) == 1,
		"hostile AI targets friendly fighters") && ok;
	ok = check(NPCManager::getAutomaticTargetPriority(
		nkBattle, nrHostile, 274, 0, nkPlayer, nrFriendly, 0, true) == 1,
		"hostile AI targets the player") && ok;
	ok = check(NPCManager::getAutomaticTargetPriority(
		nkBattle, nrHostile, 274, 1, nkBattle, nrFriendly, 272, false) == INT_MAX,
		"NoAutoAttackPlayer also suppresses automatic friendly-fighter targeting") && ok;
	ok = check(NPCManager::getAutomaticTargetPriority(
		nkBattle, nrHostile, 274, 1, nkBattle, nrHostile, 275, false) == 0,
		"NoAutoAttackPlayer still permits other hostile groups to fight") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 3, nrHostile, 4) == 1,
		"hostile projectile can hit other group hostile target after player-side targets") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 3, nrHostile, 3) == INT_MAX,
		"hostile projectile ignores same group hostile target") && ok;

	ok = check(NPCManager::getLauncherHitPriority(nrNeutral, 0, nrFriendly, 0) == INT_MAX,
		"true neutral projectile ignores friendly target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrNeutral, 0, nrHostile, 0) == INT_MAX,
		"true neutral projectile ignores hostile target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrNeutral, 0, nrNeutral, 0) == INT_MAX,
		"true neutral projectile ignores neutral target") && ok;
	ok = check(!NPCManager::isEnemyOf(nrNeutral, nrFriendly),
		"true neutral AI does not target friendly relation") && ok;
	ok = check(!NPCManager::isEnemyOf(nrNeutral, nrHostile),
		"true neutral AI does not target hostile relation") && ok;
	ok = check(!NPCManager::isEnemyOf(nrNeutral, nrNone),
		"true neutral AI does not target RelationType.None fighter") && ok;

	ok = check(NPCManager::getLauncherHitPriority(nrNone, 0, nrFriendly, 0) == 0,
		"none fighter projectile hits friendly target like C# RelationType.None") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrNone, 0, nrHostile, 0) == 0,
		"none fighter projectile hits hostile target like C# RelationType.None") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrNone, 0, nrNeutral, 0) == 0,
		"RelationType.None projectile can hit true neutral fighter target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrNone, 0, nrNone, 0) == INT_MAX,
		"none fighter projectile ignores other none fighters") && ok;
	ok = check(NPCManager::isEnemyOf(nrNone, nrNeutral),
		"RelationType.None AI can target true neutral relation") && ok;
	ok = check(!NPCManager::isEnemyOf(nrNone, nrNone),
		"RelationType.None AI ignores other none fighters") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrFriendly, 0, nrNone, 0) == 0,
		"friendly projectile can hit none fighter target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 0, nrNone, 0) == 0,
		"hostile projectile can hit none fighter target") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrNeutral, 0, nrNone, 0) == INT_MAX,
		"true neutral projectile ignores none fighter target") && ok;
	ok = check(NPCManager::canLauncherHitRelation(lkFriend, nrNone),
		"legacy launcher-kind path can hit none fighter target from friend side") && ok;
	ok = check(NPCManager::canLauncherHitRelation(lkEnemy, nrNone),
		"legacy launcher-kind path can hit none fighter target from enemy side") && ok;
	ok = check(!NPCManager::canLauncherHitRelation(lkNeutral, nrNone),
		"neutral launcher-kind path still ignores none fighter target") && ok;

	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 2, nrHostile, 5) == 1,
		"bounce collision uses hostile owner group to hit other hostile group") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrHostile, 2, nrHostile, 2) == INT_MAX,
		"bounce collision uses hostile owner group to ignore same hostile group") && ok;
	ok = check(NPCManager::getLauncherHitPriority(nrFriendly, 0, nrFriendly, 0) == INT_MAX,
		"bounce collision uses friendly owner to ignore friendly target") && ok;

	ok = check(NPCManager::isFriendDeathRelationMatch(nkBattle, nrHostile, nkBattle, nrHostile),
		"hostile fighter reacts to hostile fighter death") && ok;
	ok = check(!NPCManager::isFriendDeathRelationMatch(nkBattle, nrHostile, nkBattle, nrFriendly),
		"hostile fighter ignores friendly fighter death") && ok;
	ok = check(NPCManager::isFriendDeathRelationMatch(nkBattle, nrFriendly, nkPartner, nrFriendly),
		"friendly fighter reacts to friendly partner death") && ok;
	ok = check(!NPCManager::isFriendDeathRelationMatch(nkBattle, nrFriendly, nkNormal, nrFriendly),
		"friendly fighter ignores non-fighter friendly death") && ok;
	ok = check(!NPCManager::isFriendDeathRelationMatch(nkBattle, nrNeutral, nkBattle, nrNeutral),
		"neutral fighter death does not trigger friend-death retreat") && ok;
	ok = check(NPCManager::isFriendDeathRelationMatch(nkBattle, nrNone, nkBattle, nrNone),
		"none fighters react to none fighter death") && ok;

	{
		NPCManager& manager = makeDetachedNPCManager();
		auto watcher = std::make_shared<NPC>();
		auto releasedTarget = std::make_shared<NPC>();
		auto activeTarget = std::make_shared<NPC>();
		manager.npcList.push_back(watcher);
		manager.npcList.push_back(releasedTarget);
		manager.npcList.push_back(activeTarget);

		watcher->fightState.set(true);
		watcher->currentCombatTarget = activeTarget;
		watcher->currentCombatTargetTime = 111;
		watcher->lastCombatTarget = releasedTarget;
		watcher->lastCombatTargetTime = 222;
		watcher->lastCombatMagicDirection = { 1.0f, 0.0f };
		watcher->hasLastCombatMagicDirection = true;

		manager.clearCombatTargetIfEqual(releasedTarget);

		ok = check(watcher->currentCombatTarget.lock() == activeTarget,
			"controlled target cleanup keeps another current combat target when only last target matched") && ok;
		ok = check(watcher->currentCombatTargetTime == 111,
			"controlled target cleanup keeps current combat target time") && ok;
		ok = check(watcher->fightState.get(),
			"controlled target cleanup keeps fight state for another current combat target") && ok;
		ok = check(watcher->lastCombatTarget.expired(),
			"controlled target cleanup clears matching last combat target") && ok;
		ok = check(watcher->lastCombatTargetTime == 0,
			"controlled target cleanup clears matching last combat target time") && ok;
		ok = check(!watcher->hasLastCombatMagicDirection,
			"controlled target cleanup clears stale last combat magic direction") && ok;
	}

	{
		NPCManager& manager = makeDetachedNPCManager();
		auto watcher = std::make_shared<NPC>();
		auto releasedTarget = std::make_shared<NPC>();
		auto activeTarget = std::make_shared<NPC>();
		manager.npcList.push_back(watcher);
		manager.npcList.push_back(releasedTarget);
		manager.npcList.push_back(activeTarget);

		watcher->fightState.set(true);
		watcher->currentCombatTarget = activeTarget;
		watcher->lastKnownCombatTarget = releasedTarget;
		watcher->lastKnownCombatTargetPosition = { 12, 34 };
		watcher->lastKnownCombatTargetTime = 333;
		watcher->hasLastKnownCombatTargetPosition = true;
		watcher->actionPlan.state = npsApproaching;
		watcher->actionPlan.planTarget = activeTarget;
		watcher->actionPlan.planStartTime = 444;

		manager.clearCombatTargetIfEqual(releasedTarget);

		ok = check(watcher->currentCombatTarget.lock() == activeTarget,
			"controlled target cleanup keeps current target when only last known target matched") && ok;
		ok = check(watcher->actionPlan.isActive(),
			"controlled target cleanup keeps action plan for another target") && ok;
		ok = check(watcher->actionPlan.planTarget.lock() == activeTarget,
			"controlled target cleanup keeps action plan target when it does not match") && ok;
		ok = check(watcher->fightState.get(),
			"controlled target cleanup keeps fight state for active plan on another target") && ok;
		ok = check(watcher->lastKnownCombatTarget.expired(),
			"controlled target cleanup clears matching last known target") && ok;
		ok = check(!watcher->hasLastKnownCombatTargetPosition,
			"controlled target cleanup clears matching last known target position") && ok;
	}

	{
		NPCManager& manager = makeDetachedNPCManager();
		auto watcher = std::make_shared<NPC>();
		auto releasedTarget = std::make_shared<NPC>();
		auto activeTarget = std::make_shared<NPC>();
		manager.npcList.push_back(watcher);
		manager.npcList.push_back(releasedTarget);
		manager.npcList.push_back(activeTarget);

		watcher->fightState.set(true);
		watcher->currentCombatTarget = activeTarget;
		watcher->currentCombatTargetTime = 555;
		watcher->actionPlan.state = npsApproaching;
		watcher->actionPlan.planTarget = releasedTarget;
		watcher->actionPlan.planStartTime = 666;

		manager.clearCombatTargetIfEqual(releasedTarget);

		ok = check(!watcher->actionPlan.isActive(),
			"controlled target cleanup resets matching action plan") && ok;
		ok = check(watcher->currentCombatTarget.lock() == activeTarget,
			"controlled target cleanup keeps current target when only action plan matched") && ok;
		ok = check(watcher->currentCombatTargetTime == 555,
			"controlled target cleanup keeps current target time when only action plan matched") && ok;
		ok = check(watcher->fightState.get(),
			"controlled target cleanup keeps fight state for another current target after plan reset") && ok;
	}

	{
		NPCManager& manager = makeDetachedNPCManager();
		auto watcher = std::make_shared<NPC>();
		auto releasedTarget = std::make_shared<NPC>();
		manager.npcList.push_back(watcher);
		manager.npcList.push_back(releasedTarget);

		watcher->fightState.set(true);
		watcher->currentCombatTarget = releasedTarget;
		watcher->currentCombatTargetTime = 777;

		manager.clearCombatTargetIfEqual(releasedTarget);

		ok = check(watcher->currentCombatTarget.expired(),
			"controlled target cleanup clears matching current target") && ok;
		ok = check(watcher->currentCombatTargetTime == 0,
			"controlled target cleanup clears matching current target time") && ok;
		ok = check(!watcher->fightState.get(),
			"controlled target cleanup leaves combat when no target references remain") && ok;
	}

	ok = check(nkGroundAnimal == 4, "ground animal keeps C# CharacterKind value 4") && ok;
	ok = check(nkAfraidPlayerAnimal == 6, "afraid-player animal keeps C# CharacterKind value 6") && ok;
	ok = check(nkFlyingAnimal == 7, "flying animal keeps C# CharacterKind value 7") && ok;

	ok = check(!tileObstacleAllowsWalk(0x41), "0x41 combined transparent tile blocks character walking") && ok;
	ok = check(!tileObstacleAllowsWalk(0x62), "0x62 combined jump-transparent tile blocks character walking") && ok;
	ok = check(!tileObstacleAllowsWalk(0x81), "0x81 combined solid tile blocks character walking") && ok;
	ok = check(tileObstacleAllowsJump(0x62), "0x62 combined jump-transparent tile permits jumping") && ok;
	ok = check(!tileObstacleAllowsJump(0x81), "0x81 combined solid tile blocks jumping") && ok;
	ok = check(!tileObstacleAllowsJump(0x83), "0x83 combined solid tile blocks jumping") && ok;
	ok = check(tileObstacleAllowsMagic(0x42), "0x42 combined transparent tile permits magic") && ok;
	ok = check(tileObstacleAllowsMagic(0x62), "0x62 combined jump-transparent tile permits magic") && ok;
	ok = check(!tileObstacleAllowsMagic(0x82), "0x82 combined solid tile blocks magic") && ok;
	ok = check(tileObstacleAllowsSight(0x01), "low-bit-only tile permits intermediate sight") && ok;
	ok = check(!tileObstacleAllowsSight(0xC0), "hard bit blocks sight even when transparent bit is also set") && ok;

	ok = check(NPC::isObstacleKind(nkBattle, true), "visible fighter blocks walking") && ok;
	ok = check(NPC::isObstacleKind(nkAfraidPlayerAnimal, true), "visible afraid-player animal blocks walking like a ground character") && ok;
	ok = check(!NPC::isObstacleKind(nkFlyingAnimal, true), "visible flyer does not block walking like C# IsObstacle") && ok;
	ok = check(!NPC::isObstacleKind(nkBattle, false), "invisible fighter does not block walking") && ok;

	ok = check(NPC::isInteractiveKindRelation(nkBattle, nrHostile, false, false), "hostile fighter is interactive") && ok;
	ok = check(NPC::isInteractiveKindRelation(nkBattle, nrFriendly, false, false), "friendly fighter is interactive") && ok;
	ok = check(NPC::isInteractiveKindRelation(nkBattle, nrNone, false, false), "none fighter is interactive") && ok;
	ok = check(!NPC::isInteractiveKindRelation(nkBattle, nrNeutral, false, false), "unscripted true neutral fighter is not interactive") && ok;
	ok = check(NPC::isInteractiveKindRelation(nkNormal, nrNeutral, true, false), "scripted normal NPC is interactive") && ok;
	ok = check(!NPC::isInteractiveKindRelation(nkNormal, nrNeutral, false, false), "unscripted normal neutral NPC is not interactive") && ok;
	ok = check(NPC::shouldDrawLifeBarKindRelation(nkBattle, nrHostile, false), "hostile fighter displays a life bar") && ok;
	ok = check(NPC::shouldDrawLifeBarKindRelation(nkBattle, nrFriendly, false), "friendly fighter displays a life bar") && ok;
	ok = check(NPC::shouldDrawLifeBarKindRelation(nkBattle, nrNone, false), "none fighter retains its life bar") && ok;
	ok = check(!NPC::shouldDrawLifeBarKindRelation(nkBattle, nrNeutral, false), "true neutral fighter does not display a life bar") && ok;
	ok = check(!NPC::shouldDrawLifeBarKindRelation(nkPartner, nrFriendly, false), "non-combat partner does not display a life bar") && ok;
	ok = check(NPC::shouldDrawLifeBarKindRelation(nkPartner, nrFriendly, true), "combat-enabled partner displays a life bar") && ok;
	ok = check(NPC::isTalkDistanceReached(3, 3, 0), "NPC talk reaches dialog radius boundary") && ok;
	ok = check(!NPC::isTalkDistanceReached(4, 3, 0), "NPC talk beyond dialog radius needs direct flag") && ok;
	ok = check(NPC::isTalkDistanceReached(20, 1, 1), "CanInteractDirectly NPC talk bypasses distance") && ok;

	ok = check(NPC::canMoveInDirection(0, 1), "one-direction resources allow down direction") && ok;
	ok = check(!NPC::canMoveInDirection(4, 1), "one-direction resources reject opposite direction") && ok;
	ok = check(NPC::canMoveInDirection(0, 2), "two-direction resources allow down direction") && ok;
	ok = check(NPC::canMoveInDirection(4, 2), "two-direction resources allow up direction") && ok;
	ok = check(!NPC::canMoveInDirection(2, 2), "two-direction resources reject side direction") && ok;
	ok = check(NPC::canMoveInDirection(0, 4), "four-direction resources allow down direction") && ok;
	ok = check(NPC::canMoveInDirection(2, 4), "four-direction resources allow left direction") && ok;
	ok = check(NPC::canMoveInDirection(4, 4), "four-direction resources allow up direction") && ok;
	ok = check(NPC::canMoveInDirection(6, 4), "four-direction resources allow right direction") && ok;
	ok = check(!NPC::canMoveInDirection(1, 4), "four-direction resources reject diagonal direction") && ok;
	ok = check(NPC::canMoveInDirection(7, 8), "eight-direction resources allow every direction") && ok;
	ok = check(!NPC::canMoveInDirection(0, 0), "missing direction resources reject movement") && ok;
	ok = check(NPC::canMoveInDirection(-1, 8), "directions are normalized before checking") && ok;

	int normalPathType = NPC::resolvePathTypeForState(nkNormal, pfSingle, false, false);
	ok = check(normalPathType == nptPerfectMaxPlayerTry,
		"normal NPC uses player-grade path type like C#") && ok;
	ok = check(NPC::usePathFinderForPathType(normalPathType),
		"normal NPC uses path finder after resolving player-grade path type") && ok;

	int eventPathType = NPC::resolvePathTypeForState(nkEvent, pfSingle, false, false);
	ok = check(eventPathType == nptPerfectMaxPlayerTry,
		"event NPC uses player-grade path type like C#") && ok;
	ok = check(NPC::usePathFinderForPathType(eventPathType),
		"event NPC uses path finder after resolving player-grade path type") && ok;

	int enemyPathType = NPC::resolvePathTypeForState(nkBattle, pfSingle, false, true);
	ok = check(enemyPathType == nptPathOneStep,
		"hostile single-path NPC resolves to one-step path type") && ok;
	ok = check(!NPC::usePathFinderForPathType(enemyPathType),
		"hostile one-step NPC does not use full path finder") && ok;

	int bestPathType = NPC::resolvePathTypeForState(nkBattle, pfBest, false, true);
	ok = check(bestPathType == nptPerfectMaxNpcTry,
		"PathFinder=1 takes precedence over hostile one-step fallback") && ok;
	ok = check(NPC::usePathFinderForPathType(bestPathType),
		"PathFinder=1 NPC uses full path finder") && ok;

	int fixedPathType = NPC::resolvePathTypeForState(nkBattle, pfSingle, true, false);
	ok = check(fixedPathType == nptPathOneStep,
		"fixed-path NPC resolves to one-step path type") && ok;
	ok = check(!NPC::usePathFinderForPathType(fixedPathType),
		"fixed-path one-step NPC does not use full path finder") && ok;

	int flyerPathType = NPC::resolvePathTypeForState(nkFlyingAnimal, pfSingle, false, false);
	ok = check(flyerPathType == nptPathStraightLine,
		"flying animal resolves to straight-line path type") && ok;
	ok = check(NPC::usePathFinderForPathType(flyerPathType),
		"straight-line path type remains outside one-step fallback") && ok;

	int partnerPathType = NPC::resolvePathTypeForState(nkPartner, pfSingle, false, false);
	ok = check(partnerPathType == nptPerfectMaxNpcTry,
		"partner resolves to NPC-grade path type") && ok;
	ok = check(NPC::usePathFinderForPathType(partnerPathType),
		"partner NPC uses full path finder") && ok;

	int afraidPathType = NPC::resolvePathTypeForState(nkAfraidPlayerAnimal, pfSingle, false, false);
	ok = check(afraidPathType == nptPerfectMaxNpcTry,
		"afraid-player animal resolves to NPC-grade path type") && ok;
	ok = check(NPC::usePathFinderForPathType(afraidPathType),
		"afraid-player animal uses full path finder") && ok;

	ok = check(NPC::getPathSearchMaxTryForPathType(nptSimpleMaxNpcTry) == 100,
		"simple NPC path type keeps C# maxTry=100") && ok;
	ok = check(NPC::getPathSearchMaxTryForPathType(nptPerfectMaxNpcTry) == 100,
		"perfect NPC path type keeps C# maxTry=100") && ok;
	ok = check(NPC::getPathSearchMaxTryForPathType(nptPerfectMaxPlayerTry) == 500,
		"perfect player path type keeps C# maxTry=500") && ok;
	ok = check(NPC::getPathSearchMaxTryForPathType(nptPathOneStep) == 10,
		"one-step path type exposes the same C# maxTry=10 used by movement") && ok;
	ok = check(NPC::getPathSearchMaxTryForPathType(nptPathStraightLine) == 100,
		"straight-line path type keeps C# maxTry=100") && ok;
	ok = check(NPC::getPathSearchMaxTryForPathType(nptPerfectMaxPlayerTry, true) == -1,
		"destination move can temporarily disable player path maxTry") && ok;
	ok = check(NPC::shouldPrioritizeCombatMovement(nkPartner, true, true),
		"partner combat movement takes priority over owner following while combat work is active") && ok;
	ok = check(!NPC::shouldPrioritizeCombatMovement(nkPartner, true, false),
		"partner resumes owner following after combat work ends") && ok;
	ok = check(!NPC::shouldPrioritizeCombatMovement(nkPartner, false, true),
		"disabled partner combat keeps owner-follow movement behavior") && ok;
	ok = check(!NPC::shouldPrioritizeCombatMovement(nkBattle, true, true),
		"non-partner follower behavior is unchanged") && ok;
	ok = check(NPC::shouldAbandonPartnerCombat(nkPartner, true, true, 11),
		"partner yields combat priority when the player is more than ten tiles away") && ok;
	ok = check(!NPC::shouldAbandonPartnerCombat(nkPartner, true, true, 10),
		"partner keeps combat priority at the ten-tile boundary") && ok;
	ok = check(!NPC::shouldAbandonPartnerCombat(nkPartner, false, true, 11),
		"disabled partner combat does not alter scripted partner actions") && ok;
	ok = check(!NPC::shouldAbandonPartnerCombat(nkPartner, true, false, 11),
		"partner blocking mode does not force owner following") && ok;
	ok = check(!NPC::shouldAbandonPartnerCombat(nkBattle, true, true, 11),
		"ordinary battle NPCs do not inherit the partner leash") && ok;
	ok = check(NPC::shouldKeepPartnerOwnerFollowPriority(
		nkPartner, true, true, true, 10, 2),
		"active owner-follow priority remains latched inside the disengage boundary") && ok;
	ok = check(!NPC::shouldKeepPartnerOwnerFollowPriority(
		nkPartner, true, true, true, 2, 2),
		"active owner-follow priority releases at the configured follow radius") && ok;

	NPCActionRes twoDirection = makeActionWithDirections(2);
	NPCActionRes fourDirection = makeActionWithDirections(4);
	NPCActionRes eightDirection = makeActionWithDirections(8);
	NPCActionRes missingDirection;
	ok = check(NPC::getMinimumActionDirectionCount({ &eightDirection, &fourDirection, &missingDirection }) == 4,
		"minimum action direction count skips missing resources and keeps the minimum") && ok;
	ok = check(NPC::getMinimumActionDirectionCount({ &missingDirection }) == 0,
		"minimum action direction count returns zero when every resource is missing") && ok;
	ok = check(NPC::getMinimumActionDirectionCount({ &twoDirection, &fourDirection, &eightDirection }) == 2,
		"minimum action direction count chooses the most restrictive loaded action") && ok;

	ok = check(NPC::selectAttackActionDirectionCount(4, 2) == 2,
		"special attack direction gate prefers magic UseActionFile directions") && ok;
	ok = check(NPC::selectAttackActionDirectionCount(4, -1) == 4,
		"special attack direction gate ignores ActionFile and falls back to attack directions") && ok;
	ok = check(NPC::selectMagicActionDirectionCount(8, 2, 4) == 2,
		"magic direction gate prefers UseActionFile directions") && ok;
	ok = check(NPC::selectMagicActionDirectionCount(8, -1, 4) == 4,
		"magic direction gate falls back to ActionFile directions when UseActionFile is absent") && ok;
	ok = check(NPC::selectMagicActionDirectionCount(8, -1, -1) == 8,
		"magic direction gate falls back to NPC magic action directions when magic action images are absent") && ok;
	int useActionGateDirectionCount = NPC::selectMagicActionDirectionCount(8, 2, 4);
	ok = check(NPC::canMoveInDirection(4, useActionGateDirectionCount),
		"UseActionFile two-direction magic gate allows supported vertical direction") && ok;
	ok = check(!NPC::canMoveInDirection(2, useActionGateDirectionCount),
		"UseActionFile two-direction magic gate rejects unsupported side direction") && ok;
	int actionGateDirectionCount = NPC::selectMagicActionDirectionCount(8, -1, 4);
	ok = check(NPC::canMoveInDirection(2, actionGateDirectionCount),
		"ActionFile four-direction magic gate allows supported side direction") && ok;
	ok = check(!NPC::canMoveInDirection(1, actionGateDirectionCount),
		"ActionFile four-direction magic gate rejects unsupported diagonal direction") && ok;

	ok = check(NPC::isVisibleForRuntimeState(true, 0), "visible NPC is runtime-visible before magic invisibility") && ok;
	ok = check(!NPC::isVisibleForRuntimeState(true, 250), "magic-invisible NPC is not runtime-visible") && ok;
	ok = check(!NPC::isVisibleForRuntimeState(false, 0), "variable-hidden NPC is not runtime-visible") && ok;
	ok = check(NPC::isObstacleKindRuntime(nkBattle, true, 0), "visible battle NPC blocks walking before magic invisibility") && ok;
	ok = check(!NPC::isObstacleKindRuntime(nkBattle, true, 250), "magic-invisible NPC does not block walking") && ok;
	ok = check(!NPC::isObstacleKindRuntime(nkFlyingAnimal, true, 0), "runtime-visible flyer still does not block walking") && ok;

	return ok ? 0 : 1;
}
