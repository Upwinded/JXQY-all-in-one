#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

inline std::filesystem::path makeUniqueTestDirectory(const std::string& prefix)
{
	static std::atomic<std::uint64_t> counter{0};
	const auto timestamp = std::chrono::high_resolution_clock::now()
		.time_since_epoch().count();
	const auto threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
	const auto sequence = counter.fetch_add(1, std::memory_order_relaxed);
#if defined(_WIN32)
	const auto processId = _getpid();
#else
	const auto processId = getpid();
#endif

	std::ostringstream name;
	name << prefix << '-' << processId << '-' << std::hex << timestamp << '-'
		 << threadId << '-' << sequence;
	return std::filesystem::temp_directory_path() / name.str();
}
