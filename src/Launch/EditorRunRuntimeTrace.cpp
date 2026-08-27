#include "EditorRunRuntimeTrace.h"

#include "../File/StrictRelativeResourcePath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace
{
constexpr std::size_t MaximumVirtualPathBytes = 64 * 1024;
constexpr std::size_t MaximumResourcePackIdBytes = 1024;
constexpr std::size_t MaximumApiNameBytes = 1024;
constexpr std::size_t MaximumVariableNameBytes = 4096;
constexpr std::size_t MaximumVariableValueBytes = 64 * 1024;

bool exactJsonInteger(std::uint64_t value, bool nonZero)
{
	return value <=
			EditorRun::RuntimeTraceMaximumExactJsonInteger &&
		(!nonZero || value != 0);
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
	std::uint32_t minimum = 0;
	if ((first & 0xE0U) == 0xC0U)
	{
		length = 2;
		codePoint = first & 0x1FU;
		minimum = 0x80U;
	}
	else if ((first & 0xF0U) == 0xE0U)
	{
		length = 3;
		codePoint = first & 0x0FU;
		minimum = 0x800U;
	}
	else if ((first & 0xF8U) == 0xF0U)
	{
		length = 4;
		codePoint = first & 0x07U;
		minimum = 0x10000U;
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
			static_cast<unsigned char>(
				value[offset + index]);
		if ((continuation & 0xC0U) != 0x80U)
		{
			return 0;
		}
		codePoint =
			(codePoint << 6U) |
			(continuation & 0x3FU);
	}
	return codePoint >= minimum &&
		codePoint <= 0x10FFFFU &&
		!(codePoint >= 0xD800U &&
			codePoint <= 0xDFFFU)
		? length
		: 0;
}

bool strictUtf8(
	std::string_view value,
	std::size_t maximumBytes,
	bool allowEmpty)
{
	if (value.size() > maximumBytes ||
		(!allowEmpty && value.empty()))
	{
		return false;
	}
	for (std::size_t offset = 0;
		offset < value.size();)
	{
		const std::size_t length =
			validUtf8SequenceLength(value, offset);
		if (length == 0)
		{
			return false;
		}
		offset += length;
	}
	return true;
}

bool strictIdentifierUtf8(
	std::string_view value,
	std::size_t maximumBytes)
{
	return strictUtf8(
			value,
			maximumBytes,
			false) &&
		value.find('\0') ==
			std::string_view::npos;
}

bool canonicalSessionId(std::string_view value)
{
	if (value.size() != 36)
	{
		return false;
	}
	for (std::size_t index = 0;
		index < value.size(); ++index)
	{
		if (index == 8 || index == 13 ||
			index == 18 || index == 23)
		{
			if (value[index] != '-')
			{
				return false;
			}
			continue;
		}
		const char character = value[index];
		if (!((character >= '0' &&
				character <= '9') ||
			(character >= 'a' &&
				character <= 'f')))
		{
			return false;
		}
	}
	return true;
}

bool lowercaseApiName(std::string_view value)
{
	if (!strictIdentifierUtf8(
			value, MaximumApiNameBytes))
	{
		return false;
	}
	for (const unsigned char character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			return false;
		}
	}
	return true;
}

bool lowercaseHexSha256(std::string_view value)
{
	if (value.size() != 64)
	{
		return false;
	}
	for (const char character : value)
	{
		if (!((character >= '0' &&
				character <= '9') ||
			(character >= 'a' &&
				character <= 'f')))
		{
			return false;
		}
	}
	return true;
}

bool canonicalInteger(std::string_view value)
{
	if (value.empty())
	{
		return false;
	}
	std::size_t offset = 0;
	if (value.front() == '-')
	{
		if (value.size() == 1)
		{
			return false;
		}
		offset = 1;
	}
	if (value[offset] == '0' &&
		offset + 1 != value.size())
	{
		return false;
	}
	for (; offset < value.size(); ++offset)
	{
		if (value[offset] < '0' ||
			value[offset] > '9')
		{
			return false;
		}
	}
	return value != "-0";
}

