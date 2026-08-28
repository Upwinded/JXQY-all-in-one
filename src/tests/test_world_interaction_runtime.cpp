#include "../Game/Data/Effect.h"
#include "../Game/Data/Map.h"
#include "../Game/Data/NPCManager.h"
#include "../Game/Data/ObjectPersistence.h"
#include "../Game/Data/ObjectManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../File/File.h"
#include "../Game/Menu/MsgBox.h"
#include "../Image/IMP.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class WorldInteractionRuntimeTestAccess
{
public:
	static void processQueuedAction(Player& player)
	{
		player.onUpdate();
	}

	static bool resumeStrictInteraction(Player& player)
	{
		return player.resumeStrictQueuedInteraction();
	}

	static void processControlledAction(Player& player)
	{
		player.processControlledNextAction();
	}

	static bool hasControlledAction(const Player& player)
	{
		return player.controlledNextAction != nullptr;
	}
};

namespace
{
constexpr Point PlayerPosition = { 20, 20 };
constexpr int MapWidth = 48;
constexpr int MapHeight = 48;

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

enum class RecordedControlledAction
{
	None,
	Walk,
	Run,
	Jump,
	Attack,
	Magic,
};

class RecordingControlledNPC : public NPC
{
public:
	void resetRecording()
	{
		recordedAction = RecordedControlledAction::None;
		recordedDestination = {};
		recordedTarget.reset();
	}

	void beginWalk(Point destination) override
	{
		recordedAction = RecordedControlledAction::Walk;
		recordedDestination = destination;
	}

	void beginRun(Point destination) override
	{
		recordedAction = RecordedControlledAction::Run;
		recordedDestination = destination;
	}

	void beginJump(Point destination) override
	{
		recordedAction = RecordedControlledAction::Jump;
		recordedDestination = destination;
	}

	void beginAttack(
		Point destination,
		std::shared_ptr<GameElement> target) override
	{
		recordedAction = RecordedControlledAction::Attack;
		recordedDestination = destination;
		recordedTarget = target;
	}

	void beginMagic(
		Point destination,
		std::shared_ptr<GameElement> target) override
	{
		recordedAction = RecordedControlledAction::Magic;
		recordedDestination = destination;
		recordedTarget = target;
	}

	RecordedControlledAction recordedAction = RecordedControlledAction::None;
	Point recordedDestination = {};
	std::weak_ptr<GameElement> recordedTarget;
};

class WorldInteractionFixture
{
public:
	WorldInteractionFixture()
	{
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = MapWidth;
		gameManager.map->data->head.height = MapHeight;
		gameManager.map->data->tile.assign(
			MapHeight, std::vector<MapTile>(MapWidth));
		gameManager.map->dataMap.tile.assign(
			MapHeight, std::vector<DataTile>(MapWidth));
		gameManager.player->setPosition(PlayerPosition);
		gameManager.player->direction = 0;
		gameManager.player->canRun = true;
		gameManager.player->thew = 100;
		gameManager.player->info.thewMax = 100;
		gameManager.player->canFight = true;
		auto movementImage = std::make_shared<IMPImage>();
		movementImage->directions = 8;
		movementImage->interval = 16;
		movementImage->frame.resize(8);
		gameManager.player->res.stand.imagePackage = movementImage;
		gameManager.player->res.walk.imagePackage = movementImage;
		gameManager.player->res.run.imagePackage = movementImage;
	}

	std::shared_ptr<Object> addObject(
		Point position,
		const std::string& primaryScript = "object_primary.lua",
		const std::string& alternateScript = "")
	{
		auto object = std::make_shared<Object>();
		object->scriptFile = primaryScript;
		object->scriptFileRight = alternateScript;
		object->setPosition(position);
		gameManager.objectManager->objectList.push_back(object);
		return object;
	}

	std::shared_ptr<NPC> addNPC(
		Point position,
		int kind = nkNormal,
		int relation = nrFriendly,
		const std::string& primaryScript = "npc_primary.lua",
		const std::string& alternateScript = "")
	{
		auto npc = std::make_shared<NPC>();
		npc->kind = kind;
		npc->relation = relation;
		npc->scriptFile = primaryScript;
		npc->scriptFileRight = alternateScript;
		npc->setPosition(position);
		gameManager.npcManager->npcList.push_back(npc);
		return npc;
	}

	std::shared_ptr<NPC> addMovableNPC(
		Point position,
		int kind = nkBattle,
		int relation = nrFriendly)
	{
		auto npc = addNPC(position, kind, relation, "", "");
		auto movementImage = std::make_shared<IMPImage>();
		movementImage->directions = 8;
		movementImage->interval = 16;
		movementImage->frame.resize(8);
		npc->res.stand.imagePackage = movementImage;
		npc->res.walk.imagePackage = movementImage;
		return npc;
	}

	void resetCandidates()
	{
		gameManager.player->cancelQueuedInteraction(false);
		gameManager.objectManager->freeResource();
		gameManager.npcManager->freeResource();
		gameManager.map->dataMap.tile.assign(
			MapHeight, std::vector<DataTile>(MapWidth));
		gameManager.map->addNPCToDataMap(
			gameManager.player->getPosition(), gameManager.player);
	}

