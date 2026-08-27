#include "BuySellInventory.h"
#include "../GameTypes.h"
#include "../../File/INIReader.h"
#include "../../libconvert/libconvert.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <sstream>

namespace
{
constexpr char Base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int readHeaderInteger(INIReader& ini, const std::string& name, int defaultValue)
{
	int value = defaultValue;
	const std::string headerValue = ini.Get("Header", name, "");
	if (!headerValue.empty())
	{
		return convert::parseInteger(headerValue, value) ? value : defaultValue;
	}
	const std::string legacyValue = ini.Get("Head", name, "");
	return convert::parseInteger(legacyValue, value) ? value : defaultValue;
}

int readInventoryCount(INIReader& ini)
{
	int count = readHeaderInteger(ini, "Count", -1);
	if (count < 0)
	{
		count = 0;
	}
	if (count > BUYSELL_GOODS_COUNT)
	{
		count = BUYSELL_GOODS_COUNT;
	}
	return count;
}

std::unique_ptr<char[]> makeNullTerminatedBuffer(const std::string& text)
{
	auto buffer = std::make_unique<char[]>(text.size() + 1);
	if (!text.empty())
	{
		std::memcpy(buffer.get(), text.data(), text.size());
	}
	buffer[text.size()] = '\0';
	return buffer;
}

std::string normalizeLegacySlashComments(std::string text)
{
	size_t lineStart = 0;
	while (lineStart < text.size())
	{
		const size_t lineEnd = text.find('\n', lineStart);
		const size_t lineLimit = lineEnd == std::string::npos
			? text.size()
			: lineEnd;
		size_t contentStart = lineStart;
		while (contentStart < lineLimit
			&& (text[contentStart] == ' ' || text[contentStart] == '\t'
				|| text[contentStart] == '\r'))
		{
			contentStart++;
		}
		if (contentStart + 1 < lineLimit
			&& text[contentStart] == '/' && text[contentStart + 1] == '/')
		{
			// Formal legacy shop files use C++-style full-line comments.
			// Convert only that prefix so paths and values containing // stay intact.
			text[contentStart] = ';';
			text[contentStart + 1] = ' ';
		}
		if (lineEnd == std::string::npos)
		{
			break;
		}
		lineStart = lineEnd + 1;
	}
	return text;
}

int base64Value(unsigned char character)
{
	if (character >= 'A' && character <= 'Z')
	{
		return character - 'A';
	}
	if (character >= 'a' && character <= 'z')
	{
		return character - 'a' + 26;
	}
	if (character >= '0' && character <= '9')
	{
		return character - '0' + 52;
	}
	if (character == '+')
	{
		return 62;
	}
	if (character == '/')
	{
		return 63;
	}
	return -1;
}
}

