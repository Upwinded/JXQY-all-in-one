#include "ResourcePackageArchive.h"

#include "ArtifactChecksum.h"
#include "../File/StrictRelativeResourcePath.h"
#include "../Resource/ResourceIniReader.h"
#include "../Resource/ResourceManifest.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

extern "C"
{
#include "miniz.h"
}

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
struct ArchiveEntry
{
	mz_uint index = 0;
	std::string path;
	bool directory = false;
	bool extract = true;
	std::uint64_t uncompressedSize = 0;
};

class ArchiveReader
{
public:
	ArchiveReader() noexcept
	{
		std::memset(&archive, 0, sizeof(archive));
	}

	~ArchiveReader()
	{
		if (initialized)
		{
			mz_zip_reader_end(&archive);
		}
	}

	ArchiveReader(const ArchiveReader&) = delete;
	ArchiveReader& operator=(const ArchiveReader&) = delete;

	mz_zip_archive archive;
	bool initialized = false;
};

struct ArchiveInput
{
	std::ifstream stream;
};

struct FileOutput
{
	std::ofstream stream;
	std::uint64_t expectedSize = 0;
	std::uint64_t writtenBytes = 0;
	bool failed = false;
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

std::string foldAscii(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(character + ('a' - 'A'));
		}
	}
	return value;
}

bool sameIdentifier(const std::string& left, const std::string& right)
{
	return foldAscii(trimAscii(left)) == foldAscii(trimAscii(right));
}

std::vector<std::string> foldedDependencies(
	const std::vector<std::string>& dependencies)
{
	std::vector<std::string> result;
	result.reserve(dependencies.size());
	for (const std::string& dependency : dependencies)
	{
		result.push_back(foldAscii(trimAscii(dependency)));
	}
	return result;
}

size_t readArchive(void* opaque, mz_uint64 offset, void* buffer, size_t bytes)
{
	auto* input = static_cast<ArchiveInput*>(opaque);
	if (input == nullptr || buffer == nullptr ||
		offset > static_cast<mz_uint64>(
			std::numeric_limits<std::streamoff>::max()) ||
		bytes > static_cast<size_t>(
			std::numeric_limits<std::streamsize>::max()))
	{
		return 0;
	}
	input->stream.clear();
	input->stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	if (!input->stream)
	{
		return 0;
	}
	input->stream.read(
		static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
	return static_cast<size_t>(input->stream.gcount());
}

size_t writeExtractedFile(
	void* opaque,
	mz_uint64 offset,
	const void* buffer,
	size_t bytes)
{
	auto* output = static_cast<FileOutput*>(opaque);
	if (output == nullptr || (buffer == nullptr && bytes != 0) ||
		offset != output->writtenBytes ||
		output->writtenBytes > output->expectedSize ||
		bytes > output->expectedSize - output->writtenBytes ||
		bytes > static_cast<size_t>(
			std::numeric_limits<std::streamsize>::max()))
	{
		if (output != nullptr)
		{
			output->failed = true;
		}
		return 0;
	}
	output->stream.write(
		static_cast<const char*>(buffer),
		static_cast<std::streamsize>(bytes));
	if (!output->stream)
	{
		output->failed = true;
		return 0;
	}
	output->writtenBytes += static_cast<std::uint64_t>(bytes);
	return bytes;
}

bool isReparsePoint(const std::filesystem::path& path) noexcept
{
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
	(void)path;
	return false;
#endif
}

bool isSafeExistingDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	return !error && std::filesystem::is_directory(status) &&
		!std::filesystem::is_symlink(status) && !isReparsePoint(path);
}

bool isRegularArchiveFile(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	return !error && std::filesystem::is_regular_file(status) &&
		!std::filesystem::is_symlink(status) && !isReparsePoint(path);
}

bool isUnixSpecialEntry(const mz_zip_archive_file_stat& stat)
{
	constexpr mz_uint16 UnixHost = 3;
	constexpr mz_uint32 FileTypeMask = 0170000;
	constexpr mz_uint32 RegularFile = 0100000;
	constexpr mz_uint32 Directory = 0040000;
	if ((stat.m_version_made_by >> 8) != UnixHost)
	{
		return false;
	}
	const mz_uint32 fileType =
		(stat.m_external_attr >> 16) & FileTypeMask;
	if (fileType == 0)
	{
		return false;
	}
	if (stat.m_is_directory)
	{
		return fileType != Directory;
	}
	return fileType != RegularFile;
}