	GameManager gameManager;
};

void blockMovementAround(Map& map, Point position)
{
	for (int direction = 0; direction < 8; ++direction)
	{
		Point blockedPosition = map.getSubPoint(position, direction);
		map.data->tile[blockedPosition.y][blockedPosition.x].obstacle = toObstacle;
	}
}

bool isQueuedFor(
	const Player& player,
	const std::shared_ptr<GameElement>& target,
	NextDest destinationKind,
	NPCActionType action,
	bool useRightScript)
{
	return player.nextAction != nullptr
		&& player.nextAction->destGE.lock() == target
		&& player.nextAction->destKind == destinationKind
		&& player.nextAction->action == action
		&& player.nextAction->useRightScript == useRightScript
		&& player.nextAction->strictWorldInteraction;
}

bool isStrictPendingFor(
	const Player& player,
	const std::shared_ptr<GameElement>& target,
	NextDest destinationKind,
	bool useRightScript,
	bool requestedRunning)
{
	return player.nextAction == nullptr
		&& player.nextDest == destinationKind
		&& player.destGE.lock() == target
		&& player.nextDestUseRightScript == useRightScript
		&& player.nextDestStrictWorldInteraction
		&& player.nextDestRequestedRunning == requestedRunning;
}

bool isStrictQueueCleared(const Player& player)
{
	return player.nextAction == nullptr
		&& player.nextDest == ndNone
		&& player.destGE.expired()
		&& !player.nextDestUseRightScript
		&& !player.nextDestStrictWorldInteraction
		&& !player.nextDestRequestedRunning;
}

bool runObjectIntentMatrixTests()
{
	WorldInteractionFixture fixture;
	bool ok = true;
	auto primaryOnly = fixture.addObject({ 22, 20 }, "primary.lua", "");
	ok = check(WorldInteractionResolver::isObjectValidForIntent(
		primaryOnly, WorldInteractionIntent::Primary),
		"primary-only object is a Primary interaction candidate") && ok;
	ok = check(!WorldInteractionResolver::isObjectValidForIntent(
		primaryOnly, WorldInteractionIntent::Alternate),
		"primary-only object is not an Alternate interaction candidate") && ok;
	ok = check(!WorldInteractionResolver::isObjectValidForIntent(
		primaryOnly, WorldInteractionIntent::Attack),
		"objects are not entity targets for the explicit Attack resolver") && ok;

	auto alternateOnly = fixture.addObject({ 23, 20 }, "", "alternate.lua");
	ok = check(WorldInteractionResolver::isObjectValidForIntent(
		alternateOnly, WorldInteractionIntent::Primary)
		&& WorldInteractionResolver::isObjectValidForIntent(
			alternateOnly, WorldInteractionIntent::Alternate),
		"alternate-only object supports Primary fallback and explicit Alternate") && ok;

	auto bothScripts = fixture.addObject(
		{ 24, 20 }, "primary.lua", "alternate.lua");
	ok = check(WorldInteractionResolver::isObjectValidForIntent(
		bothScripts, WorldInteractionIntent::Primary)
		&& WorldInteractionResolver::isObjectValidForIntent(
			bothScripts, WorldInteractionIntent::Alternate),
		"dual-script object supports both script intents") && ok;

	auto touchOnly = fixture.addObject({ 21, 20 }, "touch.lua", "alternate.lua");
	touchOnly->scriptFileJustTouch = 1;
	ok = check(!WorldInteractionResolver::isObjectValidForIntent(
		touchOnly, WorldInteractionIntent::Primary)
		&& !WorldInteractionResolver::isObjectValidForIntent(
			touchOnly, WorldInteractionIntent::Alternate),
		"ScriptFileJustTouch object is excluded from manual interaction") && ok;
	return ok;
}

bool runNPCIntentMatrixTests()
{
	WorldInteractionFixture fixture;
	bool ok = true;
	auto actor = fixture.gameManager.player->getActionActor();

	auto scriptedFriendly = fixture.addNPC(
		{ 22, 20 }, nkNormal, nrFriendly, "primary.lua", "alternate.lua");
	ok = check(WorldInteractionResolver::isNPCValidForIntent(
		scriptedFriendly, WorldInteractionIntent::Primary, actor)
		&& WorldInteractionResolver::isNPCValidForIntent(
			scriptedFriendly, WorldInteractionIntent::Alternate, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			scriptedFriendly, WorldInteractionIntent::Attack, actor),
		"scripted friendly NPC supports both talks but is not attack-selected") && ok;

	auto alternateOnly = fixture.addNPC(
		{ 23, 20 }, nkNormal, nrFriendly, "", "alternate.lua");
	ok = check(WorldInteractionResolver::isNPCValidForIntent(
		alternateOnly, WorldInteractionIntent::Primary, actor)
		&& WorldInteractionResolver::isNPCValidForIntent(
			alternateOnly, WorldInteractionIntent::Alternate, actor),
		"alternate-only NPC supports Primary fallback and explicit Alternate") && ok;

	auto hostileScripted = fixture.addNPC(
		{ 24, 20 }, nkBattle, nrHostile, "primary.lua", "alternate.lua");
	ok = check(WorldInteractionResolver::isNPCValidForIntent(
		hostileScripted, WorldInteractionIntent::Primary, actor)
		&& WorldInteractionResolver::isNPCValidForIntent(
			hostileScripted, WorldInteractionIntent::Alternate, actor)
		&& WorldInteractionResolver::isNPCValidForIntent(
			hostileScripted, WorldInteractionIntent::Attack, actor),
		"hostile scripted NPC supports explicit talk and attack intents") && ok;

	auto hostileUnscripted = fixture.addNPC(
		{ 25, 20 }, nkBattle, nrHostile, "", "");
	ok = check(!WorldInteractionResolver::isNPCValidForIntent(
		hostileUnscripted, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			hostileUnscripted, WorldInteractionIntent::Alternate, actor)
		&& WorldInteractionResolver::isNPCValidForIntent(
			hostileUnscripted, WorldInteractionIntent::Attack, actor),
		"hostile unscripted NPC is attack-only") && ok;

	auto friendlyFighter = fixture.addNPC(
		{ 26, 20 }, nkBattle, nrFriendly, "", "");
	ok = check(!WorldInteractionResolver::isNPCValidForIntent(
		friendlyFighter, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			friendlyFighter, WorldInteractionIntent::Alternate, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			friendlyFighter, WorldInteractionIntent::Attack, actor),
		"friendly unscripted fighter is excluded from talk and attack") && ok;

	auto neutralFighter = fixture.addNPC(
		{ 26, 22 }, nkBattle, nrNeutral, "", "");
	ok = check(!WorldInteractionResolver::isNPCValidForIntent(
		neutralFighter, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			neutralFighter, WorldInteractionIntent::Alternate, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			neutralFighter, WorldInteractionIntent::Attack, actor),
		"true neutral fighter is excluded from talk and attack") && ok;
	const auto fastSelectionCandidates =
		fixture.gameManager.npcManager->findRadiusFastSelectionNPC(
			PlayerPosition, 4);
	ok = check(std::find(
		fastSelectionCandidates.begin(),
		fastSelectionCandidates.end(),
		neutralFighter) == fastSelectionCandidates.end(),
		"true neutral fighter is excluded from fast selection") && ok;

	auto hiddenByAction = fixture.addNPC(
		{ 27, 20 }, nkBattle, nrHostile, "primary.lua", "alternate.lua");
	hiddenByAction->nowAction = acHide;
	ok = check(!WorldInteractionResolver::isNPCValidForIntent(
		hiddenByAction, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			hiddenByAction, WorldInteractionIntent::Alternate, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			hiddenByAction, WorldInteractionIntent::Attack, actor),
		"action-hidden NPC is excluded from every interaction intent") && ok;

	auto variableHidden = fixture.addNPC(
		{ 28, 20 }, nkBattle, nrHostile, "primary.lua", "alternate.lua");
	variableHidden->isVisibleByVariable = false;
	ok = check(!WorldInteractionResolver::isNPCValidForIntent(
		variableHidden, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			variableHidden, WorldInteractionIntent::Attack, actor),
		"variable-hidden NPC is excluded from talk and attack") && ok;

	auto invisible = fixture.addNPC(
		{ 29, 20 }, nkBattle, nrHostile, "primary.lua", "alternate.lua");
	invisible->invisibleMilliseconds = 100;
	ok = check(!WorldInteractionResolver::isNPCValidForIntent(
		invisible, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			invisible, WorldInteractionIntent::Attack, actor),
		"magic-invisible NPC is excluded from talk and attack") && ok;

	auto transporting = fixture.addNPC(
		{ 30, 20 }, nkBattle, nrHostile, "primary.lua", "alternate.lua");
	auto transportEffect = std::make_shared<Effect>();
	transportEffect->magic.level[transportEffect->level].moveKind = mmkTransport;
	transporting->applyTransportEffect(transportEffect);
	ok = check(transporting->isTransporting()
		&& !WorldInteractionResolver::isNPCValidForIntent(
			transporting, WorldInteractionIntent::Primary, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			transporting, WorldInteractionIntent::Alternate, actor)
		&& !WorldInteractionResolver::isNPCValidForIntent(
			transporting, WorldInteractionIntent::Attack, actor),
		"transporting NPC is excluded from every interaction intent") && ok;
	return ok;
}

bool runExplicitQueueTests()
{
	WorldInteractionFixture fixture;
	bool ok = true;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;

	auto alternateOnlyObject = fixture.addObject(
		{ 25, 20 }, "", "object_alternate.lua");
	ok = check(gameManager.queueObjectScriptInteraction(
		alternateOnlyObject, WorldInteractionScriptSide::Primary, true)
		&& isQueuedFor(player, alternateOnlyObject, ndObj, acRun, true)
		&& player.nextAction->dest == alternateOnlyObject->position,
		"object Primary fallback queues strict right-script running metadata") && ok;

	player.cancelQueuedInteraction(false);
	auto dualScriptObject = fixture.addObject(
		{ 26, 20 }, "object_primary.lua", "object_alternate.lua");
	ok = check(gameManager.queueObjectScriptInteraction(
		dualScriptObject, WorldInteractionScriptSide::Primary, false)
		&& isQueuedFor(player, dualScriptObject, ndObj, acWalk, false),
		"dual-script object Primary queues left-script walk metadata") && ok;
	player.cancelQueuedInteraction(false);
	ok = check(gameManager.queueObjectScriptInteraction(
		dualScriptObject, WorldInteractionScriptSide::Alternate, false)
		&& isQueuedFor(player, dualScriptObject, ndObj, acWalk, true),
		"dual-script object Alternate queues right-script metadata") && ok;

	player.cancelQueuedInteraction(false);
	auto primaryOnlyObject = fixture.addObject({ 27, 20 }, "object_primary.lua", "");
	ok = check(!gameManager.queueObjectScriptInteraction(
		primaryOnlyObject, WorldInteractionScriptSide::Alternate, false)
		&& player.nextAction == nullptr,
		"object Alternate queue rejects a missing right script") && ok;

	auto alternateOnlyNPC = fixture.addNPC(
		{ 25, 22 }, nkNormal, nrFriendly, "", "npc_alternate.lua");
	ok = check(gameManager.queueNPCTalkInteraction(
		alternateOnlyNPC, WorldInteractionScriptSide::Primary, false)
		&& isQueuedFor(player, alternateOnlyNPC, ndTalk, acWalk, true),
		"NPC Primary fallback queues strict right-script talk metadata") && ok;

	player.cancelQueuedInteraction(false);
	auto hostileNPC = fixture.addNPC(
		{ 26, 22 }, nkBattle, nrHostile, "npc_primary.lua", "npc_alternate.lua");
	ok = check(gameManager.queueNPCTalkInteraction(
		hostileNPC, WorldInteractionScriptSide::Alternate, true)
		&& isQueuedFor(player, hostileNPC, ndTalk, acRun, true),
		"NPC Alternate stays a strict talk action for a hostile NPC") && ok;

	player.cancelQueuedInteraction(false);
	ok = check(gameManager.queueNPCAttackInteraction(hostileNPC, true)
		&& isQueuedFor(player, hostileNPC, ndAttack, acRun, false),
		"explicit NPC attack queues strict run metadata without a script side") && ok;

	player.cancelQueuedInteraction(false);
	player.thew = 0;
	ok = check(gameManager.queueNPCAttackInteraction(hostileNPC, true)
		&& isQueuedFor(player, hostileNPC, ndAttack, acWalk, false),
		"running request downgrades to walk when stamina is insufficient") && ok;
	return ok;
}

bool runCandidateSortingTests()
{
	WorldInteractionFixture fixture;
	bool ok = true;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;

	fixture.addObject({ 21, 20 });
	auto preferredFarObject = fixture.addObject({ 28, 20 });
	auto candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Primary, 13, 2, preferredFarObject);
	ok = check(!candidates.empty() && candidates.front().object == preferredFarObject
		&& candidates.front().preferred,
		"legal preferred controller target sorts before nearer candidates") && ok;

