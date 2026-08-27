#include "ResourceInstallTransaction.h"

#include "OnlineUpdateCatalog.h"
#include "../File/ResourcePathSafety.h"
#include "../Resource/ResourceIniReader.h"
#include "../Resource/ResourceManifest.h"

#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
constexpr std::size_t MaximumTransactionBytes = 1024 * 1024;
constexpr std::size_t MaximumManifestBytes = 16 * 1024 * 1024;
constexpr std::size_t MaximumTransactionTargets = 512;
constexpr std::size_t MaximumTargetDirectoryNameBytes = 255;
constexpr const char* RecordFileName = "install.ini";

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

std::string encodeHex(const std::string& value)
{
	static constexpr char Digits[] = "0123456789abcdef";
	std::string encoded;
	encoded.reserve(value.size() * 2);
	for (unsigned char byte : value)
	{
		encoded.push_back(Digits[byte >> 4]);
		encoded.push_back(Digits[byte & 0x0F]);
	}
	return encoded;
}

bool decodeHex(const std::string& encoded, std::string& value)
{
	value.clear();
	if (encoded.empty() || encoded.size() % 2 != 0)
	{
		return false;
	}
	const auto nibble = [](char character) -> int
	{
		if (character >= '0' && character <= '9')
		{
			return character - '0';
		}
		if (character >= 'a' && character <= 'f')
		{
			return character - 'a' + 10;
		}
		if (character >= 'A' && character <= 'F')
		{
			return character - 'A' + 10;
		}
		return -1;
	};
	value.reserve(encoded.size() / 2);
	for (std::size_t index = 0; index < encoded.size(); index += 2)
	{
		const int high = nibble(encoded[index]);
		const int low = nibble(encoded[index + 1]);
		if (high < 0 || low < 0)
		{
			value.clear();
			return false;
		}
		value.push_back(static_cast<char>((high << 4) | low));
	}
	return true;
}

bool isSafeTargetDirectoryName(
	const std::string& name,
	bool allowCommon = false)
{
	if (name.empty() || name.size() > MaximumTargetDirectoryNameBytes ||
		name == "." || name == ".." ||
		name.find('/') != std::string::npos ||
		name.find('\\') != std::string::npos ||
		!ResourcePathSafety::isSafeVirtualResourcePath(name))
	{
		return false;
	}
	const std::string folded = foldAscii(name);
	return folded != ".jxqy-update" &&
		(allowCommon || folded != "common") &&
		folded != "save" && folded != ".git" &&
		folded != ".jxqy_editor";
}

bool isPlainDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error || !std::filesystem::is_directory(status) ||
		std::filesystem::is_symlink(status))
	{
		return false;
	}
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
	return true;
#endif
}

bool resolvePlainCollectionRoot(
	const std::filesystem::path& requestedRoot,
	std::filesystem::path& resolvedRoot)
{
	resolvedRoot.clear();
	if (requestedRoot.empty())
	{
		return false;
	}
	std::error_code error;
	resolvedRoot = std::filesystem::canonical(requestedRoot, error);
	return !error && isPlainDirectory(resolvedRoot);
}

bool isPlainRegularFile(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error || !std::filesystem::is_regular_file(status) ||
		std::filesystem::is_symlink(status))
	{
		return false;
	}
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
	return true;
#endif
}

bool isPlainWorkspaceLayout(const std::filesystem::path& collectionRoot)
{
	return isPlainDirectory(collectionRoot) &&
		isPlainDirectory(
			OnlineUpdate::resourceUpdateDirectoryPath(collectionRoot)) &&
		isPlainDirectory(
			OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot));
}

bool pathEntryExists(
	const std::filesystem::path& path,
	bool& accessible)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error == std::errc::no_such_file_or_directory)
	{
		accessible = true;
		return false;
	}
	accessible = !error;
	return !error && std::filesystem::exists(status);
}

bool readBoundedFile(
	const std::filesystem::path& path,
	std::size_t maximumBytes,
	std::string& bytes)
{
	bytes.clear();
	if (!isPlainRegularFile(path))
	{
		return false;
	}
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	if (error || size == 0 || size > maximumBytes)
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
	return input.gcount() == static_cast<std::streamsize>(bytes.size());
}

