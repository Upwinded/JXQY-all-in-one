#include "../File/File.h"
#include "../JxqyEngineVersion.h"
#include "../Resource/ResourceManager.h"
#include "../Update/ResourceInstallTransaction.h"
#include "TestTemporaryDirectory.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

class ResourceManagerPolicyTestAccess
{
public:
	static void reset(ResourceManager& manager)
	{
		manager.discoveredPacks.clear();
		manager.resourceCatalogDiagnostics.clear();
		manager.currentCatalogRequest = {};
		manager.currentCatalogRequestValid = false;
		manager.assetsCollectionRoot.clear();
		manager.writableResourceCollectionRoot.clear();
		manager.commonResourceRoot.clear();
		manager.writableCommonResourceRoot.clear();
		manager.resourceCatalogUrl.clear();
		manager.applicationCatalogUrl.clear();
		manager.activeResourceRoot.clear();
		manager.activeResourceEntryKey.clear();
		manager.activeManifest = ResourceManifest();
		manager.activeResourceSelectionValid = false;
		manager.initialized = false;
		File::setAssetsCollectionRoot("");
		File::setActiveResourceRoot("");
		File::setCommonResourceRoot("");
		File::setCommonResourceFallbackRoots({});
		File::setResourceFallbackRoots({});
		File::setUiResourceFallbackRoots({}, true, "");
		File::setActiveSaveNamespace("");
	}
};

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
	const std::string& manifest,
	const std::string& payload)
{
	return writeText(root / "game_profile.ini", manifest) &&
		writeText(root / "payload.txt", payload);
}

std::string validManifest(const std::string& id)
{
	return "[Game]\nId=" + id + "\nName=" + id +
		"\nVersion=1.0\nType=0\n"
		"[Release]\nMinimumEngineVersion=" +
		JxqyBuildVersion::EngineVersion + "\n";
}

std::filesystem::path preparedRoot(
	const std::filesystem::path& assets,
	std::size_t index)
{
	return OnlineUpdate::resourceUpdateWorkspacePath(assets) /
		"prepared" / ("package-" + std::to_string(index));
}

bool prepareSingleUpdate(
	const std::filesystem::path& assets,
	const std::string& preparedManifest)
{
	if (!std::filesystem::create_directories(
			OnlineUpdate::resourceUpdateDirectoryPath(assets)) ||
		!std::filesystem::create_directories(
			OnlineUpdate::resourceUpdateWorkspacePath(assets) / "prepared") ||
		!writeResource(
			preparedRoot(assets, 0),
			preparedManifest,
			"new"))
	{
		return false;
	}
	return OnlineUpdate::stageResourceInstallTransaction(
		assets, "YYCS", {{"YYCS", "moon"}}).succeeded();
}

bool prepareCommonUpdate(const std::filesystem::path& assets)
{
	if (!std::filesystem::create_directories(
			OnlineUpdate::resourceUpdateDirectoryPath(assets)) ||
		!std::filesystem::create_directories(
			OnlineUpdate::resourceUpdateWorkspacePath(assets) / "prepared") ||
		!writeText(preparedRoot(assets, 0) / "version.ini",
			"[Common]\nVersion=1.1.0\n") ||
		!writeText(preparedRoot(assets, 0) / "payload.txt", "new"))
	{
		return false;
	}
	return OnlineUpdate::stageResourceInstallTransaction(
		assets, "common", {{"common", "common"}}).succeeded();
}

bool expect(bool condition, const char* description)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << description << '\n';
	}
	return condition;
}

bool testValidGroupCommitsAfterRuntimeScan()
{
	TemporaryTree tree("jxqy-resource-manager-install-success");
	const std::filesystem::path assets = tree.root / "assets";
	bool ok = writeText(
		assets / "resources.ini", "[Collection]\nCommonPath=common\n") &&
		writeResource(assets / "moon", validManifest("YYCS"), "old") &&
		prepareSingleUpdate(assets, validManifest("YYCS"));
	if (!ok)
	{
		return false;
	}
	ResourceManager& manager = ResourceManager::instance();
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = expect(manager.initialize(assets.generic_u8string()),
		"runtime install: manager initializes") && ok;
#if !defined(__ANDROID__) && !defined(__APPLE__)
	ok = expect(
		std::filesystem::equivalent(
			std::filesystem::u8path(
				manager.getWritableResourceCollectionRoot()),
			assets),
		"desktop runtime: writable and bundled assets use the same directory") && ok;
#endif
	ok = expect(readText(assets / "moon" / "payload.txt") == "new" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)),
			"runtime install: validated resource commits and cleans old version") && ok;
	ok = expect(manager.getDiscoveredPacks().size() == 1 &&
			manager.getDiscoveredPacks().front().manifest.id == "YYCS",
			"runtime install: committed resource remains discoverable") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}

