#include "ArtifactChecksum.h"

extern "C"
{
#include "miniz.h"
}

#include <fstream>
#include <limits>
#include <vector>

namespace OnlineUpdate
{
bool calculateCrc32(const std::uint8_t* data, std::size_t size,
	std::uint32_t& checksum) noexcept
{
	checksum = 0;
	if (data == nullptr && size != 0)
	{
		return false;
	}
	checksum = static_cast<std::uint32_t>(mz_crc32(
		MZ_CRC32_INIT,
		data,
		size));
	return true;
}

bool calculateFileCrc32(const std::filesystem::path& path,
	std::uint32_t& checksum, std::uint64_t& fileSize) noexcept
{
	checksum = 0;
	fileSize = 0;
	try
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			return false;
		}
		mz_ulong crc = MZ_CRC32_INIT;
		std::vector<std::uint8_t> buffer(256 * 1024);
		while (input)
		{
			input.read(
				reinterpret_cast<char*>(buffer.data()),
				static_cast<std::streamsize>(buffer.size()));
			const std::streamsize read = input.gcount();
			if (read > 0)
			{
				const std::uint64_t count =
					static_cast<std::uint64_t>(read);
				if (fileSize >
					std::numeric_limits<std::uint64_t>::max() - count)
				{
					return false;
				}
				fileSize += count;
				crc = mz_crc32(
					crc, buffer.data(), static_cast<std::size_t>(read));
			}
		}
		if (!input.eof())
		{
			return false;
		}
		checksum = static_cast<std::uint32_t>(crc);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

std::string crc32ToLowerHex(std::uint32_t checksum)
{
	static constexpr char Hex[] = "0123456789abcdef";
	std::string result(8, '0');
	for (int index = 7; index >= 0; --index)
	{
		result[static_cast<std::size_t>(index)] = Hex[checksum & 0x0F];
		checksum >>= 4;
	}
	return result;
}
}
