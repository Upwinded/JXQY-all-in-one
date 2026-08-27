#include "EditorRunRuntimeTraceWriter.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
bool validOptions(
	const EditorRun::RuntimeTraceWriterOptions& options)
{
	return options.maximumQueuedEvents > 0 &&
		options.maximumQueuedBytes > 0 &&
		options.maximumBatchEvents > 0 &&
		options.maximumBatchBytes > 0 &&
		options.maximumExecutionCount > 0 &&
		options.flushInterval.count() > 0;
}

bool writerOwnedEvent(
	const EditorRun::RuntimeTraceEvent& event)
{
	return std::holds_alternative<
			EditorRun::RuntimeTraceSessionStartEvent>(
			event.payload) ||
		std::holds_alternative<
			EditorRun::RuntimeTraceSessionFinishEvent>(
			event.payload) ||
		std::holds_alternative<
			EditorRun::RuntimeTraceDroppedEvent>(
			event.payload);
}

bool invokeSink(
	const EditorRun::RuntimeTraceBatchSink& sink,
	std::string_view batch) noexcept
{
	try
	{
		return sink && sink(batch);
	}
	catch (...)
	{
		return false;
	}
}
}

namespace EditorRun
{
RuntimeTraceWriter::RuntimeTraceWriter(
	std::string writerSessionId,
	RuntimeTraceBatchSink writerSink,
	RuntimeTraceWriterOptions writerOptions) :
	sessionId(std::move(writerSessionId)),
	sink(std::move(writerSink)),
	options(writerOptions),
	startTime(std::chrono::steady_clock::now())
{
}

std::unique_ptr<RuntimeTraceWriter>
RuntimeTraceWriter::create(
	std::string sessionId,
	RuntimeTraceBatchSink sink,
	const RuntimeTraceWriterOptions& options)
{
	std::unique_ptr<RuntimeTraceWriter> writer(
		new RuntimeTraceWriter(
			std::move(sessionId),
			std::move(sink),
			options));
	if (!writer->start())
	{
		return {};
	}
	return writer;
}

RuntimeTraceWriter::~RuntimeTraceWriter()
{
	finish(
		RuntimeTraceSessionFinishStatus::
			OrchestrationFailure);
}

bool RuntimeTraceWriter::start()
{
	if (!validOptions(options) || !sink)
	{
		std::lock_guard<std::mutex> lock(mutex);
		writerError =
			RuntimeTraceWriterError::
				InvalidConfiguration;
		return false;
	}

	RuntimeTraceRecord record;
	record.sessionId = sessionId;
	record.sequence = nextSequence;
	record.event.elapsedMicroseconds = 0;
	record.event.payload =
		RuntimeTraceSessionStartEvent{};
	std::string line;
	const RuntimeTraceValidationResult validation =
		serializeRuntimeTraceRecord(record, line);
	if (!validation.succeeded())
	{
		std::lock_guard<std::mutex> lock(mutex);
		writerError =
			RuntimeTraceWriterError::
				SessionStartWriteFailed;
		return false;
	}
	if (line.size() > options.maximumBatchBytes)
	{
		std::lock_guard<std::mutex> lock(mutex);
		writerError =
			RuntimeTraceWriterError::
				InvalidConfiguration;
		return false;
	}
	if (!invokeSink(sink, line))
	{
		std::lock_guard<std::mutex> lock(mutex);
		writerError =
			RuntimeTraceWriterError::
				SessionStartWriteFailed;
		return false;
	}
	++nextSequence;
	++emitted;
	accepting = true;
	try
	{
		worker = std::thread(
			[this]()
			{
				workerMain();
			});
	}
	catch (...)
	{
		accepting = false;
		writerError =
			RuntimeTraceWriterError::
				InvalidConfiguration;
		return false;
	}
	return true;
}

RuntimeTraceEnqueueResult RuntimeTraceWriter::enqueue(
	RuntimeTraceEvent event)
{
	if (writerOwnedEvent(event))
	{
		return RuntimeTraceEnqueueResult::Invalid;
	}
	if (!event.elapsedMicroseconds.has_value())
	{
		event.elapsedMicroseconds =
			elapsedMicroseconds();
	}
	if (!validateRuntimeTraceEvent(event).succeeded())
	{
		std::lock_guard<std::mutex> lock(mutex);
		setErrorLocked(
			RuntimeTraceWriterError::
				InvalidEvent);
		return RuntimeTraceEnqueueResult::Invalid;
	}

	const bool droppable =
		runtimeTraceEventIsDroppable(event);
	const std::size_t retainedBytes =
		runtimeTraceEventRetainedBytes(event);
	std::lock_guard<std::mutex> lock(mutex);
	if (!accepting || stopRequested)
	{
		return writerError ==
				RuntimeTraceWriterError::None
			? RuntimeTraceEnqueueResult::Closed
			: RuntimeTraceEnqueueResult::Failed;
	}
	if (!lifecycleAllowsLocked(event))
	{
		setErrorLocked(
			RuntimeTraceWriterError::
				InvalidExecutionLifecycle);
		return RuntimeTraceEnqueueResult::Invalid;
	}

	const auto queueWouldOverflow =
		[&]()
		{
			return queue.size() >=
					options.maximumQueuedEvents ||
				retainedBytes >
					options.maximumQueuedBytes -
						(std::min)(
							options.maximumQueuedBytes,
							queuedBytes);
		};
	if (droppable && queueWouldOverflow())
	{
		recordDroppedSourceLineLocked();
		condition.notify_one();
		return RuntimeTraceEnqueueResult::Dropped;
	}
	while (!droppable && queueWouldOverflow() &&
		evictOneDroppableEventLocked())
	{
		recordDroppedSourceLineLocked();
	}
	if (queueWouldOverflow())
	{
		setErrorLocked(
			RuntimeTraceWriterError::
				QueueSaturated);
		return RuntimeTraceEnqueueResult::Failed;
	}

	queuedBytes += retainedBytes;
	commitLifecycleLocked(event);
	queue.push_back(std::move(event));
	condition.notify_one();
	return RuntimeTraceEnqueueResult::Enqueued;
}

bool RuntimeTraceWriter::finish(
	RuntimeTraceSessionFinishStatus status)
{
	std::lock_guard<std::mutex> finishLock(
		finishMutex);
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!stopRequested)
		{
			const bool hasActiveExecution =
				std::any_of(
					executionStates.cbegin(),
					executionStates.cend(),
					[](const auto& execution)
					{
						return execution.second;
					});
			if (hasActiveExecution)
			{
				setErrorLocked(
					RuntimeTraceWriterError::
						InvalidExecutionLifecycle);
			}
			else
			{
				accepting = false;
				stopRequested = true;
				terminalStatus = status;
			}
		}
		condition.notify_one();
	}
	if (worker.joinable())
	{
		worker.join();
	}
	std::lock_guard<std::mutex> lock(mutex);
	return writerError ==
			RuntimeTraceWriterError::None &&
		terminalWritten;
}

