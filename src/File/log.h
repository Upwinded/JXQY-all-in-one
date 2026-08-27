#pragma once
#include "File.h"
#include "../libconvert/libconvert.h"

#ifndef USE_LOG_FILE_PARAM
#define USE_LOG_FILE_PARAM
#endif


namespace GameLog
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
	using EditorRunLogWriteTestHook = std::function<void()>;
#endif

	extern bool use_log_file;
	void setLogFilePath(const std::string& fileName);
	// Creates and durably writes the exact log selected by the installed
	// editor-run File layout. This is the pre-Engine writability probe.
	bool initializeEditorRunLog();
	void editorRunFileLayoutGenerationChanged(uint64_t generation);
#if defined(JXQY_ENABLE_TEST_HOOKS)
	void setEditorRunLogWriteTestHookForTests(
		const EditorRunLogWriteTestHook& hook);
	bool editorRunLogOwnsOpenFileForTests();
#endif
	void write(const char* format, ...);
};
