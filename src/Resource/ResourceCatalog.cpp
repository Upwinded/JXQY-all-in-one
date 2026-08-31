#include "ResourceCatalog.h"
#include "ResourceIniReader.h"

#include "../File/RootedResourceReader.h"
#include "../File/StrictRelativeResourcePath.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace
{
namespace fs = std::filesystem;

constexpr char ManifestFileName[] = "game_profile.ini";
constexpr char CollectionConfigFileName[] = "resources.ini";
constexpr std::size_t MaximumCatalogIniFileBytes = 1024 * 1024;

struct CatalogPack
{
	std::string stableKey;
	fs::path root;
	fs::path manifestPath;
	std::string sourceTag;
	ResourceManifest manifest;
	std::string effectiveSaveNamespace;
	bool primaryCollectionEntry = false;
	bool replacesPrimaryGameId = false;
	bool saveNamespaceAdjusted = false;
};

struct Catalog
{
	fs::path collectionRoot;
	std::vector<CatalogPack> packs;
	std::vector<RuntimeResource::CatalogDiagnostic> diagnostics;
	const RuntimeResource::ResourceCatalogFileAccess* fileAccess =
		nullptr;
	std::string collectionCommonPath;
	std::string collectionUpdateSourceUrl;
	std::string collectionResourceCatalogUrl;
	std::string collectionApplicationCatalogUrl;
	std::size_t iniBytesRead = 0;
	std::set<std::string> countedIniFiles;
	std::set<std::string> countedPackRoots;
	bool rootManifestDeclared = false;
};

class ScopedIndependentIniBudget
{
public:
	explicit ScopedIndependentIniBudget(Catalog& catalogValue)
		: catalog(catalogValue),
		  savedBytesRead(catalogValue.iniBytesRead),
		  savedCountedFiles(std::move(catalogValue.countedIniFiles))
	{
		catalog.iniBytesRead = 0;
		catalog.countedIniFiles.clear();
	}

	~ScopedIndependentIniBudget()
	{
		catalog.iniBytesRead = savedBytesRead;
		catalog.countedIniFiles = std::move(savedCountedFiles);
	}

	ScopedIndependentIniBudget(const ScopedIndependentIniBudget&) = delete;
	ScopedIndependentIniBudget& operator=(
		const ScopedIndependentIniBudget&) = delete;

private:
	Catalog& catalog;
	std::size_t savedBytesRead = 0;
	std::set<std::string> savedCountedFiles;
};

enum class IniLoadStatus
{
	Success,
	NotFound,
	UnsafePath,
	Unavailable,
	Invalid,
	BudgetExceeded
};

struct IniLoadResult
{
	IniLoadStatus status = IniLoadStatus::Unavailable;
	std::vector<std::uint8_t> bytes;
	bool sanitizedEmbeddedNullLines = false;
};

std::string trimAscii(std::string value)
{
	while (!value.empty() &&
		(value.front() == ' ' || value.front() == '\t' ||
			value.front() == '\r' || value.front() == '\n'))
	{
		value.erase(value.begin());
	}
	while (!value.empty() &&
		(value.back() == ' ' || value.back() == '\t' ||
			value.back() == '\r' || value.back() == '\n'))
	{
		value.pop_back();
	}
	return value;
}

std::string foldAsciiCase(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	return value;
}

std::string normalizedRootString(const fs::path& root)
{
	std::string value = root.generic_u8string();
	if (!value.empty() && value.back() != '/')
	{
		value.push_back('/');
	}
	return value;
}

bool containsEmbeddedNull(const std::vector<std::uint8_t>& bytes)
{
	return std::find(bytes.begin(), bytes.end(), 0) != bytes.end();
}

void sanitizeEmbeddedNullLines(
	std::vector<std::uint8_t>& bytes)
{
	std::vector<std::uint8_t> sanitized;
	sanitized.reserve(bytes.size());
	std::size_t lineStart = 0;
	while (lineStart < bytes.size())
	{
		std::size_t lineEnd = lineStart;
		while (lineEnd < bytes.size() &&
			bytes[lineEnd] != '\n')
		{
			++lineEnd;
		}
		const std::size_t contentEnd =
			lineEnd < bytes.size() ? lineEnd + 1 : lineEnd;
		const auto nullPosition = std::find(
			bytes.begin() + static_cast<std::ptrdiff_t>(lineStart),
			bytes.begin() + static_cast<std::ptrdiff_t>(lineEnd),
			0);
		if (nullPosition ==
			bytes.begin() + static_cast<std::ptrdiff_t>(lineEnd))
		{
			sanitized.insert(
				sanitized.end(),
				bytes.begin() +
					static_cast<std::ptrdiff_t>(lineStart),
				bytes.begin() +
					static_cast<std::ptrdiff_t>(contentEnd));
		}
		else
		{
			const std::string prefix(
				bytes.begin() +
					static_cast<std::ptrdiff_t>(lineStart),
				nullPosition);
			const std::string trimmedPrefix =
				trimAscii(prefix);
			const std::size_t closingBracket =
				trimmedPrefix.find(']');
			if (!trimmedPrefix.empty() &&
				trimmedPrefix.front() == '[' &&
				closingBracket != std::string::npos &&
				closingBracket > 1)
			{
				const std::string safeSection =
					trimmedPrefix.substr(
						0, closingBracket + 1) + "\n";
				sanitized.insert(
					sanitized.end(),
					safeSection.begin(),
					safeSection.end());
			}
			else
			{
				const std::string replacement =
					!trimmedPrefix.empty() &&
						trimmedPrefix.front() == '['
					? "[catalog-invalid-embedded-nul]\n"
					: "; catalog ignored a line containing an embedded NUL\n";
				sanitized.insert(
					sanitized.end(),
					replacement.begin(),
					replacement.end());
			}
		}
		lineStart = contentEnd;
	}
	bytes = std::move(sanitized);
}

IniLoadStatus statusForRootedRead(RootedResourceReader::Status status)
{
	switch (status)
	{
	case RootedResourceReader::Status::Success:
		return IniLoadStatus::Success;
	case RootedResourceReader::Status::NotFound:
		return IniLoadStatus::NotFound;
	case RootedResourceReader::Status::UnsafeRelativePath:
	case RootedResourceReader::Status::EscapesRoot:
		return IniLoadStatus::UnsafePath;
	case RootedResourceReader::Status::InvalidRoot:
	case RootedResourceReader::Status::NotRegularFile:
	case RootedResourceReader::Status::TooLarge:
	case RootedResourceReader::Status::ReadFailed:
		return IniLoadStatus::Unavailable;
	}
	return IniLoadStatus::Unavailable;
}

std::string catalogIniKey(
	const fs::path& root, std::string_view relativePath)
{
	std::string normalizedRelativePath =
		foldAsciiCase(std::string(relativePath));
	std::replace(
		normalizedRelativePath.begin(),
		normalizedRelativePath.end(),
		'\\',
		'/');
	return normalizedRootString(root) + normalizedRelativePath;
}

IniLoadResult readIniFromRoot(
	Catalog& catalog,
	const fs::path& root,
	std::string_view relativePath)
{
	IniLoadResult result;
	const ResourcePathSafety::StrictRelativePathResult normalizedPath =
		ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
			relativePath);
	if (!normalizedPath.succeeded())
	{
		result.status = IniLoadStatus::UnsafePath;
		return result;
	}
	const std::string key = catalogIniKey(
		root, normalizedPath.normalizedPath);
	const bool alreadyCounted =
		catalog.countedIniFiles.find(key) !=
		catalog.countedIniFiles.end();
	const std::size_t remainingBudget =
		catalog.iniBytesRead < RuntimeResource::MaximumCatalogIniBytes
		? RuntimeResource::MaximumCatalogIniBytes -
			catalog.iniBytesRead
		: 0;
	const std::size_t maximumReadBytes = alreadyCounted
		? MaximumCatalogIniFileBytes
		: std::min(
			MaximumCatalogIniFileBytes,
			remainingBudget);
	std::vector<std::uint8_t> bytes;
	IniLoadStatus readStatus = IniLoadStatus::Unavailable;
	bool readWasTooLarge = false;
	if (catalog.fileAccess != nullptr)
	{
		const RuntimeResource::CatalogFileReadResult read =
			catalog.fileAccess->readFileFromRoot(
				root, normalizedPath.normalizedPath,
				maximumReadBytes);
		switch (read.status)
		{
		case RuntimeResource::CatalogFileReadStatus::Success:
			readStatus = IniLoadStatus::Success;
			bytes = read.bytes;
			break;
		case RuntimeResource::CatalogFileReadStatus::NotFound:
			readStatus = IniLoadStatus::NotFound;
			break;
		case RuntimeResource::CatalogFileReadStatus::UnsafePath:
			readStatus = IniLoadStatus::UnsafePath;
			break;
		case RuntimeResource::CatalogFileReadStatus::TooLarge:
			readStatus = IniLoadStatus::Unavailable;
			readWasTooLarge = true;
			break;
		case RuntimeResource::CatalogFileReadStatus::Unavailable:
			readStatus = IniLoadStatus::Unavailable;
			break;
		}
	}
	else
	{
		RootedResourceReader::Result read =
			RootedResourceReader::readBoundedFileFromRoot(
				root, normalizedPath.normalizedPath,
				maximumReadBytes);
		readStatus = statusForRootedRead(read.status);
		readWasTooLarge =
			read.status == RootedResourceReader::Status::TooLarge;
		bytes = std::move(read.bytes);
	}
	if (readWasTooLarge &&
		!alreadyCounted &&
		maximumReadBytes < MaximumCatalogIniFileBytes)
	{
		result.status = IniLoadStatus::BudgetExceeded;
		return result;
	}
	result.status = readStatus;
	if (result.status != IniLoadStatus::Success)
	{
		return result;
	}
	if (!alreadyCounted)
	{
		catalog.countedIniFiles.insert(key);
		catalog.iniBytesRead += bytes.size();
	}
	if (bytes.empty())
	{
		result.status = IniLoadStatus::Invalid;
		return result;
	}
	if (containsEmbeddedNull(bytes))
	{
		sanitizeEmbeddedNullLines(bytes);
		result.sanitizedEmbeddedNullLines = true;
	}

	const ResourceIniReader ini(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
	if (ini.parseError() != 0)
	{
		result.status = IniLoadStatus::Invalid;
		return result;
	}
	result.bytes = std::move(bytes);
	return result;
}

IniLoadStatus readManifest(
	Catalog& catalog,
	const fs::path& root,
	std::string_view relativePath,
	ResourceManifest& manifest,
	bool requireIdentity = true,
	bool* sanitizedEmbeddedNullLines = nullptr)
{
	IniLoadResult input =
		readIniFromRoot(catalog, root, relativePath);
	if (sanitizedEmbeddedNullLines != nullptr)
	{
		*sanitizedEmbeddedNullLines =
			input.sanitizedEmbeddedNullLines;
	}
	if (input.status != IniLoadStatus::Success)
	{
		return input.status;
	}
	if (!manifest.loadFromBuffer(
			reinterpret_cast<const char*>(input.bytes.data()),
			static_cast<int>(input.bytes.size())) ||
		(requireIdentity && !manifest.isValid()))
	{
		return IniLoadStatus::Invalid;
	}
	manifest.resourceRoot = normalizedRootString(root);
	return IniLoadStatus::Success;
}

bool resolveExistingDirectory(
	const Catalog& catalog,
	const fs::path& input,
	fs::path& logicalPath,
	bool allowUnknown = false)
{
	if (catalog.fileAccess != nullptr)
	{
		if (!catalog.fileAccess->valid())
		{
			return false;
		}
		logicalPath = input.lexically_normal();
		if (input.empty())
		{
			logicalPath.clear();
		}
		const RuntimeResource::CatalogDirectoryStatus status =
			catalog.fileAccess->getDirectoryStatus(logicalPath);
		return status ==
				RuntimeResource::CatalogDirectoryStatus::Exists ||
			(allowUnknown &&
				status ==
					RuntimeResource::CatalogDirectoryStatus::Unknown);
	}

	std::error_code error;
	fs::path absolutePath = input.is_absolute()
		? input
		: fs::absolute(input, error);
	if (error || absolutePath.empty())
	{
		return false;
	}
	absolutePath = absolutePath.lexically_normal();
	const fs::file_status status = fs::status(
		absolutePath, error);
	if (error || !fs::is_directory(status))
	{
		return false;
	}
	logicalPath = std::move(absolutePath);
	return true;
}

bool sameLogicalPath(
	const fs::path& left, const fs::path& right)
{
	std::string leftText =
		left.lexically_normal().generic_u8string();
	std::string rightText =
		right.lexically_normal().generic_u8string();
#ifdef _WIN32
	leftText = foldAsciiCase(std::move(leftText));
	rightText = foldAsciiCase(std::move(rightText));
#endif
	return leftText == rightText;
}

bool containsRoot(
	const std::vector<RuntimeResource::ContentRoot>& roots,
	const fs::path& root)
{
	return std::any_of(
		roots.begin(), roots.end(),
		[&root](const RuntimeResource::ContentRoot& candidate)
		{
			return sameLogicalPath(candidate.root, root);
		});
}

std::string normalizedConfiguredPathText(std::string pathText)
{
	pathText = trimAscii(std::move(pathText));
	std::replace(pathText.begin(), pathText.end(), '\\', '/');
	return pathText;
}

fs::path resolveCollectionRelativeHostPath(
	const fs::path& collectionRoot, const std::string& pathText)
{
	std::string value = foldAsciiCase(
		normalizedConfiguredPathText(pathText));
	while (!value.empty() && value.front() == '/')
	{
		value.erase(value.begin());
	}
	std::string combined = collectionRoot.generic_u8string();
	if (!combined.empty() && combined.back() != '/')
	{
		combined.push_back('/');
	}
	combined += value;
	return fs::u8path(combined).lexically_normal();
}

void setFailure(
	RuntimeResource::ExactSelectionResult& result,
	RuntimeResource::ExactSelectionError error,
	std::string diagnosticCode,
	std::string message,
	std::string resourcePackId = {},
	fs::path hostPath = {})
{
	result.error = error;
	result.diagnosticCode = std::move(diagnosticCode);
	result.message = std::move(message);
	result.resourcePackId = std::move(resourcePackId);
	result.hostPath = std::move(hostPath);
}

void addCatalogDiagnostic(
	Catalog& catalog,
	RuntimeResource::CatalogDiagnosticSeverity severity,
	std::string code,
	std::string message,
	std::string stableEntryKey = {},
	std::string resourcePackId = {},
	fs::path hostPath = {})
{
	catalog.diagnostics.push_back({
		severity,
		std::move(code),
		std::move(stableEntryKey),
		std::move(resourcePackId),
		std::move(hostPath),
		std::move(message)
	});
}

void addSelectionDiagnostic(
	RuntimeResource::ExactSelectionResult& result,
	RuntimeResource::CatalogDiagnosticSeverity severity,
	std::string code,
	std::string message,
	std::string stableEntryKey = {},
	std::string resourcePackId = {},
	fs::path hostPath = {})
{
	result.diagnostics.push_back({
		severity,
		std::move(code),
		std::move(stableEntryKey),
		std::move(resourcePackId),
		std::move(hostPath),
		std::move(message)
	});
}

bool failForCatalogBudget(
	IniLoadStatus status,
	RuntimeResource::ExactSelectionResult& result,
	const fs::path& hostPath,
	const std::string& resourcePackId = {})
{
	if (status != IniLoadStatus::BudgetExceeded)
	{
		return false;
	}
	setFailure(
		result,
		RuntimeResource::ExactSelectionError::
			CatalogIniBudgetExceeded,
		"editor_run.resource.catalog_ini_budget_exceeded",
		"Resource catalog INI byte budget was exceeded",
		resourcePackId,
		hostPath);
	return true;
}

std::string catalogPackRootKey(const fs::path& root)
{
	std::string key = normalizedRootString(root);
#ifdef _WIN32
	key = foldAsciiCase(std::move(key));
#endif
	return key;
}

bool catalogPackRootIsRegistered(
	const Catalog& catalog, const fs::path& root)
{
	return catalog.countedPackRoots.find(
		catalogPackRootKey(root)) !=
		catalog.countedPackRoots.end();
}

bool registerCatalogPackRoot(
	Catalog& catalog,
	const fs::path& root,
	RuntimeResource::ExactSelectionResult& result,
	const std::string& resourcePackId = {})
{
	std::string key = catalogPackRootKey(root);
	if (catalog.countedPackRoots.find(key) !=
		catalog.countedPackRoots.end())
	{
		return true;
	}
	if (catalog.countedPackRoots.size() >=
		RuntimeResource::MaximumCatalogPackCount)
	{
		setFailure(
			result,
			RuntimeResource::ExactSelectionError::
				CatalogPackLimitExceeded,
			"editor_run.resource.catalog_pack_limit_exceeded",
			"Resource catalog pack limit was exceeded",
			resourcePackId,
			root);
		return false;
	}
	catalog.countedPackRoots.insert(std::move(key));
	return true;
}

RuntimeResource::CatalogDirectoryListResult listDirectChildDirectories(
	const Catalog& catalog)
{
	RuntimeResource::CatalogDirectoryListResult result;
	if (catalog.fileAccess != nullptr)
	{
		if (!catalog.fileAccess->listChildDirectories)
		{
			return result;
		}
		return catalog.fileAccess->listChildDirectories(
			catalog.collectionRoot);
	}

	std::error_code error;
	fs::directory_iterator iterator(catalog.collectionRoot, error);
	if (error)
	{
		return result;
	}
	const fs::directory_iterator end;
	while (iterator != end)
	{
		const fs::directory_entry& entry = *iterator;
		if (entry.is_directory(error))
		{
			const std::string name =
				entry.path().filename().u8string();
			if (!name.empty())
			{
				result.childDirectoryNames.push_back(name);
			}
		}
		if (error)
		{
			return result;
		}
		iterator.increment(error);
		if (error)
		{
			return result;
		}
	}

	result.status =
		RuntimeResource::CatalogDirectoryListStatus::Success;
	return result;
}

bool isDirectChildNameSafe(std::string_view name)
{
	return !name.empty() && name != "." && name != ".." &&
		name.front() != '.' &&
		name.find('\0') == std::string_view::npos &&
		name.find('/') == std::string_view::npos &&
		name.find('\\') == std::string_view::npos;
}

std::string collectionCommonDirectoryName(const Catalog& catalog)
{
	std::string commonPath = trimAscii(catalog.collectionCommonPath);
	std::replace(commonPath.begin(), commonPath.end(), '\\', '/');
	while (!commonPath.empty() && commonPath.front() == '/')
	{
		commonPath.erase(commonPath.begin());
	}
	const std::size_t separator = commonPath.find('/');
	if (separator != std::string::npos)
	{
		commonPath.resize(separator);
	}
	return commonPath.empty() ? "common" : commonPath;
}

bool appendDirectChildCatalogRoots(
	Catalog& catalog,
	RuntimeResource::ExactSelectionResult& result)
{
	RuntimeResource::CatalogDirectoryListResult listed =
		listDirectChildDirectories(catalog);
	if (listed.status !=
		RuntimeResource::CatalogDirectoryListStatus::Success)
	{
		addCatalogDiagnostic(
			catalog,
			RuntimeResource::CatalogDiagnosticSeverity::Error,
			"resource.catalog.child_discovery_unavailable",
			"Resource collection child directories could not be enumerated",
			{},
			{},
			catalog.collectionRoot);
		return true;
	}

	std::sort(
		listed.childDirectoryNames.begin(),
		listed.childDirectoryNames.end(),
		[](const std::string& left, const std::string& right)
		{
			const std::string foldedLeft = foldAsciiCase(left);
			const std::string foldedRight = foldAsciiCase(right);
			return foldedLeft != foldedRight
				? foldedLeft < foldedRight
				: left < right;
		});

	const std::string commonDirectory = foldAsciiCase(
		collectionCommonDirectoryName(catalog));
	for (const std::string& childName : listed.childDirectoryNames)
	{
		if (!isDirectChildNameSafe(childName) ||
			foldAsciiCase(childName) == commonDirectory)
		{
			continue;
		}

		fs::path packRoot;
		const fs::path requestedRoot =
			catalog.collectionRoot / fs::u8path(childName);
		if (!resolveExistingDirectory(
				catalog, requestedRoot, packRoot, true) ||
			catalogPackRootIsRegistered(catalog, packRoot))
		{
			continue;
		}

		ResourceManifest manifest;
		bool manifestSanitized = false;
		const IniLoadStatus manifestStatus = readManifest(
			catalog,
			packRoot,
			ManifestFileName,
			manifest,
			true,
			&manifestSanitized);
		if (manifestStatus == IniLoadStatus::NotFound)
		{
			continue;
		}
		const std::string stableKey =
			"pack." + foldAsciiCase(childName);
		if (manifestStatus != IniLoadStatus::Success)
		{
			if (failForCatalogBudget(
					manifestStatus,
					result,
					packRoot / ManifestFileName))
			{
				return false;
			}
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Error,
				"resource.catalog.discovered_manifest_invalid",
				"Direct child resource manifest is invalid or unavailable",
				stableKey,
				{},
				packRoot / ManifestFileName);
			continue;
		}
		if (!registerCatalogPackRoot(
				catalog, packRoot, result, manifest.id))
		{
			return false;
		}

		CatalogPack pack;
		pack.stableKey = stableKey;
		pack.root = std::move(packRoot);
		pack.manifestPath = pack.root / ManifestFileName;
		pack.primaryCollectionEntry = true;
		pack.manifest = std::move(manifest);
		catalog.packs.push_back(std::move(pack));
		if (manifestSanitized)
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.discovered_manifest_sanitized",
				"Manifest lines containing embedded NUL bytes were ignored",
				stableKey,
				catalog.packs.back().manifest.id,
				catalog.packs.back().manifestPath);
		}
	}
	return true;
}

