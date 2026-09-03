#include "../Update/ResourceInstallTransaction.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
class TemporaryTree
{
public:
	explicit TemporaryTree(const std::string& prefix)
		: root(makeUniqueTestDirectory(prefix))
	{
		std::filesystem::create_directories(root);
	}

	~TemporaryTree()
	{
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}

	std::filesystem::path root;
};

bool writeText(const std::filesystem::path& path, const std::string& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(output);
}

std::string readText(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	return std::string(
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>());
}

bool writeResource(
	const std::filesystem::path& root,
	const std::string& gameId,
	const std::string& payload)
{
	return writeText(
		root / "game_profile.ini",
		"[Game]\nId=" + gameId + "\nName=" + gameId + "\n") &&
		writeText(root / "payload.txt", payload);
}

std::filesystem::path preparedRoot(
	const std::filesystem::path& collectionRoot,
	std::size_t index)
{
	return OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot) /
		"prepared" / ("package-" + std::to_string(index));
}

bool prepareWorkspace(
	const std::filesystem::path& collectionRoot,
	const std::vector<std::pair<std::string, std::string>>& resources)
{
	const std::filesystem::path workspace =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	if (!std::filesystem::create_directories(workspace / "prepared"))
	{
		return false;
	}
	for (std::size_t index = 0; index < resources.size(); index++)
	{
		if (!writeResource(
				preparedRoot(collectionRoot, index),
				resources[index].first,
				resources[index].second))
		{
			return false;
		}
	}
	return true;
}

bool prepareCommonWorkspace(
	const std::filesystem::path& collectionRoot,
	const std::string& payload)
{
	const std::filesystem::path workspace =
		OnlineUpdate::resourceUpdateWorkspacePath(collectionRoot);
	return std::filesystem::create_directories(workspace / "prepared") &&
		writeText(preparedRoot(collectionRoot, 0) / "version.ini",
			"[Common]\nVersion=1.1.0\n") &&
		writeText(preparedRoot(collectionRoot, 0) / "payload.txt", payload);
}

bool expect(bool condition, const char* description)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << description << '\n';
	}
	return condition;
}

bool testSuccessfulGroupSwitch()
{
	TemporaryTree tree("jxqy-resource-install-success");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "dependency", "JXQY2", "old dependency") &&
		writeResource(assets / "moon", "YYCS", "old moon") &&
		writeText(assets / "common" / "sentinel.txt", "common") &&
		writeText(assets / "unrelated" / "sentinel.txt", "unrelated") &&
		prepareWorkspace(assets, {
			{"JXQY2", "new dependency"},
			{"YYCS", "new moon"}});
	if (!ok)
	{
		return false;
	}

	const std::vector<OnlineUpdate::ResourceInstallTarget> targets = {
		{"JXQY2", "dependency"},
		{"YYCS", "moon"}};
	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets, "YYCS", targets);
	const auto switched =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	const auto committed =
		OnlineUpdate::completeResourceInstallTransaction(assets, true);
	ok = expect(staged.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success,
			"successful switch: transaction staged") && ok;
	ok = expect(switched.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success &&
			switched.needsValidation,
			"successful switch: new group awaits validation") && ok;
	ok = expect(committed.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success,
			"successful switch: validated group committed") && ok;
	ok = expect(readText(assets / "dependency" / "payload.txt") ==
			"new dependency" &&
			readText(assets / "moon" / "payload.txt") == "new moon",
			"successful switch: all live targets use new content") && ok;
	ok = expect(readText(assets / "common" / "sentinel.txt") == "common" &&
			readText(assets / "unrelated" / "sentinel.txt") == "unrelated",
			"successful switch: unrelated directories remain untouched") && ok;
	ok = expect(!std::filesystem::exists(
			OnlineUpdate::resourceUpdateWorkspacePath(assets)),
			"successful switch: transaction workspace removed") && ok;
	ok = expect(!std::filesystem::exists(
			OnlineUpdate::resourceUpdateDirectoryPath(assets)),
			"successful switch: empty update directory removed") && ok;
	return ok;
}

