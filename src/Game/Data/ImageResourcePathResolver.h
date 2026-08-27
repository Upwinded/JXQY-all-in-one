#pragma once
#include "../GameTypes.h"
#include "../../File/ResourcePathSafety.h"
#include "../../Image/EncodedImageSafety.h"
#include "../../Image/ImagePackagePathCandidates.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "../../File/File.h"

inline std::string normalizeImageResourceCandidateName(std::string resourceName)
{
	if (!ResourcePathSafety::isSafeVirtualResourcePath(resourceName))
	{
		return "";
	}
	std::replace(resourceName.begin(), resourceName.end(), '\\', '/');
	while (!resourceName.empty() && resourceName.front() == '/')
	{
		resourceName.erase(resourceName.begin());
	}
	while (resourceName.rfind("./", 0) == 0)
	{
		resourceName.erase(0, 2);
	}
	return resourceName;
}

inline std::string toLowerImageResourceCandidateName(std::string resourceName)
{
	std::transform(resourceName.begin(), resourceName.end(), resourceName.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
	return resourceName;
}

inline void appendUniqueImageResourceCandidate(std::vector<std::string>& candidates, const std::string& candidate)
{
	if (candidate.empty())
	{
		return;
	}
	if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
	{
		candidates.push_back(candidate);
	}
}

inline bool imageResourceCandidateHasExtension(const std::string& candidate)
{
	std::string normalized = normalizeImageResourceCandidateName(candidate);
	size_t slashPosition = normalized.find_last_of('/');
	size_t dotPosition = normalized.find_last_of('.');
	return dotPosition != std::string::npos &&
		(slashPosition == std::string::npos || dotPosition > slashPosition);
}

inline std::string replaceImageResourceCandidateExtension(std::string candidate, const std::string& extension)
{
	size_t slashPosition = candidate.find_last_of("/\\");
	size_t dotPosition = candidate.find_last_of('.');
	if (dotPosition != std::string::npos &&
		(slashPosition == std::string::npos || dotPosition > slashPosition))
	{
		candidate.erase(dotPosition);
	}
	return candidate + extension;
}

inline bool imageResourceCandidateUsesKnownPackageExtension(const std::string& candidate)
{
	std::string lowerName = toLowerImageResourceCandidateName(normalizeImageResourceCandidateName(candidate));
	return lowerName.size() >= 4 &&
		(lowerName.rfind(".asf") == lowerName.size() - 4 ||
			lowerName.rfind(".mpc") == lowerName.size() - 4 ||
			lowerName.rfind(".shd") == lowerName.size() - 4);
}

inline bool imageResourceCandidateUsesAsfOrMpcExtension(const std::string& candidate)
{
	std::string lowerName = toLowerImageResourceCandidateName(normalizeImageResourceCandidateName(candidate));
	return lowerName.size() >= 4 &&
		(lowerName.rfind(".asf") == lowerName.size() - 4 ||
			lowerName.rfind(".mpc") == lowerName.size() - 4);
}

inline void appendImageResourceCandidateWithPackageFallbacks(
	std::vector<std::string>& candidates,
	const std::string& candidate)
{
	appendUniqueImageResourceCandidate(candidates, candidate);
	std::string lowerName = toLowerImageResourceCandidateName(normalizeImageResourceCandidateName(candidate));
	if (!imageResourceCandidateHasExtension(candidate))
	{
		if (lowerName.rfind("mpc/", 0) == 0)
		{
			appendUniqueImageResourceCandidate(candidates, candidate + ".mpc");
		}
		else
		{
			appendUniqueImageResourceCandidate(candidates, candidate + ".asf");
		}
		return;
	}
	if (!imageResourceCandidateUsesKnownPackageExtension(candidate))
	{
		if (lowerName.rfind("mpc/", 0) == 0)
		{
			appendUniqueImageResourceCandidate(candidates, replaceImageResourceCandidateExtension(candidate, ".mpc"));
		}
		else if (lowerName.rfind("asf/", 0) == 0)
		{
			appendUniqueImageResourceCandidate(candidates, replaceImageResourceCandidateExtension(candidate, ".asf"));
		}
	}
	else if (imageResourceCandidateUsesAsfOrMpcExtension(candidate))
	{
		if (lowerName.rfind("mpc/", 0) == 0 && lowerName.rfind(".mpc") != lowerName.size() - 4)
		{
			appendUniqueImageResourceCandidate(candidates, replaceImageResourceCandidateExtension(candidate, ".mpc"));
		}
		else if (lowerName.rfind("asf/", 0) == 0 && lowerName.rfind(".asf") != lowerName.size() - 4)
		{
			appendUniqueImageResourceCandidate(candidates, replaceImageResourceCandidateExtension(candidate, ".asf"));
		}
	}
}

inline std::string ensureTrailingImageResourceSlash(std::string folder)
{
	folder = normalizeImageResourceCandidateName(folder);
	if (!folder.empty() && folder.back() != '/')
	{
		folder += '/';
	}
	return folder;
}

inline void appendImageResourceAsfMpcPair(
	std::vector<std::string>& candidates,
	const std::string& asfFolder,
	const std::string& mpcFolder,
	const std::string& resourceName)
{
	appendImageResourceCandidateWithPackageFallbacks(candidates, asfFolder + resourceName);
	appendImageResourceCandidateWithPackageFallbacks(candidates, mpcFolder + resourceName);
}

inline void appendExplicitImageResourceAndAlternate(std::vector<std::string>& candidates, const std::string& resourceName)
{
	appendImageResourceCandidateWithPackageFallbacks(candidates, resourceName);
	std::string lowerName = toLowerImageResourceCandidateName(resourceName);
	if (lowerName.rfind("asf/map/", 0) == 0 ||
		lowerName.rfind("mpc/map/", 0) == 0 ||
		lowerName == "asf/map" ||
		lowerName == "mpc/map")
	{
		return;
	}
	if (lowerName.rfind("asf/", 0) == 0)
	{
		appendImageResourceCandidateWithPackageFallbacks(candidates, "mpc/" + resourceName.substr(4));
	}
	else if (lowerName.rfind("mpc/", 0) == 0)
	{
		appendImageResourceCandidateWithPackageFallbacks(candidates, "asf/" + resourceName.substr(4));
	}
}

inline std::vector<std::string> buildImageResourceCandidatesForCategory(
	const std::string& resourceName,
	const std::string& categoryName,
	const std::string& asfFolder,
	const std::string& mpcFolder,
	const std::vector<std::string>& fallbackFolders = {})
{
	std::string normalizedName = normalizeImageResourceCandidateName(resourceName);
	if (normalizedName.empty())
	{
		return {};
	}

	std::string normalizedCategory = normalizeImageResourceCandidateName(categoryName);
	std::string lowerName = toLowerImageResourceCandidateName(normalizedName);
	std::string lowerCategory = toLowerImageResourceCandidateName(normalizedCategory);
	std::string normalizedAsfFolder = ensureTrailingImageResourceSlash(asfFolder);
	std::string normalizedMpcFolder = ensureTrailingImageResourceSlash(mpcFolder);

	std::vector<std::string> candidates;
	const std::string asfCategoryPrefix = "asf/" + lowerCategory + "/";
	const std::string mpcCategoryPrefix = "mpc/" + lowerCategory + "/";
	if (!lowerCategory.empty() && lowerName.rfind(asfCategoryPrefix, 0) == 0)
	{
		std::string suffix = normalizedName.substr(asfCategoryPrefix.size());
		appendImageResourceAsfMpcPair(candidates, normalizedAsfFolder, normalizedMpcFolder, suffix);
		return candidates;
	}
	if (!lowerCategory.empty() && lowerName.rfind(mpcCategoryPrefix, 0) == 0)
	{
		std::string suffix = normalizedName.substr(mpcCategoryPrefix.size());
		appendImageResourceCandidateWithPackageFallbacks(candidates, normalizedMpcFolder + suffix);
		appendImageResourceCandidateWithPackageFallbacks(candidates, normalizedAsfFolder + suffix);
		return candidates;
	}

	const std::string categoryPrefix = lowerCategory.empty() ? "" : lowerCategory + "/";
	if (!categoryPrefix.empty() && lowerName.rfind(categoryPrefix, 0) == 0)
	{
		appendImageResourceAsfMpcPair(candidates, normalizedAsfFolder, normalizedMpcFolder, normalizedName.substr(categoryPrefix.size()));
		return candidates;
	}

	if (lowerName.rfind("asf/", 0) == 0 || lowerName.rfind("mpc/", 0) == 0)
	{
		appendExplicitImageResourceAndAlternate(candidates, normalizedName);
		return candidates;
	}

	appendImageResourceAsfMpcPair(candidates, normalizedAsfFolder, normalizedMpcFolder, normalizedName);
	for (const auto& fallbackFolder : fallbackFolders)
	{
		appendImageResourceCandidateWithPackageFallbacks(candidates, ensureTrailingImageResourceSlash(fallbackFolder) + normalizedName);
	}
	return candidates;
}

inline _shared_imp loadFirstImageResourceCandidate(const std::vector<std::string>& candidates)
{
	std::vector<std::string> expandedCandidates;
	for (const auto& candidate : candidates)
	{
		for (const auto& expandedCandidate : ImagePackagePathCandidates::build(candidate))
		{
			appendUniqueImageResourceCandidate(expandedCandidates, expandedCandidate);
		}
	}
	_shared_imp result;
	File::visitReadableResources(expandedCandidates,
		static_cast<int>(EncodedImageSafety::MaxEncodedImageBytes),
		[&](const std::string&, std::unique_ptr<char[]>& data, int size)
		{
			result = IMP::createIMPImageFromMem(data, size);
			return result != nullptr;
		});
	return result;
}