bool readManifestId(
	const std::filesystem::path& resourceRoot,
	std::string& gameId)
{
	std::string bytes;
	if (!readBoundedFile(
			resourceRoot / "game_profile.ini",
			MaximumManifestBytes,
			bytes))
	{
		return false;
	}
	ResourceManifest manifest;
	if (!manifest.loadFromBuffer(
			bytes.data(), static_cast<int>(bytes.size())) ||
		manifest.id.empty())
	{
		return false;
	}
	gameId = manifest.id;
	return true;
}

bool resourceRootHasGameId(
	const std::filesystem::path& root,
	const std::string& expectedGameId)
{
	std::string gameId;
	return isPlainDirectory(root) && readManifestId(root, gameId) &&
		foldAscii(gameId) == foldAscii(expectedGameId);
}

bool commonResourceRootIsValid(const std::filesystem::path& root)
{
	if (!isPlainDirectory(root) ||
		!isPlainRegularFile(root / "version.ini"))
	{
		return false;
	}
	std::error_code error;
	const std::filesystem::file_status manifestStatus =
		std::filesystem::symlink_status(root / "game_profile.ini", error);
	return error == std::errc::no_such_file_or_directory ||
		(!error && !std::filesystem::exists(manifestStatus));
}

bool isCommonInstallTarget(
	const OnlineUpdate::ResourceInstallTarget& target)
{
	return foldAscii(target.gameId) == "common" &&
		target.targetDirectoryName == "common";
}

bool installRootMatchesTarget(
	const std::filesystem::path& root,
	const OnlineUpdate::ResourceInstallTarget& target)
{
	return isCommonInstallTarget(target)
		? commonResourceRootIsValid(root)
		: resourceRootHasGameId(root, target.gameId);
}

std::filesystem::path recordPath(
	const std::filesystem::path& collectionRoot)
{
	return OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot) /
		RecordFileName;
}

std::filesystem::path preparedPath(
	const std::filesystem::path& workspacePath,
	std::size_t index)
{
	return workspacePath / "prepared" /
		("package-" + std::to_string(index));
}

std::filesystem::path previousPath(
	const std::filesystem::path& workspacePath,
	std::size_t index)
{
	return workspacePath / "previous" /
		("package-" + std::to_string(index));
}

const char* stateText(OnlineUpdate::ResourceInstallTransactionState state)
{
	switch (state)
	{
	case OnlineUpdate::ResourceInstallTransactionState::Ready:
		return "Ready";
	case OnlineUpdate::ResourceInstallTransactionState::Switching:
		return "Switching";
	case OnlineUpdate::ResourceInstallTransactionState::AwaitingValidation:
		return "AwaitingValidation";
	case OnlineUpdate::ResourceInstallTransactionState::Committed:
		return "Committed";
	case OnlineUpdate::ResourceInstallTransactionState::None:
	default:
		return "None";
	}
}

bool parseState(
	const std::string& text,
	OnlineUpdate::ResourceInstallTransactionState& state)
{
	if (text == "Ready")
	{
		state = OnlineUpdate::ResourceInstallTransactionState::Ready;
		return true;
	}
	if (text == "Switching")
	{
		state = OnlineUpdate::ResourceInstallTransactionState::Switching;
		return true;
	}
	if (text == "AwaitingValidation")
	{
		state = OnlineUpdate::ResourceInstallTransactionState::AwaitingValidation;
		return true;
	}
	if (text == "Committed")
	{
		state = OnlineUpdate::ResourceInstallTransactionState::Committed;
		return true;
	}
	return false;
}

