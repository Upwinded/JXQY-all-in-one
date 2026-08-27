#include "EditorRunOrchestration.h"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace
{
using EditorRun::ProcessExitCode;

int exitCode(ProcessExitCode code)
{
	return static_cast<int>(code);
}

std::string nonEmpty(
	const std::string& value,
	const char* fallback)
{
	return value.empty() ? std::string(fallback) : value;
}

void appendDetail(
	std::string& summary,
	const char* name,
	std::string_view value)
{
	if (!value.empty())
	{
		summary += " ";
		summary += name;
		summary += "=";
		summary += value;
	}
}

void writeStandardError(
	const EditorRun::OrchestrationCallbacks& callbacks,
	std::string_view code,
	std::string_view message,
	std::string_view fieldPath = {},
	std::string_view problemPath = {},
	std::uint32_t line = 0,
	std::uint32_t column = 0) noexcept
{
	if (!callbacks.writeStandardError)
	{
		return;
	}

	try
	{
		std::string summary(code);
		summary += ": ";
		summary += message;
		appendDetail(summary, "field", fieldPath);
		appendDetail(summary, "path", problemPath);
		if (line > 0)
		{
			summary += " line=";
			summary += std::to_string(line);
		}
		if (column > 0)
		{
			summary += " column=";
			summary += std::to_string(column);
		}
		summary += "\n";
		callbacks.writeStandardError(summary);
	}
	catch (...)
	{
		// stderr reporting must never destabilize the process exit mapping.
	}
}

int runtimeSessionFailureExitCode(
	const EditorRun::RuntimeSessionResult& result)
{
	switch (result.failureCategory)
	{
	case EditorRun::RuntimeSessionFailureCategory::DescriptorRead:
		return exitCode(ProcessExitCode::DescriptorRead);
	case EditorRun::RuntimeSessionFailureCategory::DescriptorValidation:
		return exitCode(ProcessExitCode::DescriptorValidation);
	case EditorRun::RuntimeSessionFailureCategory::Isolation:
		return exitCode(ProcessExitCode::Preparation);
	case EditorRun::RuntimeSessionFailureCategory::None:
		break;
	}

	switch (result.error)
	{
	case EditorRun::RuntimeSessionError::DescriptorOpenFailed:
	case EditorRun::RuntimeSessionError::DescriptorReadFailed:
	case EditorRun::RuntimeSessionError::DescriptorTooLarge:
		return exitCode(ProcessExitCode::DescriptorRead);
	case EditorRun::RuntimeSessionError::OutputDirectoryUnavailable:
	case EditorRun::RuntimeSessionError::ResourceRoutingContractUnavailable:
		return exitCode(ProcessExitCode::Preparation);
	default:
		return exitCode(ProcessExitCode::DescriptorValidation);
	}
}

bool callbacksAreComplete(
	const EditorRun::OrchestrationCallbacks& callbacks)
{
	return callbacks.loadRuntimeSession &&
		callbacks.prepareResources &&
		callbacks.installResources &&
		callbacks.installFileLayout &&
		callbacks.openDiagnostics &&
		callbacks.openRuntimeTrace &&
		callbacks.probeLog &&
		callbacks.runGame &&
		callbacks.resetFileLayout &&
		callbacks.writeStandardError;
}

bool writeDiagnostic(
	EditorRun::DiagnosticsWriter& writer,
	const EditorRun::DiagnosticEvent& event,
	const EditorRun::OrchestrationCallbacks& callbacks)
{
	if (writer.write(event))
	{
		return true;
	}
	writeStandardError(
		callbacks,
		"editor_run.diagnostics.write_failed",
		"Failed to append and flush the structured diagnostic");
	return false;
}

EditorRun::DiagnosticEvent gameFailureEvent(
	const EditorRun::GameResult& result,
	const EditorRun::RuntimeSession& session)
{
	EditorRun::DiagnosticEvent event;
	event.severity = EditorRun::DiagnosticSeverity::Error;
	event.target = session.descriptor.target.sceneId;
	if (result.failure == EditorRun::GameFailure::SceneApplication)
	{
		event.code = nonEmpty(
			result.sceneApplication.diagnosticCode,
			"editor_run.target.application_failed");
		event.message = nonEmpty(
			result.sceneApplication.message,
			"Failed to apply the editor-run scene");
		event.source.file = result.sceneApplication.virtualPath;
		event.source.line = result.sceneApplication.line;
		event.source.column = result.sceneApplication.column;
		return event;
	}

	event.code = nonEmpty(
		result.diagnosticCode,
		result.failure == EditorRun::GameFailure::EngineInitialization
			? "editor_run.engine.init_failed"
			: "editor_run.resource.initialize_failed");
	event.message = nonEmpty(
		result.message,
		result.failure == EditorRun::GameFailure::EngineInitialization
			? "Engine initialization failed"
			: "Editor-run resource routing could not be installed");
	return event;
}

int gameFailureExitCode(EditorRun::GameFailure failure)
{
	switch (failure)
	{
	case EditorRun::GameFailure::None:
		return exitCode(ProcessExitCode::Success);
	case EditorRun::GameFailure::ResourceInitialization:
		return exitCode(ProcessExitCode::Preparation);
	case EditorRun::GameFailure::EngineInitialization:
		return exitCode(ProcessExitCode::EngineInitialization);
	case EditorRun::GameFailure::SceneApplication:
		return exitCode(ProcessExitCode::SceneApplication);
	}
	return exitCode(ProcessExitCode::SceneApplication);
}

EditorRun::RuntimeTraceSessionFinishStatus
traceFinishStatus(EditorRun::GameFailure failure)
{
	switch (failure)
	{
	case EditorRun::GameFailure::None:
		return EditorRun::
			RuntimeTraceSessionFinishStatus::
				Completed;
	case EditorRun::GameFailure::ResourceInitialization:
		return EditorRun::
			RuntimeTraceSessionFinishStatus::
				ResourceFailure;
	case EditorRun::GameFailure::EngineInitialization:
		return EditorRun::
			RuntimeTraceSessionFinishStatus::
				EngineFailure;
	case EditorRun::GameFailure::SceneApplication:
		return EditorRun::
			RuntimeTraceSessionFinishStatus::
				SceneFailure;
	}
	return EditorRun::
		RuntimeTraceSessionFinishStatus::
			OrchestrationFailure;
}

int runWithInstalledFileLayout(
	const EditorRun::RuntimeSession& session,
	const EditorRun::PreparedResourcePhase& prepared,
	const EditorRun::OrchestrationCallbacks& callbacks)
{
	EditorRun::DiagnosticLineSink diagnosticSink =
		callbacks.openDiagnostics();
	if (!diagnosticSink)
	{
		writeStandardError(
			callbacks,
			"editor_run.diagnostics.open_failed",
			"Failed to create the exact diagnostics output");
		return exitCode(ProcessExitCode::Preparation);
	}

	EditorRun::DiagnosticsWriter diagnostics(
		session.descriptor.sessionId,
		std::move(diagnosticSink));
	EditorRun::DiagnosticEvent starting;
	starting.severity = EditorRun::DiagnosticSeverity::Info;
	starting.code = "editor_run.session.starting";
	starting.message =
		"Editor-run routing is ready; runtime initialization is starting";
	starting.target = session.descriptor.target.sceneId;
	if (!writeDiagnostic(diagnostics, starting, callbacks))
	{
		return exitCode(ProcessExitCode::Preparation);
	}

	if (!callbacks.probeLog())
	{
		EditorRun::DiagnosticEvent event;
		event.severity = EditorRun::DiagnosticSeverity::Error;
		event.code = "editor_run.log.open_failed";
		event.message = "Failed to create and flush the exact editor-run log";
		event.target = session.descriptor.target.sceneId;
		writeStandardError(callbacks, event.code, event.message);
		(void)writeDiagnostic(diagnostics, event, callbacks);
		return exitCode(ProcessExitCode::Preparation);
	}

	EditorRun::RuntimeTraceBatchSink traceSink =
		callbacks.openRuntimeTrace();
	std::unique_ptr<EditorRun::RuntimeTraceWriter>
		runtimeTraceWriter =
			EditorRun::RuntimeTraceWriter::create(
				session.descriptor.sessionId,
				std::move(traceSink));
	if (runtimeTraceWriter == nullptr)
	{
		EditorRun::DiagnosticEvent event;
		event.severity =
			EditorRun::DiagnosticSeverity::Error;
		event.code =
			"editor_run.trace.open_failed";
		event.message =
			"Failed to create and durably start the exact runtime trace";
		event.target =
			session.descriptor.target.sceneId;
		writeStandardError(
			callbacks, event.code, event.message);
		(void)writeDiagnostic(
			diagnostics, event, callbacks);
		return exitCode(ProcessExitCode::Preparation);
	}

	const EditorRun::GameResult gameResult =
		callbacks.runGame(
			session,
			prepared,
			runtimeTraceWriter.get());
	const bool traceFinished =
		runtimeTraceWriter->finish(
			traceFinishStatus(gameResult.failure));
	if (gameResult.failure != EditorRun::GameFailure::None)
	{
		const EditorRun::DiagnosticEvent event =
			gameFailureEvent(gameResult, session);
		writeStandardError(
			callbacks,
			event.code,
			event.message,
			gameResult.failure == EditorRun::GameFailure::SceneApplication
				? gameResult.sceneApplication.fieldPath
				: std::string(),
			event.source.file,
			event.source.line,
			event.source.column);
		(void)writeDiagnostic(diagnostics, event, callbacks);
		if (!traceFinished)
		{
			EditorRun::DiagnosticEvent traceEvent;
			traceEvent.severity =
				EditorRun::DiagnosticSeverity::Error;
			traceEvent.code =
				"editor_run.trace.write_failed";
			traceEvent.message =
				"Runtime trace terminal flush failed";
			traceEvent.target =
				session.descriptor.target.sceneId;
			writeStandardError(
				callbacks,
				traceEvent.code,
				traceEvent.message);
			(void)writeDiagnostic(
				diagnostics,
				traceEvent,
				callbacks);
		}
		return gameFailureExitCode(gameResult.failure);
	}
	if (!traceFinished)
	{
		EditorRun::DiagnosticEvent event;
		event.severity =
			EditorRun::DiagnosticSeverity::Error;
		event.code =
			"editor_run.trace.write_failed";
		event.message =
			"Runtime trace terminal flush failed";
		event.target =
			session.descriptor.target.sceneId;
		writeStandardError(
			callbacks, event.code, event.message);
		(void)writeDiagnostic(
			diagnostics, event, callbacks);
		return exitCode(ProcessExitCode::Preparation);
	}

	EditorRun::DiagnosticEvent completed;
	completed.severity = EditorRun::DiagnosticSeverity::Info;
	completed.code = "editor_run.session.completed";
	completed.message = "Editor-run ended normally";
	completed.target = session.descriptor.target.sceneId;
	if (!writeDiagnostic(diagnostics, completed, callbacks))
	{
		return exitCode(ProcessExitCode::Preparation);
	}
	return exitCode(ProcessExitCode::Success);
}
}