	fixture.resetCandidates();
	fixture.addObject({ 22, 20 });
	auto directlyExecutable = fixture.addObject({ 28, 20 });
	directlyExecutable->canInteractDirectly = 1;
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(!candidates.empty() && candidates.front().object == directlyExecutable
		&& candidates.front().executionDistanceReached,
		"execution-ready candidate sorts before a nearer candidate") && ok;

	fixture.resetCandidates();
	auto nearOffFacing = fixture.addObject({ 20, 24 });
	auto fartherFacing = fixture.addObject({ 24, 20 });
	player.direction = NPC::getDirection(PlayerPosition, fartherFacing->position);
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(!candidates.empty() && candidates.front().object == nearOffFacing
		&& candidates.front().inNearRange,
		"near-range candidate sorts before a farther facing candidate") && ok;

	fixture.resetCandidates();
	auto alignedFarObject = fixture.addObject({ 25, 20 });
	fixture.addObject({ 20, 26 });
	player.direction = NPC::getDirection(PlayerPosition, alignedFarObject->position);
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(candidates.size() == 2 && candidates.front().object == alignedFarObject
		&& candidates[0].facingDifference < candidates[1].facingDifference,
		"facing difference sorts before distance outside near and execution ranges") && ok;

	fixture.resetCandidates();
	auto closerAlignedObject = fixture.addObject({ 23, 20 });
	fixture.addObject({ 25, 20 });
	player.direction = NPC::getDirection(PlayerPosition, closerAlignedObject->position);
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(candidates.size() == 2 && candidates.front().object == closerAlignedObject
		&& candidates[0].distance < candidates[1].distance,
		"distance breaks ties after preferred, execution, near-range and facing") && ok;

	fixture.resetCandidates();
	auto tiedObject = fixture.addObject({ 24, 20 });
	auto tiedNPC = fixture.addNPC({ 24, 20 });
	player.direction = NPC::getDirection(PlayerPosition, tiedObject->position);
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(candidates.size() == 2
		&& candidates[0].targetType == WorldInteractionTargetType::Object
		&& candidates[0].object == tiedObject && candidates[1].npc == tiedNPC,
		"Object sorts before NPC when every higher-priority key ties") && ok;

