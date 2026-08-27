#include "EditorRunRuntimeTraceFile.h"

#include "../File/File.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
std::mutex g_runtimeTraceFileSinkWriteTestHookMutex;
EditorRun::RuntimeTraceFileSinkWriteTestHook
	g_runtimeTraceFileSinkWriteTestHook;
std::mutex
	g_runtimeTraceFileSinkDestructorTestHookMutex;
EditorRun::RuntimeTraceFileSinkDestructorTestHook
	g_runtimeTraceFileSinkDestructorTestHook;
#endif

bool flushRuntimeTraceFile(std::FILE* file)
{
	if (file == nullptr || std::fflush(file) != 0)
	{
		return false;
	}
#if defined(_WIN32)
	const int descriptor = _fileno(file);
	return descriptor >= 0 &&
		_commit(descriptor) == 0;
#else
	const int descriptor = fileno(file);
	return descriptor >= 0 &&
		fsync(descriptor) == 0;
#endif
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
void invokeRuntimeTraceFileSinkWriteTestHook()
{
	EditorRun::RuntimeTraceFileSinkWriteTestHook hook;
	{
		std::lock_guard<std::mutex> lock(
			g_runtimeTraceFileSinkWriteTestHookMutex);
		hook = g_runtimeTraceFileSinkWriteTestHook;
	}
	if (hook)
	{
		hook();
	}
}

void invokeRuntimeTraceFileSinkDestructorTestHook()
{
	EditorRun::RuntimeTraceFileSinkDestructorTestHook hook;
	{
		std::lock_guard<std::mutex> lock(
			g_runtimeTraceFileSinkDestructorTestHookMutex);
		hook =
			g_runtimeTraceFileSinkDestructorTestHook;
	}
	if (hook)
	{
		hook();
	}
}
#endif
}

namespace EditorRun
{
struct RuntimeTraceFileSink::HandleState
{
	void close()
	{
		std::lock_guard<std::mutex> lock(mutex);
		closeUnlocked();
	}

	void closeUnlocked()
	{
		if (file != nullptr)
		{
			std::fclose(file);
			file = nullptr;
		}
		File::closeEditorRunRuntimeTraceParent(
			parentToken);
		parentToken = -1;
		path.clear();
		generation = 0;
	}

