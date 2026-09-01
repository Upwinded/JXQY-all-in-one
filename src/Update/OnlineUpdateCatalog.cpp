#include "OnlineUpdateCatalog.h"

#include "../File/ResourcePathSafety.h"
#include "../Resource/ModReleaseMetadata.h"
#include "../Resource/ResourceIniReader.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace
{
std::string trimAscii(std::string value)
{
	const auto isWhitespace = [](char character)
	{
		return character == ' ' || character == '\t' ||
			character == '\r' || character == '\n';
	};
	std::size_t begin = 0;
	while (begin < value.size() && isWhitespace(value[begin]))
	{
		begin++;
	}
	std::size_t end = value.size();
	while (end > begin && isWhitespace(value[end - 1]))
	{
		end--;
	}
	return value.substr(begin, end - begin);
}

std::vector<std::string> splitCommaList(const std::string& text)
{
	std::vector<std::string> values;
	std::size_t start = 0;
	while (start <= text.size())
	{
		const std::size_t separator = text.find(',', start);
		values.push_back(trimAscii(text.substr(start,
			separator == std::string::npos
				? std::string::npos
				: separator - start)));
		if (separator == std::string::npos)
		{
			break;
		}
		start = separator + 1;
	}
	return values;
}

std::string foldAscii(std::string_view text)
{
	std::string folded(text);
	for (char& character : folded)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character + ('a' - 'A'));
		}
	}
	return folded;
}

bool startsWithAsciiCaseInsensitive(
	std::string_view text,
	std::string_view prefix)
{
	return text.size() >= prefix.size() &&
		foldAscii(text.substr(0, prefix.size())) == foldAscii(prefix);
}

std::vector<std::string> readSectionNamesInOrder(const std::string& text)
{
	std::vector<std::string> names;
	std::size_t lineStart = 0;
	bool firstLine = true;
	while (lineStart <= text.size())
	{
		const std::size_t lineEnd = text.find('\n', lineStart);
		std::string line = text.substr(
			lineStart,
			lineEnd == std::string::npos
				? std::string::npos
				: lineEnd - lineStart);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (firstLine && line.size() >= 3 &&
			static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF)
		{
			line.erase(0, 3);
		}
		firstLine = false;
		line = trimAscii(std::move(line));
		if (!line.empty() && line.front() == '[')
		{
			const std::size_t close = line.find(']');
			if (close != std::string::npos)
			{
				names.push_back(trimAscii(line.substr(1, close - 1)));
			}
		}
		if (lineEnd == std::string::npos)
		{
			break;
		}
		lineStart = lineEnd + 1;
	}
	return names;
}

bool parseUnsignedDecimal(const std::string& text, std::uint64_t& value)
{
	if (text.empty())
	{
		return false;
	}
	std::uint64_t parsed = 0;
	for (char character : text)
	{
		if (character < '0' || character > '9')
		{
			return false;
		}
		const std::uint64_t digit =
			static_cast<std::uint64_t>(character - '0');
		if (parsed >
			(std::numeric_limits<std::uint64_t>::max() - digit) / 10)
		{
			return false;
		}
		parsed = parsed * 10 + digit;
	}
	value = parsed;
	return true;
}

bool isValidDisplayText(std::string_view text, std::size_t maximumBytes)
{
	if (text.empty() || text.size() > maximumBytes)
	{
		return false;
	}
	return std::none_of(text.begin(), text.end(), [](char character)
	{
		const unsigned char byte = static_cast<unsigned char>(character);
		return byte < 0x20 || byte == 0x7F;
	});
}

bool isValidDisplayVersion(std::string_view text)
{
	return isValidDisplayText(text, 128);
}

bool isValidProgramTarget(std::string_view text)
{
	return ModRelease::isValidUpdateTargetIdentifier(text);
}

void appendIssue(
	OnlineUpdate::CatalogParseResult& result,
	OnlineUpdate::CatalogParseError error,
	const std::string& section,
	const std::string& field,
	const std::string& value = {})
{
	result.issues.push_back({ error, section, field, value });
}

bool parseVersionField(
	OnlineUpdate::CatalogParseResult& result,
	const std::string& section,
	const std::string& field,
	const std::string& text,
	ModRelease::SemanticVersion& version)
{
	if (text.empty())
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::MissingField,
			section, field);
		return false;
	}
	const ModRelease::SemanticVersionParseResult parsed =
		ModRelease::parseSemanticVersion(text);
	if (!parsed.succeeded())
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::InvalidVersion,
			section, field, text);
		return false;
	}
	version = parsed.version;
	return true;
}