bool loadPrimaryCatalog(
	const fs::path& assetsCollectionRoot,
	Catalog& catalog,
	RuntimeResource::ExactSelectionResult& result,
	const RuntimeResource::ResourceCatalogFileAccess* fileAccess =
		nullptr)
{
	catalog.fileAccess = fileAccess;
	if (fileAccess != nullptr && !fileAccess->valid())
	{
		setFailure(
			result,
			RuntimeResource::ExactSelectionError::
				ResourceConfigurationInvalid,
			"resource.catalog.file_access_invalid",
			"Resource catalog file access adapter is incomplete",
			{},
			assetsCollectionRoot);
		return false;
	}
	if (!resolveExistingDirectory(
			catalog,
			assetsCollectionRoot,
			catalog.collectionRoot,
			true))
	{
		setFailure(
			result,
			RuntimeResource::ExactSelectionError::
				AssetsRootUnavailable,
			"editor_run.resource.assets_root_unavailable",
			"Assets collection root is unavailable",
			{},
			assetsCollectionRoot);
		return false;
	}

	ResourceManifest rootManifest;
	bool rootManifestSanitized = false;
	const IniLoadStatus rootManifestStatus = readManifest(
		catalog,
		catalog.collectionRoot,
		ManifestFileName,
		rootManifest,
		true,
		&rootManifestSanitized);
	catalog.rootManifestDeclared =
		rootManifestStatus != IniLoadStatus::NotFound;
	if (rootManifestStatus == IniLoadStatus::Success)
	{
		if (!registerCatalogPackRoot(
				catalog,
				catalog.collectionRoot,
				result,
				rootManifest.id))
		{
			return false;
		}
		CatalogPack pack;
		pack.stableKey = "root";
		pack.root = catalog.collectionRoot;
		pack.manifestPath =
			catalog.collectionRoot / ManifestFileName;
		pack.primaryCollectionEntry = true;
		pack.manifest = std::move(rootManifest);
		catalog.packs.push_back(std::move(pack));
		if (rootManifestSanitized)
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.root_manifest_sanitized",
				"Lines containing embedded NUL bytes were ignored; usable manifest fields remain available",
				"root",
				catalog.packs.back().manifest.id,
				catalog.collectionRoot / ManifestFileName);
		}
	}
	else if (rootManifestStatus != IniLoadStatus::NotFound)
	{
		if (failForCatalogBudget(
				rootManifestStatus,
				result,
				catalog.collectionRoot / ManifestFileName))
		{
			return false;
		}
		addCatalogDiagnostic(
			catalog,
			RuntimeResource::CatalogDiagnosticSeverity::Error,
			"resource.catalog.root_manifest_invalid",
			"Collection-root resource manifest is invalid or unavailable; direct-child resources remain available",
			"root",
			{},
			catalog.collectionRoot / ManifestFileName);
	}

	IniLoadResult collectionConfigInput = readIniFromRoot(
		catalog,
		catalog.collectionRoot,
		CollectionConfigFileName);
	if (collectionConfigInput.status == IniLoadStatus::NotFound)
	{
		return appendDirectChildCatalogRoots(catalog, result);
	}
	if (collectionConfigInput.status != IniLoadStatus::Success)
	{
		if (failForCatalogBudget(
				collectionConfigInput.status,
				result,
				catalog.collectionRoot /
					CollectionConfigFileName))
		{
			return false;
		}
		addCatalogDiagnostic(
			catalog,
			RuntimeResource::CatalogDiagnosticSeverity::Error,
			"resource.catalog.collection_config_invalid",
			"Resource collection configuration is invalid or unavailable; direct-child resources remain available",
			{},
			{},
			catalog.collectionRoot / CollectionConfigFileName);
		return appendDirectChildCatalogRoots(catalog, result);
	}

	const ResourceIniReader collectionConfig(
		reinterpret_cast<const char*>(
			collectionConfigInput.bytes.data()),
		collectionConfigInput.bytes.size());
	if (collectionConfigInput.sanitizedEmbeddedNullLines)
	{
		addCatalogDiagnostic(
			catalog,
			RuntimeResource::CatalogDiagnosticSeverity::Warning,
			"resource.catalog.collection_config_sanitized",
			"resources.ini lines containing embedded NUL bytes were ignored",
			{},
			{},
			catalog.collectionRoot / CollectionConfigFileName);
	}
	catalog.collectionCommonPath = trimAscii(
		collectionConfig.get("Collection", "CommonPath", ""));
	catalog.collectionUpdateSourceUrl = trimAscii(
		collectionConfig.get("Collection", "UpdateSourceUrl", ""));
	catalog.collectionResourceCatalogUrl = trimAscii(
		collectionConfig.get("Collection", "ResourceCatalogUrl", ""));
	catalog.collectionApplicationCatalogUrl = trimAscii(
		collectionConfig.get("Collection", "ApplicationCatalogUrl", ""));

	return appendDirectChildCatalogRoots(catalog, result);
}

