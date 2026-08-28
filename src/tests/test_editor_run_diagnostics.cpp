#include "../Launch/EditorRunDiagnostics.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
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

bool runDeterministicJsonTest()
{
	std::vector<std::string> lines;
	EditorRun::DiagnosticsWriter writer(
		"c7b3fe3a-0000-4000-8000-000000000001",
		[&lines](std::string_view line)
		{
			lines.emplace_back(line);
			return true;
		});
	EditorRun::DiagnosticEvent event;
	event.severity = EditorRun::DiagnosticSeverity::Warning;
	event.code = "editor_run.target.script_runtime_failed";
	event.message = u8"入口脚本失败\n请检查 \"Event\"";
	event.source.file = u8"script/map/中都/入口.txt";
	event.source.line = 17;
	event.source.column = 9;
	event.target = "scene:scene-zhongdu";

	const bool wrote = writer.write(event);
	const std::string expected =
		u8"{\"schemaVersion\":1,\"sessionId\":"
		"\"c7b3fe3a-0000-4000-8000-000000000001\","
		"\"sequence\":1,\"severity\":\"warning\","
		"\"code\":\"editor_run.target.script_runtime_failed\","
		"\"message\":\"入口脚本失败\\n请检查 \\\"Event\\\"\","
		"\"file\":\"script/map/中都/入口.txt\","
		"\"line\":17,\"column\":9,"
		"\"target\":\"scene:scene-zhongdu\"}\n";
	return check(writer.valid(), "writer accepts a session and sink") &&
		check(wrote, "valid diagnostic is written") &&
		check(lines.size() == 1, "one event emits one JSONL line") &&
		check(lines.front() == expected, "JSONL field order and escaping are deterministic") &&
		check(writer.emittedCount() == 1, "successful write advances sequence");
}

bool runUtf8AndSinkFailureTest()
{
	std::vector<std::string> lines;
	bool failNext = true;
	EditorRun::DiagnosticsWriter writer(
		"session",
		[&](std::string_view line)
		{
			if (failNext)
			{
				failNext = false;
				return false;
			}
			lines.emplace_back(line);
			return true;
		});
	EditorRun::DiagnosticEvent event;
	event.severity = EditorRun::DiagnosticSeverity::Error;
	event.code = "editor_run.invalid_utf8";
	event.message =
		std::string("bad:", 4) +
		static_cast<char>(0xC0) +
		static_cast<char>(0xAF);

	bool ok = true;
	ok = check(
		!writer.write(event),
		"sink failure is reported") && ok;
	ok = check(
		writer.emittedCount() == 0,
		"failed sink does not consume a sequence") && ok;
	ok = check(
		writer.write(event),
		"writer can retry after a sink failure") && ok;
	ok = check(
		lines.size() == 1 &&
			lines.front().find("\"sequence\":1") != std::string::npos,
		"retry reuses the first uncommitted sequence") && ok;
	ok = check(
		lines.front().find("bad:\\ufffd\\ufffd") != std::string::npos,
		"invalid UTF-8 bytes are replaced without invalidating JSON") && ok;
	return ok;
}

bool runConcurrentSequenceTest()
{
	constexpr int ThreadCount = 8;
	constexpr int EventsPerThread = 100;
	std::vector<std::string> lines;
	lines.reserve(ThreadCount * EventsPerThread);
	EditorRun::DiagnosticsWriter writer(
		"concurrent-session",
		[&lines](std::string_view line)
		{
			lines.emplace_back(line);
			return true;
		});
	std::atomic<bool> writeFailed(false);
	std::vector<std::thread> threads;
	for (int threadIndex = 0;
		threadIndex < ThreadCount;
		++threadIndex)
	{
		threads.emplace_back(
			[
				threadIndex,
				&writer,
				&writeFailed,
				eventsPerThread = EventsPerThread
			]()
			{
				for (int eventIndex = 0;
					eventIndex < eventsPerThread;
					++eventIndex)
				{
					EditorRun::DiagnosticEvent event;
					event.code = "editor_run.concurrent";
					event.message =
						std::to_string(threadIndex) + ":" +
						std::to_string(eventIndex);
					if (!writer.write(event))
					{
						writeFailed.store(true);
					}
				}
			});
	}
	for (std::thread& thread : threads)
	{
		thread.join();
	}

	bool ok = true;
	ok = check(!writeFailed.load(), "every concurrent write succeeds") && ok;
	ok = check(
		lines.size() == ThreadCount * EventsPerThread,
		"concurrent writes emit every line") && ok;
	ok = check(
		writer.emittedCount() == lines.size(),
		"emitted count matches durable lines") && ok;
	for (std::size_t index = 0; index < lines.size(); ++index)
	{
		const std::string expectedSequence =
			"\"sequence\":" + std::to_string(index + 1) + ",";
		ok = check(
			lines[index].find(expectedSequence) != std::string::npos,
			"sink order has a contiguous monotonic sequence") && ok;
		if (!ok)
		{
			break;
		}
	}
	return ok;
}

bool runValidationTest()
{
	int sinkCalls = 0;
	EditorRun::DiagnosticsWriter invalidSession(
		{},
		[&sinkCalls](std::string_view)
		{
			++sinkCalls;
			return true;
		});
	EditorRun::DiagnosticEvent event;
	event.message = "missing code";

	bool ok = true;
	ok = check(
		!invalidSession.valid() && !invalidSession.write(event),
		"empty session is rejected") && ok;
	EditorRun::DiagnosticsWriter missingCode(
		"session",
		[&sinkCalls](std::string_view)
		{
			++sinkCalls;
			return true;
		});
	ok = check(
		!missingCode.write(event),
		"empty diagnostic code is rejected") && ok;
	ok = check(
		sinkCalls == 0,
		"invalid diagnostics never reach the sink") && ok;
	return ok;
}
}

int main()
{
	bool ok = true;
	ok = runDeterministicJsonTest() && ok;
	ok = runUtf8AndSinkFailureTest() && ok;
	ok = runConcurrentSequenceTest() && ok;
	ok = runValidationTest() && ok;
	return ok ? 0 : 1;
}
