#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace OnlineUpdate
{
bool calculateCrc32(
	const std::uint8_t* data,
	std::size_t size,
	std::uint32_t& checksum) noexcept;

bool calculateFileCrc32(
	const std::filesystem::path& path,
	std::uint32_t& checksum,
	std::uint64_t& fileSize) noexcept;

std::string crc32ToLowerHex(std::uint32_t checksum);
}
