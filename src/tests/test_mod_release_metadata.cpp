#include "../File/RootedResourceReader.h"
#include "../File/StrictRelativeResourcePath.h"
#include "../Resource/ModReleaseAssets.h"
#include "../Resource/ModReleaseMetadata.h"
#include "../Resource/SemanticVersion.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
int failureCount = 0;
bool symlinkFollowChecked = false;

void expect(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		failureCount++;
	}
}

class TemporaryDirectory
{
public:
	TemporaryDirectory()
	{
		const std::filesystem::path temporaryRoot =
			std::filesystem::temp_directory_path();
		const auto timestamp =
			std::chrono::high_resolution_clock::now()
				.time_since_epoch().count();
		for (int attempt = 0; attempt < 100; ++attempt)
		{
			std::error_code error;
			directoryPath = temporaryRoot /
				("jxqy-mod-release-" + std::to_string(timestamp) +
					"-" + std::to_string(attempt));
			if (std::filesystem::create_directory(directoryPath, error))
			{
				return;
			}
		}
		directoryPath.clear();
	}

	~TemporaryDirectory()
	{
		if (!directoryPath.empty())
		{
			std::error_code error;
			std::filesystem::remove_all(directoryPath, error);
		}
	}

	const std::filesystem::path& path() const
	{
		return directoryPath;
	}

private:
	std::filesystem::path directoryPath;
};

void writeBytes(const std::filesystem::path& path,
	const std::vector<std::uint8_t>& bytes)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	if (!bytes.empty())
	{
		output.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	}
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
	writeBytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

ModRelease::SemanticVersion requireVersion(const std::string& text)
{
	const ModRelease::SemanticVersionParseResult parsed =
		ModRelease::parseSemanticVersion(text);
	expect(parsed.succeeded(), "SemVer parses: " + text);
	return parsed.version;
}

bool containsIssue(
	const std::vector<ModRelease::MetadataValidationIssue>& issues,
	ModRelease::MetadataField field,
	ModRelease::MetadataValidationError error)
{
	return std::any_of(issues.begin(), issues.end(),
		[field, error](const ModRelease::MetadataValidationIssue& issue)
		{
			return issue.field == field && issue.error == error;
		});
}

void testSemanticVersion()
{
	using namespace ModRelease;

	const SemanticVersionParseResult parsed =
		parseSemanticVersion("1.2.3-alpha.1+build.007");
	expect(parsed.succeeded(), "SemVer accepts prerelease and build identifiers");
	expect(parsed.version.major == 1 && parsed.version.minor == 2 &&
		parsed.version.patch == 3,
		"SemVer parses all core numbers");
	expect(formatSemanticVersion(parsed.version) ==
		"1.2.3-alpha.1+build.007",
		"SemVer formatting preserves canonical identifiers");
	expect(parseSemanticVersion("0.0.0").succeeded(),
		"SemVer accepts zero core values");
	expect(parseSemanticVersion(
		"18446744073709551615.0.0").succeeded(),
		"SemVer accepts the configured uint64 core maximum");
	expect(parseSemanticVersion(
		"18446744073709551616.0.0").error ==
			SemanticVersionParseError::NumericOverflow,
		"SemVer rejects core values above the configured uint64 limit");

	const std::vector<std::string> invalidVersions =
	{
		"",
		" 1.2.3",
		"1.2.3 ",
		"v1.2.3",
		"1.2",
		"1.2.3.4",
		"01.2.3",
		"1.02.3",
		"1.2.03",
		"1.2.3-",
		"1.2.3+",
		"1.2.3-alpha..1",
		"1.2.3-01",
		"1.2.3-alpha_beta",
		u8"1.2.3-预览"
	};
	for (const std::string& version : invalidVersions)
	{
		expect(!parseSemanticVersion(version).succeeded(),
			"SemVer rejects invalid input: " + version);
	}
	expect(parseSemanticVersion("1.2.3+01").succeeded(),
		"SemVer permits leading zeroes in build metadata");
	expect(parseSemanticVersion(
		"1.2.3-184467440737095516160").succeeded(),
		"SemVer accepts unbounded numeric prerelease identifiers");

	const std::vector<std::string> precedence =
	{
		"1.0.0-alpha",
		"1.0.0-alpha.1",
		"1.0.0-alpha.beta",
		"1.0.0-beta",
		"1.0.0-beta.2",
		"1.0.0-beta.11",
		"1.0.0-rc.1",
		"1.0.0"
	};
	for (std::size_t index = 1; index < precedence.size(); ++index)
	{
		expect(compareSemanticVersionPrecedence(
			requireVersion(precedence[index - 1]),
			requireVersion(precedence[index])) < 0,
			"SemVer follows precedence chain at index " +
				std::to_string(index));
	}
	expect(compareSemanticVersionPrecedence(
		requireVersion("1.0.0+first"),
		requireVersion("1.0.0+second")) == 0,
		"SemVer build metadata does not affect precedence");
	expect(compareSemanticVersionPrecedence(
		requireVersion("1.0.0-184467440737095516160"),
		requireVersion("1.0.0-184467440737095516161")) < 0,
		"SemVer compares long numeric prerelease identifiers exactly");
}

