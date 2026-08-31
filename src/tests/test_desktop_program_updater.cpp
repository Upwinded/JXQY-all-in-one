#include "../../updater/DesktopProgramUpdater.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
class TemporaryTree
{
public:
	TemporaryTree()
		: root(makeUniqueTestDirectory("jxqy-desktop-program-updater"))
	{
		std::error_code error;
		if (!std::filesystem::create_directory(root, error) || error)
		{
			root.clear();
		}
	}

	~TemporaryTree()
	{
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}

	std::filesystem::path root;
};

int failures = 0;

void expect(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		failures++;
	}
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output << text;
}

std::string readText(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

struct Fixture
{
	explicit Fixture(const TemporaryTree& tree, const std::string& name)
		: releaseRoot(tree.root / name),
		  liveProgram(releaseRoot / "bin" / "win64"),
		  liveEngine(releaseRoot / "assets" / "engine"),
		  liveCommon(releaseRoot / "assets" / "common"),
		  workspace(releaseRoot / "bin" / ".jxqy-program-update"),
		  stagingRoot(workspace / "staging"),
		  stagingProgram(stagingRoot / "bin" / "win64"),
		  stagingEngine(stagingRoot / "assets" / "engine"),
		  stagingCommon(stagingRoot / "assets" / "common"),
		  previousRoot(workspace / "previous"),
		  previousProgram(previousRoot / "bin" / "win64"),
		  previousEngine(previousRoot / "assets" / "engine"),
		  previousCommon(previousRoot / "assets" / "common")
	{
		std::filesystem::create_directories(liveProgram);
		std::filesystem::create_directories(liveEngine / "font");
		std::filesystem::create_directories(liveCommon);
		std::filesystem::create_directories(stagingProgram);
		std::filesystem::create_directories(stagingEngine / "font");
		std::filesystem::create_directories(stagingCommon);
		std::filesystem::create_directories(releaseRoot / "assets");
		std::filesystem::create_directories(releaseRoot / "save");
		writeText(liveProgram / "jxqy-all-in-one.exe", "old-program");
		writeText(liveEngine / "font" / "font.ttf", "old-engine");
		writeText(liveCommon / "version.ini", "old-common");
		writeText(stagingProgram / "jxqy-all-in-one.exe", "new-program");
		writeText(stagingEngine / "font" / "font.ttf", "new-engine");
		writeText(stagingCommon / "version.ini", "new-common");
		writeText(releaseRoot / "assets" / "keep.txt", "assets");
		writeText(releaseRoot / "save" / "keep.txt", "save");
	}

	ProgramUpdate::DesktopProgramUpdateRequest request() const
	{
		return { releaseRoot, "win64" };
	}

	bool unrelatedDataIsUnchanged() const
	{
		return readText(releaseRoot / "assets" / "keep.txt") == "assets" &&
			readText(releaseRoot / "save" / "keep.txt") == "save";
	}

	std::filesystem::path releaseRoot;
	std::filesystem::path liveProgram;
	std::filesystem::path liveEngine;
	std::filesystem::path liveCommon;
	std::filesystem::path workspace;
	std::filesystem::path stagingRoot;
	std::filesystem::path stagingProgram;
	std::filesystem::path stagingEngine;
	std::filesystem::path stagingCommon;
	std::filesystem::path previousRoot;
	std::filesystem::path previousProgram;
	std::filesystem::path previousEngine;
	std::filesystem::path previousCommon;
};

void testSuccessfulSwitch(const TemporaryTree& tree)
{
	Fixture fixture(tree, "success");
	bool launchSawNewProgram = false;
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[&launchSawNewProgram](const std::filesystem::path& executable,
			const std::filesystem::path& workingDirectory)
		{
			launchSawNewProgram = readText(executable) == "new-program" &&
				executable.parent_path() == workingDirectory;
			return launchSawNewProgram;
		});
	expect(result.succeeded() && launchSawNewProgram &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"new-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"new-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"new-common" &&
		!std::filesystem::exists(fixture.previousRoot) &&
		!std::filesystem::exists(fixture.workspace) &&
		fixture.unrelatedDataIsUnchanged(),
		"successful update switches bin/win64, engine, and common together");
}

void testSuccessfulSwitchCleansOrphanDownload(const TemporaryTree& tree)
{
	Fixture fixture(tree, "success-with-orphan-download");
	const std::filesystem::path downloadRoot =
		fixture.workspace / "download";
	std::filesystem::create_directory(downloadRoot);
	writeText(downloadRoot / "program-update.download", "download-archive");
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.succeeded() &&
		!std::filesystem::exists(fixture.workspace) &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"new-program" && fixture.unrelatedDataIsUnchanged(),
		"successful update removes an orphaned program download workspace");
}

