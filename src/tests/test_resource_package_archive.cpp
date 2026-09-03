#include "../Update/ResourcePackageArchive.h"
#include "../Update/ResourceDownloadPreparation.h"
#include "../Update/ResourceInstallTransaction.h"
#include "../Update/ArtifactChecksum.h"
#include "../Resource/ResourceManifest.h"
#include "TestTemporaryDirectory.h"

extern "C"
{
#include "miniz.h"
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct ZipEntry
{
	std::string path;
	std::string bytes;
	mz_uint compression = MZ_BEST_COMPRESSION;
};

class TemporaryTree
{
public:
	TemporaryTree()
		: root(makeUniqueTestDirectory("jxqy-resource-package-archive"))
	{
		std::error_code error;
		if (!std::filesystem::create_directory(root, error) || error)
		{
			root.clear();
		}
	}

	~TemporaryTree()
	{
		if (!root.empty())
		{
			std::error_code error;
			std::filesystem::remove_all(root, error);
		}
	}

	std::filesystem::path root;
};

int failureCount = 0;
std::uint64_t caseCounter = 0;

void expect(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		failureCount++;
	}
}

std::string validManifest()
{
	return
		"[Game]\n"
		"Id=YYCS\n"
		"Name=Moon Shadow\n"
		"Version=1.0\n"
		"\n"
		"[Release]\n"
		"MinimumEngineVersion=2.0.0\n"
		"InstalledArtifactCrc32=deadbeef\n"
		"InstalledIncrementalArtifactCrc32=feedface\n"
		"\n"
		"[Resource]\n"
		"DependencyId=JXQY2\n";
}

std::string downloadManifest(
	const std::string& gameId,
	const std::string& version,
	const std::string& dependencyGameId)
{
	return
		"[Game]\n"
		"Id=" + gameId + "\n"
		"Name=" + gameId + "\n"
		"Version=" + version + "\n"
		"\n"
		"[Release]\n"
		"MinimumEngineVersion=2.0.0\n"
		"\n"
		"[Resource]\n"
		"DependencyId=" + dependencyGameId + "\n";
}