void testMetadataAndCompatibility()
{
	using namespace ModRelease;

	expect(isValidIsoReleaseDate("2024-02-29"),
		"release date accepts a leap day");
	expect(isValidIsoReleaseDate("2000-02-29"),
		"release date accepts a Gregorian century leap day");
	expect(!isValidIsoReleaseDate("2023-02-29"),
		"release date rejects a non-leap day");
	expect(!isValidIsoReleaseDate("1900-02-29"),
		"release date rejects a non-leap century day");
	expect(!isValidIsoReleaseDate("0000-01-01"),
		"release date rejects year zero");
	expect(!isValidIsoReleaseDate("2024-13-01") &&
		!isValidIsoReleaseDate("2024-04-31") &&
		!isValidIsoReleaseDate("2024-1-01") &&
		!isValidIsoReleaseDate("2024-01-01 "),
		"release date requires a real, exact YYYY-MM-DD date");

	ModReleaseMetadata metadata;
	expect(validateMetadata(metadata).empty(),
		"empty release metadata remains valid for legacy packs");
	expect(isValidUpdateTargetIdentifier("windows") &&
		isValidUpdateTargetIdentifier("android_arm64-v8a") &&
		!isValidUpdateTargetIdentifier("Windows-X64") &&
		!isValidUpdateTargetIdentifier("windows..x64") &&
		!isValidUpdateTargetIdentifier("windows/x64"),
		"program update targets use stable lowercase ASCII components");
	metadata.displayVersion = "1.041";
	metadata.releaseDate = "2024-02-29";
	metadata.minimumEngineVersion = "1.4.3";
	metadata.coverPath = u8"images/封面.png";
	metadata.descriptionFilePath = u8"docs/简介.txt";
	expect(validateMetadata(metadata).empty(),
		"display version is independent of strict SemVer");
	metadata.displayVersion = "bad\nversion";
	metadata.releaseDate = "2023-02-29";
	metadata.minimumEngineVersion = "1.04.3";
	metadata.coverPath = "../outside.png";
	metadata.descriptionFilePath = "/absolute.txt";
	const std::vector<MetadataValidationIssue> issues =
		validateMetadata(metadata);
	expect(issues.size() == 5,
		"metadata validation reports every invalid field");
	expect(containsIssue(issues, MetadataField::DisplayVersion,
		MetadataValidationError::ContainsControlCharacter),
		"metadata reports display control characters");
	expect(containsIssue(issues, MetadataField::ReleaseDate,
		MetadataValidationError::InvalidIsoDate),
		"metadata reports invalid calendar dates");
	expect(containsIssue(issues, MetadataField::MinimumEngineVersion,
		MetadataValidationError::InvalidSemanticVersion),
		"metadata reports invalid minimum engine SemVer");
	expect(containsIssue(issues, MetadataField::CoverPath,
		MetadataValidationError::UnsafeRelativePath) &&
		containsIssue(issues, MetadataField::DescriptionFilePath,
			MetadataValidationError::UnsafeRelativePath),
		"metadata reports both unsafe release paths");

	metadata = {};
	metadata.displayVersion = std::string("\xC3\x28", 2);
	const auto invalidUtf8Issues = validateMetadata(metadata);
	expect(containsIssue(invalidUtf8Issues, MetadataField::DisplayVersion,
		MetadataValidationError::InvalidUtf8),
		"metadata rejects invalid UTF-8 display versions");
	metadata.displayVersion = u8"version\u0085control";
	expect(containsIssue(validateMetadata(metadata),
		MetadataField::DisplayVersion,
		MetadataValidationError::ContainsControlCharacter),
		"metadata rejects Unicode C1 control characters");

	metadata = {};
	expect(evaluateCompatibility(metadata, "bad").status ==
		CompatibilityStatus::LegacyCompatible,
		"legacy packs do not require an engine comparison");
	metadata.displayVersion = "99.001";
	metadata.minimumEngineVersion = "1.4.2";
	expect(evaluateCompatibility(metadata, "1.4.3").status ==
		CompatibilityStatus::Compatible,
		"lower minimum engine versions are compatible");
	metadata.minimumEngineVersion = "1.4.3+pack";
	expect(evaluateCompatibility(metadata, "1.4.3+engine").status ==
		CompatibilityStatus::Compatible,
		"build metadata does not change compatibility");
	metadata.minimumEngineVersion = "1.4.4";
	expect(evaluateCompatibility(metadata, "1.4.3").status ==
		CompatibilityStatus::RequiresNewerEngine,
		"newer minimum engine versions are blocked");
	metadata.minimumEngineVersion = "1.4.3";
	expect(evaluateCompatibility(metadata, "1.4.3-alpha").status ==
		CompatibilityStatus::RequiresNewerEngine,
		"a stable minimum is newer than a matching prerelease engine");
	metadata.minimumEngineVersion = "1.4.3-alpha";
	expect(evaluateCompatibility(metadata, "1.4.3").status ==
		CompatibilityStatus::Compatible,
		"a stable engine satisfies a matching prerelease minimum");
	metadata.minimumEngineVersion = "1.04.3";
	const CompatibilityResult invalidMinimumResult =
		evaluateCompatibility(metadata, "1.4.3");
	expect(invalidMinimumResult.status ==
		CompatibilityStatus::InvalidMinimumEngineVersion,
		"invalid minimum engine version is reported as a distinct status");
	metadata.minimumEngineVersion = "1.4.3";
	expect(evaluateCompatibility(metadata, "invalid").status ==
		CompatibilityStatus::InvalidCurrentEngineVersion,
		"invalid current engine versions fail closed");
}

