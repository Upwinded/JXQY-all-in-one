#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace ResourceReadPrefixPolicy
{
enum class BundledRootMode
{
	FilesystemPath,
	AndroidAssetNamespace
};

inline bool appendUniquePrefix(
	std::vector<std::string>& prefixes,
	const std::string& prefix,
	bool allowEmpty)
{
	if ((!allowEmpty && prefix.empty()) ||
		std::find(prefixes.begin(), prefixes.end(), prefix) !=
			prefixes.end())
	{
		return false;
	}
	prefixes.push_back(prefix);
	return true;
}

inline bool appendPrimaryPrefix(
	std::vector<std::string>& prefixes,
	const std::string& prefix,
	BundledRootMode rootMode)
{
	return appendUniquePrefix(
		prefixes,
		prefix,
		rootMode == BundledRootMode::AndroidAssetNamespace);
}
}
