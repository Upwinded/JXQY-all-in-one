#include "../Launch/EditorRunDescriptor.h"
#include "../Launch/GameLaunchArguments.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

std::string quoteJson(const std::string& value)
{
	std::string result = "\"";
	for (const unsigned char character : value)
	{
		switch (character)
		{
		case '"':
			result += "\\\"";
			break;
		case '\\':
			result += "\\\\";
			break;
		case '\b':
			result += "\\b";
			break;
		case '\f':
			result += "\\f";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\t':
			result += "\\t";
			break;
		default:
			result.push_back(static_cast<char>(character));
			break;
		}
	}
	result += '"';
	return result;
}

std::string hostPath(const std::string& leaf)
{
#ifdef _WIN32
	return "C:/Temp/jxqy-editor-runs/session/" + leaf;
#else
	return "/tmp/jxqy-editor-runs/session/" + leaf;
#endif
}

void appendField(
	std::vector<std::string>& fields,
	const std::set<std::string>& omitted,
	const std::string& name,
	std::string value)
{
	if (omitted.find(name) == omitted.end())
	{
		fields.push_back(quoteJson(name) + ":" + std::move(value));
	}
}

std::string objectJson(const std::vector<std::string>& fields)
{
	std::string result = "{";
	for (std::size_t index = 0; index < fields.size(); ++index)
	{
		if (index != 0)
		{
			result += ',';
		}
		result += fields[index];
	}
	result += '}';
	return result;
}

struct DescriptorFixture
{
	std::string schemaVersion = "1";
	std::string sessionId = "c7b3fe3a-7a51-4a72-86d7-eab017e97649";
	std::string activeResourcePackId = "MY_MOD";
	std::string targetKind = "scene";
	std::string sceneId = "scene-zhongdu";
	std::string sceneName = u8"中都 测试";
	std::string mapPath = u8"map/中都.map";
	std::string npcPath;
	std::string objectPath;
	std::string entryScriptPath;
	std::string playerPosition = "[-2147483648,2147483647]";
	std::string integerVariables =
		"{\"Event\":-2147483648,\"chapter\":2147483647}";
	std::string autoExitMode = "manual";
	std::set<std::string> omittedTopFields;
	std::set<std::string> omittedTargetFields;
	std::set<std::string> omittedAutoExitFields;
	std::vector<std::string> extraTopFields;
	std::vector<std::string> extraTargetFields;
	std::vector<std::string> extraAutoExitFields;

	std::string json() const
	{
		std::vector<std::string> targetFields;
		appendField(
			targetFields, omittedTargetFields, "kind", quoteJson(targetKind));
		appendField(
			targetFields, omittedTargetFields, "sceneId", quoteJson(sceneId));
		appendField(
			targetFields, omittedTargetFields, "sceneName", quoteJson(sceneName));
		appendField(
			targetFields, omittedTargetFields, "map", quoteJson(mapPath));
		appendField(
			targetFields, omittedTargetFields, "npc", quoteJson(npcPath));
		appendField(
			targetFields, omittedTargetFields, "object", quoteJson(objectPath));
		appendField(
			targetFields,
			omittedTargetFields,
			"entryScript",
			quoteJson(entryScriptPath));
		appendField(
			targetFields,
			omittedTargetFields,
			"playerPosition",
			playerPosition);
		appendField(
			targetFields,
			omittedTargetFields,
			"integerVariables",
			integerVariables);
		targetFields.insert(
			targetFields.end(),
			extraTargetFields.begin(),
			extraTargetFields.end());

		std::vector<std::string> autoExitFields;
		appendField(
			autoExitFields,
			omittedAutoExitFields,
			"mode",
			quoteJson(autoExitMode));
		autoExitFields.insert(
			autoExitFields.end(),
			extraAutoExitFields.begin(),
			extraAutoExitFields.end());

		std::vector<std::string> topFields;
		appendField(
			topFields, omittedTopFields, "schemaVersion", schemaVersion);
		appendField(
			topFields, omittedTopFields, "sessionId", quoteJson(sessionId));
		appendField(
			topFields,
			omittedTopFields,
			"assetsCollectionRoot",
			quoteJson(hostPath("assets")));
		appendField(
			topFields,
			omittedTopFields,
			"activeResourcePackId",
			quoteJson(activeResourcePackId));
		appendField(
			topFields,
			omittedTopFields,
			"target",
			objectJson(targetFields));
		appendField(
			topFields,
			omittedTopFields,
			"overlayRoot",
			quoteJson(hostPath("overlay")));
		appendField(
			topFields,
			omittedTopFields,
			"isolatedSaveRoot",
			quoteJson(hostPath("save")));
		appendField(
			topFields,
			omittedTopFields,
			"applicationStateRoot",
			quoteJson(hostPath("application-state")));
		appendField(
			topFields,
			omittedTopFields,
			"diagnosticsPath",
			quoteJson(hostPath("diagnostics/events.jsonl")));
		appendField(
			topFields,
			omittedTopFields,
			"logPath",
			quoteJson(hostPath("diagnostics/game.log")));
		appendField(
			topFields,
			omittedTopFields,
			"autoExit",
			objectJson(autoExitFields));
		topFields.insert(
			topFields.end(), extraTopFields.begin(), extraTopFields.end());
		return objectJson(topFields);
	}
};

