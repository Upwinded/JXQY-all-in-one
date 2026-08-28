#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Data/Effect.h"
#include "../Game/Data/EffectManager.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/MagicManager.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/GoodsMenu.h"
#include "../Game/Menu/MsgBox.h"
#include "../Game/Menu/PracticeMenu.h"
#include "../Game/Menu/StateMenu.h"
#include "../Game/Menu/SystemNotice.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

class RageSystemTestAccess
{
public:
	static void updateRangeEffect(Effect& effect, UTime frameTime)
	{
		effect.updateRangeEffect(frameTime);
	}

	static void updateFlyMagic(Effect& effect, UTime frameTime)
	{
		effect.updateFlyMagic(frameTime);
	}

	static void updateMagicWhenNewPosition(Effect& effect)
	{
		effect.updateMagicWhenNewPosition();
	}

	static void recordActualDamage(Player& player, int damage)
	{
		player.recordActualDamageForRage(damage);
	}
};

class MagicDerivedRuntimeTestAccess
{
public:
	static void updateDelayedMagic(EffectManager& manager)
	{
		manager.updateDelayedMagic();
	}
};

namespace
{
constexpr char MagicDerivedSaveNamespace[] =
	"magic-derived-runtime";

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
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

bool prepareMagicFixtures(const std::filesystem::path& root)
{
	const std::string childMagic =
		"[Init]\n"
		"Name=CHILD\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"[Level1]\n"
		"Effect=1\n";
	const std::string parentMagic =
		"[Init]\n"
		"Name=PARENT\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"ExplodeMagicFile=base_child.ini\n"
		"AttackFile=base_child.ini\n"
		"FlyMagic=base_child.ini\n"
		"FlyInterval=100\n"
		"ParasiticMagic=base_child.ini\n"
		"Parasitic=1\n"
		"ParasiticInterval=200\n"
		"ParasiticMaxEffect=300\n"
		"RandMagicFile=base_child.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=base_child.ini\n"
		"SecondMagicDelay=400\n"
		"MagicWhenNewPos=base_child.ini\n"
		"MagicToUseWhenKillEnemy=base_child.ini\n"
		"MagicDirectionWhenKillEnemy=1\n"
		"BounceFlyEndMagic=base_child.ini\n"
		"BounceFly=2\n"
		"BounceFlySpeed=30\n"
		"BounceFlyEndHurt=5\n"
		"BounceFlyTouchHurt=6\n"
		"MagicDirectionWhenBounceFlyEnd=1\n"
		"ChangeMagic=base_child.ini\n"
		"HitCountToChangeMagic=2\n"
		"HitCountFlyRadius=20\n"
		"HitCountFlyAngleSpeed=10\n"
		"JumpEndMagic=base_child.ini\n"
		"JumpToTarget=1\n"
		"JumpMoveSpeed=40\n"
		"[Level1]\n"
		"MoveKind=1\n"
		"[Level2]\n"
		"MoveKind=1\n"
		"ExplodeMagicFile=alternate_child.ini\n"
		"AttackFile=alternate_child.ini\n"
		"FlyMagic=alternate_child.ini\n"
		"FlyInterval=110\n"
		"ParasiticMagic=alternate_child.ini\n"
		"Parasitic=2\n"
		"ParasiticInterval=210\n"
		"ParasiticMaxEffect=310\n"
		"RandMagicFile=alternate_child.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=alternate_child.ini\n"
		"SecondMagicDelay=410\n"
		"MagicWhenNewPos=alternate_child.ini\n"
		"MagicToUseWhenKillEnemy=alternate_child.ini\n"
		"MagicDirectionWhenKillEnemy=2\n"
		"BounceFlyEndMagic=alternate_child.ini\n"
		"BounceFly=3\n"
		"BounceFlySpeed=31\n"
		"BounceFlyEndHurt=7\n"
		"BounceFlyTouchHurt=8\n"
		"MagicDirectionWhenBounceFlyEnd=2\n"
		"ChangeMagic=alternate_child.ini\n"
		"HitCountToChangeMagic=3\n"
		"HitCountFlyRadius=21\n"
		"HitCountFlyAngleSpeed=11\n"
		"JumpEndMagic=alternate_child.ini\n"
		"JumpToTarget=2\n"
		"JumpMoveSpeed=41\n"
		"[Level3]\n"
		"MoveKind=1\n"
		"ExplodeMagicFile=\n"
		"AttackFile=\n"
		"FlyMagic=\n"
		"FlyInterval=0\n"
		"ParasiticMagic=\n"
		"Parasitic=0\n"
		"ParasiticInterval=0\n"
		"ParasiticMaxEffect=0\n"
		"RandMagicFile=\n"
		"RandMagicProbability=0\n"
		"SecondMagicFile=\n"
		"SecondMagicDelay=0\n"
		"MagicWhenNewPos=\n"
		"MagicToUseWhenKillEnemy=\n"
		"MagicDirectionWhenKillEnemy=0\n"
		"BounceFlyEndMagic=\n"
		"BounceFly=0\n"
		"ChangeMagic=\n"
		"HitCountToChangeMagic=0\n"
		"JumpEndMagic=\n"
		"JumpToTarget=0\n"
		"[Level4]\n"
		"MoveKind=1\n"
		"AttackFile=missing_attack.ini\n";
	const std::string rageMagic =
		"[Init]\n"
		"Name=RAGE\n"
		"MoveKind=13\n"
		"SpecialKind=99\n"
		"RangeEffect=1\n"
		"RangeRadius=0\n"
		"RangeTimeInerval=500\n"
		"[Level1]\n"
		"MoveKind=13\n"
		"SpecialKind=99\n"
		"RageCost=100\n"
		"RangeAddRage=5\n"
		"CritChanceAddValue=30\n"
		"CritDamageAddPercent=50\n"
		"LifeFrame=1000\n"
		"[Level2]\n"
		"MoveKind=13\n";
	const std::string magicExperience =
		"[HitMagicExp]\n"
		"LevelFactor=3\n"
		"[XiuLianMagicExp]\n"
		"Fraction=0.2222\n"
		"[UseMagicExp]\n"
		"Fraction=0.0333\n";

	return writeTextFile(root / "ini" / "magic" / "base_child.ini", childMagic)
		&& writeTextFile(root / "ini" / "magic" / "alternate_child.ini", childMagic)
		&& writeTextFile(root / "ini" / "magic" / "parent.ini", parentMagic)
		&& writeTextFile(root / "ini" / "magic" / "rage.ini", rageMagic)
		&& writeTextFile(root / "ini" / "level" / "MagicExp.ini", magicExperience);
}

bool prepareLinkedMagicGraphFixtures(
	const std::filesystem::path& root,
	const std::filesystem::path& firstFallbackRoot,
	const std::filesystem::path& secondFallbackRoot)
{
	const std::string leafMagic =
		"[Init]\n"
		"Name=LEAF\n"
		"MoveKind=1\n"
		"LifeFrame=10\n";
	const std::string selfMagic =
		"[Init]\n"
		"Name=SELF\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"RandMagicFile=self.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=graph_leaf.ini\n";
	const std::string mutualA =
		"[Init]\n"
		"Name=MUTUAL_A\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"RandMagicFile=mutual_b.ini\n"
		"RandMagicProbability=100\n";
	const std::string mutualB =
		"[Init]\n"
		"Name=MUTUAL_B\n"
		"MoveKind=1\n"
		"LifeFrame=10\n"
		"SecondMagicFile=mutual_a.ini\n"
		"FlyMagic=graph_leaf.ini\n"
		"FlyInterval=1\n";
	const std::string attackParent =
		"[Init]\n"
		"Name=ATTACK_PARENT\n"
		"MoveKind=1\n"
		"AttackFile=attack_child.ini\n";
	const std::string attackChild =
		"[Init]\n"
		"Name=ATTACK_CHILD\n"
		"MoveKind=1\n"
		"AttackFile=graph_leaf.ini\n"
		"RandMagicFile=graph_leaf.ini\n"
		"RandMagicProbability=100\n";
	const std::string dependencyParent =
		"[Init]\n"
		"Name=DEPENDENCY_PARENT\n"
		"MoveKind=1\n"
		"FlyMagic=dependency_child.ini\n"
		"FlyInterval=1\n";
	const std::string dependencyChild =
		"[Init]\n"
		"Name=DEPENDENCY_CHILD\n"
		"MoveKind=1\n"
		"ExplodeMagicFile=dependency_grandchild.ini\n";

	bool ok = writeTextFile(root / "ini" / "magic" / "graph_leaf.ini", leafMagic)
		&& writeTextFile(root / "ini" / "magic" / "self.ini", selfMagic)
		&& writeTextFile(root / "ini" / "magic" / "mutual_a.ini", mutualA)
		&& writeTextFile(root / "ini" / "magic" / "mutual_b.ini", mutualB)
		&& writeTextFile(root / "ini" / "magic" / "attack_parent.ini", attackParent)
		&& writeTextFile(root / "ini" / "magic" / "attack_child.ini", attackChild)
		&& writeTextFile(root / "ini" / "magic" / "dependency_parent.ini", dependencyParent)
		&& writeTextFile(firstFallbackRoot / "ini" / "magic" / "dependency_child.ini", dependencyChild)
		&& writeTextFile(
			secondFallbackRoot / "ini" / "magic" / "dependency_child.ini",
			"[Init]\nName=WRONG_PRECEDENCE\nMoveKind=1\n")
		&& writeTextFile(secondFallbackRoot / "ini" / "magic" / "dependency_grandchild.ini", leafMagic);

	for (int index = 0; index <= Magic::MaxLinkedMagicLoadDepth + 1; index++)
	{
		std::string content =
			"[Init]\n"
			"Name=DEPTH_" + std::to_string(index) + "\n"
			"MoveKind=1\n";
		if (index <= Magic::MaxLinkedMagicLoadDepth)
		{
			content += "RandMagicFile=depth_" + std::to_string(index + 1) + ".ini\n";
			content += "RandMagicProbability=100\n";
		}
		ok = writeTextFile(
			root / "ini" / "magic" / ("depth_" + std::to_string(index) + ".ini"),
			content) && ok;
	}

	for (size_t index = 1; index <= Magic::MaxLinkedMagicLoadNodes; index++)
	{
		std::string content =
			"[Init]\n"
			"Name=BUDGET_" + std::to_string(index) + "\n"
			"MoveKind=1\n";
		const size_t left = index * 2;
		const size_t right = left + 1;
		if (left < Magic::MaxLinkedMagicLoadNodes)
		{
			content += "RandMagicFile=budget_" + std::to_string(left) + ".ini\n";
			content += "RandMagicProbability=100\n";
		}
		if (right < Magic::MaxLinkedMagicLoadNodes)
		{
			content += "SecondMagicFile=budget_" + std::to_string(right) + ".ini\n";
		}
		ok = writeTextFile(
			root / "ini" / "magic" / ("budget_" + std::to_string(index) + ".ini"),
			content) && ok;
	}
	const std::string budgetRoot =
		"[Init]\n"
		"Name=BUDGET_ROOT\n"
		"MoveKind=1\n"
		"RandMagicFile=budget_1.ini\n"
		"RandMagicProbability=100\n"
		"SecondMagicFile=budget_256.ini\n";
	return writeTextFile(root / "ini" / "magic" / "budget_root.ini", budgetRoot) && ok;
}

size_t countLinkedMagicNodes(
	const std::shared_ptr<Magic>& magic,
	std::unordered_set<const Magic*>& visited)
{
	if (magic == nullptr || !visited.insert(magic.get()).second)
	{
		return 0;
	}
	const auto& linked = magic->getLinkedLevel(1);
	return 1
		+ countLinkedMagicNodes(linked.randMagic, visited)
		+ countLinkedMagicNodes(linked.secondMagic, visited);
}

bool runLinkedMagicGraphLoadingTest(
	const std::filesystem::path& root,
	const std::filesystem::path& firstFallbackRoot,
	const std::filesystem::path& secondFallbackRoot)
{
	if (!prepareLinkedMagicGraphFixtures(root, firstFallbackRoot, secondFallbackRoot))
	{
		return check(false, "write linked magic graph fixtures");
	}

	bool ok = true;
	Magic self;
	self.initFromIni("self.ini");
	ok = check(self.loadSucceeded
		&& self.getLinkedLevel(1).randMagic == nullptr
		&& self.getLinkedLevel(1).secondMagic != nullptr,
		"self cycle truncates only the cyclic branch") && ok;

	Magic mutual;
	mutual.initFromIni("mutual_a.ini");
	auto mutualChild = mutual.getLinkedLevel(1).randMagic;
	ok = check(mutualChild != nullptr
		&& mutualChild->getLinkedLevel(1).secondMagic == nullptr
		&& mutualChild->getLinkedLevel(1).flyMagic != nullptr,
		"mutual cycle truncates the back edge and keeps a valid sibling") && ok;

	Magic attackParent;
	attackParent.initFromIni("attack_parent.ini");
	auto attackChild = attackParent.getLinkedLevel(1).specialMagic;
	ok = check(attackChild != nullptr
		&& attackChild->getLinkedLevel(1).specialMagic == nullptr
		&& attackChild->getLinkedLevel(1).randMagic != nullptr,
		"AttackFile child suppresses nested AttackFile but keeps other linked magic") && ok;

	Magic depthRoot;
	depthRoot.initFromIni("depth_0.ini");
	const Magic* depthNode = &depthRoot;
	int loadedDepth = 1;
	while (depthNode != nullptr && depthNode->getLinkedLevel(1).randMagic != nullptr)
	{
		depthNode = depthNode->getLinkedLevel(1).randMagic.get();
		loadedDepth++;
	}
	ok = check(loadedDepth == Magic::MaxLinkedMagicLoadDepth
		&& depthNode != nullptr
		&& depthNode->getLinkedLevel(1).randMagic == nullptr,
		"linked magic load depth limit truncates only the over-depth edge") && ok;

	Magic budgetRoot;
	budgetRoot.initFromIni("budget_root.ini");
	std::unordered_set<const Magic*> visited;
	const size_t loadedNodes = 1
		+ countLinkedMagicNodes(budgetRoot.getLinkedLevel(1).randMagic, visited)
		+ countLinkedMagicNodes(budgetRoot.getLinkedLevel(1).secondMagic, visited);
	ok = check(loadedNodes == Magic::MaxLinkedMagicLoadNodes
		&& budgetRoot.getLinkedLevel(1).randMagic != nullptr
		&& budgetRoot.getLinkedLevel(1).secondMagic == nullptr,
		"linked magic node budget preserves loaded branches and truncates the next branch") && ok;

	File::setResourceFallbackRoots({ firstFallbackRoot.string(), secondFallbackRoot.string() });
	Magic dependencyParent;
	dependencyParent.initFromIni("dependency_parent.ini");
	auto dependencyChild = dependencyParent.getLinkedLevel(1).flyMagic;
	ok = check(dependencyChild != nullptr
		&& dependencyChild->iniName == "dependency_child.ini"
		&& dependencyChild->name == "DEPENDENCY_CHILD"
		&& dependencyChild->experienceOwnerMagicFile == "dependency_parent.ini"
		&& dependencyChild->getExplodeMagicForLevel(1) != nullptr
		&& dependencyChild->getExplodeMagicForLevel(1)->iniName == "dependency_grandchild.ini"
		&& dependencyChild->getExplodeMagicForLevel(1)->experienceOwnerMagicFile == "dependency_parent.ini",
		"linked magic recursion resolves child and grandchild through dependency roots with root experience ownership") && ok;
	File::setResourceFallbackRoots({});
	return ok;
}

std::shared_ptr<Magic> makeRuntimeMagic(const std::string& fileName)
{
	auto magic = std::make_shared<Magic>();
	magic->iniName = fileName;
	magic->experienceOwnerMagicFile = fileName;
	magic->loadSucceeded = true;
	magic->level[1].moveKind = mmkPoint;
	magic->level[1].lifeFrame = 10;
	return magic;
}

bool runLinkedMagicRuntimeBudgetTest(GameManager& gameManager)
{
	auto originalMapData = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 64;
	gameManager.map->data->head.height = 64;
	gameManager.player->setPosition({ 20, 20 }, false);
	const Point from = gameManager.player->getPosition();
	const Point to = Map::getSubPoint(from, 0);
	bool ok = true;

	gameManager.effectManager->freeResource();
	auto randA = makeRuntimeMagic("runtime_rand_a.ini");
	auto randB = makeRuntimeMagic("runtime_rand_b.ini");
	randA->linkedLevel[1].randMagic = randB;
	randA->linkedLevel[1].randMagicProbability = 100;
	randB->linkedLevel[1].randMagic = randA;
	randB->linkedLevel[1].randMagicProbability = 100;
	Magic::addEffect(randA, gameManager.player, from, to, 1, 1, 0, lkSelf, nullptr);
	ok = check(gameManager.effectManager->effectList.size() == 2,
		"runtime RandMagic mutual cycle stops after the valid child") && ok;
	randA->linkedLevel[1].randMagic = nullptr;
	randB->linkedLevel[1].randMagic = nullptr;
	gameManager.effectManager->freeResource();

	auto secondA = makeRuntimeMagic("runtime_second_a.ini");
	auto secondB = makeRuntimeMagic("runtime_second_b.ini");
	secondA->linkedLevel[1].secondMagic = secondB;
	secondA->linkedLevel[1].secondMagicDelay = 0;
	secondB->linkedLevel[1].secondMagic = secondA;
	secondB->linkedLevel[1].secondMagicDelay = 0;
	Magic::addEffect(secondA, gameManager.player, from, to, 1, 1, 0, lkSelf, nullptr);
	ok = check(gameManager.effectManager->effectList.size() == 1
		&& gameManager.effectManager->getPendingDelayedMagicCount() == 1,
		"runtime SecondMagic queues the first valid child") && ok;
	MagicDerivedRuntimeTestAccess::updateDelayedMagic(*gameManager.effectManager);
	ok = check(gameManager.effectManager->effectList.size() == 2
		&& gameManager.effectManager->getPendingDelayedMagicCount() == 0,
		"runtime SecondMagic mutual cycle does not requeue its ancestor") && ok;
	secondA->linkedLevel[1].secondMagic = nullptr;
	secondB->linkedLevel[1].secondMagic = nullptr;
	gameManager.effectManager->freeResource();

	auto contextRootMagic = makeRuntimeMagic("runtime_budget_root.ini");
	auto contextChildMagic = makeRuntimeMagic("runtime_budget_child.ini");
	auto rootContext = Magic::createRootDispatchContext(contextRootMagic);
	for (size_t index = 1; index < Magic::MaxDerivedMagicRuntimeNodes; index++)
	{
		ok = check(Magic::createDerivedDispatchContext(
			rootContext,
			contextChildMagic,
			"RuntimeBudgetSibling") != nullptr,
			"runtime linked magic accepts a child within the shared node budget") && ok;
		if (!ok)
		{
			break;
		}
	}
	ok = check(Magic::createDerivedDispatchContext(
		rootContext,
		contextChildMagic,
		"RuntimeBudgetSibling") == nullptr,
		"runtime linked magic rejects the first child beyond the shared node budget") && ok;

	auto depthContext = Magic::createRootDispatchContext(makeRuntimeMagic("runtime_depth_0.ini"));
	for (int depth = 1; depth < Magic::MaxDerivedMagicRuntimeDepth; depth++)
	{
		depthContext = Magic::createDerivedDispatchContext(
			depthContext,
			makeRuntimeMagic("runtime_depth_" + std::to_string(depth) + ".ini"),
			"RuntimeDepth");
		ok = check(depthContext != nullptr,
			"runtime linked magic accepts a child within the depth budget") && ok;
	}
	ok = check(Magic::createDerivedDispatchContext(
		depthContext,
		makeRuntimeMagic("runtime_depth_over.ini"),
		"RuntimeDepth") == nullptr,
		"runtime linked magic rejects the first over-depth child") && ok;

	gameManager.effectManager->freeResource();
	gameManager.map->data = originalMapData;
	return ok;
}

bool runRageSystemTest(GameManager& gameManager, const std::filesystem::path& root)
{
	auto rageMagic = std::make_shared<Magic>();
	rageMagic->initFromIni("rage.ini");
	bool ok = true;
	ok = check(rageMagic->loadSucceeded
		&& rageMagic->level[1].rageCost == 100
		&& rageMagic->level[1].rangeAddRage == 5
		&& rageMagic->level[1].critChanceAddValue == 30
		&& rageMagic->level[1].critDamageAddPercent == 50,
		"MG rage fields load together") && ok;
	ok = check(rageMagic->level[2].rageCost == 100
		&& rageMagic->level[2].hasRageCost
		&& rageMagic->level[2].rangeAddRage == 5
		&& rageMagic->level[2].hasRangeAddRage
		&& rageMagic->level[2].critChanceAddValue == 30
		&& rageMagic->level[2].hasCritChanceAddValue
		&& rageMagic->level[2].critDamageAddPercent == 50
		&& rageMagic->level[2].hasCritDamageAddPercent,
		"MG rage and critical fields inherit as one level capability group") && ok;
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getMagicState("rage.ini", "CritChanceAddValue", "rage_crit_chance", 2);
	gameManager.scriptAPI.getMagicState("rage.ini", "HasCritChanceAddValue", "rage_has_crit_chance", 2);
	gameManager.scriptAPI.getMagicState("rage.ini", "CritDamageAddPercent", "rage_crit_damage", 2);
	ok = check(gameManager.varList.getInteger("rage_crit_chance") == 30
		&& gameManager.varList.getInteger("rage_has_crit_chance") == 1
		&& gameManager.varList.getInteger("rage_crit_damage") == 50,
		"GetMagicState exposes inherited MG critical fields") && ok;

	auto player = gameManager.player;
	player->level = 10;
	player->life = 100;
	player->defend = 0;
	player->rageMax = 100;
	gameManager.global.feature.rageSystem = false;
	player->setRage(0);
	ok = check(player->tryConsumeMagicCost(rageMagic, 1, false),
		"disabled RageSystem leaves trilogy magic-cost behavior unchanged") && ok;
	ok = check(player->applyCriticalDamage(100, 0) == 100,
		"disabled RageSystem leaves trilogy damage behavior unchanged") && ok;

	gameManager.global.feature.rageSystem = true;
	player->setRage(99);
	ok = check(!player->tryConsumeMagicCost(rageMagic, 1, false),
		"RageCost rejects a cast below the required rage") && ok;
	player->setRage(100);
	ok = check(player->tryConsumeMagicCost(rageMagic, 1, false) && player->rage == 100,
		"RageCost is a gate and does not deduct rage at the cast entry") && ok;

	RageSystemTestAccess::recordActualDamage(*player, 10);
	ok = check(player->rage == 100,
		"actual player hurt adds one rage and clamps at RageMax") && ok;
	player->setRage(50);
	RageSystemTestAccess::recordActualDamage(*player, 10);
	ok = check(player->rage == 51,
		"each actual player hurt event adds one rage") && ok;
	gameManager.scriptAPI.getPlayerState("Rage", "player_rage");
	gameManager.scriptAPI.getPlayerState("RageMax", "player_rage_max");
	ok = check(gameManager.varList.getInteger("player_rage") == 51
		&& gameManager.varList.getInteger("player_rage_max") == 100
		&& gameManager.getBindValue("player.rage") == 51
		&& gameManager.getBindValue("player.rageMax") == 100,
		"script and config-driven UI bindings expose Rage/RageMax") && ok;

	Point playerPosition = player->getPosition();
	auto effects = Magic::addEffect(rageMagic, player, playerPosition, playerPosition, 1, 0, 0, lkSelf, player);
	ok = check(effects.size() == 1 && player->attributeChangeEffect.lock() == effects.front(),
		"SpecialKind 99 binds its active self Effect to the player") && ok;
	ok = check(effects.size() == 1 && effects.front()->getExplodinUTime() == 10000,
		"SpecialKind 99 uses LifeFrame as its active duration") && ok;
	ok = check(std::abs(player->getCriticalChancePercent() - 31.0f) < 0.001f
		&& player->getCriticalDamagePercent() == 60,
		"SpecialKind 99 combines MG level defaults with critical additions") && ok;
	bool wasCritical = false;
	int criticalDamage = player->applyCriticalDamage(100, 31, &wasCritical);
	bool wasNormalHit = true;
	int normalDamage = player->applyCriticalDamage(100, 32, &wasNormalHit);
	ok = check(criticalDamage == 160
		&& wasCritical
		&& normalDamage == 100
		&& !wasNormalHit,
		"critical chance uses the inclusive 0..100 roll and reports MG critical-tip state") && ok;

	auto rangeEffect = std::make_shared<Effect>();
	rangeEffect->level = 1;
	rangeEffect->user = player;
	rangeEffect->position = playerPosition;
	rangeEffect->initFromMagic(rageMagic);
	player->setRage(100);
	RageSystemTestAccess::updateRangeEffect(*rangeEffect, 500);
	ok = check(player->rage == 95,
		"RangeAddRage follows the MG contract and drains rage on the range cadence") && ok;
	gameManager.global.feature.rageSystem = false;
	RageSystemTestAccess::updateRangeEffect(*rangeEffect, 500);
	ok = check(player->rage == 95,
		"disabled RageSystem prevents RangeAddRage side effects") && ok;

	gameManager.global.feature.rageSystem = true;
	INIReader savedAttributeEffect;
	effects.front()->saveToIni(&savedAttributeEffect, "RAGE1");
	effects.front()->releaseRuntimeBindings();
	ok = check(player->attributeChangeEffect.expired()
		&& std::abs(player->getCriticalChancePercent() - 1.0f) < 0.001f,
		"releasing the SpecialKind 99 Effect removes its temporary critical additions") && ok;
	auto loadedAttributeEffect = std::make_shared<Effect>();
	loadedAttributeEffect->initFromIni(&savedAttributeEffect, "RAGE1");
	loadedAttributeEffect->restoreRuntimeBindingsAfterLoad();
	ok = check(player->attributeChangeEffect.lock() == loadedAttributeEffect
		&& std::abs(player->getCriticalChancePercent() - 31.0f) < 0.001f,
		"Effect save/load restores the active SpecialKind 99 critical binding") && ok;
	loadedAttributeEffect->releaseRuntimeBindings();

	std::filesystem::path originalWorkingDirectory = std::filesystem::current_path();
	std::error_code fileError;
	std::filesystem::create_directories(
		root / "save" / MagicDerivedSaveNamespace / "game",
		fileError);
	if (!check(!fileError, "create isolated RageSystem save directory"))
	{
		gameManager.global.feature.rageSystem = false;
		return false;
	}
	std::filesystem::current_path(root, fileError);
	if (!check(!fileError, "enter isolated RageSystem save directory"))
	{
		gameManager.global.feature.rageSystem = false;
		return false;
	}
	player->setRage(73);
	player->levelIni = "";
	player->save();
	auto savedPlayerPath = root / "save" /
		MagicDerivedSaveNamespace / "game" / "player.ini";
	ok = check(std::filesystem::exists(savedPlayerPath),
		"RageSystem player save writes the current player file") && ok;
	auto loadedPlayer = std::make_shared<Player>();
	loadedPlayer->load();
	ok = check(loadedPlayer->rage == 73 && loadedPlayer->rageMax == 100,
		"player save/load restores Rage and keeps the MG RageMax default") && ok;

	player->actionManager->restartActionIgnoringTransitions(acStand);
	player->setTime(5000);
	player->actionBeginTime = player->getTime();
	ok = check(player->save(),
		"standing player regression fixture writes a direct-load save") && ok;
	player->setTime(25);
	player->actionBeginTime = 5000;
	player->haveAsyncDest = true;
	player->stepList.push_back({ 1, 1 });
	player->setOffset({ 3.0f, -2.0f });
	player->load();
	const UTime loadedStandBeginTime = player->actionBeginTime;
	player->actionLastTime = 100;
	player->actionManager->update(16);
	player->actionManager->update(16);
	ok = check(
		player->actionManager->getCurrentActionType() == acStand
			&& player->isStanding()
			&& player->actionBeginTime == loadedStandBeginTime
			&& loadedStandBeginTime == player->getTime()
			&& !player->haveAsyncDest
			&& player->stepList.empty()
			&& player->getOffset().x == 0.0f
			&& player->getOffset().y == 0.0f,
		"standing direct load restarts a stable idle action without requiring movement") && ok;

	player->actionLastTime = 100;
	player->actionBeginTime = player->getTime() + 500;
	const UTime futureActionBeginTime = player->actionBeginTime;
	player->actionManager->update(16);
	ok = check(player->actionBeginTime == futureActionBeginTime,
		"standing animation ignores elapsed time while the action clock is ahead") && ok;
	player->actionManager->restartActionIgnoringTransitions(acStand);
	std::filesystem::current_path(originalWorkingDirectory, fileError);
	ok = check(!fileError, "restore working directory after RageSystem save test") && ok;
	gameManager.global.feature.rageSystem = false;
	return ok;
}

bool runInsufficientResourceMessageTest(GameManager& gameManager)
{
	gameManager.menu->messageBox = std::make_shared<MsgBox>();
	auto magic = std::make_shared<Magic>();
	magic->level[1].manaCost = 10;
	magic->level[1].thewCost = 10;
	magic->level[1].lifeCost = 10;

	auto& player = *gameManager.player;
	player.canUseMana = true;
	player.mana = 0;
	player.thew = 100;
	player.life = 100;
	bool ok = check(!player.tryConsumeMagicCost(magic, 1, true)
		&& gameManager.menu->messageBox->currentMessage == "内力不足!",
		"magic cost reports insufficient mana");

	player.mana = 100;
	player.thew = 0;
	ok = check(!player.tryConsumeMagicCost(magic, 1, true)
		&& gameManager.menu->messageBox->currentMessage == "体力不足!",
		"shared trilogy magic cost reports insufficient stamina") && ok;

	player.thew = 100;
	player.canUseMana = false;
	ok = check(!player.tryConsumeMagicCost(magic, 1, true)
		&& gameManager.menu->messageBox->currentMessage == "内力不足!",
		"LimitMana reports why magic use is blocked") && ok;
	player.canUseMana = true;
	return ok;
}

bool runExplodeMagicLevelLoadingTest(const std::filesystem::path& root, Magic& copiedParent)
{
	if (!prepareMagicFixtures(root))
	{
		std::cerr << "FAILED: write derived magic fixtures\n";
		return false;
	}

	Magic parent;
	parent.initFromIni("parent.ini");
	bool ok = true;
	ok = check(parent.loadSucceeded, "parent magic loads") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(1) == "base_child.ini",
		"Level1 inherits ExplodeMagicFile from Init") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(2) == "alternate_child.ini",
		"Level2 overrides ExplodeMagicFile") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(3).empty(),
		"explicit empty Level3 ExplodeMagicFile disables the Init child") && ok;
	ok = check(parent.getExplodeMagicFileForLevel(4) == "base_child.ini",
		"Level4 falls back to Init instead of inheriting Level2") && ok;

	auto level1Child = parent.getExplodeMagicForLevel(1);
	auto level2Child = parent.getExplodeMagicForLevel(2);
	auto level3Child = parent.getExplodeMagicForLevel(3);
	auto level4Child = parent.getExplodeMagicForLevel(4);
	ok = check(level1Child != nullptr && level1Child->iniName == "base_child.ini",
		"Level1 resolves the Init explode child") && ok;
	ok = check(level2Child != nullptr && level2Child->iniName == "alternate_child.ini",
		"Level2 resolves its alternate explode child") && ok;
	ok = check(level3Child == nullptr, "Level3 resolves no explode child") && ok;
	ok = check(level4Child == level1Child,
		"equal per-level child names reuse one loaded Magic object") && ok;

	const auto& linked1 = parent.getLinkedLevel(1);
	const auto& linked2 = parent.getLinkedLevel(2);
	const auto& linked3 = parent.getLinkedLevel(3);
	const auto& linked4 = parent.getLinkedLevel(4);
	ok = check(linked1.specialMagic != nullptr && linked1.specialMagic->iniName == "base_child.ini"
		&& linked2.specialMagic != nullptr && linked2.specialMagic->iniName == "alternate_child.ini",
		"AttackFile resolves the Init child and the Level2 override") && ok;
	ok = check(linked3.specialMagic == linked1.specialMagic
		&& linked3.attackFile == "base_child.ini",
		"explicit empty AttackFile preserves the Init attack child") && ok;
	ok = check(linked4.specialMagic == linked1.specialMagic
		&& linked4.attackFile == "base_child.ini",
		"missing Level4 AttackFile target preserves the Init attack child") && ok;
	ok = check(linked2.flyMagic != nullptr && linked2.flyMagic->iniName == "alternate_child.ini"
		&& linked2.flyInterval == 110
		&& linked2.parasiticMagic != nullptr
		&& linked2.parasitic == 2
		&& linked2.parasiticInterval == 210
		&& linked2.parasiticMaxEffect == 310
		&& linked2.randMagic != nullptr
		&& linked2.randMagicProbability == 100
		&& linked2.secondMagic != nullptr
		&& linked2.secondMagicDelay == 410,
		"Level2 overrides linked magic files and timing/probability companions as one group") && ok;
	ok = check(linked2.magicWhenNewPosition != nullptr
		&& linked2.magicToUseWhenKillEnemy != nullptr
		&& linked2.magicDirectionWhenKillEnemy == 2
		&& linked2.bounceFlyEndMagic != nullptr
		&& linked2.bounceFly == 3
		&& linked2.bounceFlySpeed == 31
		&& linked2.bounceFlyEndHurt == 7
		&& linked2.bounceFlyTouchHurt == 8
		&& linked2.magicDirectionWhenBounceFlyEnd == 2
		&& linked2.changeMagic != nullptr
		&& linked2.hitCountToChangeMagic == 3
		&& linked2.hitCountFlyRadius == 21
		&& linked2.hitCountFlyAngleSpeed == 11
		&& linked2.jumpEndMagic != nullptr
		&& linked2.jumpToTarget == 2
		&& linked2.jumpMoveSpeed == 41,
		"Level2 overrides position, kill, bounce, change and jump linked groups") && ok;
	ok = check(linked3.flyMagic == nullptr
		&& linked3.parasiticMagic == nullptr
		&& linked3.randMagic == nullptr
		&& linked3.secondMagic == nullptr
		&& linked3.magicWhenNewPosition == nullptr
		&& linked3.magicToUseWhenKillEnemy == nullptr
		&& linked3.bounceFlyEndMagic == nullptr
		&& linked3.changeMagic == nullptr
		&& linked3.jumpEndMagic == nullptr
		&& linked3.flyInterval == 0
		&& linked3.parasitic == 0
		&& linked3.randMagicProbability == 0
		&& linked3.secondMagicDelay == 0,
		"explicit empty Level3 linked fields disable that level without affecting AttackFile") && ok;
	ok = check(linked4.flyMagic == linked1.flyMagic
		&& linked4.parasiticMagic == linked1.parasiticMagic
		&& linked4.randMagic == linked1.randMagic
		&& linked4.secondMagic == linked1.secondMagic
		&& linked4.magicWhenNewPosition == linked1.magicWhenNewPosition
		&& linked4.magicToUseWhenKillEnemy == linked1.magicToUseWhenKillEnemy
		&& linked4.bounceFlyEndMagic == linked1.bounceFlyEndMagic
		&& linked4.changeMagic == linked1.changeMagic
		&& linked4.jumpEndMagic == linked1.jumpEndMagic
		&& linked4.flyInterval == 100
		&& linked4.secondMagicDelay == 400,
		"Level4 falls back to Init instead of inheriting Level2 linked overrides") && ok;
	ok = check(&parent.getLinkedLevel(0) == &parent.getLinkedLevel(1)
		&& parent.getLinkedLevel(MAGIC_MAX_LEVEL + 1).flyMagic == linked1.flyMagic,
		"linked level lookup clamps below and above the supported level range") && ok;
	ok = check(level2Child != nullptr && level2Child->experienceOwnerMagicFile == "parent.ini",
		"explode child inherits the parent experience owner") && ok;
	ok = check(linked2.flyMagic->experienceOwnerMagicFile == "parent.ini"
		&& linked2.parasiticMagic->experienceOwnerMagicFile == "parent.ini"
		&& linked2.jumpEndMagic->experienceOwnerMagicFile == "parent.ini"
		&& linked2.randMagic->experienceOwnerMagicFile == "alternate_child.ini"
		&& linked2.secondMagic->experienceOwnerMagicFile == "alternate_child.ini"
		&& linked2.changeMagic->experienceOwnerMagicFile == "alternate_child.ini",
		"experience ownership propagates only to reference-supported child groups") && ok;

	copiedParent.copy(parent);
	parent.freeResource();
	ok = check(copiedParent.getExplodeMagicForLevel(2) != nullptr,
		"Magic::copy keeps per-level explode children alive after the source releases resources") && ok;
	ok = check(copiedParent.getLinkedLevel(2).flyMagic != nullptr
		&& copiedParent.getLinkedLevel(2).secondMagic != nullptr
		&& copiedParent.getLinkedLevel(3).flyMagic == nullptr,
		"Magic::copy keeps per-level linked children and explicit disables") && ok;
	return ok;
}