void expectPathStatus(const std::string& path,
	ResourcePathSafety::StrictRelativePathStatus expected,
	const std::string& message)
{
	expect(ResourcePathSafety::normalizeStrictRelativeResourcePath(path).status ==
		expected, message);
}

void testStrictRelativePaths()
{
	using ResourcePathSafety::StrictRelativePathStatus;

	const auto normalized =
		ResourcePathSafety::normalizeStrictRelativeResourcePath(
			u8"cover\\封面 1.041.png");
	expect(normalized.succeeded() &&
		normalized.normalizedPath == u8"cover/封面 1.041.png",
		"strict paths normalize backslashes and preserve UTF-8 names");
	const auto lowercaseNormalized =
		ResourcePathSafety::normalizeLowercaseStrictRelativeResourcePath(
			u8"Cover\\封面 A.PNG");
	expect(lowercaseNormalized.succeeded() &&
		lowercaseNormalized.normalizedPath == u8"cover/封面 a.png",
		"resource paths normalize ASCII letters to lowercase while preserving UTF-8 names");
	expectPathStatus("", StrictRelativePathStatus::Empty,
		"strict paths reject empty input");
	expectPathStatus("/cover.png",
		StrictRelativePathStatus::AbsoluteOrRooted,
		"strict paths reject a leading slash");
	expectPathStatus("\\cover.png",
		StrictRelativePathStatus::AbsoluteOrRooted,
		"strict paths reject a leading backslash");
	expectPathStatus("\\\\server\\share\\cover.png",
		StrictRelativePathStatus::AbsoluteOrRooted,
		"strict paths reject UNC paths");
	expectPathStatus("C:\\cover.png",
		StrictRelativePathStatus::AbsoluteOrRooted,
		"strict paths reject drive-absolute paths");
	expectPathStatus("C:cover.png",
		StrictRelativePathStatus::AbsoluteOrRooted,
		"strict paths reject drive-relative paths");
	expectPathStatus("cover/../outside.png",
		StrictRelativePathStatus::ParentTraversal,
		"strict paths reject parent traversal");
	expectPathStatus("cover/./image.png",
		StrictRelativePathStatus::EmptyOrDotSegment,
		"strict paths reject dot segments");
	expectPathStatus("cover//image.png",
		StrictRelativePathStatus::EmptyOrDotSegment,
		"strict paths reject empty segments");
	expectPathStatus("CON.png",
		StrictRelativePathStatus::ReservedWindowsName,
		"strict paths reject reserved Windows names");
	expectPathStatus("folder/LPT9",
		StrictRelativePathStatus::ReservedWindowsName,
		"strict paths reject numbered reserved Windows names");
	expectPathStatus(u8"folder/COM¹.txt",
		StrictRelativePathStatus::ReservedWindowsName,
		"strict paths reject Windows superscript COM device names");
	expectPathStatus(u8"folder/lpt³",
		StrictRelativePathStatus::ReservedWindowsName,
		"strict paths reject Windows superscript LPT device names");
	expectPathStatus("cover/image.",
		StrictRelativePathStatus::InvalidCharacter,
		"strict paths reject trailing dots");
	expectPathStatus("cover/image ",
		StrictRelativePathStatus::InvalidCharacter,
		"strict paths reject trailing spaces");
	expectPathStatus("cover/*.png",
		StrictRelativePathStatus::InvalidCharacter,
		"strict paths reject wildcard characters");
	expectPathStatus(std::string("cover/") + '\x01' + ".png",
		StrictRelativePathStatus::InvalidCharacter,
		"strict paths reject control characters");
	expectPathStatus(std::string("cover/") +
		std::string("\xC3\x28", 2),
		StrictRelativePathStatus::InvalidUtf8,
		"strict paths reject invalid UTF-8");
	expectPathStatus(u8"cover/\u0085.png",
		StrictRelativePathStatus::InvalidCharacter,
		"strict paths reject Unicode C1 control characters");
}

