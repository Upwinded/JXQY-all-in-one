#include "../File/File.h"
#include "../File/RootedResourceReader.h"
#include "../Resource/ResourceCatalog.h"
#include "../Resource/ResourceManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace
{
bool check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << "\n";
	}
	return condition;
}

bool writeText(const fs::path& path, const std::string& text)
{
	std::error_code error;
	fs::create_directories(path.parent_path(), error);
	if (error)
	{
		return false;
	}
	std::ofstream output(path, std::ios::binary);
	output.write(
		text.data(),
		static_cast<std::streamsize>(text.size()));
	return output.good();
}

std::string readVirtualText(const std::string& virtualPath)
{
	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readFile(virtualPath, data, length) ||
		data == nullptr ||
		length < 0)
	{
		return {};
	}
	return std::string(
		data.get(), static_cast<std::size_t>(length));
}

std::string normalizedRoot(const fs::path& root)
{
	std::string result =
		fs::canonical(root).generic_u8string();
	if (!result.empty() && result.back() != '/')
	{
		result.push_back('/');
	}
	return result;
}

File::EditorRunFileLayout makeLayout(const fs::path& sessionRoot)
{
	const fs::path overlay = sessionRoot / "overlay";
	const fs::path save = sessionRoot / "save";
	const fs::path applicationState =
		sessionRoot / "application-state";
	const fs::path diagnostics =
		sessionRoot / "diagnostics";
	fs::create_directories(overlay);
	fs::create_directories(save);
	fs::create_directories(applicationState);
	fs::create_directories(diagnostics);

	File::EditorRunFileLayout layout;
	layout.overlayRoot = overlay.generic_u8string();
	layout.isolatedSaveRoot = save.generic_u8string();
	layout.applicationStateRoot =
		applicationState.generic_u8string();
	layout.diagnosticsRoot =
		diagnostics.generic_u8string();
	layout.diagnosticsPath =
		(diagnostics / "events.jsonl").generic_u8string();
	layout.logPath =
		(diagnostics / "game.log").generic_u8string();
	layout.runtimeTracePath =
		(diagnostics / "runtime-trace.jsonl").
			generic_u8string();
	return layout;
}

File::EditorRunFileLayoutIdentityProof makeIdentityProof(
	const File::EditorRunFileLayout& layout)
{
	File::EditorRunFileLayoutIdentityProof proof;
	const std::array<std::string, 4> outputRoots =
		{
			layout.overlayRoot,
			layout.isolatedSaveRoot,
			layout.applicationStateRoot,
			layout.diagnosticsRoot
		};
	for (std::size_t index = 0;
		index < outputRoots.size(); ++index)
	{
		const RootedResourceReader::RootAnchorResult opened =
			RootedResourceReader::openRootAnchor(
				fs::u8path(outputRoots[index]));
		if (opened.succeeded())
		{
			proof.outputRoots[index] =
				opened.anchor.identity();
		}
	}
	return proof;
}

bool createFixture(const fs::path& collection)
{
	return writeText(
			collection / "resources.ini",
			"[Pack.MOD]\nId=MOD\nPath=mod\n"
			"[Pack.BASE]\nId=BASE\nPath=base\n"
			"[Pack.UIBASE]\nId=UIBASE\nPath=ui-base\n"
			"[Pack.UILEAF]\nId=UILEAF\nPath=ui-leaf\n") &&
		writeText(
			collection / "mod/game_profile.ini",
			"[Game]\nId=MOD\nName=Editor Mod\n"
			"[Resource]\nDependencyId=BASE\n"
			"[UI]\nBaseId=UIBASE\nPreferLocal=false\n"
			"[Save]\nNamespace=editor-run-save\n") &&
		writeText(
			collection / "base/game_profile.ini",
			"[Game]\nId=BASE\nName=Content Base\nType=2\n") &&
		writeText(
			collection / "ui-base/game_profile.ini",
			"[Game]\nId=UIBASE\nName=UI Base\nType=0\n"
			"[Resource]\nDependencyId=UILEAF\n"
			"[UI]\nProfile=yycs\n") &&
		writeText(
			collection / "ui-leaf/game_profile.ini",
			"[Game]\nId=UILEAF\nName=UI Leaf\nType=1\n") &&
		writeText(
			collection / "base/map/content.map",
			"content-base") &&
		writeText(
			collection / "common/map/common.map",
			"collection-common") &&
		writeText(
			collection / "mod/ini/ui/theme.ini",
			"active-ui") &&
		writeText(
			collection / "ui-base/ini/ui/theme.ini",
			"ui-base-first");
}