bool writeZip(
	const std::filesystem::path& path,
	const std::vector<ZipEntry>& entries)
{
	mz_zip_archive archive;
	mz_zip_zero_struct(&archive);
	const std::string pathText = path.string();
	if (!mz_zip_writer_init_file_v2(
			&archive, pathText.c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64))
	{
		return false;
	}
	bool succeeded = true;
	static const char Empty = 0;
	for (const ZipEntry& entry : entries)
	{
		const void* bytes = entry.bytes.empty()
			? static_cast<const void*>(&Empty)
			: static_cast<const void*>(entry.bytes.data());
		if (!mz_zip_writer_add_mem(
				&archive,
				entry.path.c_str(),
				bytes,
				entry.bytes.size(),
				entry.compression))
		{
			succeeded = false;
			break;
		}
	}
	if (succeeded)
	{
		succeeded = mz_zip_writer_finalize_archive(&archive) != 0;
	}
	if (!mz_zip_writer_end(&archive))
	{
		succeeded = false;
	}
	return succeeded;
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::vector<std::uint8_t>(
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

std::string readText(const std::filesystem::path& path)
{
	const std::vector<std::uint8_t> bytes = readBytes(path);
	return std::string(bytes.begin(), bytes.end());
}

bool writeBytes(
	const std::filesystem::path& path,
	const std::vector<std::uint8_t>& bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output)
	{
		return false;
	}
	output.write(
		reinterpret_cast<const char*>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(output);
}

std::uint16_t readLe16(
	const std::vector<std::uint8_t>& bytes,
	std::size_t offset)
{
	return static_cast<std::uint16_t>(bytes[offset]) |
		(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

void writeLe32(
	std::vector<std::uint8_t>& bytes,
	std::size_t offset,
	std::uint32_t value)
{
	bytes[offset] = static_cast<std::uint8_t>(value);
	bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
	bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
	bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

bool patchCentralEntryAsUnixSymlink(
	const std::filesystem::path& path,
	const std::string& entryName)
{
	std::vector<std::uint8_t> bytes = readBytes(path);
	for (std::size_t offset = 0; offset + 46 <= bytes.size(); ++offset)
	{
		if (bytes[offset] != 0x50 || bytes[offset + 1] != 0x4B ||
			bytes[offset + 2] != 0x01 || bytes[offset + 3] != 0x02)
		{
			continue;
		}
		const std::size_t nameLength = readLe16(bytes, offset + 28);
		if (offset + 46 + nameLength > bytes.size())
		{
			return false;
		}
		const std::string name(
			reinterpret_cast<const char*>(bytes.data() + offset + 46),
			nameLength);
		if (name != entryName)
		{
			continue;
		}
		bytes[offset + 4] = 20;
		bytes[offset + 5] = 3;
		writeLe32(bytes, offset + 38, 0120777U << 16);
		return writeBytes(path, bytes);
	}
	return false;
}

bool replaceArchiveEntryName(
	const std::filesystem::path& path,
	const std::string& originalName,
	const std::string& replacementName)
{
	if (originalName.size() != replacementName.size())
	{
		return false;
	}
	std::vector<std::uint8_t> bytes = readBytes(path);
	std::size_t replacementCount = 0;
	for (std::size_t offset = 0; offset + 30 <= bytes.size(); ++offset)
	{
		std::size_t nameLengthOffset = 0;
		std::size_t nameOffset = 0;
		if (bytes[offset] == 0x50 && bytes[offset + 1] == 0x4B &&
			bytes[offset + 2] == 0x03 && bytes[offset + 3] == 0x04)
		{
			nameLengthOffset = offset + 26;
			nameOffset = offset + 30;
		}
		else if (offset + 46 <= bytes.size() &&
			bytes[offset] == 0x50 && bytes[offset + 1] == 0x4B &&
			bytes[offset + 2] == 0x01 && bytes[offset + 3] == 0x02)
		{
			nameLengthOffset = offset + 28;
			nameOffset = offset + 46;
		}
		else
		{
			continue;
		}
		const std::size_t nameLength = readLe16(bytes, nameLengthOffset);
		if (nameOffset + nameLength > bytes.size())
		{
			return false;
		}
		const std::string name(
			reinterpret_cast<const char*>(bytes.data() + nameOffset),
			nameLength);
		if (name != originalName)
		{
			continue;
		}
		std::copy(
			replacementName.begin(),
			replacementName.end(),
			bytes.begin() + static_cast<std::ptrdiff_t>(nameOffset));
		replacementCount++;
	}
	return replacementCount == 2 && writeBytes(path, bytes);
}

bool corruptStoredEntryData(
	const std::filesystem::path& path,
	const std::string& entryName)
{
	std::vector<std::uint8_t> bytes = readBytes(path);
	for (std::size_t offset = 0; offset + 30 <= bytes.size(); ++offset)
	{
		if (bytes[offset] != 0x50 || bytes[offset + 1] != 0x4B ||
			bytes[offset + 2] != 0x03 || bytes[offset + 3] != 0x04)
		{
			continue;
		}
		const std::size_t nameLength = readLe16(bytes, offset + 26);
		const std::size_t extraLength = readLe16(bytes, offset + 28);
		const std::size_t nameOffset = offset + 30;
		const std::size_t dataOffset = nameOffset + nameLength + extraLength;
		if (dataOffset >= bytes.size() || nameOffset + nameLength > bytes.size())
		{
			return false;
		}
		const std::string name(
			reinterpret_cast<const char*>(bytes.data() + nameOffset),
			nameLength);
		if (name != entryName)
		{
			continue;
		}
		bytes[dataOffset] ^= 0x40;
		return writeBytes(path, bytes);
	}
	return false;
}

OnlineUpdate::ResourcePackage expectedPackage(
	const std::filesystem::path& archivePath)
{
	OnlineUpdate::ResourcePackage package;
	package.gameId = "YYCS";
	package.versionText = "1.0";
	package.minimumEngineVersionText = "2.0.0";
	package.artifactPath = "resources/yycs.zip";
	package.dependencyGameIds = { "JXQY2" };
	std::uint32_t checksum = 0;
	if (OnlineUpdate::calculateFileCrc32(
			archivePath, checksum, package.artifactSize))
	{
		package.crc32Hex = OnlineUpdate::crc32ToLowerHex(checksum);
	}
	return package;
}

OnlineUpdate::CommonPackage expectedCommonPackage(
	const std::filesystem::path& archivePath)
{
	OnlineUpdate::CommonPackage package;
	package.versionText = "1.0";
	package.artifactPath = "resources/common.zip";
	std::uint32_t checksum = 0;
	if (OnlineUpdate::calculateFileCrc32(
			archivePath, checksum, package.artifactSize))
	{
		package.crc32Hex = OnlineUpdate::crc32ToLowerHex(checksum);
	}
	return package;
}

OnlineUpdate::ProgramPackage expectedDesktopProgramPackage(
	const std::filesystem::path& archivePath,
	const std::string& target = "windows")
{
	OnlineUpdate::ProgramPackage package;
	package.target = target;
	package.versionText = "2.1.0";
	package.version = ModRelease::parseSemanticVersion(
		package.versionText).version;
	package.artifactPath = "program/" + target + "-2.1.0.zip";
	std::uint32_t checksum = 0;
	if (OnlineUpdate::calculateFileCrc32(
			archivePath, checksum, package.artifactSize))
	{
		package.crc32Hex = OnlineUpdate::crc32ToLowerHex(checksum);
	}
	return package;
}

OnlineUpdate::ResourcePackage downloadPackage(
	const std::filesystem::path& archivePath,
	const std::string& gameId,
	const std::string& version,
	const std::string& artifactPath,
	const std::vector<std::string>& dependencies)
{
	OnlineUpdate::ResourcePackage package;
	package.gameId = gameId;
	package.displayName = gameId;
	package.versionText = version;
	package.minimumEngineVersionText = "2.0.0";
	const ModRelease::SemanticVersionParseResult minimumEngineVersion =
		ModRelease::parseSemanticVersion(package.minimumEngineVersionText);
	if (minimumEngineVersion.succeeded())
	{
		package.minimumEngineVersion = minimumEngineVersion.version;
	}
	package.artifactPath = artifactPath;
	package.dependencyGameIds = dependencies;
	std::uint32_t checksum = 0;
	if (OnlineUpdate::calculateFileCrc32(
			archivePath, checksum, package.artifactSize))
	{
		package.crc32Hex = OnlineUpdate::crc32ToLowerHex(checksum);
	}
	return package;
}

std::filesystem::path nextPath(
	const TemporaryTree& tree,
	const std::string& prefix,
	const std::string& extension = {})
{
	return tree.root /
		(prefix + "-" + std::to_string(caseCounter++) + extension);
}

OnlineUpdate::ResourcePackageArchiveResult prepare(
	const TemporaryTree& tree,
	const std::vector<ZipEntry>& entries,
	std::filesystem::path& destination,
	OnlineUpdate::ResourcePackage* packageOutput = nullptr,
	const OnlineUpdate::ResourcePackageArchiveLimits& limits = {})
{
	const std::filesystem::path archive =
		nextPath(tree, "archive", ".zip");
	expect(writeZip(archive, entries), "test ZIP is created");
	OnlineUpdate::ResourcePackage package = expectedPackage(archive);
	if (packageOutput != nullptr)
	{
		*packageOutput = package;
	}
	destination = nextPath(tree, "staging");
	return OnlineUpdate::prepareResourcePackageArchive(
		package, archive, destination, limits);
}

void testValidPackage(const TemporaryTree& tree)
{
	std::filesystem::path destination;
	const std::string content = u8"地图脚本";
	OnlineUpdate::ResourcePackage package;
	const OnlineUpdate::ResourcePackageArchiveResult result = prepare(
		tree,
		{
			{ "game_profile.ini", validManifest() },
			{ u8"script/地图/", "" },
			{ u8"script/地图/测试.txt", content }
		},
		destination,
		&package);
	expect(result.succeeded(),
		"valid forced-ZIP64 package extracts and validates");
	expect(result.fileCount == 2 &&
		result.uncompressedBytes == validManifest().size() + content.size(),
		"valid package reports file count and uncompressed size");
	expect(std::filesystem::is_regular_file(
		destination / std::filesystem::u8path(u8"script/地图/测试.txt")),
		"UTF-8 resource path is extracted under staging");
	expect(readBytes(
		destination / std::filesystem::u8path(u8"script/地图/测试.txt")) ==
		std::vector<std::uint8_t>(content.begin(), content.end()),
		"UTF-8 resource file keeps its original bytes");
	ResourceManifest installedManifest;
	const std::string installedManifestText =
		readText(destination / "game_profile.ini");
	expect(installedManifest.loadFromBuffer(
			installedManifestText.data(),
			static_cast<int>(installedManifestText.size())) &&
		installedManifest.releaseMetadata.installedArtifactCrc32 ==
			package.crc32Hex &&
		installedManifest.releaseMetadata.
			installedIncrementalArtifactCrc32.empty(),
		"a full resource install records its ZIP and clears a stale"
		" incremental receipt");
}

void testImportedResourcePackage(const TemporaryTree& tree)
{
	const std::filesystem::path archive =
		nextPath(tree, "imported-resource", ".zip");
	const std::string manifest =
		"[Game]\n"
		"Id=IMPORTED_GAME\n"
		"Name=Imported Game\n"
		"Author=Importer\n"
		"Version=1.2.3\n"
		"\n"
		"[Release]\n"
		"MinimumEngineVersion=2.0.0\n"
		"InstalledArtifactCrc32=deadbeef\n"
		"InstalledIncrementalArtifactCrc32=feedface\n"
		"InstalledIncrementalChainCrc32s=11111111,22222222\n"
		"\n"
		"[Resource]\n"
		"DependencyId=JXQY2\n";
	expect(writeZip(archive,
		{
			{ "game_profile.ini", manifest },
			{ "script/start.txt", "start" }
		}), "imported resource ZIP is created");
	const std::filesystem::path destination =
		nextPath(tree, "imported-resource-staging");
	const OnlineUpdate::ImportedResourcePackageArchiveResult result =
		OnlineUpdate::prepareImportedResourcePackageArchive(
			archive, destination);
	ResourceManifest installedManifest;
	const std::string installedManifestText =
		readText(destination / "game_profile.ini");
	expect(result.succeeded() &&
		result.package.gameId == "IMPORTED_GAME" &&
		!result.package.common &&
		!result.package.resourceOnly &&
		result.package.displayName == "Imported Game" &&
		result.package.author == "Importer" &&
		result.package.displayVersion == "1.2.3" &&
		result.package.minimumEngineVersion == "2.0.0" &&
		result.package.dependencyGameIds ==
			std::vector<std::string>{ "JXQY2" } &&
		result.package.artifactSize == std::filesystem::file_size(archive) &&
		OnlineUpdate::isValidCrc32Hex(result.package.artifactCrc32),
		"standalone import reads required identity and release metadata");
	expect(installedManifest.loadFromBuffer(
			installedManifestText.data(),
			static_cast<int>(installedManifestText.size())) &&
		installedManifest.releaseMetadata.installedArtifactCrc32 ==
			result.package.artifactCrc32 &&
		installedManifest.releaseMetadata.
			installedIncrementalArtifactCrc32.empty() &&
		installedManifest.releaseMetadata.
			installedIncrementalChainCrc32s.empty(),
		"standalone import records the selected full ZIP and clears stale"
		" incremental receipts");

	const std::filesystem::path unsafeArchive =
		nextPath(tree, "imported-unsafe-path", ".zip");
	expect(writeZip(unsafeArchive,
		{
			{ "game_profile.ini", manifest },
			{ "../outside.txt", "escape" }
		}), "unsafe imported resource ZIP is created");
	const std::filesystem::path unsafeDestination =
		nextPath(tree, "imported-unsafe-path-staging");
	const auto unsafeResult =
		OnlineUpdate::prepareImportedResourcePackageArchive(
			unsafeArchive, unsafeDestination);
	expect(unsafeResult.archive.status ==
			OnlineUpdate::ResourcePackageArchiveStatus::InvalidEntryPath &&
		!std::filesystem::exists(unsafeDestination) &&
		!std::filesystem::exists(unsafeDestination.parent_path() / "outside.txt"),
		"standalone import reuses traversal rejection and failure cleanup");

	const auto expectAcceptedManifest =
		[&tree](
			const std::string& name,
			const std::string& manifestText,
			bool resourceOnly)
		{
			const std::filesystem::path acceptedArchive =
				nextPath(tree, "imported-" + name, ".zip");
			expect(writeZip(acceptedArchive,
				{ { "game_profile.ini", manifestText } }),
				"open import fixture is created: " + name);
			const std::filesystem::path acceptedDestination =
				nextPath(tree, "imported-" + name + "-staging");
			const auto accepted =
				OnlineUpdate::prepareImportedResourcePackageArchive(
					acceptedArchive, acceptedDestination);
			expect(accepted.succeeded() &&
				accepted.package.resourceOnly == resourceOnly &&
				std::filesystem::is_regular_file(
					acceptedDestination / "game_profile.ini"),
				"open import accepts semantically unplayable package: " + name);
		};
	expectAcceptedManifest(
		"missing-version",
		"[Game]\nId=IMPORT\n[Release]\nMinimumEngineVersion=2.0.0\n",
		false);
	expectAcceptedManifest(
		"invalid-engine-version",
		"[Game]\nId=IMPORT\nVersion=1.0.0\n"
		"[Release]\nMinimumEngineVersion=invalid\n",
		false);
	expectAcceptedManifest(
		"self-dependency",
		"[Game]\nId=IMPORT\nVersion=1.0.0\n"
		"[Release]\nMinimumEngineVersion=2.0.0\n"
		"[Resource]\nDependencyId=IMPORT\n",
		false);
	expectAcceptedManifest(
		"resource-only",
		"[Game]\nId=IMPORT\nVersion=1.0.0\n"
		"[Release]\nMinimumEngineVersion=2.0.0\n"
		"[Resource]\nResourceOnly=1\n",
		true);

	const std::filesystem::path invalidIdArchive =
		nextPath(tree, "imported-invalid-id", ".zip");
	expect(writeZip(invalidIdArchive,
		{ { "game_profile.ini", "[Game]\nId=BAD,ID\n" } }),
		"invalid imported identity fixture is created");
	const std::filesystem::path invalidIdDestination =
		nextPath(tree, "imported-invalid-id-staging");
	const auto invalidId =
		OnlineUpdate::prepareImportedResourcePackageArchive(
			invalidIdArchive, invalidIdDestination);
	expect(invalidId.archive.status ==
			OnlineUpdate::ResourcePackageArchiveStatus::GameIdMismatch &&
		!std::filesystem::exists(invalidIdDestination),
		"open import still rejects an unsafe Game.Id used by installation");
}

void testCommonPackage(const TemporaryTree& tree)
{
	const std::filesystem::path archive =
		nextPath(tree, "common", ".zip");
	expect(writeZip(archive,
		{
			{ "sound/click.wav", "sound-data" },
			{ "image/ui/title.png", "image-data" }
		}), "common package ZIP is created");
	const OnlineUpdate::CommonPackage package =
		expectedCommonPackage(archive);
	const std::filesystem::path destination =
		nextPath(tree, "common-staging");
	const auto result = OnlineUpdate::prepareCommonPackageArchive(
		package, archive, destination);
	expect(result.succeeded() && std::filesystem::is_regular_file(
			destination / "sound/click.wav"),
		"common package uses the resource ZIP rules without a manifest");
	OnlineUpdate::CommonPackageInstallation commonInstallation;
	const std::string commonVersionText = readText(destination / "version.ini");
	expect(OnlineUpdate::parseCommonPackageInstallation(
			commonVersionText, commonInstallation) &&
		commonInstallation.versionText == "1.0" &&
		commonInstallation.installedArtifactCrc32 == package.crc32Hex,
		"common packages receive the catalog version and artifact receipt");

	const std::filesystem::path matchingVersionArchive =
		nextPath(tree, "common-version", ".zip");
	expect(writeZip(matchingVersionArchive,
		{
			{ "sound/click.wav", "sound-data" },
			{ "version.ini",
				"[Common]\nVersion=1.0\n"
				"InstalledArtifactCrc32=deadbeef\n" }
		}), "versioned common package fixture is created");
	const std::filesystem::path matchingVersionDestination =
		nextPath(tree, "common-version-staging");
	const OnlineUpdate::CommonPackage matchingVersionPackage =
		expectedCommonPackage(matchingVersionArchive);
	expect(OnlineUpdate::prepareCommonPackageArchive(
			matchingVersionPackage,
			matchingVersionArchive,
			matchingVersionDestination).succeeded(),
		"common package accepts a matching embedded version marker");
	const std::string matchingVersionText =
		readText(matchingVersionDestination / "version.ini");
	expect(OnlineUpdate::parseCommonPackageInstallation(
			matchingVersionText, commonInstallation) &&
		commonInstallation.installedArtifactCrc32 ==
			matchingVersionPackage.crc32Hex,
		"common installation replaces a stale embedded receipt");

	const std::filesystem::path mismatchedVersionArchive =
		nextPath(tree, "common-version-mismatch", ".zip");
	expect(writeZip(mismatchedVersionArchive,
		{
			{ "sound/click.wav", "sound-data" },
			{ "version.ini", "[Common]\nVersion=2.0\n" }
		}), "mismatched common version fixture is created");
	const std::filesystem::path mismatchedVersionDestination =
		nextPath(tree, "common-version-mismatch-staging");
	const auto mismatchedVersionResult =
		OnlineUpdate::prepareCommonPackageArchive(
			expectedCommonPackage(mismatchedVersionArchive),
			mismatchedVersionArchive,
			mismatchedVersionDestination);
	expect(mismatchedVersionResult.status == OnlineUpdate::
			ResourcePackageArchiveStatus::CommonVersionMismatch &&
		!std::filesystem::exists(mismatchedVersionDestination),
		"common package rejects a marker that disagrees with the catalog");

	const std::filesystem::path manifestArchive =
		nextPath(tree, "common-manifest", ".zip");
	expect(writeZip(manifestArchive,
		{
			{ "game_profile.ini", validManifest() },
			{ "sound/click.wav", "sound-data" }
		}), "common package manifest fixture is created");
	const std::filesystem::path manifestDestination =
		nextPath(tree, "common-manifest-staging");
	const auto manifestResult =
		OnlineUpdate::prepareCommonPackageArchive(
			expectedCommonPackage(manifestArchive),
			manifestArchive,
			manifestDestination);
	expect(manifestResult.status == OnlineUpdate::
			ResourcePackageArchiveStatus::UnexpectedManifest &&
		!std::filesystem::exists(manifestDestination),
		"common package cannot masquerade as a playable resource");

	const std::filesystem::path sharedOnlyArchive =
		nextPath(tree, "common-shared-only", ".zip");
	expect(writeZip(sharedOnlyArchive,
		{ { "image/ui/title.png", "image-data" } }),
		"common package shared-only fixture is created");
	const std::filesystem::path sharedOnlyDestination =
		nextPath(tree, "common-shared-only-staging");
	const auto sharedOnlyResult =
		OnlineUpdate::prepareCommonPackageArchive(
			expectedCommonPackage(sharedOnlyArchive),
			sharedOnlyArchive,
			sharedOnlyDestination);
	expect(sharedOnlyResult.succeeded() &&
		std::filesystem::is_regular_file(
			sharedOnlyDestination / "version.ini"),
		"common package does not require engine bootstrap files");
}

void testImportedCommonAndIncrementalPackage(const TemporaryTree& tree)
{
	const std::filesystem::path commonArchive =
		nextPath(tree, "imported-common", ".zip");
	expect(writeZip(commonArchive,
		{
			{ "version.ini", "[Common]\nVersion=custom-1\n" },
			{ "image/ui/imported.png", "common-image" }
		}), "imported common ZIP is created");
	const std::filesystem::path commonDestination =
		nextPath(tree, "imported-common-staging");
	const auto importedCommon =
		OnlineUpdate::prepareImportedFullResourcePackageArchive(
			commonArchive, commonDestination);
	OnlineUpdate::CommonPackageInstallation commonInstallation;
	const std::string commonVersionText =
		readText(commonDestination / "version.ini");
	expect(importedCommon.succeeded() && importedCommon.package.common &&
		importedCommon.package.gameId == "common" &&
		importedCommon.package.displayVersion == "custom-1" &&
		OnlineUpdate::parseCommonPackageInstallation(
			commonVersionText, commonInstallation) &&
		commonInstallation.installedArtifactCrc32 ==
			importedCommon.package.artifactCrc32,
		"full import auto-detects common and records the selected ZIP receipt");

	const std::filesystem::path unmarkedCommonArchive =
		nextPath(tree, "imported-common-unmarked", ".zip");
	expect(writeZip(unmarkedCommonArchive,
		{ { "image/ui/imported.png", "common-image" } }),
		"unmarked imported common fixture is created");
	const std::filesystem::path unmarkedCommonDestination =
		nextPath(tree, "imported-common-unmarked-staging");
	const auto unmarkedCommon =
		OnlineUpdate::prepareImportedFullResourcePackageArchive(
			unmarkedCommonArchive, unmarkedCommonDestination);
	expect(unmarkedCommon.archive.status == OnlineUpdate::
			ResourcePackageArchiveStatus::MissingCommonBootstrap &&
		!std::filesystem::exists(unmarkedCommonDestination),
		"offline common import requires version.ini when no catalog supplies it");

	const std::filesystem::path installedRoot =
		nextPath(tree, "imported-incremental-base");
	std::error_code directoryError;
	std::filesystem::create_directories(
		installedRoot / "script", directoryError);
	const std::string installedManifest =
		"[Game]\n"
		"Id=OPEN_INCREMENTAL\n"
		"Name=Installed Base\n"
		"Version=1.0\n"
		"\n"
		"[Release]\n"
		"InstalledArtifactCrc32=a1b2c3d4\n"
		"InstalledIncrementalArtifactCrc32=01020304\n"
		"InstalledIncrementalChainCrc32s=11111111,22222222\n";
	expect(!directoryError &&
		writeBytes(installedRoot / "game_profile.ini",
			std::vector<std::uint8_t>(
				installedManifest.begin(), installedManifest.end())) &&
		writeBytes(installedRoot / "script/story.txt",
			std::vector<std::uint8_t>{ 'o', 'l', 'd' }) &&
		writeBytes(installedRoot / "script/keep.txt",
			std::vector<std::uint8_t>{ 'k', 'e', 'e', 'p' }),
		"open incremental installed base fixture is created");

	const std::filesystem::path incrementalArchive =
		nextPath(tree, "imported-incremental", ".zip");
	const std::string overlayManifest =
		"[Game]\n"
		"Id=OPEN_INCREMENTAL\n"
		"Name=Imported Overlay\n"
		"Version=2.0\n"
		"\n"
		"[Release]\n"
		"MinimumEngineVersion=99.invalid\n"
		"InstalledArtifactCrc32=ffffffff\n"
		"InstalledIncrementalArtifactCrc32=eeeeeeee\n"
		"InstalledIncrementalChainCrc32s=dddddddd\n";
	expect(writeZip(incrementalArchive,
		{
			{ "game_profile.ini", overlayManifest },
			{ "script/story.txt", "updated" },
			{ "script/new.txt", "new" }
		}), "open incremental ZIP is created");
	const std::filesystem::path preparedOverlay =
		nextPath(tree, "imported-incremental-overlay");
	const auto importedIncremental =
		OnlineUpdate::prepareImportedIncrementalResourcePackageArchive(
			incrementalArchive, preparedOverlay);
	const std::filesystem::path materializedRoot =
		nextPath(tree, "imported-incremental-materialized");
	const auto materialized =
		OnlineUpdate::materializeImportedIncrementalResourcePackage(
			importedIncremental.package,
			installedRoot,
			preparedOverlay,
			materializedRoot);
	ResourceManifest materializedManifest;
	const std::string materializedManifestText =
		readText(materializedRoot / "game_profile.ini");
	expect(importedIncremental.succeeded() && materialized.succeeded() &&
		readText(materializedRoot / "script/story.txt") == "updated" &&
		readText(materializedRoot / "script/new.txt") == "new" &&
		readText(materializedRoot / "script/keep.txt") == "keep" &&
		readText(installedRoot / "script/story.txt") == "old" &&
		materializedManifest.loadFromBuffer(
			materializedManifestText.data(),
			static_cast<int>(materializedManifestText.size())) &&
		materializedManifest.releaseMetadata.installedArtifactCrc32 ==
			"a1b2c3d4" &&
		materializedManifest.releaseMetadata.
			installedIncrementalArtifactCrc32 ==
				importedIncremental.package.artifactCrc32 &&
		materializedManifest.releaseMetadata.
			installedIncrementalChainCrc32s.empty(),
		"open incremental import copies the base, overlays content, preserves the"
		" full receipt and replaces unverifiable incremental receipts");
}

void testDesktopProgramPackage(const TemporaryTree& tree)
{
	const std::filesystem::path archive =
		nextPath(tree, "desktop-program", ".zip");
	expect(writeZip(archive,
		{
			{ "jxqy-all-in-one.exe", "root-launcher" },
			{ "bin/win32/jxqy-all-in-one.exe", "program" },
			{ "bin/win32/SDL3.dll", "library" },
			{ "bin/updater/win32/jxqy-program-updater.exe", "updater" },
			{ "bin/win64/other.exe", "other-platform" },
			{ "assets/engine/font/font.ttf", "font-data" },
			{ "assets/common/version.ini", "[Common]\nVersion=1.1.0\n" },
			{ "assets/resources.ini", "[Resources]\n" }
		}), "desktop program ZIP is created");
	const std::filesystem::path destination =
		nextPath(tree, "desktop-program-staging");
	const auto result = OnlineUpdate::prepareDesktopProgramPackageArchive(
		expectedDesktopProgramPackage(archive), archive, destination);
	expect(result.succeeded() &&
		std::filesystem::is_regular_file(
			destination / "bin/win32/jxqy-all-in-one.exe") &&
		std::filesystem::is_regular_file(
			destination / "bin/win32/SDL3.dll") &&
		std::filesystem::is_regular_file(destination /
			"bin/updater/win32/jxqy-program-updater.exe") &&
		std::filesystem::is_regular_file(
			destination / "assets/engine/font/font.ttf") &&
		std::filesystem::is_regular_file(
			destination / "assets/common/version.ini") &&
		!std::filesystem::exists(destination / "jxqy-all-in-one.exe") &&
		!std::filesystem::exists(destination / "bin/win64"),
		"complete desktop package stages the updater, mutable binaries, engine, and common only");

	const std::filesystem::path linuxArchive =
		nextPath(tree, "linux-desktop-program", ".zip");
	expect(writeZip(linuxArchive,
		{
			{ "jxqy-all-in-one", "root-launcher" },
			{ "bin/linux/jxqy-all-in-one", "linux-program" },
			{ "bin/linux/libSDL3.so.0", "linux-library" },
			{ "bin/updater/linux/jxqy-program-updater", "linux-updater" },
			{ "bin/win32/jxqy-all-in-one.exe", "other-platform" },
			{ "assets/engine/font/font.ttf", "font-data" },
			{ "assets/common/version.ini", "[Common]\nVersion=1.1.0\n" }
		}), "Linux desktop program ZIP is created");
	const std::filesystem::path linuxDestination =
		nextPath(tree, "linux-desktop-program-staging");
	const auto linuxResult =
		OnlineUpdate::prepareDesktopProgramPackageArchive(
			expectedDesktopProgramPackage(linuxArchive, "linux"),
			linuxArchive,
			linuxDestination);
	expect(linuxResult.succeeded() &&
		std::filesystem::is_regular_file(
			linuxDestination / "bin/linux/jxqy-all-in-one") &&
		std::filesystem::is_regular_file(
			linuxDestination / "bin/linux/libSDL3.so.0") &&
		std::filesystem::is_regular_file(linuxDestination /
			"bin/updater/linux/jxqy-program-updater") &&
		!std::filesystem::exists(linuxDestination / "jxqy-all-in-one") &&
		!std::filesystem::exists(linuxDestination / "bin/win32"),
		"Linux package stages its program and updater without other platforms");

	const std::filesystem::path missingArchive =
		nextPath(tree, "desktop-program-missing", ".zip");
	expect(writeZip(missingArchive,
		{
			{ "bin/win32/SDL3.dll", "library" },
			{ "bin/updater/win32/jxqy-program-updater.exe", "updater" },
			{ "assets/engine/font/font.ttf", "font-data" },
			{ "assets/common/version.ini", "[Common]\nVersion=1.1.0\n" }
		}),
		"missing-executable program ZIP is created");
	const std::filesystem::path missingDestination =
		nextPath(tree, "desktop-program-missing-staging");
	const auto missing = OnlineUpdate::prepareDesktopProgramPackageArchive(
		expectedDesktopProgramPackage(missingArchive),
		missingArchive,
		missingDestination);
	expect(missing.status == OnlineUpdate::
			ResourcePackageArchiveStatus::MissingProgramExecutable &&
		!std::filesystem::exists(missingDestination),
		"desktop program package must contain the target executable");

	const std::filesystem::path missingUpdaterArchive =
		nextPath(tree, "desktop-program-missing-updater", ".zip");
	expect(writeZip(missingUpdaterArchive,
		{
			{ "bin/win32/jxqy-all-in-one.exe", "program" },
			{ "assets/engine/font/font.ttf", "font-data" },
			{ "assets/common/version.ini", "[Common]\nVersion=1.1.0\n" }
		}), "missing-updater program ZIP is created");
	const std::filesystem::path missingUpdaterDestination =
		nextPath(tree, "desktop-program-missing-updater-staging");
	const auto missingUpdater =
		OnlineUpdate::prepareDesktopProgramPackageArchive(
			expectedDesktopProgramPackage(missingUpdaterArchive),
			missingUpdaterArchive,
			missingUpdaterDestination);
	expect(missingUpdater.status == OnlineUpdate::
			ResourcePackageArchiveStatus::MissingProgramUpdater &&
		!std::filesystem::exists(missingUpdaterDestination),
		"desktop program package must contain the independent updater");

	const std::filesystem::path missingEngineArchive =
		nextPath(tree, "desktop-program-missing-engine", ".zip");
	expect(writeZip(missingEngineArchive,
		{
			{ "bin/win32/jxqy-all-in-one.exe", "program" },
			{ "bin/updater/win32/jxqy-program-updater.exe", "updater" },
			{ "assets/common/version.ini", "[Common]\nVersion=1.1.0\n" }
		}), "missing-engine program ZIP is created");
	const std::filesystem::path missingEngineDestination =
		nextPath(tree, "desktop-program-missing-engine-staging");
	const auto missingEngine =
		OnlineUpdate::prepareDesktopProgramPackageArchive(
			expectedDesktopProgramPackage(missingEngineArchive),
			missingEngineArchive,
			missingEngineDestination);
	expect(missingEngine.status == OnlineUpdate::
			ResourcePackageArchiveStatus::MissingEngineBootstrap &&
		!std::filesystem::exists(missingEngineDestination),
		"desktop program package must contain the engine font");

	const std::filesystem::path missingCommonArchive =
		nextPath(tree, "desktop-program-missing-common", ".zip");
	expect(writeZip(missingCommonArchive,
		{
			{ "bin/win32/jxqy-all-in-one.exe", "program" },
			{ "bin/updater/win32/jxqy-program-updater.exe", "updater" },
			{ "assets/engine/font/font.ttf", "font-data" }
		}), "missing-common program ZIP is created");
	const std::filesystem::path missingCommonDestination =
		nextPath(tree, "desktop-program-missing-common-staging");
	const auto missingCommon =
		OnlineUpdate::prepareDesktopProgramPackageArchive(
			expectedDesktopProgramPackage(missingCommonArchive),
			missingCommonArchive,
			missingCommonDestination);
	expect(missingCommon.status == OnlineUpdate::
			ResourcePackageArchiveStatus::MissingCommonBootstrap &&
		!std::filesystem::exists(missingCommonDestination),
		"desktop program package must contain common version metadata");

	const std::filesystem::path manifestArchive =
		nextPath(tree, "desktop-program-manifest", ".zip");
	expect(writeZip(manifestArchive,
		{
			{ "bin/win32/jxqy-all-in-one.exe", "program" },
			{ "bin/updater/win32/jxqy-program-updater.exe", "updater" },
			{ "assets/engine/font/font.ttf", "font-data" },
			{ "assets/common/version.ini", "[Common]\nVersion=1.1.0\n" },
			{ "game_profile.ini", validManifest() }
		}), "program manifest fixture is created");
	const std::filesystem::path manifestDestination =
		nextPath(tree, "desktop-program-manifest-staging");
	const auto manifest = OnlineUpdate::prepareDesktopProgramPackageArchive(
		expectedDesktopProgramPackage(manifestArchive),
		manifestArchive,
		manifestDestination);
	expect(manifest.status == OnlineUpdate::
			ResourcePackageArchiveStatus::UnexpectedManifest &&
		!std::filesystem::exists(manifestDestination),
		"desktop program package cannot masquerade as a game resource");
}

void expectEntryFailure(
	const TemporaryTree& tree,
	const std::vector<ZipEntry>& entries,
	OnlineUpdate::ResourcePackageArchiveStatus expectedStatus,
	const std::string& message)
{
	std::filesystem::path destination;
	const OnlineUpdate::ResourcePackageArchiveResult result =
		prepare(tree, entries, destination);
	expect(result.status == expectedStatus, message);
	expect(!std::filesystem::exists(destination),
		message + " leaves no staging directory");
}

void testUnsafePathsAndConflicts(const TemporaryTree& tree)
{
	using OnlineUpdate::ResourcePackageArchiveStatus;
	for (const std::string& path : {
		"../outside.txt",
		"folder\\file.txt",
		"Folder/file.txt" })
	{
		expectEntryFailure(
			tree,
			{
				{ "game_profile.ini", validManifest() },
				{ path, "invalid" }
			},
			ResourcePackageArchiveStatus::InvalidEntryPath,
			"unsafe or non-lowercase ZIP path is rejected");
	}
	const std::filesystem::path absoluteArchive =
		nextPath(tree, "absolute", ".zip");
	expect(writeZip(
		absoluteArchive,
		{
			{ "game_profile.ini", validManifest() },
			{ "xabsolute.txt", "invalid" }
		}),
		"absolute-path fixture ZIP is created");
	expect(replaceArchiveEntryName(
		absoluteArchive, "xabsolute.txt", "/absolute.txt"),
		"absolute archive entry name is patched");
	const OnlineUpdate::ResourcePackage absolutePackage =
		expectedPackage(absoluteArchive);
	const std::filesystem::path absoluteDestination =
		nextPath(tree, "absolute-staging");
	const auto absoluteResult =
		OnlineUpdate::prepareResourcePackageArchive(
			absolutePackage, absoluteArchive, absoluteDestination);
	expect(absoluteResult.status ==
		ResourcePackageArchiveStatus::InvalidEntryPath,
		"absolute ZIP path is rejected");
	expect(!std::filesystem::exists(absoluteDestination),
		"absolute ZIP path leaves no staging directory");
	expectEntryFailure(
		tree,
		{
			{ "game_profile.ini", validManifest() },
			{ "same.txt", "one" },
			{ "same.txt", "two" }
		},
		ResourcePackageArchiveStatus::DuplicateEntryPath,
		"duplicate ZIP path is rejected");
	expectEntryFailure(
		tree,
		{
			{ "game_profile.ini", validManifest() },
			{ "node", "file" },
			{ "node/child.txt", "child" }
		},
		ResourcePackageArchiveStatus::DuplicateEntryPath,
		"file and child path conflict is rejected");
}

void testSpecialEntry(const TemporaryTree& tree)
{
	const std::filesystem::path archive =
		nextPath(tree, "symlink", ".zip");
	expect(writeZip(
		archive,
		{
			{ "game_profile.ini", validManifest() },
			{ "link", "target" }
		}),
		"symlink test ZIP is created");
	expect(patchCentralEntryAsUnixSymlink(archive, "link"),
		"symlink fixture attributes are patched");
	const OnlineUpdate::ResourcePackage package = expectedPackage(archive);
	const std::filesystem::path destination = nextPath(tree, "symlink-staging");
	const OnlineUpdate::ResourcePackageArchiveResult result =
		OnlineUpdate::prepareResourcePackageArchive(
			package, archive, destination);
	expect(result.status ==
		OnlineUpdate::ResourcePackageArchiveStatus::UnsupportedEntry,
		"Unix symlink entry is rejected");
	expect(!std::filesystem::exists(destination),
		"symlink rejection leaves no staging directory");
}

void testLimitsAndManifest(const TemporaryTree& tree)
{
	using OnlineUpdate::ResourcePackageArchiveLimits;
	using OnlineUpdate::ResourcePackageArchiveStatus;
	std::filesystem::path destination;
	ResourcePackageArchiveLimits limits;
	limits.maximumEntryCount = 1;
	auto result = prepare(
		tree,
		{
			{ "game_profile.ini", validManifest() },
			{ "data.txt", "data" }
		},
		destination,
		nullptr,
		limits);
	expect(result.status == ResourcePackageArchiveStatus::TooManyEntries &&
		!std::filesystem::exists(destination),
		"entry-count limit is enforced before extraction");

	limits = {};
	limits.maximumUncompressedBytes = 8;
	result = prepare(
		tree,
		{ { "game_profile.ini", validManifest() } },
		destination,
		nullptr,
		limits);
	expect(result.status ==
		ResourcePackageArchiveStatus::UncompressedSizeLimitExceeded &&
		!std::filesystem::exists(destination),
		"total uncompressed-size limit is enforced before extraction");

	limits = {};
	limits.minimumFreeSpaceAfterExtractionBytes =
		std::numeric_limits<std::uint64_t>::max();
	result = prepare(
		tree,
		{ { "game_profile.ini", validManifest() } },
		destination,
		nullptr,
		limits);
	expect(result.status ==
		ResourcePackageArchiveStatus::InsufficientDiskSpace &&
		!std::filesystem::exists(destination),
		"target filesystem free space is checked before extraction");

	limits = {};
	limits.maximumManifestBytes = 8;
	result = prepare(
		tree,
		{ { "game_profile.ini", validManifest() } },
		destination,
		nullptr,
		limits);
	expect(result.status == ResourcePackageArchiveStatus::ManifestReadFailed &&
		!std::filesystem::exists(destination),
		"manifest-size limit is enforced before extraction");

	expectEntryFailure(
		tree,
		{ { "data.txt", "data" } },
		ResourcePackageArchiveStatus::MissingManifest,
		"missing root game_profile.ini is rejected");
	expectEntryFailure(
		tree,
		{
			{ "game_profile.ini",
				"[Game]\nName=Missing Id\nVersion=1.0\n"
				"[Release]\nMinimumEngineVersion=2.0.0\n" }
		},
		ResourcePackageArchiveStatus::InvalidManifest,
		"manifest without Game.Id is rejected");
	expectEntryFailure(
		tree,
		{
			{ "game_profile.ini",
				"[Game]\nId=YYCS\ninvalid line\nVersion=1.0\n"
				"[Release]\nMinimumEngineVersion=2.0.0\n"
				"[Resource]\nDependencyId=JXQY2\n" }
		},
		ResourcePackageArchiveStatus::InvalidManifest,
		"malformed game_profile.ini is rejected");
}

void testCatalogIdentity(const TemporaryTree& tree)
{
	using OnlineUpdate::ResourcePackageArchiveStatus;
	const std::filesystem::path archive =
		nextPath(tree, "identity", ".zip");
	expect(writeZip(
		archive, { { "game_profile.ini", validManifest() } }),
		"identity test ZIP is created");
	const OnlineUpdate::ResourcePackage valid = expectedPackage(archive);
	auto checkMismatch = [&](OnlineUpdate::ResourcePackage package,
		ResourcePackageArchiveStatus status,
		const std::string& message)
	{
		const std::filesystem::path destination =
			nextPath(tree, "identity-staging");
		const auto result = OnlineUpdate::prepareResourcePackageArchive(
			package, archive, destination);
		expect(result.status == status, message);
		expect(!std::filesystem::exists(destination),
			message + " removes staging");
	};

	OnlineUpdate::ResourcePackage package = valid;
	package.gameId = "XJXQY";
	checkMismatch(package, ResourcePackageArchiveStatus::GameIdMismatch,
		"catalog Game.Id must match the manifest");
	package = valid;
	package.versionText = "2.0";
	checkMismatch(package,
		ResourcePackageArchiveStatus::DisplayVersionMismatch,
		"catalog display version must match the manifest");
	package = valid;
	package.minimumEngineVersionText = "2.1.0";
	checkMismatch(package,
		ResourcePackageArchiveStatus::MinimumEngineVersionMismatch,
		"catalog minimum engine version must match the manifest");
	package = valid;
	package.dependencyGameIds = { "XJXQY" };
	checkMismatch(package, ResourcePackageArchiveStatus::DependencyMismatch,
		"catalog dependencies must match the manifest");
	package = valid;
	package.resourceOnly = true;
	checkMismatch(package, ResourcePackageArchiveStatus::ResourceOnlyMismatch,
		"catalog ResourceOnly must match the manifest");
	package = valid;
	package.crc32Hex.assign(8, '0');
	checkMismatch(package, ResourcePackageArchiveStatus::ArtifactMismatch,
		"catalog CRC32 must match before extraction");
}

void testExistingDestinationAndFailureCleanup(const TemporaryTree& tree)
{
	const std::filesystem::path archive =
		nextPath(tree, "existing", ".zip");
	expect(writeZip(
		archive, { { "game_profile.ini", validManifest() } }),
		"existing-destination ZIP is created");
	const OnlineUpdate::ResourcePackage package = expectedPackage(archive);
	const std::filesystem::path destination = nextPath(tree, "existing-staging");
	expect(std::filesystem::create_directory(destination),
		"existing destination fixture is created");
	const std::filesystem::path sentinel = destination / "keep.txt";
	{
		std::ofstream output(sentinel);
		output << "keep";
	}
	const auto existingResult =
		OnlineUpdate::prepareResourcePackageArchive(
			package, archive, destination);
	expect(existingResult.status == OnlineUpdate::
		ResourcePackageArchiveStatus::DestinationAlreadyExists &&
		std::filesystem::is_regular_file(sentinel),
		"pre-existing destination is never overwritten or removed");

	const std::filesystem::path corruptArchive =
		nextPath(tree, "corrupt", ".zip");
	expect(writeZip(
		corruptArchive,
		{
			{ "game_profile.ini", validManifest(), MZ_NO_COMPRESSION },
			{ "data.bin", "stored-data", MZ_NO_COMPRESSION }
		}),
		"corrupt-data ZIP is created");
	expect(corruptStoredEntryData(corruptArchive, "data.bin"),
		"stored data is corrupted after ZIP creation");
	const OnlineUpdate::ResourcePackage corruptPackage =
		expectedPackage(corruptArchive);
	const std::filesystem::path corruptDestination =
		nextPath(tree, "corrupt-staging");
	const auto corruptResult =
		OnlineUpdate::prepareResourcePackageArchive(
			corruptPackage, corruptArchive, corruptDestination);
	expect(corruptResult.status ==
		OnlineUpdate::ResourcePackageArchiveStatus::ExtractionFailed,
		"CRC failure aborts extraction");
	expect(!std::filesystem::exists(corruptDestination),
		"CRC failure removes the staging directory created by the call");
}

void testDownloadPreparation(const TemporaryTree& tree)
{
	const std::filesystem::path baseArchive =
		nextPath(tree, "download-base", ".zip");
	const std::filesystem::path targetArchive =
		nextPath(tree, "download-target", ".zip");
	expect(writeZip(baseArchive,
		{
			{ "game_profile.ini", downloadManifest("JXQY2", "1.0", "") },
			{ "script/base.txt", "base" }
		}), "download preparation base ZIP is created");
	expect(writeZip(targetArchive,
		{
			{ "game_profile.ini", downloadManifest("YYCS", "1.0", "JXQY2") },
			{ "script/story.txt", "story" }
		}), "download preparation target ZIP is created");

	OnlineUpdate::Catalog catalog;
	const OnlineUpdate::ResourcePackage basePackage = downloadPackage(
		baseArchive, "JXQY2", "1.0", "resources/jxqy2.zip", {});
	const OnlineUpdate::ResourcePackage targetPackage = downloadPackage(
		targetArchive, "YYCS", "1.0", "resources/yycs.zip", { "JXQY2" });
	catalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(basePackage.gameId), basePackage);
	catalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(targetPackage.gameId), targetPackage);

	bool downloadProgressReported = false;
	bool extractionProgressReported = false;
	const auto downloader =
		[&baseArchive, &targetArchive](
			const std::string& url,
			const std::filesystem::path& destinationPath,
			std::uint64_t,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& progress)
		{
			OnlineUpdate::HttpsDownloadResult result;
			const std::filesystem::path& sourcePath =
				url.find("jxqy2.zip") != std::string::npos
					? baseArchive : targetArchive;
			std::error_code error;
			std::filesystem::copy_file(
				sourcePath,
				destinationPath,
				std::filesystem::copy_options::none,
				error);
			if (error)
			{
				result.status = OnlineUpdate::HttpsDownloadStatus::WriteFailed;
				return result;
			}
			result.transferredBytes = expectedBytes;
			if (progress && !progress(expectedBytes, expectedBytes))
			{
				result.status = OnlineUpdate::HttpsDownloadStatus::Cancelled;
				return result;
			}
			result.status = OnlineUpdate::HttpsDownloadStatus::Success;
			return result;
		};

	const std::filesystem::path workspace =
		nextPath(tree, "download-workspace");
	const OnlineUpdate::ResourceDownloadPreparationResult result =
		OnlineUpdate::prepareResourceDownload(
			catalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			workspace,
			{},
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			[&downloadProgressReported, &extractionProgressReported](
				const OnlineUpdate::ResourceDownloadPreparationProgress& progress)
			{
				downloadProgressReported = downloadProgressReported ||
					(progress.packageCount == 2 && progress.totalBytes > 0 &&
						progress.stage == OnlineUpdate::
							ResourceDownloadPreparationProgress::Stage::Downloading);
				extractionProgressReported = extractionProgressReported ||
					(progress.packageCount == 2 && progress.totalBytes > 0 &&
						progress.stage == OnlineUpdate::
							ResourceDownloadPreparationProgress::Stage::
								ValidatingAndExtracting);
				return true;
			},
			downloader);
	expect(result.succeeded() && downloadProgressReported &&
		extractionProgressReported &&
		result.preparedResources.size() == 2 &&
		result.preparedResources[0].package.gameId == "JXQY2" &&
		result.preparedResources[1].package.gameId == "YYCS",
		"download preparation reports download and extraction stages while"
		" validating dependencies before the target");
	expect(std::filesystem::is_regular_file(
		workspace / "prepared/package-0/game_profile.ini") &&
		std::filesystem::is_regular_file(
			workspace / "prepared/package-1/game_profile.ini"),
		"download preparation keeps the complete validated group in its workspace");

	std::vector<std::string> reusedDependencyUrls;
	const auto reusedDependencyDownloader =
		[&downloader, &reusedDependencyUrls](
			const std::string& url,
			const std::filesystem::path& destinationPath,
			std::uint64_t maximumBytes,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& progress)
		{
			reusedDependencyUrls.push_back(url);
			return downloader(
				url, destinationPath, maximumBytes, expectedBytes, progress);
		};
	OnlineUpdate::InstalledResourceArtifactMap reusableBase;
	reusableBase["JXQY2"] = { basePackage.crc32Hex, {}, false };
	const std::filesystem::path reusedDependencyWorkspace =
		nextPath(tree, "download-reused-dependency");
	const OnlineUpdate::ResourceDownloadPreparationResult reusedDependency =
		OnlineUpdate::prepareResourceDownload(
			catalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			reusedDependencyWorkspace,
			reusableBase,
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			reusedDependencyDownloader);
	expect(reusedDependency.succeeded() &&
		reusedDependency.preparedResources.size() == 1 &&
		reusedDependency.preparedResources.front().package.gameId == "YYCS" &&
		reusedDependencyUrls.size() == 1 &&
		reusedDependencyUrls.front().find("yycs.zip") != std::string::npos,
		"download preparation reuses a dependency with a matching full receipt");

	const std::filesystem::path incrementalArchive =
		nextPath(tree, "download-target-incremental", ".zip");
	expect(writeZip(incrementalArchive,
		{
			{ "game_profile.ini", downloadManifest("YYCS", "1.0", "JXQY2") },
			{ "script/story.txt", "updated-story" },
			{ "script/new.txt", "new-content" }
		}), "incremental resource ZIP is created");
	OnlineUpdate::ResourcePackage incrementalTargetPackage = targetPackage;
	OnlineUpdate::IncrementalResourcePackage incrementalPackage;
	incrementalPackage.artifactPath = "resources/yycs-incremental.zip";
	std::uint32_t incrementalChecksum = 0;
	expect(OnlineUpdate::calculateFileCrc32(
			incrementalArchive,
			incrementalChecksum,
			incrementalPackage.artifactSize),
		"incremental resource ZIP checksum is calculated");
	incrementalPackage.crc32Hex =
		OnlineUpdate::crc32ToLowerHex(incrementalChecksum);
	incrementalTargetPackage.incrementalPackage = incrementalPackage;
	OnlineUpdate::Catalog incrementalCatalog;
	incrementalCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(basePackage.gameId), basePackage);
	incrementalCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(incrementalTargetPackage.gameId),
		incrementalTargetPackage);

	const std::filesystem::path installedTarget =
		nextPath(tree, "installed-target");
	std::error_code installedError;
	std::filesystem::create_directories(
		installedTarget / "script", installedError);
	std::string installedTargetManifest =
		downloadManifest("YYCS", "1.0", "JXQY2");
	const std::string minimumEngineLine =
		"MinimumEngineVersion=2.0.0\n";
	installedTargetManifest.insert(
		installedTargetManifest.find(minimumEngineLine) +
			minimumEngineLine.size(),
		"InstalledArtifactCrc32=" + targetPackage.crc32Hex + "\n");
	expect(!installedError &&
		writeBytes(
			installedTarget / "game_profile.ini",
			std::vector<std::uint8_t>(
				installedTargetManifest.begin(), installedTargetManifest.end())) &&
		writeBytes(
			installedTarget / "script/story.txt",
			std::vector<std::uint8_t>{ 'o', 'l', 'd' }) &&
		writeBytes(
			installedTarget / "script/keep.txt",
			std::vector<std::uint8_t>{ 'k', 'e', 'e', 'p' }),
		"installed incremental base fixture is created");

	OnlineUpdate::InstalledResourceArtifactMap incrementalReceipts;
	incrementalReceipts["JXQY2"] = { basePackage.crc32Hex, {}, false };
	incrementalReceipts["YYCS"] = {
		targetPackage.crc32Hex, {}, true };
	OnlineUpdate::InstalledResourceRootMap incrementalRoots;
	incrementalRoots["YYCS"] = installedTarget;
	const auto incrementalDownloader =
		[&incrementalArchive](
			const std::string& url,
			const std::filesystem::path& destinationPath,
			std::uint64_t,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& progress)
		{
			OnlineUpdate::HttpsDownloadResult downloadResult;
			if (url.find("yycs-incremental.zip") == std::string::npos)
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::NetworkError;
				return downloadResult;
			}
			std::error_code copyError;
			std::filesystem::copy_file(
				incrementalArchive,
				destinationPath,
				std::filesystem::copy_options::none,
				copyError);
			if (copyError)
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::WriteFailed;
				return downloadResult;
			}
			downloadResult.transferredBytes = expectedBytes;
			if (progress && !progress(expectedBytes, expectedBytes))
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::Cancelled;
				return downloadResult;
			}
			downloadResult.status = OnlineUpdate::HttpsDownloadStatus::Success;
			return downloadResult;
		};
	const std::filesystem::path incrementalWorkspace =
		nextPath(tree, "download-incremental-workspace");
	const OnlineUpdate::ResourceDownloadPreparationResult incremental =
		OnlineUpdate::prepareResourceDownload(
			incrementalCatalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			incrementalWorkspace,
			incrementalReceipts,
			incrementalRoots,
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			incrementalDownloader);
	const std::filesystem::path incrementalPrepared =
		incrementalWorkspace / "prepared/package-0";
	ResourceManifest incrementalManifest;
	const std::string incrementalManifestText =
		readText(incrementalPrepared / "game_profile.ini");
	expect(incremental.succeeded() &&
		incremental.preparedResources.size() == 1 &&
		incremental.preparedResources.front().artifactKind ==
			OnlineUpdate::ResourceDownloadPlan::ArtifactKind::Incremental &&
		readText(incrementalPrepared / "script/story.txt") == "updated-story" &&
		readText(incrementalPrepared / "script/new.txt") == "new-content" &&
		readText(incrementalPrepared / "script/keep.txt") == "keep" &&
		readText(installedTarget / "script/story.txt") == "old" &&
		incrementalManifest.loadFromBuffer(
			incrementalManifestText.data(),
			static_cast<int>(incrementalManifestText.size())) &&
		incrementalManifest.releaseMetadata.installedArtifactCrc32 ==
			targetPackage.crc32Hex &&
		incrementalManifest.releaseMetadata.
			installedIncrementalArtifactCrc32 ==
				incrementalPackage.crc32Hex,
		"incremental download overlays a private copy, preserves other files and"
		" records both receipts without changing the installed tree");

	const std::filesystem::path fullWithIncrementalDestination =
		nextPath(tree, "full-with-incremental");
	const OnlineUpdate::ResourcePackageArchiveResult fullWithIncremental =
		OnlineUpdate::prepareResourcePackageArchive(
			incrementalTargetPackage,
			targetArchive,
			fullWithIncrementalDestination);
	ResourceManifest fullWithIncrementalManifest;
	const std::string fullWithIncrementalManifestText =
		readText(fullWithIncrementalDestination / "game_profile.ini");
	expect(fullWithIncremental.succeeded() &&
		fullWithIncrementalManifest.loadFromBuffer(
			fullWithIncrementalManifestText.data(),
			static_cast<int>(fullWithIncrementalManifestText.size())) &&
		fullWithIncrementalManifest.releaseMetadata.installedArtifactCrc32 ==
			targetPackage.crc32Hex &&
		fullWithIncrementalManifest.releaseMetadata.
			installedIncrementalArtifactCrc32.empty(),
		"full-package staging records only the full artifact receipt until the"
		" incremental overlay is applied");

	std::vector<std::string> fullAndIncrementalUrls;
	const auto fullAndIncrementalDownloader =
		[&targetArchive, &incrementalArchive, &fullAndIncrementalUrls](
			const std::string& url,
			const std::filesystem::path& destinationPath,
			std::uint64_t,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& progress)
		{
			fullAndIncrementalUrls.push_back(url);
			OnlineUpdate::HttpsDownloadResult downloadResult;
			const std::filesystem::path& sourcePath =
				url.find("yycs-incremental.zip") != std::string::npos
					? incrementalArchive : targetArchive;
			std::error_code copyError;
			std::filesystem::copy_file(
				sourcePath,
				destinationPath,
				std::filesystem::copy_options::none,
				copyError);
			if (copyError)
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::WriteFailed;
				return downloadResult;
			}
			downloadResult.transferredBytes = expectedBytes;
			if (progress && !progress(expectedBytes, expectedBytes))
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::Cancelled;
				return downloadResult;
			}
			downloadResult.status = OnlineUpdate::HttpsDownloadStatus::Success;
			return downloadResult;
		};
	OnlineUpdate::InstalledResourceArtifactMap fullAndIncrementalReceipts;
	fullAndIncrementalReceipts["JXQY2"] = {
		basePackage.crc32Hex, {}, false };
	const std::filesystem::path fullAndIncrementalWorkspace =
		nextPath(tree, "download-full-and-incremental-workspace");
	const OnlineUpdate::ResourceDownloadPreparationResult fullAndIncremental =
		OnlineUpdate::prepareResourceDownload(
			incrementalCatalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			fullAndIncrementalWorkspace,
			fullAndIncrementalReceipts,
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			fullAndIncrementalDownloader);
	const std::filesystem::path fullAndIncrementalPrepared =
		fullAndIncrementalWorkspace / "prepared/package-0";
	ResourceManifest fullAndIncrementalManifest;
	const std::string fullAndIncrementalManifestText = readText(
		fullAndIncrementalPrepared / "game_profile.ini");
	expect(fullAndIncremental.succeeded() &&
		fullAndIncremental.preparedResources.size() == 1 &&
		fullAndIncremental.preparedResources.front().artifactKind ==
			OnlineUpdate::ResourceDownloadPlan::ArtifactKind::FullAndIncremental &&
		fullAndIncrementalUrls.size() == 2 &&
		fullAndIncrementalUrls[0].find("yycs.zip") != std::string::npos &&
		fullAndIncrementalUrls[1].find("yycs-incremental.zip") !=
			std::string::npos &&
		readText(fullAndIncrementalPrepared / "script/story.txt") ==
			"updated-story" &&
		readText(fullAndIncrementalPrepared / "script/new.txt") ==
			"new-content" &&
		fullAndIncrementalManifest.loadFromBuffer(
			fullAndIncrementalManifestText.data(),
			static_cast<int>(fullAndIncrementalManifestText.size())) &&
		fullAndIncrementalManifest.releaseMetadata.installedArtifactCrc32 ==
			targetPackage.crc32Hex &&
		fullAndIncrementalManifest.releaseMetadata.
			installedIncrementalArtifactCrc32 == incrementalPackage.crc32Hex,
		"a changed full package downloads and applies its declared incremental"
		" overlay before producing one install target");

	const std::filesystem::path chainOneArchive =
		nextPath(tree, "download-target-chain-one", ".zip");
	const std::filesystem::path chainTwoArchive =
		nextPath(tree, "download-target-chain-two", ".zip");
	expect(writeZip(chainOneArchive,
		{
			{ "game_profile.ini", downloadManifest("YYCS", "0.9", "JXQY2") },
			{ "script/story.txt", "chain-one" },
			{ "script/one.txt", "one" }
		}) && writeZip(chainTwoArchive,
		{
			{ "game_profile.ini", downloadManifest("YYCS", "1.0", "JXQY2") },
			{ "script/story.txt", "chain-two" },
			{ "script/two.txt", "two" }
		}),
		"ordered incremental chain ZIPs are created");
	OnlineUpdate::IncrementalResourcePackage chainOne;
	chainOne.artifactPath = "resources/yycs-chain-001.zip";
	std::uint32_t chainOneChecksum = 0;
	expect(OnlineUpdate::calculateFileCrc32(
			chainOneArchive, chainOneChecksum, chainOne.artifactSize),
		"first chain ZIP checksum is calculated");
	chainOne.crc32Hex = OnlineUpdate::crc32ToLowerHex(chainOneChecksum);
	OnlineUpdate::IncrementalResourcePackage chainTwo;
	chainTwo.artifactPath = "resources/yycs-chain-002.zip";
	std::uint32_t chainTwoChecksum = 0;
	expect(OnlineUpdate::calculateFileCrc32(
			chainTwoArchive, chainTwoChecksum, chainTwo.artifactSize),
		"last chain ZIP checksum is calculated");
	chainTwo.crc32Hex = OnlineUpdate::crc32ToLowerHex(chainTwoChecksum);
	OnlineUpdate::ResourcePackage chainTargetPackage = targetPackage;
	chainTargetPackage.incrementalPackage = chainTwo;
	chainTargetPackage.incrementalChain = { chainOne, chainTwo };
	OnlineUpdate::Catalog chainCatalog;
	chainCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(basePackage.gameId), basePackage);
	chainCatalog.resourcePackages.emplace(
		OnlineUpdate::foldGameId(chainTargetPackage.gameId), chainTargetPackage);

	std::vector<std::string> chainUrls;
	const auto chainDownloader =
		[&targetArchive,
		 &chainOneArchive,
		 &chainTwoArchive,
		 &chainUrls](
			const std::string& url,
			const std::filesystem::path& destinationPath,
			std::uint64_t,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& progress)
		{
			chainUrls.push_back(url);
			const std::filesystem::path* sourcePath = &targetArchive;
			if (url.find("chain-001.zip") != std::string::npos)
			{
				sourcePath = &chainOneArchive;
			}
			else if (url.find("chain-002.zip") != std::string::npos)
			{
				sourcePath = &chainTwoArchive;
			}
			OnlineUpdate::HttpsDownloadResult downloadResult;
			std::error_code copyError;
			std::filesystem::copy_file(
				*sourcePath,
				destinationPath,
				std::filesystem::copy_options::none,
				copyError);
			if (copyError)
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::WriteFailed;
				return downloadResult;
			}
			downloadResult.transferredBytes = expectedBytes;
			if (progress && !progress(expectedBytes, expectedBytes))
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::Cancelled;
				return downloadResult;
			}
			downloadResult.status = OnlineUpdate::HttpsDownloadStatus::Success;
			return downloadResult;
		};
	OnlineUpdate::InstalledResourceArtifactMap chainReceipts;
	chainReceipts["JXQY2"] = { basePackage.crc32Hex, {}, false };
	const std::filesystem::path chainWorkspace =
		nextPath(tree, "download-chain-workspace");
	const OnlineUpdate::ResourceDownloadPreparationResult chainResult =
		OnlineUpdate::prepareResourceDownload(
			chainCatalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			chainWorkspace,
			chainReceipts,
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			chainDownloader);
	const std::filesystem::path chainPrepared =
		chainWorkspace / "prepared/package-0";
	ResourceManifest chainManifest;
	const std::string chainManifestText =
		readText(chainPrepared / "game_profile.ini");
	expect(chainResult.succeeded() && chainUrls.size() == 3 &&
		chainUrls[0].find("yycs.zip") != std::string::npos &&
		chainUrls[1].find("chain-001.zip") != std::string::npos &&
		chainUrls[2].find("chain-002.zip") != std::string::npos &&
		readText(chainPrepared / "script/story.txt") == "chain-two" &&
		readText(chainPrepared / "script/one.txt") == "one" &&
		readText(chainPrepared / "script/two.txt") == "two" &&
		chainManifest.loadFromBuffer(
			chainManifestText.data(),
			static_cast<int>(chainManifestText.size())) &&
		chainManifest.releaseMetadata.installedArtifactCrc32 ==
			targetPackage.crc32Hex &&
		chainManifest.releaseMetadata.installedIncrementalArtifactCrc32 ==
			chainTwo.crc32Hex &&
		chainManifest.releaseMetadata.installedIncrementalChainCrc32s ==
			chainOne.crc32Hex + "," + chainTwo.crc32Hex,
		"resource preparation downloads and applies every chain entry in order,"
		" accepts historical metadata before the tail, and records the complete"
		" receipt only after the current tail");

	int blockedDownloadCount = 0;
	const std::filesystem::path blockedWorkspace =
		nextPath(tree, "download-chain-blocked-engine");
	const OnlineUpdate::ResourceDownloadPreparationResult blockedByEngine =
		OnlineUpdate::prepareResourceDownload(
			chainCatalog,
			"YYCS",
			"1.9.9",
			{ { "https://updates.example/catalog.ini" }, {} },
			blockedWorkspace,
			{},
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			[&blockedDownloadCount](
				const std::string&,
				const std::filesystem::path&,
				std::uint64_t,
				std::uint64_t,
				const OnlineUpdate::HttpsDownloadProgress&)
			{
				blockedDownloadCount++;
				return OnlineUpdate::HttpsDownloadResult{};
			});
	expect(blockedByEngine.status ==
		OnlineUpdate::ResourceDownloadPreparationStatus::PlanFailed &&
		blockedByEngine.planStatus ==
			OnlineUpdate::ResourcePlanStatus::RequiresNewerEngine &&
		blockedDownloadCount == 0 &&
		!std::filesystem::exists(blockedWorkspace),
		"an outdated program exits before creating a workspace or downloading"
		" any resource artifact");

	const std::filesystem::path cancelledWorkspace =
		nextPath(tree, "download-cancelled");
	const OnlineUpdate::ResourceDownloadPreparationResult cancelled =
		OnlineUpdate::prepareResourceDownload(
			catalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			cancelledWorkspace,
			{},
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			[](const OnlineUpdate::ResourceDownloadPreparationProgress&)
			{
				return false;
			},
			downloader);
	expect(cancelled.status ==
		OnlineUpdate::ResourceDownloadPreparationStatus::Cancelled &&
		!std::filesystem::exists(cancelledWorkspace),
		"cancelled group download removes only its private workspace");

	OnlineUpdate::Catalog mismatchedCatalog = catalog;
	mismatchedCatalog.resourcePackages[
		OnlineUpdate::foldGameId("JXQY2")].crc32Hex = "00000000";
	const std::filesystem::path invalidWorkspace =
		nextPath(tree, "download-invalid");
	const OnlineUpdate::ResourceDownloadPreparationResult invalid =
		OnlineUpdate::prepareResourceDownload(
			mismatchedCatalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			invalidWorkspace,
			{},
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			downloader);
	expect(invalid.status ==
		OnlineUpdate::ResourceDownloadPreparationStatus::
			ArtifactValidationFailed &&
		!std::filesystem::exists(invalidWorkspace),
		"one invalid dependency checksum cancels the group before installed"
		" resources change");

	const std::filesystem::path existingWorkspace =
		nextPath(tree, "download-existing");
	std::error_code error;
	std::filesystem::create_directory(existingWorkspace, error);
	const OnlineUpdate::ResourceDownloadPreparationResult existing =
		OnlineUpdate::prepareResourceDownload(
			catalog,
			"YYCS",
			"2.0.0",
			{ { "https://updates.example/catalog.ini" }, {} },
			existingWorkspace,
			{},
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			{},
			downloader);
	expect(existing.status ==
		OnlineUpdate::ResourceDownloadPreparationStatus::WorkspaceAlreadyExists &&
		std::filesystem::is_directory(existingWorkspace),
		"download preparation never adopts or deletes a pre-existing workspace");

	const std::filesystem::path commonArchive =
		nextPath(tree, "download-common", ".zip");
	expect(writeZip(commonArchive,
		{ { "sound/click.wav", "sound-data" } }),
		"common download preparation ZIP is created");
	catalog.commonPackage = expectedCommonPackage(commonArchive);
	const auto commonDownloader =
		[&commonArchive](
			const std::string&,
			const std::filesystem::path& destinationPath,
			std::uint64_t,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& downloadProgress)
		{
			OnlineUpdate::HttpsDownloadResult downloadResult;
			std::error_code copyError;
			std::filesystem::copy_file(
				commonArchive, destinationPath,
				std::filesystem::copy_options::none, copyError);
			if (copyError)
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::WriteFailed;
				return downloadResult;
			}
			downloadResult.transferredBytes = expectedBytes;
			if (downloadProgress &&
				!downloadProgress(expectedBytes, expectedBytes))
			{
				downloadResult.status =
					OnlineUpdate::HttpsDownloadStatus::Cancelled;
				return downloadResult;
			}
			downloadResult.status =
				OnlineUpdate::HttpsDownloadStatus::Success;
			return downloadResult;
		};
	const std::filesystem::path commonWorkspace =
		nextPath(tree, "download-common-workspace");
	const auto common = OnlineUpdate::prepareCommonDownload(
		catalog,
		{ { "https://updates.example/catalog.ini" }, {} },
		commonWorkspace,
		{},
		commonDownloader);
	expect(common.succeeded() &&
		common.package.versionText == "1.0" &&
		std::filesystem::is_regular_file(
			common.preparedCommonPath / "version.ini"),
		"common download remains user-triggered but reuses artifact validation");
	OnlineUpdate::CommonPackageInstallation commonReceipt;
	const std::string preparedCommonVersion = readText(
		common.preparedCommonPath / "version.ini");
	expect(OnlineUpdate::parseCommonPackageInstallation(
			preparedCommonVersion, commonReceipt) &&
		commonReceipt.installedArtifactCrc32 ==
			catalog.commonPackage->crc32Hex,
		"common download preparation records the validated archive receipt");
}

