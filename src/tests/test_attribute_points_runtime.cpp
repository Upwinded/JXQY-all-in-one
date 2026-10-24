#include "../File/File.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "TestTemporaryDirectory.h"

#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>

namespace
{
struct CommaDecimalPoint : std::numpunct<char>
{
	char do_decimal_point() const override
	{
		return ',';
	}
};

bool check(bool passed, const char* message)
{
	if (!passed) std::cerr << "FAILED: " << message << '\n';
	return passed;
}

bool write(const std::filesystem::path& path, const std::string& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out << text;
	return out.good();
}
}

bool runAttributePointRuntimeTests()
{
	const auto root = makeUniqueTestDirectory("jxqy_attribute_points");
	std::filesystem::create_directories(root / "state");
	File::setAssetsCollectionRoot((root / "assets").u8string());
	File::setActiveResourceRoot(root.u8string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace("attribute-points-tests");
	File::setPlatformStateParentForTests((root / "state").u8string());
	const std::string settings = "[Init]\nEnable=1\nTotalRatio=10\nLifeRatio=10\nThewRatio=10\nManaRatio=10\n"
		"AttackRatio=2\nDefendRatio=2\nEvadeRatio=2\nCritChanceRatio=0.05\nCritDamageRatio=1\n";
	bool ok = check(write(root / "ini/other/Point.ini", settings), "write the actual MG allocation ratios");
	{
		const auto previousLocale = std::locale();
		std::locale::global(std::locale(previousLocale, new CommaDecimalPoint()));
		AttributePointRules localized;
		localized.load();
		std::locale::global(previousLocale);
		ok = check(localized.enabled && localized.ratios[static_cast<std::size_t>(PlayerAttribute::CriticalChance)] == 0.05f,
			"fractional critical rates stay locale-independent") && ok;
	}
	std::string levels = "[Head]\nLevels=8\n";
	for (int level = 1; level <= 8; ++level)
	{
		const std::string value = std::to_string(100 + level * 10);
		levels += "[Level" + std::to_string(level) + "]\nLevelUpExp=" + std::to_string(level * 200)
			+ "\nLifeMax=" + value + "\nThewMax=" + value + "\nManaMax=" + value
			+ "\nAttack=" + value + "\nDefend=" + value + "\nEvade=" + value + "\n";
	}
	ok = check(write(root / "ini/level/point-test.ini", levels), "write a deterministic real level table") && ok;
	{
		GameManager game;
		game.global.loadUiSettings();
		game.global.feature.rageSystem = false;
		game.global.skillPointsEnabled = true;
		game.global.skillPointLevelUpRatio = 1;
		game.global.skillPointMaximum = 100;
		auto& player = *game.player;
		player.levelIni = "point-test.ini";
		player.loadLevel(player.levelIni);
		player.setLevel(2);
		ok = check(player.attributePoints.available == 0 && player.skillPoints == 0,
			"explicit level setting and initial levels do not award either point currency") && ok;
		player.exp = 0;
		player.addExp(401);
		ok = check(player.level == 3 && player.attributePoints.available == 10 && player.skillPoints == 1,
			"one natural level awards ten attribute points independently of one skill point") && ok;
		player.life = 80;
		const int lifeMax = player.lifeMax;
		const int attack = player.attack;
		std::string reason;
		AttributePointAmounts allocation{};
		allocation.fill(1);
		ok = check(player.allocateAttributePoints(allocation, &reason) && reason.empty()
			&& player.attributePoints.available == 2 && player.skillPoints == 1
			&& player.lifeMax == lifeMax + 10 && player.attack == attack + 2 && player.life == 80,
			"confirming all eight attributes consumes eight points, applies actual ratios and does not heal or spend skill points") && ok;
		ok = check(std::abs(player.getCriticalChancePercent() - 0.35f) < 0.00001f && player.getCriticalDamagePercent() == 4,
			"fractional critical chance and damage affect the real critical calculation even without the rage UI") && ok;
		const auto committed = player.attributePoints;
		allocation.fill(0);
		allocation[0] = 3;
		ok = check(!player.allocateAttributePoints(allocation, &reason) && !reason.empty()
			&& player.attributePoints.available == committed.available && player.lifeMax == lifeMax + 10,
			"insufficient points reject the entire allocation before changing attributes") && ok;
		allocation[0] = -1;
		ok = check(!player.allocateAttributePoints(allocation, &reason), "negative allocations cannot refund committed points") && ok;
		allocation[0] = 1;
		game.inEvent = true;
		ok = check(!player.allocateAttributePoints(allocation, &reason) && player.attributePoints.available == 2,
			"script-owned input cannot commit an allocation") && ok;
		game.inEvent = false;
		player.addExp(200);
		ok = check(player.level == 4 && player.attributePoints.available == 12 && player.skillPoints == 2
			&& player.lifeMax == lifeMax + 20 && player.attack == attack + 12,
			"later natural growth preserves already-applied allocation bonuses") && ok;
		const std::string pointFile = SaveFileManager::CurrentPath() + "point.ini";
		const std::string otherCharacter = "[Player0]\nTotalPoint=7\nMarker=keep\n";
		ok = check(File::writeFileChecked(pointFile, otherCharacter.data(), static_cast<int>(otherCharacter.size())),
			"write another character's allocation section") && ok;
		ok = check(player.save(1), "normal player save persists allocation through the existing current-save path") && ok;
		INIReader saved(pointFile);
		ok = check(saved.GetInteger("Player0", "TotalPoint", 0) == 7 && saved.Get("Player0", "Marker", "") == "keep"
			&& saved.GetInteger("Player1", "TotalPoint", 0) == 12 && saved.GetInteger("Player1", "LifePoint", 0) == 1,
			"the sidecar preserves other characters and writes only the requested role") && ok;
		player.attributePoints = {};
		ok = check(player.load(1) && player.attributePoints.available == 12 && player.attributePoints.spent[0] == 1
			&& player.lifeMax == lifeMax + 20 && std::abs(player.getCriticalChancePercent() - 0.45f) < 0.00001f,
			"load restores points and fractional bonuses without applying base attribute gains a second time") && ok;
		AttributePoints other;
		other.load(0);
		ok = check(other.available == 7 && other.spent[0] == 0, "incoming role index owns its allocation state") && ok;
		{
			AttributePoints precise;
			precise.criticalChanceBonus = std::nextafter(0.05f, 1.0f);
			const auto previousLocale = std::locale();
			std::locale::global(std::locale(previousLocale, new CommaDecimalPoint()));
			const bool savedPrecisely = precise.save(2);
			std::locale::global(previousLocale);
			INIReader preciseIni(pointFile);
			const std::string bonus = preciseIni.Get("Player2", "CritChanceBonus", "");
			AttributePoints loadedPrecisely;
			loadedPrecisely.load(2);
			ok = check(savedPrecisely && bonus.find('.') != std::string::npos && bonus.find(',') == std::string::npos
				&& loadedPrecisely.criticalChanceBonus == precise.criticalChanceBonus,
				"fractional allocation saves use a fixed decimal point and round-trip every float digit") && ok;
		}
		const auto restored = player.attributePoints;
		player.lifeMax = INT_MAX;
		ok = check(!player.allocateAttributePoints(allocation, &reason) && player.attributePoints.available == restored.available,
			"attribute overflow rejects spending without a partial mutation") && ok;
		player.lifeMax = lifeMax + 20;
		player.attributePoints.available = INT_MAX - 1;
		player.updateLevel();
		ok = check(player.attributePoints.available == INT_MAX, "level rewards saturate instead of wrapping the point balance") && ok;
		player.attributePoints.criticalDamageBonus = INT_MAX;
		ok = check(player.getCriticalDamagePercent() == INT_MAX && player.applyCriticalDamage(1000, 0) == INT_MAX,
			"large critical allocations remain bounded in actual damage calculation") && ok;
		player.attributePoints = restored;
		for (const auto& pair : { std::make_pair("TotalRatio=10", "TotalRatio=-1"),
			std::make_pair("AttackRatio=2", "AttackRatio=1.5"), std::make_pair("CritChanceRatio=0.05", "CritChanceRatio=nan") })
		{
			std::string bad = settings;
			bad.replace(bad.find(pair.first), std::char_traits<char>::length(pair.first), pair.second);
			write(root / "ini/other/Point.ini", bad);
			game.global.attributePointRules.load();
			ok = check(!game.global.attributePointRules.enabled, "invalid ratios disable the whole allocation rule without stale values") && ok;
		}
		std::filesystem::create_directories(root / "unconfigured");
		File::setActiveResourceRoot((root / "unconfigured").u8string());
		File::setResourceFallbackRoots({ root.u8string() });
		game.global.attributePointRules.load();
		ok = check(!game.global.attributePointRules.enabled && !player.allocateAttributePoints(allocation, &reason)
			&& player.attributePoints.available == restored.available,
			"dependency configuration does not enable allocation and disabled rules preserve existing progress") && ok;
		ok = check(player.save(1) && player.load(1) && player.attributePoints.available == restored.available,
			"disabled rules still retain already-earned allocation progress through a normal save/load") && ok;
		player.attributePoints.criticalDamageBonus = 0;
		ok = check(player.getCriticalDamagePercent() == player.level,
			"a saved chance allocation keeps the normal critical damage basis when allocation is disabled") && ok;
		player.attributePoints.criticalChanceBonus = 0;
		player.attributePoints.criticalDamageBonus = 1;
		ok = check(player.getCriticalChancePercent() > 0,
			"a saved damage allocation keeps the normal critical chance basis when allocation is disabled") && ok;
		player.attributePoints = {};
		ok = check(player.save(3), "an unconfigured character can save without allocation progress") && ok;
		INIReader unconfigured(pointFile);
		ok = check(!unconfigured.HasSection("Player3") && unconfigured.GetInteger("Player0", "TotalPoint", 0) == 7,
			"zero progress with disabled rules does not create a new role section or change other roles") && ok;
	}
	File::setPlatformStateParentForTests("");
	std::error_code error;
	std::filesystem::remove_all(root, error);
	return ok;
}