bool testSelectionSceneActivatesAndRescansWithoutRestart()
{
	TemporaryTree tree("jxqy-resource-manager-live-install");
	const std::filesystem::path assets = tree.root / "assets";
	bool ok = writeText(
		assets / "resources.ini", "[Collection]\nCommonPath=common\n") &&
		writeResource(assets / "moon", validManifest("YYCS"), "old");
	if (!ok)
	{
		return false;
	}
	ResourceManager& manager = ResourceManager::instance();
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = expect(manager.initialize(assets.generic_u8string()) &&
			manager.needsSelection() && !manager.hasActiveResourceRoot(),
		"live install: ordinary startup remains on explicit resource selection")
		&& ok;
	ok = expect(prepareSingleUpdate(assets, validManifest("YYCS")),
		"live install: validated download is staged while selection is open")
		&& ok;
	std::string errorText;
	ok = expect(manager.activateStagedResourceInstall(errorText) &&
			errorText.empty(),
		"live install: staged group activates without restarting the process")
		&& ok;
	ok = expect(readText(assets / "moon" / "payload.txt") == "new" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)) &&
			manager.getDiscoveredPacks().size() == 1 &&
			manager.getDiscoveredPacks().front().manifest.id == "YYCS" &&
			manager.needsSelection() && !manager.hasActiveResourceRoot(),
		"live install: refreshed package is visible but is not automatically"
		" selected or entered") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}

bool testInvalidActivatedGroupRollsBackAndRescans()
{
	TemporaryTree tree("jxqy-resource-manager-install-rollback");
	const std::filesystem::path assets = tree.root / "assets";
	bool ok = writeText(
		assets / "resources.ini", "[Collection]\nCommonPath=common\n") &&
		writeResource(assets / "moon", validManifest("YYCS"), "old") &&
		prepareSingleUpdate(assets, validManifest("YYCS"));
	if (!ok)
	{
		return false;
	}
	// The transaction identity check intentionally reads only Game.Id. This
	// malformed line makes the shared runtime catalog reject the activated
	// resource, which exercises the post-switch validation and rollback path.
	writeText(
		preparedRoot(assets, 0) / "game_profile.ini",
		"[Game]\nId=YYCS\ninvalid line\n");
	ResourceManager& manager = ResourceManager::instance();
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = expect(manager.initialize(assets.generic_u8string()),
		"runtime rollback: manager remains available") && ok;
	ok = expect(readText(assets / "moon" / "payload.txt") == "old" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)),
			"runtime rollback: invalid activated resource restores old content") && ok;
	ok = expect(manager.getDiscoveredPacks().size() == 1 &&
			manager.getDiscoveredPacks().front().manifest.id == "YYCS",
			"runtime rollback: old resource group is rescanned") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}