std::string supplementalSortKey(const fs::path& root)
{
	return foldAsciiCase(
		root.lexically_normal().generic_u8string());
}

bool catalogContainsStableKey(
	const Catalog& catalog, const std::string& stableKey)
{
	const std::string foldedKey = foldAsciiCase(stableKey);
	return std::any_of(
		catalog.packs.begin(), catalog.packs.end(),
		[&foldedKey](const CatalogPack& pack)
		{
			return foldAsciiCase(pack.stableKey) == foldedKey;
		});
}

bool appendSupplementalCatalogRoots(
	const std::vector<RuntimeResource::SupplementalResourceRoot>& inputs,
	Catalog& catalog,
	RuntimeResource::ExactSelectionResult& result)
{
	std::vector<RuntimeResource::SupplementalResourceRoot> roots =
		inputs;
	std::sort(
		roots.begin(), roots.end(),
		[](const RuntimeResource::SupplementalResourceRoot& left,
			const RuntimeResource::SupplementalResourceRoot& right)
		{
			const std::string leftPath = supplementalSortKey(left.root);
			const std::string rightPath = supplementalSortKey(right.root);
			if (leftPath != rightPath)
			{
				return leftPath < rightPath;
			}
			const std::string leftExact =
				left.root.lexically_normal().generic_u8string();
			const std::string rightExact =
				right.root.lexically_normal().generic_u8string();
			if (leftExact != rightExact)
			{
				return leftExact < rightExact;
			}
			const std::string leftKey =
				foldAsciiCase(trimAscii(left.stableEntryKey));
			const std::string rightKey =
				foldAsciiCase(trimAscii(right.stableEntryKey));
			if (leftKey != rightKey)
			{
				return leftKey < rightKey;
			}
			const std::string leftExactKey =
				trimAscii(left.stableEntryKey);
			const std::string rightExactKey =
				trimAscii(right.stableEntryKey);
			if (leftExactKey != rightExactKey)
			{
				return leftExactKey < rightExactKey;
			}
			return left.sourceTag < right.sourceTag;
		});
	if (roots.size() > RuntimeResource::MaximumCatalogPackCount)
	{
		addCatalogDiagnostic(
			catalog,
			RuntimeResource::CatalogDiagnosticSeverity::Warning,
			"resource.catalog.supplemental_candidate_limit_reached",
			"Supplemental candidate limit was reached; excess external roots were skipped while primary entries remain available");
		roots.resize(RuntimeResource::MaximumCatalogPackCount);
	}

	// External manifests have their own bounded discovery budget. A directory
	// full of large or invalid external profiles must not consume the primary
	// catalog budget later needed to resolve the selected pack's dependencies.
	ScopedIndependentIniBudget supplementalBudget(catalog);

	for (const RuntimeResource::SupplementalResourceRoot& input :
		roots)
	{
		const std::string stableKey = trimAscii(
			input.stableEntryKey);
		if (stableKey.empty() ||
			stableKey.find('\0') != std::string::npos)
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Error,
				"resource.catalog.supplemental_stable_key_invalid",
				"Supplemental resource root has no valid stable entry key and was skipped",
				stableKey,
				{},
				input.root);
			continue;
		}
		if (catalogContainsStableKey(catalog, stableKey))
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Error,
				"resource.catalog.supplemental_stable_key_conflict",
				"Supplemental stable entry key is already owned and this root was skipped",
				stableKey,
				{},
				input.root);
			continue;
		}

		fs::path root;
		if (!resolveExistingDirectory(
				catalog, input.root, root))
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Error,
				"resource.catalog.supplemental_root_unavailable",
				"Supplemental resource root is unavailable and was skipped",
				stableKey,
				{},
				input.root);
			continue;
		}
		if (catalogPackRootIsRegistered(catalog, root))
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.supplemental_duplicate_root",
				"Supplemental resource root is already registered and was skipped",
				stableKey,
				{},
				root);
			continue;
		}

		ResourceManifest manifest;
		bool manifestSanitized = false;
		const IniLoadStatus manifestStatus = readManifest(
			catalog,
			root,
			ManifestFileName,
			manifest,
			true,
			&manifestSanitized);
		if (manifestStatus != IniLoadStatus::Success)
		{
			if (manifestStatus == IniLoadStatus::BudgetExceeded)
			{
				addCatalogDiagnostic(
					catalog,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.supplemental_ini_budget_reached",
					"Supplemental INI byte budget was reached; remaining external roots were skipped while primary entries remain available",
					stableKey,
					{},
					root / ManifestFileName);
				break;
			}
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Error,
				"resource.catalog.supplemental_manifest_invalid",
				"Supplemental root manifest is invalid or unavailable and was skipped",
				stableKey,
				{},
				root / ManifestFileName);
			continue;
		}
		if (catalog.countedPackRoots.size() >=
			RuntimeResource::MaximumCatalogPackCount)
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.supplemental_pack_limit_reached",
				"Supplemental pack limit was reached; remaining external roots were skipped while primary entries remain available",
				stableKey,
				manifest.id,
				root);
			break;
		}
		if (!registerCatalogPackRoot(
				catalog, root, result, stableKey))
		{
			return false;
		}

		manifest.resourceRoot = normalizedRootString(root);
		CatalogPack pack;
		pack.stableKey = stableKey;
		pack.root = std::move(root);
		pack.manifestPath = pack.root / ManifestFileName;
		pack.sourceTag = input.sourceTag;
		pack.replacesPrimaryGameId = input.replacesPrimaryGameId;
		pack.manifest = std::move(manifest);
		catalog.packs.push_back(std::move(pack));
		if (manifestSanitized)
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.supplemental_manifest_sanitized",
				"Supplemental manifest lines containing embedded NUL bytes were ignored; usable fields remain available",
				stableKey,
				catalog.packs.back().manifest.id,
				catalog.packs.back().manifestPath);
		}
	}
	return true;
}