void testRootedReader(const std::filesystem::path& temporaryRoot)
{
	using namespace RootedResourceReader;

	const std::filesystem::path packRoot =
		temporaryRoot / std::filesystem::u8path(u8"资源包");
	const std::filesystem::path siblingRoot =
		temporaryRoot / std::filesystem::u8path(u8"资源包-escape");
	std::filesystem::create_directories(packRoot / "folder");
	std::filesystem::create_directories(siblingRoot);
	writeText(packRoot / std::filesystem::u8path(u8"folder/数据.bin"),
		std::string("A\0B", 3));
	writeText(packRoot / "empty.bin", "");
	writeText(siblingRoot / "outside.bin", "outside");

	Result result = readBoundedFileFromRoot(packRoot,
		u8"folder/数据.bin", 3);
	expect(result.succeeded() && result.bytes.size() == 3 &&
		result.bytes[0] == 'A' && result.bytes[1] == 0 &&
		result.bytes[2] == 'B',
		"rooted reader returns exact binary bytes from a UTF-8 path");
	expect(readBoundedFileFromRoot(packRoot,
		u8"folder/数据.bin", 2).status == Status::TooLarge,
		"rooted reader rejects limit plus one");
	expect(readBoundedFileFromRoot(packRoot,
		u8"folder/数据.bin", 3).status == Status::Success,
		"rooted reader accepts a file exactly at the limit");
	expect(readBoundedFileFromRoot(packRoot,
		u8"FOLDER/数据.BIN", 3).status == Status::Success,
		"rooted resource reads resolve mixed-case references through the lowercase on-disk path");
	expect(readBoundedFileFromRoot(packRoot,
		u8"folder/数据.bin", 4).status == Status::Success,
		"rooted reader accepts a file below the limit");
	expect(readBoundedFileFromRoot(packRoot,
		"empty.bin", 0).status == Status::Success,
		"rooted reader accepts an empty file at a zero-byte limit");

	const std::filesystem::path rewrittenPath =
		packRoot / "rewritten-during-read.bin";
	const std::string originalGeneration = "original-generation";
	const std::string replacementGeneration = "replacement-gener";
	writeText(rewrittenPath, originalGeneration);
	bool rewriteHookInvoked = false;
	bool rewriteSucceeded = false;
	setReadTestHookForTests(
		[&](ReadTestPhase phase)
		{
			if (phase !=
					ReadTestPhase::AfterRead ||
				rewriteHookInvoked)
			{
				return;
			}
			rewriteHookInvoked = true;
			std::ofstream output(
				rewrittenPath,
				std::ios::binary |
					std::ios::trunc);
			output.write(
				replacementGeneration.data(),
				static_cast<std::streamsize>(
					replacementGeneration.size()));
			rewriteSucceeded = output.good();
		});
	result = readBoundedFileFromRoot(
		packRoot,
		"rewritten-during-read.bin",
		originalGeneration.size());
	setReadTestHookForTests({});
	const std::vector<std::uint8_t> finalRewriteBytes =
		readBoundedFileFromRoot(
			packRoot,
			"rewritten-during-read.bin",
			std::max(
				originalGeneration.size(),
				replacementGeneration.size())).bytes;
	const std::string finalRewriteText(
		finalRewriteBytes.begin(),
		finalRewriteBytes.end());
	const bool rewriteGenerationHandled =
		rewriteHookInvoked &&
		(rewriteSucceeded
			? result.status == Status::Success &&
				std::string(
					result.bytes.begin(),
					result.bytes.end()) ==
					originalGeneration &&
				finalRewriteText ==
					replacementGeneration
			: result.status == Status::Success &&
				std::string(
					result.bytes.begin(),
					result.bytes.end()) ==
					originalGeneration &&
				finalRewriteText ==
					originalGeneration);
	expect(
		rewriteGenerationHandled,
		"open rooted reads do not block or reject a later author rewrite");

	result = readBoundedFileFromRoot(packRoot, "../outside.bin", 100);
	expect(result.status == Status::UnsafeRelativePath &&
		result.bytes.empty(),
		"rooted reader rejects traversal and clears its payload");
	result = readBoundedFileFromRoot(packRoot, "outside.bin", 100);
	expect(result.status == Status::NotFound && result.bytes.empty(),
		"rooted reader does not fall back to a sibling package");
	expect(readBoundedFileFromRoot(packRoot,
		"folder", 100).status == Status::NotRegularFile,
		"rooted reader rejects directories");
#ifndef _WIN32
	const std::filesystem::path fifoPath = packRoot / "named-pipe";
	expect(mkfifo(fifoPath.c_str(), 0600) == 0,
		"POSIX rooted reader fixture creates a FIFO");
	expect(readBoundedFileFromRoot(packRoot,
		"named-pipe", 100).status == Status::NotRegularFile,
		"rooted reader rejects a FIFO without blocking");
#endif
	expect(readBoundedFileFromRoot(packRoot,
		"missing.bin", 100).status == Status::NotFound,
		"rooted reader reports missing files");
	expect(readBoundedFileFromRoot(temporaryRoot / "missing-root",
		"file.bin", 100).status == Status::InvalidRoot,
		"rooted reader rejects a missing root");
	writeText(temporaryRoot / "root-file", "not a directory");
	expect(readBoundedFileFromRoot(temporaryRoot / "root-file",
		"file.bin", 100).status == Status::InvalidRoot,
		"rooted reader rejects a regular file as root");

	const std::filesystem::path ancestorTarget =
		temporaryRoot / "root-ancestor-target";
	const std::filesystem::path replacementAncestorTarget =
		temporaryRoot / "root-ancestor-replacement-target";
	const std::filesystem::path ancestorLink =
		temporaryRoot / "root-ancestor-link";
	std::filesystem::create_directories(
		ancestorTarget / "pack");
	std::filesystem::create_directories(
		replacementAncestorTarget / "pack");
	writeText(
		ancestorTarget / "pack" / "inside.bin",
		"inside");
	writeText(
		replacementAncestorTarget / "pack" / "inside.bin",
		"replacement");
	std::error_code ancestorLinkError;
	std::filesystem::create_directory_symlink(
		ancestorTarget,
		ancestorLink,
		ancestorLinkError);
	if (ancestorLinkError)
	{
		std::cout <<
			"SKIP: ancestor-link fixture unavailable: "
			<< ancestorLinkError.message() << std::endl;
	}
	else
	{
		const Result linkedRead =
			readBoundedFileFromRoot(
				ancestorLink / "pack",
				"inside.bin",
				100);
		expect(
			linkedRead.succeeded() &&
			std::string(
				linkedRead.bytes.begin(),
				linkedRead.bytes.end()) ==
					"inside" &&
			probeRegularFileFromRoot(
				ancestorLink / "pack",
				"inside.bin").status ==
				Status::Success,
			"rooted path reader follows the formal root's current symlink or junction target");
		std::error_code removeError;
		std::filesystem::remove(
			ancestorLink,
			removeError);
		std::error_code replacementLinkError;
		if (!removeError)
		{
			std::filesystem::create_directory_symlink(
				replacementAncestorTarget,
				ancestorLink,
				replacementLinkError);
		}
		expect(
			!removeError && !replacementLinkError,
			"rooted path reader fixture repoints the formal root link");
		if (!removeError && !replacementLinkError)
		{
			const Result replacementRead =
				readBoundedFileFromRoot(
					ancestorLink / "pack",
					"inside.bin",
					100);
			expect(
				replacementRead.succeeded() &&
				std::string(
					replacementRead.bytes.begin(),
					replacementRead.bytes.end()) ==
						"replacement",
				"each rooted path read resolves the formal root's latest target");
			std::filesystem::remove(
				ancestorLink,
				removeError);
		}
	}

	const std::filesystem::path outsideFile = siblingRoot / "outside.bin";
	const std::filesystem::path linkPath = packRoot / "outside-link.bin";
	std::error_code linkError;
	std::filesystem::create_symlink(outsideFile, linkPath, linkError);
	if (linkError)
	{
		std::cout << "SKIP: Windows symlink escape fixture unavailable: "
			<< linkError.message() << std::endl;
	}
	else
	{
		symlinkFollowChecked = true;
		result = readBoundedFileFromRoot(packRoot,
			"outside-link.bin", 100);
		expect(result.succeeded() &&
			std::string(
				result.bytes.begin(),
				result.bytes.end()) == "outside",
			"open rooted reads follow the current formal file link target");
		std::error_code removeError;
		std::filesystem::remove(linkPath, removeError);
	}
}

