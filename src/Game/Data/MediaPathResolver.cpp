#include "MediaPathResolver.h"
#include "../GameTypes.h"
#include "../../File/File.h"
#include "../../libconvert/libconvert.h"

#include <algorithm>
#include <cctype>

namespace
{
void appendUnique(std::vector<std::string>& values, const std::string& value)
{
	if (value.empty())
	{
		return;
	}
	if (std::find(values.begin(), values.end(), value) == values.end())
	{
		values.push_back(value);
	}
}

std::string toLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::string toUpperAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::toupper(c));
	});
	return value;
}

std::vector<std::string> mediaBaseNameVariants(const std::string& baseName)
{
	std::vector<std::string> variants;
	appendUnique(variants, baseName);
	appendUnique(variants, toLowerAscii(baseName));
	appendUnique(variants, toUpperAscii(baseName));
	if (!baseName.empty())
	{
		std::string capitalized = toLowerAscii(baseName);
		capitalized[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(capitalized[0])));
		appendUnique(variants, capitalized);
	}
	return variants;
}

std::string normalizeSeparators(std::string value)
{
	convert::replaceAllString(value, "\\", "/");
	while (!value.empty() && value.front() == '/')
	{
		value.erase(value.begin());
	}
	while (value.rfind("./", 0) == 0)
	{
		value.erase(0, 2);
	}
	return value;
}

std::string toRuntimeSeparators(std::string value)
{
	convert::replaceAllString(value, "/", "\\");
	return value;
}

std::string normalizeFolderPrefix(const std::string& folder)
{
	std::string prefix = normalizeSeparators(folder);
	if (!prefix.empty() && prefix.back() != '/')
	{
		prefix += "/";
	}
	return prefix;
}

bool hasMediaFolderPrefix(const std::string& fileName, const std::string& folder)
{
	std::string normalizedFileName = toLowerAscii(normalizeSeparators(fileName));
	std::string normalizedFolder = toLowerAscii(normalizeFolderPrefix(folder));
	return !normalizedFolder.empty() && normalizedFileName.rfind(normalizedFolder, 0) == 0;
}

std::string buildDirectMediaPath(const std::string& folder, const std::string& fileName)
{
	std::string normalizedFileName = normalizeSeparators(fileName);
	if (hasMediaFolderPrefix(normalizedFileName, folder))
	{
		return toRuntimeSeparators(normalizedFileName);
	}
	return toRuntimeSeparators(normalizeFolderPrefix(folder) + normalizedFileName);
}

bool hasSupportedMediaExtension(const std::string& path, const std::vector<std::string>& fallbackExtensions)
{
	std::string extension = toLowerAscii(convert::extractFileExt(normalizeSeparators(path)));
	if (extension.empty())
	{
		return true;
	}
	for (const auto& fallbackExtension : fallbackExtensions)
	{
		if (extension == toLowerAscii(fallbackExtension))
		{
			return true;
		}
	}
	return false;
}
}

std::vector<std::string> buildMediaAssetCandidates(
	const std::string& folder,
	const std::string& fileName,
	const std::vector<std::string>& fallbackExtensions)
{
	std::vector<std::string> candidates;
	if (!File::isSafeResourcePath(fileName))
	{
		return candidates;
	}

	std::string directPath = buildDirectMediaPath(folder, fileName);
	std::string normalizedDirectPath = normalizeSeparators(directPath);
	if (hasSupportedMediaExtension(normalizedDirectPath, fallbackExtensions))
	{
		appendUnique(candidates, directPath);
	}

	std::string baseDirectory = convert::extractFilePath(normalizedDirectPath);
	std::string baseName = convert::extractFileName(normalizedDirectPath);
	for (const auto& baseVariant : mediaBaseNameVariants(baseName))
	{
		for (const auto& ext : fallbackExtensions)
		{
			appendUnique(candidates, toRuntimeSeparators(baseDirectory + baseVariant + ext));
		}
	}
	return candidates;
}

std::string resolveMediaAssetPath(
	const std::string& folder,
	const std::string& fileName,
	const std::vector<std::string>& fallbackExtensions)
{
	std::vector<std::string> candidates = buildMediaAssetCandidates(folder, fileName, fallbackExtensions);
	std::string resolvedCandidate = File::resolveFirstExistingResource(candidates);
	if (!resolvedCandidate.empty())
	{
		return resolvedCandidate;
	}
	return candidates.empty() ? "" : candidates.front();
}

std::string resolveSoundAssetPath(const std::string& fileName)
{
	return resolveMediaAssetPath(SOUND_FOLDER, fileName, { ".wav" });
}