bool testValidDescriptor()
{
	DescriptorFixture fixture;
	fixture.npcPath = u8"ini\\npc\\中都.npc";
	fixture.objectPath = "ini/obj/zhongdu.obj";
	fixture.entryScriptPath = u8"script/map/中都/入口.txt";

	const EditorRun::DescriptorResult result =
		EditorRun::parseEditorRunDescriptor(fixture.json());
	bool ok = check(result.succeeded(), "valid descriptor parses");
	ok = check(
		result.descriptor.sessionId == fixture.sessionId &&
			result.descriptor.activeResourcePackId == "MY_MOD" &&
			result.descriptor.target.sceneName == u8"中都 测试",
		"descriptor preserves required UTF-8 identity fields") && ok;
	ok = check(
		result.descriptor.target.mapPath == u8"map/中都.map" &&
			result.descriptor.target.npcPath == u8"ini/npc/中都.npc" &&
			result.descriptor.target.objectPath == "ini/obj/zhongdu.obj" &&
			result.descriptor.target.entryScriptPath ==
				u8"script/map/中都/入口.txt",
		"descriptor normalizes strict virtual resource paths") && ok;
	ok = check(
		result.descriptor.target.playerX ==
				std::numeric_limits<std::int32_t>::min() &&
			result.descriptor.target.playerY ==
				std::numeric_limits<std::int32_t>::max() &&
			result.descriptor.target.integerVariables.at("Event") ==
				std::numeric_limits<std::int32_t>::min() &&
			result.descriptor.target.integerVariables.at("chapter") ==
				std::numeric_limits<std::int32_t>::max(),
		"descriptor accepts exact int32 boundaries") && ok;
	ok = check(
		result.descriptor.assetsCollectionRoot.is_absolute() &&
			result.descriptor.overlayRoot.is_absolute() &&
			result.descriptor.isolatedSaveRoot.is_absolute() &&
			result.descriptor.applicationStateRoot.is_absolute() &&
			result.descriptor.diagnosticsPath.is_absolute() &&
			result.descriptor.logPath.is_absolute(),
		"descriptor stores every host path as absolute") && ok;

	std::string withBom("\xEF\xBB\xBF", 3);
	withBom += fixture.json();
	ok = check(
		EditorRun::parseEditorRunDescriptor(withBom).succeeded(),
		"UTF-8 BOM is accepted only as a leading marker") && ok;
	return ok;
}

bool testDescriptorEncodingAndJsonLimits()
{
	bool ok = true;
	DescriptorFixture fixture;
	fixture.sceneName = std::string("\xC3\x28", 2);
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidUtf8,
		"invalid UTF-8 is rejected before schema parsing") && ok;

	ok = check(
		EditorRun::parseEditorRunDescriptor("{\"schemaVersion\":1").error ==
			EditorRun::DescriptorError::InvalidJson,
		"truncated JSON is rejected") && ok;

	std::string oversized(EditorRun::MaximumDescriptorBytes + 1, ' ');
	ok = check(
		EditorRun::parseEditorRunDescriptor(oversized).error ==
			EditorRun::DescriptorError::FileTooLarge,
		"descriptor byte limit is enforced before parsing") && ok;

	fixture = DescriptorFixture();
	fixture.sceneName.assign(EditorRun::MaximumStringBytes + 1, 'x');
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::StringTooLarge,
		"decoded JSON string byte limit is enforced") && ok;

	std::string deep = "{\"x\":";
	for (std::size_t index = 0;
		index < EditorRun::MaximumJsonDepth + 1; ++index)
	{
		deep += '[';
	}
	deep += '0';
	for (std::size_t index = 0;
		index < EditorRun::MaximumJsonDepth + 1; ++index)
	{
		deep += ']';
	}
	deep += '}';
	ok = check(
		EditorRun::parseEditorRunDescriptor(deep).error ==
			EditorRun::DescriptorError::MaximumDepthExceeded,
		"JSON nesting depth limit is enforced") && ok;

	fixture = DescriptorFixture();
	fixture.extraTopFields.push_back("\"schemaVersion\":1");
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::DuplicateKey,
		"duplicate top-level keys are rejected") && ok;

	fixture = DescriptorFixture();
	fixture.integerVariables = "{\"same\":1,\"same\":2}";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::DuplicateKey,
		"duplicate variable names are rejected by the JSON parser") && ok;
	return ok;
}