std::vector<std::uint8_t> makePngHeader(
	std::uint32_t width, std::uint32_t height)
{
	std::vector<std::uint8_t> bytes(24, 0);
	const std::uint8_t signature[] =
		{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	std::copy(std::begin(signature), std::end(signature), bytes.begin());
	bytes[11] = 13;
	bytes[12] = 'I';
	bytes[13] = 'H';
	bytes[14] = 'D';
	bytes[15] = 'R';
	bytes[16] = static_cast<std::uint8_t>((width >> 24) & 0xFF);
	bytes[17] = static_cast<std::uint8_t>((width >> 16) & 0xFF);
	bytes[18] = static_cast<std::uint8_t>((width >> 8) & 0xFF);
	bytes[19] = static_cast<std::uint8_t>(width & 0xFF);
	bytes[20] = static_cast<std::uint8_t>((height >> 24) & 0xFF);
	bytes[21] = static_cast<std::uint8_t>((height >> 16) & 0xFF);
	bytes[22] = static_cast<std::uint8_t>((height >> 8) & 0xFF);
	bytes[23] = static_cast<std::uint8_t>(height & 0xFF);
	return bytes;
}

void testReleaseAssets(const std::filesystem::path& temporaryRoot)
{
	using namespace ModRelease;

	expect(isValidDescriptionUtf8(u8"正文\r\n第二行\tOK"),
		"description validation permits UTF-8 text and CR/LF/TAB");
	expect(!isValidDescriptionUtf8(std::string("before") + '\x7F' + "after") &&
		!isValidDescriptionUtf8(u8"before\u0085after"),
		"description validation rejects DEL and Unicode C1 controls");

	const std::filesystem::path packRoot = temporaryRoot / "asset-pack";
	const std::filesystem::path dependencyRoot =
		temporaryRoot / "dependency-pack";
	std::filesystem::create_directories(packRoot / "docs");
	std::filesystem::create_directories(packRoot / "images");
	std::filesystem::create_directories(dependencyRoot / "docs");

	ModReleaseMetadata metadata;
	expect(readDescriptionFromPack(packRoot, metadata).status ==
		AssetReadStatus::NotDeclared,
		"undeclared descriptions remain optional");
	expect(readCoverFromPack(packRoot, metadata).status ==
		AssetReadStatus::NotDeclared,
		"undeclared covers remain optional");

	const std::string description =
		std::string("\xEF\xBB\xBF", 3) +
		u8"第一行\r\n第二行\tOK";
	writeText(packRoot / std::filesystem::u8path(u8"docs/简介.txt"),
		description);
	metadata.descriptionFilePath = u8"DOCS/简介.TXT";
	DescriptionReadResult descriptionResult =
		readDescriptionFromPack(packRoot, metadata);
	expect(descriptionResult.succeeded() &&
		descriptionResult.utf8Text == u8"第一行\r\n第二行\tOK",
		"description reader strips a UTF-8 BOM and permits CR/LF/TAB");

	writeBytes(packRoot / "docs" / "invalid-utf8.txt",
		{ 0xC3, 0x28 });
	metadata.descriptionFilePath = "docs/invalid-utf8.txt";
	descriptionResult = readDescriptionFromPack(packRoot, metadata);
	expect(descriptionResult.status == AssetReadStatus::InvalidUtf8 &&
		descriptionResult.utf8Text.empty(),
		"description reader rejects invalid UTF-8 and clears text");

	writeBytes(packRoot / "docs" / "embedded-control.txt",
		{ 'a', 0, 'b' });
	metadata.descriptionFilePath = "docs/embedded-control.txt";
	descriptionResult = readDescriptionFromPack(packRoot, metadata);
	expect(descriptionResult.status == AssetReadStatus::InvalidText &&
		descriptionResult.utf8Text.empty(),
		"description reader rejects embedded NUL bytes");
	writeText(packRoot / "docs" / "unicode-control.txt",
		u8"before\u0085after");
	metadata.descriptionFilePath = "docs/unicode-control.txt";
	descriptionResult = readDescriptionFromPack(packRoot, metadata);
	expect(descriptionResult.status == AssetReadStatus::InvalidText &&
		descriptionResult.utf8Text.empty(),
		"description reader rejects Unicode C1 control characters");

	writeText(packRoot / "docs" / "maximum.txt",
		std::string(MaximumDescriptionBytes, 'a'));
	metadata.descriptionFilePath = "docs/maximum.txt";
	descriptionResult = readDescriptionFromPack(packRoot, metadata);
	expect(descriptionResult.succeeded() &&
		descriptionResult.utf8Text.size() == MaximumDescriptionBytes,
		"description reader accepts exactly 64 KiB");

	writeText(packRoot / "docs" / "too-large.txt",
		std::string(MaximumDescriptionBytes + 1, 'a'));
	metadata.descriptionFilePath = "docs/too-large.txt";
	descriptionResult = readDescriptionFromPack(packRoot, metadata);
	expect(descriptionResult.status == AssetReadStatus::TooLarge &&
		descriptionResult.utf8Text.empty(),
		"description reader rejects more than 64 KiB");

	writeText(dependencyRoot / "docs" / "fallback.txt", "dependency");
	metadata.descriptionFilePath = "docs/fallback.txt";
	expect(readDescriptionFromPack(packRoot, metadata).status ==
		AssetReadStatus::NotFound,
		"description reader does not use dependency fallback");
	metadata.descriptionFilePath = "../dependency-pack/docs/fallback.txt";
	expect(readDescriptionFromPack(packRoot, metadata).status ==
		AssetReadStatus::UnsafePath,
		"description reader rejects paths outside the pack");

	const std::vector<std::uint8_t> safePng = makePngHeader(1, 1);
	writeBytes(packRoot / "images" / "cover.jpg", safePng);
	metadata.coverPath = "IMAGES/COVER.JPG";
	CoverReadResult coverResult = readCoverFromPack(packRoot, metadata);
	expect(coverResult.readyForDecode() &&
		coverResult.dimensions.format == EncodedImageSafety::Format::Png &&
		coverResult.dimensions.width == 1 &&
		coverResult.dimensions.height == 1 &&
		coverResult.encodedBytes == safePng,
		"cover reader trusts safe encoded content rather than extension");

	writeText(packRoot / "images" / "invalid.png", "not an image");
	metadata.coverPath = "images/invalid.png";
	coverResult = readCoverFromPack(packRoot, metadata);
	expect(coverResult.status == AssetReadStatus::InvalidImage &&
		coverResult.encodedBytes.empty(),
		"cover reader rejects invalid image content and clears bytes");

	writeBytes(packRoot / "images" / "huge.png",
		makePngHeader(100000, 100000));
	metadata.coverPath = "images/huge.png";
	coverResult = readCoverFromPack(packRoot, metadata);
	expect(coverResult.status == AssetReadStatus::InvalidImage &&
		coverResult.encodedBytes.empty(),
		"cover reader rejects unsafe decoded dimensions");

	metadata.coverPath = "../outside.png";
	expect(readCoverFromPack(packRoot, metadata).status ==
		AssetReadStatus::UnsafePath,
		"cover reader rejects paths outside the pack");
}

void testPackagedReleaseAssets()
{
	using namespace ModRelease;

	ModReleaseMetadata metadata;
	int readerCallCount = 0;
	std::string requestedPath;
	std::size_t requestedMaximumBytes = 0;
	const std::string description =
		std::string("\xEF\xBB\xBF", 3) +
		u8"打包简介\r\n第二行";
	const PackagedAssetReader descriptionReader =
		[&](std::string_view packagedPathUtf8,
			std::size_t maximumBytes)
		{
			readerCallCount++;
			requestedPath.assign(packagedPathUtf8);
			requestedMaximumBytes = maximumBytes;
			PackagedAssetReadResult result;
			result.status = AssetReadStatus::Ready;
			result.bytes.assign(description.begin(), description.end());
			return result;
		};

	expect(readDescriptionFromPackagedAssets(
			"mods/example/", metadata, descriptionReader).status ==
			AssetReadStatus::NotDeclared &&
		readerCallCount == 0,
		"packaged reader keeps undeclared descriptions optional");

	metadata.descriptionFilePath = u8"DOCS\\简介.TXT";
	DescriptionReadResult descriptionResult =
		readDescriptionFromPackagedAssets(
			u8"MODS\\示例包/", metadata, descriptionReader);
	expect(descriptionResult.succeeded() &&
		descriptionResult.utf8Text == u8"打包简介\r\n第二行" &&
		readerCallCount == 1 &&
		requestedPath == u8"mods/示例包/docs/简介.txt" &&
		requestedMaximumBytes == MaximumDescriptionBytes,
		"packaged description reader joins one normalized pack-local path");

	readerCallCount = 0;
	requestedPath.clear();
	descriptionResult = readDescriptionFromPackagedAssets(
		"", metadata, descriptionReader);
	expect(descriptionResult.succeeded() &&
		readerCallCount == 1 &&
		requestedPath == u8"docs/简介.txt",
		"packaged description reader supports a pack at the asset namespace root");

	readerCallCount = 0;
	descriptionResult = readDescriptionFromPackagedAssets(
		"../outside", metadata, descriptionReader);
	expect(descriptionResult.status == AssetReadStatus::InvalidRoot &&
		descriptionResult.utf8Text.empty() &&
		readerCallCount == 0,
		"packaged reader rejects a traversing pack root before transport");
	descriptionResult = readDescriptionFromPackagedAssets(
		"/absolute-pack", metadata, descriptionReader);
	expect(descriptionResult.status == AssetReadStatus::InvalidRoot &&
		readerCallCount == 0,
		"packaged reader rejects an absolute pack root before transport");

	metadata.descriptionFilePath = "../common/description.txt";
	descriptionResult = readDescriptionFromPackagedAssets(
		"mods/example", metadata, descriptionReader);
	expect(descriptionResult.status == AssetReadStatus::UnsafePath &&
		descriptionResult.utf8Text.empty() &&
		readerCallCount == 0,
		"packaged reader rejects a description path that leaves its pack");

	metadata.descriptionFilePath = "docs/missing.txt";
	readerCallCount = 0;
	const PackagedAssetReader missingReader =
		[&](std::string_view packagedPathUtf8,
			std::size_t maximumBytes)
		{
			readerCallCount++;
			requestedPath.assign(packagedPathUtf8);
			requestedMaximumBytes = maximumBytes;
			PackagedAssetReadResult result;
			result.status = AssetReadStatus::NotFound;
			result.bytes = { 's', 't', 'a', 'l', 'e' };
			return result;
		};
	descriptionResult = readDescriptionFromPackagedAssets(
		"mods/example", metadata, missingReader);
	expect(descriptionResult.status == AssetReadStatus::NotFound &&
		descriptionResult.utf8Text.empty() &&
		readerCallCount == 1 &&
		requestedPath == "mods/example/docs/missing.txt" &&
		requestedMaximumBytes == MaximumDescriptionBytes,
		"packaged reader performs one pack-local read without Common fallback");

	const PackagedAssetReader oversizedReader =
		[](std::string_view, std::size_t maximumBytes)
		{
			PackagedAssetReadResult result;
			result.status = AssetReadStatus::Ready;
			result.bytes.resize(maximumBytes + 1);
			return result;
		};
	descriptionResult = readDescriptionFromPackagedAssets(
		"mods/example", metadata, oversizedReader);
	expect(descriptionResult.status == AssetReadStatus::TooLarge &&
		descriptionResult.utf8Text.empty(),
		"packaged wrapper enforces its byte limit even if a reader overreturns");

	const PackagedAssetReader exactLimitReader =
		[](std::string_view, std::size_t maximumBytes)
		{
			PackagedAssetReadResult result;
			result.status = AssetReadStatus::Ready;
			result.bytes.assign(maximumBytes, 'a');
			return result;
		};
	descriptionResult = readDescriptionFromPackagedAssets(
		"mods/example", metadata, exactLimitReader);
	expect(descriptionResult.succeeded() &&
		descriptionResult.utf8Text.size() ==
			MaximumDescriptionBytes,
		"packaged wrapper accepts an exact-limit stream after its reader confirms EOF");

	descriptionResult = readDescriptionFromPackagedAssets(
		"mods/example", metadata, {});
	expect(descriptionResult.status == AssetReadStatus::ReadFailed &&
		descriptionResult.utf8Text.empty(),
		"packaged wrapper rejects an unavailable transport");
	const PackagedAssetReader throwingReader =
		[](std::string_view, std::size_t)
			-> PackagedAssetReadResult
		{
			throw std::runtime_error("reader failure");
		};
	descriptionResult = readDescriptionFromPackagedAssets(
		"mods/example", metadata, throwingReader);
	expect(descriptionResult.status == AssetReadStatus::ReadFailed &&
		descriptionResult.utf8Text.empty(),
		"packaged wrapper contains transport exceptions");

	const std::vector<std::uint8_t> safePng = makePngHeader(2, 3);
	metadata.coverPath = "images/cover.png";
	readerCallCount = 0;
	const PackagedAssetReader coverReader =
		[&](std::string_view packagedPathUtf8,
			std::size_t maximumBytes)
		{
			readerCallCount++;
			requestedPath.assign(packagedPathUtf8);
			requestedMaximumBytes = maximumBytes;
			PackagedAssetReadResult result;
			result.status = AssetReadStatus::Ready;
			result.bytes = safePng;
			return result;
		};
	const CoverReadResult coverResult =
		readCoverFromPackagedAssets(
			"mods/example/", metadata, coverReader);
	expect(coverResult.readyForDecode() &&
		coverResult.encodedBytes == safePng &&
		coverResult.dimensions.width == 2 &&
		coverResult.dimensions.height == 3 &&
		readerCallCount == 1 &&
		requestedPath == "mods/example/images/cover.png" &&
		requestedMaximumBytes ==
			EncodedImageSafety::MaxEncodedImageBytes,
		"packaged cover reader preserves image safety and its encoded-byte limit");
}
}

int main()
{
	TemporaryDirectory temporaryDirectory;
	if (temporaryDirectory.path().empty())
	{
		std::cerr << "FAIL: could not create a temporary directory" << std::endl;
		return 1;
	}

	testSemanticVersion();
	testMetadataAndCompatibility();
	testStrictRelativePaths();
	testRootedReader(temporaryDirectory.path());
	testReleaseAssets(temporaryDirectory.path());
	testPackagedReleaseAssets();

	if (failureCount != 0)
	{
		std::cerr << failureCount << " mod release core test(s) failed"
			<< std::endl;
		return 1;
	}

	std::cout << "All mod release core tests passed";
	if (symlinkFollowChecked)
	{
		std::cout << " (formal symlink follow checked)";
	}
	std::cout << std::endl;
	return 0;
}
