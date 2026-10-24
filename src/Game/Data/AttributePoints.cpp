#include "AttributePoints.h"
#include "SaveIniPersistence.h"
#include "../GameManager/SaveFileManager.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace
{
constexpr std::array<const char*, PlayerAttributeCount> RatioKeys = {
	"LifeRatio", "ThewRatio", "ManaRatio", "AttackRatio", "EvadeRatio", "DefendRatio", "CritChanceRatio", "CritDamageRatio"
};
constexpr std::array<const char*, PlayerAttributeCount> PointKeys = {
	"LifePoint", "ThewPoint", "ManaPoint", "AttackPoint", "EvadePoint", "DefendPoint", "CritChancePoint", "CritDamagePoint"
};

bool readNonNegative(const INIReader& ini, const std::string& section, const char* key, int& value)
{
	const std::string text = ini.Get(section, key, "0");
	const char* first = text.data();
	const char* last = first + text.size();
	if (first != last && *first == '+') ++first;
	const auto parsed = std::from_chars(first, last, value);
	return parsed.ec == std::errc() && parsed.ptr == last && value >= 0;
}
}

void AttributePointRules::load()
{
	*this = {};
	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readActiveResourceFile("ini/other/Point.ini", data, length, 64 * 1024)
		|| data == nullptr || length <= 0) return;
	INIReader ini(data);
	int enable = 0;
	AttributePointRules candidate;
	if (ini.ParseError() != 0 || !readNonNegative(ini, "Init", "Enable", enable) || enable != 1
		|| !readNonNegative(ini, "Init", "TotalRatio", candidate.levelUpPoints)) return;
	for (std::size_t i = 0; i < RatioKeys.size(); ++i)
	{
		if (i == static_cast<std::size_t>(PlayerAttribute::CriticalChance))
		{
			const std::string text = ini.Get("Init", RatioKeys[i], "0");
			float ratio = 0;
			std::istringstream input(text);
			input.imbue(std::locale::classic());
			input >> std::noskipws >> ratio;
			if (!input || !input.eof() || !std::isfinite(ratio) || ratio < 0) return;
			candidate.ratios[i] = ratio;
		}
		else
		{
			int ratio = 0;
			if (!readNonNegative(ini, "Init", RatioKeys[i], ratio)) return;
			candidate.ratios[i] = ratio;
		}
	}
	candidate.enabled = true;
	*this = candidate;
}

bool AttributePoints::hasProgress() const
{
	return available != 0 || criticalChanceBonus != 0 || criticalDamageBonus != 0
		|| std::any_of(spent.begin(), spent.end(), [](int value) { return value != 0; });
}

void AttributePoints::load(int characterIndex, bool initialTemplate)
{
	*this = {};
	const std::string file = initialTemplate ? "ini/save/point.ini" : SaveFileManager::CurrentPath() + "point.ini";
	std::shared_ptr<INIReader> ini;
	if (SaveIniPersistence::read(file, ini, 1024 * 1024) != SaveIniPersistence::ReadStatus::Loaded) return;
	const std::string section = "Player" + std::to_string(characterIndex);
	if (!readNonNegative(*ini, section, "TotalPoint", available)) available = 0;
	for (std::size_t i = 0; i < spent.size(); ++i)
	{
		if (!readNonNegative(*ini, section, PointKeys[i], spent[i])) spent[i] = 0;
	}
	// C++ keeps the applied critical bonuses independently of later config edits.
	criticalChanceBonus = ini->GetReal(section, "CritChanceBonus", 0);
	if (!std::isfinite(criticalChanceBonus) || criticalChanceBonus < 0) criticalChanceBonus = 0;
	criticalChanceBonus = std::min(100.0f, criticalChanceBonus);
	if (!readNonNegative(*ini, section, "CritDamageBonus", criticalDamageBonus)) criticalDamageBonus = 0;
}

bool AttributePoints::save(int characterIndex) const
{
	const std::string file = SaveFileManager::CurrentPath() + "point.ini";
	std::shared_ptr<INIReader> ini;
	const auto status = SaveIniPersistence::read(file, ini, 1024 * 1024);
	if (status == SaveIniPersistence::ReadStatus::Malformed || status == SaveIniPersistence::ReadStatus::Unreadable) return false;
	if (ini == nullptr) ini = std::make_shared<INIReader>();
	const std::string section = "Player" + std::to_string(characterIndex);
	ini->SetInteger(section, "TotalPoint", std::max(0, available));
	for (std::size_t i = 0; i < spent.size(); ++i) ini->SetInteger(section, PointKeys[i], std::max(0, spent[i]));
	if (criticalChanceBonus != 0)
	{
		std::ostringstream text;
		text.imbue(std::locale::classic());
		text.precision(std::numeric_limits<float>::max_digits10);
		text << criticalChanceBonus;
		if (!text) return false;
		ini->Set(section, "CritChanceBonus", text.str());
	}
	else ini->Remove(section, "CritChanceBonus");
	if (criticalDamageBonus != 0) ini->SetInteger(section, "CritDamageBonus", criticalDamageBonus);
	else ini->Remove(section, "CritDamageBonus");
	return ini->saveToFile(file);
}