	fixture.resetCandidates();
	auto stableFirst = fixture.addObject({ 24, 20 });
	auto stableMiddle = fixture.addObject({ 24, 20 });
	auto stableLast = fixture.addObject({ 24, 20 });
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(candidates.size() == 3 && candidates[0].object == stableFirst
		&& candidates[1].object == stableMiddle && candidates[2].object == stableLast,
		"manager order is stable when all semantic keys tie") && ok;
	gameManager.objectManager->deleteObject(stableMiddle);
	candidates = gameManager.findWorldInteractionCandidates(WorldInteractionIntent::Primary);
	ok = check(candidates.size() == 2 && candidates[0].object == stableFirst
		&& candidates[1].object == stableLast,
		"deletion preserves remaining manager relative order") && ok;

	fixture.resetCandidates();
	player.attackRadius = 3;
	auto alignedOutsideAttackRadius = fixture.addNPC(
		{ 24, 20 }, nkBattle, nrHostile, "", "");
	auto executionReadyOffFacing = fixture.addNPC(
		{ 20, 26 }, nkBattle, nrHostile, "", "");
	player.direction = NPC::getDirection(
		PlayerPosition, alignedOutsideAttackRadius->getPosition());
	candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Attack, 13, 2);
	ok = check(candidates.size() == 2
		&& candidates[0].npc == executionReadyOffFacing
		&& candidates[0].executionDistanceReached
		&& candidates[1].npc == alignedOutsideAttackRadius
		&& !candidates[1].executionDistanceReached
		&& candidates[0].facingDifference > candidates[1].facingDifference,
		"Attack radius readiness sorts before a better-facing target") && ok;
	candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Attack, 13, 2, alignedOutsideAttackRadius);
	ok = check(candidates.size() == 2
		&& candidates[0].npc == alignedOutsideAttackRadius
		&& candidates[0].preferred
		&& !candidates[0].executionDistanceReached,
		"legal preferred Attack target sorts before an execution-ready target") && ok;

	fixture.resetCandidates();
	auto stableFirstAttackTarget = fixture.addNPC(
		{ 24, 20 }, nkBattle, nrHostile, "", "");
	auto stableMiddleAttackTarget = fixture.addNPC(
		{ 24, 20 }, nkBattle, nrHostile, "", "");
	auto stableLastAttackTarget = fixture.addNPC(
		{ 24, 20 }, nkBattle, nrHostile, "", "");
	candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Attack, 13, 2);
	ok = check(candidates.size() == 3
		&& candidates[0].npc == stableFirstAttackTarget
		&& candidates[1].npc == stableMiddleAttackTarget
		&& candidates[2].npc == stableLastAttackTarget,
		"Attack ties preserve NPC manager order") && ok;

	fixture.resetCandidates();
	auto outsideRadius = fixture.addObject({ 34, 20 });
	outsideRadius->canInteractDirectly = 1;
	candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Primary, 13, 2);
	ok = check(candidates.empty(),
		"CanInteractDirectly does not bypass the thirteen-tile search cap") && ok;
	return ok;
}

bool loadExactJxqy2Scene(
	GameManager& gameManager,
	const std::string& mapPath,
	const std::string& objectPath)
{
	std::unique_ptr<char[]> mapBytes;
	int mapLength = 0;
	if (!File::readActiveResourceFile(
			mapPath, mapBytes, mapLength, MapSafety::MaximumFileBytes)
		|| !gameManager.map->load(mapBytes, mapLength))
	{
		return false;
	}
	gameManager.map->createDataMap();

	std::unique_ptr<char[]> objectBytes;
	int objectLength = 0;
	if (!File::readActiveResourceFile(
			objectPath,
			objectBytes,
			objectLength,
			ObjectPersistence::MaximumObjectFileBytes))
	{
		return false;
	}
	const auto* begin = reinterpret_cast<const std::uint8_t*>(
		objectBytes.get());
	return gameManager.objectManager->loadExactResourceBytes(
		objectPath,
		std::vector<std::uint8_t>(begin, begin + objectLength));
}

bool runInteractionOcclusionBoundaryTests()
{
	WorldInteractionFixture fixture;
	auto& gameManager = fixture.gameManager;
	const Point targetPosition = { 24, 20 };
	const Point intermediatePosition = { 22, 20 };
	auto object = fixture.addObject(targetPosition);
	auto npc = fixture.addNPC(targetPosition);

	gameManager.map->data->
		tile[targetPosition.y][targetPosition.x].obstacle = 0x80;
	auto objects = gameManager.objectManager->
		findRadiusScriptViewObj(PlayerPosition, 4);
	auto npcs = gameManager.npcManager->
		findRadiusScriptViewNPC(PlayerPosition, 4);
	bool ok = check(
		std::find(objects.begin(), objects.end(), object) != objects.end()
			&& std::find(npcs.begin(), npcs.end(), npc) != npcs.end(),
		"an interactable NPC or Object is not hidden by its own target tile");

	gameManager.map->data->
		tile[targetPosition.y][targetPosition.x].obstacle = 0;
	gameManager.map->data->
		tile[intermediatePosition.y][intermediatePosition.x].obstacle = 0x80;
	objects = gameManager.objectManager->
		findRadiusScriptViewObj(PlayerPosition, 4);
	npcs = gameManager.npcManager->
		findRadiusScriptViewNPC(PlayerPosition, 4);
	ok = check(
		std::find(objects.begin(), objects.end(), object) == objects.end()
			&& std::find(npcs.begin(), npcs.end(), npc) == npcs.end(),
		"opaque terrain between the actor and an interactable still blocks quick selection")
		&& ok;

	blockMovementAround(*gameManager.map, targetPosition);
	const auto fastSelectionNPCs = gameManager.npcManager->
		findRadiusFastSelectionNPC(PlayerPosition, 4);
	ok = check(
		std::find(
			fastSelectionNPCs.begin(), fastSelectionNPCs.end(), npc) ==
			fastSelectionNPCs.end(),
		"an enclosed scripted NPC remains outside reachable fast selection")
		&& ok;
	return ok;
}