bool loadCatalog(
	const RuntimeResource::ResourceCatalogRequest& request,
	Catalog& catalog,
	RuntimeResource::ExactSelectionResult& result,
	const RuntimeResource::ResourceCatalogFileAccess* fileAccess =
		nullptr)
{
	if (!loadPrimaryCatalog(
			request.primaryCollectionRoot,
			catalog,
			result,
			fileAccess))
	{
		return false;
	}
	return appendSupplementalCatalogRoots(
		request.supplementalRoots, catalog, result);
}

std::vector<std::size_t> findPacksById(
	const Catalog& catalog, std::string_view id)
{
	const std::string key =
		foldAsciiCase(std::string(id));
	std::vector<std::size_t> matches;
	for (std::size_t index = 0; index < catalog.packs.size(); ++index)
	{
		if (!key.empty() &&
			foldAsciiCase(
				catalog.packs[index].manifest.id) == key)
		{
			matches.push_back(index);
		}
	}
	return matches;
}

void applyPrimaryGameIdReplacements(Catalog& catalog)
{
	std::map<std::string, std::vector<std::size_t>> ownersByGameId;
	for (std::size_t index = 0; index < catalog.packs.size(); ++index)
	{
		const std::string gameId = foldAsciiCase(
			trimAscii(catalog.packs[index].manifest.id));
		if (!gameId.empty())
		{
			ownersByGameId[gameId].push_back(index);
		}
	}

	std::set<std::size_t> removedPrimaryIndexes;
	for (const auto& owners : ownersByGameId)
	{
		std::vector<std::size_t> replacementIndexes;
		std::vector<std::size_t> primaryIndexes;
		for (const std::size_t index : owners.second)
		{
			const CatalogPack& pack = catalog.packs[index];
			if (pack.replacesPrimaryGameId)
			{
				replacementIndexes.push_back(index);
			}
			else if (pack.primaryCollectionEntry)
			{
				primaryIndexes.push_back(index);
			}
		}
		// Only one unambiguous writable copy may replace one packaged copy.
		// Any remaining duplicate owner is still reported as a normal conflict.
		if (owners.second.size() == 2 &&
			replacementIndexes.size() == 1 && primaryIndexes.size() == 1)
		{
			removedPrimaryIndexes.insert(primaryIndexes.front());
		}
	}
	if (removedPrimaryIndexes.empty())
	{
		return;
	}

	std::vector<CatalogPack> retainedPacks;
	retainedPacks.reserve(
		catalog.packs.size() - removedPrimaryIndexes.size());
	for (std::size_t index = 0; index < catalog.packs.size(); ++index)
	{
		if (removedPrimaryIndexes.find(index) ==
			removedPrimaryIndexes.end())
		{
			retainedPacks.push_back(std::move(catalog.packs[index]));
		}
	}
	catalog.packs = std::move(retainedPacks);
}

std::optional<std::size_t> findPackByRoot(
	const Catalog& catalog, const fs::path& root)
{
	for (std::size_t index = 0; index < catalog.packs.size(); ++index)
	{
		if (sameLogicalPath(
				catalog.packs[index].root, root))
		{
			return index;
		}
	}
	return std::nullopt;
}

struct RuntimePolicyEvaluation
{
	std::vector<bool> enabled;
};

std::string folderName(const fs::path& root)
{
	return root.filename().u8string();
}

std::string sanitizePortableSaveNamespace(std::string value)
{
	std::replace(value.begin(), value.end(), '\\', '/');
	std::string result;
	for (char character : value)
	{
		const unsigned char byte =
			static_cast<unsigned char>(character);
		if (byte >= 0x80 || std::isalnum(byte) ||
			character == '-' || character == '_')
		{
			result.push_back(character);
		}
		else if (character == '/' || character == ':' ||
			character == '.')
		{
			result.push_back('_');
		}
	}
	return result.empty() ? "default" : result;
}

