#include "EditorRunDescriptor.h"
#include "StrictJson.h"

#include "../File/ResourcePathSafety.h"
#include "../File/StrictRelativeResourcePath.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using StrictJson::JsonType;
using StrictJson::JsonValue;
using StrictJson::StrictJsonParser;

using Object = std::map<std::string, JsonValue>;

void setFailure(
	EditorRun::DescriptorResult& result,
	EditorRun::DescriptorError error,
	std::string fieldPath,
	std::string message)
{
	if (result.error != EditorRun::DescriptorError::None)
	{
		return;
	}
	result.error = error;
	result.fieldPath = std::move(fieldPath);
	result.message = std::move(message);
}

bool rejectUnknownFields(
	const Object& object,
	std::initializer_list<std::string_view> allowedFields,
	const std::string& fieldPath,
	EditorRun::DescriptorResult& result)
{
	std::set<std::string, std::less<>> allowed;
	for (std::string_view field : allowedFields)
	{
		allowed.emplace(field);
	}
	for (const auto& member : object)
	{
		if (allowed.find(member.first) == allowed.end())
		{
			setFailure(
				result,
				EditorRun::DescriptorError::UnknownField,
				fieldPath.empty()
					? member.first
					: fieldPath + "." + member.first,
				"Unknown descriptor field");
			return false;
		}
	}
	return true;
}

const JsonValue* requireMember(
	const Object& object,
	const std::string& name,
	const std::string& fieldPath,
	EditorRun::DescriptorResult& result)
{
	const auto member = object.find(name);
	if (member != object.end())
	{
		return &member->second;
	}
	setFailure(
		result,
		EditorRun::DescriptorError::MissingField,
		fieldPath.empty() ? name : fieldPath + "." + name,
		"Required descriptor field is missing");
	return nullptr;
}

bool readString(
	const Object& object,
	const std::string& name,
	const std::string& fieldPath,
	std::string& value,
	EditorRun::DescriptorResult& result)
{
	const JsonValue* member = requireMember(object, name, fieldPath, result);
	if (member == nullptr)
	{
		return false;
	}
	if (member->type != JsonType::String)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidFieldType,
			fieldPath.empty() ? name : fieldPath + "." + name,
			"Descriptor field must be a string");
		return false;
	}
	value = member->text;
	return true;
}

bool readOptionalString(
	const Object& object,
	const std::string& name,
	const std::string& fieldPath,
	std::string& value,
	EditorRun::DescriptorResult& result)
{
	const auto member = object.find(name);
	if (member == object.end())
	{
		value.clear();
		return true;
	}
	if (member->second.type != JsonType::String)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidFieldType,
			fieldPath.empty() ? name : fieldPath + "." + name,
			"Descriptor field must be a string");
		return false;
	}
	value = member->second.text;
	return true;
}

bool readObject(
	const Object& object,
	const std::string& name,
	const std::string& fieldPath,
	const Object*& value,
	EditorRun::DescriptorResult& result)
{
	const JsonValue* member = requireMember(object, name, fieldPath, result);
	if (member == nullptr)
	{
		return false;
	}
	if (member->type != JsonType::Object)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidFieldType,
			fieldPath.empty() ? name : fieldPath + "." + name,
			"Descriptor field must be an object");
		return false;
	}
	value = &member->objectValues;
	return true;
}

bool readInt32Value(
	const JsonValue& value,
	const std::string& fieldPath,
	std::int32_t& output,
	EditorRun::DescriptorResult& result)
{
	if (value.type != JsonType::Number ||
		value.text.find_first_of(".eE") != std::string::npos)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidFieldType,
			fieldPath,
			"Descriptor field must be an int32 JSON integer");
		return false;
	}

	std::int64_t parsed = 0;
	const char* begin = value.text.data();
	const char* end = begin + value.text.size();
	const std::from_chars_result conversion =
		std::from_chars(begin, end, parsed, 10);
	if (conversion.ec != std::errc() || conversion.ptr != end ||
		parsed < std::numeric_limits<std::int32_t>::min() ||
		parsed > std::numeric_limits<std::int32_t>::max())
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidValue,
			fieldPath,
			"Descriptor integer is outside the int32 range");
		return false;
	}
	output = static_cast<std::int32_t>(parsed);
	return true;
}