bool runEffectReferencePersistenceTest(GameManager& gameManager)
{
	auto mapNpcA = std::make_shared<NPC>();
	mapNpcA->kind = nkBattle;
	auto mapNpcB = std::make_shared<NPC>();
	mapNpcB->kind = nkBattle;
	auto partnerA = std::make_shared<NPC>();
	partnerA->kind = nkPartner;
	auto partnerB = std::make_shared<NPC>();
	partnerB->kind = nkPartner;

	gameManager.npcManager->npcList = { mapNpcA, partnerA, mapNpcB, partnerB };
	INIReader ini;
	Effect savedEffect;
	savedEffect.level = 1;
	savedEffect.user = partnerB;
	savedEffect.target = mapNpcB;
	savedEffect.saveToIni(&ini, "PRO1");

	bool ok = true;
	ok = check(ini.GetInteger("PRO1", "UserReferenceKind", 0) == 2
		&& ini.GetInteger("PRO1", "UserReferenceIndex", -1) == 1,
		"partner caster is saved by partner ordinal") && ok;
	ok = check(ini.GetInteger("PRO1", "TargetReferenceKind", 0) == 3
		&& ini.GetInteger("PRO1", "TargetReferenceIndex", -1) == 1,
		"map target is saved by persisted NPC ordinal") && ok;

	gameManager.npcManager->npcList = { partnerA, partnerB, mapNpcA, mapNpcB };
	Effect loadedEffect;
	loadedEffect.initFromIni(&ini, "PRO1");
	ok = check(loadedEffect.user.lock() == partnerB,
		"partner caster survives the partner-first load ordering") && ok;
	ok = check(loadedEffect.target.lock() == mapNpcB,
		"map target survives the partner-first load ordering") && ok;

	Effect savedPlayerEffect;
	savedPlayerEffect.level = 1;
	savedPlayerEffect.user = gameManager.player;
	savedPlayerEffect.target = partnerA;
	savedPlayerEffect.saveToIni(&ini, "PRO2");
	Effect loadedPlayerEffect;
	loadedPlayerEffect.initFromIni(&ini, "PRO2");
	ok = check(loadedPlayerEffect.user.lock() == gameManager.player,
		"player caster survives Effect save/load") && ok;
	ok = check(loadedPlayerEffect.target.lock() == partnerA,
		"partner target survives Effect save/load") && ok;

	gameManager.npcManager->npcList.clear();
	return ok;
}