bool testOpenImportCanCommitAnUnplayableResource()
{
	TemporaryTree tree("jxqy-resource-manager-open-import");
	const std::filesystem::path assets = tree.root / "assets";
	const std::string unplayableManifest =
		"[Game]\nId=YYCS\nName=Open Import\nVersion=2.0\nType=0\n"
		"[Release]\nMinimumEngineVersion=999.0.0\n"
		"[Resource]\nDependencyId=MISSING_DEPENDENCY\n";
	bool ok = writeText(
		assets / "resources.ini", "[Collection]\nCommonPath=common\n") &&
		writeResource(assets / "moon", validManifest("YYCS"), "old");
	if (!ok)
	{
		return false;
	}
	ResourceManager& manager = ResourceManager::instance();
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = expect(manager.initialize(assets.generic_u8string()),
		"open import: manager initializes") && ok;
	ok = expect(prepareSingleUpdate(assets, unplayableManifest),
		"open import: semantically unplayable resource is staged") && ok;
	std::string errorText;
	ok = expect(manager.activateStagedResourceInstall(errorText, true) &&
			errorText.empty(),
		"open import: explicit permissive activation commits structural package")
		&& ok;
	ok = expect(readText(assets / "moon" / "payload.txt") == "new" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)) &&
			manager.getDiscoveredPacks().size() == 1 &&
			manager.getDiscoveredPacks().front().compatibility.status ==
				ModRelease::CompatibilityStatus::RequiresNewerEngine,
		"open import: incompatible package remains installed and diagnosable")
		&& ok;
	std::string blockingReason;
	ok = expect(!manager.canActivateResourcePack(0, &blockingReason) &&
			!blockingReason.empty(),
		"open import: confirmed incompatibility remains blocked from launch") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}

bool testCommonCommitsAfterRuntimeValidation()
{
	TemporaryTree tree("jxqy-resource-manager-common-success");
	const std::filesystem::path assets = tree.root / "assets";
	bool ok = writeText(
		assets / "resources.ini", "[Collection]\nCommonPath=common\n") &&
		writeText(assets / "common/version.ini",
			"[Common]\nVersion=1.0.0\n") &&
		writeText(assets / "common/payload.txt", "old") &&
		writeResource(assets / "moon", validManifest("YYCS"), "resource") &&
		prepareCommonUpdate(assets);
	if (!ok)
	{
		return false;
	}
	ResourceManager& manager = ResourceManager::instance();
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = expect(manager.initialize(assets.generic_u8string()),
		"runtime common install: manager initializes") && ok;
	ok = expect(readText(assets / "common/payload.txt") == "new" &&
			!std::filesystem::exists(
				OnlineUpdate::resourceUpdateWorkspacePath(assets)),
			"runtime common install: startup validation commits assets/common") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}

bool testResourceConfigurationErrorsRemainVisible()
{
	TemporaryTree tree("jxqy-resource-manager-diagnostics");
	const std::filesystem::path assets = tree.root / "assets";
	bool ok = writeText(
		assets / "resources.ini", "[Collection]\nCommonPath=common\n") &&
		writeResource(assets / "first", validManifest("DUPLICATE"), "one") &&
		writeResource(assets / "second", validManifest("duplicate"), "two") &&
		writeText(
			assets / "broken/game_profile.ini",
			"[Game]\nName=Missing Id\n");
	if (!ok)
	{
		return false;
	}

	ResourceManager& manager = ResourceManager::instance();
	ResourceManagerPolicyTestAccess::reset(manager);
	ok = expect(manager.initialize(assets.generic_u8string()),
		"resource diagnostics: manager remains available") && ok;
	const auto& diagnostics = manager.getResourceCatalogDiagnostics();
	const auto hasCode = [&diagnostics](const std::string& code)
	{
		return std::any_of(
			diagnostics.begin(), diagnostics.end(),
			[&code](const RuntimeResource::CatalogDiagnostic& diagnostic)
			{
				return diagnostic.code == code &&
					diagnostic.severity ==
						RuntimeResource::CatalogDiagnosticSeverity::Error;
			});
	};
	ok = expect(
		hasCode("resource.catalog.discovered_manifest_invalid") &&
			hasCode("resource.catalog.duplicate_game_id"),
		"resource diagnostics: invalid manifests and duplicate IDs remain available to the selection UI") && ok;
	ResourceManagerPolicyTestAccess::reset(manager);
	return ok;
}
}

int main()
{
	bool ok = true;
	ok = testValidGroupCommitsAfterRuntimeScan() && ok;
	ok = testSelectionSceneActivatesAndRescansWithoutRestart() && ok;
	ok = testInvalidActivatedGroupRollsBackAndRescans() && ok;
	ok = testOpenImportCanCommitAnUnplayableResource() && ok;
	ok = testCommonCommitsAfterRuntimeValidation() && ok;
	ok = testResourceConfigurationErrorsRemainVisible() && ok;
	return ok ? 0 : 1;
}