std::string effectivePortableSaveNamespace(
	const CatalogPack& pack)
{
	std::string value = pack.manifest.saveNamespace;
	if (value.empty())
	{
		value = !pack.manifest.id.empty()
			? pack.manifest.id
			: folderName(pack.root);
	}
	return foldAsciiCase(
		sanitizePortableSaveNamespace(std::move(value)));
}

std::string declaredSaveNamespace(const CatalogPack& pack)
{
	if (!pack.manifest.saveNamespace.empty())
	{
		return pack.manifest.saveNamespace;
	}
	if (!pack.manifest.id.empty())
	{
		return pack.manifest.id;
	}
	return folderName(pack.root);
}

std::string canonicalUiProfile(std::string value);

void finalizeCatalogEntries(Catalog& catalog)
{
	std::map<std::string, std::vector<std::size_t>> idOwners;
	std::map<std::string, std::vector<std::size_t>> saveOwners;
	for (std::size_t index = 0; index < catalog.packs.size(); ++index)
	{
		CatalogPack& pack = catalog.packs[index];
		if (pack.stableKey.empty())
		{
			pack.stableKey = "path:" +
				pack.root.lexically_relative(
					catalog.collectionRoot).generic_u8string();
		}
		const std::string idKey =
			foldAsciiCase(trimAscii(pack.manifest.id));
		if (!idKey.empty())
		{
			idOwners[idKey].push_back(index);
		}
		saveOwners[effectivePortableSaveNamespace(pack)]
			.push_back(index);
	}

	for (const auto& owners : idOwners)
	{
		if (owners.second.size() < 2)
		{
			continue;
		}
		const std::string& winnerKey =
			catalog.packs[owners.second.front()].stableKey;
		for (std::size_t index : owners.second)
		{
			const CatalogPack& pack = catalog.packs[index];
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Error,
				"resource.catalog.duplicate_game_id",
				"Duplicate Game.Id conflicts with entry " + winnerKey +
					"; neither resource can be activated until the conflict is resolved",
				pack.stableKey,
				pack.manifest.id,
				pack.manifestPath);
		}
	}

	std::set<std::string> usedSaveNamespaces;
	for (const auto& owners : saveOwners)
	{
		const bool conflicted = owners.second.size() > 1;
		for (std::size_t position = 0;
			position < owners.second.size();
			++position)
		{
			CatalogPack& pack =
				catalog.packs[owners.second[position]];
			const std::string declared =
				declaredSaveNamespace(pack);
			std::string effective = declared;
			std::string key = foldAsciiCase(
				sanitizePortableSaveNamespace(effective));
			if (position > 0 ||
				usedSaveNamespaces.find(key) !=
					usedSaveNamespaces.end())
			{
				const std::string suffix =
					sanitizePortableSaveNamespace(
						pack.stableKey);
				effective = declared + "--" + suffix;
				key = foldAsciiCase(
					sanitizePortableSaveNamespace(effective));
				int disambiguator = 2;
				while (usedSaveNamespaces.find(key) !=
					usedSaveNamespaces.end())
				{
					effective = declared + "--" + suffix +
						"-" + std::to_string(disambiguator++);
					key = foldAsciiCase(
						sanitizePortableSaveNamespace(
							effective));
				}
				pack.saveNamespaceAdjusted = true;
			}
			pack.effectiveSaveNamespace = effective;
			usedSaveNamespaces.insert(std::move(key));
			if (conflicted)
			{
				addCatalogDiagnostic(
					catalog,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.save_namespace_conflict",
					pack.saveNamespaceAdjusted
						? "Save namespace conflict retained with effective namespace " +
							pack.effectiveSaveNamespace
						: "Save namespace conflict retained; first entry keeps namespace " +
							pack.effectiveSaveNamespace,
					pack.stableKey,
					pack.manifest.id,
					pack.manifestPath);
			}
		}
	}

	for (CatalogPack& pack : catalog.packs)
	{
		if (pack.effectiveSaveNamespace.empty())
		{
			pack.effectiveSaveNamespace =
				declaredSaveNamespace(pack);
		}
		const ModRelease::ModReleaseMetadata& release =
			pack.manifest.releaseMetadata;
		if (release.displayVersion.empty() ||
			release.releaseDate.empty())
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.release_metadata_defaulted",
				"Release version or date is missing; default labels will be shown",
				pack.stableKey,
				pack.manifest.id,
				pack.manifestPath);
		}
		if (release.coverPath.empty())
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.cover_defaulted",
				"Release cover is missing; the default cover will be shown",
				pack.stableKey,
				pack.manifest.id,
				pack.manifestPath);
		}
		if (release.descriptionFilePath.empty())
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.description_defaulted",
				"Release description is missing; the default description will be shown",
				pack.stableKey,
				pack.manifest.id,
				pack.manifestPath);
		}
		if (pack.manifest.uiProfile.empty() &&
			pack.manifest.uiBaseId.empty())
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.ui_defaulted",
				"UI configuration is missing; the Game.Type/default UI will be used",
				pack.stableKey,
				pack.manifest.id,
				pack.manifestPath);
		}
		if (!pack.manifest.uiProfile.empty() &&
			canonicalUiProfile(pack.manifest.uiProfile).empty())
		{
			addCatalogDiagnostic(
				catalog,
				RuntimeResource::CatalogDiagnosticSeverity::Warning,
				"resource.catalog.ui_profile_unsupported",
				"Unsupported UI.Profile will be ignored and the default UI will be used",
				pack.stableKey,
				pack.manifest.id,
				pack.manifestPath);
		}
	}
}

std::string canonicalUiProfile(std::string value)
{
	value = foldAsciiCase(trimAscii(std::move(value)));
	if (value == "jxqy2")
	{
		return "JXQY2";
	}
	if (value == "yycs")
	{
		return "YYCS";
	}
	if (value == "xjxqy")
	{
		return "XJXQY";
	}
	return {};
}

std::string uiProfileForGameType(int type)
{
	switch (type)
	{
	case 0:
		return "JXQY2";
	case 1:
		return "YYCS";
	case 2:
		return "XJXQY";
	default:
		return {};
	}
}

std::vector<std::size_t> findSelectablePacksById(
	const Catalog& catalog,
	const std::vector<bool>& enabled,
	std::string_view id,
	std::optional<std::size_t> excludedIndex = std::nullopt)
{
	const std::string key = foldAsciiCase(std::string(id));
	std::vector<std::size_t> matches;
	for (std::size_t index = 0; index < catalog.packs.size(); ++index)
	{
		if (index >= enabled.size() ||
			!enabled[index] ||
			(excludedIndex.has_value() &&
				excludedIndex.value() == index))
		{
			continue;
		}
		if (!key.empty() &&
			foldAsciiCase(catalog.packs[index].manifest.id) ==
				key)
		{
			matches.push_back(index);
		}
	}
	return matches;
}


bool rejectForDependencyDepth(
	std::size_t depth,
	const ResourceManifest& manifest,
	const fs::path& root,
	RuntimeResource::ExactSelectionResult& result)
{
	if (depth < RuntimeResource::MaximumCatalogDependencyDepth)
	{
		return false;
	}
	setFailure(
		result,
		RuntimeResource::ExactSelectionError::
			CatalogDependencyDepthExceeded,
		"editor_run.resource.dependency_depth_limit_exceeded",
		"Resource dependency graph depth limit was exceeded",
		manifest.id,
		root);
	return true;
}


bool evaluateRuntimePolicy(
	Catalog& catalog,
	RuntimeResource::ExactSelectionResult&,
	RuntimePolicyEvaluation& evaluation)
{
	// Discovery and capability resolution are intentionally separate. Invalid
	// entries remain visible for diagnostics. Exact selection rejects an
	// ambiguous active Game.Id, while optional ambiguous dependencies are
	// ignored so that the selected resource's local content stays usable.
	evaluation.enabled.assign(catalog.packs.size(), true);
	return true;
}