bool runExplodeMagicLevelDispatchTest(GameManager& gameManager, Magic& copiedParent)
{
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level1_has_explode", 1);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level2_has_explode", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level3_has_explode", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasExplodeMagic", "level4_has_explode", 4);
	bool ok = true;
	ok = check(gameManager.varList.getInteger("level1_has_explode") == 1
		&& gameManager.varList.getInteger("level2_has_explode") == 1
		&& gameManager.varList.getInteger("level3_has_explode") == 0
		&& gameManager.varList.getInteger("level4_has_explode") == 1,
		"GetMagicState reports per-level ExplodeMagicFile availability") && ok;

	auto parentMagic = std::make_shared<Magic>();
	parentMagic->copy(copiedParent);
	auto dispatchAtLevel = [&](int level, const std::string& expectedChildFile, bool shouldDispatch)
	{
		size_t beforeCount = gameManager.effectManager->effectList.size();
		auto parentEffect = std::make_shared<Effect>();
		parentEffect->level = level;
		parentEffect->user = gameManager.player;
		parentEffect->position = { 5, 5 };
		parentEffect->src = parentEffect->position;
		parentEffect->initFromMagic(parentMagic);
		parentEffect->beginExplode(parentEffect->position);
		size_t afterCount = gameManager.effectManager->effectList.size();
		if (!shouldDispatch)
		{
			return afterCount == beforeCount;
		}
		return afterCount == beforeCount + 1
			&& gameManager.effectManager->effectList.back() != nullptr
			&& gameManager.effectManager->effectList.back()->magic.iniName == expectedChildFile;
	};

	ok = check(dispatchAtLevel(2, "alternate_child.ini", true),
		"Level2 dispatches its alternate explode child") && ok;
	ok = check(dispatchAtLevel(3, "", false),
		"explicit empty Level3 dispatches no explode child") && ok;
	ok = check(dispatchAtLevel(4, "base_child.ini", true),
		"Level4 dispatches the Init explode child") && ok;
	return ok;
}