bool testDependencyOnlySwitch()
{
	TemporaryTree tree("jxqy-resource-install-dependency-only");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "dependency", "YYCS", "old dependency") &&
		writeResource(assets / "requested", "JIANGHU_YUCHEN_1_03", "unchanged mod") &&
		prepareWorkspace(assets, {{"YYCS", "new dependency"}});
	if (!ok)
	{
		return false;
	}

	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets, "JIANGHU_YUCHEN_1_03", {{"YYCS", "dependency"}});
	const auto switched =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	const auto committed =
		OnlineUpdate::completeResourceInstallTransaction(assets, true);
	ok = expect(staged.succeeded() && switched.needsValidation &&
			committed.succeeded(),
			"dependency-only switch: unchanged requested resource need not be a target") && ok;
	ok = expect(readText(assets / "dependency" / "payload.txt") ==
			"new dependency" &&
			readText(assets / "requested" / "payload.txt") == "unchanged mod",
			"dependency-only switch: dependency changes without replacing requested resource") && ok;
	return ok;
}

bool testValidationFailureRollsBackGroup()
{
	TemporaryTree tree("jxqy-resource-install-rollback");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "moon", "YYCS", "old moon") &&
		prepareWorkspace(assets, {
			{"JXQY2", "new dependency"},
			{"YYCS", "new moon"}});
	if (!ok)
	{
		return false;
	}

	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets,
		"YYCS",
		{{"JXQY2", "dependency"}, {"YYCS", "moon"}});
	const auto switched =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	const auto rolledBack =
		OnlineUpdate::completeResourceInstallTransaction(assets, false);
	ok = expect(staged.succeeded() && switched.needsValidation,
		"validation rollback: group switched before validation") && ok;
	ok = expect(rolledBack.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success &&
			rolledBack.rolledBack,
			"validation rollback: rollback reported success") && ok;
	ok = expect(readText(assets / "moon" / "payload.txt") == "old moon",
		"validation rollback: replaced resource restored") && ok;
	ok = expect(!std::filesystem::exists(assets / "dependency"),
		"validation rollback: newly installed dependency removed") && ok;
	return ok;
}

bool testCommitRecordFailureCanRollBackGroup()
{
	TemporaryTree tree("jxqy-resource-install-commit-record-failure");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "moon", "YYCS", "old") &&
		prepareWorkspace(assets, {{"YYCS", "new"}});
	if (!ok)
	{
		return false;
	}

	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets, "YYCS", {{"YYCS", "moon"}});
	const auto switched =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	const std::filesystem::path blockedTemporaryRecord =
		OnlineUpdate::resourceUpdateWorkspacePath(assets) / "install.ini.tmp";
	std::filesystem::create_directory(blockedTemporaryRecord);
	const auto commitFailed =
		OnlineUpdate::completeResourceInstallTransaction(assets, true);
	const auto rolledBack =
		OnlineUpdate::completeResourceInstallTransaction(assets, false);
	ok = expect(staged.succeeded() && switched.needsValidation &&
			commitFailed.status ==
				OnlineUpdate::ResourceInstallTransactionStatus::RecordUnavailable,
		"commit record failure: switched group cannot record Committed") && ok;
	ok = expect(rolledBack.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success &&
			rolledBack.rolledBack &&
			readText(assets / "moon" / "payload.txt") == "old" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)),
		"commit record failure: existing rollback restores the old group") && ok;
	return ok;
}