bool replaceFile(
	const std::filesystem::path& temporaryPath,
	const std::filesystem::path& destinationPath)
{
#if defined(_WIN32)
	return MoveFileExW(
		temporaryPath.c_str(),
		destinationPath.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
	std::error_code error;
	std::filesystem::rename(temporaryPath, destinationPath, error);
	return !error;
#endif
}

bool writeRecord(
	const std::filesystem::path& collectionRoot,
	OnlineUpdate::ResourceInstallTransactionState state,
	const std::string& requestedGameId,
	const std::vector<OnlineUpdate::ResourceInstallTarget>& targets)
{
	const std::filesystem::path destination = recordPath(collectionRoot);
	const std::filesystem::path temporary =
		destination.parent_path() / "install.ini.tmp";
	std::ostringstream text;
	text << "[Transaction]\n"
		<< "SchemaVersion=1\n"
		<< "State=" << stateText(state) << "\n"
		<< "RequestedGameIdHex=" << encodeHex(requestedGameId) << "\n"
		<< "PackageCount=" << targets.size() << "\n";
	for (std::size_t index = 0; index < targets.size(); index++)
	{
		text << "\n[Package." << index << "]\n"
			<< "GameIdHex=" << encodeHex(targets[index].gameId) << "\n"
			<< "TargetDirectoryHex="
			<< encodeHex(targets[index].targetDirectoryName) << "\n"
			<< "HadExisting=" << (targets[index].hadExistingTarget ? 1 : 0)
			<< "\n";
	}
	const std::string bytes = text.str();
	if (bytes.size() > MaximumTransactionBytes)
	{
		return false;
	}
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			return false;
		}
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		output.flush();
		if (!output)
		{
			return false;
		}
	}
	if (!replaceFile(temporary, destination))
	{
		std::error_code cleanupError;
		std::filesystem::remove(temporary, cleanupError);
		return false;
	}
	return true;
}

OnlineUpdate::ResourceInstallTransactionResult readRecord(
	const std::filesystem::path& collectionRoot)
{
	OnlineUpdate::ResourceInstallTransactionResult result;
	const std::filesystem::path path = recordPath(collectionRoot);
	result.filesystemPath = path;
	std::error_code error;
	if (!std::filesystem::exists(path, error))
	{
		result.status = error
			? OnlineUpdate::ResourceInstallTransactionStatus::RecordUnavailable
			: OnlineUpdate::ResourceInstallTransactionStatus::NoTransaction;
		return result;
	}
	std::string bytes;
	if (!readBoundedFile(path, MaximumTransactionBytes, bytes))
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::RecordUnavailable;
		return result;
	}
	ResourceIniReader ini(bytes.data(), bytes.size());
	if (ini.parseError() != 0 ||
		ini.getInteger("Transaction", "SchemaVersion", 0) != 1 ||
		!parseState(ini.get("Transaction", "State", ""), result.state))
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::RecordInvalid;
		return result;
	}
	if (!decodeHex(
			ini.get("Transaction", "RequestedGameIdHex", ""),
			result.requestedGameId))
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::RecordInvalid;
		return result;
	}
	const long packageCount =
		ini.getInteger("Transaction", "PackageCount", 0);
	if (!OnlineUpdate::isValidOnlineGameId(result.requestedGameId) ||
		packageCount <= 0 ||
		packageCount > static_cast<long>(MaximumTransactionTargets))
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::RecordInvalid;
		return result;
	}
	std::set<std::string> gameIds;
	std::set<std::string> targetNames;
	bool requestedTargetFound = false;
	result.targets.reserve(static_cast<std::size_t>(packageCount));
	const std::filesystem::path workspacePath =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	for (long index = 0; index < packageCount; index++)
	{
		const std::string section =
			"Package." + std::to_string(index);
		OnlineUpdate::ResourceInstallTarget target;
		if (!decodeHex(ini.get(section, "GameIdHex", ""), target.gameId) ||
			!decodeHex(
				ini.get(section, "TargetDirectoryHex", ""),
				target.targetDirectoryName))
		{
			result.status =
				OnlineUpdate::ResourceInstallTransactionStatus::RecordInvalid;
			return result;
		}
		target.hadExistingTarget =
			ini.getBoolean(section, "HadExisting", false);
		const std::string foldedGameId = foldAscii(target.gameId);
		const std::string foldedTarget =
			foldAscii(target.targetDirectoryName);
		if (!OnlineUpdate::isValidOnlineGameId(target.gameId) ||
			!isSafeTargetDirectoryName(
				target.targetDirectoryName, isCommonInstallTarget(target)) ||
			!gameIds.insert(foldedGameId).second ||
			!targetNames.insert(foldedTarget).second)
		{
			result.status =
				OnlineUpdate::ResourceInstallTransactionStatus::RecordInvalid;
			result.failedGameId = target.gameId;
			return result;
		}
		requestedTargetFound = requestedTargetFound ||
			foldedGameId == foldAscii(result.requestedGameId);
		result.targets.push_back(std::move(target));
	}
	if (!requestedTargetFound)
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::RecordInvalid;
		return result;
	}
	result.status = OnlineUpdate::ResourceInstallTransactionStatus::Success;
	return result;
}