bool runLinkedMagicLevelDispatchTest(GameManager& gameManager, Magic& copiedParent)
{
	auto originalMapData = gameManager.map->data;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 64;
	gameManager.map->data->head.height = 64;
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.getMagicState("parent.ini", "HasAttackFile", "level2_has_attack", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasAttackFile", "level3_has_attack", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasAttackFile", "level4_has_attack", 4);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasFlyMagic", "level2_has_fly", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasFlyMagic", "level3_has_fly", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "HasFlyMagic", "level4_has_fly", 4);
	gameManager.scriptAPI.getMagicState("parent.ini", "FlyInterval", "level2_fly_interval", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "FlyInterval", "level3_fly_interval", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "FlyInterval", "level4_fly_interval", 4);
	gameManager.scriptAPI.getMagicState("parent.ini", "BounceFly", "level2_bounce_fly", 2);
	gameManager.scriptAPI.getMagicState("parent.ini", "BounceFlySpeed", "level3_bounce_speed", 3);
	gameManager.scriptAPI.getMagicState("parent.ini", "MagicDirectionWhenBounceFlyEnd", "level4_bounce_direction", 4);
	bool ok = true;
	ok = check(gameManager.varList.getInteger("level2_has_attack") == 1
		&& gameManager.varList.getInteger("level3_has_attack") == 1
		&& gameManager.varList.getInteger("level4_has_attack") == 1,
		"GetMagicState follows AttackFile preserve-on-empty and preserve-on-invalid semantics") && ok;
	ok = check(gameManager.varList.getInteger("level2_has_fly") == 1
		&& gameManager.varList.getInteger("level3_has_fly") == 0
		&& gameManager.varList.getInteger("level4_has_fly") == 1
		&& gameManager.varList.getInteger("level2_fly_interval") == 110
		&& gameManager.varList.getInteger("level3_fly_interval") == 0
		&& gameManager.varList.getInteger("level4_fly_interval") == 100
		&& gameManager.varList.getInteger("level2_bounce_fly") == 3
		&& gameManager.varList.getInteger("level3_bounce_speed") == 30
		&& gameManager.varList.getInteger("level4_bounce_direction") == 1,
		"GetMagicState exposes per-level Fly and Bounce companion values") && ok;

	auto parentMagic = std::make_shared<Magic>();
	parentMagic->copy(copiedParent);
	parentMagic->linkedLevel[2].jumpToTarget = 0;
	parentMagic->linkedLevel[3].jumpToTarget = 0;
	parentMagic->linkedLevel[4].jumpToTarget = 0;
	Point from = { 20, 20 };
	Point to = Map::getSubPoint(from, 0);
	auto castAtLevel = [&](int level, const std::string& expectedChild, int expectedEffectDelta, int expectedDelayedDelta)
	{
		const size_t effectCount = gameManager.effectManager->effectList.size();
		const size_t delayedCount = gameManager.effectManager->getPendingDelayedMagicCount();
		Magic::addEffect(parentMagic, gameManager.player, from, to, level, 1, 0, lkSelf, nullptr);
		const size_t actualEffectDelta = gameManager.effectManager->effectList.size() - effectCount;
		const size_t actualDelayedDelta = gameManager.effectManager->getPendingDelayedMagicCount() - delayedCount;
		const bool countsMatch = actualEffectDelta == static_cast<size_t>(expectedEffectDelta)
			&& actualDelayedDelta == static_cast<size_t>(expectedDelayedDelta);
		if (expectedEffectDelta <= 1)
		{
			return countsMatch;
		}
		return countsMatch
			&& gameManager.effectManager->effectList.back() != nullptr
			&& gameManager.effectManager->effectList.back()->magic.iniName == expectedChild;
	};
	ok = check(castAtLevel(2, "alternate_child.ini", 2, 1),
		"Level2 dispatches its RandMagic and schedules its SecondMagic") && ok;
	ok = check(castAtLevel(3, "", 1, 0),
		"explicit empty Level3 dispatches neither RandMagic nor SecondMagic") && ok;
	ok = check(castAtLevel(4, "base_child.ini", 2, 1),
		"Level4 dispatches Init RandMagic and SecondMagic instead of Level2 overrides") && ok;
	INIReader savedManager;
	gameManager.effectManager->saveToIni(savedManager);
	const int delayedCount = static_cast<int>(savedManager.GetInteger("Head", "DelayedCount", 0));
	ok = check(delayedCount >= 2
		&& savedManager.Get("Delayed1", "MagicFile", "") == "alternate_child.ini"
		&& savedManager.Get("Delayed" + std::to_string(delayedCount), "MagicFile", "") == "base_child.ini",
		"per-level SecondMagic selection survives the delayed dispatch queue") && ok;

	auto changeParent = std::make_shared<Magic>();
	changeParent->copy(copiedParent);
	changeParent->linkedLevel[2].jumpToTarget = 0;
	changeParent->linkedLevel[2].randMagic = nullptr;
	changeParent->linkedLevel[2].secondMagic = nullptr;
	gameManager.player->changeMagicHitCounts[changeParent->iniName] = 3;
	const size_t changeEffectCount = gameManager.effectManager->effectList.size();
	Magic::addEffect(changeParent, gameManager.player, from, to, 2, 1, 0, lkSelf, nullptr);
	ok = check(gameManager.effectManager->effectList.size() == changeEffectCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini"
		&& gameManager.player->changeMagicHitCounts.find(changeParent->iniName) == gameManager.player->changeMagicHitCounts.end(),
		"Level2 ChangeMagic uses its per-level threshold and child, then consumes the hit counter") && ok;

	auto jumpParent = std::make_shared<Magic>();
	jumpParent->copy(copiedParent);
	jumpParent->linkedLevel[2].randMagic = nullptr;
	jumpParent->linkedLevel[2].secondMagic = nullptr;
	const auto jumpEffects = Magic::addEffect(
		jumpParent,
		gameManager.player,
		from,
		to,
		2,
		1,
		0,
		lkSelf,
		nullptr);
	ok = check(jumpEffects.empty()
		&& gameManager.player->magicForcedMove.active
		&& gameManager.player->magicForcedMove.speed == 41.0f
		&& gameManager.player->magicForcedMove.endMagic != nullptr
		&& gameManager.player->magicForcedMove.endMagic->iniName == "alternate_child.ini"
		&& gameManager.player->magicForcedMove.level == 2,
		"Level2 JumpToTarget uses its per-level speed and JumpEndMagic child") && ok;
	gameManager.player->clearMagicForcedMoveState();

	auto flyEffect = std::make_shared<Effect>();
	flyEffect->level = 2;
	flyEffect->user = gameManager.player;
	flyEffect->position = from;
	flyEffect->dest = to;
	flyEffect->initFromMagic(parentMagic);
	flyEffect->doing = ekFlying;
	const size_t flyEffectCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateFlyMagic(*flyEffect, 109);
	ok = check(gameManager.effectManager->effectList.size() == flyEffectCount,
		"Level2 FlyInterval waits for its per-level cadence") && ok;
	RageSystemTestAccess::updateFlyMagic(*flyEffect, 1);
	ok = check(gameManager.effectManager->effectList.size() == flyEffectCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 FlyMagic dispatches its per-level child at its per-level cadence") && ok;

	auto disabledFlyEffect = std::make_shared<Effect>();
	disabledFlyEffect->level = 3;
	disabledFlyEffect->user = gameManager.player;
	disabledFlyEffect->position = from;
	disabledFlyEffect->dest = to;
	disabledFlyEffect->initFromMagic(parentMagic);
	disabledFlyEffect->doing = ekFlying;
	const size_t disabledFlyCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateFlyMagic(*disabledFlyEffect, 1000);
	ok = check(gameManager.effectManager->effectList.size() == disabledFlyCount,
		"explicit empty Level3 FlyMagic suppresses runtime dispatch") && ok;

	auto positionEffect = std::make_shared<Effect>();
	positionEffect->level = 2;
	positionEffect->user = gameManager.player;
	positionEffect->initFromMagic(parentMagic);
	positionEffect->magicWhenNewPositionInitialized = true;
	positionEffect->magicWhenNewPositionLastTile = from;
	positionEffect->position = to;
	const size_t positionEffectCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateMagicWhenNewPosition(*positionEffect);
	ok = check(gameManager.effectManager->effectList.size() == positionEffectCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 MagicWhenNewPos dispatches its per-level child") && ok;

	auto disabledPositionEffect = std::make_shared<Effect>();
	disabledPositionEffect->level = 3;
	disabledPositionEffect->user = gameManager.player;
	disabledPositionEffect->initFromMagic(parentMagic);
	disabledPositionEffect->magicWhenNewPositionInitialized = true;
	disabledPositionEffect->magicWhenNewPositionLastTile = from;
	disabledPositionEffect->position = to;
	const size_t disabledPositionCount = gameManager.effectManager->effectList.size();
	RageSystemTestAccess::updateMagicWhenNewPosition(*disabledPositionEffect);
	ok = check(gameManager.effectManager->effectList.size() == disabledPositionCount,
		"explicit empty Level3 MagicWhenNewPos suppresses runtime dispatch") && ok;
	ok = check(flyEffect->canParasitic() && !disabledFlyEffect->canParasitic(),
		"Parasitic activation follows the effect level") && ok;

	auto triggerEffect = std::make_shared<Effect>();
	triggerEffect->level = 2;
	triggerEffect->user = gameManager.player;
	triggerEffect->launcherKind = lkSelf;
	triggerEffect->initFromMagic(parentMagic);
	auto defeatedTarget = std::make_shared<NPC>();
	defeatedTarget->setPosition(to, false);
	const size_t killMagicCount = gameManager.effectManager->effectList.size();
	defeatedTarget->triggerMagicWhenKillEnemy(*triggerEffect);
	ok = check(gameManager.effectManager->effectList.size() == killMagicCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 MagicToUseWhenKillEnemy dispatches its per-level child") && ok;
	triggerEffect->level = 3;
	const size_t disabledKillMagicCount = gameManager.effectManager->effectList.size();
	defeatedTarget->triggerMagicWhenKillEnemy(*triggerEffect);
	ok = check(gameManager.effectManager->effectList.size() == disabledKillMagicCount,
		"explicit empty Level3 MagicToUseWhenKillEnemy suppresses runtime dispatch") && ok;

	auto bounceTarget = std::make_shared<NPC>();
	bounceTarget->setPosition(to, false);
	triggerEffect->level = 2;
	triggerEffect->flyingDirection = { 1, 0 };
	bounceTarget->applyBounceFlyFromEffect(*triggerEffect);
	ok = check(bounceTarget->magicForcedMove.active
		&& bounceTarget->magicForcedMove.speed == 31.0f
		&& bounceTarget->magicForcedMove.endMagic != nullptr
		&& bounceTarget->magicForcedMove.endMagic->iniName == "alternate_child.ini"
		&& bounceTarget->magicForcedMove.endDirectionMode == 2
		&& bounceTarget->magicForcedMove.endHurt == 7
		&& bounceTarget->magicForcedMove.touchHurt == 8
		&& bounceTarget->magicForcedMove.touchDistance == 3,
		"Level2 BounceFly uses its per-level child and movement companions") && ok;
	bounceTarget->clearMagicForcedMoveState();
	triggerEffect->level = 3;
	bounceTarget->applyBounceFlyFromEffect(*triggerEffect);
	ok = check(!bounceTarget->magicForcedMove.active,
		"explicit zero Level3 BounceFly suppresses forced movement") && ok;

	auto movingBounceTarget = std::make_shared<NPC>();
	Point movingBouncePosition = { to.x + 4, to.y + 4 };
	movingBounceTarget->setPosition(movingBouncePosition, false);
	movingBounceTarget->stepList.push_back({ movingBouncePosition.x, movingBouncePosition.y - 1 });
	movingBounceTarget->actionManager->forceChangeAction(acWalk);
	movingBounceTarget->setOffset({ 7.0f, -3.0f });
	Effect bounceEffect;
	bounceEffect.magic.bounce = 120;
	bounceEffect.flyingDirection = { 0, -1 };
	bounceEffect.user = gameManager.player;
	bounceEffect.launcherKind = lkSelf;
	movingBounceTarget->applyBounceFromEffect(bounceEffect);
	ok = check(movingBounceTarget->actionManager->getCurrentActionType() == acBounce
		&& movingBounceTarget->getOffset().x == 7.0f
		&& movingBounceTarget->getOffset().y == -3.0f,
		"Bounce preserves the in-progress world offset instead of snapping to the tile center") && ok;

	auto movingBounceFlyTarget = std::make_shared<NPC>();
	Point movingBounceFlyPosition = { to.x + 8, to.y + 8 };
	movingBounceFlyTarget->setPosition(movingBounceFlyPosition, false);
	movingBounceFlyTarget->stepList.push_back({ movingBounceFlyPosition.x, movingBounceFlyPosition.y - 1 });
	movingBounceFlyTarget->actionManager->forceChangeAction(acWalk);
	movingBounceFlyTarget->setOffset({ -5.0f, 4.0f });
	triggerEffect->level = 2;
	movingBounceFlyTarget->applyBounceFlyFromEffect(*triggerEffect);
	ok = check(movingBounceFlyTarget->magicForcedMove.active
		&& movingBounceFlyTarget->magicForcedMove.startOffset.x == -5.0f
		&& movingBounceFlyTarget->magicForcedMove.startOffset.y == 4.0f
		&& movingBounceFlyTarget->getOffset().x == -5.0f
		&& movingBounceFlyTarget->getOffset().y == 4.0f,
		"BounceFly starts its Bezier move at the in-progress world offset") && ok;

	const int practiceIndex = gameManager.magicManager.practiceIndex();
	gameManager.magicManager.magicList.clear();
	gameManager.magicManager.magicList.resize(static_cast<size_t>(practiceIndex + 1));
	gameManager.magicManager.magicList[practiceIndex].magic = parentMagic;
	gameManager.magicManager.magicList[practiceIndex].iniFile = parentMagic->iniName;
	gameManager.magicManager.magicList[practiceIndex].level = 1;
	gameManager.player->attackLevel = 2;
	const size_t level2AttackCount = gameManager.effectManager->effectList.size();
	ok = check(gameManager.player->doSpecialAttack(to)
		&& gameManager.effectManager->effectList.size() == level2AttackCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "alternate_child.ini",
		"Level2 special attack dispatches its AttackFile override") && ok;
	gameManager.player->attackLevel = 3;
	const size_t level3AttackCount = gameManager.effectManager->effectList.size();
	ok = check(gameManager.player->doSpecialAttack(to)
		&& gameManager.effectManager->effectList.size() == level3AttackCount + 1
		&& gameManager.effectManager->effectList.back()->magic.iniName == "base_child.ini",
		"explicit empty Level3 AttackFile preserves and dispatches the Init attack child") && ok;
	gameManager.map->data = originalMapData;
	return ok;
}

bool runDerivedExperienceTest(GameManager& gameManager, const Magic& copiedParent)
{
	auto childMagic = copiedParent.getExplodeMagicForLevel(2);
	if (childMagic == nullptr)
	{
		return check(false, "derived experience test has a Level2 child");
	}

	gameManager.magicManager.magicList.assign(
		static_cast<size_t>(gameManager.magicManager.listLength()), MagicInfo());
	const int currentUseIndex = gameManager.magicManager.bottomIndex(0);
	MagicInfo parentInfo;
	parentInfo.iniFile = "PARENT.INI";
	parentInfo.level = 1;
	parentInfo.magic = std::make_shared<Magic>();
	parentInfo.magic->level[1].levelupExp = 1000;
	gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)] = parentInfo;

	auto childEffect = std::make_shared<Effect>();
	childEffect->level = 2;
	childEffect->user = gameManager.player;
	childEffect->initFromMagic(childMagic);
	INIReader savedEffect;
	childEffect->saveToIni(&savedEffect, "PRO1");
	auto loadedChildEffect = std::make_shared<Effect>();
	loadedChildEffect->initFromIni(&savedEffect, "PRO1");
	bool ok = true;
	ok = check(loadedChildEffect->magic.experienceOwnerMagicFile == "parent.ini",
		"derived experience owner survives Effect save/load") && ok;
	gameManager.magicManager.addUseExp(loadedChildEffect, 7);
	ok = check(gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 7,
		"saved derived explode hit experience is credited to the parent magic case-insensitively") && ok;
	gameManager.magicManager.addHitExp(loadedChildEffect, 2);
	ok = check(gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 13,
		"configured target-level factor is credited to the effect owner") && ok;

	gameManager.magicManager.recordCurrentUseMagic(currentUseIndex);
	auto ordinaryAttackEffect = std::make_shared<Effect>();
	ordinaryAttackEffect->magic.iniName = "ordinary_attack.ini";
	gameManager.magicManager.addHitExp(ordinaryAttackEffect, 2);
	gameManager.magicManager.addKillExp(ordinaryAttackEffect, 100);
	ok = check(gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 22,
		"ordinary attack hit and kill experience uses the last magic and configured fraction") && ok;
	return ok;
}

