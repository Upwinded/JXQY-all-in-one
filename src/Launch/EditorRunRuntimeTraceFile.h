#pragma once

#include "EditorRunRuntimeTraceWriter.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace EditorRun
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
using RuntimeTraceFileSinkWriteTestHook =
	std::function<void()>;
using RuntimeTraceFileSinkDestructorTestHook =
	std::function<void()>;
#endif

// Owns the independent verified runtime-trace leaf and its anchored parent for
// one installed File generation. open() creates the fixed leaf exclusively.
class RuntimeTraceFileSink final :
	public std::enable_shared_from_this<
		RuntimeTraceFileSink>
{
public:
	static std::shared_ptr<RuntimeTraceFileSink> open();

	~RuntimeTraceFileSink();

	RuntimeTraceFileSink(
		const RuntimeTraceFileSink&) = delete;
	RuntimeTraceFileSink& operator=(
		const RuntimeTraceFileSink&) = delete;
	RuntimeTraceFileSink(
		RuntimeTraceFileSink&&) = delete;
	RuntimeTraceFileSink& operator=(
		RuntimeTraceFileSink&&) = delete;

	bool valid();
	// Performs exactly one fwrite and one durable flush for the complete batch.
	bool appendBatchAndFlush(std::string_view batch);
	RuntimeTraceBatchSink batchSink();
#if defined(JXQY_ENABLE_TEST_HOOKS)
	bool ownsOpenFileForTests();
#endif

private:
	struct HandleState;

	RuntimeTraceFileSink() = default;

	std::shared_ptr<HandleState> handleState;
	std::uint64_t resetHookId = 0;
};

#if defined(JXQY_ENABLE_TEST_HOOKS)
void setRuntimeTraceFileSinkWriteTestHookForTests(
	const RuntimeTraceFileSinkWriteTestHook& hook);
void setRuntimeTraceFileSinkDestructorTestHookForTests(
	const RuntimeTraceFileSinkDestructorTestHook& hook);
#endif
}
