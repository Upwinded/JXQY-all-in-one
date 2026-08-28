#include "../Launch/EditorRunSceneApplication.h"

#include <iostream>
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

EditorRun::SceneTarget makeTarget()
{
	EditorRun::SceneTarget target;
	target.sceneId = "scene-zhongdu";
	target.sceneName = u8"中都测试";
	target.mapPath = u8"map/中都.map";
	target.npcPath = u8"ini/npc/中都.npc";
	target.objectPath = u8"ini/obj/中都.obj";
	target.entryScriptPath = u8"script/map/中都/入口.txt";
	target.playerX = 100;
	target.playerY = 120;
	target.integerVariables =
	{
		{ "Event", 100 },
		{ "Progress", 7 }
	};
	return target;
}

EditorRun::ResolvedSceneTarget makePreparedTarget(
	const EditorRun::SceneTarget& target)
{
	EditorRun::ResolvedSceneTarget prepared;
	prepared.map = EditorRun::ResolvedTargetFile{
		EditorRun::TargetFileKind::Map,
		target.mapPath,
		2
	};
	if (!target.npcPath.empty())
	{
		prepared.npc = EditorRun::ResolvedTargetFile{
			EditorRun::TargetFileKind::Npc,
			target.npcPath,
			1
		};
	}
	if (!target.objectPath.empty())
	{
		prepared.object = EditorRun::ResolvedTargetFile{
			EditorRun::TargetFileKind::Object,
			target.objectPath,
			3
		};
	}
	if (!target.entryScriptPath.empty())
	{
		prepared.entryScript = EditorRun::ResolvedTargetFile{
			EditorRun::TargetFileKind::EntryScript,
			target.entryScriptPath,
			4
		};
	}
	return prepared;
}

struct RecordingRuntime
{
	std::vector<std::string> calls;
	std::vector<std::size_t> searchRootIndices;
	std::string failAt;
	EditorRun::EntryScriptExecutionResult scriptResult =
		{};

	EditorRun::SceneApplicationCallbacks callbacks()
	{
		return
		{
			[this](const std::string& name, std::int32_t value)
			{
				const std::string call =
					"variable:" + name + "=" + std::to_string(value);
				calls.push_back(call);
				return failAt != call;
			},
			[this](const EditorRun::ResolvedTargetFile& file)
			{
				searchRootIndices.push_back(file.searchRootIndex);
				calls.push_back("map:" + file.virtualPath);
				return failAt != "map";
			},
			[this](const EditorRun::ResolvedTargetFile& file)
			{
				searchRootIndices.push_back(file.searchRootIndex);
				calls.push_back("npc:" + file.virtualPath);
				return failAt != "npc";
			},
			[this](const EditorRun::ResolvedTargetFile& file)
			{
				searchRootIndices.push_back(file.searchRootIndex);
				calls.push_back("object:" + file.virtualPath);
				return failAt != "object";
			},
			[this](std::int32_t x, std::int32_t y)
			{
				calls.push_back(
					"position:" + std::to_string(x) + "," +
					std::to_string(y));
				return failAt != "position";
			},
			[this](const EditorRun::ResolvedTargetFile& file)
			{
				searchRootIndices.push_back(file.searchRootIndex);
				calls.push_back("script:" + file.virtualPath);
				return scriptResult;
			}
		};
	}
};

bool runSuccessOrderTest()
{
	const EditorRun::SceneTarget target = makeTarget();
	const EditorRun::ResolvedSceneTarget prepared =
		makePreparedTarget(target);
	RecordingRuntime runtime;
	const EditorRun::SceneApplicationResult result =
		EditorRun::applyEditorRunScene(
			target,
			prepared,
			runtime.callbacks());
	const std::vector<std::string> expected =
	{
		"variable:Event=100",
		"variable:Progress=7",
		"map:" + target.mapPath,
		"npc:" + target.npcPath,
		"object:" + target.objectPath,
		"position:100,120",
		"script:" + target.entryScriptPath
	};
	return check(result.succeeded(), "complete scene application succeeds") &&
		check(
			runtime.calls == expected,
			"scene application uses the required deterministic order") &&
		check(
			runtime.searchRootIndices ==
				std::vector<std::size_t>{ 2, 1, 3, 4 },
			"resource callbacks receive the original prepared search-root indices");
}