bool runExperienceSaturationTest(GameManager& gameManager)
{
	const int savedPlayerExperience = gameManager.player->exp;
	const int savedPlayerLevel = gameManager.player->level;
	const int savedPlayerLevelUpExperience =
		gameManager.player->levelUpExp;
	const std::vector<LevelInfo> savedPlayerLevels =
		gameManager.player->levelList;
	LevelInfo maximumThresholdLevel;
	maximumThresholdLevel.levelUpExp =
		std::numeric_limits<int>::max();
	gameManager.player->levelList = { maximumThresholdLevel };
	gameManager.player->level = 1;
	gameManager.player->levelUpExp =
		std::numeric_limits<int>::max();
	gameManager.player->exp =
		std::numeric_limits<int>::max() - 4;
	gameManager.player->addExp(10);
	bool ok = check(
		gameManager.player->exp
			== std::numeric_limits<int>::max(),
		"player experience saturates at the integer maximum");
	gameManager.player->exp =
		std::numeric_limits<int>::min() + 4;
	gameManager.player->addExp(-10);
	ok = check(
		gameManager.player->exp
			== std::numeric_limits<int>::min(),
		"negative player experience changes saturate without signed underflow") && ok;
	gameManager.player->exp = savedPlayerExperience;
	gameManager.player->level = savedPlayerLevel;
	gameManager.player->levelUpExp = savedPlayerLevelUpExperience;
	gameManager.player->levelList = savedPlayerLevels;

	gameManager.magicManager.configureLayout();
	if (gameManager.menu->practiceMenu == nullptr)
	{
		gameManager.menu->practiceMenu =
			std::make_shared<PracticeMenu>();
	}
	auto experienceMagic = std::make_shared<Magic>();
	experienceMagic->initFromIni("parent.ini");
	if (!check(
		experienceMagic->loadSucceeded,
		"experience saturation fixture Magic loads"))
	{
		return false;
	}

	const int practiceIndex =
		gameManager.magicManager.practiceIndex();
	MagicInfo practiceInfo;
	practiceInfo.iniFile = "practice-saturation.ini";
	practiceInfo.level = MAGIC_MAX_LEVEL;
	practiceInfo.exp = std::numeric_limits<int>::max() - 4;
	practiceInfo.magic = experienceMagic;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)] = practiceInfo;
	gameManager.magicManager.addPracticeExp(10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(practiceIndex)].exp
			== std::numeric_limits<int>::max(),
		"practice Magic experience saturates at the integer maximum") && ok;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)].exp =
		std::numeric_limits<int>::min() + 4;
	gameManager.magicManager.addPracticeExp(-10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(practiceIndex)].exp
			== std::numeric_limits<int>::min(),
		"practice Magic experience saturates without signed underflow") && ok;

	const int useIndex = gameManager.magicManager.bottomIndex(0);
	MagicInfo useInfo;
	useInfo.iniFile = "use-saturation.ini";
	useInfo.level = MAGIC_MAX_LEVEL;
	useInfo.exp = std::numeric_limits<int>::max() - 4;
	useInfo.magic = experienceMagic;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(useIndex)] = useInfo;
	auto useEffect = std::make_shared<Effect>();
	useEffect->magic.experienceOwnerMagicFile =
		"use-saturation.ini";
	gameManager.magicManager.addUseExp(useEffect, 10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(useIndex)].exp
			== std::numeric_limits<int>::max(),
		"used Magic experience saturates at the integer maximum") && ok;
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(useIndex)].exp =
		std::numeric_limits<int>::min() + 4;
	gameManager.magicManager.addMagicExp(
		"use-saturation.ini", -10);
	ok = check(
		gameManager.magicManager.magicList[
			static_cast<std::size_t>(useIndex)].exp
			== std::numeric_limits<int>::min(),
		"scripted Magic experience saturates without signed underflow") && ok;
	return ok;
}