void parseArtifactFields(
	OnlineUpdate::CatalogParseResult& result,
	const ResourceIniReader& ini,
	const std::string& section,
	std::string& path,
	std::uint64_t& size,
	std::string& crc32)
{
	path = trimAscii(ini.get(section, "Artifact", ""));
	if (path.empty())
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::MissingField,
			section, "Artifact");
	}
	else if (!OnlineUpdate::isSafeArtifactPath(path))
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::UnsafeArtifactPath,
			section, "Artifact", path);
	}

	const std::string sizeText = trimAscii(ini.get(section, "Size", ""));
	if (!parseUnsignedDecimal(sizeText, size) || size == 0)
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::InvalidArtifactSize,
			section, "Size", sizeText);
	}

	crc32 = trimAscii(ini.get(section, "Crc32", ""));
	if (!OnlineUpdate::isValidCrc32Hex(crc32))
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::InvalidCrc32,
			section, "Crc32", crc32);
	}
	else
	{
		std::transform(crc32.begin(), crc32.end(), crc32.begin(),
			[](unsigned char character)
			{
				return static_cast<char>(std::tolower(character));
			});
	}
}

void parseIncrementalArtifactFields(
	OnlineUpdate::CatalogParseResult& result,
	const ResourceIniReader& ini,
	const std::string& section,
	std::optional<OnlineUpdate::IncrementalResourcePackage>& incrementalPackage)
{
	incrementalPackage.reset();
	const bool declared =
		ini.hasKey(section, "IncrementalArtifact") ||
		ini.hasKey(section, "IncrementalSize") ||
		ini.hasKey(section, "IncrementalCrc32");
	if (!declared)
	{
		return;
	}

	OnlineUpdate::IncrementalResourcePackage package;
	package.artifactPath = trimAscii(
		ini.get(section, "IncrementalArtifact", ""));
	if (package.artifactPath.empty())
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::MissingField,
			section, "IncrementalArtifact");
	}
	else if (!OnlineUpdate::isSafeArtifactPath(package.artifactPath))
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::UnsafeArtifactPath,
			section, "IncrementalArtifact", package.artifactPath);
	}

	const std::string sizeText = trimAscii(
		ini.get(section, "IncrementalSize", ""));
	if (!parseUnsignedDecimal(sizeText, package.artifactSize) ||
		package.artifactSize == 0)
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::InvalidArtifactSize,
			section, "IncrementalSize", sizeText);
	}

	package.crc32Hex = trimAscii(
		ini.get(section, "IncrementalCrc32", ""));
	if (!OnlineUpdate::isValidCrc32Hex(package.crc32Hex))
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::InvalidCrc32,
			section, "IncrementalCrc32", package.crc32Hex);
	}
	else
	{
		package.crc32Hex = foldAscii(package.crc32Hex);
	}
	incrementalPackage = std::move(package);
}