bool runJxqy2LowBitChestInteractionTests()
{
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path jxqy2Root =
		repositoryRoot / "assets" / "jxqy2";
	const std::string previousActiveRoot = File::getActiveResourceRoot();
	File::setActiveResourceRoot(jxqy2Root.generic_string());

	GameManager gameManager;
	bool ok = check(loadExactJxqy2Scene(
			gameManager,
			u8"map/沙漠之战.map",
			u8"ini/save/smzz.obj"),
		"JXQY2 desert-battle map and object list load from the active pack");
	if (ok)
	{
		const Point jumpStart = { 15, 67 };
		const Point jumpDestination = { 18, 78 };
		gameManager.mapFolderName = u8"沙漠之战";
		gameManager.traps.set(
			gameManager.mapFolderName,
			2,
			u8"找药物.txt");
		const Point activeTrapJumpDestination =
			gameManager.map->getJumpPath(jumpStart, jumpDestination);
		ok = check(
			activeTrapJumpDestination != jumpDestination
				&& gameManager.map->getTrapIndex(
					activeTrapJumpDestination) == 2,
			"an active JXQY2 opening trap still stops a jump")
			&& ok;
		gameManager.traps.markTriggered(2);
		const Point resolvedJumpDestination =
			gameManager.map->getJumpPath(jumpStart, jumpDestination);
		if (resolvedJumpDestination != jumpDestination)
		{
			std::cerr << "JXQY2 opening jump resolved to ("
				<< resolvedJumpDestination.x << ','
				<< resolvedJumpDestination.y << ")\n";
		}
		ok = check(
			resolvedJumpDestination == jumpDestination,
			"the triggered JXQY2 opening trap no longer truncates the scripted jump")
			&& ok;
		gameManager.player->initRes(u8"z-南宫飞云.ini");
		gameManager.player->canJump = true;
		gameManager.inEvent = true;
		gameManager.player->forceBeginStand();
		gameManager.player->setPosition(jumpStart, false);
		gameManager.player->beginJump(jumpDestination);
		ok = check(
			gameManager.player->isJumping()
				&& gameManager.player->stepList.size() == 1
				&& gameManager.player->stepList.front() == jumpDestination,
			"JXQY2 opening story jump starts with the real player action resources")
			&& ok;
		gameManager.player->eventRun();
		ok = check(
			gameManager.player->getPosition() == jumpDestination
				&& gameManager.player->isStanding(),
			"JXQY2 opening story jump completes at the scripted destination")
			&& ok;

		const Point chestPosition = { 20, 82 };
		const Point approachPosition = { 20, 80 };
		const auto chest = gameManager.objectManager->findObj(u8"宝箱");
		const std::uint8_t obstacle =
			gameManager.map->data->tile[chestPosition.y][chestPosition.x].
				obstacle;
		const auto candidates =
			gameManager.objectManager->findRadiusScriptViewObj(
				approachPosition, 2);
		ok = check(chest != nullptr
				&& chest->getPosition() == chestPosition
				&& chest->scriptFile == u8"2级物品.txt"
				&& obstacle == 0x01
				&& tileObstacleAllowsSight(obstacle)
				&& !tileObstacleAllowsMagic(obstacle)
				&& gameManager.map->canSee(
					approachPosition, chestPosition)
				&& std::find(
					candidates.begin(), candidates.end(), chest)
					!= candidates.end(),
			"JXQY2 desert chest on low-bit tile remains a mobile fast-interaction candidate while magic stays blocked") && ok;
	}

	ok = check(loadExactJxqy2Scene(
			gameManager,
			u8"map/主角家.map",
			u8"ini/save/home.obj"),
		"JXQY2 home map and object list load from the active pack") && ok;
	if (ok)
	{
		const Point chestPosition = { 28, 31 };
		const Point approachPosition = { 27, 31 };
		const auto candidates =
			gameManager.objectManager->findRadiusScriptViewObj(
				approachPosition, 2);
		const auto chest = std::find_if(
			gameManager.objectManager->objectList.begin(),
			gameManager.objectManager->objectList.end(),
			[chestPosition](const std::shared_ptr<Object>& object)
			{
				return object != nullptr
					&& object->getPosition() == chestPosition;
			});
		ok = check(chest != gameManager.objectManager->objectList.end()
				&& gameManager.map->data->
					tile[chestPosition.y][chestPosition.x].obstacle == 0x00
				&& std::find(
					candidates.begin(), candidates.end(), *chest)
					!= candidates.end(),
			"JXQY2 home chest remains the zero-obstacle comparison candidate") && ok;
	}

	File::setActiveResourceRoot(previousActiveRoot);
	return ok;
}

bool runYYCSWudangGuestApproachTests()
{
	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path yycsRoot = repositoryRoot / "assets" / "yycs";
	const std::string previousActiveRoot = File::getActiveResourceRoot();
	File::setActiveResourceRoot(yycsRoot.generic_string());

	GameManager gameManager;
	std::unique_ptr<char[]> mapBytes;
	int mapLength = 0;
	bool ok = check(
		File::readActiveResourceFile(
			u8"map/map_003_武当山下.map",
			mapBytes,
			mapLength,
			MapSafety::MaximumFileBytes)
			&& gameManager.map->load(mapBytes, mapLength),
		"YYCS Wudang foothill map loads for the guest approach regression");
	if (ok)
	{
		gameManager.map->createDataMap();
		ok = check(
			gameManager.npcManager->load(
				u8"wudangshanxia.npc", true),
			"YYCS Wudang foothill NPC list loads for the guest approach regression")
			&& ok;
	}

	if (ok)
	{
		const auto owners = gameManager.npcManager->findNPC(
			u8"武当山下酒肆老板");
		const auto guests = gameManager.npcManager->findNPC(
			u8"武当山下酒客");
		const std::shared_ptr<NPC> owner =
			owners.empty() ? nullptr : owners.front();
		const std::shared_ptr<NPC> guest =
			guests.empty() ? nullptr : guests.front();
		const Point playerPosition = { 8, 93 };
		gameManager.player->setPosition(playerPosition, true);
		auto movementImage = std::make_shared<IMPImage>();
		movementImage->directions = 8;
		movementImage->interval = 16;
		movementImage->frame.resize(8);
		gameManager.player->res.stand.imagePackage = movementImage;
		gameManager.player->res.walk.imagePackage = movementImage;
		gameManager.player->res.run.imagePackage = movementImage;

		ok = check(owner != nullptr && guest != nullptr,
			"YYCS Wudang foothill owner and guest exist in the production NPC list")
			&& ok;
		if (owner != nullptr && guest != nullptr)
		{
			const auto lineOfSightCandidates =
				gameManager.npcManager->findRadiusScriptViewNPC(
					playerPosition,
					GameController::FastInteractionTileDistance);
			const auto oldRadiusFastSelectionCandidates =
				gameManager.npcManager->findRadiusFastSelectionNPC(
					playerPosition,
					GameController::FastInteractionTileDistance - 1);
			const auto fastSelectionCandidates =
				gameManager.npcManager->findRadiusFastSelectionNPC(
					playerPosition,
					GameController::FastInteractionTileDistance);
			ok = check(
				Map::calDistance(playerPosition, guest->getPosition()) ==
					GameController::FastInteractionTileDistance &&
				std::find(
					lineOfSightCandidates.begin(),
					lineOfSightCandidates.end(),
					guest) == lineOfSightCandidates.end() &&
				std::find(
					oldRadiusFastSelectionCandidates.begin(),
					oldRadiusFastSelectionCandidates.end(),
					guest) == oldRadiusFastSelectionCandidates.end() &&
				std::find(
					fastSelectionCandidates.begin(),
					fastSelectionCandidates.end(),
					guest) != fastSelectionCandidates.end(),
				"YYCS Wudang guest enters reachable fast selection at three tiles")
				&& ok;
			const auto ownerPath = gameManager.map->findPath(
				playerPosition, owner->getPosition(), 8);
			const auto guestExactPath = gameManager.map->findPath(
				playerPosition, guest->getPosition(), 8);
			const auto guestApproachPath = gameManager.map->getRadiusPath(
				playerPosition,
				guest->getPosition(),
				guest->dialogRadius,
				8);
			ok = check(!ownerPath.empty(),
				"YYCS player can path toward the tavern owner") && ok;
			ok = check(guestExactPath.empty(),
				"YYCS Wudang guest reproduces the blocked exact-target path") && ok;
			ok = check(!guestApproachPath.empty()
					&& Map::calDistance(
						guestApproachPath.back(), guest->getPosition())
						<= guest->dialogRadius,
				"YYCS player can path to a reachable tile inside the guest dialog radius")
				&& ok;

			gameManager.player->nextDest = ndTalk;
			gameManager.player->destGE = guest;
			gameManager.player->beginWalk(guest->getPosition());
			ok = check(gameManager.player->isWalking()
					&& !gameManager.player->stepList.empty()
					&& Map::calDistance(
						gameManager.player->stepList.back(), guest->getPosition())
						<= guest->dialogRadius,
				"YYCS click-to-talk starts movement toward the guest dialog radius")
				&& ok;
		}
	}

	if (ok)
	{
		ok = check(
			gameManager.npcManager->load(
				u8"subevent01.npc", false),
			"YYCS Wudang foothill side-event NPC list merges into the production scene")
			&& ok;
		const auto merchants = gameManager.npcManager->findNPC(
			u8"张仲天");
		const std::shared_ptr<NPC> merchant =
			merchants.empty() ? nullptr : merchants.front();
		ok = check(
			merchant != nullptr
				&& merchant->getPosition() == Point{ 9, 35 }
				&& merchant->isVisibleForRuntime()
				&& !merchant->isHiding()
				&& merchant->res.stand.imagePackage != nullptr,
			"YYCS Wudang foothill side event keeps Zhang Zhongtian visible with a stand image")
			&& ok;

		const std::filesystem::path temporaryStateRoot =
			std::filesystem::temp_directory_path() /
			"jxqy-world-interaction-wudang-roundtrip";
		std::error_code fileError;
		std::filesystem::remove_all(temporaryStateRoot, fileError);
		fileError.clear();
		std::filesystem::create_directories(temporaryStateRoot, fileError);
		File::setPlatformStateParentForTests(
			temporaryStateRoot.generic_string());
		const bool saved = !fileError
			&& gameManager.npcManager->save(
				u8"temp_subevent01.npc");
		const bool loaded = saved
			&& gameManager.npcManager->load(
				u8"temp_subevent01.npc", true);
		const auto reloadedMerchants = gameManager.npcManager->findNPC(
			u8"张仲天");
		const std::shared_ptr<NPC> reloadedMerchant =
			reloadedMerchants.empty() ? nullptr : reloadedMerchants.front();
		ok = check(
			loaded
				&& reloadedMerchant != nullptr
				&& reloadedMerchant->getPosition() == Point{ 9, 35 }
				&& reloadedMerchant->isVisibleForRuntime()
				&& !reloadedMerchant->isHiding()
				&& reloadedMerchant->res.stand.imagePackage != nullptr,
			"YYCS Wudang foothill temporary NPC save and reload keeps Zhang Zhongtian visible")
			&& ok;
		File::setPlatformStateParentForTests("");
		fileError.clear();
		std::filesystem::remove_all(temporaryStateRoot, fileError);
	}

	File::setActiveResourceRoot(previousActiveRoot);
	return ok;
}

