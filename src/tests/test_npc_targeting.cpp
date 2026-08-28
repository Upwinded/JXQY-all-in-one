#include "../Game/Data/NPCManager.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/Map.h"
#include "../Game/GameManager/GameManager.h"
#include "../File/File.h"
#include "../Image/IMP.h"
#include "MapV3ContractFixture.h"
#include "TestTemporaryDirectory.h"

#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

bool runGambleMenuRuntimeTests();
bool runMagicDerivedRuntimeTests();
bool runMagicExperienceTests();
bool runEffectRuntimePersistenceTests();
bool runNpcRuntimePersistenceTests();
bool runObjectAnimationRuntimeTests();
bool runMediaRuntimeTests();
bool runCoreLifecycleTests();
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
	return mode == "--core-lifecycle" ||
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

}

int main(int argc, char** argv)
{
#if defined(__MOBILE__)
	if (argc > 1)
	{
		const std::string mode = argv[1];
		if (mode == "--magic-derived" ||
			mode == "--magic-experience" ||
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
	if (argc > 1 && std::string(argv[1]) == "--magic-derived")
	{
		return runMagicDerivedRuntimeTests() ? 0 : 1;
	}
	if (argc > 1 && std::string(argv[1]) == "--magic-experience")
	{
		return runMagicExperienceTests() ? 0 : 1;
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
				"--effect-persistence, --npc-persistence, --object-animation, "
				"--media-runtime, --core-lifecycle, --script-engine-runtime, "
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
	ok = runPartnerCombatOwnerLeashTest() && ok;

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