bool runResourceConfiguredMinimumMagicDamageTest(GameManager& gameManager)
{
	const int originalMinimumMagicDamage =
		gameManager.global.minimumMagicDamage;
	NPC target;
	target.defend = 100;
	target.defend2 = 100;
	target.defend3 = 100;
	auto effect = std::make_shared<Effect>();
	effect->damage = 1;
	effect->damage2 = 1;
	effect->damage3 = 1;

	bool ok = true;
	for (int gameType : { GAME_JXQY2, GAME_YYCS, GAME_XJXQY, GAME_CUSTOM })
	{
		ResourceManifest behavior;
		behavior.type = gameType;
		behavior.typeDefined = true;
		behavior.minimumMagicDamage = 5;
		behavior.minimumMagicDamageDefined = true;
		gameManager.global.applyResourceManifestFeatures(behavior);
		ok = check(
			target.calculateEffectDamage(effect) == 5,
			"resource-configured minimum magic damage is independent from game type") && ok;
	}
	gameManager.global.minimumMagicDamage = 10;
	ok = check(
		target.calculateEffectDamage(effect) == 10,
		"resource configuration can restore a minimum magic damage of ten") && ok;

	target.defend = 3;
	target.defend2 = 4;
	target.defend3 = 10;
	effect->damage = 20;
	effect->damage2 = 9;
	effect->damage3 = 8;
	gameManager.global.minimumMagicDamage = 5;
	ok = check(
		target.calculateEffectDamage(effect) == 22,
		"configured minimum does not replace a larger calculated damage") && ok;
	gameManager.global.minimumMagicDamage = originalMinimumMagicDamage;
	return ok;
}