void testUnexpectedDownloadEntryIsNotDeleted(const TemporaryTree& tree)
{
	Fixture fixture(tree, "unsafe-download-entry");
	const std::filesystem::path downloadRoot =
		fixture.workspace / "download";
	writeText(downloadRoot, "not-a-directory");
	bool launchAttempted = false;
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[&launchAttempted](
			const std::filesystem::path&, const std::filesystem::path&)
		{
			launchAttempted = true;
			return true;
		});
	expect(result.status ==
			ProgramUpdate::DesktopProgramUpdateStatus::CleanupFailed &&
		!launchAttempted && std::filesystem::is_regular_file(downloadRoot) &&
		readText(downloadRoot) == "not-a-directory" &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.stagingProgram / "jxqy-all-in-one.exe") ==
			"new-program" && fixture.unrelatedDataIsUnchanged(),
		"an unexpected download entry is reported without deleting or switching");
}

void testReleaseRootFromResourceCollection(const TemporaryTree& tree)
{
	const std::filesystem::path releaseRoot = tree.root / "release-root";
	const std::filesystem::path assetsRoot = releaseRoot / "assets";
	const std::filesystem::path assetsRootWithSeparator =
		std::filesystem::u8path(assetsRoot.generic_u8string() + "/");
	expect(
		ProgramUpdate::desktopProgramReleaseRoot(assetsRoot) == releaseRoot &&
		ProgramUpdate::desktopProgramReleaseRoot(assetsRootWithSeparator) ==
			releaseRoot,
		"resource collection trailing separator does not move the program "
		"workspace under assets");
}

void testLaunchRollback(const TemporaryTree& tree)
{
	Fixture fixture(tree, "launch-failure");
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return false;
		});
	expect(result.status ==
			ProgramUpdate::DesktopProgramUpdateStatus::LaunchFailed &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		readText(fixture.stagingProgram / "jxqy-all-in-one.exe") ==
			"new-program" &&
		readText(fixture.stagingEngine / "font" / "font.ttf") ==
			"new-engine" &&
		readText(fixture.stagingCommon / "version.ini") ==
			"new-common" &&
		!std::filesystem::exists(fixture.previousRoot) &&
		fixture.unrelatedDataIsUnchanged(),
		"launch failure restores old program/engine/common and keeps them staged");
}

void testInterruptedSwitchRecovery(const TemporaryTree& tree)
{
	Fixture fixture(tree, "recovery");
	const std::filesystem::path downloadRoot =
		fixture.workspace / "download";
	std::filesystem::create_directory(downloadRoot);
	writeText(downloadRoot / "program-update.download", "download-archive");
	std::filesystem::create_directories(
		fixture.previousProgram.parent_path());
	std::filesystem::create_directories(
		fixture.previousEngine.parent_path());
	std::filesystem::rename(
		fixture.liveProgram, fixture.previousProgram);
	std::filesystem::rename(
		fixture.liveEngine, fixture.previousEngine);
	std::filesystem::rename(
		fixture.liveCommon, fixture.previousCommon);
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.status == ProgramUpdate::
			DesktopProgramUpdateStatus::RecoveredPreviousVersion &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		readText(fixture.stagingProgram / "jxqy-all-in-one.exe") ==
			"new-program" &&
		readText(fixture.stagingEngine / "font" / "font.ttf") ==
			"new-engine" &&
		readText(fixture.stagingCommon / "version.ini") ==
			"new-common" &&
		!std::filesystem::exists(downloadRoot) &&
		fixture.unrelatedDataIsUnchanged(),
		"an interrupted switch restores program, engine, and common together");
}

void testInterruptedAfterMovingProgramRecovery(const TemporaryTree& tree)
{
	Fixture fixture(tree, "recovery-after-moving-program");
	std::filesystem::create_directories(
		fixture.previousProgram.parent_path());
	std::filesystem::rename(
		fixture.liveProgram, fixture.previousProgram);
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.status == ProgramUpdate::
			DesktopProgramUpdateStatus::RecoveredPreviousVersion &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		readText(fixture.stagingProgram / "jxqy-all-in-one.exe") ==
			"new-program" &&
		readText(fixture.stagingEngine / "font" / "font.ttf") ==
			"new-engine" &&
		readText(fixture.stagingCommon / "version.ini") ==
			"new-common" &&
		!std::filesystem::exists(fixture.previousRoot) &&
		fixture.unrelatedDataIsUnchanged(),
		"an interruption after moving the old program restores it without "
		"changing common");
}

