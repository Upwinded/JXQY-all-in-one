#include "SkillTree.h"
#include "../GameTypes.h"
#include "../../File/File.h"
#include "../../File/ResourcePathSafety.h"
#include "../../Launch/StrictJson.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace
{
using StrictJson::JsonType;
using StrictJson::JsonValue;

const JsonValue* member(const JsonValue& object, const char* name)
{
	const auto found = object.objectValues.find(name);
	return found == object.objectValues.end() ? nullptr : &found->second;
}

bool readString(const JsonValue& object, const char* name, std::string& value, bool required = false)
{
	const auto* field = member(object, name);
	if (field == nullptr) return !required;
	if (field->type != JsonType::String || field->text.size() > 4096) return false;
	value = field->text;
	return !required || !value.empty();
}

bool readInteger(const JsonValue& object, const char* name, int& value, int maximum)
{
	const auto* field = member(object, name);
	if (field == nullptr) return true;
	if (field->type != JsonType::Number) return false;
	const auto parsed = std::from_chars(field->text.data(), field->text.data() + field->text.size(), value);
	return parsed.ec == std::errc() && parsed.ptr == field->text.data() + field->text.size()
		&& value >= 0 && value <= maximum;
}

bool readCoordinate(const JsonValue& object, const char* name, float& value)
{
	const auto* field = member(object, name);
	if (field == nullptr) return true;
	if (field->type != JsonType::Number) return false;
	std::istringstream input(field->text);
	input.imbue(std::locale::classic());
	input >> std::noskipws >> value;
	return input && input.eof() && std::isfinite(value) && std::abs(value) <= 1000000;
}

bool readIds(const JsonValue& object, const char* name, std::vector<std::string>& ids)
{
	const auto* field = member(object, name);
	if (field == nullptr) return true;
	if (field->type != JsonType::Array || field->arrayValues.size() > 512) return false;
	std::set<std::string> uniqueIds;
	for (const auto& id : field->arrayValues)
	{
		if (id.type != JsonType::String || id.text.empty() || id.text.size() > 4096
			|| !uniqueIds.insert(id.text).second) return false;
		ids.push_back(id.text);
	}
	return true;
}
}

bool SkillTree::parse(const std::string& json, std::string& failureReason)
{
	groups.clear();
	nodes.clear();
	failureReason = u8"技能树 JSON 格式或字段无效";
	if (json.size() > 1024 * 1024 || !ResourcePathSafety::isValidUtf8(json)) return false;
	const std::string_view source = json.compare(0, 3, "\xEF\xBB\xBF") == 0
		? std::string_view(json).substr(3) : std::string_view(json);
	JsonValue root;
	if (!StrictJson::StrictJsonParser(source).parse(root) || root.type != JsonType::Object) return false;
	const auto* groupArray = member(root, "Groups");
	if (groupArray == nullptr || groupArray->type != JsonType::Array || groupArray->arrayValues.size() > 32) return false;
	SkillTree parsed;
	std::map<std::string, std::size_t> indices;
	std::set<int> groupIds;
	std::set<std::string> magicFiles;
	std::vector<std::vector<std::string>> childIds;
	std::vector<std::vector<std::string>> requiredIds;
	for (const auto& groupValue : groupArray->arrayValues)
	{
		SkillTreeGroup group;
		if (groupValue.type != JsonType::Object
			|| !readInteger(groupValue, "GroupId", group.id, 1000000)
			|| !groupIds.insert(group.id).second
			|| !readString(groupValue, "GroupName", group.name, true)
			|| !readString(groupValue, "GroupDescription", group.description)) return false;
		const auto* nodeArray = member(groupValue, "Nodes");
		if (nodeArray == nullptr || nodeArray->type != JsonType::Array
			|| parsed.nodes.size() + nodeArray->arrayValues.size() > 512) return false;
		parsed.groups.push_back(group);
		for (const auto& nodeValue : nodeArray->arrayValues)
		{
			SkillTreeNode node;
			node.groupId = group.id;
			if (nodeValue.type != JsonType::Object
				|| !readString(nodeValue, "Id", node.id, true)
				|| !indices.emplace(node.id, parsed.nodes.size()).second
				|| !readString(nodeValue, "SkillFile", node.magicFile, true)
				|| !File::isSafeResourcePath(node.magicFile)
				|| !readString(nodeValue, "Icon", node.icon)
				|| (!node.icon.empty() && !File::isSafeResourcePath(node.icon))
				|| !readInteger(nodeValue, "RequiredLevel", node.requiredLevel, 1000000)
				|| !readInteger(nodeValue, "RequiredPoints", node.requiredPoints, 1000000)
				|| !readInteger(nodeValue, "RequiredTotalPoints", node.requiredTotalPoints, 1000000)
				|| !readInteger(nodeValue, "MaxLevel", node.maximumLevel, MAGIC_MAX_LEVEL)
				|| node.maximumLevel == 0) return false;
			std::string normalized = node.magicFile;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c)
			{
				return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
			});
			if (!magicFiles.insert(normalized).second) return false;
			const auto* position = member(nodeValue, "Position");
			if (position != nullptr && (position->type != JsonType::Object
				|| !readCoordinate(*position, "X", node.x) || !readCoordinate(*position, "Y", node.y))) return false;
			childIds.emplace_back();
			requiredIds.emplace_back();
			if (!readIds(nodeValue, "ChildIds", childIds.back())
				|| !readIds(nodeValue, "RequiredSkillIds", requiredIds.back())) return false;
			parsed.nodes.push_back(std::move(node));
		}
	}
	for (std::size_t i = 0; i < parsed.nodes.size(); ++i)
	{
		for (const auto& id : childIds[i])
		{
			const auto found = indices.find(id);
			if (found == indices.end())
			{
				failureReason = u8"技能树包含不存在的子节点：" + id;
				return false;
			}
			parsed.nodes[i].children.push_back(found->second);
			parsed.nodes[found->second].parents.push_back(i);
		}
		for (const auto& id : requiredIds[i])
		{
			const auto found = indices.find(id);
			if (found == indices.end())
			{
				failureReason = u8"技能树包含不存在的前置技能：" + id;
				return false;
			}
			parsed.nodes[i].requiredSkills.push_back(found->second);
		}
	}
	*this = std::move(parsed);
	failureReason.clear();
	return true;
}
