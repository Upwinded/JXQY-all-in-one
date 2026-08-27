#include "EditorRunSceneApplication.h"

#include <utility>

namespace
{
void setFailure(
	EditorRun::SceneApplicationResult& result,
	EditorRun::SceneApplicationError error,
	std::string diagnosticCode,
	std::string fieldPath,
	std::string virtualPath,
	std::string message,
	std::uint32_t line = 0,
	std::uint32_t column = 0)
{
	result.error = error;
	result.diagnosticCode = std::move(diagnosticCode);
	result.fieldPath = std::move(fieldPath);
	result.virtualPath = std::move(virtualPath);
	result.message = std::move(message);
	result.line = line;
	result.column = column;
}

bool optionalTargetMatches(
	const std::string& descriptorPath,
	EditorRun::TargetFileKind expectedKind,
	const std::optional<EditorRun::ResolvedTargetFile>& preparedFile)
{
	if (descriptorPath.empty())
	{
		return !preparedFile.has_value();
	}
	return preparedFile.has_value() &&
		preparedFile->kind == expectedKind &&
		preparedFile->virtualPath == descriptorPath;
}

bool preparedTargetMatches(
	const EditorRun::SceneTarget& target,
	const EditorRun::ResolvedSceneTarget& preparedTarget)
{
	return preparedTarget.map.kind ==
			EditorRun::TargetFileKind::Map &&
		preparedTarget.map.virtualPath == target.mapPath &&
		optionalTargetMatches(
			target.npcPath,
			EditorRun::TargetFileKind::Npc,
			preparedTarget.npc) &&
		optionalTargetMatches(
			target.objectPath,
			EditorRun::TargetFileKind::Object,
			preparedTarget.object) &&
		optionalTargetMatches(
			target.entryScriptPath,
			EditorRun::TargetFileKind::EntryScript,
			preparedTarget.entryScript);
}
}

namespace EditorRun
{
SceneApplicationResult applyEditorRunScene(
	const SceneTarget& target,
	const ResolvedSceneTarget& preparedTarget,
	const SceneApplicationCallbacks& callbacks)
{
	SceneApplicationResult result;
	if (!preparedTargetMatches(target, preparedTarget))
	{
		setFailure(
			result,
			SceneApplicationError::InvalidPreparedTarget,
			"editor_run.target.prepared_mismatch",
			"target",
			{},
			"Editor-run scene target does not match its prepared target");
		return result;
	}
	if (!callbacks.setIntegerVariable ||
		!callbacks.loadMap ||
		!callbacks.setPlayerPositionAndCamera ||
		(!target.npcPath.empty() && !callbacks.loadNpc) ||
		(!target.objectPath.empty() && !callbacks.loadObject) ||
		(!target.entryScriptPath.empty() &&
			!callbacks.runEntryScript))
	{
		setFailure(
			result,
			SceneApplicationError::MissingRuntimeCallback,
			"editor_run.target.runtime_unavailable",
			"target",
			{},
			"Editor-run scene target runtime callback is unavailable");
		return result;
	}

	for (const auto& variable : target.integerVariables)
	{
		if (!callbacks.setIntegerVariable(variable.first, variable.second))
		{
			setFailure(
				result,
				SceneApplicationError::IntegerVariableFailed,
				"editor_run.target.variable_apply_failed",
				"target.integerVariables." + variable.first,
				{},
				"Editor-run integer variable could not be applied");
			return result;
		}
	}

	if (!callbacks.loadMap(preparedTarget.map))
	{
		setFailure(
			result,
			SceneApplicationError::MapLoadFailed,
			"editor_run.target.map_load_failed",
			"target.map",
			preparedTarget.map.virtualPath,
			"Editor-run MAP target could not be loaded");
		return result;
	}

	if (preparedTarget.npc.has_value() &&
		!callbacks.loadNpc(*preparedTarget.npc))
	{
		setFailure(
			result,
			SceneApplicationError::NpcLoadFailed,
			"editor_run.target.npc_load_failed",
			"target.npc",
			preparedTarget.npc->virtualPath,
			"Editor-run NPC target could not be loaded");
		return result;
	}

	if (preparedTarget.object.has_value() &&
		!callbacks.loadObject(*preparedTarget.object))
	{
		setFailure(
			result,
			SceneApplicationError::ObjectLoadFailed,
			"editor_run.target.object_load_failed",
			"target.object",
			preparedTarget.object->virtualPath,
			"Editor-run OBJ target could not be loaded");
		return result;
	}

	if (!callbacks.setPlayerPositionAndCamera(
			target.playerX,
			target.playerY))
	{
		setFailure(
			result,
			SceneApplicationError::PlayerPositionFailed,
			"editor_run.target.player_position_failed",
			"target.playerPosition",
			{},
			"Editor-run player position and camera could not be applied");
		return result;
	}

	if (preparedTarget.entryScript.has_value())
	{
		const EntryScriptExecutionResult scriptResult =
			callbacks.runEntryScript(
				*preparedTarget.entryScript);
		switch (scriptResult.status)
		{
		case EntryScriptExecutionStatus::Success:
			break;
		case EntryScriptExecutionStatus::LoadFailed:
			setFailure(
				result,
				SceneApplicationError::EntryScriptLoadFailed,
				"editor_run.target.script_load_failed",
				"target.entryScript",
				preparedTarget.entryScript->virtualPath,
				scriptResult.message.empty()
					? "Editor-run entry script could not be loaded"
					: scriptResult.message,
				scriptResult.line,
				scriptResult.column);
			return result;
		case EntryScriptExecutionStatus::RuntimeFailed:
		default:
			setFailure(
				result,
				SceneApplicationError::EntryScriptRuntimeFailed,
				"editor_run.target.script_runtime_failed",
				"target.entryScript",
				preparedTarget.entryScript->virtualPath,
				scriptResult.message.empty()
					? "Editor-run entry script failed at runtime"
					: scriptResult.message,
				scriptResult.line,
				scriptResult.column);
			return result;
		}
	}

	return result;
}
}
