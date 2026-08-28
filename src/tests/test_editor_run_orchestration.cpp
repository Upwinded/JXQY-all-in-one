#include "../Launch/EditorRunOrchestration.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
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

std::string diagnosticCode(std::string_view line)
{
	const std::string prefix = "\"code\":\"";
	const std::size_t begin = line.find(prefix);
	if (begin == std::string_view::npos)
	{
		return {};
	}
	const std::size_t valueBegin = begin + prefix.size();
	const std::size_t end = line.find('"', valueBegin);
	return end == std::string_view::npos
		? std::string()
		: std::string(line.substr(valueBegin, end - valueBegin));
}

struct Fixture
{
	EditorRun::RuntimeSessionResult runtimeSession;
	EditorRun::ResourcePreparationResult resourcePreparation;
	EditorRun::GameResult game;
	bool installResources = true;
	bool installFileLayout = true;
	bool diagnosticsOpen = true;
	bool logWritable = true;
	bool runtimeTraceOpen = true;
	int failDiagnosticWrite = 0;
	int diagnosticWriteCount = 0;
	int failRuntimeTraceWrite = 0;
	int runtimeTraceWriteCount = 0;
	int resetCount = 0;
	bool diagnosticsAlive = false;
	bool runtimeTraceAlive = false;
	bool resetObservedDiagnosticsAlive = false;
	bool resetObservedRuntimeTraceAlive = false;
	bool runGameObservedRuntimeTraceWriter = false;
	std::string throwStage;
	std::vector<std::string> calls;
	std::vector<std::string> diagnosticLines;
	std::vector<std::string> runtimeTraceBatches;
	std::string standardError;

	Fixture()
	{
		runtimeSession.session.descriptor.sessionId =
			"c7b3fe3a-0000-4000-8000-000000000001";
		runtimeSession.session.descriptor.target.sceneId =
			"scene-zhongdu";
	}

	void throwAt(const char* stage) const
	{
		if (throwStage == stage)
		{
			throw std::runtime_error(
				std::string("fixture exception at ") + stage);
		}
	}