bool runResourceConfiguredMagicEffectCalculationTest(GameManager& gameManager)
{
	const MagicEffectCalculationMode originalMode =
		gameManager.global.magicEffectCalculationMode;
	const int originalAttack = gameManager.player->attack;
	gameManager.player->attack = 160;
	const int effectiveAttack = gameManager.player->getAttack();
	auto magic = std::make_shared<Magic>();
	magic->level[1].effect = 80;

	gameManager.global.magicEffectCalculationMode =
		MagicEffectCalculationMode::ReplaceAttack;
	bool ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == 80,
		"replace mode uses the player's configured magic effect");

	gameManager.global.magicEffectCalculationMode =
		MagicEffectCalculationMode::AddToAttack;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == effectiveAttack + 80,
		"additive mode combines the player's attack and magic effect") && ok;
	magic->level[1].effect = -220;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == effectiveAttack - 220,
		"additive mode preserves signed higher-level magic adjustments") && ok;
	magic->level[1].effect = 0;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == effectiveAttack,
		"zero magic effect continues to use the player's attack") && ok;

	magic->level[1].effect = 80;
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskAddLife;
	ok = check(
		Magic::calculatePrimaryEffectAmount(
			magic, gameManager.player, 1) == 80,
		"self healing keeps its explicit effect in additive mode") && ok;

	auto enemy = std::make_shared<NPC>();
	enemy->kind = nkBattle;
	enemy->attack = 75;
	magic->level[1].moveKind = mmkPoint;
	magic->level[1].specialKind = 0;
	ok = check(
		Magic::calculatePrimaryEffectAmount(magic, enemy, 1) == 75,
		"NPC magic continues to use the NPC attack in additive mode") && ok;

	gameManager.player->attack = originalAttack;
	gameManager.global.magicEffectCalculationMode = originalMode;
	return ok;
}

bool runSelfLifeExchangeTest(GameManager& gameManager)
{
	auto player = gameManager.player;
	const PlayerInfo originalInfo = player->info;
	const int originalLife = player->life;
	const int originalMana = player->mana;
	const int originalInvincible = player->invincible;
	const NPCActionType originalAction = player->nowAction;
	const auto originalStateMenu = gameManager.menu->stateMenu;
	if (gameManager.menu->stateMenu == nullptr)
	{
		gameManager.menu->stateMenu = std::make_shared<StateMenu>();
	}

	player->info.lifeMax = 2000;
	player->info.manaMax = 1000;
	player->life = 600;
	player->mana = 0;
	player->invincible = 1;
	player->nowAction = NPCActionType::acStand;

	auto magic = std::make_shared<Magic>();
	magic->level[1].moveKind = mmkSelf;
	magic->level[1].specialKind = mskAddLife;
	magic->level[1].effect = -500;
	magic->level[1].manaCost = -60;

	player->mana = 980;
	bool ok = check(player->tryConsumeMagicCost(magic, 1, false)
		&& player->mana == 1000,
		"negative ManaCost restoration clamps at the player's mana maximum");
	player->mana = 0;
	ok = check(player->tryConsumeMagicCost(magic, 1, false)
		&& player->mana == 60,
		"negative ManaCost restores mana for a life-exchange skill") && ok;
	const Point position = player->getPosition();
	const auto firstEffects = Magic::addEffect(
		magic, player, position, position, 1, -500, 0, lkSelf, player);
	ok = check(firstEffects.size() == 1 && player->life == 100,
		"negative self life effect applies its exact value without damage scaling or invincibility") && ok;

	player->life = 400;
	const auto secondEffects = Magic::addEffect(
		magic, player, position, position, 1, -500, 0, lkSelf, player);
	ok = check(secondEffects.size() == 1 && player->life == 0
		&& player->nowAction != NPCActionType::acDeath,
		"life-exchange self effect clamps at zero without starting player death") && ok;

	player->info = originalInfo;
	player->life = originalLife;
	player->mana = originalMana;
	player->invincible = originalInvincible;
	player->nowAction = originalAction;
	gameManager.menu->stateMenu = originalStateMenu;
	return ok;
}
}