bool testDescriptorStrictSchema()
{
	bool ok = true;
	const std::vector<std::string> topFields = {
		"schemaVersion",
		"sessionId",
		"assetsCollectionRoot",
		"activeResourcePackId",
		"target",
		"overlayRoot",
		"isolatedSaveRoot",
		"applicationStateRoot",
		"diagnosticsPath",
		"logPath",
		"autoExit"
	};
	for (const std::string& field : topFields)
	{
		DescriptorFixture fixture;
		fixture.omittedTopFields.insert(field);
		ok = check(
			EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
				EditorRun::DescriptorError::MissingField,
			"missing top-level field is rejected: " + field) && ok;
	}

	const std::vector<std::string> targetFields = {
		"kind",
		"sceneId",
		"sceneName",
		"map",
		"npc",
		"object",
		"entryScript",
		"playerPosition",
		"integerVariables"
	};
	for (const std::string& field : targetFields)
	{
		DescriptorFixture fixture;
		fixture.omittedTargetFields.insert(field);
		ok = check(
			EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
				EditorRun::DescriptorError::MissingField,
			"missing target field is rejected: " + field) && ok;
	}

	DescriptorFixture fixture;
	fixture.omittedAutoExitFields.insert("mode");
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::MissingField,
		"missing auto-exit mode is rejected") && ok;

	fixture = DescriptorFixture();
	fixture.extraTopFields.push_back("\"future\":true");
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::UnknownField,
		"unknown top-level fields are rejected") && ok;

	fixture = DescriptorFixture();
	fixture.extraTargetFields.push_back("\"future\":true");
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::UnknownField,
		"unknown target fields are rejected") && ok;

	fixture = DescriptorFixture();
	fixture.extraAutoExitFields.push_back("\"future\":true");
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::UnknownField,
		"unknown auto-exit fields are rejected") && ok;

	fixture = DescriptorFixture();
	fixture.schemaVersion = "3";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::UnsupportedVersion,
		"future descriptor versions are rejected") && ok;

	fixture = DescriptorFixture();
	fixture.targetKind = "map";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"v1 rejects non-scene target kinds") && ok;

	fixture = DescriptorFixture();
	fixture.autoExitMode = "after-load";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"v1 rejects non-manual auto-exit modes") && ok;
	return ok;
}

bool testVersion2TargetKinds()
{
	bool ok = true;

	DescriptorFixture fixture;
	fixture.schemaVersion = "2";
	fixture.targetKind = "map";
	const EditorRun::DescriptorResult mapResult =
		EditorRun::parseEditorRunDescriptor(fixture.json());
	ok = check(
		mapResult.succeeded() &&
			mapResult.descriptor.target.kind ==
				EditorRun::TargetKind::Map,
		"v2 accepts a map target without an entry script") && ok;

	fixture.targetKind = "script";
	fixture.entryScriptPath = "script/map/current.lua";
	const EditorRun::DescriptorResult scriptResult =
		EditorRun::parseEditorRunDescriptor(fixture.json());
	ok = check(
		scriptResult.succeeded() &&
			scriptResult.descriptor.target.kind ==
				EditorRun::TargetKind::Script,
		"v2 accepts a script target with an entry script") && ok;

	fixture.targetKind = "map";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"v2 map targets reject entry scripts") && ok;

	fixture.targetKind = "script";
	fixture.entryScriptPath.clear();
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"v2 script targets require an entry script") && ok;

	fixture.targetKind = "future";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"v2 rejects unknown target kinds") && ok;
	return ok;
}