void testProgramDownloadPreparation(const TemporaryTree& tree)
{
	const std::filesystem::path sourceArtifact =
		nextPath(tree, "program-source", ".zip");
	const std::vector<std::uint8_t> sourceBytes = {
		'j', 'x', 'q', 'y', '-', 'p', 'r', 'o', 'g', 'r', 'a', 'm'
	};
	expect(writeBytes(sourceArtifact, sourceBytes),
		"program artifact fixture is created");

	OnlineUpdate::ProgramPackage package;
	package.target = "windows";
	package.versionText = "2.1.0";
	package.version = ModRelease::parseSemanticVersion(
		package.versionText).version;
	package.artifactPath = "program/windows-2.1.0.zip";
	std::uint32_t checksum = 0;
	std::uint64_t fileSize = 0;
	expect(OnlineUpdate::calculateFileCrc32(
		sourceArtifact, checksum, fileSize),
		"program artifact checksum fixture is calculated");
	package.artifactSize = fileSize;
	package.crc32Hex = OnlineUpdate::crc32ToLowerHex(checksum);
	OnlineUpdate::Catalog catalog;
	catalog.programPackages.emplace(package.target, package);

	const auto downloader = [&sourceArtifact](
		const std::string&,
		const std::filesystem::path& destinationPath,
		std::uint64_t,
		std::uint64_t expectedBytes,
		const OnlineUpdate::HttpsDownloadProgress& progress)
	{
		OnlineUpdate::HttpsDownloadResult result;
		std::error_code error;
		std::filesystem::copy_file(
			sourceArtifact, destinationPath,
			std::filesystem::copy_options::none, error);
		if (error)
		{
			result.status = OnlineUpdate::HttpsDownloadStatus::WriteFailed;
			return result;
		}
		result.transferredBytes = expectedBytes;
		if (progress && !progress(expectedBytes, expectedBytes))
		{
			result.status = OnlineUpdate::HttpsDownloadStatus::Cancelled;
			return result;
		}
		result.status = OnlineUpdate::HttpsDownloadStatus::Success;
		return result;
	};

	const std::filesystem::path workspace =
		nextPath(tree, "program-workspace");
	const auto prepared = OnlineUpdate::prepareProgramDownload(
		catalog,
		"windows",
		"2.0.9",
		{ { "https://updates.example/catalog.ini" }, {} },
		workspace,
		{},
		downloader);
	expect(prepared.succeeded() && prepared.package.versionText == "2.1.0" &&
		std::filesystem::is_regular_file(prepared.artifactPath) &&
		readBytes(prepared.artifactPath) == sourceBytes,
		"newer program artifact downloads and validates without installation");

	const std::string catalogText =
		"[Catalog]\n"
		"SchemaVersion=1\n"
		"ProgramTargets=windows\n"
		"\n"
		"[Program.windows]\n"
		"Version=" + package.versionText + "\n"
		"Artifact=" + package.artifactPath + "\n"
		"Size=" + std::to_string(package.artifactSize) + "\n"
		"Crc32=" + package.crc32Hex + "\n";
	const auto catalogResponse = [](const std::string& text)
	{
		OnlineUpdate::HttpsBufferDownloadResult result;
		result.status = OnlineUpdate::HttpsDownloadStatus::Success;
		result.bytes.assign(text.begin(), text.end());
		result.transferredBytes = result.bytes.size();
		return result;
	};
	std::vector<std::string> initialCatalogRequests;
	const auto selectedCatalog = OnlineUpdate::selectCatalogMirrorSources(
		{
			"https://first.example/application/catalog.ini",
			"https://second.example/application/catalog.ini",
		},
		{},
		[&initialCatalogRequests, &catalogText, &catalogResponse](
			const std::string& url,
			std::size_t,
			const OnlineUpdate::HttpsDownloadProgress&)
		{
			initialCatalogRequests.push_back(url);
			return catalogResponse(catalogText);
		});
	expect(selectedCatalog.succeeded() && initialCatalogRequests.size() == 1 &&
		selectedCatalog.sources.catalogUrls.size() == 2,
		"catalog selection stops at the first valid catalog and retains later"
		" fallback candidates");

	const std::vector<std::pair<OnlineUpdate::HttpsDownloadStatus, std::string>>
		retryableFailures = {
			{ OnlineUpdate::HttpsDownloadStatus::NetworkError, "network" },
			{ OnlineUpdate::HttpsDownloadStatus::HttpError, "http" },
			{ OnlineUpdate::HttpsDownloadStatus::SizeLimitExceeded, "size-limit" },
			{ OnlineUpdate::HttpsDownloadStatus::SizeMismatch, "size-mismatch" },
		};
	for (const auto& failure : retryableFailures)
	{
		std::vector<std::string> attemptedUrls;
		std::vector<std::string> fallbackCatalogRequests;
		const auto mirrorDownloader =
			[&attemptedUrls, &downloader, failureStatus = failure.first](
				const std::string& url,
				const std::filesystem::path& destinationPath,
				std::uint64_t maximumBytes,
				std::uint64_t expectedBytes,
				const OnlineUpdate::HttpsDownloadProgress& progress)
			{
				attemptedUrls.push_back(url);
				if (attemptedUrls.size() == 1)
				{
					OnlineUpdate::HttpsDownloadResult result;
					result.status = failureStatus;
					return result;
				}
				return downloader(
					url, destinationPath, maximumBytes, expectedBytes, progress);
			};
		const auto fallback = OnlineUpdate::prepareProgramDownload(
			selectedCatalog.parse.catalog,
			"windows",
			"2.0.9",
			selectedCatalog.sources,
			nextPath(tree, "program-mirror-" + failure.second),
			{},
			mirrorDownloader,
			[&fallbackCatalogRequests, &catalogText, &catalogResponse](
				const std::string& url,
				std::size_t,
				const OnlineUpdate::HttpsDownloadProgress&)
			{
				fallbackCatalogRequests.push_back(url);
				return catalogResponse(catalogText);
			});
		expect(fallback.succeeded() && attemptedUrls.size() == 2 &&
			fallbackCatalogRequests.size() == 1 &&
			fallbackCatalogRequests.front().find("second.example") !=
				std::string::npos &&
			attemptedUrls[0].find("first.example") != std::string::npos &&
			attemptedUrls[1].find("second.example") != std::string::npos,
			"program download retries the next mirror after " + failure.second);
	}

	std::size_t differentCatalogArtifactAttempts = 0;
	const auto differentCatalog = OnlineUpdate::prepareProgramDownload(
		selectedCatalog.parse.catalog,
		"windows",
		"2.0.9",
		selectedCatalog.sources,
		nextPath(tree, "program-mirror-different-catalog"),
		{},
		[&differentCatalogArtifactAttempts](
			const std::string&,
			const std::filesystem::path&,
			std::uint64_t,
			std::uint64_t,
			const OnlineUpdate::HttpsDownloadProgress&)
		{
			differentCatalogArtifactAttempts++;
			OnlineUpdate::HttpsDownloadResult result;
			result.status = OnlineUpdate::HttpsDownloadStatus::NetworkError;
			return result;
		},
		[&catalogText, &catalogResponse](
			const std::string&,
			std::size_t,
			const OnlineUpdate::HttpsDownloadProgress&)
		{
			return catalogResponse(catalogText + "\n");
		});
	expect(differentCatalog.status == OnlineUpdate::
			ResourceDownloadPreparationStatus::DownloadFailed &&
		differentCatalogArtifactAttempts == 1,
		"a byte-different catalog cannot supply fallback artifacts");

	std::size_t writeFailureArtifactAttempts = 0;
	std::size_t writeFailureCatalogAttempts = 0;
	const auto writeFailure = OnlineUpdate::prepareProgramDownload(
		selectedCatalog.parse.catalog,
		"windows",
		"2.0.9",
		selectedCatalog.sources,
		nextPath(tree, "program-mirror-write-failure"),
		{},
		[&writeFailureArtifactAttempts](
			const std::string&,
			const std::filesystem::path&,
			std::uint64_t,
			std::uint64_t,
			const OnlineUpdate::HttpsDownloadProgress&)
		{
			writeFailureArtifactAttempts++;
			OnlineUpdate::HttpsDownloadResult result;
			result.status = OnlineUpdate::HttpsDownloadStatus::WriteFailed;
			return result;
		},
		[&writeFailureCatalogAttempts](
			const std::string&,
			std::size_t,
			const OnlineUpdate::HttpsDownloadProgress&)
		{
			writeFailureCatalogAttempts++;
			return OnlineUpdate::HttpsBufferDownloadResult();
		});
	expect(writeFailure.status == OnlineUpdate::
			ResourceDownloadPreparationStatus::DownloadFailed &&
		writeFailureArtifactAttempts == 1 && writeFailureCatalogAttempts == 0,
		"a local write failure stops without checking another mirror");

	std::size_t exceptionAttempts = 0;
	const auto unexpectedFailure = OnlineUpdate::prepareProgramDownload(
		selectedCatalog.parse.catalog,
		"windows",
		"2.0.9",
		selectedCatalog.sources,
		nextPath(tree, "program-mirror-exception"),
		{},
		[&exceptionAttempts](
			const std::string&,
			const std::filesystem::path&,
			std::uint64_t,
			std::uint64_t,
			const OnlineUpdate::HttpsDownloadProgress&) ->
				OnlineUpdate::HttpsDownloadResult
		{
			exceptionAttempts++;
			throw std::runtime_error("local download failure");
		});
	expect(unexpectedFailure.status == OnlineUpdate::
			ResourceDownloadPreparationStatus::DownloadFailed &&
		unexpectedFailure.downloadResult.status ==
			OnlineUpdate::HttpsDownloadStatus::UnexpectedError &&
		exceptionAttempts == 1,
		"an unexpected local exception stops without trying another mirror");

	std::vector<std::string> checksumAttemptedUrls;
	const auto checksumFallbackDownloader =
		[&checksumAttemptedUrls, &downloader, &sourceBytes](
			const std::string& url,
			const std::filesystem::path& destinationPath,
			std::uint64_t maximumBytes,
			std::uint64_t expectedBytes,
			const OnlineUpdate::HttpsDownloadProgress& progress)
		{
			checksumAttemptedUrls.push_back(url);
			if (checksumAttemptedUrls.size() == 1)
			{
				std::vector<std::uint8_t> wrongBytes = sourceBytes;
				wrongBytes.front() ^= 0xFF;
				OnlineUpdate::HttpsDownloadResult result;
				result.status = writeBytes(destinationPath, wrongBytes)
					? OnlineUpdate::HttpsDownloadStatus::Success
					: OnlineUpdate::HttpsDownloadStatus::WriteFailed;
				result.transferredBytes = expectedBytes;
				return result;
			}
			return downloader(
				url, destinationPath, maximumBytes, expectedBytes, progress);
		};
	const auto checksumFallback = OnlineUpdate::prepareProgramDownload(
		selectedCatalog.parse.catalog,
		"windows",
		"2.0.9",
		selectedCatalog.sources,
		nextPath(tree, "program-mirror-checksum"),
		{},
		checksumFallbackDownloader,
		[&catalogText, &catalogResponse](
			const std::string&,
			std::size_t,
			const OnlineUpdate::HttpsDownloadProgress&)
		{
			return catalogResponse(catalogText);
		});
	expect(checksumFallback.succeeded() && checksumAttemptedUrls.size() == 2 &&
		readBytes(checksumFallback.artifactPath) == sourceBytes,
		"program download retries the next mirror after CRC32 validation fails");

	std::size_t cancelledAttemptCount = 0;
	const auto cancelledDownloader = [&cancelledAttemptCount](
		const std::string&,
		const std::filesystem::path&,
		std::uint64_t,
		std::uint64_t,
		const OnlineUpdate::HttpsDownloadProgress&)
	{
		cancelledAttemptCount++;
		OnlineUpdate::HttpsDownloadResult result;
		result.status = OnlineUpdate::HttpsDownloadStatus::Cancelled;
		return result;
	};
	const std::filesystem::path cancelledMirrorWorkspace =
		nextPath(tree, "program-mirror-cancelled");
	const auto cancelledMirror = OnlineUpdate::prepareProgramDownload(
		selectedCatalog.parse.catalog,
		"windows",
		"2.0.9",
		selectedCatalog.sources,
		cancelledMirrorWorkspace,
		{},
		cancelledDownloader);
	expect(cancelledMirror.status == OnlineUpdate::
			ResourceDownloadPreparationStatus::Cancelled &&
		cancelledAttemptCount == 1 &&
		!std::filesystem::exists(cancelledMirrorWorkspace),
		"cancelled program download does not try another mirror");

	const std::filesystem::path currentWorkspace =
		nextPath(tree, "program-current");
	const auto current = OnlineUpdate::prepareProgramDownload(
		catalog,
		"windows",
		"2.1.0",
		{ { "https://updates.example/catalog.ini" }, {} },
		currentWorkspace,
		{},
		downloader);
	expect(current.succeeded() &&
		current.updateStatus == OnlineUpdate::ProgramUpdateStatus::UpToDate &&
		std::filesystem::is_regular_file(current.artifactPath) &&
		readBytes(current.artifactPath) == sourceBytes,
		"the sole online program artifact downloads at an equal Version");

	const std::filesystem::path olderWorkspace =
		nextPath(tree, "program-older");
	const auto older = OnlineUpdate::prepareProgramDownload(
		catalog,
		"windows",
		"2.2.0",
		{ { "https://updates.example/catalog.ini" }, {} },
		olderWorkspace,
		{},
		downloader);
	expect(older.succeeded() &&
		older.updateStatus == OnlineUpdate::ProgramUpdateStatus::UpToDate &&
		std::filesystem::is_regular_file(older.artifactPath) &&
		readBytes(older.artifactPath) == sourceBytes,
		"the sole online program artifact downloads below the current Version");

	OnlineUpdate::Catalog invalidCatalog = catalog;
	invalidCatalog.programPackages.at("windows").crc32Hex = "00000000";
	const std::filesystem::path invalidWorkspace =
		nextPath(tree, "program-invalid");
	const auto invalid = OnlineUpdate::prepareProgramDownload(
		invalidCatalog,
		"windows",
		"2.0.9",
		{ { "https://updates.example/catalog.ini" }, {} },
		invalidWorkspace,
		{},
		downloader);
	expect(invalid.status == OnlineUpdate::
			ResourceDownloadPreparationStatus::ArtifactValidationFailed &&
		!std::filesystem::exists(invalidWorkspace),
		"program CRC32 mismatch removes only the private workspace");
}

