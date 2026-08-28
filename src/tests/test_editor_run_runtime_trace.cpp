#include "../Launch/EditorRunRuntimeTrace.h"
#include "../Launch/EditorRunRuntimeTraceWriter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr const char* SessionId =
	"123e4567-e89b-12d3-a456-426614174000";

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

EditorRun::RuntimeTraceEvent scriptStartEvent(
	std::uint64_t executionId)
{
	EditorRun::RuntimeTraceScriptStartEvent start;
	start.executionId = executionId;
	start.source.virtualPath =
		"script/common/runtime_trace_test.lua";
	start.source.contentSha256 =
		"ba7816bf8f01cfea414140de5dae2223"
		"b00361a396177a9cb410ff61f20015ad";
	start.source.rootKind =
		EditorRun::RuntimeTraceRootKind::Common;
	start.source.rootOrdinal = 0;
	start.source.sourceLayer =
		EditorRun::RuntimeTraceSourceLayer::Formal;
	EditorRun::RuntimeTraceEvent event;
	event.payload = std::move(start);
	return event;
}

EditorRun::RuntimeTraceEvent scriptFinishEvent(
	std::uint64_t executionId)
{
	EditorRun::RuntimeTraceEvent event;
	event.payload =
		EditorRun::RuntimeTraceScriptFinishEvent{
			executionId,
			EditorRun::
				RuntimeTraceScriptFinishStatus::
					Completed};
	return event;
}

EditorRun::RuntimeTraceEvent mapChangeEvent()
{
	EditorRun::RuntimeTraceEvent event;
	EditorRun::RuntimeTraceMapChangeEvent map;
	map.target = "map/runtime_trace_test.map";
	event.payload = std::move(map);
	return event;
}

bool serializerGoldenTests()
{
	bool ok = true;
	EditorRun::RuntimeTraceRecord record;
	record.sessionId = SessionId;
	record.sequence = 7;
	record.event.elapsedMicroseconds = 42;
	EditorRun::RuntimeTraceScriptStartEvent start;
	start.executionId = 11;
	start.parentExecutionId = 3;
	start.source.virtualPath =
		u8"script/剧情/入口.lua";
	start.source.contentSha256 =
		"ba7816bf8f01cfea414140de5dae2223"
		"b00361a396177a9cb410ff61f20015ad";
	start.source.rootKind =
		EditorRun::RuntimeTraceRootKind::
			DependencyId;
	start.source.rootOrdinal = 2;
	start.source.resourcePackId = "jxqy2";
	start.source.sourceLayer =
		EditorRun::RuntimeTraceSourceLayer::Overlay;
	record.event.payload = start;

	std::string line;
	const EditorRun::RuntimeTraceValidationResult result =
		EditorRun::serializeRuntimeTraceRecord(
			record, line);
	ok = check(
		result.succeeded(),
		"valid script.start serializes") && ok;
	ok = check(
		line ==
			"{\"schemaVersion\":1,"
			"\"sessionId\":\"123e4567-e89b-12d3-a456-426614174000\","
			"\"sequence\":7,"
			"\"eventType\":\"script.start\","
			"\"elapsedMicroseconds\":42,"
			"\"executionId\":11,"
			"\"parentExecutionId\":3,"
			"\"virtualPath\":\"script/剧情/入口.lua\","
			"\"contentSha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\","
			"\"rootKind\":\"dependency-id\","
			"\"rootOrdinal\":2,"
			"\"resourcePackId\":\"jxqy2\","
			"\"sourceLayer\":\"overlay\"}\n",
		"script.start exact JSONL bytes are deterministic") &&
		ok;

	record.sequence = 8;
	record.event.elapsedMicroseconds.reset();
	EditorRun::RuntimeTraceMapChangeEvent map;
	map.target = u8"map/中都.map";
	record.event.payload = map;
	ok = check(
		EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded() &&
		line ==
			"{\"schemaVersion\":1,"
			"\"sessionId\":\"123e4567-e89b-12d3-a456-426614174000\","
			"\"sequence\":8,"
			"\"eventType\":\"map.change\","
			"\"target\":\"map/中都.map\"}\n",
		"map.change omits executionId outside a script") &&
		ok;

	record.sequence = 9;
	EditorRun::RuntimeTraceVariableChangeEvent variable;
	variable.variableName = u8"速度";
	variable.valueType =
		EditorRun::RuntimeTraceVariableValueType::Real;
	variable.beforeValue = "1";
	variable.afterValue = "1.25e-2";
	record.event.payload = variable;
	ok = check(
		EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded() &&
		line.find("\"valueType\":\"real\"") !=
			std::string::npos &&
		line.find("\"executionId\"") ==
			std::string::npos,
		"variable real values and optional execution serialize") &&
		ok;

	variable.afterValue = "1E2";
	record.event.payload = variable;
	ok = check(
		!EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded() &&
		line.empty(),
		"non-canonical real spelling is rejected") &&
		ok;

	start.source.virtualPath = "../unsafe.lua";
	record.event.payload = start;
	ok = check(
		!EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded(),
		"unsafe virtual path is rejected") && ok;

	start.source.virtualPath = "script/common/entry.lua";
	start.source.resourcePackId =
		std::string("pack\0id", 7);
	record.event.payload = start;
	ok = check(
		!EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded(),
		"resource pack identifiers reject embedded nulls") &&
		ok;

	EditorRun::RuntimeTraceApiCallEvent api;
	api.executionId = 1;
	api.apiName = std::string("talk\0alias", 10);
	record.event.payload = api;
	ok = check(
		!EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded(),
		"API identifiers reject embedded nulls") && ok;

	variable.variableName =
		std::string("Event\0Alias", 11);
	variable.afterValue = "2";
	record.event.payload = variable;
	ok = check(
		!EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded(),
		"variable identifiers reject embedded nulls") && ok;

	record.event.payload =
		EditorRun::RuntimeTraceSourceLineEvent{
			EditorRun::
				RuntimeTraceMaximumExactJsonInteger +
				1,
			1};
	ok = check(
		!EditorRun::serializeRuntimeTraceRecord(
			record, line).succeeded(),
		"integer beyond exact JSON range is rejected") &&
		ok;
	return ok;
}

