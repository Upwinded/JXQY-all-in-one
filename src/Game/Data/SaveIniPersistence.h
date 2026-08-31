#pragma once

#include "../../File/File.h"
#include "../../File/INIReader.h"

#include <cstring>
#include <memory>
#include <string>

namespace SaveIniPersistence
{
inline constexpr int MaximumFileBytes = 16 * 1024 * 1024;

enum class ReadStatus
{
	Missing,
	Empty,
	Loaded,
	Unreadable,
	Malformed
};

inline ReadStatus read(
	const std::string& fileName,
	std::shared_ptr<INIReader>& reader,
	int maximumBytes = MaximumFileBytes)
{
	reader = nullptr;
	if (!File::fileExist(fileName))
	{
		return ReadStatus::Missing;
	}

	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readFile(fileName, data, length, maximumBytes))
	{
		return ReadStatus::Unreadable;
	}
	if (length == 0)
	{
		return ReadStatus::Empty;
	}
	if (length < 0 || data == nullptr ||
		std::memchr(data.get(), '\0', static_cast<std::size_t>(length)) != nullptr)
	{
		return ReadStatus::Malformed;
	}

	auto parsed = std::make_shared<INIReader>(data);
	if (parsed->ParseError() != 0)
	{
		return ReadStatus::Malformed;
	}
	reader = std::move(parsed);
	return ReadStatus::Loaded;
}
}
