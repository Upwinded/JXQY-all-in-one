#include "../Update/HttpsDownload.h"
#include "TestTemporaryDirectory.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int failureCount = 0;

void expect(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		failureCount++;
	}
}

void testArtifactUrl()
{
	std::string url;
	expect(OnlineUpdate::buildHttpsArtifactUrl(
		"https://updates.example.test/releases/catalog.ini",
		"resources/yycs.zip",
		url) &&
		url == "https://updates.example.test/releases/resources/yycs.zip",
		"artifact path resolves beside the catalog");
	expect(OnlineUpdate::buildHttpsArtifactUrl(
		"HTTPS://updates.example.test/catalog.ini",
		u8"resources/月影.zip",
		url) &&
		url == "HTTPS://updates.example.test/resources/"
			"%E6%9C%88%E5%BD%B1.zip",
		"UTF-8 artifact path is percent encoded");
	expect(!OnlineUpdate::buildHttpsArtifactUrl(
		"http://updates.example.test/catalog.ini",
		"resources/yycs.zip",
		url),
		"plain HTTP catalog URL is rejected");
	expect(!OnlineUpdate::buildHttpsArtifactUrl(
		"https://updates.example.test/catalog.ini?channel=test",
		"resources/yycs.zip",
		url),
		"catalog URL query is rejected");
	expect(!OnlineUpdate::buildHttpsArtifactUrl(
		"https://updates.example.test/catalog.ini",
		"../yycs.zip",
		url),
		"unsafe artifact path is rejected");
	std::string invalidUtf8Url = "https://updates.example.test/";
	invalidUtf8Url.push_back(static_cast<char>(0xFF));
	expect(!OnlineUpdate::buildHttpsArtifactUrl(
		invalidUtf8Url,
		"resources/yycs.zip",
		url),
		"invalid UTF-8 catalog URL is rejected");
}

void testLocalPreconditions()
{
	const auto invalidMemory = OnlineUpdate::downloadHttpsToMemory(
		"http://updates.example.test/catalog.ini", 1024);
	expect(invalidMemory.status ==
		OnlineUpdate::HttpsDownloadStatus::InvalidUrl,
		"every platform rejects HTTP before transport selection");
	const auto invalidLimit = OnlineUpdate::downloadHttpsToMemory(
		"https://updates.example.test/catalog.ini", 0);
	expect(invalidLimit.status ==
		OnlineUpdate::HttpsDownloadStatus::InvalidInput,
		"every platform rejects a zero memory limit");

	const std::filesystem::path root =
		makeUniqueTestDirectory("jxqy-https-download");
	std::error_code error;
	expect(std::filesystem::create_directory(root, error) && !error,
		"download test directory is created");
	const std::filesystem::path existing = root / "existing.tmp";
	{
		std::ofstream output(existing, std::ios::binary);
		output << "keep";
	}
	const auto existingResult = OnlineUpdate::downloadHttpsToNewFile(
		"https://updates.example.test/resource.zip",
		existing,
		1024);
#if defined(_WIN32) || defined(__LINUX__) || defined(__ANDROID__) || \
	defined(__APPLE__)
	expect(existingResult.status ==
		OnlineUpdate::HttpsDownloadStatus::DestinationAlreadyExists,
		"supported download transport never overwrites an existing destination");
#else
	expect(existingResult.status ==
		OnlineUpdate::HttpsDownloadStatus::UnsupportedPlatform,
		"unfinished file transport reports unsupported");
#endif
	expect(std::filesystem::file_size(existing, error) == 4 && !error,
		"existing destination bytes are preserved");
	const auto invalidExpectedSize =
		OnlineUpdate::downloadHttpsToNewFile(
			"https://updates.example.test/resource.zip",
			root / "invalid-size.tmp",
			1024,
			2048);
	expect(invalidExpectedSize.status ==
		OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded,
		"every platform rejects an expected size above the limit");
	std::filesystem::remove_all(root, error);
}

void testOptionalLiveHttps()
{
#if defined(_WIN32) || defined(__LINUX__) || defined(__ANDROID__) || \
	defined(__APPLE__)
	const char* url = std::getenv("JXQY_TEST_HTTPS_URL");
	if (url == nullptr || *url == '\0')
	{
		return;
	}
	const auto memory = OnlineUpdate::downloadHttpsToMemory(url, 1024 * 1024);
	expect(memory.succeeded() && !memory.bytes.empty(),
		"optional live HTTPS memory transfer succeeds");

	const std::filesystem::path root =
		makeUniqueTestDirectory("jxqy-live-https-download");
	std::error_code error;
	expect(std::filesystem::create_directory(root, error) && !error,
		"live download test directory is created");
	const std::filesystem::path output = root / "response.tmp";
	const auto file = OnlineUpdate::downloadHttpsToNewFile(
		url, output, 1024 * 1024);
	expect(file.succeeded() && file.transferredBytes != 0 &&
		std::filesystem::file_size(output, error) == file.transferredBytes &&
		!error,
		"optional live HTTPS file transfer succeeds");

	const auto limitedMemory = OnlineUpdate::downloadHttpsToMemory(url, 1);
	expect(limitedMemory.status ==
		OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded &&
		limitedMemory.bytes.empty(),
		"live memory transfer clears bytes after exceeding its limit");
	bool memoryProgressInvoked = false;
	const auto cancelledMemory = OnlineUpdate::downloadHttpsToMemory(
		url,
		1024 * 1024,
		[&memoryProgressInvoked](std::uint64_t, std::uint64_t)
		{
			memoryProgressInvoked = true;
			return false;
		});
	expect(cancelledMemory.status ==
		OnlineUpdate::HttpsDownloadStatus::Cancelled &&
		memoryProgressInvoked && cancelledMemory.bytes.empty(),
		"cancelled live memory transfer clears its partial bytes");

	bool progressInvoked = false;
	const std::filesystem::path cancelledOutput = root / "cancelled.tmp";
	const auto cancelled = OnlineUpdate::downloadHttpsToNewFile(
		url,
		cancelledOutput,
		1024 * 1024,
		0,
		[&progressInvoked](std::uint64_t, std::uint64_t)
		{
			progressInvoked = true;
			return false;
		});
	expect(cancelled.status ==
		OnlineUpdate::HttpsDownloadStatus::Cancelled &&
		progressInvoked &&
		!std::filesystem::exists(cancelledOutput),
		"cancelled live file transfer removes its partial file");
	std::filesystem::remove_all(root, error);
#endif
}
}

int main()
{
	testArtifactUrl();
	testLocalPreconditions();
	testOptionalLiveHttps();
	if (failureCount != 0)
	{
		std::cerr << failureCount << " HTTPS download test(s) failed"
			<< std::endl;
		return 1;
	}
	std::cout << "All HTTPS download tests passed" << std::endl;
	return 0;
}