	EditorRun::OrchestrationCallbacks callbacks()
	{
		EditorRun::OrchestrationCallbacks result;
		result.loadRuntimeSession =
			[this](const std::filesystem::path&)
			{
				calls.push_back("load_runtime_session");
				throwAt("load_runtime_session");
				return std::move(runtimeSession);
			};
		result.prepareResources =
			[this](const EditorRun::RuntimeSession&)
			{
				calls.push_back("prepare_resources");
				throwAt("prepare_resources");
				return resourcePreparation;
			};
		result.installResources =
			[this](const EditorRun::PreparedResourcePhase&)
			{
				calls.push_back("install_resources");
				throwAt("install_resources");
				return installResources;
			};
		result.installFileLayout =
			[this](
				const EditorRun::RuntimeSession&,
				const EditorRun::PreparedResourcePhase&)
			{
				calls.push_back("install_file_layout");
				throwAt("install_file_layout");
				return installFileLayout;
			};
		result.openDiagnostics =
			[this]()
			{
				calls.push_back("open_diagnostics");
				throwAt("open_diagnostics");
				if (!diagnosticsOpen)
				{
					return EditorRun::DiagnosticLineSink();
				}
				diagnosticsAlive = true;
				const std::shared_ptr<int> lifetime(
					new int(0),
					[this](int* value)
					{
						delete value;
						diagnosticsAlive = false;
						calls.push_back("close_diagnostics");
					});
				return EditorRun::DiagnosticLineSink(
					[this, lifetime](std::string_view line)
					{
						(void)lifetime;
						++diagnosticWriteCount;
						const std::string code =
							diagnosticCode(line);
						calls.push_back(
							"diagnostic:" + code);
						throwAt("diagnostic_write");
						if (failDiagnosticWrite ==
							diagnosticWriteCount)
						{
							return false;
						}
						diagnosticLines.emplace_back(line);
						return true;
					});
			};
		result.probeLog =
			[this]()
			{
				calls.push_back("probe_log");
				throwAt("probe_log");
				return logWritable;
			};
		result.openRuntimeTrace =
			[this]()
			{
				calls.push_back(
					"open_runtime_trace");
				throwAt("open_runtime_trace");
				if (!runtimeTraceOpen)
				{
					return EditorRun::
						RuntimeTraceBatchSink();
				}
				runtimeTraceAlive = true;
				const std::shared_ptr<int> lifetime(
					new int(0),
					[this](int* value)
					{
						delete value;
						runtimeTraceAlive = false;
						calls.push_back(
							"close_runtime_trace");
					});
				return EditorRun::RuntimeTraceBatchSink(
					[this, lifetime](
						std::string_view batch)
					{
						(void)lifetime;
						++runtimeTraceWriteCount;
						calls.push_back(
							"trace_write");
						throwAt(
							"runtime_trace_write");
						if (failRuntimeTraceWrite ==
							runtimeTraceWriteCount)
						{
							return false;
						}
						runtimeTraceBatches.
							emplace_back(batch);
						return true;
					});
			};
		result.runGame =
			[this](
				const EditorRun::RuntimeSession&,
				const EditorRun::PreparedResourcePhase&,
				EditorRun::RuntimeTraceWriter*
					runtimeTraceWriter)
			{
				calls.push_back("run_game");
				runGameObservedRuntimeTraceWriter =
					runtimeTraceWriter != nullptr;
				throwAt("run_game");
				return game;
			};
		result.resetFileLayout =
			[this]()
			{
				calls.push_back("reset_file_layout");
				resetObservedDiagnosticsAlive =
					diagnosticsAlive;
				resetObservedRuntimeTraceAlive =
					runtimeTraceAlive;
				++resetCount;
				throwAt("reset_file_layout");
			};
		result.writeStandardError =
			[this](std::string_view message)
			{
				throwAt("write_standard_error");
				standardError.append(message);
			};
		return result;
	}

	int run()
	{
		return EditorRun::runEditorRunWithCallbacks(
			"session/run.json",
			callbacks());
	}
};

bool hasCall(
	const Fixture& fixture,
	const std::string& call)
{
	return std::find(
		fixture.calls.begin(),
		fixture.calls.end(),
		call) != fixture.calls.end();
}

bool checkCalls(
	const Fixture& fixture,
	const std::vector<std::string>& expected,
	const std::string& context)
{
	if (fixture.calls == expected)
	{
		return true;
	}
	std::cerr << "FAIL: " << context << " call order\n  actual:";
	for (const std::string& call : fixture.calls)
	{
		std::cerr << " " << call;
	}
	std::cerr << "\n";
	return false;
}

bool runSuccessOrderTest()
{
	Fixture fixture;
	const int result = fixture.run();
	bool ok = true;
	ok = check(
		result == 0,
		"successful editor-run returns 0") && ok;
	ok = checkCalls(
		fixture,
		{
			"load_runtime_session",
			"prepare_resources",
			"install_resources",
			"install_file_layout",
			"open_diagnostics",
			"diagnostic:editor_run.session.starting",
			"probe_log",
			"open_runtime_trace",
			"trace_write",
			"run_game",
			"trace_write",
			"diagnostic:editor_run.session.completed",
			"close_runtime_trace",
			"close_diagnostics",
			"reset_file_layout"
		},
		"successful editor-run") && ok;
	ok = check(
		fixture.diagnosticLines.size() == 2,
		"success durably emits starting and completed diagnostics") && ok;
	ok = check(
		fixture.standardError.empty(),
		"success emits no stderr") && ok;
	ok = check(
		fixture.resetCount == 1,
		"installed File layout resets exactly once") && ok;
	ok = check(
		!fixture.resetObservedDiagnosticsAlive,
		"diagnostics sink closes before File layout reset") && ok;
	ok = check(
		!fixture.resetObservedRuntimeTraceAlive,
		"runtime trace sink closes before File layout reset") && ok;
	ok = check(
		fixture.runGameObservedRuntimeTraceWriter,
		"game receives the orchestration-owned runtime trace writer") &&
		ok;
	return ok;
}