bool runControlledActorCandidateTests()
{
	WorldInteractionFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto controlledActor = fixture.addNPC(
		{ 30, 30 }, nkBattle, nrFriendly, "", "");
	controlledActor->direction = 6;
	auto controlEffect = std::make_shared<Effect>();
	gameManager.player->beginControlCharacter(controlledActor, controlEffect);

	fixture.addObject({ 21, 20 });
	auto controlledActorTarget = fixture.addObject({ 31, 30 });
	auto candidates = gameManager.findWorldInteractionCandidates(
		WorldInteractionIntent::Primary, 3, 2);
	bool ok = check(gameManager.player->getActionActor() == controlledActor
		&& candidates.size() == 1
		&& candidates.front().object == controlledActorTarget
		&& candidates.front().distance == 1
		&& candidates.front().facingDifference == 0,
		"candidate origin and facing follow the controlled action actor");

	ok = check(gameManager.queueBestWorldInteraction(
		WorldInteractionIntent::Primary, false, 3, 2)
		&& gameManager.player->nextAction != nullptr
		&& gameManager.player->nextAction->destGE.lock() == controlledActorTarget
		&& gameManager.player->nextAction->strictWorldInteraction,
		"controlled actor queues the candidate selected around its own position") && ok;
	gameManager.player->endControlCharacter();
	return ok;
}

bool runControlledCameraLifecycleTests()
{
	WorldInteractionFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;

	gameManager.camera->followPlayer = false;
	gameManager.camera->followNPC.reset();
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	bool ok = check(!gameManager.camera->followPlayer
		&& gameManager.camera->followNPC.expired(),
		"idle controlled-action updates preserve script-controlled camera mode");

	auto controlledActor = fixture.addNPC(
		{ 30, 30 }, nkBattle, nrFriendly, "", "");
	auto controlEffect = std::make_shared<Effect>();
	player.beginControlCharacter(controlledActor, controlEffect);
	player.endControlCharacter();
	ok = check(gameManager.camera->followPlayer
		&& gameManager.camera->followNPC.expired(),
		"ending an active control session restores player camera follow") && ok;
	return ok;
}

bool runControlledActorExecutionTests()
{
	WorldInteractionFixture fixture;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;
	auto controlledActor = std::make_shared<RecordingControlledNPC>();
	controlledActor->kind = nkBattle;
	controlledActor->relation = nrFriendly;
	controlledActor->setPosition({ 30, 30 });
	gameManager.npcManager->npcList.push_back(controlledActor);
	auto controlEffect = std::make_shared<Effect>();
	player.beginControlCharacter(controlledActor, controlEffect);
	gameManager.menu->messageBox = std::make_shared<MsgBox>();

	auto submitAndDispatch = [&](NextAction action)
	{
		controlledActor->resetRecording();
		const bool accepted = player.addNextAction(action);
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		return accepted;
	};

	NextAction runAction;
	runAction.action = acARun;
	runAction.dest = { 31, 30 };
	bool ok = check(submitAndDispatch(runAction)
		&& controlledActor->recordedAction == RecordedControlledAction::Walk
		&& controlledActor->recordedDestination == runAction.dest
		&& player.getPosition() == PlayerPosition && player.isStanding(),
		"controlled run request executes as the controlled actor's required walk downgrade");

	NextAction jumpAction;
	jumpAction.action = acJump;
	jumpAction.dest = { 32, 30 };
	ok = check(submitAndDispatch(jumpAction)
		&& controlledActor->recordedAction == RecordedControlledAction::Jump
		&& controlledActor->recordedDestination == jumpAction.dest
		&& player.getPosition() == PlayerPosition && player.isStanding(),
		"controlled jump executes on the controlled actor instead of the player") && ok;

	auto attackTarget = fixture.addNPC(
		{ 31, 31 }, nkBattle, nrHostile, "", "");
	NextAction attackAction;
	attackAction.action = acAttack;
	attackAction.dest = attackTarget->getPosition();
	attackAction.destGE = attackTarget;
	ok = check(submitAndDispatch(attackAction)
		&& controlledActor->recordedAction == RecordedControlledAction::Attack
		&& controlledActor->recordedDestination == attackAction.dest
		&& controlledActor->recordedTarget.lock() == attackTarget
		&& player.getPosition() == PlayerPosition && player.isStanding(),
		"controlled attack executes on the controlled actor with the selected target") && ok;

	NextAction magicAction;
	magicAction.action = acMagic;
	magicAction.actionParam = 0;
	magicAction.dest = attackTarget->getPosition();
	magicAction.destGE = attackTarget;
	ok = check(submitAndDispatch(magicAction)
		&& controlledActor->recordedAction == RecordedControlledAction::None
		&& gameManager.menu->messageBox != nullptr
		&& gameManager.menu->messageBox->currentMessage == "控制中不能使用武功"
		&& player.nextAction == nullptr && player.isControllingCharacter(),
		"controlled magic follows the production refusal path without dispatching a cast") && ok;

	player.endControlCharacter();
	return ok;
}

