#pragma once
#include "EditorRunDescriptor.h"
#include <vector>
#include <utility>

// Shared without changing the descriptor parser or its error contract.
namespace StrictJson
{
enum class JsonType
{
	Null,
	Boolean,
	Number,
	String,
	Array,
	Object
};

struct JsonValue
{
	JsonType type = JsonType::Null;
	bool booleanValue = false;
	std::string text;
	std::vector<JsonValue> arrayValues;
	std::map<std::string, JsonValue> objectValues;
};

class StrictJsonParser
{
public:
	explicit StrictJsonParser(std::string_view source)
		: text(source)
	{
	}

	bool parse(JsonValue& value)
	{
		skipWhitespace();
		if (!parseValue(value, 1))
		{
			return false;
		}
		skipWhitespace();
		if (position != text.size())
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Unexpected content after the root JSON value");
			return false;
		}
		return true;
	}

	EditorRun::DescriptorError error() const noexcept
	{
		return parseError;
	}

	const std::string& message() const noexcept
	{
		return parseMessage;
	}

	std::size_t errorLine() const noexcept
	{
		return failureLine;
	}

	std::size_t errorColumn() const noexcept
	{
		return failureColumn;
	}

private:
	bool parseValue(JsonValue& value, std::size_t depth)
	{
		if (depth > EditorRun::MaximumJsonDepth)
		{
			fail(EditorRun::DescriptorError::MaximumDepthExceeded,
				"JSON nesting exceeds the supported depth");
			return false;
		}
		if (position >= text.size())
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Unexpected end of JSON input");
			return false;
		}