bool testCommonSwitchAndRollback()
{
	TemporaryTree commitTree("jxqy-common-install-success");
	const std::filesystem::path commitAssets = commitTree.root / "assets";
	bool ok = writeText(
			commitAssets / "common/version.ini",
			"[Common]\nVersion=1.0.0\n") &&
		writeText(commitAssets / "common/payload.txt", "old") &&
		prepareCommonWorkspace(commitAssets, "new");
	if (!ok)
	{
		return false;
	}
	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		commitAssets, "common", {{"common", "common"}});
	const auto switched = OnlineUpdate::beginResourceInstallTransaction(
		commitAssets);
	const auto committed = OnlineUpdate::completeResourceInstallTransaction(
		commitAssets, true);
	ok = expect(staged.succeeded() && switched.needsValidation &&
			committed.succeeded() &&
			readText(commitAssets / "common/payload.txt") == "new",
			"common install: validated common replaces assets/common") && ok;

	TemporaryTree rollbackTree("jxqy-common-install-rollback");
	const std::filesystem::path rollbackAssets = rollbackTree.root / "assets";
	ok = writeText(
			rollbackAssets / "common/version.ini",
			"[Common]\nVersion=1.0.0\n") && ok;
	ok = writeText(rollbackAssets / "common/payload.txt", "old") && ok;
	ok = prepareCommonWorkspace(rollbackAssets, "new") && ok;
	const auto rollbackStaged =
		OnlineUpdate::stageResourceInstallTransaction(
			rollbackAssets, "common", {{"common", "common"}});
	const auto rollbackSwitched =
		OnlineUpdate::beginResourceInstallTransaction(rollbackAssets);
	const auto rolledBack =
		OnlineUpdate::completeResourceInstallTransaction(
			rollbackAssets, false);
	ok = expect(rollbackStaged.succeeded() &&
			rollbackSwitched.needsValidation && rolledBack.rolledBack &&
			readText(rollbackAssets / "common/payload.txt") == "old",
			"common install: failed validation restores previous common") && ok;
	return ok;
}

bool replaceTransactionState(
	const std::filesystem::path& assets,
	const std::string& from,
	const std::string& to)
{
	const std::filesystem::path path =
		OnlineUpdate::resourceUpdateWorkspacePath(assets) / "install.ini";
	std::string record = readText(path);
	const std::string oldLine = "State=" + from;
	const std::size_t position = record.find(oldLine);
	if (position == std::string::npos)
	{
		return false;
	}
	record.replace(position, oldLine.size(), "State=" + to);
	return writeText(path, record);
}

bool testInterruptedSwitchRestoresOldGroup()
{
	TemporaryTree tree("jxqy-resource-install-interrupted");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "dependency", "JXQY2", "old dependency") &&
		writeResource(assets / "moon", "YYCS", "old moon") &&
		prepareWorkspace(assets, {
			{"JXQY2", "new dependency"},
			{"YYCS", "new moon"}});
	if (!ok)
	{
		return false;
	}

	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets,
		"YYCS",
		{{"JXQY2", "dependency"}, {"YYCS", "moon"}});
	const std::filesystem::path workspace =
		OnlineUpdate::resourceUpdateWorkspacePath(assets);
	std::filesystem::create_directory(workspace / "previous");
	std::filesystem::rename(
		assets / "dependency", workspace / "previous" / "package-0");
	std::filesystem::rename(
		preparedRoot(assets, 0), assets / "dependency");
	ok = expect(staged.succeeded() &&
			replaceTransactionState(assets, "Ready", "Switching"),
			"interrupted switch: partial Switching fixture prepared") && ok;
	const auto recovered =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	ok = expect(recovered.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success &&
			recovered.rolledBack,
			"interrupted switch: startup recovery rolls back") && ok;
	ok = expect(readText(assets / "dependency" / "payload.txt") ==
			"old dependency" &&
			readText(assets / "moon" / "payload.txt") == "old moon",
			"interrupted switch: whole old group restored") && ok;
	return ok;
}

