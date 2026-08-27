#include "MagicRegionFile.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include "../../File/File.h"

namespace
{
constexpr size_t MaximumJsonDepth = 64;
constexpr size_t MaximumJsonValues = 1024 * 1024;
constexpr size_t MaximumLayers = 16;
constexpr int MaximumLayerDimension = 512;
constexpr size_t MaximumLayerTiles = 512 * 512;
constexpr size_t MaximumTilesets = 64;
constexpr size_t MaximumTilesPerTileset = 64 * 1024;
constexpr size_t MaximumAnimationFrames = 4096;
constexpr size_t MaximumRegionItems = 512 * 512;

struct JsonValue
{
	enum class Type
	{
		Null,
		Boolean,
		Number,
		String,
		Array,
		Object,
	};

	Type type = Type::Null;
	bool booleanValue = false;
	double numberValue = 0.0;
	std::string stringValue;
	std::vector<JsonValue> arrayValues;
	std::vector<std::pair<std::string, JsonValue>> objectValues;
};

class JsonParser
{
public:
	explicit JsonParser(const std::string& source)
		: text(source)
	{
	}

	bool parse(JsonValue& value)
	{
		try
		{
			position = 0;
			valueCount = 0;
			value = parseValue(0);
			skipWhitespace();
			return position == text.size();
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

private:
	const std::string& text;
	size_t position = 0;
	size_t valueCount = 0;

	void skipWhitespace()
	{
		while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
		{
			position++;
		}
	}

	char peek()
	{
		skipWhitespace();
		return position < text.size() ? text[position] : '\0';
	}

	void expect(char expected)
	{
		skipWhitespace();
		if (position >= text.size() || text[position] != expected)
		{
			throw std::runtime_error("unexpected json token");
		}
		position++;
	}

	JsonValue parseValue(size_t depth)
	{
		if (depth > MaximumJsonDepth || valueCount >= MaximumJsonValues)
		{
			throw std::runtime_error("json safety budget exceeded");
		}
		valueCount++;

		char token = peek();
		if (token == '{')
		{
			return parseObject(depth);
		}
		if (token == '[')
		{
			return parseArray(depth);
		}
		if (token == '"')
		{
			JsonValue value;
			value.type = JsonValue::Type::String;
			value.stringValue = parseString();
			return value;
		}
		if (token == '-' || (token >= '0' && token <= '9'))
		{
			return parseNumber();
		}
		if (consumeLiteral("true"))
		{
			JsonValue value;
			value.type = JsonValue::Type::Boolean;
			value.booleanValue = true;
			return value;
		}
		if (consumeLiteral("false"))
		{
			JsonValue value;
			value.type = JsonValue::Type::Boolean;
			value.booleanValue = false;
			return value;
		}
		if (consumeLiteral("null"))
		{
			return JsonValue();
		}
		throw std::runtime_error("invalid json value");
	}

	JsonValue parseObject(size_t depth)
	{
		JsonValue value;
		value.type = JsonValue::Type::Object;
		expect('{');
		if (peek() == '}')
		{
			position++;
			return value;
		}
		while (true)
		{
			std::string key = parseString();
			expect(':');
			value.objectValues.push_back({ key, parseValue(depth + 1) });
			char token = peek();
			if (token == '}')
			{
				position++;
				break;
			}
			expect(',');
		}
		return value;
	}

	JsonValue parseArray(size_t depth)
	{
		JsonValue value;
		value.type = JsonValue::Type::Array;
		expect('[');
		if (peek() == ']')
		{
			position++;
			return value;
		}
		while (true)
		{
			value.arrayValues.push_back(parseValue(depth + 1));
			char token = peek();
			if (token == ']')
			{
				position++;
				break;
			}
			expect(',');
		}
		return value;
	}

	std::string parseString()
	{
		expect('"');
		std::string result;
		while (position < text.size())
		{
			char ch = text[position++];
			if (ch == '"')
			{
				return result;
			}
			if (ch != '\\')
			{
				if (static_cast<unsigned char>(ch) < 0x20)
				{
					throw std::runtime_error("unescaped json control character");
				}
				result.push_back(ch);
				continue;
			}
			if (position >= text.size())
			{
				throw std::runtime_error("invalid json string escape");
			}
			char escaped = text[position++];
			switch (escaped)
			{
			case '"':
			case '\\':
			case '/':
				result.push_back(escaped);
				break;
			case 'b':
				result.push_back('\b');
				break;
			case 'f':
				result.push_back('\f');
				break;
			case 'n':
				result.push_back('\n');
				break;
			case 'r':
				result.push_back('\r');
				break;
			case 't':
				result.push_back('\t');
				break;
			case 'u':
				if (position + 4 > text.size())
				{
					throw std::runtime_error("invalid json unicode escape");
				}
				for (size_t index = 0; index < 4; index++)
				{
					if (!std::isxdigit(static_cast<unsigned char>(text[position + index])))
					{
						throw std::runtime_error("invalid json unicode escape");
					}
				}
				position += 4;
				result.push_back('?');
				break;
			default:
				throw std::runtime_error("unknown json string escape");
			}
		}
		throw std::runtime_error("unterminated json string");
	}

	JsonValue parseNumber()
	{
		skipWhitespace();
		const size_t beginPosition = position;
		if (position < text.size() && text[position] == '-')
		{
			position++;
		}
		if (position >= text.size())
		{
			throw std::runtime_error("invalid json number");
		}
		if (text[position] == '0')
		{
			position++;
		}
		else if (text[position] >= '1' && text[position] <= '9')
		{
			while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])))
			{
				position++;
			}
		}
		else
		{
			throw std::runtime_error("invalid json number");
		}
		if (position < text.size() && text[position] == '.')
		{
			position++;
			if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
			{
				throw std::runtime_error("invalid json number");
			}
			while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])))
			{
				position++;
			}
		}
		if (position < text.size() && (text[position] == 'e' || text[position] == 'E'))
		{
			position++;
			if (position < text.size() && (text[position] == '+' || text[position] == '-'))
			{
				position++;
			}
			if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
			{
				throw std::runtime_error("invalid json number");
			}
			while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])))
			{
				position++;
			}
		}

		const std::string numberText = text.substr(beginPosition, position - beginPosition);
		const char* begin = numberText.c_str();
		char* end = nullptr;
		double number = std::strtod(begin, &end);
		if (end == begin || *end != '\0' || !std::isfinite(number))
		{
			throw std::runtime_error("invalid json number");
		}

		JsonValue value;
		value.type = JsonValue::Type::Number;
		value.numberValue = number;
		return value;
	}

	bool consumeLiteral(const char* literal)
	{
		skipWhitespace();
		size_t length = std::strlen(literal);
		if (text.compare(position, length, literal) != 0)
		{
			return false;
		}
		position += length;
		return true;
	}
};

