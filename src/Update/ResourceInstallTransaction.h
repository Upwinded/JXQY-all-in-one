#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace OnlineUpdate
{
enum class ResourceInstallTransactionState
{
	None,
	Ready,
	Switching,
	AwaitingValidation,
	Committed
};

enum class ResourceInstallTransactionStatus
{
	Success,
	NoTransaction,
	InvalidInput,
	RecordUnavailable,
	RecordInvalid,
	WorkspaceConflict,
	TargetConflict,
	RollbackFailed,
	CleanupFailed
};

struct ResourceInstallTarget
{
	std::string gameId;
	std::string targetDirectoryName;
	bool hadExistingTarget = false;
};

struct ResourceInstallTransactionResult
{
	ResourceInstallTransactionStatus status =
		ResourceInstallTransactionStatus::InvalidInput;
	ResourceInstallTransactionState state =
		ResourceInstallTransactionState::None;
	std::string requestedGameId;
	std::string failedGameId;
	std::filesystem::path filesystemPath;
	std::vector<ResourceInstallTarget> targets;
	bool needsValidation = false;
	bool rolledBack = false;

	bool succeeded() const noexcept
	{
		return status == ResourceInstallTransactionStatus::Success ||
			status == ResourceInstallTransactionStatus::NoTransaction;
	}
};

std::filesystem::path resourceUpdateDirectoryPath(
	const std::filesystem::path& collectionRoot);
std::filesystem::path resourceUpdateWorkspacePath(
	const std::filesystem::path& collectionRoot);

// Common is a fixed collection-level target, not a playable Game.Id package.
// It must keep the startup font and must never contain game_profile.ini.
bool isValidCommonResourceRoot(const std::filesystem::path& root);

// Writes the minimal Ready record after every package has already been
// downloaded and validated. Targets are exact direct-child directory names;
// this function never guesses a target from Game.Id.
ResourceInstallTransactionResult stageResourceInstallTransaction(
	const std::filesystem::path& collectionRoot,
	const std::string& requestedGameId,
	const std::vector<ResourceInstallTarget>& targets);

// Called before resource discovery. Ready transactions are switched as one
// group and returned for validation. Interrupted Switching transactions are
// rolled back before discovery; AwaitingValidation transactions resume normal
// validation without switching again.
ResourceInstallTransactionResult beginResourceInstallTransaction(
	const std::filesystem::path& collectionRoot);

// Called after resource discovery validates every exact target. Success commits
// and removes the previous group; failure restores the previous group.
ResourceInstallTransactionResult completeResourceInstallTransaction(
	const std::filesystem::path& collectionRoot,
	bool validationSucceeded);
}