bool ensureDirectoryTree(
	const std::filesystem::path& root,
	const std::filesystem::path& relativeDirectory)
{
	std::filesystem::path current = root;
	for (const std::filesystem::path& component : relativeDirectory)
	{
		current /= component;
		std::error_code statusError;
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(current, statusError);
		if (!statusError && std::filesystem::exists(status))
		{
			if (!std::filesystem::is_directory(status) ||
				std::filesystem::is_symlink(status) ||
				isReparsePoint(current))
			{
				return false;
			}
			continue;
		}
		if (statusError &&
			statusError != std::errc::no_such_file_or_directory)
		{
			return false;
		}
		std::error_code createError;
		if (!std::filesystem::create_directory(current, createError) ||
			createError || !isSafeExistingDirectory(current))
		{
			return false;
		}
	}
	return true;
}

bool readManifest(
	const std::filesystem::path& path,
	std::size_t maximumBytes,
	std::vector<char>& bytes)
{
	bytes.clear();
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	if (error || size == 0 || size > maximumBytes ||
		size > static_cast<std::uintmax_t>(
			std::numeric_limits<int>::max()))
	{
		return false;
	}
	std::ifstream input(path, std::ios::binary);
	if (!input)
	{
		return false;
	}
	bytes.resize(static_cast<std::size_t>(size));
	input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	return input && input.gcount() ==
		static_cast<std::streamsize>(bytes.size());
}

OnlineUpdate::ResourcePackageArchiveResult cleanupFailure(
	OnlineUpdate::ResourcePackageArchiveResult result,
	const std::filesystem::path& destinationPath,
	bool destinationCreated)
{
	if (!destinationCreated)
	{
		return result;
	}
	std::error_code cleanupError;
	std::filesystem::remove_all(destinationPath, cleanupError);
	std::error_code statusError;
	const bool remains = std::filesystem::exists(destinationPath, statusError);
	if (cleanupError || statusError || remains)
	{
		result.status =
			OnlineUpdate::ResourcePackageArchiveStatus::CleanupFailed;
		result.filesystemPath = destinationPath;
	}
	return result;
}
}