bool removeTree(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	if (error == std::errc::no_such_file_or_directory)
	{
		return true;
	}
	if (error || !std::filesystem::is_directory(status) ||
		std::filesystem::is_symlink(status))
	{
		return false;
	}
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES ||
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		return false;
	}
#endif
	std::filesystem::remove_all(path, error);
	return !error;
}

void removeEmptyUpdateDirectory(
	const std::filesystem::path& collectionRoot)
{
	std::error_code error;
	std::filesystem::remove(
		OnlineUpdate::resourceUpdateDirectoryPath(collectionRoot),
		error);
}

bool removeWorkspace(
	const std::filesystem::path& collectionRoot)
{
	if (!removeTree(
			OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot)))
	{
		return false;
	}
	removeEmptyUpdateDirectory(collectionRoot);
	return true;
}

OnlineUpdate::ResourceInstallTransactionResult discardReadyTransaction(
	const std::filesystem::path& collectionRoot,
	OnlineUpdate::ResourceInstallTransactionResult result,
	OnlineUpdate::ResourceInstallTransactionStatus failureStatus)
{
	const std::filesystem::path workspacePath =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	if (!removeWorkspace(collectionRoot))
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::CleanupFailed;
		result.filesystemPath = workspacePath;
		return result;
	}
	result.status = failureStatus;
	result.state = OnlineUpdate::ResourceInstallTransactionState::None;
	result.rolledBack = true;
	return result;
}

OnlineUpdate::ResourceInstallTransactionResult rollback(
	const std::filesystem::path& collectionRoot,
	OnlineUpdate::ResourceInstallTransactionResult result)
{
	const std::filesystem::path workspacePath =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	for (std::size_t reverseIndex = result.targets.size();
		reverseIndex > 0; reverseIndex--)
	{
		const std::size_t index = reverseIndex - 1;
		const OnlineUpdate::ResourceInstallTarget& target = result.targets[index];
		const std::filesystem::path livePath =
			collectionRoot / std::filesystem::u8path(target.targetDirectoryName);
		const std::filesystem::path stagedPath =
			preparedPath(workspacePath, index);
		const std::filesystem::path backupPath =
			previousPath(workspacePath, index);
		std::error_code error;
		const bool liveExists = std::filesystem::exists(livePath, error);
		if (error)
		{
			result.status =
				OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
			result.filesystemPath = livePath;
			return result;
		}
		const bool stagedExists = std::filesystem::exists(stagedPath, error);
		if (error)
		{
			result.status =
				OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
			result.filesystemPath = stagedPath;
			return result;
		}
		const bool backupExists = std::filesystem::exists(backupPath, error);
		if (error)
		{
			result.status =
				OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
			result.filesystemPath = backupPath;
			return result;
		}

		if (backupExists)
		{
			if (!installRootMatchesTarget(backupPath, target))
			{
				result.status =
					OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
				result.filesystemPath = backupPath;
				return result;
			}
			if (liveExists &&
				(!installRootMatchesTarget(livePath, target) ||
					!removeTree(livePath)))
			{
				result.status =
					OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
				result.filesystemPath = livePath;
				return result;
			}
			std::filesystem::rename(backupPath, livePath, error);
			if (error)
			{
				result.status =
					OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
				result.filesystemPath = livePath;
				return result;
			}
			continue;
		}

		if (target.hadExistingTarget)
		{
			// With no backup, a live target is either untouched (the staged
			// directory still exists) or was restored by an earlier rollback
			// attempt (the staged directory was already consumed). Both states
			// are safe and make rollback retryable after a later target failed.
			if (!liveExists ||
				!installRootMatchesTarget(livePath, target))
			{
				result.status =
					OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
				result.filesystemPath = livePath;
				return result;
			}
		}
		else if (!stagedExists && liveExists)
		{
			if (!installRootMatchesTarget(livePath, target) ||
				!removeTree(livePath))
			{
				result.status =
					OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
				result.filesystemPath = livePath;
				return result;
			}
		}
		else if (stagedExists && liveExists)
		{
			result.status =
				OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed;
			result.filesystemPath = livePath;
			return result;
		}
	}
	if (!removeWorkspace(collectionRoot))
	{
		result.status =
			OnlineUpdate::ResourceInstallTransactionStatus::CleanupFailed;
		result.filesystemPath = workspacePath;
		return result;
	}
	result.status = OnlineUpdate::ResourceInstallTransactionStatus::Success;
	result.state = OnlineUpdate::ResourceInstallTransactionState::None;
	result.needsValidation = false;
	result.rolledBack = true;
	return result;
}
}

