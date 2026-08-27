#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace AppleHttpsDownload
{
enum class TransferStatus
{
	Success,
	NetworkError,
	HttpError,
	SizeLimitExceeded,
	SizeMismatch,
	Cancelled,
	WriteFailed
};

struct TransferResult
{
	TransferStatus status = TransferStatus::NetworkError;
	std::uint32_t httpStatus = 0;
	std::uint64_t transferredBytes = 0;
};

using ProgressCallback =
	std::function<bool(std::uint64_t transferredBytes,
		std::uint64_t expectedBytes)>;
using WriteCallback =
	std::function<bool(const char* bytes, std::size_t size)>;

// Synchronously waits on the calling worker thread while NSURLSession performs
// the transfer on its serial delegate queue. Redirects remain HTTPS-only and
// certificate/host validation uses the operating system defaults.
TransferResult download(
	const std::string& url,
	std::uint64_t maximumBytes,
	std::uint64_t expectedBytes,
	const ProgressCallback& progress,
	const WriteCallback& write);
}