bool RuntimeTraceWriter::valid() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return writerError ==
		RuntimeTraceWriterError::None;
}

RuntimeTraceWriterError RuntimeTraceWriter::error() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return writerError;
}

std::uint64_t RuntimeTraceWriter::emittedCount() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return emitted;
}

std::uint64_t
RuntimeTraceWriter::droppedSourceLineCount() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return totalDroppedSourceLines;
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
std::size_t
RuntimeTraceWriter::queuedEventCountForTests() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return queue.size();
}

std::size_t
RuntimeTraceWriter::queuedBytesForTests() const noexcept
{
	std::lock_guard<std::mutex> lock(mutex);
	return queuedBytes;
}
#endif

std::uint64_t
RuntimeTraceWriter::elapsedMicroseconds() const noexcept
{
	const auto elapsed =
		std::chrono::duration_cast<
			std::chrono::microseconds>(
			std::chrono::steady_clock::now() -
				startTime).count();
	if (elapsed <= 0)
	{
		return 0;
	}
	return (std::min)(
		static_cast<std::uint64_t>(elapsed),
		RuntimeTraceMaximumExactJsonInteger);
}

void RuntimeTraceWriter::
recordDroppedSourceLineLocked() noexcept
{
	if (pendingDroppedSourceLines <
		RuntimeTraceMaximumExactJsonInteger)
	{
		++pendingDroppedSourceLines;
	}
	if (totalDroppedSourceLines <
		(std::numeric_limits<
			std::uint64_t>::max)())
	{
		++totalDroppedSourceLines;
	}
}

