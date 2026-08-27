#pragma once

#include "EditorRunDiagnostics.h"
#include "EditorRunResourceRouting.h"
#include "EditorRunRuntimeSession.h"
#include "EditorRunRuntimeTraceWriter.h"
#include "EditorRunSceneApplication.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace EditorRun
{
enum class ProcessExitCode : int
{
	Success = 0,
	Usage = 64,
	DescriptorRead = 66,
	DescriptorValidation = 67,
	Preparation = 68,
	EngineInitialization = 69,
	SceneApplication = 70
};

enum class GameFailure
{
	None,
	ResourceInitialization,
	EngineInitialization,
	SceneApplication
};

struct GameResult
{
	GameFailure failure = GameFailure::None;
	SceneApplicationResult sceneApplication;
	std::string diagnosticCode;
	std::string message;
};

// Preserves an existing typed failure and rejects an otherwise unclassified
// non-zero Game::run() result. Production applies this at the Game adapter
// boundary; native orchestration tests exercise the mapping without a window.
GameResult finalizeGameRunResult(
	int gameRunResult,
	GameResult typedResult);

// Callback seam for the process-level startup contract. Production supplies
// adapters for RuntimeSession, ResourceManager, File, diagnostics, GameLog and
// Game; native tests inject counters and failures without creating a window.
struct OrchestrationCallbacks
{
	std::function<RuntimeSessionResult(const std::filesystem::path&)>
		loadRuntimeSession;
	std::function<ResourcePreparationResult(const RuntimeSession&)>
		prepareResources;
	std::function<bool(const PreparedResourcePhase&)>
		installResources;
	std::function<bool(
		const RuntimeSession&,
		const PreparedResourcePhase&)>
		installFileLayout;
	std::function<DiagnosticLineSink()>
		openDiagnostics;
	std::function<RuntimeTraceBatchSink()>
		openRuntimeTrace;
	std::function<bool()>
		probeLog;
	std::function<GameResult(
		const RuntimeSession&,
		const PreparedResourcePhase&,
		RuntimeTraceWriter*)>
		runGame;
	std::function<void()>
		resetFileLayout;
	std::function<void(std::string_view)>
		writeStandardError;
};

int runEditorRunWithCallbacks(
	const std::filesystem::path& descriptorPath,
	const OrchestrationCallbacks& callbacks);

// Production desktop adapter used only after GameLaunch selected EditorRun.
int runEditorRun(const std::filesystem::path& descriptorPath);
}