void parseIncrementalChainFields(
	OnlineUpdate::CatalogParseResult& result,
	const ResourceIniReader& ini,
	const std::string& section,
	const std::optional<OnlineUpdate::IncrementalResourcePackage>& legacy,
	std::vector<OnlineUpdate::IncrementalResourcePackage>& chain)
{
	chain.clear();
	const std::vector<std::string> keys = ini.sectionKeys(section);
	const bool countDeclared =
		ini.hasKey(section, "IncrementalChainCount");
	bool chainFieldDeclared = false;
	for (const std::string& key : keys)
	{
		if (key.rfind("incrementalchain", 0) == 0 &&
			key != "incrementalchaincount")
		{
			chainFieldDeclared = true;
			break;
		}
	}
	if (!countDeclared && !chainFieldDeclared)
	{
		return;
	}
	if (!countDeclared)
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::MissingField,
			section, "IncrementalChainCount");
		return;
	}

	std::uint64_t count = 0;
	const std::string countText = trimAscii(
		ini.get(section, "IncrementalChainCount", ""));
	if (!parseUnsignedDecimal(countText, count) || count == 0 ||
		count > OnlineUpdate::MaximumIncrementalChainPackageCount)
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::InvalidList,
			section, "IncrementalChainCount", countText);
		return;
	}
	if (!legacy.has_value())
	{
		appendIssue(result, OnlineUpdate::CatalogParseError::MissingField,
			section, "IncrementalArtifact");
	}

	std::set<std::string> allowedKeys = { "incrementalchaincount" };
	std::set<std::string> artifactPaths;
	chain.reserve(static_cast<std::size_t>(count));
	for (std::size_t index = 1; index <= count; index++)
	{
		const std::string prefix =
			"IncrementalChain" + std::to_string(index);
		const std::string artifactField = prefix + "Artifact";
		const std::string sizeField = prefix + "Size";
		const std::string crc32Field = prefix + "Crc32";
		allowedKeys.insert(foldAscii(artifactField));
		allowedKeys.insert(foldAscii(sizeField));
		allowedKeys.insert(foldAscii(crc32Field));

		OnlineUpdate::IncrementalResourcePackage package;
		package.artifactPath = trimAscii(
			ini.get(section, artifactField, ""));
		if (package.artifactPath.empty())
		{
			appendIssue(result, OnlineUpdate::CatalogParseError::MissingField,
				section, artifactField);
		}
		else if (!OnlineUpdate::isSafeArtifactPath(package.artifactPath))
		{
			appendIssue(result,
				OnlineUpdate::CatalogParseError::UnsafeArtifactPath,
				section, artifactField, package.artifactPath);
		}
		else if (!artifactPaths.insert(foldAscii(package.artifactPath)).second)
		{
			appendIssue(result,
				OnlineUpdate::CatalogParseError::DuplicateIdentifier,
				section, artifactField, package.artifactPath);
		}

		const std::string sizeText = trimAscii(
			ini.get(section, sizeField, ""));
		if (!parseUnsignedDecimal(sizeText, package.artifactSize) ||
			package.artifactSize == 0)
		{
			appendIssue(result,
				OnlineUpdate::CatalogParseError::InvalidArtifactSize,
				section, sizeField, sizeText);
		}
		package.crc32Hex = trimAscii(ini.get(section, crc32Field, ""));
		if (!OnlineUpdate::isValidCrc32Hex(package.crc32Hex))
		{
			appendIssue(result, OnlineUpdate::CatalogParseError::InvalidCrc32,
				section, crc32Field, package.crc32Hex);
		}
		else
		{
			package.crc32Hex = foldAscii(package.crc32Hex);
		}
		chain.push_back(std::move(package));
	}

	for (const std::string& key : keys)
	{
		if (key.rfind("incrementalchain", 0) == 0 &&
			allowedKeys.find(key) == allowedKeys.end())
		{
			appendIssue(result, OnlineUpdate::CatalogParseError::InvalidList,
				section, key);
		}
	}

	if (legacy.has_value() && !chain.empty())
	{
		const OnlineUpdate::IncrementalResourcePackage& last = chain.back();
		if (legacy->artifactPath != last.artifactPath ||
			legacy->artifactSize != last.artifactSize ||
			legacy->crc32Hex != last.crc32Hex)
		{
			appendIssue(result, OnlineUpdate::CatalogParseError::InvalidList,
				section, "IncrementalArtifact", legacy->artifactPath);
		}
	}
}

bool validateDependencyGraph(OnlineUpdate::CatalogParseResult& result)
{
	enum class VisitState
	{
		Unvisited,
		Visiting,
		Complete
	};
	std::map<std::string, VisitState> states;
	std::function<bool(const std::string&)> visit =
		[&](const std::string& resourceKey)
		{
			states[resourceKey] = VisitState::Visiting;
			const auto packageEntry =
				result.catalog.resourcePackages.find(resourceKey);
			if (packageEntry == result.catalog.resourcePackages.end())
			{
				states[resourceKey] = VisitState::Complete;
				return true;
			}
			const OnlineUpdate::ResourcePackage& package =
				packageEntry->second;
			for (const std::string& dependencyId :
				package.dependencyGameIds)
			{
				const std::string dependencyKey =
					OnlineUpdate::foldGameId(dependencyId);
				const VisitState dependencyState = states[dependencyKey];
				if (dependencyState == VisitState::Visiting)
				{
					appendIssue(
						result,
						OnlineUpdate::CatalogParseError::DependencyCycle,
						"Resource." + package.gameId,
						"Dependencies",
						dependencyId);
					return false;
				}
				if (dependencyState == VisitState::Unvisited &&
					!visit(dependencyKey))
				{
					return false;
				}
			}
			states[resourceKey] = VisitState::Complete;
			return true;
		};

	for (const auto& packageEntry : result.catalog.resourcePackages)
	{
		if (states[packageEntry.first] == VisitState::Unvisited &&
			!visit(packageEntry.first))
		{
			return false;
		}
	}
	return true;
}