bool RuntimeTraceWriter::
evictOneDroppableEventLocked() noexcept
{
	const auto found = std::find_if(
		queue.begin(),
		queue.end(),
		[](const RuntimeTraceEvent& candidate)
		{
			return runtimeTraceEventIsDroppable(
				candidate);
		});
	if (found == queue.end())
	{
		return false;
	}
	queuedBytes -=
		runtimeTraceEventRetainedBytes(*found);
	queue.erase(found);
	return true;
}

bool RuntimeTraceWriter::lifecycleAllowsLocked(
	const RuntimeTraceEvent& event) const noexcept
{
	return std::visit(
		[this](const auto& payload)
		{
			using Payload = std::decay_t<
				decltype(payload)>;
			if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptStartEvent>)
			{
				if (executionStates.find(
						payload.executionId) !=
						executionStates.cend() ||
					executionStates.size() >=
						options.maximumExecutionCount)
				{
					return false;
				}
				return !payload.parentExecutionId.
						has_value() ||
					executionStates.find(
						*payload.parentExecutionId) !=
						executionStates.cend();
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptFinishEvent>)
			{
				const auto execution =
					executionStates.find(
						payload.executionId);
				return execution !=
						executionStates.cend() &&
					execution->second;
			}
			else if constexpr (
				std::is_same_v<
					Payload,
					RuntimeTraceSourceLineEvent> ||
				std::is_same_v<
					Payload,
					RuntimeTraceApiCallEvent>)
			{
				const auto execution =
					executionStates.find(
						payload.executionId);
				return execution !=
						executionStates.cend() &&
					execution->second;
			}
			else if constexpr (
				std::is_same_v<
					Payload,
					RuntimeTraceMapChangeEvent> ||
				std::is_same_v<
					Payload,
					RuntimeTraceVariableChangeEvent>)
			{
				if (!payload.executionId.has_value())
					return true;
				const auto execution =
					executionStates.find(
						*payload.executionId);
				return execution !=
						executionStates.cend() &&
					execution->second;
			}
			else
			{
				return false;
			}
		},
		event.payload);
}

void RuntimeTraceWriter::commitLifecycleLocked(
	const RuntimeTraceEvent& event)
{
	std::visit(
		[this](const auto& payload)
		{
			using Payload = std::decay_t<
				decltype(payload)>;
			if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptStartEvent>)
			{
				executionStates.emplace(
					payload.executionId,
					true);
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptFinishEvent>)
			{
				executionStates.at(
					payload.executionId) = false;
			}
		},
		event.payload);
}

void RuntimeTraceWriter::setErrorLocked(
	RuntimeTraceWriterError error) noexcept
{
	if (writerError ==
		RuntimeTraceWriterError::None)
	{
		writerError = error;
	}
	accepting = false;
	stopRequested = true;
	condition.notify_one();
}