bool readInt32(
	const Object& object,
	const std::string& name,
	const std::string& fieldPath,
	std::int32_t& value,
	EditorRun::DescriptorResult& result)
{
	const JsonValue* member = requireMember(object, name, fieldPath, result);
	return member != nullptr &&
		readInt32Value(
			*member,
			fieldPath.empty() ? name : fieldPath + "." + name,
			value,
			result);
}

bool containsControlCharacter(const std::string& value)
{
	for (std::size_t index = 0; index < value.size(); ++index)
	{
		const unsigned char character =
			static_cast<unsigned char>(value[index]);
		if (character < 0x20 || character == 0x7F)
		{
			return true;
		}
		if (character == 0xC2 && index + 1 < value.size())
		{
			const unsigned char next =
				static_cast<unsigned char>(value[index + 1]);
			if (next >= 0x80 && next <= 0x9F)
			{
				return true;
			}
		}
	}
	return false;
}

bool isAsciiWhitespaceOnly(const std::string& value)
{
	if (value.empty())
	{
		return true;
	}
	for (char character : value)
	{
		if (character != ' ' && character != '\t' &&
			character != '\r' && character != '\n')
		{
			return false;
		}
	}
	return true;
}

bool validateRequiredText(
	const std::string& value,
	const std::string& fieldPath,
	EditorRun::DescriptorResult& result)
{
	if (isAsciiWhitespaceOnly(value) || containsControlCharacter(value))
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidValue,
			fieldPath,
			"Descriptor text must be non-empty and contain no control characters");
		return false;
	}
	return true;
}

bool readVirtualPath(
	const Object& object,
	const std::string& name,
	const std::string& fieldPath,
	bool optional,
	std::string& value,
	EditorRun::DescriptorResult& result)
{
	if (!readString(object, name, fieldPath, value, result))
	{
		return false;
	}
	const std::string fullPath =
		fieldPath.empty() ? name : fieldPath + "." + name;
	if (optional && value.empty())
	{
		return true;
	}
	const ResourcePathSafety::StrictRelativePathResult normalized =
		ResourcePathSafety::normalizeStrictRelativeResourcePath(value);
	if (!normalized.succeeded())
	{
		setFailure(
			result,
			EditorRun::DescriptorError::UnsafeVirtualPath,
			fullPath,
			"Descriptor resource path is not a strict relative virtual path");
		return false;
	}
	value = normalized.normalizedPath;
	return true;
}

bool readAbsoluteHostPath(
	const Object& object,
	const std::string& name,
	std::filesystem::path& value,
	EditorRun::DescriptorResult& result)
{
	std::string pathText;
	if (!readString(object, name, "", pathText, result))
	{
		return false;
	}
	if (pathText.empty() || containsControlCharacter(pathText))
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidHostPath,
			name,
			"Descriptor host path must be non-empty and contain no control characters");
		return false;
	}
	try
	{
		std::filesystem::path candidate =
			std::filesystem::u8path(pathText);
		if (!candidate.is_absolute())
		{
			setFailure(
				result,
				EditorRun::DescriptorError::InvalidHostPath,
				name,
				"Descriptor host path must be absolute");
			return false;
		}
		value = candidate.lexically_normal();
		return true;
	}
	catch (...)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidHostPath,
			name,
			"Descriptor host path cannot be represented on this platform");
		return false;
	}
}

