#include "../Game/Data/Goods.h"
#include "../Game/GameManager/SaveFileManager.h"
#include "../File/File.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <climits>

_shared_imp IMP::createIMPImage(const std::string&, bool)
{
	return nullptr;
}

_shared_imp IMP::createIMPImageFromMem(std::unique_ptr<char[]>&, int, bool)
{
	return nullptr;
}

namespace
{
constexpr char GoodsSaveNamespace[] = "goods-pricing";

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

class ScopedPlatformStateParent final
{
public:
	explicit ScopedPlatformStateParent(const std::filesystem::path& root)
	{
		File::setPlatformStateParentForTests(root.string());
	}

	~ScopedPlatformStateParent()
	{
		File::setPlatformStateParentForTests("");
	}

	void set(const std::filesystem::path& root)
	{
		File::setPlatformStateParentForTests(root.string());
	}
};

bool runRandomGoodsTest()
{
	auto root = makeUniqueTestDirectory("jxqy_goods_random_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "goods", errorCode);
	ScopedPlatformStateParent platformStateParent(root);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	File::setActiveSaveNamespace(GoodsSaveNamespace);

	const std::string randomGoodsIni =
		"[Init]\n"
		"Name=RANDOM_TEST_EQUIPMENT\n"
		"Kind=1\n"
		"Cost=10>20\n"
		"SellPrice=5,9\n"
		"Intro=random goods fixture\n"
		"Image=random.asf\n"
		"Icon=randoms.asf\n"
		"Part=Hand\n"
		"Sex=2\n"
		"MagicName=magic_throw.ini\n"
		"Attack=1>3\n"
		"Defend=4,8\n"
		"Evade=1>-5\n"
		"MagicIniWhenUse=magic_a.ini,magic_b.ini[3]\n";
	if (!writeTextFile(root / "ini" / "goods" / "random_goods.ini", randomGoodsIni))
	{
		std::cerr << "FAILED: write random goods fixture\n";
		return false;
	}

	bool ok = true;
	Goods randomGoods;
	randomGoods.initFromIni("random_goods.ini");
	ok = check(randomGoods.loadSucceeded, "valid goods reports successful load") && ok;
	ok = check(randomGoods.hasRandomAttributes(), "random goods detects AttrInt/AttrString random sources") && ok;
	ok = check(randomGoods.cost == 20, "random range loads max cost before instancing") && ok;
	ok = check(randomGoods.sellPrice == 9, "random list loads last sell price before instancing") && ok;
	ok = check(randomGoods.attack == 3, "random attack range loads max") && ok;
	ok = check(randomGoods.defend == 8, "random defend list loads last value") && ok;
	ok = check(randomGoods.evade == 1, "reversed random evade range loads max after swap") && ok;
	ok = check(randomGoods.sex == 2, "goods Sex field loads from ini") && ok;
	ok = check(randomGoods.magicName == "magic_throw.ini", "goods MagicName loads as cross-reference field") && ok;

	Goods fixedGoods = randomGoods.createNonRandomInstance();
	ok = check(!fixedGoods.hasRandomAttributes(), "generated goods clears random sources") && ok;
	ok = check(fixedGoods.sourceFileName != "random_goods.ini", "generated goods uses derived file name") && ok;
	ok = check(fixedGoods.attack >= 1 && fixedGoods.attack <= 3, "generated attack is inside source range") && ok;
	ok = check((fixedGoods.defend == 4 || fixedGoods.defend == 8), "generated defend is from source list") && ok;
	ok = check(fixedGoods.evade >= -5 && fixedGoods.evade <= 1, "generated evade handles reversed range") && ok;
	ok = check((fixedGoods.magicIniWhenUse == "magic_a.ini" || fixedGoods.magicIniWhenUse == "magic_b.ini"), "generated magic string comes from weighted list") && ok;
	ok = check(fixedGoods.saveRuntimeIni(fixedGoods.sourceFileName), "generated goods writes runtime ini") && ok;

	Goods reloadedGoods;
	reloadedGoods.initFromIni(fixedGoods.sourceFileName);
	ok = check(!reloadedGoods.hasRandomAttributes(), "runtime generated goods reloads as fixed") && ok;
	ok = check(reloadedGoods.attack == fixedGoods.attack, "runtime generated goods preserves attack") && ok;
	ok = check(reloadedGoods.defend == fixedGoods.defend, "runtime generated goods preserves defend") && ok;
	ok = check(reloadedGoods.evade == fixedGoods.evade, "runtime generated goods preserves evade") && ok;
	ok = check(reloadedGoods.sex == fixedGoods.sex, "runtime generated goods preserves Sex") && ok;
	ok = check(reloadedGoods.magicName == fixedGoods.magicName, "runtime generated goods preserves MagicName") && ok;
	ok = check(reloadedGoods.magicIniWhenUse == fixedGoods.magicIniWhenUse, "runtime generated goods preserves magic string") && ok;

	const std::string scopedGoodsName =
		"scoped_runtime_goods.ini";
	const std::string currentScopedGoods =
		"[Init]\n"
		"Name=CURRENT_SCOPED_GOODS\n"
		"Kind=1\n"
		"Attack=11\n";
	const std::string candidateScopedGoods =
		"[Init]\n"
		"Name=CANDIDATE_SCOPED_GOODS\n"
		"Kind=1\n"
		"Attack=77\n";
	if (!writeTextFile(
			root / "save" / GoodsSaveNamespace / "game" /
				scopedGoodsName,
			currentScopedGoods) ||
		!writeTextFile(
			root / "save" / GoodsSaveNamespace / "candidate" /
				scopedGoodsName,
			candidateScopedGoods))
	{
		std::cerr << "FAILED: write scoped runtime goods fixtures\n";
		return false;
	}
	{
		SaveFileManager::CurrentPathScope candidatePath(
			"save/candidate");
		Goods candidateGoods;
		candidateGoods.initFromIni(scopedGoodsName);
		ok = check(
			candidatePath.valid() &&
				candidateGoods.loadSucceeded &&
				candidateGoods.attack == 77,
			"runtime goods load follows the explicit candidate generation") &&
			ok;
		ok = check(
			candidateGoods.saveRuntimeIni(
				"scoped_runtime_copy.ini"),
			"runtime goods save follows the explicit candidate generation") &&
			ok;
	}
	ok = check(
		std::filesystem::exists(
			root / "save" / GoodsSaveNamespace / "candidate" /
				"scoped_runtime_copy.ini") &&
			!std::filesystem::exists(
				root / "save" / GoodsSaveNamespace / "game" /
					"scoped_runtime_copy.ini"),
		"runtime goods scoped save never writes the formal current generation") &&
		ok;

	const std::filesystem::path blockedRoot = root / "blocked-root";
	if (!writeTextFile(blockedRoot, "not-a-directory"))
	{
		std::cerr << "FAILED: write blocked goods root\n";
		return false;
	}
	platformStateParent.set(blockedRoot);
	ok = check(!fixedGoods.saveRuntimeIni("write_failure.ini"),
		"generated goods reports runtime INI write failure") && ok;
	platformStateParent.set(root);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	Goods missingGoods;
	missingGoods.initFromIni("missing_goods.ini");
	ok = check(!missingGoods.loadSucceeded,
		"missing goods remains invalid instead of becoming a blank item") && ok;
	return ok;
}

bool runUserRestrictionTest()
{
	auto root = makeUniqueTestDirectory("jxqy_goods_user_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	std::filesystem::create_directories(root / "ini" / "goods", errorCode);
	File::setAssetsCollectionRoot((root / "assets").string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});

	const std::string restrictedGoodsIni =
		"[Init]\n"
		"Name=USER_RESTRICTED_GOODS\n"
		"Kind=0\n"
		"User=Hero,Partner\n";
	if (!writeTextFile(root / "ini" / "goods" / "restricted_goods.ini", restrictedGoodsIni))
	{
		std::cerr << "FAILED: write restricted goods fixture\n";
		return false;
	}

	bool ok = true;
	Goods restrictedGoods;
	restrictedGoods.initFromIni("restricted_goods.ini");
	ok = check(restrictedGoods.isAllowedForUserName("Hero"), "goods user restriction accepts exact C# Name") && ok;
	ok = check(!restrictedGoods.isAllowedForUserName("hero"), "goods user restriction remains case-sensitive like C#") && ok;
	ok = check(restrictedGoods.isAllowedForAnyUserName({ "", "npc-Hero", "Hero" }), "goods user restriction accepts fallback display-name candidates") && ok;
	ok = check(!restrictedGoods.isAllowedForAnyUserName({ "", "npc-Hero", "Other" }), "goods user restriction rejects non-matching candidates") && ok;

	Goods unrestrictedGoods;
	ok = check(unrestrictedGoods.isAllowedForAnyUserName({}), "goods without User restriction accepts empty candidates") && ok;
	return ok;
}
}

int main()
{
	bool ok = true;

	Goods drug;
	drug.kind = gkDrug;
	drug.life = 10;
	drug.thew = 5;
	drug.mana = 3;
	ok = check(drug.getRawCost() == 46, "drug raw cost uses thew/life/mana formula") && ok;
	drug.effectType = 1;
	ok = check(drug.getRawCost() == 92, "drug effect type doubles raw cost") && ok;

	Goods equipment;
	equipment.kind = gkEquipment;
	equipment.attack = 3;
	equipment.attack2 = 2;
	equipment.attack3 = 1;
	equipment.defend = 4;
	equipment.defend2 = 3;
	equipment.defend3 = 2;
	equipment.evade = 1;
	equipment.lifeMax = 10;
	equipment.thewMax = 5;
	equipment.manaMax = 5;
	ok = check(equipment.getRawCost() == 385, "equipment raw cost uses attack/defend/resource formula") && ok;
	equipment.effectType = 1;
	ok = check(equipment.getRawCost() == 770, "equipment effect type doubles raw cost") && ok;
	equipment.noNeedToEquip = 1;
	ok = check(equipment.getRawCost() == 0, "no-need equipment has zero raw cost like C#") && ok;

	Goods priced;
	priced.kind = gkEquipment;
	priced.cost = 77;
	priced.sellPrice = 33;
	ok = check(priced.getRawCost() == 77, "explicit cost overrides computed raw cost") && ok;
	ok = check(priced.getBuyPrice(150) == 115, "buy percent applies integer truncation") && ok;
	ok = check(priced.hasExplicitSellPrice(), "explicit sell price flag") && ok;
	ok = check(priced.getSellPrice(50) == 16, "explicit sell price applies recycle percent") && ok;
	priced.sellPrice = 0;
	ok = check(!priced.hasExplicitSellPrice(), "missing sell price flag") && ok;
	ok = check(priced.getSellPrice(100) == 38, "default sell price is half raw cost") && ok;
	ok = check(priced.getBuyPrice(-100) == 0 && priced.getSellPrice(-100) == 0,
		"negative shop percentages cannot create negative prices") && ok;
	priced.cost = INT_MAX;
	ok = check(priced.getBuyPrice(INT_MAX) == INT_MAX,
		"extreme buy price arithmetic saturates without signed overflow") && ok;
	priced.cost = 0;
	priced.attack = INT_MAX;
	ok = check(priced.getRawCost() == INT_MAX,
		"extreme computed goods cost saturates without signed overflow") && ok;

	ok = runRandomGoodsTest() && ok;
	ok = runUserRestrictionTest() && ok;

	return ok ? 0 : 1;
}