namespace OnlineUpdate
{
std::filesystem::path resourceUpdateDirectoryPath(
	const std::filesystem::path& collectionRoot)
{
	return collectionRoot / ".jxqy-update";
}

std::filesystem::path resourceUpdateWorkspacePath(
	const std::filesystem::path& collectionRoot)
{
	return resourceUpdateDirectoryPath(collectionRoot) / "current";
}

bool isValidCommonResourceRoot(const std::filesystem::path& root)
{
	return commonResourceRootIsValid(root);
}

ResourceInstallTransactionResult stageResourceInstallTransaction(
	const std::filesystem::path& requestedCollectionRoot,
	const std::string& requestedGameId,
	const std::vector<ResourceInstallTarget>& requestedTargets)
{
	ResourceInstallTransactionResult result;
	result.requestedGameId = requestedGameId;
	result.targets = requestedTargets;
	result.filesystemPath = resourceUpdateWorkspacePath(requestedCollectionRoot);
	if (requestedCollectionRoot.empty() ||
		!isValidOnlineGameId(requestedGameId) ||
		requestedTargets.empty() ||
		requestedTargets.size() > MaximumTransactionTargets)
	{
		result.status = ResourceInstallTransactionStatus::InvalidInput;
		return result;
	}
	std::filesystem::path collectionRoot;
	if (!resolvePlainCollectionRoot(requestedCollectionRoot, collectionRoot))
	{
		result.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		return result;
	}
	const std::filesystem::path workspacePath =
		resourceUpdateWorkspacePath(collectionRoot);
	result.filesystemPath = workspacePath;
	bool pathAccessible = false;
	if (!isPlainWorkspaceLayout(collectionRoot) ||
		pathEntryExists(recordPath(collectionRoot), pathAccessible) ||
		!pathAccessible)
	{
		result.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		return result;
	}
	if (pathEntryExists(workspacePath / "previous", pathAccessible) ||
		!pathAccessible ||
		pathEntryExists(workspacePath / "install.ini.tmp", pathAccessible) ||
		!pathAccessible)
	{
		result.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		return result;
	}

	std::set<std::string> gameIds;
	std::set<std::string> targetNames;
	bool requestedTargetFound = false;
	for (std::size_t index = 0; index < result.targets.size(); index++)
	{
		ResourceInstallTarget& target = result.targets[index];
		result.failedGameId = target.gameId;
		const std::filesystem::path expectedPreparedPath =
			preparedPath(workspacePath, index);
		const std::string foldedGameId = foldAscii(target.gameId);
		const bool commonTarget = isCommonInstallTarget(target);
		if (!isValidOnlineGameId(target.gameId) ||
			!isSafeTargetDirectoryName(
				target.targetDirectoryName, commonTarget) ||
			!gameIds.insert(foldedGameId).second ||
			!targetNames.insert(
				foldAscii(target.targetDirectoryName)).second ||
			!installRootMatchesTarget(expectedPreparedPath, target))
		{
			result.status = ResourceInstallTransactionStatus::InvalidInput;
			result.filesystemPath = expectedPreparedPath;
			return result;
		}
		requestedTargetFound = requestedTargetFound ||
			foldedGameId == foldAscii(requestedGameId);
		const std::filesystem::path livePath =
			collectionRoot / std::filesystem::u8path(target.targetDirectoryName);
		target.hadExistingTarget = pathEntryExists(livePath, pathAccessible);
		if (!pathAccessible || (target.hadExistingTarget &&
			!installRootMatchesTarget(livePath, target)))
		{
			result.status = ResourceInstallTransactionStatus::TargetConflict;
			result.filesystemPath = livePath;
			return result;
		}
	}
	if (!requestedTargetFound)
	{
		result.status = ResourceInstallTransactionStatus::InvalidInput;
		result.failedGameId = requestedGameId;
		return result;
	}
	result.failedGameId.clear();
	if (!writeRecord(
			collectionRoot,
			ResourceInstallTransactionState::Ready,
			requestedGameId,
			result.targets))
	{
		result.status = ResourceInstallTransactionStatus::RecordUnavailable;
		result.filesystemPath = recordPath(collectionRoot);
		return result;
	}
	result.status = ResourceInstallTransactionStatus::Success;
	result.state = ResourceInstallTransactionState::Ready;
	return result;
}

ResourceInstallTransactionResult beginResourceInstallTransaction(
	const std::filesystem::path& requestedCollectionRoot)
{
	ResourceInstallTransactionResult unavailable;
	unavailable.filesystemPath =
		resourceUpdateWorkspacePath(requestedCollectionRoot);
	std::filesystem::path collectionRoot;
	if (!resolvePlainCollectionRoot(requestedCollectionRoot, collectionRoot))
	{
		unavailable.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		return unavailable;
	}
	const std::filesystem::path updateDirectory =
		resourceUpdateDirectoryPath(collectionRoot);
	const std::filesystem::path workspacePath =
		resourceUpdateWorkspacePath(collectionRoot);
	bool pathAccessible = false;
	if (!pathEntryExists(updateDirectory, pathAccessible) && pathAccessible)
	{
		unavailable.status = ResourceInstallTransactionStatus::NoTransaction;
		return unavailable;
	}
	if (!pathAccessible || !isPlainDirectory(updateDirectory))
	{
		unavailable.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		return unavailable;
	}
	if (!pathEntryExists(workspacePath, pathAccessible) && pathAccessible)
	{
		removeEmptyUpdateDirectory(collectionRoot);
		unavailable.status = ResourceInstallTransactionStatus::NoTransaction;
		return unavailable;
	}
	if (!pathAccessible || !isPlainDirectory(workspacePath))
	{
		unavailable.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		return unavailable;
	}
	ResourceInstallTransactionResult result = readRecord(collectionRoot);
	if (result.status == ResourceInstallTransactionStatus::NoTransaction)
	{
		// A process can stop after creating the private download workspace but
		// before the Ready record is written. No live resource was touched, so
		// this orphan can be discarded and the user can retry the download.
		if (!removeWorkspace(collectionRoot))
		{
			result.status = ResourceInstallTransactionStatus::CleanupFailed;
			result.filesystemPath = workspacePath;
		}
		return result;
	}
	if (result.status != ResourceInstallTransactionStatus::Success)
	{
		return result;
	}
	if (result.state == ResourceInstallTransactionState::Committed)
	{
		if (!removeWorkspace(collectionRoot))
		{
			result.status = ResourceInstallTransactionStatus::CleanupFailed;
			result.filesystemPath = workspacePath;
			return result;
		}
		result.status = ResourceInstallTransactionStatus::NoTransaction;
		result.state = ResourceInstallTransactionState::None;
		return result;
	}
	if (result.state == ResourceInstallTransactionState::Switching)
	{
		return rollback(collectionRoot, std::move(result));
	}
	if (result.state == ResourceInstallTransactionState::AwaitingValidation)
	{
		result.needsValidation = true;
		return result;
	}
	if (result.state != ResourceInstallTransactionState::Ready)
	{
		result.status = ResourceInstallTransactionStatus::RecordInvalid;
		return result;
	}

	for (std::size_t index = 0; index < result.targets.size(); index++)
	{
		const ResourceInstallTarget& target = result.targets[index];
		const std::filesystem::path livePath =
			collectionRoot / std::filesystem::u8path(target.targetDirectoryName);
		std::error_code targetError;
		const bool liveExists = std::filesystem::exists(livePath, targetError);
		if (targetError || liveExists != target.hadExistingTarget ||
			(liveExists && !installRootMatchesTarget(livePath, target)) ||
			!installRootMatchesTarget(
				preparedPath(workspacePath, index), target))
		{
			result.status = ResourceInstallTransactionStatus::TargetConflict;
			result.failedGameId = target.gameId;
			result.filesystemPath = livePath;
			return discardReadyTransaction(
				collectionRoot,
				std::move(result),
				ResourceInstallTransactionStatus::TargetConflict);
		}
	}
	if (!writeRecord(
			collectionRoot,
			ResourceInstallTransactionState::Switching,
			result.requestedGameId,
			result.targets))
	{
		result.status = ResourceInstallTransactionStatus::RecordUnavailable;
		return result;
	}
	result.state = ResourceInstallTransactionState::Switching;
	std::error_code error;
	const std::filesystem::path previousRoot = workspacePath / "previous";
	if (!std::filesystem::create_directory(previousRoot, error) || error)
	{
		return rollback(collectionRoot, std::move(result));
	}
	for (std::size_t index = 0; index < result.targets.size(); index++)
	{
		const ResourceInstallTarget& target = result.targets[index];
		const std::filesystem::path livePath =
			collectionRoot / std::filesystem::u8path(target.targetDirectoryName);
		if (target.hadExistingTarget)
		{
			std::filesystem::rename(
				livePath, previousPath(workspacePath, index), error);
			if (error)
			{
				return rollback(collectionRoot, std::move(result));
			}
		}
		std::filesystem::rename(
			preparedPath(workspacePath, index), livePath, error);
		if (error)
		{
			return rollback(collectionRoot, std::move(result));
		}
	}
	if (!writeRecord(
			collectionRoot,
			ResourceInstallTransactionState::AwaitingValidation,
			result.requestedGameId,
			result.targets))
	{
		return rollback(collectionRoot, std::move(result));
	}
	result.state = ResourceInstallTransactionState::AwaitingValidation;
	result.needsValidation = true;
	result.status = ResourceInstallTransactionStatus::Success;
	return result;
}

ResourceInstallTransactionResult completeResourceInstallTransaction(
	const std::filesystem::path& requestedCollectionRoot,
	bool validationSucceeded)
{
	std::filesystem::path collectionRoot;
	if (!resolvePlainCollectionRoot(requestedCollectionRoot, collectionRoot))
	{
		ResourceInstallTransactionResult unavailable;
		unavailable.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		unavailable.filesystemPath =
			resourceUpdateWorkspacePath(requestedCollectionRoot);
		return unavailable;
	}
	if (!isPlainWorkspaceLayout(collectionRoot))
	{
		ResourceInstallTransactionResult unavailable;
		unavailable.status = ResourceInstallTransactionStatus::WorkspaceConflict;
		unavailable.filesystemPath = resourceUpdateWorkspacePath(collectionRoot);
		return unavailable;
	}
	ResourceInstallTransactionResult result = readRecord(collectionRoot);
	if (result.status != ResourceInstallTransactionStatus::Success)
	{
		return result;
	}
	if (result.state != ResourceInstallTransactionState::AwaitingValidation)
	{
		result.status = ResourceInstallTransactionStatus::RecordInvalid;
		return result;
	}
	if (!validationSucceeded)
	{
		return rollback(collectionRoot, std::move(result));
	}
	if (!writeRecord(
			collectionRoot,
			ResourceInstallTransactionState::Committed,
			result.requestedGameId,
			result.targets))
	{
		result.status = ResourceInstallTransactionStatus::RecordUnavailable;
		return result;
	}
	result.state = ResourceInstallTransactionState::Committed;
	const std::filesystem::path workspacePath =
		resourceUpdateWorkspacePath(collectionRoot);
	if (!removeWorkspace(collectionRoot))
	{
		result.status = ResourceInstallTransactionStatus::CleanupFailed;
		result.filesystemPath = workspacePath;
		return result;
	}
	result.status = ResourceInstallTransactionStatus::Success;
	result.state = ResourceInstallTransactionState::None;
	result.needsValidation = false;
	return result;
}
}