bool parseTarget(
	const Object& targetObject,
	std::int32_t schemaVersion,
	EditorRun::SceneTarget& target,
	EditorRun::DescriptorResult& result)
{
	if (!rejectUnknownFields(
		targetObject,
		{
			"kind",
			"sceneId",
			"sceneName",
			"map",
			"npc",
			"object",
			"entryScript",
			"playerPosition",
			"integerVariables"
		},
		"target",
		result))
	{
		return false;
	}

	std::string kind;
	if (!readString(targetObject, "kind", "target", kind, result))
	{
		return false;
	}
	if (kind == "scene")
	{
		target.kind = EditorRun::TargetKind::Scene;
	}
	else if (
		schemaVersion == EditorRun::Descriptor::SchemaVersion &&
		kind == "map")
	{
		target.kind = EditorRun::TargetKind::Map;
	}
	else if (
		schemaVersion == EditorRun::Descriptor::SchemaVersion &&
		kind == "script")
	{
		target.kind = EditorRun::TargetKind::Script;
	}
	else
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidValue,
			"target.kind",
			schemaVersion ==
					EditorRun::Descriptor::LegacySchemaVersion
				? "Descriptor v1 only supports target kind 'scene'"
				: "Descriptor target kind must be 'scene', 'map', or 'script'");
		return false;
	}
	if (!readString(
			targetObject, "sceneId", "target", target.sceneId, result) ||
		!validateRequiredText(target.sceneId, "target.sceneId", result) ||
		!readString(
			targetObject, "sceneName", "target", target.sceneName, result) ||
		!validateRequiredText(target.sceneName, "target.sceneName", result) ||
		!readVirtualPath(
			targetObject, "map", "target", false, target.mapPath, result) ||
		!readVirtualPath(
			targetObject, "npc", "target", true, target.npcPath, result) ||
		!readVirtualPath(
			targetObject, "object", "target", true, target.objectPath, result) ||
		!readVirtualPath(
			targetObject, "entryScript", "target", true,
			target.entryScriptPath, result))
	{
		return false;
	}

	const JsonValue* position = requireMember(
		targetObject, "playerPosition", "target", result);
	if (position == nullptr)
	{
		return false;
	}
	if (position->type != JsonType::Array ||
		position->arrayValues.size() != 2)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidFieldType,
			"target.playerPosition",
			"Player position must be an array containing two int32 values");
		return false;
	}
	if (!readInt32Value(
			position->arrayValues[0],
			"target.playerPosition[0]",
			target.playerX,
			result) ||
		!readInt32Value(
			position->arrayValues[1],
			"target.playerPosition[1]",
			target.playerY,
			result))
	{
		return false;
	}

	const Object* variables = nullptr;
	if (!readObject(
		targetObject,
		"integerVariables",
		"target",
		variables,
		result))
	{
		return false;
	}
	if (variables->size() > EditorRun::MaximumIntegerVariableCount)
	{
		setFailure(
			result,
			EditorRun::DescriptorError::TooManyIntegerVariables,
			"target.integerVariables",
			"Integer variable count exceeds the supported limit");
		return false;
	}
	for (const auto& variable : *variables)
	{
		const std::string variablePath =
			"target.integerVariables." + variable.first;
		if (!validateRequiredText(variable.first, variablePath, result))
		{
			return false;
		}
		std::int32_t variableValue = 0;
		if (!readInt32Value(
			variable.second, variablePath, variableValue, result))
		{
			return false;
		}
		target.integerVariables.emplace(variable.first, variableValue);
	}
	if (target.kind == EditorRun::TargetKind::Map &&
		!target.entryScriptPath.empty())
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidValue,
			"target.entryScript",
			"Map targets cannot specify an entry script");
		return false;
	}
	if (target.kind == EditorRun::TargetKind::Script &&
		target.entryScriptPath.empty())
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidValue,
			"target.entryScript",
			"Script targets require an entry script");
		return false;
	}
	return true;
}