bool runMagicDerivedRuntimeTests()
{
	auto root = makeUniqueTestDirectory("jxqy_magic_derived_runtime_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "magic", errorCode);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(MagicDerivedSaveNamespace);
	File::setPlatformStateParentForTests(root.string());

	Magic copiedParent;
	auto firstFallbackRoot = root / "dependency_fallback_first";
	auto secondFallbackRoot = root / "dependency_fallback_second";
	bool ok = runLinkedMagicGraphLoadingTest(root, firstFallbackRoot, secondFallbackRoot);
	ok = runExplodeMagicLevelLoadingTest(root, copiedParent) && ok;
	GameManager gameManager;
	ok = runResourceConfiguredMinimumMagicDamageTest(gameManager) && ok;
	ok = runResourceConfiguredMagicEffectCalculationTest(gameManager) && ok;
	ok = runSelfLifeExchangeTest(gameManager) && ok;
	ok = runLinkedMagicRuntimeBudgetTest(gameManager) && ok;
	ok = runRageSystemTest(gameManager, root) && ok;
	ok = runInsufficientResourceMessageTest(gameManager) && ok;
	ok = runExplodeMagicLevelDispatchTest(gameManager, copiedParent) && ok;
	ok = runLinkedMagicLevelDispatchTest(gameManager, copiedParent) && ok;
	ok = runEffectReferencePersistenceTest(gameManager) && ok;
	ok = runDerivedExperienceTest(gameManager, copiedParent) && ok;

	File::setPlatformStateParentForTests("");
	std::filesystem::remove_all(root, errorCode);
	return ok;
}

bool runMagicExperienceTests()
{
	auto root = makeUniqueTestDirectory("jxqy_magic_experience_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "magic", errorCode);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(MagicDerivedSaveNamespace);
	if (!prepareMagicFixtures(root))
	{
		std::filesystem::remove_all(root, errorCode);
		return check(false, "write magic experience fixtures");
	}

	Magic parent;
	parent.initFromIni("parent.ini");
	Magic copiedParent;
	copiedParent.copy(parent);
	GameManager gameManager;
	bool ok = runDerivedExperienceTest(gameManager, copiedParent);
	gameManager.menu->stateMenu = std::make_shared<StateMenu>();
	LevelInfo currentLevel;
	currentLevel.levelUpExp = 100;
	currentLevel.attack = gameManager.player->attack;
	currentLevel.attack2 = gameManager.player->attack2;
	currentLevel.attack3 = gameManager.player->attack3;
	currentLevel.defend = gameManager.player->defend;
	currentLevel.defend2 = gameManager.player->defend2;
	currentLevel.defend3 = gameManager.player->defend3;
	currentLevel.evade = gameManager.player->evade;
	currentLevel.lifeMax = gameManager.player->lifeMax;
	currentLevel.thewMax = gameManager.player->thewMax;
	currentLevel.manaMax = gameManager.player->manaMax;
	LevelInfo nextLevel = currentLevel;
	nextLevel.levelUpExp = 200;
	gameManager.player->levelList = { currentLevel, nextLevel };
	gameManager.menu->systemNotice = std::make_shared<SystemNotice>();
	gameManager.setCheatModeEnabled(false);
	const int disabledCheatExperience = gameManager.player->exp;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePlayerLevel)
			&& gameManager.player->exp == disabledCheatExperience
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：请先开启作弊模式",
		"disabled cheat actions preserve state and explain how to enable cheats") && ok;
	gameManager.setCheatModeEnabled(true);
	ok = check(gameManager.isCheatModeEnabled()
		&& gameManager.menu->systemNotice->currentMessage
			== u8"系统：作弊模式已开启",
		"cheat mode exposes a runtime-only explicit enable interface") && ok;
	gameManager.player->life = 100;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::ToggleInvincibility)
			&& gameManager.isCheatInvincibilityEnabled(),
		"cheat invincibility can be enabled explicitly") && ok;
	gameManager.player->addLife(-50);
	ok = check(gameManager.player->life == 100,
		"cheat invincibility blocks player damage") && ok;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::ToggleInvincibility)
			&& !gameManager.isCheatInvincibilityEnabled(),
		"cheat invincibility can be disabled for required story defeats") && ok;
	gameManager.player->addLife(-10);
	ok = check(gameManager.player->life < 100,
		"disabling cheat invincibility restores player damage") && ok;
	for (const auto& [thresholdMode, expectedExperience] :
		std::initializer_list<std::pair<LevelUpThresholdMode, int>>{
			{ LevelUpThresholdMode::GreaterThanOrEqual, 100 },
			{ LevelUpThresholdMode::GreaterThan, 101 } })
	{
		ResourceManifest behavior;
		behavior.levelUpThresholdMode = thresholdMode;
		behavior.levelUpThresholdModeDefined = true;
		gameManager.global.applyResourceManifestFeatures(behavior);
		gameManager.player->level = 1;
		gameManager.player->exp = 0;
		gameManager.player->levelUpExp = 100;
		ok = check(
			gameManager.performCheatAction(
				GameManager::CheatAction::IncreasePlayerLevel)
				&& gameManager.player->level == 2
				&& gameManager.player->exp == expectedExperience
				&& gameManager.player->levelUpExp == 200
				&& gameManager.menu->systemNotice->currentMessage
					== u8"系统：角色已提升至2级",
			"cheat player-level increase honors the configured threshold and triggers normal leveling") && ok;
	}
	const int maximumLevelExperience = gameManager.player->exp;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePlayerLevel)
			&& gameManager.player->level == 2
			&& gameManager.player->exp == maximumLevelExperience,
		"cheat player-level increase does nothing at the configured maximum level") && ok;

	ResourceManifest extendedInventory;
	extendedInventory.uiProfile = "YYCS";
	gameManager.global.applyResourceManifestFeatures(extendedInventory);
	gameManager.magicManager.configureLayout();
	gameManager.menu->practiceMenu = std::make_shared<PracticeMenu>();
	const int practiceIndex = gameManager.magicManager.practiceIndex();
	MagicInfo practiceInfo;
	practiceInfo.iniFile = "cheat-practice.ini";
	practiceInfo.level = 1;
	practiceInfo.exp = 25;
	practiceInfo.magic = std::make_shared<Magic>();
	practiceInfo.magic->name = "Cheat Practice";
	practiceInfo.magic->level[1].levelupExp = 100;
	practiceInfo.magic->level[2].levelupExp = 250;
	for (int level = 3; level < MAGIC_MAX_LEVEL; ++level)
	{
		practiceInfo.magic->level[level].levelupExp = 1000 + level;
	}
	gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)] = practiceInfo;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].level == 2
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].exp == 100
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：Cheat Practice已提升至2级",
		"cheat practice-magic increase adds required experience through normal leveling") && ok;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].level == 3
			&& gameManager.magicManager.magicList[
				static_cast<std::size_t>(practiceIndex)].exp == 250,
		"repeated cheat practice-magic increase accumulates experience for exactly the next threshold") && ok;
	auto& maximumLevelPracticeMagic = gameManager.magicManager.magicList[
		static_cast<std::size_t>(practiceIndex)];
	maximumLevelPracticeMagic.level = MAGIC_MAX_LEVEL;
	const int maximumLevelMagicExperience = maximumLevelPracticeMagic.exp;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& maximumLevelPracticeMagic.level == MAGIC_MAX_LEVEL
			&& maximumLevelPracticeMagic.exp == maximumLevelMagicExperience,
		"cheat practice-magic increase does nothing at maximum magic level") && ok;
	maximumLevelPracticeMagic.level = 1;
	maximumLevelPracticeMagic.exp = 25;
	maximumLevelPracticeMagic.magic->level[1].levelupExp = 0;
	ok = check(
		!gameManager.performCheatAction(
			GameManager::CheatAction::IncreasePracticeMagicLevel)
			&& maximumLevelPracticeMagic.level == 1
			&& maximumLevelPracticeMagic.exp == 25,
		"cheat practice-magic increase does not invent experience for a missing threshold") && ok;

	gameManager.player->info.lifeMax = 321;
	gameManager.player->info.manaMax = 222;
	gameManager.player->info.thewMax = 123;
	gameManager.player->life = 1;
	gameManager.player->mana = 2;
	gameManager.player->thew = 3;
	ok = check(
		gameManager.performCheatAction(
			GameManager::CheatAction::RestorePlayerResources)
			&& gameManager.player->life == 321
			&& gameManager.player->mana == 222
			&& gameManager.player->thew == 123
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：生命、内力和体力已补满",
		"cheat resource restoration reports the completed operation") && ok;
	gameManager.menu->goodsMenu = std::make_shared<GoodsMenu>();
	gameManager.player->money = 456;
	ok = check(
		gameManager.performCheatAction(GameManager::CheatAction::AddMoney)
			&& gameManager.player->money == 100456
			&& gameManager.menu->systemNotice->currentMessage
				== u8"系统：已增加100000两银子，当前银两：100456",
		"cheat money grant reports the actual resulting balance") && ok;
	ok = runExperienceSaturationTest(gameManager) && ok;
	const std::string zeroHitExperience =
		"[HitMagicExp]\n"
		"LevelFactor=0\n"
		"[XiuLianMagicExp]\n"
		"Fraction=1.0\n"
		"[UseMagicExp]\n"
		"Fraction=1.0\n";
	if (!writeTextFile(root / "ini" / "level" / "MagicExp.ini", zeroHitExperience))
	{
		ok = check(false, "write zero-hit magic experience fixture") && ok;
	}
	else
	{
		gameManager.magicManager.configureLayout();
		const int currentUseIndex = gameManager.magicManager.bottomIndex(0);
		MagicInfo currentUseInfo;
		currentUseInfo.iniFile = "current.ini";
		currentUseInfo.level = 1;
		currentUseInfo.magic = std::make_shared<Magic>();
		currentUseInfo.magic->level[1].levelupExp = 1000;
		gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)] = currentUseInfo;
		gameManager.magicManager.recordCurrentUseMagic(currentUseIndex);

		auto ordinaryAttackEffect = std::make_shared<Effect>();
		ordinaryAttackEffect->magic.iniName = "ordinary_attack.ini";
		gameManager.magicManager.addHitExp(ordinaryAttackEffect, 10);
		gameManager.magicManager.addKillExp(ordinaryAttackEffect, 100);
		ok = check(
			gameManager.magicManager.magicList[static_cast<size_t>(currentUseIndex)].exp == 100,
			"zero hit factor awards no hit experience while full kill fraction remains active") && ok;
	}
	std::filesystem::remove_all(root, errorCode);
	return ok;
}