bool removeCatalogInputs(const fs::path& collection)
{
	std::error_code error;
	for (const fs::path& path : {
			collection / "resources.ini",
			collection / "mod/game_profile.ini",
			collection / "base/game_profile.ini",
			collection / "ui-base/game_profile.ini",
			collection / "ui-leaf/game_profile.ini" })
	{
		fs::rename(path, path.string() + ".removed", error);
		if (error)
		{
			return false;
		}
	}
	return true;
}

bool runTest(const fs::path& root)
{
	const fs::path collection = root / "assets";
	bool ok = check(
		createFixture(collection),
		"create resource-routing fixture");

	RuntimeResource::ExactSelectionResult result =
		RuntimeResource::resolveExactResourceSelection(
			collection, "mod");
	ok = check(
		result.succeeded(),
		"resolve exact editor-run selection") && ok;
	if (!result.succeeded())
	{
		return false;
	}

	RuntimeResource::ExactResourceSelection selection =
		std::move(result.selection);
	result = {};
	ok = check(
		selection.activeManifest.typeDefined &&
			selection.activeManifest.type == 2 &&
			selection.activeManifest.uiProfile == "YYCS",
		"materialize Game.Type and UI.Profile") && ok;
	ok = check(
		selection.commonResourceRoot ==
			fs::canonical(collection / "common"),
		"materialize collection Common root") && ok;
	ok = check(
		selection.orderedContentRoots.size() == 3 &&
			selection.orderedContentRoots[0].kind ==
				RuntimeResource::ContentRootKind::Active &&
			selection.orderedContentRoots[0].root ==
				fs::canonical(collection / "mod") &&
			selection.orderedContentRoots[1].kind ==
				RuntimeResource::ContentRootKind::DependencyId &&
			selection.orderedContentRoots[1].root ==
				fs::canonical(collection / "base") &&
			selection.orderedContentRoots[2].kind ==
				RuntimeResource::ContentRootKind::Common &&
			selection.orderedContentRoots[2].root ==
				fs::canonical(collection / "common"),
		"preserve active, content dependency, Common order") &&
		ok;
	ok = check(
		selection.orderedUiFallbackRoots.size() == 2 &&
			selection.orderedUiFallbackRoots[0] ==
				fs::canonical(collection / "ui-base") &&
			selection.orderedUiFallbackRoots[1] ==
				fs::canonical(collection / "ui-leaf") &&
			!selection.preferLocalUi,
		"preserve independent UI graph and PreferLocal") && ok;
	ok = check(
		selection.effectiveSaveNamespace ==
			"editor-run-save",
		"materialize effective Save.Namespace") && ok;
	RuntimeResource::ExactSelectionResult defaultNamespace =
		RuntimeResource::resolveExactResourceSelection(
			collection, "BASE");
	ok = check(
		defaultNamespace.succeeded() &&
			defaultNamespace.selection.effectiveSaveNamespace ==
				"BASE",
		"fall back from empty Save.Namespace to canonical Game.Id") &&
		ok;
	defaultNamespace = {};

	ResourceManager& manager = ResourceManager::instance();
	const File::EditorRunFileLayout layout =
		makeLayout(root / "session");
	ok = check(
		File::installEditorRunFileLayoutForTests(layout),
		"install File layout gate fixture") && ok;
	ok = check(
		!manager.installEditorRunSelection(selection) &&
			!manager.hasActiveResourceRoot(),
		"reject resource installation after File layout") && ok;
	File::resetEditorRunFileLayout();

	ok = check(
		removeCatalogInputs(collection),
		"remove catalog inputs after routing preparation") && ok;
	ok = check(
		manager.installEditorRunSelection(selection),
		"install prepared routing without rescanning") && ok;
	ok = check(
		manager.getDiscoveredPacks().empty() &&
			manager.getActiveManifest().type == 2 &&
			manager.getActiveManifest().uiProfile == "YYCS" &&
			manager.getActiveResourceRoot() ==
				normalizedRoot(collection / "mod"),
		"consume materialized manager state without discovery") && ok;
	ok = check(
		File::getAssetsCollectionRoot() ==
				normalizedRoot(collection) &&
			File::getActiveResourceRoot() ==
				normalizedRoot(collection / "mod") &&
			File::getActiveSaveNamespace() ==
				"editor-run-save",
		"install collection, active and save routes") && ok;

	const File::EditorRunFileLayoutIdentityProof proof =
		makeIdentityProof(layout);
	ok = check(
		File::installEditorRunFileLayout(
			layout, proof),
		"strict File handoff accepts matching private output generations") &&
		ok;
	File::resetEditorRunFileLayout();
	const fs::path saveRoot =
		fs::u8path(layout.isolatedSaveRoot);
	const fs::path originalSaveRoot =
		saveRoot.parent_path() / "save-original";
	bool replacedSaveRoot = false;
	File::setEditorRunFileOperationTestHook(
		[&](File::EditorRunFileOperationPhase phase)
		{
			if (phase !=
					File::EditorRunFileOperationPhase::
						AfterLayoutOverlayIdentityCapture ||
				replacedSaveRoot)
			{
				return;
			}
			std::error_code renameError;
			fs::rename(
				saveRoot,
				originalSaveRoot,
				renameError);
			std::error_code createError;
			replacedSaveRoot =
				!renameError &&
				fs::create_directory(
					saveRoot, createError) &&
				!createError;
		});
	const bool replacementInstalled =
		File::installEditorRunFileLayout(
			layout, proof);
	File::setEditorRunFileOperationTestHook({});
	ok = check(
		replacedSaveRoot &&
			!replacementInstalled &&
			!File::hasEditorRunFileLayout(),
		"strict File handoff rejects an output directory replaced after the first identity capture") &&
		ok;
	if (replacedSaveRoot)
	{
		std::error_code removeError;
		fs::remove(saveRoot, removeError);
		std::error_code restoreError;
		fs::rename(
			originalSaveRoot,
			saveRoot,
			restoreError);
		ok = check(
			!removeError && !restoreError,
			"restore output identity after strict handoff race fixture") &&
			ok;
	}

	ok = check(
		File::installEditorRunFileLayout(
			layout, proof),
		"install frozen resource paths with private output identity proofs") &&
		ok;
	ok = check(
		readVirtualText("map/content.map") ==
				"content-base" &&
			readVirtualText("map/common.map").empty(),
		"ordinary content uses declared dependencies without Common fallback") && ok;
	ok = check(
		readVirtualText("ini/ui/theme.ini") ==
			"ui-base-first",
		"PreferLocal=false consumes UI base before active UI") && ok;

	const fs::path baseRoot = collection / "base";
	const fs::path originalBaseRoot =
		collection / "base-original";
	std::error_code baseRenameError;
	fs::rename(
		baseRoot,
		originalBaseRoot,
		baseRenameError);
	bool replacementBaseCreated = false;
	if (!baseRenameError)
	{
		replacementBaseCreated =
			writeText(
				baseRoot / "map/content.map",
				"content-replacement");
	}
	std::string installedLogPath;
	ok = check(
		!baseRenameError &&
			replacementBaseCreated,
		"replace the formal dependency root with a new directory generation") &&
		ok;
	ok = check(
		File::getEditorRunLogPath(
			installedLogPath) ==
				File::EditorRunFileLayoutState::Valid &&
			!installedLogPath.empty(),
		"formal root replacement keeps the private layout valid") &&
		ok;
	ok = check(
		readVirtualText("map/content.map") ==
			"content-replacement",
		"formal reads use the replacement root's current content") &&
		ok;
	if (!baseRenameError)
	{
		std::error_code removeBaseError;
		fs::remove_all(baseRoot, removeBaseError);
		std::error_code restoreBaseError;
		fs::rename(
			originalBaseRoot,
			baseRoot,
			restoreBaseError);
		ok = check(
			!removeBaseError && !restoreBaseError,
			"restore formal root after replacement fixture") &&
			ok;
	}

	const std::string activeBeforeInitialize =
		manager.getActiveResourceRoot();
	ok = check(
		manager.initialize(
			(root / "missing-rescan-root").generic_u8string()) &&
			manager.getActiveResourceRoot() ==
				activeBeforeInitialize &&
			manager.getDiscoveredPacks().empty(),
		"initialize consumes installed state without rescanning") &&
		ok;
	File::resetEditorRunFileLayout();
	return ok;
}
}

int main()
{
#if defined(__MOBILE__)
	std::cout
		<< "SKIP: editor-run private host layout is unavailable in the mobile surrogate\n";
	return 0;
#else
	const auto unique =
		std::chrono::steady_clock::now()
			.time_since_epoch()
			.count();
	const fs::path root =
		fs::temp_directory_path() /
		("jxqy-editor-run-resource-install-" +
			std::to_string(unique));
	std::error_code error;
	fs::create_directories(root, error);
	if (error)
	{
		std::cerr << "FAILED: create temporary test root\n";
		return 1;
	}

	const bool ok = runTest(root);
	File::resetEditorRunFileLayout();
	fs::remove_all(root, error);
	return ok ? 0 : 1;
#endif
}
