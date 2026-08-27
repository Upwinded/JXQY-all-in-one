#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace EditorRun
{
constexpr std::size_t MaximumDescriptorBytes = 1024 * 1024;
constexpr std::size_t MaximumJsonDepth = 16;
constexpr std::size_t MaximumIntegerVariableCount = 4096;
constexpr std::size_t MaximumStringBytes = 64 * 1024;

enum class DescriptorError
{
	None,
	FileOpenFailed,
	FileReadFailed,
	FileTooLarge,
	InvalidUtf8,
	InvalidJson,
	MaximumDepthExceeded,
	StringTooLarge,
	DuplicateKey,
	UnknownField,
	MissingField,
	InvalidFieldType,
	UnsupportedVersion,
	InvalidValue,
	UnsafeVirtualPath,
	InvalidHostPath,
	TooManyIntegerVariables
};

enum class TargetKind
{
	Scene,
	Map,
	Script
};

struct SceneTarget
{
	TargetKind kind = TargetKind::Scene;
	std::string sceneId;
	std::string sceneName;
	std::string mapPath;
	std::string npcPath;
	std::string objectPath;
	std::string entryScriptPath;
	std::int32_t playerX = 0;
	std::int32_t playerY = 0;
	std::map<std::string, std::int32_t> integerVariables;
};

struct Descriptor
{
	static constexpr std::int32_t LegacySchemaVersion = 1;
	static constexpr std::int32_t SchemaVersion = 2;

	static constexpr bool supportsSchemaVersion(
		std::int32_t version) noexcept
	{
		return version == LegacySchemaVersion ||
			version == SchemaVersion;
	}

	// The schema version read from the source bytes. New descriptors default
	// to the current schema; serialization always emits the current schema.
	std::int32_t sourceSchemaVersion = SchemaVersion;
	std::string sessionId;
	std::filesystem::path assetsCollectionRoot;
	std::string activeResourcePackId;
	// Optional stable resources.ini/root entry key used to preserve an exact
	// selection when multiple catalog entries share the same Game.Id.
	std::string activeResourcePackEntryKey;
	SceneTarget target;
	std::filesystem::path overlayRoot;
	std::filesystem::path isolatedSaveRoot;
	std::filesystem::path applicationStateRoot;
	std::filesystem::path diagnosticsPath;
	std::filesystem::path logPath;
};

struct DescriptorResult
{
	Descriptor descriptor;
	DescriptorError error = DescriptorError::None;
	std::string fieldPath;
	std::string message;
	std::size_t line = 0;
	std::size_t column = 0;

	bool succeeded() const noexcept
	{
		return error == DescriptorError::None;
	}
};

struct DescriptorSerializationResult
{
	std::string bytes;
	DescriptorError error = DescriptorError::None;
	std::string fieldPath;
	std::string message;

	bool succeeded() const noexcept
	{
		return error == DescriptorError::None;
	}
};

// Parses one complete UTF-8 JSON descriptor. A UTF-8 BOM is accepted only at
// the beginning. The result is assigned from a complete candidate only after
// every schema and path check succeeds.
DescriptorResult parseEditorRunDescriptor(std::string_view bytes);

// Serializes the current schema and validates the generated JSON once with the
// same parser used by the runtime.
DescriptorSerializationResult serializeEditorRunDescriptor(
	const Descriptor& descriptor);

// Reads at most MaximumDescriptorBytes and then applies the same parser.
DescriptorResult readEditorRunDescriptor(
	const std::filesystem::path& descriptorPath);
}