void RuntimeTraceWriter::workerMain()
{
	while (true)
	{
		std::vector<RuntimeTraceEvent> events;
		RuntimeTraceSessionFinishStatus finishStatus =
			RuntimeTraceSessionFinishStatus::
				OrchestrationFailure;
		bool appendFinish = false;
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(
				lock,
				[this]()
				{
					return stopRequested ||
						!queue.empty() ||
						pendingDroppedSourceLines != 0;
				});
			if (!stopRequested &&
				queue.size() <
					options.maximumBatchEvents &&
				queuedBytes <
					options.maximumBatchBytes)
			{
				condition.wait_for(
					lock,
					options.flushInterval,
					[this]()
					{
						return stopRequested ||
							queue.size() >=
								options.
									maximumBatchEvents ||
							queuedBytes >=
								options.
									maximumBatchBytes;
					});
			}

			if (writerError ==
				RuntimeTraceWriterError::WriteFailed)
			{
				queue.clear();
				queuedBytes = 0;
				pendingDroppedSourceLines = 0;
				return;
			}

			events.reserve(
				(std::min)(
					options.maximumBatchEvents,
					queue.size() + 2));
			std::size_t batchBytes = 0;
			if (pendingDroppedSourceLines != 0)
			{
				RuntimeTraceEvent dropped;
				dropped.elapsedMicroseconds =
					elapsedMicroseconds();
				dropped.payload =
					RuntimeTraceDroppedEvent{
						pendingDroppedSourceLines};
				batchBytes +=
					runtimeTraceEventRetainedBytes(
						dropped);
				events.push_back(
					std::move(dropped));
				pendingDroppedSourceLines = 0;
			}
			while (!queue.empty() &&
				events.size() <
					options.maximumBatchEvents)
			{
				const std::size_t eventBytes =
					runtimeTraceEventRetainedBytes(
						queue.front());
				if (!events.empty() &&
					batchBytes + eventBytes >
						options.maximumBatchBytes)
				{
					break;
				}
				batchBytes += eventBytes;
				queuedBytes -= eventBytes;
				events.push_back(
					std::move(queue.front()));
				queue.pop_front();
			}
			if (stopRequested && queue.empty() &&
				pendingDroppedSourceLines == 0)
			{
				if (writerError !=
					RuntimeTraceWriterError::None)
				{
					return;
				}
				appendFinish = true;
				finishStatus = terminalStatus;
			}
		}

		std::vector<std::string> serializedLines;
		serializedLines.reserve(
			events.size() +
			(appendFinish ? 1U : 0U));
		for (RuntimeTraceEvent& event : events)
		{
			RuntimeTraceRecord record;
			record.sessionId = sessionId;
			{
				std::lock_guard<std::mutex> lock(
					mutex);
				if (nextSequence >
					RuntimeTraceMaximumExactJsonInteger)
				{
					setErrorLocked(
						RuntimeTraceWriterError::
							SequenceExhausted);
					return;
				}
				record.sequence = nextSequence++;
			}
			record.event = std::move(event);
			std::string line;
			if (!serializeRuntimeTraceRecord(
					record, line).succeeded())
			{
				std::lock_guard<std::mutex> lock(
					mutex);
				setErrorLocked(
					RuntimeTraceWriterError::
						InvalidEvent);
				return;
			}
			serializedLines.push_back(
				std::move(line));
		}
		if (appendFinish)
		{
			RuntimeTraceRecord record;
			record.sessionId = sessionId;
			{
				std::lock_guard<std::mutex> lock(
					mutex);
				if (nextSequence >
					RuntimeTraceMaximumExactJsonInteger)
				{
					setErrorLocked(
						RuntimeTraceWriterError::
							SequenceExhausted);
					return;
				}
				record.sequence = nextSequence++;
			}
			record.event.elapsedMicroseconds =
				elapsedMicroseconds();
			record.event.payload =
				RuntimeTraceSessionFinishEvent{
					finishStatus};
			std::string line;
			if (!serializeRuntimeTraceRecord(
					record, line).succeeded())
			{
				std::lock_guard<std::mutex> lock(
					mutex);
				setErrorLocked(
					RuntimeTraceWriterError::
						InvalidEvent);
				return;
			}
			serializedLines.push_back(
				std::move(line));
		}

		for (const std::string& line :
			serializedLines)
		{
			if (line.size() >
				options.maximumBatchBytes)
			{
				std::lock_guard<std::mutex> lock(
					mutex);
				setErrorLocked(
					RuntimeTraceWriterError::
						BatchLimitExceeded);
				return;
			}
		}

		std::string batch;
		std::size_t batchEventCount = 0;
		const auto flushBatch =
			[this, &batch, &batchEventCount]()
			{
				if (batch.empty())
					return true;
				if (!invokeSink(sink, batch))
				{
					std::lock_guard<std::mutex> lock(
						mutex);
					setErrorLocked(
						RuntimeTraceWriterError::
							WriteFailed);
					return false;
				}
				{
					std::lock_guard<std::mutex> lock(
						mutex);
					emitted +=
						static_cast<std::uint64_t>(
							batchEventCount);
				}
				batch.clear();
				batchEventCount = 0;
				return true;
			};
		for (const std::string& line :
			serializedLines)
		{
			if (!batch.empty() &&
				(batchEventCount >=
						options.maximumBatchEvents ||
				 line.size() >
						options.maximumBatchBytes -
							batch.size()))
			{
				if (!flushBatch())
					return;
			}
			batch.append(line);
			++batchEventCount;
		}
		if (!flushBatch())
			return;

		if (appendFinish)
		{
			std::lock_guard<std::mutex> lock(mutex);
			terminalWritten = true;
			return;
		}
	}
}
}
