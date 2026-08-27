#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace OnlineUpdate
{
enum class HttpsDownloadStatus
{
	Success,
	UnsupportedPlatform,
	InvalidUrl,
	InvalidInput,
	DestinationAlreadyExists,
	DestinationUnavailable,
	NetworkError,
	HttpError,
	SizeLimitExceeded,
	SizeMismatch,
	WriteFailed,
	Cancelled,
	CleanupFailed
};

struct HttpsDownloadResult
{
	HttpsDownloadStatus status = HttpsDownloadStatus::InvalidInput;
	std::uint64_t transferredBytes = 0;
	// Optional backend diagnostic only; callers branch on status/httpStatus.
	std::uint64_t nativeErrorCode = 0;
	std::uint32_t httpStatus = 0;

	bool succeeded() const noexcept
	{
		return status == HttpsDownloadStatus::Success;
	}
};

struct HttpsBufferDownloadResult : HttpsDownloadResult
{
	std::vector<char> bytes;
};

using HttpsDownloadProgress =
	std::function<bool(std::uint64_t transferredBytes,
		std::uint64_t expectedBytes)>;

// Builds an artifact URL relative to the catalog file. The catalog URL must be
// a plain HTTPS URL without a query or fragment. Artifact paths use the strict
// relative paths accepted by OnlineUpdateCatalog.
bool buildHttpsArtifactUrl(
	const std::string& catalogUrl,
	const std::string& artifactPath,
	std::string& artifactUrl);

// Synchronous platform transport. Callers must run these functions on a worker
// thread so network timeouts never block the game loop. Certificate validation
// remains enabled and HTTP URLs are rejected.
HttpsBufferDownloadResult downloadHttpsToMemory(
	const std::string& url,
	std::size_t maximumBytes,
	const HttpsDownloadProgress& progress = {});

// Creates a new file and never overwrites an existing path. A failed or
// cancelled transfer removes only the partial file created by this call.
HttpsDownloadResult downloadHttpsToNewFile(
	const std::string& url,
	const std::filesystem::path& destinationPath,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes = 0,
	const HttpsDownloadProgress& progress = {});
}