bool testDescriptorValuesAndPaths()
{
	bool ok = true;
	DescriptorFixture fixture;
	fixture.sessionId = "C7B3FE3A-7A51-4A72-86D7-EAB017E97649";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).succeeded(),
		"session identifier does not impose UUID letter casing") && ok;

	fixture = DescriptorFixture();
	fixture.sessionId = "c7b3fe3a7a514a7286d7eab017e97649";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).succeeded(),
		"session identifier does not impose a UUID representation") && ok;

	fixture = DescriptorFixture();
	fixture.playerPosition = "[2147483648,0]";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"player position overflow is rejected") && ok;

	fixture = DescriptorFixture();
	fixture.playerPosition = "[1.0,0]";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidFieldType,
		"floating-point coordinates are rejected") && ok;

	fixture = DescriptorFixture();
	fixture.integerVariables = "{\"chapter\":-2147483649}";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::InvalidValue,
		"integer variable underflow is rejected") && ok;

	fixture = DescriptorFixture();
	std::vector<std::string> variables;
	for (std::size_t index = 0;
		index < EditorRun::MaximumIntegerVariableCount + 1; ++index)
	{
		variables.push_back("\"v" + std::to_string(index) + "\":0");
	}
	fixture.integerVariables = objectJson(variables);
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::TooManyIntegerVariables,
		"integer variable count limit is enforced") && ok;

	const std::vector<std::string> unsafePaths = {
		"",
		"../outside.map",
		"./map.map",
		"map//bad.map",
		"/map/rooted.map",
		"C:/map/drive.map",
		"map/CON.txt"
	};
	for (const std::string& path : unsafePaths)
	{
		fixture = DescriptorFixture();
		fixture.mapPath = path;
		ok = check(
			EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
				EditorRun::DescriptorError::UnsafeVirtualPath,
			"unsafe required virtual path is rejected: " + path) && ok;
	}

	fixture = DescriptorFixture();
	fixture.npcPath = "../outside.npc";
	ok = check(
		EditorRun::parseEditorRunDescriptor(fixture.json()).error ==
			EditorRun::DescriptorError::UnsafeVirtualPath,
		"non-empty optional virtual path remains strict") && ok;

	fixture = DescriptorFixture();
	std::string relativeHost = fixture.json();
	const std::string absoluteAssets = quoteJson(hostPath("assets"));
	const std::size_t assetsPosition = relativeHost.find(absoluteAssets);
	if (assetsPosition != std::string::npos)
	{
		relativeHost.replace(
			assetsPosition, absoluteAssets.size(), "\"relative/assets\"");
	}
	ok = check(
		assetsPosition != std::string::npos &&
			EditorRun::parseEditorRunDescriptor(relativeHost).error ==
				EditorRun::DescriptorError::InvalidHostPath,
		"relative host paths are rejected") && ok;
	return ok;
}

