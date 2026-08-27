#include "EditorRunDiagnosticsFile.h"

#include "../File/File.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
std::mutex g_diagnosticsFileSinkWriteTestHookMutex;
EditorRun::DiagnosticsFileSinkWriteTestHook
	g_diagnosticsFileSinkWriteTestHook;
std::mutex g_diagnosticsFileSinkDestructorTestHookMutex;
EditorRun::DiagnosticsFileSinkDestructorTestHook
	g_diagnosticsFileSinkDestructorTestHook;
#endif

bool flushDiagnosticsFile(std::FILE* file)
{
	if (file == nullptr || std::fflush(file) != 0)
	{
		return false;
	}
#if defined(_WIN32)
	const int descriptor = _fileno(file);
	return descriptor >= 0 && _commit(descriptor) == 0;
#else
	const int descriptor = fileno(file);
	return descriptor >= 0 && fsync(descriptor) == 0;
#endif
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
void invokeDiagnosticsFileSinkWriteTestHook()
{
	EditorRun::DiagnosticsFileSinkWriteTestHook hook;
	{
		std::lock_guard<std::mutex> lock(
			g_diagnosticsFileSinkWriteTestHookMutex);
		hook = g_diagnosticsFileSinkWriteTestHook;
	}
	if (hook)
	{
		hook();
	}
}

void invokeDiagnosticsFileSinkDestructorTestHook()
{
	EditorRun::DiagnosticsFileSinkDestructorTestHook hook;
	{
		std::lock_guard<std::mutex> lock(
			g_diagnosticsFileSinkDestructorTestHookMutex);
		hook = g_diagnosticsFileSinkDestructorTestHook;
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
// Reset callbacks own this state directly. They never retain the sink object,
// but a callback copied before sink destruction can still close the exact
// output handle before reset returns.
struct DiagnosticsFileSink::HandleState
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
		File::closeEditorRunDiagnosticsParent(parentToken);
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

std::shared_ptr<DiagnosticsFileSink> DiagnosticsFileSink::open()
{
	std::shared_ptr<DiagnosticsFileSink> result(
		new DiagnosticsFileSink());
	result->handleState = std::make_shared<HandleState>();
	const std::shared_ptr<HandleState> state =
		result->handleState;
	if (File::getEditorRunDiagnosticsPath(
			state->path, state->generation) !=
			File::EditorRunFileLayoutState::Valid)
	{
		return {};
	}

	File::EditorRunFileLayoutUse layoutUse(state->generation);
	if (!layoutUse.valid() ||
		!File::openEditorRunDiagnostics(
			state->path,
			state->generation,
			state->file,
			state->parentToken) ||
		!File::editorRunDiagnosticsHandleIsCurrent(
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

DiagnosticsFileSink::~DiagnosticsFileSink()
{
	if (handleState != nullptr && resetHookId != 0)
	{
#if defined(JXQY_ENABLE_TEST_HOOKS)
		invokeDiagnosticsFileSinkDestructorTestHook();
#endif
	}
	if (handleState != nullptr)
	{
		handleState->close();
	}
	File::removeEditorRunFileLayoutResetHook(resetHookId);
	resetHookId = 0;
}

bool DiagnosticsFileSink::valid()
{
	std::uint64_t expectedGeneration = 0;
	{
		std::lock_guard<std::mutex> lock(handleState->mutex);
		expectedGeneration = handleState->generation;
	}
	File::EditorRunFileLayoutUse layoutUse(expectedGeneration);
	std::lock_guard<std::mutex> lock(handleState->mutex);
	const bool current =
		layoutUse.valid() &&
		handleState->generation == expectedGeneration &&
		File::editorRunDiagnosticsHandleIsCurrent(
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

bool DiagnosticsFileSink::appendAndFlush(std::string_view line)
{
	if (line.empty())
	{
		return false;
	}

	std::uint64_t expectedGeneration = 0;
	{
		std::lock_guard<std::mutex> lock(handleState->mutex);
		expectedGeneration = handleState->generation;
	}
	File::EditorRunFileLayoutUse layoutUse(expectedGeneration);
	std::lock_guard<std::mutex> lock(handleState->mutex);
	if (!layoutUse.valid() ||
		handleState->generation != expectedGeneration ||
		!File::editorRunDiagnosticsHandleIsCurrent(
			handleState->file,
			handleState->parentToken,
			handleState->path,
			handleState->generation))
	{
		handleState->closeUnlocked();
		return false;
	}
#if defined(JXQY_ENABLE_TEST_HOOKS)
	invokeDiagnosticsFileSinkWriteTestHook();
#endif
	if (!File::editorRunDiagnosticsHandleIsCurrent(
			handleState->file,
			handleState->parentToken,
			handleState->path,
			handleState->generation))
	{
		handleState->closeUnlocked();
		return false;
	}
	const std::size_t written =
		std::fwrite(
			line.data(), 1, line.size(),
			handleState->file);
	const bool succeeded =
		written == line.size() &&
		flushDiagnosticsFile(handleState->file) &&
		File::editorRunDiagnosticsHandleIsCurrent(
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

DiagnosticLineSink DiagnosticsFileSink::lineSink()
{
	const std::shared_ptr<DiagnosticsFileSink> self =
		shared_from_this();
	return [self](std::string_view line)
	{
		return self->appendAndFlush(line);
	};
}

#if defined(JXQY_ENABLE_TEST_HOOKS)
bool DiagnosticsFileSink::ownsOpenFileForTests()
{
	std::lock_guard<std::mutex> lock(handleState->mutex);
	return handleState->file != nullptr;
}

void setDiagnosticsFileSinkWriteTestHookForTests(
	const DiagnosticsFileSinkWriteTestHook& hook)
{
	std::lock_guard<std::mutex> lock(
		g_diagnosticsFileSinkWriteTestHookMutex);
	g_diagnosticsFileSinkWriteTestHook = hook;
}

void setDiagnosticsFileSinkDestructorTestHookForTests(
	const DiagnosticsFileSinkDestructorTestHook& hook)
{
	std::lock_guard<std::mutex> lock(
		g_diagnosticsFileSinkDestructorTestHookMutex);
	g_diagnosticsFileSinkDestructorTestHook = hook;
}
#endif
}