struct RegionLayer
{
	int name = -1;
	int width = 0;
	int height = 0;
	std::vector<int> data;
};

const JsonValue* findObjectValue(const JsonValue& object, const std::string& key)
{
	if (object.type != JsonValue::Type::Object)
	{
		return nullptr;
	}
	for (const auto& item : object.objectValues)
	{
		if (item.first == key)
		{
			return &item.second;
		}
	}
	return nullptr;
}

bool readInteger(const JsonValue* value, int& out)
{
	if (value == nullptr || value->type != JsonValue::Type::Number)
	{
		return false;
	}
	if (!std::isfinite(value->numberValue)
		|| std::trunc(value->numberValue) != value->numberValue
		|| value->numberValue < static_cast<double>((std::numeric_limits<int>::min)())
		|| value->numberValue > static_cast<double>((std::numeric_limits<int>::max)()))
	{
		return false;
	}
	out = static_cast<int>(value->numberValue);
	return true;
}

bool readIntegerString(const JsonValue* value, int& out)
{
	if (value == nullptr)
	{
		return false;
	}
	if (value->type == JsonValue::Type::Number)
	{
		return readInteger(value, out);
	}
	if (value->type != JsonValue::Type::String || value->stringValue.empty())
	{
		return false;
	}
	char* end = nullptr;
	long long number = std::strtoll(value->stringValue.c_str(), &end, 10);
	if (end == value->stringValue.c_str() || *end != '\0')
	{
		return false;
	}
	if (number < static_cast<long long>((std::numeric_limits<int>::min)())
		|| number > static_cast<long long>((std::numeric_limits<int>::max)()))
	{
		return false;
	}
	out = static_cast<int>(number);
	return true;
}