bool isPlainHttpsCatalogUrl(std::string_view url)
{
	constexpr std::string_view Scheme = "https://";
	if (url.size() <= Scheme.size() || url.size() > 4096 ||
		foldAscii(url.substr(0, Scheme.size())) != Scheme ||
		url.find('?') != std::string_view::npos ||
		url.find('#') != std::string_view::npos ||
		url.find('\\') != std::string_view::npos ||
		!ResourcePathSafety::isValidUtf8(std::string(url)))
	{
		return false;
	}
	const std::size_t pathStart = url.find('/', Scheme.size());
	if (pathStart == std::string_view::npos || pathStart + 1 >= url.size())
	{
		return false;
	}
	const std::string_view authority =
		url.substr(Scheme.size(), pathStart - Scheme.size());
	if (authority.empty() || authority.find('@') != std::string_view::npos)
	{
		return false;
	}
	return std::none_of(url.begin(), url.end(), [](char character)
	{
		const unsigned char byte = static_cast<unsigned char>(character);
		return byte <= 0x20 || byte == 0x7F;
	});
}

bool parseCatalogUrlList(
	const std::string& text,
	std::vector<std::string>& urls)
{
	urls.clear();
	if (trimAscii(text).empty())
	{
		return true;
	}
	for (std::string url : splitCommaList(text))
	{
		if (!isPlainHttpsCatalogUrl(url))
		{
			urls.clear();
			return false;
		}
		if (std::find(urls.begin(), urls.end(), url) == urls.end())
		{
			urls.push_back(std::move(url));
			if (urls.size() > OnlineUpdate::MaximumCatalogUrlsPerType)
			{
				urls.clear();
				return false;
			}
		}
	}
	return true;
}
}