bool canonicalReal(std::string_view value)
{
	if (value.empty() ||
		value.front() == '+' ||
		value == "nan" ||
		value == "inf" ||
		value == "-inf")
	{
		return false;
	}
	std::size_t offset =
		value.front() == '-' ? 1 : 0;
	if (offset == value.size())
	{
		return false;
	}
	if (value[offset] == '0')
	{
		++offset;
		if (offset < value.size() &&
			value[offset] >= '0' &&
			value[offset] <= '9')
		{
			return false;
		}
	}
	else
	{
		if (value[offset] < '1' ||
			value[offset] > '9')
		{
			return false;
		}
		while (offset < value.size() &&
			value[offset] >= '0' &&
			value[offset] <= '9')
		{
			++offset;
		}
	}
	if (offset < value.size() &&
		value[offset] == '.')
	{
		++offset;
		const std::size_t fractionStart = offset;
		while (offset < value.size() &&
			value[offset] >= '0' &&
			value[offset] <= '9')
		{
			++offset;
		}
		if (offset == fractionStart)
		{
			return false;
		}
	}
	if (offset < value.size() &&
		value[offset] == 'e')
	{
		++offset;
		if (offset < value.size() &&
			value[offset] == '-')
		{
			++offset;
		}
		const std::size_t exponentStart = offset;
		while (offset < value.size() &&
			value[offset] >= '0' &&
			value[offset] <= '9')
		{
			++offset;
		}
		if (offset == exponentStart ||
			(value[exponentStart] == '0' &&
				offset - exponentStart > 1))
		{
			return false;
		}
	}
	return offset == value.size() &&
		value != "-0";
}

EditorRun::RuntimeTraceValidationResult failure(
	EditorRun::RuntimeTraceValidationError error,
	std::string fieldPath)
{
	EditorRun::RuntimeTraceValidationResult result;
	result.error = error;
	result.fieldPath = std::move(fieldPath);
	return result;
}

EditorRun::RuntimeTraceValidationResult validateExecutionId(
	std::uint64_t value,
	std::string field)
{
	return exactJsonInteger(value, true)
		? EditorRun::RuntimeTraceValidationResult{}
		: failure(
			EditorRun::RuntimeTraceValidationError::
				IntegerOutOfRange,
			std::move(field));
}

bool strictVirtualPath(std::string_view value)
{
	if (value.empty() ||
		value.size() > MaximumVirtualPathBytes)
	{
		return false;
	}
	const ResourcePathSafety::StrictRelativePathResult
		normalized =
			ResourcePathSafety::
				normalizeStrictRelativeResourcePath(value);
	return normalized.succeeded() &&
		normalized.normalizedPath == value;
}