bool testDescriptorFileReader()
{
	namespace fs = std::filesystem;
	bool ok = true;
	std::error_code errorCode;
	const auto suffix =
		std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path directory =
		fs::temp_directory_path(errorCode) /
		("jxqy_editor_run_descriptor_" + std::to_string(suffix));
	if (errorCode || !fs::create_directories(directory, errorCode))
	{
		return check(false, "descriptor reader test directory is available");
	}

	const fs::path descriptorPath = directory / u8"中文 descriptor.json";
	{
		std::ofstream output(descriptorPath, std::ios::binary);
		const std::string bytes = DescriptorFixture().json();
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	ok = check(
		EditorRun::readEditorRunDescriptor(descriptorPath).succeeded(),
		"descriptor file reader supports UTF-8 and spaces in its host path") && ok;
	ok = check(
		EditorRun::readEditorRunDescriptor(directory / "missing.json").error ==
			EditorRun::DescriptorError::FileOpenFailed,
		"descriptor file reader distinguishes a missing file") && ok;
	const fs::path oversizedPath = directory / "oversized.json";
	{
		std::ofstream output(oversizedPath, std::ios::binary);
		const std::string bytes(EditorRun::MaximumDescriptorBytes + 1, ' ');
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	ok = check(
		EditorRun::readEditorRunDescriptor(oversizedPath).error ==
			EditorRun::DescriptorError::FileTooLarge,
		"descriptor file reader enforces the bounded-read limit") && ok;
	fs::remove_all(directory, errorCode);
	return ok;
}

bool testDescriptorSerializerRoundTrip()
{
	bool ok = true;
	DescriptorFixture fixture;
	fixture.npcPath = u8"ini/npc/中都.npc";
	fixture.objectPath = "ini/obj/zhongdu.obj";
	fixture.entryScriptPath = u8"script/map/中都/入口.txt";
	const EditorRun::DescriptorResult parsed =
		EditorRun::parseEditorRunDescriptor(fixture.json());
	if (!check(parsed.succeeded(),
			"serializer fixture parses before round-trip"))
	{
		return false;
	}

	EditorRun::Descriptor descriptor = parsed.descriptor;
	descriptor.activeResourcePackEntryKey = "pack.mod-b";
	descriptor.target.sceneName = u8"中都 \"引号\" \\ 反斜杠";
	descriptor.target.integerVariables.clear();
	descriptor.target.integerVariables.emplace(
		u8"变量\"\\名称", std::numeric_limits<std::int32_t>::min());
	descriptor.target.integerVariables.emplace(
		"chapter", std::numeric_limits<std::int32_t>::max());

	const EditorRun::DescriptorSerializationResult serialized =
		EditorRun::serializeEditorRunDescriptor(descriptor);
	ok = check(
		serialized.succeeded() && !serialized.bytes.empty(),
		"valid descriptor serializes only after strict validation") && ok;
	ok = check(
		serialized.bytes.find("\"schemaVersion\":2") !=
			std::string::npos,
		"serializer emits the current v2 schema") && ok;
	const EditorRun::DescriptorSerializationResult serializedAgain =
		EditorRun::serializeEditorRunDescriptor(descriptor);
	ok = check(
		serializedAgain.succeeded() &&
			serializedAgain.bytes == serialized.bytes,
		"descriptor serialization is deterministic") && ok;

	const EditorRun::DescriptorResult roundTrip =
		EditorRun::parseEditorRunDescriptor(serialized.bytes);
	ok = check(
		roundTrip.succeeded() &&
			roundTrip.descriptor.sessionId == descriptor.sessionId &&
			roundTrip.descriptor.assetsCollectionRoot ==
				descriptor.assetsCollectionRoot &&
			roundTrip.descriptor.activeResourcePackId ==
				descriptor.activeResourcePackId &&
			roundTrip.descriptor.activeResourcePackEntryKey ==
				descriptor.activeResourcePackEntryKey &&
			roundTrip.descriptor.target.kind ==
				descriptor.target.kind &&
			roundTrip.descriptor.target.sceneId ==
				descriptor.target.sceneId &&
			roundTrip.descriptor.target.sceneName ==
				descriptor.target.sceneName &&
			roundTrip.descriptor.target.mapPath ==
				descriptor.target.mapPath &&
			roundTrip.descriptor.target.npcPath ==
				descriptor.target.npcPath &&
			roundTrip.descriptor.target.objectPath ==
				descriptor.target.objectPath &&
			roundTrip.descriptor.target.entryScriptPath ==
				descriptor.target.entryScriptPath &&
			roundTrip.descriptor.target.playerX ==
				descriptor.target.playerX &&
			roundTrip.descriptor.target.playerY ==
				descriptor.target.playerY &&
			roundTrip.descriptor.target.integerVariables ==
				descriptor.target.integerVariables &&
			roundTrip.descriptor.overlayRoot == descriptor.overlayRoot &&
			roundTrip.descriptor.isolatedSaveRoot ==
				descriptor.isolatedSaveRoot &&
			roundTrip.descriptor.applicationStateRoot ==
				descriptor.applicationStateRoot &&
			roundTrip.descriptor.diagnosticsPath ==
				descriptor.diagnosticsPath &&
			roundTrip.descriptor.logPath == descriptor.logPath,
		"runtime parser recovers every serialized descriptor field") && ok;

	EditorRun::Descriptor mapDescriptor = descriptor;
	mapDescriptor.target.kind = EditorRun::TargetKind::Map;
	mapDescriptor.target.entryScriptPath.clear();
	const EditorRun::DescriptorSerializationResult serializedMap =
		EditorRun::serializeEditorRunDescriptor(mapDescriptor);
	const EditorRun::DescriptorResult parsedMap =
		EditorRun::parseEditorRunDescriptor(serializedMap.bytes);
	ok = check(
		serializedMap.succeeded() &&
			parsedMap.succeeded() &&
			parsedMap.descriptor.target.kind ==
				EditorRun::TargetKind::Map,
		"serializer round-trips a v2 map target") && ok;

	EditorRun::Descriptor scriptDescriptor = descriptor;
	scriptDescriptor.target.kind = EditorRun::TargetKind::Script;
	const EditorRun::DescriptorSerializationResult serializedScript =
		EditorRun::serializeEditorRunDescriptor(scriptDescriptor);
	const EditorRun::DescriptorResult parsedScript =
		EditorRun::parseEditorRunDescriptor(serializedScript.bytes);
	ok = check(
		serializedScript.succeeded() &&
			parsedScript.succeeded() &&
			parsedScript.descriptor.target.kind ==
				EditorRun::TargetKind::Script,
		"serializer round-trips a v2 script target") && ok;

	EditorRun::Descriptor invalid = descriptor;
	invalid.sessionId.clear();
	const EditorRun::DescriptorSerializationResult invalidResult =
		EditorRun::serializeEditorRunDescriptor(invalid);
	ok = check(
		!invalidResult.succeeded() &&
			invalidResult.bytes.empty() &&
			invalidResult.error == EditorRun::DescriptorError::InvalidValue &&
			invalidResult.fieldPath == "sessionId",
		"serializer rejects an empty required session identifier") &&
		ok;

	invalid = descriptor;
	invalid.target.sceneName.assign(
		EditorRun::MaximumStringBytes + 1, 'x');
	const EditorRun::DescriptorSerializationResult oversizedResult =
		EditorRun::serializeEditorRunDescriptor(invalid);
	ok = check(
		!oversizedResult.succeeded() &&
			oversizedResult.bytes.empty() &&
			oversizedResult.error ==
				EditorRun::DescriptorError::StringTooLarge,
		"serializer enforces the parser string and byte budgets") && ok;

	invalid = descriptor;
	invalid.target.mapPath = "map\\normalized-by-parser.map";
	const EditorRun::DescriptorSerializationResult
		nonCanonicalVirtualPath =
			EditorRun::serializeEditorRunDescriptor(invalid);
	const EditorRun::DescriptorResult normalizedVirtualPath =
		EditorRun::parseEditorRunDescriptor(
			nonCanonicalVirtualPath.bytes);
	ok = check(
		nonCanonicalVirtualPath.succeeded() &&
		normalizedVirtualPath.succeeded() &&
		normalizedVirtualPath.descriptor.target.mapPath ==
			"map/normalized-by-parser.map",
		"serializer emits valid descriptors while the parser normalizes portable separators") &&
		ok;

	invalid = descriptor;
	invalid.overlayRoot =
		descriptor.overlayRoot /
		".." /
		descriptor.overlayRoot.filename();
	const EditorRun::DescriptorSerializationResult
		nonCanonicalHostPath =
			EditorRun::serializeEditorRunDescriptor(invalid);
	const EditorRun::DescriptorResult normalizedHostPath =
		EditorRun::parseEditorRunDescriptor(
			nonCanonicalHostPath.bytes);
	ok = check(
		nonCanonicalHostPath.succeeded() &&
		normalizedHostPath.succeeded() &&
		normalizedHostPath.descriptor.overlayRoot ==
			descriptor.overlayRoot.lexically_normal(),
		"serializer emits valid descriptors while the parser normalizes host paths") &&
		ok;
	return ok;
}

GameLaunch::Arguments parseArguments(
	const std::vector<std::string>& arguments,
	bool editorRunEnabled)
{
	std::vector<const char*> pointers;
	pointers.reserve(arguments.size());
	for (const std::string& argument : arguments)
	{
		pointers.push_back(argument.c_str());
	}
	return GameLaunch::parseArguments(
		static_cast<int>(pointers.size()),
		pointers.data(),
		editorRunEnabled);
}

bool testLegacyLaunchArguments()
{
	bool ok = true;
	GameLaunch::Arguments result = parseArguments({ "game.exe" }, true);
	ok = check(
		result.succeeded() &&
			result.mode == GameLaunch::ArgumentMode::Legacy,
		"empty command line stays in legacy mode") && ok;

	result = parseArguments({ "-lf" }, true);
	ok = check(
		result.succeeded() && result.legacy.useLogFile,
		"legacy parser preserves its historical argv[0] option handling") && ok;

	result = parseArguments(
		{
			"game.exe",
			"--assets",
			"first",
			"--assets",
			"second",
			"--user-data-root",
			"first-state",
			"--user-data-root",
			u8"second state 中文",
			"--pack-id",
			"BASE",
			"--resource-id",
			"MOD",
			"--skip-startup-videos",
			"--probe-resource",
			"--enable-automation-hooks",
			"--startup-int",
			"a=1",
			"--startup-int",
			"a=2",
			"--expect-int",
			"done=-1",
			"--test-scenario",
			"7",
			"--post-newgame-wait-ms",
			"25",
			"--exit-after-newgame-script",
			"--unknown"
		},
		true);
	ok = check(
		result.succeeded() &&
			result.legacy.assetsPath == "second" &&
			result.legacy.userDataRootPath ==
				u8"second state 中文" &&
			result.legacy.resourcePackId == "MOD" &&
			result.legacy.skipStartupVideos &&
			result.legacy.probeResource &&
			result.legacy.automationHooksEnabled &&
			result.legacy.autoStartNewGame &&
			result.legacy.exitAfterNewGameScript &&
			result.legacy.postNewGameAutomationWaitMilliseconds == 25,
		"legacy aliases, overwrite order and unknown arguments are preserved") &&
		ok;
	ok = check(
		result.legacy.startupIntegerVariables.size() == 4 &&
			result.legacy.startupIntegerVariables[0] ==
				std::make_pair(std::string("a"), 1) &&
			result.legacy.startupIntegerVariables[1] ==
				std::make_pair(std::string("a"), 2) &&
			result.legacy.expectedIntegerVariables.size() == 1,
		"legacy variable arguments append in encounter order") && ok;

	result = parseArguments({ "game.exe" }, true);
	ok = check(
		result.succeeded() &&
			!result.legacy.automationHooksEnabled,
		"automation hooks stay disabled without explicit launch authorization") &&
		ok;

	struct RestrictedAutomationArgument
	{
		std::vector<std::string> arguments;
		const char* description = nullptr;
	};
	const std::vector<RestrictedAutomationArgument>
		restrictedAutomationArguments =
	{
		{ { "--startup-int", "test_value=1" },
			"startup variable injection" },
		{ { "--expect-int", "test_value=1" },
			"runtime variable assertions" },
		{ { "--test-scenario-choice", "1" },
			"test scenario choice" },
		{ { "--test-scenario", "1" },
			"test scenario alias" },
		{ { "--open-test-runner" },
			"test runner alias" }
	};
	for (const RestrictedAutomationArgument& restricted :
		restrictedAutomationArguments)
	{
		std::vector<std::string> unauthorized = { "game.exe" };
		unauthorized.insert(
			unauthorized.end(),
			restricted.arguments.begin(),
			restricted.arguments.end());
		result = parseArguments(unauthorized, true);
		ok = check(
			result.error ==
				GameLaunch::ArgumentError::
					UnauthorizedAutomation,
			restricted.description) && ok;

		for (bool authorizationFirst : { false, true })
		{
			std::vector<std::string> authorized = { "game.exe" };
			if (authorizationFirst)
			{
				authorized.push_back(
					"--enable-automation-hooks");
			}
			authorized.insert(
				authorized.end(),
				restricted.arguments.begin(),
				restricted.arguments.end());
			if (!authorizationFirst)
			{
				authorized.push_back(
					"--enable-automation-hooks");
			}
			result = parseArguments(authorized, true);
			ok = check(
				result.succeeded() &&
					result.legacy.
						automationHooksEnabled,
				"restricted automation accepts explicit authorization in either argument order") &&
				ok;
		}
	}

	result = parseArguments({ "game.exe", "--log-file" }, true);
	ok = check(
		result.succeeded() && result.legacy.useLogFile &&
			result.legacy.logFilePath.empty(),
		"legacy --log-file without a value keeps its historical behavior") && ok;

	result = parseArguments({ "game.exe", "--assets" }, true);
	ok = check(
		result.error == GameLaunch::ArgumentError::MissingValue,
		"legacy required value errors remain explicit") && ok;
	result = parseArguments(
		{ "game.exe", "--user-data-root" }, true);
	ok = check(
		result.error == GameLaunch::ArgumentError::MissingValue,
		"user-data root requires an explicit value") && ok;
	result = parseArguments(
		{ "game.exe", "--post-newgame-wait-ms", "-1" }, true);
	ok = check(
		result.error == GameLaunch::ArgumentError::InvalidValue,
		"legacy invalid integer errors remain explicit") && ok;
	return ok;
}

bool testNewGameAutomationArgumentCombinations()
{
	struct Combination
	{
		std::vector<std::string> arguments;
		bool autoStart = false;
		bool exitAfterScript = false;
		int waitMilliseconds = 0;
		const char* description = nullptr;
	};

	const std::vector<Combination> combinations =
	{
		{ { "game.exe" }, false, false, 0,
			"no automation option keeps the title flow" },
		{ { "game.exe", "--newgame" }, true, false, 0,
			"auto-start alone keeps the game running" },
		{ { "game.exe", "--exit-after-newgame-script" },
			true, true, 0,
			"exit alone auto-starts and exits immediately after the script" },
		{ { "game.exe", "--post-newgame-wait-ms", "25" },
			true, true, 25,
			"wait alone auto-starts, waits, and exits" },
		{ { "game.exe", "--newgame",
			"--exit-after-newgame-script" },
			true, true, 0,
			"auto-start plus exit has immediate-exit semantics" },
		{ { "game.exe", "--newgame",
			"--post-newgame-wait-ms", "25" },
			true, true, 25,
			"auto-start plus wait has delayed-exit semantics" },
		{ { "game.exe", "--exit-after-newgame-script",
			"--post-newgame-wait-ms", "25" },
			true, true, 25,
			"exit plus wait has delayed-exit semantics" },
		{ { "game.exe", "--newgame",
			"--exit-after-newgame-script",
			"--post-newgame-wait-ms", "25" },
			true, true, 25,
			"all automation stages compose deterministically" }
	};

	bool ok = true;
	for (const Combination& combination : combinations)
	{
		const GameLaunch::Arguments result =
			parseArguments(combination.arguments, true);
		ok = check(
			result.succeeded() &&
				result.legacy.autoStartNewGame ==
					combination.autoStart &&
				result.legacy.exitAfterNewGameScript ==
					combination.exitAfterScript &&
				result.legacy.
					postNewGameAutomationWaitMilliseconds ==
					combination.waitMilliseconds,
			combination.description) && ok;
	}
	return ok;
}

bool testEditorRunLaunchArguments()
{
	bool ok = true;
	GameLaunch::Arguments result = parseArguments(
		{ "game.exe", "--editor-run", u8"C:/临时 目录/launch.json" },
		true);
	ok = check(
		result.succeeded() &&
			result.mode == GameLaunch::ArgumentMode::EditorRun &&
			result.editorRunDescriptorPath ==
				u8"C:/临时 目录/launch.json",
		"desktop parser accepts exactly one editor-run descriptor") && ok;

	result = parseArguments({ "game.exe", "--editor-run" }, true);
	ok = check(
		result.error == GameLaunch::ArgumentError::MissingValue,
		"editor-run requires a descriptor value") && ok;

	result = parseArguments(
		{
			"game.exe",
			"--editor-run",
			"one.json",
			"--editor-run",
			"two.json"
		},
		true);
	ok = check(
		result.error == GameLaunch::ArgumentError::DuplicateEditorRun,
		"duplicate editor-run arguments are rejected") && ok;

	result = parseArguments(
		{ "game.exe", "--editor-run", "run.json", "--assets", "assets" },
		true);
	ok = check(
		result.error == GameLaunch::ArgumentError::MixedEditorRunArguments,
		"editor-run cannot be mixed with legacy arguments") && ok;

	result = parseArguments(
		{ "game.exe", "--editor-run", "run.json" },
		false);
	ok = check(
		result.succeeded() &&
			result.mode == GameLaunch::ArgumentMode::Legacy &&
			result.editorRunDescriptorPath.empty(),
		"disabled editor-run support preserves mobile unknown-argument behavior") &&
		ok;
	return ok;
}
}

int main()
{
	bool ok = true;
	ok = testValidDescriptor() && ok;
	ok = testDescriptorEncodingAndJsonLimits() && ok;
	ok = testDescriptorStrictSchema() && ok;
	ok = testVersion2TargetKinds() && ok;
	ok = testDescriptorValuesAndPaths() && ok;
	ok = testDescriptorFileReader() && ok;
	ok = testDescriptorSerializerRoundTrip() && ok;
	ok = testLegacyLaunchArguments() && ok;
	ok = testNewGameAutomationArgumentCombinations() && ok;
	ok = testEditorRunLaunchArguments() && ok;

	if (!ok)
	{
		return 1;
	}
	std::cout << "Editor-run descriptor and launch argument tests passed\n";
	return 0;
}