namespace OnlineUpdate
{
std::string foldGameId(std::string_view gameId)
{
	return foldAscii(gameId);
}

bool isValidOnlineGameId(std::string_view gameId)
{
	if (gameId.empty() || gameId.size() > 128 ||
		!ResourcePathSafety::isValidUtf8(std::string(gameId)))
	{
		return false;
	}
	const auto isAsciiWhitespace = [](char character)
	{
		return character == ' ' || character == '\t' ||
			character == '\r' || character == '\n';
	};
	if (isAsciiWhitespace(gameId.front()) ||
		isAsciiWhitespace(gameId.back()))
	{
		return false;
	}
	for (char character : gameId)
	{
		const unsigned char byte = static_cast<unsigned char>(character);
		if (byte < 0x20 || byte == 0x7F ||
			character == ',' || character == '[' || character == ']')
		{
			return false;
		}
	}
	return true;
}

bool isSafeArtifactPath(std::string_view path) noexcept
{
	if (path.empty() || path.size() > 1024 || path.front() == '/' ||
		path.back() == '/' || path.find('\\') != std::string_view::npos ||
		path.find(':') != std::string_view::npos ||
		path.find('?') != std::string_view::npos ||
		path.find('#') != std::string_view::npos ||
		!ResourcePathSafety::isValidUtf8(std::string(path)))
	{
		return false;
	}
	std::size_t start = 0;
	while (start <= path.size())
	{
		const std::size_t separator = path.find('/', start);
		const std::string_view component = path.substr(start,
			separator == std::string_view::npos
				? std::string_view::npos
				: separator - start);
		if (component.empty() || component == "." || component == "..")
		{
			return false;
		}
		if (separator == std::string_view::npos)
		{
			break;
		}
		start = separator + 1;
	}
	return true;
}

bool isValidCrc32Hex(std::string_view text) noexcept
{
	if (text.size() != 8)
	{
		return false;
	}
	return std::all_of(text.begin(), text.end(), [](char character)
	{
		return (character >= '0' && character <= '9') ||
			(character >= 'a' && character <= 'f') ||
			(character >= 'A' && character <= 'F');
	});
}

bool parseIncrementalChainReceipt(
	std::string_view text,
	std::vector<std::string>& crc32s)
{
	crc32s.clear();
	if (text.empty())
	{
		return true;
	}
	const std::vector<std::string> values = splitCommaList(std::string(text));
	if (values.empty() ||
		values.size() > MaximumIncrementalChainPackageCount)
	{
		return false;
	}
	crc32s.reserve(values.size());
	for (const std::string& value : values)
	{
		if (!isValidCrc32Hex(value))
		{
			crc32s.clear();
			return false;
		}
		crc32s.push_back(foldAscii(value));
	}
	return true;
}

CatalogParseResult parseCatalog(std::string_view utf8Text)
{
	return parseCatalog(utf8Text.data(), utf8Text.size());
}

CatalogParseResult parseCatalog(const char* data, std::size_t length)
{
	CatalogParseResult result;
	if (data == nullptr || length == 0)
	{
		appendIssue(result, CatalogParseError::Empty, "Catalog", "");
		return result;
	}
	if (length > MaximumCatalogBytes)
	{
		appendIssue(result, CatalogParseError::TooLarge, "Catalog", "");
		return result;
	}
	const std::string text(data, length);
	if (!ResourcePathSafety::isValidUtf8(text) ||
		text.find('\0') != std::string::npos)
	{
		appendIssue(result, CatalogParseError::InvalidUtf8, "Catalog", "");
		return result;
	}

	const ResourceIniReader ini(data, length);
	if (ini.parseError() != 0)
	{
		appendIssue(result, CatalogParseError::InvalidIni, "Catalog", "");
		return result;
	}
	result.catalog.schemaVersion = static_cast<int>(
		ini.getInteger("Catalog", "SchemaVersion", 0));
	if (result.catalog.schemaVersion != 1)
	{
		appendIssue(result, CatalogParseError::UnsupportedSchema,
			"Catalog", "SchemaVersion",
			std::to_string(result.catalog.schemaVersion));
		return result;
	}

	const std::vector<std::string> sectionNames =
		readSectionNamesInOrder(text);
	std::string commonSection;
	for (const std::string& section : sectionNames)
	{
		if (foldAscii(section) != "common")
		{
			continue;
		}
		if (!commonSection.empty())
		{
			appendIssue(result, CatalogParseError::DuplicateIdentifier,
				section, "Common");
			continue;
		}
		commonSection = section;
	}
	if (!commonSection.empty())
	{
		CommonPackage package;
		package.versionText = trimAscii(
			ini.get(commonSection, "Version", ""));
		if (package.versionText.empty())
		{
			appendIssue(result, CatalogParseError::MissingField,
				commonSection, "Version");
		}
		else if (!isValidDisplayVersion(package.versionText))
		{
			appendIssue(result, CatalogParseError::InvalidVersion,
				commonSection, "Version", package.versionText);
		}
		parseArtifactFields(result, ini, commonSection,
			package.artifactPath, package.artifactSize, package.crc32Hex);
		package.releaseNotes = ini.get(
			commonSection, "ReleaseNotes", "");
		result.catalog.commonPackage = std::move(package);
	}

	std::size_t resourceSectionCount = 0;
	std::set<std::string> declaredResourceIds;
	for (const std::string& section : sectionNames)
	{
		constexpr std::string_view ResourcePrefix = "Resource.";
		if (!startsWithAsciiCaseInsensitive(section, ResourcePrefix))
		{
			continue;
		}
		resourceSectionCount++;
		if (resourceSectionCount > MaximumResourcePackageCount)
		{
			appendIssue(result, CatalogParseError::TooManyPackages,
				"Catalog", "Resource sections");
			break;
		}

		const std::string gameId = section.substr(ResourcePrefix.size());
		const std::string foldedGameId = foldGameId(gameId);
		if (!isValidOnlineGameId(gameId))
		{
			appendIssue(result, CatalogParseError::InvalidIdentifier,
				section, "Game.Id", gameId);
			continue;
		}
		if (!declaredResourceIds.insert(foldedGameId).second)
		{
			appendIssue(result, CatalogParseError::DuplicateIdentifier,
				section, "Game.Id", gameId);
			continue;
		}

		ResourcePackage package;
		package.gameId = gameId;
		package.displayName = trimAscii(ini.get(section, "Name", ""));
		if (!package.displayName.empty() &&
			!isValidDisplayText(package.displayName, 256))
		{
			appendIssue(result, CatalogParseError::InvalidDisplayText,
				section, "Name", package.displayName);
		}
		package.author = trimAscii(ini.get(section, "Author", ""));
		if (!package.author.empty() &&
			!isValidDisplayText(package.author, 256))
		{
			appendIssue(result, CatalogParseError::InvalidDisplayText,
				section, "Author", package.author);
		}
		package.versionText = trimAscii(ini.get(section, "Version", ""));
		if (package.versionText.empty())
		{
			appendIssue(result, CatalogParseError::MissingField,
				section, "Version");
		}
		else if (!isValidDisplayVersion(package.versionText))
		{
			appendIssue(result, CatalogParseError::InvalidVersion,
				section, "Version", package.versionText);
		}

		package.minimumEngineVersionText =
			trimAscii(ini.get(section, "MinimumEngineVersion", ""));
		parseVersionField(result, section, "MinimumEngineVersion",
			package.minimumEngineVersionText,
			package.minimumEngineVersion);
		parseArtifactFields(result, ini, section, package.artifactPath,
			package.artifactSize, package.crc32Hex);
		parseIncrementalArtifactFields(
			result, ini, section, package.incrementalPackage);
		parseIncrementalChainFields(
			result,
			ini,
			section,
			package.incrementalPackage,
			package.incrementalChain);
		package.releaseNotes = ini.get(section, "ReleaseNotes", "");
		package.resourceOnly = ini.getBoolean(
			section, "ResourceOnly", false);

		const std::string dependencyText =
			trimAscii(ini.get(section, "Dependencies", ""));
		if (!dependencyText.empty())
		{
			const std::vector<std::string> dependencyValues =
				splitCommaList(dependencyText);
			if (dependencyValues.size() > MaximumDependencyCountPerPackage)
			{
				appendIssue(result, CatalogParseError::InvalidDependency,
					section, "Dependencies", dependencyText);
			}
			else
			{
				std::set<std::string> dependencyIds;
				for (const std::string& dependencyGameId : dependencyValues)
				{
					const std::string foldedDependencyId =
						foldGameId(dependencyGameId);
					if (!isValidOnlineGameId(dependencyGameId) ||
						!dependencyIds.insert(foldedDependencyId).second)
					{
						appendIssue(result,
							CatalogParseError::InvalidDependency,
							section, "Dependencies", dependencyGameId);
						continue;
					}
					package.dependencyGameIds.push_back(dependencyGameId);
				}
			}
		}
		result.catalog.resourcePackages.emplace(
			foldedGameId, std::move(package));
	}

	const std::vector<std::string> programTargets = splitCommaList(
		trimAscii(ini.get("Catalog", "ProgramTargets", "")));
	if (programTargets.size() > MaximumProgramPackageCount)
	{
		appendIssue(result, CatalogParseError::TooManyPackages,
			"Catalog", "ProgramTargets");
		result.catalog = {};
		return result;
	}
	std::set<std::string> declaredProgramTargets;
	for (const std::string& target : programTargets)
	{
		if (target.empty())
		{
			if (programTargets.size() == 1)
			{
				continue;
			}
			appendIssue(result, CatalogParseError::InvalidList,
				"Catalog", "ProgramTargets");
			continue;
		}
		if (!isValidProgramTarget(target))
		{
			appendIssue(result, CatalogParseError::InvalidIdentifier,
				"Catalog", "ProgramTargets", target);
			continue;
		}
		if (!declaredProgramTargets.insert(target).second)
		{
			appendIssue(result, CatalogParseError::DuplicateIdentifier,
				"Catalog", "ProgramTargets", target);
			continue;
		}

		const std::string section = "Program." + target;
		ProgramPackage package;
		package.target = target;
		package.versionText = trimAscii(ini.get(section, "Version", ""));
		parseVersionField(result, section, "Version", package.versionText,
			package.version);
		parseArtifactFields(result, ini, section, package.artifactPath,
			package.artifactSize, package.crc32Hex);
		package.releaseNotes = ini.get(section, "ReleaseNotes", "");
		result.catalog.programPackages.emplace(target, std::move(package));
	}

	for (const auto& packageEntry : result.catalog.resourcePackages)
	{
		const ResourcePackage& package = packageEntry.second;
		for (const std::string& dependencyGameId : package.dependencyGameIds)
		{
			if (result.catalog.resourcePackages.find(
					foldGameId(dependencyGameId)) ==
				result.catalog.resourcePackages.end())
			{
				appendIssue(result, CatalogParseError::UnknownDependency,
					"Resource." + package.gameId,
					"Dependencies", dependencyGameId);
			}
		}
	}
	if (result.issues.empty())
	{
		validateDependencyGraph(result);
	}

	if (!result.issues.empty())
	{
		result.catalog = {};
	}
	return result;
}

bool parseUpdateSources(
	std::string_view utf8Text,
	UpdateSources& sources)
{
	sources = {};
	if (utf8Text.empty() || utf8Text.size() > MaximumUpdateSourceBytes)
	{
		return false;
	}
	const std::string text(utf8Text);
	if (!ResourcePathSafety::isValidUtf8(text) ||
		text.find('\0') != std::string::npos)
	{
		return false;
	}
	const ResourceIniReader ini(text.data(), text.size());
	if (ini.parseError() != 0)
	{
		return false;
	}
	sources.schemaVersion = static_cast<int>(
		ini.getInteger("Sources", "SchemaVersion", 0));
	if (sources.schemaVersion != 1 ||
		!parseCatalogUrlList(
			ini.get("Sources", "ResourceCatalogUrls", ""),
			sources.resourceCatalogUrls) ||
		!parseCatalogUrlList(
			ini.get("Sources", "ApplicationCatalogUrls", ""),
			sources.applicationCatalogUrls) ||
		(sources.resourceCatalogUrls.empty() &&
			sources.applicationCatalogUrls.empty()))
	{
		sources = {};
		return false;
	}
	return true;
}

bool parseCommonPackageVersion(
	std::string_view utf8Text,
	std::string& versionText)
{
	CommonPackageInstallation installation;
	const bool succeeded = parseCommonPackageInstallation(
		utf8Text, installation);
	versionText = succeeded ? installation.versionText : std::string();
	return succeeded;
}

bool parseCommonPackageInstallation(
	std::string_view utf8Text,
	CommonPackageInstallation& installation)
{
	installation = {};
	if (utf8Text.empty() ||
		utf8Text.size() > MaximumCommonVersionFileBytes)
	{
		return false;
	}
	const std::string text(utf8Text);
	if (!ResourcePathSafety::isValidUtf8(text) ||
		text.find('\0') != std::string::npos)
	{
		return false;
	}
	const ResourceIniReader ini(text.data(), text.size());
	if (ini.parseError() != 0)
	{
		return false;
	}
	installation.versionText = trimAscii(ini.get("Common", "Version", ""));
	if (!isValidDisplayVersion(installation.versionText))
	{
		installation = {};
		return false;
	}
	installation.installedArtifactCrc32 = trimAscii(
		ini.get("Common", "InstalledArtifactCrc32", ""));
	if (isValidCrc32Hex(installation.installedArtifactCrc32))
	{
		installation.installedArtifactCrc32 = foldAscii(
			installation.installedArtifactCrc32);
	}
	else
	{
		installation.installedArtifactCrc32.clear();
	}
	return true;
}

bool commonPackageNeedsDownload(
	const Catalog& catalog,
	std::string_view installedArtifactCrc32) noexcept
{
	if (!catalog.commonPackage.has_value())
	{
		return false;
	}
	return !isValidCrc32Hex(installedArtifactCrc32) ||
		foldAscii(installedArtifactCrc32) !=
			catalog.commonPackage->crc32Hex;
}

ProgramUpdateCheck checkProgramUpdate(
	const Catalog& catalog,
	std::string_view target,
	std::string_view currentVersion) noexcept
{
	ProgramUpdateCheck result;
	const ModRelease::SemanticVersionParseResult parsedCurrentVersion =
		ModRelease::parseSemanticVersion(currentVersion);
	if (target.empty() || !parsedCurrentVersion.succeeded())
	{
		return result;
	}

	const auto package = catalog.programPackages.find(std::string(target));
	if (package == catalog.programPackages.end())
	{
		result.status = ProgramUpdateStatus::TargetNotFound;
		return result;
	}

	result.package = &package->second;
	result.versionComparison = ModRelease::compareSemanticVersionPrecedence(
		package->second.version, parsedCurrentVersion.version);
	result.status = result.versionComparison > 0
		? ProgramUpdateStatus::UpdateAvailable
		: ProgramUpdateStatus::UpToDate;
	return result;
}
}