bool resolveEffectiveGameType(
	Catalog& catalog,
	const RuntimePolicyEvaluation& evaluation,
	std::size_t startIndex,
	RuntimeResource::ExactSelectionResult& result,
	int& type)
{
	struct ResolutionFrame
	{
		fs::path root;
		std::unique_ptr<ResourceManifest> manifest;
		std::optional<std::size_t> indexedPack;
		std::size_t depth = 1;
		std::vector<std::string> dependencyIds;
		std::size_t nextDependencyIndex = 0;
		std::string key;
		bool entered = false;
	};

	std::set<std::string> visiting;
	std::set<std::string> completed;
	std::vector<ResolutionFrame> stack;
	stack.reserve(RuntimeResource::MaximumCatalogDependencyDepth);
	const CatalogPack& active = catalog.packs[startIndex];
	stack.push_back({
		active.root,
		std::make_unique<ResourceManifest>(active.manifest),
		startIndex,
		1 });

	while (!stack.empty())
	{
		ResolutionFrame& frame = stack.back();
		if (!frame.entered)
		{
			if (frame.manifest->typeDefined)
			{
				type = frame.manifest->type;
				return true;
			}
			frame.key = frame.indexedPack.has_value()
				? "indexed:" +
					std::to_string(frame.indexedPack.value())
				: "path:" + normalizedRootString(frame.root);
			if (completed.find(frame.key) != completed.end() ||
				!visiting.insert(frame.key).second)
			{
				stack.pop_back();
				continue;
			}
			frame.dependencyIds =
				frame.manifest->getDependencyIds();
			frame.entered = true;
			continue;
		}

		if (frame.nextDependencyIndex <
			frame.dependencyIds.size())
		{
			const std::string dependencyId =
				frame.dependencyIds[
					frame.nextDependencyIndex++];
			const std::vector<std::size_t> matches =
				findSelectablePacksById(
					catalog,
					evaluation.enabled,
					dependencyId,
					frame.indexedPack);
			if (matches.empty())
			{
				continue;
			}
			if (matches.size() != 1)
			{
				continue;
			}
			const std::size_t dependencyIndex = matches.front();
			const fs::path dependencyRoot =
				catalog.packs[dependencyIndex].root;
			auto dependencyManifest =
				std::make_unique<ResourceManifest>(
					catalog.packs[dependencyIndex].manifest);
			if (rejectForDependencyDepth(
					frame.depth,
					*dependencyManifest,
					dependencyRoot,
					result))
			{
				return false;
			}
			stack.push_back({
				dependencyRoot,
				std::move(dependencyManifest),
				dependencyIndex,
				frame.depth + 1 });
			continue;
		}

		visiting.erase(frame.key);
		completed.insert(frame.key);
		stack.pop_back();
	}
	return false;
}

bool resolveEffectiveUiProfile(
	Catalog& catalog,
	const RuntimePolicyEvaluation& evaluation,
	std::size_t startIndex,
	RuntimeResource::ExactSelectionResult& result,
	std::string& profile)
{
	struct ResolutionFrame
	{
		std::size_t index = 0;
		std::size_t depth = 1;
		bool entered = false;
		bool childProcessed = false;
	};

	std::set<std::size_t> visiting;
	std::set<std::size_t> completed;
	std::vector<ResolutionFrame> stack;
	stack.reserve(RuntimeResource::MaximumCatalogDependencyDepth);
	stack.push_back({ startIndex, 1 });
	while (!stack.empty())
	{
		ResolutionFrame& frame = stack.back();
		if (!frame.entered)
		{
			if (frame.index >= catalog.packs.size() ||
				frame.index >= evaluation.enabled.size() ||
				!evaluation.enabled[frame.index] ||
				completed.find(frame.index) != completed.end() ||
				!visiting.insert(frame.index).second)
			{
				stack.pop_back();
				continue;
			}
			frame.entered = true;
			const std::string explicitProfile =
				catalog.packs[frame.index].manifest.uiProfile;
			if (!explicitProfile.empty())
			{
				profile = canonicalUiProfile(explicitProfile);
				if (!profile.empty())
				{
					return true;
				}
				visiting.erase(frame.index);
				completed.insert(frame.index);
				stack.pop_back();
				continue;
			}
		}

		const ResourceManifest manifest =
			catalog.packs[frame.index].manifest;
		if (!frame.childProcessed &&
			!manifest.uiBaseId.empty())
		{
			frame.childProcessed = true;
			const std::vector<std::size_t> matches =
				findSelectablePacksById(
					catalog,
					evaluation.enabled,
					manifest.uiBaseId,
					frame.index);
			if (matches.size() > 1)
			{
				setFailure(
					result,
					RuntimeResource::ExactSelectionError::
						UiDependencyAmbiguous,
					"resource.catalog.ui_dependency_id_ambiguous",
					"Declared UI dependency ID has multiple owners",
					manifest.uiBaseId,
					catalog.packs[frame.index].root);
				return false;
			}
			if (!matches.empty())
			{
				const std::size_t dependencyIndex = matches.front();
				if (rejectForDependencyDepth(
						frame.depth,
						catalog.packs[dependencyIndex].manifest,
						catalog.packs[dependencyIndex].root,
						result))
				{
					return false;
				}
				stack.push_back({
					dependencyIndex,
					frame.depth + 1 });
				continue;
			}
		}

		if (!manifest.uiBaseId.empty())
		{
			profile = canonicalUiProfile(manifest.uiBaseId);
			if (!profile.empty())
			{
				return true;
			}
		}
		profile = canonicalUiProfile(manifest.id);
		if (!profile.empty())
		{
			return true;
		}

		int resolvedType = manifest.type;
		if (manifest.typeDefined ||
			resolveEffectiveGameType(
				catalog,
				evaluation,
				frame.index,
				result,
				resolvedType))
		{
			profile = uiProfileForGameType(resolvedType);
			if (!profile.empty())
			{
				return true;
			}
		}
		if (!result.succeeded())
		{
			return false;
		}
		visiting.erase(frame.index);
		completed.insert(frame.index);
		stack.pop_back();
	}
	return false;
}

bool pathListContains(
	const std::vector<fs::path>& paths, const fs::path& path)
{
	return std::any_of(
		paths.begin(), paths.end(),
		[&path](const fs::path& candidate)
		{
			return sameLogicalPath(candidate, path);
		});
}

bool appendDependencyGraph(
	Catalog& catalog,
	const std::vector<bool>& enabled,
	const fs::path& root,
	const ResourceManifest& manifest,
	RuntimeResource::ExactSelectionResult& result,
	std::vector<fs::path>& visiting,
	std::vector<fs::path>& completed,
	std::size_t depth)
{
	struct TraversalFrame
	{
		fs::path root;
		std::unique_ptr<ResourceManifest> manifest;
		std::size_t depth = 1;
		std::vector<std::string> dependencyIds;
		std::size_t nextDependencyIndex = 0;
		bool entered = false;
	};

	std::vector<TraversalFrame> stack;
	stack.reserve(RuntimeResource::MaximumCatalogDependencyDepth);
	stack.push_back({
		root,
		std::make_unique<ResourceManifest>(manifest),
		depth });

	while (!stack.empty())
	{
		TraversalFrame& frame = stack.back();
		if (!frame.entered)
		{
			if (pathListContains(visiting, frame.root))
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.dependency_cycle_ignored",
					"Resource dependency cycle was ignored",
					{},
					frame.manifest->id,
					frame.root);
				stack.pop_back();
				continue;
			}
			if (pathListContains(completed, frame.root))
			{
				stack.pop_back();
				continue;
			}
			if (frame.depth >
				RuntimeResource::MaximumCatalogDependencyDepth)
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.dependency_depth_ignored",
					"Resource dependency branch exceeded the depth limit and was ignored",
					{},
					frame.manifest->id,
					frame.root);
				stack.pop_back();
				continue;
			}
			visiting.push_back(frame.root);
			frame.dependencyIds =
				frame.manifest->getDependencyIds();
			frame.entered = true;
			continue;
		}

		if (frame.nextDependencyIndex <
			frame.dependencyIds.size())
		{
			const std::string dependencyId =
				frame.dependencyIds[
					frame.nextDependencyIndex++];
			const std::vector<std::size_t> matches =
				findSelectablePacksById(
					catalog, enabled, dependencyId);
			if (matches.empty())
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.dependency_not_found",
					"Declared resource dependency ID was not found; local content remains available",
					{},
					dependencyId,
					frame.root);
				continue;
			}
			if (matches.size() != 1)
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.dependency_id_ambiguous",
					"Declared resource dependency ID has multiple owners; the ambiguous dependency was ignored",
					{},
					dependencyId,
					frame.root);
				continue;
			}

			const std::size_t dependencyIndex = matches.front();
			const fs::path dependencyRoot =
				catalog.packs[dependencyIndex].root;
			auto dependencyManifest =
				std::make_unique<ResourceManifest>(
					catalog.packs[dependencyIndex].manifest);
			if (!containsRoot(
					result.selection.orderedContentRoots,
					dependencyRoot))
			{
				result.selection.orderedContentRoots.push_back(
					{
						RuntimeResource::ContentRootKind::DependencyId,
						dependencyRoot,
						dependencyManifest->id
					});
			}
			if (!pathListContains(visiting, dependencyRoot) &&
				!pathListContains(completed, dependencyRoot) &&
				frame.depth >=
					RuntimeResource::MaximumCatalogDependencyDepth)
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.dependency_depth_ignored",
					"Resource dependency branch exceeded the depth limit and was ignored",
					catalog.packs[dependencyIndex].stableKey,
					dependencyManifest->id,
					dependencyRoot);
				continue;
			}
			stack.push_back({
				dependencyRoot,
				std::move(dependencyManifest),
				frame.depth + 1 });
			continue;
		}

		visiting.pop_back();
		completed.push_back(frame.root);
		stack.pop_back();
	}
	return true;
}
bool appendUniqueUiRoot(
	std::vector<fs::path>& roots, const fs::path& root)
{
	if (pathListContains(roots, root))
	{
		return false;
	}
	roots.push_back(root);
	return true;
}

