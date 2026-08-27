#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace EditorRun
{
inline constexpr std::uint32_t RuntimeTraceSchemaVersion = 1;
inline constexpr std::uint64_t RuntimeTraceMaximumExactJsonInteger =
	9'007'199'254'740'991ULL;

inline constexpr std::string_view RuntimeTraceSessionStartEventType =
	"session.start";
inline constexpr std::string_view RuntimeTraceSessionFinishEventType =
	"session.finish";
inline constexpr std::string_view RuntimeTraceScriptStartEventType =
	"script.start";
inline constexpr std::string_view RuntimeTraceScriptFinishEventType =
	"script.finish";
inline constexpr std::string_view RuntimeTraceSourceLineEventType =
	"source.line";
inline constexpr std::string_view RuntimeTraceApiCallEventType =
	"api.call";
inline constexpr std::string_view RuntimeTraceMapChangeEventType =
	"map.change";
inline constexpr std::string_view RuntimeTraceVariableChangeEventType =
	"variable.change";
inline constexpr std::string_view RuntimeTraceDroppedEventType =
	"trace.dropped";

enum class RuntimeTraceRootKind
{
	Active,
	DependencyId,
	Common
};

enum class RuntimeTraceSourceLayer
{
	Formal,
	Overlay
};

enum class RuntimeTraceSessionFinishStatus
{
	Completed,
	ResourceFailure,
	EngineFailure,
	SceneFailure,
	OrchestrationFailure
};

enum class RuntimeTraceScriptFinishStatus
{
	Completed,
	LoadError,
	RuntimeError,
	Aborted
};

enum class RuntimeTraceVariableValueType
{
	Integer,
	Real,
	String,
	Boolean,
	Nil
};

struct RuntimeTraceScriptIdentity
{
	std::string virtualPath;
	std::string contentSha256;
	RuntimeTraceRootKind rootKind =
		RuntimeTraceRootKind::Active;
	std::uint64_t rootOrdinal = 0;
	std::optional<std::string> resourcePackId;
	RuntimeTraceSourceLayer sourceLayer =
		RuntimeTraceSourceLayer::Formal;
};

struct RuntimeTraceSessionStartEvent
{
};

struct RuntimeTraceSessionFinishEvent
{
	RuntimeTraceSessionFinishStatus status =
		RuntimeTraceSessionFinishStatus::Completed;
};

struct RuntimeTraceScriptStartEvent
{
	std::uint64_t executionId = 0;
	std::optional<std::uint64_t> parentExecutionId;
	RuntimeTraceScriptIdentity source;
};

struct RuntimeTraceScriptFinishEvent
{
	std::uint64_t executionId = 0;
	RuntimeTraceScriptFinishStatus status =
		RuntimeTraceScriptFinishStatus::Completed;
};

struct RuntimeTraceSourceLineEvent
{
	std::uint64_t executionId = 0;
	std::uint64_t line = 0;
};

struct RuntimeTraceApiCallEvent
{
	std::uint64_t executionId = 0;
	std::string apiName;
};

struct RuntimeTraceMapChangeEvent
{
	std::optional<std::uint64_t> executionId;
	std::string target;
};

struct RuntimeTraceVariableChangeEvent
{
	std::optional<std::uint64_t> executionId;
	std::string variableName;
	RuntimeTraceVariableValueType valueType =
		RuntimeTraceVariableValueType::Integer;
	std::string beforeValue;
	std::string afterValue;
};

struct RuntimeTraceDroppedEvent
{
	std::uint64_t droppedSourceLineCount = 0;
};

using RuntimeTraceEventPayload = std::variant<
	RuntimeTraceSessionStartEvent,
	RuntimeTraceSessionFinishEvent,
	RuntimeTraceScriptStartEvent,
	RuntimeTraceScriptFinishEvent,
	RuntimeTraceSourceLineEvent,
	RuntimeTraceApiCallEvent,
	RuntimeTraceMapChangeEvent,
	RuntimeTraceVariableChangeEvent,
	RuntimeTraceDroppedEvent>;

struct RuntimeTraceEvent
{
	std::optional<std::uint64_t> elapsedMicroseconds;
	RuntimeTraceEventPayload payload;
};

struct RuntimeTraceRecord
{
	std::string sessionId;
	std::uint64_t sequence = 0;
	RuntimeTraceEvent event;
};

enum class RuntimeTraceValidationError
{
	None,
	InvalidSessionId,
	IntegerOutOfRange,
	InvalidVirtualPath,
	InvalidSha256,
	InvalidResourcePackId,
	InvalidApiName,
	InvalidVariableName,
	InvalidVariableValue
};

struct RuntimeTraceValidationResult
{
	RuntimeTraceValidationError error =
		RuntimeTraceValidationError::None;
	std::string fieldPath;

	bool succeeded() const noexcept
	{
		return error == RuntimeTraceValidationError::None;
	}
};

std::string_view runtimeTraceRootKindName(
	RuntimeTraceRootKind value) noexcept;
std::string_view runtimeTraceSourceLayerName(
	RuntimeTraceSourceLayer value) noexcept;
std::string_view runtimeTraceSessionFinishStatusName(
	RuntimeTraceSessionFinishStatus value) noexcept;
std::string_view runtimeTraceScriptFinishStatusName(
	RuntimeTraceScriptFinishStatus value) noexcept;
std::string_view runtimeTraceVariableValueTypeName(
	RuntimeTraceVariableValueType value) noexcept;
std::string_view runtimeTraceEventTypeName(
	const RuntimeTraceEvent& event) noexcept;

bool runtimeTraceEventIsDroppable(
	const RuntimeTraceEvent& event) noexcept;
std::size_t runtimeTraceEventRetainedBytes(
	const RuntimeTraceEvent& event) noexcept;

RuntimeTraceValidationResult validateRuntimeTraceEvent(
	const RuntimeTraceEvent& event);
RuntimeTraceValidationResult validateRuntimeTraceRecord(
	const RuntimeTraceRecord& record);

// Emits one deterministic, strict UTF-8 JSON object followed by LF. Output is
// cleared and remains empty when validation fails.
RuntimeTraceValidationResult serializeRuntimeTraceRecord(
	const RuntimeTraceRecord& record,
	std::string& output);

// SHA-256 of the exact supplied bytes, encoded as 64 lowercase hex digits.
std::string runtimeTraceSha256Hex(std::string_view bytes);
}