bool runRuntimeSessionExitMappingTest()
{
	struct Case
	{
		EditorRun::RuntimeSessionFailureCategory category;
		int expectedExit;
		const char* name;
	};
	const Case cases[] =
	{
		{
			EditorRun::RuntimeSessionFailureCategory::DescriptorRead,
			66,
			"descriptor read"
		},
		{
			EditorRun::RuntimeSessionFailureCategory::DescriptorValidation,
			67,
			"descriptor validation"
		},
		{
			EditorRun::RuntimeSessionFailureCategory::Isolation,
			68,
			"isolation"
		}
	};

	bool ok = true;
	for (const Case& testCase : cases)
	{
		Fixture fixture;
		fixture.runtimeSession.failureCategory =
			testCase.category;
		fixture.runtimeSession.error =
			EditorRun::RuntimeSessionError::DescriptorInvalid;
		fixture.runtimeSession.diagnosticCode =
			"editor_run.fixture.failure";
		fixture.runtimeSession.message = "fixture failure";
		const int result = fixture.run();
		ok = check(
			result == testCase.expectedExit,
			std::string(testCase.name) +
				" maps to its stable exit") && ok;
		ok = checkCalls(
			fixture,
			{ "load_runtime_session" },
			testCase.name) && ok;
		ok = check(
			!fixture.standardError.empty(),
			std::string(testCase.name) +
				" emits stderr") && ok;
		ok = check(
			fixture.resetCount == 0,
			std::string(testCase.name) +
				" never installs or resets File") && ok;
	}
	return ok;
}

bool runPreEngineFailureTest()
{
	bool ok = true;
	{
		Fixture fixture;
		fixture.resourcePreparation.error =
			EditorRun::ResourcePreparationError::ResourceRoutingUnavailable;
		fixture.resourcePreparation.diagnosticCode =
			"editor_run.resource.assets_unavailable";
		fixture.resourcePreparation.message = "assets unavailable";
		ok = check(fixture.run() == 68, "resource preparation failure returns 68") && ok;
		ok = checkCalls(
			fixture,
			{
				"load_runtime_session",
				"prepare_resources"
			},
			"resource preparation failure") && ok;
		ok = check(
			!hasCall(fixture, "run_game"),
			"resource preparation failure never initializes the game") && ok;
	}
	{
		Fixture fixture;
		fixture.installResources = false;
		ok = check(
			fixture.run() == 68,
			"exact resource install failure returns 68") && ok;
		ok = checkCalls(
			fixture,
			{
				"load_runtime_session",
				"prepare_resources",
				"install_resources"
			},
			"resource install failure") && ok;
	}
	{
		Fixture fixture;
		fixture.installFileLayout = false;
		ok = check(
			fixture.run() == 68,
			"File layout install failure returns 68") && ok;
		ok = checkCalls(
			fixture,
			{
				"load_runtime_session",
				"prepare_resources",
				"install_resources",
				"install_file_layout"
			},
			"File layout install failure") && ok;
		ok = check(
			fixture.resetCount == 0,
			"failed File install is not reset as an installed layout") && ok;
	}
	return ok;
}