bool parseDescriptorObject(
	const Object& root,
	EditorRun::Descriptor& descriptor,
	EditorRun::DescriptorResult& result)
{
	if (!rejectUnknownFields(
		root,
		{
			"schemaVersion",
			"sessionId",
			"assetsCollectionRoot",
			"activeResourcePackId",
			"activeResourcePackEntryKey",
			"target",
			"overlayRoot",
			"isolatedSaveRoot",
			"applicationStateRoot",
			"diagnosticsPath",
			"logPath",
			"autoExit"
		},
		"",
		result))
	{
		return false;
	}

	std::int32_t schemaVersion = 0;
	if (!readInt32(root, "schemaVersion", "", schemaVersion, result))
	{
		return false;
	}
	if (!EditorRun::Descriptor::supportsSchemaVersion(
			schemaVersion))
	{
		setFailure(
			result,
			EditorRun::DescriptorError::UnsupportedVersion,
			"schemaVersion",
			"Unsupported editor-run descriptor schema version");
		return false;
	}
	descriptor.sourceSchemaVersion = schemaVersion;

	if (!readString(root, "sessionId", "", descriptor.sessionId, result))
	{
		return false;
	}
	if (!validateRequiredText(
			descriptor.sessionId,
			"sessionId",
			result))
	{
		return false;
	}

	if (!readAbsoluteHostPath(
			root,
			"assetsCollectionRoot",
			descriptor.assetsCollectionRoot,
			result) ||
		!readString(
			root,
			"activeResourcePackId",
			"",
			descriptor.activeResourcePackId,
			result) ||
		!validateRequiredText(
			descriptor.activeResourcePackId,
			"activeResourcePackId",
			result))
	{
		return false;
	}
	if (!readOptionalString(
			root,
			"activeResourcePackEntryKey",
			"",
			descriptor.activeResourcePackEntryKey,
			result) ||
		(!descriptor.activeResourcePackEntryKey.empty() &&
			!validateRequiredText(
				descriptor.activeResourcePackEntryKey,
				"activeResourcePackEntryKey",
				result)))
	{
		return false;
	}

	const Object* targetObject = nullptr;
	if (!readObject(root, "target", "", targetObject, result) ||
		!parseTarget(
			*targetObject,
			schemaVersion,
			descriptor.target,
			result))
	{
		return false;
	}

	if (!readAbsoluteHostPath(
			root, "overlayRoot", descriptor.overlayRoot, result) ||
		!readAbsoluteHostPath(
			root, "isolatedSaveRoot", descriptor.isolatedSaveRoot, result) ||
		!readAbsoluteHostPath(
			root,
			"applicationStateRoot",
			descriptor.applicationStateRoot,
			result) ||
		!readAbsoluteHostPath(
			root, "diagnosticsPath", descriptor.diagnosticsPath, result) ||
		!readAbsoluteHostPath(root, "logPath", descriptor.logPath, result))
	{
		return false;
	}

	const Object* autoExit = nullptr;
	if (!readObject(root, "autoExit", "", autoExit, result) ||
		!rejectUnknownFields(*autoExit, { "mode" }, "autoExit", result))
	{
		return false;
	}
	std::string autoExitMode;
	if (!readString(*autoExit, "mode", "autoExit", autoExitMode, result))
	{
		return false;
	}
	if (autoExitMode != "manual")
	{
		setFailure(
			result,
			EditorRun::DescriptorError::InvalidValue,
			"autoExit.mode",
			"Editor-run descriptors only support auto-exit mode 'manual'");
		return false;
	}
	return true;
}

using StrictJson::appendEscapedJsonString;

void appendJsonMemberPrefix(
	std::string_view name,
	bool& first,
	std::string& output)
{
	if (!first)
	{
		output.push_back(',');
	}
	first = false;
	appendEscapedJsonString(name, output);
	output.push_back(':');
}

