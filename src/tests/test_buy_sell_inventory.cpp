#include "../Game/Data/BuySellInventory.h"

#include <iostream>

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

bool runBase64Test()
{
	bool ok = true;
	std::string decoded;
	ok = check(BuySellInventory::encodeString("hello") == "aGVsbG8=", "base64 encode matches C# UTF-8 bytes") && ok;
	ok = check(BuySellInventory::decodeString("aGVsbG8=", decoded), "base64 decode succeeds") && ok;
	ok = check(decoded == "hello", "base64 decode restores input") && ok;
	ok = check(!BuySellInventory::decodeString("aGVsbG8=A", decoded), "base64 rejects trailing data after padding") && ok;
	ok = check(!BuySellInventory::decodeString("aGVsbG8=AAAA", decoded), "base64 rejects full chunk after padding") && ok;
	return ok;
}

bool runParseTest()
{
	const std::string text =
		"// Formal legacy shop description\n"
		"[Head]\n"
		"Count=2\n"
		"BuyPercent=125\n"
		"RecyclePercent=75\n"
		"\n"
		"[1]\n"
		"IniFile=drug.ini\n"
		"\n"
		"[2]\n"
		"IniFile=sword.ini\n"
		"Number=4\n";

	BuySellInventoryData inventory;
	bool ok = check(BuySellInventory::parseText(text, inventory),
		"parse [Head] inventory after a legacy slash-comment line");
	ok = check(inventory.count == 2, "inventory count parsed") && ok;
	ok = check(!inventory.numberValid, "missing NumberValid keeps file shop unlimited") && ok;
	ok = check(inventory.buyPercent == 125, "BuyPercent parsed") && ok;
	ok = check(inventory.recyclePercent == 75, "RecyclePercent parsed") && ok;
	ok = check(inventory.items.size() == 2, "items sized to count") && ok;
	if (inventory.items.size() == 2)
	{
		ok = check(inventory.items[0].iniFile == "drug.ini", "first item ini parsed") && ok;
		ok = check(inventory.items[0].number == 0, "parser keeps missing number as zero for target-backed shops") && ok;
		ok = check(inventory.items[1].number == 4, "explicit item number parsed") && ok;
	}
	return ok;
}

bool runHeaderNumberValidParseTest()
{
	const std::string text =
		"[Header]\n"
		"Count=2\n"
		"NumberValid=1\n"
		"BuyPercent=150\n"
		"RecyclePercent=60\n"
		"\n"
		"[1]\n"
		"IniFile=limited.ini\n"
		"Number=5\n"
		"\n"
		"[2]\n"
		"IniFile=empty.ini\n"
		"Number=0\n";

	BuySellInventoryData inventory;
	bool ok = check(BuySellInventory::parseText(text, inventory), "parse [Header] NumberValid inventory");
	ok = check(inventory.count == 2, "Header inventory count parsed") && ok;
	ok = check(inventory.numberValid, "Header NumberValid parsed") && ok;
	ok = check(inventory.buyPercent == 150, "Header BuyPercent parsed") && ok;
	ok = check(inventory.recyclePercent == 60, "Header RecyclePercent parsed") && ok;
	if (inventory.items.size() == 2)
	{
		ok = check(inventory.items[0].iniFile == "limited.ini", "Header first item parsed") && ok;
		ok = check(inventory.items[0].number == 5, "Header first stock parsed") && ok;
		ok = check(inventory.items[1].number == 0, "Header zero stock preserved") && ok;
	}
	else
	{
		ok = check(false, "Header items sized to count") && ok;
	}
	return ok;
}

bool runSerializeTest()
{
	BuySellInventoryData inventory;
	inventory.count = 2;
	inventory.numberValid = true;
	inventory.buyPercent = 120;
	inventory.recyclePercent = 80;
	inventory.items = {
		{ "drug.ini", 3 },
		{ "sword.ini", 0 },
	};

	std::string text = BuySellInventory::serializeText(inventory);
	bool ok = true;
	ok = check(text.find("[Header]") != std::string::npos, "serialize keeps C# Header casing") && ok;
	ok = check(text.find("IniFile=drug.ini") != std::string::npos, "serialize keeps C# IniFile casing") && ok;
	ok = check(text.find("Number=0") != std::string::npos, "serialize preserves zero stock") && ok;

	std::string decoded;
	std::string encoded = BuySellInventory::encodeString(text);
	ok = check(BuySellInventory::decodeString(encoded, decoded), "serialized inventory base64 decodes") && ok;
	ok = check(decoded == text, "serialized inventory base64 roundtrips") && ok;

	BuySellInventoryData reparsed;
	ok = check(BuySellInventory::parseText(decoded, reparsed), "serialized inventory reparses") && ok;
	ok = check(reparsed.count == 2, "reparsed count preserved") && ok;
	ok = check(reparsed.numberValid, "reparsed NumberValid preserved") && ok;
	ok = check(reparsed.buyPercent == 120, "reparsed BuyPercent preserved") && ok;
	ok = check(reparsed.recyclePercent == 80, "reparsed RecyclePercent preserved") && ok;
	return ok;
}

bool runInvalidIntegerTest()
{
	const std::string text =
		"[Header]\n"
		"Count=2junk\n"
		"BuyPercent=100\n"
		"\n"
		"[Head]\n"
		"Count=2\n"
		"BuyPercent=250\n"
		"\n"
		"[1]\n"
		"IniFile=drug.ini\n"
		"Number=7junk\n";
	BuySellInventoryData inventory;
	bool ok = check(BuySellInventory::parseText(text, inventory),
		"inventory with malformed integer fields still parses safely");
	ok = check(inventory.count == 0,
		"malformed Header Count uses safe default instead of permissive suffix or legacy override") && ok;
	ok = check(inventory.buyPercent == 100,
		"explicit Header default keeps precedence over legacy Head") && ok;
	return ok;
}
}

int main()
{
	bool ok = true;
	ok = runBase64Test() && ok;
	ok = runParseTest() && ok;
	ok = runHeaderNumberValidParseTest() && ok;
	ok = runSerializeTest() && ok;
	ok = runInvalidIntegerTest() && ok;
	return ok ? 0 : 1;
}