bool runOutputProbeFailureTest()
{
	bool ok = true;
	{
		Fixture fixture;
		fixture.diagnosticsOpen = false;
		ok = check(
			fixture.run() == 68,
			"diagnostics open failure returns 68") && ok;
		ok = check(
			!hasCall(fixture, "probe_log") &&
				!hasCall(fixture, "run_game"),
			"diagnostics open failure stops before log and game") && ok;
		ok = check(
			fixture.resetCount == 1 &&
				!fixture.standardError.empty(),
			"diagnostics open failure resets File and emits stderr") && ok;
	}
	{
		Fixture fixture;
		fixture.failDiagnosticWrite = 1;
		ok = check(
			fixture.run() == 68,
			"initial diagnostic flush failure returns 68") && ok;
		ok = check(
			!hasCall(fixture, "probe_log") &&
				!hasCall(fixture, "run_game"),
			"initial diagnostic failure stops before log and game") && ok;
		ok = check(
			fixture.resetCount == 1 &&
				!fixture.standardError.empty(),
			"initial diagnostic failure resets and emits stderr") && ok;
	}
	{
		Fixture fixture;
		fixture.logWritable = false;
		ok = check(
			fixture.run() == 68,
			"log writability failure returns 68") && ok;
		ok = check(
			!hasCall(fixture, "run_game"),
			"log writability failure stops before game initialization") && ok;
		ok = check(
			hasCall(
				fixture,
				"diagnostic:editor_run.log.open_failed"),
			"log failure is written to structured diagnostics") && ok;
		ok = check(
			!fixture.standardError.empty(),
			"log failure is also written to stderr") && ok;
	}
	{
		Fixture fixture;
		fixture.runtimeTraceOpen = false;
		ok = check(
			fixture.run() == 68,
			"runtime trace open failure returns 68") && ok;
		ok = check(
			!hasCall(fixture, "run_game"),
			"runtime trace open failure stops before game initialization") &&
			ok;
		ok = check(
			hasCall(
				fixture,
				"diagnostic:editor_run.trace.open_failed"),
			"runtime trace open failure is written to diagnostics") &&
			ok;
	}
	{
		Fixture fixture;
		fixture.failRuntimeTraceWrite = 1;
		ok = check(
			fixture.run() == 68,
			"session.start trace flush failure returns 68") &&
			ok;
		ok = check(
			!hasCall(fixture, "run_game"),
			"trace start failure stops before game initialization") &&
			ok;
	}
	{
		Fixture fixture;
		fixture.failRuntimeTraceWrite = 2;
		ok = check(
			fixture.run() == 68,
			"terminal trace flush failure returns 68 after successful game") &&
			ok;
		ok = check(
			hasCall(fixture, "run_game") &&
			hasCall(
				fixture,
				"diagnostic:editor_run.trace.write_failed"),
			"terminal trace failure is observable without skipping the game") &&
			ok;
	}
	{
		Fixture fixture;
		fixture.failDiagnosticWrite = 2;
		ok = check(
			fixture.run() == 68,
			"completion diagnostic flush failure returns 68") && ok;
		ok = check(
			hasCall(fixture, "run_game"),
			"completion diagnostic failure occurs after the game") && ok;
		ok = check(
			fixture.resetCount == 1,
			"completion diagnostic failure still resets File") && ok;
	}
	return ok;
}

