#include "EditorRunDiagnostics.h"

#include <array>
#include <utility>

namespace
{
const char* severityName(EditorRun::DiagnosticSeverity severity)
{
	switch (severity)
	{
	case EditorRun::DiagnosticSeverity::Info:
		return "info";
	case EditorRun::DiagnosticSeverity::Warning:
		return "warning";
	case EditorRun::DiagnosticSeverity::Error:
		return "error";
	}
	return "error";
}

bool isContinuationByte(unsigned char value)
{
	return (value & 0xC0U) == 0x80U;
}

std::size_t validUtf8SequenceLength(
	std::string_view value,
	std::size_t offset)
{
	const unsigned char first =
		static_cast<unsigned char>(value[offset]);
	if (first < 0x80U)
	{
		return 1;
	}

	std::size_t length = 0;
	std::uint32_t codePoint = 0;
	std::uint32_t minimumCodePoint = 0;
	if ((first & 0xE0U) == 0xC0U)
	{
		length = 2;
		codePoint = first & 0x1FU;
		minimumCodePoint = 0x80U;
	}
	else if ((first & 0xF0U) == 0xE0U)
	{
		length = 3;
		codePoint = first & 0x0FU;
		minimumCodePoint = 0x800U;
	}
	else if ((first & 0xF8U) == 0xF0U)
	{
		length = 4;
		codePoint = first & 0x07U;
		minimumCodePoint = 0x10000U;
	}
	else
	{
		return 0;
	}

	if (offset + length > value.size())
	{
		return 0;
	}
	for (std::size_t index = 1; index < length; ++index)
	{
		const unsigned char continuation =
			static_cast<unsigned char>(value[offset + index]);
		if (!isContinuationByte(continuation))
		{
			return 0;
		}
		codePoint =
			(codePoint << 6U) |
			static_cast<std::uint32_t>(continuation & 0x3FU);
	}
	if (codePoint < minimumCodePoint ||
		codePoint > 0x10FFFFU ||
		(codePoint >= 0xD800U && codePoint <= 0xDFFFU))
	{
		return 0;
	}
	return length;
}

void appendJsonString(std::string& output, std::string_view value)
{
	static constexpr std::array<char, 16> Hex =
	{
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	output.push_back('"');
	for (std::size_t offset = 0; offset < value.size();)
	{
		const unsigned char current =
			static_cast<unsigned char>(value[offset]);
		switch (current)
		{
		case '"':
			output.append("\\\"");
			++offset;
			continue;
		case '\\':
			output.append("\\\\");
			++offset;
			continue;
		case '\b':
			output.append("\\b");
			++offset;
			continue;
		case '\f':
			output.append("\\f");
			++offset;
			continue;
		case '\n':
			output.append("\\n");
			++offset;
			continue;
		case '\r':
			output.append("\\r");
			++offset;
			continue;
		case '\t':
			output.append("\\t");
			++offset;
			continue;
		default:
			break;
		}

		if (current < 0x20U)
		{
			output.append("\\u00");
			output.push_back(Hex[(current >> 4U) & 0x0FU]);
			output.push_back(Hex[current & 0x0FU]);
			++offset;
			continue;
		}
		if (current < 0x80U)
		{
			output.push_back(static_cast<char>(current));
			++offset;
			continue;
		}

		const std::size_t length =
			validUtf8SequenceLength(value, offset);
		if (length == 0)
		{
			output.append("\\ufffd");
			++offset;
			continue;
		}
		output.append(value.substr(offset, length));
		offset += length;
	}
	output.push_back('"');
}

void appendNamedString(
	std::string& output,
	const char* name,
	std::string_view value)
{
	output.push_back(',');
	output.push_back('"');
	output.append(name);
	output.append("\":");
	appendJsonString(output, value);
}

std::string buildLine(
	std::string_view sessionId,
	std::uint64_t sequence,
	const EditorRun::DiagnosticEvent& event)
{
	std::string line;
	line.reserve(
		sessionId.size() +
		event.code.size() +
		event.message.size() +
		event.source.file.size() +
		event.target.size() +
		160);
	line.append("{\"schemaVersion\":1,\"sessionId\":");
	appendJsonString(line, sessionId);
	line.append(",\"sequence\":");
	line.append(std::to_string(sequence));
	appendNamedString(line, "severity", severityName(event.severity));
	appendNamedString(line, "code", event.code);
	appendNamedString(line, "message", event.message);
	if (!event.source.file.empty())
	{
		appendNamedString(line, "file", event.source.file);
		if (event.source.line > 0)
		{
			line.append(",\"line\":");
			line.append(std::to_string(event.source.line));
		}
		if (event.source.column > 0)
		{
			line.append(",\"column\":");
			line.append(std::to_string(event.source.column));
		}
	}
	if (!event.target.empty())
	{
		appendNamedString(line, "target", event.target);
	}
	line.append("}\n");
	return line;
}
}

namespace EditorRun
{
DiagnosticsWriter::DiagnosticsWriter(
	std::string writerSessionId,
	DiagnosticLineSink writerSink) :
	sessionId(std::move(writerSessionId)),
	sink(std::move(writerSink))
{
}

bool DiagnosticsWriter::valid() const noexcept
{
	return !sessionId.empty() && static_cast<bool>(sink);
}

bool DiagnosticsWriter::write(const DiagnosticEvent& event)
{
	if (event.code.empty() || !valid())
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex);
	const std::string line =
		buildLine(sessionId, nextSequence, event);
	if (!sink(line))
	{
		return false;
	}
	++nextSequence;
	return true;
}

std::uint64_t DiagnosticsWriter::emittedCount() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return nextSequence - 1;
}
}
