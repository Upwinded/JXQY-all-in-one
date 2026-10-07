#pragma once

#include "../Launch/StrictJson.h"
#include <charconv>
#include <initializer_list>
#include <stdexcept>

namespace GameplayAutomation
{
using Value = StrictJson::JsonValue;
using Type = StrictJson::JsonType;
inline constexpr std::size_t MaximumRequestBytes = 64 * 1024;
inline constexpr std::size_t MaximumResponseBytes = 4 * 1024 * 1024;

inline Value object()
{
	Value value;
	value.type = Type::Object;
	return value;
}
inline Value array()
{
	Value value;
	value.type = Type::Array;
	return value;
}
inline Value string(std::string text)
{
	Value value;
	value.type = Type::String;
	value.text = std::move(text);
	return value;
}
inline Value number(std::int64_t number)
{
	Value value;
	value.type = Type::Number;
	value.text = std::to_string(number);
	return value;
}
inline Value boolean(bool state)
{
	Value value;
	value.type = Type::Boolean;
	value.booleanValue = state;
	return value;
}
inline const Value& member(const Value& value, const std::string& key)
{
	const auto found = value.objectValues.find(key);
	if (value.type != Type::Object || found == value.objectValues.end())
	{
		throw std::runtime_error("missing_field: " + key);
	}
	return found->second;
}
inline std::string textMember(const Value& value, const std::string& key)
{
	const auto& field = member(value, key);
	if (field.type != Type::String)
	{
		throw std::runtime_error("expected_string: " + key);
	}
	return field.text;
}
inline std::int64_t integer(const Value& value)
{
	std::int64_t result = 0;
	const auto parsed = std::from_chars(value.text.data(), value.text.data() + value.text.size(), result);
	if (value.type != Type::Number || parsed.ec != std::errc()
		|| parsed.ptr != value.text.data() + value.text.size())
	{
		throw std::runtime_error("expected_integer");
	}
	return result;
}
inline int integerMember(const Value& value, const std::string& key, int minimum, int maximum)
{
	const auto result = integer(member(value, key));
	if (result < minimum || result > maximum)
	{
		throw std::runtime_error("out_of_range: " + key);
	}
	return static_cast<int>(result);
}
inline bool boolMember(const Value& value, const std::string& key)
{
	const auto& field = member(value, key);
	if (field.type != Type::Boolean)
	{
		throw std::runtime_error("expected_boolean: " + key);
	}
	return field.booleanValue;
}
inline void fields(const Value& value, std::initializer_list<const char*> allowed)
{
	if (value.type != Type::Object)
	{
		throw std::runtime_error("expected_object");
	}
	for (const auto& entry : value.objectValues)
	{
		bool found = false;
		for (const auto* key : allowed)
		{
			found = found || entry.first == key;
		}
		if (!found)
		{
			throw std::runtime_error("unknown_field: " + entry.first);
		}
	}
}
inline void append(const Value& value, std::string& output)
{
	switch (value.type)
	{
	case Type::Null: output += "null"; break;
	case Type::Boolean: output += value.booleanValue ? "true" : "false"; break;
	case Type::Number: output += value.text; break;
	case Type::String: StrictJson::appendEscapedJsonString(value.text, output); break;
	case Type::Array:
		output += '[';
		for (const auto& child : value.arrayValues)
		{
			if (&child != &value.arrayValues.front()) output += ',';
			append(child, output);
		}
		output += ']';
		break;
	case Type::Object:
		output += '{';
		for (auto it = value.objectValues.begin(); it != value.objectValues.end(); ++it)
		{
			if (it != value.objectValues.begin()) output += ',';
			StrictJson::appendEscapedJsonString(it->first, output);
			output += ':';
			append(it->second, output);
		}
		output += '}';
		break;
	}
	if (output.size() > MaximumResponseBytes) throw std::runtime_error("response_too_large");
}
inline std::string serialize(const Value& value)
{
	std::string output;
	append(value, output);
	return output + '\n';
}
inline Value response(std::int64_t id, bool ok, Value data)
{
	Value result = object();
	result.objectValues = {{"version", number(1)}, {"id", number(id)},
		{"ok", boolean(ok)}, {ok ? "data" : "error", std::move(data)}};
	return result;
}
}