bool runThrowingCallbackTest()
{
	struct Case
	{
		const char* stage;
		bool expectedReset;
	};
	const Case cases[] =
	{
		{ "load_runtime_session", false },
		{ "prepare_resources", false },
		{ "install_resources", false },
		{ "install_file_layout", false },
		{ "open_diagnostics", true },
		{ "diagnostic_write", true },
		{ "probe_log", true },
		{ "open_runtime_trace", true },
		{ "run_game", true },
		{ "reset_file_layout", true }
	};

	bool ok = true;
	for (const Case& testCase : cases)
	{
		Fixture fixture;
		fixture.throwStage = testCase.stage;
		const int result = fixture.run();
		ok = check(
			result == 68,
			std::string(testCase.stage) +
				" exception maps to stable exit 68") && ok;
		ok = check(
			fixture.resetCount ==
				(testCase.expectedReset ? 1 : 0),
			std::string(testCase.stage) +
				(testCase.expectedReset
					? " resets the confirmed File layout once"
					: " does not reset an unconfirmed File layout")) && ok;
		ok = check(
			fixture.standardError.find(
				std::string_view(testCase.stage) ==
					"reset_file_layout"
					? "editor_run.isolation.reset_failed"
					: "editor_run.orchestration.exception") !=
				std::string::npos,
			std::string(testCase.stage) +
				" exception emits a stderr diagnostic") && ok;
		ok = check(
			!fixture.diagnosticsAlive,
			std::string(testCase.stage) +
				" leaves no live diagnostics sink") && ok;
		if (testCase.expectedReset)
		{
			ok = check(
				!fixture.resetObservedDiagnosticsAlive,
				std::string(testCase.stage) +
					" closes diagnostics before reset") && ok;
			ok = check(
				!fixture.resetObservedRuntimeTraceAlive,
				std::string(testCase.stage) +
					" closes runtime trace before reset") &&
				ok;
		}
	}

	Fixture stderrFixture;
	stderrFixture.logWritable = false;
	stderrFixture.throwStage = "write_standard_error";
	ok = check(
		stderrFixture.run() == 68,
		"throwing stderr sink cannot escape stable exit 68") && ok;
	ok = check(
		stderrFixture.resetCount == 1 &&
			hasCall(
				stderrFixture,
				"diagnostic:editor_run.log.open_failed"),
		"throwing stderr sink preserves structured diagnostics and reset") &&
		ok;
	return ok;
}

bool runRawGameResultGuardTest()
{
	bool ok = true;
	EditorRun::GameResult success =
		EditorRun::finalizeGameRunResult(
			0,
			EditorRun::GameResult());
	ok = check(
		success.failure == EditorRun::GameFailure::None,
		"zero Game::run result remains successful") && ok;

	EditorRun::GameResult typedEngineFailure;
	typedEngineFailure.failure =
		EditorRun::GameFailure::EngineInitialization;
	typedEngineFailure.diagnosticCode =
		"editor_run.engine.init_failed";
	typedEngineFailure =
		EditorRun::finalizeGameRunResult(
			-1,
			std::move(typedEngineFailure));
	ok = check(
		typedEngineFailure.failure ==
			EditorRun::GameFailure::EngineInitialization &&
		typedEngineFailure.diagnosticCode ==
			"editor_run.engine.init_failed",
		"raw non-zero result preserves an existing typed failure") && ok;

	EditorRun::GameResult unclassified =
		EditorRun::finalizeGameRunResult(
			7,
			EditorRun::GameResult());
	ok = check(
		unclassified.failure ==
			EditorRun::GameFailure::SceneApplication &&
		unclassified.sceneApplication.diagnosticCode ==
			"editor_run.runtime.unclassified_failure" &&
		unclassified.sceneApplication.message.find("7") !=
			std::string::npos,
		"raw non-zero result becomes a diagnosable runtime failure") && ok;

	Fixture fixture;
	fixture.game = std::move(unclassified);
	ok = check(
		fixture.run() == 70,
		"unclassified non-zero Game result cannot be reported as 0") && ok;
	ok = check(
		hasCall(
			fixture,
			"diagnostic:editor_run.runtime.unclassified_failure"),
		"unclassified Game result emits structured diagnostics") && ok;
	return ok;
}

bool runGameExitMappingTest()
{
	struct Case
	{
		EditorRun::GameFailure failure;
		int expectedExit;
		const char* code;
	};
	const Case cases[] =
	{
		{
			EditorRun::GameFailure::ResourceInitialization,
			68,
			"editor_run.resource.initialize_failed"
		},
		{
			EditorRun::GameFailure::EngineInitialization,
			69,
			"editor_run.engine.init_failed"
		}
	};

	bool ok = true;
	for (const Case& testCase : cases)
	{
		Fixture fixture;
		fixture.game.failure = testCase.failure;
		fixture.game.diagnosticCode = testCase.code;
		fixture.game.message = "fixture game failure";
		ok = check(
			fixture.run() == testCase.expectedExit,
			std::string(testCase.code) +
				" maps to its stable exit") && ok;
		ok = check(
			hasCall(
				fixture,
				std::string("diagnostic:") +
					testCase.code),
			std::string(testCase.code) +
				" emits structured diagnostics") && ok;
		ok = check(
			!fixture.standardError.empty() &&
				fixture.resetCount == 1,
			std::string(testCase.code) +
				" emits stderr and resets File") && ok;
	}
	return ok;
}