bool runControlledStrictApproachTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"controlled strict approach tests start without SDL Video");
	constexpr Point ControlledPosition = { 30, 30 };

	{
		WorldInteractionFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& player = *gameManager.player;
		auto controlledActor = fixture.addMovableNPC(ControlledPosition);
		controlledActor->pathFinder = pfBest;
		constexpr UTime FailureTime = 1000;
		controlledActor->setTime(FailureTime);
		auto controlEffect = std::make_shared<Effect>();
		player.beginControlCharacter(controlledActor, controlEffect);
		auto unreachableObject = fixture.addObject({ 36, 30 });
		blockMovementAround(*gameManager.map, ControlledPosition);

		ok = check(gameManager.queueObjectScriptInteraction(unreachableObject),
			"controlled strict object approach queues before path failure") && ok;
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		ok = check(!WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isStanding()
			&& controlledActor->lastPathFindFailTime == FailureTime
			&& player.isControllingCharacter(),
			"unreachable controlled strict object cancels after one real path failure") && ok;

		const UTime recordedFailureTime = controlledActor->lastPathFindFailTime;
		controlledActor->setTime(FailureTime + NPC_PATH_FIND_FAIL_COOLDOWN + 1);
		WorldInteractionRuntimeTestAccess::processControlledAction(player);
		ok = check(!WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->lastPathFindFailTime == recordedFailureTime,
			"canceled controlled object approach does not retry after cooldown") && ok;
		player.endControlCharacter();
	}

	{
		WorldInteractionFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& player = *gameManager.player;
		auto controlledActor = fixture.addMovableNPC(ControlledPosition);
		controlledActor->pathFinder = pfBest;
		constexpr UTime FailureTime = 2000;
		controlledActor->setTime(FailureTime);
		auto controlEffect = std::make_shared<Effect>();
		player.beginControlCharacter(controlledActor, controlEffect);
		auto unreachableNPC = fixture.addNPC({ 36, 30 });
		blockMovementAround(*gameManager.map, ControlledPosition);

		ok = check(gameManager.queueNPCTalkInteraction(unreachableNPC),
			"controlled strict NPC talk queues before path failure") && ok;
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		ok = check(!WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isStanding()
			&& controlledActor->lastPathFindFailTime == FailureTime
			&& player.isControllingCharacter(),
			"unreachable controlled strict NPC talk cancels after one real path failure") && ok;
		player.endControlCharacter();
	}

	{
		WorldInteractionFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& player = *gameManager.player;
		auto controlledActor = fixture.addMovableNPC(ControlledPosition);
		controlledActor->pathFinder = pfBest;
		constexpr UTime PreviousFailureTime = 3000;
		controlledActor->lastPathFindFailTime = PreviousFailureTime;
		controlledActor->setTime(PreviousFailureTime + 1);
		auto controlEffect = std::make_shared<Effect>();
		player.beginControlCharacter(controlledActor, controlEffect);
		auto reachableObject = fixture.addObject({ 36, 30 });

		ok = check(gameManager.queueObjectScriptInteraction(reachableObject),
			"controlled strict object queues during an existing path cooldown") && ok;
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		ok = check(WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isStanding()
			&& controlledActor->lastPathFindFailTime == PreviousFailureTime,
			"existing path cooldown keeps the controlled strict action pending") && ok;

		controlledActor->setTime(
			PreviousFailureTime + NPC_PATH_FIND_FAIL_COOLDOWN);
		WorldInteractionRuntimeTestAccess::processControlledAction(player);
		ok = check(WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isWalking()
			&& controlledActor->lastPathFindFailTime == PreviousFailureTime,
			"controlled strict action starts its reachable path when cooldown expires") && ok;
		player.endControlCharacter();
	}

	{
		WorldInteractionFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& player = *gameManager.player;
		auto controlledActor = fixture.addMovableNPC(ControlledPosition);
		controlledActor->pathFinder = pfBest;
		controlledActor->setTime(4000);
		auto controlEffect = std::make_shared<Effect>();
		player.beginControlCharacter(controlledActor, controlEffect);
		auto reachableNPC = fixture.addNPC({ 36, 30 });

		ok = check(gameManager.queueNPCTalkInteraction(reachableNPC),
			"reachable controlled strict NPC talk queues without cooldown") && ok;
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		ok = check(WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isWalking(),
			"normal reachable controlled strict NPC talk is not canceled") && ok;
		player.endControlCharacter();
	}

	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"controlled strict approach tests finish without SDL Video") && ok;
	return ok;
}

