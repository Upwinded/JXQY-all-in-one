#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class SaveGenerationError
{
	None,
	GameIniInvalid,
	Cancelled,
	PublicationFailed
};

struct SaveGenerationLimits
{
	std::size_t maximumFileCount = 0;
	std::uint64_t maximumTotalBytes = 0;
	int maximumSingleFileBytes = 0;
};

struct SaveGenerationPolicy
{
	SaveGenerationLimits limits;
	std::function<bool()> cancellationRequested;
};

struct SaveGenerationResult
{
	SaveGenerationError error = SaveGenerationError::None;
	std::string sourceDirectory;
	std::string destinationDirectory;
	std::string errorPath;

	bool succeeded() const
	{
		return error == SaveGenerationError::None;
	}
};

class SaveGeneration
{
public:
	// Normalizes the current runtime save directory and explicit save slots.
	static bool NormalizeGenerationDirectory(
		const std::string& directoryName, std::string& normalizedDirectory);
	static const char* DescribeError(SaveGenerationError error);
};
