#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ProgramUpdate
{
enum class DesktopProgramUpdateStatus
{
	Success,
	InvalidInput,
	UnsafePath,
	StagingMissing,
	LiveProgramMissing,
	PreviousVersionExists,
	SwitchFailed,
	LaunchFailed,
	RollbackFailed,
	CleanupFailed,
	RecoveredPreviousVersion
};

struct DesktopProgramUpdateRequest
{
	std::filesystem::path releaseRoot;
	std::string target;
};

struct DesktopProgramUpdateResult
{
	DesktopProgramUpdateStatus status =
		DesktopProgramUpdateStatus::InvalidInput;
	std::filesystem::path filesystemPath;

	bool succeeded() const noexcept
	{
		return status == DesktopProgramUpdateStatus::Success;
	}
};

using DesktopProgramLaunchFunction = std::function<bool(
	const std::filesystem::path& executablePath,
	const std::filesystem::path& workingDirectory)>;

inline std::filesystem::path desktopProgramReleaseRoot(
	std::filesystem::path resourceCollectionRoot)
{
	resourceCollectionRoot = resourceCollectionRoot.lexically_normal();
	if (!resourceCollectionRoot.has_filename())
	{
		resourceCollectionRoot = resourceCollectionRoot.parent_path();
	}
	return resourceCollectionRoot.parent_path();
}

// Applies the current platform program, engine assets, and common assets from
// one extracted portable package. Every mutable path is derived from
// releaseRoot and target; playable resources and save are never parameters.
DesktopProgramUpdateResult applyDesktopProgramUpdate(
	const DesktopProgramUpdateRequest& request,
	const DesktopProgramLaunchFunction& launchProgram);
}