std::string hostPathToUtf8(const std::filesystem::path& path)
{
	const auto bytes = path.generic_u8string();
	return std::string(bytes.begin(), bytes.end());
}

std::string serializeDescriptorUnchecked(
	const EditorRun::Descriptor& descriptor)
{
	std::string output;
	output.reserve(1024);
	output.push_back('{');
	bool firstRootMember = true;

	appendJsonMemberPrefix("schemaVersion", firstRootMember, output);
	output += std::to_string(EditorRun::Descriptor::SchemaVersion);
	appendJsonMemberPrefix("sessionId", firstRootMember, output);
	appendEscapedJsonString(descriptor.sessionId, output);
	appendJsonMemberPrefix("assetsCollectionRoot", firstRootMember, output);
	appendEscapedJsonString(
		hostPathToUtf8(descriptor.assetsCollectionRoot), output);
	appendJsonMemberPrefix("activeResourcePackId", firstRootMember, output);
	appendEscapedJsonString(descriptor.activeResourcePackId, output);
	appendJsonMemberPrefix(
		"activeResourcePackEntryKey",
		firstRootMember,
		output);
	appendEscapedJsonString(
		descriptor.activeResourcePackEntryKey,
		output);

	appendJsonMemberPrefix("target", firstRootMember, output);
	output.push_back('{');
	bool firstTargetMember = true;
	appendJsonMemberPrefix("kind", firstTargetMember, output);
	switch (descriptor.target.kind)
	{
	case EditorRun::TargetKind::Map:
		appendEscapedJsonString("map", output);
		break;
	case EditorRun::TargetKind::Script:
		appendEscapedJsonString("script", output);
		break;
	case EditorRun::TargetKind::Scene:
	default:
		appendEscapedJsonString("scene", output);
		break;
	}
	appendJsonMemberPrefix("sceneId", firstTargetMember, output);
	appendEscapedJsonString(descriptor.target.sceneId, output);
	appendJsonMemberPrefix("sceneName", firstTargetMember, output);
	appendEscapedJsonString(descriptor.target.sceneName, output);
	appendJsonMemberPrefix("map", firstTargetMember, output);
	appendEscapedJsonString(descriptor.target.mapPath, output);
	appendJsonMemberPrefix("npc", firstTargetMember, output);
	appendEscapedJsonString(descriptor.target.npcPath, output);
	appendJsonMemberPrefix("object", firstTargetMember, output);
	appendEscapedJsonString(descriptor.target.objectPath, output);
	appendJsonMemberPrefix("entryScript", firstTargetMember, output);
	appendEscapedJsonString(descriptor.target.entryScriptPath, output);
	appendJsonMemberPrefix("playerPosition", firstTargetMember, output);
	output.push_back('[');
	output += std::to_string(descriptor.target.playerX);
	output.push_back(',');
	output += std::to_string(descriptor.target.playerY);
	output.push_back(']');
	appendJsonMemberPrefix("integerVariables", firstTargetMember, output);
	output.push_back('{');
	bool firstVariable = true;
	for (const auto& variable : descriptor.target.integerVariables)
	{
		appendJsonMemberPrefix(variable.first, firstVariable, output);
		output += std::to_string(variable.second);
	}
	output.push_back('}');
	output.push_back('}');

	appendJsonMemberPrefix("overlayRoot", firstRootMember, output);
	appendEscapedJsonString(hostPathToUtf8(descriptor.overlayRoot), output);
	appendJsonMemberPrefix("isolatedSaveRoot", firstRootMember, output);
	appendEscapedJsonString(hostPathToUtf8(descriptor.isolatedSaveRoot), output);
	appendJsonMemberPrefix("applicationStateRoot", firstRootMember, output);
	appendEscapedJsonString(
		hostPathToUtf8(descriptor.applicationStateRoot), output);
	appendJsonMemberPrefix("diagnosticsPath", firstRootMember, output);
	appendEscapedJsonString(hostPathToUtf8(descriptor.diagnosticsPath), output);
	appendJsonMemberPrefix("logPath", firstRootMember, output);
	appendEscapedJsonString(hostPathToUtf8(descriptor.logPath), output);
	appendJsonMemberPrefix("autoExit", firstRootMember, output);
	output += "{\"mode\":\"manual\"}";
	output.push_back('}');
	return output;
}

}