	std::mutex mutex;
	std::FILE* file = nullptr;
	std::intptr_t parentToken = -1;
	std::string path;
	std::uint64_t generation = 0;
};

std::shared_ptr<RuntimeTraceFileSink>
RuntimeTraceFileSink::open()
{
	std::shared_ptr<RuntimeTraceFileSink> result(
		new RuntimeTraceFileSink());
	result->handleState =
		std::make_shared<HandleState>();
	const std::shared_ptr<HandleState> state =
		result->handleState;
	if (File::getEditorRunRuntimeTracePath(
			state->path, state->generation) !=
			File::EditorRunFileLayoutState::Valid)
	{
		return {};
	}

	File::EditorRunFileLayoutUse layoutUse(
		state->generation);
	if (!layoutUse.valid() ||
		!File::openEditorRunRuntimeTrace(
			state->path,
			state->generation,
			state->file,
			state->parentToken) ||
		!File::editorRunRuntimeTraceHandleIsCurrent(
			state->file,
			state->parentToken,
			state->path,
			state->generation))
	{
		state->close();
		return {};
	}

	result->resetHookId =
		File::addEditorRunFileLayoutResetHook(
			[state]()
			{
				state->close();
			});
	if (result->resetHookId == 0)
	{
		state->close();
		return {};
	}
	return result;
}

RuntimeTraceFileSink::~RuntimeTraceFileSink()
{
	if (handleState != nullptr &&
		resetHookId != 0)
	{
#if defined(JXQY_ENABLE_TEST_HOOKS)
		invokeRuntimeTraceFileSinkDestructorTestHook();
#endif
	}
	if (handleState != nullptr)
	{
		handleState->close();
	}
	File::removeEditorRunFileLayoutResetHook(
		resetHookId);
	resetHookId = 0;
}

bool RuntimeTraceFileSink::valid()
{
	if (handleState == nullptr)
	{
		return false;
	}
	std::uint64_t expectedGeneration = 0;
	{
		std::lock_guard<std::mutex> lock(
			handleState->mutex);
		expectedGeneration =
			handleState->generation;
	}
	File::EditorRunFileLayoutUse layoutUse(
		expectedGeneration);
	std::lock_guard<std::mutex> lock(
		handleState->mutex);
	const bool current =
		layoutUse.valid() &&
		handleState->generation ==
			expectedGeneration &&
		File::editorRunRuntimeTraceHandleIsCurrent(
			handleState->file,
			handleState->parentToken,
			handleState->path,
			handleState->generation);
	if (!current)
	{
		handleState->closeUnlocked();
	}
	return current;
}

bool RuntimeTraceFileSink::appendBatchAndFlush(
	std::string_view batch)
{
	if (batch.empty() || handleState == nullptr)
	{
		return false;
	}

	std::uint64_t expectedGeneration = 0;
	{
		std::lock_guard<std::mutex> lock(
			handleState->mutex);
		expectedGeneration =
			handleState->generation;
	}
	File::EditorRunFileLayoutUse layoutUse(
		expectedGeneration);
	std::lock_guard<std::mutex> lock(
		handleState->mutex);
	if (!layoutUse.valid() ||
		handleState->generation !=
			expectedGeneration ||
		!File::editorRunRuntimeTraceHandleIsCurrent(
			handleState->file,
			handleState->parentToken,
			handleState->path,
			handleState->generation))
	{
		handleState->closeUnlocked();
		return false;
	}
#if defined(JXQY_ENABLE_TEST_HOOKS)
	invokeRuntimeTraceFileSinkWriteTestHook();
#endif
	if (!File::editorRunRuntimeTraceHandleIsCurrent(
			handleState->file,
			handleState->parentToken,
			handleState->path,
			handleState->generation))
	{
		handleState->closeUnlocked();
		return false;
	}
	const std::size_t written = std::fwrite(
		batch.data(),
		1,
		batch.size(),
		handleState->file);
	const bool succeeded =
		written == batch.size() &&
		flushRuntimeTraceFile(handleState->file) &&
		File::editorRunRuntimeTraceHandleIsCurrent(
			handleState->file,
			handleState->parentToken,
			handleState->path,
			handleState->generation);
	if (!succeeded)
	{
		handleState->closeUnlocked();
	}
	return succeeded;
}

RuntimeTraceBatchSink
RuntimeTraceFileSink::batchSink()
{
	const std::shared_ptr<RuntimeTraceFileSink> self =
		shared_from_this();
	return [self](std::string_view batch)
	{
		return self->appendBatchAndFlush(batch);
	};
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
bool RuntimeTraceFileSink::ownsOpenFileForTests()
{
	if (handleState == nullptr)
	{
		return false;
	}
	std::lock_guard<std::mutex> lock(
		handleState->mutex);
	return handleState->file != nullptr;
}

void setRuntimeTraceFileSinkWriteTestHookForTests(
	const RuntimeTraceFileSinkWriteTestHook& hook)
{
	std::lock_guard<std::mutex> lock(
		g_runtimeTraceFileSinkWriteTestHookMutex);
	g_runtimeTraceFileSinkWriteTestHook = hook;
}

void setRuntimeTraceFileSinkDestructorTestHookForTests(
	const RuntimeTraceFileSinkDestructorTestHook& hook)
{
	std::lock_guard<std::mutex> lock(
		g_runtimeTraceFileSinkDestructorTestHookMutex);
	g_runtimeTraceFileSinkDestructorTestHook = hook;
}
#endif
}
