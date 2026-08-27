#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "../File/ResourcePathSafety.h"

namespace ImagePackagePathCandidates
{
inline std::string normalize(std::string path)
{
	if (!ResourcePathSafety::isSafeVirtualResourcePath(path))
	{
		return "";
	}
	std::replace(path.begin(), path.end(), '\\', '/');
	while (!path.empty() && path.front() == '/')
	{
		path.erase(path.begin());
	}
	while (path.rfind("./", 0) == 0)
	{
		path.erase(0, 2);
	}
	return path;
}

inline std::string toLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
	return value;
}

inline bool hasSuffix(const std::string& value, const std::string& suffix)
{
	return value.size() >= suffix.size()
		&& value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool hasExtension(const std::string& path)
{
	const size_t slashPosition = path.find_last_of('/');
	const size_t dotPosition = path.find_last_of('.');
	return dotPosition != std::string::npos
		&& (slashPosition == std::string::npos || dotPosition > slashPosition);
}

inline std::string replaceExtension(std::string path, const std::string& extension)
{
	const size_t slashPosition = path.find_last_of('/');
	const size_t dotPosition = path.find_last_of('.');
	if (dotPosition != std::string::npos
		&& (slashPosition == std::string::npos || dotPosition > slashPosition))
	{
		path.erase(dotPosition);
	}
	return path + extension;
}

inline void appendUnique(std::vector<std::string>& candidates, const std::string& candidate)
{
	if (!candidate.empty()
		&& std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
	{
		candidates.push_back(candidate);
	}
}

inline bool isMapPackagePath(const std::string& lowerPath)
{
	return lowerPath == "asf/map"
		|| lowerPath == "mpc/map"
		|| lowerPath.rfind("asf/map/", 0) == 0
		|| lowerPath.rfind("mpc/map/", 0) == 0;
}

inline void appendFolderPackageFallbacks(
	std::vector<std::string>& candidates,
	const std::string& candidate)
{
	appendUnique(candidates, candidate);
	const std::string lowerPath = toLowerAscii(candidate);
	const bool usesAsfFolder = lowerPath.rfind("asf/", 0) == 0;
	const bool usesMpcFolder = lowerPath.rfind("mpc/", 0) == 0;
	if (!usesAsfFolder && !usesMpcFolder)
	{
		return;
	}

	const std::string expectedExtension = usesMpcFolder ? ".mpc" : ".asf";
	if (!hasExtension(candidate))
	{
		appendUnique(candidates, candidate + expectedExtension);
		return;
	}

	const bool usesAsfExtension = hasSuffix(lowerPath, ".asf");
	const bool usesMpcExtension = hasSuffix(lowerPath, ".mpc");
	const bool usesShdExtension = hasSuffix(lowerPath, ".shd");
	if (usesShdExtension)
	{
		return;
	}
	if ((usesAsfFolder && !usesAsfExtension)
		|| (usesMpcFolder && !usesMpcExtension))
	{
		appendUnique(candidates, replaceExtension(candidate, expectedExtension));
	}
}

inline std::vector<std::string> build(const std::string& fileName)
{
	const std::string normalizedPath = normalize(fileName);
	if (normalizedPath.empty())
	{
		return {};
	}

	const std::string lowerPath = toLowerAscii(normalizedPath);
	std::vector<std::string> candidates;
	if (isMapPackagePath(lowerPath))
	{
		appendUnique(candidates, normalizedPath);
		return candidates;
	}

	appendFolderPackageFallbacks(candidates, normalizedPath);
	if (lowerPath.rfind("asf/", 0) == 0)
	{
		appendFolderPackageFallbacks(candidates, "mpc/" + normalizedPath.substr(4));
	}
	else if (lowerPath.rfind("mpc/", 0) == 0)
	{
		appendFolderPackageFallbacks(candidates, "asf/" + normalizedPath.substr(4));
	}
	return candidates;
}
}