void appendJsonString(
	std::string& output,
	std::string_view value)
{
	static constexpr std::array<char, 16> Hex =
	{
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	output.push_back('"');
	for (const unsigned char character : value)
	{
		switch (character)
		{
		case '"':
			output.append("\\\"");
			break;
		case '\\':
			output.append("\\\\");
			break;
		case '\b':
			output.append("\\b");
			break;
		case '\f':
			output.append("\\f");
			break;
		case '\n':
			output.append("\\n");
			break;
		case '\r':
			output.append("\\r");
			break;
		case '\t':
			output.append("\\t");
			break;
		default:
			if (character < 0x20U)
			{
				output.append("\\u00");
				output.push_back(
					Hex[(character >> 4U) & 0x0FU]);
				output.push_back(
					Hex[character & 0x0FU]);
			}
			else
			{
				output.push_back(
					static_cast<char>(character));
			}
			break;
		}
	}
	output.push_back('"');
}

void appendNamedString(
	std::string& output,
	std::string_view name,
	std::string_view value)
{
	output.append(",\"");
	output.append(name);
	output.append("\":");
	appendJsonString(output, value);
}

void appendNamedInteger(
	std::string& output,
	std::string_view name,
	std::uint64_t value)
{
	output.append(",\"");
	output.append(name);
	output.append("\":");
	output.append(std::to_string(value));
}

template<typename Value>
std::size_t retainedStringBytes(const Value&)
{
	return 0;
}

std::size_t retainedStringBytes(
	const EditorRun::RuntimeTraceScriptStartEvent& event)
{
	return event.source.virtualPath.size() +
		event.source.contentSha256.size() +
		(event.source.resourcePackId.has_value()
			? event.source.resourcePackId->size()
			: 0);
}

std::size_t retainedStringBytes(
	const EditorRun::RuntimeTraceApiCallEvent& event)
{
	return event.apiName.size();
}

std::size_t retainedStringBytes(
	const EditorRun::RuntimeTraceMapChangeEvent& event)
{
	return event.target.size();
}

std::size_t retainedStringBytes(
	const EditorRun::RuntimeTraceVariableChangeEvent& event)
{
	return event.variableName.size() +
		event.beforeValue.size() +
		event.afterValue.size();
}

class Sha256
{
public:
	Sha256()
	{
		reset();
	}

	void update(
		const unsigned char* data,
		std::size_t size)
	{
		totalBytes += size;
		while (size > 0)
		{
			const std::size_t available =
				block.size() - blockSize;
			const std::size_t copied =
				(std::min)(available, size);
			std::memcpy(
				block.data() + blockSize,
				data,
				copied);
			blockSize += copied;
			data += copied;
			size -= copied;
			if (blockSize == block.size())
			{
				transform(block.data());
				blockSize = 0;
			}
		}
	}

	std::array<unsigned char, 32> finish()
	{
		const std::uint64_t bitCount =
			static_cast<std::uint64_t>(
				totalBytes) * 8U;
		block[blockSize++] = 0x80U;
		if (blockSize > 56)
		{
			while (blockSize < block.size())
			{
				block[blockSize++] = 0;
			}
			transform(block.data());
			blockSize = 0;
		}
		while (blockSize < 56)
		{
			block[blockSize++] = 0;
		}
		for (int shift = 56; shift >= 0; shift -= 8)
		{
			block[blockSize++] =
				static_cast<unsigned char>(
					(bitCount >> shift) & 0xFFU);
		}
		transform(block.data());

		std::array<unsigned char, 32> digest = {};
		for (std::size_t index = 0;
			index < state.size(); ++index)
		{
			digest[index * 4] =
				static_cast<unsigned char>(
					state[index] >> 24U);
			digest[index * 4 + 1] =
				static_cast<unsigned char>(
					state[index] >> 16U);
			digest[index * 4 + 2] =
				static_cast<unsigned char>(
					state[index] >> 8U);
			digest[index * 4 + 3] =
				static_cast<unsigned char>(
					state[index]);
		}
		return digest;
	}

private:
	static std::uint32_t rotateRight(
		std::uint32_t value,
		std::uint32_t amount)
	{
		return (value >> amount) |
			(value << (32U - amount));
	}

	void reset()
	{
		state =
		{
			0x6a09e667U, 0xbb67ae85U,
			0x3c6ef372U, 0xa54ff53aU,
			0x510e527fU, 0x9b05688cU,
			0x1f83d9abU, 0x5be0cd19U
		};
		totalBytes = 0;
		blockSize = 0;
	}

	void transform(const unsigned char* bytes)
	{
		static constexpr std::array<
			std::uint32_t, 64> Constants =
		{
			0x428a2f98U, 0x71374491U,
			0xb5c0fbcfU, 0xe9b5dba5U,
			0x3956c25bU, 0x59f111f1U,
			0x923f82a4U, 0xab1c5ed5U,
			0xd807aa98U, 0x12835b01U,
			0x243185beU, 0x550c7dc3U,
			0x72be5d74U, 0x80deb1feU,
			0x9bdc06a7U, 0xc19bf174U,
			0xe49b69c1U, 0xefbe4786U,
			0x0fc19dc6U, 0x240ca1ccU,
			0x2de92c6fU, 0x4a7484aaU,
			0x5cb0a9dcU, 0x76f988daU,
			0x983e5152U, 0xa831c66dU,
			0xb00327c8U, 0xbf597fc7U,
			0xc6e00bf3U, 0xd5a79147U,
			0x06ca6351U, 0x14292967U,
			0x27b70a85U, 0x2e1b2138U,
			0x4d2c6dfcU, 0x53380d13U,
			0x650a7354U, 0x766a0abbU,
			0x81c2c92eU, 0x92722c85U,
			0xa2bfe8a1U, 0xa81a664bU,
			0xc24b8b70U, 0xc76c51a3U,
			0xd192e819U, 0xd6990624U,
			0xf40e3585U, 0x106aa070U,
			0x19a4c116U, 0x1e376c08U,
			0x2748774cU, 0x34b0bcb5U,
			0x391c0cb3U, 0x4ed8aa4aU,
			0x5b9cca4fU, 0x682e6ff3U,
			0x748f82eeU, 0x78a5636fU,
			0x84c87814U, 0x8cc70208U,
			0x90befffaU, 0xa4506cebU,
			0xbef9a3f7U, 0xc67178f2U
		};
		std::array<std::uint32_t, 64> words = {};
		for (std::size_t index = 0; index < 16; ++index)
		{
			words[index] =
				(static_cast<std::uint32_t>(
					bytes[index * 4]) << 24U) |
				(static_cast<std::uint32_t>(
					bytes[index * 4 + 1]) << 16U) |
				(static_cast<std::uint32_t>(
					bytes[index * 4 + 2]) << 8U) |
				static_cast<std::uint32_t>(
					bytes[index * 4 + 3]);
		}
		for (std::size_t index = 16;
			index < words.size(); ++index)
		{
			const std::uint32_t s0 =
				rotateRight(words[index - 15], 7U) ^
				rotateRight(words[index - 15], 18U) ^
				(words[index - 15] >> 3U);
			const std::uint32_t s1 =
				rotateRight(words[index - 2], 17U) ^
				rotateRight(words[index - 2], 19U) ^
				(words[index - 2] >> 10U);
			words[index] =
				words[index - 16] + s0 +
				words[index - 7] + s1;
		}
		std::uint32_t a = state[0];
		std::uint32_t b = state[1];
		std::uint32_t c = state[2];
		std::uint32_t d = state[3];
		std::uint32_t e = state[4];
		std::uint32_t f = state[5];
		std::uint32_t g = state[6];
		std::uint32_t h = state[7];
		for (std::size_t index = 0;
			index < words.size(); ++index)
		{
			const std::uint32_t sum1 =
				rotateRight(e, 6U) ^
				rotateRight(e, 11U) ^
				rotateRight(e, 25U);
			const std::uint32_t choice =
				(e & f) ^ ((~e) & g);
			const std::uint32_t temporary1 =
				h + sum1 + choice +
				Constants[index] + words[index];
			const std::uint32_t sum0 =
				rotateRight(a, 2U) ^
				rotateRight(a, 13U) ^
				rotateRight(a, 22U);
			const std::uint32_t majority =
				(a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t temporary2 =
				sum0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + temporary1;
			d = c;
			c = b;
			b = a;
			a = temporary1 + temporary2;
		}
		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}

	std::array<std::uint32_t, 8> state = {};
	std::array<unsigned char, 64> block = {};
	std::size_t totalBytes = 0;
	std::size_t blockSize = 0;
};
}

namespace EditorRun
{
std::string_view runtimeTraceRootKindName(
	RuntimeTraceRootKind value) noexcept
{
	switch (value)
	{
	case RuntimeTraceRootKind::Active:
		return "active";
	case RuntimeTraceRootKind::DependencyId:
		return "dependency-id";
	case RuntimeTraceRootKind::Common:
		return "common";
	}
	return {};
}

std::string_view runtimeTraceSourceLayerName(
	RuntimeTraceSourceLayer value) noexcept
{
	switch (value)
	{
	case RuntimeTraceSourceLayer::Formal:
		return "formal";
	case RuntimeTraceSourceLayer::Overlay:
		return "overlay";
	}
	return {};
}

std::string_view runtimeTraceSessionFinishStatusName(
	RuntimeTraceSessionFinishStatus value) noexcept
{
	switch (value)
	{
	case RuntimeTraceSessionFinishStatus::Completed:
		return "completed";
	case RuntimeTraceSessionFinishStatus::ResourceFailure:
		return "resource-failure";
	case RuntimeTraceSessionFinishStatus::EngineFailure:
		return "engine-failure";
	case RuntimeTraceSessionFinishStatus::SceneFailure:
		return "scene-failure";
	case RuntimeTraceSessionFinishStatus::OrchestrationFailure:
		return "orchestration-failure";
	}
	return {};
}

std::string_view runtimeTraceScriptFinishStatusName(
	RuntimeTraceScriptFinishStatus value) noexcept
{
	switch (value)
	{
	case RuntimeTraceScriptFinishStatus::Completed:
		return "completed";
	case RuntimeTraceScriptFinishStatus::LoadError:
		return "load-error";
	case RuntimeTraceScriptFinishStatus::RuntimeError:
		return "runtime-error";
	case RuntimeTraceScriptFinishStatus::Aborted:
		return "aborted";
	}
	return {};
}

std::string_view runtimeTraceVariableValueTypeName(
	RuntimeTraceVariableValueType value) noexcept
{
	switch (value)
	{
	case RuntimeTraceVariableValueType::Integer:
		return "integer";
	case RuntimeTraceVariableValueType::Real:
		return "real";
	case RuntimeTraceVariableValueType::String:
		return "string";
	case RuntimeTraceVariableValueType::Boolean:
		return "boolean";
	case RuntimeTraceVariableValueType::Nil:
		return "nil";
	}
	return {};
}

std::string_view runtimeTraceEventTypeName(
	const RuntimeTraceEvent& event) noexcept
{
	return std::visit(
		[](const auto& payload) -> std::string_view
		{
			using Payload = std::decay_t<
				decltype(payload)>;
			if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSessionStartEvent>)
			{
				return RuntimeTraceSessionStartEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSessionFinishEvent>)
			{
				return RuntimeTraceSessionFinishEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptStartEvent>)
			{
				return RuntimeTraceScriptStartEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptFinishEvent>)
			{
				return RuntimeTraceScriptFinishEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSourceLineEvent>)
			{
				return RuntimeTraceSourceLineEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceApiCallEvent>)
			{
				return RuntimeTraceApiCallEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceMapChangeEvent>)
			{
				return RuntimeTraceMapChangeEventType;
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceVariableChangeEvent>)
			{
				return RuntimeTraceVariableChangeEventType;
			}
			else
			{
				return RuntimeTraceDroppedEventType;
			}
		},
		event.payload);
}

bool runtimeTraceEventIsDroppable(
	const RuntimeTraceEvent& event) noexcept
{
	return std::holds_alternative<
		RuntimeTraceSourceLineEvent>(event.payload);
}

std::size_t runtimeTraceEventRetainedBytes(
	const RuntimeTraceEvent& event) noexcept
{
	return sizeof(RuntimeTraceEvent) +
		std::visit(
			[](const auto& payload)
			{
				return retainedStringBytes(payload);
			},
			event.payload);
}

RuntimeTraceValidationResult validateRuntimeTraceEvent(
	const RuntimeTraceEvent& event)
{
	if (event.elapsedMicroseconds.has_value() &&
		!exactJsonInteger(
			*event.elapsedMicroseconds, false))
	{
		return failure(
			RuntimeTraceValidationError::
				IntegerOutOfRange,
			"elapsedMicroseconds");
	}
	return std::visit(
		[](const auto& payload)
			-> RuntimeTraceValidationResult
		{
			using Payload = std::decay_t<
				decltype(payload)>;
			if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSessionStartEvent>)
			{
				return {};
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSessionFinishEvent>)
			{
				return runtimeTraceSessionFinishStatusName(
						payload.status).empty()
					? failure(
						RuntimeTraceValidationError::
							InvalidVariableValue,
						"status")
					: RuntimeTraceValidationResult{};
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptStartEvent>)
			{
				RuntimeTraceValidationResult result =
					validateExecutionId(
						payload.executionId,
						"executionId");
				if (!result.succeeded())
				{
					return result;
				}
				if (payload.parentExecutionId.has_value())
				{
					result = validateExecutionId(
						*payload.parentExecutionId,
						"parentExecutionId");
					if (!result.succeeded())
					{
						return result;
					}
				}
				if (!strictVirtualPath(
						payload.source.virtualPath))
				{
					return failure(
						RuntimeTraceValidationError::
							InvalidVirtualPath,
						"virtualPath");
				}
				if (!lowercaseHexSha256(
						payload.source.contentSha256))
				{
					return failure(
						RuntimeTraceValidationError::
							InvalidSha256,
						"contentSha256");
				}
				if (runtimeTraceRootKindName(
						payload.source.rootKind).empty() ||
					!exactJsonInteger(
						payload.source.rootOrdinal,
						false))
				{
					return failure(
						RuntimeTraceValidationError::
							IntegerOutOfRange,
						"rootOrdinal");
				}
				if (payload.source.resourcePackId.
						has_value() &&
					!strictIdentifierUtf8(
						*payload.source.resourcePackId,
						MaximumResourcePackIdBytes))
				{
					return failure(
						RuntimeTraceValidationError::
							InvalidResourcePackId,
						"resourcePackId");
				}
				if (runtimeTraceSourceLayerName(
						payload.source.sourceLayer).empty())
				{
					return failure(
						RuntimeTraceValidationError::
							InvalidVariableValue,
						"sourceLayer");
				}
				return {};
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptFinishEvent>)
			{
				RuntimeTraceValidationResult result =
					validateExecutionId(
						payload.executionId,
						"executionId");
				if (!result.succeeded())
				{
					return result;
				}
				return runtimeTraceScriptFinishStatusName(
						payload.status).empty()
					? failure(
						RuntimeTraceValidationError::
							InvalidVariableValue,
						"status")
					: RuntimeTraceValidationResult{};
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSourceLineEvent>)
			{
				RuntimeTraceValidationResult result =
					validateExecutionId(
						payload.executionId,
						"executionId");
				if (!result.succeeded())
				{
					return result;
				}
				return exactJsonInteger(
						payload.line, true)
					? RuntimeTraceValidationResult{}
					: failure(
						RuntimeTraceValidationError::
							IntegerOutOfRange,
						"line");
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceApiCallEvent>)
			{
				RuntimeTraceValidationResult result =
					validateExecutionId(
						payload.executionId,
						"executionId");
				if (!result.succeeded())
				{
					return result;
				}
				return lowercaseApiName(
						payload.apiName)
					? RuntimeTraceValidationResult{}
					: failure(
						RuntimeTraceValidationError::
							InvalidApiName,
						"apiName");
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceMapChangeEvent>)
			{
				if (payload.executionId.has_value())
				{
					RuntimeTraceValidationResult result =
						validateExecutionId(
							*payload.executionId,
							"executionId");
					if (!result.succeeded())
					{
						return result;
					}
				}
				return strictVirtualPath(payload.target)
					? RuntimeTraceValidationResult{}
					: failure(
						RuntimeTraceValidationError::
							InvalidVirtualPath,
						"target");
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceVariableChangeEvent>)
			{
				if (payload.executionId.has_value())
				{
					RuntimeTraceValidationResult result =
						validateExecutionId(
							*payload.executionId,
							"executionId");
					if (!result.succeeded())
					{
						return result;
					}
				}
				if (!strictIdentifierUtf8(
						payload.variableName,
						MaximumVariableNameBytes))
				{
					return failure(
						RuntimeTraceValidationError::
							InvalidVariableName,
						"variableName");
				}
				const bool valuesAreUtf8 =
					strictUtf8(
						payload.beforeValue,
						MaximumVariableValueBytes,
						true) &&
					strictUtf8(
						payload.afterValue,
						MaximumVariableValueBytes,
						true);
				bool valuesAreCanonical = false;
				switch (payload.valueType)
				{
				case RuntimeTraceVariableValueType::Integer:
					valuesAreCanonical =
						canonicalInteger(
							payload.beforeValue) &&
						canonicalInteger(
							payload.afterValue);
					break;
				case RuntimeTraceVariableValueType::Real:
					valuesAreCanonical =
						canonicalReal(
							payload.beforeValue) &&
						canonicalReal(
							payload.afterValue);
					break;
				case RuntimeTraceVariableValueType::String:
					valuesAreCanonical = valuesAreUtf8;
					break;
				case RuntimeTraceVariableValueType::Boolean:
					valuesAreCanonical =
						(payload.beforeValue == "true" ||
							payload.beforeValue == "false") &&
						(payload.afterValue == "true" ||
							payload.afterValue == "false");
					break;
				case RuntimeTraceVariableValueType::Nil:
					valuesAreCanonical =
						payload.beforeValue.empty() &&
						payload.afterValue.empty();
					break;
				default:
					break;
				}
				return valuesAreUtf8 &&
						valuesAreCanonical &&
						!runtimeTraceVariableValueTypeName(
							payload.valueType).empty()
					? RuntimeTraceValidationResult{}
					: failure(
						RuntimeTraceValidationError::
							InvalidVariableValue,
						"beforeValue");
			}
			else
			{
				return exactJsonInteger(
						payload.
							droppedSourceLineCount,
						true)
					? RuntimeTraceValidationResult{}
					: failure(
						RuntimeTraceValidationError::
							IntegerOutOfRange,
						"droppedSourceLineCount");
			}
		},
		event.payload);
}

