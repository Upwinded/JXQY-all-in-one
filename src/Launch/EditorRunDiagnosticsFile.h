#pragma once

#include "EditorRunDiagnostics.h"

#include <cstdio>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace EditorRun
{
#if defined(JXQY_ENABLE_TEST_HOOKS)
using DiagnosticsFileSinkWriteTestHook = std::function<void()>;
using DiagnosticsFileSinkDestructorTestHook = std::function<void()>;
#endif

// Owns the verified diagnostics leaf and its anchored parent for one installed
// editor-run File generation. Construction creates the exact descriptor path
// exclusively; it never accepts a pre-existing file.
class DiagnosticsFileSink final :
	public std::enable_shared_from_this<DiagnosticsFileSink>
{
public:
	static std::shared_ptr<DiagnosticsFileSink> open();

	~DiagnosticsFileSink();

	DiagnosticsFileSink(const DiagnosticsFileSink&) = delete;
	DiagnosticsFileSink& operator=(const DiagnosticsFileSink&) = delete;
	DiagnosticsFileSink(DiagnosticsFileSink&&) = delete;
	DiagnosticsFileSink& operator=(DiagnosticsFileSink&&) = delete;

	bool valid();
	bool appendAndFlush(std::string_view line);
	DiagnosticLineSink lineSink();
#if defined(JXQY_ENABLE_TEST_HOOKS)
	bool ownsOpenFileForTests();
#endif

private:
	struct HandleState;

	DiagnosticsFileSink() = default;

	std::shared_ptr<HandleState> handleState;
	std::uint64_t resetHookId = 0;
};

#if defined(JXQY_ENABLE_TEST_HOOKS)
void setDiagnosticsFileSinkWriteTestHookForTests(
	const DiagnosticsFileSinkWriteTestHook& hook);

// Pauses a registered sink at destructor entry, before it closes its independent
// handle state, so native lifecycle tests can force reset/destructor overlap.
void setDiagnosticsFileSinkDestructorTestHookForTests(
	const DiagnosticsFileSinkDestructorTestHook& hook);
#endif
}