namespace EditorRun
{
GameResult finalizeGameRunResult(
	int gameRunResult,
	GameResult typedResult)
{
	if (gameRunResult == 0 ||
		typedResult.failure != GameFailure::None)
	{
		return typedResult;
	}

	typedResult.failure = GameFailure::SceneApplication;
	typedResult.sceneApplication.error =
		SceneApplicationError::MissingRuntimeCallback;
	if (typedResult.sceneApplication.diagnosticCode.empty())
	{
		typedResult.sceneApplication.diagnosticCode =
			"editor_run.runtime.unclassified_failure";
	}
	if (typedResult.sceneApplication.message.empty())
	{
		typedResult.sceneApplication.message =
			"Game::run() returned a non-zero result without a typed "
			"editor-run failure: " +
			std::to_string(gameRunResult);
	}
	return typedResult;
}

int runEditorRunWithCallbacks(
	const std::filesystem::path& descriptorPath,
	const OrchestrationCallbacks& callbacks)
{
	if (!callbacksAreComplete(callbacks))
	{
		writeStandardError(
			callbacks,
			"editor_run.orchestration.invalid",
			"Editor-run production callbacks are incomplete");
		return exitCode(ProcessExitCode::Preparation);
	}

	bool fileLayoutInstalled = false;
	int result = exitCode(ProcessExitCode::Preparation);
	RuntimeSessionResult runtimeSession;
	ResourcePreparationResult resourcePreparation;
	try
	{
		runtimeSession =
			callbacks.loadRuntimeSession(descriptorPath);
		if (!runtimeSession.succeeded() ||
			runtimeSession.failureCategory !=
				RuntimeSessionFailureCategory::None)
		{
			writeStandardError(
				callbacks,
				nonEmpty(
					runtimeSession.diagnosticCode,
					"editor_run.descriptor.invalid"),
				nonEmpty(
					runtimeSession.message,
					"Failed to load the editor-run descriptor"),
				runtimeSession.fieldPath,
				runtimeSession.problemPath.generic_u8string(),
				static_cast<std::uint32_t>(runtimeSession.line),
				static_cast<std::uint32_t>(runtimeSession.column));
			return runtimeSessionFailureExitCode(runtimeSession);
		}

		resourcePreparation =
			callbacks.prepareResources(
				runtimeSession.session);
		if (!resourcePreparation.succeeded())
		{
			const std::string problemPath =
				!resourcePreparation.virtualPath.empty()
					? resourcePreparation.virtualPath
					: resourcePreparation.hostPath.generic_u8string();
			writeStandardError(
				callbacks,
					nonEmpty(
						resourcePreparation.diagnosticCode,
						"editor_run.resource.routing_failed"),
				nonEmpty(
					resourcePreparation.message,
					"Editor-run resource routing failed"),
				resourcePreparation.fieldPath,
				problemPath);
			return exitCode(ProcessExitCode::Preparation);
		}

		if (!callbacks.installResources(resourcePreparation.prepared))
		{
			writeStandardError(
				callbacks,
				"editor_run.resource.install_failed",
				"Failed to install the prepared resource routing");
			return exitCode(ProcessExitCode::Preparation);
		}
		if (!callbacks.installFileLayout(
				runtimeSession.session,
				resourcePreparation.prepared))
		{
			writeStandardError(
				callbacks,
				"editor_run.isolation.install_failed",
				"Failed to install the isolated editor-run file layout");
			return exitCode(ProcessExitCode::Preparation);
		}
		fileLayoutInstalled = true;
		result = runWithInstalledFileLayout(
			runtimeSession.session,
			resourcePreparation.prepared,
			callbacks);
	}
	catch (const std::exception& exception)
	{
		writeStandardError(
			callbacks,
			"editor_run.orchestration.exception",
			exception.what());
		result = exitCode(ProcessExitCode::Preparation);
	}
	catch (...)
	{
		writeStandardError(
			callbacks,
			"editor_run.orchestration.exception",
			"Unknown exception during editor-run orchestration");
		result = exitCode(ProcessExitCode::Preparation);
	}

	if (fileLayoutInstalled)
	{
		try
		{
			callbacks.resetFileLayout();
		}
		catch (const std::exception& exception)
		{
			writeStandardError(
				callbacks,
				"editor_run.isolation.reset_failed",
				exception.what());
			result = exitCode(ProcessExitCode::Preparation);
		}
		catch (...)
		{
			writeStandardError(
				callbacks,
				"editor_run.isolation.reset_failed",
				"Unknown exception while resetting the editor-run "
				"File layout");
			result = exitCode(ProcessExitCode::Preparation);
		}
	}
	return result;
}
}