void testInterruptedAfterSwitchingProgramRecovery(const TemporaryTree& tree)
{
	Fixture fixture(tree, "recovery-after-switching-program");
	std::filesystem::create_directories(
		fixture.previousProgram.parent_path());
	std::filesystem::create_directories(
		fixture.previousEngine.parent_path());
	std::filesystem::rename(
		fixture.liveProgram, fixture.previousProgram);
	std::filesystem::rename(
		fixture.liveEngine, fixture.previousEngine);
	std::filesystem::rename(
		fixture.liveCommon, fixture.previousCommon);
	std::filesystem::rename(
		fixture.stagingProgram, fixture.liveProgram);
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.status == ProgramUpdate::
			DesktopProgramUpdateStatus::RecoveredPreviousVersion &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		readText(fixture.stagingProgram / "jxqy-all-in-one.exe") ==
			"new-program" &&
		readText(fixture.stagingEngine / "font" / "font.ttf") ==
			"new-engine" &&
		readText(fixture.stagingCommon / "version.ini") ==
			"new-common" &&
		!std::filesystem::exists(fixture.previousRoot) &&
		fixture.unrelatedDataIsUnchanged(),
		"an interruption while switching program assets rolls all components "
		"back to one consistent version");
}

void testInterruptedAfterSwitchingEngineRecovery(const TemporaryTree& tree)
{
	Fixture fixture(tree, "interrupted-after-switching-engine");
	std::filesystem::create_directories(
		fixture.previousProgram.parent_path());
	std::filesystem::create_directories(
		fixture.previousEngine.parent_path());
	std::filesystem::rename(
		fixture.liveProgram, fixture.previousProgram);
	std::filesystem::rename(
		fixture.liveEngine, fixture.previousEngine);
	std::filesystem::rename(
		fixture.liveCommon, fixture.previousCommon);
	std::filesystem::rename(
		fixture.stagingProgram, fixture.liveProgram);
	std::filesystem::rename(
		fixture.stagingEngine, fixture.liveEngine);
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.status == ProgramUpdate::
			DesktopProgramUpdateStatus::RecoveredPreviousVersion &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		readText(fixture.stagingProgram / "jxqy-all-in-one.exe") ==
			"new-program" &&
		readText(fixture.stagingEngine / "font" / "font.ttf") ==
			"new-engine" &&
		readText(fixture.stagingCommon / "version.ini") ==
			"new-common" &&
		!std::filesystem::exists(fixture.previousRoot) &&
		fixture.unrelatedDataIsUnchanged(),
		"an interruption after switching engine rolls all components back");
}

void testMissingStagedCommon(const TemporaryTree& tree)
{
	Fixture fixture(tree, "missing-staged-common");
	const std::filesystem::path downloadRoot =
		fixture.workspace / "download";
	std::filesystem::create_directory(downloadRoot);
	writeText(
		downloadRoot / "program-update.download",
		"incomplete-download");
	std::filesystem::remove_all(fixture.stagingCommon);
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		fixture.request(),
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.status ==
			ProgramUpdate::DesktopProgramUpdateStatus::StagingMissing &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		!std::filesystem::exists(downloadRoot) &&
		fixture.unrelatedDataIsUnchanged(),
		"missing staged common removes the orphan download and leaves current program assets unchanged");
}

void testInvalidTarget(const TemporaryTree& tree)
{
	Fixture fixture(tree, "invalid-target");
	ProgramUpdate::DesktopProgramUpdateRequest request = fixture.request();
	request.target = "../assets";
	const auto result = ProgramUpdate::applyDesktopProgramUpdate(
		request,
		[](const std::filesystem::path&, const std::filesystem::path&)
		{
			return true;
		});
	expect(result.status ==
			ProgramUpdate::DesktopProgramUpdateStatus::InvalidInput &&
		readText(fixture.liveProgram / "jxqy-all-in-one.exe") ==
			"old-program" &&
		readText(fixture.liveEngine / "font" / "font.ttf") ==
			"old-engine" &&
		readText(fixture.liveCommon / "version.ini") ==
			"old-common" &&
		fixture.unrelatedDataIsUnchanged(),
		"target selection cannot escape the three fixed bin directories");
}
}

int main()
{
	TemporaryTree tree;
	if (tree.root.empty())
	{
		std::cerr << "FAIL: temporary directory unavailable" << std::endl;
		return 1;
	}
	testSuccessfulSwitch(tree);
	testSuccessfulSwitchCleansOrphanDownload(tree);
	testUnexpectedDownloadEntryIsNotDeleted(tree);
	testReleaseRootFromResourceCollection(tree);
	testLaunchRollback(tree);
	testInterruptedSwitchRecovery(tree);
	testInterruptedAfterMovingProgramRecovery(tree);
	testInterruptedAfterSwitchingProgramRecovery(tree);
	testInterruptedAfterSwitchingEngineRecovery(tree);
	testMissingStagedCommon(tree);
	testInvalidTarget(tree);
	if (failures != 0)
	{
		std::cerr << failures << " desktop program updater test(s) failed"
			<< std::endl;
		return 1;
	}
	std::cout << "All desktop program updater tests passed" << std::endl;
	return 0;
}
