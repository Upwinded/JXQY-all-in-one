#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace EditorRun
{
enum class DiagnosticSeverity
{
	Info,
	Warning,
	Error
};

struct DiagnosticSourceLocation
{
	std::string file;
	std::uint32_t line = 0;
	std::uint32_t column = 0;
};

struct DiagnosticEvent
{
	DiagnosticSeverity severity = DiagnosticSeverity::Info;
	std::string code;
	std::string message;
	DiagnosticSourceLocation source;
	std::string target;
};

// The sink must append and flush the complete JSONL line before returning
// true. DiagnosticsWriter serializes concurrent callers and only advances the
// sequence after the sink confirms that the line is durable.
using DiagnosticLineSink =
	std::function<bool(std::string_view line)>;

class DiagnosticsWriter
{
public:
	DiagnosticsWriter(
		std::string sessionId,
		DiagnosticLineSink sink);

	bool valid() const noexcept;
	bool write(const DiagnosticEvent& event);
	std::uint64_t emittedCount() const;

private:
	std::string sessionId;
	DiagnosticLineSink sink;
	mutable std::mutex mutex;
	std::uint64_t nextSequence = 1;
};
}
