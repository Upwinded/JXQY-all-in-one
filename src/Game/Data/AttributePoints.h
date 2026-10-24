#pragma once

#include <array>
#include <cstddef>

enum class PlayerAttribute : std::size_t
{
	Life, Thew, Mana, Attack, Evade, Defend, CriticalChance, CriticalDamage, Count
};

constexpr std::size_t PlayerAttributeCount = static_cast<std::size_t>(PlayerAttribute::Count);
using AttributePointAmounts = std::array<int, PlayerAttributeCount>;

struct AttributePointRules
{
	bool enabled = false;
	int levelUpPoints = 0;
	std::array<double, PlayerAttributeCount> ratios{};
	void load();
};

struct AttributePoints
{
	int available = 0;
	AttributePointAmounts spent{};
	float criticalChanceBonus = 0;
	int criticalDamageBonus = 0;
	bool hasProgress() const;
	void load(int characterIndex, bool initialTemplate = false);
	bool save(int characterIndex) const;
};
