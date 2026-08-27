#pragma once

#include <string>
#include <vector>

std::vector<std::string> buildMediaAssetCandidates(
	const std::string& folder,
	const std::string& fileName,
	const std::vector<std::string>& fallbackExtensions);

std::string resolveMediaAssetPath(
	const std::string& folder,
	const std::string& fileName,
	const std::vector<std::string>& fallbackExtensions);

std::string resolveSoundAssetPath(const std::string& fileName);