bool runSceneFailureMappingTest()
{
	struct Case
	{
		EditorRun::SceneApplicationError error;
		const char* code;
	};
	const Case cases[] =
	{
		{
			EditorRun::SceneApplicationError::MapLoadFailed,
			"editor_run.target.map_load_failed"
		},
		{
			EditorRun::SceneApplicationError::NpcLoadFailed,
			"editor_run.target.npc_load_failed"
		},
		{
			EditorRun::SceneApplicationError::ObjectLoadFailed,
			"editor_run.target.object_load_failed"
		},
		{
			EditorRun::SceneApplicationError::EntryScriptLoadFailed,
			"editor_run.target.script_load_failed"
		},
		{
			EditorRun::SceneApplicationError::EntryScriptRuntimeFailed,
			"editor_run.target.script_runtime_failed"
		}
	};

	bool ok = true;
	for (const Case& testCase : cases)
	{
		Fixture fixture;
		fixture.game.failure =
			EditorRun::GameFailure::SceneApplication;
		fixture.game.sceneApplication.error =
			testCase.error;
		fixture.game.sceneApplication.diagnosticCode =
			testCase.code;
		fixture.game.sceneApplication.fieldPath =
			"target.entryScript";
		fixture.game.sceneApplication.virtualPath =
			u8"script/map/中都/入口.txt";
		fixture.game.sceneApplication.line = 17;
		fixture.game.sceneApplication.column = 9;
		fixture.game.sceneApplication.message =
			u8"目标加载失败";
		const int result = fixture.run();
		ok = check(
			result == 70,
			std::string(testCase.code) +
				" maps to stable exit 70") && ok;
		ok = check(
			hasCall(
				fixture,
				std::string("diagnostic:") +
					testCase.code),
			std::string(testCase.code) +
				" is preserved") && ok;
		ok = check(
			fixture.diagnosticLines.back().find(
				u8"\"file\":\"script/map/中都/入口.txt\"") !=
				std::string::npos &&
			fixture.diagnosticLines.back().find(
				"\"line\":17,\"column\":9") !=
				std::string::npos,
			std::string(testCase.code) +
				" preserves source location") && ok;
		ok = check(
			fixture.standardError.find("line=17") !=
				std::string::npos,
			std::string(testCase.code) +
				" writes source location to stderr") && ok;
	}
	return ok;
}
}

static_assert(
	static_cast<int>(EditorRun::ProcessExitCode::Usage) == 64,
	"editor-run usage exit must remain 64");
static_assert(
	static_cast<int>(EditorRun::ProcessExitCode::DescriptorRead) == 66,
	"editor-run must not consume ordinary automation exit 65");
static_assert(
	static_cast<int>(EditorRun::ProcessExitCode::SceneApplication) == 70,
	"editor-run stable exit range must end at 70");

int main()
{
	bool ok = true;
	ok = runSuccessOrderTest() && ok;
	ok = runRuntimeSessionExitMappingTest() && ok;
	ok = runPreEngineFailureTest() && ok;
	ok = runOutputProbeFailureTest() && ok;
	ok = runThrowingCallbackTest() && ok;
	ok = runRawGameResultGuardTest() && ok;
	ok = runGameExitMappingTest() && ok;
	ok = runSceneFailureMappingTest() && ok;
	return ok ? 0 : 1;
}