namespace OnlineUpdate
{
enum class PackageArchiveKind
{
	Resource,
	Common,
	DesktopProgram
};

static ResourcePackageArchiveResult preparePackageArchive(
	std::uint64_t expectedArtifactSize,
	const std::string& expectedCrc32Hex,
	const ResourcePackage* expectedResourcePackage,
	const CommonPackage* expectedCommonPackage,
	PackageArchiveKind archiveKind,
	const std::string& desktopProgramBinPrefix,
	const std::string& requiredProgramExecutable,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits)
{
	ResourcePackageArchiveResult result;
	if (archivePath.empty() || destinationPath.empty() ||
		limits.maximumEntryCount == 0 ||
		limits.maximumUncompressedBytes == 0 ||
		limits.maximumManifestBytes == 0 ||
		expectedArtifactSize == 0 ||
		!isValidCrc32Hex(expectedCrc32Hex))
	{
		result.status = ResourcePackageArchiveStatus::InvalidInput;
		return result;
	}
	if (!isRegularArchiveFile(archivePath))
	{
		result.status = ResourcePackageArchiveStatus::ArchiveUnavailable;
		result.filesystemPath = archivePath;
		return result;
	}

	std::error_code destinationStatusError;
	const std::filesystem::file_status destinationStatus =
		std::filesystem::symlink_status(
			destinationPath, destinationStatusError);
	if (!destinationStatusError && std::filesystem::exists(destinationStatus))
	{
		result.status =
			ResourcePackageArchiveStatus::DestinationAlreadyExists;
		result.filesystemPath = destinationPath;
		return result;
	}
	if (destinationStatusError &&
		destinationStatusError != std::errc::no_such_file_or_directory)
	{
		result.status = ResourcePackageArchiveStatus::UnsafeDestination;
		result.filesystemPath = destinationPath;
		return result;
	}
	const std::filesystem::path destinationParent =
		destinationPath.parent_path();
	if (destinationParent.empty() ||
		!isSafeExistingDirectory(destinationParent))
	{
		result.status = ResourcePackageArchiveStatus::UnsafeDestination;
		result.filesystemPath = destinationParent;
		return result;
	}

	std::uint32_t checksum = 0;
	std::uint64_t artifactSize = 0;
	if (!calculateFileCrc32(archivePath, checksum, artifactSize) ||
		artifactSize != expectedArtifactSize ||
		crc32ToLowerHex(checksum) != expectedCrc32Hex)
	{
		result.status = ResourcePackageArchiveStatus::ArtifactMismatch;
		result.filesystemPath = archivePath;
		return result;
	}

	ArchiveInput archiveInput;
	archiveInput.stream.open(archivePath, std::ios::binary);
	if (!archiveInput.stream)
	{
		result.status = ResourcePackageArchiveStatus::ArchiveOpenFailed;
		result.filesystemPath = archivePath;
		return result;
	}
	ArchiveReader reader;
	reader.archive.m_pRead = readArchive;
	reader.archive.m_pIO_opaque = &archiveInput;
	if (!mz_zip_reader_init(
			&reader.archive,
			static_cast<mz_uint64>(artifactSize),
			MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY))
	{
		result.status = ResourcePackageArchiveStatus::ArchiveOpenFailed;
		result.archiveError = static_cast<int>(
			mz_zip_get_last_error(&reader.archive));
		result.filesystemPath = archivePath;
		return result;
	}
	reader.initialized = true;

	const mz_uint entryCount = mz_zip_reader_get_num_files(&reader.archive);
	if (entryCount > limits.maximumEntryCount)
	{
		result.status = ResourcePackageArchiveStatus::TooManyEntries;
		return result;
	}

	std::vector<ArchiveEntry> entries;
	entries.reserve(entryCount);
	std::set<std::string> portablePaths;
	std::size_t manifestCount = 0;
	std::size_t commonVersionCount = 0;
	bool desktopProgramExecutableFound = false;
	bool desktopEngineBootstrapFound = false;
	bool desktopCommonVersionFound = false;
	const std::string desktopProgramExecutablePath =
		desktopProgramBinPrefix.empty()
			? std::string()
			: desktopProgramBinPrefix + "/" + requiredProgramExecutable;
	for (mz_uint index = 0; index < entryCount; ++index)
	{
		mz_zip_archive_file_stat stat;
		std::memset(&stat, 0, sizeof(stat));
		if (!mz_zip_reader_file_stat(&reader.archive, index, &stat))
		{
			result.status = ResourcePackageArchiveStatus::ArchiveOpenFailed;
			result.archiveError = static_cast<int>(
				mz_zip_get_last_error(&reader.archive));
			return result;
		}
		const mz_uint nameSize =
			mz_zip_reader_get_filename(&reader.archive, index, nullptr, 0);
		if (nameSize <= 1)
		{
			result.status = ResourcePackageArchiveStatus::InvalidEntryPath;
			return result;
		}
		std::vector<char> nameBytes(nameSize);
		if (mz_zip_reader_get_filename(
				&reader.archive,
				index,
				nameBytes.data(),
				nameSize) != nameSize)
		{
			result.status = ResourcePackageArchiveStatus::InvalidEntryPath;
			return result;
		}
		std::string archivePathText(
			nameBytes.data(), static_cast<std::size_t>(nameSize - 1));
		if (archivePathText.find('\0') != std::string::npos ||
			archivePathText.find('\\') != std::string::npos)
		{
			result.status = ResourcePackageArchiveStatus::InvalidEntryPath;
			result.archiveEntryPath = archivePathText;
			return result;
		}
		const bool hasDirectorySuffix =
			!archivePathText.empty() && archivePathText.back() == '/';
		const bool isDirectory = stat.m_is_directory != 0;
		if (hasDirectorySuffix)
		{
			archivePathText.pop_back();
		}
		const ResourcePathSafety::StrictRelativePathResult normalized =
			archiveKind == PackageArchiveKind::DesktopProgram
			? ResourcePathSafety::normalizeStrictRelativeResourcePath(
				archivePathText)
			: ResourcePathSafety::
				normalizeLowercaseStrictRelativeResourcePath(
					archivePathText);
		if (!normalized.succeeded() ||
			normalized.normalizedPath != archivePathText ||
			isDirectory != hasDirectorySuffix)
		{
			result.status = ResourcePackageArchiveStatus::InvalidEntryPath;
			result.archiveEntryPath = archivePathText;
			return result;
		}
		if (!stat.m_is_supported || stat.m_is_encrypted ||
			isUnixSpecialEntry(stat) ||
			(isDirectory &&
				(stat.m_uncomp_size != 0 || stat.m_comp_size != 0)))
		{
			result.status = ResourcePackageArchiveStatus::UnsupportedEntry;
			result.archiveEntryPath = archivePathText;
			return result;
		}
		const std::string portablePath = foldAscii(archivePathText);
		if (!portablePaths.insert(portablePath).second)
		{
			result.status =
				ResourcePackageArchiveStatus::DuplicateEntryPath;
			result.archiveEntryPath = archivePathText;
			return result;
		}
		if (stat.m_uncomp_size > limits.maximumUncompressedBytes ||
			result.uncompressedBytes >
			limits.maximumUncompressedBytes - stat.m_uncomp_size)
		{
			result.status = ResourcePackageArchiveStatus::
				UncompressedSizeLimitExceeded;
			result.archiveEntryPath = archivePathText;
			return result;
		}
		result.uncompressedBytes += stat.m_uncomp_size;
		if (!isDirectory)
		{
			result.fileCount++;
			if (archivePathText == "game_profile.ini")
			{
				if (stat.m_uncomp_size > limits.maximumManifestBytes)
				{
					result.status =
						ResourcePackageArchiveStatus::ManifestReadFailed;
					result.archiveEntryPath = archivePathText;
					return result;
				}
				manifestCount++;
			}
			if (archiveKind == PackageArchiveKind::Common &&
				archivePathText == "version.ini")
			{
				if (stat.m_uncomp_size >
					MaximumCommonVersionFileBytes)
				{
					result.status = ResourcePackageArchiveStatus::
						CommonVersionMismatch;
					result.archiveEntryPath = archivePathText;
					return result;
				}
				commonVersionCount++;
			}
			if (archiveKind == PackageArchiveKind::DesktopProgram)
			{
				desktopProgramExecutableFound =
					desktopProgramExecutableFound ||
					archivePathText == desktopProgramExecutablePath;
				desktopEngineBootstrapFound =
					desktopEngineBootstrapFound ||
					archivePathText == "assets/engine/font/font.ttf";
				desktopCommonVersionFound =
					desktopCommonVersionFound ||
					archivePathText == "assets/common/version.ini";
			}
		}
		bool extractEntry = true;
		if (archiveKind == PackageArchiveKind::DesktopProgram)
		{
			const std::string programPrefix = desktopProgramBinPrefix + "/";
			constexpr std::string_view CommonPrefix = "assets/common/";
			constexpr std::string_view EnginePrefix = "assets/engine/";
			extractEntry =
				(archivePathText.size() > programPrefix.size() &&
					archivePathText.compare(
						0, programPrefix.size(), programPrefix) == 0) ||
				(archivePathText.size() > CommonPrefix.size() &&
					archivePathText.compare(
						0, CommonPrefix.size(), CommonPrefix) == 0) ||
				(archivePathText.size() > EnginePrefix.size() &&
					archivePathText.compare(
						0, EnginePrefix.size(), EnginePrefix) == 0);
		}
		entries.push_back({
			index,
			std::move(archivePathText),
			isDirectory,
			extractEntry,
			stat.m_uncomp_size });
	}
	if (archiveKind == PackageArchiveKind::Resource && manifestCount != 1)
	{
		result.status = ResourcePackageArchiveStatus::MissingManifest;
		return result;
	}
	if (archiveKind != PackageArchiveKind::Resource && manifestCount != 0)
	{
		result.status = ResourcePackageArchiveStatus::UnexpectedManifest;
		return result;
	}
	if (archiveKind == PackageArchiveKind::Common &&
		commonVersionCount > 1)
	{
		result.status = ResourcePackageArchiveStatus::CommonVersionMismatch;
		return result;
	}
	if (archiveKind == PackageArchiveKind::DesktopProgram &&
		!desktopProgramExecutableFound)
	{
		result.status = ResourcePackageArchiveStatus::MissingProgramExecutable;
		result.archiveEntryPath = desktopProgramExecutablePath;
		return result;
	}
	if (archiveKind == PackageArchiveKind::DesktopProgram &&
		!desktopEngineBootstrapFound)
	{
		result.status = ResourcePackageArchiveStatus::MissingEngineBootstrap;
		result.archiveEntryPath = "assets/engine/font/font.ttf";
		return result;
	}
	if (archiveKind == PackageArchiveKind::DesktopProgram &&
		!desktopCommonVersionFound)
	{
		result.status = ResourcePackageArchiveStatus::MissingCommonBootstrap;
		result.archiveEntryPath = "assets/common/version.ini";
		return result;
	}

	std::vector<const ArchiveEntry*> sortedEntries;
	sortedEntries.reserve(entries.size());
	for (const ArchiveEntry& entry : entries)
	{
		sortedEntries.push_back(&entry);
	}
	std::sort(sortedEntries.begin(), sortedEntries.end(),
		[](const ArchiveEntry* left, const ArchiveEntry* right)
		{
			return left->path < right->path;
		});
	for (std::size_t index = 0; index + 1 < sortedEntries.size(); ++index)
	{
		const ArchiveEntry& entry = *sortedEntries[index];
		const ArchiveEntry& next = *sortedEntries[index + 1];
		if (!entry.directory && next.path.size() > entry.path.size() &&
			next.path.compare(0, entry.path.size(), entry.path) == 0 &&
			next.path[entry.path.size()] == '/')
		{
			result.status =
				ResourcePackageArchiveStatus::DuplicateEntryPath;
			result.archiveEntryPath = entry.path;
			return result;
		}
	}

	std::error_code createRootError;
	const bool rootCreated = std::filesystem::create_directory(
		destinationPath, createRootError);
	if (!rootCreated || createRootError ||
		!isSafeExistingDirectory(destinationPath))
	{
		result.status = ResourcePackageArchiveStatus::DestinationCreateFailed;
		result.filesystemPath = destinationPath;
		return cleanupFailure(result, destinationPath, rootCreated);
	}
	const bool destinationCreated = true;

	for (const ArchiveEntry& entry : entries)
	{
		if (!entry.extract)
		{
			continue;
		}
		const std::filesystem::path relativePath =
			std::filesystem::u8path(entry.path);
		const std::filesystem::path outputPath =
			destinationPath / relativePath;
		const std::filesystem::path relativeParent =
			entry.directory ? relativePath : relativePath.parent_path();
		if (!ensureDirectoryTree(destinationPath, relativeParent))
		{
			result.status = ResourcePackageArchiveStatus::DestinationCreateFailed;
			result.archiveEntryPath = entry.path;
			result.filesystemPath = outputPath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		if (entry.directory)
		{
			continue;
		}
		std::error_code outputStatusError;
		const std::filesystem::file_status outputStatus =
			std::filesystem::symlink_status(outputPath, outputStatusError);
		if ((!outputStatusError && std::filesystem::exists(outputStatus)) ||
			(outputStatusError &&
				outputStatusError != std::errc::no_such_file_or_directory))
		{
			result.status = ResourcePackageArchiveStatus::ExtractionFailed;
			result.archiveEntryPath = entry.path;
			result.filesystemPath = outputPath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		FileOutput output;
		output.expectedSize = entry.uncompressedSize;
		output.stream.open(
			outputPath, std::ios::binary | std::ios::out | std::ios::trunc);
		if (!output.stream || !mz_zip_reader_extract_to_callback(
				&reader.archive,
				entry.index,
				writeExtractedFile,
				&output,
				0))
		{
			output.stream.close();
			result.status = ResourcePackageArchiveStatus::ExtractionFailed;
			result.archiveEntryPath = entry.path;
			result.filesystemPath = outputPath;
			result.archiveError = static_cast<int>(
				mz_zip_get_last_error(&reader.archive));
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		output.stream.flush();
		if (output.failed || !output.stream ||
			output.writtenBytes != output.expectedSize)
		{
			output.stream.close();
			result.status = ResourcePackageArchiveStatus::ExtractionFailed;
			result.archiveEntryPath = entry.path;
			result.filesystemPath = outputPath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
	}

	if (archiveKind == PackageArchiveKind::Common)
	{
		const std::filesystem::path versionPath =
			destinationPath / "version.ini";
		const std::string expectedVersionFile =
			"[Common]\nVersion=" +
			expectedCommonPackage->versionText + "\n";
		if (commonVersionCount == 0)
		{
			std::ofstream versionOutput(
				versionPath,
				std::ios::binary | std::ios::out | std::ios::trunc);
			versionOutput.write(
				expectedVersionFile.data(),
				static_cast<std::streamsize>(expectedVersionFile.size()));
			versionOutput.flush();
			if (!versionOutput)
			{
				versionOutput.close();
				result.status =
					ResourcePackageArchiveStatus::ExtractionFailed;
				result.filesystemPath = versionPath;
				return cleanupFailure(
					result, destinationPath, destinationCreated);
			}
		}
		std::vector<char> versionBytes;
		std::string installedVersion;
		if (!readManifest(
				versionPath,
				MaximumCommonVersionFileBytes,
				versionBytes) ||
			!parseCommonPackageVersion(
				std::string_view(versionBytes.data(), versionBytes.size()),
				installedVersion) ||
			installedVersion != expectedCommonPackage->versionText)
		{
			result.status =
				ResourcePackageArchiveStatus::CommonVersionMismatch;
			result.filesystemPath = versionPath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		result.status = ResourcePackageArchiveStatus::Success;
		result.filesystemPath = destinationPath;
		return result;
	}
	if (archiveKind == PackageArchiveKind::DesktopProgram)
	{
		const std::filesystem::path executablePath =
			destinationPath /
			std::filesystem::u8path(desktopProgramExecutablePath);
		std::error_code executableError;
		const std::filesystem::file_status executableStatus =
			std::filesystem::symlink_status(
				executablePath, executableError);
		if (executableError ||
			!std::filesystem::is_regular_file(executableStatus) ||
			std::filesystem::is_symlink(executableStatus))
		{
			result.status =
				ResourcePackageArchiveStatus::MissingProgramExecutable;
			result.filesystemPath = executablePath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		const std::filesystem::path engineFontPath =
			destinationPath / "assets" / "engine" / "font" / "font.ttf";
		std::error_code engineFontError;
		const std::filesystem::file_status engineFontStatus =
			std::filesystem::symlink_status(
				engineFontPath, engineFontError);
		if (engineFontError ||
			!std::filesystem::is_regular_file(engineFontStatus) ||
			std::filesystem::is_symlink(engineFontStatus))
		{
			result.status =
				ResourcePackageArchiveStatus::MissingEngineBootstrap;
			result.filesystemPath = engineFontPath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		const std::filesystem::path commonVersionPath =
			destinationPath / "assets" / "common" / "version.ini";
		std::error_code commonVersionError;
		const std::filesystem::file_status commonVersionStatus =
			std::filesystem::symlink_status(
				commonVersionPath, commonVersionError);
		if (commonVersionError ||
			!std::filesystem::is_regular_file(commonVersionStatus) ||
			std::filesystem::is_symlink(commonVersionStatus))
		{
			result.status =
				ResourcePackageArchiveStatus::MissingCommonBootstrap;
			result.filesystemPath = commonVersionPath;
			return cleanupFailure(
				result, destinationPath, destinationCreated);
		}
		if (requiredProgramExecutable == "jxqy-all-in-one")
		{
			std::filesystem::permissions(
				executablePath,
				std::filesystem::perms::owner_all |
					std::filesystem::perms::group_read |
					std::filesystem::perms::group_exec |
					std::filesystem::perms::others_read |
					std::filesystem::perms::others_exec,
				std::filesystem::perm_options::replace,
				executableError);
			if (executableError)
			{
				result.status =
					ResourcePackageArchiveStatus::ExtractionFailed;
				result.filesystemPath = executablePath;
				return cleanupFailure(
					result, destinationPath, destinationCreated);
			}
		}
		result.status = ResourcePackageArchiveStatus::Success;
		result.filesystemPath = destinationPath;
		return result;
	}

	const ResourcePackage& expectedPackage = *expectedResourcePackage;
	std::vector<char> manifestBytes;
	const std::filesystem::path manifestPath =
		destinationPath / "game_profile.ini";
	if (!readManifest(
			manifestPath, limits.maximumManifestBytes, manifestBytes))
	{
		result.status = ResourcePackageArchiveStatus::ManifestReadFailed;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}
	ResourceManifest manifest;
	const ResourceIniReader manifestIni(
		manifestBytes.data(), manifestBytes.size());
	if (manifestIni.parseError() != 0 ||
		!manifest.loadFromBuffer(
			manifestBytes.data(), static_cast<int>(manifestBytes.size())) ||
		!manifest.isValid())
	{
		result.status = ResourcePackageArchiveStatus::InvalidManifest;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}
	if (!sameIdentifier(manifest.id, expectedPackage.gameId))
	{
		result.status = ResourcePackageArchiveStatus::GameIdMismatch;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}
	if (manifest.releaseMetadata.displayVersion != expectedPackage.versionText)
	{
		result.status =
			ResourcePackageArchiveStatus::DisplayVersionMismatch;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}
	if (manifest.releaseMetadata.minimumEngineVersion !=
		expectedPackage.minimumEngineVersionText)
	{
		result.status =
			ResourcePackageArchiveStatus::MinimumEngineVersionMismatch;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}
	if (foldedDependencies(manifest.getDependencyIds()) !=
		foldedDependencies(expectedPackage.dependencyGameIds))
	{
		result.status = ResourcePackageArchiveStatus::DependencyMismatch;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}
	if (manifest.resourceOnly != expectedPackage.resourceOnly)
	{
		result.status = ResourcePackageArchiveStatus::ResourceOnlyMismatch;
		result.filesystemPath = manifestPath;
		return cleanupFailure(result, destinationPath, destinationCreated);
	}

	result.status = ResourcePackageArchiveStatus::Success;
	result.filesystemPath = destinationPath;
	return result;
}

ResourcePackageArchiveResult prepareResourcePackageArchive(
	const ResourcePackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits)
{
	if (!isValidOnlineGameId(expectedPackage.gameId) ||
		expectedPackage.versionText.empty() ||
		expectedPackage.minimumEngineVersionText.empty() ||
		!isSafeArtifactPath(expectedPackage.artifactPath))
	{
		ResourcePackageArchiveResult result;
		result.status = ResourcePackageArchiveStatus::InvalidInput;
		return result;
	}
	return preparePackageArchive(
		expectedPackage.artifactSize,
		expectedPackage.crc32Hex,
		&expectedPackage,
		nullptr,
		PackageArchiveKind::Resource,
		{},
		{},
		archivePath,
		destinationPath,
		limits);
}

ResourcePackageArchiveResult prepareCommonPackageArchive(
	const CommonPackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits)
{
	std::string parsedVersion;
	const std::string versionFile =
		"[Common]\nVersion=" + expectedPackage.versionText + "\n";
	if (!parseCommonPackageVersion(versionFile, parsedVersion) ||
		!isSafeArtifactPath(expectedPackage.artifactPath))
	{
		ResourcePackageArchiveResult result;
		result.status = ResourcePackageArchiveStatus::InvalidInput;
		return result;
	}
	return preparePackageArchive(
		expectedPackage.artifactSize,
		expectedPackage.crc32Hex,
		nullptr,
		&expectedPackage,
		PackageArchiveKind::Common,
		{},
		{},
		archivePath,
		destinationPath,
		limits);
}

ResourcePackageArchiveResult prepareDesktopProgramPackageArchive(
	const ProgramPackage& expectedPackage,
	const std::filesystem::path& archivePath,
	const std::filesystem::path& destinationPath,
	const ResourcePackageArchiveLimits& limits)
{
	std::string executableName;
	std::string binPrefix;
	if (expectedPackage.target == "windows")
	{
		executableName = "jxqy-all-in-one.exe";
		binPrefix = "bin/win32";
	}
	else if (expectedPackage.target == "linux")
	{
		executableName = "jxqy-all-in-one";
		binPrefix = "bin/linux";
	}
	else
	{
		ResourcePackageArchiveResult result;
		result.status = ResourcePackageArchiveStatus::InvalidInput;
		return result;
	}
	if (binPrefix.empty() ||
		!ModRelease::parseSemanticVersion(
			expectedPackage.versionText).succeeded() ||
		!ModRelease::isValidUpdateTargetIdentifier(expectedPackage.target) ||
		!isSafeArtifactPath(expectedPackage.artifactPath))
	{
		ResourcePackageArchiveResult result;
		result.status = ResourcePackageArchiveStatus::InvalidInput;
		return result;
	}
	return preparePackageArchive(
		expectedPackage.artifactSize,
		expectedPackage.crc32Hex,
		nullptr,
		nullptr,
		PackageArchiveKind::DesktopProgram,
		binPrefix,
		executableName,
		archivePath,
		destinationPath,
		limits);
}
}