bool readLayer(const JsonValue& layerValue, RegionLayer& layer)
{
	if (!readIntegerString(findObjectValue(layerValue, "name"), layer.name)
		|| !readInteger(findObjectValue(layerValue, "width"), layer.width)
		|| !readInteger(findObjectValue(layerValue, "height"), layer.height))
	{
		return false;
	}

	const JsonValue* dataValue = findObjectValue(layerValue, "data");
	if (dataValue == nullptr || dataValue->type != JsonValue::Type::Array)
	{
		return false;
	}
	if (layer.width <= 0 || layer.height <= 0
		|| layer.width > MaximumLayerDimension || layer.height > MaximumLayerDimension)
	{
		return false;
	}
	const size_t expectedTileCount = static_cast<size_t>(layer.width) * static_cast<size_t>(layer.height);
	if (expectedTileCount > MaximumLayerTiles || dataValue->arrayValues.size() != expectedTileCount)
	{
		return false;
	}

	layer.data.clear();
	layer.data.reserve(expectedTileCount);
	for (const auto& item : dataValue->arrayValues)
	{
		int tile = 0;
		if (!readInteger(&item, tile))
		{
			return false;
		}
		layer.data.push_back(tile);
	}
	return true;
}

PointEx getTilePixelOffset(Point tile, Point center)
{
	PointEx point;
	int line = std::abs(center.y % 2);
	int line2 = std::abs(tile.y % 2);
	int x = tile.x - center.x;
	int y = tile.y - center.y;
	if (line == line2)
	{
		point.x = static_cast<float>(x * TILE_WIDTH);
		point.y = static_cast<float>(y * TILE_HEIGHT / 2);
	}
	else
	{
		point.y = static_cast<float>(y * TILE_HEIGHT / 2);
		if (line == 0)
		{
			point.x = static_cast<float>(x * TILE_WIDTH + TILE_WIDTH / 2);
		}
		else
		{
			point.x = static_cast<float>(x * TILE_WIDTH - TILE_WIDTH / 2);
		}
	}
	return point;
}

Point findBeginTile(const RegionLayer& layer)
{
	for (int y = 0; y < layer.height; y++)
	{
		for (int x = 0; x < layer.width; x++)
		{
			if (layer.data[x + y * layer.width] > 0)
			{
				return { x, y };
			}
		}
	}
	return { 0, 0 };
}