RuntimeTraceValidationResult validateRuntimeTraceRecord(
	const RuntimeTraceRecord& record)
{
	if (!canonicalSessionId(record.sessionId))
	{
		return failure(
			RuntimeTraceValidationError::
				InvalidSessionId,
			"sessionId");
	}
	if (!exactJsonInteger(record.sequence, true))
	{
		return failure(
			RuntimeTraceValidationError::
				IntegerOutOfRange,
			"sequence");
	}
	return validateRuntimeTraceEvent(record.event);
}

RuntimeTraceValidationResult serializeRuntimeTraceRecord(
	const RuntimeTraceRecord& record,
	std::string& output)
{
	output.clear();
	const RuntimeTraceValidationResult validation =
		validateRuntimeTraceRecord(record);
	if (!validation.succeeded())
	{
		return validation;
	}
	output.reserve(
		256 + runtimeTraceEventRetainedBytes(
			record.event));
	output.append("{\"schemaVersion\":1,\"sessionId\":");
	appendJsonString(output, record.sessionId);
	appendNamedInteger(
		output, "sequence", record.sequence);
	appendNamedString(
		output,
		"eventType",
		runtimeTraceEventTypeName(record.event));
	if (record.event.elapsedMicroseconds.has_value())
	{
		appendNamedInteger(
			output,
			"elapsedMicroseconds",
			*record.event.elapsedMicroseconds);
	}
	std::visit(
		[&output](const auto& payload)
		{
			using Payload = std::decay_t<
				decltype(payload)>;
			if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSessionFinishEvent>)
			{
				appendNamedString(
					output,
					"status",
					runtimeTraceSessionFinishStatusName(
						payload.status));
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptStartEvent>)
			{
				appendNamedInteger(
					output,
					"executionId",
					payload.executionId);
				if (payload.parentExecutionId.has_value())
				{
					appendNamedInteger(
						output,
						"parentExecutionId",
						*payload.parentExecutionId);
				}
				appendNamedString(
					output,
					"virtualPath",
					payload.source.virtualPath);
				appendNamedString(
					output,
					"contentSha256",
					payload.source.contentSha256);
				appendNamedString(
					output,
					"rootKind",
					runtimeTraceRootKindName(
						payload.source.rootKind));
				appendNamedInteger(
					output,
					"rootOrdinal",
					payload.source.rootOrdinal);
				if (payload.source.resourcePackId.
					has_value())
				{
					appendNamedString(
						output,
						"resourcePackId",
						*payload.source.resourcePackId);
				}
				appendNamedString(
					output,
					"sourceLayer",
					runtimeTraceSourceLayerName(
						payload.source.sourceLayer));
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceScriptFinishEvent>)
			{
				appendNamedInteger(
					output,
					"executionId",
					payload.executionId);
				appendNamedString(
					output,
					"status",
					runtimeTraceScriptFinishStatusName(
						payload.status));
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceSourceLineEvent>)
			{
				appendNamedInteger(
					output,
					"executionId",
					payload.executionId);
				appendNamedInteger(
					output,
					"line",
					payload.line);
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceApiCallEvent>)
			{
				appendNamedInteger(
					output,
					"executionId",
					payload.executionId);
				appendNamedString(
					output,
					"apiName",
					payload.apiName);
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceMapChangeEvent>)
			{
				if (payload.executionId.has_value())
				{
					appendNamedInteger(
						output,
						"executionId",
						*payload.executionId);
				}
				appendNamedString(
					output,
					"target",
					payload.target);
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceVariableChangeEvent>)
			{
				if (payload.executionId.has_value())
				{
					appendNamedInteger(
						output,
						"executionId",
						*payload.executionId);
				}
				appendNamedString(
					output,
					"variableName",
					payload.variableName);
				appendNamedString(
					output,
					"valueType",
					runtimeTraceVariableValueTypeName(
						payload.valueType));
				appendNamedString(
					output,
					"beforeValue",
					payload.beforeValue);
				appendNamedString(
					output,
					"afterValue",
					payload.afterValue);
			}
			else if constexpr (std::is_same_v<
					Payload,
					RuntimeTraceDroppedEvent>)
			{
				appendNamedInteger(
					output,
					"droppedSourceLineCount",
					payload.
						droppedSourceLineCount);
			}
		},
		record.event.payload);
	output.append("}\n");
	return {};
}

std::string runtimeTraceSha256Hex(
	std::string_view bytes)
{
	Sha256 sha256;
	sha256.update(
		reinterpret_cast<const unsigned char*>(
			bytes.data()),
		bytes.size());
	const std::array<unsigned char, 32> digest =
		sha256.finish();
	static constexpr std::array<char, 16> Hex =
	{
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
	};
	std::string result;
	result.reserve(64);
	for (const unsigned char byte : digest)
	{
		result.push_back(Hex[byte >> 4U]);
		result.push_back(Hex[byte & 0x0FU]);
	}
	return result;
}
}