// Own each manifest copy on the heap so the supported maximum dependency
// depth does not exhaust the Windows Debug thread stack.
bool appendUiFallbackGraph(
	Catalog& catalog,
	const std::vector<bool>& enabled,
	fs::path root,
	std::unique_ptr<ResourceManifest> manifest,
	RuntimeResource::ExactSelectionResult& result,
	std::vector<fs::path>& visiting,
	std::vector<fs::path>& completed,
	std::size_t depth)
{
	struct TraversalFrame
	{
		fs::path root;
		std::unique_ptr<ResourceManifest> manifest;
		std::size_t depth = 1;
		std::vector<std::string> dependencyIds;
		std::size_t nextDependencyIndex = 0;
		std::optional<std::size_t> currentIndex;
		bool hasExplicitUiBase = false;
		bool entered = false;
	};

	std::vector<TraversalFrame> stack;
	stack.reserve(RuntimeResource::MaximumCatalogDependencyDepth);
	stack.push_back({
		std::move(root),
		std::move(manifest),
		depth });

	while (!stack.empty())
	{
		TraversalFrame& frame = stack.back();
		if (!frame.entered)
		{
			if (pathListContains(visiting, frame.root))
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.ui_dependency_cycle_ignored",
					"Resource UI dependency cycle was ignored",
					{},
					frame.manifest->id,
					frame.root);
				stack.pop_back();
				continue;
			}
			if (pathListContains(completed, frame.root))
			{
				stack.pop_back();
				continue;
			}
			if (frame.depth >
				RuntimeResource::MaximumCatalogDependencyDepth)
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.ui_dependency_depth_ignored",
					"Resource UI dependency branch exceeded the depth limit and was ignored",
					{},
					frame.manifest->id,
					frame.root);
				stack.pop_back();
				continue;
			}
			visiting.push_back(frame.root);
			frame.hasExplicitUiBase =
				!trimAscii(frame.manifest->uiBaseId).empty();
			frame.dependencyIds = frame.hasExplicitUiBase
				? std::vector<std::string>{
					frame.manifest->uiBaseId }
				: frame.manifest->getDependencyIds();
			frame.currentIndex =
				findPackByRoot(catalog, frame.root);
			frame.entered = true;
			continue;
		}

		if (frame.nextDependencyIndex <
			frame.dependencyIds.size())
		{
			const std::string dependencyId =
				frame.dependencyIds[
					frame.nextDependencyIndex++];
			const std::vector<std::size_t> matches =
				findSelectablePacksById(
					catalog,
					enabled,
					dependencyId,
					frame.currentIndex);
			if (matches.empty())
			{
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					frame.hasExplicitUiBase
						? "resource.catalog.ui_dependency_not_found"
						: "resource.catalog.dependency_not_found",
					frame.hasExplicitUiBase
						? "Declared UI dependency ID was not found; the default UI will be used"
						: "Declared resource dependency ID was not found; local UI remains available",
					{},
					dependencyId,
					frame.root);
				continue;
			}
			if (matches.size() != 1)
			{
				if (frame.hasExplicitUiBase)
				{
					setFailure(
						result,
						RuntimeResource::ExactSelectionError::
							UiDependencyAmbiguous,
						"resource.catalog.ui_dependency_id_ambiguous",
						"Declared UI dependency ID has multiple owners",
						dependencyId,
						frame.root);
					return false;
				}
				addSelectionDiagnostic(
					result,
					RuntimeResource::CatalogDiagnosticSeverity::Warning,
					"resource.catalog.dependency_id_ambiguous",
					"Declared resource dependency ID has multiple owners; the ambiguous UI fallback was ignored",
					{},
					dependencyId,
					frame.root);
				continue;
			}

			const std::size_t dependencyIndex = matches.front();
			const fs::path dependencyRoot =
				catalog.packs[dependencyIndex].root;
			auto dependencyManifest =
				std::make_unique<ResourceManifest>(
					catalog.packs[dependencyIndex].manifest);
			appendUniqueUiRoot(
				result.selection.orderedUiFallbackRoots,
				dependencyRoot);
			stack.push_back({
				dependencyRoot,
				std::move(dependencyManifest),
				frame.depth + 1 });
			continue;
		}

		visiting.pop_back();
		completed.push_back(frame.root);
		stack.pop_back();
	}
	return true;
}

}

