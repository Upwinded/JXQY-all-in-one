#pragma once

#include <string>
#include <vector>

struct BuySellInventoryItem
{
	std::string iniFile = "";
	int number = 0;
};

struct BuySellInventoryData
{
	int count = 0;
	bool numberValid = false;
	int buyPercent = 100;
	int recyclePercent = 100;
	std::vector<BuySellInventoryItem> items;
};

namespace BuySellInventory
{
	bool decodeString(const std::string& encoded, std::string& decoded);
	std::string encodeString(const std::string& decoded);

	bool parseText(const std::string& text, BuySellInventoryData& data);
	std::string serializeText(const BuySellInventoryData& data);
}
