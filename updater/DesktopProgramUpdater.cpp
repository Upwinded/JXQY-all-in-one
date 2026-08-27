#include "DesktopProgramUpdater.h"

#include <array>
#include <system_error>

namespace
{
bool isAllowedTarget(const std::string& target)
{
	return target == "win32" || target == "win64" || target == "linux";
}

bool isSafeDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	return !error && std::filesystem::is_directory(status) &&
		!std::filesystem::is_symlink(status);
}

bool isSafeRegularFile(const std::filesystem::path& path)
{
	std::error_code error;
	const std::filesystem::file_status status =
		std::filesystem::symlink_status(path, error);
	return !error && std::filesystem::is_regular_file(status) &&
		!std::filesystem::is_symlink(status);
}

bool pathExists(const std::filesystem::path& path, bool& failed)
{
	std::error_code error;
	const bool exists = std::filesystem::exists(path, error);
	failed = static_cast<bool>(error);
	return exists;
}
}

namespace ProgramUpdate
{
DesktopProgramUpdateResult applyDesktopProgramUpdate(
	const DesktopProgramUpdateRequest& request,
	const DesktopProgramLaunchFunction& launchProgram)
{
	DesktopProgramUpdateResult result;
	if (request.releaseRoot.empty() || !isAllowedTarget(request.target) ||
		!launchProgram)
	{
		return result;
	}

	std::error_code error;
	const std::filesystem::path releaseRoot =
		std::filesystem::weakly_canonical(request.releaseRoot, error);
	if (error || !releaseRoot.is_absolute() ||
		!isSafeDirectory(releaseRoot))
	{
		result.status = DesktopProgramUpdateStatus::UnsafePath;
		result.filesystemPath = request.releaseRoot;
		return result;
	}

	const std::filesystem::path binRoot = releaseRoot / "bin";
	const std::filesystem::path assetsRoot = releaseRoot / "assets";
	const std::filesystem::path liveProgramPath = binRoot / request.target;
	const std::filesystem::path liveEnginePath = assetsRoot / "engine";
	const std::filesystem::path liveCommonPath = assetsRoot / "common";
	const std::filesystem::path workspacePath =
		binRoot / ".jxqy-program-update";
	const std::filesystem::path stagingRoot = workspacePath / "staging";
	const std::filesystem::path stagingProgramPath =
		stagingRoot / "bin" / request.target;
	const std::filesystem::path stagingEnginePath =
		stagingRoot / "assets" / "engine";
	const std::filesystem::path stagingCommonPath =
		stagingRoot / "assets" / "common";
	const std::filesystem::path previousRoot = workspacePath / "previous";
	const std::filesystem::path previousProgramPath =
		previousRoot / "bin" / request.target;
	const std::filesystem::path previousEnginePath =
		previousRoot / "assets" / "engine";
	const std::filesystem::path previousCommonPath =
		previousRoot / "assets" / "common";
	const std::filesystem::path executableName = request.target == "linux"
		? std::filesystem::path("jxqy-all-in-one")
		: std::filesystem::path("jxqy-all-in-one.exe");
	const std::filesystem::path engineFontPath =
		std::filesystem::path("font") / "font.ttf";
	const std::filesystem::path commonVersionPath = "version.ini";

	if (!isSafeDirectory(binRoot) || !isSafeDirectory(assetsRoot) ||
		!isSafeDirectory(workspacePath))
	{
		result.status = DesktopProgramUpdateStatus::UnsafePath;
		result.filesystemPath = !isSafeDirectory(binRoot)
			? binRoot
			: (!isSafeDirectory(assetsRoot) ? assetsRoot : workspacePath);
		return result;
	}
	auto renamePath = [](const std::filesystem::path& source,
		const std::filesystem::path& destination)
	{
		std::error_code renameError;
		std::filesystem::rename(source, destination, renameError);
		return !renameError;
	};
	auto cleanupPreviousRoot = [&previousRoot]()
	{
		std::error_code cleanupError;
		std::filesystem::remove_all(previousRoot, cleanupError);
		return !cleanupError;
	};
	struct UpdateComponent
	{
		std::filesystem::path livePath;
		std::filesystem::path stagingPath;
		std::filesystem::path previousPath;
		std::filesystem::path requiredFile;
	};
	const std::array<UpdateComponent, 3> components = {{
		{ liveProgramPath, stagingProgramPath, previousProgramPath,
			executableName },
		{ liveEnginePath, stagingEnginePath, previousEnginePath,
			engineFontPath },
		{ liveCommonPath, stagingCommonPath, previousCommonPath,
			commonVersionPath }
	}};
	const auto componentIsReady = [](const UpdateComponent& component,
		const std::filesystem::path& root)
	{
		return isSafeDirectory(root) &&
			isSafeRegularFile(root / component.requiredFile);
	};
	const auto restorePreviousVersion = [&components, &renamePath,
		&cleanupPreviousRoot, &componentIsReady, &previousRoot](
		std::filesystem::path& failedPath)
	{
		for (const UpdateComponent& component : components)
		{
			bool statusFailed = false;
			const bool previousExists = pathExists(
				component.previousPath, statusFailed);
			if (statusFailed)
			{
				failedPath = component.previousPath;
				return false;
			}
			const bool liveExists = pathExists(
				component.livePath, statusFailed);
			if (statusFailed)
			{
				failedPath = component.livePath;
				return false;
			}
			const bool stagingExists = pathExists(
				component.stagingPath, statusFailed);
			if (statusFailed ||
				(previousExists && !componentIsReady(
					component, component.previousPath)) ||
				(liveExists && !componentIsReady(
					component, component.livePath)))
			{
				failedPath = statusFailed
					? component.stagingPath
					: (previousExists
						? component.previousPath
						: component.livePath);
				return false;
			}
			if (!previousExists)
			{
				if (!liveExists)
				{
					failedPath = component.livePath;
					return false;
				}
				continue;
			}
			if (liveExists)
			{
				if (stagingExists || !renamePath(
					component.livePath, component.stagingPath))
				{
					failedPath = component.livePath;
					return false;
				}
			}
			if (!renamePath(component.previousPath, component.livePath))
			{
				failedPath = component.previousPath;
				return false;
			}
		}
		if (!cleanupPreviousRoot())
		{
			failedPath = previousRoot;
			return false;
		}
		return true;
	};

	bool statusFailed = false;
	const bool previousExists = pathExists(previousRoot, statusFailed);
	if (statusFailed)
	{
		result.status = DesktopProgramUpdateStatus::UnsafePath;
		result.filesystemPath = previousRoot;
		return result;
	}
	if (previousExists)
	{
		if (!isSafeDirectory(previousRoot) ||
			!restorePreviousVersion(result.filesystemPath))
		{
			result.status = DesktopProgramUpdateStatus::RollbackFailed;
			return result;
		}
		result.status = DesktopProgramUpdateStatus::RecoveredPreviousVersion;
		result.filesystemPath = liveProgramPath;
		return result;
	}

	for (const UpdateComponent& component : components)
	{
		if (!componentIsReady(component, component.livePath))
		{
			result.status = DesktopProgramUpdateStatus::LiveProgramMissing;
			result.filesystemPath =
				component.livePath / component.requiredFile;
			return result;
		}
	}
	if (!isSafeDirectory(stagingRoot))
	{
		result.status = DesktopProgramUpdateStatus::StagingMissing;
		result.filesystemPath = stagingRoot;
		return result;
	}
	for (const UpdateComponent& component : components)
	{
		if (!componentIsReady(component, component.stagingPath))
		{
			result.status = DesktopProgramUpdateStatus::StagingMissing;
			result.filesystemPath =
				component.stagingPath / component.requiredFile;
			return result;
		}
	}

	std::filesystem::create_directories(
		previousProgramPath.parent_path(), error);
	if (error || !isSafeDirectory(previousProgramPath.parent_path()))
	{
		result.status = DesktopProgramUpdateStatus::SwitchFailed;
		result.filesystemPath = previousProgramPath.parent_path();
		return result;
	}
	std::filesystem::create_directories(
		previousEnginePath.parent_path(), error);
	if (error || !isSafeDirectory(previousEnginePath.parent_path()))
	{
		result.status = DesktopProgramUpdateStatus::SwitchFailed;
		result.filesystemPath = previousEnginePath.parent_path();
		return result;
	}

	for (const UpdateComponent& component : components)
	{
		if (renamePath(component.livePath, component.previousPath))
		{
			continue;
		}
		const std::filesystem::path switchFailurePath = component.livePath;
		std::filesystem::path rollbackFailurePath;
		const bool restored = restorePreviousVersion(rollbackFailurePath);
		result.status = restored
			? DesktopProgramUpdateStatus::SwitchFailed
			: DesktopProgramUpdateStatus::RollbackFailed;
		result.filesystemPath = restored
			? switchFailurePath : rollbackFailurePath;
		return result;
	}
	for (const UpdateComponent& component : components)
	{
		if (renamePath(component.stagingPath, component.livePath))
		{
			continue;
		}
		const std::filesystem::path switchFailurePath = component.stagingPath;
		std::filesystem::path rollbackFailurePath;
		const bool restored = restorePreviousVersion(rollbackFailurePath);
		result.status = restored
			? DesktopProgramUpdateStatus::SwitchFailed
			: DesktopProgramUpdateStatus::RollbackFailed;
		result.filesystemPath = restored
			? switchFailurePath : rollbackFailurePath;
		return result;
	}

	bool launched = false;
	try
	{
		launched = launchProgram(
			liveProgramPath / executableName, liveProgramPath);
	}
	catch (...)
	{
		launched = false;
	}
	if (!launched)
	{
		std::filesystem::path rollbackFailurePath;
		if (!restorePreviousVersion(rollbackFailurePath))
		{
			result.status = DesktopProgramUpdateStatus::RollbackFailed;
			result.filesystemPath = rollbackFailurePath;
			return result;
		}
		result.status = DesktopProgramUpdateStatus::LaunchFailed;
		result.filesystemPath = liveProgramPath / executableName;
		return result;
	}

	std::filesystem::remove_all(previousRoot, error);
	if (error)
	{
		result.status = DesktopProgramUpdateStatus::CleanupFailed;
		result.filesystemPath = previousRoot;
		return result;
	}
	std::filesystem::remove_all(stagingRoot, error);
	if (error)
	{
		result.status = DesktopProgramUpdateStatus::CleanupFailed;
		result.filesystemPath = stagingRoot;
		return result;
	}
	const bool workspaceRemoved =
		std::filesystem::remove(workspacePath, error);
	if (error || !workspaceRemoved)
	{
		result.status = DesktopProgramUpdateStatus::CleanupFailed;
		result.filesystemPath = workspacePath;
		return result;
	}

	result.status = DesktopProgramUpdateStatus::Success;
	result.filesystemPath = liveProgramPath;
	return result;
}
}