bool readDelayInfo(const JsonValue& root, std::map<int, std::vector<unsigned int>>& delayInfo)
{
	const JsonValue* tilesetsValue = findObjectValue(root, "tilesets");
	if (tilesetsValue == nullptr)
	{
		return true;
	}
	if (tilesetsValue->type != JsonValue::Type::Array || tilesetsValue->arrayValues.size() > MaximumTilesets)
	{
		return false;
	}

	for (const auto& tileset : tilesetsValue->arrayValues)
	{
		int firstGid = 0;
		if (!readInteger(findObjectValue(tileset, "firstgid"), firstGid) || firstGid < 0)
		{
			return false;
		}

		const JsonValue* tilesValue = findObjectValue(tileset, "tiles");
		if (tilesValue == nullptr)
		{
			continue;
		}
		if (tilesValue->type != JsonValue::Type::Object || tilesValue->objectValues.size() > MaximumTilesPerTileset)
		{
			return false;
		}

		for (const auto& tileEntry : tilesValue->objectValues)
		{
			JsonValue tileIdValue;
			tileIdValue.type = JsonValue::Type::String;
			tileIdValue.stringValue = tileEntry.first;
			int tileId = 0;
			if (!readIntegerString(&tileIdValue, tileId) || tileId < 0
				|| firstGid > (std::numeric_limits<int>::max)() - tileId)
			{
				return false;
			}

			const JsonValue* animationValue = findObjectValue(tileEntry.second, "animation");
			if (animationValue == nullptr)
			{
				continue;
			}
			if (animationValue->type != JsonValue::Type::Array
				|| animationValue->arrayValues.size() > MaximumAnimationFrames)
			{
				return false;
			}

			std::vector<unsigned int> durations;
			for (const auto& animation : animationValue->arrayValues)
			{
				int duration = 0;
				if (!readInteger(findObjectValue(animation, "duration"), duration) || duration < 0)
				{
					return false;
				}
				durations.push_back(static_cast<unsigned int>(duration));
			}
			if (!durations.empty())
			{
				if (!delayInfo.emplace(firstGid + tileId, std::move(durations)).second)
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool readLayers(const JsonValue& root, std::map<int, RegionLayer>& layers)
{
	const JsonValue* layersValue = findObjectValue(root, "layers");
	if (layersValue == nullptr || layersValue->type != JsonValue::Type::Array)
	{
		return false;
	}
	if (layersValue->arrayValues.empty() || layersValue->arrayValues.size() > MaximumLayers)
	{
		return false;
	}

	for (const auto& layerValue : layersValue->arrayValues)
	{
		RegionLayer layer;
		if (!readLayer(layerValue, layer))
		{
			return false;
		}
		if (!layers.emplace(layer.name, std::move(layer)).second)
		{
			return false;
		}
	}
	return true;
}
}

bool loadMagicRegionFile(const std::string& fileName, MagicRegionFile& regionFile)
{
	regionFile.clear();

	std::unique_ptr<char[]> buffer;
	int length = 0;
	if (!File::readFile(fileName, buffer, length, MagicRegionFileSafety::MaximumFileBytes)
		|| buffer == nullptr || length <= 0)
	{
		return false;
	}

	std::string content(buffer.get(), static_cast<size_t>(length));
	JsonValue root;
	JsonParser parser(content);
	if (!parser.parse(root) || root.type != JsonValue::Type::Object)
	{
		return false;
	}

	std::map<int, std::vector<unsigned int>> delayInfo;
	if (!readDelayInfo(root, delayInfo))
	{
		return false;
	}

	std::map<int, RegionLayer> layers;
	if (!readLayers(root, layers))
	{
		return false;
	}

	auto beginLayer = layers.find(8);
	if (beginLayer == layers.end())
	{
		return false;
	}

	Point beginTile = findBeginTile(beginLayer->second);
	MagicRegionFile parsedRegionFile(8);
	size_t regionItemCount = 0;
	for (const auto& layerEntry : layers)
	{
		int direction = layerEntry.first;
		if (direction < 0 || direction >= 8)
		{
			continue;
		}

		const RegionLayer& layer = layerEntry.second;
		for (int y = 0; y < layer.height; y++)
		{
			for (int x = 0; x < layer.width; x++)
			{
				int tileIndex = layer.data[x + y * layer.width];
				if (tileIndex <= 0)
				{
					continue;
				}

				PointEx offset = getTilePixelOffset({ x, y }, beginTile);
				auto delayIterator = delayInfo.find(tileIndex);
				if (delayIterator != delayInfo.end())
				{
					if (delayIterator->second.size() > MaximumRegionItems - regionItemCount)
					{
						return false;
					}
					for (unsigned int delay : delayIterator->second)
					{
						parsedRegionFile[direction].push_back({ offset, delay });
					}
					regionItemCount += delayIterator->second.size();
				}
				else
				{
					if (regionItemCount >= MaximumRegionItems)
					{
						return false;
					}
					parsedRegionFile[direction].push_back({ offset, 0 });
					regionItemCount++;
				}
			}
		}
	}
	regionFile = std::move(parsedRegionFile);
	return true;
}