namespace RuntimeResource
{
namespace
{
ExactSelectionResult resolveCatalogSelection(
	const RuntimeResource::ResourceCatalogRequest& request,
	std::string_view requestedValue,
	bool selectByStableEntryKey,
	const ResourceCatalogFileAccess* fileAccess)
{
	ExactSelectionResult result;
	try
	{
		Catalog catalog;
		if (!loadCatalog(
				request,
				catalog,
				result,
				fileAccess))
		{
			return result;
		}

		applyPrimaryGameIdReplacements(catalog);
		finalizeCatalogEntries(catalog);

		RuntimePolicyEvaluation policy;
		if (!evaluateRuntimePolicy(catalog, result, policy))
		{
			return result;
		}

		const std::string requested(requestedValue);
		std::vector<std::size_t> matches;
		if (selectByStableEntryKey)
		{
			const std::string requestedKey =
				foldAsciiCase(trimAscii(requested));
			for (std::size_t index = 0;
				index < catalog.packs.size();
				++index)
			{
				if (foldAsciiCase(
						catalog.packs[index].stableKey) ==
					requestedKey)
				{
					matches.push_back(index);
				}
			}
		}
		else
		{
			matches = findPacksById(catalog, requested);
		}
		if (matches.empty())
		{
			// When no selectable entry remains, retain the per-entry/index
			// diagnostics that explain why discovery skipped it.
			result.diagnostics = catalog.diagnostics;
			setFailure(
				result,
				ExactSelectionError::ActiveResourcePackIdNotFound,
				"editor_run.resource.id_not_found",
				selectByStableEntryKey
					? "Requested stable resource entry key was not found"
					: "Requested active resource pack ID was not found",
				requested,
				catalog.collectionRoot);
			return result;
		}
		if (matches.size() > 1)
		{
			result.diagnostics = catalog.diagnostics;
			setFailure(
				result,
				ExactSelectionError::ActiveResourcePackIdAmbiguous,
				"resource.catalog.active_id_ambiguous",
				"Requested Game.Id has multiple owners",
				requested,
				catalog.collectionRoot);
			return result;
		}

		const std::size_t activeIndex = matches.front();
		const std::vector<std::size_t> idOwners = findPacksById(
			catalog, catalog.packs[activeIndex].manifest.id);
		if (idOwners.size() > 1)
		{
			result.diagnostics = catalog.diagnostics;
			setFailure(
				result,
				ExactSelectionError::ActiveResourcePackIdAmbiguous,
				"resource.catalog.active_id_ambiguous",
				"Selected resource Game.Id has multiple owners",
				catalog.packs[activeIndex].manifest.id,
				catalog.collectionRoot);
			return result;
		}
		for (const CatalogDiagnostic& diagnostic :
			catalog.diagnostics)
		{
			if (diagnostic.stableEntryKey.empty() ||
				foldAsciiCase(
					diagnostic.stableEntryKey) ==
					foldAsciiCase(
						catalog.packs[activeIndex].stableKey))
			{
				result.diagnostics.push_back(diagnostic);
			}
		}
		// Keep active values independent from traversal state.
		const fs::path activeRoot =
			catalog.packs[activeIndex].root;
		const ResourceManifest sourceActiveManifest =
			catalog.packs[activeIndex].manifest;
		ResourceManifest materializedManifest =
			sourceActiveManifest;
		if (!materializedManifest.typeDefined)
		{
			int resolvedType = materializedManifest.type;
			if (!resolveEffectiveGameType(
					catalog,
					policy,
					activeIndex,
					result,
					resolvedType))
			{
				if (!result.succeeded())
				{
					return result;
				}
				resolvedType = 0;
				addSelectionDiagnostic(
					result,
					CatalogDiagnosticSeverity::Warning,
					"resource.catalog.game_type_defaulted",
					"Game.Type could not be inherited; the JXQY2 default was used",
					catalog.packs[activeIndex].stableKey,
					sourceActiveManifest.id,
					activeRoot);
			}
			materializedManifest.type = resolvedType;
			materializedManifest.typeDefined = true;
		}
		if (!materializedManifest.uiProfile.empty())
		{
			const std::string canonicalProfile =
				canonicalUiProfile(
					materializedManifest.uiProfile);
			if (canonicalProfile.empty())
			{
				addSelectionDiagnostic(
					result,
					CatalogDiagnosticSeverity::Warning,
					"resource.catalog.ui_profile_defaulted",
					"Unsupported UI.Profile was ignored; a default UI profile was selected",
					catalog.packs[activeIndex].stableKey,
					sourceActiveManifest.id,
					activeRoot);
				materializedManifest.uiProfile.clear();
			}
			else
			{
				materializedManifest.uiProfile = canonicalProfile;
			}
		}
		if (materializedManifest.uiProfile.empty())
		{
			std::string resolvedProfile;
			if (!materializedManifest.uiBaseId.empty() &&
				!resolveEffectiveUiProfile(
					catalog,
					policy,
					activeIndex,
					result,
					resolvedProfile))
			{
				if (!result.succeeded())
				{
					return result;
				}
			}
			if (resolvedProfile.empty())
			{
				resolvedProfile =
					uiProfileForGameType(
						materializedManifest.type);
			}
			if (resolvedProfile.empty())
			{
				resolvedProfile = "JXQY2";
			}
			materializedManifest.uiProfile =
				std::move(resolvedProfile);
		}

		result.selection.assetsCollectionRoot =
			catalog.collectionRoot;
		result.selection.activeResourceRoot = activeRoot;
		result.selection.activeManifest =
			std::move(materializedManifest);
		result.selection.canonicalActiveResourcePackId =
			sourceActiveManifest.id;
		result.selection.stableActiveEntryKey =
			catalog.packs[activeIndex].stableKey;
		result.selection.preferLocalUi =
			result.selection.activeManifest.preferLocalUi;
		result.selection.effectiveSaveNamespace =
			catalog.packs[activeIndex].effectiveSaveNamespace;
		result.selection.orderedContentRoots.push_back(
			{
				ContentRootKind::Active,
				activeRoot,
				sourceActiveManifest.id
			});

		std::vector<fs::path> visiting;
		std::vector<fs::path> completed;
		if (!appendDependencyGraph(
				catalog,
				policy.enabled,
				activeRoot,
				sourceActiveManifest,
				result,
				visiting,
				completed,
				1))
		{
			return result;
		}

		visiting.clear();
		completed.clear();
		if (!appendUiFallbackGraph(
				catalog,
				policy.enabled,
				activeRoot,
				std::make_unique<ResourceManifest>(
					sourceActiveManifest),
				result,
				visiting,
				completed,
				1))
		{
			return result;
		}

		const bool commonPathIsExplicit =
			!trimAscii(catalog.collectionCommonPath).empty();
		const fs::path requestedCommonRoot =
			commonPathIsExplicit
				? resolveCollectionRelativeHostPath(
					catalog.collectionRoot,
					catalog.collectionCommonPath)
				: catalog.collectionRoot / "common";

		fs::path commonRoot;
		if (resolveExistingDirectory(
				catalog,
				requestedCommonRoot,
				commonRoot,
				commonPathIsExplicit ||
					catalog.fileAccess != nullptr))
		{
			result.selection.commonResourceRoot = commonRoot;
			if (!containsRoot(
					result.selection.orderedContentRoots,
					commonRoot))
			{
				result.selection.orderedContentRoots.push_back(
					{
						ContentRootKind::Common,
						commonRoot,
						{}
					});
			}
		}
		else if (commonPathIsExplicit)
		{
			addSelectionDiagnostic(
				result,
				CatalogDiagnosticSeverity::Warning,
				"resource.catalog.common_root_unavailable",
				"Configured collection Common resource root is unavailable; local content remains available",
				catalog.packs[activeIndex].stableKey,
				sourceActiveManifest.id,
				requestedCommonRoot);
		}

		return result;
	}
	catch (...)
	{
		setFailure(
			result,
			ExactSelectionError::ResourceConfigurationInvalid,
			"editor_run.resource.configuration_invalid",
			"Resource catalog could not be represented on this platform",
			{},
			request.primaryCollectionRoot);
		return result;
	}
}
}

ResourceCatalogSnapshotResult loadResourceCatalogSnapshotImpl(
	const ResourceCatalogRequest& request,
	const ResourceCatalogFileAccess* fileAccess)
{
	ResourceCatalogSnapshotResult snapshotResult;
	ExactSelectionResult loadResult;
	try
	{
		Catalog catalog;
		if (!loadCatalog(
				request,
				catalog,
				loadResult,
				fileAccess))
		{
			snapshotResult.error = loadResult.error;
			snapshotResult.diagnosticCode =
				loadResult.diagnosticCode;
			snapshotResult.hostPath = loadResult.hostPath;
			snapshotResult.message = loadResult.message;
			return snapshotResult;
		}

		applyPrimaryGameIdReplacements(catalog);
		finalizeCatalogEntries(catalog);
		ResourceCatalogSnapshot& snapshot =
			snapshotResult.snapshot;
		snapshot.collectionRoot = catalog.collectionRoot;
		snapshot.rootManifestDeclared =
			catalog.rootManifestDeclared;
		snapshot.updateSourceUrl =
			catalog.collectionUpdateSourceUrl;
		snapshot.resourceCatalogUrl =
			catalog.collectionResourceCatalogUrl;
		snapshot.applicationCatalogUrl =
			catalog.collectionApplicationCatalogUrl;
		snapshot.diagnostics = catalog.diagnostics;

		if (!catalog.collectionCommonPath.empty())
		{
			snapshot.commonResourceRoot =
				resolveCollectionRelativeHostPath(
					catalog.collectionRoot,
					catalog.collectionCommonPath);
		}
		else
		{
			const fs::path conventionalCommon =
				catalog.collectionRoot / "common";
			fs::path resolvedCommon;
			if (resolveExistingDirectory(
					catalog,
					conventionalCommon,
					resolvedCommon,
					catalog.fileAccess != nullptr))
			{
				snapshot.commonResourceRoot =
					std::move(resolvedCommon);
			}
		}

		snapshot.entries.reserve(catalog.packs.size());
		for (std::size_t index = 0;
			index < catalog.packs.size();
			++index)
		{
			const CatalogPack& pack = catalog.packs[index];
			ResourceCatalogEntry entry;
			entry.stableKey = pack.stableKey;
			entry.root = pack.root;
			entry.manifestPath = pack.manifestPath;
			entry.sourceTag = pack.sourceTag;
			entry.manifest = pack.manifest;
			entry.effectiveSaveNamespace =
				pack.effectiveSaveNamespace;
			entry.saveNamespaceAdjusted =
				pack.saveNamespaceAdjusted;
			entry.discoveryOrder = index;
			snapshot.entries.push_back(std::move(entry));
		}
		return snapshotResult;
	}
	catch (...)
	{
		snapshotResult.error =
			ExactSelectionError::ResourceConfigurationInvalid;
		snapshotResult.diagnosticCode =
			"resource.catalog.configuration_invalid";
		snapshotResult.hostPath =
			request.primaryCollectionRoot;
		snapshotResult.message =
			"Resource catalog could not be represented on this platform";
		return snapshotResult;
	}
}

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const std::filesystem::path& assetsCollectionRoot)
{
	ResourceCatalogRequest request;
	request.primaryCollectionRoot = assetsCollectionRoot;
	return loadResourceCatalogSnapshotImpl(request, nullptr);
}

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const std::filesystem::path& assetsCollectionRoot,
	const ResourceCatalogFileAccess& fileAccess)
{
	ResourceCatalogRequest request;
	request.primaryCollectionRoot = assetsCollectionRoot;
	return loadResourceCatalogSnapshotImpl(request, &fileAccess);
}

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const ResourceCatalogRequest& request)
{
	return loadResourceCatalogSnapshotImpl(request, nullptr);
}

ResourceCatalogSnapshotResult loadResourceCatalogSnapshot(
	const ResourceCatalogRequest& request,
	const ResourceCatalogFileAccess& fileAccess)
{
	return loadResourceCatalogSnapshotImpl(request, &fileAccess);
}

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view stableEntryKey)
{
	ResourceCatalogRequest request;
	request.primaryCollectionRoot = assetsCollectionRoot;
	return resolveCatalogSelection(
		request,
		stableEntryKey,
		true,
		nullptr);
}

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view stableEntryKey,
	const ResourceCatalogFileAccess& fileAccess)
{
	ResourceCatalogRequest request;
	request.primaryCollectionRoot = assetsCollectionRoot;
	return resolveCatalogSelection(
		request,
		stableEntryKey,
		true,
		&fileAccess);
}

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const ResourceCatalogRequest& request,
	std::string_view stableEntryKey)
{
	return resolveCatalogSelection(
		request,
		stableEntryKey,
		true,
		nullptr);
}

ExactSelectionResult resolveResourceCatalogEntrySelection(
	const ResourceCatalogRequest& request,
	std::string_view stableEntryKey,
	const ResourceCatalogFileAccess& fileAccess)
{
	return resolveCatalogSelection(
		request,
		stableEntryKey,
		true,
		&fileAccess);
}

ExactSelectionResult resolveExactResourceSelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view requestedResourcePackId)
{
	ResourceCatalogRequest request;
	request.primaryCollectionRoot = assetsCollectionRoot;
	return resolveCatalogSelection(
		request,
		requestedResourcePackId,
		false,
		nullptr);
}

ExactSelectionResult resolveExactResourceSelection(
	const std::filesystem::path& assetsCollectionRoot,
	std::string_view requestedResourcePackId,
	const ResourceCatalogFileAccess& fileAccess)
{
	ResourceCatalogRequest request;
	request.primaryCollectionRoot = assetsCollectionRoot;
	return resolveCatalogSelection(
		request,
		requestedResourcePackId,
		false,
		&fileAccess);
}

ExactSelectionResult resolveExactResourceSelection(
	const ResourceCatalogRequest& request,
	std::string_view requestedResourcePackId)
{
	return resolveCatalogSelection(
		request,
		requestedResourcePackId,
		false,
		nullptr);
}

ExactSelectionResult resolveExactResourceSelection(
	const ResourceCatalogRequest& request,
	std::string_view requestedResourcePackId,
	const ResourceCatalogFileAccess& fileAccess)
{
	return resolveCatalogSelection(
		request,
		requestedResourcePackId,
		false,
		&fileAccess);
}
}