void testOptionalLiveResourceUpdate(const TemporaryTree& tree)
{
	const char* catalogUrl =
		std::getenv("JXQY_TEST_RESOURCE_CATALOG_URL");
	if (catalogUrl == nullptr || *catalogUrl == '\0')
	{
		return;
	}
	const char* requestedGameId =
		std::getenv("JXQY_TEST_RESOURCE_GAME_ID");
	expect(requestedGameId != nullptr && *requestedGameId != '\0',
		"live resource update requires JXQY_TEST_RESOURCE_GAME_ID");
	if (requestedGameId == nullptr || *requestedGameId == '\0')
	{
		return;
	}
	const char* configuredEngineVersion =
		std::getenv("JXQY_TEST_ENGINE_VERSION");
	const std::string engineVersion =
		configuredEngineVersion == nullptr || *configuredEngineVersion == '\0'
		? "2.0.0" : configuredEngineVersion;

	std::cout << "LIVE: downloading catalog " << catalogUrl << std::endl;
	const OnlineUpdate::HttpsBufferDownloadResult catalogDownload =
		OnlineUpdate::downloadHttpsToMemory(
			catalogUrl, OnlineUpdate::MaximumCatalogBytes);
	expect(catalogDownload.succeeded(),
		"live resource update downloads the catalog with production HTTPS");
	if (!catalogDownload.succeeded())
	{
		return;
	}
	const OnlineUpdate::CatalogParseResult parsed =
		OnlineUpdate::parseCatalog(
			catalogDownload.bytes.data(), catalogDownload.bytes.size());
	expect(parsed.succeeded(),
		"live resource update parses the downloaded catalog");
	if (!parsed.succeeded())
	{
		return;
	}

	const std::filesystem::path collectionRoot =
		nextPath(tree, "live-resource-collection");
	std::error_code error;
	expect(std::filesystem::create_directory(collectionRoot, error) && !error,
		"live resource update creates an isolated collection");
	expect(std::filesystem::create_directory(
			OnlineUpdate::resourceUpdateDirectoryPath(collectionRoot), error) &&
		!error,
		"live resource update creates its private update directory");
	if (error)
	{
		return;
	}

	const std::filesystem::path workspace =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	std::uint64_t lastReportedBucket =
		std::numeric_limits<std::uint64_t>::max();
	const auto reportProgress = [&lastReportedBucket](
		const OnlineUpdate::ResourceDownloadPreparationProgress& progress)
	{
		if (progress.totalBytes == 0)
		{
			return true;
		}
		const std::uint64_t percent =
			progress.completedBytes * 100 / progress.totalBytes;
		const std::uint64_t bucket = percent / 10;
		if (bucket != lastReportedBucket)
		{
			lastReportedBucket = bucket;
			std::cout << "LIVE: "
				<< (progress.gameId.empty() ? "common" : progress.gameId)
				<< ' ' << percent << "% ("
				<< progress.completedBytes << '/' << progress.totalBytes
				<< ")" << std::endl;
		}
		return true;
	};

	const OnlineUpdate::CommonDownloadPreparationResult common =
		OnlineUpdate::prepareCommonDownload(
			parsed.catalog, { { catalogUrl }, {} }, workspace, reportProgress);
	expect(common.succeeded() && std::filesystem::is_regular_file(
			common.preparedCommonPath / "version.ini"),
		"live common package downloads, validates and extracts");
	if (!common.succeeded())
	{
		return;
	}
	const OnlineUpdate::ResourceInstallTransactionResult commonStaged =
		OnlineUpdate::stageResourceInstallTransaction(
			collectionRoot, "common", {{"common", "common"}});
	expect(commonStaged.succeeded(),
		"live common package stages through the restart transaction");
	if (!commonStaged.succeeded())
	{
		return;
	}
	const OnlineUpdate::ResourceInstallTransactionResult commonSwitched =
		OnlineUpdate::beginResourceInstallTransaction(collectionRoot);
	expect(commonSwitched.needsValidation,
		"live common package switches through the restart transaction");
	if (!commonSwitched.needsValidation)
	{
		return;
	}
	const OnlineUpdate::ResourceInstallTransactionResult commonCommitted =
		OnlineUpdate::completeResourceInstallTransaction(collectionRoot, true);
	expect(commonCommitted.succeeded() && std::filesystem::is_regular_file(
			collectionRoot / "common/version.ini"),
		"live common package commits through the restart transaction");
	if (!commonCommitted.succeeded())
	{
		return;
	}

	lastReportedBucket = std::numeric_limits<std::uint64_t>::max();
	std::cout << "LIVE: preparing resource " << requestedGameId << std::endl;
	const OnlineUpdate::ResourceDownloadPreparationResult prepared =
		OnlineUpdate::prepareResourceDownload(
			parsed.catalog,
			requestedGameId,
			engineVersion,
			{ { catalogUrl }, {} },
			workspace,
			{},
			{},
			OnlineUpdate::RequestedResourceDownloadMode::IfNeeded,
			reportProgress);
	expect(prepared.succeeded() && !prepared.preparedResources.empty(),
		"live dependency closure downloads, validates and extracts");
	if (!prepared.succeeded())
	{
		return;
	}

	std::vector<OnlineUpdate::ResourceInstallTarget> targets;
	for (const OnlineUpdate::PreparedResourceDownload& resource :
		prepared.preparedResources)
	{
		OnlineUpdate::ResourceInstallTarget target;
		target.gameId = resource.package.gameId;
		target.targetDirectoryName = std::filesystem::u8path(
			resource.package.artifactPath).stem().generic_u8string();
		targets.push_back(std::move(target));
	}
	const OnlineUpdate::ResourceInstallTransactionResult staged =
		OnlineUpdate::stageResourceInstallTransaction(
			collectionRoot, requestedGameId, targets);
	expect(staged.succeeded(),
		"live resource group stages through the restart transaction");
	if (!staged.succeeded())
	{
		return;
	}
	const OnlineUpdate::ResourceInstallTransactionResult switched =
		OnlineUpdate::beginResourceInstallTransaction(collectionRoot);
	expect(switched.needsValidation,
		"live resource group switches through the restart transaction");
	if (!switched.needsValidation)
	{
		return;
	}
	bool installedFilesExist = true;
	for (const OnlineUpdate::ResourceInstallTarget& target : targets)
	{
		installedFilesExist = installedFilesExist &&
			std::filesystem::is_regular_file(
				collectionRoot / target.targetDirectoryName /
				"game_profile.ini");
	}
	const OnlineUpdate::ResourceInstallTransactionResult committed =
		OnlineUpdate::completeResourceInstallTransaction(collectionRoot, true);
	expect(installedFilesExist && committed.succeeded() &&
		!std::filesystem::exists(workspace),
		"live resource group commits atomically and cleans its workspace");
}
}

int main()
{
	TemporaryTree tree;
	if (tree.root.empty())
	{
		std::cerr << "FAIL: could not create test directory" << std::endl;
		return 1;
	}

	testValidPackage(tree);
	testImportedResourcePackage(tree);
	testCommonPackage(tree);
	testImportedCommonAndIncrementalPackage(tree);
	testDesktopProgramPackage(tree);
	testUnsafePathsAndConflicts(tree);
	testSpecialEntry(tree);
	testLimitsAndManifest(tree);
	testCatalogIdentity(tree);
	testExistingDestinationAndFailureCleanup(tree);
	testDownloadPreparation(tree);
	testProgramDownloadPreparation(tree);
	testOptionalLiveResourceUpdate(tree);

	if (failureCount != 0)
	{
		std::cerr << failureCount << " resource package archive test(s) failed"
			<< std::endl;
		return 1;
	}
	std::cout << "All resource package archive tests passed" << std::endl;
	return 0;
}