		switch (text[position])
		{
		case '{':
			return parseObject(value, depth);
		case '[':
			return parseArray(value, depth);
		case '"':
			value.type = JsonType::String;
			return parseString(value.text);
		case 't':
			value.type = JsonType::Boolean;
			value.booleanValue = true;
			return consumeLiteral("true");
		case 'f':
			value.type = JsonType::Boolean;
			value.booleanValue = false;
			return consumeLiteral("false");
		case 'n':
			value.type = JsonType::Null;
			return consumeLiteral("null");
		default:
			if (text[position] == '-' ||
				(text[position] >= '0' && text[position] <= '9'))
			{
				value.type = JsonType::Number;
				return parseNumber(value.text);
			}
			fail(EditorRun::DescriptorError::InvalidJson,
				"Expected a JSON value");
			return false;
		}
	}

	bool parseObject(JsonValue& value, std::size_t depth)
	{
		value.type = JsonType::Object;
		++position;
		skipWhitespace();
		if (consume('}'))
		{
			return true;
		}

		while (position < text.size())
		{
			if (text[position] != '"')
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Expected a quoted object key");
				return false;
			}
			std::string key;
			if (!parseString(key))
			{
				return false;
			}
			if (value.objectValues.find(key) != value.objectValues.end())
			{
				fail(EditorRun::DescriptorError::DuplicateKey,
					"Duplicate JSON object key: " + key);
				return false;
			}

			skipWhitespace();
			if (!consume(':'))
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Expected ':' after an object key");
				return false;
			}
			skipWhitespace();

			JsonValue member;
			if (!parseValue(member, depth + 1))
			{
				return false;
			}
			value.objectValues.emplace(std::move(key), std::move(member));

			skipWhitespace();
			if (consume('}'))
			{
				return true;
			}
			if (!consume(','))
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Expected ',' or '}' in an object");
				return false;
			}
			skipWhitespace();
		}

		fail(EditorRun::DescriptorError::InvalidJson,
			"Unterminated JSON object");
		return false;
	}

	bool parseArray(JsonValue& value, std::size_t depth)
	{
		value.type = JsonType::Array;
		++position;
		skipWhitespace();
		if (consume(']'))
		{
			return true;
		}

		while (position < text.size())
		{
			JsonValue item;
			if (!parseValue(item, depth + 1))
			{
				return false;
			}
			value.arrayValues.push_back(std::move(item));
			skipWhitespace();
			if (consume(']'))
			{
				return true;
			}
			if (!consume(','))
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Expected ',' or ']' in an array");
				return false;
			}
			skipWhitespace();
		}

		fail(EditorRun::DescriptorError::InvalidJson,
			"Unterminated JSON array");
		return false;
	}

	bool parseString(std::string& value)
	{
		if (!consume('"'))
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Expected a JSON string");
			return false;
		}

		value.clear();
		while (position < text.size())
		{
			const unsigned char character =
				static_cast<unsigned char>(text[position++]);
			if (character == '"')
			{
				return true;
			}
			if (character < 0x20)
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Unescaped control character in JSON string");
				return false;
			}
			if (character != '\\')
			{
				value.push_back(static_cast<char>(character));
			}
			else
			{
				if (position >= text.size())
				{
					fail(EditorRun::DescriptorError::InvalidJson,
						"Unterminated JSON escape");
					return false;
				}
				const char escape = text[position++];
				switch (escape)
				{
				case '"':
				case '\\':
				case '/':
					value.push_back(escape);
					break;
				case 'b':
					value.push_back('\b');
					break;
				case 'f':
					value.push_back('\f');
					break;
				case 'n':
					value.push_back('\n');
					break;
				case 'r':
					value.push_back('\r');
					break;
				case 't':
					value.push_back('\t');
					break;
				case 'u':
					if (!appendUnicodeEscape(value))
					{
						return false;
					}
					break;
				default:
					fail(EditorRun::DescriptorError::InvalidJson,
						"Invalid JSON string escape");
					return false;
				}
			}
			if (value.size() > EditorRun::MaximumStringBytes)
			{
				fail(EditorRun::DescriptorError::StringTooLarge,
					"JSON string exceeds the supported byte limit");
				return false;
			}
		}

		fail(EditorRun::DescriptorError::InvalidJson,
			"Unterminated JSON string");
		return false;
	}

	bool appendUnicodeEscape(std::string& value)
	{
		std::uint32_t codePoint = 0;
		if (!parseHexQuad(codePoint))
		{
			return false;
		}
		if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
		{
			if (position + 2 > text.size() ||
				text[position] != '\\' || text[position + 1] != 'u')
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"High surrogate is not followed by a low surrogate");
				return false;
			}
			position += 2;
			std::uint32_t lowSurrogate = 0;
			if (!parseHexQuad(lowSurrogate))
			{
				return false;
			}
			if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF)
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Invalid low surrogate in JSON string");
				return false;
			}
			codePoint = 0x10000 +
				((codePoint - 0xD800) << 10) +
				(lowSurrogate - 0xDC00);
		}
		else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Unexpected low surrogate in JSON string");
			return false;
		}

		if (codePoint <= 0x7F)
		{
			value.push_back(static_cast<char>(codePoint));
		}
		else if (codePoint <= 0x7FF)
		{
			value.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
			value.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else if (codePoint <= 0xFFFF)
		{
			value.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
			value.push_back(static_cast<char>(
				0x80 | ((codePoint >> 6) & 0x3F)));
			value.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else
		{
			value.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
			value.push_back(static_cast<char>(
				0x80 | ((codePoint >> 12) & 0x3F)));
			value.push_back(static_cast<char>(
				0x80 | ((codePoint >> 6) & 0x3F)));
			value.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		return true;
	}

	bool parseHexQuad(std::uint32_t& value)
	{
		if (position + 4 > text.size())
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Incomplete Unicode escape");
			return false;
		}
		value = 0;
		for (int index = 0; index < 4; ++index)
		{
			const char character = text[position++];
			value <<= 4;
			if (character >= '0' && character <= '9')
			{
				value += static_cast<std::uint32_t>(character - '0');
			}
			else if (character >= 'a' && character <= 'f')
			{
				value += static_cast<std::uint32_t>(
					character - 'a' + 10);
			}
			else if (character >= 'A' && character <= 'F')
			{
				value += static_cast<std::uint32_t>(
					character - 'A' + 10);
			}
			else
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Invalid hexadecimal digit in Unicode escape");
				return false;
			}
		}
		return true;
	}

	bool parseNumber(std::string& value)
	{
		const std::size_t start = position;
		if (consume('-') && position >= text.size())
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Incomplete JSON number");
			return false;
		}

		if (consume('0'))
		{
			if (position < text.size() &&
				text[position] >= '0' && text[position] <= '9')
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"JSON number has a leading zero");
				return false;
			}
		}
		else
		{
			if (position >= text.size() ||
				text[position] < '1' || text[position] > '9')
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"Invalid JSON number");
				return false;
			}
			while (position < text.size() &&
				text[position] >= '0' && text[position] <= '9')
			{
				++position;
			}
		}

		if (consume('.'))
		{
			if (position >= text.size() ||
				text[position] < '0' || text[position] > '9')
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"JSON fraction has no digits");
				return false;
			}
			while (position < text.size() &&
				text[position] >= '0' && text[position] <= '9')
			{
				++position;
			}
		}
		if (position < text.size() &&
			(text[position] == 'e' || text[position] == 'E'))
		{
			++position;
			if (position < text.size() &&
				(text[position] == '+' || text[position] == '-'))
			{
				++position;
			}
			if (position >= text.size() ||
				text[position] < '0' || text[position] > '9')
			{
				fail(EditorRun::DescriptorError::InvalidJson,
					"JSON exponent has no digits");
				return false;
			}
			while (position < text.size() &&
				text[position] >= '0' && text[position] <= '9')
			{
				++position;
			}
		}

		value.assign(text.substr(start, position - start));
		return true;
	}

	bool consumeLiteral(std::string_view literal)
	{
		if (text.substr(position, literal.size()) != literal)
		{
			fail(EditorRun::DescriptorError::InvalidJson,
				"Invalid JSON literal");
			return false;
		}
		position += literal.size();
		return true;
	}

	bool consume(char expected)
	{
		if (position >= text.size() || text[position] != expected)
		{
			return false;
		}
		++position;
		return true;
	}

	void skipWhitespace()
	{
		while (position < text.size() &&
			(text[position] == ' ' || text[position] == '\t' ||
				text[position] == '\r' || text[position] == '\n'))
		{
			++position;
		}
	}

	void fail(EditorRun::DescriptorError error, std::string message)
	{
		if (parseError != EditorRun::DescriptorError::None)
		{
			return;
		}
		parseError = error;
		parseMessage = std::move(message);
		failureLine = 1;
		failureColumn = 1;
		for (std::size_t index = 0;
			index < position && index < text.size(); ++index)
		{
			if (text[index] == '\n')
			{
				++failureLine;
				failureColumn = 1;
			}
			else
			{
				++failureColumn;
			}
		}
	}

	std::string_view text;
	std::size_t position = 0;
	EditorRun::DescriptorError parseError =
		EditorRun::DescriptorError::None;
	std::string parseMessage;
	std::size_t failureLine = 0;
	std::size_t failureColumn = 0;
};

inline void appendEscapedJsonString(std::string_view value, std::string& output)
{
	constexpr char HexDigits[] = "0123456789abcdef";
	output.push_back('"');
	for (const unsigned char character : value)
	{
		switch (character)
		{
		case '"':
			output += "\\\"";
			break;
		case '\\':
			output += "\\\\";
			break;
		case '\b':
			output += "\\b";
			break;
		case '\f':
			output += "\\f";
			break;
		case '\n':
			output += "\\n";
			break;
		case '\r':
			output += "\\r";
			break;
		case '\t':
			output += "\\t";
			break;
		default:
			if (character < 0x20)
			{
				output += "\\u00";
				output.push_back(HexDigits[(character >> 4) & 0x0F]);
				output.push_back(HexDigits[character & 0x0F]);
			}
			else
			{
				output.push_back(static_cast<char>(character));
			}
			break;
		}
	}
	output.push_back('"');
}

}
