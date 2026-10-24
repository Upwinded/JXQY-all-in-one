#pragma once

#include <string>
#include <vector>

struct SkillTreeNode
{
	std::string id;
	std::string magicFile;
	std::string icon;
	int groupId = 0;
	int requiredLevel = 0;
	int requiredPoints = 0;
	int requiredTotalPoints = 0;
	int maximumLevel = 10;
	float x = 0;
	float y = 0;
	std::vector<std::size_t> children;
	std::vector<std::size_t> parents;
	std::vector<std::size_t> requiredSkills;
	int currentLevel = 0;
	bool unlocked = false;
};

struct SkillTreeGroup
{
	int id = 0;
	std::string name;
	std::string description;
};

struct SkillTree
{
	std::vector<SkillTreeGroup> groups;
	std::vector<SkillTreeNode> nodes;
	bool parse(const std::string& json, std::string& failureReason);
};
