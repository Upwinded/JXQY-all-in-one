#pragma once

#include "EditorRunDescriptor.h"
#include "EditorRunResourceRouting.h"

#include <cstdint>
#include <functional>
#include <string>

namespace EditorRun
{
enum class EntryScriptExecutionStatus
{
	Success,
	LoadFailed,
	RuntimeFailed
};

struct EntryScriptExecutionResult
{
	EntryScriptExecutionStatus status =
		EntryScriptExecutionStatus::Success;
	std::uint32_t line = 0;
	std::uint32_t column = 0;
	std::string message;
};

enum class SceneApplicationError
{
	None,
	InvalidPreparedTarget,
	MissingRuntimeCallback,
	PlayerInitializationFailed,
	IntegerVariableFailed,
	MapLoadFailed,
	NpcLoadFailed,
	ObjectLoadFailed,
	PlayerPositionFailed,
	EntryScriptLoadFailed,
	EntryScriptRuntimeFailed
};

struct SceneApplicationCallbacks
{
	std::function<bool(const std::string&, std::int32_t)>
		setIntegerVariable;
	std::function<bool(const ResolvedTargetFile&)> loadMap;
	std::function<bool(const ResolvedTargetFile&)> loadNpc;
	std::function<bool(const ResolvedTargetFile&)> loadObject;
	std::function<bool(std::int32_t, std::int32_t)>
		setPlayerPositionAndCamera;
	std::function<EntryScriptExecutionResult(const ResolvedTargetFile&)>
		runEntryScript;
};

struct SceneApplicationResult
{
	SceneApplicationError error = SceneApplicationError::None;
	std::string diagnosticCode;
	std::string fieldPath;
	std::string virtualPath;
	std::string message;
	std::uint32_t line = 0;
	std::uint32_t column = 0;

	bool succeeded() const noexcept
	{
		return error == SceneApplicationError::None;
	}
};

// Applies an already preflighted scene synchronously and in the only supported
// order: variables, MAP, optional NPC, optional OBJ, player/camera, optional
// entry script. The first failed callback stops the sequence.
SceneApplicationResult applyEditorRunScene(
	const SceneTarget& target,
	const ResolvedSceneTarget& preparedTarget,
	const SceneApplicationCallbacks& callbacks);
}