bool runControlledStrictCompletionTests()
{
	bool ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"controlled strict completion tests start without SDL Video");
	constexpr Point ControlledPosition = { 30, 30 };

	{
		WorldInteractionFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& player = *gameManager.player;
		auto controlledActor = fixture.addMovableNPC(ControlledPosition);
		auto controlEffect = std::make_shared<Effect>();
		player.beginControlCharacter(controlledActor, controlEffect);
		auto object = fixture.addObject(
			{ 36, 30 }, "controlled_object_primary.lua", "");

		ok = check(gameManager.queueObjectScriptInteraction(
				object, WorldInteractionScriptSide::Primary, false),
			"controlled strict Object completion queues its approach") && ok;
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		ok = check(WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isWalking()
			&& gameManager.scriptTaskList.empty(),
			"controlled strict Object ran before reaching interaction distance") && ok;

		controlledActor->setPosition({ 35, 30 }, true);
		gameManager.inEvent = true;
		WorldInteractionRuntimeTestAccess::processControlledAction(player);
		ok = check(!WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isStanding()
			&& gameManager.scriptTaskList.size() == 1
			&& gameManager.scriptTaskList[0].type == stObject
			&& gameManager.scriptTaskList[0].obj == object
			&& gameManager.scriptTaskList[0].scriptName
				== "controlled_object_primary.lua"
			&& gameManager.scriptTaskList[0].clearPlayerAction
			&& player.isControllingCharacter(),
			"controlled strict Object did not complete into the selected ScriptTask") && ok;
		gameManager.inEvent = false;
		player.endControlCharacter();
	}

	{
		WorldInteractionFixture fixture;
		auto& gameManager = fixture.gameManager;
		auto& player = *gameManager.player;
		auto controlledActor = fixture.addMovableNPC(ControlledPosition);
		auto controlEffect = std::make_shared<Effect>();
		player.beginControlCharacter(controlledActor, controlEffect);
		auto npc = fixture.addNPC(
			{ 36, 30 }, nkNormal, nrFriendly,
			"controlled_npc_primary.lua", "controlled_npc_alternate.lua");

		ok = check(gameManager.queueNPCTalkInteraction(
				npc, WorldInteractionScriptSide::Alternate, true),
			"controlled strict alternate NPC completion queues its approach") && ok;
		WorldInteractionRuntimeTestAccess::processQueuedAction(player);
		ok = check(WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isWalking()
			&& gameManager.scriptTaskList.empty(),
			"controlled strict NPC script ran before reaching talk distance") && ok;

		controlledActor->setPosition({ 35, 30 }, true);
		gameManager.inEvent = true;
		WorldInteractionRuntimeTestAccess::processControlledAction(player);
		ok = check(!WorldInteractionRuntimeTestAccess::hasControlledAction(player)
			&& controlledActor->isStanding()
			&& gameManager.scriptTaskList.size() == 1
			&& gameManager.scriptTaskList[0].type == stNPC
			&& gameManager.scriptTaskList[0].npc == npc
			&& gameManager.scriptTaskList[0].scriptName
				== "controlled_npc_alternate.lua"
			&& gameManager.scriptTaskList[0].clearPlayerAction
			&& player.isControllingCharacter(),
			"controlled strict NPC did not complete into the selected alternate ScriptTask") && ok;
		gameManager.inEvent = false;
		player.endControlCharacter();
	}

	ok = check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
		"controlled strict completion tests finish without SDL Video") && ok;
	return ok;
}

bool runStrictRevalidationTests()
{
	WorldInteractionFixture fixture;
	bool ok = true;
	auto& gameManager = fixture.gameManager;
	auto& player = *gameManager.player;

	auto deletedBeforeDispatch = fixture.addObject({ 26, 20 });
	ok = check(gameManager.queueObjectScriptInteraction(deletedBeforeDispatch),
		"strict object action queues before target deletion") && ok;
	gameManager.objectManager->deleteObject(deletedBeforeDispatch);
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	ok = check(isStrictQueueCleared(player),
		"production dispatch cancels target deleted before movement") && ok;

	auto scriptChangedObject = fixture.addObject({ 26, 22 });
	ok = check(gameManager.queueObjectScriptInteraction(scriptChangedObject),
		"strict object action queues before script mutation") && ok;
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	ok = check(isStrictPendingFor(player, scriptChangedObject, ndObj, false, false),
		"production dispatch promotes strict object metadata into movement") && ok;
	scriptChangedObject->scriptFile.clear();
	ok = check(player.handleQueuedInteractionAtCurrentPosition()
		&& isStrictQueueCleared(player),
		"pending strict object action cancels when its selected script disappears") && ok;

	auto rightScriptChangedNPC = fixture.addNPC(
		{ 26, 24 }, nkNormal, nrFriendly, "npc_primary.lua", "npc_right.lua");
	ok = check(gameManager.queueNPCTalkInteraction(
		rightScriptChangedNPC, WorldInteractionScriptSide::Alternate, true),
		"strict alternate NPC talk queues before script mutation") && ok;
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	ok = check(isStrictPendingFor(player, rightScriptChangedNPC, ndTalk, true, true),
		"production dispatch preserves strict script side and run intent") && ok;
	rightScriptChangedNPC->scriptFileRight.clear();
	ok = check(player.handleQueuedInteractionAtCurrentPosition()
		&& isStrictQueueCleared(player),
		"strict Alternate cancels instead of falling back after right-script removal") && ok;

	auto relationChangedNPC = fixture.addNPC(
		{ 27, 20 }, nkBattle, nrHostile, "", "");
	ok = check(gameManager.queueNPCAttackInteraction(relationChangedNPC),
		"strict attack queues while target is hostile") && ok;
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	ok = check(isStrictPendingFor(player, relationChangedNPC, ndAttack, false, false),
		"production dispatch promotes strict attack metadata into movement") && ok;
	relationChangedNPC->relation = nrFriendly;
	ok = check(player.handleQueuedInteractionAtCurrentPosition()
		&& isStrictQueueCleared(player),
		"pending strict attack cancels after relationship becomes friendly") && ok;

	auto hiddenNPC = fixture.addNPC(
		{ 28, 22 }, nkBattle, nrHostile, "npc_primary.lua", "");
	ok = check(gameManager.queueNPCTalkInteraction(hiddenNPC),
		"strict NPC talk queues while target is visible") && ok;
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	hiddenNPC->isVisibleByVariable = false;
	ok = check(player.handleQueuedInteractionAtCurrentPosition()
		&& isStrictQueueCleared(player),
		"pending strict talk cancels when target becomes hidden") && ok;

	auto movedObject = fixture.addObject({ 29, 20 });
	ok = check(gameManager.queueObjectScriptInteraction(
		movedObject, WorldInteractionScriptSide::Primary, true),
		"strict running object action queues before target movement") && ok;
	WorldInteractionRuntimeTestAccess::processQueuedAction(player);
	movedObject->setPosition({ 30, 20 });
	ok = check(WorldInteractionRuntimeTestAccess::resumeStrictInteraction(player)
		&& player.nextDest == ndObj && player.destGE.lock() == movedObject
		&& player.nextDestStrictWorldInteraction,
		"strict resume revalidates and keeps a moved but still-legal target") && ok;
	player.cancelQueuedInteraction(false);
	return ok;
}
}

bool runWorldInteractionRuntimeTests()
{
	bool ok = true;
	ok = runObjectIntentMatrixTests() && ok;
	ok = runNPCIntentMatrixTests() && ok;
	ok = runExplicitQueueTests() && ok;
	ok = runCandidateSortingTests() && ok;
	ok = runInteractionOcclusionBoundaryTests() && ok;
	ok = runJxqy2LowBitChestInteractionTests() && ok;
	ok = runYYCSWudangGuestApproachTests() && ok;
	ok = runControlledActorCandidateTests() && ok;
	ok = runControlledCameraLifecycleTests() && ok;
	ok = runControlledActorExecutionTests() && ok;
	ok = runControlledStrictApproachTests() && ok;
	ok = runControlledStrictCompletionTests() && ok;
	ok = runStrictRevalidationTests() && ok;
	return ok;
}
