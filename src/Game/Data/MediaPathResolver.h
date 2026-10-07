#pragma once

#include <string>
#include <vector>

#if defined(JXQY_ENABLE_TEST_HOOKS)
#include <atomic>
inline std::atomic_size_t mediaAssetResolutionCountForTests{0};
#endif

std::vector<std::string> buildMediaAssetCandidates(
	const std::string& folder,
	const std::string& fileName,
	const std::vector<std::string>& fallbackExtensions);

std::string resolveMediaAssetPath(
	const std::string& folder,
	const std::string& fileName,
	const std::vector<std::string>& fallbackExtensions);

std::string resolveSoundAssetPath(const std::string& fileName);
std::string resolveVideoAssetPath(const std::string& fileName);
// Normalizes the logical sound path without accessing the filesystem.
std::string buildSoundAssetPath(const std::string& fileName);