bool testInterruptedRollbackCanResume()
{
	TemporaryTree tree("jxqy-resource-install-rollback-resume");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "dependency", "JXQY2", "old dependency") &&
		writeResource(assets / "moon", "YYCS", "old moon") &&
		prepareWorkspace(assets, {
			{"JXQY2", "new dependency"},
			{"YYCS", "new moon"}});
	if (!ok)
	{
		return false;
	}
	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets,
		"YYCS",
		{{"JXQY2", "dependency"}, {"YYCS", "moon"}});
	const std::filesystem::path workspace =
		OnlineUpdate::resourceUpdateWorkspacePath(assets);
	std::filesystem::create_directory(workspace / "previous");
	for (std::size_t index = 0; index < 2; index++)
	{
		const std::filesystem::path live =
			index == 0 ? assets / "dependency" : assets / "moon";
		std::filesystem::rename(
			live,
			workspace / "previous" / ("package-" + std::to_string(index)));
		std::filesystem::rename(preparedRoot(assets, index), live);
	}
	writeResource(
		workspace / "previous" / "package-0",
		"BROKEN",
		"old dependency");
	ok = expect(staged.succeeded() &&
			replaceTransactionState(assets, "Ready", "Switching"),
			"rollback resume: fully switched fixture prepared") && ok;
	const auto firstRecovery =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	ok = expect(firstRecovery.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::RollbackFailed &&
			readText(assets / "moon" / "payload.txt") == "old moon",
			"rollback resume: later target restored before earlier failure") && ok;
	writeResource(
		workspace / "previous" / "package-0",
		"JXQY2",
		"old dependency");
	const auto secondRecovery =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	ok = expect(secondRecovery.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::Success &&
			secondRecovery.rolledBack &&
			readText(assets / "dependency" / "payload.txt") ==
				"old dependency" &&
			readText(assets / "moon" / "payload.txt") == "old moon",
			"rollback resume: next startup completes remaining rollback") && ok;
	return ok;
}

bool testAwaitingValidationResumesWithoutSecondSwitch()
{
	TemporaryTree tree("jxqy-resource-install-resume");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "moon", "YYCS", "old") &&
		prepareWorkspace(assets, {{"YYCS", "new"}});
	if (!ok)
	{
		return false;
	}
	OnlineUpdate::stageResourceInstallTransaction(
		assets, "YYCS", {{"YYCS", "moon"}});
	const auto first = OnlineUpdate::beginResourceInstallTransaction(assets);
	const auto resumed = OnlineUpdate::beginResourceInstallTransaction(assets);
	ok = expect(first.needsValidation && resumed.needsValidation &&
			readText(assets / "moon" / "payload.txt") == "new",
			"validation resume: startup observes already switched group") && ok;
	const auto committed =
		OnlineUpdate::completeResourceInstallTransaction(assets, true);
	ok = expect(committed.succeeded(),
		"validation resume: resumed group commits") && ok;
	return ok;
}

bool testChangedTargetAndInvalidTargetsAreRejected()
{
	TemporaryTree tree("jxqy-resource-install-conflict");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	bool ok = writeResource(assets / "moon", "YYCS", "old") &&
		prepareWorkspace(assets, {{"YYCS", "new"}});
	if (!ok)
	{
		return false;
	}
	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets, "YYCS", {{"YYCS", "moon"}});
	writeResource(assets / "moon", "OTHER", "changed");
	const auto conflict = OnlineUpdate::beginResourceInstallTransaction(assets);
	ok = expect(staged.succeeded() && conflict.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::TargetConflict &&
			conflict.rolledBack &&
			readText(assets / "moon" / "payload.txt") == "changed" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)),
			"target conflict: changed target is not replaced") && ok;

	TemporaryTree invalidTree("jxqy-resource-install-invalid");
	const std::filesystem::path invalidAssets = invalidTree.root / "assets";
	std::filesystem::create_directories(invalidAssets);
	prepareWorkspace(invalidAssets, {{"YYCS", "new"}, {"YYCS", "duplicate"}});
	const auto duplicateId = OnlineUpdate::stageResourceInstallTransaction(
		invalidAssets,
		"YYCS",
		{{"YYCS", "moon"}, {"YYCS", "other"}});
	ok = expect(duplicateId.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::InvalidInput,
			"invalid targets: duplicate Game.Id rejected") && ok;

	std::error_code cleanupError;
	std::filesystem::remove_all(
		OnlineUpdate::resourceUpdateDirectoryPath(invalidAssets), cleanupError);
	prepareWorkspace(invalidAssets, {{"YYCS", "new"}});
	const auto reserved = OnlineUpdate::stageResourceInstallTransaction(
		invalidAssets, "YYCS", {{"YYCS", "common"}});
	ok = expect(reserved.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::InvalidInput,
			"invalid targets: reserved collection directory rejected") && ok;
	return ok;
}