bool runFailureShortCircuitTests()
{
	bool ok = true;
	const EditorRun::SceneTarget target = makeTarget();
	const EditorRun::ResolvedSceneTarget prepared =
		makePreparedTarget(target);
	struct FailureCase
	{
		std::string failAt;
		EditorRun::SceneApplicationError expectedError;
		std::size_t expectedCallCount = 0;
	};
	const std::vector<FailureCase> failures =
	{
		{ "variable:Event=100",
			EditorRun::SceneApplicationError::IntegerVariableFailed, 1 },
		{ "map", EditorRun::SceneApplicationError::MapLoadFailed, 3 },
		{ "npc", EditorRun::SceneApplicationError::NpcLoadFailed, 4 },
		{ "object", EditorRun::SceneApplicationError::ObjectLoadFailed, 5 },
		{ "position",
			EditorRun::SceneApplicationError::PlayerPositionFailed, 6 }
	};
	for (const auto& failure : failures)
	{
		RecordingRuntime runtime;
		runtime.failAt = failure.failAt;
		const EditorRun::SceneApplicationResult result =
			EditorRun::applyEditorRunScene(
				target,
				prepared,
				runtime.callbacks());
		ok = check(
			result.error == failure.expectedError,
			"callback failure maps to its stable application error") && ok;
		ok = check(
			runtime.calls.size() == failure.expectedCallCount,
			"the first failed callback prevents every later stage") && ok;
	}

	for (const auto scriptFailure :
		{
			EditorRun::EntryScriptExecutionStatus::LoadFailed,
			EditorRun::EntryScriptExecutionStatus::RuntimeFailed
		})
	{
		RecordingRuntime runtime;
		runtime.scriptResult.status = scriptFailure;
		const EditorRun::SceneApplicationResult result =
			EditorRun::applyEditorRunScene(
				target,
				prepared,
				runtime.callbacks());
		const EditorRun::SceneApplicationError expected =
			scriptFailure ==
				EditorRun::EntryScriptExecutionStatus::LoadFailed
			? EditorRun::SceneApplicationError::EntryScriptLoadFailed
			: EditorRun::SceneApplicationError::EntryScriptRuntimeFailed;
		ok = check(
			result.error == expected,
			"script load and runtime failures remain distinct") && ok;
	}

	RecordingRuntime unknownScriptFailureRuntime;
	unknownScriptFailureRuntime.scriptResult.status =
		static_cast<EditorRun::EntryScriptExecutionStatus>(999);
	const EditorRun::SceneApplicationResult unknownScriptFailure =
		EditorRun::applyEditorRunScene(
			target,
			prepared,
			unknownScriptFailureRuntime.callbacks());
	ok = check(
		unknownScriptFailure.error ==
			EditorRun::SceneApplicationError::
				EntryScriptRuntimeFailed,
		"unknown script execution status fails closed") && ok;
	return ok;
}

bool runPreparedTargetValidationTests()
{
	bool ok = true;
	const EditorRun::SceneTarget target = makeTarget();
	EditorRun::ResolvedSceneTarget prepared = makePreparedTarget(target);
	prepared.map.virtualPath = "map/other.map";
	RecordingRuntime runtime;
	EditorRun::SceneApplicationResult result =
		EditorRun::applyEditorRunScene(
			target,
			prepared,
			runtime.callbacks());
	ok = check(
		result.error ==
			EditorRun::SceneApplicationError::InvalidPreparedTarget,
		"a prepared target cannot be substituted during scene application") && ok;
	ok = check(
		runtime.calls.empty(),
		"prepared-target mismatch has no runtime side effects") && ok;

	prepared = makePreparedTarget(target);
	EditorRun::SceneApplicationCallbacks missingCallback =
		runtime.callbacks();
	missingCallback.loadNpc = {};
	result = EditorRun::applyEditorRunScene(
		target,
		prepared,
		missingCallback);
	ok = check(
		result.error ==
			EditorRun::SceneApplicationError::MissingRuntimeCallback,
		"a required callback must be installed before application") && ok;
	ok = check(
		runtime.calls.empty(),
		"missing callback has no runtime side effects") && ok;
	return ok;
}

bool runOptionalTargetTest()
{
	EditorRun::SceneTarget target = makeTarget();
	target.npcPath.clear();
	target.objectPath.clear();
	target.entryScriptPath.clear();
	const EditorRun::ResolvedSceneTarget prepared =
		makePreparedTarget(target);
	RecordingRuntime runtime;
	EditorRun::SceneApplicationCallbacks callbacks =
		runtime.callbacks();
	callbacks.loadNpc = {};
	callbacks.loadObject = {};
	callbacks.runEntryScript = {};
	const EditorRun::SceneApplicationResult result =
		EditorRun::applyEditorRunScene(
			target,
			prepared,
			callbacks);
	const std::vector<std::string> expected =
	{
		"variable:Event=100",
		"variable:Progress=7",
		"map:" + target.mapPath,
		"position:100,120"
	};
	return check(
			result.succeeded(),
			"optional target callbacks are not required when paths are empty") &&
		check(
			runtime.calls == expected,
			"empty optional targets are skipped");
}

bool runEntryScriptDiagnosticPropagationTest()
{
	const EditorRun::SceneTarget target = makeTarget();
	const EditorRun::ResolvedSceneTarget prepared =
		makePreparedTarget(target);
	RecordingRuntime runtime;
	runtime.scriptResult.status =
		EditorRun::EntryScriptExecutionStatus::LoadFailed;
	runtime.scriptResult.line = 9;
	runtime.scriptResult.column = 17;
	runtime.scriptResult.message =
		"unexpected symbol near ')'";
	const EditorRun::SceneApplicationResult result =
		EditorRun::applyEditorRunScene(
			target,
			prepared,
			runtime.callbacks());
	return check(
			result.error ==
				EditorRun::SceneApplicationError::
					EntryScriptLoadFailed,
			"structured script failure keeps its stable scene error") &&
		check(
			result.virtualPath == target.entryScriptPath,
			"structured script failure keeps the exact virtual path") &&
		check(
			result.line == 9 && result.column == 17,
			"structured script failure keeps its true source location") &&
		check(
			result.message == "unexpected symbol near ')'",
			"structured script failure keeps its canonical Lua message");
}
}

int main()
{
	bool ok = true;
	ok = runSuccessOrderTest() && ok;
	ok = runFailureShortCircuitTests() && ok;
	ok = runPreparedTargetValidationTests() && ok;
	ok = runOptionalTargetTest() && ok;
	ok = runEntryScriptDiagnosticPropagationTest() && ok;
	return ok ? 0 : 1;
}