namespace BuySellInventory
{
std::string encodeString(const std::string& decoded)
{
	std::string encoded;
	encoded.reserve(((decoded.size() + 2) / 3) * 4);
	for (size_t index = 0; index < decoded.size(); index += 3)
	{
		unsigned int value = static_cast<unsigned char>(decoded[index]) << 16;
		bool hasSecondByte = index + 1 < decoded.size();
		bool hasThirdByte = index + 2 < decoded.size();
		if (hasSecondByte)
		{
			value |= static_cast<unsigned char>(decoded[index + 1]) << 8;
		}
		if (hasThirdByte)
		{
			value |= static_cast<unsigned char>(decoded[index + 2]);
		}

		encoded.push_back(Base64Alphabet[(value >> 18) & 0x3F]);
		encoded.push_back(Base64Alphabet[(value >> 12) & 0x3F]);
		encoded.push_back(hasSecondByte ? Base64Alphabet[(value >> 6) & 0x3F] : '=');
		encoded.push_back(hasThirdByte ? Base64Alphabet[value & 0x3F] : '=');
	}
	return encoded;
}

bool decodeString(const std::string& encoded, std::string& decoded)
{
	decoded.clear();
	std::array<int, 4> values = { 0, 0, 0, 0 };
	int valueCount = 0;
	int paddingCount = 0;
	bool paddingStarted = false;
	bool finalPaddingSeen = false;

	for (unsigned char character : encoded)
	{
		if (std::isspace(character) != 0)
		{
			continue;
		}
		if (finalPaddingSeen)
		{
			decoded.clear();
			return false;
		}

		if (character == '=')
		{
			paddingStarted = true;
			values[valueCount++] = 0;
			paddingCount++;
		}
		else
		{
			if (paddingStarted)
			{
				decoded.clear();
				return false;
			}
			int value = base64Value(character);
			if (value < 0)
			{
				decoded.clear();
				return false;
			}
			values[valueCount++] = value;
		}

		if (valueCount == 4)
		{
			if (paddingCount > 2)
			{
				decoded.clear();
				return false;
			}
			unsigned int combined =
				(static_cast<unsigned int>(values[0]) << 18) |
				(static_cast<unsigned int>(values[1]) << 12) |
				(static_cast<unsigned int>(values[2]) << 6) |
				static_cast<unsigned int>(values[3]);
			decoded.push_back(static_cast<char>((combined >> 16) & 0xFF));
			if (paddingCount < 2)
			{
				decoded.push_back(static_cast<char>((combined >> 8) & 0xFF));
			}
			if (paddingCount < 1)
			{
				decoded.push_back(static_cast<char>(combined & 0xFF));
			}
			valueCount = 0;
			if (paddingCount > 0)
			{
				finalPaddingSeen = true;
			}
			paddingCount = 0;
			paddingStarted = false;
		}
	}

	if (valueCount != 0)
	{
		decoded.clear();
		return false;
	}
	return true;
}

bool parseText(const std::string& text, BuySellInventoryData& data)
{
	data = BuySellInventoryData();
	if (text.empty())
	{
		return false;
	}

	const std::string normalizedText = normalizeLegacySlashComments(text);
	auto buffer = makeNullTerminatedBuffer(normalizedText);
	INIReader ini(buffer);
	if (ini.ParseError() != 0)
	{
		return false;
	}

	data.count = readInventoryCount(ini);
	data.numberValid = readHeaderInteger(ini, "NumberValid", 0) != 0;
	data.buyPercent = readHeaderInteger(ini, "BuyPercent", 100);
	data.recyclePercent = readHeaderInteger(ini, "RecyclePercent", 100);
	data.items.resize(data.count);
	for (int i = 0; i < data.count; i++)
	{
		std::string section = std::to_string(i + 1);
		data.items[i].iniFile = ini.Get(section, "IniFile", "");
		const std::string numberText = ini.Get(section, "Number", "");
		data.items[i].number = 0;
		convert::parseInteger(numberText, data.items[i].number);
		if (data.items[i].iniFile.empty())
		{
			data.items[i].number = 0;
		}
	}
	return true;
}

std::string serializeText(const BuySellInventoryData& data)
{
	int count = std::max(0, std::min(data.count, BUYSELL_GOODS_COUNT));
	std::ostringstream output;
	output << "[Header]\r\n";
	output << "Count=" << count << "\r\n";
	output << "NumberValid=" << (data.numberValid ? 1 : 0) << "\r\n";
	if (data.buyPercent != 100)
	{
		output << "BuyPercent=" << data.buyPercent << "\r\n";
	}
	if (data.recyclePercent != 100)
	{
		output << "RecyclePercent=" << data.recyclePercent << "\r\n";
	}
	output << "\r\n";
	for (int i = 0; i < count; i++)
	{
		output << "[" << (i + 1) << "]\r\n";
		if (i < static_cast<int>(data.items.size()))
		{
			output << "IniFile=" << data.items[i].iniFile << "\r\n";
			output << "Number=" << data.items[i].number << "\r\n";
		}
		else
		{
			output << "IniFile=\r\n";
			output << "Number=0\r\n";
		}
		output << "\r\n";
	}
	return output.str();
}
}
