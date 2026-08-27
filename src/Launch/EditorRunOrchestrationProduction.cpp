#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !defined(__MOBILE__) && !defined(__ANDROID__) && \
	!(defined(__APPLE__) && TARGET_OS_IOS)

#include "EditorRunOrchestration.h"

#include "EditorRunDiagnosticsFile.h"
#include "EditorRunRuntimeTraceFile.h"
#include "../File/File.h"
#include "../File/log.h"
#include "../Game/Game.h"
#include "../JxqyEngineVersion.h"
#include "../Resource/ResourceManager.h"

#include <iostream>
#include <utility>

namespace
{
EditorRun::GameResult runGame(
	const EditorRun::RuntimeSession& session,
	const EditorRun::PreparedResourcePhase& prepared,
	EditorRun::RuntimeTraceWriter* runtimeTraceWriter)
{
	Game game;
	game.setEditorRunScene(
		session.descriptor.target,
		prepared,
		runtimeTraceWriter);
	const int gameRunResult = game.run();

	EditorRun::GameResult result;
	switch (game.getEditorRunFailure())
	{
	case EditorRunGameFailure::None:
		break;
	case EditorRunGameFailure::ResourceInitialization:
		result.failure =
			EditorRun::GameFailure::ResourceInitialization;
		result.diagnosticCode =
			"editor_run.resource.initialize_failed";
		result.message =
			"Editor-run resource routing could not be installed";
		break;
	case EditorRunGameFailure::EngineInitialization:
		result.failure =
			EditorRun::GameFailure::EngineInitialization;
		result.diagnosticCode =
			"editor_run.engine.init_failed";
		result.message = "Engine initialization failed";
		break;
	case EditorRunGameFailure::SceneApplication:
		result.failure =
			EditorRun::GameFailure::SceneApplication;
		result.sceneApplication =
			game.getEditorRunSceneApplicationResult();
		break;
	}
	return EditorRun::finalizeGameRunResult(
		gameRunResult,
		std::move(result));
}
}

namespace EditorRun
{
int runEditorRun(const std::filesystem::path& descriptorPath)
{
	OrchestrationCallbacks callbacks;
	callbacks.loadRuntimeSession =
		[](const std::filesystem::path& path)
		{
			return loadEditorRunRuntimeSession(path);
		};
	callbacks.prepareResources =
		[](const RuntimeSession& session)
		{
			return prepareEditorRunResources(
				session,
				JxqyBuildVersion::EngineVersion);
		};
	callbacks.installResources =
		[](const PreparedResourcePhase& prepared)
		{
			return ResourceManager::instance().
				installEditorRunSelection(prepared.resources);
		};
	callbacks.installFileLayout =
		[](const RuntimeSession& session,
			const PreparedResourcePhase&)
		{
			File::EditorRunFileLayout layout;
			layout.overlayRoot =
				(session.sessionRoot / "overlay").
					generic_u8string();
			layout.isolatedSaveRoot =
				(session.sessionRoot / "save").
					generic_u8string();
			layout.applicationStateRoot =
				(session.sessionRoot / "application-state").
					generic_u8string();
			layout.diagnosticsRoot =
				session.diagnosticsRoot.generic_u8string();
			layout.diagnosticsPath =
				(session.diagnosticsRoot / "diagnostics.jsonl").
					generic_u8string();
			layout.logPath =
				(session.diagnosticsRoot / "game.log").
					generic_u8string();
			layout.runtimeTracePath =
				session.runtimeTracePath.
					generic_u8string();
			File::EditorRunFileLayoutIdentityProof proof;
			proof.outputRoots =
				session.outputDirectoryIdentities;
			return File::installEditorRunFileLayout(
				layout, proof);
		};
	callbacks.openDiagnostics =
		[]()
		{
			const std::shared_ptr<DiagnosticsFileSink> sink =
				DiagnosticsFileSink::open();
			return sink != nullptr
				? sink->lineSink()
				: DiagnosticLineSink();
		};
	callbacks.openRuntimeTrace =
		[]()
		{
			const std::shared_ptr<
				RuntimeTraceFileSink> sink =
					RuntimeTraceFileSink::open();
			return sink != nullptr
				? sink->batchSink()
				: RuntimeTraceBatchSink();
		};
	callbacks.probeLog =
		[]()
		{
			return GameLog::initializeEditorRunLog();
		};
	callbacks.runGame = runGame;
	callbacks.resetFileLayout =
		[]()
		{
			File::resetEditorRunFileLayout();
		};
	callbacks.writeStandardError =
		[](std::string_view message)
		{
			std::cerr.write(
				message.data(),
				static_cast<std::streamsize>(message.size()));
			std::cerr.flush();
		};
	return runEditorRunWithCallbacks(
		descriptorPath,
		callbacks);
}
}

#endif