bool sha256Test()
{
	return check(
		EditorRun::runtimeTraceSha256Hex("abc") ==
			"ba7816bf8f01cfea414140de5dae2223"
			"b00361a396177a9cb410ff61f20015ad",
		"runtime trace SHA-256 hashes exact bytes");
}

bool writerLifecycleTest()
{
	std::mutex sinkMutex;
	std::string output;
	EditorRun::RuntimeTraceWriterOptions options;
	options.flushInterval =
		std::chrono::milliseconds(1);
	options.maximumBatchEvents = 8;
	const auto sink =
		[&](std::string_view batch)
		{
			std::lock_guard<std::mutex> lock(
				sinkMutex);
			output.append(batch);
			return true;
		};
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink, options);
	bool ok = check(
		writer != nullptr,
		"writer durably starts") ;
	if (writer == nullptr)
	{
		return false;
	}
	EditorRun::RuntimeTraceApiCallEvent api;
	api.executionId = 1;
	api.apiName = "loadmap";
	EditorRun::RuntimeTraceEvent event;
	event.payload = api;
	ok = check(
		writer->enqueue(scriptStartEvent(1)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued &&
		writer->enqueue(std::move(event)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued &&
		writer->enqueue(scriptFinishEvent(1)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued,
		"writer enqueues a valid script lifecycle") && ok;
	ok = check(
		writer->finish(
			EditorRun::
				RuntimeTraceSessionFinishStatus::
					Completed),
		"writer drains and durably finishes") && ok;
	{
		std::lock_guard<std::mutex> lock(sinkMutex);
		ok = check(
			output.find(
				"\"sequence\":1,"
				"\"eventType\":\"session.start\"") !=
				std::string::npos &&
			output.find(
				"\"sequence\":2,"
				"\"eventType\":\"script.start\"") !=
				std::string::npos &&
			output.find(
				"\"sequence\":3,"
				"\"eventType\":\"api.call\"") !=
				std::string::npos &&
			output.find(
				"\"sequence\":4,"
				"\"eventType\":\"script.finish\"") !=
				std::string::npos &&
			output.find(
				"\"sequence\":5,"
				"\"eventType\":\"session.finish\"") !=
				std::string::npos,
			"writer assigns continuous persisted sequence numbers") &&
			ok;
	}
	return ok;
}

bool writerDropPriorityTest()
{
	std::mutex sinkMutex;
	std::condition_variable sinkCondition;
	bool secondSinkEntered = false;
	bool releaseSecondSink = false;
	std::size_t sinkCalls = 0;
	std::string output;
	const auto sink =
		[&](std::string_view batch)
		{
			std::unique_lock<std::mutex> lock(
				sinkMutex);
			++sinkCalls;
			if (sinkCalls == 2)
			{
				secondSinkEntered = true;
				sinkCondition.notify_all();
				sinkCondition.wait(
					lock,
					[&]()
					{
						return releaseSecondSink;
					});
			}
			output.append(batch);
			return true;
		};
	EditorRun::RuntimeTraceWriterOptions options;
	options.maximumQueuedEvents = 2;
	options.maximumQueuedBytes = 64 * 1024;
	options.maximumBatchEvents = 1;
	options.maximumBatchBytes = 64 * 1024;
	options.flushInterval =
		std::chrono::milliseconds(1);
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink, options);
	if (!check(writer != nullptr,
			"bounded writer starts"))
	{
		return false;
	}

	const auto lineEvent =
		[](std::uint64_t line)
		{
			EditorRun::RuntimeTraceEvent event;
			event.payload =
				EditorRun::RuntimeTraceSourceLineEvent{
					1, line};
			return event;
		};
	(void)writer->enqueue(scriptStartEvent(1));
	{
		std::unique_lock<std::mutex> lock(sinkMutex);
		if (!sinkCondition.wait_for(
				lock,
				std::chrono::seconds(2),
				[&]()
				{
					return secondSinkEntered;
				}))
		{
			releaseSecondSink = true;
			lock.unlock();
			sinkCondition.notify_all();
			writer->finish(
				EditorRun::
					RuntimeTraceSessionFinishStatus::
						OrchestrationFailure);
			return check(
				false,
				"worker reached the blocked batch sink");
		}
	}

	bool ok = true;
	ok = check(
		writer->enqueue(lineEvent(2)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued &&
		writer->enqueue(lineEvent(3)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued &&
		writer->enqueue(lineEvent(4)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Dropped,
		"full bounded queue drops source.line without blocking") &&
		ok;
	ok = check(
		writer->enqueue(scriptFinishEvent(1)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued,
		"structural event evicts a queued source line") &&
		ok;
	{
		std::lock_guard<std::mutex> lock(sinkMutex);
		releaseSecondSink = true;
	}
	sinkCondition.notify_all();
	ok = check(
		writer->finish(
			EditorRun::
				RuntimeTraceSessionFinishStatus::
					Completed),
		"bounded writer completes after pressure") &&
		ok;
	{
		std::lock_guard<std::mutex> lock(sinkMutex);
		ok = check(
			output.find(
				"\"eventType\":\"trace.dropped\"") !=
				std::string::npos &&
			output.find(
				"\"droppedSourceLineCount\":2") !=
				std::string::npos &&
			output.find(
				"\"eventType\":\"script.finish\"") !=
				std::string::npos,
			"drops are observable and structural lifecycle survives") &&
			ok;
	}
	return ok;
}

bool writerFailureTest()
{
	std::atomic<int> calls{0};
	const auto sink =
		[&](std::string_view)
		{
			return ++calls == 1;
		};
	EditorRun::RuntimeTraceWriterOptions options;
	options.flushInterval =
		std::chrono::milliseconds(1);
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink, options);
	if (!check(writer != nullptr,
			"failure fixture writes session.start"))
	{
		return false;
	}
	(void)writer->enqueue(mapChangeEvent());
	const bool finished = writer->finish(
		EditorRun::
			RuntimeTraceSessionFinishStatus::
				Completed);
	return check(
		!finished &&
		writer->error() ==
			EditorRun::RuntimeTraceWriterError::
				WriteFailed,
		"batch sink failure is observable at terminal flush");
}

bool writerQueueSaturationStaysIncompleteTest()
{
	std::mutex sinkMutex;
	std::condition_variable sinkCondition;
	bool workerBatchEntered = false;
	bool releaseWorkerBatch = false;
	std::size_t sinkCalls = 0;
	std::string output;
	const auto sink =
		[&](std::string_view batch)
		{
			std::unique_lock<std::mutex> lock(
				sinkMutex);
			++sinkCalls;
			if (sinkCalls == 2)
			{
				workerBatchEntered = true;
				sinkCondition.notify_all();
				sinkCondition.wait(
					lock,
					[&]()
					{
						return releaseWorkerBatch;
					});
			}
			output.append(batch);
			return true;
		};
	EditorRun::RuntimeTraceWriterOptions options;
	options.maximumQueuedEvents = 1;
	options.maximumQueuedBytes = 64 * 1024;
	options.maximumBatchEvents = 1;
	options.maximumBatchBytes = 64 * 1024;
	options.flushInterval =
		std::chrono::milliseconds(1);
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink, options);
	if (!check(writer != nullptr,
			"queue-saturation fixture writes session.start"))
	{
		return false;
	}

	const auto structuralEvent =
		[]()
		{
			return mapChangeEvent();
		};
	if (!check(
			writer->enqueue(structuralEvent()) ==
				EditorRun::RuntimeTraceEnqueueResult::
					Enqueued,
			"queue-saturation fixture starts a worker batch"))
	{
		return false;
	}
	{
		std::unique_lock<std::mutex> lock(sinkMutex);
		if (!sinkCondition.wait_for(
				lock,
				std::chrono::seconds(2),
				[&]()
				{
					return workerBatchEntered;
				}))
		{
			releaseWorkerBatch = true;
			lock.unlock();
			sinkCondition.notify_all();
			writer->finish(
				EditorRun::
					RuntimeTraceSessionFinishStatus::
						OrchestrationFailure);
			return check(
				false,
				"queue-saturation worker reached the blocked batch sink");
		}
	}

	const bool queueFilled =
		writer->enqueue(structuralEvent()) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued &&
		writer->queuedEventCountForTests() == 1;
	const bool saturated =
		writer->enqueue(structuralEvent()) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Failed &&
		writer->error() ==
			EditorRun::RuntimeTraceWriterError::
				QueueSaturated;
	{
		std::lock_guard<std::mutex> lock(sinkMutex);
		releaseWorkerBatch = true;
	}
	sinkCondition.notify_all();
	const bool finished = writer->finish(
		EditorRun::
			RuntimeTraceSessionFinishStatus::
				Completed);
	std::lock_guard<std::mutex> lock(sinkMutex);
	return check(
		queueFilled &&
		saturated &&
		!finished &&
		output.find(
			"\"eventType\":\"session.finish\"") ==
			std::string::npos,
		"queue saturation leaves the on-disk trace incomplete");
}

bool writerRejectedEventStaysIncompleteTest()
{
	std::mutex sinkMutex;
	std::string output;
	const auto sink =
		[&](std::string_view batch)
		{
			std::lock_guard<std::mutex> lock(
				sinkMutex);
			output.append(batch);
			return true;
		};
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink);
	if (!check(writer != nullptr,
			"invalid-event fixture writes session.start"))
	{
		return false;
	}
	EditorRun::RuntimeTraceEvent invalid;
	invalid.payload =
		EditorRun::RuntimeTraceSourceLineEvent{
			0, 1};
	const bool rejected =
		writer->enqueue(std::move(invalid)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Invalid;
	const bool finished = writer->finish(
		EditorRun::
			RuntimeTraceSessionFinishStatus::
				Completed);
	std::lock_guard<std::mutex> lock(sinkMutex);
	return check(
		rejected &&
		!finished &&
		output.find(
			"\"eventType\":\"session.finish\"") ==
			std::string::npos,
		"rejected structural data leaves the on-disk trace incomplete");
}

bool writerLifecycleValidationTest()
{
	std::string output;
	const auto sink =
		[&output](std::string_view batch)
		{
			output.append(batch);
			return true;
		};
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink);
	if (!check(
			writer != nullptr,
			"lifecycle validation fixture writes session.start"))
	{
		return false;
	}
	EditorRun::RuntimeTraceEvent api;
	api.payload =
		EditorRun::RuntimeTraceApiCallEvent{
			1, "talk"};
	const bool rejected =
		writer->enqueue(std::move(api)) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Invalid;
	const bool finished = writer->finish(
		EditorRun::
			RuntimeTraceSessionFinishStatus::
				Completed);
	return check(
		rejected &&
		!finished &&
		writer->error() ==
			EditorRun::RuntimeTraceWriterError::
				InvalidExecutionLifecycle &&
		output.find(
			"\"eventType\":\"api.call\"") ==
				std::string::npos &&
		output.find(
			"\"eventType\":\"session.finish\"") ==
				std::string::npos,
		"writer rejects a consumer-invalid execution lifecycle");
}

bool writerActualBatchLimitsTest()
{
	EditorRun::RuntimeTraceVariableChangeEvent variable;
	variable.variableName = "trace";
	variable.valueType =
		EditorRun::RuntimeTraceVariableValueType::
			String;
	variable.beforeValue =
		std::string(256, '\x01');
	variable.afterValue =
		std::string(256, '\x01');
	EditorRun::RuntimeTraceEvent event;
	event.elapsedMicroseconds = 0;
	event.payload = variable;

	EditorRun::RuntimeTraceRecord sample;
	sample.sessionId = SessionId;
	sample.sequence = 2;
	sample.event = event;
	std::string sampleLine;
	if (!check(
			EditorRun::serializeRuntimeTraceRecord(
				sample, sampleLine).succeeded(),
			"control-heavy batch fixture serializes"))
	{
		return false;
	}
	const std::size_t retainedBytes =
		EditorRun::runtimeTraceEventRetainedBytes(
			event);
	const std::size_t batchLimit =
		(std::max)(
			sampleLine.size() + 1,
			retainedBytes * 2 + 1);
	if (!check(
			batchLimit < sampleLine.size() * 2,
			"control-heavy fixture distinguishes retained and serialized bytes"))
	{
		return false;
	}

	std::vector<std::string> batches;
	const auto sink =
		[&batches](std::string_view batch)
		{
			batches.emplace_back(batch);
			return true;
		};
	EditorRun::RuntimeTraceWriterOptions options;
	options.maximumBatchEvents = 1;
	options.maximumBatchBytes = batchLimit;
	options.flushInterval =
		std::chrono::milliseconds(1);
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			SessionId, sink, options);
	if (!check(
			writer != nullptr,
			"actual batch-limit writer starts"))
	{
		return false;
	}
	bool ok = true;
	ok = check(
		writer->enqueue(event) ==
				EditorRun::RuntimeTraceEnqueueResult::
					Enqueued &&
			writer->enqueue(event) ==
				EditorRun::RuntimeTraceEnqueueResult::
					Enqueued &&
			writer->finish(
				EditorRun::
					RuntimeTraceSessionFinishStatus::
						Completed),
		"control-heavy events drain successfully") &&
		ok;
	std::size_t persistedLineCount = 0;
	for (const std::string& batch : batches)
	{
		const std::size_t lineCount =
			static_cast<std::size_t>(
				std::count(
					batch.cbegin(),
					batch.cend(),
					'\n'));
		persistedLineCount += lineCount;
		ok = check(
			batch.size() <= batchLimit &&
			lineCount <=
				options.maximumBatchEvents,
			"each sink call obeys actual byte and event limits") &&
			ok;
	}
	ok = check(
		persistedLineCount == 4 &&
		batches.size() == 4,
		"session start, two events, and terminal record use bounded batches") &&
		ok;

	std::vector<std::string> oversizeBatches;
	EditorRun::RuntimeTraceRecord startRecord;
	startRecord.sessionId = SessionId;
	startRecord.sequence = 1;
	startRecord.event.elapsedMicroseconds = 0;
	startRecord.event.payload =
		EditorRun::RuntimeTraceSessionStartEvent{};
	std::string startLine;
	EditorRun::serializeRuntimeTraceRecord(
		startRecord, startLine);
	EditorRun::RuntimeTraceWriterOptions smallOptions;
	smallOptions.maximumBatchBytes =
		startLine.size() + 1;
	smallOptions.flushInterval =
		std::chrono::milliseconds(1);
	std::unique_ptr<EditorRun::RuntimeTraceWriter>
		oversizeWriter =
			EditorRun::RuntimeTraceWriter::create(
				SessionId,
				[&oversizeBatches](
					std::string_view batch)
				{
					oversizeBatches.emplace_back(
						batch);
					return true;
				},
				smallOptions);
	if (!check(
			oversizeWriter != nullptr &&
			sampleLine.size() >
				smallOptions.maximumBatchBytes,
			"oversize fixture keeps session.start within the limit"))
	{
		return false;
	}
	const bool queued =
		oversizeWriter->enqueue(event) ==
			EditorRun::RuntimeTraceEnqueueResult::
				Enqueued;
	const bool oversizeFinished =
		oversizeWriter->finish(
			EditorRun::
				RuntimeTraceSessionFinishStatus::
					Completed);
	ok = check(
		queued &&
		!oversizeFinished &&
		oversizeWriter->error() ==
			EditorRun::RuntimeTraceWriterError::
				BatchLimitExceeded &&
		oversizeBatches.size() == 1 &&
		oversizeBatches.front().size() <=
			smallOptions.maximumBatchBytes,
		"a single oversized serialized event fails closed without an oversized sink call") &&
		ok;
	return ok;
}
}

int main()
{
	bool ok = true;
	ok = serializerGoldenTests() && ok;
	ok = sha256Test() && ok;
	ok = writerLifecycleTest() && ok;
	ok = writerDropPriorityTest() && ok;
	ok = writerFailureTest() && ok;
	ok = writerQueueSaturationStaysIncompleteTest() && ok;
	ok = writerRejectedEventStaysIncompleteTest() && ok;
	ok = writerLifecycleValidationTest() && ok;
	ok = writerActualBatchLimitsTest() && ok;
	return ok ? 0 : 1;
}