bool testTargetDirectoryWithIniCommentCharacter()
{
	TemporaryTree tree("jxqy-resource-install-semicolon");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	const std::string targetDirectory = "moon ; custom";
	bool ok = writeResource(assets / targetDirectory, "YYCS", "old") &&
		prepareWorkspace(assets, {{"YYCS", "new"}});
	if (!ok)
	{
		return false;
	}
	const auto staged = OnlineUpdate::stageResourceInstallTransaction(
		assets, "YYCS", {{"YYCS", targetDirectory}});
	const auto switched = OnlineUpdate::beginResourceInstallTransaction(assets);
	const auto committed =
		OnlineUpdate::completeResourceInstallTransaction(assets, true);
	ok = expect(staged.succeeded() && switched.needsValidation &&
			committed.succeeded() &&
			readText(assets / targetDirectory / "payload.txt") == "new",
			"transaction record: semicolon in exact target name round-trips") && ok;
	return ok;
}

bool testOrphanDownloadWorkspaceIsRemoved()
{
	TemporaryTree tree("jxqy-resource-install-orphan");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(assets);
	const std::filesystem::path workspace =
		OnlineUpdate::resourceUpdateWorkspacePath(assets);
	writeText(workspace / "archives" / "partial.zip", "partial");
	const auto recovered =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	return expect(
		recovered.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::NoTransaction &&
		!std::filesystem::exists(workspace) &&
		!std::filesystem::exists(
			OnlineUpdate::resourceUpdateDirectoryPath(assets)),
		"orphan download: startup removes the unrecorded workspace and empty update directory");
}

bool testEmptyUpdateDirectoryIsRemoved()
{
	TemporaryTree tree("jxqy-resource-install-empty-update");
	const std::filesystem::path assets = tree.root / "assets";
	std::filesystem::create_directories(
		OnlineUpdate::resourceUpdateDirectoryPath(assets));
	const auto recovered =
		OnlineUpdate::beginResourceInstallTransaction(assets);
	return expect(
		recovered.status ==
			OnlineUpdate::ResourceInstallTransactionStatus::NoTransaction &&
		!std::filesystem::exists(
			OnlineUpdate::resourceUpdateDirectoryPath(assets)),
		"empty update directory: startup removes it without creating a transaction");
}

}

int main()
{
	bool ok = true;
	ok = testSuccessfulGroupSwitch() && ok;
	ok = testDependencyOnlySwitch() && ok;
	ok = testValidationFailureRollsBackGroup() && ok;
	ok = testCommitRecordFailureCanRollBackGroup() && ok;
	ok = testCommonSwitchAndRollback() && ok;
	ok = testInterruptedSwitchRestoresOldGroup() && ok;
	ok = testInterruptedRollbackCanResume() && ok;
	ok = testAwaitingValidationResumesWithoutSecondSwitch() && ok;
	ok = testChangedTargetAndInvalidTargetsAreRejected() && ok;
	ok = testTargetDirectoryWithIniCommentCharacter() && ok;
	ok = testOrphanDownloadWorkspaceIsRemoved() && ok;
	ok = testEmptyUpdateDirectoryIsRemoved() && ok;
	return ok ? 0 : 1;
}
