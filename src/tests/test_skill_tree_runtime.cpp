#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Data/MagicManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>

namespace
{
bool check(bool passed, const char* message)
{
	if (!passed) std::cerr << "FAILED: " << message << '\n';
	return passed;
}

bool write(const std::filesystem::path& path, const std::string& contents)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << contents;
	return out.good();
}
}

bool runSkillTreeRuntimeTests()
{
	const auto root = makeUniqueTestDirectory("jxqy_skill_tree");
	std::filesystem::create_directories(root / "state");
	File::setAssetsCollectionRoot((root / "assets").u8string());
	File::setActiveResourceRoot(root.u8string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace("skill-tree-tests");
	File::setPlatformStateParentForTests((root / "state").u8string());
	std::ifstream source(std::filesystem::u8path(__FILE__).parent_path() / "fixtures/mg-yuemeier-skilltree.json", std::ios::binary);
	const std::string json((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
	SkillTree parsed;
	std::string reason;
	bool ok = check(parsed.parse(json, reason) && parsed.groups.size() == 1 && parsed.nodes.size() == 7,
		"parse the actual MG Yuemeier seven-node skill tree")
		&& check(parsed.nodes[1].parents == std::vector<std::size_t>{ 0 }
			&& parsed.nodes[1].requiredSkills == std::vector<std::size_t>{ 0 }
			&& parsed.nodes[6].requiredSkills == std::vector<std::size_t>{ 2 },
			"resolve both branches from authored IDs without relying on array order");
	if (!ok) return false;
	{
		struct CommaDecimalPoint : std::numpunct<char>
		{
			char do_decimal_point() const override
			{
				return ',';
			}
		};
		const auto previousLocale = std::locale();
		std::locale::global(std::locale(previousLocale, new CommaDecimalPoint()));
		std::string decimal = json;
		decimal.replace(decimal.find("\"X\": 49.0"), std::string("\"X\": 49.0").size(), "\"X\": 1.25e-1");
		SkillTree localized;
		const bool accepted = localized.parse(decimal, reason);
		std::locale::global(previousLocale);
		ok = check(accepted && localized.nodes.front().x == 0.125f,
			"decimal and scientific coordinates stay locale-independent") && ok;
	}
	for (const auto& pair : { std::make_pair(std::string("\"skill003\""), std::string("\"missing\"")),
		std::make_pair(std::string("\"MaxLevel\": 10"), std::string("\"MaxLevel\": 0")),
		std::make_pair(std::string("\"RequiredLevel\": 5"), std::string("\"RequiredLevel\": 5.5")),
		std::make_pair(std::string("\"X\": 49.0"), std::string("\"X\": 1e40")),
		std::make_pair(std::string("\"X\": 49.0"), std::string("\"X\": 1e-1000")) })
	{
		std::string bad = json;
		bad.replace(bad.find(pair.first), pair.first.size(), pair.second);
		SkillTree rejected;
		ok = check(!rejected.parse(bad, reason) && rejected.nodes.empty() && !reason.empty(),
			"invalid references, levels and coordinates disable the entire definition") && ok;
	}
	ok = check(!parsed.parse("{\"Groups\":[],\"Groups\":[]}", reason), "reject duplicate JSON keys") && ok;
	ok = check(!parsed.parse("{\"Groups\":[{\"GroupName\":\"Bad\",\"Nodes\":[{\"Id\":\"a\",\"SkillFile\":\"../outside.ini\"}]}]}", reason),
		"reject magic paths that leave the resource root") && ok;
	ok = check(!parsed.parse("{\"Groups\":[{\"GroupName\":\"Bad\",\"Nodes\":[{\"Id\":\"a\",\"SkillFile\":\"a.ini\",\"ChildIds\":[\"b\",\"b\"]},{\"Id\":\"b\",\"SkillFile\":\"b.ini\"}]}]}", reason),
		"duplicate parent edges cannot inflate an investment total") && ok;
	ok = check(parsed.parse(std::string("\xEF\xBB\xBF") + json, reason), "accept UTF-8 BOM tree files") && ok;
	ok = check(parsed.parse(json, reason), "reload a valid definition after a rejected one") && ok;
	const int thresholds[7][9] = {
		{300,1000,2200,4200,7200,12000,20000,32000,50000},
		{300,1000,2200,4200,7200,12000,20000,32000,50000},
		{400,2000,4000,7000,10000,15000,30000,50000,80000},
		{200,800,1800,2800,3800,5000,6200,7400,8800},
		{80,136,184,241,308,414,823,1481,2666},
		{20,800,2000,3500,5000,6000,8000,11000,20000},
		{300,1000,2200,4200,7200,12000,20000,32000,50000}
	};
	for (std::size_t i = 0; i < parsed.nodes.size(); ++i)
	{
		std::string magic = "[Init]\nName=" + parsed.nodes[i].magicFile + "\nMoveKind=13\n";
		for (int level = 1; level <= 10; ++level)
		{
			magic += "[Level" + std::to_string(level) + "]\nEffect=5\nLevelupExp="
				+ std::to_string(level < 10 ? thresholds[i][level - 1] : 0) + "\n";
		}
		ok = check(write(root / "ini/magic" / std::filesystem::u8path(parsed.nodes[i].magicFile), magic),
			"write magic resources using the actual seven upgrade-threshold sequences") && ok;
	}
	const std::string settings = "[Init]\nTreeEnable=1\nPointEnable=0\n[TreeFiles]\n1=ini/other/skilltree1.json\n";
	ok = check(write(root / "ini/other/Skill.ini", settings)
		&& write(root / "ini/other/skilltree1.json", json), "write explicit character-one tree binding") && ok;
	GameManager game;
	game.global.loadUiSettings();
	game.global.data.characterIndex = 0;
	game.player->level = 3;
	game.player->skillPoints = 40;
	auto& manager = game.magicManager;
	ok = check(game.global.skillTreeEnabled && !game.global.skillPointsEnabled,
		"tree enable and point-award enable are independent") && ok;
	ok = check(manager.loadSkillTree(0, &reason) && manager.skillTree().nodes.empty(),
		"unmapped characters do not acquire another character's indexed tree") && ok;
	ok = check(manager.loadSkillTree(1, &reason) && manager.skillTree().nodes.size() == 7,
		"the requested incoming character selects its tree before global characterIndex changes") && ok;
	const auto beforeFailedRoleChange = manager;
	manager.loadSkillTree(0);
	manager = beforeFailedRoleChange;
	ok = check(manager.skillTree().nodes.size() == 7, "existing character-change rollback copies also retain the tree definition") && ok;
	const auto learn = [&](std::size_t index) { return manager.learnSkillTreeNode(parsed.nodes[index].id, &reason); };
	ok = check(!learn(3) && game.player->skillPoints == 40, "failed level gate does not spend a point") && ok;
	game.player->level = 4;
	ok = check(learn(3) && game.player->skillPoints == 39
		&& manager.findPrimaryMagic(parsed.nodes[3].magicFile)->level == 1,
		"level four unlocks Qingxinzhou and grants its real magic for one point") && ok;
	game.player->level = 10;
	ok = check(!learn(1) && game.player->skillPoints == 39,
		"a parent investment requirement is not treated as the purchase price") && ok;
	ok = check(learn(0) && learn(1) && learn(2) && learn(4) && game.player->skillPoints == 35,
		"both actual branches unlock and learn their prerequisites") && ok;
	ok = check(!learn(5) && !learn(6) && game.player->skillPoints == 35,
		"level fifteen and twenty nodes retain their authored character-level gates") && ok;
	game.player->level = 20;
	ok = check(learn(5) && learn(6), "learn both late branch endpoints") && ok;
	for (int level = 2; level <= 10; ++level)
	{
		ok = check(learn(0) && manager.findPrimaryMagic(parsed.nodes[0].magicFile)->level == level
			&& manager.findPrimaryMagic(parsed.nodes[0].magicFile)->exp == thresholds[0][level - 2],
			"each point upgrades exactly one level through the authored experience threshold") && ok;
	}
	const int afterUpgrades = game.player->skillPoints;
	ok = check(!learn(0) && game.player->skillPoints == afterUpgrades, "max-level purchase does not spend a point") && ok;
	manager.setMagicHidden(parsed.nodes[1].magicFile, true, false, false);
	ok = check(learn(1) && manager.isMagicHidden(parsed.nodes[1].magicFile)
		&& manager.skillTree().nodes[1].currentLevel == 2 && manager.findPrimaryMagic(parsed.nodes[1].magicFile) == nullptr,
		"hidden learned magic upgrades without adding a duplicate visible magic") && ok;
	write(root / "ini/magic/form.ini", "[Init]\nName=Replacement\n[Level1]\nLevelupExp=100\n[Level2]\n");
	manager.replaceMagicList("form.ini", "skill-tree-test-form");
	ok = check(manager.hasActiveReplaceMagicList() && learn(2) && manager.skillTree().nodes[2].currentLevel == 2
		&& manager.findMagic("form.ini") != nullptr && manager.findMagic("form.ini")->level == 1,
		"learning during a replacement form advances the original character's magic") && ok;
	manager.stopReplaceMagicList();
	const int savedPoints = game.player->skillPoints;
	ok = check(game.player->save(1) && manager.save(1), "save learned tree progress through existing player and magic files") && ok;
	manager.clearMagicList();
	game.player->skillPoints = 0;
	ok = check(game.player->load(1) && manager.load(1, &reason)
		&& game.player->skillPoints == savedPoints && manager.skillTree().nodes[0].currentLevel == 10
		&& manager.skillTree().nodes[1].currentLevel == 2 && manager.isMagicHidden(parsed.nodes[1].magicFile),
		"ordinary character load reconstructs node levels and restores the independent point balance") && ok;
	game.player->skillPoints = 0;
	ok = check(!learn(3) && !reason.empty() && manager.findPrimaryMagic(parsed.nodes[3].magicFile) != nullptr
		&& manager.findPrimaryMagic(parsed.nodes[3].magicFile)->level == 1,
		"zero points prevent an upgrade") && ok;
	game.player->skillPoints = 4;
	manager.clearMagicList();
	std::filesystem::remove(root / "ini/magic" / std::filesystem::u8path(parsed.nodes[0].magicFile));
	ok = check(!learn(0) && game.player->skillPoints == 4 && manager.findPrimaryMagic(parsed.nodes[0].magicFile) == nullptr,
		"missing magic resources neither grant a node nor spend points") && ok;
	std::string grouped = "{\"Groups\":[{\"GroupName\":\"Conditions\",\"Nodes\":["
		"{\"Id\":\"a\",\"SkillFile\":\"a.ini\",\"ChildIds\":[\"c\"]},"
		"{\"Id\":\"b\",\"SkillFile\":\"b.ini\"},"
		"{\"Id\":\"c\",\"SkillFile\":\"c.ini\",\"RequiredPoints\":2,\"RequiredTotalPoints\":3,\"RequiredSkillIds\":[\"b\"]}]}]}";
	ok = check(write(root / "ini/other/skilltree1.json", grouped), "write independent parent and group investment fixture") && ok;
	for (const auto& name : { "a.ini", "b.ini", "c.ini" })
		write(root / "ini/magic" / name, "[Init]\nName=Conditions\n[Level1]\nLevelupExp=100\n[Level2]\nLevelupExp=200\n[Level3]\n");
	ok = check(manager.loadSkillTree(1), "load independent investment conditions") && ok;
	manager.addPrimaryMagic("a.ini", false, false);
	manager.setPrimaryMagicLevel("a.ini", 3);
	ok = check(manager.skillTree().nodes[2].unlocked && !manager.learnSkillTreeNode("c", &reason)
		&& game.player->skillPoints == 4,
		"unlock requires unlocked prerequisites, but first learning requires learned prerequisites") && ok;
	manager.addPrimaryMagic("b.ini", false, false);
	ok = check(!manager.learnSkillTreeNode("a", &reason) && game.player->skillPoints == 4,
		"a magic's defined resource limit still applies when a tree permits more levels") && ok;
	manager.findPrimaryMagic("b.ini")->magic->level[1].levelupExp = 0;
	ok = check(!manager.learnSkillTreeNode("b", &reason) && game.player->skillPoints == 4,
		"a missing positive upgrade threshold does not spend a point") && ok;
	ok = check(manager.learnSkillTreeNode("c", &reason) && game.player->skillPoints == 3,
		"RequiredPoints and RequiredTotalPoints are gates; purchase still costs one") && ok;
	manager.clearMagicList();
	for (auto& info : manager.magicList)
	{
		info.iniFile = "occupied.ini";
		info.magic = std::make_shared<Magic>();
		info.level = 1;
	}
	ok = check(!manager.learnSkillTreeNode("a", &reason) && game.player->skillPoints == 3,
		"a full magic inventory does not spend points") && ok;
	write(root / "ini/other/skilltree1.json", "invalid");
	ok = check(!manager.loadSkillTree(1, &reason) && manager.skillTree().nodes.empty()
		&& game.player->skillPoints == 3, "broken optional JSON disables its tree without changing points or existing magic") && ok;
	const auto fallback = root / "fallback";
	write(fallback / "ini/other/skilltree1.json", json);
	std::filesystem::remove(root / "ini/other/skilltree1.json");
	File::setResourceFallbackRoots({ fallback.u8string() });
	ok = check(!manager.loadSkillTree(1), "tree definitions do not fall back to another package") && ok;
	write(root / "ini/other/Skill.ini", "[Init]\nTreeEnable=1\n");
	write(root / "ini/other/skilltree.json", json);
	game.global.loadUiSettings();
	ok = check(manager.loadSkillTree(0) && manager.skillTree().nodes.size() == 7,
		"without explicit mappings, the reference engine's plain filename remains supported") && ok;
	write(root / "ini/other/Skill.ini", "[Init]\nTreeEnable=1\n[TreeFiles]\n1=../outside.json\n");
	game.global.loadUiSettings();
	ok = check(manager.loadSkillTree(1) && manager.skillTree().nodes.empty(),
		"an invalid explicit role mapping never falls back to the shared plain tree") && ok;
	write(root / "ini/other/Skill.ini", "[Init]\nTreeEnable=0\n");
	game.global.loadUiSettings();
	ok = check(manager.loadSkillTree(1) && manager.skillTree().nodes.empty()
		&& game.global.skillTreeFiles.empty(), "resource settings reset tree rules and bindings") && ok;
	File::setResourceFallbackRoots({});
	File::setPlatformStateParentForTests("");
	std::error_code error;
	std::filesystem::remove_all(root, error);
	return ok;
}