namespace EditorRun
{
DescriptorResult parseEditorRunDescriptor(std::string_view bytes)
{
	DescriptorResult result;
	if (bytes.size() > MaximumDescriptorBytes)
	{
		setFailure(
			result,
			DescriptorError::FileTooLarge,
			"",
			"Editor-run descriptor exceeds the supported byte limit");
		return result;
	}

	if (bytes.size() >= 3 &&
		static_cast<unsigned char>(bytes[0]) == 0xEF &&
		static_cast<unsigned char>(bytes[1]) == 0xBB &&
		static_cast<unsigned char>(bytes[2]) == 0xBF)
	{
		bytes.remove_prefix(3);
	}
	if (!ResourcePathSafety::isValidUtf8(std::string(bytes)))
	{
		setFailure(
			result,
			DescriptorError::InvalidUtf8,
			"",
			"Editor-run descriptor is not valid UTF-8");
		return result;
	}

	JsonValue root;
	StrictJsonParser parser(bytes);
	if (!parser.parse(root))
	{
		result.error = parser.error();
		result.message = parser.message();
		result.line = parser.errorLine();
		result.column = parser.errorColumn();
		return result;
	}
	if (root.type != JsonType::Object)
	{
		setFailure(
			result,
			DescriptorError::InvalidFieldType,
			"",
			"Editor-run descriptor root must be a JSON object");
		return result;
	}

	Descriptor candidate;
	if (!parseDescriptorObject(root.objectValues, candidate, result))
	{
		return result;
	}
	result.descriptor = std::move(candidate);
	return result;
}

DescriptorSerializationResult serializeEditorRunDescriptor(
	const Descriptor& descriptor)
{
	DescriptorSerializationResult result;
	try
	{
		const std::string candidate = serializeDescriptorUnchecked(descriptor);
		const DescriptorResult validation =
			parseEditorRunDescriptor(candidate);
		if (!validation.succeeded())
		{
			result.error = validation.error;
			result.fieldPath = validation.fieldPath;
			result.message = validation.message;
			return result;
		}
		result.bytes = candidate;
		return result;
	}
	catch (const std::exception&)
	{
		result.error = DescriptorError::InvalidHostPath;
		result.message =
			"Editor-run descriptor cannot be represented on this platform";
		return result;
	}
}

DescriptorResult readEditorRunDescriptor(
	const std::filesystem::path& descriptorPath)
{
	DescriptorResult result;
	std::ifstream input(descriptorPath, std::ios::binary);
	if (!input)
	{
		setFailure(
			result,
			DescriptorError::FileOpenFailed,
			"",
			"Unable to open editor-run descriptor");
		return result;
	}

	// Read one byte past the public limit. This remains bounded and also
	// rejects a descriptor that grows after the stream is opened.
	std::vector<char> buffer(MaximumDescriptorBytes + 1);
	input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
	const std::streamsize bytesRead = input.gcount();
	if (input.bad() || bytesRead < 0)
	{
		setFailure(
			result,
			DescriptorError::FileReadFailed,
			"",
			"Unable to read the complete editor-run descriptor");
		return result;
	}
	if (static_cast<std::size_t>(bytesRead) > MaximumDescriptorBytes)
	{
		setFailure(
			result,
			DescriptorError::FileTooLarge,
			"",
			"Editor-run descriptor exceeds the supported byte limit");
		return result;
	}
	const std::string bytes(
		buffer.data(), static_cast<std::size_t>(bytesRead));
	return parseEditorRunDescriptor(bytes);
}
}
