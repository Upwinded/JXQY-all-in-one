#pragma once

#include "EditorRunRuntimeTrace.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace EditorRun
{
// Each sink call appends the complete batch and makes it durable before
// returning true. Batches contain one or more complete LF-terminated records.
using RuntimeTraceBatchSink =
	std::function<bool(std::string_view batch)>;

struct RuntimeTraceWriterOptions
{
	std::size_t maximumQueuedEvents = 4096;
	std::size_t maximumQueuedBytes = 4 * 1024 * 1024;
	std::size_t maximumBatchEvents = 256;
	std::size_t maximumBatchBytes = 256 * 1024;
	std::size_t maximumExecutionCount = 100'000;
	std::chrono::milliseconds flushInterval =
		std::chrono::milliseconds(100);
};

enum class RuntimeTraceWriterError
{
	None,
	InvalidConfiguration,
	SessionStartWriteFailed,
	InvalidEvent,
	InvalidExecutionLifecycle,
	QueueSaturated,
	BatchLimitExceeded,
	WriteFailed,
	SequenceExhausted
};

enum class RuntimeTraceEnqueueResult
{
	Enqueued,
	Dropped,
	Invalid,
	Closed,
	Failed
};

// Owns one bounded producer queue and one background durable batch writer.
// The producer-side lifecycle validator prevents records that the editor
// consumer would necessarily reject. session.start is written synchronously
// by create(); finish() drains the queue, emits trace.dropped when needed,
// writes session.finish durably, and joins the worker before returning. Every
// sink call obeys both configured serialized-byte and record-count limits.
class RuntimeTraceWriter final
{
public:
	static std::unique_ptr<RuntimeTraceWriter> create(
		std::string sessionId,
		RuntimeTraceBatchSink sink,
		const RuntimeTraceWriterOptions& options =
			RuntimeTraceWriterOptions());

	~RuntimeTraceWriter();

	RuntimeTraceWriter(const RuntimeTraceWriter&) = delete;
	RuntimeTraceWriter& operator=(const RuntimeTraceWriter&) = delete;
	RuntimeTraceWriter(RuntimeTraceWriter&&) = delete;
	RuntimeTraceWriter& operator=(RuntimeTraceWriter&&) = delete;

	RuntimeTraceEnqueueResult enqueue(
		RuntimeTraceEvent event);
	bool finish(RuntimeTraceSessionFinishStatus status);

	bool valid() const noexcept;
	RuntimeTraceWriterError error() const noexcept;
	std::uint64_t emittedCount() const noexcept;
	std::uint64_t droppedSourceLineCount() const noexcept;
#if defined(JXQY_ENABLE_TEST_HOOKS)
	std::size_t queuedEventCountForTests() const noexcept;
	std::size_t queuedBytesForTests() const noexcept;
#endif

private:
	RuntimeTraceWriter(
		std::string sessionId,
		RuntimeTraceBatchSink sink,
		RuntimeTraceWriterOptions options);

	bool start();
	void workerMain();
	std::uint64_t elapsedMicroseconds() const noexcept;
	void recordDroppedSourceLineLocked() noexcept;
	bool evictOneDroppableEventLocked() noexcept;
	bool lifecycleAllowsLocked(
		const RuntimeTraceEvent& event) const noexcept;
	void commitLifecycleLocked(
		const RuntimeTraceEvent& event);
	void setErrorLocked(
		RuntimeTraceWriterError writerError) noexcept;

	std::string sessionId;
	RuntimeTraceBatchSink sink;
	RuntimeTraceWriterOptions options;
	std::chrono::steady_clock::time_point startTime;
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::deque<RuntimeTraceEvent> queue;
	std::unordered_map<std::uint64_t, bool>
		executionStates;
	std::size_t queuedBytes = 0;
	std::uint64_t pendingDroppedSourceLines = 0;
	std::uint64_t totalDroppedSourceLines = 0;
	std::uint64_t nextSequence = 1;
	std::uint64_t emitted = 0;
	RuntimeTraceWriterError writerError =
		RuntimeTraceWriterError::None;
	bool accepting = false;
	bool stopRequested = false;
	bool terminalWritten = false;
	RuntimeTraceSessionFinishStatus terminalStatus =
		RuntimeTraceSessionFinishStatus::
			OrchestrationFailure;
	std::thread worker;
	mutable std::mutex finishMutex;
};
}
